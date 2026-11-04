// Source ratchet for the "no hardcoded chrome colors in src/ui" rule
// (docs/ui-conventions.md, "Color scheme"). Chrome colors are named roles in
// src/ui/theme_palette.hpp; a literal QColor in a painted widget freezes the Dark
// value in Light and in every custom theme.
//
// Some literals are legitimate content (channel hues, the Levels black/gray/white
// handle fills, swatch contents, over-artwork contrast pairs), so the scan cannot
// demand zero. Instead every src/ui file has a pinned count in
// tests/core/ui_color_literal_baseline.txt and the test fails both ways:
//   - a file above its count gained a literal: use a theme() role, or mark a
//     genuine content color with a `color-literal-ok: <reason>` comment on the
//     same line;
//   - a file below its count lost one: lower its entry (or delete the line at
//     zero), so the ceiling only ever goes down.
//
// Counted, after comments and string contents are blanked:
//   - QColor(...) / QColor{...} / QColor name(...) / QColor name{...} whose first
//     argument is a number, a "#..." string, or QStringLiteral("#...");
//   - QColor::fromRgb / fromRgbF / fromHsv / fromHsl and friends with a numeric
//     first argument;
//   - Qt::<global color> (Qt::white, Qt::black, Qt::gray, ...). Qt::transparent is
//     not a color choice and is not counted.

#include "test_harness.hpp"
#include "test_groups.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;

// Files whose literals are the point of the file, not a violation.
constexpr std::array<std::string_view, 2> kExemptFiles{
    // The role values themselves.
    "src/ui/theme_palette.cpp",
    // The transparency checkerboard squares, theme-exempt by rule: they depict alpha.
    "src/ui/transparency_checkerboard.hpp",
};

constexpr std::string_view kLineMarker = "color-literal-ok";

constexpr std::array<std::string_view, 17> kQtGlobalColors{
    "white",     "black",      "red",       "darkRed",     "green",    "darkGreen",
    "blue",      "darkBlue",   "cyan",      "darkCyan",    "magenta",  "darkMagenta",
    "yellow",    "darkYellow", "gray",      "darkGray",    "lightGray",
};

