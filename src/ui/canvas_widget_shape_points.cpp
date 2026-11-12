// CanvasWidget's drag points for a live Polygon and a live Line (GitHub issue 690 follow-up,
// docs/vector-tools-shape-handles.md, "Drag points on a live Polygon and Line"). With a shape tool active
// in Shape mode, a shape layer whose one component is a live polygon with a square box, or
// a live line, shows the two points of the drag that drew it in place of the eight-handle
// box (canvas_widget_shape_resize.cpp): a polygon's center and first vertex, a line's two
// ends. Dragging the vertex sets the polygon's size and angle as the drawing drag does,
// dragging the center moves it, dragging a line's end re-aims the line about the other end.
// The shape is regenerated from its parameters on every move, so it stays live. The points
// share the box's entry points: a press anywhere else still draws a new shape, Esc cancels,
// and a drag previews through MainWindow and lands as one undo step.
//
// Legal boundary (docs/legal-constraints.md, "Shape drag points"): these are the two points
// of the creation drag and nothing else. No point edits a corner radius, a side count or a
// star inset, and nothing is drawn with the points but the two discs. The center point
// moves the whole shape, so it snaps as every whole-shape drag does (GitHub issue 695):
// the Move tool's scan of the polygon's bounds, both edges and the center on each axis,
// never a feature chosen from where the drag was grabbed.

#include "ui/canvas_widget.hpp"

#include "core/vector_live_shapes.hpp"
#include "core/vector_shape.hpp"
#include "ui/canvas_types.hpp"
#include "ui/theme_palette.hpp"

#include <QApplication>
#include <QColor>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPointF>
#include <QRectF>
#include <Qt>

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

// The Gradient Fill end handles' disc and reach, in widget pixels.
constexpr double kPointRadius = 6.0;
constexpr double kPointReach = 8.0;
// What a repaint of one point must cover: the disc, its ring and antialiasing.
constexpr double kPointRepaintReach = 10.0;

std::optional<CanvasWidget::ShapeLivePoints> points_from_params(LayerId layer_id, const LiveShapeParams& params,
                                                                int shape_group = 0) {
  CanvasWidget::ShapeLivePoints points;
  points.layer_id = layer_id;
  points.params = params;
  points.shape_group = shape_group;
  if (live_polygon_is_regular(params)) {
    const auto center = live_polygon_center(params);
    const auto vertex = live_polygon_first_vertex(params);
    points.kind = CanvasWidget::ShapePointsKind::Polygon;
    points.first = QPointF(center.x, center.y);
    points.second = QPointF(vertex.x, vertex.y);
    return points;
  }
  if (params.kind == LiveShapeKind::Line &&
      std::hypot(params.line_end_x - params.line_start_x, params.line_end_y - params.line_start_y) >= 1.0) {
    points.kind = CanvasWidget::ShapePointsKind::Line;
    points.first = QPointF(params.line_start_x, params.line_start_y);
    points.second = QPointF(params.line_end_x, params.line_end_y);
    return points;
  }
  return std::nullopt;
}

QRect point_repaint_rect(QPointF widget_point) {
  return QRectF(widget_point.x() - kPointRepaintReach, widget_point.y() - kPointRepaintReach,
                kPointRepaintReach * 2.0, kPointRepaintReach * 2.0)
      .toAlignedRect();
}

}  // namespace

void CanvasWidget::set_shape_point_callback(ShapePointCallback callback) {
  shape_point_callback_ = std::move(callback);
}

std::optional<CanvasWidget::ShapeLivePoints> CanvasWidget::shape_live_points_of(const Layer& layer) const {
  const auto* content = layer.vector_shape();
  if (content == nullptr) {
    return std::nullopt;
  }
  // One component only, the one the box and the W / H fields would size: the layer's only
  // component, or the only one Path Select picked. Several components keep the box.
  auto groups = path_edit_selected_groups();
  if (groups.empty()) {
    for (const auto& subpath : content->path.subpaths) {
      groups.push_back(subpath.shape_group);
    }
    std::sort(groups.begin(), groups.end());
    groups.erase(std::unique(groups.begin(), groups.end()), groups.end());
  }
  if (groups.size() != 1U) {
    return std::nullopt;
  }
  // Live only while the path is still what the parameters generate: a polygon with an
  // edited point, or a line the box stretched, is a plain path and keeps the box.
  const auto params = resolve_live_shape_group(content->path, content->origination, groups.front());
  if (!params.has_value()) {
    return std::nullopt;
  }
  return points_from_params(layer.id(), *params, groups.front());
}

