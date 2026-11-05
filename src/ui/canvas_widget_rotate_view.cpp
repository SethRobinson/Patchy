// CanvasWidget's Rotate View tool (docs/view-navigation-rotation.md): a drag turns the
// view about the view area's center the way the pointer travels around it, Shift snaps
// the angle to 15-degree steps, Escape puts the angle back. Only the view turns; the
// document never changes, so the tool needs no undo.

#include "ui/canvas_widget.hpp"

#include "ui/theme_palette.hpp"

#include <QFontMetrics>
#include <QPainter>
#include <QPen>

#include <cmath>
#include <numbers>

namespace patchy::ui {

namespace {

constexpr double kRotateViewSnapDegrees = 15.0;
// Closer to the pivot than this the pointer's direction is noise; the drag waits.
constexpr double kRotateViewDeadZone = 6.0;
constexpr double kCompassRadius = 34.0;

double pointer_degrees(QPointF offset) {
  return std::atan2(offset.y(), offset.x()) * 180.0 / std::numbers::pi;
}

}  // namespace

bool CanvasWidget::rotating_view() const noexcept {
  return rotating_view_;
}

void CanvasWidget::begin_rotate_view_drag(QPointF widget_position) {
  rotating_view_ = true;
  rotate_view_pivot_ = view_content_rect().center();
  rotate_view_start_angle_ = view_rotation_degrees_;
  const auto offset = widget_position - rotate_view_pivot_;
  rotate_view_pointer_valid_ = std::hypot(offset.x(), offset.y()) >= kRotateViewDeadZone;
  rotate_view_start_pointer_degrees_ = rotate_view_pointer_valid_ ? pointer_degrees(offset) : 0.0;
  setCursor(Qt::ClosedHandCursor);
  update();
}

void CanvasWidget::update_rotate_view_drag(QPointF widget_position, Qt::KeyboardModifiers modifiers) {
  if (!rotating_view_) {
    return;
  }
  const auto offset = widget_position - rotate_view_pivot_;
  if (std::hypot(offset.x(), offset.y()) < kRotateViewDeadZone) {
    return;
  }
  if (!rotate_view_pointer_valid_) {
    // The press landed on the pivot: the turn starts from the first clear direction.
    rotate_view_pointer_valid_ = true;
    rotate_view_start_pointer_degrees_ = pointer_degrees(offset);
    return;
  }
  auto angle = rotate_view_start_angle_ + (pointer_degrees(offset) - rotate_view_start_pointer_degrees_);
  if ((modifiers & Qt::ShiftModifier) != 0) {
    angle = std::round(angle / kRotateViewSnapDegrees) * kRotateViewSnapDegrees;
  }
  set_view_rotation(angle);
  update();
}

void CanvasWidget::end_rotate_view_drag(bool cancel) {
  if (!rotating_view_) {
    return;
  }
  rotating_view_ = false;
  if (cancel) {
    set_view_rotation(rotate_view_start_angle_);
  }
  update_tool_cursor();
  update();
}

void CanvasWidget::draw_rotate_view_compass(QPainter& painter) const {
  if (!rotating_view_) {
    return;
  }
  const auto& palette = theme();
  const auto center = rotate_view_pivot_;
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setPen(QPen(palette.canvas_hud_border, 1.0));
  auto fill = palette.canvas_hud_bg;
  painter.setBrush(fill);
  painter.drawEllipse(center, kCompassRadius, kCompassRadius);
  // The needle points along the document's up direction; its accent end is the top.
  const auto up = view_transform().map_vector_to_widget(QPointF(0.0, -1.0));
  const auto length = std::hypot(up.x(), up.y());
  const QPointF direction = length > 0.0 ? up / length : QPointF(0.0, -1.0);
  const auto tip = center + direction * (kCompassRadius - 6.0);
  const auto tail = center - direction * (kCompassRadius - 6.0);
  painter.setPen(QPen(palette.canvas_hud_text, 2.0, Qt::SolidLine, Qt::RoundCap));
  painter.drawLine(center, tail);
  painter.setPen(QPen(palette.accent, 3.0, Qt::SolidLine, Qt::RoundCap));
  painter.drawLine(center, tip);
  // The angle, below the disc.
  const auto label = QString::number(std::lround(view_rotation_degrees_)) + QChar(0x00B0);
  const QFontMetrics metrics(painter.font());
  const auto text_width = metrics.horizontalAdvance(label);
  const QRectF text_rect(center.x() - text_width / 2.0 - 6.0, center.y() + kCompassRadius + 6.0, text_width + 12.0,
                         metrics.height() + 4.0);
  painter.setPen(QPen(palette.canvas_hud_border, 1.0));
  painter.setBrush(fill);
  painter.drawRoundedRect(text_rect, 4.0, 4.0);
  painter.setPen(palette.canvas_hud_text);
  painter.drawText(text_rect, Qt::AlignCenter, label);
  painter.restore();
}

}  // namespace patchy::ui
