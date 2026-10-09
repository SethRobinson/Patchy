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

#if defined(__EMSCRIPTEN__)
#include <vector>
#include <emscripten.h>
#endif

namespace patchy::test {

#if defined(__EMSCRIPTEN__)
// NODERAWFS exposes host paths, but wasm libc++ parses paths as POSIX even on a
// Windows host. Let Node validate absoluteness and resolve existing ancestors;
// never fall back to a lexical path when realpath fails (including dangling links).
inline std::filesystem::path resolve_node_test_scratch_path(const std::filesystem::path& path,
                                                          bool preserve_leaf, std::error_code& error) {
  const auto text = path.u8string();
  const auto resolve_into = [&](char8_t* buffer, int capacity) {
    return EM_ASM_INT({
    try {
      if (typeof process !== 'object' || !process.versions || !process.versions.node ||
          typeof require !== 'function' || typeof NODERAWFS === 'undefined') {
        return 0;
      }
      const hostPath = require('path');
      const hostFs = require('fs');
      const absolute = hostPath.resolve(UTF8ToString(arguments[0]));
      const leaf = arguments[1] ? hostPath.basename(absolute) : "";
      let parent = arguments[1] ? hostPath.dirname(absolute) : absolute;
      const missing = [];
      for (;;) {
        try {
          hostFs.lstatSync(parent);
          break;
        } catch (e) {
          if (e.code !== 'ENOENT') return 0;
          const next = hostPath.dirname(parent);
          if (next === parent) return 0;
          missing.push(hostPath.basename(parent));
          parent = next;
        }
      }
      // lstat succeeds for a dangling link, so realpath must succeed separately.
      const canonical = hostFs.realpathSync(parent);
      if (!hostFs.statSync(canonical).isDirectory()) return 0;
      let result = hostPath.join(canonical, ...missing.reverse(), leaf);
      if (!hostPath.isAbsolute(result)) return 0;
      if (process.platform === 'win32') result = result.split(hostPath.sep).join("/");
      const size = lengthBytesUTF8(result) + 1;
      if (size <= arguments[3]) stringToUTF8(result, arguments[2], arguments[3]);
      return size;
    } catch (e) {
      return 0;
    }
    }, text.c_str(), preserve_leaf, buffer, capacity);
  };
  // Allocate on the C++ side: malloc need not be exported to Emscripten's JS glue.
  const int size = resolve_into(nullptr, 0);
  if (size <= 0) {
    error = std::make_error_code(std::errc::io_error);
    return {};
  }
  std::vector<char8_t> resolved(static_cast<std::size_t>(size));
  if (resolve_into(resolved.data(), size) != size) {
    error = std::make_error_code(std::errc::io_error);
    return {};
  }
  error.clear();
  return std::filesystem::path(std::u8string(resolved.data()));
}
#endif

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
  auto temp = std::filesystem::temp_directory_path(error);
  if (error) {
    return false;
  }
#if defined(__EMSCRIPTEN__)
  temp = resolve_node_test_scratch_path(temp, false, error);
#else
  temp = std::filesystem::weakly_canonical(temp, error).lexically_normal();
#endif
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
#if defined(__EMSCRIPTEN__)
  // This result is already host-absolute. POSIX is_absolute() would reject C:/...
  // even though NODERAWFS passes that exact path to the Windows filesystem.
  auto absolute = resolve_node_test_scratch_path(path, true, error);
#else
  auto absolute = std::filesystem::absolute(path, error).lexically_normal();
  if (!error && !absolute.has_filename()) {
    absolute = absolute.parent_path();  // drop the trailing separator
  }
  // Resolve symlinks in the parent so a link inside test-artifacts cannot lead the delete
  // elsewhere; the last piece stays as named, so a link there is removed, not its target.
  if (!error) {
    absolute = std::filesystem::weakly_canonical(absolute.parent_path(), error) / absolute.filename();
  }
  if (!error && !absolute.is_absolute()) {
    error = std::make_error_code(std::errc::invalid_argument);
  }
#endif
  if (error ||
      !(is_below_test_scratch_root(absolute) || is_below_patchy_temp_dir(absolute))) {
    const auto text = path.u8string();
    throw std::runtime_error("refusing to recursively delete '" + std::string(text.begin(), text.end()) +
                             "': it is not below test-artifacts, qttest, or a patchy* temp folder");
  }
#if defined(__EMSCRIPTEN__)
  // musl remove_all cannot remove Windows directory links through NODERAWFS.
  // Only pass the validated, host-absolute path to Node; rm unlinks the final link.
  const auto absolute_text = absolute.u8string();
  return EM_ASM_INT({
    try {
      require('fs').rmSync(UTF8ToString(arguments[0]), {recursive: true, force: true});
      return 1;
    } catch (e) {
      return 0;
    }
  }, absolute_text.c_str()) != 0;
#else
  std::filesystem::remove_all(absolute, error);
  return !error;
#endif
}

}  // namespace patchy::test
