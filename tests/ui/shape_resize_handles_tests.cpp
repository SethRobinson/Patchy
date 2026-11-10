// The shape tools' bounding-box resize handles (GitHub issue 690): with a shape tool
// active on an editable shape layer the box of the shape draws with eight handles, a drag
// of a handle resizes the shape as one undo step and keeps live shapes live, Shift and Alt
// constrain it, Esc cancels it, and a press anywhere else still draws a new shape.
#include "core/layer_metadata.hpp"
#include "core/vector_live_shapes.hpp"
#include "core/vector_shape.hpp"
#include "test_harness.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/main_window.hpp"
#include "ui_test_access.hpp"
#include "ui_test_support.hpp"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCursor>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QPoint>
#include <QPointF>
#include <QRectF>
#include <QSpinBox>
#include <Qt>

#include <cmath>
#include <optional>
#include <utility>
#include <vector>

namespace {

using namespace patchy::test::ui;

void shape_drag(patchy::ui::CanvasWidget& canvas, QPoint document_from, QPoint document_to,
                Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  drag(canvas, canvas.widget_position_for_document_point(document_from),
       canvas.widget_position_for_document_point(document_to), modifiers);
  QApplication::processEvents();
}

bool rect_near(const std::optional<QRectF>& rect, QRectF want, double tolerance = 0.6) {
  return rect.has_value() && std::abs(rect->left() - want.left()) <= tolerance &&
         std::abs(rect->top() - want.top()) <= tolerance && std::abs(rect->right() - want.right()) <= tolerance &&
         std::abs(rect->bottom() - want.bottom()) <= tolerance;
}

std::optional<QRectF> shape_bounds(const patchy::Document& document, patchy::LayerId id) {
  const auto* layer = document.find_layer(id);
  const auto* shape = layer != nullptr ? layer->vector_shape() : nullptr;
  const auto bounds = shape != nullptr ? shape->path.bounds() : std::nullopt;
  if (!bounds.has_value()) {
    return std::nullopt;
  }
  return QRectF(QPointF(bounds->left, bounds->top), QPointF(bounds->right, bounds->bottom));
}

// Whether the layer's one shape is still what its live parameters generate.
std::optional<patchy::LiveShapeParams> live_shape(const patchy::Document& document, patchy::LayerId id) {
  const auto* layer = document.find_layer(id);
  const auto* shape = layer != nullptr ? layer->vector_shape() : nullptr;
  if (shape == nullptr || shape->path.subpaths.empty()) {
    return std::nullopt;
  }
  return patchy::resolve_live_shape_group(shape->path, shape->origination, shape->path.subpaths.front().shape_group);
}

// The Rectangle tool in Shape mode on a quiet canvas: no snapping, square corners.
void use_rectangle_tool(patchy::ui::MainWindow& window, patchy::ui::CanvasWidget& canvas, int radius = 0) {
  require_action(window, "toolRectAction")->trigger();
  QApplication::processEvents();
  canvas.setFocus(Qt::MouseFocusReason);
  canvas.set_snap_enabled(false);
  auto* radius_spin = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  CHECK(radius_spin != nullptr);
  if (radius_spin != nullptr) {
    radius_spin->setValue(radius);
  }
  QApplication::processEvents();
}

void ui_shape_tool_corner_handle_resizes_live_rectangle_in_one_undo_step() {
  VectorSettingsGuard settings_guard;
  auto&& [window, canvas] = ShownWindowFixture{};
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto initial_layers = document.layers().size();

  use_rectangle_tool(window, *canvas);
  shape_drag(*canvas, QPoint(120, 140), QPoint(320, 260));
  CHECK(document.layers().size() == initial_layers + 1);
  const auto layer_id = *document.active_layer_id();
  CHECK(live_shape(document, layer_id).has_value());

  // The drawn shape shows its box at once, under the tool that drew it.
  CHECK(rect_near(canvas->shape_resize_box_rect(), QRectF(120.0, 140.0, 200.0, 120.0)));
  save_widget_artifact("ui_issue_690_shape_handles_after_draw", *canvas);

  auto* options_width = window.findChild<QDoubleSpinBox*>(QStringLiteral("vectorShapeWidthSpin"));
  auto* options_height = window.findChild<QDoubleSpinBox*>(QStringLiteral("vectorShapeHeightSpin"));
  CHECK(options_width != nullptr && options_height != nullptr);
  CHECK(std::abs(options_width->value() - 200.0) < 0.5);

  // Over the corner handle the cursor is the corner's resize cursor.
  const auto corner = canvas->widget_position_for_document_point(QPoint(320, 260));
  const auto target = canvas->widget_position_for_document_point(QPoint(380, 300));
  send_mouse(*canvas, QEvent::MouseMove, corner, Qt::NoButton, Qt::NoButton);
  CHECK(canvas->cursor().shape() == Qt::SizeFDiagCursor);

  send_mouse(*canvas, QEvent::MouseButtonPress, corner, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, target, Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  // Mid-drag: the shape, the box and the W / H fields follow, and nothing new was added.
  CHECK(rect_near(shape_bounds(document, layer_id), QRectF(120.0, 140.0, 260.0, 160.0)));
  CHECK(rect_near(canvas->shape_resize_box_rect(), QRectF(120.0, 140.0, 260.0, 160.0)));
  CHECK(std::abs(options_width->value() - 260.0) < 0.5);
  CHECK(std::abs(options_height->value() - 160.0) < 0.5);
  CHECK(document.layers().size() == initial_layers + 1);
  send_mouse(*canvas, QEvent::MouseButtonRelease, target, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  CHECK(document.layers().size() == initial_layers + 1);
  CHECK(document.active_layer_id() == layer_id);
  CHECK(rect_near(shape_bounds(document, layer_id), QRectF(120.0, 140.0, 260.0, 160.0)));
  const auto* layer = std::as_const(document).find_layer(layer_id);
  CHECK(layer != nullptr);
  if (layer != nullptr) {
    CHECK(layer->bounds().x == 120 && layer->bounds().y == 140);
    CHECK(layer->bounds().width == 260 && layer->bounds().height == 160);
    const auto* content = layer->vector_shape();
    CHECK(content != nullptr && content->path.subpaths.size() == 1 && content->path.subpaths[0].anchors.size() == 4);
    CHECK(content != nullptr && content->origination.size() == 1 &&
          content->origination[0].kind == patchy::LiveShapeKind::Rectangle);
  }
  // Still a live rectangle, now at the new size.
  const auto live = live_shape(document, layer_id);
  CHECK(live.has_value());
  if (live.has_value()) {
    CHECK(std::abs(live->right - 380.0) < 0.5 && std::abs(live->bottom - 300.0) < 0.5);
  }
  CHECK(color_close(canvas_pixel(*canvas, QPoint(370, 290)), Qt::black, 8));
  CHECK(rect_near(canvas->shape_resize_box_rect(), QRectF(120.0, 140.0, 260.0, 160.0)));
  CHECK(std::abs(options_width->value() - 260.0) < 0.5);
  save_widget_artifact("ui_issue_690_shape_handles_after_resize", *canvas);

  // One undo puts the drawn size back; the next one removes the rectangle.
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(document.layers().size() == initial_layers + 1);
  CHECK(rect_near(shape_bounds(document, layer_id), QRectF(120.0, 140.0, 200.0, 120.0)));
  CHECK(live_shape(document, layer_id).has_value());
  CHECK(color_close(canvas_pixel(*canvas, QPoint(370, 290)), Qt::white, 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(220, 200)), Qt::black, 8));
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(document.layers().size() == initial_layers);
}

void ui_shape_tool_drag_away_from_handles_draws_a_new_shape() {
  VectorSettingsGuard settings_guard;
  auto&& [window, canvas] = ShownWindowFixture{};
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto initial_layers = document.layers().size();

  use_rectangle_tool(window, *canvas);
  shape_drag(*canvas, QPoint(100, 100), QPoint(220, 180));
  const auto first_id = *document.active_layer_id();
  CHECK(document.layers().size() == initial_layers + 1);

  // Away from the handles the tool keeps its own cursor.
  send_mouse(*canvas, QEvent::MouseMove, canvas->widget_position_for_document_point(QPoint(280, 130)), Qt::NoButton,
             Qt::NoButton);
  const auto idle_cursor = canvas->cursor().shape();
  CHECK(idle_cursor != Qt::SizeFDiagCursor && idle_cursor != Qt::SizeBDiagCursor && idle_cursor != Qt::SizeHorCursor &&
        idle_cursor != Qt::SizeVerCursor);

  // A drag beside the shape draws a second one and leaves the first alone.
  shape_drag(*canvas, QPoint(260, 120), QPoint(340, 200));
  CHECK(document.layers().size() == initial_layers + 2);
  const auto second_id = *document.active_layer_id();
  CHECK(second_id != first_id);
  CHECK(rect_near(shape_bounds(document, first_id), QRectF(100.0, 100.0, 120.0, 80.0)));
  CHECK(rect_near(shape_bounds(document, second_id), QRectF(260.0, 120.0, 80.0, 80.0)));
  // The box moved to the new shape.
  CHECK(rect_near(canvas->shape_resize_box_rect(), QRectF(260.0, 120.0, 80.0, 80.0)));

  // So does a drag that starts inside the active shape's box, off its handles.
  shape_drag(*canvas, QPoint(285, 150), QPoint(320, 185));
  CHECK(document.layers().size() == initial_layers + 3);
  CHECK(rect_near(shape_bounds(document, second_id), QRectF(260.0, 120.0, 80.0, 80.0)));
  CHECK(rect_near(shape_bounds(document, *document.active_layer_id()), QRectF(285.0, 150.0, 35.0, 35.0)));

  // A click on a handle with no travel changes nothing and draws nothing.
  const auto third_id = *document.active_layer_id();
  const auto handle = canvas->widget_position_for_document_point(QPoint(320, 185));
  send_mouse(*canvas, QEvent::MouseButtonPress, handle, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, handle, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(document.layers().size() == initial_layers + 3);
  CHECK(rect_near(shape_bounds(document, third_id), QRectF(285.0, 150.0, 35.0, 35.0)));
  // Its undo step is still the draw: one undo removes the third rectangle.
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(document.layers().size() == initial_layers + 2);
}

void ui_shape_tool_handle_shift_alt_and_escape() {
  VectorSettingsGuard settings_guard;
  auto&& [window, canvas] = ShownWindowFixture{};
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto initial_layers = document.layers().size();

  use_rectangle_tool(window, *canvas);
  shape_drag(*canvas, QPoint(120, 140), QPoint(320, 260));
  const auto layer_id = *document.active_layer_id();
  const auto widget = [&canvas](int x, int y) { return canvas->widget_position_for_document_point(QPoint(x, y)); };

  // Shift on a corner keeps the proportions (200 : 120), whichever way the pointer leans.
  shape_drag(*canvas, QPoint(320, 260), QPoint(420, 280), Qt::ShiftModifier);
  auto bounds = shape_bounds(document, layer_id);
  CHECK(bounds.has_value());
  if (bounds.has_value()) {
    CHECK(std::abs(bounds->left() - 120.0) < 0.01 && std::abs(bounds->top() - 140.0) < 0.01);
    CHECK(bounds->width() > 250.0);
    CHECK(std::abs(bounds->width() / bounds->height() - 200.0 / 120.0) < 0.01);
  }
  CHECK(live_shape(document, layer_id).has_value());
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(rect_near(shape_bounds(document, layer_id), QRectF(120.0, 140.0, 200.0, 120.0)));

  // Alt resizes about the center: the left side mirrors the dragged right side.
  shape_drag(*canvas, QPoint(320, 200), QPoint(350, 200), Qt::AltModifier);
  CHECK(rect_near(shape_bounds(document, layer_id), QRectF(90.0, 140.0, 260.0, 120.0)));
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(rect_near(shape_bounds(document, layer_id), QRectF(120.0, 140.0, 200.0, 120.0)));

  // A side handle changes one dimension; a handle never crosses its opposite side.
  shape_drag(*canvas, QPoint(220, 140), QPoint(220, 100));
  CHECK(rect_near(shape_bounds(document, layer_id), QRectF(120.0, 100.0, 200.0, 160.0)));
  shape_drag(*canvas, QPoint(120, 180), QPoint(500, 180));
  bounds = shape_bounds(document, layer_id);
  CHECK(bounds.has_value() && bounds->width() >= 0.99 && bounds->width() <= 1.5 &&
        std::abs(bounds->right() - 320.0) < 0.01);
  CHECK(live_shape(document, layer_id).has_value());
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(rect_near(shape_bounds(document, layer_id), QRectF(120.0, 140.0, 200.0, 120.0)));

  // Esc mid-drag puts the shape back and leaves no history step.
  send_mouse(*canvas, QEvent::MouseButtonPress, widget(320, 260), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, widget(400, 330), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  CHECK(rect_near(shape_bounds(document, layer_id), QRectF(120.0, 140.0, 280.0, 190.0)));
  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(rect_near(shape_bounds(document, layer_id), QRectF(120.0, 140.0, 200.0, 120.0)));
  CHECK(rect_near(canvas->shape_resize_box_rect(), QRectF(120.0, 140.0, 200.0, 120.0)));
  send_mouse(*canvas, QEvent::MouseButtonRelease, widget(400, 330), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(rect_near(shape_bounds(document, layer_id), QRectF(120.0, 140.0, 200.0, 120.0)));
  CHECK(document.layers().size() == initial_layers + 1);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(380, 310)), Qt::white, 8));
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(document.layers().size() == initial_layers);
}

void ui_shape_tool_handles_keep_rounded_rectangles_ellipses_and_polygons_live() {
  VectorSettingsGuard settings_guard;
  SettingsValueRestorer saved_sides(QStringLiteral("tools/polygonSides"));
  SettingsValueRestorer saved_inset(QStringLiteral("tools/polygonStarInset"));
  auto&& [window, canvas] = ShownWindowFixture{};
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  // A rounded rectangle widened by a side handle keeps round corners: it is rebuilt from
  // its scaled parameters, so it is still the live shape its Radius field edits.
  use_rectangle_tool(window, *canvas, 20);
  shape_drag(*canvas, QPoint(100, 100), QPoint(300, 200));
  const auto rounded_id = *document.active_layer_id();
  const auto drawn = live_shape(document, rounded_id);
  CHECK(drawn.has_value() && drawn->kind == patchy::LiveShapeKind::RoundedRectangle);
  shape_drag(*canvas, QPoint(300, 150), QPoint(400, 150));
  CHECK(rect_near(shape_bounds(document, rounded_id), QRectF(100.0, 100.0, 300.0, 100.0)));
  const auto widened = live_shape(document, rounded_id);
  CHECK(widened.has_value());
  if (widened.has_value()) {
    CHECK(widened->kind == patchy::LiveShapeKind::RoundedRectangle);
    CHECK(std::abs(widened->right - 400.0) < 0.5);
    // The radii follow the mean of the two scale factors (1.5 and 1).
    CHECK(std::abs(widened->corner_radii[0] - 25.0) < 0.01);
  }
  // Round means the top edge's first knot sits one radius in from the corner.
  if (const auto* layer = std::as_const(document).find_layer(rounded_id); layer != nullptr) {
    const auto& anchors = layer->vector_shape()->path.subpaths.front().anchors;
    CHECK(anchors.size() == 8);
    CHECK(!anchors.empty() && std::abs(anchors.front().anchor_x - 125.0) < 0.01 &&
          std::abs(anchors.front().anchor_y - 100.0) < 0.01);
  }

  // An ellipse stays a live ellipse.
  canvas->set_tool(patchy::ui::CanvasTool::Ellipse);
  shape_drag(*canvas, QPoint(100, 260), QPoint(220, 340));
  const auto ellipse_id = *document.active_layer_id();
  CHECK(ellipse_id != rounded_id);
  CHECK(rect_near(canvas->shape_resize_box_rect(), QRectF(100.0, 260.0, 120.0, 80.0)));
  shape_drag(*canvas, QPoint(220, 340), QPoint(280, 360));
  CHECK(rect_near(shape_bounds(document, ellipse_id), QRectF(100.0, 260.0, 180.0, 100.0)));
  const auto ellipse = live_shape(document, ellipse_id);
  CHECK(ellipse.has_value() && ellipse->kind == patchy::LiveShapeKind::Ellipse);

  // A polygon keeps its side count.
  canvas->set_tool(patchy::ui::CanvasTool::Polygon);
  auto* sides = window.findChild<QSpinBox*>(QStringLiteral("polygonSidesSpin"));
  auto* inset = window.findChild<QSpinBox*>(QStringLiteral("polygonStarInsetSpin"));
  CHECK(sides != nullptr && inset != nullptr);
  if (sides != nullptr && inset != nullptr) {
    sides->setValue(6);
    inset->setValue(0);
  }
  shape_drag(*canvas, QPoint(480, 300), QPoint(480, 240));
  const auto polygon_id = *document.active_layer_id();
  CHECK(polygon_id != ellipse_id);
  const auto polygon_box = canvas->shape_resize_box_rect();
  CHECK(polygon_box.has_value());
  const auto polygon_before = live_shape(document, polygon_id);
  CHECK(polygon_before.has_value() && polygon_before->kind == patchy::LiveShapeKind::Polygon);
  if (polygon_box.has_value()) {
    const QPoint grab(static_cast<int>(std::lround(polygon_box->right())),
                      static_cast<int>(std::lround(polygon_box->bottom())));
    shape_drag(*canvas, grab, grab + QPoint(30, 20));
    const auto resized = shape_bounds(document, polygon_id);
    CHECK(resized.has_value() && std::abs(resized->left() - polygon_box->left()) < 0.01 &&
          std::abs(resized->top() - polygon_box->top()) < 0.01);
    CHECK(resized.has_value() && std::abs(resized->right() - (grab.x() + 30)) < 0.01 &&
          std::abs(resized->bottom() - (grab.y() + 20)) < 0.01);
  }
  const auto polygon_after = live_shape(document, polygon_id);
  CHECK(polygon_after.has_value());
  if (polygon_after.has_value() && polygon_before.has_value()) {
    CHECK(polygon_after->kind == patchy::LiveShapeKind::Polygon);
    CHECK(polygon_after->polygon_sides == polygon_before->polygon_sides);
  }
  if (const auto* layer = std::as_const(document).find_layer(polygon_id); layer != nullptr) {
    CHECK(layer->vector_shape()->path.subpaths.front().anchors.size() == 6);
  }
}

void ui_shape_tool_handles_show_only_for_an_editable_shape_layer() {
  VectorSettingsGuard settings_guard;
  auto&& [window, canvas] = ShownWindowFixture{};
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  use_rectangle_tool(window, *canvas);
  // No shape layer yet: nothing to grab.
  CHECK(!canvas->shape_resize_box_rect().has_value());
  std::optional<patchy::LayerId> pixel_layer = document.active_layer_id();
  shape_drag(*canvas, QPoint(120, 140), QPoint(320, 260));
  const auto shape_id = *document.active_layer_id();
  const QRectF box(120.0, 140.0, 200.0, 120.0);
  CHECK(rect_near(canvas->shape_resize_box_rect(), box));

  // Every shape tool shows it; Move and Path Select have their own controls.
  for (const auto tool : {patchy::ui::CanvasTool::Ellipse, patchy::ui::CanvasTool::Line,
                          patchy::ui::CanvasTool::Polygon, patchy::ui::CanvasTool::CustomShape,
                          patchy::ui::CanvasTool::Rectangle}) {
    canvas->set_tool(tool);
    CHECK(rect_near(canvas->shape_resize_box_rect(), box));
  }
  for (const auto tool : {patchy::ui::CanvasTool::Move, patchy::ui::CanvasTool::PathSelect,
                          patchy::ui::CanvasTool::Brush}) {
    canvas->set_tool(tool);
    CHECK(!canvas->shape_resize_box_rect().has_value());
  }
  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  CHECK(rect_near(canvas->shape_resize_box_rect(), box));

  // Path and Pixels modes draw no shape layer, so they show no box.
  canvas->set_vector_tool_mode(patchy::ui::VectorToolMode::Path);
  CHECK(!canvas->shape_resize_box_rect().has_value());
  canvas->set_vector_tool_mode(patchy::ui::VectorToolMode::Pixels);
  CHECK(!canvas->shape_resize_box_rect().has_value());
  canvas->set_vector_tool_mode(patchy::ui::VectorToolMode::Shape);
  CHECK(rect_near(canvas->shape_resize_box_rect(), box));

  // While a new shape is being dragged out the box is hidden.
  const auto from = canvas->widget_position_for_document_point(QPoint(400, 150));
  const auto to = canvas->widget_position_for_document_point(QPoint(460, 210));
  send_mouse(*canvas, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);
  CHECK(!canvas->shape_resize_box_rect().has_value());
  send_key(*canvas, Qt::Key_Escape);
  send_mouse(*canvas, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  // Whatever that drag left, the first rectangle is selected again for the checks below.
  patchy::ui::MainWindowTestAccess::select_layers(window, {shape_id}, shape_id);
  QApplication::processEvents();
  CHECK(rect_near(canvas->shape_resize_box_rect(), box));

  // A hidden or locked shape layer has no handles, and a handle press there draws instead.
  auto* layer = document.find_layer(shape_id);
  CHECK(layer != nullptr);
  if (layer != nullptr) {
    layer->set_visible(false);
    CHECK(!canvas->shape_resize_box_rect().has_value());
    layer->set_visible(true);
    CHECK(rect_near(canvas->shape_resize_box_rect(), box));
    patchy::set_layer_locks_position(*layer, true);
    CHECK(!canvas->shape_resize_box_rect().has_value());
    patchy::set_layer_locks_position(*layer, false);
    patchy::set_layer_locked(*layer, true);
    CHECK(!canvas->shape_resize_box_rect().has_value());
    patchy::set_layer_locked(*layer, false);
    CHECK(rect_near(canvas->shape_resize_box_rect(), box));
  }

  // Another layer active, or several layers selected: no box.
  if (pixel_layer.has_value() && *pixel_layer != shape_id) {
    patchy::ui::MainWindowTestAccess::select_layers(window, {*pixel_layer}, *pixel_layer);
    QApplication::processEvents();
    CHECK(!canvas->shape_resize_box_rect().has_value());
    patchy::ui::MainWindowTestAccess::select_layers(window, {shape_id, *pixel_layer}, shape_id);
    QApplication::processEvents();
    CHECK(!canvas->shape_resize_box_rect().has_value());
    patchy::ui::MainWindowTestAccess::select_layers(window, {shape_id}, shape_id);
    QApplication::processEvents();
    CHECK(rect_near(canvas->shape_resize_box_rect(), box));
  }

  // View > Extras hides the box with the other overlays, and a hidden box takes no grab.
  canvas->set_extras_visible(false);
  CHECK(!canvas->shape_resize_box_rect().has_value());
  canvas->set_extras_visible(true);
  CHECK(rect_near(canvas->shape_resize_box_rect(), box));
}

}  // namespace

std::vector<patchy::test::TestCase> shape_resize_handles_tests() {
  return {
      {"ui_shape_tool_corner_handle_resizes_live_rectangle_in_one_undo_step",
       ui_shape_tool_corner_handle_resizes_live_rectangle_in_one_undo_step},
      {"ui_shape_tool_drag_away_from_handles_draws_a_new_shape",
       ui_shape_tool_drag_away_from_handles_draws_a_new_shape},
      {"ui_shape_tool_handle_shift_alt_and_escape", ui_shape_tool_handle_shift_alt_and_escape},
      {"ui_shape_tool_handles_keep_rounded_rectangles_ellipses_and_polygons_live",
       ui_shape_tool_handles_keep_rounded_rectangles_ellipses_and_polygons_live},
      {"ui_shape_tool_handles_show_only_for_an_editable_shape_layer",
       ui_shape_tool_handles_show_only_for_an_editable_shape_layer},
  };
}
