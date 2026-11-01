#pragma once

// Running the plug-ins of GIMP's Flatpak (docs/gimp-plugins.md, "GIMP's Flatpak"). A
// plug-in starts inside GIMP's own sandbox through `flatpak run --command=<plug-in>
// org.gimp.GIMP`, which keeps descriptors 3 and 4 (the protocol pipes) open; from
// Patchy's own Flatpak the same command runs on the host through `flatpak-spawn --host
// --forward-fd=3 --forward-fd=4`. Linux only; Qt-free.

#include "plugins/gimp/gimp_host_link.hpp"
#include "plugins/gimp/gimp_install.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace patchy::gimp {

// Patchy itself runs in a Flatpak sandbox (/.flatpak-info exists).
[[nodiscard]] bool running_in_flatpak();

// Patchy's Flatpak may start host programs (its /.flatpak-info grants talk access to
// org.freedesktop.Flatpak). `flatpak_info` is that file's text.
[[nodiscard]] bool flatpak_info_allows_host_commands(const std::string& flatpak_info);

// The path a file of GIMP's Flatpak has inside its sandbox: the app's files folder is
// /app and an extension's files folder /app/extensions/<Name> (org.gimp.GIMP.Plugin.<Name>).
// Other paths are unchanged (GIMP's sandbox shares the home folder and /tmp).
[[nodiscard]] std::filesystem::path flatpak_sandbox_path(const GimpInstall& install, const std::filesystem::path& host_path);

// How a plug-in file starts inside the sandbox, in sandbox paths: a native program, or
// its interpreter by name (the sandbox's PATH finds python3 and Script-Fu's interpreter)
// and the script.
[[nodiscard]] std::optional<PluginCommand> flatpak_plugin_command(const GimpInstall& install,
                                                                  const std::filesystem::path& plugin_file,
                                                                  const Interpreters& interpreters, std::string* error);

struct FlatpakLaunchOptions {
  std::vector<std::string> environment;  // NAME=value set inside the sandbox
  std::vector<std::filesystem::path> shared_folders;  // made visible inside (Patchy's profile folder)
  std::string program;          // absolute path of flatpak, or of flatpak-spawn when from_flatpak
  bool from_flatpak{false};     // Patchy runs in its own Flatpak: go through flatpak-spawn --host
};

// The launch for one plug-in: `program` is flatpak (or flatpak-spawn) and the sandbox
// command follows `--command=`; the helper appends GIMP's -gimp arguments as usual.
[[nodiscard]] link::LaunchSpec flatpak_launch(const GimpInstall& install, const PluginCommand& sandbox_command,
                                              const FlatpakLaunchOptions& options);

}  // namespace patchy::gimp
