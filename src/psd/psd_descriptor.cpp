#include "psd/psd_descriptor.hpp"

#include <bit>
#include <stdexcept>

namespace patchy::psd {

std::array<char, 4> read_signature(BigEndianReader& reader) {
  const auto bytes = reader.read_bytes(4);
  return {static_cast<char>(bytes[0]), static_cast<char>(bytes[1]), static_cast<char>(bytes[2]),
          static_cast<char>(bytes[3])};
}

std::string key_string(const std::array<char, 4>& key) {
  return std::string(key.begin(), key.end());
}

void append_utf8(std::string& output, std::uint32_t codepoint) {
  if (codepoint <= 0x7FU) {
    output.push_back(static_cast<char>(codepoint));
  } else if (codepoint <= 0x7FFU) {
    output.push_back(static_cast<char>(0xC0U | (codepoint >> 6U)));
    output.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
  } else if (codepoint <= 0xFFFFU) {
    output.push_back(static_cast<char>(0xE0U | (codepoint >> 12U)));
    output.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
    output.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
  } else {
    output.push_back(static_cast<char>(0xF0U | (codepoint >> 18U)));
    output.push_back(static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3FU)));
    output.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
    output.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
  }
}

double read_f64(BigEndianReader& reader) {
  const auto bits = reader.read_u64();
  return std::bit_cast<double>(bits);
}

void write_f64(BigEndianWriter& writer, double value) {
  writer.write_u64(std::bit_cast<std::uint64_t>(value));
}

std::string read_descriptor_unicode_string(BigEndianReader& reader) {
  const auto code_unit_count = reader.read_u32();
  if (code_unit_count > reader.remaining() / 2U) {
    throw std::runtime_error("PSD descriptor string is truncated");
  }
  std::string decoded;
  for (std::uint32_t index = 0; index < code_unit_count; ++index) {
    auto codepoint = static_cast<std::uint32_t>(reader.read_u16());
    if (codepoint == 0) {
      continue;
    }
    if (codepoint >= 0xD800U && codepoint <= 0xDBFFU && index + 1 < code_unit_count) {
      const auto low = static_cast<std::uint32_t>(reader.read_u16());
      ++index;
      if (low >= 0xDC00U && low <= 0xDFFFU) {
        codepoint = 0x10000U + ((codepoint - 0xD800U) << 10U) + (low - 0xDC00U);
      } else {
        codepoint = '?';
      }
    }
    append_utf8(decoded, codepoint);
  }
  return decoded;
}

std::string read_descriptor_id(BigEndianReader& reader, bool& long_form) {
  const auto length = reader.read_u32();
  long_form = length != 0;
  if (length == 0) {
    return key_string(read_signature(reader));
  }
  const auto bytes = reader.read_bytes(length);
  return std::string(bytes.begin(), bytes.end());
}

std::string read_descriptor_id(BigEndianReader& reader) {
  bool long_form = false;
  return read_descriptor_id(reader, long_form);
}

