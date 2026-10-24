#include "ui/theme_manager.hpp"

#include "ui/app_settings.hpp"

#include <QGuiApplication>
#include <QPalette>
#include <QSettings>
#include <QStyleHints>
#include <QToolTip>

namespace patchy::ui {

namespace {

// Persisted identifier. Changing this orphans every existing user's choice.
const QString& color_scheme_key() {
  static const QString key = QStringLiteral("preferences/colorScheme");
  return key;
}

// Persisted identifier, additive alongside color_scheme_key(). Empty means no
// custom theme is active; a non-empty value is a file name within
// user_themes_directory().
const QString& custom_theme_id_key() {
  static const QString key = QStringLiteral("preferences/customThemeId");
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

ColorScheme resolve_reported_color_scheme(Qt::ColorScheme reported, bool desktop_linux_build,
                                          const QString& platform_name) {
  switch (reported) {
    case Qt::ColorScheme::Light:
      return ColorScheme::Light;
    case Qt::ColorScheme::Dark:
      return ColorScheme::Dark;
    case Qt::ColorScheme::Unknown:
      break;
  }
  const bool desktop_session = platform_name == QStringLiteral("xcb") ||
                               platform_name.startsWith(QStringLiteral("wayland"));
  return desktop_linux_build && desktop_session ? ColorScheme::Light : ColorScheme::Dark;
}

ThemeManager& ThemeManager::instance() {
  static ThemeManager manager;
  return manager;
}

ThemeManager::ThemeManager() {
  apply_tooltip_palette();
  connect(this, &ThemeManager::color_scheme_changed, this, [this] { apply_tooltip_palette(); });
  // On Windows this fires within a frame of the user flipping the OS toggle;
  // QWindowsTheme watches AppsUseLightTheme and re-reports on WM_SETTINGCHANGE,
  // so there is no reason to read the registry ourselves. Under offscreen it
  // never fires, which is what keeps the UI suite deterministic.
  connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
    // A custom theme pins its declared base scheme; an OS flip while one is
    // active must not silently swap it back to a built-in palette.
    if (preference_ == ColorSchemePreference::FollowSystem && !active_custom_id_) {
      apply_resolved_scheme();
    }
  });
}

ColorScheme ThemeManager::system_scheme() const {
  const auto reported =
      system_scheme_override_.value_or(QGuiApplication::styleHints()->colorScheme());
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
  constexpr bool desktop_linux_build = true;
#else
  constexpr bool desktop_linux_build = false;
#endif
  return resolve_reported_color_scheme(reported, desktop_linux_build,
                                       QGuiApplication::platformName());
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

// Tooltips are top-level QTipLabels with no owner, so the MainWindow's stylesheet
// never reaches them and Qt painted the platform's yellow or white tip in Dark.
// QToolTip's application palette is the one surface they read (October 2026).
void ThemeManager::apply_tooltip_palette() {
  auto palette = QToolTip::palette();
  palette.setColor(QPalette::ToolTipBase, theme().panel_bg);
  palette.setColor(QPalette::ToolTipText, theme().text_primary);
  QToolTip::setPalette(palette);
}

void ThemeManager::apply_resolved_scheme() {
  const auto scheme = resolved_scheme();
  if (scheme == active_color_scheme()) {
    apply_tooltip_palette();
    return;
  }
  set_active_color_scheme(scheme);
  emit color_scheme_changed(scheme);
}

void ThemeManager::set_preference(ColorSchemePreference preference, bool persist) {
  const bool had_custom_theme = active_custom_id_.has_value();
  preference_ = preference;
  if (persist) {
    auto settings = app_settings();
    settings.setValue(color_scheme_key(), color_scheme_preference_to_token(preference));
    // Always drop the stored custom theme id, not only when one is active now: the
    // Preferences combo previews a built-in scheme without persisting, which already
    // cleared the active custom theme, so the commit that follows used to leave the
    // old id behind and the next launch re-applied it (GitHub issue 133).
    settings.setValue(custom_theme_id_key(), QString());
  }
  if (had_custom_theme) {
    // clear_custom_theme() mirrors and force-applies against the preference_
    // just set above, which is exactly the built-in scheme being switched to.
    clear_custom_theme(persist);
    return;
  }
  // Mirroring first can already re-resolve us through colorSchemeChanged; the
  // apply below is then a no-op rather than a second restyle.
  mirror_scheme_onto_qt();
  apply_resolved_scheme();
}

void ThemeManager::set_custom_theme(const QString& id, const CustomTheme& theme, bool persist) {
  // Never routes through apply_resolved_scheme(): a custom theme can share its
  // base scheme's label with the scheme already active while carrying an
  // entirely different palette, so that function's equal-scheme guard must
  // not suppress this apply.
  active_custom_id_ = id;
  set_active_custom_palette(theme.palette, theme.base);
  if (!system_scheme_override_.has_value()) {
    QGuiApplication::styleHints()->setColorScheme(theme.base == ColorScheme::Light ? Qt::ColorScheme::Light
                                                                                    : Qt::ColorScheme::Dark);
  }
  if (persist) {
    auto settings = app_settings();
    settings.setValue(custom_theme_id_key(), id);
  }
  emit color_scheme_changed(theme.base);
}

void ThemeManager::clear_custom_theme(bool persist) {
  if (!active_custom_id_) {
    return;
  }
  active_custom_id_.reset();
  clear_active_custom_palette();
  if (persist) {
    auto settings = app_settings();
    settings.setValue(custom_theme_id_key(), QString());
  }
  mirror_scheme_onto_qt();
  // Same guard, in reverse: the built-in scheme being reverted to can share
  // the label the custom theme was pinning, so force the apply and emit
  // instead of going through apply_resolved_scheme().
  const auto scheme = resolved_scheme();
  set_active_color_scheme(scheme);
  emit color_scheme_changed(scheme);
}

void ThemeManager::load_saved_preference() {
  auto settings = app_settings();
  const auto token =
      settings
          .value(color_scheme_key(),
                 color_scheme_preference_to_token(ColorSchemePreference::FollowSystem))
          .toString();
  set_preference(color_scheme_preference_from_token(token), /*persist=*/false);

  const auto custom_id = settings.value(custom_theme_id_key(), QString()).toString();
  if (custom_id.isEmpty()) {
    return;
  }
  // A missing or invalid file (moved or deleted by hand outside Patchy, or a
  // bundled theme this build no longer ships) falls back to the built-in
  // preference already applied above rather than failing startup.
  auto result = load_theme_by_id(custom_id);
  if (!result.theme) {
    return;
  }
  set_custom_theme(custom_id, *result.theme, /*persist=*/false);
}

void ThemeManager::set_system_color_scheme_for_testing(std::optional<Qt::ColorScheme> scheme) {
  system_scheme_override_ = scheme;
  apply_resolved_scheme();
}

}  // namespace patchy::ui
