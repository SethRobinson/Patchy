#pragma once

#include <QString>

// Per-user application data (QStandardPaths::AppDataLocation: the dropped-font
// store `user-fonts/` and the user scripts folder `scripts/`) lives under
// `<platform app data>/<organization name>/Patchy`. Releases through 0.98 set the
// organization name to "Seth A. Robinson"; later releases use "RTsoft"
// (`%APPDATA%\RTsoft\Patchy` on Windows, `~/Library/Application Support/RTsoft/Patchy`
// on macOS, `~/.local/share/RTsoft/Patchy` on Linux).
//
// Preferences (`Patchy.ini`) and the libraries stored beside it (brushes, brush presets,
// gradients, Looks, patterns, shapes, styles) lived in `<Qt user ini root>/Patchy` through
// 1.19, because `app_settings()` named the organization "Patchy" (`%APPDATA%\Patchy`,
// `~/.config/Patchy`). They now live beside Patchy.ini at app_settings_file_path(): the
// AppDataLocation folder itself on Windows and macOS, `~/.config/RTsoft/Patchy` on Linux.
//
// Machine-specific data lives in AppLocalDataLocation (`%LOCALAPPDATA%\RTsoft\Patchy`, the
// same folder as AppDataLocation on macOS and Linux): `ai-models/` (downloaded models),
// `AutoRecover/` and `stress-reports/`, beside `updates/`. Through 1.19 the first two were in
// AppDataLocation and the reports beside Patchy.ini, so on Windows they roamed with the profile.
//
// So an upgraded install keeps all of it, startup merges each legacy folder into the
// current one before anything reads it. The merge
// is idempotent and never overwrites: an existing current file wins, an identical
// legacy copy is dropped, a differing legacy copy stays where it was for the user to
// sort out. Empty legacy folders are removed afterwards.
namespace patchy::ui::app_data_migration {

inline constexpr const char* kLegacyOrganizationName = "Seth A. Robinson";

struct MigrationResult {
  bool legacy_found = false;    // the legacy folder existed when the migration ran
  bool completed = false;       // nothing is left in the legacy folder and it is gone
  int files_moved = 0;          // legacy files now under the current folder
  int duplicates_removed = 0;   // legacy files identical to an existing current file
  int conflicts_kept = 0;       // legacy files that differ from the current file; left in place
};

// Merges `legacy_dir` into `current_dir` as described above. Pure in the sense that it
// touches only those two trees, so tests drive it with temporary folders. An emptied
// `legacy_dir` is removed, and with `remove_empty_parent` its parent too when that
// leaves it empty (the legacy organization folder).
MigrationResult migrate_app_data_directory(const QString& legacy_dir, const QString& current_dir,
                                           bool remove_empty_parent = true);

// The AppDataLocation the legacy organization name resolved to (empty on wasm).
QString legacy_app_data_directory();

// Resolves both folders for the running application and merges. Call after
// setOrganizationName and before anything reads AppDataLocation. No-op on wasm.
MigrationResult migrate_legacy_app_data();

// The folder Patchy.ini lived in through 1.19 (empty on wasm). Follows set_app_settings_root.
QString legacy_settings_directory();

// Merges legacy_settings_directory() into the folder of app_settings_file_path(). Call
// after any set_app_settings_root and before the first app_settings() read; it needs no
// QCoreApplication. Its parent (the user ini root) is never removed. No-op on wasm.
MigrationResult migrate_legacy_settings();

// Merges `ai-models/`, `AutoRecover/` and `stress-reports/` from `roaming_dir` (AppDataLocation)
// and `stress-reports/` from `settings_dir` into the same folders under `local_dir`
// (AppLocalDataLocation). Each emptied legacy folder is removed, never its parent. A no-op
// where the folders coincide. Pure like migrate_app_data_directory, for tests.
MigrationResult migrate_local_data(const QString& roaming_dir, const QString& local_dir,
                                   const QString& settings_dir);

// migrate_local_data for the running application. Call after migrate_legacy_settings and
// migrate_legacy_app_data, before anything reads the moved folders. No-op on wasm.
MigrationResult migrate_local_app_data();

}  // namespace patchy::ui::app_data_migration
