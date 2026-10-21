#include "ui/canvas_widget.hpp"

#include "ui/background_workers.hpp"
#include "ui/canvas_widget_shared.hpp"
#include "ui/image_document_io.hpp"
#include "ui/vector_preview_renderer.hpp"

#include <QApplication>
#include <QMetaObject>
#include <QPainter>
#include <QPointer>
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QString>
#include <Qt>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <new>
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

void CanvasWidget::set_vector_preview_status_callback(std::function<void(QString, bool)> callback) {
  vector_preview_status_callback_ = std::move(callback);
}

void CanvasWidget::report_vector_preview_status(QString status, bool notice) {
  if (vector_preview_status_ == status) {
    return;
  }
  vector_preview_status_ = std::move(status);
  if (vector_preview_status_callback_) {
    vector_preview_status_callback_(vector_preview_status_, notice);
  }
}

void CanvasWidget::invalidate_vector_preview() noexcept {
  ++vector_preview_generation_;
  if (vector_preview_cancel_) {
    vector_preview_cancel_->store(true, std::memory_order_relaxed);
  }
  vector_preview_scene_.reset();
  vector_preview_transform_document_.reset();
  vector_preview_image_ = {};
  vector_preview_grid_image_ = {};
  vector_preview_grid_rect_ = {};
  vector_preview_completed_view_.reset();
  vector_preview_fallback_ = VectorPreviewFallback::None;
}

bool CanvasWidget::vector_preview_available_for_view() const noexcept {
  return vector_preview_enabled_ && document_ != nullptr && isVisible() &&
      view_zoom() > 1.0 && !pointer_gesture_active() &&
      (!transforming_layer_ || free_transform_vector_preview_possible()) && !warping_layer_ && !tiling_preview_enabled_ &&
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
  // A Free Transform change the next paint has not seen yet is a frame still to render.
  if (vector_preview_available_for_view() && !kBackgroundWorkRunsInline &&
      free_transform_vector_preview_key() != vector_preview_transform_key_) {
    return false;
  }
  if (!vector_preview_available_for_view() || kBackgroundWorkRunsInline ||
      (vector_preview_scene_ && (vector_preview_scene_->fallback != VectorPreviewFallback::None ||
                                 !vector_preview_scene_->has_vectors))) {
    return true;
  }
  return !vector_preview_in_flight_ && vector_preview_completed_generation_ == vector_preview_generation_ &&
      vector_preview_completed_view_ == vector_preview_view();
}

