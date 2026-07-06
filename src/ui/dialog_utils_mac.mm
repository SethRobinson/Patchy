// macOS half of keep_dialog_above_parent_window (see dialog_utils.hpp).
//
// macOS has no Win32-style "owned window" z-order: a non-modal QDialog is an
// ordinary sibling window, so clicking the main window raises it OVER the open
// dialog. With the main window edit-locked while a preview dialog runs, the app
// then looks frozen. AppKit's native answer is the child-window relationship:
// [parent addChildWindow:child ordered:NSWindowAbove] makes the window server
// keep the child above its parent no matter which window is clicked, at every
// level of a dialog chain (main window -> Layer Style -> its color picker).

#include "ui/dialog_utils.hpp"

#include <QDialog>
#include <QEvent>
#include <QGuiApplication>
#include <QTimer>
#include <QWidget>
#include <QWindow>

#import <AppKit/AppKit.h>

namespace patchy::ui {

namespace {

constexpr auto kDialogAnchorInstalledProperty = "patchy.macDialogAnchorInstalled";

// winId() is an NSView* only on the cocoa platform; under the offscreen test
// platform it is an opaque token that must never be dereferenced.
bool platform_is_cocoa() {
  static const bool is_cocoa =
      QGuiApplication::platformName().compare(QStringLiteral("cocoa"), Qt::CaseInsensitive) == 0;
  return is_cocoa;
}

NSWindow* ns_window_for_widget(QWidget* widget) {
  if (widget == nullptr) {
    return nil;
  }
  QWindow* handle = widget->windowHandle();
  if (handle == nullptr || handle->handle() == nullptr) {
    return nil;  // no native window (never created, or already torn down)
  }
  auto* view = reinterpret_cast<NSView*>(handle->winId());
  return view != nil ? view.window : nil;
}

// Attaches on Show and detaches on Hide/Close. The detach must happen before the
// window goes away: AppKit keeps ordering attached children with their parent even
// when hidden, which would flash closed dialogs back on screen.
class MacDialogChildWindowAnchor final : public QObject {
public:
  explicit MacDialogChildWindowAnchor(QDialog& dialog) : QObject(&dialog), dialog_(dialog) {}

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    Q_UNUSED(watched);
    switch (event->type()) {
      case QEvent::Show:
        // The NSWindow exists but is not ordered on screen yet; attaching now would
        // order it in prematurely. Defer to the next event-loop turn (both exec_dialog
        // and run_non_modal_dialog enter an event loop right after show()).
        QTimer::singleShot(0, &dialog_, [this] { attach(); });
        break;
      case QEvent::Hide:
      case QEvent::Close:
        detach();
        break;
      default:
        break;
    }
    return QObject::eventFilter(watched, event);
  }

private:
  void attach() {
    if (!dialog_.isVisible()) {
      return;
    }
    NSWindow* child = ns_window_for_widget(&dialog_);
    if (child == nil || child.parentWindow != nil) {
      return;
    }
    QWidget* parent_widget = dialog_.parentWidget();
    NSWindow* parent = ns_window_for_widget(parent_widget != nullptr ? parent_widget->window() : nullptr);
    if (parent == nil || parent == child) {
      return;
    }
    [parent addChildWindow:child ordered:NSWindowAbove];
  }

  void detach() {
    NSWindow* child = ns_window_for_widget(&dialog_);
    if (child != nil && child.parentWindow != nil) {
      [child.parentWindow removeChildWindow:child];
    }
  }

  QDialog& dialog_;
};

}  // namespace

void keep_dialog_above_parent_window(QDialog& dialog) {
  if (!platform_is_cocoa() || dialog.property(kDialogAnchorInstalledProperty).toBool()) {
    return;
  }
  dialog.installEventFilter(new MacDialogChildWindowAnchor(dialog));
  dialog.setProperty(kDialogAnchorInstalledProperty, true);
}

}  // namespace patchy::ui
