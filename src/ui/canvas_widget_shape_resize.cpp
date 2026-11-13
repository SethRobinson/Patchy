// CanvasWidget's bounding-box handles for the shape tools (GitHub issues 690 and 716,
// docs/vector-tools-shape-handles.md, "Resize handles on the shape tools"). With Rectangle,
// Ellipse, Polygon, Line or Custom Shape active, the box the options-bar W / H fields
// describe is drawn with the Move tool's transform controls (the dashed frame under the
// shape's outline and eight squares over it, no rotate stem): on the active editable shape
// layer in Shape mode, on the picked components of the work path or the active saved path
// in Path mode, and on the pending pixel shape in Pixels mode. A press on a handle square
// resizes the shape: Shift keeps the proportions on a corner, Alt resizes about the center,
// Esc cancels. A press just outside a corner turns it about the box's center (Shift: steps
// of 15 degrees). Any other press still draws a new shape. A layer's or a path's drag
// previews through MainWindow and lands as one undo step.
//
// A live Polygon with a square box and a live Line show their two drag points in place of
// the box in Shape mode (canvas_widget_shape_points.cpp); those share the entry points below.
//
// This is a plain bounding-box resize and rotate only, the Move tool's. Nothing here edits a
// shape parameter (corner radius, side count, star inset): those stay in the options bar and
// the Properties panel (docs/vector-tools.md, "Patents and trademarks").

#include "ui/canvas_widget.hpp"

#include "core/layer_metadata.hpp"
#include "core/layer_tree.hpp"
#include "core/vector_live_shapes.hpp"
#include "core/vector_shape.hpp"
#include "ui/canvas_types.hpp"

#include <QApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QPointF>
#include <QRectF>
#include <Qt>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

// The smallest box a handle drag leaves, in document pixels. A handle stops there instead
// of crossing its opposite side: a mirrored shape would stop being a live shape.
constexpr double kMinimumExtent = 1.0;
// An axis this thin (a horizontal hairline path) has nothing to scale along it.
constexpr double kDegenerateExtent = 1e-6;

// The bounds of `path`, or of the subpaths in `groups` when that names some of them. The
// control hull, as VectorPath::bounds() and the W / H fields measure it.
std::optional<QRectF> component_bounds(const VectorPath& path, const std::vector<int>& groups) {
  bool any = false;
  double left = 0.0;
  double top = 0.0;
  double right = 0.0;
  double bottom = 0.0;
  const auto include = [&](double x, double y) {
    if (!any) {
      left = right = x;
      top = bottom = y;
      any = true;
      return;
    }
    left = std::min(left, x);
    right = std::max(right, x);
    top = std::min(top, y);
    bottom = std::max(bottom, y);
  };
  for (const auto& subpath : path.subpaths) {
    if (!groups.empty() && std::find(groups.begin(), groups.end(), subpath.shape_group) == groups.end()) {
      continue;
    }
    for (const auto& anchor : subpath.anchors) {
      include(anchor.anchor_x, anchor.anchor_y);
      include(anchor.in_x, anchor.in_y);
      include(anchor.out_x, anchor.out_y);
    }
  }
  if (!any) {
    return std::nullopt;
  }
  return QRectF(QPointF(left, top), QPointF(right, bottom));
}

// `offset` turned by `degrees`, clockwise on screen; exact at the quarter turns.
QPointF turned(QPointF offset, double degrees) {
  const auto turn = patchy::rotation_cos_sin(degrees);
  return QPointF((offset.x() * turn.cos) - (offset.y() * turn.sin), (offset.x() * turn.sin) + (offset.y() * turn.cos));
}

// An angle in degrees brought into (-180, 180].
double normalized_degrees(double degrees) {
  degrees = std::fmod(degrees, 360.0);
  if (degrees > 180.0) {
    degrees -= 360.0;
  } else if (degrees <= -180.0) {
    degrees += 360.0;
  }
  return degrees;
}

}  // namespace

