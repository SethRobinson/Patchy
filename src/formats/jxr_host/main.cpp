// patchy-jxr-host: decodes and encodes JPEG XR for Patchy on Linux through the
// distribution's own jxrlib (docs/jxr.md, Linux). Patchy ships no JPEG XR codec: this
// program is compiled against the distribution's jxrlib headers, links nothing from it, and
// loads libjxrglue.so.0 (which pulls in libjpegxr.so.0) with dlopen at run time, so neither
// the AppImage nor sharun ever picks the library up as a dependency. It runs as a separate
// process because jxrlib is known to crash on malformed files (GitHub issue 269): a crash
// ends this helper, and Patchy reports it instead of going down with it.
//
// Protocol (src/formats/jxr_host_protocol.hpp): one request on stdin, one reply on stdout,
// then exit. The real stdout is kept on a private descriptor and fd 1 points at stderr,
// so anything the library prints can never corrupt a reply.

#include "formats/jxr_host_protocol.hpp"

#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// jxrlib's headers define min/max as macros and need __ANSI__ for the non-Windows build
// (CMake passes it); they come last and the macros are dropped right after.
#include <JXRGlue.h>
#undef min
#undef max

namespace {

using namespace patchy::jxr::host;

// ---- the library, resolved by name ----

struct Library {
  void* handle{nullptr};
  std::string error;
  ERR (*decode_create)(PKImageDecode**){nullptr};
  ERR (*encode_create)(PKImageEncode**){nullptr};
  ERR (*stream_memory)(WMPStream**, void*, size_t){nullptr};
  ERR (*stream_file)(WMPStream**, const char*, const char*){nullptr};
  ERR (*pixel_lookup)(PKPixelInfo*, U8){nullptr};
  const PKPixelFormatGUID* rgb24{nullptr};
  const PKPixelFormatGUID* rgba32{nullptr};
};

template <typename Target>
bool resolve(void* handle, const char* name, Target& target) {
  target = reinterpret_cast<Target>(dlsym(handle, name));
  return target != nullptr;
}

Library load_library() {
  Library library;
  const char* override_name = std::getenv("PATCHY_JXR_LIBRARY");
  const std::string name = (override_name != nullptr && *override_name != '\0') ? override_name : "libjxrglue.so.0";
  // RTLD_LOCAL: jxrlib's symbols stay out of the global namespace. dlsym on the handle
  // also searches its dependency, libjpegxr.so.0, where the stream functions live.
  library.handle = dlopen(name.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (library.handle == nullptr) {
    const char* reason = dlerror();
    library.error = reason != nullptr ? reason : "dlopen failed";
    return library;
  }
  const bool complete = resolve(library.handle, "PKImageDecode_Create_WMP", library.decode_create) &&
                        resolve(library.handle, "PKImageEncode_Create_WMP", library.encode_create) &&
                        resolve(library.handle, "CreateWS_Memory", library.stream_memory) &&
                        resolve(library.handle, "CreateWS_File", library.stream_file) &&
                        resolve(library.handle, "PixelFormatLookup", library.pixel_lookup) &&
                        resolve(library.handle, "GUID_PKPixelFormat24bppRGB", library.rgb24) &&
                        resolve(library.handle, "GUID_PKPixelFormat32bppRGBA", library.rgba32);
  if (!complete) {
    library.error = "the jxrlib library lacks a required function";
  }
  return library;
}

// ---- reply helpers ----

int g_reply_fd = -1;

void send_reply(Status status, const std::string& detail, const Writer& payload) {
  Writer reply;
  reply.u32(kMagic);
  reply.u32(static_cast<std::uint32_t>(status));
  reply.text(detail);
  reply.append(payload.bytes());
  (void)write_message(g_reply_fd, reply.bytes());
}

void send_error(Status status, const std::string& detail) {
  send_reply(status, detail, Writer{});
}

std::string err_text(ERR err) {
  return "jxrlib error " + std::to_string(static_cast<long>(err));
}

// ---- pixel conversion ----

float half_to_float(std::uint16_t half) {
  const std::uint32_t sign = (half & 0x8000U) << 16;
  std::uint32_t exponent = (half >> 10) & 0x1FU;
  std::uint32_t mantissa = half & 0x3FFU;
  std::uint32_t bits = 0;
  if (exponent == 0) {
    if (mantissa == 0) {
      bits = sign;
    } else {
      exponent = 127 - 15 + 1;
      while ((mantissa & 0x400U) == 0) {
        mantissa <<= 1;
        --exponent;
      }
      mantissa &= 0x3FFU;
      bits = sign | (exponent << 23) | (mantissa << 13);
    }
  } else if (exponent == 0x1F) {
    bits = sign | 0x7F800000U | (mantissa << 13);
  } else {
    bits = sign | ((exponent + 127 - 15) << 23) | (mantissa << 13);
  }
  float value = 0.0F;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

template <typename T>
T load(const std::uint8_t* at) {
  T value{};
  std::memcpy(&value, at, sizeof(T));
  return value;
}

// One sample of a native pixel as a float (the HDR path) or 0..255 (the integer path).
struct SampleReader {
  BITDEPTH_BITS depth;
  std::size_t bytes;  // per sample
  float to_float(const std::uint8_t* at) const {
    switch (depth) {
      case BD_8: return static_cast<float>(*at) / 255.0F;
      case BD_16: return static_cast<float>(load<std::uint16_t>(at)) / 65535.0F;
      case BD_16S: return static_cast<float>(load<std::int16_t>(at)) / 8192.0F;       // s2.13 fixed point
      case BD_16F: return half_to_float(load<std::uint16_t>(at));
      case BD_32S: return static_cast<float>(load<std::int32_t>(at)) / 16777216.0F;  // s7.24 fixed point
      case BD_32F: return load<float>(at);
      default: return 0.0F;
    }
  }
  std::uint8_t to_byte(const std::uint8_t* at) const {
    if (depth == BD_8) {
      return *at;
    }
    // 16-bit unsigned: the high byte, rounded.
    const auto value = static_cast<std::uint32_t>(load<std::uint16_t>(at));
    return static_cast<std::uint8_t>(std::min<std::uint32_t>(255U, (value + 128U) / 257U));
  }
};

// ---- decode ----

void decode(const Library& library, std::vector<std::uint8_t> file) {
  WMPStream* stream = nullptr;
  if (ERR err = library.stream_memory(&stream, file.data(), file.size()); Failed(err) || stream == nullptr) {
    send_error(Status::DecodeFailed, err_text(err));
    return;
  }
  PKImageDecode* decoder = nullptr;
  ERR err = library.decode_create(&decoder);
  if (!Failed(err)) {
    err = decoder->Initialize(decoder, stream);
  }
  if (Failed(err)) {
    send_error(Status::DecodeFailed, err_text(err));
    return;  // the process exits right after: nothing to free
  }

  U32 frames = 1;
  if (Failed(decoder->GetFrameCount(decoder, &frames)) || frames == 0) {
    frames = 1;
  }
  PKPixelFormatGUID guid{};
  I32 width = 0;
  I32 height = 0;
  Float res_x = 0.0F;
  Float res_y = 0.0F;
  if (Failed(err = decoder->GetPixelFormat(decoder, &guid)) || Failed(err = decoder->GetSize(decoder, &width, &height))) {
    send_error(Status::DecodeFailed, err_text(err));
    return;
  }
  (void)decoder->GetResolution(decoder, &res_x, &res_y);
  if (width <= 0 || height <= 0 ||
      static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) > kMaximumPixels) {
    send_error(Status::BadDimensions, std::to_string(width) + "x" + std::to_string(height));
    return;
  }

  PKPixelInfo info{};
  info.pGUIDPixFmt = &guid;
  if (Failed(err = library.pixel_lookup(&info, LOOKUP_FORWARD))) {
    send_error(Status::UnsupportedFormat, err_text(err));
    return;
  }
  const bool gray = info.cfColorFormat == Y_ONLY;
  const bool rgb = info.cfColorFormat == CF_RGB;
  std::size_t sample_bytes = 0;
  switch (info.bdBitDepth) {
    case BD_8: sample_bytes = 1; break;
    case BD_16: case BD_16S: case BD_16F: sample_bytes = 2; break;
    case BD_32S: case BD_32F: sample_bytes = 4; break;
    default: break;
  }
  const std::size_t unit_bits = info.cbitUnit;
  if ((!gray && !rgb) || sample_bytes == 0 || unit_bits == 0 || unit_bits % (8U * sample_bytes) != 0) {
    // CMYK, n-channel, RGBE, packed 5/10/565-bit and 1-bit formats.
    send_error(Status::UnsupportedFormat, "pixel format with " + std::to_string(unit_bits) + " bits per pixel");
    return;
  }
  const std::size_t samples_per_pixel = unit_bits / (8U * sample_bytes);
  const bool has_alpha = (info.grBit & PK_pixfmtHasAlpha) != 0;
  const bool premultiplied = (info.grBit & PK_pixfmtPreMul) != 0;
  const bool bgr = (info.grBit & PK_pixfmtBGR) != 0;
  const std::size_t color_samples = gray ? 1U : 3U;
  if (samples_per_pixel < color_samples + (has_alpha ? 1U : 0U)) {
    send_error(Status::UnsupportedFormat, "inconsistent pixel layout");
    return;
  }
  const bool is_float = info.bdBitDepth == BD_16F || info.bdBitDepth == BD_32F || info.bdBitDepth == BD_16S ||
                        info.bdBitDepth == BD_32S;
  // The decoder leaves a planar alpha channel undecoded (alpha reads back as 0) unless
  // asked, as jxrlib's own JxrDecApp does: 2 decodes the image and its alpha plane.
  if (has_alpha) {
    decoder->WMP.wmiSCP.uAlphaMode = 2;
  }

  // jxrlib writes whole macroblocks in places: pad the buffer by a macroblock row and a
  // macroblock column so a codec over-write lands in owned memory.
  const std::size_t stride = (static_cast<std::size_t>(width) + 16U) * (unit_bits / 8U);
  std::vector<std::uint8_t> native(stride * (static_cast<std::size_t>(height) + 16U));
  PKRect rect{0, 0, width, height};
  if (Failed(err = decoder->Copy(decoder, &rect, native.data(), static_cast<U32>(stride)))) {
    send_error(Status::DecodeFailed, err_text(err));
    return;
  }

  const SampleReader reader{info.bdBitDepth, sample_bytes};
  const auto pixel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  Writer payload;
  payload.i32(width);
  payload.i32(height);
  payload.f64(res_x);
  payload.f64(res_y);
  payload.u32(frames);
  payload.u8(is_float ? 1 : 0);
  payload.u32(static_cast<std::uint32_t>(sample_bytes * 8U));
  if (is_float) {
    std::vector<float> out(pixel_count * 4U);
    for (I32 y = 0; y < height; ++y) {
      const std::uint8_t* row = native.data() + static_cast<std::size_t>(y) * stride;
      for (I32 x = 0; x < width; ++x) {
        const std::uint8_t* px = row + static_cast<std::size_t>(x) * samples_per_pixel * sample_bytes;
        float* dst = out.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + x) * 4U;
        if (gray) {
          dst[0] = dst[1] = dst[2] = reader.to_float(px);
        } else {
          for (std::size_t c = 0; c < 3U; ++c) {
            dst[bgr ? 2U - c : c] = reader.to_float(px + c * sample_bytes);
          }
        }
        dst[3] = has_alpha ? reader.to_float(px + color_samples * sample_bytes) : 1.0F;
        if (premultiplied && dst[3] > 0.0F) {
          for (std::size_t c = 0; c < 3U; ++c) {
            dst[c] /= dst[3];
          }
        }
      }
    }
    payload.blob(reinterpret_cast<const std::uint8_t*>(out.data()), out.size() * sizeof(float));
  } else {
    std::vector<std::uint8_t> out(pixel_count * 4U);
    for (I32 y = 0; y < height; ++y) {
      const std::uint8_t* row = native.data() + static_cast<std::size_t>(y) * stride;
      for (I32 x = 0; x < width; ++x) {
        const std::uint8_t* px = row + static_cast<std::size_t>(x) * samples_per_pixel * sample_bytes;
        std::uint8_t* dst = out.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + x) * 4U;
        if (gray) {
          dst[0] = dst[1] = dst[2] = reader.to_byte(px);
        } else {
          for (std::size_t c = 0; c < 3U; ++c) {
            dst[bgr ? 2U - c : c] = reader.to_byte(px + c * sample_bytes);
          }
        }
        dst[3] = has_alpha ? reader.to_byte(px + color_samples * sample_bytes) : 255U;
        if (premultiplied && dst[3] > 0U && dst[3] < 255U) {
          for (std::size_t c = 0; c < 3U; ++c) {
            dst[c] = static_cast<std::uint8_t>(std::min(255, dst[c] * 255 / dst[3]));
          }
        }
      }
    }
    payload.blob(out.data(), out.size());
  }
  send_reply(Status::Ok, std::string{}, payload);
}

