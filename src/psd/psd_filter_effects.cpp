#include "psd/psd_filter_effects.hpp"

#include "psd/psd_binary.hpp"
#include "psd/psd_descriptor.hpp"
#include "core/smart_filter.hpp"

#include <algorithm>
#include <exception>
#include <functional>
#include <limits>
#include <stdexcept>
#include <utility>

namespace patchy::psd {

namespace {

constexpr std::uint32_t kMinimumOuterVersion = 1;
constexpr std::uint32_t kMaximumOuterVersion = 3;
constexpr std::uint32_t kSupportedRecordVersion = 1;
constexpr std::uint32_t kSupportedCacheDepth = 8;
constexpr std::uint32_t kMaximumSupportedCacheChannels = 64;
constexpr std::int64_t kMaximumPsdDimension = 300000;

[[nodiscard]] std::int32_t read_i32(BigEndianReader &reader) {
  return static_cast<std::int32_t>(reader.read_u32());
}

[[nodiscard]] std::optional<Rect> checked_rect(std::int32_t top,
                                               std::int32_t left,
                                               std::int32_t bottom,
                                               std::int32_t right) {
  const auto width =
      static_cast<std::int64_t>(right) - static_cast<std::int64_t>(left);
  const auto height =
      static_cast<std::int64_t>(bottom) - static_cast<std::int64_t>(top);
  if (width <= 0 || height <= 0 || width > kMaximumPsdDimension ||
      height > kMaximumPsdDimension ||
      width > std::numeric_limits<std::int32_t>::max() ||
      height > std::numeric_limits<std::int32_t>::max()) {
    return std::nullopt;
  }
  return Rect{left, top, static_cast<std::int32_t>(width),
              static_cast<std::int32_t>(height)};
}

[[nodiscard]] bool
raw_record_range_is_valid(const SmartFilterEffectsRecord &record) noexcept {
  return record.raw_storage != nullptr &&
         record.raw_body_offset <= record.raw_storage->size() &&
         record.raw_body_length <=
             record.raw_storage->size() - record.raw_body_offset;
}

[[nodiscard]] std::optional<std::int32_t>
checked_edge(std::int32_t start, std::int32_t extent) noexcept {
  const auto edge = static_cast<std::int64_t>(start) + extent;
  if (edge < std::numeric_limits<std::int32_t>::min() ||
      edge > std::numeric_limits<std::int32_t>::max()) {
    return std::nullopt;
  }
  return static_cast<std::int32_t>(edge);
}

[[nodiscard]] bool editable_mask_is_valid(
    const SmartFilterMask &mask) noexcept {
  if (mask.pixels.empty()) {
    return true;
  }
  if (mask.pixels.format() != PixelFormat::gray8() || mask.bounds.empty() ||
      mask.bounds.width != mask.pixels.width() ||
      mask.bounds.height != mask.pixels.height() ||
      mask.bounds.width > kMaximumPsdDimension ||
      mask.bounds.height > kMaximumPsdDimension ||
      !checked_edge(mask.bounds.x, mask.bounds.width).has_value() ||
      !checked_edge(mask.bounds.y, mask.bounds.height).has_value()) {
    return false;
  }
  const auto pixels64 = static_cast<std::uint64_t>(mask.bounds.width) *
                        static_cast<std::uint64_t>(mask.bounds.height);
  return pixels64 != 0U &&
         pixels64 <= kMaximumEditableSmartFilterMaskPixels &&
         pixels64 == mask.pixels.data().size();
}

[[nodiscard]] std::vector<std::uint8_t>
encode_filter_mask_tail(const SmartFilterMask &mask) {
  if (mask.pixels.empty()) {
    return {};
  }

  std::vector<std::vector<std::uint8_t>> rows;
  rows.reserve(static_cast<std::size_t>(mask.bounds.height));
  for (std::int32_t y = 0; y < mask.bounds.height; ++y) {
    const auto row_begin = mask.pixels.data().begin() +
                           static_cast<std::ptrdiff_t>(y) * mask.bounds.width;
    rows.push_back(encode_packbits_row(std::span<const std::uint8_t>(
        row_begin, static_cast<std::size_t>(mask.bounds.width))));
  }

  BigEndianWriter plane;
  plane.write_u16(1U);
  for (const auto &row : rows) {
    plane.write_u32(static_cast<std::uint32_t>(row.size()));
  }
  for (const auto &row : rows) {
    plane.write_bytes(row);
  }

  const auto right = checked_edge(mask.bounds.x, mask.bounds.width);
  const auto bottom = checked_edge(mask.bounds.y, mask.bounds.height);
  if (!right.has_value() || !bottom.has_value()) {
    throw std::runtime_error("PSD filter mask bounds overflow");
  }
  BigEndianWriter tail;
  tail.write_u8(1U);
  tail.write_u32(static_cast<std::uint32_t>(mask.bounds.y));
  tail.write_u32(static_cast<std::uint32_t>(mask.bounds.x));
  tail.write_u32(static_cast<std::uint32_t>(*bottom));
  tail.write_u32(static_cast<std::uint32_t>(*right));
  tail.write_u64(static_cast<std::uint64_t>(plane.bytes().size()));
  tail.write_bytes(plane.bytes());
  return tail.bytes();
}

[[nodiscard]] bool parse_cache_layout(std::span<const std::uint8_t> bytes,
                                      SmartFilterEffectsRecord &record) {
  try {
    BigEndianReader reader(bytes);
    if (reader.remaining() < 24U) {
      return false;
    }
    const auto top = read_i32(reader);
    const auto left = read_i32(reader);
    const auto bottom = read_i32(reader);
    const auto right = read_i32(reader);
    const auto bounds = checked_rect(top, left, bottom, right);
    record.cache_depth = reader.read_u32();
    record.cache_max_channels = reader.read_u32();
    if (!bounds.has_value() ||
        record.cache_max_channels > kMaximumSupportedCacheChannels) {
      return false;
    }

    const auto slot_count =
        static_cast<std::uint64_t>(record.cache_max_channels) + 2ULL;
    // Every slot needs at least its u32 written flag. This also makes the loop
    // bound depend on the containing cache body rather than untrusted metadata.
    if (slot_count > reader.remaining() / 4U) {
      return false;
    }
    for (std::uint64_t slot = 0; slot < slot_count; ++slot) {
      const auto written = reader.read_u32();
      if (written == 0U) {
        continue;
      }
      if (written != 1U || reader.remaining() < 8U) {
        return false;
      }
      const auto plane_length = reader.read_u64();
      if (plane_length > reader.remaining()) {
        return false;
      }
      reader.skip(static_cast<std::size_t>(plane_length));
    }
    if (reader.remaining() != 0U) {
      return false;
    }
    record.cache_bounds = *bounds;
    record.cache_layout_valid = true;
    return record.cache_depth == kSupportedCacheDepth;
  } catch (const std::runtime_error &) {
    return false;
  }
}

[[nodiscard]] bool decode_filter_mask(std::span<const std::uint8_t> bytes,
                                      const Rect &bounds,
                                      SmartFilterEffectsRecord &record) {
  const auto pixels64 = static_cast<std::uint64_t>(bounds.width) *
                        static_cast<std::uint64_t>(bounds.height);
  if (pixels64 == 0U ||
      pixels64 > kMaximumEditableSmartFilterMaskPixels) {
    return false;
  }
  const auto expected = static_cast<std::size_t>(pixels64);

  try {
    BigEndianReader reader(bytes);
    if (reader.remaining() < 2U) {
      return false;
    }
    const auto compression = reader.read_u16();
    std::vector<std::uint8_t> samples;
    if (compression == 0U) {
      if (reader.remaining() != expected) {
        return false;
      }
      samples = reader.read_bytes(expected);
    } else if (compression == 1U) {
      const auto table_bytes = static_cast<std::uint64_t>(bounds.height) * 4ULL;
      if (table_bytes > reader.remaining()) {
        return false;
      }
      std::vector<std::uint32_t> row_lengths(
          static_cast<std::size_t>(bounds.height));
      for (auto &row_length : row_lengths) {
        row_length = reader.read_u32();
      }
      samples.reserve(expected);
      for (const auto row_length : row_lengths) {
        if (row_length > reader.remaining()) {
          return false;
        }
        const auto encoded = reader.read_bytes(row_length);
        std::size_t consumed = 0;
        auto row = decode_packbits(encoded,
                                   static_cast<std::size_t>(bounds.width),
                                   &consumed);
        if (consumed != encoded.size()) {
          return false;
        }
        samples.insert(samples.end(), row.begin(), row.end());
      }
      if (reader.remaining() != 0U || samples.size() != expected) {
        return false;
      }
    } else {
      return false;
    }

    SmartFilterEffectsMask mask;
    mask.bounds = bounds;
    mask.samples =
        std::make_shared<const std::vector<std::uint8_t>>(std::move(samples));
    record.mask = std::move(mask);
    record.mask_decoded = true;
    return true;
  } catch (const std::runtime_error &) {
    return false;
  }
}

[[nodiscard]] bool
parse_optional_filter_mask(std::span<const std::uint8_t> bytes,
                           SmartFilterEffectsRecord &record) {
  // A record may end immediately when no filter mask was written. Some versions
  // instead append a zero presence byte; both forms are accepted.
  if (bytes.empty()) {
    return true;
  }
  try {
    BigEndianReader reader(bytes);
    const auto present = reader.read_u8();
    if (present == 0U) {
      return reader.remaining() == 0U;
    }
    record.mask_present = true;
    if (present != 1U || reader.remaining() < 24U) {
      return false;
    }

    const auto top = read_i32(reader);
    const auto left = read_i32(reader);
    const auto bottom = read_i32(reader);
    const auto right = read_i32(reader);
    const auto bounds = checked_rect(top, left, bottom, right);
    const auto mask_length = reader.read_u64();
    if (!bounds.has_value() || mask_length > reader.remaining()) {
      return false;
    }
    const auto body_offset = reader.position();
    const auto mask_body =
        bytes.subspan(body_offset, static_cast<std::size_t>(mask_length));
    reader.skip(static_cast<std::size_t>(mask_length));
    if (reader.remaining() != 0U ||
        record.cache_depth != kSupportedCacheDepth) {
      return false;
    }
    return decode_filter_mask(mask_body, *bounds, record);
  } catch (const std::runtime_error &) {
    return false;
  }
}

[[nodiscard]] SmartFilterEffectsRecord parse_filter_effects_record(
    const SmartFilterEffectsBlock &block,
    const std::shared_ptr<const std::vector<std::uint8_t>> &storage,
    std::size_t body_offset, std::size_t body_length) {
  SmartFilterEffectsRecord record;
  record.source_block_key = block.key;
  record.source_block_version = block.version;
  record.source_long_length = block.long_length;
  record.raw_storage = storage;
  record.raw_body_offset = body_offset;
  record.raw_body_length = body_length;

  try {
    const auto body = std::span<const std::uint8_t>(*storage).subspan(
        body_offset, body_length);
    BigEndianReader reader(body);
    const auto id_length = reader.read_u8();
    const auto id_bytes = reader.read_bytes(id_length);
    record.placed_uuid.assign(id_bytes.begin(), id_bytes.end());
    record.original_placed_uuid = record.placed_uuid;
    if (reader.remaining() < 4U) {
      return record;
    }
    record.record_version = reader.read_u32();
    if (record.record_version != kSupportedRecordVersion ||
        reader.remaining() < 8U) {
      return record;
    }

    const auto cache_length = reader.read_u64();
    if (cache_length > reader.remaining()) {
      return record;
    }
    const auto cache_offset = reader.position();
    const auto cache_bytes =
        body.subspan(cache_offset, static_cast<std::size_t>(cache_length));
    reader.skip(static_cast<std::size_t>(cache_length));
    const auto cache_supported = parse_cache_layout(cache_bytes, record);
    // Do not decompress an optional mask when the containing cache layout is
    // already invalid. Besides being semantically meaningless, a malformed
    // record could otherwise force a large bounded allocation before failing.
    const auto mask_supported =
        cache_supported &&
        parse_optional_filter_mask(body.subspan(reader.position()), record);
    record.data_supported = cache_supported && mask_supported;
  } catch (const std::runtime_error &) {
    // The outer u64 record boundary remains useful for byte preservation and
    // clone/rekey even when the bounded contents are malformed.
  }
  return record;
}

void mark_block_association_uniqueness(SmartFilterEffectsBlock &block) {
  for (auto &record : block.records) {
    if (record.placed_uuid.empty()) {
      record.association_unique = false;
      continue;
    }
    const auto matches = static_cast<std::size_t>(
        std::count_if(block.records.begin(), block.records.end(),
                      [&record](const SmartFilterEffectsRecord &candidate) {
                        return candidate.placed_uuid == record.placed_uuid;
                      }));
    record.association_unique = matches == 1U;
  }
}

[[nodiscard]] std::vector<std::uint8_t>
serialize_filter_effects_record_body(const SmartFilterEffectsRecord &record) {
  if (!raw_record_range_is_valid(record)) {
    throw std::runtime_error("PSD filter-effects record has no raw body");
  }
  const auto raw = raw_filter_effects_record_body(record);
  if (record.placed_uuid == record.original_placed_uuid) {
    return std::vector<std::uint8_t>(raw.begin(), raw.end());
  }
  if (record.placed_uuid.size() > 255U ||
      record.original_placed_uuid.size() > 255U || raw.empty() ||
      raw.front() != record.original_placed_uuid.size() ||
      raw.size() < 1U + record.original_placed_uuid.size()) {
    throw std::runtime_error(
        "PSD filter-effects record cannot be rekeyed safely");
  }
  const auto original_begin = raw.begin() + 1;
  const auto original_end =
      original_begin +
      static_cast<std::ptrdiff_t>(record.original_placed_uuid.size());
  if (!std::equal(original_begin, original_end,
                  record.original_placed_uuid.begin(),
                  record.original_placed_uuid.end())) {
    throw std::runtime_error(
        "PSD filter-effects record id does not match its raw body");
  }

  std::vector<std::uint8_t> body;
  body.reserve(raw.size() - record.original_placed_uuid.size() +
               record.placed_uuid.size());
  body.push_back(static_cast<std::uint8_t>(record.placed_uuid.size()));
  body.insert(body.end(), record.placed_uuid.begin(), record.placed_uuid.end());
  body.insert(body.end(), original_end, raw.end());
  return body;
}

} // namespace

std::span<const std::uint8_t> raw_filter_effects_record_body(
    const SmartFilterEffectsRecord &record) noexcept {
  if (!raw_record_range_is_valid(record)) {
    return {};
  }
  return std::span<const std::uint8_t>(*record.raw_storage)
      .subspan(record.raw_body_offset, record.raw_body_length);
}

bool replace_filter_effects_mask(SmartFilterEffectsStore &store,
                                 std::string_view placed_uuid,
                                 const SmartFilterMask &mask) {
  if (placed_uuid.empty() || !editable_mask_is_valid(mask)) {
    return false;
  }

  std::size_t target_block_index = 0U;
  std::size_t target_record_index = 0U;
  std::size_t matches = 0U;
  for (std::size_t block_index = 0U; block_index < store.blocks.size();
       ++block_index) {
    const auto &block = store.blocks[block_index];
    if (block.opaque || (block.key != "FEid" && block.key != "FXid") ||
        block.version < kMinimumOuterVersion ||
        block.version > kMaximumOuterVersion) {
      return false;
    }
    for (std::size_t record_index = 0U;
         record_index < block.records.size(); ++record_index) {
      if (block.records[record_index].placed_uuid == placed_uuid) {
        target_block_index = block_index;
        target_record_index = record_index;
        ++matches;
      }
    }
  }
  if (matches != 1U) {
    return false;
  }

  const auto &target_block = store.blocks[target_block_index];
  const auto &target = target_block.records[target_record_index];
  if (!target.semantic_supported() || !raw_record_range_is_valid(target) ||
      target.source_block_key != target_block.key ||
      target.source_block_version != target_block.version ||
      target.source_long_length != target_block.long_length) {
    return false;
  }

  // SoLd owns enabled/linked/extension flags. A flag-only change therefore
  // leaves the native FEid/FXid mask bytes exactly untouched.
  if (mask.pixels.empty()) {
    if (!target.mask_present) {
      return true;
    }
  } else if (target.mask_present && target.mask_decoded &&
             target.mask.has_value() && target.mask->samples != nullptr &&
             target.mask->bounds.x == mask.bounds.x &&
             target.mask->bounds.y == mask.bounds.y &&
             target.mask->bounds.width == mask.bounds.width &&
             target.mask->bounds.height == mask.bounds.height &&
             target.mask->samples->size() == mask.pixels.data().size() &&
             std::equal(target.mask->samples->begin(),
                        target.mask->samples->end(),
                        mask.pixels.data().begin())) {
    return true;
  }

  try {
    // Rebuilding this one block must be safe for every retained raw record.
    // This check occurs before any state changes so failures stay atomic.
    for (const auto &record : target_block.records) {
      (void)serialize_filter_effects_record_body(record);
    }

    auto current_body = serialize_filter_effects_record_body(target);
    auto current_storage =
        std::make_shared<const std::vector<std::uint8_t>>(current_body);
    auto current = parse_filter_effects_record(
        target_block, current_storage, 0U, current_storage->size());
    if (!current.data_supported || current.placed_uuid != placed_uuid ||
        current.record_version != kSupportedRecordVersion) {
      return false;
    }

    BigEndianReader reader(current_body);
    const auto id_length = reader.read_u8();
    const auto id = reader.read_bytes(id_length);
    if (std::string_view(reinterpret_cast<const char *>(id.data()), id.size()) !=
            placed_uuid ||
        reader.read_u32() != kSupportedRecordVersion) {
      return false;
    }
    const auto cache_length = reader.read_u64();
    if (cache_length > reader.remaining()) {
      return false;
    }
    reader.skip(static_cast<std::size_t>(cache_length));
    const auto cache_prefix_length = reader.position();
    const auto replacement_tail = encode_filter_mask_tail(mask);

    std::vector<std::uint8_t> replacement_body;
    replacement_body.reserve(cache_prefix_length + replacement_tail.size());
    replacement_body.insert(
        replacement_body.end(), current_body.begin(),
        current_body.begin() + static_cast<std::ptrdiff_t>(cache_prefix_length));
    replacement_body.insert(replacement_body.end(), replacement_tail.begin(),
                            replacement_tail.end());

    auto replacement_storage =
        std::make_shared<const std::vector<std::uint8_t>>(
            std::move(replacement_body));
    auto replacement = parse_filter_effects_record(
        target_block, replacement_storage, 0U, replacement_storage->size());
    if (!replacement.data_supported || replacement.placed_uuid != placed_uuid ||
        replacement.mask_present != !mask.pixels.empty() ||
        (!mask.pixels.empty() &&
         (!replacement.mask_decoded || !replacement.mask.has_value() ||
          replacement.mask->samples == nullptr ||
          replacement.mask->bounds.x != mask.bounds.x ||
          replacement.mask->bounds.y != mask.bounds.y ||
          replacement.mask->bounds.width != mask.bounds.width ||
          replacement.mask->bounds.height != mask.bounds.height ||
          replacement.mask->samples->size() != mask.pixels.data().size() ||
          !std::equal(replacement.mask->samples->begin(),
                      replacement.mask->samples->end(),
                      mask.pixels.data().begin())))) {
      return false;
    }
    replacement.association_unique = true;

    auto &mutated_block = store.blocks[target_block_index];
    mutated_block.records[target_record_index] = std::move(replacement);
    mutated_block.original_payload.reset();
    return true;
  } catch (const std::exception &) {
    return false;
  }
}

SmartFilterEffectsBlock parse_filter_effects_block(
    std::string key, std::shared_ptr<const std::vector<std::uint8_t>> payload,
    bool long_length, std::size_t original_global_index) {
  SmartFilterEffectsBlock block;
  block.key = std::move(key);
  block.long_length = long_length;
  block.original_global_index = original_global_index;
  block.original_payload = payload;
  if (payload == nullptr || (block.key != "FEid" && block.key != "FXid")) {
    block.opaque = true;
    return block;
  }

  try {
    BigEndianReader reader(*payload);
    block.version = reader.read_u32();
    if (block.version < kMinimumOuterVersion ||
        block.version > kMaximumOuterVersion) {
      block.opaque = true;
      block.records.clear();
      return block;
    }
    while (reader.remaining() != 0U) {
      if (reader.remaining() < 8U) {
        // Photoshop includes up to three zero alignment bytes in the declared
        // FEid payload length. They are not another length-prefixed record.
        // Keep rejecting any other tail so a malformed record walk never
        // becomes editable by accident.
        bool zero_padding = reader.remaining() <= 3U;
        while (reader.remaining() != 0U) {
          zero_padding = reader.read_u8() == 0U && zero_padding;
        }
        if (zero_padding) {
          break;
        }
        block.opaque = true;
        block.records.clear();
        return block;
      }
      const auto record_length = reader.read_u64();
      if (record_length > reader.remaining()) {
        block.opaque = true;
        block.records.clear();
        return block;
      }
      const auto body_offset = reader.position();
      block.records.push_back(
          parse_filter_effects_record(block, payload, body_offset,
                                      static_cast<std::size_t>(record_length)));
      reader.skip(static_cast<std::size_t>(record_length));
      // Photoshop aligns every length-prefixed FEid/FXid record to four bytes,
      // not only the final block payload. The padding is outside the declared
      // record body. Patchy builds before 0.20 omitted inter-record padding,
      // so retain that readable legacy shape when only its next u64 boundary
      // is plausible. Ambiguous or nonzero padding fails closed.
      const auto padding = (4U - (reader.position() % 4U)) % 4U;
      if (padding != 0U && reader.remaining() != 0U) {
        const auto bytes = std::span<const std::uint8_t>(*payload);
        const auto position = reader.position();
        const auto read_length_at = [&](std::size_t offset) {
          std::uint64_t value = 0U;
          for (std::size_t byte = 0; byte < 8U; ++byte) {
            value = (value << 8U) | bytes[offset + byte];
          }
          return value;
        };
        const auto plausible_record_at = [&](std::size_t offset) {
          if (offset + 8U > bytes.size()) {
            return false;
          }
          const auto length = read_length_at(offset);
          return length != 0U && length <= bytes.size() - offset - 8U;
        };
        const auto padding_is_zero =
            padding <= reader.remaining() &&
            std::all_of(bytes.begin() + static_cast<std::ptrdiff_t>(position),
                        bytes.begin() +
                            static_cast<std::ptrdiff_t>(position + padding),
                        [](std::uint8_t value) { return value == 0U; });
        const auto legacy_boundary = plausible_record_at(position);
        const auto aligned_boundary =
            padding_is_zero && plausible_record_at(position + padding);
        const auto final_padding =
            padding_is_zero && reader.remaining() == padding;
        if ((aligned_boundary && legacy_boundary) ||
            (!aligned_boundary && !legacy_boundary && !final_padding)) {
          block.opaque = true;
          block.records.clear();
          return block;
        }
        if (aligned_boundary || final_padding) {
          reader.skip(padding);
        }
      }
    }
    mark_block_association_uniqueness(block);
  } catch (const std::runtime_error &) {
    block.opaque = true;
    block.records.clear();
  }
  return block;
}

SmartFilterEffectsBlock parse_filter_effects_block(
    std::string_view key, std::span<const std::uint8_t> payload,
    bool long_length, std::size_t original_global_index) {
  auto shared_payload = std::make_shared<const std::vector<std::uint8_t>>(
      payload.begin(), payload.end());
  return parse_filter_effects_block(std::string(key), std::move(shared_payload),
                                    long_length, original_global_index);
}

std::vector<std::uint8_t>
serialize_filter_effects_block(const SmartFilterEffectsBlock &block) {
  if (block.original_payload != nullptr) {
    return *block.original_payload;
  }
  if (block.opaque || (block.key != "FEid" && block.key != "FXid") ||
      block.version < kMinimumOuterVersion ||
      block.version > kMaximumOuterVersion) {
    throw std::runtime_error("PSD filter-effects block cannot be regenerated");
  }

  BigEndianWriter writer;
  writer.write_u32(block.version);
  for (const auto &record : block.records) {
    const auto body = serialize_filter_effects_record_body(record);
    writer.write_u64(static_cast<std::uint64_t>(body.size()));
    writer.write_bytes(body);
    while ((writer.bytes().size() % 4U) != 0U) {
      writer.write_u8(0);
    }
  }
  return writer.bytes();
}

std::optional<SmartFilterEffectsRecord>
author_filter_effects_record(std::string_view placed_uuid,
                             Rect document_bounds,
                             const PixelBuffer &unfiltered_pixels,
                             Rect unfiltered_bounds,
                             const SmartFilterMask &mask) {
  if (placed_uuid.empty() || placed_uuid.size() > 255U ||
      document_bounds.empty() || unfiltered_pixels.empty() ||
      document_bounds.width > kMaximumPsdDimension ||
      document_bounds.height > kMaximumPsdDimension ||
      unfiltered_pixels.format().bit_depth != BitDepth::UInt8 ||
      unfiltered_pixels.format().channels < 3U ||
      unfiltered_pixels.format().channels > 4U ||
      unfiltered_bounds.width != unfiltered_pixels.width() ||
      unfiltered_bounds.height != unfiltered_pixels.height() ||
      (!mask.pixels.empty() &&
       (mask.pixels.format() != PixelFormat::gray8() ||
        mask.bounds.width != mask.pixels.width() ||
        mask.bounds.height != mask.pixels.height()))) {
    return std::nullopt;
  }

  const auto checked_edge = [](std::int32_t start,
                               std::int32_t extent) -> std::optional<std::int32_t> {
    const auto edge = static_cast<std::int64_t>(start) + extent;
    if (edge < std::numeric_limits<std::int32_t>::min() ||
        edge > std::numeric_limits<std::int32_t>::max()) {
      return std::nullopt;
    }
    return static_cast<std::int32_t>(edge);
  };
  const auto document_right =
      checked_edge(document_bounds.x, document_bounds.width);
  const auto document_bottom =
      checked_edge(document_bounds.y, document_bounds.height);
  if (!document_right.has_value() || !document_bottom.has_value()) {
    return std::nullopt;
  }

  const auto pixel_count =
      static_cast<std::uint64_t>(document_bounds.width) *
      static_cast<std::uint64_t>(document_bounds.height);
  if (pixel_count > std::numeric_limits<std::size_t>::max() ||
      pixel_count > kMaximumEditableSmartFilterMaskPixels) {
    return std::nullopt;
  }
  const auto encode_plane = [&](const std::function<std::uint8_t(
                                     std::int32_t, std::int32_t)> &sample,
                                 std::vector<std::uint8_t>* decoded = nullptr) {
    std::vector<std::vector<std::uint8_t>> rows;
    rows.reserve(static_cast<std::size_t>(document_bounds.height));
    std::vector<std::uint8_t> raw_row(
        static_cast<std::size_t>(document_bounds.width));
    if (decoded != nullptr) {
      decoded->resize(static_cast<std::size_t>(pixel_count));
    }
    for (std::int32_t y = 0; y < document_bounds.height; ++y) {
      const auto document_y = document_bounds.y + y;
      for (std::int32_t x = 0; x < document_bounds.width; ++x) {
        raw_row[static_cast<std::size_t>(x)] =
            sample(document_bounds.x + x, document_y);
      }
      if (decoded != nullptr) {
        std::copy(raw_row.begin(), raw_row.end(),
                  decoded->begin() +
                      static_cast<std::ptrdiff_t>(y) *
                          document_bounds.width);
      }
      rows.push_back(encode_packbits_row(raw_row));
    }

    BigEndianWriter plane;
    plane.write_u16(1U);
    for (const auto &row : rows) {
      plane.write_u32(static_cast<std::uint32_t>(row.size()));
    }
    for (const auto &row : rows) {
      plane.write_bytes(row);
    }
    return plane.bytes();
  };

  const auto source_sample = [&](std::int32_t document_x,
                                 std::int32_t document_y,
                                 std::uint16_t channel) -> std::uint8_t {
    const auto local_x = static_cast<std::int64_t>(document_x) -
                         unfiltered_bounds.x;
    const auto local_y = static_cast<std::int64_t>(document_y) -
                         unfiltered_bounds.y;
    if (local_x < 0 || local_y < 0 || local_x >= unfiltered_bounds.width ||
        local_y >= unfiltered_bounds.height) {
      return 0U;
    }
    const auto *pixel = unfiltered_pixels.pixel(
        static_cast<std::int32_t>(local_x),
        static_cast<std::int32_t>(local_y));
    if (channel == 3U) {
      return unfiltered_pixels.format().channels >= 4U ? pixel[3] : 255U;
    }
    return pixel[channel];
  };
  const auto mask_sample = [&](std::int32_t document_x,
                               std::int32_t document_y) -> std::uint8_t {
    if (!mask.pixels.empty()) {
      const auto local_x =
          static_cast<std::int64_t>(document_x) - mask.bounds.x;
      const auto local_y =
          static_cast<std::int64_t>(document_y) - mask.bounds.y;
      if (local_x >= 0 && local_y >= 0 && local_x < mask.bounds.width &&
          local_y < mask.bounds.height) {
        return mask.pixels.pixel(static_cast<std::int32_t>(local_x),
                                 static_cast<std::int32_t>(local_y))[0];
      }
    }
    return mask.extend_with_white ? 255U : mask.default_color;
  };

  std::array<std::vector<std::uint8_t>, 4> cache_planes;
  for (std::uint16_t channel = 0; channel < 4U; ++channel) {
    cache_planes[channel] = encode_plane(
        [&](std::int32_t x, std::int32_t y) {
          return source_sample(x, y, channel);
        });
  }
  std::vector<std::uint8_t> decoded_mask;
  const auto mask_plane = encode_plane(mask_sample, &decoded_mask);

  BigEndianWriter cache;
  cache.write_u32(static_cast<std::uint32_t>(document_bounds.y));
  cache.write_u32(static_cast<std::uint32_t>(document_bounds.x));
  cache.write_u32(static_cast<std::uint32_t>(*document_bottom));
  cache.write_u32(static_cast<std::uint32_t>(*document_right));
  cache.write_u32(kSupportedCacheDepth);
  constexpr std::uint32_t kPhotoshopMaximumChannels = 24U;
  cache.write_u32(kPhotoshopMaximumChannels);
  for (std::uint32_t slot = 0; slot < kPhotoshopMaximumChannels + 2U;
       ++slot) {
    const int plane_index =
        slot <= 2U ? static_cast<int>(slot) : (slot == 25U ? 3 : -1);
    if (plane_index < 0) {
      cache.write_u32(0U);
      continue;
    }
    const auto &plane = cache_planes[static_cast<std::size_t>(plane_index)];
    cache.write_u32(1U);
    cache.write_u64(static_cast<std::uint64_t>(plane.size()));
    cache.write_bytes(plane);
  }

  BigEndianWriter body;
  body.write_u8(static_cast<std::uint8_t>(placed_uuid.size()));
  body.write_bytes(std::span<const std::uint8_t>(
      reinterpret_cast<const std::uint8_t *>(placed_uuid.data()),
      placed_uuid.size()));
  body.write_u32(kSupportedRecordVersion);
  body.write_u64(static_cast<std::uint64_t>(cache.bytes().size()));
  body.write_bytes(cache.bytes());
  body.write_u8(1U);
  body.write_u32(static_cast<std::uint32_t>(document_bounds.y));
  body.write_u32(static_cast<std::uint32_t>(document_bounds.x));
  body.write_u32(static_cast<std::uint32_t>(*document_bottom));
  body.write_u32(static_cast<std::uint32_t>(*document_right));
  body.write_u64(static_cast<std::uint64_t>(mask_plane.size()));
  body.write_bytes(mask_plane);

  SmartFilterEffectsRecord record;
  record.source_block_key = "FEid";
  record.source_block_version = 3U;
  record.source_long_length = false;
  record.placed_uuid = std::string(placed_uuid);
  record.original_placed_uuid = record.placed_uuid;
  record.record_version = kSupportedRecordVersion;
  record.raw_storage =
      std::make_shared<const std::vector<std::uint8_t>>(
          std::move(body.bytes()));
  record.raw_body_offset = 0U;
  record.raw_body_length = record.raw_storage->size();
  record.cache_bounds = document_bounds;
  record.cache_depth = kSupportedCacheDepth;
  record.cache_max_channels = kPhotoshopMaximumChannels;
  record.cache_layout_valid = true;
  record.mask_present = true;
  record.mask_decoded = true;
  SmartFilterEffectsMask decoded;
  decoded.bounds = document_bounds;
  decoded.samples = std::make_shared<const std::vector<std::uint8_t>>(
      std::move(decoded_mask));
  record.mask = std::move(decoded);
  record.data_supported = true;
  record.association_unique = true;
  return record;
}

} // namespace patchy::psd