void CanvasWidget::prepare_vector_preview() {
  // A Free Transform session previews the document as its commit would leave it; any
  // change of the session (or its start and end) is a new scene.
  auto transform_key = vector_preview_enabled_ ? free_transform_vector_preview_key() : std::nullopt;
  if (transform_key != vector_preview_transform_key_) {
    invalidate_vector_preview();
    vector_preview_transform_key_ = transform_key;
  }
  if (!vector_preview_available_for_view()) {
    if (vector_preview_cancel_) {
      vector_preview_cancel_->store(true, std::memory_order_relaxed);
    }
    if (vector_preview_enabled_) {
      report_vector_preview_status(view_zoom() <= 1.0
          ? tr("Dynamic Vector Preview: pixel view at this zoom.")
          : tr("Dynamic Vector Preview: pixel view during editing or alternate canvas views."));
    }
    return;
  }
  if constexpr (kBackgroundWorkRunsInline) {
    report_vector_preview_status(tr("Pixel view: Dynamic Vector Preview requires background rendering."));
    return;
  }
  const auto view = vector_preview_view();
  if (!vector_preview_in_flight_ && vector_preview_completed_generation_ == vector_preview_generation_ &&
      vector_preview_completed_view_ == view) {
    report_vector_preview_status(vector_preview_fallback_ == VectorPreviewFallback::None
        ? tr("Dynamic Vector Preview: sharp vector view.") : vector_preview_fallback_text(vector_preview_fallback_),
        vector_preview_fallback_ != VectorPreviewFallback::None);
    return;
  }
  if (!vector_preview_scene_) {
    try {
      if (transform_key.has_value()) {
        auto preview = std::make_shared<const Document>(free_transform_vector_preview_document(*transform_key));
        vector_preview_scene_ = std::make_shared<VectorPreviewScene>(build_vector_preview_scene(*preview));
        vector_preview_transform_document_ = std::move(preview);
      } else {
        vector_preview_scene_ = std::make_shared<VectorPreviewScene>(build_vector_preview_scene(std::as_const(*document_)));
      }
    } catch (...) {
      vector_preview_completed_view_ = view;
      vector_preview_completed_generation_ = vector_preview_generation_;
      vector_preview_fallback_ = VectorPreviewFallback::Memory;
      report_vector_preview_status(vector_preview_fallback_text(vector_preview_fallback_), true);
      return;
    }
  }
  if (vector_preview_scene_->fallback != VectorPreviewFallback::None) {
    report_vector_preview_status(vector_preview_fallback_text(vector_preview_scene_->fallback));
    return;
  }
  if (!vector_preview_scene_->has_vectors) {
    report_vector_preview_status(tr("Dynamic Vector Preview: no vector artwork to sharpen."));
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
    report_vector_preview_status(vector_preview_fallback_text(vector_preview_fallback_), true);
    return;
  }
  vector_preview_requested_view_ = view;
  vector_preview_in_flight_ = true;
  ++render_cache_diagnostics_.vector_preview_renders;
  const auto generation = vector_preview_generation_;
  const auto retained_bytes = static_cast<std::uint64_t>(vector_preview_image_.sizeInBytes());
  report_vector_preview_status(tr("Dynamic Vector Preview: rendering sharp vectors..."));
  // A transform scene's grid tones come from its own document, over the view's document
  // rect only (the grid shows at deep zoom, where that rect is small).
  std::shared_ptr<const Document> grid_source;
  QRect grid_rect;
  if (vector_preview_transform_document_ && pixel_grid_visible_ && uses_deep_zoom_pixel_renderer(view_zoom())) {
    const auto left = static_cast<int>(std::floor(-view.offset.x() / view.scale));
    const auto top = static_cast<int>(std::floor(-view.offset.y() / view.scale));
    const auto right = static_cast<int>(std::ceil((view.pixels.width() - view.offset.x()) / view.scale));
    const auto bottom = static_cast<int>(std::ceil((view.pixels.height() - view.offset.y()) / view.scale));
    grid_rect = QRect(QPoint(left, top), QPoint(right - 1, bottom - 1))
                    .intersected(QRect(0, 0, document_->width(), document_->height()));
    if (!grid_rect.isEmpty()) {
      grid_source = vector_preview_transform_document_;
    }
  }
  QPointer<CanvasWidget> widget(this);
  auto* app = QApplication::instance();
  run_tracked_background_worker([widget, app, generation, view, retained_bytes, grid_source, grid_rect,
                                 scene = vector_preview_scene_, cancelled = vector_preview_cancel_] {
    auto result = render_vector_preview(*scene, view, retained_bytes, cancelled.get());
    QImage grid_image;
    if (grid_source && !result.image.isNull() && !cancelled->load(std::memory_order_relaxed)) {
      grid_image = qimage_from_document_rect(*grid_source, grid_rect, true);
    }
    QMetaObject::invokeMethod(app, [widget, generation, view, cancelled, grid_rect, grid_image = std::move(grid_image),
                                    result = std::move(result)]() mutable {
      if (!widget) {
        return;
      }
      widget->vector_preview_in_flight_ = false;
      if (!cancelled->load(std::memory_order_relaxed) && generation == widget->vector_preview_generation_ &&
          widget->vector_preview_available_for_view() && view == widget->vector_preview_view()) {
        widget->vector_preview_image_ = std::move(result.image);
        widget->vector_preview_grid_image_ = std::move(grid_image);
        widget->vector_preview_grid_rect_ = grid_rect;
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

bool CanvasWidget::draw_vector_preview(QPainter& painter, QRect exposed_rect) {
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
  // The backdrop the composite draws on: the checkerboard, or with artboards the
  // pasteboard between them and the checkerboard inside each.
  draw_canvas_backdrop(painter, QRectF(pan_, QSizeF(document_->width() * zoom_, document_->height() * zoom_)),
                       destination.toAlignedRect().intersected(rect()));
  painter.drawImage(destination, vector_preview_image_);
  painter.restore();
  // The frame just covered the composite's deep-zoom raster, pixel grid included. Put the
  // grid back on top, from the document pixels (each line keeps its cell's own tone), at
  // the current view's cell edges: so View > Pixel Grid shows with the sharp view, during
  // a zoom while the old frame is mapped, and before a new frame lands. One grid-only
  // raster write per exposed device pixel, the same bound as the composite's own blit.
  if (pixel_grid_visible_ && uses_deep_zoom_pixel_renderer(view_zoom())) {
    if (vector_preview_transform_key_.has_value()) {
      // A Free Transform frame: the render cache still holds the layer where it was, so the
      // grid takes its tones from the frame's own document.
      if (!vector_preview_grid_image_.isNull()) {
        const QImage under = proof_display_image(vector_preview_grid_image_);
        draw_deep_zoom_pixels(painter, under, vector_preview_grid_rect_, exposed_rect, true);
      }
    } else {
      const QImage under = proof_display_image(render_cache_);
      draw_deep_zoom_pixels(painter, under, QRect(QPoint(0, 0), under.size()), exposed_rect, true);
    }
  }
  return true;
}

}  // namespace patchy::ui
