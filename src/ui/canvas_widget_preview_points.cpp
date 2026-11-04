// CanvasWidget's preview point handles: draggable position markers a preview
// dialog puts on the canvas while it is open (the G'MIC Filter Library's point
// parameters, docs/gmic.md). They are plain markers: a position and nothing
// else, with no extent, ring, boundary or region drawn around them
// (docs/legal-constraints-filters.md, "G'MIC engine").

#include "ui/canvas_widget.hpp"

#include "ui/canvas_types.hpp"

#include <QColor>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPointF>
#include <Qt>

#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

constexpr double kHandleRadius = 6.0;
// A press this close to a marker's centre (widget pixels) grabs it.
constexpr double kHandleGrabRadius = 10.0;

}  // namespace

void CanvasWidget::set_preview_point_handles(std::vector<QPointF> document_positions,
                                             std::function<void(int, QPointF, bool)> moved) {
  // A marker being dragged keeps the pointer's position: the owner echoes every
  // move back through here, rounded to its own precision.
  if (preview_point_drag_ >= 0 && preview_point_drag_ < static_cast<int>(document_positions.size()) &&
      preview_point_drag_ < static_cast<int>(preview_points_.size())) {
    document_positions[static_cast<std::size_t>(preview_point_drag_)] =
        preview_points_[static_cast<std::size_t>(preview_point_drag_)];
  } else {
    preview_point_drag_ = -1;
  }
  preview_points_ = std::move(document_positions);
  preview_point_moved_ = std::move(moved);
  update_tool_cursor();
  update();
}

void CanvasWidget::clear_preview_point_handles() {
  if (preview_points_.empty() && !preview_point_moved_) {
    return;
  }
  preview_points_.clear();
  preview_point_moved_ = {};
  preview_point_drag_ = -1;
  update_tool_cursor();
  update();
}

int CanvasWidget::preview_point_handle_count() const noexcept {
  return static_cast<int>(preview_points_.size());
}

QPointF CanvasWidget::preview_point_handle(int index) const {
  return index >= 0 && index < static_cast<int>(preview_points_.size())
             ? preview_points_[static_cast<std::size_t>(index)]
             : QPointF();
}

QPointF CanvasWidget::preview_point_handle_widget_position(int index) const {
  return widget_position_f(preview_point_handle(index));
}

int CanvasWidget::preview_point_handle_at(QPointF widget_position) const {
  // The topmost (last drawn) marker under the pointer wins.
  int hit = -1;
  double best = kHandleGrabRadius;
  for (int index = 0; index < static_cast<int>(preview_points_.size()); ++index) {
    const double distance =
        QLineF(widget_position_f(preview_points_[static_cast<std::size_t>(index)]), widget_position).length();
    if (distance <= best) {
      best = distance;
      hit = index;
    }
  }
  return hit;
}

bool CanvasWidget::apply_preview_point_cursor() {
  if (preview_points_.empty()) {
    return false;
  }
  if (preview_point_drag_ >= 0) {
    setCursor(Qt::ClosedHandCursor);
    return true;
  }
  if (preview_point_handle_at(QPointF(last_mouse_position_)) >= 0) {
    setCursor(Qt::OpenHandCursor);
    return true;
  }
  if (tool_ == CanvasTool::Zoom) {
    return false;  // still works while the dialog locks edits
  }
  setCursor(Qt::ArrowCursor);
  return true;
}

bool CanvasWidget::press_preview_point_handle(QMouseEvent* event) {
  if (preview_points_.empty() || event->button() != Qt::LeftButton) {
    return false;
  }
  const int hit = preview_point_handle_at(event->position());
  if (hit < 0) {
    return false;
  }
  preview_point_drag_ = hit;
  // Keep the marker under the pointer where it was grabbed, not snapped to its centre.
  preview_point_drag_offset_ =
      widget_position_f(preview_points_[static_cast<std::size_t>(hit)]) - event->position();
  last_mouse_position_ = event->pos();
  update_tool_cursor();
  update();
  return true;
}

bool CanvasWidget::move_preview_point_handle(QMouseEvent* event, bool finished) {
  if (preview_point_drag_ < 0 || preview_point_drag_ >= static_cast<int>(preview_points_.size())) {
    preview_point_drag_ = -1;
    return false;
  }
  const int index = preview_point_drag_;
  const QPointF position = document_position_f(event->position() + preview_point_drag_offset_);
  preview_points_[static_cast<std::size_t>(index)] = position;
  last_mouse_position_ = event->pos();
  if (finished) {
    preview_point_drag_ = -1;
    update_tool_cursor();
  }
  update();
  if (preview_point_moved_) {
    // Copied first: the callback may replace the handles (and itself).
    const auto moved = preview_point_moved_;
    moved(index, position, finished);
  }
  return true;
}

void CanvasWidget::draw_preview_point_handles(QPainter& painter) const {
  if (preview_points_.empty()) {
    return;
  }
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);
  for (int index = 0; index < static_cast<int>(preview_points_.size()); ++index) {
    const QPointF center = widget_position_f(preview_points_[static_cast<std::size_t>(index)]);
    const bool active = index == preview_point_drag_;
    // A dark halo under a bright ring, like the clone source marker, so the
    // handle reads over light and dark artwork alike.
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(20, 23, 28, 210), 3.6));
    painter.drawEllipse(center, kHandleRadius, kHandleRadius);
    painter.setPen(QPen(QColor(245, 248, 252, 240), 1.6));
    painter.drawEllipse(center, kHandleRadius, kHandleRadius);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(20, 23, 28, 210));
    painter.drawEllipse(center, active ? 3.4 : 2.6, active ? 3.4 : 2.6);
    painter.setBrush(QColor(245, 248, 252, 240));
    painter.drawEllipse(center, active ? 2.2 : 1.4, active ? 2.2 : 1.4);
  }
  painter.restore();
}

}  // namespace patchy::ui