std::optional<CanvasWidget::ShapeLivePoints> CanvasWidget::shape_live_points() const {
  if (shape_point_drag_.has_value()) {
    return points_from_params(shape_point_drag_->layer_id, shape_point_drag_->current);
  }
  if (shape_resize_drag_.has_value()) {
    return std::nullopt;
  }
  // The points show exactly where the box would: shape_resize_layer holds every hide case.
  const auto* layer = shape_resize_layer();
  if (layer == nullptr) {
    return std::nullopt;
  }
  return shape_live_points_of(*layer);
}

CanvasWidget::ShapePoint CanvasWidget::shape_point_at(QPointF widget_point) const {
  const auto points = shape_live_points();
  if (!points.has_value()) {
    return ShapePoint::None;
  }
  const double first = QLineF(widget_point, widget_position_f(points->first)).length();
  const double second = QLineF(widget_point, widget_position_f(points->second)).length();
  // Where the two discs overlap (a tiny shape) the nearer one is taken; a tie goes to the
  // second point, the one the drawing drag ended on.
  if (second <= kPointReach && second <= first) {
    return ShapePoint::Second;
  }
  if (first <= kPointReach) {
    return ShapePoint::First;
  }
  return ShapePoint::None;
}

bool CanvasWidget::press_shape_point(QMouseEvent* event) {
  const auto points = shape_live_points();
  if (!points.has_value()) {
    return false;
  }
  const auto point = shape_point_at(event->position());
  if (point == ShapePoint::None) {
    return false;
  }
  if (edit_locked_) {
    show_edit_locked_message();
    return true;
  }
  ShapePointDrag drag;
  drag.layer_id = points->layer_id;
  drag.kind = points->kind;
  drag.point = point;
  drag.start = points->params;
  drag.current = points->params;
  drag.press_widget = event->position();
  drag.press_document = document_position_f(event->position());
  drag.grab_offset = (point == ShapePoint::First ? points->first : points->second) - drag.press_document;
  if (points->kind == ShapePointsKind::Polygon && point == ShapePoint::First) {
    // The center moves the whole polygon: its bounds, as they are at the press, are what
    // the drag snaps (the bounds a Ctrl drag of the same shape snaps).
    const auto* layer = shape_resize_layer();
    const auto* content = layer != nullptr ? layer->vector_shape() : nullptr;
    if (content != nullptr) {
      if (const auto bounds = shape_group_snap_bounds(content->path, points->shape_group); bounds.has_value()) {
        drag.snap_sources.push_back(*bounds);
      }
    }
  }
  drag.serial = ++shape_resize_drag_serial_;
  shape_point_drag_ = drag;
  shape_point_hover_ = point;
  setCursor(gradient_handle_qcursor(GradientHandleCursor::Move));
  update();
  return true;
}

void CanvasWidget::update_shape_point_drag(QPointF widget_point) {
  if (!shape_point_drag_.has_value()) {
    return;
  }
  auto& drag = *shape_point_drag_;
  if (!drag.moved) {
    // A click on a point changes nothing.
    if ((widget_point - drag.press_widget).manhattanLength() < QApplication::startDragDistance()) {
      return;
    }
    drag.moved = true;
  }
  const auto pointer = document_position_f(widget_point);
  auto edited = drag.start;
  if (drag.kind == ShapePointsKind::Polygon && drag.point == ShapePoint::First) {
    // The center moves the polygon by the pointer's travel, on whole pixels, through the
    // snapping of every whole-shape drag (GitHub issue 695): both edges and the center of
    // the polygon's bounds are the sources, as in the Move tool's scan, never the grabbed
    // point alone (docs/legal-constraints.md, "Shape drag points"). An axis that snaps
    // lands exactly on its line; the polygon's own layer is never a target.
    const QPointF travel(std::round(pointer.x() - drag.press_document.x()),
                         std::round(pointer.y() - drag.press_document.y()));
    const auto total = snap_whole_shape_drag(drag.snap_sources, {drag.layer_id}, travel, false, false);
    translate_live_shape_params(edited, total.x(), total.y());
  } else {
    // The vertex or the line's end follows the pointer through the snap targets on whole
    // pixels, as the second point of the drawing drag does.
    const auto target = snapped_document_point_f(pointer + drag.grab_offset);
    const bool placed = drag.kind == ShapePointsKind::Polygon
                            ? set_live_polygon_first_vertex(edited, target.x(), target.y())
                            : set_live_line_point(edited, drag.point == ShapePoint::Second, target.x(), target.y());
    if (!placed) {
      edited = drag.current;  // on top of the other point: the shape stays as it last was
    }
  }
  if (edited == drag.current) {
    return;
  }
  drag.current = edited;
  drag.previewed = true;
  // The callback re-renders the layer and may repaint or end the drag; copy what it needs.
  const auto layer_id = drag.layer_id;
  const auto serial = drag.serial;
  if (shape_point_callback_) {
    shape_point_callback_(layer_id, edited, ShapeResizePhase::Preview, serial);
  }
  update();
}

