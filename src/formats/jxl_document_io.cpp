#include "formats/jxl_document_io.hpp"

// The private symbol names must be in place before any libjxl header (cmake/patchy_jxl.cmake
// builds the codec with the same header force-included).
#include "formats/jxl_symbols.hpp"

#include "core/pixel_depth.hpp"
#include "core/worker_budget.hpp"
#include "formats/document_flatten.hpp"
#include "formats/format_file_io.hpp"
#include "formats/jxr_document_io.hpp"
#include "formats/animation_timing.hpp"
#include "render/compositor.hpp"
#include "support/string_utils.hpp"
#include "support/translate_noop.hpp"

#include <jxl/cms.h>
#include <jxl/decode.h>
#include <jxl/encode.h>
#include <jxl/thread_parallel_runner.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

namespace patchy::jxl {

namespace {

// The 256 Mpx ceiling JPEG XR and HEIF use: a corrupt or hostile header cannot ask for an
// allocation the editor could never hold.
constexpr std::uint64_t kMaxPixels = 268'435'456ULL;
// All frames of an animation together; each one becomes a full-canvas layer.
constexpr std::uint64_t kMaxAnimationPixels = 1'073'741'824ULL;
// SDR reference white for PQ and HLG content (ITU-R BT.2408). libjxl linearizes HDR so that
// 1.0 is the file's intensity target; Patchy's 32-bit documents put 1.0 at SDR white.
constexpr float kSdrWhiteNits = 203.0F;

std::atomic<std::int64_t> g_last_patches{-1};
std::atomic<std::int64_t> g_last_dots{-1};

struct DecoderDeleter {
  void operator()(JxlDecoder* decoder) const noexcept {
    JxlDecoderDestroy(decoder);
  }
};
struct EncoderDeleter {
  void operator()(JxlEncoder* encoder) const noexcept {
    JxlEncoderDestroy(encoder);
  }
};
using DecoderPtr = std::unique_ptr<JxlDecoder, DecoderDeleter>;
using EncoderPtr = std::unique_ptr<JxlEncoder, EncoderDeleter>;

// libjxl's thread pool, sized from the shared worker budget (which also keeps the wasm main
// thread from waiting on workers that cannot start). Below two workers it stays
// single-threaded.
class Runner {
public:
  Runner() {
    const int workers = max_blocking_fanout_workers(std::clamp(hardware_worker_threads(), 1, 16));
    if (workers >= 2) {
      handle_ = JxlThreadParallelRunnerCreate(nullptr, static_cast<std::size_t>(workers));
    }
  }
  ~Runner() {
    if (handle_ != nullptr) {
      JxlThreadParallelRunnerDestroy(handle_);
    }
  }
  Runner(const Runner&) = delete;
  Runner& operator=(const Runner&) = delete;
  [[nodiscard]] void* handle() const noexcept {
    return handle_;
  }

private:
  void* handle_{nullptr};
};

[[noreturn]] void fail_damaged() {
  throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "The JPEG XL file is damaged or not supported"));
}

bool is_plain_srgb(const JxlColorEncoding& encoding) {
  return encoding.color_space == JXL_COLOR_SPACE_RGB && encoding.white_point == JXL_WHITE_POINT_D65 &&
         encoding.primaries == JXL_PRIMARIES_SRGB && encoding.transfer_function == JXL_TRANSFER_FUNCTION_SRGB;
}

bool is_hdr_transfer(const JxlColorEncoding& encoding) {
  return encoding.transfer_function == JXL_TRANSFER_FUNCTION_PQ ||
         encoding.transfer_function == JXL_TRANSFER_FUNCTION_HLG;
}

std::vector<std::uint8_t> original_icc(const JxlDecoder* decoder) {
  std::size_t size = 0;
  if (JxlDecoderGetICCProfileSize(decoder, JXL_COLOR_PROFILE_TARGET_ORIGINAL, &size) != JXL_DEC_SUCCESS ||
      size == 0) {
    return {};
  }
  std::vector<std::uint8_t> icc(size);
  if (JxlDecoderGetColorAsICCProfile(decoder, JXL_COLOR_PROFILE_TARGET_ORIGINAL, icc.data(), icc.size()) !=
      JXL_DEC_SUCCESS) {
    return {};
  }
  return icc;
}

std::string default_frame_name(int index, std::uint32_t delay_ms) {
  return "Frame " + std::to_string(index) + " " + animation::format_delay_seconds_token(delay_ms);
}

