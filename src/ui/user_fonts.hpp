#pragma once

#include <QString>
#include <QStringList>

// User-added fonts: loose .ttf/.otf/.ttc files or a .zip of them, dropped onto
// the window. Fonts register into QFontDatabase for the running session and
// persist per platform (desktop: an AppData user-fonts directory reloaded at
// startup; wasm: an IndexedDB store restored by a startup poll). Registered
// application fonts are never removed at runtime (removeApplicationFont can
// crash live font users); clearing the store takes effect on the next launch.
namespace patchy::ui::user_fonts {

struct AddFontsResult {
  QStringList added_families;
  QStringList invalid_names;      // files or zip entries that failed to register
  QStringList zips_without_fonts; // dropped zips (malformed or fontless) that yielded nothing
  int duplicate_count = 0;        // fonts already added this session or restored
};

bool is_user_font_path(const QString& path);
bool is_zip_path(const QString& path);

// Register (and persist) the given local font or zip paths. Zip archives
// contribute their contained font entries.
AddFontsResult add_user_fonts(const QStringList& paths);

// Desktop persistence directory; empty on wasm (the store is IndexedDB).
QString user_fonts_directory();

// Desktop: synchronously registers every persisted font. Wasm: arms a QTimer
// that polls the page-side IndexedDB load and registers fonts as they arrive
// (page JS only writes plain state; C++ polls, per docs/wasm.md).
void restore_user_fonts_at_startup();

// Empties the persistence store. Fonts already registered stay usable until
// the app restarts (desktop) or the page reloads (wasm).
void clear_user_font_store();

// Bundled stand-ins for system families a browser cannot provide. Used on wasm
// twice: QFont::insertSubstitution at startup (rendering-level fallback) and
// the text tool's family matching (available_text_family_match), which needs
// its own lookup because substitutions never appear in
// QFontDatabase::families().
struct FamilyAlias {
  const char* missing;
  const char* bundled;
};
inline constexpr FamilyAlias kWasmFamilyAliases[] = {
    {"Arial", "Liberation Sans"},
    {"Helvetica", "Liberation Sans"},
    {"Times New Roman", "Liberation Serif"},
    {"Times", "Liberation Serif"},
    {"Courier New", "Liberation Mono"},
    {"Courier", "Liberation Mono"},
    {"Calibri", "Carlito"},
    {"Segoe UI", "Noto Sans"},
    {"Tahoma", "Noto Sans"},
    {"Verdana", "Noto Sans"},
    {"Georgia", "Noto Serif"},
    {"MS Gothic", "Noto Sans JP"},
    {"MS PGothic", "Noto Sans JP"},
    {"MS Mincho", "Noto Sans JP"},
    {"Meiryo", "Noto Sans JP"},
    {"Yu Gothic", "Noto Sans JP"},
};

}  // namespace patchy::ui::user_fonts
