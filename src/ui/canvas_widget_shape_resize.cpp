// CanvasWidget's resize handles for the shape tools (GitHub issue 690,
// docs/vector-tools-shape-handles.md, "Resize handles on the shape tools"). With Rectangle,
// Ellipse, Polygon, Line or Custom Shape active in Shape mode and an editable shape layer
// active, the box the options-bar W / H fields describe is drawn with the Move tool's
// transform controls (the dashed frame under the shape's outline and eight squares over it,
// no rotate stem). A press on a handle square resizes the shape: Shift keeps the proportions
// on a corner, Alt resizes about the center, Esc cancels. Any other press still draws a new
// shape. The drag previews through MainWindow and lands as one "Shape size" undo step, the
// scale the W / H fields apply, so live shapes stay live.
//
// A live Polygon with a square box and a live Line show their two drag points in place of
// the box (canvas_widget_shape_points.cpp); those share the entry points below.
//
// This is a plain bounding-box resize only. Nothing here edits a shape parameter (corner
// radius, side count, star inset): those stay in the options bar and the Properties panel
// (docs/vector-tools.md, "Patents and trademarks").

#include "ui/canvas_widget.hpp"

#include "core/layer_metadata.hpp"
#include "core/layer_tree.hpp"
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

}  // namespace

void CanvasWidget::set_shape_resize_callback(ShapeResizeCallback callback) {
  shape_resize_callback_ = std::move(callback);
}

const Layer* CanvasWidget::shape_resize_layer() const {
  if (document_ == nullptr) {
    return nullptr;
  }
  const auto& document = std::as_const(*document_);
  if (shape_resize_drag_.has_value()) {
    return document.find_layer(shape_resize_drag_->layer_id);
  }
  if (shape_point_drag_.has_value()) {
    return document.find_layer(shape_point_drag_->layer_id);
  }
  // Shape mode only: Path and Pixels modes draw no shape layer, and the W / H fields the
  // box mirrors show in Shape mode alone.
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
  const auto* layer = shape_resize_layer();
  if (layer == nullptr) {
    return std::nullopt;
  }
  // A live Polygon or Line shows its two drag points instead of the box.
  if (shape_live_points_of(*layer).has_value()) {
    return std::nullopt;
  }
  const auto& path = layer->vector_shape()->path;
  // Components Path Select picked are what the W / H fields size; every component (or
  // none) is the whole shape.
  const auto rect = component_bounds(path, path_edit_selected_groups());
  // A path with no area along an axis (a straight two-point line) has no box to grab.
  if (!rect.has_value() || rect->width() <= kDegenerateExtent || rect->height() <= kDegenerateExtent) {
    return std::nullopt;
  }
  return rect;
}

CanvasWidget::TransformHandle CanvasWidget::shape_resize_handle_at(QPoint widget_point) const {
  const auto rect = shape_resize_box_rect();
  if (!rect.has_value()) {
    return TransformHandle::None;
  }
  const auto handle = transform_handle_at(widget_point, *rect, 0.0, TransformHitZones::ResizeHandles);
  // A pending pixel shape moves by a press inside its box; a shape layer's box does not (a
  // press there draws the next shape, and Ctrl moves it).
  if (handle == TransformHandle::None && pending_pixel_shape_ != nullptr && !shape_resize_drag_.has_value() &&
      widget_rect_for_document_rect(*rect).contains(QPointF(widget_point))) {
    return TransformHandle::Move;
  }
  return handle;
}

