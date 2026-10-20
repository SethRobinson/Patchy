// Free Transform Skew, Distort and Perspective (GitHub issue 225): Ctrl+drag a
// corner distorts, Edit > Transform picks a mode in which plain drags skew,
// distort or apply perspective, pixel layers commit through the quad's
// homography, type layers refuse a non-affine quad, and session Undo steps back
// over a distortion. docs/free-transform.md.
#include "ui/canvas_widget.hpp"
#include "core/layer_metadata.hpp"
#include "ui/main_window.hpp"
#include "test_harness.hpp"
#include <QAction>
#include <QApplication>
#include <QColor>
#include <QImage>
#include <QList>
#include <QMenu>
#include <QPoint>
#include <QRect>
#include <QStatusBar>
#include <QTextEdit>
#include <optional>
#include <cmath>
#include <string>
#include <utility>
#include <vector>
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

namespace {

using namespace patchy::test::ui;

const QColor kBoxColor(60, 130, 230);

// A position-locked white Background plus one opaque blue layer at `bounds`, at
// 100% with the Move tool and snapping off.
void prepare_distort_scene(patchy::ui::MainWindow& window, QRect bounds) {
  patchy::Document document(400, 300, patchy::PixelFormat::rgba8());
  auto& background = document.add_pixel_layer(
      "Background", solid_pixels(400, 300, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  patchy::set_layer_locks_position(background, true);
  patchy::Layer layer(document.allocate_layer_id(), "Box",
                      solid_pixels(bounds.width(), bounds.height(), patchy::PixelFormat::rgba8(), kBoxColor));
  layer.set_bounds(patchy::Rect{bounds.x(), bounds.y(), bounds.width(), bounds.height()});
  document.add_layer(std::move(layer));
  window.add_document_session(std::move(document), QStringLiteral("Distort"));
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_zoom(1.0);
  canvas->center_document_in_view();
  canvas->set_snap_enabled(false);
  canvas->set_show_transform_controls(false);
  QApplication::processEvents();
}

QPoint widget_at(patchy::ui::CanvasWidget& canvas, int x, int y) {
  return canvas.widget_position_for_document_point(QPoint(x, y));
}

bool is_box(patchy::ui::CanvasWidget& canvas, int x, int y) {
  return color_close(canvas_pixel(canvas, QPoint(x, y)), kBoxColor, 24);
}

bool is_white(patchy::ui::CanvasWidget& canvas, int x, int y) {
  return color_close(canvas_pixel(canvas, QPoint(x, y)), QColor(Qt::white), 24);
}

// Bounds of a resampled quad: the homography's floating-point corners can put
// an edge on either side of a whole pixel.
bool rect_near(const std::optional<QRect>& actual, QRect expected) {
  return actual.has_value() && std::abs(actual->left() - expected.left()) <= 1 &&
         std::abs(actual->top() - expected.top()) <= 1 && std::abs(actual->right() - expected.right()) <= 1 &&
         std::abs(actual->bottom() - expected.bottom()) <= 1;
}

QImage canvas_crop(patchy::ui::CanvasWidget& canvas, QRect document_rect) {
  QApplication::processEvents();
  return canvas.grab().toImage().copy(
      QRect(canvas.widget_position_for_document_point(document_rect.topLeft()), document_rect.size()));
}

// Ctrl+drag a corner moves that corner alone; the commit resamples the pixels
// onto the new quad.
void ui_free_transform_ctrl_drag_corner_distorts() {
  patchy::ui::MainWindow window;
  const QRect bounds(100, 70, 160, 110);
  prepare_distort_scene(window, bounds);
  auto* canvas = require_canvas(window);
  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());

  drag(*canvas, widget_at(*canvas, 260, 180), widget_at(*canvas, 300, 210), Qt::ControlModifier);
  const auto view = QRect(40, 30, 320, 220);
  QList<QImage> strips{canvas_crop(*canvas, view)};
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  strips.push_back(canvas_crop(*canvas, view));
  save_stacked_artifact("ui_free_transform_distort_corner", strips);

  CHECK(!canvas->free_transform_active());
  CHECK(rect_near(canvas->active_layer_document_rect(), QRect(100, 70, 200, 140)));
  // Inside the new quad near the moved corner, and below the old bottom edge.
  CHECK(is_box(*canvas, 295, 205));
  CHECK(is_box(*canvas, 265, 185));
  // The bottom edge now runs from (100, 180) to (300, 210): below it near the
  // left stays white, which a plain corner scale would have filled.
  CHECK(is_white(*canvas, 120, 195));
  // The other three corners stayed put.
  CHECK(is_box(*canvas, 102, 72));
  CHECK(is_box(*canvas, 257, 72));
  CHECK(is_box(*canvas, 102, 177));
}

// Edit > Transform > Perspective: dragging a corner sideways moves the corner
// across the same side the opposite way (a symmetric trapezoid).
void ui_free_transform_perspective_mode_mirrors_the_partner_corner() {
  patchy::ui::MainWindow window;
  const QRect bounds(100, 70, 160, 110);
  prepare_distort_scene(window, bounds);
  auto* canvas = require_canvas(window);
  require_action(window, "editTransformPerspectiveAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());

  drag(*canvas, widget_at(*canvas, 260, 70), widget_at(*canvas, 300, 70));
  const auto view = QRect(40, 30, 320, 220);
  QList<QImage> strips{canvas_crop(*canvas, view)};
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  strips.push_back(canvas_crop(*canvas, view));
  save_stacked_artifact("ui_free_transform_perspective_mode", strips);

  CHECK(rect_near(canvas->active_layer_document_rect(), QRect(60, 70, 240, 110)));
  CHECK(is_box(*canvas, 65, 72));
  CHECK(is_box(*canvas, 295, 72));
  // The bottom keeps its width: the slanted sides leave the lower outer area white.
  CHECK(is_white(*canvas, 70, 175));
  CHECK(is_white(*canvas, 290, 175));
  CHECK(is_box(*canvas, 102, 177));
  CHECK(is_box(*canvas, 257, 177));
}

// Edit > Transform > Skew: an edge slides along itself (the vertical part of the
// drag is dropped), turning the box into a parallelogram.
void ui_free_transform_skew_mode_slides_an_edge() {
  patchy::ui::MainWindow window;
  const QRect bounds(100, 70, 160, 110);
  prepare_distort_scene(window, bounds);
  auto* canvas = require_canvas(window);
  require_action(window, "editTransformSkewAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());

  drag(*canvas, widget_at(*canvas, 180, 70), widget_at(*canvas, 210, 74));
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();

  CHECK(rect_near(canvas->active_layer_document_rect(), QRect(100, 70, 190, 110)));
  CHECK(is_box(*canvas, 285, 72));
  CHECK(is_white(*canvas, 105, 72));
  CHECK(is_box(*canvas, 103, 177));
  CHECK(is_white(*canvas, 285, 177));
}

// A type layer takes a skew (affine) but refuses a corner distortion: the drag
// does nothing and the status bar says to rasterize first.
void ui_free_transform_distort_refuses_type_layer() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  QApplication::processEvents();
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  drag(*canvas, widget_at(*canvas, 200, 150), widget_at(*canvas, 520, 400));
  QApplication::processEvents();
  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  if (editor == nullptr) {
    return;
  }
  editor->setPlainText(QStringLiteral("Distort me"));
  QApplication::processEvents();
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  process_events_for(120);
  const auto before = canvas->active_layer_document_rect();
  CHECK(before.has_value());
  if (!before.has_value()) {
    return;
  }

  require_action(window, "editTransformDistortAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  const auto corner = before->topLeft() + QPoint(before->width(), before->height());
  drag(*canvas, canvas->widget_position_for_document_point(corner),
       canvas->widget_position_for_document_point(corner + QPoint(40, 30)));
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("Rasterize")));
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(canvas->active_layer_document_rect() == before);
  const auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto* layer = document.active_layer_id().has_value() ? document.find_layer(*document.active_layer_id()) : nullptr;
  CHECK(layer != nullptr && patchy::layer_is_text(*layer));
}

// Session Undo takes back a distortion like any other drag.
void ui_free_transform_distort_is_a_session_step() {
  patchy::ui::MainWindow window;
  const QRect bounds(100, 70, 160, 110);
  prepare_distort_scene(window, bounds);
  auto* canvas = require_canvas(window);
  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  drag(*canvas, widget_at(*canvas, 260, 180), widget_at(*canvas, 300, 210), Qt::ControlModifier);
  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(canvas->active_layer_document_rect() == bounds);
  CHECK(is_white(*canvas, 280, 200));
}

// The three entries live under Edit > Transform with permanent hotkey ids, no
// default keys, and NoRole (macOS menu merging).
void ui_free_transform_mode_menu_entries() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* menu = window.findChild<QMenu*>(QStringLiteral("editTransformMenu"));
  CHECK(menu != nullptr);
  CHECK(menu != nullptr && menu->menuAction()->menuRole() == QAction::NoRole);
  for (const auto& [object_name, hotkey_id] :
       std::vector<std::pair<const char*, const char*>>{{"editTransformSkewAction", "edit.transform_skew"},
                                                        {"editTransformDistortAction", "edit.transform_distort"},
                                                        {"editTransformPerspectiveAction", "edit.transform_perspective"}}) {
    auto* action = require_action(window, object_name);
    CHECK(action->menuRole() == QAction::NoRole);
    CHECK(action->shortcut().isEmpty());
    CHECK(require_hotkey_action(window, QLatin1String(hotkey_id)) == action);
    CHECK(menu != nullptr && menu->actions().contains(action));
  }
}

}  // namespace

std::vector<patchy::test::TestCase> free_transform_distort_tests() {
  return {
      {"ui_free_transform_ctrl_drag_corner_distorts", ui_free_transform_ctrl_drag_corner_distorts},
      {"ui_free_transform_perspective_mode_mirrors_the_partner_corner",
       ui_free_transform_perspective_mode_mirrors_the_partner_corner},
      {"ui_free_transform_skew_mode_slides_an_edge", ui_free_transform_skew_mode_slides_an_edge},
      {"ui_free_transform_distort_refuses_type_layer", ui_free_transform_distort_refuses_type_layer},
      {"ui_free_transform_distort_is_a_session_step", ui_free_transform_distort_is_a_session_step},
      {"ui_free_transform_mode_menu_entries", ui_free_transform_mode_menu_entries},
  };
}