bool is_identifier_char(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

bool is_digit(char c) { return c >= '0' && c <= '9'; }

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

// Returns the text with comments blanked and string/char literal contents replaced
// by spaces (newlines kept, so offsets and line numbers still match). A string that
// starts with '#' keeps the '#', so a hex-name QColor("#rrggbb") is still visible.
std::string blank_comments_and_strings(const std::string& text) {
  std::string out = text;
  const auto size = text.size();
  std::size_t i = 0;
  const auto blank = [&out](std::size_t from, std::size_t to) {
    for (std::size_t k = from; k < to && k < out.size(); ++k) {
      if (out[k] != '\n') {
        out[k] = ' ';
      }
    }
  };
  while (i < size) {
    const char c = text[i];
    if (c == '/' && i + 1 < size && text[i + 1] == '/') {
      const auto end = text.find('\n', i);
      const auto stop = end == std::string::npos ? size : end;
      blank(i, stop);
      i = stop;
      continue;
    }
    if (c == '/' && i + 1 < size && text[i + 1] == '*') {
      const auto end = text.find("*/", i + 2);
      const auto stop = end == std::string::npos ? size : end + 2;
      blank(i, stop);
      i = stop;
      continue;
    }
    if (c == '"') {
      // Raw string: R"delim( ... )delim", possibly with an encoding prefix.
      if (i > 0 && text[i - 1] == 'R' && (i < 2 || !is_identifier_char(text[i - 2]) || text[i - 2] == '8' ||
                                          text[i - 2] == 'u' || text[i - 2] == 'U' || text[i - 2] == 'L')) {
        const auto open = text.find('(', i + 1);
        if (open != std::string::npos) {
          const std::string terminator = ")" + text.substr(i + 1, open - i - 1) + "\"";
          const auto end = text.find(terminator, open + 1);
          const auto stop = end == std::string::npos ? size : end + terminator.size();
          blank(i + 1, stop - 1);
          i = stop;
          continue;
        }
      }
      std::size_t k = i + 1;
      while (k < size && text[k] != '"' && text[k] != '\n') {
        if (text[k] == '\\') {
          ++k;
        }
        ++k;
      }
      const bool hex_name = i + 1 < size && text[i + 1] == '#';
      blank(i + (hex_name ? 2 : 1), k);
      i = k + 1;
      continue;
    }
    if (c == '\'') {
      // A digit separator (1'000) follows a digit; anything else opens a char literal.
      if (i > 0 && is_digit(text[i - 1])) {
        ++i;
        continue;
      }
      std::size_t k = i + 1;
      while (k < size && text[k] != '\'' && text[k] != '\n') {
        if (text[k] == '\\') {
          ++k;
        }
        ++k;
      }
      blank(i + 1, k);
      i = k + 1;
      continue;
    }
    ++i;
  }
  return out;
}

std::size_t skip_space(const std::string& text, std::size_t at) {
  while (at < text.size() && is_space(text[at])) {
    ++at;
  }
  return at;
}

// True when the argument starting at `at` is a literal color value.
bool literal_argument(const std::string& text, std::size_t at) {
  at = skip_space(text, at);
  if (at >= text.size()) {
    return false;
  }
  if (is_digit(text[at])) {
    return true;
  }
  if (text.compare(at, 2, "\"#") == 0) {
    return true;
  }
  constexpr std::string_view kLiteral = "QStringLiteral(\"#";
  return text.compare(at, kLiteral.size(), kLiteral) == 0;
}

// Offsets of every counted literal in blanked text.
std::vector<std::size_t> literal_offsets(const std::string& text) {
  std::vector<std::size_t> offsets;
  constexpr std::string_view kQColor = "QColor";
  for (auto pos = text.find(kQColor); pos != std::string::npos; pos = text.find(kQColor, pos + 1)) {
    if (pos > 0 && is_identifier_char(text[pos - 1])) {
      continue;
    }
    auto cursor = pos + kQColor.size();
    if (cursor < text.size() && is_identifier_char(text[cursor])) {
      continue;  // QColorDialog, QColorSpace, ...
    }
    if (text.compare(cursor, 6, "::from") == 0) {
      cursor += 6;
      while (cursor < text.size() && is_identifier_char(text[cursor])) {
        ++cursor;
      }
      cursor = skip_space(text, cursor);
      if (cursor < text.size() && text[cursor] == '(' && literal_argument(text, cursor + 1)) {
        offsets.push_back(pos);
      }
      continue;
    }
    cursor = skip_space(text, cursor);
    // `QColor name(...)` / `QColor name{...}`: step over the declared name.
    if (cursor < text.size() && is_identifier_char(text[cursor]) && !is_digit(text[cursor])) {
      while (cursor < text.size() && is_identifier_char(text[cursor])) {
        ++cursor;
      }
      cursor = skip_space(text, cursor);
    }
    if (cursor < text.size() && (text[cursor] == '(' || text[cursor] == '{') && literal_argument(text, cursor + 1)) {
      offsets.push_back(pos);
    }
  }
  constexpr std::string_view kQt = "Qt::";
  for (auto pos = text.find(kQt); pos != std::string::npos; pos = text.find(kQt, pos + 1)) {
    if (pos > 0 && (is_identifier_char(text[pos - 1]) || text[pos - 1] == ':')) {
      continue;
    }
    const auto start = pos + kQt.size();
    auto end = start;
    while (end < text.size() && is_identifier_char(text[end])) {
      ++end;
    }
    const std::string_view name(text.data() + start, end - start);
    if (std::find(kQtGlobalColors.begin(), kQtGlobalColors.end(), name) != kQtGlobalColors.end()) {
      offsets.push_back(pos);
    }
  }
  std::sort(offsets.begin(), offsets.end());
  return offsets;
}

struct FileHits {
  std::size_t count{0};
  std::vector<std::string> lines;
};

FileHits scan_file(const fs::path& path, const std::string& display) {
  std::ifstream in(path, std::ios::binary);
  const std::string raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  const auto blanked = blank_comments_and_strings(raw);
  // Line start offsets, for line numbers and the per-line marker.
  std::vector<std::size_t> line_starts{0};
  for (std::size_t i = 0; i < raw.size(); ++i) {
    if (raw[i] == '\n') {
      line_starts.push_back(i + 1);
    }
  }
  FileHits hits;
  for (const auto offset : literal_offsets(blanked)) {
    const auto line_index =
        static_cast<std::size_t>(std::upper_bound(line_starts.begin(), line_starts.end(), offset) - line_starts.begin()) - 1;
    const auto line_begin = line_starts[line_index];
    const auto line_end = raw.find('\n', line_begin);
    auto line = raw.substr(line_begin, (line_end == std::string::npos ? raw.size() : line_end) - line_begin);
    if (line.find(kLineMarker) != std::string::npos) {
      continue;
    }
    while (!line.empty() && (line.back() == '\r' || is_space(line.back()))) {
      line.pop_back();
    }
    const auto first = line.find_first_not_of(" \t");
    ++hits.count;
    hits.lines.push_back(display + ":" + std::to_string(line_index + 1) + ": " +
                         (first == std::string::npos ? std::string() : line.substr(first)));
  }
  return hits;
}

std::map<std::string, std::size_t> read_baseline(const fs::path& path, bool& ok) {
  std::map<std::string, std::size_t> baseline;
  std::ifstream in(path);
  ok = static_cast<bool>(in);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty() || line.front() == '#') {
      continue;
    }
    std::istringstream fields(line);
    std::string file;
    std::size_t count = 0;
    if (fields >> file >> count) {
      baseline[file] = count;
    } else {
      ok = false;
    }
  }
  return baseline;
}

