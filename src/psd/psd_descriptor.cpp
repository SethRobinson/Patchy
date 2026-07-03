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

std::string read_descriptor_id(BigEndianReader& reader) {
  const auto length = reader.read_u32();
  if (length == 0) {
    return key_string(read_signature(reader));
  }
  const auto bytes = reader.read_bytes(length);
  return std::string(bytes.begin(), bytes.end());
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
    value.type = DescriptorValue::Type::Integer;
    value.integer_value = static_cast<std::int32_t>(reader.read_u64());
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
    value.enum_type = read_descriptor_id(reader);
    value.enum_value = read_descriptor_id(reader);
    return value;
  }
  if (type_key == "Objc" || type_key == "GlbO") {
    value.type = DescriptorValue::Type::Object;
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
    const auto length = reader.read_u32();
    value.raw_value = reader.read_bytes(length);
    return value;
  }
  if (type_key == "type" || type_key == "GlbC") {
    value.type = DescriptorValue::Type::String;
    (void)read_descriptor_unicode_string(reader);
    value.string_value = read_descriptor_id(reader);
    return value;
  }
  throw std::runtime_error("Unsupported PSD descriptor value type: " + type_key);
}

DescriptorObject read_descriptor(BigEndianReader& reader) {
  DescriptorObject object;
  object.name = read_descriptor_unicode_string(reader);
  object.class_id = read_descriptor_id(reader);
  const auto item_count = reader.read_u32();
  for (std::uint32_t index = 0; index < item_count; ++index) {
    const auto key = read_descriptor_id(reader);
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
  return fallback;
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

}  // namespace patchy::psd
