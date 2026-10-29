#include "ui/blocked_window_cursor.hpp"

#include "ui/wayland_pointer_cursor.hpp"

#include <QApplication>
#include <QCursor>
#include <QEvent>
#include <QGuiApplication>
#include <QLibraryInfo>
#include <QPointer>
#include <QWidget>
#include <QWindow>

#include <algorithm>
#include <vector>

namespace patchy::ui {
namespace {

class BlockedWindowCursorFilter : public QObject {
 public:
  explicit BlockedWindowCursorFilter(QObject* parent) : QObject(parent) {}

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    const auto type = event->type();
    if (type != QEvent::WindowBlocked && type != QEvent::WindowUnblocked && type != QEvent::CursorChange &&
        type != QEvent::EnabledChange) {
      return false;
    }
    auto* widget = qobject_cast<QWidget*>(watched);
    if (type == QEvent::WindowBlocked || type == QEvent::WindowUnblocked) {
      // Qt sends both to each QWindow and on to its widget, which forwards them to its children
      // (no window of their own unless native); block and unblock are idempotent.
      QWindow* handle = widget != nullptr ? widget->windowHandle() : qobject_cast<QWindow*>(watched);
      if (handle != nullptr && type == QEvent::WindowBlocked) {
        block(*handle);
      } else if (handle != nullptr) {
        unblock(*handle);
      }
    } else if (widget != nullptr && !blocked_.empty()) {
      // The widget already applied its cursor (or the arrow, when disabled) to the window.
      if (QWindow* handle = cursor_native_window(*widget); handle != nullptr && is_blocked(*handle)) {
        handle->setCursor(Qt::ArrowCursor);
      }
    }
    return false;
  }

 private:
  struct Blocked {
    QPointer<QWindow> window;
    QCursor saved;
  };

  bool is_blocked(const QWindow& window) const {
    for (const auto& entry : blocked_) {
      if (entry.window == &window) {
        return true;
      }
    }
    return false;
  }

  void block(QWindow& window) {
    std::erase_if(blocked_, [](const Blocked& entry) { return entry.window == nullptr; });
    if (is_blocked(window)) {
      return;
    }
    blocked_.push_back(Blocked{&window, window.cursor()});
    window.setCursor(Qt::ArrowCursor);
  }

  void unblock(QWindow& window) {
    const auto it = std::find_if(blocked_.begin(), blocked_.end(),
                                 [&window](const Blocked& entry) { return entry.window == &window; });
    if (it == blocked_.end()) {
      return;
    }
    const QCursor saved = it->saved;
    blocked_.erase(it);
    // Enter events were blocked too, so the cursor Qt last applied may belong to a widget the
    // pointer has since left: take the one under the pointer now when it is in this window.
    if (const QWidget* pointer = QApplication::widgetAt(QCursor::pos());
        pointer != nullptr && cursor_native_window(*pointer) == &window) {
      window.setCursor(effective_widget_cursor(*pointer));
    } else {
      window.setCursor(saved);
    }
  }

  std::vector<Blocked> blocked_;
};

}  // namespace

QWindow* cursor_native_window(const QWidget& widget) {
  const QWidget* native = widget.internalWinId() != 0 ? &widget : widget.nativeParentWidget();
  return native != nullptr ? native->windowHandle() : nullptr;
}

QCursor effective_widget_cursor(const QWidget& widget) {
  const QWidget* source = &widget;
  while (!source->isWindow() && !source->testAttribute(Qt::WA_SetCursor) && source->parentWidget() != nullptr) {
    source = source->parentWidget();
  }
  return source->isEnabled() ? source->cursor() : QCursor(Qt::ArrowCursor);
}

QObject* create_blocked_window_cursor_filter(QObject* parent) {
  return new BlockedWindowCursorFilter(parent);
}

void install_blocked_window_cursor_fix() {
  auto* app = qApp;
  if (app == nullptr || wayland_pointer_cursor_fix_needed(QGuiApplication::platformName(), QLibraryInfo::version())) {
    return;
  }
  app->installEventFilter(create_blocked_window_cursor_filter(app));
}

}  // namespace patchy::ui
