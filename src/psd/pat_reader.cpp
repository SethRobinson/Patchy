#include "psd/pat_reader.hpp"

#include "psd/psd_binary.hpp"
#include "psd/psd_descriptor.hpp"
#include "psd/psd_patterns.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace patchy::psd {

namespace {

constexpr std::uint16_t kPatFileVersion = 1;
constexpr std::uint32_t kPatternVersion = 1;
constexpr std::uint32_t kVirtualMemoryArrayVersion = 3;
constexpr std::uint32_t kModeGrayscale = 1;
constexpr std::uint32_t kModeIndexed = 2;
constexpr std::uint32_t kModeRgb = 3;
constexpr std::uint32_t kModeCmyk = 4;
constexpr std::uint32_t kMaxPatternCount = 4096;
constexpr std::int32_t kMaxPatternDimension = 16384;
constexpr std::uint64_t kMaxPatternPixels = 8ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaxTotalPatternPixels = 16ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaxAttemptedPlaneSamples = 80ULL * 1024ULL * 1024ULL;
constexpr std::size_t kMaxPatternRecordBytes = 32ULL * 1024ULL * 1024ULL;
constexpr std::size_t kIndexedColorTableBytes = 256U * 3U;
constexpr std::size_t kChannelHeaderBytes = 23U;

struct PatRecord {
  std::size_t record_start{0};
  std::size_t indexed_table_start{0};
  std::size_t vma_start{0};
  std::size_t record_end{0};
  std::uint32_t version{0};
  std::uint32_t mode{0};
  std::uint32_t vma_version{0};
  std::int32_t width{0};
  std::int32_t height{0};
  std::uint16_t colors_used{0};
  std::uint16_t transparent_index{std::numeric_limits<std::uint16_t>::max()};
  std::string name;
  std::string id;
};

struct IndexedPlane {
  std::int32_t top{0};
  std::int32_t left{0};
  std::int32_t width{0};
  std::int32_t height{0};
  std::vector<std::uint8_t> samples;
};

struct PatPlaneInspection {
  bool sheet_alpha{false};
  std::uint64_t decode_samples{0};
};

[[nodiscard]] bool supported_mode(std::uint32_t mode) noexcept {
  return mode == kModeGrayscale || mode == kModeIndexed || mode == kModeRgb || mode == kModeCmyk;
}

[[nodiscard]] std::string pattern_label(const PatRecord& record, std::uint32_t one_based_index) {
  if (!record.name.empty()) {
    return "Pattern \"" + record.name + "\"";
  }
  return "Pattern " + std::to_string(one_based_index);
}

[[nodiscard]] bool is_valid_utf8(std::string_view text) noexcept {
  std::size_t cursor = 0;
  while (cursor < text.size()) {
    const auto lead = static_cast<std::uint8_t>(text[cursor]);
    if (lead <= 0x7FU) {
      ++cursor;
      continue;
    }

    std::size_t continuation_count = 0;
    std::uint8_t second_min = 0x80U;
    std::uint8_t second_max = 0xBFU;
    if (lead >= 0xC2U && lead <= 0xDFU) {
      continuation_count = 1;
    } else if (lead >= 0xE0U && lead <= 0xEFU) {
      continuation_count = 2;
      if (lead == 0xE0U) {
        second_min = 0xA0U;  // reject overlong encodings
      } else if (lead == 0xEDU) {
        second_max = 0x9FU;  // reject UTF-16 surrogate code points
      }
    } else if (lead >= 0xF0U && lead <= 0xF4U) {
      continuation_count = 3;
      if (lead == 0xF0U) {
        second_min = 0x90U;  // reject overlong encodings
      } else if (lead == 0xF4U) {
        second_max = 0x8FU;  // reject values above U+10FFFF
      }
    } else {
      return false;
    }

    if (continuation_count > text.size() - cursor - 1U) {
      return false;
    }
    const auto second = static_cast<std::uint8_t>(text[cursor + 1U]);
    if (second < second_min || second > second_max) {
      return false;
    }
    for (std::size_t offset = 2U; offset <= continuation_count; ++offset) {
      const auto byte = static_cast<std::uint8_t>(text[cursor + offset]);
      if (byte < 0x80U || byte > 0xBFU) {
        return false;
      }
    }
    cursor += continuation_count + 1U;
  }
  return true;
}

[[nodiscard]] std::uint64_t validate_plane_geometry(std::span<const std::uint8_t> bytes) {
  if (bytes.size() < kChannelHeaderBytes) {
    throw std::runtime_error("PAT pattern channel is truncated");
  }
  BigEndianReader channel(bytes);
  (void)channel.read_u32();  // depth; the shared decoder validates supported values
  const auto top = static_cast<std::int32_t>(channel.read_u32());
  const auto left = static_cast<std::int32_t>(channel.read_u32());
  const auto bottom = static_cast<std::int32_t>(channel.read_u32());
  const auto right = static_cast<std::int32_t>(channel.read_u32());
  const auto width64 = static_cast<std::int64_t>(right) - static_cast<std::int64_t>(left);
  const auto height64 = static_cast<std::int64_t>(bottom) - static_cast<std::int64_t>(top);
  if (width64 <= 0 || height64 <= 0 || width64 > kMaxPatternDimension ||
      height64 > kMaxPatternDimension) {
    throw std::runtime_error("PAT pattern channel rectangle is invalid");
  }
  const auto plane_pixels = static_cast<std::uint64_t>(width64) *
                            static_cast<std::uint64_t>(height64);
  if (plane_pixels > kMaxPatternPixels) {
    throw std::runtime_error("PAT pattern channel has too many pixels");
  }
  return plane_pixels;
}

// Reads enough of one standalone record to find its exact end. Unlike a PSD
// Patt entry, a .pat record has no outer length or padding. Its VMA length is
// therefore the only reliable resynchronization point for the next record.
PatRecord read_record(BigEndianReader& reader, std::span<const std::uint8_t> bytes) {
  PatRecord record;
  record.record_start = reader.position();
  record.version = reader.read_u32();
  record.mode = reader.read_u32();
  record.height = static_cast<std::int32_t>(reader.read_u16());
  record.width = static_cast<std::int32_t>(reader.read_u16());
  record.name = read_descriptor_unicode_string(reader);
  const auto id_length = reader.read_u8();
  const auto id_bytes = reader.read_bytes(id_length);
  record.id.assign(id_bytes.begin(), id_bytes.end());
  while (!record.id.empty() && record.id.back() == '\0') {
    record.id.pop_back();
  }

  if (record.mode == kModeIndexed) {
    record.indexed_table_start = reader.position();
    reader.skip(kIndexedColorTableBytes);
    // Standalone indexed records append two fields that Patt blocks omit.
    // colorsUsed is informational for our fixed 256-entry table; a valid
    // transparentIndex is applied only when no sheet-alpha VMA slot exists.
    record.colors_used = reader.read_u16();
    record.transparent_index = reader.read_u16();
  }

  record.vma_start = reader.position();
  record.vma_version = reader.read_u32();
  const auto vma_length = static_cast<std::size_t>(reader.read_u32());
  if (vma_length > reader.remaining()) {
    throw std::runtime_error("PAT pattern VMA is truncated");
  }
  reader.skip(vma_length);
  record.record_end = reader.position();
  if (record.record_end > bytes.size()) {
    throw std::runtime_error("PAT pattern record is truncated");
  }
  return record;
}

// Scans all slots without decoding their pixels. Only color planes and the
// final sheet-transparency plane are relevant to the shared decoder; written
// spare/user-mask slots still have their lengths validated and are skipped.
[[nodiscard]] PatPlaneInspection inspect_planes(const PatRecord& record,
                                                std::span<const std::uint8_t> bytes) {
  BigEndianReader vma(bytes.subspan(record.vma_start, record.record_end - record.vma_start));
  if (vma.read_u32() != kVirtualMemoryArrayVersion) {
    throw std::runtime_error("PAT pattern VMA version is unsupported");
  }
  const auto vma_length = static_cast<std::size_t>(vma.read_u32());
  if (vma_length > vma.remaining() || vma_length < 20U) {
    throw std::runtime_error("PAT pattern VMA length is invalid");
  }
  const auto payload = bytes.subspan(record.vma_start + 8U, vma_length);
  BigEndianReader slots(payload);
  slots.skip(16U);  // VMA bounds
  const auto declared_channels = slots.read_u32();
  if (declared_channels > 64U) {
    throw std::runtime_error("PAT pattern channel count is invalid");
  }
  const auto slot_count = declared_channels + 2U;
  const auto color_channel_count =
      record.mode == kModeRgb ? 3U : (record.mode == kModeCmyk ? 4U : 1U);
  PatPlaneInspection inspection;
  std::uint64_t indexed_color_samples = 0;
  for (std::uint32_t slot = 0; slot < slot_count; ++slot) {
    const auto written = slots.read_u32();
    if (written == 0U) {
      continue;
    }
    const auto length = static_cast<std::size_t>(slots.read_u32());
    if (length > slots.remaining()) {
      throw std::runtime_error("PAT pattern channel is truncated");
    }
    if (length != 0U && length < kChannelHeaderBytes) {
      throw std::runtime_error("PAT pattern channel is truncated");
    }
    const auto relevant = slot < color_channel_count || slot == declared_channels + 1U;
    if (length != 0U && relevant) {
      const auto plane_samples =
          validate_plane_geometry(payload.subspan(slots.position(), length));
      if (plane_samples > kMaxAttemptedPlaneSamples - inspection.decode_samples) {
        throw std::runtime_error("PAT pattern plane sample count is too large");
      }
      inspection.decode_samples += plane_samples;
      if (record.mode == kModeIndexed && slot == 0U) {
        indexed_color_samples = plane_samples;
      }
      if (slot == declared_channels + 1U) {
        inspection.sheet_alpha = true;
      }
    }
    slots.skip(length);
  }

  // Applying a standalone indexed transparent-index footer requires decoding
  // the color plane a second time to retain exact palette indices.
  if (record.mode == kModeIndexed && !inspection.sheet_alpha &&
      record.transparent_index < 256U) {
    if (indexed_color_samples > kMaxAttemptedPlaneSamples - inspection.decode_samples) {
      throw std::runtime_error("PAT pattern plane sample count is too large");
    }
    inspection.decode_samples += indexed_color_samples;
  }
  return inspection;
}

[[nodiscard]] std::uint8_t deep_sample_to_byte(std::uint32_t value16) noexcept {
  // Match psd_patterns.cpp: Photoshop deep integer data uses 0..32768.
  return static_cast<std::uint8_t>(std::min<std::uint32_t>(255U, (value16 * 255U + 16384U) / 32768U));
}

// Decodes the indexed color plane only when the standalone transparent-index
// footer must be applied. The main RGBA decode still goes through
// parse_patterns_block; this supplemental pass retains exact palette indices
// even when multiple table entries have identical RGB values.
[[nodiscard]] IndexedPlane decode_indexed_plane(const PatRecord& record,
                                                std::span<const std::uint8_t> bytes) {
  BigEndianReader vma(bytes.subspan(record.vma_start, record.record_end - record.vma_start));
  if (vma.read_u32() != kVirtualMemoryArrayVersion) {
    throw std::runtime_error("PAT pattern VMA version is unsupported");
  }
  const auto vma_length = static_cast<std::size_t>(vma.read_u32());
  if (vma_length > vma.remaining() || vma_length < 24U) {
    throw std::runtime_error("PAT pattern VMA length is invalid");
  }
  const auto payload = bytes.subspan(record.vma_start + 8U, vma_length);
  BigEndianReader slots(payload);
  slots.skip(16U);  // VMA bounds
  (void)slots.read_u32();  // declared max channels
  if (slots.read_u32() == 0U) {
    throw std::runtime_error("PAT indexed pattern has no color plane");
  }
  const auto slot_length = static_cast<std::size_t>(slots.read_u32());
  if (slot_length < kChannelHeaderBytes || slot_length > slots.remaining()) {
    throw std::runtime_error("PAT indexed channel is truncated");
  }
  const auto slot_offset = slots.position();
  BigEndianReader channel(payload.subspan(slot_offset, slot_length));
  const auto depth = channel.read_u32();
  const auto top = static_cast<std::int32_t>(channel.read_u32());
  const auto left = static_cast<std::int32_t>(channel.read_u32());
  const auto bottom = static_cast<std::int32_t>(channel.read_u32());
  const auto right = static_cast<std::int32_t>(channel.read_u32());
  const auto pixel_depth = channel.read_u16();
  const auto compression = channel.read_u8();
  const auto width64 = static_cast<std::int64_t>(right) - static_cast<std::int64_t>(left);
  const auto height64 = static_cast<std::int64_t>(bottom) - static_cast<std::int64_t>(top);
  if (width64 <= 0 || height64 <= 0 || width64 > kMaxPatternDimension ||
      height64 > kMaxPatternDimension) {
    throw std::runtime_error("PAT indexed channel rectangle is invalid");
  }
  const auto width = static_cast<std::int32_t>(width64);
  const auto height = static_cast<std::int32_t>(height64);
  const auto plane_pixels = static_cast<std::uint64_t>(width64) *
                            static_cast<std::uint64_t>(height64);
  if (plane_pixels > kMaxPatternPixels) {
    throw std::runtime_error("PAT indexed channel has too many pixels");
  }
  if ((depth != 8U && depth != 16U) || (pixel_depth != 8U && pixel_depth != 16U)) {
    throw std::runtime_error("PAT indexed channel depth is unsupported");
  }
  const auto bytes_per_sample = static_cast<std::size_t>(pixel_depth / 8U);
  const auto row_bytes = static_cast<std::size_t>(width) * bytes_per_sample;
  const auto expected = row_bytes * static_cast<std::size_t>(height);
  const auto data_length = slot_length - kChannelHeaderBytes;
  std::vector<std::uint8_t> raw;
  if (compression == 0U) {
    if (data_length < expected) {
      throw std::runtime_error("PAT indexed channel data is truncated");
    }
    raw = channel.read_bytes(expected);
  } else if (compression == 1U) {
    const auto table_bytes = static_cast<std::size_t>(height) * 2U;
    if (data_length < table_bytes) {
      throw std::runtime_error("PAT indexed RLE table is truncated");
    }
    std::vector<std::uint16_t> row_lengths(static_cast<std::size_t>(height));
    std::size_t encoded_total = 0;
    for (auto& row_length : row_lengths) {
      row_length = channel.read_u16();
      if (row_length > data_length - table_bytes - encoded_total) {
        throw std::runtime_error("PAT indexed RLE row is truncated");
      }
      encoded_total += row_length;
    }
    raw.reserve(expected);
    for (const auto row_length : row_lengths) {
      const auto encoded = channel.read_bytes(row_length);
      auto row = decode_packbits(encoded, row_bytes);
      raw.insert(raw.end(), row.begin(), row.end());
    }
  } else {
    throw std::runtime_error("PAT indexed compression mode is unsupported");
  }

  IndexedPlane plane;
  plane.top = top;
  plane.left = left;
  plane.width = width;
  plane.height = height;
  plane.samples.resize(static_cast<std::size_t>(plane_pixels));
  if (bytes_per_sample == 1U) {
    std::copy_n(raw.begin(), plane.samples.size(), plane.samples.begin());
  } else {
    for (std::size_t index = 0; index < plane.samples.size(); ++index) {
      const auto value = (static_cast<std::uint32_t>(raw[index * 2U]) << 8U) | raw[index * 2U + 1U];
      plane.samples[index] = deep_sample_to_byte(value);
    }
  }
  return plane;
}

[[nodiscard]] std::uint8_t indexed_sample(const IndexedPlane& plane, std::int32_t x,
                                          std::int32_t y) noexcept {
  const auto px = static_cast<std::int64_t>(x) - static_cast<std::int64_t>(plane.left);
  const auto py = static_cast<std::int64_t>(y) - static_cast<std::int64_t>(plane.top);
  if (px < 0 || py < 0 || px >= plane.width || py >= plane.height || plane.samples.empty()) {
    return 0U;  // same missing-plane fallback as psd_patterns.cpp
  }
  return plane.samples[static_cast<std::size_t>(py) * static_cast<std::size_t>(plane.width) +
                       static_cast<std::size_t>(px)];
}

// Converts one standalone record into the length-prefixed Patt-block record
// consumed by parse_patterns_block. Indexed .pat records have a four-byte
// colorsUsed/transparentIndex footer after the 768-byte table; Patt records do
// not, so that footer is intentionally omitted from the adapted bytes.
[[nodiscard]] std::vector<std::uint8_t> adapt_to_patterns_block(const PatRecord& record,
                                                                std::span<const std::uint8_t> bytes) {
  const auto prefix_end = record.mode == kModeIndexed
                              ? record.indexed_table_start + kIndexedColorTableBytes
                              : record.vma_start;
  const auto prefix_size = prefix_end - record.record_start;
  const auto vma_size = record.record_end - record.vma_start;
  if (vma_size > std::numeric_limits<std::uint32_t>::max() ||
      prefix_size > std::numeric_limits<std::uint32_t>::max() - vma_size) {
    throw std::runtime_error("PAT pattern record is too large");
  }
  const auto pattern_size = prefix_size + vma_size;
  const auto consumed = 4U + pattern_size;
  const auto padding = (4U - (consumed % 4U)) % 4U;
  std::vector<std::uint8_t> block(consumed + padding, 0U);
  const auto length = static_cast<std::uint32_t>(pattern_size);
  block[0] = static_cast<std::uint8_t>((length >> 24U) & 0xFFU);
  block[1] = static_cast<std::uint8_t>((length >> 16U) & 0xFFU);
  block[2] = static_cast<std::uint8_t>((length >> 8U) & 0xFFU);
  block[3] = static_cast<std::uint8_t>(length & 0xFFU);
  std::copy_n(bytes.data() + record.record_start, prefix_size, block.data() + 4U);
  std::copy_n(bytes.data() + record.vma_start, vma_size, block.data() + 4U + prefix_size);
  return block;
}

}  // namespace

