#include "formats/af_tree.hpp"

#include "formats/binary_le.hpp"

#include <cstring>
#include <stdexcept>
#include <utility>

namespace patchy::af {

namespace {

constexpr std::uint32_t kDocTag = 0x534BFF00U;  // doc.dat stream header

// Hostile-input caps. Real documents: hundreds to low thousands of classes,
// nesting a few dozen deep, arrays up to a few thousand tiles.
constexpr std::size_t kMaxClasses = 2'000'000;
constexpr int kMaxDepth = 512;
constexpr std::uint32_t kMaxArray = 1U << 24U;
constexpr std::uint32_t kMaxString = 1U << 24U;

class TreeReader {
public:
  TreeReader(std::span<const std::uint8_t> bytes, std::map<std::uint32_t, std::shared_ptr<AfClass>>& shared)
      : reader_(bytes, "Affinity document tree is truncated"), bytes_(bytes), shared_(shared) {}

  AfDocument parse() {
    AfDocument document;
    if (reader_.read_u32() != kDocTag) {
      throw std::runtime_error("Affinity document tree has a bad header");
    }
    const std::uint16_t file_version = reader_.read_u16();
    document.root_type_tag = reader_.read_u32();
    reader_.read_u16();  // tag version
    if (file_version >= 2) {
      document.document_version = reader_.read_u32();
    }
    document.root = std::make_shared<AfClass>();
    document.root->type_tag = document.root_type_tag;
    read_fields(*document.root, true, 0);
    return document;
  }

private:
  void bump_class_count() {
    if (++class_count_ > kMaxClasses) {
      throw std::runtime_error("Affinity document tree is implausibly large");
    }
  }

  void read_fields(AfClass& parent, bool with_tag, int depth) {
    if (depth > kMaxDepth) {
      throw std::runtime_error("Affinity document tree nests too deeply");
    }
    for (;;) {
      std::uint8_t type = reader_.read_u8();
      const bool array = (type & 0x80U) != 0;
      type &= 0x7FU;
      if (type == 0x00) {
        return;
      }
      if (type > 0x77) {
        throw std::runtime_error("Affinity document tree has an unknown field type");
      }
      AfField field;
      if (with_tag) {
        field.tag = reader_.read_u32();
      }
      field.value = read_value(type, array, depth);
      parent.fields.push_back(std::move(field));
    }
  }

  AfValue read_value(std::uint8_t type, bool array, int depth) {
    switch (type) {
      case 0x01: return read_scalar<std::uint8_t>(array);
      case 0x02: return read_scalar<std::uint16_t>(array);
      case 0x03: return read_scalar<std::uint32_t>(array);
      case 0x04: return read_scalar<std::uint64_t>(array);
      case 0x05: return read_scalar<std::int8_t>(array);
      case 0x06: return read_scalar<std::int16_t>(array);
      case 0x07: return read_scalar<std::int32_t>(array);
      case 0x08: return read_scalar<std::int64_t>(array);
      case 0x09: return read_scalar_double<float>(array);
      case 0x0A: return read_scalar_double<double>(array);
      case 0x2F:
      case 0x34: return read_scalar<std::uint32_t>(array);
      case 0x29: return read_bool(array);
      case 0x2A: return read_enum(array);
      case 0x2B:
      case 0x2E: return read_string(array);
      case 0x2C: return read_curve(array);
      case 0x2D: return read_binary(array);
      case 0x30:
      case 0x31:
      case 0x32: return read_class_field(type, array, depth);
      case 0x33: return read_embedded(array);
      case 0x75: return read_flags(array);
      default: break;
    }
    if (type >= 0x15 && type <= 0x19) {
      return read_int_vector(type - 0x15 + 2, array);
    }
    if (type >= 0x1F && type <= 0x23) {
      return read_float_vector(type - 0x1F + 2, 4, array);
    }
    if (type >= 0x24 && type <= 0x28) {
      return read_float_vector(type - 0x24 + 2, 8, array);
    }
    if (type >= 0x35 && type <= 0x74) {
      return read_sized_struct(static_cast<std::size_t>(type) - 0x34, array);
    }
    throw std::runtime_error("Affinity document tree has an unhandled field type");
  }

  template <typename T>
  AfValue read_scalar(bool array) {
    if (!array) {
      return static_cast<std::int64_t>(read_int<T>());
    }
    const std::uint32_t count = read_count();
    std::vector<std::int64_t> values(count);
    for (std::uint32_t i = 0; i < count; ++i) {
      values[i] = static_cast<std::int64_t>(read_int<T>());
    }
    return values;
  }