DescriptorValue read_descriptor_value(BigEndianReader& reader, const std::array<char, 4>& type) {
  DescriptorValue value;
  const auto type_key = key_string(type);
  if (type_key == "bool") {
    value.type = DescriptorValue::Type::Bool;
    value.bool_value = reader.read_u8() != 0;
    return value;
  }
  if (type_key == "long") {
    value.type = DescriptorValue::Type::Integer;
    value.integer_value = static_cast<std::int32_t>(reader.read_u32());
    return value;
  }
  if (type_key == "comp") {
    value.type = DescriptorValue::Type::LargeInteger;
    value.large_integer_value = static_cast<std::int64_t>(reader.read_u64());
    value.integer_value = static_cast<std::int32_t>(value.large_integer_value);
    return value;
  }
  if (type_key == "doub") {
    value.type = DescriptorValue::Type::Double;
    value.double_value = read_f64(reader);
    return value;
  }
  if (type_key == "UntF") {
    value.type = DescriptorValue::Type::UnitFloat;
    value.unit = key_string(read_signature(reader));
    value.double_value = read_f64(reader);
    return value;
  }
  if (type_key == "TEXT") {
    value.type = DescriptorValue::Type::String;
    value.string_value = read_descriptor_unicode_string(reader);
    return value;
  }
  if (type_key == "enum") {
    value.type = DescriptorValue::Type::Enum;
    value.enum_type = read_descriptor_id(reader, value.enum_type_long_form);
    value.enum_value = read_descriptor_id(reader, value.enum_value_long_form);
    return value;
  }
  if (type_key == "Objc" || type_key == "GlbO") {
    value.type = DescriptorValue::Type::Object;
    value.object_is_global = type_key == "GlbO";
    value.object_value = std::make_shared<DescriptorObject>(read_descriptor(reader));
    return value;
  }
  if (type_key == "VlLs") {
    value.type = DescriptorValue::Type::List;
    const auto count = reader.read_u32();
    value.list_value.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
      value.list_value.push_back(read_descriptor_value(reader, read_signature(reader)));
    }
    return value;
  }
  if (type_key == "tdta" || type_key == "alis") {
    value.type = DescriptorValue::Type::Raw;
    value.raw_is_alias = type_key == "alis";
    const auto length = reader.read_u32();
    value.raw_value = reader.read_bytes(length);
    return value;
  }
  if (type_key == "type" || type_key == "GlbC") {
    value.type = DescriptorValue::Type::ClassReference;
    value.enum_type = read_descriptor_unicode_string(reader);
    value.enum_value = read_descriptor_id(reader, value.enum_value_long_form);
    value.string_value = value.enum_value;
    return value;
  }
  if (type_key == "UnFl") {
    // Unit float ARRAY (Photoshop warp meshes): one unit OSType, then packed doubles
    // (unlike VlLs there are no per-item type signatures).
    value.type = DescriptorValue::Type::UnitFloatArray;
    value.unit = key_string(read_signature(reader));
    const auto count = reader.read_u32();
    value.unit_floats.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
      value.unit_floats.push_back(read_f64(reader));
    }
    return value;
  }
  if (type_key == "ObAr") {
    // Object array: a u32 item count followed by a standard descriptor body (name,
    // class id, key count, keys) whose list values hold the per-item data.
    value.type = DescriptorValue::Type::ObjectArray;
    value.integer_value = static_cast<std::int32_t>(reader.read_u32());
    value.object_value = std::make_shared<DescriptorObject>(read_descriptor(reader));
    return value;
  }
  if (type_key == "obj ") {
    // Action Manager reference (e.g. blendOptions per-channel 'Chnl' references).
    value.type = DescriptorValue::Type::Reference;
    const auto count = reader.read_u32();
    value.reference_items.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
      DescriptorReferenceItem item;
      item.form = key_string(read_signature(reader));
      item.class_name = read_descriptor_unicode_string(reader);
      item.class_id = read_descriptor_id(reader, item.class_id_long_form);
      if (item.form == "prop") {
        item.id_a = read_descriptor_id(reader, item.id_a_long_form);
      } else if (item.form == "Enmr") {
        item.id_a = read_descriptor_id(reader, item.id_a_long_form);
        item.id_b = read_descriptor_id(reader, item.id_b_long_form);
      } else if (item.form == "rele" || item.form == "Idnt" || item.form == "indx") {
        item.number = reader.read_u32();
      } else if (item.form == "name") {
        item.name_value = read_descriptor_unicode_string(reader);
      } else if (item.form != "Clss") {
        throw std::runtime_error("Unsupported PSD reference form: " + item.form);
      }
      value.reference_items.push_back(std::move(item));
    }
    return value;
  }
  throw std::runtime_error("Unsupported PSD descriptor value type: " + type_key);
}

DescriptorObject read_descriptor(BigEndianReader& reader) {
  DescriptorObject object;
  object.name = read_descriptor_unicode_string(reader);
  object.class_id = read_descriptor_id(reader, object.class_id_long_form);
  const auto item_count = reader.read_u32();
  object.key_order.reserve(item_count);
  for (std::uint32_t index = 0; index < item_count; ++index) {
    bool key_long_form = false;
    const auto key = read_descriptor_id(reader, key_long_form);
    if (object.values.find(key) == object.values.end()) {
      object.key_order.push_back(DescriptorObject::KeyEntry{key, key_long_form});
    }
    object.values[key] = read_descriptor_value(reader, read_signature(reader));
  }
  return object;
}

const DescriptorValue* descriptor_value(const DescriptorObject& object, std::string_view key) {
  const auto found = object.values.find(std::string(key));
  return found == object.values.end() ? nullptr : &found->second;
}

