// CanvasWidget's Crop tool session. Picking the tool frames the canvas (or the
// active selection) with handles; a drag inside that default box lays out a
// new document-space rect (which may extend past the canvas onto the
// pasteboard), handles adjust it, and Enter/Apply hands the rect to MainWindow
// through the crop-commit callback; the document itself is only mutated there.
// Escape resets the box to the canvas; a tool switch, an edit lock, or a
// document swap cancel the session without committing (the swap then frames
// the new document).

#include "ui/canvas_widget.hpp"
#include "ui/canvas_widget_shared.hpp"
#include "ui/tool_cursors.hpp"

#include <QApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>
#include <QTransform>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <optional>

namespace patchy::ui {

namespace {

constexpr double kDegreesPerRadian = 180.0 / std::numbers::pi;

// Normalizes to (-180, 180] so the readout and snap behave near the wrap.
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

bool CanvasWidget::crop_session_active() const noexcept {
  return crop_session_active_;
}

double CanvasWidget::crop_session_angle() const noexcept {
  return crop_angle_;
}

std::optional<QRect> CanvasWidget::crop_session_rect() const noexcept {
  if (!crop_session_active_) {
    return std::nullopt;
  }
  return crop_rect_;
}

bool CanvasWidget::crop_session_has_changes() const noexcept {
  return crop_session_active_ && (crop_angle_ != 0.0 || crop_rect_ != canvas_document_rect());
}

QRect CanvasWidget::canvas_document_rect() const noexcept {
  return document_ != nullptr ? QRect(0, 0, document_->width(), document_->height()) : QRect();
}

QRect CanvasWidget::ratio_fitted_crop_rect(QRect within) const {
  if (crop_ratio_w_ <= 0.0 || crop_ratio_h_ <= 0.0 || within.isEmpty()) {
    return within;
  }
  const auto ratio = crop_ratio_w_ / crop_ratio_h_;
  auto width = within.width();
  auto height = within.height();
  if (static_cast<double>(width) / static_cast<double>(height) > ratio) {
    width = std::max(1, static_cast<int>(std::lround(height * ratio)));
  } else {
    height = std::max(1, static_cast<int>(std::lround(width / ratio)));
  }
  return QRect(within.x() + (within.width() - width) / 2, within.y() + (within.height() - height) / 2, width,
               height);
}

void CanvasWidget::set_crop_ratio(double width, double height) noexcept {
  const auto new_w = std::max(0.0, width);
  const auto new_h = std::max(0.0, height);
  const auto changed = new_w != crop_ratio_w_ || new_h != crop_ratio_h_;
  crop_ratio_w_ = new_w;
  crop_ratio_h_ = new_h;
  // A new ratio reshapes the pending box at once, so typing a ratio or picking
  // a preset shows the result (GitHub issue 66). The automatic canvas frame
  // re-fits from the whole canvas (so Clear grows it back); a custom box keeps
  // its width and center and derives the height, which makes the result
  // independent of the intermediate ratios a field passes through while it is
  // typed digit by digit (fitting inside itself would shrink cumulatively).
  // Clear leaves a custom box alone.
  if (!changed || !crop_session_active_) {
    return;
  }
  const auto constrained = crop_ratio_w_ > 0.0 && crop_ratio_h_ > 0.0;
  QRect fitted = crop_rect_;
  if (crop_box_is_default_) {
    fitted = ratio_fitted_crop_rect(canvas_document_rect());
  } else if (constrained) {
    const auto derived_height =
        std::max(1, static_cast<int>(std::lround(crop_rect_.width() * crop_ratio_h_ / crop_ratio_w_)));
    fitted = QRect(crop_rect_.x(), crop_rect_.y() + (crop_rect_.height() - derived_height) / 2,
                   crop_rect_.width(), derived_height);
  }
  if (fitted != crop_rect_) {
    crop_rect_ = fitted;
    update();
    notify_crop_session_changed();
  }
}

double CanvasWidget::crop_ratio_width() const noexcept {
  return crop_ratio_w_;
}

double CanvasWidget::crop_ratio_height() const noexcept {
  return crop_ratio_h_;
}

void CanvasWidget::set_crop_commit_requested_callback(std::function<void(QRect, double)> callback) {
  crop_commit_requested_callback_ = std::move(callback);
}

void CanvasWidget::set_crop_session_changed_callback(std::function<void()> callback) {
  crop_session_changed_callback_ = std::move(callback);
}

void CanvasWidget::notify_crop_session_changed() {
  if (crop_session_changed_callback_) {
    crop_session_changed_callback_();
  }
}

void CanvasWidget::reset_crop_session_state() {
  crop_session_active_ = false;
  crop_box_is_default_ = false;
  crop_box_from_selection_ = false;
  crop_dragging_out_ = false;
  crop_rotating_ = false;
  crop_drag_handle_ = TransformHandle::None;
  crop_anchor_document_ = QPoint();
  crop_current_document_ = QPoint();
  crop_press_widget_point_ = QPoint();
  crop_rect_ = QRect();
  crop_drag_start_rect_ = QRect();
  crop_drag_start_point_ = QPointF();
  crop_angle_ = 0.0;
  crop_rotate_start_angle_ = 0.0;
  crop_rotate_start_vector_degrees_ = 0.0;
  crop_square_constrained_ = false;
}

void CanvasWidget::commit_crop_session() {
  if (!crop_session_active_ || crop_rect_.isEmpty()) {
    return;
  }
  // The default canvas frame would re-create the same document and leave a
  // pointless undo step behind.
  if (!crop_session_has_changes()) {
    if (status_callback_) {
      status_callback_(tr("Nothing to crop: the box matches the canvas"));
    }
    return;
  }
  // State is left intact: a refused commit (smart filters, unparsed smart
  // objects) keeps the session adjustable; a successful one cancels it from
  // MainWindow before the document reset (which frames the new canvas).
  if (crop_commit_requested_callback_) {
    crop_commit_requested_callback_(crop_rect_, crop_angle_);
  }
}

void CanvasWidget::cancel_crop_session() {
  const auto had_session = crop_session_active_;
  if (!had_session && !crop_dragging_out_ && !crop_rotating_ &&
      crop_drag_handle_ == TransformHandle::None) {
    return;
  }
  // Leaving the tool with the untouched canvas frame is not worth a message.
  const auto had_changes = crop_session_has_changes();
  reset_crop_session_state();
  update_tool_cursor();
  update();
  if (had_changes && status_callback_) {
    status_callback_(tr("Crop cancelled"));
  }
  notify_crop_session_changed();
}

void CanvasWidget::begin_default_crop_session() {
  if (tool_ != CanvasTool::Crop || document_ == nullptr || edit_locked_) {
    return;
  }
  reset_crop_session_state();
  crop_session_active_ = true;
  if (!selection_.isEmpty() && !quick_mask_active_) {
    // The active selection becomes the crop box, overriding the ratio for this
    // box (GitHub issue 66 item 5); the marching ants hide while the session
    // runs so the box is the only outline. A commit clears the selection, a
    // tool switch brings it back.
    crop_rect_ = selection_.boundingRect();
    crop_box_is_default_ = false;
    crop_box_from_selection_ = true;
  } else {
    crop_rect_ = ratio_fitted_crop_rect(canvas_document_rect());
    crop_box_is_default_ = true;
  }
  update_tool_cursor();
  update();
  notify_crop_session_changed();
}

void CanvasWidget::follow_selection_into_crop_session() {
  if (tool_ != CanvasTool::Crop || !crop_session_active_ || crop_dragging_out_ || crop_rotating_ ||
      crop_drag_handle_ != TransformHandle::None) {
    return;
  }
  if (!crop_box_is_default_ && !crop_box_from_selection_) {
    return;
  }
  begin_default_crop_session();
}

void CanvasWidget::set_crop_session_size(QSize size) {
  if (!crop_session_active_ || size.width() < 1 || size.height() < 1) {
    return;
  }
  const QRect rect(crop_rect_.x() + (crop_rect_.width() - size.width()) / 2,
                   crop_rect_.y() + (crop_rect_.height() - size.height()) / 2, size.width(), size.height());
  if (rect == crop_rect_) {
    return;
  }
  crop_rect_ = rect;
  crop_box_is_default_ = false;
  update();
  notify_crop_session_changed();
}

void CanvasWidget::reset_crop_session_to_canvas() {
  if (tool_ != CanvasTool::Crop || document_ == nullptr || edit_locked_) {
    cancel_crop_session();
    return;
  }
  const auto had_changes = crop_session_has_changes();
  reset_crop_session_state();
  crop_session_active_ = true;
  crop_box_is_default_ = true;
  crop_rect_ = ratio_fitted_crop_rect(canvas_document_rect());
  update_tool_cursor();
  update();
  if (had_changes && status_callback_) {
    status_callback_(tr("Crop box reset to the canvas"));
  }
  notify_crop_session_changed();
}

// The third deliberately-unmerged twin of marquee_selection_rect and
// shape_drag_rect (docs/tools.md): crop has no combine modes or fixed size,
// but its anchor may live on the pasteboard, so nothing is edge-clamped.
QRect CanvasWidget::crop_drag_rect(QPoint anchor, QPoint current) const {
  QRect rect;
  if (crop_ratio_w_ > 0.0 && crop_ratio_h_ > 0.0) {
    const auto ratio = crop_ratio_w_ / crop_ratio_h_;
    const auto delta = current - anchor;
    auto width = std::max(1, std::abs(delta.x()));
    auto height = std::max(1, std::abs(delta.y()));
    if (static_cast<double>(width) / static_cast<double>(height) > ratio) {
      width = std::max(1, static_cast<int>(std::round(height * ratio)));
    } else {
      height = std::max(1, static_cast<int>(std::round(width / ratio)));
    }
    const auto signed_width = delta.x() < 0 ? -width : width;
    const auto signed_height = delta.y() < 0 ? -height : height;
    rect = QRect(anchor, anchor + QPoint(signed_width, signed_height)).normalized();
  } else if (crop_square_constrained_) {
    const auto delta = current - anchor;
    const auto side = std::max(1, std::min(std::abs(delta.x()), std::abs(delta.y())));
    const auto x = delta.x() < 0 ? anchor.x() - side : anchor.x();
    const auto y = delta.y() < 0 ? anchor.y() - side : anchor.y();
    rect = QRect(x, y, side, side);
  } else {
    rect = normalized_rect(anchor, current);
  }
  // Keep at least 1px per axis (QRect::normalized() collapses to 0-size when
  // the cursor lands exactly 1px before the anchor).
  if (rect.width() < 1) {
    rect.setWidth(1);
  }
  if (rect.height() < 1) {
    rect.setHeight(1);
  }
  return rect;
}

CanvasWidget::TransformHandle CanvasWidget::crop_handle_at(QPoint widget_point) const {
  if (!crop_session_active_) {
    return TransformHandle::None;
  }
  const auto handle = transform_handle_at(widget_point, QRectF(crop_rect_), crop_angle_);
  // The shared hit-test reports the rotate stem hanging above the top edge;
  // crop rotates by dragging anywhere outside the box instead, so those
  // clicks must not grab a phantom handle.
  return handle == TransformHandle::Rotate ? TransformHandle::None : handle;
}

void CanvasWidget::begin_crop_drag_out(QMouseEvent* event, QPoint document_point) {
  const auto snapped = snapped_document_point(document_point);
  crop_dragging_out_ = true;
  spacebar_repositioning_drag_rect_ = false;
  crop_press_widget_point_ = event->pos();
  crop_anchor_document_ = snapped;
  crop_current_document_ = snapped;
  crop_angle_ = 0.0;
  crop_square_constrained_ = (event->modifiers() & Qt::ShiftModifier) != 0;
  update();
}

void CanvasWidget::handle_crop_session_press(QMouseEvent* event) {
  const auto handle = crop_handle_at(event->pos());
  const auto on_handle = handle != TransformHandle::None && handle != TransformHandle::Move;
  if (crop_box_is_default_ && !on_handle &&
      canvas_document_rect().contains(document_position(event->pos()))) {
    // The automatic canvas frame has no interior to move: a press on the
    // canvas (inside the frame or beside a ratio-fitted one) lays out a new
    // box; a mere click keeps the frame. The handles work as on any box.
    begin_crop_drag_out(event, document_position(event->pos()));
    return;
  }
  if (handle == TransformHandle::None) {
    // A press off the box starts the rotate gesture (Photoshop's straighten):
    // the box pivots about its center to follow the drag. A plain click (no
    // travel) leaves the angle untouched. Esc resets to the canvas frame, where
    // a drag lays out a new rect.
    crop_rotating_ = true;
    crop_press_widget_point_ = event->pos();
    crop_rotate_start_angle_ = crop_angle_;
    setCursor(crop_rotate_cursor());
    const auto document_point = document_position_f(event->position());
    const auto center_x = static_cast<double>(crop_rect_.x()) + crop_rect_.width() / 2.0;
    const auto center_y = static_cast<double>(crop_rect_.y()) + crop_rect_.height() / 2.0;
    crop_rotate_start_vector_degrees_ =
        std::atan2(document_point.y() - center_y, document_point.x() - center_x) * kDegreesPerRadian;
    return;
  }
  crop_drag_handle_ = handle;
  crop_drag_start_rect_ = crop_rect_;
  crop_drag_start_point_ = document_position_f(event->position());
}

void CanvasWidget::update_crop_rotate_drag(QPointF document_point, Qt::KeyboardModifiers modifiers) {
  if (!crop_rotating_) {
    return;
  }
  const auto center_x = static_cast<double>(crop_rect_.x()) + crop_rect_.width() / 2.0;
  const auto center_y = static_cast<double>(crop_rect_.y()) + crop_rect_.height() / 2.0;
  const auto vector_degrees =
      std::atan2(document_point.y() - center_y, document_point.x() - center_x) * kDegreesPerRadian;
  auto degrees =
      normalized_degrees(crop_rotate_start_angle_ + vector_degrees - crop_rotate_start_vector_degrees_);
  // Shift snaps to 15 degrees, matching the transform rotate handle.
  if ((modifiers & Qt::ShiftModifier) != 0) {
    degrees = normalized_degrees(std::round(degrees / 15.0) * 15.0);
  }
  crop_angle_ = degrees;
  update();
}

void CanvasWidget::update_crop_drag_out(QPoint document_point) {
  if (spacebar_repositioning_drag_rect_) {
    const auto delta = document_point - spacebar_reposition_last_document_position_;
    crop_anchor_document_ += delta;
    crop_current_document_ += delta;
    spacebar_reposition_last_document_position_ = document_point;
  } else {
    crop_current_document_ = snapped_marquee_current_point(crop_anchor_document_, document_point);
  }
  update();
}

void CanvasWidget::update_crop_adjust_drag(QPointF document_point, Qt::KeyboardModifiers modifiers) {
  if (crop_drag_handle_ == TransformHandle::None) {
    return;
  }

  const auto rotated = crop_angle_ != 0.0;
  if (spacebar_repositioning_drag_rect_) {
    // Space held mid-drag slides the whole box (the marquee handle rule). The
    // drag-start rect follows so releasing Space resumes the resize where the
    // box now is; a rotated box moves raw (snapping assumes axis-aligned edges).
    const QPoint rounded(static_cast<int>(std::lround(document_point.x())),
                         static_cast<int>(std::lround(document_point.y())));
    const auto raw_delta = rounded - spacebar_reposition_origin_document_position_;
    const auto delta = rotated ? raw_delta : snapped_rect_delta(spacebar_reposition_start_marquee_rect_, raw_delta);
    crop_drag_start_rect_ = spacebar_reposition_start_marquee_start_rect_.translated(delta);
    crop_rect_ = spacebar_reposition_start_marquee_rect_.translated(delta);
    update();
    return;
  }
  if (crop_drag_handle_ == TransformHandle::Move) {
    const QPoint raw_delta(static_cast<int>(std::lround(document_point.x() - crop_drag_start_point_.x())),
                           static_cast<int>(std::lround(document_point.y() - crop_drag_start_point_.y())));
    // Guide/grid snapping assumes axis-aligned edges, so a rotated box moves raw.
    crop_rect_ = crop_drag_start_rect_.translated(
        rotated ? raw_delta : snapped_rect_delta(crop_drag_start_rect_, raw_delta));
    update();
    return;
  }

  // Handle drags on a rotated box work in the box's own frame: map the cursor
  // through the inverse rotation about the drag-start center, then run the
  // axis-aligned edge math there (the free-transform approach).
  auto tracked = rotated ? document_point : snapped_document_point_f(document_point);
  if (rotated) {
    const auto radians = crop_angle_ / kDegreesPerRadian;
    const auto cos_theta = std::cos(radians);
    const auto sin_theta = std::sin(radians);
    const auto center_x =
        static_cast<double>(crop_drag_start_rect_.x()) + crop_drag_start_rect_.width() / 2.0;
    const auto center_y =
        static_cast<double>(crop_drag_start_rect_.y()) + crop_drag_start_rect_.height() / 2.0;
    const auto dx = document_point.x() - center_x;
    const auto dy = document_point.y() - center_y;
    tracked = QPointF(center_x + cos_theta * dx + sin_theta * dy,
                      center_y - sin_theta * dx + cos_theta * dy);
  }
  const QPoint point(static_cast<int>(std::lround(tracked.x())), static_cast<int>(std::lround(tracked.y())));
  const auto start = crop_drag_start_rect_;
  const auto moves_left = crop_drag_handle_ == TransformHandle::TopLeft ||
                          crop_drag_handle_ == TransformHandle::Left ||
                          crop_drag_handle_ == TransformHandle::BottomLeft;
  const auto moves_right = crop_drag_handle_ == TransformHandle::TopRight ||
                           crop_drag_handle_ == TransformHandle::Right ||
                           crop_drag_handle_ == TransformHandle::BottomRight;
  const auto moves_top = crop_drag_handle_ == TransformHandle::TopLeft ||
                         crop_drag_handle_ == TransformHandle::Top ||
                         crop_drag_handle_ == TransformHandle::TopRight;
  const auto moves_bottom = crop_drag_handle_ == TransformHandle::BottomLeft ||
                            crop_drag_handle_ == TransformHandle::Bottom ||
                            crop_drag_handle_ == TransformHandle::BottomRight;
  const auto corner = (moves_left || moves_right) && (moves_top || moves_bottom);

  // A set ratio always constrains; otherwise Shift on a corner holds the
  // drag-start aspect. (This is a drag-rect tool, not a transform session, so
  // transform_drag_keeps_aspect deliberately does not apply.)
  double target_ratio = 0.0;
  if (crop_ratio_w_ > 0.0 && crop_ratio_h_ > 0.0) {
    target_ratio = crop_ratio_w_ / crop_ratio_h_;
  } else if (corner && (modifiers & Qt::ShiftModifier) != 0 && start.height() > 0) {
    target_ratio = static_cast<double>(start.width()) / start.height();
  }

  QRect rect;
  if ((modifiers & Qt::AltModifier) != 0) {
    // Alt resizes about the center: the opposite side mirrors the dragged one
    // (GitHub issue 66; read mid-drag, so it works with the Alt pressed after the
    // grab too). A set ratio or Shift still constrains the result.
    rect = center_anchored_handle_rect(start, moves_left || moves_right, moves_top || moves_bottom, point,
                                       target_ratio);
  } else if (target_ratio > 0.0 && corner) {
    const auto anchor_x = moves_left ? start.x() + start.width() : start.x();
    const auto anchor_y = moves_top ? start.y() + start.height() : start.y();
    auto width = std::max(1, std::abs(point.x() - anchor_x));
    auto height = std::max(1, std::abs(point.y() - anchor_y));
    if (static_cast<double>(width) / static_cast<double>(height) > target_ratio) {
      width = std::max(1, static_cast<int>(std::round(height * target_ratio)));
    } else {
      height = std::max(1, static_cast<int>(std::round(width / target_ratio)));
    }
    const auto x = point.x() < anchor_x ? anchor_x - width : anchor_x;
    const auto y = point.y() < anchor_y ? anchor_y - height : anchor_y;
    rect = QRect(x, y, width, height);
  } else if (target_ratio > 0.0) {
    // Ratio-locked edge drag: the dragged axis follows the cursor, the other is
    // derived and re-centered on the start rect.
    if (moves_left || moves_right) {
      const auto anchor_x = moves_left ? start.x() + start.width() : start.x();
      const auto width = std::max(1, std::abs(point.x() - anchor_x));
      const auto height = std::max(1, static_cast<int>(std::round(width / target_ratio)));
      const auto x = point.x() < anchor_x ? anchor_x - width : anchor_x;
      rect = QRect(x, start.y() + start.height() / 2 - height / 2, width, height);
    } else {
      const auto anchor_y = moves_top ? start.y() + start.height() : start.y();
      const auto height = std::max(1, std::abs(point.y() - anchor_y));
      const auto width = std::max(1, static_cast<int>(std::round(height * target_ratio)));
      const auto y = point.y() < anchor_y ? anchor_y - height : anchor_y;
      rect = QRect(start.x() + start.width() / 2 - width / 2, y, width, height);
    }
  } else {
    auto left = start.x();
    auto top = start.y();
    auto right = start.x() + start.width();
    auto bottom = start.y() + start.height();
    if (moves_left) {
      left = point.x();
    }
    if (moves_right) {
      right = point.x();
    }
    if (moves_top) {
      top = point.y();
    }
    if (moves_bottom) {
      bottom = point.y();
    }
    // Dragging through the opposite edge flips cleanly.
    const auto x0 = std::min(left, right);
    const auto y0 = std::min(top, bottom);
    rect = QRect(x0, y0, std::max(1, std::abs(right - left)), std::max(1, std::abs(bottom - top)));
  }

  crop_rect_ = rect;
  update();
}

void CanvasWidget::finish_crop_mouse_release(QMouseEvent* event) {
  if (crop_dragging_out_) {
    const auto was_repositioning = spacebar_repositioning_drag_rect_;
    crop_dragging_out_ = false;
    spacebar_repositioning_drag_rect_ = false;
    const auto widget_delta = event->pos() - crop_press_widget_point_;
    const auto was_click = !was_repositioning &&
                           widget_delta.manhattanLength() < QApplication::startDragDistance();
    if (!was_click) {
      if (!was_repositioning) {
        crop_current_document_ =
            snapped_marquee_current_point(crop_anchor_document_, document_position(event->pos()));
      }
      crop_rect_ = crop_drag_rect(crop_anchor_document_, crop_current_document_);
      crop_session_active_ = true;
      crop_box_is_default_ = false;
      crop_box_from_selection_ = false;
      notify_crop_session_changed();
    }
    // A plain click leaves any pending rect (and the session) untouched.
    crop_square_constrained_ = false;
    update_tool_cursor();
    update();
    return;
  }

  if (crop_rotating_) {
    update_crop_rotate_drag(document_position_f(event->position()), event->modifiers());
    crop_rotating_ = false;
    if (crop_angle_ != 0.0) {
      crop_box_is_default_ = false;
    }
    update_tool_cursor();
    update();
    notify_crop_session_changed();
    return;
  }

  if (crop_drag_handle_ != TransformHandle::None) {
    update_crop_adjust_drag(document_position_f(event->position()), event->modifiers());
    crop_drag_handle_ = TransformHandle::None;
    spacebar_repositioning_drag_rect_ = false;
    crop_box_is_default_ = false;
    update_tool_cursor();
    update();
    notify_crop_session_changed();
  }
}

void CanvasWidget::nudge_crop_rect(QPoint delta) {
  if (!crop_session_active_ || delta.isNull()) {
    return;
  }
  crop_rect_.translate(delta);
  crop_box_is_default_ = false;
  update();
  notify_crop_session_changed();
}

void CanvasWidget::draw_crop_overlay(QPainter& painter) const {
  std::optional<QRect> document_rect;
  double angle = 0.0;
  if (crop_dragging_out_) {
    document_rect = crop_drag_rect(crop_anchor_document_, crop_current_document_);
  } else if (crop_session_active_) {
    document_rect = crop_rect_;
    angle = crop_angle_;
  }
  if (!document_rect.has_value() || document_rect->isEmpty()) {
    return;
  }

  // Exact widget-space box (the +1 doc-rect convention is internal to the
  // QRectF overload); rotation happens about its center in widget space.
  const auto widget_rect = widget_rect_for_document_rect(QRectF(*document_rect));
  const auto center = widget_rect.center();
  QTransform box_to_widget;
  box_to_widget.translate(center.x(), center.y());
  box_to_widget.rotate(angle);
  const QRectF local_rect(-widget_rect.width() / 2.0, -widget_rect.height() / 2.0, widget_rect.width(),
                          widget_rect.height());
  const auto corners = box_to_widget.map(QPolygonF(local_rect));

  // Shield: darken everything outside the pending crop. Deliberately unthemed
  // black-over-artwork, like the marching ants (docs/ui-conventions.md).
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, false);
  QPainterPath shield;
  shield.addRect(QRectF(rect()));
  QPainterPath box_path;
  box_path.addPolygon(corners);
  box_path.closeSubpath();
  painter.fillPath(shield.subtracted(box_path), QColor(0, 0, 0, 120));
  painter.restore();

  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, angle != 0.0);
  painter.setTransform(box_to_widget, true);

