#include "ui/zoomable_image_preview.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QString>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace patchy::ui {

namespace {

constexpr double kMinimumZoom = 0.0625;
constexpr double kMaximumZoom = 16.0;
constexpr double kOverlayHandleHitRadius = 11.0;
constexpr QColor kOverlayAccent{76, 154, 255};

}  // namespace

ZoomableImagePreview::ZoomableImagePreview(QWidget* parent) : QWidget(parent) {
  setMinimumSize(420, 300);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  setMouseTracking(true);
  setCursor(Qt::OpenHandCursor);
  const auto description =
      QObject::tr("Drag to pan. The mouse wheel zooms.");
  setToolTip(description);
  setAccessibleDescription(description);
  publish_state();
  publish_overlay_state();
}

void ZoomableImagePreview::set_image(QImage image) {
  const auto size_changed = image_.size() != image.size();
  image_ = std::move(image);
  if (size_changed) {
    fit_mode_ = true;
    pan_offset_ = {};
  }
  clamp_pan();
  publish_state();
  update();
}

const QImage& ZoomableImagePreview::image() const noexcept { return image_; }

double ZoomableImagePreview::zoom() const {
  return fit_mode_ ? fit_zoom() : zoom_;
}

bool ZoomableImagePreview::fit_mode() const noexcept { return fit_mode_; }

void ZoomableImagePreview::zoom_to_fit() {
  fit_mode_ = true;
  pan_offset_ = {};
  publish_state();
  update();
}

void ZoomableImagePreview::zoom_to(double factor,
                                   std::optional<QPointF> anchor) {
  const auto previous_zoom = zoom();
  const auto target = std::clamp(factor, kMinimumZoom, kMaximumZoom);
  const QPointF widget_center(width() / 2.0, height() / 2.0);
  const auto anchor_point = anchor.value_or(widget_center);
  if (!image_.isNull() && previous_zoom > 0.0) {
    const auto old_top_left = displayed_rect().topLeft();
    const auto image_point = (anchor_point - old_top_left) / previous_zoom;
    const QPointF centered_top_left((width() - image_.width() * target) / 2.0,
                                    (height() - image_.height() * target) / 2.0);
    pan_offset_ = anchor_point - centered_top_left - image_point * target;
  }
  fit_mode_ = false;
  zoom_ = target;
  clamp_pan();
  publish_state();
  update();
}

void ZoomableImagePreview::zoom_step(int direction,
                                     std::optional<QPointF> anchor) {
  static constexpr std::array<double, 15> kSteps = {
      kMinimumZoom, 0.125, 0.25, 1.0 / 3.0, 0.5, 2.0 / 3.0, 1.0,
      1.5,          2.0,   3.0,  4.0,       6.0, 8.0,       12.0,
      kMaximumZoom};
  const auto current = zoom();
  auto target = direction > 0 ? kMaximumZoom : kMinimumZoom;
  if (direction > 0) {
    for (const auto step : kSteps) {
      if (step > current * 1.001) {
        target = step;
        break;
      }
    }
  } else {
    for (auto it = kSteps.rbegin(); it != kSteps.rend(); ++it) {
      if (*it < current * 0.999) {
        target = *it;
        break;
      }
    }
  }
  zoom_to(target, anchor);
}

void ZoomableImagePreview::set_zoom_changed_callback(
    std::function<void()> callback) {
  zoom_changed_ = std::move(callback);
  publish_state();
}

void ZoomableImagePreview::set_center_radius_overlay(
    std::optional<NormalizedCenterRadiusOverlay> overlay) {
  if (overlay) {
    overlay->center.setX(std::clamp(overlay->center.x(), 0.0, 1.0));
    overlay->center.setY(std::clamp(overlay->center.y(), 0.0, 1.0));
    if (overlay->radius) {
      overlay->radius = std::clamp(*overlay->radius, 0.0, 1.0);
    }
  }
  if (center_radius_overlay_ == overlay) {
    return;
  }
  if (!overlay) {
    active_overlay_handle_ = OverlayHandle::None;
    overlay_drag_changed_ = false;
  }
  center_radius_overlay_ = std::move(overlay);
  if (active_overlay_handle_ == OverlayHandle::None &&
      center_radius_overlay_) {
    last_requested_overlay_ = *center_radius_overlay_;
  }
  const auto description = center_radius_overlay_
                               ? QObject::tr(
                                     "Drag the center or radius handle to "
                                     "position the filter. Drag elsewhere to "
                                     "pan; the mouse wheel zooms.")
                               : QObject::tr(
                                     "Drag to pan. The mouse wheel zooms.");
  setAccessibleDescription(description);
  setToolTip(description);
  publish_overlay_state();
  update();
}

