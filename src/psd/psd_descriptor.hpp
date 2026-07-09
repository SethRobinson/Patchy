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
    LargeInteger,  // 'comp' (8-byte); kept distinct so write_descriptor_value round-trips it
    Double,
    UnitFloat,
    String,
    Enum,
    ClassReference,  // 'type'/'GlbC': string_value = class id, enum_type = the unicode name
    Object,
    List,
    Raw
  };

  Type type{Type::Empty};
  bool bool_value{false};
  std::int32_t integer_value{0};
  std::int64_t large_integer_value{0};
  double double_value{0.0};
  std::string unit;
  std::string string_value;
  std::string enum_type;   // ClassReference: the unicode class name instead
  std::string enum_value;  // ClassReference: the class id instead
  // Descriptor ids are stored on disk either as a 4-char charID (length field 0) or as
  // a length-prefixed stringID — and the form is NOT derivable from the text (Photoshop
  // writes the 4-char key "warp" as a stringID). These flags record each id's form so
  // write_descriptor can reproduce the exact bytes.
  bool enum_type_long_form{false};
  bool enum_value_long_form{false};
  bool object_is_global{false};  // Object read from 'GlbO' rather than 'Objc'
  bool raw_is_alias{false};      // Raw read from 'alis' rather than 'tdta'
  std::shared_ptr<DescriptorObject> object_value;
  std::vector<DescriptorValue> list_value;
  std::vector<std::uint8_t> raw_value;
};

struct DescriptorObject {
  struct KeyEntry {
    std::string key;
    bool long_form{false};  // see the id-form note on DescriptorValue
  };

  std::string name;
  std::string class_id;
  bool class_id_long_form{false};
  std::map<std::string, DescriptorValue> values;
  // File order of the keys in `values` (with each key's on-disk id form), appended by
  // read_descriptor. write_descriptor emits keys in this order (Photoshop is
  // layout-sensitive for some descriptors), then any keys present only in `values` in
  // map order with the 4-char-means-charID default form.
  std::vector<KeyEntry> key_order;
};

[[nodiscard]] std::array<char, 4> read_signature(BigEndianReader& reader);
[[nodiscard]] std::string key_string(const std::array<char, 4>& key);
void append_utf8(std::string& output, std::uint32_t codepoint);
[[nodiscard]] double read_f64(BigEndianReader& reader);
void write_f64(BigEndianWriter& writer, double value);

[[nodiscard]] std::string read_descriptor_unicode_string(BigEndianReader& reader);
[[nodiscard]] std::string read_descriptor_id(BigEndianReader& reader);
[[nodiscard]] std::string read_descriptor_id(BigEndianReader& reader, bool& long_form);
[[nodiscard]] DescriptorValue read_descriptor_value(BigEndianReader& reader, const std::array<char, 4>& type);
[[nodiscard]] DescriptorObject read_descriptor(BigEndianReader& reader);

// Generic writer, the mirror of read_descriptor: emits the object with its key order
// and every id's recorded charID/stringID form preserved; unicode strings gain the
// trailing NUL Photoshop includes in the code-unit count. Read → write is
// byte-identical for Photoshop-authored payloads (pinned by
// psd_descriptor_writer_round_trips_sold). Ids without a recorded form (hand-built
// descriptors) default to the 4-char-means-charID convention.
void write_descriptor_unicode_string(BigEndianWriter& writer, std::string_view utf8);
void write_descriptor_id(BigEndianWriter& writer, std::string_view id);
void write_descriptor_id(BigEndianWriter& writer, std::string_view id, bool long_form);
void write_descriptor_value(BigEndianWriter& writer, const DescriptorValue& value);
void write_descriptor(BigEndianWriter& writer, const DescriptorObject& object);

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
