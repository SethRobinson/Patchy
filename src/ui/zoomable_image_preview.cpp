#include "ui/zoomable_image_preview.hpp"
#include "ui/theme_palette.hpp"
#include "ui/background_workers.hpp"
#include "ui/modifier_names.hpp"

#include <QCoreApplication>
#include <QEvent>
#include <QPointer>
#include <QCursor>
#include <QKeyEvent>
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
// Fit mode may go below the zoom buttons' floor so a very large image still
// fits the widget whole.
constexpr double kMinimumFitZoom = 0.01;
constexpr double kOverlayHandleHitRadius = 11.0;
// Scrubby Zoom's sensitivity and click slop, the canvas Zoom tool's values
// (kZoomDragFactorPerPixel in canvas_widget_view.cpp, CanvasWidget::kZoomClickSlopPx).
constexpr double kScrubFactorPerPixel = 1.01;
constexpr double kScrubClickSlop = 8.0;
[[nodiscard]] QColor overlay_accent() { return theme().accent_control; }
constexpr double kPi = 3.14159265358979323846;

[[nodiscard]] double normalized_degrees(double degrees) {
  if (!std::isfinite(degrees)) {
    return 0.0;
  }
  auto normalized = std::remainder(degrees, 360.0);
  if (normalized < -180.0) {
    normalized += 360.0;
  }
  return normalized;
}

[[nodiscard]] QPointF tilt_tangent(double degrees) {
  const auto radians = degrees * kPi / 180.0;
  // Match Patchy's Angle presentation: positive degrees turn
  // counterclockwise in image space, whose Y axis points down.
  return {std::cos(radians), -std::sin(radians)};
}

[[nodiscard]] QPointF tilt_normal(double degrees) {
  const auto tangent = tilt_tangent(degrees);
  return {-tangent.y(), tangent.x()};
}

}  // namespace

struct ZoomableImagePreview::ScaleState {
  struct Request { QImage image; QSize size; std::uint64_t generation; };
  std::optional<Request> pending;
  bool in_flight{false};
  qint64 requested_key{0};
  QSize requested_size;
  std::uint64_t generation{0};
};

ZoomableImagePreview::ZoomableImagePreview(QWidget* parent) : QWidget(parent) {
  scale_state_ = std::make_shared<ScaleState>();
  setMinimumSize(420, 300);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);
  setCursor(Qt::OpenHandCursor);
  refresh_interaction_tooltip();
  publish_state();
  publish_overlay_state();
}

void ZoomableImagePreview::set_image(QImage image, QSize logical_size) {
  if (logical_size.isEmpty()) logical_size = image.size();
  image_ = std::move(image);
  set_logical_size(logical_size);
}

void ZoomableImagePreview::set_logical_size(QSize logical_size) {
  const auto size_changed = logical_size_ != logical_size;
  logical_size_ = logical_size;
  if (size_changed) {
    scaled_image_ = {};
    fit_mode_ = true;
    pan_offset_ = {};
  }
  clamp_pan();
  publish_state();
  update();
}

QSize ZoomableImagePreview::logical_size() const noexcept { return logical_size_; }

