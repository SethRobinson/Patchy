#include "formats/nrbf_reader.hpp"

#include <cstring>
#include <limits>
#include <type_traits>
#include <utility>

// Record layouts follow MS-NRBF section 2 (the record type byte, then the fields named
// there). The diagnostics below are technical: callers that face users (the .pdn
// importer) turn any ParseError into their own translated notice.

namespace patchy::nrbf {

namespace {

// MS-NRBF 2.1.2.1 RecordTypeEnumeration.
enum RecordType : std::uint8_t {
  kSerializedStreamHeader = 0,
  kClassWithId = 1,
  kSystemClassWithMembers = 2,
  kClassWithMembers = 3,
  kSystemClassWithMembersAndTypes = 4,
  kClassWithMembersAndTypes = 5,
  kBinaryObjectString = 6,
  kBinaryArray = 7,
  kMemberPrimitiveTyped = 8,
  kMemberReference = 9,
  kObjectNull = 10,
  kMessageEnd = 11,
  kBinaryLibrary = 12,
  kObjectNullMultiple256 = 13,
  kObjectNullMultiple = 14,
  kArraySinglePrimitive = 15,
  kArraySingleObject = 16,
  kArraySingleString = 17,
  kMethodCall = 21,
  kMethodReturn = 22,
};

// MS-NRBF 2.1.2.2 BinaryTypeEnumeration.
enum BinaryType : std::uint8_t {
  kBinaryPrimitive = 0,
  kBinaryString = 1,
  kBinaryObject = 2,
  kBinarySystemClass = 3,
  kBinaryClass = 4,
  kBinaryObjectArray = 5,
  kBinaryStringArray = 6,
  kBinaryPrimitiveArray = 7,
};

struct MemberType {
  std::uint8_t binary_type{kBinaryObject};
  PrimitiveType primitive{PrimitiveType::Null};  // kBinaryPrimitive / kBinaryPrimitiveArray
};

struct ClassLayout {
  std::shared_ptr<const ClassInfo> info;
  std::vector<MemberType> member_types;  // empty when the record carried no type info
  bool typed{false};
};

// What one record produced, as seen by the member or element that holds it.
struct RecordResult {
  enum class Kind : std::uint8_t { Value, NullRun, End };
  Kind kind{Kind::Value};
  Value value;
  std::size_t null_count{0};
};

bool valid_primitive(std::uint8_t type) noexcept {
  return (type >= 1 && type <= 18) && type != 4;
}

}  // namespace

std::size_t primitive_size(PrimitiveType type) noexcept {
  switch (type) {
    case PrimitiveType::Boolean:
    case PrimitiveType::Byte:
    case PrimitiveType::SByte:
      return 1;
    case PrimitiveType::Int16:
    case PrimitiveType::UInt16:
      return 2;
    case PrimitiveType::Int32:
    case PrimitiveType::UInt32:
    case PrimitiveType::Single:
      return 4;
    case PrimitiveType::Int64:
    case PrimitiveType::UInt64:
    case PrimitiveType::Double:
    case PrimitiveType::TimeSpan:
    case PrimitiveType::DateTime:
      return 8;
    case PrimitiveType::Char:
    case PrimitiveType::Decimal:
    case PrimitiveType::Null:
    case PrimitiveType::String:
      return 0;
  }
  return 0;
}

const std::string& Object::class_name() const noexcept {
  static const std::string empty;
  return class_info != nullptr ? class_info->name : empty;
}

class Parser {
public:
  Parser(std::span<const std::uint8_t> bytes, std::size_t offset, const Limits& limits)
      : bytes_(bytes), position_(offset), limits_(limits) {}

  Graph run() {
    if (read_u8() != kSerializedStreamHeader) {
      throw ParseError("NRBF stream does not start with a serialization header");
    }
    graph_.root_id_ = read_i32();
    static_cast<void>(read_i32());  // header id
    const auto major = read_i32();
    const auto minor = read_i32();
    if (major != 1 || minor != 0) {
      throw ParseError("NRBF serialization header has an unknown version");
    }
    while (true) {
      const auto result = read_record(0, false);
      if (result.kind == RecordResult::Kind::End) {
        break;
      }
      if (result.kind == RecordResult::Kind::NullRun) {
        throw ParseError("NRBF null run outside an array");
      }
    }
    graph_.end_offset_ = position_;
    for (const auto& value : graph_.values_) {
      if (value.kind == Value::Kind::Object && graph_.find(value.object_id) == nullptr) {
        ++graph_.dangling_references_;
      }
    }
    return std::move(graph_);
  }

private:
  [[nodiscard]] std::size_t remaining() const noexcept { return bytes_.size() - position_; }

