// Move tool and transform box hit zones (GitHub issues 176 and 170): a drag
// anywhere along an edge of the transform box resizes that side, a drag just
// outside a corner rotates, a tiny box keeps a Move area, the Crop box edges
// resize too, and the Move tool shows its four-way cursor only where a press
// would drag something (an arrow with a move badge elsewhere).
#include "ui/canvas_widget.hpp"
#include "core/layer_metadata.hpp"
#include "ui/main_window.hpp"
#include "ui/tool_cursors.hpp"
#include "test_harness.hpp"
#include <QApplication>
#include <QColor>
#include <QCursor>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QTransform>
#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

namespace {

using namespace patchy::test::ui;

// A position-locked white Background plus one opaque blue layer at `bounds`,
// shown at 100% with the Move tool, snapping off, Auto-Select on.
patchy::LayerId prepare_zone_scene(patchy::ui::MainWindow& window, QRect bounds, bool show_controls) {
  patchy::Document document(360, 260, patchy::PixelFormat::rgba8());
  auto& background = document.add_pixel_layer(
      "Background", solid_pixels(360, 260, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  patchy::set_layer_locks_position(background, true);
  patchy::Layer layer(document.allocate_layer_id(), "Box",
                      solid_pixels(bounds.width(), bounds.height(), patchy::PixelFormat::rgba8(), QColor(60, 130, 230)));
  const auto id = layer.id();
  layer.set_bounds(patchy::Rect{bounds.x(), bounds.y(), bounds.width(), bounds.height()});
  document.add_layer(std::move(layer));
  window.add_document_session(std::move(document), QStringLiteral("Zones"));
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_zoom(1.0);
  canvas->center_document_in_view();
  canvas->set_snap_enabled(false);
  canvas->set_auto_select_layer(true);
  canvas->set_show_transform_controls(show_controls);
  QApplication::processEvents();
  return id;
}

void hover(patchy::ui::CanvasWidget& canvas, QPoint position) {
  send_mouse(canvas, QEvent::MouseMove, position, Qt::NoButton, Qt::NoButton);
}

bool is_rotate_cursor(const QCursor& cursor) {
  return cursor.shape() == Qt::BitmapCursor &&
         cursor.pixmap().toImage() == patchy::ui::rotate_cursor().pixmap().toImage();
}

bool is_resize_shape(Qt::CursorShape shape) {
  return shape == Qt::SizeHorCursor || shape == Qt::SizeVerCursor || shape == Qt::SizeFDiagCursor ||
         shape == Qt::SizeBDiagCursor;
}

// Hovers every `step`-th widget pixel of `area` and tints it by the cursor the
// canvas shows there, over a grab of the canvas taken first. `zones` picks the
// legend: transform zones (orange resize, green rotate) or the Move cursor
// (blue where the four-way move cursor shows).
enum class ZoneLegend { Transform, MoveCursor };

QImage hover_zone_map(patchy::ui::CanvasWidget& canvas, QRect area, ZoneLegend legend, int step = 1) {
  hover(canvas, QPoint(1, 1));
  QApplication::processEvents();
  auto image = canvas.grab().toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
  QPainter painter(&image);
  painter.setPen(Qt::NoPen);
  for (int y = area.top(); y <= area.bottom(); y += step) {
    for (int x = area.left(); x <= area.right(); x += step) {
      hover(canvas, QPoint(x, y));
      const auto cursor = canvas.cursor();
      std::optional<QColor> tint;
      if (legend == ZoneLegend::Transform) {
        if (is_resize_shape(cursor.shape())) {
          tint = QColor(255, 140, 0, 170);
        } else if (is_rotate_cursor(cursor)) {
          tint = QColor(0, 190, 90, 150);
        }
      } else if (cursor.shape() == Qt::SizeAllCursor) {
        tint = QColor(40, 110, 255, 110);
      }
      if (tint.has_value()) {
        painter.fillRect(QRect(x, y, step, step), *tint);
      }
    }
  }
  painter.end();
  hover(canvas, QPoint(1, 1));
  return image.copy(area);
}

QImage scaled_up(const QImage& image, int factor) {
  return image.scaled(image.size() * factor, Qt::IgnoreAspectRatio, Qt::FastTransformation);
}

patchy::ui::CanvasWidget::TransformControlsState require_state(patchy::ui::CanvasWidget& canvas) {
  const auto state = canvas.transform_controls_state();
  CHECK(state.has_value());
  return state.value_or(patchy::ui::CanvasWidget::TransformControlsState{});
}

// GitHub issue 176: with Show Transform Controls the whole edge resizes (a
// band on both sides of the line), just outside a corner rotates, the center
// still moves, and an edge grab does not jump by the grab offset.
void ui_transform_zone_passive_edges_resize_and_corners_rotate() {
  patchy::ui::MainWindow window;
  const QRect bounds(100, 70, 160, 110);
  prepare_zone_scene(window, bounds, true);
  auto* canvas = require_canvas(window);
  const auto tl = canvas->widget_position_for_document_point(bounds.topLeft());
  const auto br = canvas->widget_position_for_document_point(bounds.topLeft() + QPoint(bounds.width(), bounds.height()));
  // Select the layer first so the passive box is up.
  const auto center = canvas->widget_position_for_document_point(bounds.center());
  send_mouse(*canvas, QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  save_image_artifact("ui_transform_zone_passive_map",
                      scaled_up(hover_zone_map(*canvas, QRect(tl - QPoint(44, 60), br + QPoint(44, 44)),
                                               ZoneLegend::Transform),
                                2));

  hover(*canvas, center);
  CHECK(canvas->cursor().shape() == Qt::SizeAllCursor);
  // Anywhere along an edge, just outside or just inside the line.
  hover(*canvas, QPoint(tl.x() + 40, tl.y() - 3));
  CHECK(canvas->cursor().shape() == Qt::SizeVerCursor);
  hover(*canvas, QPoint(tl.x() + 40, tl.y() + 4));
  CHECK(canvas->cursor().shape() == Qt::SizeVerCursor);
  hover(*canvas, QPoint(br.x() + 4, tl.y() + 30));
  CHECK(canvas->cursor().shape() == Qt::SizeHorCursor);
  hover(*canvas, QPoint(tl.x() + 120, br.y() - 3));
  CHECK(canvas->cursor().shape() == Qt::SizeVerCursor);
  hover(*canvas, QPoint(tl.x() - 5, tl.y() + 80));
  CHECK(canvas->cursor().shape() == Qt::SizeHorCursor);
  // Just outside each corner rotates; farther away does not.
  hover(*canvas, br + QPoint(12, 12));
  CHECK(is_rotate_cursor(canvas->cursor()));
  hover(*canvas, tl - QPoint(14, 10));
  CHECK(is_rotate_cursor(canvas->cursor()));
  hover(*canvas, QPoint(br.x() + 60, tl.y() + 50));
  CHECK(!is_rotate_cursor(canvas->cursor()));
  CHECK(!is_resize_shape(canvas->cursor().shape()));

  // A grab 4 px outside the left edge (clear of the handle squares) resizes
  // that side, and the edge keeps its place under the grab: 20 px of travel
  // widens the box by exactly 20.
  const QPoint left_grab(tl.x() - 4, tl.y() + 20);
  send_mouse(*canvas, QEvent::MouseButtonPress, left_grab, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, left_grab + QPoint(-20, 0), Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, left_grab + QPoint(-20, 0), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  const auto widened = require_state(*canvas);
  CHECK(std::abs(widened.scale_x_percent - 112.5) < 0.01);
  CHECK(std::abs(widened.scale_y_percent - 100.0) < 0.01);
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
  CHECK(canvas->active_layer_document_rect() == QRect(80, 70, 180, 110));

  // Just outside the bottom-right corner, a quarter turn about the center.
  const auto box = canvas->active_layer_document_rect();
  CHECK(box.has_value());
  const auto box_tl = canvas->widget_position_for_document_point(box->topLeft());
  const auto box_br = canvas->widget_position_for_document_point(box->topLeft() + QPoint(box->width(), box->height()));
  const QPointF box_center = (QPointF(box_tl) + QPointF(box_br)) / 2.0;
  const auto grab = box_br + QPoint(12, 12);
  hover(*canvas, grab);
  CHECK(is_rotate_cursor(canvas->cursor()));
  const QPointF arm = QPointF(grab) - box_center;
  const auto turned = (box_center + QPointF(-arm.y(), arm.x())).toPoint();
  send_mouse(*canvas, QEvent::MouseButtonPress, grab, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, turned, Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, turned, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  CHECK(std::abs(require_state(*canvas).rotation_degrees - 90.0) < 1.0);
  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
}

// Inside a rotated Free Transform session the zones turn with the box, and the
// edge cursor follows the edge's on-screen direction.
void ui_transform_zone_rotated_session_edges_follow_the_box() {
  patchy::ui::MainWindow window;
  const QRect bounds(100, 70, 160, 110);
  prepare_zone_scene(window, bounds, false);
  auto* canvas = require_canvas(window);
  CHECK(canvas->begin_free_transform());
  const auto start = require_state(*canvas);
  CHECK(canvas->set_transform_controls_state(start.reference_position, 100.0, 100.0, 30.0));
  QApplication::processEvents();

  const QPointF center(canvas->widget_position_for_document_point(
      QPoint(bounds.x() + bounds.width() / 2, bounds.y() + bounds.height() / 2)));
  QTransform to_widget;
  to_widget.translate(center.x(), center.y());
  to_widget.rotate(30.0);
  const auto at_local = [&](QPointF local) { return to_widget.map(local).toPoint(); };
  const double half_w = bounds.width() / 2.0;
  const double half_h = bounds.height() / 2.0;

  save_image_artifact("ui_transform_zone_rotated_map",
                      scaled_up(hover_zone_map(*canvas,
                                               QRect(center.toPoint() - QPoint(150, 130), QSize(300, 260)),
                                               ZoneLegend::Transform, 2),
                                2));

  // The top edge, 3 px outside and away from its handle: its outward normal
  // points up and to the right after a 30 degree turn.
  const auto top_edge = at_local(QPointF(-40.0, -half_h - 3.0));
  hover(*canvas, top_edge);
  CHECK(canvas->cursor().shape() == Qt::SizeBDiagCursor);
  hover(*canvas, at_local(QPointF(half_w + 3.0, 25.0)));
  CHECK(canvas->cursor().shape() == Qt::SizeFDiagCursor);
  hover(*canvas, at_local(QPointF(half_w + 12.0, half_h + 12.0)));
  CHECK(is_rotate_cursor(canvas->cursor()));
  hover(*canvas, at_local(QPointF(0.0, 0.0)));
  CHECK(canvas->cursor().shape() == Qt::SizeAllCursor);

  // Pull the top edge 20 px outward along its normal.
  const auto outward = at_local(QPointF(-40.0, -half_h - 23.0));
  send_mouse(*canvas, QEvent::MouseButtonPress, top_edge, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, outward, Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, outward, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  const auto after = require_state(*canvas);
  CHECK(std::abs(after.scale_y_percent - 100.0 * (bounds.height() + 20.0) / bounds.height()) < 1.5);
  CHECK(std::abs(after.scale_x_percent - 100.0) < 0.5);
  CHECK(std::abs(after.rotation_degrees - 30.0) < 0.01);
  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();
}

// A box a few widget pixels across keeps a Move area in its middle: the eight
// 14 px handle squares used to cover all of it, so it could only be resized.
void ui_transform_zone_tiny_box_keeps_move_area() {
  patchy::ui::MainWindow window;
  const QRect bounds(150, 100, 8, 8);
  prepare_zone_scene(window, bounds, true);
  auto* canvas = require_canvas(window);
  const auto center = canvas->widget_position_for_document_point(bounds.center());
  send_mouse(*canvas, QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());

  save_image_artifact("ui_transform_zone_tiny_map",
                      scaled_up(hover_zone_map(*canvas, QRect(center - QPoint(40, 50), QSize(80, 90)),
                                               ZoneLegend::Transform),
                                4));

  hover(*canvas, center);
  CHECK(canvas->cursor().shape() == Qt::SizeAllCursor);
  const auto br = canvas->widget_position_for_document_point(bounds.topLeft() + QPoint(bounds.width(), bounds.height()));
  hover(*canvas, br + QPoint(2, 2));
  CHECK(canvas->cursor().shape() == Qt::SizeFDiagCursor);

  drag(*canvas, center, center + QPoint(30, 20));
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
  CHECK(canvas->active_layer_document_rect() == QRect(180, 120, 8, 8));
}

// The Crop box gets the same edge bands (GitHub issue 176, the reporter's
// follow-up); off the box still rotates.
void ui_transform_zone_crop_edges_resize() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action(window, "toolCropAction")->trigger();
  QApplication::processEvents();
  canvas->set_snap_enabled(false);
  canvas->set_crop_ratio(0.0, 0.0);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(100, 100)),
       canvas->widget_position_for_document_point(QPoint(200, 180)));
  CHECK(canvas->crop_session_rect() == QRect(100, 100, 101, 81));

  const auto top_inside = canvas->widget_position_for_document_point(QPoint(130, 100)) + QPoint(0, 3);
  const auto right_outside = canvas->widget_position_for_document_point(QPoint(201, 160)) + QPoint(4, 0);
  const auto box_tl = canvas->widget_position_for_document_point(QPoint(100, 100));
  save_image_artifact("ui_transform_zone_crop_map",
                      scaled_up(hover_zone_map(*canvas, QRect(box_tl - QPoint(30, 30), QSize(162, 142)),
                                               ZoneLegend::Transform),
                                2));

  hover(*canvas, top_inside);
  CHECK(canvas->cursor().shape() == Qt::SizeVerCursor);
  hover(*canvas, right_outside);
  CHECK(canvas->cursor().shape() == Qt::SizeHorCursor);
  hover(*canvas, canvas->widget_position_for_document_point(QPoint(201, 181)) + QPoint(15, 15));
  CHECK(is_rotate_cursor(canvas->cursor()));

  // The top edge follows the drag without jumping by the 3 px grab offset.
  drag(*canvas, top_inside, top_inside + QPoint(0, -20));
  CHECK(canvas->crop_session_rect() == QRect(100, 80, 101, 101));
  drag(*canvas, right_outside, right_outside + QPoint(10, 0));
  CHECK(canvas->crop_session_rect() == QRect(100, 80, 111, 101));
}

// GitHub issue 170: the four-way move cursor shows only where a press drags
// something. Over empty canvas or the pasteboard a press with Auto-Select on
// starts a layer selection rectangle, so the pointer is an arrow with a move
// badge there. Auto-Select off drags the selected layers from anywhere.
void ui_move_cursor_shows_drag_only_where_a_press_moves() {
  patchy::ui::MainWindow window;
  const QRect bounds(100, 70, 160, 110);
  const auto id = prepare_zone_scene(window, bounds, false);
  auto* canvas = require_canvas(window);
  const auto origin = canvas->widget_position_for_document_point(QPoint(0, 0));
  const auto over_layer = canvas->widget_position_for_document_point(bounds.center());
  const auto over_background = canvas->widget_position_for_document_point(QPoint(40, 220));
  const auto pasteboard = origin - QPoint(30, -40);
  CHECK(canvas->rect().contains(pasteboard));

  auto map = hover_zone_map(*canvas, QRect(origin - QPoint(40, 30), QSize(440, 320)), ZoneLegend::MoveCursor, 2);
  {
    // Show the cursor drawn at two sample spots: over empty canvas and the pasteboard.
    QPainter painter(&map);
    for (const auto spot : {over_background, pasteboard}) {
      hover(*canvas, spot);
      const auto cursor = canvas->cursor();
      if (cursor.shape() == Qt::BitmapCursor) {
        painter.drawPixmap(spot - (origin - QPoint(40, 30)) - cursor.hotSpot(), cursor.pixmap());
      }
    }
  }
  save_image_artifact("ui_move_cursor_map", scaled_up(map, 2));

  hover(*canvas, over_layer);
  CHECK(canvas->cursor().shape() == Qt::SizeAllCursor);
  hover(*canvas, over_background);
  CHECK(canvas->cursor().shape() == Qt::BitmapCursor);
  CHECK(!is_rotate_cursor(canvas->cursor()));
  const auto pointer_image = canvas->cursor().pixmap().toImage();
  hover(*canvas, pasteboard);
  CHECK(canvas->cursor().shape() == Qt::BitmapCursor);
  CHECK(canvas->cursor().pixmap().toImage() == pointer_image);

  // The pointer's promise holds: a drag from empty canvas draws a selection
  // rectangle (matching nothing, it deselects) and leaves the layer put.
  drag(*canvas, over_background, over_background + QPoint(30, -20));
  QApplication::processEvents();
  const auto* box = patchy::ui::MainWindowTestAccess::document(window).find_layer(id);
  CHECK(box != nullptr && box->bounds().x == bounds.x() && box->bounds().y == bounds.y());

  // Auto-Select off: a press anywhere drags the selected layer.
  send_mouse(*canvas, QEvent::MouseButtonPress, over_layer, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, over_layer, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(canvas->active_layer_document_rect() == bounds);
  canvas->set_auto_select_layer(false);
  hover(*canvas, over_background);
  CHECK(canvas->cursor().shape() == Qt::SizeAllCursor);
  hover(*canvas, pasteboard);
  CHECK(canvas->cursor().shape() == Qt::SizeAllCursor);
}

}  // namespace

std::vector<patchy::test::TestCase> move_transform_zones_tests() {
  return {
      {"ui_transform_zone_passive_edges_resize_and_corners_rotate",
       ui_transform_zone_passive_edges_resize_and_corners_rotate},
      {"ui_transform_zone_rotated_session_edges_follow_the_box", ui_transform_zone_rotated_session_edges_follow_the_box},
      {"ui_transform_zone_tiny_box_keeps_move_area", ui_transform_zone_tiny_box_keeps_move_area},
      {"ui_transform_zone_crop_edges_resize", ui_transform_zone_crop_edges_resize},
      {"ui_move_cursor_shows_drag_only_where_a_press_moves", ui_move_cursor_shows_drag_only_where_a_press_moves},
  };
}
