#include "ui/zoomable_image_preview.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace patchy::ui {

namespace {

constexpr double kMinimumZoom = 0.0625;
constexpr double kMaximumZoom = 16.0;

}  // namespace

ZoomableImagePreview::ZoomableImagePreview(QWidget* parent) : QWidget(parent) {
  setMinimumSize(420, 300);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  setCursor(Qt::OpenHandCursor);
  setToolTip(QObject::tr("Drag to pan. The mouse wheel zooms."));
  publish_state();
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
}

void ZoomableImagePreview::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  clamp_pan();
  publish_state();
}

void ZoomableImagePreview::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
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
  if (panning_ && (event->buttons() & Qt::LeftButton) != 0) {
    pan_offset_ = pan_press_offset_ + event->position() - pan_press_position_;
    clamp_pan();
    publish_state();
    update();
    event->accept();
    return;
  }
  QWidget::mouseMoveEvent(event);
}

void ZoomableImagePreview::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton && panning_) {
    panning_ = false;
    setCursor(Qt::OpenHandCursor);
    event->accept();
    return;
  }
  QWidget::mouseReleaseEvent(event);
}

void ZoomableImagePreview::mouseDoubleClickEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
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

}  // namespace patchy::ui
