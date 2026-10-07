#include "ui/canvas_alt_space_filter.hpp"

#include "ui/canvas_widget.hpp"

#include <QApplication>
#include <QKeyEvent>
#include <QPointer>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace patchy::ui {

#ifdef Q_OS_WIN

namespace {

class AltSpaceDragFilter final : public QAbstractNativeEventFilter {
 public:
  bool nativeEventFilter(const QByteArray& event_type, void* message, qintptr* result) override {
    Q_UNUSED(event_type);
    const auto* native_message = static_cast<const MSG*>(message);
    if (native_message == nullptr || native_message->wParam != VK_SPACE) {
      return false;
    }
    switch (native_message->message) {
      case WM_SYSKEYDOWN: {
        // Alt is down (that is what makes it a SYS key message). Start
        // forwarding only for a gesture in flight on the focused canvas; once
        // started, auto-repeats keep forwarding so the press/release pair
        // stays balanced even if the gesture ends under the held key.
        if (!forwarding_) {
          auto* canvas = qobject_cast<CanvasWidget*>(QApplication::focusWidget());
          if (canvas == nullptr || !canvas->pointer_gesture_active()) {
            return false;
          }
          target_ = canvas;
          forwarding_ = true;
        }
        const bool auto_repeat = (native_message->lParam & (1u << 30)) != 0;
        send_space(QEvent::KeyPress, Qt::AltModifier, auto_repeat);
        return consume(result);
      }
      case WM_SYSCHAR:
        // TranslateMessage derives this from the key-down above; DefWindowProc
        // would turn it into WM_SYSCOMMAND / SC_KEYMENU (the system menu).
        if (!forwarding_) {
          return false;
        }
        return consume(result);
      case WM_SYSKEYUP:
      case WM_KEYUP: {
        // The key-up arrives as WM_KEYUP when Alt went up first; either way it
        // ends the forwarded press.
        if (!forwarding_) {
          return false;
        }
        forwarding_ = false;
        send_space(QEvent::KeyRelease,
                   native_message->message == WM_SYSKEYUP ? Qt::AltModifier : Qt::NoModifier, false);
        return consume(result);
      }
      default:
        return false;
    }
  }

 private:
  static bool consume(qintptr* result) {
    if (result != nullptr) {
      *result = 0;
    }
    return true;
  }

  void send_space(QEvent::Type type, Qt::KeyboardModifiers modifiers, bool auto_repeat) {
    if (target_.isNull()) {
      return;
    }
    if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) {
      modifiers |= Qt::ShiftModifier;
    }
    if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) {
      modifiers |= Qt::ControlModifier;
    }
    QKeyEvent event(type, Qt::Key_Space, modifiers, QString(), auto_repeat);
    QApplication::sendEvent(target_.data(), &event);
  }

  QPointer<CanvasWidget> target_;
  bool forwarding_{false};
};

AltSpaceDragFilter& shared_filter() {
  static AltSpaceDragFilter filter;
  return filter;
}

}  // namespace

void install_alt_space_drag_filter() {
  // Re-install when a new QCoreApplication replaces the one the filter was
  // registered with (the suites construct one per process, but keep this
  // honest for anything that does not).
  static QPointer<QCoreApplication> installed_for;
  auto* app = QCoreApplication::instance();
  if (app == nullptr || installed_for == app) {
    return;
  }
  app->installNativeEventFilter(&shared_filter());
  installed_for = app;
}

QAbstractNativeEventFilter* alt_space_drag_native_filter() {
  return &shared_filter();
}

#else

void install_alt_space_drag_filter() {}

QAbstractNativeEventFilter* alt_space_drag_native_filter() {
  return nullptr;
}

#endif

}  // namespace patchy::ui
