#include "formats/af_raster_internal.hpp"

#include "color/color_management.hpp"
#include "core/environment.hpp"
#include "formats/heif_document_io.hpp"
#include "formats/stb/stb_image.h"
#include "support/srgb_transfer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <tuple>

namespace patchy::af::detail {
namespace {

constexpr int kTileSize = 256;

// DyBm bitmap format ids (the app's RasterFormat enum). M8/M16 are the
// single-channel mask planes; CMYK converts through the embedded ICC profile
// when the bitmap carries one, else approximately. LabA16 stores the ICC v4
// Lab16 PCS encoding on the wire (pinned July 2026 against a saturated
// calibration doc; the old "compressed a/b scale" mystery was a desaturated
// probe document) and converts through lcms2's built-in Lab profile.
enum class RasterFormat {
  RGBA8,
  RGBA16,
  Gray8,
  Gray16,
  CMYKA8,
  LabA16,
  Mask8,
  Mask16,
  RGBAFloat,
  Unsupported
};

[[nodiscard]] RasterFormat classify_format(std::int64_t id) {
  switch (id) {
    case 0: return RasterFormat::RGBA8;
    case 1: return RasterFormat::RGBA16;
    case 2: return RasterFormat::Gray8;
    case 3: return RasterFormat::Gray16;
    case 4: return RasterFormat::CMYKA8;
    case 5: return RasterFormat::LabA16;
    case 6: return RasterFormat::Mask8;
    case 7: return RasterFormat::Mask16;
    case 9: return RasterFormat::RGBAFloat;
    default: return RasterFormat::Unsupported;
  }
}

[[nodiscard]] float srgb_to_linear(float value) {
  value = std::clamp(value, 0.0F, 1.0F);
  return value <= 0.04045F ? value / 12.92F : std::pow((value + 0.055F) / 1.055F, 2.4F);
}

// ------------------------------------------------------------- channel planes

// One decoded channel plane. Tiles are always 256 bytes wide by 256 rows
// regardless of sample depth, so the tile-grid width fields count 256-BYTE
// columns (a 16-bit channel's row spans width*2 bytes) while the height fields
// count plain 256-row bands; the plane's horizontal axis is bytes, not samples.
struct ChannelPlane {
  std::vector<std::uint8_t> bytes;
  std::size_t width_bytes{0};
  std::size_t rows{0};
};

// Sta tile codes: 0/1 empty, 2 fill max, 3 fill float 1.0, 4 stored tile,
// 5 base pixels come from the layer's placed-original image (the Bckg stream).
constexpr std::int64_t kTileFillMax = 2;
constexpr std::int64_t kTileFillFloatOne = 3;
constexpr std::int64_t kTileStored = 4;
constexpr std::int64_t kTileFromOriginal = 5;

// Per-channel tile-grid field tags. The base level uses TWi<n>/THi<n>/Idx<n>/
// Sta<n>; mip levels 1..7 use 'M','W'|'H'|'I'|'T',<raw level byte>,<digit>.
struct PlaneTags {
  std::uint32_t tiles_w{0};
  std::uint32_t tiles_h{0};
  std::uint32_t index{0};
  std::uint32_t status{0};
};

[[nodiscard]] PlaneTags base_plane_tags(int channel) {
  return {tag_of("TWi" + std::to_string(channel)), tag_of("THi" + std::to_string(channel)),
          tag_of("Idx" + std::to_string(channel)), tag_of("Sta" + std::to_string(channel))};
}

[[nodiscard]] std::uint32_t mip_tag(char kind, int level, int channel) {
  return (static_cast<std::uint32_t>('M') << 24U) |
         (static_cast<std::uint32_t>(static_cast<std::uint8_t>(kind)) << 16U) |
         (static_cast<std::uint32_t>(static_cast<std::uint8_t>(level)) << 8U) |
         static_cast<std::uint32_t>(static_cast<std::uint8_t>('0' + channel));
}

[[nodiscard]] PlaneTags mip_plane_tags(int level, int channel) {
  return {mip_tag('W', level, channel), mip_tag('H', level, channel),
          mip_tag('I', level, channel), mip_tag('T', level, channel)};
}

// Fill a run of samples with the format's "full" value: 0xFF bytes for the
// integer formats (255 / 65535), 1.0f for float planes (0xFF bytes would be NaN).
void fill_full_samples(std::uint8_t* dst, std::size_t sample_count, std::size_t sample_bytes,
                       bool is_float) {
  if (!is_float) {
    std::memset(dst, 0xFF, sample_count * sample_bytes);
    return;
  }
  const float one = 1.0F;
  for (std::size_t i = 0; i < sample_count; ++i) {
    std::memcpy(dst + i * 4U, &one, sizeof(one));
  }
}

enum class PlaneStatus { Ok, NeedsOriginal, UnknownCode, Invalid };

// Decode one channel plane from its tile streams. `source` (same plane layout)
// supplies the pixels of code-5 tiles; without one a code-5 plane reports
// NeedsOriginal so the caller can build a source (original image or mip
// fallback) and decode again. `out` keeps its layout dimensions either way.
// `plane_width`/`plane_height` are the plane's pixel dimensions (base bitmap
// or mip level): old-generation (1.x, doc version <= 9) DyBms store no
// TWi/THi tile-grid fields at all, so when both are absent the grid is
// derived from those dimensions (256-byte columns by 256-row bands; the
// derived count matches every Sta list across the dbacchet v9 schematic).
[[nodiscard]] PlaneStatus decode_channel_plane(std::span<const std::uint8_t> bytes,
                                               const Container& container, const af::AfClass& dybm,
                                               const PlaneTags& tags, std::size_t sample_bytes,
                                               bool is_float, std::int64_t plane_width,
                                               std::int64_t plane_height,
                                               const ChannelPlane* source, ChannelPlane& out,
                                               std::uint64_t max_plane_bytes) {
  std::int64_t tiles_w = dybm.int_field(tags.tiles_w, 1);
  std::int64_t tiles_h = dybm.int_field(tags.tiles_h, 1);
  if (dybm.field(tags.tiles_w) == nullptr && dybm.field(tags.tiles_h) == nullptr) {
    const std::int64_t derived_w = std::max<std::int64_t>(
        1, (plane_width * static_cast<std::int64_t>(sample_bytes) + kTileSize - 1) / kTileSize);
    const std::int64_t derived_h =
        std::max<std::int64_t>(1, (plane_height + kTileSize - 1) / kTileSize);
    // Only trust the derivation when the plane carries a Sta list of exactly
    // that many codes. A plane with NO status field must keep the 1x1
    // default: 2.x DyBms whose pixels come from a placed original (code-5
    // tiles) often store no mip fields at all, and an oversized empty mip
    // plane would otherwise pass mip_source_planes' size check and feed the
    // code-5 tiles a BLANK source instead of failing to the honest
    // placeholder (paulgessinger screens regressed this way).
    const auto* derived_sta = dybm.field(tags.status);
    const auto* derived_codes =
        derived_sta != nullptr ? std::get_if<std::vector<std::int64_t>>(&derived_sta->value)
                               : nullptr;
    if (derived_codes != nullptr &&
        static_cast<std::int64_t>(derived_codes->size()) == derived_w * derived_h) {
      tiles_w = derived_w;
      tiles_h = derived_h;
    }
  }
  if (tiles_w <= 0 || tiles_h <= 0 || tiles_w > 8192 || tiles_h > 8192 ||
      tiles_w * tiles_h > (1LL << 20U)) {
    return PlaneStatus::Invalid;
  }
  // Up to 2^20 tiles of 64 KiB: 2^36 bytes, which wraps a 32-bit size_t (a 256 x 256 grid
  // is exactly 2^32). Size the plane in 64 bits before anything is written into it.
  const auto tile_count = static_cast<std::uint64_t>(tiles_w) * static_cast<std::uint64_t>(tiles_h);
  const auto plane_bytes =
      checked_buffer_bytes(tile_count, static_cast<std::uint64_t>(kTileSize) * kTileSize, max_plane_bytes);
  if (!plane_bytes.has_value()) {
    return PlaneStatus::Invalid;
  }
  out.width_bytes = static_cast<std::size_t>(tiles_w) * kTileSize;
  out.rows = static_cast<std::size_t>(tiles_h) * kTileSize;
  out.bytes.assign(*plane_bytes, 0);

  const auto* sta = dybm.field(tags.status);
  if (sta == nullptr) {
    if (environment_variable_is_set("PATCHY_AF_TRACE")) {
      std::fprintf(stderr, "[af]   plane tag %08x: no status field\n", tags.status);
    }
    return PlaneStatus::Ok;
  }
  const auto* codes = std::get_if<std::vector<std::int64_t>>(&sta->value);
  if (codes == nullptr) {
    if (environment_variable_is_set("PATCHY_AF_TRACE")) {
      std::fprintf(stderr, "[af]   plane tag %08x: status has variant index %zu\n", tags.status,
                   sta->value.index());
    }
    return PlaneStatus::Ok;
  }
  const auto* idx = dybm.field(tags.index);
  const auto* blocks =
      idx != nullptr ? std::get_if<std::vector<std::shared_ptr<af::AfClass>>>(&idx->value) : nullptr;
  if (environment_variable_is_set("PATCHY_AF_TRACE")) {
    std::array<int, 8> histogram{};
    for (const auto code : *codes) {
      if (code >= 0 && code < 8) {
        ++histogram[static_cast<std::size_t>(code)];
      }
    }
    std::fprintf(stderr,
                 "[af]   plane %08x: %zu codes [0]=%d [2]=%d [4]=%d [5]=%d blocks=%zu src=%d\n",
                 tags.status, codes->size(), histogram[0], histogram[2], histogram[4], histogram[5],
                 blocks != nullptr ? blocks->size() : 0U, source != nullptr ? 1 : 0);
  }
  std::size_t block_index = 0;
  for (std::size_t t = 0; t < codes->size(); ++t) {
    const std::size_t tx = (t % static_cast<std::size_t>(tiles_w)) * kTileSize;
    const std::size_t ty = (t / static_cast<std::size_t>(tiles_w)) * kTileSize;
    if (ty >= out.rows) {
      break;  // more status codes than grid slots; ignore the excess
    }
    const std::int64_t code = (*codes)[t];
    switch (code) {
      case 0:
      case 1:
        break;
      case kTileFillMax:
      case kTileFillFloatOne:
        for (std::size_t row = 0; row < static_cast<std::size_t>(kTileSize); ++row) {
          fill_full_samples(out.bytes.data() + (ty + row) * out.width_bytes + tx,
                            static_cast<std::size_t>(kTileSize) / sample_bytes, sample_bytes,
                            is_float);
        }
        break;
      case kTileFromOriginal: {
        if (source == nullptr) {
          return PlaneStatus::NeedsOriginal;
        }
        if (source->width_bytes != out.width_bytes || source->rows != out.rows) {
          return PlaneStatus::Invalid;
        }
        for (std::size_t row = 0; row < static_cast<std::size_t>(kTileSize); ++row) {
          const std::size_t at = (ty + row) * out.width_bytes + tx;
          std::memcpy(out.bytes.data() + at, source->bytes.data() + at,
                      static_cast<std::size_t>(kTileSize));
        }
        break;
      }
      case kTileStored: {
        if (blocks == nullptr || block_index >= blocks->size() ||
            (*blocks)[block_index] == nullptr) {
          ++block_index;
          break;
        }
        const af::AfClass& block = *(*blocks)[block_index];
        ++block_index;
        const auto* data_field = block.field(af::tag4("Data"));
        if (data_field == nullptr) {
          break;
        }
        const auto* embedded = std::get_if<af::AfEmbedded>(&data_field->value);
        if (embedded == nullptr) {
          break;
        }
        const auto stream = container.streams.find(embedded->data);
        if (stream == container.streams.end()) {
          break;
        }
        std::vector<std::uint8_t> tile;
        try {
          tile = extract_stream(bytes, stream->second, embedded->data, nullptr);
        } catch (const std::exception& error) {
          if (environment_variable_is_set("PATCHY_AF_TRACE")) {
            std::fprintf(stderr, "[af]   tile '%s': %s\n", embedded->data.c_str(), error.what());
          }
          break;
        }
        if (tile.size() == static_cast<std::size_t>(kTileSize) * kTileSize) {
          for (std::size_t row = 0; row < static_cast<std::size_t>(kTileSize); ++row) {
            std::memcpy(out.bytes.data() + (ty + row) * out.width_bytes + tx,
                        tile.data() + row * static_cast<std::size_t>(kTileSize),
                        static_cast<std::size_t>(kTileSize));
          }
          break;
        }
        // Short stream: a partial tile carrying only a sub-rect, described by
        // the block's optional Rect (byte-pitch x like the tile grid). Accept
        // either [x, y, w, h] or [x0, y0, x1, y1], validated by the stream
        // size; anything that does not line up stays skipped (transparent).
        {
          const auto rect = block.vec_field(af::tag4("Rect"));
          if (rect.size() != 4) {
            break;
          }
          const auto rx = static_cast<std::int64_t>(rect[0]);
          const auto ry = static_cast<std::int64_t>(rect[1]);
          std::int64_t rw = static_cast<std::int64_t>(rect[2]);
          std::int64_t rh = static_cast<std::int64_t>(rect[3]);
          if (rw > rx && rh > ry &&
              static_cast<std::uint64_t>(rw - rx) * static_cast<std::uint64_t>(rh - ry) ==
                  tile.size()) {
            rw -= rx;  // [x0, y0, x1, y1]
            rh -= ry;
          }
          if (rx < 0 || ry < 0 || rw <= 0 || rh <= 0 || rx + rw > kTileSize ||
              ry + rh > kTileSize ||
              static_cast<std::uint64_t>(rw) * static_cast<std::uint64_t>(rh) != tile.size()) {
            break;
          }
          for (std::int64_t row = 0; row < rh; ++row) {
            std::memcpy(out.bytes.data() +
                            (ty + static_cast<std::size_t>(ry + row)) * out.width_bytes + tx +
                            static_cast<std::size_t>(rx),
                        tile.data() + static_cast<std::size_t>(row * rw),
                        static_cast<std::size_t>(rw));
          }
        }
        break;
      }
      default:
        return PlaneStatus::UnknownCode;
    }
  }
  return PlaneStatus::Ok;
}

// Sample scale matches the compose step below: 8/16-bit samples map onto the
// 0..255 range (value/257 for 16-bit), float planes return their raw
// linear-light value.
[[nodiscard]] float sample_plane(const ChannelPlane& plane, std::size_t sample_bytes, bool is_float,
                                 std::int32_t x, std::int32_t y) {
  const std::uint8_t* p = plane.bytes.data() + static_cast<std::size_t>(y) * plane.width_bytes +
                          static_cast<std::size_t>(x) * sample_bytes;
  if (is_float) {
    float v = 0.0F;
    std::memcpy(&v, p, sizeof(v));
    return v;
  }
  if (sample_bytes == 2) {
    const std::uint16_t v =
        static_cast<std::uint16_t>(p[0] | (static_cast<std::uint16_t>(p[1]) << 8U));
    return static_cast<float>(v) / 257.0F;
  }
  return static_cast<float>(p[0]);
}

// ---------------------------------------------------- placed-original sources

// A DyBm whose base tiles carry code 5 stores its full-resolution pixels as the
// untouched original image file in the c/<n> stream named by its `Bckg` field:
// a serialized Blck tree (same wire grammar as doc.dat) with Data = the file
// bytes, TifO = the EXIF orientation, DSrc/Filn = the source path. The vendored
// stb_image decodes the JPEG/PNG originals; other embedded formats fall back to
// the stored mip pyramid at half resolution.
[[nodiscard]] std::optional<PixelBuffer> decode_original_file_bytes(
    const std::vector<std::uint8_t>& file, int orientation) {
  if (file.empty() || file.size() > kMaxStreamBytes) {
    return std::nullopt;
  }
  int width = 0;
  int height = 0;
  int components = 0;
  if (stbi_info_from_memory(file.data(), static_cast<int>(file.size()), &width, &height,
                            &components) == 0) {
    return std::nullopt;
  }
  if (width <= 0 || height <= 0 || width > kMaxLayerSide || height > kMaxLayerSide ||
      static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) > (1ULL << 28U)) {
    return std::nullopt;  // keep decoded RGBA under 1 GiB
  }
  stbi_uc* decoded = stbi_load_from_memory(file.data(), static_cast<int>(file.size()), &width,
                                           &height, &components, 4);
  if (decoded == nullptr) {
    return std::nullopt;
  }
  const std::span<const std::uint8_t> rgba(
      decoded, static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U);
  PixelBuffer pixels;
  if (orientation >= 2 && orientation <= 8) {
    const heif::OrientedImage oriented =
        heif::apply_exif_orientation(rgba, width, height, orientation);
    pixels = PixelBuffer(oriented.width, oriented.height, PixelFormat::rgba8());
    for (std::int32_t y = 0; y < oriented.height; ++y) {
      std::memcpy(pixels.row(y).data(),
                  oriented.rgba.data() + static_cast<std::size_t>(y) * oriented.width * 4U,
                  static_cast<std::size_t>(oriented.width) * 4U);
    }
  } else {
    pixels = PixelBuffer(width, height, PixelFormat::rgba8());
    for (int y = 0; y < height; ++y) {
      std::memcpy(pixels.row(y).data(),
                  rgba.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 4U,
                  static_cast<std::size_t>(width) * 4U);
    }
  }
  stbi_image_free(decoded);
  return pixels;
}

// Build the code-5 source plane for one channel in `layout`'s byte layout.
// Channels 1..4 map onto R,G,B,A of the decoded original; deep formats
// re-encode per sample (16-bit value*257, float linear light).
[[nodiscard]] ChannelPlane source_plane_from_original(const PixelBuffer& original,
                                                      const ChannelPlane& layout, int channel,
                                                      std::size_t sample_bytes, bool is_float) {
  ChannelPlane plane;
  plane.width_bytes = layout.width_bytes;
  plane.rows = layout.rows;
  plane.bytes.assign(layout.bytes.size(), 0);  // width_bytes x rows, sized when the layout was
  const int component = std::clamp(channel - 1, 0, 3);
  for (std::int32_t y = 0; y < original.height() && static_cast<std::size_t>(y) < plane.rows; ++y) {
    std::uint8_t* dst_row = plane.bytes.data() + static_cast<std::size_t>(y) * plane.width_bytes;
    for (std::int32_t x = 0; x < original.width(); ++x) {
      const std::size_t at = static_cast<std::size_t>(x) * sample_bytes;
      if (at + sample_bytes > plane.width_bytes) {
        break;
      }
      const std::uint8_t value = original.pixel(x, y)[component];
      if (is_float) {
        const float f = component < 3 ? srgb_to_linear(static_cast<float>(value) / 255.0F)
                                      : static_cast<float>(value) / 255.0F;
        std::memcpy(dst_row + at, &f, sizeof(f));
      } else if (sample_bytes == 2) {
        const std::uint16_t wide = static_cast<std::uint16_t>(value * 257U);
        dst_row[at] = static_cast<std::uint8_t>(wide & 0xFFU);
        dst_row[at + 1] = static_cast<std::uint8_t>(wide >> 8U);
      } else {
        dst_row[at] = value;
      }
    }
  }
  return plane;
}

// Half-resolution fallback: decode each channel's mip level-1 plane (stored as
// plain tiles) and upsample it 2x into the base layouts with bilinear
// filtering. Deterministic plain-float math (cross-toolchain rule).
[[nodiscard]] bool mip_source_planes(std::span<const std::uint8_t> bytes, const Container& container,
                                     const af::AfClass& dybm, int channel_count,
                                     std::size_t sample_bytes, bool is_float, std::int64_t width,
                                     std::int64_t height, const std::vector<ChannelPlane>& layouts,
                                     std::vector<ChannelPlane>& out, std::uint64_t max_plane_bytes) {
  const std::int64_t mip_width = (width + 1) / 2;
  const std::int64_t mip_height = (height + 1) / 2;
  for (int channel = 1; channel <= channel_count; ++channel) {
    ChannelPlane mip;
    const PlaneStatus status =
        decode_channel_plane(bytes, container, dybm, mip_plane_tags(1, channel), sample_bytes,
                             is_float, mip_width, mip_height, nullptr, mip, max_plane_bytes);
    if (status != PlaneStatus::Ok ||
        static_cast<std::size_t>(mip_width) * sample_bytes > mip.width_bytes ||
        static_cast<std::size_t>(mip_height) > mip.rows) {
      return false;
    }
    const ChannelPlane& layout = layouts[static_cast<std::size_t>(channel - 1)];
    ChannelPlane scaled;
    scaled.width_bytes = layout.width_bytes;
    scaled.rows = layout.rows;
    // The layout's own buffer is width_bytes x rows, sized through checked_buffer_bytes.
    scaled.bytes.assign(layout.bytes.size(), 0);
    for (std::int64_t y = 0; y < height && static_cast<std::size_t>(y) < scaled.rows; ++y) {
      for (std::int64_t x = 0; x < width; ++x) {
        const std::size_t at = static_cast<std::size_t>(x) * sample_bytes;
        if (at + sample_bytes > scaled.width_bytes) {
          break;
        }
        const float sx = std::clamp((static_cast<float>(x) + 0.5F) * 0.5F - 0.5F, 0.0F,
                                    static_cast<float>(mip_width - 1));
        const float sy = std::clamp((static_cast<float>(y) + 0.5F) * 0.5F - 0.5F, 0.0F,
                                    static_cast<float>(mip_height - 1));
        const auto x0 = static_cast<std::int32_t>(sx);
        const auto y0 = static_cast<std::int32_t>(sy);
        const std::int32_t x1 =
            std::min<std::int32_t>(x0 + 1, static_cast<std::int32_t>(mip_width - 1));
        const std::int32_t y1 =
            std::min<std::int32_t>(y0 + 1, static_cast<std::int32_t>(mip_height - 1));
        const float fx = sx - static_cast<float>(x0);
        const float fy = sy - static_cast<float>(y0);
        const float top = sample_plane(mip, sample_bytes, is_float, x0, y0) * (1.0F - fx) +
                          sample_plane(mip, sample_bytes, is_float, x1, y0) * fx;
        const float bottom = sample_plane(mip, sample_bytes, is_float, x0, y1) * (1.0F - fx) +
                             sample_plane(mip, sample_bytes, is_float, x1, y1) * fx;
        const float value = top * (1.0F - fy) + bottom * fy;
        std::uint8_t* dst =
            scaled.bytes.data() + static_cast<std::size_t>(y) * scaled.width_bytes + at;
        if (is_float) {
          std::memcpy(dst, &value, sizeof(value));
        } else if (sample_bytes == 2) {
          const auto wide = static_cast<std::uint16_t>(
              std::lround(std::clamp(value, 0.0F, 255.0F) * 257.0F));
          dst[0] = static_cast<std::uint8_t>(wide & 0xFFU);
          dst[1] = static_cast<std::uint8_t>(wide >> 8U);
        } else {
          dst[0] = static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0F, 255.0F)));
        }
      }
    }
    out.push_back(std::move(scaled));
  }
  return true;
}

