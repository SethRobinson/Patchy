#include "formats/pdf_syntax.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>

namespace patchy::pdf {
namespace {

// Deep enough for any real document; a malformed file that nests forever must not
// blow the stack. Acrobat's own limit is far lower than this in practice.
constexpr int kMaximumDepth = 256;

int hex_value(unsigned char byte) noexcept {
  if (byte >= '0' && byte <= '9') {
    return byte - '0';
  }
  if (byte >= 'a' && byte <= 'f') {
    return byte - 'a' + 10;
  }
  if (byte >= 'A' && byte <= 'F') {
    return byte - 'A' + 10;
  }
  return -1;
}

}  // namespace

double Object::number(double fallback) const noexcept {
  if (const auto* value = std::get_if<std::int64_t>(&value_); value != nullptr) {
    return static_cast<double>(*value);
  }
  if (const auto* value = std::get_if<double>(&value_); value != nullptr) {
    return *value;
  }
  return fallback;
}

std::int64_t Object::integer(std::int64_t fallback) const noexcept {
  if (const auto* value = std::get_if<std::int64_t>(&value_); value != nullptr) {
    return *value;
  }
  if (const auto* value = std::get_if<double>(&value_); value != nullptr) {
    return static_cast<std::int64_t>(*value);
  }
  return fallback;
}

std::string_view Object::name(std::string_view fallback) const noexcept {
  const auto* value = std::get_if<Name>(&value_);
  return value != nullptr ? std::string_view(value->value) : fallback;
}

std::string_view Object::string(std::string_view fallback) const noexcept {
  const auto* value = std::get_if<String>(&value_);
  return value != nullptr ? std::string_view(value->value) : fallback;
}

const Array* Object::array() const noexcept {
  const auto* value = std::get_if<std::shared_ptr<Array>>(&value_);
  return value != nullptr ? value->get() : nullptr;
}

const Dictionary* Object::dictionary() const noexcept {
  if (const auto* value = std::get_if<std::shared_ptr<Dictionary>>(&value_); value != nullptr) {
    return value->get();
  }
  if (const auto* value = std::get_if<std::shared_ptr<RawStream>>(&value_); value != nullptr && *value != nullptr) {
    return &(*value)->dict;
  }
  return nullptr;
}

const RawStream* Object::stream() const noexcept {
  const auto* value = std::get_if<std::shared_ptr<RawStream>>(&value_);
  return value != nullptr ? value->get() : nullptr;
}

std::optional<Reference> Object::reference() const noexcept {
  const auto* value = std::get_if<Reference>(&value_);
  return value != nullptr ? std::optional<Reference>(*value) : std::nullopt;
}

const Object& Object::get(std::string_view key) const noexcept {
  const auto* dict = dictionary();
  if (dict == nullptr) {
    return null_object();
  }
  const auto it = dict->find(key);
  return it != dict->end() ? it->second : null_object();
}

const Object& null_object() noexcept {
  static const Object instance;
  return instance;
}

bool is_whitespace(unsigned char byte) noexcept {
  return byte == 0x00 || byte == 0x09 || byte == 0x0A || byte == 0x0C || byte == 0x0D || byte == 0x20;
}

bool is_delimiter(unsigned char byte) noexcept {
  return byte == '(' || byte == ')' || byte == '<' || byte == '>' || byte == '[' || byte == ']' || byte == '{' ||
         byte == '}' || byte == '/' || byte == '%';
}

bool is_regular(unsigned char byte) noexcept {
  return !is_whitespace(byte) && !is_delimiter(byte);
}

std::string decode_name(std::string_view raw) {
  std::string decoded;
  decoded.reserve(raw.size());
  for (std::size_t index = 0; index < raw.size(); ++index) {
    if (raw[index] != '#' || index + 2 >= raw.size()) {
      decoded.push_back(raw[index]);
      continue;
    }
    const int high = hex_value(static_cast<unsigned char>(raw[index + 1]));
    const int low = hex_value(static_cast<unsigned char>(raw[index + 2]));
    if (high < 0 || low < 0) {
      decoded.push_back(raw[index]);
      continue;
    }
    decoded.push_back(static_cast<char>(high * 16 + low));
    index += 2;
  }
  return decoded;
}

void Lexer::skip_whitespace_and_comments() noexcept {
  while (position_ < bytes_.size()) {
    const auto byte = bytes_[position_];
    if (is_whitespace(byte)) {
      ++position_;
      continue;
    }
    if (byte != '%') {
      return;
    }
    // A comment runs to the end of the line (7.2.4).
    while (position_ < bytes_.size() && bytes_[position_] != '\r' && bytes_[position_] != '\n') {
      ++position_;
    }
  }
}

Object Lexer::read_literal_string() {
  ++position_;  // consume '('
  std::string value;
  int nesting = 1;
  while (position_ < bytes_.size()) {
    const auto byte = bytes_[position_++];
    if (byte == '\\') {
      if (position_ >= bytes_.size()) {
        break;
      }
      const auto escaped = bytes_[position_++];
      switch (escaped) {
        case 'n': value.push_back('\n'); break;
        case 'r': value.push_back('\r'); break;
        case 't': value.push_back('\t'); break;
        case 'b': value.push_back('\b'); break;
        case 'f': value.push_back('\f'); break;
        case '(': value.push_back('('); break;
        case ')': value.push_back(')'); break;
        case '\\': value.push_back('\\'); break;
        case '\r':
          // A backslash before an end-of-line marker continues the line; CRLF counts once.
          if (position_ < bytes_.size() && bytes_[position_] == '\n') {
            ++position_;
          }
          break;
        case '\n': break;
        default:
          if (escaped >= '0' && escaped <= '7') {
            int octal = escaped - '0';
            for (int digit = 0; digit < 2 && position_ < bytes_.size(); ++digit) {
              const auto next_byte = bytes_[position_];
              if (next_byte < '0' || next_byte > '7') {
                break;
              }
              octal = octal * 8 + (next_byte - '0');
              ++position_;
            }
            value.push_back(static_cast<char>(octal & 0xFF));
          } else {
            // An unknown escape drops the backslash and keeps the character (7.3.4.2).
            value.push_back(static_cast<char>(escaped));
          }
          break;
      }
      continue;
    }
    if (byte == '(') {
      ++nesting;
      value.push_back('(');
      continue;
    }
    if (byte == ')') {
      if (--nesting == 0) {
        break;
      }
      value.push_back(')');
      continue;
    }
    value.push_back(static_cast<char>(byte));
  }
  return Object(Object::String{std::move(value)});
}

Object Lexer::read_hex_string() {
  ++position_;  // consume '<'
  std::string value;
  int high = -1;
  while (position_ < bytes_.size()) {
    const auto byte = bytes_[position_++];
    if (byte == '>') {
      break;
    }
    const int digit = hex_value(byte);
    if (digit < 0) {
      continue;  // whitespace and junk are ignored inside a hex string
    }
    if (high < 0) {
      high = digit;
      continue;
    }
    value.push_back(static_cast<char>(high * 16 + digit));
    high = -1;
  }
  // An odd final digit is padded with zero (7.3.4.3).
  if (high >= 0) {
    value.push_back(static_cast<char>(high * 16));
  }
  return Object(Object::String{std::move(value)});
}

Object Lexer::read_name() {
  ++position_;  // consume '/'
  const std::size_t start = position_;
  while (position_ < bytes_.size() && is_regular(bytes_[position_])) {
    ++position_;
  }
  const std::string_view raw(reinterpret_cast<const char*>(bytes_.data()) + start, position_ - start);
  return Object(Object::Name{decode_name(raw)});
}

Object Lexer::read_array() {
  ++position_;  // consume '['
  Array items;
  if (depth_ >= kMaximumDepth) {
    return Object(std::move(items));
  }
  ++depth_;
  while (true) {
    skip_whitespace_and_comments();
    if (position_ >= bytes_.size()) {
      break;
    }
    if (bytes_[position_] == ']') {
      ++position_;
      break;
    }
    const auto before = position_;
    auto token = next();
    if (!token.has_value() || position_ == before) {
      break;  // no progress: malformed input, stop rather than spin
    }
    if (token->is_keyword()) {
      // "null", "true", "false" arrive as objects; anything else inside an array is
      // junk from a damaged file and is dropped.
      continue;
    }
    items.push_back(std::move(token->object));
  }
  --depth_;
  return Object(std::move(items));
}

Object Lexer::read_dictionary_or_stream() {
  position_ += 2;  // consume '<<'
  Dictionary dict;
  if (depth_ >= kMaximumDepth) {
    return Object(std::move(dict));
  }
  ++depth_;
  while (true) {
    skip_whitespace_and_comments();
    if (position_ >= bytes_.size()) {
      break;
    }
    if (position_ + 1 < bytes_.size() && bytes_[position_] == '>' && bytes_[position_ + 1] == '>') {
      position_ += 2;
      break;
    }
    if (bytes_[position_] != '/') {
      // Only names can be keys. Skip a stray token rather than abandoning the
      // dictionary; damaged files routinely carry one.
      const auto before = position_;
      (void)next();
      if (position_ == before) {
        ++position_;
      }
      continue;
    }
    auto key = read_name();
    auto value = next_object();
    dict.emplace(std::string(key.name()), std::move(value));
  }
  --depth_;

  // "stream" immediately after the dictionary makes this a stream object. The
  // keyword is followed by CRLF or LF, never by CR alone (7.3.8.1).
  const auto after_dict = position_;
  skip_whitespace_and_comments();
  const std::string_view tail(reinterpret_cast<const char*>(bytes_.data()) + position_,
                              std::min<std::size_t>(6, bytes_.size() - position_));
  if (tail != "stream") {
    position_ = after_dict;
    return Object(std::move(dict));
  }
  position_ += 6;
  if (position_ < bytes_.size() && bytes_[position_] == '\r') {
    ++position_;
  }
  if (position_ < bytes_.size() && bytes_[position_] == '\n') {
    ++position_;
  }

  RawStream stream;
  stream.dict = std::move(dict);
  stream.data_offset = position_;
  // The declared length may be an indirect reference or simply wrong, so the real
  // extent is settled by pdf_file.hpp, which can resolve references and fall back to
  // searching for "endstream". Record what is knowable here.
  stream.data_length = 0;
  return Object(std::move(stream));
}

Lexer::Token Lexer::read_number_or_reference() {
  const std::size_t start = position_;
  while (position_ < bytes_.size() && is_regular(bytes_[position_])) {
    ++position_;
  }
  const std::string text(reinterpret_cast<const char*>(bytes_.data()) + start, position_ - start);

  const bool is_real_number = text.find('.') != std::string::npos;
  if (!is_real_number) {
    std::int64_t value = 0;
    const auto* first = text.data();
    const auto* last = text.data() + text.size();
    const auto parsed = std::from_chars(first, last, value);
    if (parsed.ec != std::errc{} || parsed.ptr != last) {
      // Not a clean integer after all (for example "6.-2" or "--5"); fall through to
      // the permissive real parse below.
      return Token{Object(std::strtod(text.c_str(), nullptr)), {}};
    }

    // Look ahead for "G R" (an indirect reference) or "G obj" (an object header).
    // Both are three tokens in the grammar and are resolved here so no caller has to.
    const auto save = position_;
    skip_whitespace_and_comments();
    const std::size_t generation_start = position_;
    while (position_ < bytes_.size() && is_regular(bytes_[position_])) {
      ++position_;
    }
    const std::string generation_text(reinterpret_cast<const char*>(bytes_.data()) + generation_start,
                                      position_ - generation_start);
    std::int64_t generation = 0;
    const auto* generation_first = generation_text.data();
    const auto* generation_last = generation_text.data() + generation_text.size();
    const auto generation_parsed = std::from_chars(generation_first, generation_last, generation);
    if (!generation_text.empty() && generation_parsed.ec == std::errc{} && generation_parsed.ptr == generation_last &&
        generation >= 0 && value >= 0) {
      const auto before_keyword = position_;
      skip_whitespace_and_comments();
      const auto remaining = bytes_.size() - position_;
      const std::string_view keyword(reinterpret_cast<const char*>(bytes_.data()) + position_,
                                     std::min<std::size_t>(3, remaining));
      if (keyword.substr(0, 1) == "R" && (remaining == 1 || !is_regular(bytes_[position_ + 1]))) {
        position_ += 1;
        return Token{Object(Reference{static_cast<std::uint32_t>(value), static_cast<std::uint16_t>(generation)}), {}};
      }
      if (keyword == "obj") {
        position_ += 3;
        return Token{Object(Reference{static_cast<std::uint32_t>(value), static_cast<std::uint16_t>(generation)}),
                     "obj"};
      }
      position_ = before_keyword;
    }
    position_ = save;
    return Token{Object(value), {}};
  }

  // PDF reals allow forms std::from_chars rejects ("4.", ".5", "-.002", "6.02e23"
  // from broken producers), so strtod does the work.
  return Token{Object(std::strtod(text.c_str(), nullptr)), {}};
}

std::optional<Lexer::Token> Lexer::next() {
  skip_whitespace_and_comments();
  if (position_ >= bytes_.size()) {
    return std::nullopt;
  }
  const auto byte = bytes_[position_];
  switch (byte) {
    case '(': return Token{read_literal_string(), {}};
    case '/': return Token{read_name(), {}};
    case '[': return Token{read_array(), {}};
    case ']':
      ++position_;  // stray closer: report as a keyword so callers can resynchronize
      return Token{Object(), "]"};
    case '<':
      if (position_ + 1 < bytes_.size() && bytes_[position_ + 1] == '<') {
        return Token{read_dictionary_or_stream(), {}};
      }
      return Token{read_hex_string(), {}};
    case '>':
      if (position_ + 1 < bytes_.size() && bytes_[position_ + 1] == '>') {
        position_ += 2;
        return Token{Object(), ">>"};
      }
      ++position_;
      return Token{Object(), ">"};
    case '{':
    case '}':
      // PostScript calculator function delimiters (used by type 4 shadings).
      ++position_;
      return Token{Object(), std::string(1, static_cast<char>(byte))};
    case ')':
      ++position_;
      return Token{Object(), ")"};
    default: break;
  }

  if (byte == '+' || byte == '-' || byte == '.' || (byte >= '0' && byte <= '9')) {
    return read_number_or_reference();
  }

  const std::size_t start = position_;
  while (position_ < bytes_.size() && is_regular(bytes_[position_])) {
    ++position_;
  }
  if (position_ == start) {
    ++position_;  // never stall on a byte no rule matched
    return Token{Object(), {}};
  }
  std::string keyword(reinterpret_cast<const char*>(bytes_.data()) + start, position_ - start);
  if (keyword == "true") {
    return Token{Object(true), {}};
  }
  if (keyword == "false") {
    return Token{Object(false), {}};
  }
  if (keyword == "null") {
    return Token{Object(), {}};
  }
  return Token{Object(), std::move(keyword)};
}

Object Lexer::next_object() {
  while (true) {
    const auto before = position_;
    auto token = next();
    if (!token.has_value()) {
      return Object();
    }
    if (!token->is_keyword()) {
      return std::move(token->object);
    }
    if (position_ == before) {
      return Object();
    }
    // A keyword where a value belongs means the file is damaged. ">>" and "]" would
    // close the container the caller is filling, so give the position back and let it
    // see them.
    if (token->keyword == ">>" || token->keyword == "]") {
      position_ = before;
      return Object();
    }
  }
}

}  // namespace patchy::pdf
