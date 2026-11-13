// The shape tools' bounding-box resize handles (GitHub issue 690): with a shape tool
// active on an editable shape layer the box of the shape draws with eight handles, a drag
// of a handle resizes the shape as one undo step and keeps live shapes live, Shift and Alt
// constrain it, Esc cancels it, and a press anywhere else still draws a new shape.
#include "core/layer_metadata.hpp"
#include "core/vector_live_shapes.hpp"
#include "core/vector_shape.hpp"
#include "psd/psd_document_io.hpp"
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
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QPoint>
#include <QPointF>
#include <QPushButton>
#include <QRectF>
#include <QSpinBox>
#include <Qt>

#include <cmath>
#include <filesystem>
#include <optional>
#include <string>
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

// The work path's bounds, as a rect.
std::optional<QRectF> work_path_bounds(const patchy::Document& document) {
  const auto* work = document.work_path();
  const auto bounds = work != nullptr ? work->path().bounds() : std::nullopt;
  if (!bounds.has_value()) {
    return std::nullopt;
  }
  return QRectF(QPointF(bounds->left, bounds->top), QPointF(bounds->right, bounds->bottom));
}

// Whether the work path's last component is still what its live parameters generate.
std::optional<patchy::LiveShapeParams> work_path_live_shape(const patchy::Document& document) {
  const auto* work = document.work_path();
  if (work == nullptr || work->path().subpaths.empty()) {
    return std::nullopt;
  }
  return patchy::resolve_live_shape_group(work->path(), work->live_shapes(),
                                          work->path().subpaths.back().shape_group);
}

void use_vector_mode(patchy::ui::MainWindow& window, int index) {
  auto* mode = window.findChild<QComboBox*>(QStringLiteral("vectorModeCombo"));
  CHECK(mode != nullptr);
  if (mode != nullptr) {
    mode->setCurrentIndex(index);  // 0 Shape, 1 Path, 2 Pixels
  }
  QApplication::processEvents();
}

// A window shot with the canvas's current cursor painted where the pointer is: a grab shows
// no pointer, and these shots are about the rotate cursor.
void save_window_shot_with_cursor(const std::string& name, QWidget& window, patchy::ui::CanvasWidget& canvas,
                                  QPoint canvas_point) {
  auto image = capture_widget_artifact(name, window);
  const auto cursor = canvas.cursor();
  const auto pixmap = cursor.pixmap();
  if (image.isNull() || pixmap.isNull()) {
    return;
  }
  QPainter painter(&image);
  painter.drawPixmap(canvas.mapTo(&window, canvas_point) - cursor.hotSpot(), pixmap);
  painter.end();
  save_image_artifact(name, image);
}

// Where a rotate drag of `box` presses (10 px outside its bottom right corner, in the
// rotate band) and where the pointer is after a turn of `degrees` about the box's center.
struct RotateDrag {
  QPoint press;
  QPoint target;
};
RotateDrag rotate_drag_points(patchy::ui::CanvasWidget& canvas, QRectF box, double degrees) {
  const QPointF center(canvas.widget_position_for_document_point(box.center().toPoint()));
  const QPointF press(canvas.widget_position_for_document_point(box.bottomRight().toPoint()) + QPoint(10, 10));
  const double radians = degrees * 3.14159265358979323846 / 180.0;
  const auto arm = press - center;
  const QPointF target(center.x() + (arm.x() * std::cos(radians)) - (arm.y() * std::sin(radians)),
                       center.y() + (arm.x() * std::sin(radians)) + (arm.y() * std::cos(radians)));
  return {press.toPoint(), target.toPoint()};
}

