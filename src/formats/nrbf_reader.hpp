#pragma once

// A bounded reader for the .NET Remoting Binary Format (MS-NRBF), the byte stream
// BinaryFormatter writes. Paint.NET's .pdn files carry their document graph in it
// (docs/pdn-format.md), and the reader knows nothing about Paint.NET: it turns the
// records into a flat table of objects keyed by object id, with member values that
// point at other objects by id, so forward references resolve by lookup once the
// stream has been read. Written from the published MS-NRBF specification.
//
// Qt-free. Every count, length and nesting level is checked against the bytes that
// remain and against Limits, so hostile input ends in nrbf::ParseError, never in a
// crash or an allocation the file does not pay for. MethodCall/MethodReturn records
// (remoting messages, never in a serialized file) are rejected.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace patchy::nrbf {

class ParseError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

struct Limits {
  std::size_t max_depth{64};            // nested inline records
  std::size_t max_objects{262'144};     // objects with an id (classes, strings, arrays)
  std::size_t max_value_slots{1'048'576};  // member values plus object-array elements, null runs included
  std::size_t max_members{4096};        // members of one class
};

// MS-NRBF 2.1.2.3 PrimitiveTypeEnumeration.
enum class PrimitiveType : std::uint8_t {
  Boolean = 1,
  Byte = 2,
  Char = 3,
  Decimal = 5,
  Double = 6,
  Int16 = 7,
  Int32 = 8,
  Int64 = 9,
  SByte = 10,
  Single = 11,
  TimeSpan = 12,
  DateTime = 13,
  UInt16 = 14,
  UInt32 = 15,
  UInt64 = 16,
  Null = 17,
  String = 18,
};

// The byte size of a fixed-size primitive; 0 for Char, Decimal, Null and String.
[[nodiscard]] std::size_t primitive_size(PrimitiveType type) noexcept;

struct Value {
  enum class Kind : std::uint8_t {
    Null,
    Boolean,
    Integer,   // signed primitives, TimeSpan (100 ns ticks)
    Unsigned,  // unsigned primitives, DateTime (raw 64-bit ticks with the kind bits)
    Real,      // Single, Double
    String,    // an inline string, Char or Decimal: Graph::text(index)
    Object,    // an object by id: an inline record or a member reference
  };
  Kind kind{Kind::Null};
  PrimitiveType primitive{PrimitiveType::Null};  // the wire type of a primitive
  union {
    bool boolean;
    std::int64_t integer;
    std::uint64_t unsigned_integer;
    double real;
    std::int32_t object_id;
    std::uint32_t text_index;
  };
  Value() : integer(0) {}
};

struct ClassInfo {
  std::string name;
  std::int32_t library_id{0};  // 0 for system classes
  std::vector<std::string> member_names;
};

struct Object {
  enum class Kind : std::uint8_t { Class, String, PrimitiveArray, ObjectArray };
  Kind kind{Kind::Class};
  std::int32_t id{0};
  std::uint32_t order{0};  // position in the stream among objects with ids
  std::shared_ptr<const ClassInfo> class_info;  // Class only
  // Class: one value per member. ObjectArray: one per element (null runs expanded).
  std::uint32_t first_value{0};
  std::uint32_t value_count{0};
  std::uint32_t text_index{0};  // String
  // PrimitiveArray: the element type and where the raw little-endian elements sit in
  // the parsed byte span (Char and Decimal elements are variable-sized).
  PrimitiveType element_type{PrimitiveType::Null};
  std::size_t element_count{0};
  std::size_t data_offset{0};
  std::size_t data_size{0};
  std::vector<std::int32_t> lengths;  // array dimensions (one for single arrays)

  [[nodiscard]] const std::string& class_name() const noexcept;
};

class Graph {
public:
  [[nodiscard]] std::int32_t root_id() const noexcept { return root_id_; }
  [[nodiscard]] const Object* root() const noexcept { return find(root_id_); }
  [[nodiscard]] const Object* find(std::int32_t id) const noexcept;
  [[nodiscard]] const std::vector<Object>& objects() const noexcept { return objects_; }
  // Byte offset (in the span given to parse) just past the MessageEnd record.
  [[nodiscard]] std::size_t end_offset() const noexcept { return end_offset_; }
  [[nodiscard]] const std::unordered_map<std::int32_t, std::string>& libraries() const noexcept {
    return libraries_;
  }
  // Member references whose target id never appeared in the stream.
  [[nodiscard]] std::size_t dangling_references() const noexcept { return dangling_references_; }

  [[nodiscard]] std::span<const Value> values(const Object& object) const noexcept;
  // A class member by exact name, or null.
  [[nodiscard]] const Value* member(const Object& object, std::string_view name) const noexcept;
  // The object a value points at (an inline record or a reference), or null.
  [[nodiscard]] const Object* object(const Value* value) const noexcept;
  [[nodiscard]] const Object* member_object(const Object& object, std::string_view name) const noexcept;
  [[nodiscard]] const std::string& text(std::uint32_t index) const noexcept;
  // The text of an inline string value or of a String object a value points at.
  [[nodiscard]] std::optional<std::string_view> string_value(const Value* value) const noexcept;
  [[nodiscard]] std::optional<std::string_view> member_string(const Object& object, std::string_view name) const noexcept;
  // Integers, unsigned integers and booleans as int64 (unsigned values above INT64_MAX: nullopt).
  [[nodiscard]] static std::optional<std::int64_t> integer_value(const Value* value) noexcept;
  [[nodiscard]] static std::optional<bool> bool_value(const Value* value) noexcept;
  [[nodiscard]] std::optional<std::int64_t> member_integer(const Object& object, std::string_view name) const noexcept;
  [[nodiscard]] std::optional<bool> member_bool(const Object& object, std::string_view name) const noexcept;

private:
  friend class Parser;
  std::int32_t root_id_{0};
  std::size_t end_offset_{0};
  std::size_t dangling_references_{0};
  std::vector<Object> objects_;
  std::unordered_map<std::int32_t, std::size_t> index_;
  std::vector<Value> values_;
  std::vector<std::string> texts_;
  std::unordered_map<std::int32_t, std::string> libraries_;
};

// Parses one NRBF stream starting at `offset`: a SerializationHeader record, records,
// and a MessageEnd. Throws ParseError on malformed or over-limit input.
[[nodiscard]] Graph parse(std::span<const std::uint8_t> bytes, std::size_t offset = 0, const Limits& limits = {});

// True when `offset` starts a SerializationHeader record for format version 1.0.
[[nodiscard]] bool looks_like_stream(std::span<const std::uint8_t> bytes, std::size_t offset = 0) noexcept;

}  // namespace patchy::nrbf
