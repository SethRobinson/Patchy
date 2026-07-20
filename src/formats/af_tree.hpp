#pragma once

// Schema-less parser for the tagged object tree inside an Affinity document's
// doc.dat stream. The format is a tree of "classes" (tagged, versioned records)
// whose fields carry a leading type byte that fully determines their byte
// layout, so the whole tree parses without knowing any class semantics; the
// importer then queries fields by their 4-char tag. Derived from observation of
// Affinity-authored files and the MIT-licensed afread project (see
// docs/file-formats.md ".af (Affinity)"). Qt-free.

#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace patchy::af {

class AfClass;

// A field value. Only the shapes the importer needs are distinguished; anything
// else parses into Skipped (still consumed, so the walk stays in sync).
struct AfEnum {
  std::uint16_t id{0};
  std::uint16_t version{0};
};

struct AfEmbedded {
  std::uint32_t tag{0};
  std::string data;  // ASCII payload (stream names like "d/1f")
};

// A "curve" field (type 0x2C): fixed-size records (vector path points ride
// here as 18-byte x/y/flags triples). bytes holds count * record_size.
struct AfCurveArray {
  std::uint16_t record_size{0};
  std::vector<std::uint8_t> bytes;
};

struct AfSkipped {};  // a value that parsed and was consumed but is not modeled

using AfValue = std::variant<AfSkipped, bool, std::int64_t, double, std::string,
                             AfEnum, AfEmbedded, AfCurveArray, std::vector<double>,
                             std::vector<std::int64_t>, std::vector<std::uint8_t>,
                             std::shared_ptr<AfClass>,
                             std::vector<std::shared_ptr<AfClass>>>;

struct AfField {
  std::uint32_t tag{0};
  AfValue value{};
};

class AfClass {
public:
  std::uint32_t type_tag{0};  // first ancestor tag (the concrete class name)
  std::uint32_t shared_id{0};
  std::vector<AfField> fields;

  [[nodiscard]] const AfField* field(std::uint32_t tag) const noexcept;
  [[nodiscard]] const AfClass* child_class(std::uint32_t tag) const noexcept;
  [[nodiscard]] bool bool_field(std::uint32_t tag, bool fallback) const noexcept;
  [[nodiscard]] double double_field(std::uint32_t tag, double fallback) const noexcept;
  [[nodiscard]] std::int64_t int_field(std::uint32_t tag, std::int64_t fallback) const noexcept;
  [[nodiscard]] std::string string_field(std::uint32_t tag) const;
  // A numeric vector field (int or double), converted to double; empty if absent
  // or not a numeric vector.
  [[nodiscard]] std::vector<double> vec_field(std::uint32_t tag) const;
};

struct AfDocument {
  std::uint32_t root_type_tag{0};
  std::uint32_t document_version{0};
  std::shared_ptr<AfClass> root;
};

// Parse a doc.dat payload. Throws std::runtime_error on malformed input; bounded
// against hostile files (class/field/recursion/list caps).
[[nodiscard]] AfDocument parse_tree(std::span<const std::uint8_t> bytes);

// Compile-time 4CC helper. On the wire a field tag is 4 ASCII bytes stored
// reversed ("Desc" appears as 'c','s','e','D'), so reading them little-endian
// yields 0x44657363 = 'D'<<24 | 'e'<<16 | 's'<<8 | 'c'. tag4("Desc") matches
// that, so callers compare against the reader's u32 directly.
constexpr std::uint32_t tag4(const char (&s)[5]) {
  return (static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[0])) << 24U) |
         (static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[1])) << 16U) |
         (static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[2])) << 8U) |
         static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[3]));
}

}  // namespace patchy::af
