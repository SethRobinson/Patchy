#include "psd/engine_data.hpp"

#include "psd/psd_io_internal.hpp"

#include <charconv>
#include <utility>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace patchy::psd {

namespace {

bool is_engine_space(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

bool is_delimiter(char c) {
  return c == '<' || c == '>' || c == '[' || c == ']' || c == '(' || c == ')' || c == '/';
}

struct Token {
  std::string ws;
  std::string text;  // empty at end of input
};

class Tokenizer {
 public:
  explicit Tokenizer(std::string_view source) : source_(source) {}

  // Returns false on a malformed token (an unterminated string).
  bool next(Token& out) {
    std::size_t pos = pos_;
    const std::size_t ws_start = pos;
    while (pos < source_.size() && is_engine_space(source_[pos])) {
      ++pos;
    }
    out.ws.assign(source_.substr(ws_start, pos - ws_start));
    out.text.clear();
    if (pos >= source_.size()) {
      pos_ = pos;
      return true;
    }
    const char c = source_[pos];
    if ((c == '<' || c == '>') && pos + 1 < source_.size() && source_[pos + 1] == c) {
      out.text.assign(source_.substr(pos, 2));
      pos_ = pos + 2;
      return true;
    }
    if (c == '[' || c == ']') {
      out.text.assign(1, c);
      pos_ = pos + 1;
      return true;
    }
    if (c == '(') {
      std::size_t end = pos + 1;
      while (end < source_.size()) {
        if (source_[end] == '\\') {
          end += 2;
          continue;
        }
        if (source_[end] == ')') {
          break;
        }
        ++end;
      }
      if (end >= source_.size()) {
        return false;
      }
      out.text.assign(source_.substr(pos, end + 1 - pos));
      pos_ = end + 1;
      return true;
    }
    std::size_t end = pos + 1;
    while (end < source_.size() && !is_engine_space(source_[end]) && !is_delimiter(source_[end])) {
      ++end;
    }
    out.text.assign(source_.substr(pos, end - pos));
    pos_ = end;
    return true;
  }

 private:
  std::string_view source_;
  std::size_t pos_{0};
};

class Parser {
 public:
  explicit Parser(std::string_view source) : tokenizer_(source) {}

  bool parse_value(Token token, EngineNode& out, int depth) {
    if (depth > 512) {
      return false;
    }
    if (token.text == "<<") {
      out.kind = EngineNode::Kind::Dict;
      out.ws = std::move(token.ws);
      return parse_dict_entries(out, depth, /*bare=*/false);
    }
    if (token.text == "[") {
      out.kind = EngineNode::Kind::List;
      out.ws = std::move(token.ws);
      while (true) {
        Token next;
        if (!tokenizer_.next(next) || next.text.empty()) {
          return false;
        }
        if (next.text == "]") {
          out.close_ws = std::move(next.ws);
          return true;
        }
        if (next.text == ">>" || next.text[0] == '/') {
          return false;
        }
        EngineNode value;
        if (!parse_value(std::move(next), value, depth + 1)) {
          return false;
        }
        out.list.push_back(std::move(value));
      }
    }
    if (token.text == ">>" || token.text == "]" || token.text.empty()) {
      return false;
    }
    out.kind = EngineNode::Kind::Scalar;
    out.ws = std::move(token.ws);
    out.raw = std::move(token.text);
    return true;
  }

  // Reads "/key value" pairs until `>>` (or the end of input when bare).
  bool parse_dict_entries(EngineNode& out, int depth, bool bare) {
    while (true) {
      Token key;
      if (!tokenizer_.next(key)) {
        return false;
      }
      if (key.text.empty()) {
        if (!bare) {
          return false;
        }
        out.close_ws = std::move(key.ws);
        return true;
      }
      if (key.text == ">>") {
        if (bare) {
          return false;
        }
        out.close_ws = std::move(key.ws);
        return true;
      }
      if (key.text.size() < 2 || key.text[0] != '/') {
        return false;
      }
      Token value_token;
      if (!tokenizer_.next(value_token) || value_token.text.empty()) {
        return false;
      }
      EngineDictEntry entry;
      entry.key_ws = std::move(key.ws);
      entry.key = key.text.substr(1);
      if (!parse_value(std::move(value_token), entry.value, depth + 1)) {
        return false;
      }
      out.dict.push_back(std::move(entry));
    }
  }

  Tokenizer tokenizer_;
};

void serialize_into(const EngineNode& node, std::string& out) {
  switch (node.kind) {
    case EngineNode::Kind::Scalar:
      out += node.ws;
      out += node.raw;
      break;
    case EngineNode::Kind::Dict:
      out += node.ws;
      out += "<<";
      for (const auto& entry : node.dict) {
        out += entry.key_ws;
        out.push_back('/');
        out += entry.key;
        serialize_into(entry.value, out);
      }
      out += node.close_ws;
      out += ">>";
      break;
    case EngineNode::Kind::List:
      out += node.ws;
      out.push_back('[');
      for (const auto& value : node.list) {
        serialize_into(value, out);
      }
      out += node.close_ws;
      out.push_back(']');
      break;
  }
}

EngineNode scalar_node(std::string raw) {
  EngineNode node;
  node.kind = EngineNode::Kind::Scalar;
  node.ws = " ";
  node.raw = std::move(raw);
  return node;
}

}  // namespace

std::string utf16_units_to_utf8(const std::vector<std::uint16_t>& units) {
  std::string out;
  out.reserve(units.size() * 2);
  const auto append = [&out](std::uint32_t code) {
    if (code < 0x80U) {
      out.push_back(static_cast<char>(code));
    } else if (code < 0x800U) {
      out.push_back(static_cast<char>(0xC0U | (code >> 6U)));
      out.push_back(static_cast<char>(0x80U | (code & 0x3FU)));
    } else if (code < 0x10000U) {
      out.push_back(static_cast<char>(0xE0U | (code >> 12U)));
      out.push_back(static_cast<char>(0x80U | ((code >> 6U) & 0x3FU)));
      out.push_back(static_cast<char>(0x80U | (code & 0x3FU)));
    } else {
      out.push_back(static_cast<char>(0xF0U | (code >> 18U)));
      out.push_back(static_cast<char>(0x80U | ((code >> 12U) & 0x3FU)));
      out.push_back(static_cast<char>(0x80U | ((code >> 6U) & 0x3FU)));
      out.push_back(static_cast<char>(0x80U | (code & 0x3FU)));
    }
  };
  for (std::size_t i = 0; i < units.size(); ++i) {
    const std::uint32_t unit = units[i];
    if (unit >= 0xD800U && unit <= 0xDBFFU && i + 1 < units.size() && units[i + 1] >= 0xDC00U &&
        units[i + 1] <= 0xDFFFU) {
      append(0x10000U + ((unit - 0xD800U) << 10U) + (units[i + 1] - 0xDC00U));
      ++i;
    } else if (unit >= 0xD800U && unit <= 0xDFFFU) {
      append(0xFFFDU);
    } else {
      append(unit);
    }
  }
  return out;
}

const EngineNode* EngineNode::find(std::string_view key) const {
  if (kind != Kind::Dict) {
    return nullptr;
  }
  for (const auto& entry : dict) {
    if (entry.key == key) {
      return &entry.value;
    }
  }
  return nullptr;
}

EngineNode* EngineNode::find(std::string_view key) {
  return const_cast<EngineNode*>(std::as_const(*this).find(key));
}

EngineNode& EngineNode::set(std::string_view key, EngineNode value) {
  for (auto& entry : dict) {
    if (entry.key == key) {
      entry.value = std::move(value);
      return entry.value;
    }
  }
  EngineDictEntry entry;
  entry.key_ws = " ";
  entry.key.assign(key);
  entry.value = std::move(value);
  dict.push_back(std::move(entry));
  return dict.back().value;
}

bool EngineNode::erase(std::string_view key) {
  for (auto it = dict.begin(); it != dict.end(); ++it) {
    if (it->key == key) {
      dict.erase(it);
      return true;
    }
  }
  return false;
}

const EngineNode* EngineNode::at_path(std::initializer_list<std::string_view> path) const {
  const EngineNode* current = this;
  for (const auto step : path) {
    if (current == nullptr) {
      return nullptr;
    }
    if (current->kind == Kind::Dict) {
      current = current->find(step);
    } else if (current->kind == Kind::List) {
      std::size_t index = 0;
      const auto result = std::from_chars(step.data(), step.data() + step.size(), index);
      if (result.ec != std::errc{} || result.ptr != step.data() + step.size() || index >= current->list.size()) {
        return nullptr;
      }
      current = &current->list[index];
    } else {
      return nullptr;
    }
  }
  return current;
}

EngineNode* EngineNode::at_path(std::initializer_list<std::string_view> path) {
  return const_cast<EngineNode*>(std::as_const(*this).at_path(path));
}

std::optional<double> EngineNode::number() const {
  if (kind != Kind::Scalar || raw.empty() || raw[0] == '/' || raw[0] == '(') {
    return std::nullopt;
  }
  if (raw == "true" || raw == "false") {
    return std::nullopt;
  }
  char* end = nullptr;
  const auto value = std::strtod(raw.c_str(), &end);
  if (end == raw.c_str() || end != raw.c_str() + raw.size() || !std::isfinite(value)) {
    return std::nullopt;
  }
  return value;
}

std::optional<long long> EngineNode::integer() const {
  if (kind != Kind::Scalar || raw.empty()) {
    return std::nullopt;
  }
  long long value = 0;
  const auto result = std::from_chars(raw.data(), raw.data() + raw.size(), value);
  if (result.ec != std::errc{} || result.ptr != raw.data() + raw.size()) {
    return std::nullopt;
  }
  return value;
}

std::optional<bool> EngineNode::boolean() const {
  if (kind != Kind::Scalar) {
    return std::nullopt;
  }
  if (raw == "true") {
    return true;
  }
  if (raw == "false") {
    return false;
  }
  return std::nullopt;
}

bool EngineNode::is_nil() const {
  return kind == Kind::Scalar && raw == "/nil";
}

std::optional<std::string> EngineNode::name() const {
  if (kind != Kind::Scalar || raw.size() < 2 || raw[0] != '/') {
    return std::nullopt;
  }
  return raw.substr(1);
}

std::optional<std::string> EngineNode::string_utf8() const {
  if (kind != Kind::Scalar || raw.size() < 2 || raw.front() != '(' || raw.back() != ')') {
    return std::nullopt;
  }
  std::vector<std::uint8_t> bytes;
  bytes.reserve(raw.size());
  for (std::size_t i = 1; i + 1 < raw.size(); ++i) {
    if (raw[i] == '\\' && i + 2 < raw.size()) {
      ++i;
    }
    bytes.push_back(static_cast<std::uint8_t>(raw[i]));
  }
  if (bytes.size() >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF) {
    std::vector<std::uint16_t> units;
    for (std::size_t i = 2; i + 1 < bytes.size(); i += 2) {
      units.push_back(static_cast<std::uint16_t>((bytes[i] << 8) | bytes[i + 1]));
    }
    return utf16_units_to_utf8(units);
  }
  // A raw ASCII payload (the story bit strings); returned as-is.
  return std::string(bytes.begin(), bytes.end());
}

std::optional<EngineNode> parse_engine_data(std::string_view text, bool bare_root) {
  Parser parser(text);
  EngineNode root;
  if (bare_root) {
    root.kind = EngineNode::Kind::Dict;
    if (!parser.parse_dict_entries(root, 0, /*bare=*/true)) {
      return std::nullopt;
    }
    return root;
  }
  Token first;
  if (!parser.tokenizer_.next(first) || first.text.empty()) {
    return std::nullopt;
  }
  if (!parser.parse_value(std::move(first), root, 0)) {
    return std::nullopt;
  }
  Token trailing;
  if (!parser.tokenizer_.next(trailing) || !trailing.text.empty()) {
    return std::nullopt;
  }
  root.close_ws += trailing.ws;
  return root;
}

std::optional<EngineNode> parse_engine_data(std::span<const std::uint8_t> bytes, bool bare_root) {
  return parse_engine_data(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()), bare_root);
}

