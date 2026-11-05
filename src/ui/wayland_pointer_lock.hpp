#pragma once

#include "ui/pointer_lock.hpp"

#include <memory>

namespace patchy::ui {

// Linux only (PATCHY_HAVE_WAYLAND_POINTER_LOCK): a oneshot zwp_locked_pointer_v1 on the
// widget's window surface with its cursor position hint at `widget_position`, plus a
// zwp_relative_pointer_v1 for the motion the lock hides. Talks to the compositor over Qt's
// own wl_display (QNativeInterface::QWaylandApplication) on a private event queue, through
// the libwayland-client Qt's plug-in already loaded (dlopen RTLD_NOLOAD: nothing is linked).
// Returns nullptr on any other platform, when the compositor lacks either protocol, or when
// Qt hands out no surface or pointer. docs/wayland-pointer-lock.md.
[[nodiscard]] std::unique_ptr<PointerLockSession> create_wayland_pointer_lock(QWidget& widget, QPoint widget_position,
                                                                              PointerLockEvents events);

}  // namespace patchy::ui
