#pragma once

// Theme packages (docs/theme-files.md, "Theme packages"): a folder in
// user_themes_directory() holding theme.json plus icons/, images/ and sounds/, or
// that folder zipped. Everything a package holds is read into memory once, at
// load, under ThemePackageLimits, so a folder and a zip behave the same and
// nothing reads the disk at paint time. A package holds JSON, SVG, PNG, BMP, JPEG,
// WAV and theme JavaScript; the scripts run only in the locked-down theme engine
// (theme_script_host.hpp), once the user allows them.

#include "ui/theme_file.hpp"

#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QVariantMap>
#include <QLatin1StringView>
#include <QString>
#include <QStringList>

#include <memory>

namespace patchy::ui {

// The manifest every package holds at its root (or, in a zip, inside one top folder).
inline constexpr QLatin1StringView kThemePackageManifest{"theme.json"};

struct ThemeIcon {
  QByteArray svg;   // an SVG override; empty for a bitmap one
  QImage image;     // a PNG or BMP override at 1x (null for SVG)
  QImage image_2x;  // <stem>@2x.png, drawn when the target has the pixels for it
};

// One theme.json "events" entry (docs/theme-files.md, "Events and effects").
struct ThemeEventAction {
  QByteArray sound;    // RIFF/WAVE bytes; empty for none
  QVariantMap effect;  // {"name": "fireworks" | "sparkles" | "confetti" | "sprite", options...}; empty for none
};

struct ThemeAssets {
  ThemePackageOptions options;
  ThemeInfo info;  // carried into Export Theme Package
  QHash<QString, ThemeIcon> icons;    // keyed on the built-in icon stem
  QImage backdrop;                    // options.backdrop_image decoded; null when none
  QHash<QString, QByteArray> sounds;  // keyed on the sound event id (theme_sounds.hpp), RIFF/WAVE bytes
  // Every file the package holds except theme.json, by package-relative path, so
  // Export Theme Package can write the package back out.
  QHash<QString, QByteArray> files;
  QHash<QString, ThemeEventAction> events;  // theme.json "events", by event id
  // The parsed theme.json, for the keys read where they are used (menuTitle, cursors,
  // splash, panelBackgrounds).
  QJsonObject manifest;
  // Read by a *_summary loader: options, info, manifest and backdrop only, no icons,
  // sounds, events or files. Never applied; load the package in full first.
  bool summary = false;

  // True when the package holds scripts/main.js (docs/theme-files.md, "Theme scripts").
  [[nodiscard]] bool has_scripts() const;
};

// Decodes a PNG, JPEG or BMP the package holds at `path`; null when missing, unsafe,
// another format, or past `max_side` pixels a side.
[[nodiscard]] QImage theme_package_image(const ThemeAssets& assets, const QString& path, int max_side = 4096);

struct ThemePackageLimits {
  qint64 max_package_bytes = 64LL * 1024 * 1024;  // a zip file as stored
  qint64 max_entry_bytes = 16LL * 1024 * 1024;    // one file inside
  qint64 max_total_bytes = 64LL * 1024 * 1024;    // everything read from one package
  int max_entries = 2048;                         // files read from one package
  int max_icon_side = 1024;                       // pixels, a bitmap icon
  int max_backdrop_side = 8192;                   // pixels, the backdrop image
};

// ".zip", any case.
[[nodiscard]] bool is_theme_package_zip_name(const QString& file_name);
// A package-relative path that stays inside the package: no "..", no absolute or
// drive path, no backslash, not empty.
[[nodiscard]] bool is_safe_theme_package_path(const QString& path);
// True when a theme's SVG icon reaches nothing outside itself. QtSvg loads an
// <image> href as a file path (a network path on Windows sends the user's login
// hash to that server), so a theme SVG may not hold: an href other than "#id", an
// <image> other than embedded PNG or JPEG data, a url() other than "url(#id)", a
// CSS @import, a DTD (entities) or a processing instruction. Malformed XML is unsafe.
[[nodiscard]] bool is_safe_theme_svg(const QByteArray& svg);

// Loaders. A path that escapes the package, a package past a limit, or a missing
// or invalid theme.json is an error; an unreadable icon, sound or backdrop is a
// warning and the theme loads without it.
[[nodiscard]] ThemeLoadResult load_theme_package_directory(const QString& directory,
                                                           const ThemePackageLimits& limits = {});
[[nodiscard]] ThemeLoadResult load_theme_package_zip(const QByteArray& zip, const ThemePackageLimits& limits = {});
[[nodiscard]] ThemeLoadResult load_theme_package_zip_file(const QString& path, const ThemePackageLimits& limits = {});
// What a theme list row shows, read cheaply: theme.json and the decoded backdrop, with
// ThemeAssets::summary set. Nothing else is read or checked (a zip's entry names
// still are), so a package that summarizes can still fail its full load.
[[nodiscard]] ThemeLoadResult load_theme_package_directory_summary(const QString& directory,
                                                                   const ThemePackageLimits& limits = {});
[[nodiscard]] ThemeLoadResult load_theme_package_zip_summary(const QByteArray& zip,
                                                             const ThemePackageLimits& limits = {});

// A zip holding theme.json (`palette` under `name`, with `assets`' options) at its
// root and every file in `assets`, in path order. `assets` may be null.
[[nodiscard]] QByteArray serialize_theme_package_zip(const ThemePalette& palette, ColorScheme base,
                                                     const QString& name, const ThemeAssets* assets);

// The built-in icon stems a package may replace: every SVG in the icon resources
// except the ones stylesheets load by url() (icon_theme.hpp), sorted.
[[nodiscard]] QStringList overridable_icon_stems();
// Export Icon Template: writes every overridable built-in SVG, as authored, into
// `directory` (created when missing). Returns the number written; on a failure
// returns -1 and fills `error`.
[[nodiscard]] int export_icon_template(const QString& directory, QString* error);

// The active theme's package, installed by ThemeManager together with the palette
// (so theme_generation() moves with it); null for a built-in scheme or a
// single-file theme.
void set_active_theme_assets(std::shared_ptr<const ThemeAssets> assets);
[[nodiscard]] const ThemeAssets* active_theme_assets();
[[nodiscard]] std::shared_ptr<const ThemeAssets> active_theme_assets_shared();

}  // namespace patchy::ui
