#pragma once

// Finding the user's GIMP 3 install, its plug-in folders, interpreters and plug-in
// environment (docs/gimp-plugins.md, "Finding GIMP"). Facts used: the Windows
// installer's uninstall key, the standard install locations, and the formats of the
// files GIMP installs (interpreters/*.interp, environ/*.env, gimprc); see
// docs/gimp-plugins-provenance.md. Qt-free; Patchy and the helper both use it.

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace patchy::gimp {

struct GimpInstall {
  std::filesystem::path prefix;           // the folder holding bin/ and lib/ (Windows, Linux)
  std::filesystem::path lib_dir;          // <prefix>/lib/gimp/3.0: plug-ins, interpreters, environ
  std::filesystem::path data_dir;         // <prefix>/share/gimp/3.0
  std::filesystem::path bin_dir;          // GIMP's programs and libraries (PATH for plug-ins)
  std::filesystem::path user_dir;         // the user's GIMP settings for this version, may not exist
  std::string version;                    // "3.2.6" when known
  std::string series;                     // "3.2"
  std::string source;                     // how it was found ("registry", "path", "flatpak", "folder")
  bool flatpak{false};                    // GIMP's Flatpak (plug-ins run in its sandbox)
  std::filesystem::path flatpak_root;     // its installation (~/.local/share/flatpak or /var/lib/flatpak)
  std::string flatpak_app{"org.gimp.GIMP"};
  std::vector<std::filesystem::path> plugin_folders;  // system, user, then gimprc's plug-in-path
};

// Every GIMP 3 install this machine has, best first (a per-user install before an
// all-users one). Reads the registry or file system only; runs nothing.
[[nodiscard]] std::vector<GimpInstall> detect_gimp_installs();

// An install described by a folder the user picked: the install root, its bin or lib
// folder, or GIMP.app. Nullopt when no GIMP 3 lives there.
[[nodiscard]] std::optional<GimpInstall> gimp_install_from_folder(const std::filesystem::path& folder);

// The plug-in-path entries of a gimprc file, with ${gimp_dir} and ${gimp_plug_in_dir}
// expanded. Missing file: empty.
[[nodiscard]] std::vector<std::filesystem::path> gimprc_plugin_path(const std::filesystem::path& gimprc,
                                                                    const std::filesystem::path& gimp_dir,
                                                                    const std::filesystem::path& plugin_dir);

// interpreters/*.interp: program names, and file-extension rules.
struct Interpreters {
  std::map<std::string, std::string> programs;      // "python3" -> "pythonw.exe" (later files win)
  std::map<std::string, std::string> by_extension;  // "py" -> "python3" (lower-case extension)
};
[[nodiscard]] Interpreters load_interpreters(const std::vector<std::filesystem::path>& folders);

// environ/*.env: NAME=value lines with ${gimp_installation_dir} expanded, in file order.
[[nodiscard]] std::vector<std::pair<std::string, std::string>> load_environ(
    const std::vector<std::filesystem::path>& folders, const std::filesystem::path& installation_dir);

// GIMP 3 plug-ins: each in its own subfolder, named after it (<name>/<name>[.ext]).
[[nodiscard]] std::vector<std::filesystem::path> find_plugin_files(const std::filesystem::path& plugins_folder,
                                                                   const Interpreters& interpreters);

// The interpreter a plug-in file names: by its extension (interpreters/*.interp) or its
// first line ("#!/usr/bin/env python3" -> "python3"); empty for a native program.
[[nodiscard]] std::string plugin_interpreter(const std::filesystem::path& plugin_file, const Interpreters& interpreters);

// How to start one plug-in file: a native program, or an interpreter and the script.
struct PluginCommand {
  std::filesystem::path program;
  std::vector<std::string> leading_args;  // the script path for interpreted plug-ins (UTF-8)
  std::string interpreter;                // "python3", empty for native programs
};
// Nullopt with `error` set when the file cannot be started (no interpreter for it).
[[nodiscard]] std::optional<PluginCommand> resolve_plugin_command(const std::filesystem::path& plugin_file,
                                                                  const Interpreters& interpreters,
                                                                  const std::vector<std::filesystem::path>& search_path,
                                                                  std::string* error);

// The search path the environ files give plug-ins (their PATH), split.
[[nodiscard]] std::vector<std::filesystem::path> environ_search_path(
    const std::vector<std::pair<std::string, std::string>>& environment);

// GIMP 3's per-user settings folder for a series ("3.2") on this platform.
[[nodiscard]] std::filesystem::path default_user_dir(const std::string& series);

}  // namespace patchy::gimp
