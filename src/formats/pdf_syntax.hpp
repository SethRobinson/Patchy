#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// PDF object model and tokenizer (ISO 32000-1 clause 7.2-7.3). Qt-free, like every
// other module in src/formats. Nothing here knows about xref tables, filters, or
// page content; pdf_file.hpp layers document structure on top, pdf_content.hpp
// interprets content streams.
//
// The syntax is deliberately lenient. Real PDFs in the wild carry malformed
// numbers, unbalanced dictionaries, and truncated strings, and every viewer
// recovers rather than refusing the file, so the parser skips what it cannot
// understand and reports it instead of throwing.

namespace patchy::pdf {

class Object;

using Array = std::vector<Object>;
// Keys are name objects WITHOUT the leading slash, already #-decoded.
using Dictionary = std::map<std::string, Object, std::less<>>;

struct Reference {
  std::uint32_t number{0};
  std::uint16_t generation{0};

  friend bool operator==(const Reference&, const Reference&) = default;
};

// A stream's dictionary plus the location of its RAW (still encoded) bytes in the
// file. Decoding needs the filter chain and, for encrypted files, the object's
// decryption key, neither of which the tokenizer knows about, so streams stay raw
// here and pdf_file.hpp resolves them.
struct RawStream {
  Dictionary dict;
  std::size_t data_offset{0};
  std::size_t data_length{0};
};

class Object {
public:
  using Null = std::monostate;
  // A PDF name, kept distinct from a string: /Foo and (Foo) are different objects.
  struct Name {
    std::string value;
  };
  struct String {
    std::string value;
  };

  Object() = default;
  explicit Object(bool value) : value_(value) {}
  explicit Object(std::int64_t value) : value_(value) {}
  explicit Object(double value) : value_(value) {}
  explicit Object(Name value) : value_(std::move(value)) {}
  explicit Object(String value) : value_(std::move(value)) {}
  explicit Object(Reference value) : value_(value) {}
  explicit Object(Array value) : value_(std::make_shared<Array>(std::move(value))) {}
  explicit Object(Dictionary value) : value_(std::make_shared<Dictionary>(std::move(value))) {}
  explicit Object(RawStream value) : value_(std::make_shared<RawStream>(std::move(value))) {}

  [[nodiscard]] bool is_null() const noexcept { return std::holds_alternative<Null>(value_); }
  [[nodiscard]] bool is_boolean() const noexcept { return std::holds_alternative<bool>(value_); }
  [[nodiscard]] bool is_integer() const noexcept { return std::holds_alternative<std::int64_t>(value_); }
  [[nodiscard]] bool is_real() const noexcept { return std::holds_alternative<double>(value_); }
  [[nodiscard]] bool is_number() const noexcept { return is_integer() || is_real(); }
  [[nodiscard]] bool is_name() const noexcept { return std::holds_alternative<Name>(value_); }
  [[nodiscard]] bool is_string() const noexcept { return std::holds_alternative<String>(value_); }
  [[nodiscard]] bool is_array() const noexcept { return std::holds_alternative<std::shared_ptr<Array>>(value_); }
  [[nodiscard]] bool is_dictionary() const noexcept {
    return std::holds_alternative<std::shared_ptr<Dictionary>>(value_);
  }
  [[nodiscard]] bool is_stream() const noexcept { return std::holds_alternative<std::shared_ptr<RawStream>>(value_); }
  [[nodiscard]] bool is_reference() const noexcept { return std::holds_alternative<Reference>(value_); }

  [[nodiscard]] bool boolean(bool fallback = false) const noexcept {
    const auto* value = std::get_if<bool>(&value_);
    return value != nullptr ? *value : fallback;
  }
  // Integers and reals both answer number(); PDF freely writes "1" where a real is
  // expected and "1.0" where an integer is.
  [[nodiscard]] double number(double fallback = 0.0) const noexcept;
  [[nodiscard]] std::int64_t integer(std::int64_t fallback = 0) const noexcept;
  [[nodiscard]] std::string_view name(std::string_view fallback = {}) const noexcept;
  [[nodiscard]] std::string_view string(std::string_view fallback = {}) const noexcept;
  [[nodiscard]] const Array* array() const noexcept;
  // A stream answers dictionary() with its own dictionary: /Length, /Filter and the
  // rest are reached the same way whether or not the object carries data.
  [[nodiscard]] const Dictionary* dictionary() const noexcept;
  [[nodiscard]] const RawStream* stream() const noexcept;
  [[nodiscard]] std::optional<Reference> reference() const noexcept;

  // Direct (non-resolving) dictionary lookup; returns a null Object when absent or
  // when this is not a dictionary or stream. Use pdf_file.hpp's resolving lookups
  // when the value may be an indirect reference, which it usually may.
  [[nodiscard]] const Object& get(std::string_view key) const noexcept;

private:
  std::variant<Null, bool, std::int64_t, double, Name, String, std::shared_ptr<Array>, std::shared_ptr<Dictionary>,
               std::shared_ptr<RawStream>, Reference>
      value_;
};

[[nodiscard]] const Object& null_object() noexcept;

// PDF whitespace (7.2.2) and delimiters (7.2.2 table 2).
[[nodiscard]] bool is_whitespace(unsigned char byte) noexcept;
[[nodiscard]] bool is_delimiter(unsigned char byte) noexcept;
[[nodiscard]] bool is_regular(unsigned char byte) noexcept;

// One pass over a byte range, handing back objects and bare keywords. Content
// streams and file bodies use the same tokenizer: the only difference is that a
// content stream's keywords are operators and a body's are "obj"/"endobj"/"R".
class Lexer {
public:
  Lexer(std::span<const std::uint8_t> bytes, std::size_t position = 0) noexcept : bytes_(bytes), position_(position) {}

  [[nodiscard]] std::size_t position() const noexcept { return position_; }
  void seek(std::size_t position) noexcept { position_ = std::min(position, bytes_.size()); }
  [[nodiscard]] bool at_end() const noexcept { return position_ >= bytes_.size(); }
  // Everything from the cursor on. Inline images need it: their data has no
  // declared length, so the end has to be found by scanning the raw bytes.
  [[nodiscard]] std::span<const std::uint8_t> remaining() const noexcept { return bytes_.subspan(position_); }

  void skip_whitespace_and_comments() noexcept;

  // A token is either an object or a bare keyword (an operator, "obj", "R", ...).
  struct Token {
    Object object;
    std::string keyword;  // empty when the token is an object
    [[nodiscard]] bool is_keyword() const noexcept { return !keyword.empty(); }
  };

  // Reads the next token. Returns nullopt at end of input. Resolves the "N G R"
  // and "N G obj" three-token forms by lookahead, so callers never see them.
  [[nodiscard]] std::optional<Token> next();

  // Reads one complete object, skipping keywords. Used where the grammar demands a
  // value (a dictionary entry, an array element).
  [[nodiscard]] Object next_object();

private:
  [[nodiscard]] Object read_literal_string();
  [[nodiscard]] Object read_hex_string();
  [[nodiscard]] Object read_name();
  [[nodiscard]] Object read_array();
  [[nodiscard]] Object read_dictionary_or_stream();
  [[nodiscard]] Token read_number_or_reference();

  std::span<const std::uint8_t> bytes_;
  std::size_t position_{0};
  int depth_{0};
};

// #-decodes a name body (7.3.5): "A#20B" -> "A B".
[[nodiscard]] std::string decode_name(std::string_view raw);

}  // namespace patchy::pdf
