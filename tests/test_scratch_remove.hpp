#pragma once

// The only way test code deletes a directory tree. A recursive delete of a blank or
// mistaken path (QDir("") is the current directory; a relative path follows the cwd)
// can wipe a build tree or a real user profile, so this refuses, by throwing, any path
// that is blank or does not lie strictly below a "test-artifacts" folder, Qt's
// QStandardPaths test-mode folder ("qttest" / ".qttest"), or a "patchy..." folder in the
// system temp directory. Qt-free so the core suite can use it; the UI suite wraps it for
// QString paths (remove_test_scratch_dir).

#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace patchy::test {

// True when `absolute` names something strictly below a test-artifacts or qttest folder.
inline bool is_below_test_scratch_root(const std::filesystem::path& absolute) {
  bool below_marker = false;
  for (const auto& piece : absolute) {
    const auto name = piece.u8string();
    if (name.empty()) {
      continue;  // the trailing piece of "dir/"
    }
    if (below_marker) {
      return true;
    }
    below_marker = name == u8"test-artifacts" || name == u8"qttest" || name == u8".qttest";
  }
  return false;
}

// True when `absolute` names something strictly below a "patchy..." folder directly inside
// the system temp directory (QTemporaryDir's default <temp>/<app name>-XXXXXX).
inline bool is_below_patchy_temp_dir(const std::filesystem::path& absolute) {
  std::error_code error;
  // Canonical, like the path it is compared with: macOS's temp folder sits behind the
  // /var -> /private/var symlink.
  auto temp = std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(error), error).lexically_normal();
  if (error || temp.empty()) {
    return false;
  }
  if (!temp.has_filename()) {
    temp = temp.parent_path();  // drop the trailing separator
  }
  const auto relative = absolute.lexically_relative(temp);
  auto piece = relative.begin();
  if (relative.empty() || piece == relative.end()) {
    return false;
  }
  constexpr std::u8string_view kPrefix = u8"patchy";
  const auto owner = piece->u8string();
  if (owner.size() <= kPrefix.size()) {
    return false;
  }
  for (std::size_t i = 0; i < kPrefix.size(); ++i) {
    auto c = owner[i];
    if (c >= u8'A' && c <= u8'Z') {
      c = static_cast<char8_t>(c - u8'A' + u8'a');
    }
    if (c != kPrefix[i]) {
      return false;
    }
  }
  for (++piece; piece != relative.end(); ++piece) {
    if (!piece->empty()) {
      return true;
    }
  }
  return false;
}

// Removes `path` and everything under it; true when it no longer exists (a missing path
// counts, like QDir::removeRecursively). Throws for a path outside the test scratch roots.
inline bool remove_test_scratch_tree(const std::filesystem::path& path) {
  if (path.empty()) {
    throw std::runtime_error("refusing to recursively delete a blank path");
  }
  std::error_code error;
  auto absolute = std::filesystem::absolute(path, error).lexically_normal();
  if (!error && !absolute.has_filename()) {
    absolute = absolute.parent_path();  // drop the trailing separator
  }
  // Resolve symlinks in the parent so a link inside test-artifacts cannot lead the delete
  // elsewhere; the last piece stays as named, so a link there is removed, not its target.
  if (!error) {
    absolute = std::filesystem::weakly_canonical(absolute.parent_path(), error) / absolute.filename();
  }
  if (error || !absolute.is_absolute() ||
      !(is_below_test_scratch_root(absolute) || is_below_patchy_temp_dir(absolute))) {
    const auto text = path.u8string();
    throw std::runtime_error("refusing to recursively delete '" + std::string(text.begin(), text.end()) +
                             "': it is not below test-artifacts, qttest, or a patchy* temp folder");
  }
  std::filesystem::remove_all(absolute, error);
  return !error;
}

}  // namespace patchy::test
