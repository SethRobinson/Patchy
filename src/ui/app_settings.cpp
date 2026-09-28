#include "ui/app_settings.hpp"

#include <QString>

#include <algorithm>

namespace patchy::ui {
namespace {

// Persisted identifiers: never rename them (see AGENTS.md).
QString gui_scale_key() { return QStringLiteral("preferences/guiScalePercent"); }
#ifndef Q_OS_WASM
QString recovery_enabled_key() { return QStringLiteral("recovery/enabled"); }
#endif
QString recovery_interval_key() { return QStringLiteral("recovery/intervalMinutes"); }

}  // namespace

QSettings app_settings() {
#ifdef Q_OS_WASM
  // IniFormat would resolve to a real .ini in MEMFS, which is recreated empty on
  // every page load. The localStorage backend is synchronous (empty flush()) and
  // survives reloads, which is what makes preferences persist in the browser.
  return QSettings(QSettings::WebLocalStorageFormat, QSettings::UserScope, QStringLiteral("Patchy"),
                   QStringLiteral("Patchy"));
#else
  return QSettings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("Patchy"),
                   QStringLiteral("Patchy"));
#endif
}

int normalize_gui_scale_percent(int stored) {
  const auto match = std::find(kGuiScalePercents.begin(), kGuiScalePercents.end(), stored);
  return match != kGuiScalePercents.end() ? *match : kDefaultGuiScalePercent;
}

QSettings brush_library_settings() {
  const auto file = qEnvironmentVariable("PATCHY_BRUSH_SETTINGS_FILE");
  if (!file.isEmpty()) return QSettings(file, QSettings::IniFormat);
  return app_settings();
}

QSettings recent_history_settings() {
  const auto file = qEnvironmentVariable("PATCHY_RECENT_SETTINGS_FILE");
  if (!file.isEmpty()) return QSettings(file, QSettings::IniFormat);
  return app_settings();
}

int stored_gui_scale_percent() {
  return normalize_gui_scale_percent(
      app_settings().value(gui_scale_key(), kDefaultGuiScalePercent).toInt());
}

void set_stored_gui_scale_percent(int percent) {
  auto settings = app_settings();
  settings.setValue(gui_scale_key(), normalize_gui_scale_percent(percent));
}

int normalize_recovery_interval_minutes(int stored) {
  const auto match = std::find(kRecoveryIntervalMinutes.begin(), kRecoveryIntervalMinutes.end(), stored);
  return match != kRecoveryIntervalMinutes.end() ? *match : kDefaultRecoveryIntervalMinutes;
}

bool stored_recovery_enabled() {
#ifdef Q_OS_WASM
  return false;
#else
  return app_settings().value(recovery_enabled_key(), true).toBool();
#endif
}

void set_stored_recovery_enabled(bool enabled) {
#ifdef Q_OS_WASM
  Q_UNUSED(enabled);
#else
  auto settings = app_settings();
  settings.setValue(recovery_enabled_key(), enabled);
#endif
}

int stored_recovery_interval_minutes() {
  return normalize_recovery_interval_minutes(
      app_settings().value(recovery_interval_key(), kDefaultRecoveryIntervalMinutes).toInt());
}

void set_stored_recovery_interval_minutes(int minutes) {
#ifdef Q_OS_WASM
  Q_UNUSED(minutes);
#else
  auto settings = app_settings();
  settings.setValue(recovery_interval_key(), normalize_recovery_interval_minutes(minutes));
#endif
}

}  // namespace patchy::ui
