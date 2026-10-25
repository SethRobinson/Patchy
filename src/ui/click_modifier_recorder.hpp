#pragma once

#include <QEvent>
#include <QMouseEvent>
#include <QObject>
#include <QVariant>

namespace patchy::ui {

inline constexpr const char* kLastClickModifiersProperty = "patchyLastClickModifiers";

// A button's clicked signal carries no modifiers, and QApplication::keyboardModifiers()
// can lag the event stream, so record the folded modifiers off the button's own mouse
// events for the clicked handler to read (docs/ui-conventions.md).
class ClickModifierRecorder final : public QObject {
public:
  using QObject::QObject;

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease) {
      watched->setProperty(kLastClickModifiersProperty, static_cast<QMouseEvent*>(event)->modifiers().toInt());
    }
    return QObject::eventFilter(watched, event);
  }
};

// Installs a ClickModifierRecorder owned by `button`.
inline void record_click_modifiers(QObject* button) {
  button->installEventFilter(new ClickModifierRecorder(button));
}

// The modifiers of the click that fired `button` (none for a keyboard or programmatic
// click), cleared so the next click starts fresh.
[[nodiscard]] inline Qt::KeyboardModifiers take_click_modifiers(QObject* button) {
  const auto modifiers = static_cast<Qt::KeyboardModifiers>(button->property(kLastClickModifiersProperty).toInt());
  button->setProperty(kLastClickModifiersProperty, {});
  return modifiers;
}

}  // namespace patchy::ui