  void need(std::size_t count) const {
    if (count > remaining()) {
      throw ParseError("NRBF stream is truncated");
    }
  }

  std::uint8_t read_u8() {
    need(1);
    return bytes_[position_++];
  }

  template <typename T>
  T read_le() {
    need(sizeof(T));
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
      value |= static_cast<std::uint64_t>(bytes_[position_ + i]) << (8U * i);
    }
    position_ += sizeof(T);
    T result;
    if constexpr (sizeof(T) == 8) {
      std::memcpy(&result, &value, sizeof(T));
    } else {
      const auto narrow = static_cast<std::conditional_t<sizeof(T) == 4, std::uint32_t,
                                                         std::conditional_t<sizeof(T) == 2, std::uint16_t, std::uint8_t>>>(value);
      std::memcpy(&result, &narrow, sizeof(T));
    }
    return result;
  }

  std::int32_t read_i32() { return read_le<std::int32_t>(); }

  std::int32_t read_count() {
    const auto count = read_i32();
    if (count < 0) {
      throw ParseError("NRBF stream has a negative count");
    }
    return count;
  }

  // MS-NRBF 2.1.1.6 LengthPrefixedString: a 7-bit varint length of at most 5 bytes.
  std::string read_string() {
    std::uint32_t length = 0;
    for (int index = 0;; ++index) {
      if (index == 5) {
        throw ParseError("NRBF string length is malformed");
      }
      const auto byte = read_u8();
      if (index == 4 && (byte & 0xF8U) != 0) {
        throw ParseError("NRBF string length is malformed");
      }
      length |= static_cast<std::uint32_t>(byte & 0x7FU) << (7 * index);
      if ((byte & 0x80U) == 0) {
        break;
      }
    }
    need(length);
    std::string text(reinterpret_cast<const char*>(bytes_.data() + position_), length);
    position_ += length;
    return text;
  }

  void skip_string() { static_cast<void>(read_string()); }

  // One UTF-8 character (MS-NRBF Char).
  std::string read_char() {
    const auto lead = read_u8();
    const std::size_t extra = lead < 0x80U ? 0 : lead >= 0xF0U ? 3 : lead >= 0xE0U ? 2 : lead >= 0xC0U ? 1 : 0;
    need(extra);
    std::string text(1, static_cast<char>(lead));
    text.append(reinterpret_cast<const char*>(bytes_.data() + position_), extra);
    position_ += extra;
    return text;
  }

  std::uint32_t add_text(std::string text) {
    if (graph_.texts_.size() >= std::numeric_limits<std::uint32_t>::max()) {
      throw ParseError("NRBF stream has too many strings");
    }
    graph_.texts_.push_back(std::move(text));
    return static_cast<std::uint32_t>(graph_.texts_.size() - 1);
  }

  PrimitiveType read_primitive_type() {
    const auto type = read_u8();
    if (!valid_primitive(type)) {
      throw ParseError("NRBF primitive type is invalid");
    }
    return static_cast<PrimitiveType>(type);
  }