// ---- encode ----

void encode(const Library& library, Reader& request) {
  const std::int32_t width = request.i32();
  const std::int32_t height = request.i32();
  const bool has_alpha = request.u8() != 0;
  const double ppi_x = request.f64();
  const double ppi_y = request.f64();
  const int quality = std::clamp(request.i32(), 1, 100);
  const bool lossless = request.u8() != 0;
  std::vector<std::uint8_t> rgba = request.blob();
  if (!request.ok() || width <= 0 || height <= 0 ||
      static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) > kMaximumPixels ||
      rgba.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U) {
    send_error(Status::BadRequest, "malformed encode request");
    return;
  }

  // jxrlib has no growable memory stream: encode to a private temporary file, read it back.
  const char* tmpdir = std::getenv("TMPDIR");
  std::string path = std::string(tmpdir != nullptr && *tmpdir != '\0' ? tmpdir : "/tmp") + "/patchy-jxr-XXXXXX";
  const int fd = mkstemp(path.data());
  if (fd < 0) {
    send_error(Status::EncodeFailed, "cannot create a temporary file");
    return;
  }
  close(fd);

  WMPStream* stream = nullptr;
  PKImageEncode* encoder = nullptr;
  ERR err = library.stream_file(&stream, path.c_str(), "wb");
  if (!Failed(err)) {
    err = library.encode_create(&encoder);
  }
  CWMIStrCodecParam params{};
  params.bVerbose = FALSE;
  params.cfColorFormat = YUV_444;
  params.bdBitDepth = BD_LONG;
  params.bfBitstreamFormat = SPATIAL;
  params.bProgressiveMode = TRUE;
  params.sbSubband = SB_ALL;
  params.uAlphaMode = has_alpha ? 2 : 0;  // 2: planar alpha
  params.uiDefaultQPIndex = quality_to_qp(quality, lossless);
  params.olOverlap = lossless ? OL_ONE : (quality >= 50 ? OL_ONE : OL_TWO);
  if (!Failed(err)) {
    err = encoder->Initialize(encoder, stream, &params, sizeof(params));
  }
  if (!Failed(err) && has_alpha) {
    encoder->WMP.wmiSCP_Alpha.uiDefaultQPIndex = params.uiDefaultQPIndex;
  }

  // Opaque frames go out as 24bpp RGB, the Windows writer's 24/32bpp rule.
  std::vector<std::uint8_t> pixels;
  std::size_t stride = 0;
  if (has_alpha) {
    pixels = std::move(rgba);
    stride = static_cast<std::size_t>(width) * 4U;
  } else {
    stride = static_cast<std::size_t>(width) * 3U;
    pixels.resize(stride * static_cast<std::size_t>(height));
    const auto count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    for (std::size_t i = 0; i < count; ++i) {
      std::memcpy(pixels.data() + i * 3U, rgba.data() + i * 4U, 3U);
    }
  }
  if (!Failed(err)) {
    err = encoder->SetPixelFormat(encoder, has_alpha ? *library.rgba32 : *library.rgb24);
  }
  if (!Failed(err)) {
    err = encoder->SetSize(encoder, width, height);
  }
  if (!Failed(err)) {
    // No density recorded reads back as the 96 DPI "untagged" default (jxr.md).
    const bool tagged = ppi_x > 0.0 && ppi_y > 0.0;
    err = encoder->SetResolution(encoder, static_cast<Float>(tagged ? ppi_x : 96.0),
                                 static_cast<Float>(tagged ? ppi_y : 96.0));
  }
  if (!Failed(err)) {
    err = encoder->WritePixels(encoder, static_cast<U32>(height), pixels.data(), static_cast<U32>(stride));
  }
  if (encoder != nullptr) {
    encoder->Release(&encoder);  // also closes the stream
  } else if (stream != nullptr) {
    stream->Close(&stream);
  }
  if (Failed(err)) {
    unlink(path.c_str());
    send_error(Status::EncodeFailed, err_text(err));
    return;
  }

  std::vector<std::uint8_t> encoded;
  if (FILE* file = std::fopen(path.c_str(), "rb"); file != nullptr) {
    std::uint8_t buffer[65536];
    std::size_t got = 0;
    while ((got = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
      encoded.insert(encoded.end(), buffer, buffer + got);
    }
    std::fclose(file);
  }
  unlink(path.c_str());
  if (encoded.empty()) {
    send_error(Status::EncodeFailed, "the encoder wrote no data");
    return;
  }
  Writer payload;
  payload.blob(encoded.data(), encoded.size());
  send_reply(Status::Ok, std::string{}, payload);
}

}  // namespace

int main() {
  // Keep the real stdout for replies; library chatter on fd 1 goes to stderr.
  g_reply_fd = fcntl(STDOUT_FILENO, F_DUPFD_CLOEXEC, 3);
  if (g_reply_fd < 0) {
    return 2;
  }
  dup2(STDERR_FILENO, STDOUT_FILENO);

  std::vector<std::uint8_t> request_bytes;
  if (!read_message(STDIN_FILENO, request_bytes)) {
    return 2;
  }
  Reader request(request_bytes);
  if (request.u32() != kMagic) {
    send_error(Status::BadRequest, "bad request magic");
    return 2;
  }
  const auto operation = static_cast<Operation>(request.u32());

  const Library library = load_library();
  if (!library.error.empty()) {
    send_error(Status::LibraryMissing, library.error);
    return 0;
  }
  switch (operation) {
    case Operation::Probe:
      send_error(Status::Ok, std::string{});
      return 0;
    case Operation::Decode:
      decode(library, request.blob());
      return 0;
    case Operation::Encode:
      encode(library, request);
      return 0;
  }
  send_error(Status::BadRequest, "unknown operation");
  return 2;
}
