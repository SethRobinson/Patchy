#pragma once

class QCursor;
class QObject;
class QWidget;
class QWindow;

namespace patchy::ui {

// GitHub issue 504. While a modal dialog is open, the platform plug-in still applies a blocked
// window's cursor when the pointer enters it (Windows: QWindowsMouseHandler), so the canvas's
// tool cursor, or whatever cursor the window last held, showed over a main window that could
// not be used. The filter shows the arrow on every window from the moment it is blocked, and
// again whenever anything gives a blocked window another cursor (a widget inside it changing
// its cursor, Qt's enter and leave dispatch, the canvas refreshing its tool cursor when the
// application is reactivated), and on unblock gives the window the cursor of the widget under
// the pointer, or the cursor it held before. GitHub issue 634: Qt drops the platform's enter
// for a blocked window, so the window's first mouse move or press after unblock sends the
// enter Qt missed when the widget under the pointer never got one.

// An event filter that applies the rule; install it on the application. Owned by `parent`.
QObject* create_blocked_window_cursor_filter(QObject* parent);

// Installs the filter on the application on every platform. Where the Wayland pointer-cursor
// workaround runs, install that one first so this filter, installed last, runs first and the
// workaround puts the dialog's cursor back after the arrow. Call after the QApplication exists.
void install_blocked_window_cursor_fix();

// Shared with the Wayland pointer-cursor fix. The native window a widget's cursor is applied
// to, as qt_qpa_set_cursor finds it.
QWindow* cursor_native_window(const QWidget& widget);

// The cursor Qt shows over `widget`: its own or its nearest ancestor's within the window, and
// the arrow over a disabled one (qt_qpa_set_cursor's rules).
QCursor effective_widget_cursor(const QWidget& widget);

}  // namespace patchy::ui