std::string serialize_engine_data(const EngineNode& node, bool bare_root) {
  std::string out;
  if (bare_root && node.kind == EngineNode::Kind::Dict) {
    for (const auto& entry : node.dict) {
      out += entry.key_ws;
      out.push_back('/');
      out += entry.key;
      serialize_into(entry.value, out);
    }
    out += node.close_ws;
    return out;
  }
  serialize_into(node, out);
  return out;
}

std::vector<std::uint8_t> serialize_engine_data_bytes(const EngineNode& node, bool bare_root) {
  const auto text = serialize_engine_data(node, bare_root);
  return std::vector<std::uint8_t>(text.begin(), text.end());
}

std::string engine_number_text(double value) {
  if (!std::isfinite(value)) {
    value = 0.0;
  }
  if (std::abs(value) < 0.000005) {
    return "0.0";
  }
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%.5f", value);
  std::string text(buffer);
  while (text.size() > 1U && text.back() == '0' && text[text.size() - 2U] != '.') {
    text.pop_back();
  }
  if (text.rfind("0.", 0) == 0) {
    text.erase(0, 1);
  } else if (text.rfind("-0.", 0) == 0) {
    text.erase(1, 1);
  }
  return text;
}

EngineNode engine_number(double value) {
  return scalar_node(engine_number_text(value));
}

