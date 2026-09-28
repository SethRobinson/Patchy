#pragma once

// The plug-ins folder for classic Photoshop .8bf filters: `plugins` next to the
// application on Windows (the portable zip and the per-user installer both keep
// it writable), nothing on other platforms, where the plug-ins cannot run. The
// About dialog, the Plugins menu and patchy.plugins.folder all go through here
// so the folder is created with its README the first time anyone looks for it.
// See docs/plugins.md.

#include <QString>

namespace patchy::ui {

// Absolute path of the plug-ins folder, or empty when this platform has none.
[[nodiscard]] QString legacy_plugins_folder_path();

// The README.txt dropped into a fresh plug-ins folder. Identical to the shipped
// packaging/plugins/README.txt (a test pins that).
[[nodiscard]] QString legacy_plugins_readme_text();

// Creates the folder and its README.txt when missing. Returns false, with a
// translated message in `error`, when the folder cannot be created or this
// platform has no plug-ins folder.
bool ensure_legacy_plugins_folder(QString* error = nullptr);

}  // namespace patchy::ui