bool ZoomableImagePreview::panning() const noexcept { return panning_; }

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
  const auto target = std::clamp(factor, minimum_zoom(), kMaximumZoom);
  const QPointF widget_center(width() / 2.0, height() / 2.0);
  const auto anchor_point = anchor.value_or(widget_center);
  if (!logical_size_.isEmpty() && previous_zoom > 0.0) {
    const auto old_top_left = displayed_rect().topLeft();
    const auto image_point = (anchor_point - old_top_left) / previous_zoom;
    const QPointF centered_top_left((width() - logical_size_.width() * target) / 2.0,
                                    (height() - logical_size_.height() * target) / 2.0);
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
  auto target = direction > 0 ? kMaximumZoom : minimum_zoom();
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

void ZoomableImagePreview::set_scrubby_zoom(bool enabled) {
  if (scrubby_zoom_ == enabled) return;
  scrubby_zoom_ = enabled;
  scrub_armed_ = false;
  scrub_started_ = false;
  refresh_interaction_tooltip();
  setProperty("previewScrubbyZoom", enabled);
  update_interaction_cursor(mapFromGlobal(QCursor::pos()));
}

bool ZoomableImagePreview::scrubby_zoom() const noexcept { return scrubby_zoom_; }

void ZoomableImagePreview::refresh_interaction_tooltip() {
  const auto description =
      scrubby_zoom_ ? resolve_modifier_names(QObject::tr("Drag left or right to zoom. Click zooms in, %ALT%-click zooms "
                                                         "out. Hold Space or use the middle button to pan."))
                    : QObject::tr("Drag to pan. The mouse wheel zooms.");
  setToolTip(description);
  setAccessibleDescription(description);
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
  if (!overlay && (active_overlay_handle_ == OverlayHandle::Center ||
                   active_overlay_handle_ == OverlayHandle::Radius)) {
    active_overlay_handle_ = OverlayHandle::None;
    overlay_drag_changed_ = false;
    center_radius_gesture_start_.reset();
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

void ZoomableImagePreview::set_tilt_shift_overlay(
    std::optional<NormalizedTiltShiftOverlay> overlay) {
  if (overlay) {
    overlay->center.setX(std::clamp(overlay->center.x(), 0.0, 1.0));
    overlay->center.setY(std::clamp(overlay->center.y(), 0.0, 1.0));
    overlay->angle_degrees = normalized_degrees(overlay->angle_degrees);
    overlay->focus_half_width =
        std::clamp(overlay->focus_half_width, 0.0, 1.0);
    overlay->transition_width =
        std::clamp(overlay->transition_width, 0.0, 1.0);
  }
  if (tilt_shift_overlay_ == overlay) {
    return;
  }
  if (!overlay &&
      (active_overlay_handle_ == OverlayHandle::TiltCenter ||
       active_overlay_handle_ == OverlayHandle::TiltAngle ||
       active_overlay_handle_ == OverlayHandle::TiltFocus ||
       active_overlay_handle_ == OverlayHandle::TiltTransition)) {
    active_overlay_handle_ = OverlayHandle::None;
    overlay_drag_changed_ = false;
    tilt_shift_gesture_start_.reset();
  }
  tilt_shift_overlay_ = std::move(overlay);
  if (active_overlay_handle_ == OverlayHandle::None && tilt_shift_overlay_) {
    last_requested_tilt_shift_overlay_ = *tilt_shift_overlay_;
  }
  publish_overlay_state();
  update();
}

const std::optional<NormalizedTiltShiftOverlay>&
ZoomableImagePreview::tilt_shift_overlay() const noexcept {
  return tilt_shift_overlay_;
}

void ZoomableImagePreview::set_tilt_shift_changed_callback(
    std::function<void(NormalizedTiltShiftOverlay, bool)> callback) {
  tilt_shift_changed_ = std::move(callback);
}

void ZoomableImagePreview::paintEvent(QPaintEvent*) {
  QPainter painter(this);
  paint_backdrop(painter);
  if (logical_size_.isEmpty()) {
    return;
  }
  paint_content(painter, displayed_rect());
  draw_center_radius_overlay(painter);
  draw_tilt_shift_overlay(painter);
}

void ZoomableImagePreview::paint_backdrop(QPainter& painter) {
  painter.fillRect(rect(), QColor(31, 33, 37));
}

void ZoomableImagePreview::paint_content(QPainter& painter, const QRectF& target) {
  if (image_.isNull()) {
    return;
  }
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
  const bool reduced = target.width() * devicePixelRatioF() < image_.width() && zoom() < 1.0;
  if (!reduced) painter.drawImage(target, image_);
  else if (!scaled_image_.isNull()) painter.drawImage(target, scaled_image_);
  painter.restore();
  painter.setPen(QColor(18, 20, 23));
  painter.drawRect(target.adjusted(-1.0, -1.0, 0.0, 0.0));
}

QCursor ZoomableImagePreview::interaction_cursor(bool is_panning) const {
  return is_panning ? Qt::ClosedHandCursor : Qt::OpenHandCursor;
}

void ZoomableImagePreview::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  clamp_pan();
  publish_state();
}

bool ZoomableImagePreview::event(QEvent* event) {
  const auto handled = QWidget::event(event);
  if (event->type() == QEvent::DevicePixelRatioChange) {
    request_display_cache();
    update();
  }
  return handled;
}

void ZoomableImagePreview::request_display_cache() {
  if (!scale_state_) return;
  const auto target = displayed_rect().size() * devicePixelRatioF();
  const QSize size(std::max(1, static_cast<int>(std::ceil(target.width()))),
                   std::max(1, static_cast<int>(std::ceil(target.height()))));
  if (image_.isNull() || zoom() >= 1.0 || size.width() >= image_.width()) {
    ++scale_state_->generation;
    scale_state_->pending.reset();
    scale_state_->requested_key = 0;
    setProperty("previewScaleReady", true);
    return;
  }
  if (scaled_source_key_ == image_.cacheKey() && scaled_size_ == size) {
    if (scale_state_->requested_key != image_.cacheKey() || scale_state_->requested_size != size) {
      ++scale_state_->generation;
      scale_state_->pending.reset();
      scale_state_->requested_key = image_.cacheKey();
      scale_state_->requested_size = size;
    }
    setProperty("previewScaleReady", true);
    return;
  }
  setProperty("previewScaleReady", false);
  if (scale_state_->requested_key == image_.cacheKey() && scale_state_->requested_size == size) return;
  scale_state_->requested_key = image_.cacheKey();
  scale_state_->requested_size = size;
  scale_state_->pending = ScaleState::Request{image_, size, ++scale_state_->generation};
  if (!scale_state_->in_flight) start_display_cache();
}

void ZoomableImagePreview::start_display_cache() {
  auto state = scale_state_;
  if (!state->pending) return;
  auto request = std::move(*state->pending);
  state->pending.reset();
  state->in_flight = true;
  auto* app = QCoreApplication::instance();
  const QPointer<ZoomableImagePreview> guard(this);
  run_tracked_background_worker([state, request = std::move(request), app, guard] {
    auto scaled = request.image.scaled(request.size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QMetaObject::invokeMethod(app, [state, request, scaled = std::move(scaled), guard]() mutable {
      state->in_flight = false;
      if (!guard) return;
      if (request.generation == state->generation) {
        guard->scaled_image_ = std::move(scaled);
        guard->scaled_size_ = request.size;
        guard->scaled_source_key_ = request.image.cacheKey();
        guard->setProperty("previewScaleReady", !guard->scaled_image_.isNull());
        guard->update();
      }
      if (state->pending) guard->start_display_cache();
    }, Qt::QueuedConnection);
  });
}

void ZoomableImagePreview::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    const auto overlay_handle = overlay_handle_at(event->position());
    if (overlay_handle != OverlayHandle::None) {
      active_overlay_handle_ = overlay_handle;
      overlay_drag_changed_ = false;
      setFocus(Qt::MouseFocusReason);
      update_interaction_cursor(event->position());
      publish_overlay_state();
      // Notify the host at gesture start so it can freeze any size-changing
      // preview before the pointer's coordinate system is captured.
      if ((overlay_handle == OverlayHandle::Center ||
           overlay_handle == OverlayHandle::Radius) &&
          center_radius_overlay_) {
        last_requested_overlay_ = *center_radius_overlay_;
        center_radius_gesture_start_ = last_requested_overlay_;
        request_center_radius_overlay(last_requested_overlay_, false);
      } else if (tilt_shift_overlay_) {
        last_requested_tilt_shift_overlay_ = *tilt_shift_overlay_;
        tilt_shift_gesture_start_ = last_requested_tilt_shift_overlay_;
        request_tilt_shift_overlay(last_requested_tilt_shift_overlay_, false);
      }
      update();
      event->accept();
      return;
    }
    if (scrubby_zoom_ && !space_held_) {
      scrub_armed_ = true;
      scrub_started_ = false;
      const auto frame = displayed_rect();
      scrub_anchor_ = frame.isEmpty() ? event->position()
                                      : QPointF(std::clamp(event->position().x(), frame.left(), frame.right()),
                                                std::clamp(event->position().y(), frame.top(), frame.bottom()));
      scrub_press_position_ = event->position();
      scrub_last_position_ = event->position();
      setFocus(Qt::MouseFocusReason);
      event->accept();
      return;
    }
  }
  if ((event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton) && !panning_ && !scrub_armed_) {
    panning_ = true;
    pan_button_ = event->button();
    pan_press_position_ = event->position();
    pan_press_offset_ = pan_offset_;
    setCursor(interaction_cursor(true));
    event->accept();
    return;
  }
  QWidget::mousePressEvent(event);
}

void ZoomableImagePreview::mouseMoveEvent(QMouseEvent* event) {
  if (active_overlay_handle_ != OverlayHandle::None &&
      (event->buttons() & Qt::LeftButton) != 0) {
    if (active_overlay_handle_ == OverlayHandle::Center ||
        active_overlay_handle_ == OverlayHandle::Radius) {
      const auto requested = requested_overlay_at(event->position());
      if (requested != last_requested_overlay_) {
        last_requested_overlay_ = requested;
        overlay_drag_changed_ = true;
        request_center_radius_overlay(requested, false);
      }
    } else {
      const auto requested = requested_tilt_overlay_at(event->position());
      if (requested != last_requested_tilt_shift_overlay_) {
        last_requested_tilt_shift_overlay_ = requested;
        overlay_drag_changed_ = true;
        request_tilt_shift_overlay(requested, false);
      }
    }
    event->accept();
    return;
  }
  if (scrub_armed_ && (event->buttons() & Qt::LeftButton) != 0) {
    const auto travel = event->position() - scrub_press_position_;
    if (!scrub_started_ && std::abs(travel.x()) + std::abs(travel.y()) >= kScrubClickSlop) scrub_started_ = true;
    if (scrub_started_) {
      const auto dx = event->position().x() - scrub_last_position_.x();
      scrub_last_position_ = event->position();
      if (dx != 0.0) zoom_to(zoom() * std::pow(kScrubFactorPerPixel, dx), scrub_anchor_);
    }
    event->accept();
    return;
  }
  if (panning_ && (event->buttons() & pan_button_) != 0) {
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
    const auto finished_handle = active_overlay_handle_;
    if (finished_handle == OverlayHandle::Center ||
        finished_handle == OverlayHandle::Radius) {
      const auto requested = requested_overlay_at(event->position());
      if (requested != last_requested_overlay_) {
        last_requested_overlay_ = requested;
        overlay_drag_changed_ = true;
        request_center_radius_overlay(requested, false);
      }
    } else {
      const auto requested = requested_tilt_overlay_at(event->position());
      if (requested != last_requested_tilt_shift_overlay_) {
        last_requested_tilt_shift_overlay_ = requested;
        overlay_drag_changed_ = true;
        request_tilt_shift_overlay(requested, false);
      }
    }
    active_overlay_handle_ = OverlayHandle::None;
    publish_overlay_state();
    // Always finish a gesture that was announced on press. Even an unmoved
    // click lets the host resume a preview it froze for pointer stability.
    if (finished_handle == OverlayHandle::Center ||
        finished_handle == OverlayHandle::Radius) {
      request_center_radius_overlay(last_requested_overlay_, true);
      center_radius_gesture_start_.reset();
    } else {
      request_tilt_shift_overlay(last_requested_tilt_shift_overlay_, true);
      tilt_shift_gesture_start_.reset();
    }
    overlay_drag_changed_ = false;
    update_interaction_cursor(event->position());
    update();
    event->accept();
    return;
  }
  if (event->button() == Qt::LeftButton && scrub_armed_) {
    const auto clicked = !scrub_started_;
    scrub_armed_ = false;
    scrub_started_ = false;
    // A click without travel is one Zoom-tool step; Alt steps out.
    if (clicked) zoom_step((event->modifiers() & Qt::AltModifier) != 0 ? -1 : 1, scrub_anchor_);
    update_interaction_cursor(event->position());
    event->accept();
    return;
  }
  if (event->button() == pan_button_ && panning_) {
    panning_ = false;
    pan_button_ = Qt::NoButton;
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
    // With Scrubby Zoom each click already stepped the zoom; a double-click
    // must not undo that by toggling Fit.
    if (scrubby_zoom_ && !space_held_) {
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

void ZoomableImagePreview::keyPressEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_Space && scrubby_zoom_) {
    if (!event->isAutoRepeat()) {
      space_held_ = true;
      update_interaction_cursor(mapFromGlobal(QCursor::pos()));
    }
    event->accept();
    return;
  }
  if (event->key() == Qt::Key_Escape &&
      active_overlay_handle_ != OverlayHandle::None) {
    const auto cancelled_handle = active_overlay_handle_;
    active_overlay_handle_ = OverlayHandle::None;
    overlay_drag_changed_ = false;
    if ((cancelled_handle == OverlayHandle::Center ||
         cancelled_handle == OverlayHandle::Radius) &&
        center_radius_gesture_start_) {
      last_requested_overlay_ = *center_radius_gesture_start_;
      request_center_radius_overlay(last_requested_overlay_, false);
      request_center_radius_overlay(last_requested_overlay_, true);
      center_radius_gesture_start_.reset();
    } else if (tilt_shift_gesture_start_) {
      last_requested_tilt_shift_overlay_ = *tilt_shift_gesture_start_;
      request_tilt_shift_overlay(last_requested_tilt_shift_overlay_, false);
      request_tilt_shift_overlay(last_requested_tilt_shift_overlay_, true);
      tilt_shift_gesture_start_.reset();
    }
    publish_overlay_state();
    update_interaction_cursor(mapFromGlobal(QCursor::pos()));
    update();
    event->accept();
    return;
  }
  QWidget::keyPressEvent(event);
}

void ZoomableImagePreview::keyReleaseEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_Space && scrubby_zoom_) {
    if (!event->isAutoRepeat()) {
      space_held_ = false;
      update_interaction_cursor(mapFromGlobal(QCursor::pos()));
    }
    event->accept();
    return;
  }
  QWidget::keyReleaseEvent(event);
}