// How a decode delivers pixels: the depth of the document it builds, the libjxl sample type
// it asks for, and the extra steps on the way.
struct DecodePlan {
  BitDepth depth{BitDepth::UInt8};
  JxlDataType type{JXL_TYPE_UINT8};
  bool gray{false};
  bool alpha{false};
  // Float output scaled so 1.0 is SDR white (PQ and HLG sources).
  float hdr_scale{1.0F};
  // 8-bit document from float HDR samples through the JPEG XR highlight rolloff.
  bool tone_map{false};
};

// Converts one decoded frame (tightly packed, `plan` layout) to a PixelBuffer at the
// document's depth: gray expands to RGB, alpha stays straight (libjxl unpremultiplies).
PixelBuffer frame_to_pixels(const std::vector<std::uint8_t>& raw, std::int32_t width, std::int32_t height,
                            const DecodePlan& plan) {
  const auto source_channels = static_cast<std::size_t>((plan.gray ? 1 : 3) + (plan.alpha ? 1 : 0));
  const auto pixel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  if (plan.tone_map) {
    // Linear sRGB floats, 1.0 at SDR white; the rolloff keeps SDR content as it is.
    std::vector<float> rgba(pixel_count * 4U);
    const auto* in = reinterpret_cast<const float*>(raw.data());
    for (std::size_t i = 0; i < pixel_count; ++i) {
      const float* px = in + i * source_channels;
      for (int c = 0; c < 3; ++c) {
        rgba[i * 4U + static_cast<std::size_t>(c)] = (plan.gray ? px[0] : px[c]) * plan.hdr_scale;
      }
      rgba[i * 4U + 3U] = plan.alpha ? px[source_channels - 1] : 1.0F;
    }
    auto rgba8 = jxr::tone_map_scrgb_to_rgba8(rgba, width, height);
    PixelBuffer pixels(width, height, plan.alpha ? PixelFormat::rgba8() : PixelFormat::rgb8());
    auto out = pixels.data();
    const std::size_t out_channels = plan.alpha ? 4U : 3U;
    for (std::size_t i = 0; i < pixel_count; ++i) {
      std::copy_n(rgba8.data() + i * 4U, out_channels, out.data() + i * out_channels);
    }
    return pixels;
  }

  const auto base = plan.alpha ? PixelFormat::rgba8() : PixelFormat::rgb8();
  PixelBuffer pixels(width, height, with_bit_depth(base, plan.depth));
  const std::size_t out_channels = plan.alpha ? 4U : 3U;
  const auto expand = [&](const auto* in, auto* out, auto scale) {
    for (std::size_t i = 0; i < pixel_count; ++i) {
      const auto* px = in + i * source_channels;
      auto* dst = out + i * out_channels;
      for (std::size_t c = 0; c < 3; ++c) {
        dst[c] = plan.gray ? scale(px[0], false) : scale(px[c], false);
      }
      if (plan.alpha) {
        dst[3] = scale(px[source_channels - 1], true);
      }
    }
  };
  auto out = pixels.data();
  switch (plan.depth) {
    case BitDepth::UInt8:
      expand(raw.data(), out.data(), [](std::uint8_t v, bool) { return v; });
      break;
    case BitDepth::UInt16:
      expand(reinterpret_cast<const std::uint16_t*>(raw.data()), reinterpret_cast<std::uint16_t*>(out.data()),
             [](std::uint16_t v, bool) { return v; });
      break;
    case BitDepth::Float32: {
      const float scale_color = plan.hdr_scale;
      expand(reinterpret_cast<const float*>(raw.data()), reinterpret_cast<float*>(out.data()),
             [scale_color](float v, bool alpha) {
               if (!std::isfinite(v)) {
                 return 0.0F;
               }
               return alpha ? std::clamp(v, 0.0F, 1.0F) : v * scale_color;
             });
      break;
    }
  }
  return pixels;
}

struct DecodedFrame {
  PixelBuffer pixels;
  std::uint32_t delay_ms{0};
};

// Thrown when a lossy (XYB) file fails to render into its own color space: the CMS could not
// build that transform (the libjxl conformance "patches" case's ICC profile does this with
// skcms). The reader then decodes once more to sRGB.
struct SourceSpaceTargetFailed {};

}  // namespace

const std::vector<std::string>& jxl_extensions() {
  static const std::vector<std::string> extensions = {"jxl"};
  return extensions;
}

bool is_jxl_extension(std::string_view extension) {
  const auto normalized = normalized_extension(extension, false);
  const auto& extensions = jxl_extensions();
  return std::find(extensions.begin(), extensions.end(), normalized) != extensions.end();
}

