#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

// Where a path a document names would make Patchy connect (docs/smart-object-editing.md,
// "Network links"). On Windows, touching \\host\share (even a stat) connects to that host
// and signs in with the user's Windows credentials, so a path read from a file must not
// reach a server the user has not allowed. Local paths and mapped drive letters are Local:
// a mapped drive goes to a server the user chose. Plain string rules, the same on every
// platform, so tests pin them anywhere.
namespace patchy {

enum class PathLocationKind {
  Local,          // a drive, a POSIX path, or a relative path
  NetworkServer,  // \\host\share, //host/share, \\?\UNC\host\share
  Device,         // \\.\pipe\x, \\?\Volume{...}\, "\\\x": never opened from a document
};

struct PathLocation {
  PathLocationKind kind{PathLocationKind::Local};
  std::string server;  // NetworkServer only: the host, ASCII lower case
};

[[nodiscard]] inline PathLocation classify_path_location(std::string_view path) {
  const auto separator = [](char c) { return c == '/' || c == '\\'; };
  if (path.size() < 2 || !separator(path[0]) || !separator(path[1])) {
    return {};
  }
  auto rest = path.substr(2);
  const auto next_component = [&]() {
    std::size_t end = 0;
    while (end < rest.size() && !separator(rest[end])) {
      ++end;
    }
    const auto component = rest.substr(0, end);
    rest = rest.substr(std::min(end + 1, rest.size()));
    return component;
  };
  const auto lower = [](std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
    return out;
  };
  const auto server = [&](std::string_view host) {
    return host.empty() ? PathLocation{PathLocationKind::Device, {}}
                        : PathLocation{PathLocationKind::NetworkServer, lower(host)};
  };
  const auto first = next_component();
  if (first == "?" || first == ".") {
    const auto second = next_component();
    const bool drive = second.size() == 2 && second[1] == ':' &&
                       ((second[0] >= 'A' && second[0] <= 'Z') || (second[0] >= 'a' && second[0] <= 'z'));
    if (drive) {
      return {};  // \\?\C:\art: a long-path spelling of a local drive
    }
    if (lower(second) == "unc") {
      return server(next_component());
    }
    return {PathLocationKind::Device, {}};
  }
  return server(first);
}

}  // namespace patchy