const std::optional<NormalizedCenterRadiusOverlay>&
ZoomableImagePreview::center_radius_overlay() const noexcept {
  return center_radius_overlay_;
}

void ZoomableImagePreview::set_center_radius_changed_callback(
    std::function<void(NormalizedCenterRadiusOverlay, bool)> callback) {
  center_radius_changed_ = std::move(callback);
}

void ZoomableImagePreview::paintEvent(QPaintEvent*) {
  QPainter painter(this);
  painter.fillRect(rect(), QColor(31, 33, 37));
  if (image_.isNull()) {
    return;
  }

  const auto target = displayed_rect();
  const auto visible_target = target.intersected(QRectF(rect()));
  if (visible_target.isEmpty()) {
    return;
  }
  painter.save();
  painter.setClipRect(visible_target);
  constexpr int tile = 12;
  const auto left =
      static_cast<int>(std::floor(visible_target.left() / tile)) * tile;
  const auto top =
      static_cast<int>(std::floor(visible_target.top() / tile)) * tile;
  const auto right = static_cast<int>(std::ceil(visible_target.right()));
  const auto bottom = static_cast<int>(std::ceil(visible_target.bottom()));
  for (int y = top; y < bottom; y += tile) {
    for (int x = left; x < right; x += tile) {
      const auto light = ((x / tile) + (y / tile)) % 2 == 0;
      painter.fillRect(QRect(x, y, tile, tile),
                       light ? QColor(104, 106, 110) : QColor(78, 80, 84));
    }
  }
  painter.setRenderHint(QPainter::SmoothPixmapTransform, zoom() < 1.0);
  painter.drawImage(target, image_);
  painter.restore();
  painter.setPen(QColor(18, 20, 23));
  painter.drawRect(target.adjusted(-1.0, -1.0, 0.0, 0.0));
  draw_center_radius_overlay(painter);
}

void ZoomableImagePreview::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  clamp_pan();
  publish_state();
}

void ZoomableImagePreview::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    const auto overlay_handle = overlay_handle_at(event->position());
    if (overlay_handle != OverlayHandle::None && center_radius_overlay_) {
      active_overlay_handle_ = overlay_handle;
      overlay_drag_changed_ = false;
      last_requested_overlay_ = *center_radius_overlay_;
      update_interaction_cursor(event->position());
      publish_overlay_state();
      // Notify the host at gesture start so it can freeze any size-changing
      // preview before the pointer's coordinate system is captured.
      request_center_radius_overlay(last_requested_overlay_, false);
      update();
      event->accept();
      return;
    }
    panning_ = true;
    pan_press_position_ = event->position();
    pan_press_offset_ = pan_offset_;
    setCursor(Qt::ClosedHandCursor);
    event->accept();
    return;
  }
  QWidget::mousePressEvent(event);
}

void ZoomableImagePreview::mouseMoveEvent(QMouseEvent* event) {
  if (active_overlay_handle_ != OverlayHandle::None &&
      (event->buttons() & Qt::LeftButton) != 0) {
    const auto requested = requested_overlay_at(event->position());
    if (requested != last_requested_overlay_) {
      last_requested_overlay_ = requested;
      overlay_drag_changed_ = true;
      request_center_radius_overlay(requested, false);
    }
    event->accept();
    return;
  }
  if (panning_ && (event->buttons() & Qt::LeftButton) != 0) {
    pan_offset_ = pan_press_offset_ + event->position() - pan_press_position_;
    clamp_pan();
    publish_state();
    update();
    event->accept();
    return;
  }
  update_interaction_cursor(event->position());
  QWidget::mouseMoveEvent(event);
}

