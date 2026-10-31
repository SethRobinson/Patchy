#include "ui/wayland_pointer_cursor.hpp"

#include "ui/blocked_window_cursor.hpp"

#include <QApplication>
#include <QApplicationStateChangeEvent>
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

class WaylandPointerCursorFilter : public QObject {
 public:
  explicit WaylandPointerCursorFilter(QObject* parent) : QObject(parent) {}

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    // QGuiApplication::restoreOverrideCursor re-applies every window's cursor in turn and
    // announces none of it; the plug-in shows the last one (often the main window's tool
    // cursor) wherever the pointer is. Its first event after the stack empties puts the
    // pointer's cursor back.
    const bool has_override = QGuiApplication::overrideCursor() != nullptr;
    if (had_override_ && !has_override) {
      restore_pointer_cursor();
    }
    had_override_ = has_override;

    const auto type = event->type();
    if (type == QEvent::WindowBlocked || type == QEvent::WindowUnblocked) {
      if (auto* window = qobject_cast<QWindow*>(watched); window != nullptr) {
        set_blocked(*window, type == QEvent::WindowBlocked);
      }
      return false;
    }
    if (type == QEvent::ApplicationStateChange) {
      // Back from another application (Alt+Tab): the compositor shows the last cursor Patchy
      // sent wherever the pointer comes back to, so send the right one now.
      if (static_cast<QApplicationStateChangeEvent*>(event)->applicationState() == Qt::ApplicationActive) {
        restore_pointer_cursor();
      }
      return false;
    }
    if (type != QEvent::Enter && type != QEvent::Leave && type != QEvent::CursorChange) {
      return false;
    }
    auto* widget = qobject_cast<QWidget*>(watched);
    if (type == QEvent::CursorChange) {
      // A widget announces its change, and its window then sends one of its own; Qt's enter
      // and leave dispatch and its synthetic enter changes a window's cursor with no widget
      // event, so both count.
      if (widget != nullptr) {
        // An uncreated widget applied nothing.
        if (widget->testAttribute(Qt::WA_WState_Created)) {
          reapply_pointer_cursor(cursor_native_window(*widget));
        }
      } else if (auto* window = qobject_cast<QWindow*>(watched); window != nullptr && window->handle() != nullptr) {
        reapply_pointer_cursor(window);
      }
      return false;
    }
    if (widget == nullptr) {
      return false;  // QWindows get Enter and Leave too
    }
    if (type == QEvent::Enter) {
      // Qt enters the window first, then each widget down to the one under the pointer. Qt
      // drops the compositor's enter on a window a modal blocks, so an Enter there is Qt's
      // synthetic one for a widget shown under where the pointer last was, not the pointer.
      if (!in_blocked_window(*widget)) {
        pointer_widget_ = widget;
      }
    } else if (pointer_widget_ != nullptr && widget->isWindow() &&
               (widget == pointer_widget_ || widget->isAncestorOf(pointer_widget_))) {
      // Leaving the window leaves every widget in it, whatever Leave order Qt used inside.
      pointer_widget_ = nullptr;
      // Off a dialog with a modal up, the pointer is on a blocked window or outside Patchy;
      // a blocked window gets no enter from Qt, so it would keep this window's cursor.
      if (has_blocked_window()) {
        restore_pointer_cursor();
      }
    } else if (widget == pointer_widget_) {
      pointer_widget_ = widget->parentWidget();
    }
    return false;
  }

 private:
  // After `changed` applied a cursor, the pointer's window shows its own again.
  void reapply_pointer_cursor(const QWindow* changed) {
    QWidget* pointer = pointer_widget_.data();
    // The override cursor shows everywhere by design.
    if (reapplying_ || pointer == nullptr || !pointer->isVisible() || QGuiApplication::overrideCursor() != nullptr) {
      return;
    }
    QWindow* pointer_window = cursor_native_window(*pointer);
    if (pointer_window == nullptr || pointer_window == changed || in_blocked_window(*pointer)) {
      return;
    }
    apply(*pointer_window, effective_widget_cursor(*pointer));
  }

  // Sends the cursor the pointer should show, whichever window changed last: the cursor of
  // the widget under it, or the arrow when it is on a window a modal blocks (where Qt tracks
  // no widget) or nowhere Qt knows of while a modal is up.
  void restore_pointer_cursor() {
    if (reapplying_ || QGuiApplication::overrideCursor() != nullptr) {
      return;
    }
    QWidget* pointer = pointer_widget_.data();
    if (pointer != nullptr && pointer->isVisible() && !in_blocked_window(*pointer)) {
      if (QWindow* pointer_window = cursor_native_window(*pointer); pointer_window != nullptr) {
        apply(*pointer_window, effective_widget_cursor(*pointer));
      }
      return;
    }
    for (const auto& blocked : blocked_) {
      if (blocked != nullptr && blocked->handle() != nullptr && blocked->isVisible()) {
        apply(*blocked, QCursor(Qt::ArrowCursor));
        return;
      }
    }
  }

  void apply(QWindow& window, const QCursor& cursor) {
    reapplying_ = true;
    // QWindow::setCursor skips a shape the window already holds; unsetting first makes the
    // cursor reach the plug-in again.
    window.unsetCursor();
    window.setCursor(cursor);
    reapplying_ = false;
  }

  void set_blocked(QWindow& window, bool blocked) {
    std::erase_if(blocked_, [&window](const QPointer<QWindow>& entry) { return entry == nullptr || entry == &window; });
    if (blocked) {
      blocked_.emplace_back(&window);
    }
  }

  bool has_blocked_window() const {
    return std::any_of(blocked_.begin(), blocked_.end(), [](const QPointer<QWindow>& entry) { return entry != nullptr; });
  }

  bool in_blocked_window(const QWidget& widget) const {
    const QWindow* window = cursor_native_window(widget);
    return window != nullptr && std::any_of(blocked_.begin(), blocked_.end(), [window](const QPointer<QWindow>& entry) {
             return entry.data() == window;
           });
  }

  QPointer<QWidget> pointer_widget_;
  std::vector<QPointer<QWindow>> blocked_;
  bool had_override_{false};
  bool reapplying_{false};
};

}  // namespace

bool wayland_pointer_cursor_fix_needed(const QString& platform_name, const QVersionNumber& qt_version) {
  return platform_name.startsWith(QLatin1String("wayland"), Qt::CaseInsensitive) &&
         qt_version < QVersionNumber(6, 10);
}

QObject* create_wayland_pointer_cursor_filter(QObject* parent) {
  return new WaylandPointerCursorFilter(parent);
}

void install_wayland_pointer_cursor_fix() {
  auto* app = qApp;
  if (app == nullptr || !wayland_pointer_cursor_fix_needed(QGuiApplication::platformName(), QLibraryInfo::version())) {
    return;
  }
  app->installEventFilter(create_wayland_pointer_cursor_filter(app));
}

}  // namespace patchy::ui
