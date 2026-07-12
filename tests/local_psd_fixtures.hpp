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

inline std::filesystem::path committed_abr_fixture_path(std::string_view file_name) {
  return source_root_path() / "test-fixtures" / "abr" / std::string(file_name);
}

// Generic committed-fixture path for the flat image formats (ico, tga, gif, aseprite, pcx,
// ilbm, ...): test-fixtures/<format_dir>/<file_name>.
inline std::filesystem::path committed_format_fixture_path(std::string_view format_dir, std::string_view file_name) {
  return source_root_path() / "test-fixtures" / std::string(format_dir) / std::string(file_name);
}

// Generic untracked local fixture path (files copied from outside the project
// per AGENTS.md; tests using these skip when the file is absent, e.g. on the
// remote builders).
inline std::filesystem::path local_format_fixture_path(std::string_view format_dir, std::string_view file_name) {
  return source_root_path() / "local-test-fixtures" / std::string(format_dir) / std::string(file_name);
}

}  // namespace patchy::test