bool sniff(std::span<const std::uint8_t> bytes) {
  const auto signature = JxlSignatureCheck(bytes.data(), bytes.size());
  return signature == JXL_SIG_CODESTREAM || signature == JXL_SIG_CONTAINER;
}

namespace {

ReadResult decode_jxl(std::span<const std::uint8_t> bytes, const ReadOptions& options, bool prefer_source_space) {
  if (!sniff(bytes)) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "The file is not a JPEG XL image"));
  }
  DecoderPtr decoder(JxlDecoderCreate(nullptr));
  if (!decoder) {
    fail_damaged();
  }
  JxlDecoder* dec = decoder.get();
  Runner runner;
  if (runner.handle() != nullptr &&
      JxlDecoderSetParallelRunner(dec, JxlThreadParallelRunner, runner.handle()) != JXL_DEC_SUCCESS) {
    fail_damaged();
  }
  if (JxlDecoderSubscribeEvents(dec, JXL_DEC_BASIC_INFO | JXL_DEC_COLOR_ENCODING | JXL_DEC_FRAME |
                                         JXL_DEC_FULL_IMAGE) != JXL_DEC_SUCCESS ||
      JxlDecoderSetCms(dec, *JxlGetDefaultCms()) != JXL_DEC_SUCCESS ||
      JxlDecoderSetUnpremultiplyAlpha(dec, JXL_TRUE) != JXL_DEC_SUCCESS ||
      JxlDecoderSetInput(dec, bytes.data(), bytes.size()) != JXL_DEC_SUCCESS) {
    fail_damaged();
  }
  JxlDecoderCloseInput(dec);

  const bool deep = deep_editing_enabled();
  JxlBasicInfo info{};
  DecodePlan plan;
  ReadResult result;
  result.orientation_applied = true;
  std::int32_t width = 0;
  std::int32_t height = 0;
  std::vector<std::uint8_t> raw;
  std::vector<DecodedFrame> frames;
  std::uint32_t pending_delay_ms = 0;
  std::uint64_t decoded_pixels = 0;
  bool targeted_source_space = false;

  for (;;) {
    const auto status = JxlDecoderProcessInput(dec);
    if (status == JXL_DEC_ERROR) {
      if (targeted_source_space) {
        throw SourceSpaceTargetFailed{};
      }
      fail_damaged();
    }
    if (status == JXL_DEC_NEED_MORE_INPUT) {
      throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "The JPEG XL file is truncated"));
    }
    if (status == JXL_DEC_SUCCESS) {
      break;
    }
    if (status == JXL_DEC_BASIC_INFO) {
      if (JxlDecoderGetBasicInfo(dec, &info) != JXL_DEC_SUCCESS) {
        fail_damaged();
      }
      const bool transposed = info.orientation >= JXL_ORIENT_TRANSPOSE;
      const auto pixels = static_cast<std::uint64_t>(info.xsize) * info.ysize;
      if (info.xsize == 0 || info.ysize == 0 || pixels > kMaxPixels ||
          info.xsize > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) ||
          info.ysize > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
        throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "The JPEG XL image is too large to open"));
      }
      width = static_cast<std::int32_t>(transposed ? info.ysize : info.xsize);
      height = static_cast<std::int32_t>(transposed ? info.xsize : info.ysize);
      plan.gray = info.num_color_channels == 1;
      plan.alpha = info.alpha_bits != 0;
      if (info.num_extra_channels > (plan.alpha ? 1U : 0U)) {
        result.read.notices.emplace_back(PATCHY_TRANSLATE_NOOP(
            "QObject", "JPEG XL: extra channels other than alpha were not imported"));
      }
    } else if (status == JXL_DEC_COLOR_ENCODING) {
      JxlColorEncoding original{};
      const bool encoded =
          JxlDecoderGetColorAsEncodedProfile(dec, JXL_COLOR_PROFILE_TARGET_ORIGINAL, &original) == JXL_DEC_SUCCESS;
      const bool hdr = encoded && is_hdr_transfer(original);
      const bool float_samples = info.exponent_bits_per_sample > 0;
      if (hdr) {
        plan.hdr_scale = info.intensity_target > 0.0F ? info.intensity_target / kSdrWhiteNits : 1.0F;
      }
      if ((float_samples || hdr) && deep) {
        plan.depth = BitDepth::Float32;
        plan.type = JXL_TYPE_FLOAT;
      } else if (hdr) {
        plan.tone_map = true;
        plan.type = JXL_TYPE_FLOAT;
      } else if (info.bits_per_sample > 8 && !float_samples && deep) {
        plan.depth = BitDepth::UInt16;
        plan.type = JXL_TYPE_UINT16;
      }

      JxlColorEncoding target{};
      bool set_target = false;
      if (plan.type == JXL_TYPE_FLOAT) {
        JxlColorEncodingSetToLinearSRGB(&target, plan.gray ? JXL_TRUE : JXL_FALSE);
        set_target = true;
      } else if (plan.gray) {
        JxlColorEncodingSetToSRGB(&target, JXL_TRUE);
        set_target = true;
      }
      bool source_space = !plan.gray && plan.type != JXL_TYPE_FLOAT;
      if (!plan.gray && plan.type != JXL_TYPE_FLOAT && !info.uses_original_profile) {
        // XYB: hand the pixels back in the file's own space so its profile describes them.
        // A profile the CMS cannot target falls back to sRGB, and the profile is then shown
        // for reference only.
        if (!prefer_source_space) {
          JxlColorEncodingSetToSRGB(&target, JXL_FALSE);
          set_target = true;
          source_space = false;
        } else if (encoded) {
          target = original;
          set_target = true;
          targeted_source_space = true;
        } else {
          const auto icc = original_icc(dec);
          if (!icc.empty() && JxlDecoderSetOutputColorProfile(dec, nullptr, icc.data(), icc.size()) == JXL_DEC_SUCCESS) {
            targeted_source_space = true;
          } else {
            JxlColorEncodingSetToSRGB(&target, JXL_FALSE);
            set_target = true;
            source_space = false;
          }
        }
      }
      if (set_target && JxlDecoderSetOutputColorProfile(dec, &target, nullptr, 0) != JXL_DEC_SUCCESS) {
        fail_damaged();
      }
      result.pixels_in_source_space = source_space;
    } else if (status == JXL_DEC_FRAME) {
      JxlFrameHeader header{};
      if (JxlDecoderGetFrameHeader(dec, &header) != JXL_DEC_SUCCESS) {
        fail_damaged();
      }
      pending_delay_ms = 0;
      if (info.have_animation && info.animation.tps_numerator > 0) {
        const double ms = static_cast<double>(header.duration) * 1000.0 * info.animation.tps_denominator /
                          info.animation.tps_numerator;
        pending_delay_ms = static_cast<std::uint32_t>(std::clamp(std::lround(ms), 0L, 0x7FFFFFFFL));
      }
    } else if (status == JXL_DEC_NEED_IMAGE_OUT_BUFFER) {
      decoded_pixels += static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
      if (decoded_pixels > kMaxAnimationPixels) {
        throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "The JPEG XL animation is too large to open"));
      }
      const JxlPixelFormat format{static_cast<std::uint32_t>((plan.gray ? 1 : 3) + (plan.alpha ? 1 : 0)), plan.type,
                                  JXL_NATIVE_ENDIAN, 0};
      std::size_t size = 0;
      if (JxlDecoderImageOutBufferSize(dec, &format, &size) != JXL_DEC_SUCCESS) {
        fail_damaged();
      }
      raw.assign(size, 0);
      if (JxlDecoderSetImageOutBuffer(dec, &format, raw.data(), raw.size()) != JXL_DEC_SUCCESS) {
        fail_damaged();
      }
    } else if (status == JXL_DEC_FULL_IMAGE) {
      frames.push_back({frame_to_pixels(raw, width, height, plan), pending_delay_ms});
      if (!info.have_animation) {
        break;
      }
    }
  }
  if (frames.empty()) {
    fail_damaged();
  }

  const auto document_depth = plan.tone_map ? BitDepth::UInt8 : plan.depth;
  const auto base = plan.alpha ? PixelFormat::rgba8() : PixelFormat::rgb8();
  Document document(width, height, with_bit_depth(base, document_depth));
  document.color_state().bit_depth = document_depth;
  document.print_settings().horizontal_ppi = 72.0;
  document.print_settings().vertical_ppi = 72.0;
  if (info.have_animation && frames.size() > 1) {
    document.metadata().values[kLoopCountMetadata] = std::to_string(info.animation.num_loops);
    const std::function<std::string(int, std::uint32_t)> name =
        options.frame_name ? options.frame_name : std::function<std::string(int, std::uint32_t)>(default_frame_name);
    int index = 0;
    for (auto& frame : frames) {
      document.add_pixel_layer(name(++index, frame.delay_ms), std::move(frame.pixels));
    }
    std::reverse(document.layers().begin(), document.layers().end());
    document.set_active_layer(document.layers().back().id());
  } else {
    document.add_pixel_layer("Background", std::move(frames.front().pixels));
  }
  result.frame_count = static_cast<std::uint32_t>(frames.size());
  result.read.document = std::move(document);
  return result;
}

}  // namespace

