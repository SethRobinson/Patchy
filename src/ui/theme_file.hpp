#pragma once

// User-importable theme files: a JSON document declaring a base built-in
// color scheme plus a full set of @role_name color overrides (see
// theme_palette.hpp and the "Color scheme" section of docs/ui-conventions.md).
//
// A role the file omits keeps the base scheme's value; an unknown role name
// is tolerated (skipped, reported as a warning) so a file written by a newer
// build still loads in an older one and the reverse. Colors are strict
// #RRGGBB or #RRGGBBAA; anything else is a hard load error naming the role.

#include "ui/theme_palette.hpp"

#include <QByteArray>
#include <QColor>
#include <QString>
#include <QStringList>

#include <memory>
#include <optional>

namespace patchy::ui {

struct ThemeAssets;  // theme_package.hpp

// The "format" key a theme file carries. A file without one is format 1. Bump
// only for a change an older build could not read correctly; adding roles never
// needs it (unknown roles are ignored, missing ones fall back to the base).
inline constexpr int kThemeFileFormat = 1;

enum class ThemeBackdropMode { Tile, Stretch, Center };
enum class ThemeIconScaling { Smooth, Nearest };

// The optional theme.json keys a theme package uses (docs/theme-files.md, "Theme
// packages"). A single .patchytheme may carry them too; its "backdrop" has no image
// to point at there and is ignored.
struct ThemePackageOptions {
  bool recolor_icons = true;                                   // "recolorIcons"
  ThemeIconScaling icon_scaling = ThemeIconScaling::Smooth;    // "iconScaling"
  QString backdrop_image;                                      // "backdrop"."image", package-relative
  ThemeBackdropMode backdrop_mode = ThemeBackdropMode::Tile;   // "backdrop"."mode"
};

// Optional theme.json keys describing the theme itself (docs/theme-files.md, "Format"),
// shown in Preferences. Each is empty when the file has none.
struct ThemeInfo {
  QString version;             // "version": the theme's own version, free text ("1.2")
  QString author;              // "author"
  QString min_patchy_version;  // "minPatchyVersion": dotted digits ("1.20"), validated
  QString description;         // "description": a few sentences, line breaks kept
};

// Longest "description" a theme keeps, in UTF-16 units; the rest is cut.
inline constexpr qsizetype kThemeDescriptionMaxLength = 2000;

struct CustomTheme {
  QString name;
  ColorScheme base = ColorScheme::Dark;
  ThemePalette palette;
  ThemePackageOptions options;
  ThemeInfo info;
  // A package's icons, backdrop image and sounds; null for a single-file theme.
  std::shared_ptr<const ThemeAssets> assets;
};

struct ThemeLoadResult {
  std::optional<CustomTheme> theme;
  QString error;         // non-empty means `theme` is unset; names the failure.
  QStringList warnings;  // non-fatal: unknown role names, etc.
};

// Strict "#RRGGBB" / "#RRGGBBAA" parse. Deliberately not QColor::fromString:
// that also accepts SVG color names and "#RGB", which the file format does not.
[[nodiscard]] std::optional<QColor> parse_theme_color(const QString& text);

// Parses a theme file. `theme.palette` starts as a copy of theme(base), so
// every role the file omits keeps that scheme's value.
[[nodiscard]] ThemeLoadResult load_theme_from_json(const QByteArray& json);

// Serializes every role in `palette` (Export writes the full, current set of
// values, never a diff against `base`). `options`, when given, adds the package keys
// that differ from their defaults; `info`, when given, its non-empty keys.
[[nodiscard]] QByteArray serialize_theme_to_json(const ThemePalette& palette, ColorScheme base,
                                                  const QString& name,
                                                  const ThemePackageOptions* options = nullptr,
                                                  const ThemeInfo* info = nullptr);

// True when the theme names a "minPatchyVersion" newer than this build.
[[nodiscard]] bool theme_needs_newer_patchy(const ThemeInfo& info);
// This build's version, as theme files compare against it ("1.19").
[[nodiscard]] QString running_patchy_version();

// Desktop persistence directory for imported theme files; empty on wasm (no
// AppData store there -- the theme buttons are desktop-only, see
// docs/ui-conventions.md). Honors the PATCHY_THEMES_DIR environment override
// for test isolation, like PATCHY_RECOVERY_DIR (document_recovery.hpp).
[[nodiscard]] QString user_themes_directory();

// A theme id, the value ThemeManager persists, is either a name within
// user_themes_directory() (a .patchytheme file, a .zip theme package, or a folder
// holding theme.json) or kBundledThemeIdPrefix followed by the name of a theme
// compiled into the binary (a .patchytheme in themes.qrc, or a package folder in the
// generated theme_packages.qrc; both from themes/bundled/). Bundled themes exist on
// every platform, wasm included.
inline constexpr QLatin1StringView kBundledThemeIdPrefix{"bundled:"};
[[nodiscard]] bool is_bundled_theme_id(const QString& id);
// The bundled theme names (files and package folders), in the order Preferences lists them.
[[nodiscard]] QStringList bundled_theme_file_names();
// The path the id reads from: a resource path for a bundled id, or, for a bundled
// package while PATCHY_THEME_SOURCE_DIR names the source themes/bundled folder, that
// package's source folder (edits show without a rebuild). Empty when a user id has no
// directory (wasm).
[[nodiscard]] QString theme_file_path_for_id(const QString& id);
// Reads and parses the file, zip or folder behind an id. A missing or unreadable
// one is an error like any other, so callers have one branch.
[[nodiscard]] ThemeLoadResult load_theme_by_id(const QString& id);
// load_theme_by_id for a list row: a package reads only its theme.json and backdrop
// (ThemeAssets::summary, theme_package.hpp); single-file themes load in full.
[[nodiscard]] ThemeLoadResult load_theme_summary_by_id(const QString& id);
// True when the id names a folder package on disk: one in user_themes_directory(), or a
// bundled package read from PATCHY_THEME_SOURCE_DIR. ThemeManager watches these.
[[nodiscard]] bool is_theme_package_directory_id(const QString& id);
// Every theme id in user_themes_directory(), sorted by name: *.patchytheme files,
// *.zip packages and folders that hold theme.json. Empty without a directory.
[[nodiscard]] QStringList user_theme_ids();

}  // namespace patchy::ui
