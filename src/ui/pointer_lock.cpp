#include "ui/pointer_lock.hpp"

#include <QGuiApplication>
#include <QString>

#include <utility>

#ifdef PATCHY_HAVE_WAYLAND_POINTER_LOCK
#include "ui/wayland_pointer_lock.hpp"
#endif

namespace patchy::ui {
namespace {

PointerLockFactory& factory_override() {
  static PointerLockFactory factory;
  return factory;
}

std::function<bool()>& can_move_override() {
  static std::function<bool()> answer;
  return answer;
}

}  // namespace

std::unique_ptr<PointerLockSession> lock_pointer_at(QWidget& widget, QPoint widget_position,
                                                    PointerLockEvents events) {
  if (const auto& factory = factory_override(); factory) {
    return factory(widget, widget_position, std::move(events));
  }
#ifdef PATCHY_HAVE_WAYLAND_POINTER_LOCK
  return create_wayland_pointer_lock(widget, widget_position, std::move(events));
#else
  static_cast<void>(widget);
  static_cast<void>(widget_position);
  static_cast<void>(events);
  return nullptr;
#endif
}

bool platform_can_move_pointer() {
  if (const auto& answer = can_move_override(); answer) {
    return answer();
  }
  return !QGuiApplication::platformName().startsWith(QStringLiteral("wayland"));
}

PointerLockFactory set_pointer_lock_factory_for_tests(PointerLockFactory factory) {
  return std::exchange(factory_override(), std::move(factory));
}

std::function<bool()> set_platform_can_move_pointer_for_tests(std::function<bool()> answer) {
  return std::exchange(can_move_override(), std::move(answer));
}

}  // namespace patchy::ui
