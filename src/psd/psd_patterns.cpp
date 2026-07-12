#include "psd/psd_patterns.hpp"

#include "color/color_management.hpp"
#include "psd/psd_binary.hpp"
#include "psd/psd_descriptor.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace patchy::psd {

namespace {

constexpr std::uint32_t kPatternVersion = 1;
constexpr std::uint32_t kVirtualMemoryArrayVersion = 3;
// Photoshop declares 24 "max channels" regardless of image mode; the slot list
// is max channels + one user-mask slot + one sheet-transparency slot.
constexpr std::uint32_t kDeclaredMaxChannels = 24;
constexpr std::size_t kChannelHeaderBytes = 23;  // depth u32 + rect + depth u16 + compression u8

enum PatternImageMode : std::uint32_t {
  kModeBitmap = 0,
  kModeGrayscale = 1,
  kModeIndexed = 2,
  kModeRgb = 3,
  kModeCmyk = 4,
};

struct DecodedPlane {
  std::int32_t top{0};
  std::int32_t left{0};
  std::int32_t width{0};
  std::int32_t height{0};
  std::vector<std::uint8_t> samples;  // 8-bit, row-major width*height
};

[[nodiscard]] std::uint8_t deep_sample_to_byte(std::uint32_t value16) noexcept {
  // Photoshop 16-bit uses the 0..32768 scale.
  return static_cast<std::uint8_t>(std::min<std::uint32_t>(255U, (value16 * 255U + 16384U) / 32768U));
}

// Reads one virtual-memory-array slot; returns false for unwritten/empty slots.
bool read_plane(BigEndianReader& reader, DecodedPlane& plane) {
  const auto written = reader.read_u32();
  if (written == 0U) {
    return false;
  }
  const auto length = reader.read_u32();
  if (length == 0U) {
    return false;
  }
  if (length < kChannelHeaderBytes || length > reader.remaining()) {
    throw std::runtime_error("PSD pattern channel is truncated");
  }
  const auto end_position = reader.position() + length;
  const auto depth = reader.read_u32();
  const auto top = static_cast<std::int32_t>(reader.read_u32());
  const auto left = static_cast<std::int32_t>(reader.read_u32());
  const auto bottom = static_cast<std::int32_t>(reader.read_u32());
  const auto right = static_cast<std::int32_t>(reader.read_u32());
  const auto pixel_depth = reader.read_u16();
  const auto compression = reader.read_u8();
  const auto width = right - left;
  const auto height = bottom - top;
  if (width <= 0 || height <= 0 || width > 30000 || height > 30000) {
    throw std::runtime_error("PSD pattern channel rectangle is invalid");
  }
  if ((depth != 8U && depth != 16U) || (pixel_depth != 8U && pixel_depth != 16U)) {
    throw std::runtime_error("PSD pattern channel depth is unsupported");
  }
  const auto bytes_per_sample = static_cast<std::size_t>(pixel_depth / 8U);
  const auto row_bytes = static_cast<std::size_t>(width) * bytes_per_sample;
  const auto expected = row_bytes * static_cast<std::size_t>(height);
  const auto data_length = length - kChannelHeaderBytes;
  std::vector<std::uint8_t> raw;
  if (compression == 0U) {
    if (data_length < expected) {
      throw std::runtime_error("PSD pattern channel data is truncated");
    }
    raw = reader.read_bytes(expected);
  } else if (compression == 1U) {
    // PackBits with the per-row big-endian u16 count table (the channel-image
    // convention).
    const auto table_bytes = static_cast<std::size_t>(height) * 2U;
    if (data_length < table_bytes) {
      throw std::runtime_error("PSD pattern RLE table is truncated");
    }
    std::vector<std::uint16_t> counts(static_cast<std::size_t>(height));
    for (auto& count : counts) {
      count = reader.read_u16();
    }
    raw.reserve(expected);
    for (const auto count : counts) {
      const auto encoded = reader.read_bytes(count);
      auto row = decode_packbits(encoded, row_bytes);
      raw.insert(raw.end(), row.begin(), row.end());
    }
  } else {
    throw std::runtime_error("PSD pattern compression mode is unsupported");
  }

  plane.top = top;
  plane.left = left;
  plane.width = width;
  plane.height = height;
  plane.samples.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
  if (bytes_per_sample == 1U) {
    std::copy(raw.begin(), raw.begin() + static_cast<std::ptrdiff_t>(plane.samples.size()),
              plane.samples.begin());
  } else {
    for (std::size_t index = 0; index < plane.samples.size(); ++index) {
      const auto value = (static_cast<std::uint32_t>(raw[index * 2U]) << 8U) | raw[index * 2U + 1U];
      plane.samples[index] = deep_sample_to_byte(value);
    }
  }
  // Skip whatever trails the decoded data inside this slot (defensive).
  if (reader.position() < end_position) {
    reader.skip(end_position - reader.position());
  }
  return true;
}

[[nodiscard]] std::uint8_t plane_sample(const DecodedPlane& plane, std::int32_t x, std::int32_t y,
                                        std::uint8_t fallback) noexcept {
  const auto px = x - plane.left;
  const auto py = y - plane.top;
  if (plane.samples.empty() || px < 0 || py < 0 || px >= plane.width || py >= plane.height) {
    return fallback;
  }
  return plane.samples[static_cast<std::size_t>(py) * static_cast<std::size_t>(plane.width) +
                       static_cast<std::size_t>(px)];
}

[[nodiscard]] std::uint8_t naive_cmyk_component(std::uint8_t colorant_inverted,
                                                std::uint8_t black_inverted) noexcept {
  // PSD channels store inverted ink (255 = no ink); same mix as the pixel decode.
  return static_cast<std::uint8_t>((static_cast<int>(colorant_inverted) * static_cast<int>(black_inverted)) /
                                   255);
}

// Parses one pattern starting at the reader's position; the reader is positioned
// at the pattern's u32 length field. Returns nullopt for undecodable content.
std::optional<PatternResource> parse_single_pattern(BigEndianReader& reader,
                                                    const CmykToRgbTransform* cmyk_icc) {
  const auto declared_length = reader.read_u32();
  if (declared_length < 16U || declared_length > reader.remaining()) {
    throw std::runtime_error("PSD pattern length is invalid");
  }
  const auto pattern_end = reader.position() + declared_length;
  std::optional<PatternResource> result;
  try {
    const auto version = reader.read_u32();
    const auto mode = reader.read_u32();
    const auto height = static_cast<std::int32_t>(reader.read_u16());
    const auto width = static_cast<std::int32_t>(reader.read_u16());
    auto pattern_name = read_descriptor_unicode_string(reader);
    const auto id_length = reader.read_u8();
    const auto id_bytes = reader.read_bytes(id_length);
    std::string pattern_id(id_bytes.begin(), id_bytes.end());
    while (!pattern_id.empty() && pattern_id.back() == '\0') {
      pattern_id.pop_back();
    }

    std::vector<std::uint8_t> color_table;
    if (version == kPatternVersion && mode == kModeIndexed) {
      color_table = reader.read_bytes(256U * 3U);
    }

    const auto supported_mode =
        mode == kModeGrayscale || mode == kModeIndexed || mode == kModeRgb || mode == kModeCmyk;
    if (version == kPatternVersion && supported_mode && width > 0 && height > 0 && width <= 30000 &&
        height <= 30000 && !pattern_id.empty()) {
      const auto vma_version = reader.read_u32();
      const auto vma_length = reader.read_u32();
      if (vma_version != kVirtualMemoryArrayVersion || vma_length > reader.remaining()) {
        throw std::runtime_error("PSD pattern VMA header is invalid");
      }
      const auto vma_end = reader.position() + vma_length;
      reader.skip(16U);  // VMA rectangle (matches the pattern point in captures)
      const auto declared_channels = reader.read_u32();
      if (declared_channels > 64U) {
        throw std::runtime_error("PSD pattern channel count is invalid");
      }
      const auto slot_count = declared_channels + 2U;
      const auto color_channel_count =
          mode == kModeRgb ? 3U : (mode == kModeCmyk ? 4U : 1U);

      std::vector<DecodedPlane> color_planes(color_channel_count);
      std::vector<bool> color_present(color_channel_count, false);
      DecodedPlane alpha_plane;
      bool alpha_present = false;
      for (std::uint32_t slot = 0; slot < slot_count && reader.position() < vma_end; ++slot) {
        DecodedPlane plane;
        if (!read_plane(reader, plane)) {
          continue;
        }
        if (slot < color_channel_count) {
          color_planes[slot] = std::move(plane);
          color_present[slot] = true;
        } else if (slot == declared_channels + 1U) {
          alpha_plane = std::move(plane);
          alpha_present = true;
        }
      }
      if (reader.position() < vma_end) {
        reader.skip(vma_end - reader.position());
      }

      const auto any_color =
          std::any_of(color_present.begin(), color_present.end(), [](bool present) { return present; });
      if (any_color) {
        PatternResource resource;
        resource.id = std::move(pattern_id);
        resource.name = std::move(pattern_name);
        resource.provenance = PatternProvenance::ImportedRaw;
        resource.tile = PixelBuffer(width, height, PixelFormat::rgba8());
        for (std::int32_t y = 0; y < height; ++y) {
          auto* row = resource.tile.pixel(0, y);
          for (std::int32_t x = 0; x < width; ++x) {
            auto* px = row + static_cast<std::size_t>(x) * 4U;
            const auto alpha = alpha_present ? plane_sample(alpha_plane, x, y, 255U) : 255U;
            switch (mode) {
              case kModeGrayscale: {
                const auto gray = plane_sample(color_planes[0], x, y, 0U);
                px[0] = gray;
                px[1] = gray;
                px[2] = gray;
                break;
              }
              case kModeIndexed: {
                const auto index = plane_sample(color_planes[0], x, y, 0U);
                px[0] = color_table[static_cast<std::size_t>(index) * 3U];
                px[1] = color_table[static_cast<std::size_t>(index) * 3U + 1U];
                px[2] = color_table[static_cast<std::size_t>(index) * 3U + 2U];
                break;
              }
              case kModeCmyk: {
                const auto cyan = plane_sample(color_planes[0], x, y, 255U);
                const auto magenta = plane_sample(color_planes[1], x, y, 255U);
                const auto yellow = plane_sample(color_planes[2], x, y, 255U);
                const auto black = plane_sample(color_planes[3], x, y, 255U);
                if (cmyk_icc != nullptr) {
                  const auto rgb = cmyk_icc->convert_single(cyan, magenta, yellow, black);
                  px[0] = rgb.red;
                  px[1] = rgb.green;
                  px[2] = rgb.blue;
                } else {
                  px[0] = naive_cmyk_component(cyan, black);
                  px[1] = naive_cmyk_component(magenta, black);
                  px[2] = naive_cmyk_component(yellow, black);
                }
                break;
              }
              case kModeRgb:
              default: {
                px[0] = plane_sample(color_planes[0], x, y, 0U);
                px[1] = plane_sample(color_planes[1], x, y, 0U);
                px[2] = plane_sample(color_planes[2], x, y, 0U);
                break;
              }
            }
            px[3] = static_cast<std::uint8_t>(alpha);
          }
        }
        result = std::move(resource);
      }
    }
  } catch (const std::exception&) {
    result.reset();  // undecodable pattern: the raw block preserves it
  }

  // Always resynchronize to the declared pattern end plus its 4-byte pad.
  if (reader.position() < pattern_end) {
    reader.skip(pattern_end - reader.position());
  }
  const auto consumed = 4U + static_cast<std::size_t>(declared_length);
  const auto padding = (4U - (consumed % 4U)) % 4U;
  reader.skip(std::min<std::size_t>(padding, reader.remaining()));
  return result;
}

}  // namespace