QRectF CanvasWidget::turned_rect_bounds(QRectF rect, double angle_degrees) {
  if (angle_degrees == 0.0) {
    return rect;
  }
  const auto center = rect.center();
  QRectF bounds;
  bool any = false;
  for (const auto& corner : {rect.topLeft(), rect.topRight(), rect.bottomRight(), rect.bottomLeft()}) {
    const auto at = center + turned(corner - center, angle_degrees);
    const QRectF point(at, QSizeF(0.0, 0.0));
    if (!any) {
      bounds = point;
      any = true;
    } else {
      bounds.setLeft(std::min(bounds.left(), at.x()));
      bounds.setRight(std::max(bounds.right(), at.x()));
      bounds.setTop(std::min(bounds.top(), at.y()));
      bounds.setBottom(std::max(bounds.bottom(), at.y()));
    }
  }
  return bounds;
}

void CanvasWidget::set_shape_resize_callback(ShapeResizeCallback callback) {
  shape_resize_callback_ = std::move(callback);
}

const Layer* CanvasWidget::shape_resize_layer() const {
  if (document_ == nullptr) {
    return nullptr;
  }
  const auto& document = std::as_const(*document_);
  if (shape_resize_drag_.has_value()) {
    if (shape_resize_drag_->pixel_shape || shape_resize_drag_->document_path.has_value()) {
      return nullptr;
    }
    return document.find_layer(shape_resize_drag_->layer_id);
  }
  if (shape_point_drag_.has_value()) {
    return document.find_layer(shape_point_drag_->layer_id);
  }
  // Shape mode: Path mode's box rides a work or saved path (shape_resize_document_path) and
  // Pixels mode's the pending shape.
  if (!shape_tool_active() || vector_tool_mode_ != VectorToolMode::Shape || drawing_shape_ ||
      shape_ctrl_path_select_ || shape_temp_path_select_ || quick_mask_active_ ||
      layer_edit_target_ != LayerEditTarget::Content || path_transform_active_ || transforming_layer_ ||
      warping_layer_) {
    return nullptr;
  }
  // The box rides the shape's path outline: dismissing the outline (Esc, View > Show >
  // Target Path, View > Extras) hides the handles with it.
  if (!panel_path_targeted_ || !target_path_visible_ || !extra_shown(ViewExtra::TargetPath) ||
      !extra_shown(ViewExtra::TransformControls)) {
    return nullptr;
  }
  if (selected_layer_ids_.size() > 1U) {
    return nullptr;
  }
  const auto active = document.active_layer_id();
  const auto* layer = active.has_value() ? document.find_layer(*active) : nullptr;
  if (layer == nullptr || !layer_is_vector_shape(*layer) || layer->vector_shape() == nullptr ||
      !vector_lock_reason(*layer).empty() || path_edit_target_path() != &layer->vector_shape()->path) {
    return nullptr;
  }
  const auto& layers = document.layers();
  if (layer_effectively_locks_position(*layer) || patchy::layer_is_effectively_locked(layers, layer->id())) {
    return nullptr;
  }
  // A hidden layer, or one inside a hidden folder, shows nothing to grab.
  if (!layer->visible()) {
    return nullptr;
  }
  std::vector<LayerId> ancestors;
  if (collect_layer_ancestor_groups(layers, layer->id(), ancestors)) {
    for (const auto ancestor_id : ancestors) {
      if (const auto* ancestor = document.find_layer(ancestor_id); ancestor != nullptr && !ancestor->visible()) {
        return nullptr;
      }
    }
  }
  return layer;
}

