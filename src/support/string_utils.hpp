#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace patchy {

[[nodiscard]] inline std::string ascii_lower_copy(std::string_view text) {
  std::string lower(text);
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return lower;
}

[[nodiscard]] inline std::string normalized_extension(std::string_view extension, bool keep_dot = true) {
  std::string normalized(extension);
  if (normalized.empty()) {
    return normalized;
  }
  if (normalized.front() == '.') {
    if (!keep_dot) {
      normalized.erase(normalized.begin());
    }
  } else if (keep_dot) {
    normalized.insert(normalized.begin(), '.');
  }
  return ascii_lower_copy(normalized);
}

}  // namespace patchy
