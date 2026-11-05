#include "ui/blocked_window_cursor.hpp"

#include <QApplication>
#include <QCursor>
#include <QEnterEvent>
#include <QEvent>
#include <QMouseEvent>
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
    if (type == QEvent::MouseMove || type == QEvent::MouseButtonPress || type == QEvent::Enter) {
      if (!awaiting_enter_.empty()) {
        if (auto* handle = qobject_cast<QWindow*>(watched); handle != nullptr) {
          enter_if_missed(*handle, type == QEvent::Enter ? nullptr : static_cast<QMouseEvent*>(event));
        }
      }
      return false;
    }
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
    } else if (blocked_.empty()) {
      return false;
    } else if (widget != nullptr) {
      // The widget already applied its cursor (or the arrow, when disabled) to the window.
      if (QWindow* handle = cursor_native_window(*widget); handle != nullptr && is_blocked(*handle)) {
        handle->setCursor(Qt::ArrowCursor);
      }
    } else if (auto* handle = qobject_cast<QWindow*>(watched); handle != nullptr && is_blocked(*handle) &&
                                                               handle->cursor().shape() != Qt::ArrowCursor) {
      // Every cursor Qt gives a window passes QWindow::setCursor, which sends this: a widget's
      // own change, and also the ones no widget announces (enter and leave dispatch, Qt's
      // synthetic enter when a widget under the pointer shows or hides).
      handle->setCursor(Qt::ArrowCursor);
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
    std::erase_if(awaiting_enter_,
                  [&window](const QPointer<QWindow>& entry) { return entry == nullptr || entry == &window; });
    awaiting_enter_.emplace_back(&window);
    // Enter events were blocked too, so the cursor Qt last applied may belong to a widget the
    // pointer has since left: take the one under the pointer now when it is in this window.
    if (const QWidget* pointer = QApplication::widgetAt(QCursor::pos());
        pointer != nullptr && cursor_native_window(*pointer) == &window) {
      window.setCursor(effective_widget_cursor(*pointer));
    } else {
      window.setCursor(saved);
    }
  }

  // Qt drops the platform's enter for a blocked window, and a window keeps no later one while
  // the pointer stays inside: the pointer that came back from a menu while the modal opened
  // (issue 634) left Qt with no widget under it, so moves entered no widget and no cursor
  // change reached the window. The first mouse event after the modal closes sends the enter
  // Qt missed, unless the widget under the pointer already has it.
  void enter_if_missed(QWindow& window, const QMouseEvent* event) {
    const auto it = std::find_if(awaiting_enter_.begin(), awaiting_enter_.end(),
                                 [&window](const QPointer<QWindow>& entry) { return entry == &window; });
    if (it == awaiting_enter_.end()) {
      return;
    }
    awaiting_enter_.erase(it);
    // The platform's own enter (event == nullptr) needs no help; a popup takes every move.
    if (event == nullptr || QApplication::activePopupWidget() != nullptr ||
        (event->type() == QEvent::MouseMove && event->buttons() != Qt::NoButton)) {
      return;
    }
    const QPointF position = event->position();
    if (!QRectF(QPointF(0, 0), QSizeF(window.size())).contains(position)) {
      return;
    }
    QWidget* top = nullptr;
    for (QWidget* widget : QApplication::topLevelWidgets()) {
      if (widget->windowHandle() == &window) {
        top = widget;
        break;
      }
    }
    if (top == nullptr) {
      return;
    }
    QWidget* under = top->childAt(position.toPoint());
    if ((under != nullptr ? under : top)->underMouse()) {
      return;
    }
    // QWidgetWindow dispatches it as the platform's: enter events down to the widget under
    // the pointer, which then becomes Qt's reference for later moves and cursor changes.
    QEnterEvent enter(position, event->scenePosition(), event->globalPosition());
    QCoreApplication::sendEvent(&window, &enter);
  }

  std::vector<Blocked> blocked_;
  std::vector<QPointer<QWindow>> awaiting_enter_;
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
  if (auto* app = qApp; app != nullptr) {
    app->installEventFilter(create_blocked_window_cursor_filter(app));
  }
}

}  // namespace patchy::ui