ReadResult read_jxl_detailed(std::span<const std::uint8_t> bytes, const ReadOptions& options) {
  try {
    return decode_jxl(bytes, options, true);
  } catch (const SourceSpaceTargetFailed&) {
    return decode_jxl(bytes, options, false);
  }
}

FormatReadResult read_jxl(std::span<const std::uint8_t> bytes) {
  return read_jxl_detailed(bytes).read;
}

FormatReadResult read_jxl_file(const std::filesystem::path& path) {
  const auto bytes = formats::read_file_bytes(path, "JPEG XL");
  auto result = read_jxl(bytes);
  formats::rename_first_layer_to_stem(result.document, path);
  return result;
}

FileMetadata read_metadata(std::span<const std::uint8_t> bytes) {
  FileMetadata metadata;
  const auto signature = JxlSignatureCheck(bytes.data(), bytes.size());
  if (signature != JXL_SIG_CODESTREAM && signature != JXL_SIG_CONTAINER) {
    return metadata;
  }
  metadata.container = signature == JXL_SIG_CONTAINER;
  if (metadata.container) {
    std::uint64_t pos = 0;
    while (pos + 8 <= bytes.size() && metadata.boxes.size() < 256) {
      std::uint64_t size = (static_cast<std::uint64_t>(bytes[pos]) << 24) | (bytes[pos + 1] << 16) |
                           (bytes[pos + 2] << 8) | bytes[pos + 3];
      std::string type(reinterpret_cast<const char*>(bytes.data() + pos + 4), 4);
      if (size == 1 && pos + 16 <= bytes.size()) {
        size = 0;
        for (int i = 0; i < 8; ++i) {
          size = (size << 8) | bytes[pos + 8 + static_cast<std::uint64_t>(i)];
        }
      } else if (size == 0) {
        size = bytes.size() - pos;
      }
      if (size < 8 || size > bytes.size() - pos) {
        break;
      }
      metadata.boxes.push_back({std::move(type), pos, size});
      pos += size;
    }
  }

  DecoderPtr decoder(JxlDecoderCreate(nullptr));
  if (!decoder) {
    return metadata;
  }
  JxlDecoder* dec = decoder.get();
  if (JxlDecoderSubscribeEvents(dec, JXL_DEC_COLOR_ENCODING | JXL_DEC_BOX) != JXL_DEC_SUCCESS ||
      JxlDecoderSetDecompressBoxes(dec, JXL_TRUE) != JXL_DEC_SUCCESS ||
      JxlDecoderSetInput(dec, bytes.data(), bytes.size()) != JXL_DEC_SUCCESS) {
    return metadata;
  }
  JxlDecoderCloseInput(dec);
  std::vector<std::uint8_t> box;
  std::string box_type;
  constexpr std::size_t kChunk = 64U * 1024U;
  constexpr std::size_t kMaxBox = 64U * 1024U * 1024U;
  const auto finish_box = [&] {
    if (box_type.empty()) {
      return;
    }
    const auto remaining = JxlDecoderReleaseBoxBuffer(dec);
    box.resize(box.size() - remaining);
    if (box_type == "Exif" && box.size() >= 4) {
      // A big-endian offset from the end of this field to the TIFF header.
      const std::size_t offset = (static_cast<std::size_t>(box[0]) << 24) | (box[1] << 16) | (box[2] << 8) | box[3];
      if (offset <= box.size() - 4) {
        metadata.exif.assign(box.begin() + static_cast<std::ptrdiff_t>(4 + offset), box.end());
      }
    } else if (box_type == "xml ") {
      metadata.xmp = std::move(box);
    }
    box.clear();
    box_type.clear();
  };
  for (;;) {
    const auto status = JxlDecoderProcessInput(dec);
    if (status == JXL_DEC_COLOR_ENCODING) {
      JxlColorEncoding original{};
      const bool encoded =
          JxlDecoderGetColorAsEncodedProfile(dec, JXL_COLOR_PROFILE_TARGET_ORIGINAL, &original) == JXL_DEC_SUCCESS;
      if (!(encoded && is_plain_srgb(original))) {
        metadata.icc = original_icc(dec);
      }
    } else if (status == JXL_DEC_BOX) {
      finish_box();
      JxlBoxType type{};
      if (JxlDecoderGetBoxType(dec, type, JXL_TRUE) != JXL_DEC_SUCCESS) {
        break;
      }
      const std::string name(type, 4);
      if (name == "Exif" || name == "xml ") {
        box_type = name;
        box.assign(kChunk, 0);
        if (JxlDecoderSetBoxBuffer(dec, box.data(), box.size()) != JXL_DEC_SUCCESS) {
          box_type.clear();
        }
      }
    } else if (status == JXL_DEC_BOX_NEED_MORE_OUTPUT) {
      const auto remaining = JxlDecoderReleaseBoxBuffer(dec);
      const auto used = box.size() - remaining;
      if (box.size() >= kMaxBox) {
        box_type.clear();
        box.clear();
        continue;
      }
      box.resize(box.size() * 2);
      if (JxlDecoderSetBoxBuffer(dec, box.data() + used, box.size() - used) != JXL_DEC_SUCCESS) {
        break;
      }
    } else if (status == JXL_DEC_SUCCESS) {
      finish_box();
      break;
    } else {
      break;  // error or truncated: keep what was read
    }
  }
  return metadata;
}