EngineNode engine_integer(long long value) {
  return scalar_node(std::to_string(value));
}

EngineNode engine_bool(bool value) {
  return scalar_node(value ? "true" : "false");
}

EngineNode engine_name(std::string_view name) {
  return scalar_node("/" + std::string(name));
}

EngineNode engine_nil() {
  return scalar_node("/nil");
}

EngineNode engine_string(std::string_view utf8) {
  std::string raw = "(";
  const auto append_byte = [&raw](std::uint8_t byte) {
    if (byte == '(' || byte == ')' || byte == '\\') {
      raw.push_back('\\');
    }
    raw.push_back(static_cast<char>(byte));
  };
  append_byte(0xFEU);
  append_byte(0xFFU);
  for (const auto unit : utf8_to_utf16(utf8)) {
    append_byte(static_cast<std::uint8_t>((unit >> 8U) & 0xFFU));
    append_byte(static_cast<std::uint8_t>(unit & 0xFFU));
  }
  raw.push_back(')');
  return scalar_node(std::move(raw));
}

EngineNode engine_dict() {
  EngineNode node;
  node.kind = EngineNode::Kind::Dict;
  node.ws = " ";
  node.close_ws = " ";
  return node;
}

EngineNode engine_list() {
  EngineNode node;
  node.kind = EngineNode::Kind::List;
  node.ws = " ";
  node.close_ws = " ";
  return node;
}

EngineNode engine_dict_of(std::initializer_list<std::pair<std::string_view, EngineNode>> entries) {
  auto node = engine_dict();
  for (const auto& [key, value] : entries) {
    node.set(key, value);
  }
  return node;
}

EngineNode engine_list_of(std::initializer_list<EngineNode> values) {
  auto node = engine_list();
  for (const auto& value : values) {
    node.list.push_back(value);
  }
  return node;
}

}  // namespace patchy::psd