  Value read_primitive(PrimitiveType type) {
    Value value;
    value.primitive = type;
    switch (type) {
      case PrimitiveType::Boolean:
        value.kind = Value::Kind::Boolean;
        value.boolean = read_u8() != 0;
        break;
      case PrimitiveType::Byte:
        value.kind = Value::Kind::Unsigned;
        value.unsigned_integer = read_u8();
        break;
      case PrimitiveType::SByte:
        value.kind = Value::Kind::Integer;
        value.integer = read_le<std::int8_t>();
        break;
      case PrimitiveType::Int16:
        value.kind = Value::Kind::Integer;
        value.integer = read_le<std::int16_t>();
        break;
      case PrimitiveType::Int32:
        value.kind = Value::Kind::Integer;
        value.integer = read_le<std::int32_t>();
        break;
      case PrimitiveType::Int64:
      case PrimitiveType::TimeSpan:
        value.kind = Value::Kind::Integer;
        value.integer = read_le<std::int64_t>();
        break;
      case PrimitiveType::UInt16:
        value.kind = Value::Kind::Unsigned;
        value.unsigned_integer = read_le<std::uint16_t>();
        break;
      case PrimitiveType::UInt32:
        value.kind = Value::Kind::Unsigned;
        value.unsigned_integer = read_le<std::uint32_t>();
        break;
      case PrimitiveType::UInt64:
      case PrimitiveType::DateTime:
        value.kind = Value::Kind::Unsigned;
        value.unsigned_integer = read_le<std::uint64_t>();
        break;
      case PrimitiveType::Single:
        value.kind = Value::Kind::Real;
        value.real = static_cast<double>(read_le<float>());
        break;
      case PrimitiveType::Double:
        value.kind = Value::Kind::Real;
        value.real = read_le<double>();
        break;
      case PrimitiveType::Char:
        value.kind = Value::Kind::String;
        value.text_index = add_text(read_char());
        break;
      case PrimitiveType::Decimal:
      case PrimitiveType::String:
        value.kind = Value::Kind::String;
        value.text_index = add_text(read_string());
        break;
      case PrimitiveType::Null:
        value.kind = Value::Kind::Null;
        break;
    }
    return value;
  }

  // Walks `count` primitive elements, keeping only where they are.
  void skip_primitive_elements(PrimitiveType type, std::size_t count) {
    if (const auto size = primitive_size(type); size != 0) {
      if (count > remaining() / size) {
        throw ParseError("NRBF primitive array is truncated");
      }
      position_ += count * size;
      return;
    }
    if (type == PrimitiveType::Null || count > remaining()) {
      throw ParseError("NRBF primitive array is invalid");
    }
    for (std::size_t i = 0; i < count; ++i) {
      if (type == PrimitiveType::Char) {
        static_cast<void>(read_char());
      } else {
        skip_string();
      }
    }
  }

  MemberType read_additional_info(std::uint8_t binary_type) {
    MemberType type;
    type.binary_type = binary_type;
    switch (binary_type) {
      case kBinaryPrimitive:
      case kBinaryPrimitiveArray:
        type.primitive = read_primitive_type();
        if (binary_type == kBinaryPrimitive &&
            (type.primitive == PrimitiveType::Null || type.primitive == PrimitiveType::String)) {
          throw ParseError("NRBF member type is invalid");
        }
        break;
      case kBinarySystemClass:
        skip_string();
        break;
      case kBinaryClass:
        skip_string();
        static_cast<void>(read_i32());
        break;
      case kBinaryString:
      case kBinaryObject:
      case kBinaryObjectArray:
      case kBinaryStringArray:
        break;
      default:
        throw ParseError("NRBF binary type is invalid");
    }
    return type;
  }

  Object& add_object(Object object) {
    if (graph_.objects_.size() >= limits_.max_objects) {
      throw ParseError("NRBF stream has too many objects");
    }
    if (object.id == 0 || graph_.index_.contains(object.id)) {
      throw ParseError("NRBF object id is missing or repeated");
    }
    object.order = static_cast<std::uint32_t>(graph_.objects_.size());
    graph_.index_.emplace(object.id, graph_.objects_.size());
    graph_.objects_.push_back(std::move(object));
    return graph_.objects_.back();
  }

  // Reserves `count` value slots for an object; returns the first slot's index.
  std::uint32_t reserve_values(std::size_t count) {
    if (count > limits_.max_value_slots || graph_.values_.size() > limits_.max_value_slots - count) {
      throw ParseError("NRBF stream has too many values");
    }
    const auto first = graph_.values_.size();
    graph_.values_.resize(first + count);
    return static_cast<std::uint32_t>(first);
  }

  static Value object_value(std::int32_t id) {
    Value value;
    value.kind = Value::Kind::Object;
    value.object_id = id;
    return value;
  }

