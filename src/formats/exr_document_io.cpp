#include "formats/exr_document_io.hpp"

#include "core/pixel_depth.hpp"
#include "formats/document_flatten.hpp"
#include "render/compositor.hpp"
#include "support/decode_budget.hpp"
#include "support/string_utils.hpp"
#include "support/translate_noop.hpp"

// tinyexr's implementation lives in this one translation unit. It inflates ZIP channels
// through the vendored miniz (TINYEXR_USE_MINIZ is its default), with threads off so the
// decode stays deterministic and free of std::thread in a format reader.
#define TINYEXR_IMPLEMENTATION
#define TINYEXR_USE_THREAD 0
#define TINYEXR_USE_OPENMP 0
// Vendored source compiled into a Patchy-owned target: its own diagnostics are scoped to
// this include (AGENTS.md, zero-warning rule).
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4244 4245 4267 4456 4457 4458 4701 4702)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#include "formats/tinyexr/tinyexr.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>

namespace patchy::exr {

namespace {

[[nodiscard]] float srgb_to_linear(float v) noexcept {
  return v <= 0.04045F ? v / 12.92F : std::pow((v + 0.055F) / 1.055F, 2.4F);
}

struct FreeError {
  const char* text{nullptr};
  ~FreeError() {
    if (text != nullptr) {
      FreeEXRErrorMessage(text);
    }
  }
};

[[nodiscard]] std::runtime_error damaged() {
  return std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "The OpenEXR file is damaged or truncated"));
}

// tinyexr sizes its channel buffers from the data window and its offset table from
// `chunkCount` (single-part scanline files) or the tile grid, before reading any pixel data,
// so a few header bytes could ask for gigabytes. Reject a header the file cannot back
// (docs/psd-reader-hardening.md, "Decode budget").
void check_declared_size(const EXRHeader& header, std::size_t file_bytes) {
  const auto width = static_cast<std::int64_t>(header.data_window.max_x) - header.data_window.min_x + 1;
  const auto height = static_cast<std::int64_t>(header.data_window.max_y) - header.data_window.min_y + 1;
  if (width <= 0 || height <= 0 || header.num_channels <= 0) {
    throw damaged();
  }
  // Every channel at its stored size (subsampled channels count in full: an upper bound).
  std::uint64_t sample_bytes = 0;
  for (int c = 0; c < header.num_channels; ++c) {
    sample_bytes += header.pixel_types[c] == TINYEXR_PIXELTYPE_HALF ? 2U : 4U;
  }
  if (!checked_decode_bytes(width, height, sample_bytes, file_bytes).has_value()) {
    throw damaged();
  }
  std::uint64_t blocks = 0;
  if (header.tiled != 0) {
    if (header.tile_size_x <= 0 || header.tile_size_y <= 0) {
      throw damaged();
    }
    // The full-resolution level alone; mip and rip levels only add tiles.
    const auto across = (static_cast<std::uint64_t>(width) + static_cast<std::uint64_t>(header.tile_size_x) - 1U) /
                        static_cast<std::uint64_t>(header.tile_size_x);
    const auto down = (static_cast<std::uint64_t>(height) + static_cast<std::uint64_t>(header.tile_size_y) - 1U) /
                      static_cast<std::uint64_t>(header.tile_size_y);
    blocks = across * down;  // each at most 2^23 (tinyexr's dimension cap), so no overflow
  } else {
    // tinyexr's own lines per block, which it uses to count blocks without `chunkCount`.
    const std::uint64_t lines = header.compression_type == TINYEXR_COMPRESSIONTYPE_ZIP   ? 16U
                                : header.compression_type == TINYEXR_COMPRESSIONTYPE_PIZ ? 32U
                                : header.compression_type == TINYEXR_COMPRESSIONTYPE_ZFP ? 16U
                                                                                         : 1U;
    blocks = (static_cast<std::uint64_t>(height) + lines - 1U) / lines;
    // tinyexr trusts `chunkCount` over the data window for a scanline file; the two must agree.
    if (header.chunk_count > 0 && static_cast<std::uint64_t>(header.chunk_count) != blocks) {
      throw damaged();
    }
  }
  // One 8-byte offset per block must fit in the file.
  if (blocks > file_bytes / 8U) {
    throw damaged();
  }
}

}  // namespace

const std::vector<std::string>& exr_extensions() {
  static const std::vector<std::string> extensions = {"exr"};
  return extensions;
}

bool is_exr_extension(std::string_view extension) {
  return normalized_extension(extension, false) == "exr";
}

bool sniff(std::span<const std::uint8_t> bytes) noexcept {
  return bytes.size() >= 4 && bytes[0] == 0x76 && bytes[1] == 0x2f && bytes[2] == 0x31 && bytes[3] == 0x01;
}

