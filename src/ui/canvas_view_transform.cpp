#include "ui/canvas_view_transform.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace patchy::ui {

namespace {

// Exact values at right angles, so a 90-degree view maps whole pixels to whole pixels.
void angle_cos_sin(double degrees, double& cos_value, double& sin_value) noexcept {
  if (degrees == 0.0) {
    cos_value = 1.0;
    sin_value = 0.0;
  } else if (degrees == 90.0) {
    cos_value = 0.0;
    sin_value = 1.0;
  } else if (degrees == 180.0) {
    cos_value = -1.0;
    sin_value = 0.0;
  } else if (degrees == -90.0) {
    cos_value = 0.0;
    sin_value = -1.0;
  } else {
    const auto radians = degrees * std::numbers::pi / 180.0;
    cos_value = std::cos(radians);
    sin_value = std::sin(radians);
  }
}

}  // namespace

double CanvasViewTransform::normalized_angle(double degrees) noexcept {
  if (!std::isfinite(degrees)) {
    return 0.0;
  }
  auto wrapped = std::fmod(degrees, 360.0);
  if (wrapped <= -180.0) {
    wrapped += 360.0;
  } else if (wrapped > 180.0) {
    wrapped -= 360.0;
  }
  // Collapse -0 and the rounding dust of repeated steps onto 0, so an angle that
  // came back to upright takes the axis-aligned paths again.
  if (std::abs(wrapped) < 1e-9) {
    return 0.0;
  }
  return wrapped;
}

CanvasViewTransform::CanvasViewTransform(double zoom, QPointF pan, double angle_degrees,
                                         QSizeF document_size) noexcept
    : zoom_(zoom), pan_(pan), angle_(normalized_angle(angle_degrees)), document_size_(document_size) {
  angle_cos_sin(angle_, cos_, sin_);
  if (axis_aligned()) {
    return;
  }
  const std::array<QPointF, 4> corners{QPointF(0.0, 0.0), QPointF(document_size_.width(), 0.0),
                                       QPointF(document_size_.width(), document_size_.height()),
                                       QPointF(0.0, document_size_.height())};
  double min_x = 0.0;
  double min_y = 0.0;
  bool first = true;
  for (const auto& corner : corners) {
    const auto mapped = map_vector_to_widget(corner);
    min_x = first ? mapped.x() : std::min(min_x, mapped.x());
    min_y = first ? mapped.y() : std::min(min_y, mapped.y());
    first = false;
  }
  bounds_offset_ = QPointF(min_x, min_y);
}

QPointF CanvasViewTransform::map_vector_to_widget(QPointF document_vector) const noexcept {
  if (axis_aligned()) {
    return QPointF(document_vector.x() * zoom_, document_vector.y() * zoom_);
  }
  return QPointF((cos_ * document_vector.x() - sin_ * document_vector.y()) * zoom_,
                 (sin_ * document_vector.x() + cos_ * document_vector.y()) * zoom_);
}

QPointF CanvasViewTransform::map_vector_to_document(QPointF widget_vector) const noexcept {
  if (axis_aligned()) {
    return QPointF(widget_vector.x() / zoom_, widget_vector.y() / zoom_);
  }
  return QPointF((cos_ * widget_vector.x() + sin_ * widget_vector.y()) / zoom_,
                 (-sin_ * widget_vector.x() + cos_ * widget_vector.y()) / zoom_);
}

QPointF CanvasViewTransform::map_to_widget(QPointF document_point) const noexcept {
  if (axis_aligned()) {
    // The exact pre-rotation arithmetic: pan + document * zoom, per axis.
    return QPointF(pan_.x() + document_point.x() * zoom_, pan_.y() + document_point.y() * zoom_);
  }
  return pan_ + map_vector_to_widget(document_point);
}

QPointF CanvasViewTransform::map_to_document(QPointF widget_point) const noexcept {
  if (axis_aligned()) {
    return QPointF((widget_point.x() - pan_.x()) / zoom_, (widget_point.y() - pan_.y()) / zoom_);
  }
  return map_vector_to_document(widget_point - pan_);
}

QTransform CanvasViewTransform::to_widget() const noexcept {
  if (axis_aligned()) {
    return QTransform(zoom_, 0.0, 0.0, zoom_, pan_.x(), pan_.y());
  }
  return QTransform(cos_ * zoom_, sin_ * zoom_, -sin_ * zoom_, cos_ * zoom_, pan_.x(), pan_.y());
}

QTransform CanvasViewTransform::to_document() const noexcept {
  return to_widget().inverted();
}

QPolygonF CanvasViewTransform::map_document_rect(const QRectF& document_rect) const {
  QPolygonF polygon;
  polygon.reserve(4);
  polygon << map_to_widget(document_rect.topLeft()) << map_to_widget(document_rect.topRight())
          << map_to_widget(document_rect.bottomRight()) << map_to_widget(document_rect.bottomLeft());
  return polygon;
}

QRectF CanvasViewTransform::widget_bounds_of_document_rect(const QRectF& document_rect) const noexcept {
  if (axis_aligned()) {
    return QRectF(map_to_widget(document_rect.topLeft()), map_to_widget(document_rect.bottomRight())).normalized();
  }
  const std::array<QPointF, 4> corners{map_to_widget(document_rect.topLeft()), map_to_widget(document_rect.topRight()),
                                       map_to_widget(document_rect.bottomRight()),
                                       map_to_widget(document_rect.bottomLeft())};
  auto left = corners[0].x();
  auto right = left;
  auto top = corners[0].y();
  auto bottom = top;
  for (const auto& corner : corners) {
    left = std::min(left, corner.x());
    right = std::max(right, corner.x());
    top = std::min(top, corner.y());
    bottom = std::max(bottom, corner.y());
  }
  return QRectF(QPointF(left, top), QPointF(right, bottom));
}

QRectF CanvasViewTransform::document_bounds_of_widget_rect(const QRectF& widget_rect) const noexcept {
  if (axis_aligned()) {
    return QRectF(map_to_document(widget_rect.topLeft()), map_to_document(widget_rect.bottomRight())).normalized();
  }
  const std::array<QPointF, 4> corners{
      map_to_document(widget_rect.topLeft()), map_to_document(widget_rect.topRight()),
      map_to_document(widget_rect.bottomRight()), map_to_document(widget_rect.bottomLeft())};
  auto left = corners[0].x();
  auto right = left;
  auto top = corners[0].y();
  auto bottom = top;
  for (const auto& corner : corners) {
    left = std::min(left, corner.x());
    right = std::max(right, corner.x());
    top = std::min(top, corner.y());
    bottom = std::max(bottom, corner.y());
  }
  return QRectF(QPointF(left, top), QPointF(right, bottom));
}

QSizeF CanvasViewTransform::extent() const noexcept {
  const auto width = document_size_.width() * zoom_;
  const auto height = document_size_.height() * zoom_;
  if (axis_aligned()) {
    return QSizeF(width, height);
  }
  return QSizeF(std::abs(cos_) * width + std::abs(sin_) * height, std::abs(sin_) * width + std::abs(cos_) * height);
}

QPointF CanvasViewTransform::pan_placing(QPointF document_point, QPointF widget_point) const noexcept {
  if (axis_aligned()) {
    return QPointF(widget_point.x() - document_point.x() * zoom_, widget_point.y() - document_point.y() * zoom_);
  }
  return widget_point - map_vector_to_widget(document_point);
}

}  // namespace patchy::ui