  template <typename T>
  AfValue read_scalar_double(bool array) {
    if (!array) {
      return static_cast<double>(read_float<T>());
    }
    const std::uint32_t count = read_count();
    std::vector<double> values(count);
    for (std::uint32_t i = 0; i < count; ++i) {
      values[i] = static_cast<double>(read_float<T>());
    }
    return values;
  }

  AfValue read_bool(bool array) {
    if (!array) {
      return reader_.read_u8() != 0;
    }
    const std::uint32_t count = read_count();
    std::vector<std::int64_t> values(count);
    const std::uint32_t whole = count / 8;
    std::uint32_t index = 0;
    for (std::uint32_t b = 0; b < whole; ++b) {
      const std::uint8_t byte = reader_.read_u8();
      for (std::uint8_t bit = 0; bit < 8; ++bit) {
        values[index++] = ((byte >> bit) & 1U) != 0 ? 1 : 0;
      }
    }
    const std::uint32_t remainder = count % 8;
    if (remainder != 0) {
      const std::uint8_t byte = reader_.read_u8();
      for (std::uint32_t bit = 0; bit < remainder; ++bit) {
        values[index++] = ((byte >> bit) & 1U) != 0 ? 1 : 0;
      }
    }
    return values;
  }

  AfValue read_enum(bool array) {
    if (!array) {
      AfEnum value;
      value.id = reader_.read_u16();
      value.version = reader_.read_u16();
      return value;
    }
    const std::uint32_t count = read_count();
    reader_.read_u16();  // shared version
    std::vector<std::int64_t> ids(count);
    for (std::uint32_t i = 0; i < count; ++i) {
      ids[i] = reader_.read_u16();
    }
    return ids;
  }

  AfValue read_string(bool array) {
    if (!array) {
      return read_one_string();
    }
    reader_.read_u32();  // total size
    const std::uint32_t count = read_count();
    // Represent string arrays as skipped (not needed by the importer) but still
    // consume them so the walk stays aligned.
    for (std::uint32_t i = 0; i < count; ++i) {
      (void)read_one_string();
    }
    return AfSkipped{};
  }

  AfValue read_curve(bool array) {
    const std::uint32_t count = array ? read_count() : 1;
    const std::uint16_t size = reader_.read_u16();
    for (std::uint32_t i = 0; i < count; ++i) {
      reader_.skip(size);
    }
    return AfSkipped{};
  }

  AfValue read_binary(bool array) {
    if (array) {
      throw std::runtime_error("Affinity document tree has an invalid binary array");
    }
    const std::uint32_t size = read_length();
    std::vector<std::uint8_t> data(size);
    for (std::uint32_t i = 0; i < size; ++i) {
      data[i] = reader_.read_u8();
    }
    return data;
  }

  AfValue read_embedded(bool array) {
    if (array) {
      throw std::runtime_error("Affinity document tree has an invalid embedded array");
    }
    AfEmbedded value;
    value.tag = reader_.read_u32();
    const std::uint32_t size = read_length();
    value.data.resize(size);
    for (std::uint32_t i = 0; i < size; ++i) {
      value.data[i] = static_cast<char>(reader_.read_u8());
    }
    return value;
  }

  AfValue read_flags(bool array) {
    if (array) {
      throw std::runtime_error("Affinity document tree has an invalid flags array");
    }
    reader_.read_u16();  // version
    const std::uint8_t count = reader_.read_u8();
    if (count > 8) {
      throw std::runtime_error("Affinity document tree has an invalid flags count");
    }
    std::uint64_t flags = 0;
    for (std::uint8_t i = 0; i < count; ++i) {
      flags |= static_cast<std::uint64_t>(reader_.read_u8()) << (8U * i);
    }
    return static_cast<std::int64_t>(flags);
  }

  AfValue read_int_vector(int components, bool array) {
    const std::uint32_t count = array ? read_count() : 1;
    if (!array) {
      std::vector<std::int64_t> values(components);
      for (int c = 0; c < components; ++c) {
        values[c] = read_int<std::int32_t>();
      }
      return values;
    }
    // Arrays of vectors are not needed; consume and skip.
    for (std::uint32_t i = 0; i < count; ++i) {
      for (int c = 0; c < components; ++c) {
        (void)read_int<std::int32_t>();
      }
    }
    return AfSkipped{};
  }