FormatReadResult read_exr(std::span<const std::uint8_t> bytes) {
  if (!sniff(bytes)) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "This file is not an OpenEXR image"));
  }
  EXRVersion version;
  if (ParseEXRVersionFromMemory(&version, bytes.data(), bytes.size()) != TINYEXR_SUCCESS) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "The OpenEXR file is damaged or truncated"));
  }
  if (version.multipart) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Multi-part OpenEXR files are not supported"));
  }
  if (version.non_image) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Deep OpenEXR files are not supported"));
  }
  // The low-level API rather than LoadEXR: it also reads grayscale ("Y") files and tiled
  // ones come back as tiles to paste. Every channel is requested as FLOAT.
  FreeError error;
  EXRHeader header;
  InitEXRHeader(&header);
  if (ParseEXRHeaderFromMemory(&header, &version, bytes.data(), bytes.size(), &error.text) != TINYEXR_SUCCESS) {
    std::string message = PATCHY_TRANSLATE_NOOP("QObject", "The OpenEXR file could not be decoded");
    if (error.text != nullptr) {
      message += ": ";
      message += error.text;
    }
    throw std::runtime_error(message);
  }
  struct HeaderGuard {
    EXRHeader* header;
    ~HeaderGuard() { FreeEXRHeader(header); }
  } header_guard{&header};
  for (int c = 0; c < header.num_channels; ++c) {
    // HALF decodes to FLOAT; UINT channels stay UINT (tinyexr does not convert them) and
    // are scaled below.
    header.requested_pixel_types[c] =
        header.pixel_types[c] == TINYEXR_PIXELTYPE_UINT ? TINYEXR_PIXELTYPE_UINT : TINYEXR_PIXELTYPE_FLOAT;
  }
  check_declared_size(header, bytes.size());
  int channel_index[4] = {-1, -1, -1, -1};  // R, G, B, A
  int luminance = -1;
  for (int c = 0; c < header.num_channels; ++c) {
    const std::string name = header.channels[c].name;
    if (name == "R") channel_index[0] = c;
    else if (name == "G") channel_index[1] = c;
    else if (name == "B") channel_index[2] = c;
    else if (name == "A") channel_index[3] = c;
    else if (name == "Y") luminance = c;
  }
  const bool rgb = channel_index[0] >= 0 && channel_index[1] >= 0 && channel_index[2] >= 0;
  if (!rgb && luminance < 0) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "This OpenEXR file has no R, G, B or Y channels to show"));
  }
  EXRImage image;
  InitEXRImage(&image);
  if (LoadEXRImageFromMemory(&image, &header, bytes.data(), bytes.size(), &error.text) != TINYEXR_SUCCESS) {
    std::string message = PATCHY_TRANSLATE_NOOP("QObject", "The OpenEXR file could not be decoded");
    if (error.text != nullptr) {
      message += ": ";
      message += error.text;
    }
    throw std::runtime_error(message);
  }
  struct ImageGuard {
    EXRImage* image;
    ~ImageGuard() { FreeEXRImage(image); }
  } image_guard{&image};
  const int width = image.width;
  const int height = image.height;
  if (width <= 0 || height <= 0) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "The OpenEXR file is damaged or truncated"));
  }
  PixelBuffer pixels(width, height, with_bit_depth(PixelFormat::rgba8(), BitDepth::Float32));
  auto* out = reinterpret_cast<float*>(pixels.data().data());
  const auto count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  for (std::size_t i = 0; i < count; ++i) {
    out[i * 4U + 3U] = 1.0F;
  }
  // Writes one decoded block (a whole scanline image or one tile) into the buffer.
  const auto paste = [&](unsigned char** planes, int block_w, int block_h, int offset_x, int offset_y) {
    for (int y = 0; y < block_h; ++y) {
      const int ty = offset_y + y;
      if (ty < 0 || ty >= height) continue;
      for (int x = 0; x < block_w; ++x) {
        const int tx = offset_x + x;
        if (tx < 0 || tx >= width) continue;
        const auto src = static_cast<std::size_t>(y) * static_cast<std::size_t>(block_w) + static_cast<std::size_t>(x);
        auto* dst = out + (static_cast<std::size_t>(ty) * static_cast<std::size_t>(width) + static_cast<std::size_t>(tx)) * 4U;
        const auto read = [&](int c) {
          if (header.pixel_types[c] == TINYEXR_PIXELTYPE_UINT) {
            // Integer channels (IDs or counts in most pipelines) map onto 0..1 by their full range.
            return static_cast<float>(reinterpret_cast<const unsigned int*>(planes[c])[src]) / 4294967295.0F;
          }
          const float v = reinterpret_cast<const float*>(planes[c])[src];
          return std::isfinite(v) ? v : 0.0F;
        };
        if (rgb) {
          dst[0] = read(channel_index[0]);
          dst[1] = read(channel_index[1]);
          dst[2] = read(channel_index[2]);
        } else {
          dst[0] = dst[1] = dst[2] = read(luminance);
        }
        if (channel_index[3] >= 0) {
          dst[3] = std::clamp(read(channel_index[3]), 0.0F, 1.0F);
        }
      }
    }
  };
  if (image.images != nullptr) {
    paste(image.images, width, height, 0, 0);
  } else if (image.tiles != nullptr) {
    for (int t = 0; t < image.num_tiles; ++t) {
      const auto& tile = image.tiles[t];
      if (tile.level_x != 0 || tile.level_y != 0) {
        continue;  // mip and rip levels below the full-resolution one
      }
      paste(tile.images, tile.width, tile.height, tile.offset_x * header.tile_size_x, tile.offset_y * header.tile_size_y);
    }
  } else {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "The OpenEXR file could not be decoded"));
  }
  FormatReadResult result{Document(width, height, with_bit_depth(PixelFormat::rgba8(), BitDepth::Float32)), {}};
  result.document.color_state().bit_depth = BitDepth::Float32;
  result.document.add_pixel_layer("Background", std::move(pixels));
  return result;
}