void ZoomableImagePreview::focusOutEvent(QFocusEvent* event) {
  space_held_ = false;
  scrub_armed_ = false;
  scrub_started_ = false;
  QWidget::focusOutEvent(event);
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
  if (!panning_ && !scrub_armed_ && active_overlay_handle_ == OverlayHandle::None) {
    setCursor(interaction_cursor(false));
  }
  QWidget::leaveEvent(event);
}

double ZoomableImagePreview::fit_zoom() const {
  if (logical_size_.isEmpty() || width() <= 0 || height() <= 0) {
    return 1.0;
  }
  const auto scale =
      std::min(static_cast<double>(width()) / logical_size_.width(),
               static_cast<double>(height()) / logical_size_.height());
  return std::clamp(scale, kMinimumFitZoom, kMaximumZoom);
}

double ZoomableImagePreview::minimum_zoom() const {
  return std::min(fit_zoom(), kMinimumZoom);
}

QRectF ZoomableImagePreview::displayed_rect() const {
  if (logical_size_.isEmpty()) {
    return {};
  }
  const auto z = zoom();
  const QSizeF size(logical_size_.width() * z, logical_size_.height() * z);
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
  if (!center_radius_overlay_) {
    return tilt_overlay_handle_at(position);
  }
  if (image_.isNull()) {
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

QPointF ZoomableImagePreview::tilt_center_point(
    const NormalizedTiltShiftOverlay& overlay) const {
  const auto target = displayed_rect();
  return QPointF(target.left() + overlay.center.x() * target.width(),
                 target.top() + overlay.center.y() * target.height());
}

QPointF ZoomableImagePreview::tilt_angle_handle_point(
    const NormalizedTiltShiftOverlay& overlay) const {
  const auto target = displayed_rect();
  const auto distance = std::clamp(
      std::min(target.width(), target.height()) * 0.18, 36.0, 72.0);
  return tilt_center_point(overlay) +
         tilt_tangent(overlay.angle_degrees) * distance;
}

QPointF ZoomableImagePreview::tilt_focus_handle_point(
    const NormalizedTiltShiftOverlay& overlay, double side) const {
  const auto target = displayed_rect();
  const auto distance = overlay.focus_half_width *
                        std::min(target.width(), target.height());
  return tilt_center_point(overlay) +
         tilt_normal(overlay.angle_degrees) * (side * distance) +
         tilt_tangent(overlay.angle_degrees) * (side * 7.0);
}

QPointF ZoomableImagePreview::tilt_transition_handle_point(
    const NormalizedTiltShiftOverlay& overlay, double side) const {
  const auto target = displayed_rect();
  const auto distance =
      (overlay.focus_half_width + overlay.transition_width) *
      std::min(target.width(), target.height());
  return tilt_center_point(overlay) +
         tilt_normal(overlay.angle_degrees) * (side * distance) +
         tilt_tangent(overlay.angle_degrees) * (side * 14.0);
}

ZoomableImagePreview::OverlayHandle
ZoomableImagePreview::tilt_overlay_handle_at(QPointF position) const {
  if (!tilt_shift_overlay_ || image_.isNull()) {
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

  const auto distance_squared = [position](QPointF point) {
    const auto delta = position - point;
    return delta.x() * delta.x() + delta.y() * delta.y();
  };
  const auto& overlay = *tilt_shift_overlay_;
  const std::array<std::pair<OverlayHandle, QPointF>, 6> handles = {{
      {OverlayHandle::TiltCenter, tilt_center_point(overlay)},
      {OverlayHandle::TiltAngle, tilt_angle_handle_point(overlay)},
      {OverlayHandle::TiltFocus, tilt_focus_handle_point(overlay, 1.0)},
      {OverlayHandle::TiltFocus, tilt_focus_handle_point(overlay, -1.0)},
      {OverlayHandle::TiltTransition,
       tilt_transition_handle_point(overlay, 1.0)},
      {OverlayHandle::TiltTransition,
       tilt_transition_handle_point(overlay, -1.0)},
  }};
  auto closest = OverlayHandle::None;
  auto closest_distance = std::numeric_limits<double>::infinity();
  const auto hit_distance =
      kOverlayHandleHitRadius * kOverlayHandleHitRadius;
  for (const auto& [handle, point] : handles) {
    const auto distance = distance_squared(point);
    if (distance <= hit_distance && distance < closest_distance) {
      closest = handle;
      closest_distance = distance;
    }
  }
  return closest;
}

NormalizedTiltShiftOverlay ZoomableImagePreview::requested_tilt_overlay_at(
    QPointF position) const {
  auto requested = last_requested_tilt_shift_overlay_;
  const auto target = displayed_rect();
  const auto shorter = std::min(target.width(), target.height());
  if (target.width() <= 0.0 || target.height() <= 0.0 || shorter <= 0.0) {
    return requested;
  }
  if (active_overlay_handle_ == OverlayHandle::TiltCenter) {
    requested.center.setX(
        std::clamp((position.x() - target.left()) / target.width(), 0.0, 1.0));
    requested.center.setY(
        std::clamp((position.y() - target.top()) / target.height(), 0.0, 1.0));
    return requested;
  }

  const auto center = tilt_center_point(requested);
  const auto delta = position - center;
  if (active_overlay_handle_ == OverlayHandle::TiltAngle) {
    requested.angle_degrees = normalized_degrees(
        std::atan2(-delta.y(), delta.x()) * 180.0 / kPi);
    return requested;
  }
  const auto distance =
      std::abs(QPointF::dotProduct(
          delta, tilt_normal(requested.angle_degrees))) /
      shorter;
  if (active_overlay_handle_ == OverlayHandle::TiltFocus) {
    requested.focus_half_width = std::clamp(distance, 0.0, 1.0);
  } else if (active_overlay_handle_ == OverlayHandle::TiltTransition) {
    requested.transition_width =
        std::clamp(distance - requested.focus_half_width, 0.0, 1.0);
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
    painter.setPen(QPen(overlay_accent(), 2.0));
    painter.drawEllipse(circle);

    const auto handle = overlay_radius_handle_point(overlay);
    painter.setPen(QPen(QColor(18, 20, 23, 220), 4.0,
                        Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(center, handle);
    painter.setPen(QPen(overlay_accent(), 2.0, Qt::SolidLine,
                        Qt::RoundCap));
    painter.drawLine(center, handle);
    painter.setPen(QPen(QColor(18, 20, 23), 1.5));
    painter.setBrush(overlay_accent());
    painter.drawEllipse(handle, 5.5, 5.5);
  }

  painter.setPen(QPen(QColor(18, 20, 23, 230), 5.0,
                      Qt::SolidLine, Qt::RoundCap));
  painter.drawLine(center + QPointF(-9.0, 0.0),
                   center + QPointF(9.0, 0.0));
  painter.drawLine(center + QPointF(0.0, -9.0),
                   center + QPointF(0.0, 9.0));
  painter.setPen(QPen(overlay_accent(), 2.0, Qt::SolidLine, Qt::RoundCap));
  painter.drawLine(center + QPointF(-9.0, 0.0),
                   center + QPointF(9.0, 0.0));
  painter.drawLine(center + QPointF(0.0, -9.0),
                   center + QPointF(0.0, 9.0));
  painter.restore();
}

void ZoomableImagePreview::draw_tilt_shift_overlay(QPainter& painter) const {
  if (!tilt_shift_overlay_ || image_.isNull()) {
    return;
  }
  const auto target = displayed_rect();
  const auto visible_target = target.intersected(QRectF(rect()));
  if (visible_target.isEmpty()) {
    return;
  }

  const auto& overlay = *tilt_shift_overlay_;
  const auto center = tilt_center_point(overlay);
  // Patent design constraint (Apple US 8971623; docs/legal-constraints.md):
  // the band boundaries carry no line or bar at all, only their handle dots.
  // Never draw a mark along a boundary, short or spanning;
  // ui_filter_gallery_tilt_shift_overlay_has_no_boundary_marks pins this.
  const auto visible_shorter =
      std::min(visible_target.width(), visible_target.height());
  // Keep an unpainted rim even at tiny zooms, at corners and when the image is
  // clipped by the viewport. Pen caps and the other handles must not bridge it.
  const auto rim = std::max(1.0, std::min(4.0, visible_shorter * 0.1));
  const auto overlay_clip = visible_target.adjusted(rim, rim, -rim, -rim);
  if (overlay_clip.isEmpty()) {
    return;
  }

  painter.save();
  painter.setClipRect(overlay_clip);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setBrush(Qt::NoBrush);

  const auto angle_handle = tilt_angle_handle_point(overlay);
  const auto opposite_angle_handle = center - (angle_handle - center);
  painter.setPen(QPen(QColor(18, 20, 23, 225), 4.0,
                      Qt::SolidLine, Qt::RoundCap));
  painter.drawLine(opposite_angle_handle, angle_handle);
  painter.setPen(QPen(overlay_accent(), 2.0, Qt::SolidLine, Qt::RoundCap));
  painter.drawLine(opposite_angle_handle, angle_handle);

  const auto draw_handle = [&](QPointF point, bool filled) {
    painter.setPen(QPen(QColor(18, 20, 23, 230), 2.0));
    painter.setBrush(filled ? overlay_accent() : QColor(31, 33, 37, 225));
    painter.drawEllipse(point, 5.5, 5.5);
    if (!filled) {
      painter.setPen(QPen(overlay_accent(), 2.0));
      painter.setBrush(Qt::NoBrush);
      painter.drawEllipse(point, 4.0, 4.0);
    }
  };
  draw_handle(angle_handle, true);
  draw_handle(tilt_focus_handle_point(overlay, 1.0), true);
  draw_handle(tilt_focus_handle_point(overlay, -1.0), true);
  draw_handle(tilt_transition_handle_point(overlay, 1.0), false);
  draw_handle(tilt_transition_handle_point(overlay, -1.0), false);

  painter.setPen(QPen(QColor(18, 20, 23, 230), 5.0,
                      Qt::SolidLine, Qt::RoundCap));
  painter.drawLine(center + QPointF(-9.0, 0.0),
                   center + QPointF(9.0, 0.0));
  painter.drawLine(center + QPointF(0.0, -9.0),
                   center + QPointF(0.0, 9.0));
  painter.setPen(QPen(overlay_accent(), 2.0, Qt::SolidLine, Qt::RoundCap));
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

void ZoomableImagePreview::request_tilt_shift_overlay(
    NormalizedTiltShiftOverlay overlay, bool gesture_finished) {
  overlay.center.setX(std::clamp(overlay.center.x(), 0.0, 1.0));
  overlay.center.setY(std::clamp(overlay.center.y(), 0.0, 1.0));
  overlay.angle_degrees = normalized_degrees(overlay.angle_degrees);
  overlay.focus_half_width =
      std::clamp(overlay.focus_half_width, 0.0, 1.0);
  overlay.transition_width =
      std::clamp(overlay.transition_width, 0.0, 1.0);
  if (tilt_shift_changed_) {
    tilt_shift_changed_(std::move(overlay), gesture_finished);
  }
}

void ZoomableImagePreview::update_interaction_cursor(QPointF position) {
  if (panning_) {
    setCursor(interaction_cursor(true));
    return;
  }
  const auto handle = active_overlay_handle_ != OverlayHandle::None
                          ? active_overlay_handle_
                          : overlay_handle_at(position);
  if (handle == OverlayHandle::Center) {
    setCursor(Qt::SizeAllCursor);
  } else if (handle == OverlayHandle::Radius) {
    setCursor(Qt::SizeHorCursor);
  } else if (handle == OverlayHandle::TiltCenter) {
    setCursor(Qt::SizeAllCursor);
  } else if (handle == OverlayHandle::TiltAngle) {
    setCursor(Qt::CrossCursor);
  } else if (handle == OverlayHandle::TiltFocus ||
             handle == OverlayHandle::TiltTransition) {
    setCursor(Qt::SizeVerCursor);
  } else if (scrubby_zoom_ && !space_held_) {
    setCursor(Qt::SizeHorCursor);
  } else {
    setCursor(interaction_cursor(false));
  }
}

void ZoomableImagePreview::clamp_pan() {
  if (fit_mode_ || logical_size_.isEmpty()) {
    pan_offset_ = {};
    return;
  }
  const auto z = zoom();
  const auto excess_x = std::max(0.0, logical_size_.width() * z - width());
  const auto excess_y = std::max(0.0, logical_size_.height() * z - height());
  pan_offset_.setX(std::clamp(pan_offset_.x(), -excess_x / 2.0,
                              excess_x / 2.0));
  pan_offset_.setY(std::clamp(pan_offset_.y(), -excess_y / 2.0,
                              excess_y / 2.0));
}

void ZoomableImagePreview::publish_state() {
  request_display_cache();
  setProperty("previewZoomPercent",
              static_cast<int>(std::lround(zoom() * 100.0)));
  setProperty("previewFitMode", fit_mode_);
  setProperty("previewPanOffset",
              QPoint(static_cast<int>(std::lround(pan_offset_.x())),
                     static_cast<int>(std::lround(pan_offset_.y()))));
  publish_overlay_state();
  if (zoom_changed_) {
    zoom_changed_();
  }
}

void ZoomableImagePreview::publish_overlay_state() {
  const auto visible = center_radius_overlay_.has_value();
  const auto tilt_visible = tilt_shift_overlay_.has_value();
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
      case OverlayHandle::TiltCenter:
        return QStringLiteral("tiltCenter");
      case OverlayHandle::TiltAngle:
        return QStringLiteral("tiltAngle");
      case OverlayHandle::TiltFocus:
        return QStringLiteral("tiltFocus");
      case OverlayHandle::TiltTransition:
        return QStringLiteral("tiltTransition");
    }
    return QStringLiteral("none");
  }();

  setProperty("filterSpatialOverlayVisible", visible || tilt_visible);
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

  const auto tilt_center = tilt_visible
                               ? tilt_shift_overlay_->center
                               : QPointF(-1.0, -1.0);
  const auto tilt_angle =
      tilt_visible ? tilt_shift_overlay_->angle_degrees : 0.0;
  const auto tilt_focus =
      tilt_visible ? tilt_shift_overlay_->focus_half_width : -1.0;
  const auto tilt_transition =
      tilt_visible ? tilt_shift_overlay_->transition_width : -1.0;
  setProperty("filterTiltShiftOverlayVisible", tilt_visible);
  setProperty("filterTiltShiftCenterXNormalized", tilt_center.x());
  setProperty("filterTiltShiftCenterYNormalized", tilt_center.y());
  setProperty("filterTiltShiftAngleDegrees", tilt_angle);
  setProperty("filterTiltShiftFocusHalfWidthNormalized", tilt_focus);
  setProperty("filterTiltShiftTransitionWidthNormalized", tilt_transition);
  setProperty("filterTiltShiftFocusHalfWidthPercent",
              tilt_visible ? tilt_focus * 100.0 : -1.0);
  setProperty("filterTiltShiftTransitionWidthPercent",
              tilt_visible ? tilt_transition * 100.0 : -1.0);
  setProperty("filterTiltShiftDragging",
              active_overlay_handle_ == OverlayHandle::TiltCenter ||
                  active_overlay_handle_ == OverlayHandle::TiltAngle ||
                  active_overlay_handle_ == OverlayHandle::TiltFocus ||
                  active_overlay_handle_ == OverlayHandle::TiltTransition);
  setProperty("filterTiltShiftDragHandle",
              tilt_visible ? handle_name : QStringLiteral("none"));
  setProperty("filterTiltShiftCenterPoint",
              tilt_visible ? tilt_center_point(*tilt_shift_overlay_)
                           : QPointF(-1.0, -1.0));
  setProperty("filterTiltShiftAngleHandlePoint",
              tilt_visible ? tilt_angle_handle_point(*tilt_shift_overlay_)
                           : QPointF(-1.0, -1.0));
  setProperty("filterTiltShiftFocusHandlePoint",
              tilt_visible
                  ? tilt_focus_handle_point(*tilt_shift_overlay_, 1.0)
                  : QPointF(-1.0, -1.0));
  setProperty("filterTiltShiftTransitionHandlePoint",
              tilt_visible
                  ? tilt_transition_handle_point(*tilt_shift_overlay_, 1.0)
                  : QPointF(-1.0, -1.0));
}

}  // namespace patchy::ui
