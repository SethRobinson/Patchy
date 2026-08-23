#pragma once

#include <filesystem>
#include <string>

namespace patchy {

// UTF-8 text for a path or path piece (a stem, an extension). Never use
// std::filesystem::path::string() for that: on MSVC it converts through the ANSI
// code page and silently replaces every unrepresentable character with '?'.
[[nodiscard]] inline std::string path_to_utf8(const std::filesystem::path& path) {
  const auto utf8 = path.u8string();
  return std::string(utf8.begin(), utf8.end());
}

}  // namespace patchy
