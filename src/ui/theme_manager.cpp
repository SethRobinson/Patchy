#include "ui/theme_manager.hpp"

#include "ui/app_settings.hpp"

#include <QGuiApplication>
#include <QSettings>
#include <QStyleHints>

namespace patchy::ui {

namespace {

// Persisted identifier. Changing this orphans every existing user's choice.
const QString& color_scheme_key() {
  static const QString key = QStringLiteral("preferences/colorScheme");
  return key;
}

}  // namespace

QString color_scheme_preference_to_token(ColorSchemePreference preference) {
  switch (preference) {
    case ColorSchemePreference::FollowSystem:
      return QStringLiteral("system");
    case ColorSchemePreference::Dark:
      return QStringLiteral("dark");
    case ColorSchemePreference::Light:
      return QStringLiteral("light");
  }
  return QStringLiteral("system");
}

ColorSchemePreference color_scheme_preference_from_token(const QString& token) {
  if (token == QStringLiteral("dark")) {
    return ColorSchemePreference::Dark;
  }
  if (token == QStringLiteral("light")) {
    return ColorSchemePreference::Light;
  }
  return ColorSchemePreference::FollowSystem;
}

ThemeManager& ThemeManager::instance() {
  static ThemeManager manager;
  return manager;
}

ThemeManager::ThemeManager() {
  // On Windows this fires within a frame of the user flipping the OS toggle;
  // QWindowsTheme watches AppsUseLightTheme and re-reports on WM_SETTINGCHANGE,
  // so there is no reason to read the registry ourselves. Under offscreen it
  // never fires, which is what keeps the UI suite deterministic.
  connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
    if (preference_ == ColorSchemePreference::FollowSystem) {
      apply_resolved_scheme();
    }
  });
}

ColorScheme ThemeManager::system_scheme() const {
  const auto reported =
      system_scheme_override_.value_or(QGuiApplication::styleHints()->colorScheme());
  // Unknown is what the offscreen platform reports, and what any platform
  // without a color-scheme notion reports. Dark is Patchy's historical look, so
  // that is the safe resolution.
  return reported == Qt::ColorScheme::Light ? ColorScheme::Light : ColorScheme::Dark;
}

ColorScheme ThemeManager::resolved_scheme() const {
  switch (preference_) {
    case ColorSchemePreference::Dark:
      return ColorScheme::Dark;
    case ColorSchemePreference::Light:
      return ColorScheme::Light;
    case ColorSchemePreference::FollowSystem:
      break;
  }
  return system_scheme();
}

void ThemeManager::mirror_scheme_onto_qt() {
  // A test override must not reach the real platform: the UI suite would then
  // depend on offscreen honoring setColorScheme, which it does not.
  if (system_scheme_override_.has_value()) {
    return;
  }
  auto* hints = QGuiApplication::styleHints();
  switch (preference_) {
    case ColorSchemePreference::FollowSystem:
      hints->unsetColorScheme();
      return;
    case ColorSchemePreference::Dark:
      hints->setColorScheme(Qt::ColorScheme::Dark);
      return;
    case ColorSchemePreference::Light:
      hints->setColorScheme(Qt::ColorScheme::Light);
      return;
  }
}

void ThemeManager::apply_resolved_scheme() {
  const auto scheme = resolved_scheme();
  if (scheme == active_color_scheme()) {
    return;
  }
  set_active_color_scheme(scheme);
  emit color_scheme_changed(scheme);
}

void ThemeManager::set_preference(ColorSchemePreference preference, bool persist) {
  preference_ = preference;
  if (persist) {
    auto settings = app_settings();
    settings.setValue(color_scheme_key(), color_scheme_preference_to_token(preference));
  }
  // Mirroring first can already re-resolve us through colorSchemeChanged; the
  // apply below is then a no-op rather than a second restyle.
  mirror_scheme_onto_qt();
  apply_resolved_scheme();
}

void ThemeManager::load_saved_preference() {
  auto settings = app_settings();
  const auto token =
      settings
          .value(color_scheme_key(),
                 color_scheme_preference_to_token(ColorSchemePreference::FollowSystem))
          .toString();
  set_preference(color_scheme_preference_from_token(token), /*persist=*/false);
}

void ThemeManager::set_system_color_scheme_for_testing(std::optional<Qt::ColorScheme> scheme) {
  system_scheme_override_ = scheme;
  apply_resolved_scheme();
}

}  // namespace patchy::ui
