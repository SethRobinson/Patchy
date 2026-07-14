#include "psd_test_support.hpp"

namespace patchy::test {

std::vector<std::uint8_t> test_blend_if_identity_payload() {
  std::vector<std::uint8_t> payload(40U);
  for (std::size_t offset = 0; offset < payload.size(); offset += kTestBlendIfIdentityEntry.size()) {
    std::copy(kTestBlendIfIdentityEntry.begin(), kTestBlendIfIdentityEntry.end(), payload.begin() + offset);
  }
  return payload;
}

void write_ascii4(patchy::psd::BigEndianWriter& writer, const char (&value)[5]) {
  for (int i = 0; i < 4; ++i) {
    writer.write_u8(static_cast<std::uint8_t>(value[i]));
  }
}

void write_pascal_padded(patchy::psd::BigEndianWriter& writer, const std::string& value,
                         std::size_t padded_multiple) {
  const auto length = std::min<std::size_t>(value.size(), 255);
  writer.write_u8(static_cast<std::uint8_t>(length));
  writer.write_bytes(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(value.data()), length));
  const auto consumed = 1 + length;
  const auto padded = ((consumed + padded_multiple - 1) / padded_multiple) * padded_multiple;
  for (std::size_t i = consumed; i < padded; ++i) {
    writer.write_u8(0);
  }
}

std::string read_pascal_padded(patchy::psd::BigEndianReader& reader, std::size_t padded_multiple) {
  const auto start = reader.position();
  const auto length = reader.read_u8();
  auto bytes = reader.read_bytes(length);
  const auto consumed = reader.position() - start;
  const auto padded = ((consumed + padded_multiple - 1) / padded_multiple) * padded_multiple;
  if (padded > consumed) {
    reader.skip(padded - consumed);
  }
  return std::string(bytes.begin(), bytes.end());
}

std::vector<std::string> psd_raw_layer_record_names(std::span<const std::uint8_t> bytes) {
  patchy::psd::BigEndianReader reader(bytes);
  (void)patchy::psd::read_header(reader);

  const auto color_mode_length = reader.read_u32();
  reader.skip(color_mode_length);
  const auto image_resource_length = reader.read_u32();
  reader.skip(image_resource_length);

  const auto layer_mask_length = reader.read_u32();
  CHECK(layer_mask_length > 0);
  const auto layer_info_length = reader.read_u32();
  CHECK(layer_info_length > 0);

  const auto layer_count_raw = static_cast<std::int16_t>(reader.read_u16());
  const auto layer_count = layer_count_raw < 0 ? -layer_count_raw : layer_count_raw;
  std::vector<std::string> names;
  names.reserve(static_cast<std::size_t>(layer_count));

  for (std::int16_t index = 0; index < layer_count; ++index) {
    reader.skip(16);  // bounds
    const auto channel_count = reader.read_u16();
    for (std::uint16_t channel = 0; channel < channel_count; ++channel) {
      reader.skip(2);  // channel id
      reader.skip(4);  // channel byte length
    }
    reader.skip(12);  // blend signature/key, opacity, clipping, flags, filler

    const auto extra_length = reader.read_u32();
    const auto extra_end = reader.position() + extra_length;
    const auto mask_length = reader.read_u32();
    reader.skip(mask_length);
    const auto blending_ranges_length = reader.read_u32();
    reader.skip(blending_ranges_length);
    names.push_back(read_pascal_padded(reader, 4));
    if (reader.position() < extra_end) {
      reader.skip(extra_end - reader.position());
    }
  }

  return names;
}

std::vector<PsdLayerChannelRecord> psd_layer_channel_records(std::span<const std::uint8_t> bytes) {
  patchy::psd::BigEndianReader reader(bytes);
  (void)patchy::psd::read_header(reader);

  const auto color_mode_length = reader.read_u32();
  reader.skip(color_mode_length);
  const auto image_resource_length = reader.read_u32();
  reader.skip(image_resource_length);

  const auto layer_mask_length = reader.read_u32();
  if (layer_mask_length == 0) {
    return {};
  }
  const auto layer_info_length = reader.read_u32();
  if (layer_info_length == 0) {
    return {};
  }

  const auto layer_count_raw = static_cast<std::int16_t>(reader.read_u16());
  const auto layer_count = layer_count_raw < 0 ? -layer_count_raw : layer_count_raw;
  std::vector<PsdLayerChannelRecord> records;
  for (std::int16_t index = 0; index < layer_count; ++index) {
    reader.skip(16);  // bounds
    const auto channel_count = reader.read_u16();
    for (std::uint16_t channel = 0; channel < channel_count; ++channel) {
      const auto id = static_cast<std::int16_t>(reader.read_u16());
      const auto length = reader.read_u32();
      records.push_back(PsdLayerChannelRecord{id, length, 0});
    }
    reader.skip(12);  // blend signature/key, opacity, clipping, flags, filler

    const auto extra_length = reader.read_u32();
    reader.skip(extra_length);
  }

  for (auto& record : records) {
    CHECK(record.length >= 2U);
    record.compression = reader.read_u16();
    reader.skip(record.length - 2U);
  }

  return records;
}

