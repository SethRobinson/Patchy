#pragma once

#include <QString>

// Per-user application data (QStandardPaths::AppDataLocation: the dropped-font
// store `user-fonts/` and the user scripts folder `scripts/`) lives under
// `<platform app data>/<organization name>/Patchy`. Releases through 0.98 set the
// organization name to "Seth A. Robinson"; later releases use "RTsoft"
// (`%APPDATA%\RTsoft\Patchy` on Windows, `~/Library/Application Support/RTsoft/Patchy`
// on macOS, `~/.local/share/RTsoft/Patchy` on Linux). Preferences are unaffected:
// `app_settings()` names its own organization ("Patchy") explicitly.
//
// So an upgraded install keeps its fonts and scripts, startup merges the legacy
// folder into the current one before anything reads it. The merge is idempotent
// and never overwrites: an existing current file wins, an identical legacy copy is
// dropped, a differing legacy copy stays where it was for the user to sort out.
// Empty legacy folders (down to the organization folder) are removed afterwards.
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
// touches only those two trees, so tests drive it with temporary folders.
MigrationResult migrate_app_data_directory(const QString& legacy_dir, const QString& current_dir);

// The AppDataLocation the legacy organization name resolved to (empty on wasm).
QString legacy_app_data_directory();

// Resolves both folders for the running application and merges. Call after
// setOrganizationName and before anything reads AppDataLocation. No-op on wasm.
MigrationResult migrate_legacy_app_data();

}  // namespace patchy::ui::app_data_migration
