#include "plugins/gimp/gimp_flatpak.hpp"

#include "support/path_utils.hpp"

#include <sstream>
#include <system_error>

namespace patchy::gimp {

namespace fs = std::filesystem;

namespace {

fs::path from_utf8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

// `path` below `base`: the rest, or nullopt.
std::optional<fs::path> relative_below(const fs::path& path, const fs::path& base) {
  if (base.empty()) {
    return std::nullopt;
  }
  auto p = path.lexically_normal();
  auto b = base.lexically_normal();
  auto pi = p.begin();
  for (auto bi = b.begin(); bi != b.end(); ++bi, ++pi) {
    if (bi->empty()) {
      continue;  // a trailing separator
    }
    if (pi == p.end() || *pi != *bi) {
      return std::nullopt;
    }
  }
  fs::path rest;
  for (; pi != p.end(); ++pi) {
    rest /= *pi;
  }
  return rest;
}

// Sandbox paths are POSIX paths, also when the tests build them on Windows.
fs::path sandbox_join(std::string base, const std::vector<fs::path>& parts, std::size_t from) {
  for (std::size_t i = from; i < parts.size(); ++i) {
    base += "/" + path_to_utf8(parts[i]);
  }
  return from_utf8(base);
}

}  // namespace

bool running_in_flatpak() {
  std::error_code ec;
  return fs::exists("/.flatpak-info", ec);
}

bool flatpak_info_allows_host_commands(const std::string& flatpak_info) {
  std::istringstream lines(flatpak_info);
  std::string line;
  bool in_policy = false;
  while (std::getline(lines, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
      line.pop_back();
    }
    if (!line.empty() && line.front() == '[') {
      in_policy = line == "[Session Bus Policy]";
      continue;
    }
    if (in_policy && line == "org.freedesktop.Flatpak=talk") {
      return true;
    }
  }
  return false;
}

fs::path flatpak_sandbox_path(const GimpInstall& install, const fs::path& host_path) {
  if (auto rest = relative_below(host_path, install.prefix)) {
    return sandbox_join("/app", std::vector<fs::path>(rest->begin(), rest->end()), 0);
  }
  // <root>/runtime/<app>.Plugin.<Name>/<arch>/<branch>/active/files/...
  if (auto rest = relative_below(host_path, install.flatpak_root / "runtime")) {
    const std::vector<fs::path> parts(rest->begin(), rest->end());
    const std::string prefix = install.flatpak_app + ".Plugin.";
    const auto id = parts.empty() ? std::string() : path_to_utf8(parts[0]);
    if (parts.size() > 5 && id.rfind(prefix, 0) == 0 && parts[3] == "active" && parts[4] == "files") {
      return sandbox_join("/app/extensions/" + id.substr(prefix.size()), parts, 5);
    }
  }
  return host_path;
}

std::optional<PluginCommand> flatpak_plugin_command(const GimpInstall& install, const fs::path& plugin_file,
                                                    const Interpreters& interpreters, std::string* error) {
  // Files outside the Flatpak (the user's plug-ins folder, added folders) keep their
  // path: GIMP's sandbox shares the home folder.
  const auto sandbox_file = flatpak_sandbox_path(install, plugin_file);
  if (sandbox_file.generic_u8string().rfind(u8"/", 0) != 0) {  // POSIX-absolute, also in Windows tests
    if (error != nullptr) {
      *error = "GIMP's sandbox cannot start " + path_to_utf8(plugin_file);
    }
    return std::nullopt;
  }
  PluginCommand command;
  const auto interpreter = plugin_interpreter(plugin_file, interpreters);
  if (interpreter.empty()) {
    command.program = sandbox_file;
    return command;
  }
  // The sandbox's own interpreter: a registered program (GIMP's .interp files name
  // sandbox paths) or the name, which the sandbox's PATH resolves.
  std::string program = interpreter;
  if (const auto it = interpreters.programs.find(interpreter); it != interpreters.programs.end()) {
    program = it->second;
  }
  command.program = from_utf8(program);
  command.leading_args.push_back(path_to_utf8(sandbox_file));
  command.interpreter = interpreter;
  return command;
}

link::LaunchSpec flatpak_launch(const GimpInstall& install, const PluginCommand& sandbox_command,
                                const FlatpakLaunchOptions& options) {
  link::LaunchSpec launch;
  launch.program = options.program;
  auto& args = launch.leading_args;
  if (options.from_flatpak) {
    // --watch-bus: the host side ends when the helper (and its bus connection) does.
    args = {"--host", "--watch-bus", "--forward-fd=3", "--forward-fd=4", "flatpak"};
  }
  args.emplace_back("run");
  const bool system_wide = relative_below(install.flatpak_root, "/var/lib/flatpak").has_value();
  args.emplace_back(system_wide ? "--system" : "--user");
  args.emplace_back("--die-with-parent");
  const auto script = sandbox_command.leading_args.empty() ? path_to_utf8(sandbox_command.program)
                                                           : sandbox_command.leading_args.front();
  const auto slash = script.rfind('/');
  if (!script.empty() && script.front() == '/' && slash != std::string::npos && slash > 0) {
    args.push_back("--cwd=" + script.substr(0, slash));
  }
  for (const auto& shared : options.shared_folders) {
    args.push_back("--filesystem=" + path_to_utf8(shared));
  }
  for (const auto& entry : options.environment) {
    args.push_back("--env=" + entry);
  }
  args.push_back("--command=" + path_to_utf8(sandbox_command.program));
  args.push_back(install.flatpak_app);
  args.insert(args.end(), sandbox_command.leading_args.begin(), sandbox_command.leading_args.end());
  return launch;
}

}  // namespace patchy::gimp
