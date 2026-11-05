#pragma once

// Patchy started from an AppImage. The AppImage runtime mounts the image at a new
// /tmp/.mount_* directory on every launch and sets APPIMAGE to the .AppImage file and
// APPDIR to the mount, so anything shown to the user, stored, or compared across
// launches names the .AppImage file, never applicationFilePath(). See docs/appimage.md.

#include "support/cli_flags.hpp"

#include <QString>

namespace patchy::ui {

// The connector argument, `<file>.AppImage patchy-mcp`, is kAppImageConnectorArgument in
// support/cli_flags.hpp: Patchy's AppRun and the patchy binary itself both accept it.
// Set to 1 by AppRun when it chose QT_QPA_PLATFORMTHEME itself, so a companion Patchy starts
// gets the user's own setting back (src/ui/companion_apps.cpp).
inline constexpr char kAppRunSetPlatformThemeVariable[] = "PATCHY_APPRUN_SET_PLATFORMTHEME";

// The absolute path of the running .AppImage file. Empty off Linux, inside a Flatpak,
// and when this executable does not live under APPDIR: an AppImage'd program (an AI
// app, an IDE terminal) that starts a native Patchy leaves its own APPIMAGE and APPDIR
// in that Patchy's environment.
[[nodiscard]] QString appimage_file_path();

// appimage_file_path() when it is set, else QCoreApplication::applicationFilePath().
[[nodiscard]] QString user_visible_executable_path();

}  // namespace patchy::ui
