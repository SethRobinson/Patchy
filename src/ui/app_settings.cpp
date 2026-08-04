#include "ui/app_settings.hpp"

#include <QString>

#include <algorithm>

namespace patchy::ui {
namespace {

// Persisted identifier: never rename it (see AGENTS.md).
QString gui_scale_key() { return QStringLiteral("preferences/guiScalePercent"); }

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

int stored_gui_scale_percent() {
  return normalize_gui_scale_percent(
      app_settings().value(gui_scale_key(), kDefaultGuiScalePercent).toInt());
}

void set_stored_gui_scale_percent(int percent) {
  auto settings = app_settings();
  settings.setValue(gui_scale_key(), normalize_gui_scale_percent(percent));
}

}  // namespace patchy::ui