void CanvasWidget::release_shape_point(QMouseEvent* event) {
  update_shape_point_drag(event->position());
  if (!shape_point_drag_.has_value()) {
    return;  // the preview callback ended the drag (the document was swapped)
  }
  const auto drag = *shape_point_drag_;
  shape_point_drag_.reset();
  clear_move_snap_guides();  // a center drag's alignment guides end with it
  if (shape_point_callback_) {
    if (drag.moved && drag.current != drag.start) {
      shape_point_callback_(drag.layer_id, drag.current, ShapeResizePhase::Commit, drag.serial);
    } else if (drag.previewed) {
      shape_point_callback_(drag.layer_id, drag.start, ShapeResizePhase::Cancel, drag.serial);
    }
  }
  last_mouse_position_ = event->pos();
  update_tool_cursor();
  update();
}

void CanvasWidget::cancel_shape_point_drag() {
  if (!shape_point_drag_.has_value()) {
    return;
  }
  const auto drag = *shape_point_drag_;
  shape_point_drag_.reset();
  clear_move_snap_guides();
  if (drag.previewed && shape_point_callback_) {
    shape_point_callback_(drag.layer_id, drag.start, ShapeResizePhase::Cancel, drag.serial);
  }
  update_tool_cursor();
  update();
}

void CanvasWidget::set_shape_point_hover(ShapePoint point) {
  if (shape_point_hover_ == point) {
    return;
  }
  shape_point_hover_ = point;
  if (shape_points_painted_.has_value()) {
    update(point_repaint_rect(shape_points_painted_->first));
    update(point_repaint_rect(shape_points_painted_->second));
  }
}

bool CanvasWidget::apply_shape_point_cursor(QPointF widget_point) {
  if (shape_point_drag_.has_value()) {
    setCursor(gradient_handle_qcursor(GradientHandleCursor::Move));
    return true;
  }
  const auto point = shape_point_at(widget_point);
  set_shape_point_hover(point);
  if (point == ShapePoint::None) {
    return false;
  }
  setCursor(gradient_handle_qcursor(GradientHandleCursor::Move));
  return true;
}

void CanvasWidget::draw_shape_points(QPainter& painter) const {
  const auto points = shape_live_points();
  if (!points.has_value()) {
    shape_points_painted_.reset();
    return;
  }
  // Document to screen through view_transform(), so the points follow zoom and rotation.
  const auto first = widget_position_f(points->first);
  const auto second = widget_position_f(points->second);
  shape_points_painted_ = std::make_pair(first, second);
  const auto& palette = theme();
  const QColor ink = palette.canvas_gradient_handle_ink;
  const QColor light = palette.canvas_gradient_handle_fill;
  const QColor active = palette.canvas_gradient_handle_active;
  const auto highlighted = [this](ShapePoint point) {
    return shape_point_drag_.has_value() ? shape_point_drag_->point == point : shape_point_hover_ == point;
  };
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);
  // Two discs and nothing else: a light disc in a dark ring, readable on any artwork. A
  // polygon's center carries a dark dot, which tells it from the vertex.
  const auto draw_point = [&](QPointF at, bool dotted, bool on) {
    painter.setPen(QPen(on ? active : ink, on ? 2.5 : 1.5));
    painter.setBrush(light);
    painter.drawEllipse(at, kPointRadius, kPointRadius);
    if (dotted) {
      painter.setPen(Qt::NoPen);
      painter.setBrush(on ? active : ink);
      painter.drawEllipse(at, 1.75, 1.75);
    }
  };
  draw_point(first, points->kind == ShapePointsKind::Polygon, highlighted(ShapePoint::First));
  draw_point(second, false, highlighted(ShapePoint::Second));
  painter.restore();
}

}  // namespace patchy::ui