  AfValue read_float_vector(int components, int element_size, bool array) {
    const std::uint32_t count = array ? read_count() : 1;
    if (!array) {
      std::vector<double> values(components);
      for (int c = 0; c < components; ++c) {
        values[c] = element_size == 4 ? static_cast<double>(read_float<float>())
                                      : read_float<double>();
      }
      return values;
    }
    // Small arrays flatten into one double vector (gradient stop positions,
    // Posn = float2 pairs, ride here); implausibly large ones stay skipped.
    const std::uint64_t total = static_cast<std::uint64_t>(count) * static_cast<std::uint64_t>(components);
    if (total <= 4096) {
      std::vector<double> values(static_cast<std::size_t>(total));
      for (auto& value : values) {
        value = element_size == 4 ? static_cast<double>(read_float<float>()) : read_float<double>();
      }
      return values;
    }
    for (std::uint32_t i = 0; i < count; ++i) {
      for (int c = 0; c < components; ++c) {
        if (element_size == 4) {
          (void)read_float<float>();
        } else {
          (void)read_float<double>();
        }
      }
    }
    return AfSkipped{};
  }

  AfValue read_sized_struct(std::size_t size, bool array) {
    if (!array && size >= 8 && size <= 64) {
      // Preserve the payload of small scalar structs (colors like the spread
      // background's RGBA float quad live here); the importer decodes the
      // bytes it understands. Arrays and outliers stay consumed-but-skipped.
      std::vector<std::uint8_t> data(size);
      for (std::size_t i = 0; i < size; ++i) {
        data[i] = reader_.read_u8();
      }
      return data;
    }
    const std::uint32_t count = array ? read_count() : 1;
    for (std::uint32_t i = 0; i < count; ++i) {
      reader_.skip(size);
    }
    return AfSkipped{};
  }

  AfValue read_class_field(std::uint8_t type, bool array, int depth) {
    std::uint32_t count = 1;
    std::uint32_t array_tag = 0;
    std::uint16_t array_version = 0;
    bool have_array_header = false;
    if (array) {
      count = read_count();
      if (type == 0x32) {
        array_tag = reader_.read_u32();
        array_version = reader_.read_u16();
        have_array_header = true;
      }
    }
    std::vector<std::shared_ptr<AfClass>> classes;
    classes.reserve(count <= 4096 ? count : 0);
    for (std::uint32_t i = 0; i < count; ++i) {
      classes.push_back(read_class(type, depth + 1, have_array_header, array_tag, array_version));
    }
    if (!array) {
      return classes.empty() ? std::shared_ptr<AfClass>() : classes.front();
    }
    return classes;
  }

  std::shared_ptr<AfClass> read_class(std::uint8_t type, int depth, bool have_array_header,
                                      std::uint32_t array_tag, std::uint16_t array_version) {
    if (depth > kMaxDepth) {
      throw std::runtime_error("Affinity document tree nests too deeply");
    }
    if (type == 0x30) {
      bump_class_count();
      auto cls = std::make_shared<AfClass>();
      read_fields(*cls, false, depth);
      return cls;
    }
    const std::uint8_t flag = reader_.read_u8();
    if (flag == 0) {
      return nullptr;  // null class
    }
    if (type == 0x31) {
      if (flag == 2) {
        const std::uint32_t id = reader_.read_u32();
        auto found = shared_.find(id);
        return found != shared_.end() ? found->second : nullptr;
      }
      if (flag != 1) {
        throw std::runtime_error("Affinity document tree has an invalid shared class");
      }
      bump_class_count();
      auto cls = std::make_shared<AfClass>();
      cls->shared_id = reader_.read_u32();
      shared_[cls->shared_id] = cls;
      bool first_type = true;
      for (;;) {
        const std::uint8_t type_flag = reader_.read_u8();
        if (type_flag == 1) {
          const std::uint32_t tag = reader_.read_u32();
          if (first_type) {
            cls->type_tag = tag;
            first_type = false;
          }
          break;
        }
        if (type_flag == 2) {
          break;
        }
        if (type_flag == 0) {
          const std::uint32_t tag = reader_.read_u32();
          reader_.read_u16();  // ancestor version (u16 in v3; afread read u32 for v1/v2)
          if (first_type) {
            cls->type_tag = tag;
            first_type = false;
          }
          AfClass ancestor_fields;  // ancestor field lists are usually empty
          read_fields(ancestor_fields, true, depth);
          continue;
        }
        throw std::runtime_error("Affinity document tree has an invalid class type flag " +
                                 std::to_string(static_cast<int>(type_flag)) + " at offset " +
                                 std::to_string(reader_.position()));
      }
      read_fields(*cls, true, depth);
      return cls;
    }
    // type == 0x32
    if (flag != 1) {
      throw std::runtime_error("Affinity document tree has an invalid class");
    }
    bump_class_count();
    auto cls = std::make_shared<AfClass>();
    if (have_array_header) {
      cls->type_tag = array_tag;
      (void)array_version;
    } else {
      cls->type_tag = reader_.read_u32();
      reader_.read_u16();  // type version
    }
    read_fields(*cls, true, depth);
    return cls;
  }

  // ---- primitive readers ----

