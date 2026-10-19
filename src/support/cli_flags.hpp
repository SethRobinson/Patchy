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

// Value of "--language <code>" or "--language=<code>" for main(): the UI language for
// this run only, applied after the saved preference loads and never persisted. Returns
// nullptr when absent. Same scan rules as headless_flag_present; QCommandLineParser
// still validates the option.
[[nodiscard]] inline const char* language_flag_value(int argc, const char* const* argv) noexcept {
  constexpr const char* kFlag = "--language";
  constexpr int kFlagLength = 10;
  for (int i = 1; argv != nullptr && i < argc; ++i) {
    if (argv[i] == nullptr) {
      continue;
    }
    if (std::strcmp(argv[i], "--") == 0) {
      return nullptr;
    }
    if (std::strcmp(argv[i], kFlag) == 0) {
      return i + 1 < argc ? argv[i + 1] : nullptr;
    }
    if (std::strncmp(argv[i], kFlag, kFlagLength) == 0 && argv[i][kFlagLength] == '=') {
      return argv[i] + kFlagLength + 1;
    }
  }
  return nullptr;
}

// Value of "--startup-benchmark <report.json>" or "--startup-benchmark=<report.json>" for
// main(), which must switch the startup trace on before anything it times runs. Returns
// nullptr when absent. Same scan rules as headless_flag_present.
[[nodiscard]] inline const char* startup_benchmark_flag_value(int argc, const char* const* argv) noexcept {
  constexpr const char* kFlag = "--startup-benchmark";
  constexpr int kFlagLength = 19;
  for (int i = 1; argv != nullptr && i < argc; ++i) {
    if (argv[i] == nullptr) {
      continue;
    }
    if (std::strcmp(argv[i], "--") == 0) {
      return nullptr;
    }
    if (std::strcmp(argv[i], kFlag) == 0) {
      return i + 1 < argc ? argv[i + 1] : nullptr;
    }
    if (std::strncmp(argv[i], kFlag, kFlagLength) == 0 && argv[i][kFlagLength] == '=') {
      return argv[i] + kFlagLength + 1;
    }
  }
  return nullptr;
}

}  // namespace patchy