std::optional<DocumentPathId> CanvasWidget::shape_resize_document_path() const {
  if (document_ == nullptr) {
    return std::nullopt;
  }
  if (shape_resize_drag_.has_value()) {
    return shape_resize_drag_->document_path;
  }
  if (shape_point_drag_.has_value() || pending_pixel_shape_ != nullptr) {
    return std::nullopt;
  }
  // Path mode (GitHub issue 716): the components Path Select picked on the work path or the
  // active saved path, which is also the component a shape tool just drew. Every hide case
  // of the Shape mode box applies.
  if (!shape_tool_active() || vector_tool_mode_ != VectorToolMode::Path || drawing_shape_ ||
      shape_ctrl_path_select_ || shape_temp_path_select_ || quick_mask_active_ ||
      layer_edit_target_ != LayerEditTarget::Content || path_transform_active_ || transforming_layer_ ||
      warping_layer_) {
    return std::nullopt;
  }
  if (!panel_path_targeted_ || !target_path_visible_ || !extra_shown(ViewExtra::TargetPath) ||
      !extra_shown(ViewExtra::TransformControls)) {
    return std::nullopt;
  }
  const auto& document = std::as_const(*document_);
  const auto* target = path_edit_target_path();
  if (target == nullptr) {
    return std::nullopt;
  }
  std::optional<DocumentPathId> id;
  if (active_document_path_.has_value()) {
    if (const auto* path = document.find_path(*active_document_path_); path != nullptr && &path->path() == target) {
      id = path->id();
    }
  }
  if (!id.has_value()) {
    if (const auto* work = document.work_path(); work != nullptr && &work->path() == target) {
      id = work->id();
    }
  }
  if (!id.has_value() || path_edit_selected_groups().empty()) {
    return std::nullopt;
  }
  return id;
}

std::optional<QRectF> CanvasWidget::shape_resize_box_rect() const {
  if (shape_resize_drag_.has_value()) {
    return shape_resize_drag_->current_rect;
  }
  // A pending pixel shape (GitHub issue 716) shows the same box while its tool is active.
  if (pending_pixel_shape_ != nullptr) {
    if (!extra_shown(ViewExtra::TransformControls)) {
      return std::nullopt;
    }
    return pending_pixel_shape_box();
  }
  if (shape_point_drag_.has_value()) {
    return std::nullopt;
  }
  const VectorPath* path = nullptr;
  if (const auto* layer = shape_resize_layer(); layer != nullptr) {
    // A live Polygon or Line shows its two drag points instead of the box.
    if (shape_live_points_of(*layer).has_value()) {
      return std::nullopt;
    }
    path = &layer->vector_shape()->path;
  } else if (shape_resize_document_path().has_value()) {
    path = path_edit_target_path();
  }
  if (path == nullptr) {
    return std::nullopt;
  }
  // Components Path Select picked are what the W / H fields size; every component (or
  // none) is the whole shape.
  const auto rect = component_bounds(*path, path_edit_selected_groups());
  // A path with no area along an axis (a straight two-point line) has no box to grab.
  if (!rect.has_value() || rect->width() <= kDegenerateExtent || rect->height() <= kDegenerateExtent) {
    return std::nullopt;
  }
  return rect;
}

double CanvasWidget::shape_resize_box_angle() const {
  if (shape_resize_drag_.has_value()) {
    return shape_resize_drag_->angle;
  }
  return pending_pixel_shape_angle();
}

CanvasWidget::TransformHandle CanvasWidget::shape_resize_handle_at(QPoint widget_point) const {
  const auto rect = shape_resize_box_rect();
  if (!rect.has_value()) {
    return TransformHandle::None;
  }
  const auto handle =
      transform_handle_at(widget_point, *rect, shape_resize_box_angle(), TransformHitZones::ResizeHandlesAndRotate);
  // A pending pixel shape moves by a press inside its box; a shape layer's or a path's box
  // does not (a press there draws the next shape, and Ctrl moves it).
  if (handle == TransformHandle::Move && (pending_pixel_shape_ == nullptr || shape_resize_drag_.has_value())) {
    return TransformHandle::None;
  }
  return handle;
}

