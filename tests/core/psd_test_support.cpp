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
  const auto header = patchy::psd::read_header(reader);

  const auto color_mode_length = reader.read_u32();
  reader.skip(color_mode_length);
  const auto image_resource_length = reader.read_u32();
  reader.skip(image_resource_length);

  const auto layer_mask_length = header.large_document ? reader.read_u64() : reader.read_u32();
  CHECK(layer_mask_length > 0);
  const auto layer_info_length = header.large_document ? reader.read_u64() : reader.read_u32();
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
      reader.skip(header.large_document ? 8 : 4);  // channel byte length
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

std::vector<std::uint8_t> single_text_layer_psd(std::span<const std::uint8_t> text_payload,
                                                const char (&key)[5]) {
  patchy::psd::BigEndianWriter layer_extra;
  layer_extra.write_u32(0);
  layer_extra.write_u32(0);
  write_pascal_padded(layer_extra, "Text Layer", 4);
  write_test_layer_block(layer_extra, key, text_payload);

  patchy::psd::BigEndianWriter layer_info;
  layer_info.write_u16(1);
  layer_info.write_u32(12);
  layer_info.write_u32(10);
  layer_info.write_u32(82);
  layer_info.write_u32(210);
  layer_info.write_u16(0);
  write_ascii4(layer_info, "8BIM");
  write_ascii4(layer_info, "norm");
  layer_info.write_u8(255);
  layer_info.write_u8(0);
  layer_info.write_u8(0);
  layer_info.write_u8(0);
  layer_info.write_u32(static_cast<std::uint32_t>(layer_extra.bytes().size()));
  layer_info.write_bytes(layer_extra.bytes());
  if ((layer_info.bytes().size() % 2U) != 0) {
    layer_info.write_u8(0);
  }

  patchy::psd::BigEndianWriter layer_mask;
  layer_mask.write_u32(static_cast<std::uint32_t>(layer_info.bytes().size()));
  layer_mask.write_bytes(layer_info.bytes());
  layer_mask.write_u32(0);

  constexpr std::uint32_t width = 240;
  constexpr std::uint32_t height = 120;
  patchy::psd::BigEndianWriter writer;
  patchy::psd::write_header(writer, patchy::psd::Header{false, 3, height, width, 8, 3});
  writer.write_u32(0);
  writer.write_u32(0);
  writer.write_u32(static_cast<std::uint32_t>(layer_mask.bytes().size()));
  writer.write_bytes(layer_mask.bytes());
  writer.write_u16(0);
  for (std::size_t i = 0; i < static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3U; ++i) {
    writer.write_u8(255);
  }
  return writer.bytes();
}

// A minimal ICC v2 gray profile with a linear (gamma 1.0) tone curve. lcms accepts it,
// and mid-gray comes out visibly brighter in sRGB than a neutral copy would, so the
// test can tell the profile path from the fallback.
std::vector<std::uint8_t> test_linear_gray_icc_profile() {
  const std::string description = "Patchy Test Linear Gray";
  patchy::psd::BigEndianWriter desc;  // textDescriptionType
  write_ascii4(desc, "desc");
  desc.write_u32(0);
  desc.write_u32(static_cast<std::uint32_t>(description.size() + 1U));
  for (const char ch : description) {
    desc.write_u8(static_cast<std::uint8_t>(ch));
  }
  desc.write_u8(0);
  desc.write_u32(0);  // Unicode language code
  desc.write_u32(0);  // Unicode count
  desc.write_u16(0);  // ScriptCode code
  desc.write_u8(0);   // ScriptCode count
  for (int i = 0; i < 67; ++i) {
    desc.write_u8(0);
  }
  patchy::psd::BigEndianWriter wtpt;  // XYZType, D50
  write_ascii4(wtpt, "XYZ ");
  wtpt.write_u32(0);
  wtpt.write_u32(0x0000F6D6U);
  wtpt.write_u32(0x00010000U);
  wtpt.write_u32(0x0000D32DU);
  patchy::psd::BigEndianWriter ktrc;  // curveType with one entry: gamma as u8Fixed8
  write_ascii4(ktrc, "curv");
  ktrc.write_u32(0);
  ktrc.write_u32(1);
  ktrc.write_u16(0x0100);
  ktrc.write_u16(0);

  struct TagEntry {
    const char* signature;
    const std::vector<std::uint8_t>* data;
  };
  const std::vector<TagEntry> tags{{"desc", &desc.bytes()}, {"wtpt", &wtpt.bytes()}, {"kTRC", &ktrc.bytes()}};
  const auto padded = [](std::size_t size) { return (size + 3U) & ~static_cast<std::size_t>(3U); };
  std::size_t total = 128U + 4U + 12U * tags.size();
  for (const auto& tag : tags) {
    total += padded(tag.data->size());
  }

  patchy::psd::BigEndianWriter profile;
  profile.write_u32(static_cast<std::uint32_t>(total));
  profile.write_u32(0);           // preferred CMM
  profile.write_u32(0x02100000U);  // version 2.1
  write_ascii4(profile, "mntr");
  write_ascii4(profile, "GRAY");
  write_ascii4(profile, "XYZ ");
  for (int i = 0; i < 12; ++i) {
    profile.write_u8(0);  // creation date
  }
  write_ascii4(profile, "acsp");
  for (int i = 0; i < 24; ++i) {
    profile.write_u8(0);  // platform, flags, manufacturer, model, attributes
  }
  profile.write_u32(0);  // rendering intent
  profile.write_u32(0x0000F6D6U);
  profile.write_u32(0x00010000U);
  profile.write_u32(0x0000D32DU);
  profile.write_u32(0);  // creator
  for (int i = 0; i < 44; ++i) {
    profile.write_u8(0);
  }
  CHECK(profile.bytes().size() == 128U);
  profile.write_u32(static_cast<std::uint32_t>(tags.size()));
  std::size_t offset = 128U + 4U + 12U * tags.size();
  for (const auto& tag : tags) {
    for (int i = 0; i < 4; ++i) {
      profile.write_u8(static_cast<std::uint8_t>(tag.signature[i]));
    }
    profile.write_u32(static_cast<std::uint32_t>(offset));
    profile.write_u32(static_cast<std::uint32_t>(tag.data->size()));
    offset += padded(tag.data->size());
  }
  for (const auto& tag : tags) {
    profile.write_bytes(*tag.data);
    for (std::size_t pad = tag.data->size(); pad < padded(tag.data->size()); ++pad) {
      profile.write_u8(0);
    }
  }
  CHECK(profile.bytes().size() == total);
  return profile.bytes();
}

}  // namespace patchy::test