std::vector<PatternResource> parse_patterns_block(std::span<const std::uint8_t> payload,
                                                  const CmykToRgbTransform* cmyk_icc) {
  std::vector<PatternResource> resources;
  BigEndianReader reader(payload);
  try {
    while (reader.remaining() >= 16U) {
      auto resource = parse_single_pattern(reader, cmyk_icc);
      if (resource.has_value()) {
        resources.push_back(std::move(*resource));
      }
    }
  } catch (const std::exception&) {
    // Malformed block: keep whatever decoded cleanly; the raw bytes stay preserved.
  }
  return resources;
}

std::vector<std::string> pattern_ids_in_block(std::span<const std::uint8_t> payload) {
  std::vector<std::string> ids;
  BigEndianReader reader(payload);
  try {
    while (reader.remaining() >= 16U) {
      const auto declared_length = reader.read_u32();
      if (declared_length < 16U || declared_length > reader.remaining()) {
        break;
      }
      const auto pattern_end = reader.position() + declared_length;
      reader.skip(8U);  // version + mode
      reader.skip(4U);  // point
      const auto name_units = reader.read_u32();
      if (static_cast<std::size_t>(name_units) * 2U > reader.remaining()) {
        break;
      }
      reader.skip(static_cast<std::size_t>(name_units) * 2U);
      const auto id_length = reader.read_u8();
      const auto id_bytes = reader.read_bytes(id_length);
      std::string id(id_bytes.begin(), id_bytes.end());
      while (!id.empty() && id.back() == '\0') {
        id.pop_back();
      }
      if (!id.empty()) {
        ids.push_back(std::move(id));
      }
      if (reader.position() < pattern_end) {
        reader.skip(pattern_end - reader.position());
      }
      const auto consumed = 4U + static_cast<std::size_t>(declared_length);
      const auto padding = (4U - (consumed % 4U)) % 4U;
      reader.skip(std::min<std::size_t>(padding, reader.remaining()));
    }
  } catch (const std::exception&) {
  }
  return ids;
}