  void check_depth(std::size_t depth) const {
    if (depth > limits_.max_depth) {
      throw ParseError("NRBF records nest too deeply");
    }
  }

  RecordResult read_record(std::size_t depth, bool in_array) {
    check_depth(depth);
    auto type = read_u8();
    while (type == kBinaryLibrary) {
      const auto id = read_i32();
      auto name = read_string();
      graph_.libraries_[id] = std::move(name);
      type = read_u8();
    }
    RecordResult result;
    switch (type) {
      case kClassWithId:
      case kSystemClassWithMembers:
      case kClassWithMembers:
      case kSystemClassWithMembersAndTypes:
      case kClassWithMembersAndTypes:
        result.value = object_value(read_class(type, depth));
        return result;
      case kBinaryObjectString: {
        Object object;
        object.kind = Object::Kind::String;
        object.id = read_i32();
        object.text_index = add_text(read_string());
        const auto id = object.id;
        add_object(std::move(object));
        result.value = object_value(id);
        return result;
      }
      case kBinaryArray:
        result.value = object_value(read_binary_array(depth));
        return result;
      case kMemberPrimitiveTyped: {
        const auto primitive = read_primitive_type();
        if (primitive == PrimitiveType::Null || primitive == PrimitiveType::String) {
          throw ParseError("NRBF typed primitive is invalid");
        }
        result.value = read_primitive(primitive);
        return result;
      }
      case kMemberReference:
        result.value = object_value(read_i32());
        return result;
      case kObjectNull:
        return result;
      case kMessageEnd:
        result.kind = RecordResult::Kind::End;
        return result;
      case kObjectNullMultiple256:
      case kObjectNullMultiple:
        if (!in_array) {
          throw ParseError("NRBF null run outside an array");
        }
        result.kind = RecordResult::Kind::NullRun;
        result.null_count = type == kObjectNullMultiple256 ? read_u8() : static_cast<std::size_t>(read_count());
        return result;
      case kArraySinglePrimitive:
        result.value = object_value(read_single_primitive_array());
        return result;
      case kArraySingleObject:
      case kArraySingleString:
        result.value = object_value(read_single_object_array(depth));
        return result;
      case kMethodCall:
      case kMethodReturn:
        throw ParseError("NRBF remoting messages are not supported");
      default:
        throw ParseError("NRBF record type is invalid");
    }
  }

  // A member or element that must be one value (not a null run, not the end).
  Value read_value_record(std::size_t depth) {
    const auto result = read_record(depth, false);
    if (result.kind != RecordResult::Kind::Value) {
      throw ParseError("NRBF member value is invalid");
    }
    return result.value;
  }

  std::int32_t read_class(std::uint8_t type, std::size_t depth) {
    Object object;
    object.kind = Object::Kind::Class;
    std::shared_ptr<const ClassLayout> layout;
    if (type == kClassWithId) {
      object.id = read_i32();
      const auto metadata_id = read_i32();
      const auto found = layouts_.find(metadata_id);
      if (found == layouts_.end()) {
        throw ParseError("NRBF class refers to unknown metadata");
      }
      layout = found->second;
    } else {
      auto info = std::make_shared<ClassInfo>();
      object.id = read_i32();
      info->name = read_string();
      const auto member_count = static_cast<std::size_t>(read_count());
      if (member_count > limits_.max_members || member_count > remaining()) {
        throw ParseError("NRBF class has too many members");
      }
      info->member_names.reserve(member_count);
      for (std::size_t i = 0; i < member_count; ++i) {
        info->member_names.push_back(read_string());
      }
      auto built = std::make_shared<ClassLayout>();
      if (type == kSystemClassWithMembersAndTypes || type == kClassWithMembersAndTypes) {
        built->typed = true;
        std::vector<std::uint8_t> binary_types(member_count);
        for (auto& binary_type : binary_types) {
          binary_type = read_u8();
        }
        built->member_types.reserve(member_count);
        for (const auto binary_type : binary_types) {
          built->member_types.push_back(read_additional_info(binary_type));
        }
      }
      if (type == kClassWithMembers || type == kClassWithMembersAndTypes) {
        info->library_id = read_i32();
      }
      built->info = std::move(info);
      layouts_[object.id] = built;
      layout = std::move(built);
    }
    object.class_info = layout->info;
    const auto member_count = layout->info->member_names.size();
    object.first_value = reserve_values(member_count);
    object.value_count = static_cast<std::uint32_t>(member_count);
    const auto id = object.id;
    const auto first = object.first_value;
    add_object(std::move(object));
    for (std::size_t i = 0; i < member_count; ++i) {
      Value value;
      if (layout->typed && layout->member_types[i].binary_type == kBinaryPrimitive) {
        value = read_primitive(layout->member_types[i].primitive);
      } else {
        value = read_value_record(depth + 1);
      }
      graph_.values_[first + i] = value;
    }
    return id;
  }

