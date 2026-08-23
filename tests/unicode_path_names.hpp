#pragma once

// Shared file-name fixtures for the Unicode / special-character path tests (core and
// UI suites). Every name is spelled with \u / \U escapes in u8 literals, never raw
// non-ASCII bytes: the Qt-free core test binary compiles without -utf-8, so raw
// bytes would be read through the ANSI code page (and 0x81-style bytes raise C4819).
// u8 literals are UTF-8 whatever the execution charset, and
// std::filesystem::path(std::u8string) decodes them as UTF-8 on every platform.

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace patchy::test {

// Directory component: Japanese katakana + accented Latin + an astral-plane emoji
// (U+1F3A8, artist palette) + a space.
inline constexpr std::u8string_view kUnicodeDirName = u8"\u30C6\u30B9\u30C8 caf\u00E9 \U0001F3A8 dir";

// One stem per character class so a failure pinpoints the class that broke.
//  0: Japanese hiragana "sesu" (the name typed in the August 2026 Save As bug)
//  1: accented Latin
//  2: astral-plane emoji (a surrogate pair in UTF-16 / NTFS)
//  3: the mojibake name that bug actually wrote (U+0081 is a C1 control; the name
//     is legal on NTFS and must open as-is)
//  4: every special character Windows allows. `%20` and `#` are canaries for a
//     stray QUrl decode; `~` never leads a name because Qt's widget file dialog
//     tilde-expands typed names on POSIX; `<>:"|?*` are illegal on Windows.
inline constexpr std::array<std::u8string_view, 5> kUnicodePathStems = {
    u8"\u305B\u3059",
    u8"caf\u00E9 r\u00E9sum\u00E9",
    u8"\U0001F3A8 art",
    u8"\u00E3\u0081\u203A\u00E3\u0081\u2122",
    u8"sp ace #1 50% %20 &and 'q' a+b [x] ! , ; = @ x~y (v2)",
};

// Every class in one name (about 70 characters, well inside MAX_PATH under a
// build tree's test-artifacts folder).
inline constexpr std::u8string_view kUnicodeCombinedStem =
    u8"\u305B\u3059 caf\u00E9 \U0001F3A8 \u00E3\u0081\u203A #1 50% %20 &and 'q' a+b [x] ! , ; = @ x~y (v2)";

inline std::string utf8_string(std::u8string_view text) { return std::string(text.begin(), text.end()); }

inline std::filesystem::path unicode_path_piece(std::u8string_view text) {
  return std::filesystem::path(std::u8string(text));
}

// A fresh test-artifacts/<kUnicodeDirName>/<leaf> directory (removed first so stale
// or mojibake files from an earlier run cannot satisfy the listing checks).
inline std::filesystem::path unicode_artifact_dir(std::u8string_view leaf) {
  const auto dir =
      std::filesystem::path("test-artifacts") / unicode_path_piece(kUnicodeDirName) / unicode_path_piece(leaf);
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  return dir;
}

// True when `dir` holds exactly `expected` (same count, and every entry is equivalent
// to one expected path). A mojibake sibling fails the count; a mangled expected name
// fails the lookup. `equivalent` keeps macOS NFD normalization out of the comparison.
inline bool directory_holds_only(const std::filesystem::path& dir,
                                 const std::vector<std::filesystem::path>& expected) {
  std::size_t count = 0;
  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    ++count;
    bool matched = false;
    for (const auto& path : expected) {
      std::error_code error;
      if (std::filesystem::equivalent(entry.path(), path, error) && !error) {
        matched = true;
        break;
      }
    }
    if (!matched) {
      return false;
    }
  }
  return count == expected.size();
}

}  // namespace patchy::test