// ------------------------------------------------------------------------- encoding

namespace {

struct EncodeLayout {
  std::int32_t width{0};
  std::int32_t height{0};
  BitDepth depth{BitDepth::UInt8};
  bool alpha{false};
};

JxlDataType data_type_for(BitDepth depth) {
  switch (depth) {
    case BitDepth::UInt16:
      return JXL_TYPE_UINT16;
    case BitDepth::Float32:
      return JXL_TYPE_FLOAT;
    case BitDepth::UInt8:
      break;
  }
  return JXL_TYPE_UINT8;
}

[[noreturn]] void fail_encode() {
  throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Could not encode the JPEG XL image"));
}

// Sets up the encoder for one image or animation: basic info, color, metadata boxes.
void configure_encoder(JxlEncoder* enc, const EncodeLayout& layout, const WriteOptions& options,
                       const std::vector<std::uint8_t>& icc, bool animated, std::uint16_t loops) {
  JxlEncoderSetCms(enc, *JxlGetDefaultCms());
  JxlBasicInfo info;
  JxlEncoderInitBasicInfo(&info);
  info.xsize = static_cast<std::uint32_t>(layout.width);
  info.ysize = static_cast<std::uint32_t>(layout.height);
  switch (layout.depth) {
    case BitDepth::UInt8:
      info.bits_per_sample = 8;
      break;
    case BitDepth::UInt16:
      info.bits_per_sample = 16;
      break;
    case BitDepth::Float32:
      info.bits_per_sample = 32;
      info.exponent_bits_per_sample = 8;
      break;
  }
  info.num_color_channels = 3;
  if (layout.alpha) {
    info.num_extra_channels = 1;
    info.alpha_bits = info.bits_per_sample;
    info.alpha_exponent_bits = info.exponent_bits_per_sample;
  }
  info.uses_original_profile = options.lossless ? JXL_TRUE : JXL_FALSE;
  if (animated) {
    info.have_animation = JXL_TRUE;
    info.animation.tps_numerator = 1000;  // ticks are milliseconds
    info.animation.tps_denominator = 1;
    info.animation.num_loops = loops;
  }
  if (JxlEncoderSetBasicInfo(enc, &info) != JXL_ENC_SUCCESS) {
    fail_encode();
  }
  bool color_set = false;
  if (!icc.empty() && layout.depth != BitDepth::Float32) {
    color_set = JxlEncoderSetICCProfile(enc, icc.data(), icc.size()) == JXL_ENC_SUCCESS;
  }
  if (!color_set) {
    JxlColorEncoding encoding{};
    if (layout.depth == BitDepth::Float32) {
      JxlColorEncodingSetToLinearSRGB(&encoding, JXL_FALSE);
    } else {
      JxlColorEncodingSetToSRGB(&encoding, JXL_FALSE);
    }
    if (JxlEncoderSetColorEncoding(enc, &encoding) != JXL_ENC_SUCCESS) {
      fail_encode();
    }
  }
  if (!options.exif.empty() || !options.xmp.empty()) {
    if (JxlEncoderUseBoxes(enc) != JXL_ENC_SUCCESS) {
      fail_encode();
    }
    if (!options.exif.empty()) {
      // A zero offset: the TIFF header follows the field directly.
      std::vector<std::uint8_t> exif(4, 0);
      exif.insert(exif.end(), options.exif.begin(), options.exif.end());
      if (JxlEncoderAddBox(enc, "Exif", exif.data(), exif.size(), JXL_FALSE) != JXL_ENC_SUCCESS) {
        fail_encode();
      }
    }
    if (!options.xmp.empty() &&
        JxlEncoderAddBox(enc, "xml ", options.xmp.data(), options.xmp.size(), JXL_FALSE) != JXL_ENC_SUCCESS) {
      fail_encode();
    }
    JxlEncoderCloseBoxes(enc);
  }
}

// Every frame goes through here. Patches and dots stay off on every encode: they are the
// encoder's content-classified coding tools, and docs/legal-licensing.md (JPEG XL
// condition 3) forbids them. Do not add an option that turns them back on.
JxlEncoderFrameSettings* create_frame_settings(JxlEncoder* enc, const WriteOptions& options) {
  auto* settings = JxlEncoderFrameSettingsCreate(enc, nullptr);
  if (settings == nullptr) {
    fail_encode();
  }
  constexpr std::int64_t kOff = 0;
  if (JxlEncoderFrameSettingsSetOption(settings, JXL_ENC_FRAME_SETTING_PATCHES, kOff) != JXL_ENC_SUCCESS ||
      JxlEncoderFrameSettingsSetOption(settings, JXL_ENC_FRAME_SETTING_DOTS, kOff) != JXL_ENC_SUCCESS) {
    fail_encode();
  }
  g_last_patches = kOff;
  g_last_dots = kOff;
  if (JxlEncoderFrameSettingsSetOption(settings, JXL_ENC_FRAME_SETTING_EFFORT, std::clamp(options.effort, 1, 9)) !=
      JXL_ENC_SUCCESS) {
    fail_encode();
  }
  if (options.lossless) {
    if (JxlEncoderSetFrameLossless(settings, JXL_TRUE) != JXL_ENC_SUCCESS) {
      fail_encode();
    }
  } else if (JxlEncoderSetFrameDistance(settings, JxlEncoderDistanceFromQuality(static_cast<float>(
                                                      std::clamp(options.quality, 1, 100)))) != JXL_ENC_SUCCESS) {
    fail_encode();
  }
  return settings;
}

// Tightly packed samples for libjxl: RGBA buffers drop alpha when the layout has none.
std::vector<std::uint8_t> packed_samples(const PixelBuffer& pixels, const EncodeLayout& layout) {
  const auto format = pixels.format();
  const auto sample = bytes_per_channel(layout.depth);
  const std::size_t out_channels = layout.alpha ? 4U : 3U;
  const std::size_t in_channels = format.channels;
  const auto pixel_count = static_cast<std::size_t>(layout.width) * static_cast<std::size_t>(layout.height);
  std::vector<std::uint8_t> out(pixel_count * out_channels * sample);
  for (std::int32_t y = 0; y < layout.height; ++y) {
    const auto row = pixels.row(y);
    auto* dst = out.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(layout.width) * out_channels * sample;
    if (in_channels == out_channels) {
      std::memcpy(dst, row.data(), static_cast<std::size_t>(layout.width) * out_channels * sample);
      continue;
    }
    for (std::int32_t x = 0; x < layout.width; ++x) {
      const auto* src = row.data() + static_cast<std::size_t>(x) * in_channels * sample;
      std::memcpy(dst + static_cast<std::size_t>(x) * out_channels * sample, src, 3U * sample);
      if (out_channels == 4U) {
        // A 3-channel source never reaches here with alpha requested: callers derive the
        // layout from the buffer.
        std::memcpy(dst + (static_cast<std::size_t>(x) * out_channels + 3U) * sample, src + 3U * sample, sample);
      }
    }
  }
  return out;
}

void add_frame(JxlEncoderFrameSettings* settings, const PixelBuffer& pixels,
               const EncodeLayout& layout) {
  if (pixels.width() != layout.width || pixels.height() != layout.height ||
      pixels.format().bit_depth != layout.depth || pixels.format().channels < 3 ||
      (layout.alpha && pixels.format().channels < 4)) {
    fail_encode();
  }
  const auto samples = packed_samples(pixels, layout);
  const JxlPixelFormat format{layout.alpha ? 4U : 3U, data_type_for(layout.depth), JXL_NATIVE_ENDIAN, 0};
  if (JxlEncoderAddImageFrame(settings, &format, samples.data(), samples.size()) != JXL_ENC_SUCCESS) {
    fail_encode();
  }
}

std::vector<std::uint8_t> drain(JxlEncoder* enc) {
  std::vector<std::uint8_t> out(64U * 1024U);
  std::uint8_t* next = out.data();
  std::size_t available = out.size();
  for (;;) {
    const auto status = JxlEncoderProcessOutput(enc, &next, &available);
    if (status == JXL_ENC_SUCCESS) {
      break;
    }
    if (status != JXL_ENC_NEED_MORE_OUTPUT) {
      fail_encode();
    }
    const auto used = static_cast<std::size_t>(next - out.data());
    out.resize(out.size() * 2);
    next = out.data() + used;
    available = out.size() - used;
  }
  out.resize(static_cast<std::size_t>(next - out.data()));
  return out;
}

bool any_transparent(const PixelBuffer& rgba) {
  if (rgba.format().channels < 4) {
    return false;
  }
  for (std::int32_t y = 0; y < rgba.height(); ++y) {
    for (std::int32_t x = 0; x < rgba.width(); ++x) {
      if (pixel_alpha_at(rgba, x, y) < 1.0F) {
        return true;
      }
    }
  }
  return false;
}

std::vector<std::uint8_t> rgb_profile_of(const Document& document) {
  const auto& icc = document.color_state().embedded_icc_profile;
  // An RGB profile header carries "RGB " at offset 16; anything else is not written.
  if (icc.size() >= 20 && std::memcmp(icc.data() + 16, "RGB ", 4) == 0) {
    return icc;
  }
  return {};
}

}  // namespace