// A whole rotate drag with Shift held, so the turn is exactly `degrees` (a multiple of 15).
void rotate_drag(patchy::ui::CanvasWidget& canvas, QRectF box, double degrees) {
  const auto points = rotate_drag_points(canvas, box, degrees);
  drag(canvas, points.press, points.target, Qt::ShiftModifier);
  QApplication::processEvents();
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
  // As drawn it shows its two drag points (the tests below); stretched by a W / H edit it
  // is no longer what the drawing drag makes and shows the box.
  CHECK(!canvas->shape_resize_box_rect().has_value());
  if (auto* polygon_layer = document.find_layer(polygon_id); polygon_layer != nullptr) {
    auto content = *std::as_const(*polygon_layer).vector_shape();
    auto params = content.origination.front();
    params.right += 40.0;
    patchy::populate_live_shape_box_corners(params);
    CHECK(patchy::replace_live_shape_group(content.path, params));
    content.origination.front() = params;
    polygon_layer->set_vector_shape(std::move(content));
    QApplication::processEvents();
  }
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


// GitHub issue 690 follow-up: a live Polygon and a live Line show the two points of the
// drag that drew them in place of the box (canvas_widget_shape_points.cpp).

bool point_near(QPointF point, QPointF want, double tolerance = 0.6) {
  return std::abs(point.x() - want.x()) <= tolerance && std::abs(point.y() - want.y()) <= tolerance;
}

bool points_near(const patchy::ui::CanvasWidget& canvas, patchy::ui::CanvasWidget::ShapePointsKind kind,
                 QPointF first, QPointF second) {
  const auto points = canvas.shape_live_points();
  return points.has_value() && points->kind == kind && point_near(points->first, first) &&
         point_near(points->second, second);
}

// The Polygon tool in Shape mode on a quiet canvas.
void use_polygon_tool(patchy::ui::MainWindow& window, patchy::ui::CanvasWidget& canvas, int sides, int inset) {
  use_rectangle_tool(window, canvas);
  canvas.set_tool(patchy::ui::CanvasTool::Polygon);
  auto* sides_spin = window.findChild<QSpinBox*>(QStringLiteral("polygonSidesSpin"));
  auto* inset_spin = window.findChild<QSpinBox*>(QStringLiteral("polygonStarInsetSpin"));
  CHECK(sides_spin != nullptr && inset_spin != nullptr);
  if (sides_spin != nullptr && inset_spin != nullptr) {
    sides_spin->setValue(sides);
    inset_spin->setValue(inset);
  }
  QApplication::processEvents();
}

void ui_shape_tool_polygon_vertex_point_sets_size_and_angle_in_one_undo_step() {
  VectorSettingsGuard settings_guard;
  SettingsValueRestorer saved_sides(QStringLiteral("tools/polygonSides"));
  SettingsValueRestorer saved_inset(QStringLiteral("tools/polygonStarInset"));
  auto&& [window, canvas] = ShownWindowFixture{};
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto initial_layers = document.layers().size();
  using Kind = patchy::ui::CanvasWidget::ShapePointsKind;
  const auto widget = [&canvas](int x, int y) { return canvas->widget_position_for_document_point(QPoint(x, y)); };

  // A hexagon drawn from its center straight up: radius 60, first vertex at the top.
  use_polygon_tool(window, *canvas, 6, 0);
  shape_drag(*canvas, QPoint(300, 260), QPoint(300, 200));
  CHECK(document.layers().size() == initial_layers + 1);
  const auto layer_id = *document.active_layer_id();
  const auto drawn = live_shape(document, layer_id);
  CHECK(drawn.has_value() && drawn->kind == patchy::LiveShapeKind::Polygon);

  // It shows its center and its first vertex, and no box.
  CHECK(points_near(*canvas, Kind::Polygon, QPointF(300.0, 260.0), QPointF(300.0, 200.0)));
  CHECK(!canvas->shape_resize_box_rect().has_value());
  save_widget_artifact("ui_issue_690_polygon_points_after_draw", *canvas);

  auto* options_width = window.findChild<QDoubleSpinBox*>(QStringLiteral("vectorShapeWidthSpin"));
  CHECK(options_width != nullptr);

  // Dragging the vertex to the right of the center turns the hexagon a quarter turn and
  // grows it to radius 80, as a drawing drag from the center to there would.
  send_mouse(*canvas, QEvent::MouseButtonPress, widget(300, 200), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, widget(380, 260), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  // Mid-drag: the shape, the points and the W field follow, with no new layer.
  auto live = live_shape(document, layer_id);
  CHECK(live.has_value() && std::abs(live->right - 380.0) < 0.01 && std::abs(live->polygon_angle) < 1e-6);
  CHECK(points_near(*canvas, Kind::Polygon, QPointF(300.0, 260.0), QPointF(380.0, 260.0)));
  CHECK(options_width != nullptr && std::abs(options_width->value() - 160.0) < 0.5);
  CHECK(document.layers().size() == initial_layers + 1);
  send_mouse(*canvas, QEvent::MouseButtonRelease, widget(380, 260), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  CHECK(document.layers().size() == initial_layers + 1);
  CHECK(document.active_layer_id() == layer_id);
  live = live_shape(document, layer_id);
  CHECK(live.has_value());
  if (live.has_value()) {
    CHECK(live->kind == patchy::LiveShapeKind::Polygon);
    CHECK(live->polygon_sides == 6 && live->polygon_star_inset == 0);
    CHECK(std::abs(live->polygon_angle) < 1e-6);
    CHECK(std::abs(live->left - 220.0) < 0.01 && std::abs(live->right - 380.0) < 0.01);
    CHECK(std::abs(live->top - 180.0) < 0.01 && std::abs(live->bottom - 340.0) < 0.01);
  }
  if (const auto* layer = std::as_const(document).find_layer(layer_id); layer != nullptr) {
    const auto& anchors = layer->vector_shape()->path.subpaths.front().anchors;
    CHECK(anchors.size() == 6);
    CHECK(!anchors.empty() && std::abs(anchors.front().anchor_x - 380.0) < 0.01 &&
          std::abs(anchors.front().anchor_y - 260.0) < 0.01);
    CHECK(layer->vector_shape()->origination.size() == 1);
  }
  CHECK(points_near(*canvas, Kind::Polygon, QPointF(300.0, 260.0), QPointF(380.0, 260.0)));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(370, 260)), Qt::black, 8));
  save_widget_artifact("ui_issue_690_polygon_points_after_turn", *canvas);

  // The turned polygon saves and reopens as a live polygon at the same angle. The file is
  // kept as an artifact for a Photoshop open check (docs/photoshop-com.md).
  patchy::psd::DocumentIo::write_layered_rgb8_file(
      document, std::filesystem::path("test-artifacts") / "ui_issue_690_turned_polygon.psd");
  {
    const auto reread = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(document), {});
    const auto* content = reread.layers().back().vector_shape();
    CHECK(content != nullptr && content->origination.size() == 1);
    if (content != nullptr && content->origination.size() == 1) {
      const auto& found = content->origination.front();
      CHECK(found.kind == patchy::LiveShapeKind::Polygon && found.polygon_sides == 6);
      CHECK(std::abs(found.right - 380.0) < 0.01);
      CHECK(std::abs(std::remainder(found.polygon_angle, 6.283185307179586)) < 1e-4);
    }
  }

  // One undo puts the drawn hexagon back; the next one removes it.
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(document.layers().size() == initial_layers + 1);
  live = live_shape(document, layer_id);
  CHECK(live.has_value() && std::abs(live->right - 360.0) < 0.01 &&
        std::abs(live->polygon_angle + 1.5707963267948966) < 1e-6);
  CHECK(points_near(*canvas, Kind::Polygon, QPointF(300.0, 260.0), QPointF(300.0, 200.0)));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(370, 260)), Qt::white, 8));

  // With the view zoomed and turned the points stay on the same document positions, and a
  // drag puts the vertex on the document point under the pointer.
  canvas->set_zoom_centered(1.5);
  canvas->set_view_rotation(30.0);
  QApplication::processEvents();
  CHECK(points_near(*canvas, Kind::Polygon, QPointF(300.0, 260.0), QPointF(300.0, 200.0)));
  const auto turned_view = [&canvas](double x, double y) {
    const auto at = canvas->widget_position_f(QPointF(x, y));
    return QPoint(static_cast<int>(std::lround(at.x())), static_cast<int>(std::lround(at.y())));
  };
  drag(*canvas, turned_view(300.0, 200.0), turned_view(360.0, 260.0));
  QApplication::processEvents();
  live = live_shape(document, layer_id);
  CHECK(live.has_value() && std::abs(live->right - 360.0) < 1.5 && std::abs(live->polygon_angle) < 0.03);
  CHECK(live.has_value() && std::abs((live->left + live->right) / 2.0 - 300.0) < 1e-9);
  canvas->set_view_rotation(0.0);
  canvas->set_zoom_centered(1.0);
  QApplication::processEvents();
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  live = live_shape(document, layer_id);
  CHECK(live.has_value() && std::abs(live->polygon_angle + 1.5707963267948966) < 1e-6);

  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(document.layers().size() == initial_layers);
}