const DescriptorObject* descriptor_object(const DescriptorObject& object, std::string_view key) {
  const auto* value = descriptor_value(object, key);
  if (value == nullptr || value->type != DescriptorValue::Type::Object || value->object_value == nullptr) {
    return nullptr;
  }
  return value->object_value.get();
}

bool descriptor_bool(const DescriptorObject& object, std::string_view key, bool fallback) {
  const auto* value = descriptor_value(object, key);
  return value != nullptr && value->type == DescriptorValue::Type::Bool ? value->bool_value : fallback;
}

double descriptor_number(const DescriptorObject& object, std::string_view key, double fallback) {
  const auto* value = descriptor_value(object, key);
  if (value == nullptr) {
    return fallback;
  }
  if (value->type == DescriptorValue::Type::UnitFloat || value->type == DescriptorValue::Type::Double) {
    return value->double_value;
  }
  if (value->type == DescriptorValue::Type::Integer) {
    return static_cast<double>(value->integer_value);
  }
  if (value->type == DescriptorValue::Type::LargeInteger) {
    return static_cast<double>(value->large_integer_value);
  }
  return fallback;
}

namespace {

void write_type_signature(BigEndianWriter& writer, const char* type) {
  for (int i = 0; i < 4; ++i) {
    writer.write_u8(static_cast<std::uint8_t>(type[i]));
  }
}

// Decodes one UTF-8 codepoint (the inverse of append_utf8); malformed input degrades
// to '?' rather than throwing (these strings originate from our own reader).
std::uint32_t next_utf8_codepoint(std::string_view text, std::size_t& cursor) {
  const auto lead = static_cast<std::uint8_t>(text[cursor]);
  ++cursor;
  std::size_t continuation_count = 0;
  std::uint32_t codepoint = 0;
  if (lead < 0x80U) {
    return lead;
  }
  if ((lead & 0xE0U) == 0xC0U) {
    codepoint = lead & 0x1FU;
    continuation_count = 1;
  } else if ((lead & 0xF0U) == 0xE0U) {
    codepoint = lead & 0x0FU;
    continuation_count = 2;
  } else if ((lead & 0xF8U) == 0xF0U) {
    codepoint = lead & 0x07U;
    continuation_count = 3;
  } else {
    return '?';
  }
  for (std::size_t i = 0; i < continuation_count; ++i) {
    if (cursor >= text.size() || (static_cast<std::uint8_t>(text[cursor]) & 0xC0U) != 0x80U) {
      return '?';
    }
    codepoint = (codepoint << 6U) | (static_cast<std::uint8_t>(text[cursor]) & 0x3FU);
    ++cursor;
  }
  return codepoint;
}

}  // namespace

void write_descriptor_unicode_string(BigEndianWriter& writer, std::string_view utf8) {
  std::vector<std::uint16_t> code_units;
  code_units.reserve(utf8.size() + 1U);
  std::size_t cursor = 0;
  while (cursor < utf8.size()) {
    const auto codepoint = next_utf8_codepoint(utf8, cursor);
    if (codepoint > 0xFFFFU) {
      const auto offset = codepoint - 0x10000U;
      code_units.push_back(static_cast<std::uint16_t>(0xD800U + (offset >> 10U)));
      code_units.push_back(static_cast<std::uint16_t>(0xDC00U + (offset & 0x3FFU)));
    } else {
      code_units.push_back(static_cast<std::uint16_t>(codepoint));
    }
  }
  // Photoshop includes a terminating NUL in the code-unit count (the reader strips it).
  code_units.push_back(0);
  writer.write_u32(static_cast<std::uint32_t>(code_units.size()));
  for (const auto unit : code_units) {
    writer.write_u16(unit);
  }
}

void write_descriptor_id(BigEndianWriter& writer, std::string_view id, bool long_form) {
  if (id.size() == 4U && !long_form) {
    writer.write_u32(0);
    for (const auto ch : id) {
      writer.write_u8(static_cast<std::uint8_t>(ch));
    }
    return;
  }
  writer.write_u32(static_cast<std::uint32_t>(id.size()));
  for (const auto ch : id) {
    writer.write_u8(static_cast<std::uint8_t>(ch));
  }
}