std::vector<std::uint8_t> write_jxl(const Document& document, const WriteOptions& options) {
  const auto depth = document.color_state().bit_depth;
  PixelBuffer flat = depth == BitDepth::UInt8 ? flatten_document_rgba8(document) : Compositor{}.flatten_rgba_deep(document);
  if (flat.width() <= 0 || flat.height() <= 0) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "The document has no pixels to save"));
  }
  const EncodeLayout layout{flat.width(), flat.height(), depth, any_transparent(flat)};
  EncoderPtr encoder(JxlEncoderCreate(nullptr));
  if (!encoder) {
    fail_encode();
  }
  Runner runner;
  if (runner.handle() != nullptr &&
      JxlEncoderSetParallelRunner(encoder.get(), JxlThreadParallelRunner, runner.handle()) != JXL_ENC_SUCCESS) {
    fail_encode();
  }
  configure_encoder(encoder.get(), layout, options, rgb_profile_of(document), false, 0);
  auto* settings = create_frame_settings(encoder.get(), options);
  add_frame(settings, flat, layout);
  JxlEncoderCloseInput(encoder.get());
  return drain(encoder.get());
}

std::vector<std::uint8_t> write_jxl(const Document& document) {
  return write_jxl(document, WriteOptions{});
}

void write_jxl_file(const Document& document, const std::filesystem::path& path, const WriteOptions& options) {
  formats::write_file_bytes(path, write_jxl(document, options), "JPEG XL");
}

