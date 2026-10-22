// CanvasWidget's Perspective Crop session (GitHub issue 328, docs/crop-tool.md). A
// drag lays out a box whose four corners then move one at a time, or four clicks
// place the corners directly; the inside of a finished quad moves it whole. Enter
// or Apply hands the quad to MainWindow through the commit callback, which rectifies
// the document onto a rectangle (perspective_crop_document); Esc and the X clear the
// quad. A tool switch, an edit lock or a document swap cancel it, like Crop.

#include "ui/canvas_widget.hpp"

#include "ui/canvas_types.hpp"
#include "ui/tool_cursors.hpp"

#include <QApplication>
#include <QColor>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QTransform>
#include <Qt>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <optional>
#include <utility>

namespace patchy::ui {

namespace {

constexpr double kPerspectiveCropHandleSize = 8.0;
// Widget pixels around a corner that grab it (the handle square plus a margin).
constexpr double kPerspectiveCropHandleReach = 7.0;

// Corners in output order: the one nearest the top left first, then clockwise on
// screen, so the result is upright whichever corner was clicked first and in which
// direction the clicks went round.
std::array<QPointF, 4> upright_quad(std::array<QPointF, 4> quad) {
  double signed_area = 0.0;
  for (std::size_t i = 0; i < 4U; ++i) {
    const auto& a = quad[i];
    const auto& b = quad[(i + 1U) % 4U];
    signed_area += a.x() * b.y() - b.x() * a.y();
  }
  if (signed_area < 0.0) {
    // Counterclockwise on screen (y down): reverse, keeping the first corner.
    std::swap(quad[1], quad[3]);
  }
  std::size_t first = 0;
  for (std::size_t i = 1; i < 4U; ++i) {
    if (quad[i].x() + quad[i].y() < quad[first].x() + quad[first].y()) {
      first = i;
    }
  }
  std::rotate(quad.begin(), quad.begin() + static_cast<std::ptrdiff_t>(first), quad.end());
  return quad;
}

}  // namespace

bool CanvasWidget::perspective_crop_session_active() const noexcept {
  return perspective_crop_points_.size() == 4U;
}

bool CanvasWidget::perspective_crop_session_pending() const noexcept {
  return !perspective_crop_points_.empty() || perspective_crop_dragging_out_;
}

std::optional<std::array<QPointF, 4>> CanvasWidget::perspective_crop_quad() const {
  if (!perspective_crop_session_active()) {
    return std::nullopt;
  }
  return std::array<QPointF, 4>{perspective_crop_points_[0], perspective_crop_points_[1],
                                perspective_crop_points_[2], perspective_crop_points_[3]};
}

bool CanvasWidget::set_perspective_crop_quad(const std::array<QPointF, 4>& quad) {
  if (tool_ != CanvasTool::PerspectiveCrop || document_ == nullptr || edit_locked_ ||
      transform_quad_winding(quad) == 0) {
    return false;
  }
  const auto upright = upright_quad(quad);
  perspective_crop_points_.assign(upright.begin(), upright.end());
  perspective_crop_dragging_out_ = false;
  perspective_crop_drag_corner_ = -1;
  update_tool_cursor();
  update();
  notify_crop_session_changed();
  return true;
}

void CanvasWidget::set_perspective_crop_commit_requested_callback(
    std::function<void(std::array<QPointF, 4>)> callback) {
  perspective_crop_commit_requested_callback_ = std::move(callback);
}

void CanvasWidget::set_perspective_crop_show_grid(bool show) {
  if (perspective_crop_show_grid_ == show) {
    return;
  }
  perspective_crop_show_grid_ = show;
  if (perspective_crop_session_active()) {
    update();
  }
}

bool CanvasWidget::perspective_crop_show_grid() const noexcept {
  return perspective_crop_show_grid_;
}

void CanvasWidget::commit_perspective_crop_session() {
  const auto quad = perspective_crop_quad();
  if (!quad.has_value()) {
    if (status_callback_) {
      status_callback_(tr("Nothing to crop: place four corners first"));
    }
    return;
  }
  // The quad stays until MainWindow cancels it after a successful commit, so a
  // refusal (smart filters, artboards) leaves it adjustable.
  if (perspective_crop_commit_requested_callback_) {
    perspective_crop_commit_requested_callback_(*quad);
  }
}

void CanvasWidget::cancel_perspective_crop_session() {
  if (!perspective_crop_session_pending()) {
    return;
  }
  const auto had_quad = perspective_crop_session_active();
  perspective_crop_points_.clear();
  perspective_crop_dragging_out_ = false;
  perspective_crop_drag_corner_ = -1;
  update_tool_cursor();
  update();
  if (had_quad && status_callback_) {
    status_callback_(tr("Perspective crop cancelled"));
  }
  notify_crop_session_changed();
}

int CanvasWidget::perspective_crop_corner_at(QPoint widget_point) const {
  if (!perspective_crop_session_active()) {
    return -1;
  }
  int best = -1;
  double best_distance = kPerspectiveCropHandleReach;
  for (std::size_t i = 0; i < 4U; ++i) {
    const auto corner = widget_position_f(perspective_crop_points_[i]);
    const auto distance = std::max(std::abs(corner.x() - widget_point.x()), std::abs(corner.y() - widget_point.y()));
    if (distance <= best_distance) {
      best_distance = distance;
      best = static_cast<int>(i);
    }
  }
  return best;
}

bool CanvasWidget::perspective_crop_quad_contains(QPoint widget_point) const {
  if (!perspective_crop_session_active()) {
    return false;
  }
  QPolygonF polygon;
  for (const auto& point : perspective_crop_points_) {
    polygon << widget_position_f(point);
  }
  return polygon.containsPoint(QPointF(widget_point), Qt::OddEvenFill);
}

void CanvasWidget::handle_perspective_crop_press(QMouseEvent* event) {
  const auto document_point = document_position_f(event->position());
  if (perspective_crop_session_active()) {
    // A corner moves on its own; the inside moves the whole quad. A press
    // elsewhere keeps the quad (Esc clears it).
    auto corner = perspective_crop_corner_at(event->pos());
    if (corner < 0 && perspective_crop_quad_contains(event->pos())) {
      corner = 4;
    }
    if (corner < 0) {
      return;
    }
    perspective_crop_drag_corner_ = corner;
    perspective_crop_drag_start_point_ = document_point;
    std::copy(perspective_crop_points_.begin(), perspective_crop_points_.end(),
              perspective_crop_drag_start_quad_.begin());
    update_tool_cursor();
    return;
  }
  if (!perspective_crop_points_.empty()) {
    // Click placement: the next corner. The fourth closes the quad.
    perspective_crop_points_.push_back(document_point);
    perspective_crop_hover_document_ = document_point;
    if (perspective_crop_points_.size() == 4U) {
      const std::array<QPointF, 4> quad{perspective_crop_points_[0], perspective_crop_points_[1],
                                        perspective_crop_points_[2], perspective_crop_points_[3]};
      if (transform_quad_winding(quad) == 0) {
        perspective_crop_points_.pop_back();
        report_status_error(tr("The corners must form a convex shape"));
      } else {
        const auto upright = upright_quad(quad);
        perspective_crop_points_.assign(upright.begin(), upright.end());
      }
    }
    update_tool_cursor();
    update();
    notify_crop_session_changed();
    return;
  }
  // First press: a drag lays out a box, a click places the first corner.
  perspective_crop_dragging_out_ = true;
  perspective_crop_press_widget_point_ = event->pos();
  perspective_crop_anchor_document_ = document_point;
  perspective_crop_hover_document_ = document_point;
  update();
}

void CanvasWidget::update_perspective_crop_drag(QPointF document_point) {
  perspective_crop_hover_document_ = document_point;
  if (perspective_crop_dragging_out_) {
    update();
    return;
  }
  if (perspective_crop_drag_corner_ < 0 || !perspective_crop_session_active()) {
    // Only corners being clicked in draw a rubber band to the pointer; a hover over
    // a finished quad changes nothing on screen.
    if (!perspective_crop_session_active()) {
      update();
    }
    return;
  }
  const auto delta = document_point - perspective_crop_drag_start_point_;
  auto quad = perspective_crop_drag_start_quad_;
  if (perspective_crop_drag_corner_ == 4) {
    for (auto& corner : quad) {
      corner += delta;
    }
  } else {
    quad[static_cast<std::size_t>(perspective_crop_drag_corner_)] += delta;
    // The quad never folds: a frame that would make it concave or turn it inside
    // out keeps the last good shape (Free Transform's distort rule).
    if (transform_quad_winding(quad) != transform_quad_winding(perspective_crop_drag_start_quad_)) {
      return;
    }
  }
  std::copy(quad.begin(), quad.end(), perspective_crop_points_.begin());
  update();
}

void CanvasWidget::finish_perspective_crop_mouse_release(QMouseEvent* event) {
  if (perspective_crop_dragging_out_) {
    perspective_crop_dragging_out_ = false;
    const auto widget_delta = event->pos() - perspective_crop_press_widget_point_;
    const auto end = document_position_f(event->position());
    if (widget_delta.manhattanLength() < QApplication::startDragDistance()) {
      perspective_crop_points_ = {perspective_crop_anchor_document_};
      perspective_crop_hover_document_ = end;
    } else {
      const QRectF box = QRectF(perspective_crop_anchor_document_, end).normalized();
      perspective_crop_points_ = {box.topLeft(), box.topRight(), box.bottomRight(), box.bottomLeft()};
    }
    update_tool_cursor();
    update();
    notify_crop_session_changed();
    return;
  }
  if (perspective_crop_drag_corner_ >= 0) {
    update_perspective_crop_drag(document_position_f(event->position()));
    perspective_crop_drag_corner_ = -1;
    if (perspective_crop_session_active()) {
      const auto upright = upright_quad(*perspective_crop_quad());
      perspective_crop_points_.assign(upright.begin(), upright.end());
    }
    update_tool_cursor();
    update();
    notify_crop_session_changed();
  }
}

void CanvasWidget::update_perspective_crop_cursor(QPoint widget_point) {
  if (perspective_crop_drag_corner_ == 4 || (perspective_crop_drag_corner_ < 0 &&
                                             perspective_crop_corner_at(widget_point) < 0 &&
                                             perspective_crop_quad_contains(widget_point))) {
    setCursor(Qt::SizeAllCursor);
    return;
  }
  if (perspective_crop_drag_corner_ >= 0 || perspective_crop_corner_at(widget_point) >= 0) {
    setCursor(transform_gesture_cursor(TransformGestureCursor::Distort));
    return;
  }
  setCursor(Qt::CrossCursor);
}

void CanvasWidget::draw_perspective_crop_overlay(QPainter& painter) const {
  if (tool_ != CanvasTool::PerspectiveCrop || !perspective_crop_session_pending()) {
    return;
  }
  // Same unthemed overlay inks as the Crop box (docs/crop-tool.md): they sit on
  // artwork, not chrome.
  const QColor box_color(95, 170, 255);
  const QColor handle_ink(10, 14, 20);
  const QColor handle_fill(245, 248, 252);

  QPolygonF quad;
  if (perspective_crop_dragging_out_) {
    const auto box = QRectF(widget_position_f(perspective_crop_anchor_document_),
                            widget_position_f(perspective_crop_hover_document_))
                         .normalized();
    quad << box.topLeft() << box.topRight() << box.bottomRight() << box.bottomLeft();
  } else {
    for (const auto& point : perspective_crop_points_) {
      quad << widget_position_f(point);
    }
  }

  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);
  if (!perspective_crop_dragging_out_ && !perspective_crop_session_active()) {
    // Corners being clicked in: the placed outline plus a rubber band to the pointer.
    QPolygonF band = quad;
    band << widget_position_f(perspective_crop_hover_document_);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(box_color, 1.0, Qt::DashLine));
    painter.drawPolyline(band);
  } else {
    const auto& options = crop_overlay_options_;
    if (options.show_cropped_area && options.shield_enabled && options.shield_opacity_percent > 0) {
      QColor shield_color = options.shield_color;
      shield_color.setAlpha(
          static_cast<int>(std::lround(std::clamp(options.shield_opacity_percent, 0, 100) * 255.0 / 100.0)));
      QPainterPath shield;
      shield.addRect(QRectF(rect()));
      QPainterPath inside;
      inside.addPolygon(quad);
      inside.closeSubpath();
      painter.fillPath(shield.subtracted(inside), shield_color);
    }
    QTransform unit_to_widget;
    if (perspective_crop_show_grid_ && QTransform::squareToQuad(quad, unit_to_widget)) {
      // Lines of the rectified grid, straight under the homography; about one
      // per 40 widget pixels of the longer side.
      double longest = 0.0;
      for (int i = 0; i < 4; ++i) {
        longest = std::max(longest, QLineF(quad[i], quad[(i + 1) % 4]).length());
      }
      const int divisions = std::clamp(static_cast<int>(longest / 40.0), 2, 12);
      QPen dark_pen(QColor(10, 14, 20, 110), 1.0);
      dark_pen.setCosmetic(true);
      QPen light_pen(QColor(245, 248, 252, 110), 1.0);
      light_pen.setCosmetic(true);
      for (int pass = 0; pass < 2; ++pass) {
        // Dark under light, the dark twin one pixel down and right.
        painter.setPen(pass == 0 ? dark_pen : light_pen);
        const QPointF nudge = pass == 0 ? QPointF(1.0, 1.0) : QPointF();
        for (int i = 1; i < divisions; ++i) {
          const double t = static_cast<double>(i) / divisions;
          painter.drawLine(QLineF(unit_to_widget.map(QPointF(t, 0.0)), unit_to_widget.map(QPointF(t, 1.0)))
                               .translated(nudge));
          painter.drawLine(QLineF(unit_to_widget.map(QPointF(0.0, t)), unit_to_widget.map(QPointF(1.0, t)))
                               .translated(nudge));
        }
      }
    }
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(box_color, 1.0, Qt::DashLine));
    painter.drawPolygon(quad);
  }
  if (!perspective_crop_dragging_out_) {
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(QPen(handle_ink, 1.0));
    painter.setBrush(handle_fill);
    for (const auto& point : quad) {
      painter.drawRect(QRectF(point.x() - kPerspectiveCropHandleSize / 2.0,
                              point.y() - kPerspectiveCropHandleSize / 2.0, kPerspectiveCropHandleSize,
                              kPerspectiveCropHandleSize));
    }
  }
  painter.restore();
}

}  // namespace patchy::ui