std::uint32_t read_u32_be_at(std::span<const std::uint8_t> bytes, std::size_t offset) {
  CHECK(offset + 4U <= bytes.size());
  return (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
         (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U) |
         (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U) |
         static_cast<std::uint32_t>(bytes[offset + 3U]);
}

std::vector<std::uint8_t> psd_layer_extra_data(std::span<const std::uint8_t> bytes, std::int16_t target_index) {
  patchy::psd::BigEndianReader reader(bytes);
  (void)patchy::psd::read_header(reader);

  const auto color_mode_length = reader.read_u32();
  reader.skip(color_mode_length);
  const auto image_resource_length = reader.read_u32();
  reader.skip(image_resource_length);

  const auto layer_mask_length = reader.read_u32();
  CHECK(layer_mask_length > 0);
  const auto layer_info_length = reader.read_u32();
  CHECK(layer_info_length > 0);
  const auto layer_count_raw = static_cast<std::int16_t>(reader.read_u16());
  const auto layer_count = layer_count_raw < 0 ? -layer_count_raw : layer_count_raw;
  CHECK(layer_count > 0);
  CHECK(target_index >= 0);
  CHECK(target_index < layer_count);

  for (std::int16_t index = 0; index < layer_count; ++index) {
    reader.skip(16);  // bounds
    const auto channel_count = reader.read_u16();
    for (std::uint16_t channel = 0; channel < channel_count; ++channel) {
      reader.skip(2);  // channel id
      reader.skip(4);  // channel byte length
    }
    reader.skip(12);  // blend signature/key, opacity, clipping, flags, filler

    const auto extra_length = reader.read_u32();
    auto extra_data = reader.read_bytes(extra_length);
    if (index == target_index) {
      return extra_data;
    }
  }

  CHECK(false);
  return {};
}

std::vector<std::uint8_t> psd_first_layer_extra_data(std::span<const std::uint8_t> bytes) {
  return psd_layer_extra_data(bytes, 0);
}

std::optional<std::vector<std::uint8_t>> psd_layer_block_payload(std::span<const std::uint8_t> extra_data,
                                                                 const char (&target_key)[5]) {
  patchy::psd::BigEndianReader reader(extra_data);
  const auto mask_length = reader.read_u32();
  reader.skip(mask_length);
  const auto blending_ranges_length = reader.read_u32();
  reader.skip(blending_ranges_length);
  (void)read_pascal_padded(reader, 4);

  while (reader.remaining() >= 12U) {
    const auto signature = reader.read_bytes(4);
    if (signature != std::vector<std::uint8_t>{'8', 'B', 'I', 'M'} &&
        signature != std::vector<std::uint8_t>{'8', 'B', '6', '4'}) {
      break;
    }
    const auto key = reader.read_bytes(4);
    const auto payload_length = reader.read_u32();
    auto payload = reader.read_bytes(payload_length);
    if (std::equal(key.begin(), key.end(), target_key)) {
      return payload;
    }
  }
  return std::nullopt;
}

void write_test_layer_block(patchy::psd::BigEndianWriter& writer, const char (&key)[5],
                            std::span<const std::uint8_t> payload) {
  write_ascii4(writer, "8BIM");
  write_ascii4(writer, key);
  writer.write_u32(static_cast<std::uint32_t>(payload.size()));
  writer.write_bytes(payload);
  if ((payload.size() % 2U) != 0) {
    writer.write_u8(0);
  }
}

std::optional<std::vector<std::uint8_t>> test_image_resource_payload(std::span<const std::uint8_t> resources,
                                                                     std::uint16_t id) {
  patchy::psd::BigEndianReader reader(resources);
  while (reader.remaining() > 0) {
    auto signature = reader.read_bytes(4);
    CHECK(signature[0] == '8');
    CHECK(signature[1] == 'B');
    const auto resource_id = reader.read_u16();
    (void)read_pascal_padded(reader, 2);
    const auto payload_length = reader.read_u32();
    auto payload = reader.read_bytes(payload_length);
    if ((payload_length % 2U) != 0 && reader.remaining() > 0) {
      reader.skip(1);
    }
    if (resource_id == id) {
      return payload;
    }
  }
  return std::nullopt;
}

std::filesystem::path arrows_fixture_path() {
  return patchy::test::committed_psd_fixture_path("arrows.psd");
}

bool layer_has_psd_block(const patchy::Layer& layer, const std::string& key) {
  return std::any_of(layer.unknown_psd_blocks().begin(), layer.unknown_psd_blocks().end(),
                     [&key](const patchy::UnknownPsdBlock& block) { return block.key == key; });
}

}  // namespace patchy::test
