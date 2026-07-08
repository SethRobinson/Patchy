#pragma once

#include "psd/psd_binary.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Shared Photoshop serialized-data primitives: the ActionDescriptor structure (used by PSD tagged
// blocks and by ABR v6+ 'desc' blocks) plus PackBits decoding and the small big-endian helpers the
// descriptor format depends on. Extracted from psd_document_io.cpp so the ABR brush reader can
// reuse them.
namespace patchy::psd {

struct DescriptorObject;

struct DescriptorValue {
  enum class Type {
    Empty,
    Bool,
    Integer,
    Double,
    UnitFloat,
    String,
    Enum,
    Object,
    List,
    Raw
  };

  Type type{Type::Empty};
  bool bool_value{false};
  std::int32_t integer_value{0};
  double double_value{0.0};
  std::string unit;
  std::string string_value;
  std::string enum_type;
  std::string enum_value;
  std::shared_ptr<DescriptorObject> object_value;
  std::vector<DescriptorValue> list_value;
  std::vector<std::uint8_t> raw_value;
};

struct DescriptorObject {
  std::string name;
  std::string class_id;
  std::map<std::string, DescriptorValue> values;
};

[[nodiscard]] std::array<char, 4> read_signature(BigEndianReader& reader);
[[nodiscard]] std::string key_string(const std::array<char, 4>& key);
void append_utf8(std::string& output, std::uint32_t codepoint);
[[nodiscard]] double read_f64(BigEndianReader& reader);

[[nodiscard]] std::string read_descriptor_unicode_string(BigEndianReader& reader);
[[nodiscard]] std::string read_descriptor_id(BigEndianReader& reader);
[[nodiscard]] DescriptorValue read_descriptor_value(BigEndianReader& reader, const std::array<char, 4>& type);
[[nodiscard]] DescriptorObject read_descriptor(BigEndianReader& reader);

[[nodiscard]] const DescriptorValue* descriptor_value(const DescriptorObject& object, std::string_view key);
[[nodiscard]] const DescriptorObject* descriptor_object(const DescriptorObject& object, std::string_view key);
[[nodiscard]] bool descriptor_bool(const DescriptorObject& object, std::string_view key, bool fallback = false);
[[nodiscard]] double descriptor_number(const DescriptorObject& object, std::string_view key,
                                       double fallback = 0.0);

[[nodiscard]] std::vector<std::uint8_t> decode_packbits(std::span<const std::uint8_t> encoded,
                                                        std::size_t expected_size);
// One row of PackBits/ByteRun1 encoding (shared by the PSD RLE writer and the ILBM BODY
// writer; the algorithms are identical).
[[nodiscard]] std::vector<std::uint8_t> encode_packbits_row(std::span<const std::uint8_t> row);

}  // namespace patchy::psd