bool CanvasWidget::press_shape_resize_handle(QMouseEvent* event) {
  if (shape_handle_drag_active()) {
    // A press while a drag is live means its release was lost: put the shape back.
    cancel_shape_resize_drag();
  }
  // The pending pixel shape: a handle resizes it and a press inside its box moves it. A
  // press anywhere else is not taken, so the caller commits the shape and draws the next.
  if (pending_pixel_shape_ != nullptr) {
    const auto rect = shape_resize_box_rect();
    const auto handle = rect.has_value() ? shape_resize_handle_at(event->pos()) : TransformHandle::None;
    if (handle == TransformHandle::None) {
      return false;
    }
    if (edit_locked_) {
      show_edit_locked_message();
      return true;
    }
    ShapeResizeDrag drag;
    drag.pixel_shape = true;
    drag.handle = handle;
    drag.start_rect = *rect;
    drag.current_rect = *rect;
    drag.press_widget = event->position();
    drag.pointer_widget = event->position();
    if (handle != TransformHandle::Move) {
      drag.grab_offset = transform_handle_grab_offset(handle, *rect, 0.0, document_position_f(event->position()));
    }
    drag.serial = ++shape_resize_drag_serial_;
    shape_resize_drag_ = drag;
    set_transform_cursor_for_handle(handle);
    update();
    return true;
  }
  // Ctrl is the temporary Path Select and never reaches here; Shift and Alt on a handle
  // constrain the resize, so they do not turn the press into a draw.
  const auto* layer = shape_resize_layer();
  if (layer == nullptr) {
    return false;
  }
  // A live Polygon or Line: its two drag points (canvas_widget_shape_points.cpp).
  if (press_shape_point(event)) {
    return true;
  }
  const auto rect = shape_resize_box_rect();
  if (!rect.has_value()) {
    return false;
  }
  const auto handle = transform_handle_at(event->pos(), *rect, 0.0, TransformHitZones::ResizeHandles);
  if (handle == TransformHandle::None) {
    return false;
  }
  if (edit_locked_) {
    show_edit_locked_message();
    return true;
  }
  ShapeResizeDrag drag;
  drag.layer_id = layer->id();
  drag.handle = handle;
  drag.start_rect = *rect;
  drag.current_rect = *rect;
  drag.press_widget = event->position();
  drag.pointer_widget = event->position();
  drag.grab_offset = transform_handle_grab_offset(handle, *rect, 0.0, document_position_f(event->position()));
  drag.serial = ++shape_resize_drag_serial_;
  shape_resize_drag_ = drag;
  set_transform_cursor_for_handle(handle);
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
  if (handle == TransformHandle::Move) {
    // The pending pixel shape, dragged by its inside: the pointer's travel on whole pixels
    // through the snapping of every whole-shape drag, with both edges and the center of the
    // box as the sources, never a feature chosen from where it was grabbed
    // (docs/legal-constraints.md, "Alignment snapping").
    const auto pointer = document_position_f(widget_point);
    const auto press = document_position_f(drag.press_widget);
    const QPointF travel(std::round(pointer.x() - press.x()), std::round(pointer.y() - press.y()));
    const auto total = snap_whole_shape_drag({start}, {}, travel, false, false);
    const auto moved = start.translated(total);
    if (moved == drag.current_rect) {
      return;
    }
    drag.current_rect = moved;
    drag.previewed = true;
    set_pending_pixel_shape_box(moved);
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
  // through the snap targets, like a Move-box handle.
  const auto point = snapped_document_point_f(document_position_f(widget_point) + drag.grab_offset);
  // Alt scales about the center (the Move box's default reference point); otherwise the
  // opposite side stays put.
  const bool from_center = (modifiers & Qt::AltModifier) != 0;
  const auto center = start.center();
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
  const QRectF rect(QPointF(anchor_x + ((start.left() - anchor_x) * factor_x),
                            anchor_y + ((start.top() - anchor_y) * factor_y)),
                    QPointF(anchor_x + ((start.right() - anchor_x) * factor_x),
                            anchor_y + ((start.bottom() - anchor_y) * factor_y)));
  if (rect == drag.current_rect) {
    return;
  }
  drag.current_rect = rect;
  drag.previewed = true;
  if (drag.pixel_shape) {
    set_pending_pixel_shape_box(rect);  // the preview follows; nothing is painted yet
    update();
    return;
  }
  // The callback re-renders the layer and may repaint; copy what it needs first.
  const auto layer_id = drag.layer_id;
  const auto from = drag.start_rect;
  const auto serial = drag.serial;
  if (shape_resize_callback_) {
    shape_resize_callback_(layer_id, from, rect, ShapeResizePhase::Preview, serial);
  }
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
      set_transform_cursor_for_handle(shape_resize_drag_->handle);
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
    // The pending shape keeps the box the drag left it; a click changed nothing.
    if (drag.handle == TransformHandle::Move) {
      clear_move_snap_guides();
    }
    last_mouse_position_ = event->pos();
    update_tool_cursor();
    update();
    return true;
  }
  if (shape_resize_callback_) {
    if (drag.moved && drag.current_rect != drag.start_rect) {
      shape_resize_callback_(drag.layer_id, drag.start_rect, drag.current_rect, ShapeResizePhase::Commit,
                             drag.serial);
    } else if (drag.previewed) {
      shape_resize_callback_(drag.layer_id, drag.start_rect, drag.start_rect, ShapeResizePhase::Cancel,
                             drag.serial);
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
    set_pending_pixel_shape_box(drag.start_rect);  // Esc mid-drag puts the box back
    update_tool_cursor();
    update();
    return;
  }
  if (drag.previewed && shape_resize_callback_) {
    shape_resize_callback_(drag.layer_id, drag.start_rect, drag.start_rect, ShapeResizePhase::Cancel, drag.serial);
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
  set_transform_cursor_for_handle(handle);
  return true;
}

void CanvasWidget::draw_shape_resize_frame(QPainter& painter) const {
  const auto rect = shape_resize_box_rect();
  shape_resize_painted_rect_ = rect;
  if (rect.has_value()) {
    draw_transform_frame(painter, *rect, 0.0, /*include_rotate=*/false);
  }
}

void CanvasWidget::draw_shape_resize_handles(QPainter& painter) const {
  if (const auto rect = shape_resize_box_rect(); rect.has_value()) {
    draw_transform_handle_squares(painter, *rect, 0.0, /*include_rotate=*/false);
  }
}

void CanvasWidget::refresh_shape_resize_box() {
  if (!shape_resize_painted_rect_.has_value() && !shape_points_painted_.has_value() && !shape_tool_active()) {
    return;  // nothing drawn and nothing to draw: the common case for every other tool
  }
  if (const auto rect = shape_resize_box_rect(); rect != shape_resize_painted_rect_) {
    // The squares are 8 px with a 1 px outline, centered on the box's corners and sides.
    constexpr int kHandleReach = 8;
    for (const auto& box : {shape_resize_painted_rect_, rect}) {
      if (box.has_value()) {
        update(widget_rect_for_document_rect(*box).toAlignedRect().adjusted(-kHandleReach, -kHandleReach,
                                                                           kHandleReach, kHandleReach));
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