std::vector<std::uint8_t> write_exr(const Document& document) {
  const auto depth = document.color_state().bit_depth;
  const PixelBuffer flat = depth == BitDepth::UInt8 ? flatten_document_rgba8(document) : Compositor{}.flatten_rgba_deep(document);
  const auto w = flat.width();
  const auto h = flat.height();
  if (w <= 0 || h <= 0) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Cannot write an empty image as OpenEXR"));
  }
  const auto channels = flat.format().channels;
  const auto flat_depth = flat.format().bit_depth;
  // Four planar float channels in the order OpenEXR expects when sorted (A, B, G, R).
  std::vector<float> planes[4];
  for (auto& plane : planes) {
    plane.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
  }
  for (std::int32_t y = 0; y < h; ++y) {
    const auto row = flat.row(y);
    for (std::int32_t x = 0; x < w; ++x) {
      float rgba[4] = {0.0F, 0.0F, 0.0F, 1.0F};
      for (int c = 0; c < 4 && c < channels; ++c) {
        const auto index = static_cast<std::size_t>(x) * channels + static_cast<std::size_t>(c);
        float v = 0.0F;
        switch (flat_depth) {
          case BitDepth::UInt8: v = static_cast<float>(row[index]) / 255.0F; break;
          case BitDepth::UInt16: v = static_cast<float>(reinterpret_cast<const std::uint16_t*>(row.data())[index]) / 65535.0F; break;
          case BitDepth::Float32: v = reinterpret_cast<const float*>(row.data())[index]; break;
        }
        rgba[c] = (c < 3 && flat_depth != BitDepth::Float32) ? srgb_to_linear(v) : v;
      }
      const auto at = static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
      planes[0][at] = rgba[3];  // A
      planes[1][at] = rgba[2];  // B
      planes[2][at] = rgba[1];  // G
      planes[3][at] = rgba[0];  // R
    }
  }
  EXRHeader header;
  InitEXRHeader(&header);
  EXRImage image;
  InitEXRImage(&image);
  image.num_channels = 4;
  float* image_planes[4] = {planes[0].data(), planes[1].data(), planes[2].data(), planes[3].data()};
  image.images = reinterpret_cast<unsigned char**>(image_planes);
  image.width = w;
  image.height = h;
  header.num_channels = 4;
  EXRChannelInfo channel_infos[4];
  std::memset(channel_infos, 0, sizeof(channel_infos));
  const char* names[4] = {"A", "B", "G", "R"};
  int pixel_types[4];
  int requested_types[4];
  for (int c = 0; c < 4; ++c) {
    channel_infos[c].name[0] = names[c][0];  // one-letter names, NUL-terminated by the memset
    pixel_types[c] = TINYEXR_PIXELTYPE_FLOAT;
    requested_types[c] = TINYEXR_PIXELTYPE_FLOAT;
  }
  header.channels = channel_infos;
  header.pixel_types = pixel_types;
  header.requested_pixel_types = requested_types;
  header.compression_type = TINYEXR_COMPRESSIONTYPE_ZIP;
  unsigned char* memory = nullptr;
  FreeError error;
  const auto size = SaveEXRImageToMemory(&image, &header, &memory, &error.text);
  if (size == 0 || memory == nullptr) {
    std::string message = PATCHY_TRANSLATE_NOOP("QObject", "The OpenEXR file could not be written");
    if (error.text != nullptr) {
      message += ": ";
      message += error.text;
    }
    std::free(memory);
    throw std::runtime_error(message);
  }
  std::vector<std::uint8_t> out(memory, memory + size);
  std::free(memory);
  return out;
}

}  // namespace patchy::exr
