#pragma once

#include <QPoint>
#include <QPointF>

#include <functional>
#include <memory>

class QWidget;

namespace patchy::ui {

// A pointer lock for a mouse gesture that must end where it started (the Alt+right-drag
// brush resize, GitHub issue 637). Where the pointer cannot be moved back by the app
// (Wayland), the gesture locks it instead: the pointer stays on the press point, the
// gesture reads its motion as relative deltas, and the compositor leaves the pointer at
// the hint when the lock ends. docs/wayland-pointer-lock.md.

struct PointerLockEvents {
  // The compositor locked the pointer: motion now arrives only through relative_motion.
  std::function<void()> locked;
  // The compositor ended the lock before the gesture did (it never comes back).
  std::function<void()> unlocked;
  // Pointer motion while locked, in the widget's logical pixels (pointer acceleration applied).
  std::function<void(QPointF)> relative_motion;
};

// Destroying the session ends the lock. Callbacks never run after destruction starts.
class PointerLockSession {
 public:
  virtual ~PointerLockSession() = default;
};

// Locks the pointer over `widget`'s window, with the pointer hint at `widget_position`.
// Returns nullptr where no lock is available (any platform but a Wayland session whose
// compositor offers both the pointer-constraints and relative-pointer protocols).
using PointerLockFactory =
    std::function<std::unique_ptr<PointerLockSession>(QWidget& widget, QPoint widget_position, PointerLockEvents events)>;

[[nodiscard]] std::unique_ptr<PointerLockSession> lock_pointer_at(QWidget& widget, QPoint widget_position,
                                                                  PointerLockEvents events);

// Whether QCursor::setPos can move the pointer (false on Wayland).
[[nodiscard]] bool platform_can_move_pointer();

// Test seams: replace the lock factory and the pointer-move answer for the tests that drive
// the gesture offscreen. Each returns the previous value; restore it when done.
PointerLockFactory set_pointer_lock_factory_for_tests(PointerLockFactory factory);
std::function<bool()> set_platform_can_move_pointer_for_tests(std::function<bool()> answer);

}  // namespace patchy::ui