bool CanvasWidget::press_shape_resize_handle(QMouseEvent* event) {
  if (shape_handle_drag_active()) {
    // A press while a drag is live means its release was lost: put the shape back.
    cancel_shape_resize_drag();
  }
  // The pending pixel shape: a handle resizes it, a press inside its box moves it and a
  // press just outside a corner turns it. A press anywhere else is not taken, so the caller
  // commits the shape and draws the next. Ctrl is the temporary Path Select and never
  // reaches here; Shift and Alt on a handle constrain the resize, so they do not turn the
  // press into a draw.
  const bool pixel_shape = pending_pixel_shape_ != nullptr;
  const Layer* layer = nullptr;
  std::optional<DocumentPathId> document_path;
  if (!pixel_shape) {
    layer = shape_resize_layer();
    if (layer != nullptr) {
      // A live Polygon or Line: its two drag points (canvas_widget_shape_points.cpp).
      if (press_shape_point(event)) {
        return true;
      }
    } else {
      document_path = shape_resize_document_path();
      if (!document_path.has_value()) {
        return false;
      }
    }
  }
  const auto rect = shape_resize_box_rect();
  if (!rect.has_value()) {
    return false;
  }
  const auto handle = shape_resize_handle_at(event->pos());
  if (handle == TransformHandle::None) {
    return false;
  }
  if (edit_locked_) {
    show_edit_locked_message();
    return true;
  }
  const double angle = shape_resize_box_angle();
  ShapeResizeDrag drag;
  drag.pixel_shape = pixel_shape;
  if (layer != nullptr) {
    drag.layer_id = layer->id();
  }
  drag.document_path = document_path;
  drag.handle = handle;
  drag.start_rect = *rect;
  drag.current_rect = *rect;
  drag.start_angle = angle;
  drag.angle = angle;
  drag.press_widget = event->position();
  drag.press_document = document_position_f(event->position());
  drag.pointer_widget = event->position();
  if (handle != TransformHandle::Move && handle != TransformHandle::Rotate) {
    drag.grab_offset = transform_handle_grab_offset(handle, *rect, angle, drag.press_document);
  }
  drag.serial = ++shape_resize_drag_serial_;
  shape_resize_drag_ = drag;
  apply_shape_resize_cursor(event->pos());
  update();
  return true;
}

