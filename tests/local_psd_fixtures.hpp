#pragma once

#include <cstdlib>
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

// PATCHY_LOCAL_FIXTURES_DIR, empty when unset. Read as UTF-16 on Windows: the folder may
// have a non-ASCII name.
inline std::filesystem::path local_fixtures_override() {
  std::filesystem::path result;
#ifdef _MSC_VER
  wchar_t* buffer = nullptr;
  std::size_t length = 0;
  if (_wdupenv_s(&buffer, &length, L"PATCHY_LOCAL_FIXTURES_DIR") == 0 && buffer != nullptr && buffer[0] != L'\0') {
    result = buffer;
  }
  std::free(buffer);
#else
  if (const char* value = std::getenv("PATCHY_LOCAL_FIXTURES_DIR"); value != nullptr && value[0] != '\0') {
    result = value;
  }
#endif
  return result;
}

// True when the local fixtures come from PATCHY_LOCAL_FIXTURES_DIR: they then belong to
// another checkout, so nothing may write into them (docs/testing-skips.md).
inline bool local_fixtures_overridden() {
  return !local_fixtures_override().empty();
}

// The untracked local fixture folder: PATCHY_LOCAL_FIXTURES_DIR when set (a worktree reads
// the main checkout's fixtures without a junction), else <source>/local-test-fixtures.
inline std::filesystem::path local_fixtures_root() {
  if (auto root = local_fixtures_override(); !root.empty()) {
    return root;
  }
  return source_root_path() / "local-test-fixtures";
}

inline std::filesystem::path committed_psd_fixture_path(std::string_view file_name) {
  return source_root_path() / "test-fixtures" / "psd" / std::string(file_name);
}

inline std::filesystem::path local_psd_fixture_path(std::string_view file_name) {
  return local_fixtures_root() / "psd" / std::string(file_name);
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
  return local_fixtures_root() / std::string(format_dir) / std::string(file_name);
}

// The Filter Foundry test plug-ins (docs/plugins.md): GPL binaries the repository does
// not carry. scripts/fetch-test-plugins.{ps1,sh} downloads them, hash-checked, into
// this untracked folder; tests that need them skip while it is missing.
inline std::filesystem::path filter_foundry_fixture_dir() {
  return local_fixtures_root() / "photoshop-plugins" / "filter-foundry";
}

inline std::filesystem::path filter_foundry_fixture_path(std::string_view file_name) {
  return filter_foundry_fixture_dir() / std::string(file_name);
}

}  // namespace patchy::test
