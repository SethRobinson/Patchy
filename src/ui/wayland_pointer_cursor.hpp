#pragma once

#include <QString>
#include <QVersionNumber>

class QObject;

namespace patchy::ui {

// Workaround for a Qt Wayland bug behind GitHub issue 215 (docs/wayland.md). Before Qt
// 6.10, QWaylandCursor::changeCursor ignores the window whose cursor changed and sets the
// cursor on the seat's pointer, wherever the pointer is. A tool cursor the canvas refreshes
// while a dialog sits under the pointer (a zoom fit after a document opens, a modifier key)
// then shows over that dialog until the pointer crosses a widget with a cursor of its own.
// After a widget or window changes its cursor, the filter re-applies the cursor of the widget
// under the pointer when the pointer is in another window. It does the same when the
// application override cursor stack empties (Qt then re-applies every window's cursor) and
// when the application becomes active again; with a modal dialog up, a pointer on a blocked
// window (where Qt sees no enter) gets the arrow (GitHub issues 215 and 504).

// Whether the workaround applies: a Wayland platform and a Qt before 6.10, whose plug-in
// applies each window's cursor only while the pointer is over that window.
[[nodiscard]] bool wayland_pointer_cursor_fix_needed(const QString& platform_name, const QVersionNumber& qt_version);

// An event filter that applies the workaround whatever the platform (tests drive it
// offscreen). Install it on the application; owned by `parent`.
QObject* create_wayland_pointer_cursor_filter(QObject* parent);

// Installs the filter on the application when wayland_pointer_cursor_fix_needed() holds
// for the running platform and Qt. Call after the QApplication exists.
void install_wayland_pointer_cursor_fix();

}  // namespace patchy::ui
