#include "ui/build_info.hpp"

#include <QDate>
#include <QLocale>
#include <QTime>

namespace patchy::ui {

QString build_timestamp_text() {
  // __DATE__/__TIME__ are the build machine's local wall clock at the moment
  // this translation unit compiled. A version bump changes the target-wide
  // PATCHY_VERSION compile definition and recompiles every patchy_ui source,
  // so release builds always carry a fresh stamp. All release builds happen
  // on machines that run JST, so the label is hardcoded rather than guessed
  // at runtime: a user's machine cannot recover the build zone from a naive
  // timestamp.
  // __DATE__ pads single-digit days with a space ("Jul  3 2026"); simplified()
  // collapses that so the C-locale parse below accepts both forms.
  const auto raw_date = QString::fromLatin1(__DATE__).simplified();
  const auto date = QLocale::c().toDate(raw_date, QStringLiteral("MMM d yyyy"));
  const auto time = QTime::fromString(QStringLiteral(__TIME__), QStringLiteral("HH:mm:ss"));
  if (!date.isValid() || !time.isValid()) {
    return raw_date + QLatin1Char(' ') + QStringLiteral(__TIME__);
  }
  return QStringLiteral("%1 %2 JST").arg(date.toString(Qt::ISODate), time.toString(QStringLiteral("HH:mm")));
}

}  // namespace patchy::ui
