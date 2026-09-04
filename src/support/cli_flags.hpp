#pragma once

#include <cstring>

namespace patchy {

// Exact "--headless" token scan for main() BEFORE the QApplication exists: Qt reads
// QT_QPA_PLATFORM only at construction, so the platform choice cannot wait for
// QCommandLineParser. No "=value" form, no abbreviation, and "--" ends the scan the
// way it ends option parsing. QCommandLineParser still owns the real parse.
[[nodiscard]] inline bool headless_flag_present(int argc, const char* const* argv) noexcept {
  for (int i = 1; argv != nullptr && i < argc; ++i) {
    if (argv[i] == nullptr) {
      continue;
    }
    if (std::strcmp(argv[i], "--") == 0) {
      return false;
    }
    if (std::strcmp(argv[i], "--headless") == 0) {
      return true;
    }
  }
  return false;
}

}  // namespace patchy