void ZoomableImagePreview::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton &&
      active_overlay_handle_ != OverlayHandle::None) {
    const auto requested = requested_overlay_at(event->position());
    if (requested != last_requested_overlay_) {
      last_requested_overlay_ = requested;
      overlay_drag_changed_ = true;
      request_center_radius_overlay(requested, false);
    }
    active_overlay_handle_ = OverlayHandle::None;
    publish_overlay_state();
    // Always finish a gesture that was announced on press. Even an unmoved
    // click lets the host resume a preview it froze for pointer stability.
    request_center_radius_overlay(last_requested_overlay_, true);
    overlay_drag_changed_ = false;
    update_interaction_cursor(event->position());
    update();
    event->accept();
    return;
  }
  if (event->button() == Qt::LeftButton && panning_) {
    panning_ = false;
    update_interaction_cursor(event->position());
    event->accept();
    return;
  }
  QWidget::mouseReleaseEvent(event);
}

void ZoomableImagePreview::mouseDoubleClickEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    if (overlay_handle_at(event->position()) != OverlayHandle::None) {
      event->accept();
      return;
    }
    if (fit_mode_) {
      zoom_to(1.0, event->position());
    } else {
      zoom_to_fit();
    }
    event->accept();
    return;
  }
  QWidget::mouseDoubleClickEvent(event);
}

void ZoomableImagePreview::wheelEvent(QWheelEvent* event) {
  const auto delta = event->angleDelta().y();
  if (delta == 0) {
    event->ignore();
    return;
  }
  zoom_step(delta > 0 ? 1 : -1, event->position());
  event->accept();
}

void ZoomableImagePreview::leaveEvent(QEvent* event) {
  if (!panning_ && active_overlay_handle_ == OverlayHandle::None) {
    setCursor(Qt::OpenHandCursor);
  }
  QWidget::leaveEvent(event);
}

double ZoomableImagePreview::fit_zoom() const {
  if (image_.isNull() || image_.width() <= 0 || image_.height() <= 0 ||
      width() <= 0 || height() <= 0) {
    return 1.0;
  }
  const auto scale =
      std::min(static_cast<double>(width()) / image_.width(),
               static_cast<double>(height()) / image_.height());
  return std::clamp(scale, kMinimumZoom, kMaximumZoom);
}

QRectF ZoomableImagePreview::displayed_rect() const {
  if (image_.isNull()) {
    return {};
  }
  const auto z = zoom();
  const QSizeF size(image_.width() * z, image_.height() * z);
  return QRectF(QPointF((width() - size.width()) / 2.0,
                        (height() - size.height()) / 2.0) +
                    pan_offset_,
                size);
}

QPointF ZoomableImagePreview::overlay_center_point(
    const NormalizedCenterRadiusOverlay& overlay) const {
  const auto target = displayed_rect();
  return QPointF(target.left() + overlay.center.x() * target.width(),
                 target.top() + overlay.center.y() * target.height());
}

double ZoomableImagePreview::overlay_radius_pixels(
    const NormalizedCenterRadiusOverlay& overlay) const {
  if (!overlay.radius) {
    return 0.0;
  }
  const auto target = displayed_rect();
  return *overlay.radius * std::min(target.width(), target.height()) / 2.0;
}

QPointF ZoomableImagePreview::overlay_radius_handle_point(
    const NormalizedCenterRadiusOverlay& overlay) const {
  const auto target = displayed_rect();
  const auto center = overlay_center_point(overlay);
  const auto radius = overlay_radius_pixels(overlay);
  const auto right_room = target.right() - center.x();
  const auto left_room = center.x() - target.left();
  return center + QPointF(right_room >= left_room ? radius : -radius, 0.0);
}