// Halve `source` along one axis with a box filter: RGBA averages alpha-weighted
// (transparent pixels contribute no color), gray masks average plainly. An odd
// trailing row/column keeps its single pixel. Deterministic double math.
[[nodiscard]] PixelBuffer box_halve_axis(const PixelBuffer& source, bool horizontal, bool gray) {
  const std::int32_t out_w = horizontal ? (source.width() + 1) / 2 : source.width();
  const std::int32_t out_h = horizontal ? source.height() : (source.height() + 1) / 2;
  PixelBuffer out(out_w, out_h, gray ? PixelFormat::gray8() : PixelFormat::rgba8());
  for (std::int32_t y = 0; y < out_h; ++y) {
    auto row = out.row(y);
    for (std::int32_t x = 0; x < out_w; ++x) {
      const std::int32_t x0 = horizontal ? x * 2 : x;
      const std::int32_t y0 = horizontal ? y : y * 2;
      const std::int32_t x1 = horizontal ? std::min(x0 + 1, source.width() - 1) : x0;
      const std::int32_t y1 = horizontal ? y0 : std::min(y0 + 1, source.height() - 1);
      const std::uint8_t* p0 = source.pixel(x0, y0);
      const std::uint8_t* p1 = source.pixel(x1, y1);
      if (gray) {
        row[static_cast<std::size_t>(x)] = static_cast<std::uint8_t>(
            std::lround((static_cast<double>(p0[0]) + static_cast<double>(p1[0])) / 2.0));
        continue;
      }
      const double a0 = static_cast<double>(p0[3]);
      const double a1 = static_cast<double>(p1[3]);
      std::uint8_t* dst = row.data() + static_cast<std::size_t>(x) * 4U;
      dst[3] = static_cast<std::uint8_t>(std::lround((a0 + a1) / 2.0));
      if (a0 + a1 > 0.0) {
        for (int channel = 0; channel < 3; ++channel) {
          dst[channel] = static_cast<std::uint8_t>(std::lround(
              (static_cast<double>(p0[channel]) * a0 + static_cast<double>(p1[channel]) * a1) /
              (a0 + a1)));
        }
      } else {
        dst[0] = dst[1] = dst[2] = 0;
      }
    }
  }
  return out;
}

