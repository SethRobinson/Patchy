#include "ui/canvas_widget.hpp"
#include "ui/background_workers.hpp"

#include <QApplication>
#include <QPainter>
#include <QPointer>

#include <cmath>
#include <utility>

namespace patchy::ui {

void CanvasWidget::set_vector_preview_enabled(bool enabled) {
  if (vector_preview_enabled_ == enabled) {
    return;
  }
  vector_preview_enabled_ = enabled;
  invalidate_vector_preview();
  if (!enabled) {
    report_vector_preview_status({});
  }
  update();
}

bool CanvasWidget::vector_preview_enabled() const noexcept { return vector_preview_enabled_; }
QString CanvasWidget::vector_preview_status() const { return vector_preview_status_; }

void CanvasWidget::set_vector_preview_status_callback(std::function<void(QString)> callback) {
  vector_preview_status_callback_ = std::move(callback);
}

void CanvasWidget::report_vector_preview_status(QString status) {
  if (vector_preview_status_ == status) {
    return;
  }
  vector_preview_status_ = std::move(status);
  if (vector_preview_status_callback_) {
    vector_preview_status_callback_(vector_preview_status_);
  }
}

void CanvasWidget::invalidate_vector_preview() noexcept {
  ++vector_preview_generation_;
  if (vector_preview_cancel_) {
    vector_preview_cancel_->store(true, std::memory_order_relaxed);
  }
  vector_preview_scene_.reset();
  vector_preview_image_ = {};
  vector_preview_completed_view_.reset();
  vector_preview_fallback_ = VectorPreviewFallback::None;
}

bool CanvasWidget::vector_preview_available_for_view() const noexcept {
  return vector_preview_enabled_ && document_ != nullptr && isVisible() &&
      zoom_ * devicePixelRatioF() > 1.0 && !pointer_gesture_active() &&
      !transforming_layer_ && !warping_layer_ && !tiling_preview_enabled_ &&
      layer_edit_target_ == LayerEditTarget::Content && !quick_mask_active_ &&
      mask_display_mode_ == MaskDisplayMode::None && !curves_clipping_mode_ &&
      !processing_operation_active();
}

VectorPreviewView CanvasWidget::vector_preview_view() const noexcept {
  const double dpr = devicePixelRatioF();
  return {QSize(static_cast<int>(std::ceil(width() * dpr)), static_cast<int>(std::ceil(height() * dpr))),
          zoom_ * dpr, pan_ * dpr};
}

bool CanvasWidget::vector_preview_settled() const noexcept {
  if (!vector_preview_available_for_view() || kBackgroundWorkRunsInline ||
      (vector_preview_scene_ && vector_preview_scene_->fallback != VectorPreviewFallback::None)) {
    return true;
  }
  return !vector_preview_in_flight_ && vector_preview_completed_generation_ == vector_preview_generation_ &&
      vector_preview_completed_view_ == vector_preview_view();
}

void CanvasWidget::prepare_vector_preview() {
  if (!vector_preview_available_for_view()) {
    if (vector_preview_cancel_) {
      vector_preview_cancel_->store(true, std::memory_order_relaxed);
    }
    if (vector_preview_enabled_) {
      report_vector_preview_status(zoom_ * devicePixelRatioF() <= 1.0
          ? tr("Vector Preview: pixel view at this zoom.")
          : tr("Vector Preview: pixel view during editing or alternate canvas views."));
    }
    return;
  }
  if constexpr (kBackgroundWorkRunsInline) {
    report_vector_preview_status(tr("Pixel view: Vector Preview requires background rendering."));
    return;
  }
  const auto view = vector_preview_view();
  if (!vector_preview_in_flight_ && vector_preview_completed_generation_ == vector_preview_generation_ &&
      vector_preview_completed_view_ == view) {
    report_vector_preview_status(vector_preview_fallback_ == VectorPreviewFallback::None
        ? tr("Vector Preview: sharp vector view.") : vector_preview_fallback_text(vector_preview_fallback_));
    return;
  }
  if (!vector_preview_scene_) {
    try {
      vector_preview_scene_ = std::make_shared<VectorPreviewScene>(build_vector_preview_scene(std::as_const(*document_)));
    } catch (...) {
      vector_preview_completed_view_ = view;
      vector_preview_completed_generation_ = vector_preview_generation_;
      vector_preview_fallback_ = VectorPreviewFallback::Memory;
      report_vector_preview_status(vector_preview_fallback_text(vector_preview_fallback_));
      return;
    }
  }
  if (vector_preview_scene_->fallback != VectorPreviewFallback::None) {
    report_vector_preview_status(vector_preview_fallback_text(vector_preview_scene_->fallback));
    return;
  }
  if (vector_preview_in_flight_) {
    if (vector_preview_requested_view_ != view) {
      vector_preview_cancel_->store(true, std::memory_order_relaxed);
    }
    return;
  }
  try {
    vector_preview_cancel_ = std::make_shared<std::atomic_bool>(false);
  } catch (const std::bad_alloc&) {
    vector_preview_completed_view_ = view;
    vector_preview_completed_generation_ = vector_preview_generation_;
    vector_preview_fallback_ = VectorPreviewFallback::Memory;
    vector_preview_image_ = {};
    report_vector_preview_status(vector_preview_fallback_text(vector_preview_fallback_));
    return;
  }
  vector_preview_requested_view_ = view;
  vector_preview_in_flight_ = true;
  ++render_cache_diagnostics_.vector_preview_renders;
  const auto generation = vector_preview_generation_;
  const auto retained_bytes = static_cast<std::uint64_t>(vector_preview_image_.sizeInBytes());
  report_vector_preview_status(tr("Vector Preview: rendering sharp vectors..."));
  QPointer<CanvasWidget> widget(this);
  auto* app = QApplication::instance();
  run_tracked_background_worker([widget, app, generation, view, retained_bytes,
                                 scene = vector_preview_scene_, cancelled = vector_preview_cancel_] {
    auto result = render_vector_preview(*scene, view, retained_bytes, cancelled.get());
    QMetaObject::invokeMethod(app, [widget, generation, view, cancelled, result = std::move(result)]() mutable {
      if (!widget) {
        return;
      }
      widget->vector_preview_in_flight_ = false;
      if (!cancelled->load(std::memory_order_relaxed) && generation == widget->vector_preview_generation_ &&
          widget->vector_preview_available_for_view() && view == widget->vector_preview_view()) {
        widget->vector_preview_image_ = std::move(result.image);
        widget->vector_preview_completed_view_ = view;
        widget->vector_preview_completed_generation_ = generation;
        widget->vector_preview_fallback_ = result.fallback;
        widget->render_cache_diagnostics_.vector_preview_peak_raster_bytes = result.peak_raster_bytes;
        widget->render_cache_diagnostics_.vector_preview_elapsed_ms = result.elapsed_ms;
      }
      widget->update();
    }, Qt::QueuedConnection);
  });
}

bool CanvasWidget::draw_vector_preview(QPainter& painter) {
  if (!vector_preview_available_for_view() || vector_preview_image_.isNull() ||
      !vector_preview_completed_view_ || vector_preview_completed_generation_ != vector_preview_generation_) {
    return false;
  }
  // A view-only change can reuse the old frame at its document location while
  // the newest viewport renders. Uncovered strips use the ordinary composite.
  const auto& old = *vector_preview_completed_view_;
  const auto origin = pan_ - old.offset * (zoom_ / old.scale);
  const auto size = QSizeF(old.pixels) * (zoom_ / old.scale);
  painter.save();
  painter.setRenderHint(QPainter::SmoothPixmapTransform, old != vector_preview_view());
  const QRectF destination(origin, size);
  draw_checkerboard(painter, QRectF(pan_, QSizeF(document_->width() * zoom_, document_->height() * zoom_)),
                    destination.toAlignedRect().intersected(rect()));
  painter.drawImage(destination, vector_preview_image_);
  painter.restore();
  return true;
}

}  // namespace patchy::ui
