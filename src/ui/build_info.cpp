#include "ui/build_info.hpp"

#include <QDate>
#include <QLocale>

namespace patchy::ui {

QString build_date_text() {
  // __DATE__ is the compile date of this translation unit. A version bump
  // changes the target-wide PATCHY_VERSION compile definition and recompiles
  // every patchy_ui source, so release builds always carry a fresh date.
  // __DATE__ pads single-digit days with a space ("Jul  3 2026"); simplified()
  // collapses that so the C-locale parse below accepts both forms.
  const auto raw = QString::fromLatin1(__DATE__).simplified();
  const auto date = QLocale::c().toDate(raw, QStringLiteral("MMM d yyyy"));
  return date.isValid() ? date.toString(Qt::ISODate) : raw;
}

}  // namespace patchy::ui