void write_descriptor_id(BigEndianWriter& writer, std::string_view id) {
  write_descriptor_id(writer, id, false);
}

void write_descriptor_value(BigEndianWriter& writer, const DescriptorValue& value) {
  switch (value.type) {
    case DescriptorValue::Type::Bool:
      write_type_signature(writer, "bool");
      writer.write_u8(value.bool_value ? 1 : 0);
      return;
    case DescriptorValue::Type::Integer:
      write_type_signature(writer, "long");
      writer.write_u32(static_cast<std::uint32_t>(value.integer_value));
      return;
    case DescriptorValue::Type::LargeInteger:
      write_type_signature(writer, "comp");
      writer.write_u64(static_cast<std::uint64_t>(value.large_integer_value));
      return;
    case DescriptorValue::Type::Double:
      write_type_signature(writer, "doub");
      write_f64(writer, value.double_value);
      return;
    case DescriptorValue::Type::UnitFloat: {
      write_type_signature(writer, "UntF");
      const auto unit = value.unit.size() == 4U ? value.unit : std::string("#Nne");
      for (const auto ch : unit) {
        writer.write_u8(static_cast<std::uint8_t>(ch));
      }
      write_f64(writer, value.double_value);
      return;
    }
    case DescriptorValue::Type::String:
      write_type_signature(writer, "TEXT");
      write_descriptor_unicode_string(writer, value.string_value);
      return;
    case DescriptorValue::Type::Enum:
      write_type_signature(writer, "enum");
      write_descriptor_id(writer, value.enum_type, value.enum_type_long_form);
      write_descriptor_id(writer, value.enum_value, value.enum_value_long_form);
      return;
    case DescriptorValue::Type::ClassReference:
      write_type_signature(writer, "type");
      write_descriptor_unicode_string(writer, value.enum_type);
      write_descriptor_id(writer, value.enum_value, value.enum_value_long_form);
      return;
    case DescriptorValue::Type::Object:
      write_type_signature(writer, value.object_is_global ? "GlbO" : "Objc");
      if (value.object_value != nullptr) {
        write_descriptor(writer, *value.object_value);
      } else {
        write_descriptor(writer, DescriptorObject{});
      }
      return;
    case DescriptorValue::Type::List:
      write_type_signature(writer, "VlLs");
      writer.write_u32(static_cast<std::uint32_t>(value.list_value.size()));
      for (const auto& item : value.list_value) {
        write_descriptor_value(writer, item);
      }
      return;
    case DescriptorValue::Type::Raw:
      write_type_signature(writer, value.raw_is_alias ? "alis" : "tdta");
      writer.write_u32(static_cast<std::uint32_t>(value.raw_value.size()));
      writer.write_bytes(value.raw_value);
      return;
    case DescriptorValue::Type::UnitFloatArray:
      write_type_signature(writer, "UnFl");
      write_type_signature(writer, value.unit.c_str());
      writer.write_u32(static_cast<std::uint32_t>(value.unit_floats.size()));
      for (const auto item : value.unit_floats) {
        write_f64(writer, item);
      }
      return;
    case DescriptorValue::Type::ObjectArray:
      write_type_signature(writer, "ObAr");
      writer.write_u32(static_cast<std::uint32_t>(value.integer_value));
      if (value.object_value != nullptr) {
        write_descriptor(writer, *value.object_value);
      } else {
        write_descriptor(writer, DescriptorObject{});
      }
      return;
    case DescriptorValue::Type::Reference:
      write_type_signature(writer, "obj ");
      writer.write_u32(static_cast<std::uint32_t>(value.reference_items.size()));
      for (const auto& item : value.reference_items) {
        if (item.form.size() != 4U) {
          throw std::runtime_error("PSD reference form must be a 4-character key");
        }
        write_type_signature(writer, item.form.c_str());
        write_descriptor_unicode_string(writer, item.class_name);
        write_descriptor_id(writer, item.class_id, item.class_id_long_form);
        if (item.form == "prop") {
          write_descriptor_id(writer, item.id_a, item.id_a_long_form);
        } else if (item.form == "Enmr") {
          write_descriptor_id(writer, item.id_a, item.id_a_long_form);
          write_descriptor_id(writer, item.id_b, item.id_b_long_form);
        } else if (item.form == "rele" || item.form == "Idnt" || item.form == "indx") {
          writer.write_u32(item.number);
        } else if (item.form == "name") {
          write_descriptor_unicode_string(writer, item.name_value);
        }
        // "Clss" carries only the class fields written above.
      }
      return;
    case DescriptorValue::Type::Empty:
      throw std::runtime_error("Cannot serialize an empty PSD descriptor value");
  }
  throw std::runtime_error("Unsupported PSD descriptor value type for writing");
}