void ui_color_literals_stay_within_baseline() {
#ifndef PATCHY_SOURCE_DIR
  CHECK(false && "PATCHY_SOURCE_DIR is required");
#else
  const fs::path repo = fs::path(PATCHY_SOURCE_DIR);
  const fs::path ui = repo / "src" / "ui";
  bool baseline_ok = false;
  const auto baseline = read_baseline(repo / "tests" / "core" / "ui_color_literal_baseline.txt", baseline_ok);
  CHECK(baseline_ok);

  std::map<std::string, FileHits> found;
  std::size_t scanned = 0;
  for (const auto& entry : fs::recursive_directory_iterator(ui)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const auto extension = entry.path().extension().string();
    if (extension != ".cpp" && extension != ".hpp" && extension != ".h" && extension != ".mm") {
      continue;
    }
    // lexically_relative: see translation_marker_tests.cpp (wasm NODERAWFS).
    const auto display = entry.path().lexically_relative(repo).generic_string();
    if (std::find(kExemptFiles.begin(), kExemptFiles.end(), display) != kExemptFiles.end()) {
      continue;
    }
    ++scanned;
    auto hits = scan_file(entry.path(), display);
    if (hits.count > 0) {
      found.emplace(display, std::move(hits));
    }
  }
  CHECK(scanned > 200);

  std::size_t total = 0;
  std::size_t ceiling = 0;
  std::vector<std::string> over;
  std::vector<std::string> under;
  for (const auto& [file, hits] : found) {
    total += hits.count;
    const auto it = baseline.find(file);
    const auto allowed = it == baseline.end() ? std::size_t{0} : it->second;
    if (hits.count > allowed) {
      over.push_back(file + ": " + std::to_string(hits.count) + " literal colors, baseline " +
                     std::to_string(allowed));
      for (const auto& line : hits.lines) {
        over.push_back("    " + line);
      }
    }
  }
  for (const auto& [file, allowed] : baseline) {
    ceiling += allowed;
    const auto it = found.find(file);
    const auto actual = it == found.end() ? std::size_t{0} : it->second.count;
    if (actual < allowed) {
      under.push_back(file + ": " + std::to_string(actual) + " (baseline " + std::to_string(allowed) + ")");
    }
  }
  std::fprintf(stderr, "  ui color literals: %zu in %zu files, ceiling %zu\n", total, found.size(), ceiling);
  if (!over.empty()) {
    std::fprintf(stderr,
                 "New hardcoded colors in src/ui (use a theme() role, docs/ui-conventions.md \"Color scheme\"; "
                 "mark genuine content with a `%s: <reason>` comment on the line):\n",
                 std::string(kLineMarker).c_str());
    for (const auto& line : over) {
      std::fprintf(stderr, "  %s\n", line.c_str());
    }
  }
  if (!under.empty()) {
    std::fprintf(stderr,
                 "Color literals were removed: lower these entries in tests/core/ui_color_literal_baseline.txt "
                 "(delete a line that reaches 0):\n");
    for (const auto& line : under) {
      std::fprintf(stderr, "  %s\n", line.c_str());
    }
  }
  CHECK(over.empty());
  CHECK(under.empty());
#endif
}

}  // namespace

std::vector<patchy::test::TestCase> ui_color_literal_tests() {
  return {
      {"ui_color_literals_stay_within_baseline", ui_color_literals_stay_within_baseline},
  };
}