[[nodiscard]] PixelBuffer box_reduce(const PixelBuffer& source, int halvings_x, int halvings_y,
                                     bool gray) {
  PixelBuffer reduced = box_halve_axis(source, halvings_x > 0, gray);
  if (halvings_x > 0) {
    --halvings_x;
  } else {
    --halvings_y;
  }
  while (halvings_x > 0) {
    reduced = box_halve_axis(reduced, true, gray);
    --halvings_x;
  }
  while (halvings_y > 0) {
    reduced = box_halve_axis(reduced, false, gray);
    --halvings_y;
  }
  return reduced;
}

}  // namespace
// Runtime 4CC (for channel-indexed tags like "Idx1"); matches af::tag4's layout.
[[nodiscard]] std::uint32_t tag_of(const std::string& s) {
  if (s.size() != 4) {
    return 0;
  }
  return (static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[0])) << 24U) |
         (static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[1])) << 16U) |
         (static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[2])) << 8U) |
         static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[3]));
}
[[nodiscard]] std::optional<EmbeddedOriginal> decode_embedded_original(
    std::span<const std::uint8_t> bytes, const Container& container, const af::AfClass& dybm) {
  const auto trace = [&](const char* what) {
    if (environment_variable_is_set("PATCHY_AF_TRACE")) {
      std::fprintf(stderr, "[af] placed original: %s\n", what);
    }
  };
  const auto* field = dybm.field(af::tag4("Bckg"));
  if (field == nullptr) {
    trace("no Bckg field");
    return std::nullopt;
  }
  const auto* embedded = std::get_if<af::AfEmbedded>(&field->value);
  if (embedded == nullptr || embedded->data.empty()) {
    trace("Bckg is not an embedded stream reference");
    return std::nullopt;
  }
  const auto stream = container.streams.find(embedded->data);
  if (stream == container.streams.end()) {
    trace("Bckg stream is missing from the container");
    return std::nullopt;
  }
  try {
    const auto blob = extract_stream(bytes, stream->second, embedded->data, nullptr);
    const af::AfDocument block = af::parse_tree(std::span<const std::uint8_t>(blob));
    if (block.root == nullptr) {
      trace("Bckg block tree has no root");
      return std::nullopt;
    }
    const auto* data_field = block.root->field(af::tag4("Data"));
    const auto* data = data_field != nullptr
                           ? std::get_if<std::vector<std::uint8_t>>(&data_field->value)
                           : nullptr;
    if (data == nullptr) {
      trace("Bckg block tree has no Data bytes");
      return std::nullopt;
    }
    const int orientation = static_cast<int>(block.root->int_field(af::tag4("TifO"), 1));
    auto pixels = decode_original_file_bytes(*data, orientation);
    if (!pixels) {
      trace("original file bytes did not decode (not JPEG/PNG?)");
      return std::nullopt;
    }
    EmbeddedOriginal original;
    original.pixels = std::move(*pixels);
    original.bytes = std::make_shared<const std::vector<std::uint8_t>>(*data);
    std::string path;
    if (const af::AfClass* source = block.root->child_class(af::tag4("DSrc"))) {
      path = source->string_field(af::tag4("Filn"));
    }
    const auto separator = path.find_last_of("/\\");
    original.name = separator == std::string::npos ? path : path.substr(separator + 1);
    return original;
  } catch (const std::exception& error) {
    if (environment_variable_is_set("PATCHY_AF_TRACE")) {
      std::fprintf(stderr, "[af] placed original: threw '%s'\n", error.what());
    }
    return std::nullopt;
  }
}
// Decode a DyBm bitmap. RGBA/gray/float/CMYK produce `rgba`; the mask formats
// (M8/M16) produce `mask`. Returns nullopt for empty/unknown/undecodable
// bitmaps, with a notice-ready reason in `fail_reason` when one is known.
[[nodiscard]] std::optional<DecodedBitmap> decode_bitmap(std::span<const std::uint8_t> bytes,
                                                         const Container& container,
                                                         const af::AfClass& dybm,
                                                         const std::string& layer_name,
                                                         std::vector<std::string>* notices,
                                                         std::string* fail_reason,
                                                         std::uint64_t max_plane_bytes) {
  const auto set_reason = [&](const char* reason) {
    if (fail_reason != nullptr) {
      *fail_reason = reason;
    }
  };
  const auto* frmt_field = dybm.field(af::tag4("Frmt"));
  std::int64_t format_id = -1;
  if (frmt_field != nullptr) {
    if (const auto* e = std::get_if<af::AfEnum>(&frmt_field->value)) {
      format_id = e->id;
    }
  }
  const RasterFormat kind = classify_format(format_id);
  if (environment_variable_is_set("PATCHY_AF_TRACE")) {
    std::fprintf(stderr, "[af] raster '%s': format %lld %s %lldx%lld\n", layer_name.c_str(),
                 static_cast<long long>(format_id),
                 kind == RasterFormat::Unsupported ? "(unsupported)" : "",
                 static_cast<long long>(dybm.int_field(af::tag4("BmpW"), 0)),
                 static_cast<long long>(dybm.int_field(af::tag4("BmpH"), 0)));
  }
  if (kind == RasterFormat::Unsupported) {
    return std::nullopt;
  }
  const std::int64_t width = dybm.int_field(af::tag4("BmpW"), 0);
  const std::int64_t height = dybm.int_field(af::tag4("BmpH"), 0);
  if (width <= 0 || height <= 0 || width > kMaxLayerSide || height > kMaxLayerSide) {
    return std::nullopt;
  }
  const bool is16 = kind == RasterFormat::RGBA16 || kind == RasterFormat::Gray16 ||
                    kind == RasterFormat::LabA16 || kind == RasterFormat::Mask16;
  const bool is_float = kind == RasterFormat::RGBAFloat;
  const std::size_t sample_bytes = is_float ? 4U : (is16 ? 2U : 1U);
  int channel_count = 4;
  switch (kind) {
    case RasterFormat::Gray8:
    case RasterFormat::Gray16: channel_count = 2; break;
    case RasterFormat::CMYKA8: channel_count = 5; break;
    case RasterFormat::Mask8:
    case RasterFormat::Mask16: channel_count = 1; break;
    default: break;
  }

  // Lazy placed-image DyBms (3.x saves of untouched placed images) store NO
  // base-plane fields at all - no TWi/THi tile grid, no Idx blocks, no Sta
  // codes on any channel - only the Bckg original plus a mip pyramid that
  // starts several levels down. The base is implicitly "every tile code 5":
  // size each plane's layout from the derived tile grid and fill it entirely
  // from the decoded original (the esdreika wild file holds 33 of these,
  // which previously imported as empty placeholders).
  const bool base_planes_absent = [&] {
    if (dybm.field(af::tag4("Bckg")) == nullptr) {
      return false;
    }
    for (int channel = 1; channel <= channel_count; ++channel) {
      const PlaneTags tags = base_plane_tags(channel);
      if (dybm.field(tags.status) != nullptr || dybm.field(tags.tiles_w) != nullptr ||
          dybm.field(tags.tiles_h) != nullptr || dybm.field(tags.index) != nullptr) {
        return false;
      }
    }
    return true;
  }();

  // First pass without a code-5 source; when some plane needs the placed
  // original, build per-channel sources (original image, else mip fallback)
  // and decode those planes again.
  std::vector<ChannelPlane> planes(static_cast<std::size_t>(channel_count));
  bool needs_original = false;
  if (base_planes_absent) {
    needs_original = true;
    // BmpW x BmpH alone sizes these planes (300000 a side): form the size in 64 bits.
    const std::uint64_t tiles_w =
        (static_cast<std::uint64_t>(width) * sample_bytes + kTileSize - 1) / kTileSize;
    const std::uint64_t tiles_h = (static_cast<std::uint64_t>(height) + kTileSize - 1) / kTileSize;
    const auto plane_bytes = checked_buffer_bytes(
        tiles_w * tiles_h, static_cast<std::uint64_t>(kTileSize) * kTileSize, max_plane_bytes);
    if (!plane_bytes.has_value()) {
      set_reason("has an invalid tile layout");
      return std::nullopt;
    }
    for (auto& plane : planes) {
      plane.width_bytes = static_cast<std::size_t>(tiles_w) * kTileSize;
      plane.rows = static_cast<std::size_t>(tiles_h) * kTileSize;
      plane.bytes.assign(*plane_bytes, 0);
    }
  }
  for (int channel = 1; !base_planes_absent && channel <= channel_count; ++channel) {
    const PlaneStatus status = decode_channel_plane(
        bytes, container, dybm, base_plane_tags(channel), sample_bytes, is_float, width, height,
        nullptr, planes[static_cast<std::size_t>(channel - 1)], max_plane_bytes);
    if (status == PlaneStatus::NeedsOriginal) {
      needs_original = true;
      continue;
    }
    if (status != PlaneStatus::Ok) {
      set_reason(status == PlaneStatus::UnknownCode ? "uses an unknown tile encoding"
                                                    : "has an invalid tile layout");
      return std::nullopt;
    }
  }
  std::shared_ptr<const std::vector<std::uint8_t>> pristine_original_bytes;
  std::string pristine_original_name;
  if (needs_original) {
    std::vector<ChannelPlane> sources;
    bool have_sources = false;
    const bool rgba_kind = kind == RasterFormat::RGBA8 || kind == RasterFormat::RGBA16 ||
                           kind == RasterFormat::RGBAFloat;
    if (rgba_kind) {
      const auto original = decode_embedded_original(bytes, container, dybm);
      if (original && environment_variable_is_set("PATCHY_AF_TRACE") &&
          (original->pixels.width() != static_cast<std::int32_t>(width) ||
           original->pixels.height() != static_cast<std::int32_t>(height))) {
        std::fprintf(stderr, "[af] placed original: decoded %dx%d but DyBm is %lldx%lld\n",
                     original->pixels.width(), original->pixels.height(),
                     static_cast<long long>(width), static_cast<long long>(height));
      }
      if (original && original->pixels.width() == static_cast<std::int32_t>(width) &&
          original->pixels.height() == static_cast<std::int32_t>(height)) {
        for (int channel = 1; channel <= channel_count; ++channel) {
          sources.push_back(source_plane_from_original(
              original->pixels, planes[static_cast<std::size_t>(channel - 1)], channel,
              sample_bytes, is_float));
        }
        have_sources = true;
        // Pristine when no base tile was hand-painted (no code-4 anywhere):
        // the layer IS the placed original, eligible for a smart-object wrap.
        bool any_stored = false;
        for (int channel = 1; channel <= channel_count && !any_stored; ++channel) {
          const auto* status = dybm.field(base_plane_tags(channel).status);
          const auto* codes = status != nullptr
                                  ? std::get_if<std::vector<std::int64_t>>(&status->value)
                                  : nullptr;
          if (codes != nullptr) {
            for (const auto code : *codes) {
              if (code == kTileStored) {
                any_stored = true;
                break;
              }
            }
          }
        }
        if (!any_stored) {
          pristine_original_bytes = original->bytes;
          pristine_original_name = original->name;
        }
      }
    }
    if (!have_sources) {
      sources.clear();
      if (mip_source_planes(bytes, container, dybm, channel_count, sample_bytes, is_float, width,
                            height, planes, sources, max_plane_bytes)) {
        have_sources = true;
        if (notices != nullptr) {
          notices->push_back("Layer '" + layer_name +
                             "': its placed original image could not be decoded; imported at "
                             "half resolution from the stored preview");
        }
      }
    }
    if (!have_sources) {
      set_reason("references a placed original image that could not be decoded");
      return std::nullopt;
    }
    if (base_planes_absent) {
      // No Sta codes to walk: the whole base level comes from the source.
      planes = std::move(sources);
    } else {
      for (int channel = 1; channel <= channel_count; ++channel) {
        const PlaneStatus status = decode_channel_plane(
            bytes, container, dybm, base_plane_tags(channel), sample_bytes, is_float, width,
            height, &sources[static_cast<std::size_t>(channel - 1)],
            planes[static_cast<std::size_t>(channel - 1)], max_plane_bytes);
        if (status != PlaneStatus::Ok) {
          set_reason("has an invalid tile layout");
          return std::nullopt;
        }
      }
    }
  }
  for (const auto& plane : planes) {
    if (static_cast<std::size_t>(width) * sample_bytes > plane.width_bytes ||
        static_cast<std::size_t>(height) > plane.rows) {
      set_reason("has an invalid tile layout");
      return std::nullopt;
    }
  }

  const auto sample = [&](int ch, std::int32_t x, std::int32_t y) -> float {
    return sample_plane(planes[static_cast<std::size_t>(ch)], sample_bytes, is_float, x, y);
  };
  const auto to_byte = [](float v) {
    return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0F, 255.0F)));
  };

  // New-format bitmaps can embed real ICC profile bytes (Prof -> ICCP with a
  // per-space child; CMYP is the CMYK space). When present, convert CMYK
  // through it (the PSD path's lcms2 transform; inputs there are INVERTED ink,
  // .af stores straight ink, so invert on the way in).
  std::optional<CmykToRgbTransform> cmyk_transform;
  if (kind == RasterFormat::CMYKA8) {
    if (const af::AfClass* profiles = dybm.child_class(af::tag4("Prof"))) {
      if (const af::AfClass* cmyk_profile = profiles->child_class(af::tag4("CMYP"))) {
        if (const auto* data_field = cmyk_profile->field(af::tag4("Data"))) {
          if (const auto* data = std::get_if<std::vector<std::uint8_t>>(&data_field->value)) {
            cmyk_transform =
                CmykToRgbTransform::from_icc_profile(std::span<const std::uint8_t>(*data));
          }
        }
      }
    }
  }

  DecodedBitmap decoded;
  decoded.original_bytes = std::move(pristine_original_bytes);
  decoded.original_name = std::move(pristine_original_name);
  if (kind == RasterFormat::Mask8 || kind == RasterFormat::Mask16) {
    PixelBuffer mask(static_cast<std::int32_t>(width), static_cast<std::int32_t>(height),
                     PixelFormat::gray8());
    for (std::int32_t y = 0; y < static_cast<std::int32_t>(height); ++y) {
      auto row = mask.row(y);
      for (std::int32_t x = 0; x < static_cast<std::int32_t>(width); ++x) {
        row[static_cast<std::size_t>(x)] = to_byte(sample(0, x, y));
      }
    }
    decoded.mask = std::move(mask);
    return decoded;
  }

  PixelBuffer buffer(static_cast<std::int32_t>(width), static_cast<std::int32_t>(height),
                     PixelFormat::rgba8());
  if (kind == RasterFormat::LabA16) {
    // The wire is the ICC v4 Lab16 PCS encoding; feed lcms TYPE_Lab_16 rows.
    const auto lab_transform = LabToRgbTransform::create();
    if (!lab_transform) {
      set_reason("could not build the Lab color transform");
      return std::nullopt;
    }
    const auto raw_u16 = [&](int channel, std::int32_t x, std::int32_t y) {
      const ChannelPlane& plane = planes[static_cast<std::size_t>(channel)];
      const std::uint8_t* p = plane.bytes.data() +
                              static_cast<std::size_t>(y) * plane.width_bytes +
                              static_cast<std::size_t>(x) * 2U;
      return static_cast<std::uint16_t>(p[0] | (static_cast<std::uint16_t>(p[1]) << 8U));
    };
    std::vector<std::uint16_t> lab(static_cast<std::size_t>(width) * 3U);
    std::vector<std::uint8_t> rgb(static_cast<std::size_t>(width) * 3U);
    for (std::int32_t y = 0; y < static_cast<std::int32_t>(height); ++y) {
      for (std::int32_t x = 0; x < static_cast<std::int32_t>(width); ++x) {
        for (int channel = 0; channel < 3; ++channel) {
          lab[static_cast<std::size_t>(x) * 3U + static_cast<std::size_t>(channel)] =
              raw_u16(channel, x, y);
        }
      }
      lab_transform->convert(lab.data(), rgb.data(), static_cast<std::size_t>(width));
      auto row = buffer.row(y);
      for (std::int32_t x = 0; x < static_cast<std::int32_t>(width); ++x) {
        std::uint8_t* out = row.data() + static_cast<std::size_t>(x) * 4U;
        std::memcpy(out, rgb.data() + static_cast<std::size_t>(x) * 3U, 3U);
        out[3] = to_byte(sample(3, x, y));
      }
    }
    decoded.rgba = std::move(buffer);
    return decoded;
  }
  if (cmyk_transform) {
    std::vector<std::uint8_t> inverted(static_cast<std::size_t>(width) * 4U);
    std::vector<std::uint8_t> rgb(static_cast<std::size_t>(width) * 3U);
    for (std::int32_t y = 0; y < static_cast<std::int32_t>(height); ++y) {
      for (std::int32_t x = 0; x < static_cast<std::int32_t>(width); ++x) {
        for (int channel = 0; channel < 4; ++channel) {
          inverted[static_cast<std::size_t>(x) * 4U + static_cast<std::size_t>(channel)] =
              static_cast<std::uint8_t>(255 - to_byte(sample(channel, x, y)));
        }
      }
      cmyk_transform->convert(inverted.data(), rgb.data(), static_cast<std::size_t>(width));
      auto row = buffer.row(y);
      for (std::int32_t x = 0; x < static_cast<std::int32_t>(width); ++x) {
        std::uint8_t* out = row.data() + static_cast<std::size_t>(x) * 4U;
        std::memcpy(out, rgb.data() + static_cast<std::size_t>(x) * 3U, 3U);
        out[3] = to_byte(sample(4, x, y));
      }
    }
    decoded.rgba = std::move(buffer);
    return decoded;
  }
  for (std::int32_t y = 0; y < static_cast<std::int32_t>(height); ++y) {
    auto row = buffer.row(y);
    for (std::int32_t x = 0; x < static_cast<std::int32_t>(width); ++x) {
      std::uint8_t* out = row.data() + static_cast<std::size_t>(x) * 4U;
      switch (kind) {
        case RasterFormat::Gray8:
        case RasterFormat::Gray16: {
          const std::uint8_t g = to_byte(sample(0, x, y));
          out[0] = out[1] = out[2] = g;
          out[3] = to_byte(sample(1, x, y));
          break;
        }
        case RasterFormat::RGBAFloat:
          out[0] = linear_to_srgb8(sample(0, x, y));
          out[1] = linear_to_srgb8(sample(1, x, y));
          out[2] = linear_to_srgb8(sample(2, x, y));
          out[3] = to_byte(sample(3, x, y) * 255.0F);
          break;
        case RasterFormat::CMYKA8: {
          // No ICC bytes in the file: the naive ink mix (same fallback the PSD
          // reader uses for profile-less CMYK). Channels are straight ink.
          const float c = sample(0, x, y) / 255.0F;
          const float m = sample(1, x, y) / 255.0F;
          const float ye = sample(2, x, y) / 255.0F;
          const float k = sample(3, x, y) / 255.0F;
          out[0] = to_byte(255.0F * (1.0F - c) * (1.0F - k));
          out[1] = to_byte(255.0F * (1.0F - m) * (1.0F - k));
          out[2] = to_byte(255.0F * (1.0F - ye) * (1.0F - k));
          out[3] = to_byte(sample(4, x, y));
          break;
        }
        default:
          for (int c = 0; c < 4; ++c) {
            out[c] = to_byte(sample(c, x, y));
          }
          break;
      }
    }
  }
  decoded.rgba = std::move(buffer);
  decoded.approximate_color = kind == RasterFormat::CMYKA8;
  return decoded;
}
// Read the [tx, ty] integer origin for a layer from a pure-translation
// effective transform (node_xfrm); untransformed nodes sit at the document
// origin. Returns nullopt for a non-trivial affine (scale/rotate), which
// rasterizes through resample_affine. BitI is deliberately NOT a placement
// source: it is the bitmap's used/dirty sub-rect (akiko's full-canvas photo
// carries BitI y0=635 yet belongs at 0,0; every corpus doc that placed
// correctly did so through Xfrm or at origin).
[[nodiscard]] std::optional<std::pair<std::int32_t, std::int32_t>> layer_origin(
    const std::vector<double>& xfrm) {
  if (xfrm.size() == 6) {
    const double a = xfrm[0];
    const double b = xfrm[1];
    const double c = xfrm[3];
    const double d = xfrm[4];
    if (std::abs(a - 1.0) < 1e-6 && std::abs(d - 1.0) < 1e-6 && std::abs(b) < 1e-6 &&
        std::abs(c) < 1e-6) {
      return std::pair<std::int32_t, std::int32_t>{static_cast<std::int32_t>(std::lround(xfrm[2])),
                                                   static_cast<std::int32_t>(std::lround(xfrm[5]))};
    }
    return std::nullopt;
  }
  return std::pair<std::int32_t, std::int32_t>{0, 0};
}
// Rasterize `source` through the affine Xfrm [a, b, tx, c, d, ty]
// (dest = [a b; c d] * src + [tx, ty]; the convention and the bilinear
// premultiplied-accumulation edges are pinned against Affinity's own PNG
// export of a rotated+scaled raster, RMSE 0.01) into an axis-aligned buffer
// at the returned origin. `gray_mask` sources sample edge-clamped single-byte
// planes (layer masks). Returns nullopt for degenerate or implausible results.
[[nodiscard]] std::optional<PlacedRaster> resample_affine(const PixelBuffer& source,
                                                          const std::vector<double>& xfrm,
                                                          bool gray_mask) {
  if (xfrm.size() != 6 || source.width() <= 0 || source.height() <= 0) {
    return std::nullopt;
  }
  const double a = xfrm[0];
  const double b = xfrm[1];
  const double tx = xfrm[2];
  const double c = xfrm[3];
  const double d = xfrm[4];
  const double ty = xfrm[5];
  const double det = a * d - b * c;
  if (!std::isfinite(det) || std::abs(det) < 1e-9) {
    return std::nullopt;
  }
  const auto source_w = static_cast<double>(source.width());
  const auto source_h = static_cast<double>(source.height());
  double min_x = tx;
  double max_x = tx;
  double min_y = ty;
  double max_y = ty;
  const std::array<std::pair<double, double>, 3> corners = {
      std::pair<double, double>{source_w, 0.0}, {0.0, source_h}, {source_w, source_h}};
  for (const auto& [sx, sy] : corners) {
    const double px = a * sx + b * sy + tx;
    const double py = c * sx + d * sy + ty;
    min_x = std::min(min_x, px);
    max_x = std::max(max_x, px);
    min_y = std::min(min_y, py);
    max_y = std::max(max_y, py);
  }
  if (!std::isfinite(min_x) || !std::isfinite(max_x) || !std::isfinite(min_y) ||
      !std::isfinite(max_y)) {
    return std::nullopt;
  }
  const double origin_x = std::floor(min_x);
  const double origin_y = std::floor(min_y);
  const double extent_w = std::ceil(max_x) - origin_x;
  const double extent_h = std::ceil(max_y) - origin_y;
  if (extent_w < 1.0 || extent_h < 1.0 || extent_w > kMaxLayerSide || extent_h > kMaxLayerSide ||
      extent_w * extent_h > 268435456.0) {  // cap the buffer at 1 GiB of RGBA
    return std::nullopt;
  }
  const auto out_w = static_cast<std::int32_t>(extent_w);
  const auto out_h = static_cast<std::int32_t>(extent_h);
  const double inv_a = d / det;
  const double inv_b = -b / det;
  const double inv_c = -c / det;
  const double inv_d = a / det;

  // Minification quality: bilinear alone reads a 2x2 neighborhood, so a
  // strongly downscaled placement (esdreika places 4096px texture originals at
  // 1/32 scale) shimmers with aliasing that Affinity's own mip-filtered render
  // does not show. Box-reduce the source by powers of two per axis until the
  // remaining scale is above one half, then bilinear as before. Alpha-weighted
  // averaging keeps transparent padding from bleeding dark fringes in.
  const double scale_x = std::hypot(a, c);
  const double scale_y = std::hypot(b, d);
  int halvings_x = 0;
  int halvings_y = 0;
  while (scale_x * static_cast<double>(1 << (halvings_x + 1)) <= 1.0 &&
         (source.width() >> (halvings_x + 1)) >= 1) {
    ++halvings_x;
  }
  while (scale_y * static_cast<double>(1 << (halvings_y + 1)) <= 1.0 &&
         (source.height() >> (halvings_y + 1)) >= 1) {
    ++halvings_y;
  }
  PixelBuffer reduced_storage;
  const PixelBuffer* sampled = &source;
  if (halvings_x > 0 || halvings_y > 0) {
    reduced_storage = box_reduce(source, halvings_x, halvings_y, gray_mask);
    sampled = &reduced_storage;
  }
  const double factor_x = static_cast<double>(1 << halvings_x);
  const double factor_y = static_cast<double>(1 << halvings_y);

  PlacedRaster placed;
  placed.x = static_cast<std::int32_t>(origin_x);
  placed.y = static_cast<std::int32_t>(origin_y);
  placed.pixels = PixelBuffer(out_w, out_h, gray_mask ? PixelFormat::gray8() : PixelFormat::rgba8());
  for (std::int32_t y = 0; y < out_h; ++y) {
    auto row = placed.pixels.row(y);
    for (std::int32_t x = 0; x < out_w; ++x) {
      const double rel_x = origin_x + x + 0.5 - tx;
      const double rel_y = origin_y + y + 0.5 - ty;
      const double sx = (inv_a * rel_x + inv_b * rel_y) / factor_x - 0.5;
      const double sy = (inv_c * rel_x + inv_d * rel_y) / factor_y - 0.5;
      const double fx0 = std::floor(sx);
      const double fy0 = std::floor(sy);
      const auto x0 = static_cast<std::int32_t>(fx0);
      const auto y0 = static_cast<std::int32_t>(fy0);
      const double fx = sx - fx0;
      const double fy = sy - fy0;
      const std::array<std::tuple<std::int32_t, std::int32_t, double>, 4> taps = {
          std::tuple<std::int32_t, std::int32_t, double>{x0, y0, (1.0 - fx) * (1.0 - fy)},
          {x0 + 1, y0, fx * (1.0 - fy)},
          {x0, y0 + 1, (1.0 - fx) * fy},
          {x0 + 1, y0 + 1, fx * fy}};
      if (gray_mask) {
        double accumulated = 0.0;
        for (const auto& [nx, ny, weight] : taps) {
          const std::int32_t cx = std::clamp(nx, 0, sampled->width() - 1);
          const std::int32_t cy = std::clamp(ny, 0, sampled->height() - 1);
          accumulated += static_cast<double>(sampled->pixel(cx, cy)[0]) * weight;
        }
        row[static_cast<std::size_t>(x)] =
            static_cast<std::uint8_t>(std::lround(std::clamp(accumulated, 0.0, 255.0)));
        continue;
      }
      double acc_r = 0.0;
      double acc_g = 0.0;
      double acc_b = 0.0;
      double acc_a = 0.0;
      for (const auto& [nx, ny, weight] : taps) {
        if (nx < 0 || ny < 0 || nx >= sampled->width() || ny >= sampled->height()) {
          continue;  // outside the source: transparent
        }
        const std::uint8_t* p = sampled->pixel(nx, ny);
        const double alpha_weight = static_cast<double>(p[3]) / 255.0 * weight;
        acc_r += static_cast<double>(p[0]) * alpha_weight;
        acc_g += static_cast<double>(p[1]) * alpha_weight;
        acc_b += static_cast<double>(p[2]) * alpha_weight;
        acc_a += static_cast<double>(p[3]) * weight;
      }
      std::uint8_t* out = row.data() + static_cast<std::size_t>(x) * 4U;
      if (acc_a > 0.5) {
        const double scale = 255.0 / acc_a;
        out[0] = static_cast<std::uint8_t>(std::clamp<long>(std::lround(acc_r * scale), 0, 255));
        out[1] = static_cast<std::uint8_t>(std::clamp<long>(std::lround(acc_g * scale), 0, 255));
        out[2] = static_cast<std::uint8_t>(std::clamp<long>(std::lround(acc_b * scale), 0, 255));
        out[3] = static_cast<std::uint8_t>(std::lround(std::min(255.0, acc_a)));
      } else {
        out[0] = out[1] = out[2] = out[3] = 0;
      }
    }
  }
  return placed;
}

}  // namespace patchy::af::detail
