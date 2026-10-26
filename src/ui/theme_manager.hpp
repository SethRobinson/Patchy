#pragma once

// Owns the user's color-scheme preference and turns it into the active
// ThemePalette, live and without a restart.
//
// Three preferences resolve to two schemes. "Follow system" tracks the OS
// light/dark toggle through QStyleHints and re-resolves whenever it moves;
// "Dark" and "Light" pin the scheme and ignore the OS.
//
// The manager also mirrors the resolved scheme onto Qt itself via
// QStyleHints::setColorScheme, which is what makes native chrome Patchy does not
// style (dock and list scroll bars on Windows and Linux, tooltips, QMessageBox,
// the color picker, native title bars) agree with the app. That call is a no-op
// under the offscreen platform, so it cannot move any pinned test color.

#include "ui/theme_file.hpp"
#include "ui/theme_palette.hpp"

#include <QObject>
#include <QString>
#include <QStringList>
#include <Qt>

#include <optional>

class QFileSystemWatcher;
class QTimer;

namespace patchy::ui {

enum class ColorSchemePreference { FollowSystem, Dark, Light };

// Persisted identifiers: "system", "dark", "light". Never rename or re-spell.
[[nodiscard]] QString color_scheme_preference_to_token(ColorSchemePreference preference);
[[nodiscard]] ColorSchemePreference color_scheme_preference_from_token(const QString& token);

// What "follow system" resolves to for the scheme the platform reports. An
// explicit Light or Dark report always wins. Unknown (no desktop color scheme)
// resolves to Light on a desktop Linux session (`xcb` or `wayland*` platform),
// the light look a bare desktop or test X server expects (#334), and to Dark,
// Patchy's historical look, everywhere else, offscreen included.
// `desktop_linux_build` is true only in a Linux build that is not Android; the
// caller passes the compile-time answer so every platform can test the mapping.
[[nodiscard]] ColorScheme resolve_reported_color_scheme(Qt::ColorScheme reported,
                                                        bool desktop_linux_build,
                                                        const QString& platform_name);

class ThemeManager : public QObject {
  Q_OBJECT

 public:
  // Function-local static: the constructor connects to QGuiApplication's style
  // hints, so this must never be reached before the QApplication exists.
  [[nodiscard]] static ThemeManager& instance();

  [[nodiscard]] ColorSchemePreference preference() const { return preference_; }
  [[nodiscard]] ColorScheme resolved_scheme() const;

  // persist == false previews the scheme without touching settings, which is how
  // the Preferences combo shows a choice before the dialog is accepted.
  //
  // Also clears any active custom theme: a built-in preference and a custom
  // theme are mutually exclusive, and switching to a built-in one always wins.
  void set_preference(ColorSchemePreference preference, bool persist);

  // Applies a user-imported theme (see theme_file.hpp). `id` is the theme
  // file's name within user_themes_directory(), persisted so
  // load_saved_preference() can find it again; it does not touch the
  // preferences/colorScheme preference, which stays as the fallback if the
  // custom theme is ever cleared or its file goes missing.
  //
  // persist == false previews the theme without touching settings, matching
  // set_preference.
  void set_custom_theme(const QString& id, const CustomTheme& theme, bool persist);

  // Reverts to the built-in scheme selected by the current preference(). A
  // no-op when no custom theme is active.
  void clear_custom_theme(bool persist);

  // Empty when a built-in scheme is active.
  [[nodiscard]] std::optional<QString> active_custom_theme_id() const { return active_custom_id_; }

  // A folder theme package reloads by itself when one of its files changes (the
  // authoring loop): every change re-arms a short timer, which then reloads the
  // folder and re-applies it unpersisted. A folder that no longer loads keeps the
  // theme already showing. kPackageReloadDelayMs is that debounce.
  static constexpr int kPackageReloadDelayMs = 250;
  // The paths being watched; empty unless the active theme is a folder package.
  [[nodiscard]] QStringList watched_package_paths() const;

  // Reads the saved preference (and, if one was saved, the custom theme) and
  // applies them. Called once at startup, after QSettings::setPath so
  // PATCHY_SETTINGS_DIR isolation holds.
  void load_saved_preference();

  // Test seam. The offscreen platform reports Qt::ColorScheme::Unknown and never
  // emits colorSchemeChanged, so follow-system behavior is otherwise unreachable
  // in the UI suite. Pass std::nullopt to go back to the real platform value.
  void set_system_color_scheme_for_testing(std::optional<Qt::ColorScheme> scheme);

 signals:
  // Emitted only when the resolved scheme actually moves.
  void color_scheme_changed(ColorScheme scheme);

 private:
  ThemeManager();

  [[nodiscard]] ColorScheme system_scheme() const;
  void mirror_scheme_onto_qt();
  void apply_resolved_scheme();
  // QToolTip's application palette follows the active palette (tooltips have no
  // owner, so no stylesheet reaches them).
  void apply_tooltip_palette();

  void watch_active_package();
  void reload_active_package();

  ColorSchemePreference preference_ = ColorSchemePreference::FollowSystem;
  QFileSystemWatcher* package_watcher_ = nullptr;
  QTimer* package_reload_timer_ = nullptr;
  std::optional<Qt::ColorScheme> system_scheme_override_;
  std::optional<QString> active_custom_id_;
};

}  // namespace patchy::ui