  std::int32_t read_single_primitive_array() {
    Object object;
    object.kind = Object::Kind::PrimitiveArray;
    object.id = read_i32();
    const auto count = static_cast<std::size_t>(read_count());
    object.element_type = read_primitive_type();
    if (object.element_type == PrimitiveType::String) {
      throw ParseError("NRBF primitive array type is invalid");
    }
    object.element_count = count;
    object.lengths = {static_cast<std::int32_t>(count)};
    object.data_offset = position_;
    skip_primitive_elements(object.element_type, count);
    object.data_size = position_ - object.data_offset;
    const auto id = object.id;
    add_object(std::move(object));
    return id;
  }

  // Reads `count` element records (null runs expanded) into fresh value slots.
  void read_object_elements(Object& object, std::size_t count, std::size_t depth) {
    // Each element record takes at least one byte; only null runs cover more slots.
    object.first_value = reserve_values(count);
    object.value_count = static_cast<std::uint32_t>(count);
    const auto first = object.first_value;
    add_object(std::move(object));
    std::size_t index = 0;
    while (index < count) {
      const auto result = read_record(depth + 1, true);
      if (result.kind == RecordResult::Kind::End) {
        throw ParseError("NRBF array ends early");
      }
      if (result.kind == RecordResult::Kind::NullRun) {
        if (result.null_count == 0 || result.null_count > count - index) {
          throw ParseError("NRBF null run overruns its array");
        }
        index += result.null_count;  // the slots are already null
        continue;
      }
      graph_.values_[first + index] = result.value;
      ++index;
    }
  }

  std::int32_t read_single_object_array(std::size_t depth) {
    Object object;
    object.kind = Object::Kind::ObjectArray;
    object.id = read_i32();
    const auto count = static_cast<std::size_t>(read_count());
    object.element_count = count;
    object.lengths = {static_cast<std::int32_t>(count)};
    const auto id = object.id;
    read_object_elements(object, count, depth);
    return id;
  }

  std::int32_t read_binary_array(std::size_t depth) {
    Object object;
    object.id = read_i32();
    const auto array_type = read_u8();
    if (array_type > 5) {
      throw ParseError("NRBF array type is invalid");
    }
    const auto rank = static_cast<std::size_t>(read_count());
    if (rank == 0 || rank > 32) {
      throw ParseError("NRBF array rank is invalid");
    }
    std::size_t count = 1;
    for (std::size_t i = 0; i < rank; ++i) {
      const auto length = read_count();
      object.lengths.push_back(length);
      // Primitive elements are bounded by the bytes left and object elements by the
      // value slots; this only keeps the product from overflowing.
      if (length != 0 && count > (std::size_t{1} << 40U) / static_cast<std::size_t>(length)) {
        throw ParseError("NRBF array is too large");
      }
      count *= static_cast<std::size_t>(length);
    }
    if (array_type >= 3) {  // the *Offset variants carry lower bounds
      for (std::size_t i = 0; i < rank; ++i) {
        static_cast<void>(read_i32());
      }
    }
    const auto element = read_additional_info(read_u8());
    object.element_count = count;
    const auto id = object.id;
    if (element.binary_type == kBinaryPrimitive) {
      object.kind = Object::Kind::PrimitiveArray;
      object.element_type = element.primitive;
      object.data_offset = position_;
      skip_primitive_elements(element.primitive, count);
      object.data_size = position_ - object.data_offset;
      add_object(std::move(object));
      return id;
    }
    object.kind = Object::Kind::ObjectArray;
    read_object_elements(object, count, depth);
    return id;
  }

