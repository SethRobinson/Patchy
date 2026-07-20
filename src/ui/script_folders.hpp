#pragma once

#include <QString>

#include <vector>

namespace patchy::ui {

// One node of the script browser model shared by the File > Scripts menu and
// the Script Editor tree (docs/scripting.md). Folders carry children; files
// carry the absolute path to load/run. A bundled script shadowed by a user
// copy at the same relative path reports the USER copy as `path` (is_override
// set) and keeps the original in `bundled_path` so Revert to Bundled works.
struct ScriptFolderEntry {
  QString name;           // file base name (no extension) or raw folder name
  QString file_name;      // files: name with extension ("breakout.js"); empty for folders
  QString relative_path;  // "/"-separated path below the root ("Games/breakout.js")
  QString path;           // absolute path to load/run; empty for folders
  QString bundled_path;   // overridden bundled original; empty otherwise
  bool is_folder{false};
  bool is_override{false};
  std::vector<ScriptFolderEntry> children;
};

// Recursively scans one scripts root: *.js files plus subfolders, folders
// first, each group sorted by name case-insensitively. Empty folders are
// dropped.
[[nodiscard]] std::vector<ScriptFolderEntry> scan_script_folder(const QString& root);

// The merged model: bundled entries overlaid by user shadow copies at the same
// relative path, plus the user's own non-overriding scripts as a second tree
// ("My Scripts" shows only those).
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

}  // namespace patchy::ui
