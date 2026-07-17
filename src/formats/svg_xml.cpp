#include "formats/svg_xml.hpp"

#include <algorithm>
#include <map>
#include <stdexcept>

namespace patchy::svg {
namespace {

// Hard caps against pathological input (billion-laughs entities, absurd
// nesting). Generous for real artwork: an export with hundreds of thousands
// of nodes falls back to the raster import path anyway.
constexpr std::size_t kMaxNodes = 250000;
constexpr std::size_t kMaxDepth = 512;
constexpr std::size_t kMaxEntityOutput = 8U * 1024U * 1024U;
constexpr int kMaxEntityDepth = 8;

constexpr std::string_view kSvgNamespaceUri = "http://www.w3.org/2000/svg";
constexpr std::string_view kXlinkNamespaceUri = "http://www.w3.org/1999/xlink";

bool is_xml_whitespace(char c) noexcept {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

bool is_name_delimiter(char c) noexcept {
  return is_xml_whitespace(c) || c == '=' || c == '>' || c == '/' || c == '<' || c == '"' || c == '\'';
}

void append_utf8(std::string& out, std::uint32_t code_point) {
  if (code_point > 0x10FFFFU || (code_point >= 0xD800U && code_point <= 0xDFFFU)) {
    code_point = 0xFFFDU;  // replacement character for invalid scalars
  }
  if (code_point < 0x80U) {
    out.push_back(static_cast<char>(code_point));
  } else if (code_point < 0x800U) {
    out.push_back(static_cast<char>(0xC0U | (code_point >> 6)));
    out.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
  } else if (code_point < 0x10000U) {
    out.push_back(static_cast<char>(0xE0U | (code_point >> 12)));
    out.push_back(static_cast<char>(0x80U | ((code_point >> 6) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
  } else {
    out.push_back(static_cast<char>(0xF0U | (code_point >> 18)));
    out.push_back(static_cast<char>(0x80U | ((code_point >> 12) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | ((code_point >> 6) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
  }
}

std::string transcode_utf16(std::span<const std::uint8_t> bytes, bool big_endian) {
  std::string out;
  out.reserve(bytes.size() / 2);
  std::size_t i = 2;  // past the BOM
  while (i + 1 < bytes.size()) {
    const auto unit_at = [&](std::size_t index) -> std::uint32_t {
      return big_endian ? (static_cast<std::uint32_t>(bytes[index]) << 8) | bytes[index + 1]
                        : static_cast<std::uint32_t>(bytes[index]) | (static_cast<std::uint32_t>(bytes[index + 1]) << 8);
    };
    std::uint32_t code_point = unit_at(i);
    i += 2;
    if (code_point >= 0xD800U && code_point <= 0xDBFFU) {
      if (i + 1 < bytes.size()) {
        const auto low = unit_at(i);
        if (low >= 0xDC00U && low <= 0xDFFFU) {
          code_point = 0x10000U + ((code_point - 0xD800U) << 10) + (low - 0xDC00U);
          i += 2;
        } else {
          code_point = 0xFFFDU;
        }
      } else {
        code_point = 0xFFFDU;
      }
    } else if (code_point >= 0xDC00U && code_point <= 0xDFFFU) {
      code_point = 0xFFFDU;
    }
    append_utf8(out, code_point);
  }
  return out;
}

// Case-insensitive substring check over a bounded ASCII prefix of the input,
// used only for the XML-declaration encoding sniff.
bool prefix_contains_token(std::string_view text, std::string_view token) {
  const auto limit = std::min<std::size_t>(text.size(), 256U);
  if (token.size() > limit) {
    return false;
  }
  for (std::size_t i = 0; i + token.size() <= limit; ++i) {
    bool match = true;
    for (std::size_t j = 0; j < token.size(); ++j) {
      const char a = text[i + j];
      const char lower = (a >= 'A' && a <= 'Z') ? static_cast<char>(a - 'A' + 'a') : a;
      if (lower != token[j]) {
        match = false;
        break;
      }
    }
    if (match) {
      return true;
    }
  }
  return false;
}

// BOM / declared-encoding handling: UTF-8 (with or without BOM) passes
// through, UTF-16 transcodes, a declared Latin-1 family maps bytes 1:1 to
// code points. Anything else is treated as UTF-8 (the numeric/color grammar
// SVG needs is ASCII regardless).
std::string decode_input_bytes(std::span<const std::uint8_t> bytes) {
  if (bytes.size() >= 2) {
    if (bytes[0] == 0xFF && bytes[1] == 0xFE) {
      return transcode_utf16(bytes, /*big_endian*/ false);
    }
    if (bytes[0] == 0xFE && bytes[1] == 0xFF) {
      return transcode_utf16(bytes, /*big_endian*/ true);
    }
  }
  if (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) {
    bytes = bytes.subspan(3);
  }
  std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  if (prefix_contains_token(text, "iso-8859-1") || prefix_contains_token(text, "latin1") ||
      prefix_contains_token(text, "windows-1252")) {
    std::string latin;
    latin.reserve(text.size());
    for (const char c : text) {
      const auto byte = static_cast<std::uint8_t>(c);
      if (byte < 0x80U) {
        latin.push_back(c);
      } else {
        append_utf8(latin, byte);
      }
    }
    return latin;
  }
  return text;
}

struct Parser {
  std::string_view input;
  std::size_t pos{0};
  std::size_t node_count{0};
  // Internal-DTD general entities, raw replacement text (decoded at use).
  std::map<std::string, std::string, std::less<>> entities;

  [[noreturn]] void fail(std::string message, std::size_t at) const {
    std::size_t line = 1;
    const auto limit = std::min(at, input.size());
    for (std::size_t i = 0; i < limit; ++i) {
      if (input[i] == '\n') {
        ++line;
      }
    }
    throw std::runtime_error("SVG parse error: " + std::move(message) + " (line " + std::to_string(line) + ")");
  }

  [[nodiscard]] bool at_end() const noexcept { return pos >= input.size(); }
  [[nodiscard]] char peek() const noexcept { return pos < input.size() ? input[pos] : '\0'; }
  [[nodiscard]] bool starts_with(std::string_view token) const noexcept {
    return input.substr(pos).starts_with(token);
  }
  void skip_whitespace() noexcept {
    while (pos < input.size() && is_xml_whitespace(input[pos])) {
      ++pos;
    }
  }
  void count_node() {
    if (++node_count > kMaxNodes) {
      fail("too many nodes", pos);
    }
  }

  std::string_view read_name() {
    const auto start = pos;
    while (pos < input.size() && !is_name_delimiter(input[pos])) {
      ++pos;
    }
    if (pos == start) {
      fail("expected a name", start);
    }
    return input.substr(start, pos - start);
  }

  // Skips a "<!...>" declaration, honoring quoted strings so a '>' inside an
  // entity value does not end it early.
  void skip_declaration() {
    char quote = '\0';
    while (pos < input.size()) {
      const char c = input[pos++];
      if (quote != '\0') {
        if (c == quote) {
          quote = '\0';
        }
      } else if (c == '"' || c == '\'') {
        quote = c;
      } else if (c == '>') {
        return;
      }
    }
    fail("unterminated declaration", input.size());
  }

  void skip_comment() {
    const auto start = pos;
    pos += 4;  // "<!--"
    const auto end = input.find("-->", pos);
    if (end == std::string_view::npos) {
      fail("unterminated comment", start);
    }
    pos = end + 3;
  }

  void skip_processing_instruction() {
    const auto start = pos;
    pos += 2;  // "<?"
    const auto end = input.find("?>", pos);
    if (end == std::string_view::npos) {
      fail("unterminated processing instruction", start);
    }
    pos = end + 2;
  }
};

// Entity-aware text decoding. `normalize_whitespace` implements XML
// attribute-value normalization (raw tab/newline/return become spaces;
// character references stay literal). Unknown or malformed references keep
// their literal '&' (real-world files contain stray ampersands).
void decode_text(const Parser& parser, std::string_view raw, bool normalize_whitespace, std::string& out,
                 int depth) {
  if (depth > kMaxEntityDepth) {
    parser.fail("entity references nested too deeply", parser.pos);
  }
  std::size_t i = 0;
  while (i < raw.size()) {
    const char c = raw[i];
    if (c != '&') {
      if (normalize_whitespace && (c == '\t' || c == '\n' || c == '\r')) {
        out.push_back(' ');
      } else {
        out.push_back(c);
      }
      ++i;
      continue;
    }
    const auto semicolon = raw.find(';', i + 1);
    if (semicolon == std::string_view::npos || semicolon - i > 64) {
      out.push_back('&');
      ++i;
      continue;
    }
    const auto body = raw.substr(i + 1, semicolon - i - 1);
    if (body.starts_with("#")) {
      std::uint32_t code_point = 0;
      bool valid = body.size() > 1;
      if (body.size() > 2 && (body[1] == 'x' || body[1] == 'X')) {
        for (std::size_t j = 2; j < body.size() && valid; ++j) {
          const char h = body[j];
          std::uint32_t digit = 0;
          if (h >= '0' && h <= '9') {
            digit = static_cast<std::uint32_t>(h - '0');
          } else if (h >= 'a' && h <= 'f') {
            digit = static_cast<std::uint32_t>(h - 'a' + 10);
          } else if (h >= 'A' && h <= 'F') {
            digit = static_cast<std::uint32_t>(h - 'A' + 10);
          } else {
            valid = false;
            break;
          }
          code_point = std::min<std::uint32_t>(code_point * 16 + digit, 0x110000U);
        }
        valid = valid && body.size() > 2;
      } else {
        for (std::size_t j = 1; j < body.size() && valid; ++j) {
          const char d = body[j];
          if (d < '0' || d > '9') {
            valid = false;
            break;
          }
          code_point = std::min<std::uint32_t>(code_point * 10 + static_cast<std::uint32_t>(d - '0'), 0x110000U);
        }
      }
      if (valid) {
        append_utf8(out, code_point);
        i = semicolon + 1;
        continue;
      }
    } else if (body == "amp") {
      out.push_back('&');
      i = semicolon + 1;
      continue;
    } else if (body == "lt") {
      out.push_back('<');
      i = semicolon + 1;
      continue;
    } else if (body == "gt") {
      out.push_back('>');
      i = semicolon + 1;
      continue;
    } else if (body == "quot") {
      out.push_back('"');
      i = semicolon + 1;
      continue;
    } else if (body == "apos") {
      out.push_back('\'');
      i = semicolon + 1;
      continue;
    } else if (const auto found = parser.entities.find(body); found != parser.entities.end()) {
      decode_text(parser, found->second, normalize_whitespace, out, depth + 1);
      if (out.size() > kMaxEntityOutput) {
        parser.fail("entity expansion too large", parser.pos);
      }
      i = semicolon + 1;
      continue;
    }
    out.push_back('&');
    ++i;
  }
}

std::string decoded(const Parser& parser, std::string_view raw, bool normalize_whitespace) {
  std::string out;
  out.reserve(raw.size());
  decode_text(parser, raw, normalize_whitespace, out, 0);
  return out;
}

// Collects <!ENTITY name "value"> declarations from a DOCTYPE internal
// subset. External identifiers (SYSTEM/PUBLIC) and parameter entities are
// skipped; everything else in the subset is ignored.
void parse_doctype(Parser& parser) {
  parser.pos += 9;  // "<!DOCTYPE"
  char quote = '\0';
  while (!parser.at_end()) {
    const char c = parser.input[parser.pos];
    if (quote != '\0') {
      if (c == quote) {
        quote = '\0';
      }
      ++parser.pos;
      continue;
    }
    if (c == '"' || c == '\'') {
      quote = c;
      ++parser.pos;
      continue;
    }
    if (c == '>') {
      ++parser.pos;
      return;
    }
    if (c != '[') {
      ++parser.pos;
      continue;
    }
    // Internal subset.
    ++parser.pos;
    while (!parser.at_end() && parser.peek() != ']') {
      parser.skip_whitespace();
      if (parser.starts_with("<!--")) {
        parser.skip_comment();
        continue;
      }
      if (parser.starts_with("<!ENTITY")) {
        parser.pos += 8;
        parser.skip_whitespace();
        if (parser.peek() == '%') {  // parameter entity: unsupported, skip
          parser.skip_declaration();
          continue;
        }
        const auto name = parser.read_name();
        parser.skip_whitespace();
        const char value_quote = parser.peek();
        if (value_quote == '"' || value_quote == '\'') {
          ++parser.pos;
          const auto value_end = parser.input.find(value_quote, parser.pos);
          if (value_end == std::string_view::npos) {
            parser.fail("unterminated entity value", parser.pos);
          }
          parser.entities.emplace(std::string(name),
                                  std::string(parser.input.substr(parser.pos, value_end - parser.pos)));
          parser.pos = value_end + 1;
        }
        parser.skip_declaration();  // to the '>' ending this declaration
        continue;
      }
      if (parser.starts_with("<!") || parser.starts_with("<?")) {
        parser.skip_declaration();
        continue;
      }
      if (parser.at_end() || parser.peek() == ']') {
        break;
      }
      ++parser.pos;  // stray character inside the subset
    }
    if (parser.at_end()) {
      parser.fail("unterminated DOCTYPE internal subset", parser.pos);
    }
    ++parser.pos;  // ']'
  }
  parser.fail("unterminated DOCTYPE", parser.pos);
}

// Namespace scopes form a parse-time chain; prefix "" is the default
// namespace (elements only, per the XML namespaces rules).
struct NamespaceScope {
  const NamespaceScope* parent{nullptr};
  std::vector<std::pair<std::string, std::string>> bindings;

  [[nodiscard]] const std::string* resolve(std::string_view prefix) const noexcept {
    for (const auto& [bound_prefix, uri] : bindings) {
      if (bound_prefix == prefix) {
        return &uri;
      }
    }
    return parent != nullptr ? parent->resolve(prefix) : nullptr;
  }
};

std::string resolve_element_name(std::string_view raw, const NamespaceScope& scope) {
  const auto colon = raw.find(':');
  if (colon == std::string_view::npos) {
    return std::string(raw);  // default namespace: SVG content either way
  }
  const auto prefix = raw.substr(0, colon);
  const auto local = raw.substr(colon + 1);
  const auto* uri = scope.resolve(prefix);
  if (uri != nullptr && *uri == kSvgNamespaceUri) {
    return std::string(local);
  }
  return std::string(raw);  // foreign namespace stays verbatim (consumers skip it)
}

std::string resolve_attribute_name(std::string_view raw, const NamespaceScope& scope) {
  const auto colon = raw.find(':');
  if (colon == std::string_view::npos) {
    return std::string(raw);
  }
  const auto prefix = raw.substr(0, colon);
  const auto local = raw.substr(colon + 1);
  if (prefix == "xml") {
    return std::string(raw);  // xml:space and friends keep their reserved prefix
  }
  const auto* uri = scope.resolve(prefix);
  if (uri != nullptr && (*uri == kXlinkNamespaceUri || *uri == kSvgNamespaceUri)) {
    return std::string(local);  // xlink:href -> href
  }
  return std::string(raw);
}

XmlNode parse_element(Parser& parser, const NamespaceScope* parent_scope, std::size_t depth);

// Parses child content until the matching close tag of `raw_name`.
void parse_content(Parser& parser, XmlNode& node, std::string_view raw_name, const NamespaceScope& scope,
                   std::size_t depth) {
  while (true) {
    if (parser.at_end()) {
      parser.fail("unexpected end of file inside <" + std::string(raw_name) + ">", parser.pos);
    }
    if (parser.peek() != '<') {
      const auto start = parser.pos;
      const auto next_tag = parser.input.find('<', parser.pos);
      const auto end = next_tag == std::string_view::npos ? parser.input.size() : next_tag;
      const auto raw_text = parser.input.substr(start, end - start);
      parser.pos = end;
      if (std::any_of(raw_text.begin(), raw_text.end(), [](char c) { return !is_xml_whitespace(c); })) {
        parser.count_node();
        XmlNode text_node;
        text_node.text = decoded(parser, raw_text, /*normalize_whitespace*/ false);
        node.children.push_back(std::move(text_node));
      }
      continue;
    }
    if (parser.starts_with("</")) {
      const auto close_at = parser.pos;
      parser.pos += 2;
      const auto close_name = parser.read_name();
      parser.skip_whitespace();
      if (parser.peek() != '>') {
        parser.fail("malformed closing tag", close_at);
      }
      ++parser.pos;
      if (close_name != raw_name) {
        parser.fail("mismatched closing tag </" + std::string(close_name) + "> for <" + std::string(raw_name) + ">",
                    close_at);
      }
      return;
    }
    if (parser.starts_with("<!--")) {
      parser.skip_comment();
      continue;
    }
    if (parser.starts_with("<![CDATA[")) {
      const auto start = parser.pos;
      parser.pos += 9;
      const auto end = parser.input.find("]]>", parser.pos);
      if (end == std::string_view::npos) {
        parser.fail("unterminated CDATA section", start);
      }
      const auto raw_text = parser.input.substr(parser.pos, end - parser.pos);
      parser.pos = end + 3;
      if (std::any_of(raw_text.begin(), raw_text.end(), [](char c) { return !is_xml_whitespace(c); })) {
        parser.count_node();
        XmlNode text_node;
        text_node.text = std::string(raw_text);  // CDATA is literal: no entity decoding
        node.children.push_back(std::move(text_node));
      }
      continue;
    }
    if (parser.starts_with("<!")) {
      parser.skip_declaration();
      continue;
    }
    if (parser.starts_with("<?")) {
      parser.skip_processing_instruction();
      continue;
    }
    node.children.push_back(parse_element(parser, &scope, depth + 1));
  }
}

XmlNode parse_element(Parser& parser, const NamespaceScope* parent_scope, std::size_t depth) {
  if (depth > kMaxDepth) {
    parser.fail("elements nested too deeply", parser.pos);
  }
  parser.count_node();
  const auto open_at = parser.pos;
  ++parser.pos;  // '<'
  const auto raw_name = parser.read_name();

  struct RawAttribute {
    std::string_view name;
    std::string value;
  };
  std::vector<RawAttribute> raw_attributes;
  bool self_closing = false;
  while (true) {
    parser.skip_whitespace();
    if (parser.at_end()) {
      parser.fail("unexpected end of file inside <" + std::string(raw_name) + ">", open_at);
    }
    if (parser.starts_with("/>")) {
      parser.pos += 2;
      self_closing = true;
      break;
    }
    if (parser.peek() == '>') {
      ++parser.pos;
      break;
    }
    const auto attribute_at = parser.pos;
    const auto attribute_name = parser.read_name();
    parser.skip_whitespace();
    if (parser.peek() != '=') {
      parser.fail("attribute " + std::string(attribute_name) + " has no value", attribute_at);
    }
    ++parser.pos;
    parser.skip_whitespace();
    const char quote = parser.peek();
    if (quote != '"' && quote != '\'') {
      parser.fail("attribute " + std::string(attribute_name) + " is not quoted", attribute_at);
    }
    ++parser.pos;
    const auto value_end = parser.input.find(quote, parser.pos);
    if (value_end == std::string_view::npos) {
      parser.fail("unterminated attribute value", attribute_at);
    }
    const auto raw_value = parser.input.substr(parser.pos, value_end - parser.pos);
    parser.pos = value_end + 1;
    raw_attributes.push_back({attribute_name, decoded(parser, raw_value, /*normalize_whitespace*/ true)});
  }

  NamespaceScope scope;
  scope.parent = parent_scope;
  for (const auto& attribute : raw_attributes) {
    if (attribute.name == "xmlns") {
      scope.bindings.emplace_back(std::string(), attribute.value);
    } else if (attribute.name.starts_with("xmlns:")) {
      scope.bindings.emplace_back(std::string(attribute.name.substr(6)), attribute.value);
    }
  }

  XmlNode node;
  node.name = resolve_element_name(raw_name, scope);
  node.attributes.reserve(raw_attributes.size());
  for (auto& attribute : raw_attributes) {
    if (attribute.name == "xmlns" || attribute.name.starts_with("xmlns:")) {
      continue;
    }
    node.attributes.emplace_back(resolve_attribute_name(attribute.name, scope), std::move(attribute.value));
  }

  if (!self_closing) {
    parse_content(parser, node, raw_name, scope, depth);
  }
  return node;
}

}  // namespace

const std::string* XmlNode::attribute(std::string_view attribute_name) const noexcept {
  for (const auto& [name_key, value] : attributes) {
    if (name_key == attribute_name) {
      return &value;
    }
  }
  return nullptr;
}

std::string XmlNode::all_text() const {
  std::string out;
  const auto append = [&out](const XmlNode& node, const auto& self) -> void {
    if (node.is_text()) {
      out += node.text;
      return;
    }
    for (const auto& child : node.children) {
      self(child, self);
    }
  };
  append(*this, append);
  return out;
}

XmlNode parse_xml(std::span<const std::uint8_t> bytes) {
  const std::string text = decode_input_bytes(bytes);
  Parser parser;
  parser.input = text;
  while (true) {
    parser.skip_whitespace();
    if (parser.at_end()) {
      parser.fail("no root element", parser.pos);
    }
    if (parser.starts_with("<?")) {
      parser.skip_processing_instruction();
      continue;
    }
    if (parser.starts_with("<!--")) {
      parser.skip_comment();
      continue;
    }
    if (parser.starts_with("<!DOCTYPE")) {
      parse_doctype(parser);
      continue;
    }
    if (parser.starts_with("<!")) {
      parser.skip_declaration();
      continue;
    }
    if (parser.peek() == '<') {
      return parse_element(parser, nullptr, 0);  // trailing content is ignored
    }
    parser.fail("not an XML document", parser.pos);
  }
}

}  // namespace patchy::svg
