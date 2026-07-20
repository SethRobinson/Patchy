#pragma once

#include <QIcon>
#include <QImage>
#include <QString>

#include <vector>

namespace patchy::ui {

// One node of the script browser model shared by the File > Scripts menu and
// the Script Manager tree (docs/scripting.md). Folders carry children; files
// carry the absolute path to load/run. A bundled script shadowed by a user
// copy at the same relative path reports the USER copy as `path` (is_override
// set) and keeps the original in `bundled_path` so Revert to Bundled works.
struct ScriptFolderEntry {
  QString name;           // file base name (no extension) or raw folder name
  QString file_name;      // files: name with extension ("breakout.js"); empty for folders
  QString relative_path;  // "/"-separated path below the root ("Games/breakout.js")
  QString path;           // absolute path to load/run; empty for folders
  QString bundled_path;   // overridden bundled original; empty otherwise
  QString display_name;   // "@name" header directive; falls back to `name`
  QString description;    // "@description" header directive (may span lines)
  QString author;         // "@author" header directive
  QString cli_example;    // "@cli" header directive (example command-line tokens)
  QString icon_path;      // sidecar icon PNG (user copy wins); empty = none
  bool is_folder{false};
  bool is_override{false};
  bool opens_window{false};  // "@window" header directive
  std::vector<ScriptFolderEntry> children;
};

// Directives read from the comment block at the top of a script (docs/
// scripting.md): "// @name Breakout" sets the display name, "// @description
// ..." the hover-card blurb (repeated lines join with a space), "// @author
// ..." the credit line, "// @window" declares that the script creates its
// own window or document, and "// @cli ..." holds the extra tokens of the
// script's command-line example (repeated lines join with a space). Parsing
// stops at the first non-comment line (30 lines max).
struct ScriptMetadata {
  QString name;
  QString description;
  QString author;
  QString cli_example;
  bool opens_window{false};
};
[[nodiscard]] ScriptMetadata read_script_metadata(const QString& path);

// The copyable "run this script from a terminal" example the Script Manager
// shows: quoted exe path, --run-script with the quoted script path, then the
// script's @cli tokens verbatim. Without @cli, active-document scripts (no
// @window) get an "example.png" positional placeholder; @window scripts get
// the bare command. Empty inputs simply produce a shorter command - never an
// error (metadata may be missing entirely).
[[nodiscard]] QString script_cli_example_command(const QString& exe_path,
                                                 const QString& script_path,
                                                 const ScriptMetadata& meta);

// Recursively scans one scripts root: *.js files plus subfolders, folders
// first (by name), then files sorted by display name case-insensitively.
// Empty folders are dropped. File entries carry their header metadata and the
// sibling <base>.png icon when present.
[[nodiscard]] std::vector<ScriptFolderEntry> scan_script_folder(const QString& root);

// The merged model: bundled entries overlaid by user shadow copies at the same
// relative path, plus the user's own non-overriding scripts as a second tree
// ("My Scripts" shows only those). A user <base>.png also overrides a bundled
// script's icon on its own, independent of whether the .js is overridden.
struct ScriptScan {
  std::vector<ScriptFolderEntry> bundled;
  std::vector<ScriptFolderEntry> user;
};
[[nodiscard]] ScriptScan scan_scripts(const QString& bundled_root, const QString& user_root);

// Translated display name for the well-known bundled folder names (Games,
// Demos, Effects, Utilities); any other folder shows its raw on-disk name.
[[nodiscard]] QString script_folder_display_name(const QString& folder_name);

// Relative path of `path` under `root`, empty when `path` is not inside
// `root` (including the different-drive case, where QDir::relativeFilePath
// returns an absolute path instead of a ".."-prefixed one).
[[nodiscard]] QString relative_path_under(const QString& root, const QString& path);

// Where "Set Icon..." writes a script's icon: the user scripts root at the
// script's relative path with a .png extension. Bundled scripts stay pristine
// (the user icon shadows them); a user script's relative path already sits
// below the user root, so its icon lands beside the .js.
[[nodiscard]] QString script_icon_write_target(const QString& user_root,
                                               const QString& relative_path);

// Center-crops `image` to a square, smooth-scales it to 128x128 (large enough
// for the hover card; the tree scales down), and writes it to `target`
// (creating parent folders). False when the image is null or the write fails.
bool write_script_icon(const QImage& image, const QString& target);

// The icon shown for a script entry: its sidecar PNG when present and
// readable, else the generic code-drawn JS-page icon.
[[nodiscard]] QIcon script_entry_icon(const ScriptFolderEntry& entry);
[[nodiscard]] QIcon script_generic_icon();
// Code-drawn folder icon for the tree's folder and root rows.
[[nodiscard]] QIcon script_folder_icon();
// The red octagon stop sign (the Script Manager's Stop button and the
// running-script stop panel).
[[nodiscard]] QIcon script_stop_icon();
// The "C:\_" terminal window (the Script Manager's command-line example
// button).
[[nodiscard]] QIcon script_cli_icon();

}  // namespace patchy::ui