ZoomableImagePreview::OverlayHandle ZoomableImagePreview::overlay_handle_at(
    QPointF position) const {
  if (!center_radius_overlay_ || image_.isNull()) {
    return OverlayHandle::None;
  }

  auto visible_target = displayed_rect().intersected(QRectF(rect()));
  if (visible_target.isEmpty()) {
    return OverlayHandle::None;
  }
  visible_target.adjust(-kOverlayHandleHitRadius, -kOverlayHandleHitRadius,
                        kOverlayHandleHitRadius, kOverlayHandleHitRadius);
  if (!visible_target.contains(position)) {
    return OverlayHandle::None;
  }

  const auto distance_squared = [](QPointF a, QPointF b) {
    const auto delta = a - b;
    return delta.x() * delta.x() + delta.y() * delta.y();
  };
  const auto hit_radius_squared =
      kOverlayHandleHitRadius * kOverlayHandleHitRadius;
  const auto center_distance = distance_squared(
      position, overlay_center_point(*center_radius_overlay_));
  const auto radius_distance = center_radius_overlay_->radius
                                   ? distance_squared(
                                         position,
                                         overlay_radius_handle_point(
                                             *center_radius_overlay_))
                                   : std::numeric_limits<double>::infinity();
  const auto center_hit = center_distance <= hit_radius_squared;
  const auto radius_hit = radius_distance <= hit_radius_squared;
  if (center_hit || radius_hit) {
    return radius_hit && radius_distance < center_distance
               ? OverlayHandle::Radius
               : OverlayHandle::Center;
  }
  return OverlayHandle::None;
}

NormalizedCenterRadiusOverlay ZoomableImagePreview::requested_overlay_at(
    QPointF position) const {
  auto requested = last_requested_overlay_;
  const auto target = displayed_rect();
  if (target.width() <= 0.0 || target.height() <= 0.0) {
    return requested;
  }

  if (active_overlay_handle_ == OverlayHandle::Center) {
    requested.center.setX(
        std::clamp((position.x() - target.left()) / target.width(), 0.0, 1.0));
    requested.center.setY(
        std::clamp((position.y() - target.top()) / target.height(), 0.0, 1.0));
  } else if (active_overlay_handle_ == OverlayHandle::Radius &&
             requested.radius) {
    const auto center = overlay_center_point(requested);
    const auto delta = position - center;
    const auto denominator = std::min(target.width(), target.height()) / 2.0;
    if (denominator > 0.0) {
      requested.radius =
          std::clamp(std::hypot(delta.x(), delta.y()) / denominator, 0.0,
                     1.0);
    }
  }
  return requested;
}

void ZoomableImagePreview::draw_center_radius_overlay(
    QPainter& painter) const {
  if (!center_radius_overlay_ || image_.isNull()) {
    return;
  }

  const auto target = displayed_rect();
  const auto visible_target = target.intersected(QRectF(rect()));
  if (visible_target.isEmpty()) {
    return;
  }

  const auto& overlay = *center_radius_overlay_;
  const auto center = overlay_center_point(overlay);
  painter.save();
  painter.setClipRect(visible_target);
  painter.setRenderHint(QPainter::Antialiasing, true);

  if (overlay.radius) {
    const auto radius = overlay_radius_pixels(overlay);
    const QRectF circle(center.x() - radius, center.y() - radius,
                        radius * 2.0, radius * 2.0);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(18, 20, 23, 220), 4.0));
    painter.drawEllipse(circle);
    painter.setPen(QPen(kOverlayAccent, 2.0));
    painter.drawEllipse(circle);

    const auto handle = overlay_radius_handle_point(overlay);
    painter.setPen(QPen(QColor(18, 20, 23, 220), 4.0,
                        Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(center, handle);
    painter.setPen(QPen(kOverlayAccent, 2.0, Qt::SolidLine,
                        Qt::RoundCap));
    painter.drawLine(center, handle);
    painter.setPen(QPen(QColor(18, 20, 23), 1.5));
    painter.setBrush(kOverlayAccent);
    painter.drawEllipse(handle, 5.5, 5.5);
  }

  painter.setPen(QPen(QColor(18, 20, 23, 230), 5.0,
                      Qt::SolidLine, Qt::RoundCap));
  painter.drawLine(center + QPointF(-9.0, 0.0),
                   center + QPointF(9.0, 0.0));
  painter.drawLine(center + QPointF(0.0, -9.0),
                   center + QPointF(0.0, 9.0));
  painter.setPen(QPen(kOverlayAccent, 2.0, Qt::SolidLine, Qt::RoundCap));
  painter.drawLine(center + QPointF(-9.0, 0.0),
                   center + QPointF(9.0, 0.0));
  painter.drawLine(center + QPointF(0.0, -9.0),
                   center + QPointF(0.0, 9.0));
  painter.restore();
}

