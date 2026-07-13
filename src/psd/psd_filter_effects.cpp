#include "psd/psd_filter_effects.hpp"

#include "psd/psd_binary.hpp"
#include "psd/psd_descriptor.hpp"

#include <algorithm>
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
constexpr std::uint64_t kMaximumDecodedMaskPixels = 64ULL * 1024ULL * 1024ULL;

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
  if (pixels64 == 0U || pixels64 > kMaximumDecodedMaskPixels) {
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
  }
  // Match Photoshop's FEid/FXid shape: the alignment bytes are part of the
  // tagged block payload (and therefore its declared length), rather than the
  // generic padding that follows a tagged block.
  while ((writer.bytes().size() % 4U) != 0U) {
    writer.write_u8(0);
  }
  return writer.bytes();
}

} // namespace patchy::psd
