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

#include "ui/theme_palette.hpp"

#include <QObject>
#include <QString>
#include <Qt>

#include <optional>

namespace patchy::ui {

enum class ColorSchemePreference { FollowSystem, Dark, Light };

// Persisted identifiers: "system", "dark", "light". Never rename or re-spell.
[[nodiscard]] QString color_scheme_preference_to_token(ColorSchemePreference preference);
[[nodiscard]] ColorSchemePreference color_scheme_preference_from_token(const QString& token);

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
  void set_preference(ColorSchemePreference preference, bool persist);

  // Reads the saved preference and applies it. Called once at startup, after
  // QSettings::setPath so PATCHY_SETTINGS_DIR isolation holds.
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

  ColorSchemePreference preference_ = ColorSchemePreference::FollowSystem;
  std::optional<Qt::ColorScheme> system_scheme_override_;
};

}  // namespace patchy::ui