struct AnimationEncoder::Impl {
  EncoderPtr encoder;
  Runner runner;
  EncodeLayout layout;
  WriteOptions options;
  JxlEncoderFrameSettings* settings{nullptr};
  bool finished{false};
};

AnimationEncoder::AnimationEncoder(int width, int height, BitDepth depth, std::uint16_t loops,
                                   const WriteOptions& options, std::vector<std::uint8_t> icc_profile)
    : impl_(std::make_unique<Impl>()) {
  if (width <= 0 || height <= 0) {
    fail_encode();
  }
  impl_->encoder.reset(JxlEncoderCreate(nullptr));
  if (!impl_->encoder) {
    fail_encode();
  }
  if (impl_->runner.handle() != nullptr &&
      JxlEncoderSetParallelRunner(impl_->encoder.get(), JxlThreadParallelRunner, impl_->runner.handle()) !=
          JXL_ENC_SUCCESS) {
    fail_encode();
  }
  impl_->layout = EncodeLayout{width, height, depth, true};
  impl_->options = options;
  configure_encoder(impl_->encoder.get(), impl_->layout, options, icc_profile, true, loops);
  impl_->settings = create_frame_settings(impl_->encoder.get(), options);
}

AnimationEncoder::~AnimationEncoder() = default;

void AnimationEncoder::add(const PixelBuffer& rgba, std::uint32_t duration_ms) {
  if (impl_->finished) {
    fail_encode();
  }
  JxlFrameHeader header;
  JxlEncoderInitFrameHeader(&header);
  header.duration = duration_ms;
  if (JxlEncoderSetFrameHeader(impl_->settings, &header) != JXL_ENC_SUCCESS) {
    fail_encode();
  }
  add_frame(impl_->settings, rgba, impl_->layout);
}

std::vector<std::uint8_t> AnimationEncoder::finish() {
  if (impl_->finished) {
    fail_encode();
  }
  impl_->finished = true;
  JxlEncoderCloseInput(impl_->encoder.get());
  return drain(impl_->encoder.get());
}

EncoderToolSettings last_encoder_tool_settings() noexcept {
  return {g_last_patches.load(), g_last_dots.load()};
}

}  // namespace patchy::jxl
