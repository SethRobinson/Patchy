// The Alt+right-drag brush resize with a pointer lock (GitHub issue 637, docs/wayland.md
// "The brush-resize drag"). A fake lock factory stands in for the Wayland protocols, so the
// drag-state logic runs offscreen on every platform: when the lock is requested and
// released, relative motion driving the size, and the fallbacks when no lock holds.

#include "core/document.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/pointer_lock.hpp"
#include <QApplication>
#include <QColor>
#include <QCursor>
#include <QFocusEvent>
#include <QImage>
#include <QPoint>
#include <QPointF>
#include <functional>
#include <memory>
#include <utility>
#include <vector>
#include "test_harness.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

namespace {

using namespace patchy::test::ui;

struct FakeLockLog {
  int requested = 0;
  int released = 0;
  QPoint hint;
  patchy::ui::PointerLockEvents events;
};

class FakeLockSession final : public patchy::ui::PointerLockSession {
 public:
  explicit FakeLockSession(FakeLockLog& log) : log_(log) {}
  ~FakeLockSession() override { ++log_.released; }
  FakeLockSession(const FakeLockSession&) = delete;
  FakeLockSession& operator=(const FakeLockSession&) = delete;

 private:
  FakeLockLog& log_;
};

// Installs the fake for one test and restores the real platform answers afterwards.
class FakePointerLockPlatform {
 public:
  FakePointerLockPlatform(bool offer_lock, bool can_move_pointer) {
    previous_factory_ = patchy::ui::set_pointer_lock_factory_for_tests(
        [this, offer_lock](QWidget&, QPoint widget_position, patchy::ui::PointerLockEvents events)
            -> std::unique_ptr<patchy::ui::PointerLockSession> {
          ++log.requested;
          log.hint = widget_position;
          log.events = std::move(events);
          if (!offer_lock) {
            return nullptr;
          }
          return std::make_unique<FakeLockSession>(log);
        });
    previous_can_move_ =
        patchy::ui::set_platform_can_move_pointer_for_tests([can_move_pointer]() { return can_move_pointer; });
  }
  ~FakePointerLockPlatform() {
    patchy::ui::set_pointer_lock_factory_for_tests(std::move(previous_factory_));
    patchy::ui::set_platform_can_move_pointer_for_tests(std::move(previous_can_move_));
  }
  FakePointerLockPlatform(const FakePointerLockPlatform&) = delete;
  FakePointerLockPlatform& operator=(const FakePointerLockPlatform&) = delete;

  void lock() const { log.events.locked(); }
  void unlock() const { log.events.unlocked(); }
  void move_relative(QPointF delta) const { log.events.relative_motion(delta); }

  FakeLockLog log;

 private:
  patchy::ui::PointerLockFactory previous_factory_;
  std::function<bool()> previous_can_move_;
};

struct BrushScene {
  patchy::Document document{120, 80, patchy::PixelFormat::rgba8()};
  patchy::ui::CanvasWidget canvas;

  BrushScene() {
    document.add_pixel_layer("Paint", solid_pixels(120, 80, patchy::PixelFormat::rgba8(), QColor(255, 255, 255)));
    canvas.resize(420, 300);
    canvas.set_document(&document);
    canvas.set_tool(patchy::ui::CanvasTool::Brush);
    canvas.set_zoom(1.0);
    canvas.set_brush_size(20);
    canvas.set_brush_softness(50);
    canvas.show();
    QApplication::processEvents();
  }

  QPoint origin() const { return QPoint(180, 150); }

