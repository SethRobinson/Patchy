// Source lint for the Qt-free libraries (docs/localization.md): every diagnostic
// or preset name that reaches the user as plain English text must be wrapped in
// PATCHY_TRANSLATE_NOOP so lupdate extracts it. The UI translates the text at the
// display boundary (translate_data_text), so an unmarked literal is not a crash, it
// is a string that silently stays English in every language.

#include "test_harness.hpp"
#include "test_groups.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace fs = std::filesystem;

bool is_identifier_char(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

// Call spellings whose first argument is user-facing text when it is a literal.
constexpr std::string_view kMarkedCalls[] = {
    "throw std::runtime_error(",
    "throw std::invalid_argument(",
    "throw std::logic_error(",
    "throw std::out_of_range(",
    "warnings.push_back(",
    "warnings.emplace_back(",
    "notices.push_back(",
    "notices.emplace_back(",
    "notice(",
};

constexpr std::string_view kScannedDirectories[] = {"core", "formats", "psd", "filters",
                                                    "render", "plugins", "color", "support"};

constexpr std::string_view kVendoredDirectories[] = {"zstd", "miniz", "libheif", "libraw", "stb", "bcdec", "lcms2"};

bool is_vendored(const fs::path& path) {
  for (const auto& part : path) {
    const auto name = part.string();
    for (const auto vendored : kVendoredDirectories) {
      if (name == vendored) {
        return true;
      }
    }
  }
  return false;
}

std::vector<std::string> unmarked_literals(const fs::path& file, const std::string& text) {
  std::vector<std::string> findings;
  for (const auto call : kMarkedCalls) {
    std::size_t pos = 0;
    while ((pos = text.find(call, pos)) != std::string::npos) {
      const auto call_start = pos;
      pos += call.size();
      // "notice(" must be the whole identifier, not the tail of add_notice( etc.
      if (call_start > 0 && is_identifier_char(text[call_start - 1])) {
        continue;
      }
      const auto skip_space = [&text](std::size_t at) {
        while (at < text.size() &&
               (text[at] == ' ' || text[at] == '\n' || text[at] == '\r' || text[at] == '\t')) {
          ++at;
        }
        return at;
      };
      auto cursor = skip_space(pos);
      if (cursor >= text.size() || text[cursor] != '"') {
        continue;
      }
      // Walk the literal (and any adjacent literals). A literal that is the whole
      // argument is user-facing text; one followed by `+` is a concatenation, which
      // cannot match a catalog entry and stays English by design.
      while (cursor < text.size() && text[cursor] == '"') {
        ++cursor;
        while (cursor < text.size() && text[cursor] != '"') {
          if (text[cursor] == '\\') {
            ++cursor;
          }
          ++cursor;
        }
        cursor = skip_space(cursor + 1);
      }
      if (cursor < text.size() && text[cursor] == ')') {
        const auto line = static_cast<long>(std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(call_start), '\n')) + 1;
        findings.push_back(file.generic_string() + ":" + std::to_string(line) + ": " + std::string(call) + "\"...\")");
      }
    }
  }
  return findings;
}

void translation_markers_cover_core_messages() {
#ifndef PATCHY_SOURCE_DIR
  CHECK(false && "PATCHY_SOURCE_DIR is required");
#else
  const fs::path root = fs::path(PATCHY_SOURCE_DIR) / "src";
  std::vector<std::string> findings;
  std::size_t scanned = 0;
  for (const auto directory : kScannedDirectories) {
    const auto base = root / std::string(directory);
    if (!fs::exists(base)) {
      continue;
    }
    for (const auto& entry : fs::recursive_directory_iterator(base)) {
      if (!entry.is_regular_file()) {
        continue;
      }
      const auto extension = entry.path().extension().string();
      if (extension != ".cpp" && extension != ".hpp") {
        continue;
      }
      // lexically_relative, not relative: the latter goes through weakly_canonical,
      // which fails under the wasm build's NODERAWFS. Every path here is already
      // an absolute child of root, so the answer is purely lexical.
      if (is_vendored(entry.path().lexically_relative(root))) {
        continue;
      }
      std::ifstream in(entry.path(), std::ios::binary);
      const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      ++scanned;
      auto file_findings =
          unmarked_literals(entry.path().lexically_relative(root.parent_path()), text);
      findings.insert(findings.end(), file_findings.begin(), file_findings.end());
    }
  }
  CHECK(scanned > 100);
  if (!findings.empty()) {
    std::fprintf(stderr, "Unmarked user-facing literals (wrap in PATCHY_TRANSLATE_NOOP(\"QObject\", ...)):\n");
    for (const auto& finding : findings) {
      std::fprintf(stderr, "  %s\n", finding.c_str());
    }
  }
  CHECK(findings.empty());
#endif
}

}  // namespace

std::vector<patchy::test::TestCase> translation_marker_tests() {
  return {
      {"translation_markers_cover_core_messages", translation_markers_cover_core_messages},
  };
}
