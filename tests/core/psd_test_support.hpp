#pragma once

// Shared PSD byte-level helpers moved from tests/test_main.cpp (used by more than one test group).

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace patchy { class Layer; }
namespace patchy::psd { class BigEndianReader; class BigEndianWriter; }

namespace patchy::test {

constexpr std::array<std::uint8_t, 8> kTestBlendIfIdentityEntry{0, 0, 255, 255, 0, 0, 255, 255};

std::vector<std::uint8_t> test_blend_if_identity_payload();

void write_ascii4(patchy::psd::BigEndianWriter& writer, const char (&value)[5]);

void write_pascal_padded(patchy::psd::BigEndianWriter& writer, const std::string& value,
                         std::size_t padded_multiple);

std::string read_pascal_padded(patchy::psd::BigEndianReader& reader, std::size_t padded_multiple);

std::vector<std::string> psd_raw_layer_record_names(std::span<const std::uint8_t> bytes);
// Each layer record's name and the file offset of its flags byte, in file order.
std::vector<std::pair<std::string, std::size_t>> psd_raw_layer_record_flag_offsets(std::span<const std::uint8_t> bytes);

struct PsdLayerChannelRecord {
  std::int16_t id{0};
  std::uint32_t length{0};
  std::uint16_t compression{0};
};

std::vector<PsdLayerChannelRecord> psd_layer_channel_records(std::span<const std::uint8_t> bytes);

std::uint32_t read_u32_be_at(std::span<const std::uint8_t> bytes, std::size_t offset);

std::vector<std::uint8_t> psd_layer_extra_data(std::span<const std::uint8_t> bytes, std::int16_t target_index);

std::vector<std::uint8_t> psd_first_layer_extra_data(std::span<const std::uint8_t> bytes);

std::optional<std::vector<std::uint8_t>> psd_layer_block_payload(std::span<const std::uint8_t> extra_data,
                                                                 const char (&target_key)[5]);

void write_test_layer_block(patchy::psd::BigEndianWriter& writer, const char (&key)[5],
                            std::span<const std::uint8_t> payload);

// A minimal 240x120 RGB PSD with one "Text Layer" (bounds 10,12 200x70, no channels)
// whose only tagged block is `key` (a modern "TySh" or a PS 5.x "tySh") with `payload`.
std::vector<std::uint8_t> single_text_layer_psd(std::span<const std::uint8_t> text_payload,
                                                const char (&key)[5] = "TySh");

std::optional<std::vector<std::uint8_t>> test_image_resource_payload(std::span<const std::uint8_t> resources,
                                                                     std::uint16_t id);

std::filesystem::path arrows_fixture_path();

bool layer_has_psd_block(const patchy::Layer& layer, const std::string& key);

std::vector<std::uint8_t> test_linear_gray_icc_profile();

}  // namespace patchy::test