  void press() {
    send_mouse(canvas, QEvent::MouseButtonPress, origin(), Qt::RightButton, Qt::RightButton, Qt::AltModifier);
  }
  void move(QPoint position) {
    send_mouse(canvas, QEvent::MouseMove, position, Qt::NoButton, Qt::RightButton, Qt::AltModifier);
  }
  void release(QPoint position) {
    send_mouse(canvas, QEvent::MouseButtonRelease, position, Qt::RightButton, Qt::NoButton, Qt::AltModifier);
  }
};

bool reddish(const QImage& image, QPoint point) {
  const auto color = image.pixelColor(point);
  return color.red() > color.green() + 60;
}

void ui_brush_adjust_pointer_lock_drives_size_with_relative_motion() {
  FakePointerLockPlatform platform(true, false);
  BrushScene scene;

  scene.press();
  CHECK(platform.log.requested == 1);
  CHECK(platform.log.hint == scene.origin());
  platform.lock();
  // The same 25 px right, 25 px up an absolute drag makes (ui_brush_alt_right_drag_syncs_options_bar_spins):
  // size 20 + 2 * 25 = 70, softness 50 + 0.4 * 25 = 60.
  platform.move_relative(QPointF(12.5, -10.0));
  platform.move_relative(QPointF(12.5, -15.0));
  CHECK(scene.canvas.brush_size() == 70);
  CHECK(scene.canvas.brush_softness() == 60);
  // A locked pointer does not move: a stale absolute position (Qt's synthesized move) is ignored.
  scene.move(scene.origin() + QPoint(150, 0));
  CHECK(scene.canvas.brush_size() == 70);
  // The disc (radius 35) stays on the press point while the drag reads relative motion: a
  // point 10 px left of it is inside only when the disc is not centered on the virtual pointer.
  const auto hud = scene.canvas.grab().toImage();
  CHECK(reddish(hud, scene.origin() - QPoint(10, 0)));
  CHECK(!reddish(hud, scene.origin() + QPoint(60, 0)));

  // The compositor holds the pointer on the press point: releasing ends the lock and never
  // tries to move the pointer itself.
  const QPoint elsewhere = scene.canvas.mapToGlobal(QPoint(20, 20));
  QCursor::setPos(elsewhere);
  scene.release(scene.origin());
  CHECK(platform.log.released == 1);
  CHECK(QCursor::pos() == elsewhere);
  CHECK(scene.canvas.brush_size() == 70);
}

void ui_brush_adjust_pointer_lock_is_released_on_every_exit() {
  FakePointerLockPlatform platform(true, false);
  BrushScene scene;

  // Escape cancels the drag (size back to 20) and ends the lock.
  scene.press();
  platform.lock();
  platform.move_relative(QPointF(20.0, 0.0));
  CHECK(scene.canvas.brush_size() == 60);
  send_key_press(scene.canvas, Qt::Key_Escape, Qt::AltModifier);
  CHECK(platform.log.released == 1);
  CHECK(scene.canvas.brush_size() == 20);

  // Focus loss commits and ends the lock.
  scene.press();
  platform.lock();
  platform.move_relative(QPointF(10.0, 0.0));
  QFocusEvent focus_out(QEvent::FocusOut, Qt::ActiveWindowFocusReason);
  QApplication::sendEvent(&scene.canvas, &focus_out);
  CHECK(platform.log.released == 2);
  CHECK(scene.canvas.brush_size() == 40);

  // A tool switch ends it.
  scene.press();
  platform.lock();
  scene.canvas.set_tool(patchy::ui::CanvasTool::Eraser);
  CHECK(platform.log.released == 3);
  scene.canvas.set_tool(patchy::ui::CanvasTool::Brush);

  // Hiding the canvas (its window going away, a tab switch) ends it.
  scene.press();
  platform.lock();
  scene.canvas.hide();
  CHECK(platform.log.released == 4);
  scene.canvas.show();
  QApplication::processEvents();

  // A document switch ends it.
  scene.press();
  platform.lock();
  patchy::Document other(60, 40, patchy::PixelFormat::rgba8());
  other.add_pixel_layer("Other", solid_pixels(60, 40, patchy::PixelFormat::rgba8(), QColor(255, 255, 255)));
  scene.canvas.set_document(&other);
  CHECK(platform.log.released == 5);
  scene.canvas.set_document(&scene.document);
  scene.canvas.set_zoom(1.0);

  // Destroying the canvas mid-drag (app quit) ends it with the canvas.
  {
    BrushScene doomed;
    doomed.press();
    platform.lock();
    CHECK(platform.log.requested == 6);
  }
  CHECK(platform.log.released == 6);

  // A pen drag never locks.
  send_tablet(scene.canvas, QEvent::TabletPress, scene.origin(), 0.0, Qt::RightButton, Qt::RightButton,
              Qt::AltModifier);
  send_tablet(scene.canvas, QEvent::TabletRelease, scene.origin(), 0.0, Qt::RightButton, Qt::NoButton,
              Qt::AltModifier);
  CHECK(platform.log.requested == 6);
}

void ui_brush_adjust_pointer_lock_lost_mid_drag_continues() {
  FakePointerLockPlatform platform(true, false);
  BrushScene scene;

  scene.press();
  platform.lock();
  platform.move_relative(QPointF(20.0, 0.0));
  CHECK(scene.canvas.brush_size() == 60);
  // The compositor ends the lock early. The pointer is still where it was locked, so the
  // next absolute positions continue from the drag position instead of jumping back.
  platform.unlock();
  scene.move(scene.origin() + QPoint(2, 0));
  CHECK(scene.canvas.brush_size() == 60);
  scene.move(scene.origin() + QPoint(7, 0));
  CHECK(scene.canvas.brush_size() == 70);
  // The pointer cannot be moved back: the brush is left where the pointer is, untouched.
  const QPoint pointer_global = scene.canvas.mapToGlobal(scene.origin() + QPoint(7, 0));
  QCursor::setPos(pointer_global);
  scene.release(scene.origin() + QPoint(7, 0));
  CHECK(platform.log.released == 1);
  CHECK(QCursor::pos() == pointer_global);
  CHECK(scene.canvas.brush_size() == 70);
}

void ui_brush_adjust_without_lock_or_warp_centers_on_pointer() {
  // Wayland without the protocols: no lock and no pointer warp. The disc follows the
  // pointer, as for a pen, so the gesture ends with the brush under it at the size it shows.
  FakePointerLockPlatform platform(false, false);
  BrushScene scene;
  scene.canvas.set_brush_softness(0);

  scene.press();
  CHECK(platform.log.requested == 1);
  scene.move(scene.origin() + QPoint(20, 0));
  CHECK(scene.canvas.brush_size() == 100);
  const auto hud = scene.canvas.grab().toImage();
  CHECK(reddish(hud, scene.origin() + QPoint(65, 0)));
  CHECK(reddish(hud, scene.origin() - QPoint(25, 0)));
  const QPoint pointer_global = scene.canvas.mapToGlobal(scene.origin() + QPoint(20, 0));
  QCursor::setPos(pointer_global);
  scene.release(scene.origin() + QPoint(20, 0));
  CHECK(QCursor::pos() == pointer_global);
  CHECK(scene.canvas.brush_size() == 100);
}

void ui_brush_adjust_moves_pointer_back_where_it_can() {
  // Windows, macOS, X11: no lock, and the pointer goes back to the press point as before.
  FakePointerLockPlatform platform(false, true);
  BrushScene scene;

  scene.press();
  scene.move(scene.origin() + QPoint(25, -25));
  CHECK(scene.canvas.brush_size() == 70);
  CHECK(scene.canvas.brush_softness() == 60);
  QCursor::setPos(scene.canvas.mapToGlobal(scene.origin() + QPoint(25, -25)));
  scene.release(scene.origin() + QPoint(25, -25));
  CHECK(QCursor::pos() == scene.canvas.mapToGlobal(scene.origin()));
}

}  // namespace

std::vector<patchy::test::TestCase> brush_adjust_pointer_lock_tests() {
  return {
      {"ui_brush_adjust_pointer_lock_drives_size_with_relative_motion",
       ui_brush_adjust_pointer_lock_drives_size_with_relative_motion},
      {"ui_brush_adjust_pointer_lock_is_released_on_every_exit", ui_brush_adjust_pointer_lock_is_released_on_every_exit},
      {"ui_brush_adjust_pointer_lock_lost_mid_drag_continues", ui_brush_adjust_pointer_lock_lost_mid_drag_continues},
      {"ui_brush_adjust_without_lock_or_warp_centers_on_pointer",
       ui_brush_adjust_without_lock_or_warp_centers_on_pointer},
      {"ui_brush_adjust_moves_pointer_back_where_it_can", ui_brush_adjust_moves_pointer_back_where_it_can},
  };
}