  std::uint32_t read_count() {
    const std::uint32_t count = reader_.read_u32();
    if (count > kMaxArray) {
      throw std::runtime_error("Affinity document tree has an implausible array");
    }
    return count;
  }

  std::uint32_t read_length() {
    const std::uint32_t length = reader_.read_u32();
    if (length > bytes_.size()) {
      throw std::runtime_error("Affinity document tree has an implausible length");
    }
    return length;
  }

  std::string read_one_string() {
    const std::uint32_t length = reader_.read_u32();
    if (length > kMaxString) {
      throw std::runtime_error("Affinity document tree has an implausible string");
    }
    std::string value(length, '\0');
    for (std::uint32_t i = 0; i < length; ++i) {
      value[i] = static_cast<char>(reader_.read_u8());
    }
    return value;
  }

  template <typename T>
  T read_int() {
    if constexpr (sizeof(T) == 1) {
      return static_cast<T>(reader_.read_u8());
    } else if constexpr (sizeof(T) == 2) {
      return static_cast<T>(reader_.read_u16());
    } else if constexpr (sizeof(T) == 4) {
      return static_cast<T>(reader_.read_u32());
    } else {
      return static_cast<T>(reader_.read_u64());
    }
  }

  template <typename T>
  T read_float() {
    if constexpr (sizeof(T) == 4) {
      const std::uint32_t bits = reader_.read_u32();
      float value = 0.0F;
      std::memcpy(&value, &bits, sizeof(value));
      return value;
    } else {
      const std::uint64_t bits = reader_.read_u64();
      double value = 0.0;
      std::memcpy(&value, &bits, sizeof(value));
      return value;
    }
  }

  LittleEndianReader reader_;
  std::span<const std::uint8_t> bytes_;
  std::map<std::uint32_t, std::shared_ptr<AfClass>>& shared_;
  std::size_t class_count_{0};
};

}  // namespace

const AfField* AfClass::field(std::uint32_t tag) const noexcept {
  for (const auto& f : fields) {
    if (f.tag == tag) {
      return &f;
    }
  }
  return nullptr;
}

const AfClass* AfClass::child_class(std::uint32_t tag) const noexcept {
  const auto* f = field(tag);
  if (f == nullptr) {
    return nullptr;
  }
  if (const auto* ptr = std::get_if<std::shared_ptr<AfClass>>(&f->value)) {
    return ptr->get();
  }
  return nullptr;
}

bool AfClass::bool_field(std::uint32_t tag, bool fallback) const noexcept {
  const auto* f = field(tag);
  if (f == nullptr) {
    return fallback;
  }
  if (const auto* value = std::get_if<bool>(&f->value)) {
    return *value;
  }
  if (const auto* value = std::get_if<std::int64_t>(&f->value)) {
    return *value != 0;
  }
  return fallback;
}

double AfClass::double_field(std::uint32_t tag, double fallback) const noexcept {
  const auto* f = field(tag);
  if (f == nullptr) {
    return fallback;
  }
  if (const auto* value = std::get_if<double>(&f->value)) {
    return *value;
  }
  if (const auto* value = std::get_if<std::int64_t>(&f->value)) {
    return static_cast<double>(*value);
  }
  return fallback;
}

std::int64_t AfClass::int_field(std::uint32_t tag, std::int64_t fallback) const noexcept {
  const auto* f = field(tag);
  if (f == nullptr) {
    return fallback;
  }
  if (const auto* value = std::get_if<std::int64_t>(&f->value)) {
    return *value;
  }
  if (const auto* value = std::get_if<double>(&f->value)) {
    return static_cast<std::int64_t>(*value);
  }
  return fallback;
}

std::string AfClass::string_field(std::uint32_t tag) const {
  const auto* f = field(tag);
  if (f == nullptr) {
    return {};
  }
  if (const auto* value = std::get_if<std::string>(&f->value)) {
    return *value;
  }
  return {};
}

std::vector<double> AfClass::vec_field(std::uint32_t tag) const {
  const auto* f = field(tag);
  if (f == nullptr) {
    return {};
  }
  if (const auto* value = std::get_if<std::vector<double>>(&f->value)) {
    return *value;
  }
  if (const auto* value = std::get_if<std::vector<std::int64_t>>(&f->value)) {
    std::vector<double> out(value->size());
    for (std::size_t i = 0; i < value->size(); ++i) {
      out[i] = static_cast<double>((*value)[i]);
    }
    return out;
  }
  return {};
}

AfDocument parse_tree(std::span<const std::uint8_t> bytes) {
  std::map<std::uint32_t, std::shared_ptr<AfClass>> shared;
  TreeReader reader(bytes, shared);
  return reader.parse();
}

}  // namespace patchy::af