void write_descriptor(BigEndianWriter& writer, const DescriptorObject& object) {
  write_descriptor_unicode_string(writer, object.name);
  if (object.class_id.empty()) {
    write_descriptor_id(writer, "null", false);
  } else {
    write_descriptor_id(writer, object.class_id, object.class_id_long_form);
  }
  writer.write_u32(static_cast<std::uint32_t>(object.values.size()));
  std::vector<DescriptorObject::KeyEntry> ordered;
  ordered.reserve(object.values.size());
  for (const auto& entry : object.key_order) {
    if (object.values.find(entry.key) != object.values.end()) {
      ordered.push_back(entry);
    }
  }
  for (const auto& [key, value] : object.values) {
    (void)value;
    bool already_listed = false;
    for (const auto& listed : ordered) {
      if (listed.key == key) {
        already_listed = true;
        break;
      }
    }
    if (!already_listed) {
      ordered.push_back(DescriptorObject::KeyEntry{key, false});
    }
  }
  for (const auto& entry : ordered) {
    write_descriptor_id(writer, entry.key, entry.long_form);
    write_descriptor_value(writer, object.values.at(entry.key));
  }
}

std::vector<std::uint8_t> decode_packbits(std::span<const std::uint8_t> encoded, std::size_t expected_size) {
  std::vector<std::uint8_t> decoded;
  decoded.reserve(expected_size);
  std::size_t cursor = 0;
  while (cursor < encoded.size() && decoded.size() < expected_size) {
    const auto header = static_cast<std::int8_t>(encoded[cursor++]);
    if (header >= 0) {
      const auto count = static_cast<std::size_t>(header) + 1U;
      if (cursor + count > encoded.size()) {
        throw std::runtime_error("PSD PackBits literal run is truncated");
      }
      decoded.insert(decoded.end(), encoded.begin() + static_cast<std::ptrdiff_t>(cursor),
                     encoded.begin() + static_cast<std::ptrdiff_t>(cursor + count));
      cursor += count;
    } else if (header != -128) {
      const auto count = static_cast<std::size_t>(1 - header);
      if (cursor >= encoded.size()) {
        throw std::runtime_error("PSD PackBits repeat run is truncated");
      }
      decoded.insert(decoded.end(), count, encoded[cursor++]);
    }
  }

  if (decoded.size() != expected_size) {
    throw std::runtime_error("PSD PackBits row decoded to the wrong length");
  }
  return decoded;
}

std::vector<std::uint8_t> encode_packbits_row(std::span<const std::uint8_t> row) {
  std::vector<std::uint8_t> encoded;
  encoded.reserve(row.size());

  std::size_t cursor = 0;
  while (cursor < row.size()) {
    std::size_t run_length = 1;
    while (cursor + run_length < row.size() && run_length < 128U &&
           row[cursor + run_length] == row[cursor]) {
      ++run_length;
    }

    if (run_length >= 3U) {
      encoded.push_back(static_cast<std::uint8_t>(257U - run_length));
      encoded.push_back(row[cursor]);
      cursor += run_length;
      continue;
    }

    const auto literal_start = cursor;
    std::size_t literal_length = 0;
    while (cursor < row.size() && literal_length < 128U) {
      run_length = 1;
      while (cursor + run_length < row.size() && run_length < 128U &&
             row[cursor + run_length] == row[cursor]) {
        ++run_length;
      }
      if (run_length >= 3U) {
        break;
      }

      const auto take = std::min(run_length, std::size_t{128} - literal_length);
      cursor += take;
      literal_length += take;
    }

    encoded.push_back(static_cast<std::uint8_t>(literal_length - 1U));
    encoded.insert(encoded.end(), row.begin() + static_cast<std::ptrdiff_t>(literal_start),
                   row.begin() + static_cast<std::ptrdiff_t>(literal_start + literal_length));
  }

  return encoded;
}

}  // namespace patchy::psd