  // Rule-of-thirds guides, as a dark+light pair so they read on any artwork;
  // skipped when the rect is too small for them to help.
  if (widget_rect.width() >= 24.0 && widget_rect.height() >= 24.0) {
    QPen dark_pen(QColor(10, 14, 20, 110), 1.0);
    dark_pen.setCosmetic(true);
    QPen light_pen(QColor(245, 248, 252, 110), 1.0);
    light_pen.setCosmetic(true);
    for (int i = 1; i <= 2; ++i) {
      const auto x = local_rect.left() + local_rect.width() * i / 3.0;
      const auto y = local_rect.top() + local_rect.height() * i / 3.0;
      painter.setPen(dark_pen);
      painter.drawLine(QPointF(x + 1.0, local_rect.top()), QPointF(x + 1.0, local_rect.bottom()));
      painter.drawLine(QPointF(local_rect.left(), y + 1.0), QPointF(local_rect.right(), y + 1.0));
      painter.setPen(light_pen);
      painter.drawLine(QPointF(x, local_rect.top()), QPointF(x, local_rect.bottom()));
      painter.drawLine(QPointF(local_rect.left(), y), QPointF(local_rect.right(), y));
    }
  }

  painter.setBrush(Qt::NoBrush);
  painter.setPen(QPen(QColor(95, 170, 255), 1.0, Qt::DashLine));
  painter.drawRect(local_rect);
  painter.restore();

  // Handles appear once a rect is pending; a drag-out in flight shows only the
  // rect and shield.
  if (!crop_dragging_out_) {
    constexpr double kHandleSize = 8.0;
    const std::array<TransformHandle, 8> handles = {
        TransformHandle::TopLeft,    TransformHandle::Top,    TransformHandle::TopRight,
        TransformHandle::Right,      TransformHandle::BottomRight, TransformHandle::Bottom,
        TransformHandle::BottomLeft, TransformHandle::Left};
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, angle != 0.0);
    painter.setPen(QPen(QColor(10, 14, 20), 1.0));
    painter.setBrush(QColor(245, 248, 252));
    for (const auto handle : handles) {
      const auto point = transform_handle_position(handle, QRectF(*document_rect), angle);
      painter.drawRect(
          QRectF(point.x() - kHandleSize / 2.0, point.y() - kHandleSize / 2.0, kHandleSize, kHandleSize));
    }
    painter.restore();
  }
}

}  // namespace patchy::ui