void ZoomableImagePreview::request_center_radius_overlay(
    NormalizedCenterRadiusOverlay overlay, bool gesture_finished) {
  overlay.center.setX(std::clamp(overlay.center.x(), 0.0, 1.0));
  overlay.center.setY(std::clamp(overlay.center.y(), 0.0, 1.0));
  if (overlay.radius) {
    overlay.radius = std::clamp(*overlay.radius, 0.0, 1.0);
  }
  if (center_radius_changed_) {
    center_radius_changed_(std::move(overlay), gesture_finished);
  }
}

void ZoomableImagePreview::update_interaction_cursor(QPointF position) {
  if (panning_) {
    setCursor(Qt::ClosedHandCursor);
    return;
  }
  const auto handle = active_overlay_handle_ != OverlayHandle::None
                          ? active_overlay_handle_
                          : overlay_handle_at(position);
  if (handle == OverlayHandle::Center) {
    setCursor(Qt::SizeAllCursor);
  } else if (handle == OverlayHandle::Radius) {
    setCursor(Qt::SizeHorCursor);
  } else {
    setCursor(Qt::OpenHandCursor);
  }
}

void ZoomableImagePreview::clamp_pan() {
  if (fit_mode_ || image_.isNull()) {
    pan_offset_ = {};
    return;
  }
  const auto z = zoom();
  const auto excess_x = std::max(0.0, image_.width() * z - width());
  const auto excess_y = std::max(0.0, image_.height() * z - height());
  pan_offset_.setX(std::clamp(pan_offset_.x(), -excess_x / 2.0,
                              excess_x / 2.0));
  pan_offset_.setY(std::clamp(pan_offset_.y(), -excess_y / 2.0,
                              excess_y / 2.0));
}

void ZoomableImagePreview::publish_state() {
  setProperty("previewZoomPercent",
              static_cast<int>(std::lround(zoom() * 100.0)));
  setProperty("previewFitMode", fit_mode_);
  setProperty("previewPanOffset",
              QPoint(static_cast<int>(std::lround(pan_offset_.x())),
                     static_cast<int>(std::lround(pan_offset_.y()))));
  if (zoom_changed_) {
    zoom_changed_();
  }
}

void ZoomableImagePreview::publish_overlay_state() {
  const auto visible = center_radius_overlay_.has_value();
  const auto radius_visible =
      visible && center_radius_overlay_->radius.has_value();
  const auto center =
      visible ? center_radius_overlay_->center : QPointF(-1.0, -1.0);
  const auto radius = radius_visible ? *center_radius_overlay_->radius : -1.0;
  const auto handle_name = [this]() {
    switch (active_overlay_handle_) {
      case OverlayHandle::None:
        return QStringLiteral("none");
      case OverlayHandle::Center:
        return QStringLiteral("center");
      case OverlayHandle::Radius:
        return QStringLiteral("radius");
    }
    return QStringLiteral("none");
  }();

  setProperty("filterSpatialOverlayVisible", visible);
  setProperty("filterSpatialRadiusVisible", radius_visible);
  setProperty("filterCenterXNormalized", center.x());
  setProperty("filterCenterYNormalized", center.y());
  setProperty("filterRadiusNormalized", radius);
  setProperty("filterCenterXPercent",
              visible ? static_cast<int>(std::lround(center.x() * 100.0)) : -1);
  setProperty("filterCenterYPercent",
              visible ? static_cast<int>(std::lround(center.y() * 100.0)) : -1);
  setProperty("filterRadiusPercent",
              radius_visible ? static_cast<int>(std::lround(radius * 100.0))
                             : -1);
  setProperty("filterSpatialDragging",
              active_overlay_handle_ != OverlayHandle::None);
  setProperty("filterSpatialDragHandle", handle_name);
}

}  // namespace patchy::ui