void CanvasWidget::update_shape_resize_drag(QPointF widget_point, Qt::KeyboardModifiers modifiers) {
  if (!shape_resize_drag_.has_value()) {
    return;
  }
  auto& drag = *shape_resize_drag_;
  drag.pointer_widget = widget_point;
  if (!drag.moved) {
    // A click on a handle changes nothing.
    if ((widget_point - drag.press_widget).manhattanLength() < QApplication::startDragDistance()) {
      return;
    }
    drag.moved = true;
  }
  const auto handle = drag.handle;
  const auto& start = drag.start_rect;
  // What a layer's or a path's drag sends MainWindow: the box at the press and the box or
  // the angle now. The callback re-renders and may repaint, so it gets copies.
  const auto send = [this](const ShapeResizeDrag& state, ShapeResizePhase phase) {
    if (!shape_resize_callback_) {
      return;
    }
    ShapeBoxEdit edit;
    if (state.document_path.has_value()) {
      edit.document_path = state.document_path;
    } else {
      edit.layer_id = state.layer_id;
    }
    edit.from = state.start_rect;
    edit.to = state.current_rect;
    edit.rotate = state.handle == TransformHandle::Rotate;
    edit.angle_degrees = state.angle;
    const auto serial = state.serial;
    shape_resize_callback_(edit, phase, serial);
  };
  if (handle == TransformHandle::Rotate) {
    // The Move box's rotate: the pointer's turn about the box's center since the press, in
    // steps of 15 degrees with Shift (Free Transform's rule). Nothing else is read or drawn.
    const auto pivot = start.center();
    const auto from = drag.press_document - pivot;
    const auto now = document_position_f(widget_point) - pivot;
    if (std::hypot(from.x(), from.y()) <= 1e-6 || std::hypot(now.x(), now.y()) <= 1e-6) {
      return;
    }
    double degrees = drag.start_angle + ((std::atan2(now.y(), now.x()) - std::atan2(from.y(), from.x())) * 180.0 /
                                         std::numbers::pi);
    if ((modifiers & Qt::ShiftModifier) != 0) {
      degrees = std::round(degrees / 15.0) * 15.0;
    }
    degrees = normalized_degrees(degrees);
    if (degrees == drag.angle) {
      return;
    }
    drag.angle = degrees;
    drag.previewed = true;
    if (drag.pixel_shape) {
      set_pending_pixel_shape_geometry(start, degrees);  // the preview follows; nothing is painted yet
      update();
      return;
    }
    const auto state = drag;
    send(state, ShapeResizePhase::Preview);
    update();
    return;
  }
  if (handle == TransformHandle::Move) {
    // The pending pixel shape, dragged by its inside: the pointer's travel on whole pixels
    // through the snapping of every whole-shape drag, with both edges and the center of the
    // shape's bounds as the sources, never a feature chosen from where it was grabbed
    // (docs/legal-constraints.md, "Alignment snapping").
    const auto pointer = document_position_f(widget_point);
    const auto press = document_position_f(drag.press_widget);
    const QPointF travel(std::round(pointer.x() - press.x()), std::round(pointer.y() - press.y()));
    const auto total = snap_whole_shape_drag({turned_rect_bounds(start, drag.start_angle)}, {}, travel, false, false);
    const auto moved = start.translated(total);
    if (moved == drag.current_rect) {
      return;
    }
    drag.current_rect = moved;
    drag.previewed = true;
    set_pending_pixel_shape_geometry(moved, drag.start_angle);
    update();
    return;
  }
  const bool moves_left = handle == TransformHandle::TopLeft || handle == TransformHandle::Left ||
                          handle == TransformHandle::BottomLeft;
  const bool moves_right = handle == TransformHandle::TopRight || handle == TransformHandle::Right ||
                           handle == TransformHandle::BottomRight;
  const bool moves_top = handle == TransformHandle::TopLeft || handle == TransformHandle::Top ||
                         handle == TransformHandle::TopRight;
  const bool moves_bottom = handle == TransformHandle::BottomLeft || handle == TransformHandle::Bottom ||
                            handle == TransformHandle::BottomRight;
  const bool scales_x = (moves_left || moves_right) && start.width() > kDegenerateExtent;
  const bool scales_y = (moves_top || moves_bottom) && start.height() > kDegenerateExtent;
  // The grabbed side follows the pointer from where it was pressed, on whole pixels and
  // through the snap targets, like a Move-box handle. A turned box (a pending pixel shape
  // only) is resized in its own frame: the pointer is turned back about the box's center
  // at the press, and the new box's center is turned out again, so the opposite side stays
  // where it is on the canvas.
  const double box_angle = drag.start_angle;
  const auto center = start.center();
  auto point = snapped_document_point_f(document_position_f(widget_point) + drag.grab_offset);
  if (box_angle != 0.0) {
    point = center + turned(point - center, -box_angle);
  }
  // Alt scales about the center (the Move box's default reference point); otherwise the
  // opposite side stays put.
  const bool from_center = (modifiers & Qt::AltModifier) != 0;
  const double anchor_x = from_center ? center.x() : (moves_left ? start.right() : start.left());
  const double anchor_y = from_center ? center.y() : (moves_top ? start.bottom() : start.top());
  const double span_x = (moves_left ? start.left() : start.right()) - anchor_x;
  const double span_y = (moves_top ? start.top() : start.bottom()) - anchor_y;
  double factor_x = scales_x ? (point.x() - anchor_x) / span_x : 1.0;
  double factor_y = scales_y ? (point.y() - anchor_y) / span_y : 1.0;
  const double minimum_x = start.width() > kDegenerateExtent ? kMinimumExtent / start.width() : 1.0;
  const double minimum_y = start.height() > kDegenerateExtent ? kMinimumExtent / start.height() : 1.0;
  if (scales_x && scales_y && (modifiers & Qt::ShiftModifier) != 0) {
    // Shift on a corner: one factor for both axes, the pointer's travel projected onto the
    // box diagonal (the Move box's rule), so the proportions hold.
    const double factor = std::max({(((point.x() - anchor_x) * span_x) + ((point.y() - anchor_y) * span_y)) /
                                        ((span_x * span_x) + (span_y * span_y)),
                                    minimum_x, minimum_y});
    factor_x = factor;
    factor_y = factor;
  } else {
    factor_x = std::max(factor_x, minimum_x);
    factor_y = std::max(factor_y, minimum_y);
  }
  QRectF rect(QPointF(anchor_x + ((start.left() - anchor_x) * factor_x),
                      anchor_y + ((start.top() - anchor_y) * factor_y)),
              QPointF(anchor_x + ((start.right() - anchor_x) * factor_x),
                      anchor_y + ((start.bottom() - anchor_y) * factor_y)));
  if (box_angle != 0.0) {
    const auto turned_center = center + turned(rect.center() - center, box_angle);
    rect.moveCenter(turned_center);
  }
  if (rect == drag.current_rect) {
    return;
  }
  drag.current_rect = rect;
  drag.previewed = true;
  if (drag.pixel_shape) {
    set_pending_pixel_shape_geometry(rect, box_angle);  // the preview follows; nothing is painted yet
    update();
    return;
  }
  const auto state = drag;
  send(state, ShapeResizePhase::Preview);
  update();
}

