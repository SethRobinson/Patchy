#include "ui/modifier_names.hpp"

#include <QCoreApplication>
#include <QLatin1StringView>

namespace patchy::ui {

namespace {

// Translatable so a locale can spell the macOS keys its own way; Apple Japan writes
// "command" and "option" in Latin script beside the katakana, so the default stands.
constexpr const char* kModifierContext = "patchy::ui::ModifierNames";

}  // namespace

QString ctrl_key_name() {
#ifdef Q_OS_MACOS
  return QCoreApplication::translate(kModifierContext, "Command");
#else
  return QCoreApplication::translate(kModifierContext, "Ctrl");
#endif
}

QString alt_key_name() {
#ifdef Q_OS_MACOS
  return QCoreApplication::translate(kModifierContext, "Option");
#else
  return QCoreApplication::translate(kModifierContext, "Alt");
#endif
}

QString resolve_modifier_names(QString text) {
  // replace() is a no-op when the token is absent, so both passes always run rather than
  // paying for a contains() probe first.
  text.replace(QLatin1StringView(kCtrlModifierToken), ctrl_key_name());
  text.replace(QLatin1StringView(kAltModifierToken), alt_key_name());
  return text;
}

}  // namespace patchy::ui