void ui_shape_tool_polygon_center_point_moves_and_escape_cancels() {
  VectorSettingsGuard settings_guard;
  SettingsValueRestorer saved_sides(QStringLiteral("tools/polygonSides"));
  SettingsValueRestorer saved_inset(QStringLiteral("tools/polygonStarInset"));
  auto&& [window, canvas] = ShownWindowFixture{};
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto initial_layers = document.layers().size();
  using Kind = patchy::ui::CanvasWidget::ShapePointsKind;
  const auto widget = [&canvas](int x, int y) { return canvas->widget_position_for_document_point(QPoint(x, y)); };

  // A five-pointed star: the points keep its Sides and Star inset.
  use_polygon_tool(window, *canvas, 5, 40);
  shape_drag(*canvas, QPoint(200, 200), QPoint(200, 140));
  const auto layer_id = *document.active_layer_id();
  const auto drawn = live_shape(document, layer_id);
  CHECK(drawn.has_value() && drawn->polygon_sides == 5 && drawn->polygon_star_inset == 40);
  CHECK(points_near(*canvas, Kind::Polygon, QPointF(200.0, 200.0), QPointF(200.0, 140.0)));

  // The center point moves the star and changes nothing else.
  shape_drag(*canvas, QPoint(200, 200), QPoint(240, 230));
  auto live = live_shape(document, layer_id);
  CHECK(live.has_value());
  if (live.has_value() && drawn.has_value()) {
    CHECK(std::abs(live->left - (drawn->left + 40.0)) < 0.01 && std::abs(live->top - (drawn->top + 30.0)) < 0.01);
    CHECK(std::abs((live->right - live->left) - 120.0) < 0.01 && std::abs((live->bottom - live->top) - 120.0) < 0.01);
    CHECK(std::abs(live->polygon_angle - drawn->polygon_angle) < 1e-9);
    CHECK(live->polygon_sides == 5 && live->polygon_star_inset == 40);
  }
  CHECK(points_near(*canvas, Kind::Polygon, QPointF(240.0, 230.0), QPointF(240.0, 170.0)));
  CHECK(document.layers().size() == initial_layers + 1);
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(points_near(*canvas, Kind::Polygon, QPointF(200.0, 200.0), QPointF(200.0, 140.0)));

  // The vertex point keeps the star a star.
  shape_drag(*canvas, QPoint(200, 140), QPoint(200, 110));
  live = live_shape(document, layer_id);
  CHECK(live.has_value() && live->polygon_sides == 5 && live->polygon_star_inset == 40 &&
        std::abs(live->top - 110.0) < 0.01 && std::abs(live->bottom - 290.0) < 0.01);
  if (const auto* layer = std::as_const(document).find_layer(layer_id); layer != nullptr) {
    CHECK(layer->vector_shape()->path.subpaths.front().anchors.size() == 10);
  }
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();

  // Esc mid-drag puts the star back and leaves no history step.
  send_mouse(*canvas, QEvent::MouseButtonPress, widget(200, 140), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, widget(280, 200), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  live = live_shape(document, layer_id);
  CHECK(live.has_value() && std::abs(live->right - 280.0) < 0.01);
  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(points_near(*canvas, Kind::Polygon, QPointF(200.0, 200.0), QPointF(200.0, 140.0)));
  send_mouse(*canvas, QEvent::MouseButtonRelease, widget(280, 200), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  live = live_shape(document, layer_id);
  CHECK(live.has_value() && drawn.has_value() && *live == *drawn);
  CHECK(document.layers().size() == initial_layers + 1);

  // A click on a point with no travel changes nothing and draws nothing.
  if (canvas->shape_live_points().has_value()) {
    send_mouse(*canvas, QEvent::MouseButtonPress, widget(200, 140), Qt::LeftButton, Qt::LeftButton);
    send_mouse(*canvas, QEvent::MouseButtonRelease, widget(200, 140), Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
  }
  live = live_shape(document, layer_id);
  CHECK(live.has_value() && drawn.has_value() && *live == *drawn);
  CHECK(document.layers().size() == initial_layers + 1);

  // A drag that starts off the two points, inside the star included, draws a new polygon.
  shape_drag(*canvas, QPoint(215, 222), QPoint(215, 252));
  CHECK(document.layers().size() == initial_layers + 2);
  const auto second_id = *document.active_layer_id();
  CHECK(second_id != layer_id);
  live = live_shape(document, layer_id);
  CHECK(live.has_value() && drawn.has_value() && *live == *drawn);
  CHECK(points_near(*canvas, Kind::Polygon, QPointF(215.0, 222.0), QPointF(215.0, 252.0)));
  // Its undo step is the draw; the one before it is the first star's draw (Esc and the
  // click left none).
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(document.layers().size() == initial_layers + 1);
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(document.layers().size() == initial_layers);
}

void ui_shape_tool_line_end_points_re_aim_a_live_line() {
  VectorSettingsGuard settings_guard;
  SettingsValueRestorer arrow_start_setting(QStringLiteral("tools/lineArrowStart"));
  SettingsValueRestorer arrow_end_setting(QStringLiteral("tools/lineArrowEnd"));
  auto&& [window, canvas] = ShownWindowFixture{};
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto initial_layers = document.layers().size();
  using Kind = patchy::ui::CanvasWidget::ShapePointsKind;
  const auto widget = [&canvas](int x, int y) { return canvas->widget_position_for_document_point(QPoint(x, y)); };

  // A line of weight 8 with an arrowhead at its end.
  use_rectangle_tool(window, *canvas);
  require_action_by_text(window, QStringLiteral("Line"))->trigger();
  QApplication::processEvents();
  canvas->setFocus(Qt::MouseFocusReason);
  canvas->set_snap_enabled(false);
  auto* weight = window.findChild<QDoubleSpinBox*>(QStringLiteral("vectorLineWeightSpin"));
  auto* arrow_start = window.findChild<QPushButton*>(QStringLiteral("lineArrowStartButton"));
  auto* arrow_end = window.findChild<QPushButton*>(QStringLiteral("lineArrowEndButton"));
  CHECK(weight != nullptr && arrow_start != nullptr && arrow_end != nullptr);
  if (weight == nullptr || arrow_start == nullptr || arrow_end == nullptr) {
    return;
  }
  weight->setValue(8.0);
  QMetaObject::invokeMethod(weight, "editingFinished", Qt::DirectConnection);
  if (arrow_start->isChecked()) {
    arrow_start->click();
  }
  if (!arrow_end->isChecked()) {
    arrow_end->click();
  }
  QApplication::processEvents();

  shape_drag(*canvas, QPoint(100, 200), QPoint(300, 200));
  CHECK(document.layers().size() == initial_layers + 1);
  const auto layer_id = *document.active_layer_id();
  const auto drawn = live_shape(document, layer_id);
  CHECK(drawn.has_value() && drawn->kind == patchy::LiveShapeKind::Line);
  CHECK(drawn.has_value() && std::abs(drawn->line_weight - 8.0) < 1e-9 && drawn->arrow_end && !drawn->arrow_start);

  // It shows its two ends, and no box.
  CHECK(points_near(*canvas, Kind::Line, QPointF(100.0, 200.0), QPointF(300.0, 200.0)));
  CHECK(!canvas->shape_resize_box_rect().has_value());
  save_widget_artifact("ui_issue_690_line_points_after_draw", *canvas);

  // Dragging the end re-aims the line about its start; weight and arrowhead stay.
  send_mouse(*canvas, QEvent::MouseButtonPress, widget(300, 200), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, widget(260, 100), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  auto live = live_shape(document, layer_id);
  CHECK(live.has_value() && std::abs(live->line_end_x - 260.0) < 0.01 && std::abs(live->line_end_y - 100.0) < 0.01);
  CHECK(document.layers().size() == initial_layers + 1);
  send_mouse(*canvas, QEvent::MouseButtonRelease, widget(260, 100), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  live = live_shape(document, layer_id);
  CHECK(live.has_value());
  if (live.has_value() && drawn.has_value()) {
    CHECK(live->kind == patchy::LiveShapeKind::Line);
    CHECK(std::abs(live->line_start_x - 100.0) < 1e-9 && std::abs(live->line_start_y - 200.0) < 1e-9);
    CHECK(std::abs(live->line_end_x - 260.0) < 0.01 && std::abs(live->line_end_y - 100.0) < 0.01);
    CHECK(live->line_weight == drawn->line_weight);
    CHECK(live->arrow_end && !live->arrow_start);
    CHECK(live->arrow_width == drawn->arrow_width && live->arrow_length == drawn->arrow_length);
    // The box is the hull of the regenerated line: it holds both ends.
    CHECK(live->left <= 100.0 && live->right >= 260.0 && live->top <= 100.0 && live->bottom >= 200.0);
  }
  CHECK(points_near(*canvas, Kind::Line, QPointF(100.0, 200.0), QPointF(260.0, 100.0)));
  CHECK(document.layers().size() == initial_layers + 1);
  save_widget_artifact("ui_issue_690_line_points_after_re_aim", *canvas);

  // The re-aimed line saves and reopens as a live line (the file: as for the polygon).
  patchy::psd::DocumentIo::write_layered_rgb8_file(
      document, std::filesystem::path("test-artifacts") / "ui_issue_690_re_aimed_line.psd");
  {
    const auto reread = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(document), {});
    const auto* content = reread.layers().back().vector_shape();
    CHECK(content != nullptr && content->origination.size() == 1);
    if (content != nullptr && content->origination.size() == 1) {
      const auto found = patchy::resolve_live_shape_group(content->path, content->origination,
                                                          content->origination.front().index);
      CHECK(found.has_value() && found->kind == patchy::LiveShapeKind::Line);
      CHECK(found.has_value() && std::abs(found->line_end_x - 260.0) < 0.01 &&
            std::abs(found->line_end_y - 100.0) < 0.01 && found->arrow_end);
    }
  }

  // One undo puts the drawn line back.
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  live = live_shape(document, layer_id);
  CHECK(live.has_value() && drawn.has_value() && *live == *drawn);
  CHECK(points_near(*canvas, Kind::Line, QPointF(100.0, 200.0), QPointF(300.0, 200.0)));

  // The start point moves the other way round: the end stays.
  shape_drag(*canvas, QPoint(100, 200), QPoint(140, 260));
  live = live_shape(document, layer_id);
  CHECK(live.has_value() && std::abs(live->line_start_x - 140.0) < 0.01 &&
        std::abs(live->line_start_y - 260.0) < 0.01 && std::abs(live->line_end_x - 300.0) < 1e-9 &&
        std::abs(live->line_end_y - 200.0) < 1e-9);
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();

  // An end dropped on the other end would leave no line: the line stays as it was.
  shape_drag(*canvas, QPoint(300, 200), QPoint(100, 200));
  CHECK(document.layers().size() == initial_layers + 1);
  live = live_shape(document, layer_id);
  CHECK(live.has_value() && drawn.has_value() && *live == *drawn);

  // Esc mid-drag restores it, with no history step: one undo removes the line.
  send_mouse(*canvas, QEvent::MouseButtonPress, widget(300, 200), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, widget(300, 300), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  live = live_shape(document, layer_id);
  CHECK(live.has_value() && std::abs(live->line_end_y - 300.0) < 0.01);
  send_key(*canvas, Qt::Key_Escape);
  send_mouse(*canvas, QEvent::MouseButtonRelease, widget(300, 300), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  live = live_shape(document, layer_id);
  CHECK(live.has_value() && drawn.has_value() && *live == *drawn);

  // A drag away from the two ends draws a second line.
  shape_drag(*canvas, QPoint(120, 300), QPoint(260, 340));
  CHECK(document.layers().size() == initial_layers + 2);
  CHECK(*document.active_layer_id() != layer_id);
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(document.layers().size() == initial_layers);
}

void ui_shape_tool_points_show_only_for_a_live_regular_polygon_or_line() {
  VectorSettingsGuard settings_guard;
  SettingsValueRestorer saved_sides(QStringLiteral("tools/polygonSides"));
  SettingsValueRestorer saved_inset(QStringLiteral("tools/polygonStarInset"));
  auto&& [window, canvas] = ShownWindowFixture{};
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  using Kind = patchy::ui::CanvasWidget::ShapePointsKind;

  // Rectangles and ellipses keep the eight-handle box.
  use_rectangle_tool(window, *canvas);
  shape_drag(*canvas, QPoint(40, 40), QPoint(120, 100));
  CHECK(!canvas->shape_live_points().has_value());
  CHECK(rect_near(canvas->shape_resize_box_rect(), QRectF(40.0, 40.0, 80.0, 60.0)));
  canvas->set_tool(patchy::ui::CanvasTool::Ellipse);
  shape_drag(*canvas, QPoint(40, 140), QPoint(120, 200));
  CHECK(!canvas->shape_live_points().has_value());
  CHECK(rect_near(canvas->shape_resize_box_rect(), QRectF(40.0, 140.0, 80.0, 60.0)));

  use_polygon_tool(window, *canvas, 6, 0);
  std::optional<patchy::LayerId> other_layer = document.active_layer_id();
  shape_drag(*canvas, QPoint(300, 260), QPoint(300, 200));
  const auto polygon_id = *document.active_layer_id();
  const QPointF center(300.0, 260.0);
  const QPointF vertex(300.0, 200.0);
  const auto shows = [&] { return points_near(*canvas, Kind::Polygon, center, vertex); };
  CHECK(shows());

  // Every shape tool shows them; Move, Path Select and the paint tools do not.
  for (const auto tool : {patchy::ui::CanvasTool::Ellipse, patchy::ui::CanvasTool::Line,
                          patchy::ui::CanvasTool::Rectangle, patchy::ui::CanvasTool::CustomShape,
                          patchy::ui::CanvasTool::Polygon}) {
    canvas->set_tool(tool);
    CHECK(shows());
    CHECK(!canvas->shape_resize_box_rect().has_value());
  }
  for (const auto tool : {patchy::ui::CanvasTool::Move, patchy::ui::CanvasTool::PathSelect,
                          patchy::ui::CanvasTool::Brush}) {
    canvas->set_tool(tool);
    CHECK(!canvas->shape_live_points().has_value());
  }
  canvas->set_tool(patchy::ui::CanvasTool::Polygon);
  CHECK(shows());

  // The box's hide cases hide the points: Path and Pixels modes, a shape being dragged
  // out, a hidden or locked layer, several layers selected, View > Extras off.
  canvas->set_vector_tool_mode(patchy::ui::VectorToolMode::Path);
  CHECK(!canvas->shape_live_points().has_value());
  canvas->set_vector_tool_mode(patchy::ui::VectorToolMode::Pixels);
  CHECK(!canvas->shape_live_points().has_value());
  canvas->set_vector_tool_mode(patchy::ui::VectorToolMode::Shape);
  CHECK(shows());
  const auto from = canvas->widget_position_for_document_point(QPoint(420, 120));
  const auto to = canvas->widget_position_for_document_point(QPoint(460, 160));
  send_mouse(*canvas, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);
  CHECK(!canvas->shape_live_points().has_value());
  send_key(*canvas, Qt::Key_Escape);
  send_mouse(*canvas, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  patchy::ui::MainWindowTestAccess::select_layers(window, {polygon_id}, polygon_id);
  QApplication::processEvents();
  CHECK(shows());
  auto* layer = document.find_layer(polygon_id);
  CHECK(layer != nullptr);
  if (layer == nullptr) {
    return;
  }
  layer->set_visible(false);
  CHECK(!canvas->shape_live_points().has_value());
  layer->set_visible(true);
  patchy::set_layer_locks_position(*layer, true);
  CHECK(!canvas->shape_live_points().has_value());
  patchy::set_layer_locks_position(*layer, false);
  patchy::set_layer_locked(*layer, true);
  CHECK(!canvas->shape_live_points().has_value());
  patchy::set_layer_locked(*layer, false);
  CHECK(shows());
  if (other_layer.has_value() && *other_layer != polygon_id) {
    patchy::ui::MainWindowTestAccess::select_layers(window, {polygon_id, *other_layer}, polygon_id);
    QApplication::processEvents();
    CHECK(!canvas->shape_live_points().has_value());
    patchy::ui::MainWindowTestAccess::select_layers(window, {polygon_id}, polygon_id);
    QApplication::processEvents();
    CHECK(shows());
  }
  canvas->set_extras_visible(false);
  CHECK(!canvas->shape_live_points().has_value());
  canvas->set_extras_visible(true);
  CHECK(shows());

  // A polygon whose box is no longer square (a W / H edit stretched it) is not what the
  // drawing drag makes: it gets the eight-handle box, and stays live under it.
  // (The layer is looked up each time: an undo swaps the document's layers.)
  const auto stretch = [&](double extra_width) {
    auto* target = document.find_layer(polygon_id);
    CHECK(target != nullptr);
    if (target == nullptr) {
      return;
    }
    auto content = *std::as_const(*target).vector_shape();
    auto params = content.origination.front();
    params.right += extra_width;
    patchy::populate_live_shape_box_corners(params);
    CHECK(patchy::replace_live_shape_group(content.path, params));
    content.origination.front() = params;
    target->set_vector_shape(std::move(content));
    QApplication::processEvents();
  };
  stretch(60.0);
  CHECK(live_shape(document, polygon_id).has_value());
  CHECK(!canvas->shape_live_points().has_value());
  const auto stretched_box = canvas->shape_resize_box_rect();
  CHECK(stretched_box.has_value());
  if (stretched_box.has_value()) {
    const QPoint grab(static_cast<int>(std::lround(stretched_box->right())),
                      static_cast<int>(std::lround(stretched_box->bottom())));
    shape_drag(*canvas, grab, grab + QPoint(30, 20));
    const auto resized = live_shape(document, polygon_id);
    CHECK(resized.has_value() && resized->kind == patchy::LiveShapeKind::Polygon && resized->polygon_sides == 6);
    require_action_by_text(window, QStringLiteral("Undo"))->trigger();
    QApplication::processEvents();
  }
  stretch(-60.0);
  CHECK(shows());

  // A polygon with an edited point is a plain path: the box again.
  if (auto* target = document.find_layer(polygon_id); target != nullptr) {
    auto content = *std::as_const(*target).vector_shape();
    auto& anchor = content.path.subpaths.front().anchors[2];
    anchor.anchor_x += 9.0;
    anchor.in_x = anchor.out_x = anchor.anchor_x;
    target->set_vector_shape(std::move(content));
    QApplication::processEvents();
  }
  CHECK(!live_shape(document, polygon_id).has_value());
  CHECK(!canvas->shape_live_points().has_value());
  CHECK(canvas->shape_resize_box_rect().has_value());

  // Two components in one layer keep the box; the one Path Select picks gets its points.
  shape_drag(*canvas, QPoint(480, 300), QPoint(480, 260));
  const auto pair_id = *document.active_layer_id();
  CHECK(pair_id != polygon_id);
  CHECK(points_near(*canvas, Kind::Polygon, QPointF(480.0, 300.0), QPointF(480.0, 260.0)));
  shape_drag(*canvas, QPoint(560, 300), QPoint(560, 270), Qt::ShiftModifier);
  CHECK(*document.active_layer_id() == pair_id);
  if (const auto* pair = std::as_const(document).find_layer(pair_id); pair != nullptr) {
    const auto& subpaths = pair->vector_shape()->path.subpaths;
    CHECK(subpaths.size() == 2);
    CHECK(!canvas->shape_live_points().has_value());
    CHECK(canvas->shape_resize_box_rect().has_value());
    if (subpaths.size() == 2) {
      canvas->select_path_groups({subpaths[1].shape_group});
      QApplication::processEvents();
      CHECK(points_near(*canvas, Kind::Polygon, QPointF(560.0, 300.0), QPointF(560.0, 270.0)));
      const auto first_before = subpaths[0];
      // Its vertex point edits that component alone.
      shape_drag(*canvas, QPoint(560, 270), QPoint(590, 300));
      const auto* after = std::as_const(document).find_layer(pair_id);
      CHECK(after != nullptr && after->vector_shape()->path.subpaths.size() == 2);
      if (after != nullptr && after->vector_shape()->path.subpaths.size() == 2) {
        CHECK(after->vector_shape()->path.subpaths[0] == first_before);
        const auto second = patchy::resolve_live_shape_group(after->vector_shape()->path,
                                                             after->vector_shape()->origination,
                                                             after->vector_shape()->path.subpaths[1].shape_group);
        CHECK(second.has_value() && std::abs(second->polygon_angle) < 1e-6 &&
              std::abs(second->right - 590.0) < 0.01);
      }
    }
  }
}

// GitHub issue 716: in Path mode the component a shape tool just drew (or the one Path Select
// picked) carries the same eight-handle box. A handle resizes the work path as one undo step
// and a live rectangle stays live; Shift, Alt and Esc work as in Shape mode; a drag away from
// the box draws the next component.
void ui_shape_tool_path_mode_box_resizes_the_drawn_path() {
  using patchy::ui::MainWindowTestAccess;
  VectorSettingsGuard settings_guard;
  auto&& [window, canvas] = ShownWindowFixture{};
  window.resize(1500, 820);  // the bar on one row, for the issue shots
  auto& document = MainWindowTestAccess::document(window);
  const auto initial_layers = document.layers().size();
  const auto widget = [&canvas](int x, int y) { return canvas->widget_position_for_document_point(QPoint(x, y)); };

  use_rectangle_tool(window, *canvas);
  use_vector_mode(window, 1);
  CHECK(!canvas->shape_resize_box_rect().has_value());
  shape_drag(*canvas, QPoint(120, 140), QPoint(320, 260));
  CHECK(document.layers().size() == initial_layers);  // a path, no layer
  CHECK(rect_near(work_path_bounds(document), QRectF(120.0, 140.0, 200.0, 120.0)));
  // The drawn component is picked and shows its box at once.
  CHECK(rect_near(canvas->shape_resize_box_rect(), QRectF(120.0, 140.0, 200.0, 120.0)));
  CHECK(canvas->shape_resize_box_angle() == 0.0);
  save_widget_artifact("issue_716_path_mode_box", window);
  const auto depth = MainWindowTestAccess::active_session_undo_depth(window);

  // Over the corner handle the cursor is the corner's resize cursor.
  send_mouse(*canvas, QEvent::MouseMove, widget(320, 260), Qt::NoButton, Qt::NoButton);
  CHECK(canvas->cursor().shape() == Qt::SizeFDiagCursor);

  // The corner handle resizes the path; nothing reaches history before the release.
  send_mouse(*canvas, QEvent::MouseButtonPress, widget(320, 260), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, widget(380, 300), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  CHECK(rect_near(work_path_bounds(document), QRectF(120.0, 140.0, 260.0, 160.0)));
  CHECK(rect_near(canvas->shape_resize_box_rect(), QRectF(120.0, 140.0, 260.0, 160.0)));
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == depth);
  send_mouse(*canvas, QEvent::MouseButtonRelease, widget(380, 300), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(rect_near(work_path_bounds(document), QRectF(120.0, 140.0, 260.0, 160.0)));
  CHECK(rect_near(canvas->shape_resize_box_rect(), QRectF(120.0, 140.0, 260.0, 160.0)));
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == depth + 1);
  CHECK(document.layers().size() == initial_layers);
  // Still a live rectangle, at the new size.
  auto live = work_path_live_shape(document);
  CHECK(live.has_value() && live->kind == patchy::LiveShapeKind::Rectangle);
  if (live.has_value()) {
    CHECK(std::abs(live->right - 380.0) < 0.5 && std::abs(live->bottom - 300.0) < 0.5);
  }
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(rect_near(work_path_bounds(document), QRectF(120.0, 140.0, 200.0, 120.0)));
  require_action_by_text(window, QStringLiteral("Redo"))->trigger();
  QApplication::processEvents();
  CHECK(rect_near(work_path_bounds(document), QRectF(120.0, 140.0, 260.0, 160.0)));
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(rect_near(canvas->shape_resize_box_rect(), QRectF(120.0, 140.0, 200.0, 120.0)));

  // Alt resizes about the center.
  shape_drag(*canvas, QPoint(320, 200), QPoint(350, 200), Qt::AltModifier);
  CHECK(rect_near(work_path_bounds(document), QRectF(90.0, 140.0, 260.0, 120.0)));
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  // Shift on a corner keeps the proportions.
  shape_drag(*canvas, QPoint(320, 260), QPoint(420, 280), Qt::ShiftModifier);
  auto bounds = work_path_bounds(document);
  CHECK(bounds.has_value() && bounds->width() > 250.0 &&
        std::abs(bounds->width() / bounds->height() - 200.0 / 120.0) < 0.01);
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(rect_near(work_path_bounds(document), QRectF(120.0, 140.0, 200.0, 120.0)));

  // Esc mid-drag puts the path back and leaves no history step.
  const auto depth_before_escape = MainWindowTestAccess::active_session_undo_depth(window);
  send_mouse(*canvas, QEvent::MouseButtonPress, widget(320, 260), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, widget(400, 330), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  CHECK(rect_near(work_path_bounds(document), QRectF(120.0, 140.0, 280.0, 190.0)));
  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(rect_near(work_path_bounds(document), QRectF(120.0, 140.0, 200.0, 120.0)));
  send_mouse(*canvas, QEvent::MouseButtonRelease, widget(400, 330), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(rect_near(work_path_bounds(document), QRectF(120.0, 140.0, 200.0, 120.0)));
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == depth_before_escape);
  CHECK(work_path_live_shape(document).has_value());

  // A drag away from the box draws the next component, which takes the box; the first stays.
  shape_drag(*canvas, QPoint(400, 320), QPoint(460, 360));
  const auto* work = std::as_const(document).work_path();
  CHECK(work != nullptr && work->path().subpaths.size() == 2);
  CHECK(rect_near(canvas->shape_resize_box_rect(), QRectF(400.0, 320.0, 60.0, 40.0)));
  CHECK(rect_near(work_path_bounds(document), QRectF(120.0, 140.0, 340.0, 220.0)));
  // Its handle resizes that component alone.
  shape_drag(*canvas, QPoint(460, 360), QPoint(480, 380));
  CHECK(rect_near(canvas->shape_resize_box_rect(), QRectF(400.0, 320.0, 80.0, 60.0)));
  CHECK(rect_near(work_path_bounds(document), QRectF(120.0, 140.0, 360.0, 240.0)));

  // The box belongs to Path mode: Shape mode (no shape layer here) shows none, and neither
  // does another tool.
  use_vector_mode(window, 0);
  CHECK(!canvas->shape_resize_box_rect().has_value());
  use_vector_mode(window, 1);
  CHECK(canvas->shape_resize_box_rect().has_value());
  require_action(window, "toolMoveAction")->trigger();
  QApplication::processEvents();
  CHECK(!canvas->shape_resize_box_rect().has_value());
}

// GitHub issue 716: a press just outside a corner of the box turns the shape about the box's
// center, with the Move box's corner rotate cursor. In Shape mode that is one "Rotate" undo
// step; Shift turns in steps of 15 degrees; Esc cancels. A rectangle stays a live rectangle
// through a quarter turn and becomes a plain path at any other angle. A press further out
// still draws a new shape.
void ui_shape_tool_rotate_band_turns_a_shape_layer() {
  using patchy::ui::MainWindowTestAccess;
  VectorSettingsGuard settings_guard;
  auto&& [window, canvas] = ShownWindowFixture{};
  window.resize(1500, 820);  // the bar on one row, for the issue shots
  auto& document = MainWindowTestAccess::document(window);
  const auto initial_layers = document.layers().size();

  use_rectangle_tool(window, *canvas);
  shape_drag(*canvas, QPoint(200, 200), QPoint(400, 300));
  const auto layer_id = *document.active_layer_id();
  const QRectF box(200.0, 200.0, 200.0, 100.0);
  CHECK(rect_near(canvas->shape_resize_box_rect(), box));
  CHECK(live_shape(document, layer_id).has_value());
  const auto depth = MainWindowTestAccess::active_session_undo_depth(window);

  // The cursor: the tool's own away from the box, the rotate cursor in the band outside a
  // corner, a resize cursor on the handle itself.
  const auto corner = canvas->widget_position_for_document_point(QPoint(400, 300));
  send_mouse(*canvas, QEvent::MouseMove, corner + QPoint(120, 90), Qt::NoButton, Qt::NoButton);
  const auto idle_cursor = canvas->cursor();
  send_mouse(*canvas, QEvent::MouseMove, corner + QPoint(10, 10), Qt::NoButton, Qt::NoButton);
  const auto rotate_cursor = canvas->cursor();
  CHECK(rotate_cursor.shape() == Qt::BitmapCursor);
  CHECK(idle_cursor.shape() != Qt::BitmapCursor || idle_cursor.pixmap().cacheKey() != rotate_cursor.pixmap().cacheKey());
  send_mouse(*canvas, QEvent::MouseMove, corner, Qt::NoButton, Qt::NoButton);
  CHECK(canvas->cursor().shape() == Qt::SizeFDiagCursor);
  // Each corner has its band; along an edge, away from the corners, there is none.
  send_mouse(*canvas, QEvent::MouseMove, canvas->widget_position_for_document_point(QPoint(200, 200)) - QPoint(10, 10),
             Qt::NoButton, Qt::NoButton);
  CHECK(canvas->cursor().shape() == Qt::BitmapCursor);
  send_mouse(*canvas, QEvent::MouseMove, canvas->widget_position_for_document_point(QPoint(300, 300)) + QPoint(0, 10),
             Qt::NoButton, Qt::NoButton);
  CHECK(canvas->cursor().shape() == idle_cursor.shape());

  // A quarter turn, mid-drag: the shape and the box follow, nothing is in history yet.
  auto points = rotate_drag_points(*canvas, box, 90.0);
  send_mouse(*canvas, QEvent::MouseButtonPress, points.press, Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier);
  send_mouse(*canvas, QEvent::MouseMove, points.target, Qt::NoButton, Qt::LeftButton, Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(canvas->shape_resize_box_angle() == 90.0);
  CHECK(rect_near(canvas->shape_resize_box_rect(), box));  // the box at the press, drawn turned
  CHECK(rect_near(shape_bounds(document, layer_id), QRectF(250.0, 150.0, 100.0, 200.0)));
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == depth);
  CHECK(canvas->cursor().shape() == Qt::BitmapCursor);
  send_mouse(*canvas, QEvent::MouseButtonRelease, points.target, Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(document.layers().size() == initial_layers + 1);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == depth + 1);
  CHECK(rect_near(shape_bounds(document, layer_id), QRectF(250.0, 150.0, 100.0, 200.0)));
  CHECK(rect_near(canvas->shape_resize_box_rect(), QRectF(250.0, 150.0, 100.0, 200.0)));
  CHECK(canvas->shape_resize_box_angle() == 0.0);
  // A quarter turn keeps the live rectangle: the box turned, on whole pixels.
  auto live = live_shape(document, layer_id);
  CHECK(live.has_value() && live->kind == patchy::LiveShapeKind::Rectangle);
  if (live.has_value()) {
    CHECK(live->left == 250.0 && live->top == 150.0 && live->right == 350.0 && live->bottom == 350.0);
  }
  CHECK(color_close(canvas_pixel(*canvas, QPoint(300, 170)), Qt::black, 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(220, 250)), Qt::white, 8));
  // One undo puts the drawn rectangle back.
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(rect_near(shape_bounds(document, layer_id), box));
  CHECK(live_shape(document, layer_id).has_value());
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == depth);

  // 30 degrees: a plain path of the same four points, turned about the center; one step.
  points = rotate_drag_points(*canvas, box, 30.0);
  send_mouse(*canvas, QEvent::MouseButtonPress, points.press, Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier);
  send_mouse(*canvas, QEvent::MouseMove, points.target, Qt::NoButton, Qt::LeftButton, Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(canvas->shape_resize_box_angle() == 30.0);
  save_window_shot_with_cursor("issue_716_rotate_shape_mid_drag", window, *canvas, points.target);
  send_mouse(*canvas, QEvent::MouseButtonRelease, points.target, Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == depth + 1);
  CHECK(!live_shape(document, layer_id).has_value());
  const double turned_width = (200.0 * std::cos(3.14159265358979323846 / 6.0)) + (100.0 * 0.5);
  const double turned_height = (200.0 * 0.5) + (100.0 * std::cos(3.14159265358979323846 / 6.0));
  const QRectF turned_box(300.0 - (turned_width / 2.0), 250.0 - (turned_height / 2.0), turned_width, turned_height);
  CHECK(rect_near(shape_bounds(document, layer_id), turned_box));
  CHECK(rect_near(canvas->shape_resize_box_rect(), turned_box));
  {
    const auto* layer = std::as_const(document).find_layer(layer_id);
    const auto* content = layer != nullptr ? layer->vector_shape() : nullptr;
    CHECK(content != nullptr && content->path.subpaths.size() == 1 && content->path.subpaths[0].anchors.size() == 4);
    CHECK(content != nullptr && content->origination.empty());
  }
  // The shape's center is painted, its old top left corner is not.
  CHECK(color_close(canvas_pixel(*canvas, QPoint(300, 250)), Qt::black, 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(204, 204)), Qt::white, 8));
  save_widget_artifact("issue_716_rotated_shape_layer", window);
  // A second turn works on the turned shape: 60 more degrees stand it upright (a plain path).
  rotate_drag(*canvas, turned_box, 60.0);
  CHECK(rect_near(shape_bounds(document, layer_id), QRectF(250.0, 150.0, 100.0, 200.0), 1.0));
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == depth + 2);
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(rect_near(shape_bounds(document, layer_id), box));
  CHECK(live_shape(document, layer_id).has_value());

  // Without Shift the turn is free.
  points = rotate_drag_points(*canvas, box, 20.0);
  drag(*canvas, points.press, points.target);
  QApplication::processEvents();
  CHECK(!live_shape(document, layer_id).has_value());
  auto bounds = shape_bounds(document, layer_id);
  CHECK(bounds.has_value() && bounds->width() > 215.0 && bounds->width() < 228.0);
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();

  // Esc mid-turn puts the shape back and leaves no history step.
  points = rotate_drag_points(*canvas, box, 45.0);
  send_mouse(*canvas, QEvent::MouseButtonPress, points.press, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, points.target, Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  CHECK(!rect_near(shape_bounds(document, layer_id), box));
  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(rect_near(shape_bounds(document, layer_id), box));
  CHECK(canvas->shape_resize_box_angle() == 0.0);
  send_mouse(*canvas, QEvent::MouseButtonRelease, points.target, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(rect_near(shape_bounds(document, layer_id), box));
  CHECK(live_shape(document, layer_id).has_value());
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == depth);
  // A click in the band with no travel changes nothing and draws nothing.
  send_mouse(*canvas, QEvent::MouseButtonPress, points.press, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, points.press, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(document.layers().size() == initial_layers + 1);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == depth);

  // Further out than the band a press still draws a new shape.
  drag(*canvas, corner + QPoint(60, 60), corner + QPoint(120, 110));
  QApplication::processEvents();
  CHECK(document.layers().size() == initial_layers + 2);
  CHECK(rect_near(shape_bounds(document, layer_id), box));
}

// GitHub issue 716: the same rotate in Path mode turns the picked component of the work path
// as one undo step, and an ellipse rounds a quarter turn as a live ellipse.
void ui_shape_tool_rotate_band_turns_a_path_in_path_mode() {
  using patchy::ui::MainWindowTestAccess;
  VectorSettingsGuard settings_guard;
  auto&& [window, canvas] = ShownWindowFixture{};
  window.resize(1500, 820);  // the bar on one row, for the issue shots
  auto& document = MainWindowTestAccess::document(window);
  const auto initial_layers = document.layers().size();

  require_action(window, "toolEllipseAction")->trigger();
  QApplication::processEvents();
  canvas->setFocus(Qt::MouseFocusReason);
  canvas->set_snap_enabled(false);
  use_vector_mode(window, 1);
  shape_drag(*canvas, QPoint(200, 200), QPoint(400, 300));
  const QRectF box(200.0, 200.0, 200.0, 100.0);
  CHECK(rect_near(canvas->shape_resize_box_rect(), box));
  auto live = work_path_live_shape(document);
  CHECK(live.has_value() && live->kind == patchy::LiveShapeKind::Ellipse);
  const auto depth = MainWindowTestAccess::active_session_undo_depth(window);

  send_mouse(*canvas, QEvent::MouseMove, canvas->widget_position_for_document_point(QPoint(400, 300)) + QPoint(10, 10),
             Qt::NoButton, Qt::NoButton);
  CHECK(canvas->cursor().shape() == Qt::BitmapCursor);

  // A quarter turn: still a live ellipse, its box turned.
  rotate_drag(*canvas, box, 90.0);
  CHECK(document.layers().size() == initial_layers);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == depth + 1);
  CHECK(rect_near(work_path_bounds(document), QRectF(250.0, 150.0, 100.0, 200.0)));
  CHECK(rect_near(canvas->shape_resize_box_rect(), QRectF(250.0, 150.0, 100.0, 200.0)));
  live = work_path_live_shape(document);
  CHECK(live.has_value() && live->kind == patchy::LiveShapeKind::Ellipse);
  if (live.has_value()) {
    CHECK(live->left == 250.0 && live->bottom == 350.0);
  }
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(rect_near(work_path_bounds(document), box));

  // 45 degrees: a plain path; the turned ellipse is as wide as it is tall.
  rotate_drag(*canvas, box, 45.0);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == depth + 1);
  CHECK(!work_path_live_shape(document).has_value());
  const auto* work = std::as_const(document).work_path();
  CHECK(work != nullptr && work->path().subpaths.size() == 1 && work->live_shapes().empty());
  const auto turned = canvas->shape_resize_box_rect();
  CHECK(turned.has_value());
  if (turned.has_value()) {
    CHECK(std::abs(turned->width() - turned->height()) < 0.01);
    CHECK(std::abs(turned->center().x() - 300.0) < 0.01 && std::abs(turned->center().y() - 250.0) < 0.01);
  }
  save_widget_artifact("issue_716_rotated_path", window);
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(rect_near(work_path_bounds(document), box));
  CHECK(work_path_live_shape(document).has_value());

  // Esc mid-turn puts the path back.
  const auto points = rotate_drag_points(*canvas, box, 60.0);
  send_mouse(*canvas, QEvent::MouseButtonPress, points.press, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, points.target, Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  CHECK(!rect_near(work_path_bounds(document), box));
  CHECK(canvas->cursor().shape() == Qt::BitmapCursor);
  save_window_shot_with_cursor("issue_716_rotate_path_mid_drag", window, *canvas, points.target);
  send_key(*canvas, Qt::Key_Escape);
  send_mouse(*canvas, QEvent::MouseButtonRelease, points.target, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(rect_near(work_path_bounds(document), box));
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == depth);
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
      {"ui_shape_tool_polygon_vertex_point_sets_size_and_angle_in_one_undo_step",
       ui_shape_tool_polygon_vertex_point_sets_size_and_angle_in_one_undo_step},
      {"ui_shape_tool_polygon_center_point_moves_and_escape_cancels",
       ui_shape_tool_polygon_center_point_moves_and_escape_cancels},
      {"ui_shape_tool_line_end_points_re_aim_a_live_line", ui_shape_tool_line_end_points_re_aim_a_live_line},
      {"ui_shape_tool_points_show_only_for_a_live_regular_polygon_or_line",
       ui_shape_tool_points_show_only_for_a_live_regular_polygon_or_line},
      {"ui_shape_tool_path_mode_box_resizes_the_drawn_path", ui_shape_tool_path_mode_box_resizes_the_drawn_path},
      {"ui_shape_tool_rotate_band_turns_a_shape_layer", ui_shape_tool_rotate_band_turns_a_shape_layer},
      {"ui_shape_tool_rotate_band_turns_a_path_in_path_mode", ui_shape_tool_rotate_band_turns_a_path_in_path_mode},
  };
}