bool CanvasWidget::move_shape_resize_handle(QMouseEvent* event) {
  if (shape_point_drag_.has_value()) {
    if ((event->buttons() & Qt::LeftButton) == 0) {
      cancel_shape_point_drag();  // the release never arrived
      return true;
    }
    update_shape_point_drag(event->position());
    return true;
  }
  if (shape_resize_drag_.has_value()) {
    if ((event->buttons() & Qt::LeftButton) == 0) {
      // The release never arrived: put the shape back rather than commit a guess.
      cancel_shape_resize_drag();
      return true;
    }
    update_shape_resize_drag(event->position(), event->modifiers());
    if (shape_resize_drag_.has_value()) {
      apply_shape_resize_cursor(event->pos());
    }
    return true;
  }
  if (event->buttons() != Qt::NoButton) {
    return false;
  }
  return apply_shape_resize_cursor(event->pos());
}

bool CanvasWidget::release_shape_resize_handle(QMouseEvent* event) {
  if (shape_point_drag_.has_value() && event->button() == Qt::LeftButton) {
    release_shape_point(event);
    return true;
  }
  if (!shape_resize_drag_.has_value() || event->button() != Qt::LeftButton) {
    return false;
  }
  update_shape_resize_drag(event->position(), event->modifiers());
  if (!shape_resize_drag_.has_value()) {
    return true;  // the preview callback ended the drag (the document was swapped)
  }
  const auto drag = *shape_resize_drag_;
  shape_resize_drag_.reset();
  if (drag.pixel_shape) {
    // The pending shape keeps the box and the angle the drag left it; a click changed nothing.
    if (drag.handle == TransformHandle::Move) {
      clear_move_snap_guides();
    }
    last_mouse_position_ = event->pos();
    update_tool_cursor();
    update();
    return true;
  }
  if (shape_resize_callback_) {
    ShapeBoxEdit edit;
    if (drag.document_path.has_value()) {
      edit.document_path = drag.document_path;
    } else {
      edit.layer_id = drag.layer_id;
    }
    edit.from = drag.start_rect;
    edit.rotate = drag.handle == TransformHandle::Rotate;
    const bool changed = edit.rotate ? drag.angle != drag.start_angle : drag.current_rect != drag.start_rect;
    if (drag.moved && changed) {
      edit.to = drag.current_rect;
      edit.angle_degrees = drag.angle;
      shape_resize_callback_(edit, ShapeResizePhase::Commit, drag.serial);
    } else if (drag.previewed) {
      edit.to = drag.start_rect;
      shape_resize_callback_(edit, ShapeResizePhase::Cancel, drag.serial);
    }
  }
  last_mouse_position_ = event->pos();
  update_tool_cursor();
  update();
  return true;
}

void CanvasWidget::cancel_shape_resize_drag() {
  cancel_shape_point_drag();
  if (!shape_resize_drag_.has_value()) {
    return;
  }
  const auto drag = *shape_resize_drag_;
  shape_resize_drag_.reset();
  if (drag.pixel_shape) {
    if (drag.handle == TransformHandle::Move) {
      clear_move_snap_guides();
    }
    // Esc mid-drag puts the box and its angle back.
    set_pending_pixel_shape_geometry(drag.start_rect, drag.start_angle);
    update_tool_cursor();
    update();
    return;
  }
  if (drag.previewed && shape_resize_callback_) {
    ShapeBoxEdit edit;
    if (drag.document_path.has_value()) {
      edit.document_path = drag.document_path;
    } else {
      edit.layer_id = drag.layer_id;
    }
    edit.from = drag.start_rect;
    edit.to = drag.start_rect;
    edit.rotate = drag.handle == TransformHandle::Rotate;
    shape_resize_callback_(edit, ShapeResizePhase::Cancel, drag.serial);
  }
  update_tool_cursor();
  update();
}