std::optional<PatReadResult> read_pat(std::span<const std::uint8_t> bytes, std::string& error,
                                      const CmykToRgbTransform* cmyk_icc) {
  error.clear();
  try {
    BigEndianReader reader(bytes);
    const auto signature = reader.read_bytes(4U);
    if (signature != std::vector<std::uint8_t>{'8', 'B', 'P', 'T'}) {
      error = "Not a Photoshop PAT file";
      return std::nullopt;
    }
    const auto version = reader.read_u16();
    if (version != kPatFileVersion) {
      error = "Unsupported PAT version " + std::to_string(version);
      return std::nullopt;
    }
    const auto pattern_count = reader.read_u32();
    if (pattern_count == 0U) {
      error = "The file contains no patterns";
      return std::nullopt;
    }
    if (pattern_count > kMaxPatternCount) {
      error = "PAT pattern count exceeds " + std::to_string(kMaxPatternCount);
      return std::nullopt;
    }

    PatReadResult result;
    result.patterns.reserve(std::min<std::size_t>(pattern_count, 256U));
    std::uint64_t attempted_pattern_pixels = 0;
    std::uint64_t attempted_plane_samples = 0;
    for (std::uint32_t index = 0; index < pattern_count; ++index) {
      PatRecord record;
      try {
        record = read_record(reader, bytes);
      } catch (const std::exception& record_error) {
        const auto message = "Stopped at unreadable pattern " + std::to_string(index + 1U) + ": " +
                             record_error.what();
        if (result.patterns.empty()) {
          error = message;
          return std::nullopt;
        }
        result.warnings.push_back(message);
        break;
      }

      const auto label = pattern_label(record, index + 1U);
      if (record.version != kPatternVersion) {
        result.warnings.push_back(label + ": unsupported record version " +
                                  std::to_string(record.version));
        continue;
      }
      if (!supported_mode(record.mode)) {
        result.warnings.push_back(label + ": unsupported image mode " + std::to_string(record.mode));
        continue;
      }
      if (record.vma_version != kVirtualMemoryArrayVersion) {
        result.warnings.push_back(label + ": unsupported VMA version " +
                                  std::to_string(record.vma_version));
        continue;
      }
      if (record.width <= 0 || record.height <= 0) {
        result.warnings.push_back(label + ": empty dimensions");
        continue;
      }
      if (record.width > kMaxPatternDimension || record.height > kMaxPatternDimension) {
        result.warnings.push_back(label + ": dimensions exceed " +
                                  std::to_string(kMaxPatternDimension) + "px");
        continue;
      }
      const auto pixel_count = static_cast<std::uint64_t>(record.width) *
                               static_cast<std::uint64_t>(record.height);
      if (pixel_count > kMaxPatternPixels) {
        result.warnings.push_back(label + ": pixel count exceeds the import limit");
        continue;
      }
      if (record.record_end - record.record_start > kMaxPatternRecordBytes) {
        result.warnings.push_back(label + ": record is too large");
        continue;
      }
      if (pixel_count > kMaxTotalPatternPixels - attempted_pattern_pixels) {
        result.warnings.push_back(label + ": skipped because the file exceeds the total pixel limit");
        continue;
      }
      // Charge records admitted to the decode path before decoding so a file
      // cannot evade the cumulative bound with a series of malformed records.
      attempted_pattern_pixels += pixel_count;

      auto pattern_id = record.id;
      if (pattern_id.empty() || pattern_id.size() > 255U || !is_valid_utf8(pattern_id)) {
        pattern_id = generate_pattern_uuid();
        result.warnings.push_back(label +
                                  ": missing, oversized, or invalid UTF-8 pattern id was replaced");
      }

      PatPlaneInspection inspection;
      try {
        inspection = inspect_planes(record, bytes);
      } catch (const std::exception& inspect_error) {
        result.warnings.push_back(label + ": skipped unreadable pattern: " + inspect_error.what());
        continue;
      }
      if (inspection.decode_samples > kMaxAttemptedPlaneSamples - attempted_plane_samples) {
        result.warnings.push_back(
            label + ": skipped because the file exceeds the cumulative plane sample limit");
        continue;
      }
      // Keep the charge even when the shared decoder later rejects malformed
      // compression data. Failed records still consumed attempted decode work.
      attempted_plane_samples += inspection.decode_samples;

      try {
        const auto adapted = adapt_to_patterns_block(record, bytes);
        auto decoded = parse_patterns_block(adapted, cmyk_icc);
        if (decoded.size() != 1U) {
          throw std::runtime_error("channel data could not be decoded");
        }
        auto resource = std::move(decoded.front());
        // Keep the standalone record's semantic identity even if a future
        // codec normalizes its header representation.
        resource.id = std::move(pattern_id);
        resource.name = record.name;
        resource.provenance = PatternProvenance::Authored;

        if (record.mode == kModeIndexed && !inspection.sheet_alpha &&
            record.transparent_index < 256U) {
          const auto indices = decode_indexed_plane(record, bytes);
          for (std::int32_t y = 0; y < record.height; ++y) {
            auto* row = resource.tile.pixel(0, y);
            for (std::int32_t x = 0; x < record.width; ++x) {
              if (indexed_sample(indices, x, y) == record.transparent_index) {
                row[static_cast<std::size_t>(x) * 4U + 3U] = 0U;
              }
            }
          }
        }

        result.patterns.push_back(std::move(resource));
      } catch (const std::exception& decode_error) {
        result.warnings.push_back(label + ": skipped unreadable pattern: " + decode_error.what());
      }
    }

    // Any remaining bytes belong to Photoshop's optional 8BIMphry hierarchy
    // descriptor (or another future trailer). The counted record list is the
    // authoritative boundary, so trailing data never makes an otherwise valid
    // library fail.
    if (result.patterns.empty()) {
      error = "The file contains no supported patterns";
      return std::nullopt;
    }
    return result;
  } catch (const std::exception& parse_error) {
    error = std::string("Could not read PAT file: ") + parse_error.what();
    return std::nullopt;
  }
}

}  // namespace patchy::psd
