#include "ui/app_settings.hpp"

#include <QString>

namespace patchy::ui {

QSettings app_settings() {
  return QSettings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("Patchy"),
                   QStringLiteral("Patchy"));
}

}  // namespace patchy::ui
