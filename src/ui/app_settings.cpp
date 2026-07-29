#include "ui/app_settings.hpp"

#include <QString>

namespace patchy::ui {

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

}  // namespace patchy::ui
