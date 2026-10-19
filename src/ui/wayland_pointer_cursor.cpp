#include "ui/wayland_pointer_cursor.hpp"

#include <QApplication>
#include <QCursor>
#include <QEvent>
#include <QGuiApplication>
#include <QLibraryInfo>
#include <QPointer>
#include <QWidget>
#include <QWindow>

namespace patchy::ui {
namespace {

// The native window a widget's cursor is applied to, as qt_qpa_set_cursor finds it.
QWindow* native_window_of(const QWidget& widget) {
  const QWidget* native = widget.internalWinId() != 0 ? &widget : widget.nativeParentWidget();
  return native != nullptr ? native->windowHandle() : nullptr;
}

// The cursor Qt shows over `widget`: its own or its nearest ancestor's within the window,
// and the arrow over a disabled one (qt_qpa_set_cursor's rules).
QCursor effective_cursor(const QWidget& widget) {
  const QWidget* source = &widget;
  while (!source->isWindow() && !source->testAttribute(Qt::WA_SetCursor) && source->parentWidget() != nullptr) {
    source = source->parentWidget();
  }
  return source->isEnabled() ? source->cursor() : QCursor(Qt::ArrowCursor);
}

class WaylandPointerCursorFilter : public QObject {
 public:
  explicit WaylandPointerCursorFilter(QObject* parent) : QObject(parent) {}

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    const auto type = event->type();
    if (type != QEvent::Enter && type != QEvent::Leave && type != QEvent::CursorChange) {
      return false;
    }
    auto* widget = qobject_cast<QWidget*>(watched);  // QWindows get these events too
    if (widget == nullptr) {
      return false;
    }
    if (type == QEvent::Enter) {
      // Qt enters the window first, then each widget down to the one under the pointer.
      pointer_widget_ = widget;
    } else if (type == QEvent::Leave) {
      if (widget == pointer_widget_) {
        pointer_widget_ = widget->isWindow() ? nullptr : widget->parentWidget();
      }
    } else {
      reapply_pointer_cursor(*widget);
    }
    return false;
  }

 private:
  void reapply_pointer_cursor(const QWidget& changed) {
    QWidget* pointer = pointer_widget_.data();
    // An uncreated widget applied nothing; the override cursor shows everywhere by design.
    if (reapplying_ || pointer == nullptr || !pointer->isVisible() ||
        !changed.testAttribute(Qt::WA_WState_Created) || QGuiApplication::overrideCursor() != nullptr) {
      return;
    }
    QWindow* pointer_window = native_window_of(*pointer);
    if (pointer_window == nullptr || pointer_window == native_window_of(changed)) {
      return;
    }
    reapplying_ = true;
    // QWindow::setCursor skips a shape the window already holds; unsetting first makes the
    // cursor reach the plug-in again.
    pointer_window->unsetCursor();
    pointer_window->setCursor(effective_cursor(*pointer));
    reapplying_ = false;
  }

  QPointer<QWidget> pointer_widget_;
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
