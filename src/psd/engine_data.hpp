#pragma once

// Photoshop's EngineData text syntax, shared by the per-layer TySh engine data (named keys)
// and the document-level 'Txt2' text engine block (integer keys plus a /99 class tag):
// `<< /key value >>` dictionaries, `[ value ... ]` arrays, `(\xfe\xff...)` UTF-16BE strings
// with only `(`, `)` and `\` escaped, `/name` enums, `true`/`false`, `/nil`, and numbers.
// The tree keeps every token's spelling and leading whitespace, so a parsed document
// serializes byte-exact; authored nodes get Photoshop's own spelling (single spaces,
// `24.0` / `.583` / `-20.5957` numbers). See docs/txt2.md.

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace patchy::psd {

struct EngineDictEntry;

struct EngineNode {
  enum class Kind { Scalar, Dict, List };
  Kind kind{Kind::Scalar};
  // Whitespace that preceded this node's first token in the source (" " when authored).
  std::string ws;
  // Scalar token text exactly as spelled, parentheses and escapes included for strings.
  std::string raw;
  std::vector<EngineDictEntry> dict;
  std::vector<EngineNode> list;
  // Whitespace before the closing `>>` or `]`.
  std::string close_ws;

  [[nodiscard]] bool is_dict() const noexcept { return kind == Kind::Dict; }
  [[nodiscard]] bool is_list() const noexcept { return kind == Kind::List; }
  [[nodiscard]] bool is_scalar() const noexcept { return kind == Kind::Scalar; }

  // Dictionary access by key text ("0", "99", "Justification"); nullptr when absent or not a dict.
  [[nodiscard]] const EngineNode* find(std::string_view key) const;
  [[nodiscard]] EngineNode* find(std::string_view key);
  // Replaces the entry's value or appends a new entry (single-space layout).
  EngineNode& set(std::string_view key, EngineNode value);
  bool erase(std::string_view key);
  // Nested lookup: each step is a dict key or a list index. nullptr when any step fails.
  [[nodiscard]] const EngineNode* at_path(std::initializer_list<std::string_view> path) const;
  [[nodiscard]] EngineNode* at_path(std::initializer_list<std::string_view> path);

  // Scalar readers (nullopt when the node is not that kind of scalar).
  [[nodiscard]] std::optional<double> number() const;
  [[nodiscard]] std::optional<long long> integer() const;
  [[nodiscard]] std::optional<bool> boolean() const;
  [[nodiscard]] bool is_nil() const;
  [[nodiscard]] std::optional<std::string> name() const;      // "/CoolTypeFont" -> "CoolTypeFont"
  [[nodiscard]] std::optional<std::string> string_utf8() const;  // decoded UTF-16BE string
};

struct EngineDictEntry {
  std::string key_ws;  // whitespace before the "/key" token
  std::string key;     // without the leading slash
  EngineNode value;
};

// Parses a document. `bare_root` reads a key/value sequence with no enclosing `<< >>` (the
// Txt2 block body); otherwise the text must be one value. Malformed input yields nullopt.
std::optional<EngineNode> parse_engine_data(std::string_view text, bool bare_root);
std::optional<EngineNode> parse_engine_data(std::span<const std::uint8_t> bytes, bool bare_root);
// Serializes the tree; a parsed tree reproduces its source byte for byte.
std::string serialize_engine_data(const EngineNode& node, bool bare_root);
std::vector<std::uint8_t> serialize_engine_data_bytes(const EngineNode& node, bool bare_root);

// Photoshop's number spelling: at most five decimals, trailing zeros stripped, ".0" kept for
// whole values, no leading zero before the point ("24.0", "-20.5957", ".583", "0.0").
std::string engine_number_text(double value);

// Authored nodes (single-space layout).
EngineNode engine_number(double value);
EngineNode engine_integer(long long value);
EngineNode engine_bool(bool value);
EngineNode engine_name(std::string_view name);  // written as "/name"
EngineNode engine_nil();
EngineNode engine_string(std::string_view utf8);  // "(\xfe\xff...)" UTF-16BE with escapes
EngineNode engine_dict();
EngineNode engine_list();
// Builds a dict from key/value pairs in order: engine_dict_of({{"0", engine_integer(1)}, ...}).
EngineNode engine_dict_of(std::initializer_list<std::pair<std::string_view, EngineNode>> entries);
EngineNode engine_list_of(std::initializer_list<EngineNode> values);

}  // namespace patchy::psd
