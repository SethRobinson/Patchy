#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace patchy::test {

inline std::filesystem::path source_root_path() {
#ifdef PATCHY_SOURCE_DIR
  return std::filesystem::path(PATCHY_SOURCE_DIR);
#else
  return std::filesystem::current_path();
#endif
}

inline std::filesystem::path committed_psd_fixture_path(std::string_view file_name) {
  return source_root_path() / "test-fixtures" / "psd" / std::string(file_name);
}

inline std::filesystem::path local_psd_fixture_path(std::string_view file_name) {
  return source_root_path() / "local-test-fixtures" / "psd" / std::string(file_name);
}

}  // namespace patchy::test