std::vector<std::uint8_t> serialize_patterns_block(std::span<const PatternResource> patterns) {
  BigEndianWriter block;
  for (const auto& resource : patterns) {
    const auto& tile = resource.tile;
    if (tile.empty() || tile.format() != PixelFormat::rgba8() || resource.id.empty() ||
        resource.id.size() > 255U || tile.width() > 30000 || tile.height() > 30000) {
      continue;
    }
    const auto width = tile.width();
    const auto height = tile.height();
    const auto pixel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);

    bool has_transparency = false;
    for (std::size_t index = 0; index < pixel_count && !has_transparency; ++index) {
      has_transparency = tile.data()[index * 4U + 3U] != 255U;
    }

    // Channel planes: R, G, B in the first slots; transparency in the last slot.
    const auto plane_bytes = pixel_count;
    const auto channel_slot_bytes = 8U + kChannelHeaderBytes + plane_bytes;  // written+length+header+data
    const auto written_channels = 3U + (has_transparency ? 1U : 0U);
    const auto slot_count = kDeclaredMaxChannels + 2U;
    const auto unwritten_slots = slot_count - written_channels;
    const auto vma_length = 16U + 4U + written_channels * channel_slot_bytes + unwritten_slots * 4U;

    BigEndianWriter pattern;
    pattern.write_u32(kPatternVersion);
    pattern.write_u32(kModeRgb);
    pattern.write_u16(static_cast<std::uint16_t>(height));
    pattern.write_u16(static_cast<std::uint16_t>(width));
    write_descriptor_unicode_string(pattern, resource.name);
    pattern.write_u8(static_cast<std::uint8_t>(resource.id.size()));
    pattern.write_bytes(
        std::span(reinterpret_cast<const std::uint8_t*>(resource.id.data()), resource.id.size()));
    pattern.write_u32(kVirtualMemoryArrayVersion);
    pattern.write_u32(static_cast<std::uint32_t>(vma_length));
    pattern.write_u32(0);
    pattern.write_u32(0);
    pattern.write_u32(static_cast<std::uint32_t>(height));
    pattern.write_u32(static_cast<std::uint32_t>(width));
    pattern.write_u32(kDeclaredMaxChannels);

    const auto write_plane = [&pattern, &tile, width, height, pixel_count](std::size_t component) {
      pattern.write_u32(1);  // written
      pattern.write_u32(static_cast<std::uint32_t>(kChannelHeaderBytes + pixel_count));
      pattern.write_u32(8);  // pixel depth
      pattern.write_u32(0);
      pattern.write_u32(0);
      pattern.write_u32(static_cast<std::uint32_t>(height));
      pattern.write_u32(static_cast<std::uint32_t>(width));
      pattern.write_u16(8);  // pixel depth again
      pattern.write_u8(0);   // raw compression, PS 27.8's own choice for small tiles
      std::vector<std::uint8_t> plane(pixel_count);
      const auto data = tile.data();
      for (std::size_t index = 0; index < pixel_count; ++index) {
        plane[index] = data[index * 4U + component];
      }
      pattern.write_bytes(plane);
    };

    write_plane(0);
    write_plane(1);
    write_plane(2);
    for (std::uint32_t slot = 3; slot < slot_count; ++slot) {
      if (has_transparency && slot == kDeclaredMaxChannels + 1U) {
        write_plane(3);
      } else {
        pattern.write_u32(0);  // unwritten slot
      }
    }

    const auto& pattern_bytes = pattern.bytes();
    block.write_u32(static_cast<std::uint32_t>(pattern_bytes.size()));
    block.write_bytes(pattern_bytes);
    const auto consumed = 4U + pattern_bytes.size();
    const auto padding = (4U - (consumed % 4U)) % 4U;
    for (std::size_t i = 0; i < padding; ++i) {
      block.write_u8(0);
    }
  }
  return block.bytes();
}

}  // namespace patchy::psd