  std::span<const std::uint8_t> bytes_;
  std::size_t position_;
  Limits limits_;
  Graph graph_;
  std::unordered_map<std::int32_t, std::shared_ptr<const ClassLayout>> layouts_;
};

const Object* Graph::find(std::int32_t id) const noexcept {
  const auto found = index_.find(id);
  return found != index_.end() ? &objects_[found->second] : nullptr;
}

std::span<const Value> Graph::values(const Object& object) const noexcept {
  if (object.kind != Object::Kind::Class && object.kind != Object::Kind::ObjectArray) {
    return {};
  }
  return std::span<const Value>(values_).subspan(object.first_value, object.value_count);
}

const Value* Graph::member(const Object& object, std::string_view name) const noexcept {
  if (object.kind != Object::Kind::Class || object.class_info == nullptr) {
    return nullptr;
  }
  const auto& names = object.class_info->member_names;
  for (std::size_t i = 0; i < names.size() && i < object.value_count; ++i) {
    if (names[i] == name) {
      return &values_[object.first_value + i];
    }
  }
  return nullptr;
}

const Object* Graph::object(const Value* value) const noexcept {
  if (value == nullptr || value->kind != Value::Kind::Object) {
    return nullptr;
  }
  return find(value->object_id);
}

const Object* Graph::member_object(const Object& object, std::string_view name) const noexcept {
  return this->object(member(object, name));
}

const std::string& Graph::text(std::uint32_t index) const noexcept {
  static const std::string empty;
  return index < texts_.size() ? texts_[index] : empty;
}

std::optional<std::string_view> Graph::string_value(const Value* value) const noexcept {
  if (value == nullptr) {
    return std::nullopt;
  }
  if (value->kind == Value::Kind::String) {
    return std::string_view(text(value->text_index));
  }
  if (const auto* target = object(value); target != nullptr && target->kind == Object::Kind::String) {
    return std::string_view(text(target->text_index));
  }
  return std::nullopt;
}

std::optional<std::string_view> Graph::member_string(const Object& object, std::string_view name) const noexcept {
  return string_value(member(object, name));
}

std::optional<std::int64_t> Graph::integer_value(const Value* value) noexcept {
  if (value == nullptr) {
    return std::nullopt;
  }
  switch (value->kind) {
    case Value::Kind::Integer:
      return value->integer;
    case Value::Kind::Unsigned:
      if (value->unsigned_integer > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
      }
      return static_cast<std::int64_t>(value->unsigned_integer);
    case Value::Kind::Boolean:
      return value->boolean ? 1 : 0;
    default:
      return std::nullopt;
  }
}

std::optional<bool> Graph::bool_value(const Value* value) noexcept {
  if (value == nullptr) {
    return std::nullopt;
  }
  if (value->kind == Value::Kind::Boolean) {
    return value->boolean;
  }
  if (const auto integer = integer_value(value)) {
    return *integer != 0;
  }
  return std::nullopt;
}

std::optional<std::int64_t> Graph::member_integer(const Object& object, std::string_view name) const noexcept {
  return integer_value(member(object, name));
}

std::optional<bool> Graph::member_bool(const Object& object, std::string_view name) const noexcept {
  return bool_value(member(object, name));
}

Graph parse(std::span<const std::uint8_t> bytes, std::size_t offset, const Limits& limits) {
  if (offset > bytes.size()) {
    throw ParseError("NRBF stream is truncated");
  }
  Parser parser(bytes, offset, limits);
  return parser.run();
}

bool looks_like_stream(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
  if (offset > bytes.size() || bytes.size() - offset < 17 || bytes[offset] != kSerializedStreamHeader) {
    return false;
  }
  const auto i32 = [&](std::size_t at) {
    return static_cast<std::uint32_t>(bytes[at]) | (static_cast<std::uint32_t>(bytes[at + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[at + 2]) << 16) | (static_cast<std::uint32_t>(bytes[at + 3]) << 24);
  };
  return i32(offset + 9) == 1U && i32(offset + 13) == 0U;
}

}  // namespace patchy::nrbf
