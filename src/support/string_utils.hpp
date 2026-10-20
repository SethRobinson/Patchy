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

// Photoshop names its own presets (patterns, styles, gradients) with a localization key and
// the English default after the first '=': "$$$/Presets/Patterns/Patterns_pat/TieDye=Tie Dye".
// Returns the text a user should see: that default, or the key's last segment when there is
// none. Any other name comes back unchanged. Files keep the raw name (GitHub issue 271).
[[nodiscard]] inline std::string photoshop_zstring_display_text(std::string_view name) {
  if (!name.starts_with("$$$/")) {
    return std::string(name);
  }
  const auto equals = name.find('=');
  if (equals == std::string_view::npos || equals + 1U >= name.size()) {
    const auto key = name.substr(0, equals);
    const auto slash = key.rfind('/');
    return std::string(key.substr(slash + 1U));
  }
  return std::string(name.substr(equals + 1U));
}

}  // namespace patchy