void CanvasWidget::abandon_shape_resize_drag() {
  // The document under the drag was swapped (undo, redo, a history jump): its preview went
  // with it, so nothing is restored and a later call for this drag is stale.
  const bool center_drag = shape_center_drag_active();
  shape_resize_drag_.reset();
  shape_point_drag_.reset();
  if (center_drag) {
    clear_move_snap_guides();  // its alignment guides go with it
  }
}

bool CanvasWidget::apply_shape_resize_cursor(QPoint widget_point) {
  if (!shape_resize_drag_.has_value() && apply_shape_point_cursor(QPointF(widget_point))) {
    return true;
  }
  const auto handle =
      shape_resize_drag_.has_value() ? shape_resize_drag_->handle : shape_resize_handle_at(widget_point);
  if (handle == TransformHandle::None) {
    return false;
  }
  const double angle = shape_resize_box_angle();
  if (handle == TransformHandle::Rotate) {
    // The corner rotate cursor of the Move box and Free Transform, for the corner the
    // pointer is beside.
    if (const auto rect = shape_resize_box_rect(); rect.has_value()) {
      setCursor(transform_rotate_cursor_at(widget_point, *rect, angle, /*stem_counts=*/false));
      return true;
    }
  }
  set_transform_cursor_for_handle(handle, angle);
  return true;
}

void CanvasWidget::draw_shape_resize_frame(QPainter& painter) const {
  const auto rect = shape_resize_box_rect();
  const double angle = rect.has_value() ? shape_resize_box_angle() : 0.0;
  shape_resize_painted_rect_ = rect;
  shape_resize_painted_angle_ = angle;
  if (rect.has_value()) {
    draw_transform_frame(painter, *rect, angle, /*include_rotate=*/false);
  }
}

void CanvasWidget::draw_shape_resize_handles(QPainter& painter) const {
  if (const auto rect = shape_resize_box_rect(); rect.has_value()) {
    draw_transform_handle_squares(painter, *rect, shape_resize_box_angle(), /*include_rotate=*/false);
  }
}

void CanvasWidget::refresh_shape_resize_box() {
  if (!shape_resize_painted_rect_.has_value() && !shape_points_painted_.has_value() && !shape_tool_active()) {
    return;  // nothing drawn and nothing to draw: the common case for every other tool
  }
  const auto rect = shape_resize_box_rect();
  const double angle = rect.has_value() ? shape_resize_box_angle() : 0.0;
  if (rect != shape_resize_painted_rect_ || (rect.has_value() && angle != shape_resize_painted_angle_)) {
    // The squares are 8 px with a 1 px outline, centered on the box's corners and sides.
    constexpr int kHandleReach = 8;
    const std::pair<std::optional<QRectF>, double> boxes[] = {{shape_resize_painted_rect_, shape_resize_painted_angle_},
                                                              {rect, angle}};
    for (const auto& [box, box_angle] : boxes) {
      if (box.has_value()) {
        update(widget_rect_for_document_rect(turned_rect_bounds(*box, box_angle))
                   .toAlignedRect()
                   .adjusted(-kHandleReach, -kHandleReach, kHandleReach, kHandleReach));
      }
    }
  }
  // The drag points of a live Polygon or Line (canvas_widget_shape_points.cpp): discs of
  // 6 px with a ring, where they were last painted and where they are now.
  std::optional<std::pair<QPointF, QPointF>> points_now;
  if (const auto points = shape_live_points(); points.has_value()) {
    points_now = std::make_pair(widget_position_f(points->first), widget_position_f(points->second));
  }
  if (points_now != shape_points_painted_) {
    constexpr double kRepaintReach = 10.0;  // the disc, its ring and antialiasing
    for (const auto& pair : {shape_points_painted_, points_now}) {
      if (pair.has_value()) {
        for (const auto& at : {pair->first, pair->second}) {
          update(QRectF(at.x() - kRepaintReach, at.y() - kRepaintReach, kRepaintReach * 2.0, kRepaintReach * 2.0)
                     .toAlignedRect());
        }
      }
    }
  }
}

}  // namespace patchy::ui
