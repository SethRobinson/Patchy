// CanvasWidget's rendering implementation, split out of canvas_widget.cpp:
// paintEvent, the render invalidation and processing-operation plumbing (the
// document_changed overload family, force_refresh, and the processing overlay),
// the composite render cache with its async refresh and patch renderers, the
// display mip / curves-clipping / move-base display caches, and the static
// overlay painters (checkerboard, deep zoom, grid, guides, rulers, and the
// mask display overlay). Pure function moves from canvas_widget.cpp; behavior
// must stay identical.

#include "ui/canvas_widget.hpp"

#include "color/color_management.hpp"
#include "core/artboard.hpp"
#include "core/document.hpp"
#include "core/document_channel.hpp"
#include "core/layer.hpp"
#include "core/layer_render_utils.hpp"
#include "core/pixel_buffer.hpp"
#include "core/smart_object.hpp"
#include "core/worker_budget.hpp"
#include "ui/background_workers.hpp"
#include "ui/canvas_types.hpp"
#include "ui/canvas_widget_shared.hpp"
#include "ui/curves_clipping_preview.hpp"
#include "ui/image_document_io.hpp"
#include "ui/measurement_units.hpp"
#include "ui/theme_palette.hpp"

#include <QApplication>
#include <QBrush>
#include <QColor>
#include <QEvent>
#include <QEventLoop>
#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QLatin1Char>
#include <QMetaObject>
#include <QMouseEvent>
#include <QObject>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QPoint>
#include <QPointF>
#include <QPointer>
#include <QRect>
#include <QRectF>
#include <QRegion>
#include <QRgb>
#include <QScrollBar>
#include <QSize>
#include <QString>
#include <QTimer>
#include <Qt>
#include <QtGlobal>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

#ifdef Q_OS_WASM
#include "ui/dialog_utils_wasm.hpp"
#endif

namespace patchy::ui {

// One running render and one replaceable request. The worker drains the queue
// itself so exact-pixel readers can wait without depending on UI completion
// delivery to start the latest request.
struct CanvasWidget::MoveCommitWorker {
  struct Request {
    std::uint64_t generation{};
    std::shared_ptr<const Document> snapshot;
    QRegion region;
    std::shared_ptr<std::promise<std::vector<RenderedDocumentPatch>>> promise;
    std::shared_ptr<std::atomic_bool> cancelled;
  };
  std::mutex mutex;
  bool running{false};
  std::optional<Request> pending;
  std::shared_ptr<std::atomic_bool> current_cancel;
};

namespace {

constexpr std::int64_t kProcessingOverlayDirtyAreaThreshold = 8'000'000;
constexpr int kProcessingOverlayDelayMs = 1000;
constexpr int kProcessingAnimationIntervalMs = 80;
constexpr int kMaxDirtyRegionRects = 64;
// A full recomposite can take seconds on a document with hundreds of layers
// even when its pixel area is small (the 622-layer Affinity card template is
// under 1 Mpx but composites in ~5 s cold: dozens of styled layers each
// recompute stroke distance fields). Such documents take the same async
// full-refresh path as pixel-huge ones instead of blocking paint. Compile-time
// constant on purpose, like the pixel threshold above: the gate must stay
// deterministic for tests.
constexpr int kDeferFullRefreshMinLayers = 200;

int total_layer_count(const std::vector<Layer>& layers) noexcept {
  int count = 0;
  for (const auto& layer : layers) {
    count += 1 + total_layer_count(layer.children());
  }
  return count;
}

int processing_overlay_delay_ms() noexcept {
  bool ok = false;
  const auto value = qEnvironmentVariableIntValue("PATCHY_PROCESSING_OVERLAY_DELAY_MS", &ok);
  return ok ? std::max(0, value) : kProcessingOverlayDelayMs;
}

std::int64_t processing_overlay_dirty_area_threshold() noexcept {
  bool ok = false;
  const auto value = qEnvironmentVariableIntValue("PATCHY_PROCESSING_OVERLAY_MIN_PIXELS", &ok);
  return ok ? std::max(0, value) : kProcessingOverlayDirtyAreaThreshold;
}

int processing_render_test_delay_ms() noexcept {
  bool ok = false;
  const auto value = qEnvironmentVariableIntValue("PATCHY_PROCESSING_RENDER_TEST_DELAY_MS", &ok);
  return ok ? std::max(0, value) : 0;
}

int mip_dimension(int size, int level) noexcept {
  for (int i = 0; i < level && size > 1; ++i) {
    size = (size + 1) / 2;
  }
  return std::max(1, size);
}

QImage downscaled_to_mip_level(QImage image, int level) {
  for (int i = 0; i < level && !image.isNull(); ++i) {
    const QSize next_size(std::max(1, (image.width() + 1) / 2), std::max(1, (image.height() + 1) / 2));
    if (next_size == image.size()) {
      break;
    }
    image = halved_display_mip(image);
  }
  return image;
}

double pixel_aligned_coordinate(double coordinate, double zoom) noexcept {
  return uses_pixel_aligned_view(zoom) ? std::round(coordinate) : coordinate;
}

std::int64_t region_area(QRegion region) noexcept {
  std::int64_t area = 0;
  for (const auto& rect : region) {
    if (rect.isEmpty()) {
      continue;
    }
    area += static_cast<std::int64_t>(rect.width()) * static_cast<std::int64_t>(rect.height());
  }
  return area;
}

QRegion outset_region(const QRegion& region, int amount) {
  if (amount <= 0 || region.isEmpty()) {
    return region;
  }
  QRegion expanded;
  for (const auto& rect : region) {
    if (!rect.isEmpty()) {
      expanded += rect.adjusted(-amount, -amount, amount, amount);
    }
  }
  return expanded;
}

// The Move tool's layer outline (hover, selection-rectangle preview, drag):
// light-blue dashes over a solid dark line on the first widget pixels outside
// the content box [top_left, end) (end exclusive), so it never covers the art
// and reads on light, dark and blue pixels alike. A layer narrower than a
// widget pixel (top_left == end) still gets a 2 px ring. Like the marching
// ants, the pair is theme-exempt (docs/ui-conventions.md): it sits on
// artwork, not on chrome.
void draw_move_layer_outline(QPainter& painter, QPoint top_left, QPoint end) {
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, false);
  painter.setBrush(Qt::NoBrush);
  // A 1 px aliased drawRect covers left()..right() + 1, so the ring lands on
  // top_left - 1 and end.
  const QRect ring(top_left - QPoint(1, 1), end - QPoint(1, 1));
  painter.setPen(QPen(QColor(18, 20, 24), 1));
  painter.drawRect(ring);
  painter.setPen(QPen(QColor(95, 170, 255), 1, Qt::DashLine));
  painter.drawRect(ring);
  painter.restore();
}

}  // namespace

CanvasWidget::RenderCacheDiagnostics CanvasWidget::render_cache_diagnostics() const noexcept {
  return render_cache_diagnostics_;
}

bool CanvasWidget::render_settled() const noexcept {
  return !render_cache_dirty_ && !async_render_cache_in_flight_ && !move_commit_job_.has_value() &&
         !move_preview_in_flight_ &&
         vector_preview_settled();
}

bool CanvasWidget::move_commit_job_pending() const noexcept {
  return move_commit_job_.has_value();
}

bool CanvasWidget::should_defer_full_refresh_to_async() const noexcept {
  if constexpr (kBackgroundWorkRunsInline) {
    // With workers running inline, deferring means composing the frame inside
    // paintEvent anyway, plus a Document deep copy and a second repaint. The
    // direct ensure_render_cache path is strictly cheaper there.
    return false;
  }
  if (!render_cache_dirty_ || document_ == nullptr || processing_operation_active()) {
    return false;
  }
  if (render_cache_.isNull() ||
      render_cache_.size() != QSize(document_->width(), document_->height())) {
    return false;
  }
  const auto canvas_area =
      static_cast<std::int64_t>(document_->width()) * static_cast<std::int64_t>(document_->height());
  return canvas_area >= kProcessingOverlayDirtyAreaThreshold ||
         total_layer_count(std::as_const(*document_).layers()) >= kDeferFullRefreshMinLayers;
}

bool CanvasWidget::should_prepare_move_preview_async() const noexcept {
  if constexpr (kBackgroundWorkRunsInline) return false;
  return document_ != nullptr &&
         (static_cast<std::int64_t>(document_->width()) * document_->height() >= kProcessingOverlayDirtyAreaThreshold ||
          total_layer_count(std::as_const(*document_).layers()) >= kDeferFullRefreshMinLayers);
}

bool CanvasWidget::should_defer_first_render_to_async() const noexcept {
  // The deferred path above needs a same-size previous frame to keep showing.
  // Without one (first paint of a fresh session, or a canvas resize), a
  // many-layer document still must not block paint on a seconds-long
  // composite: kick the async refresh and paint checkerboard plus the
  // processing spinner until the frame lands. Pixel-huge documents keep their
  // historical synchronous first paint.
  if constexpr (kBackgroundWorkRunsInline) {
    return false;
  }
  if (document_ == nullptr || processing_operation_active()) {
    return false;
  }
  if (!render_cache_dirty_ && !render_cache_.isNull()) {
    return false;
  }
  if (!render_cache_.isNull() &&
      render_cache_.size() == QSize(document_->width(), document_->height())) {
    return false;
  }
  return total_layer_count(std::as_const(*document_).layers()) >= kDeferFullRefreshMinLayers;
}

bool CanvasWidget::first_render_spinner_active() const noexcept {
  return async_render_cache_in_flight_ && document_ != nullptr &&
         (render_cache_.isNull() ||
          render_cache_.size() != QSize(document_->width(), document_->height())) &&
         total_layer_count(std::as_const(*document_).layers()) >= kDeferFullRefreshMinLayers;
}

bool CanvasWidget::processing_overlay_visible() const noexcept {
  return processing_overlay_visible_;
}

bool CanvasWidget::processing_operation_active() const noexcept {
  return processing_operation_depth_ > 0;
}

void CanvasWidget::begin_processing_operation(QString message, int delay_ms_override) {
  if (processing_operation_depth_ == 0) {
    processing_operation_started_ = std::chrono::steady_clock::now();
    processing_operation_owns_overlay_ = false;
    processing_operation_delay_ms_ = delay_ms_override;
    processing_overlay_message_ = message.isEmpty() ? tr("Processing...") : std::move(message);
  }
  ++processing_operation_depth_;
}

void CanvasWidget::set_processing_operation_message(QString message) {
  if (processing_operation_depth_ <= 0) {
    return;
  }
  processing_overlay_message_ = message.isEmpty() ? tr("Processing...") : std::move(message);
  if (processing_overlay_visible_) {
    update();
  }
}

void CanvasWidget::tick_processing_operation() {
  if (processing_operation_depth_ <= 0) {
    return;
  }
  if (!processing_overlay_visible_) {
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - processing_operation_started_)
                                .count();
    const auto delay_ms = processing_operation_delay_ms_ >= 0 ? processing_operation_delay_ms_
                                                              : processing_overlay_delay_ms();
    if (elapsed_ms >= delay_ms) {
      show_processing_overlay(processing_overlay_message_);
      processing_operation_owns_overlay_ = true;
    }
  }
  if (processing_overlay_visible_) {
    // Not on wasm: the browser paints nothing until the main thread suspends
    // in an idle event loop, so a mid-operation pump cannot animate the
    // overlay there; it only dispatches Qt timers into the running operation.
#ifndef Q_OS_WASM
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 16);
#endif
  }
}

void CanvasWidget::end_processing_operation() {
  if (processing_operation_depth_ <= 0) {
    return;
  }
  --processing_operation_depth_;
  if (processing_operation_depth_ != 0) {
    return;
  }
  if (processing_operation_owns_overlay_) {
    hide_processing_overlay();
  }
  processing_operation_owns_overlay_ = false;
  processing_overlay_message_.clear();
#ifdef Q_OS_WASM
  // Same DOM focus heal as wait_for_processing_operation, for operations
  // whose busy stints run without a nested wait (docs/wasm-input.md).
  wasm_files::restore_qt_dom_focus();
#endif
}

void CanvasWidget::notify_document_changed(DocumentChangeReason reason) {
  invalidate_vector_preview();
  mark_object_select_embedding_unverified();
  if (document_changed_reason_callback_) {
    document_changed_reason_callback_(reason);
  } else if (document_changed_callback_) {
    document_changed_callback_();
  }
}

void CanvasWidget::document_changed() {
  clear_transform_commit_hold();
  cancel_move_commit_job();
  cancel_async_render_cache_refresh();
  render_cache_dirty_ = true;
  // This overload bypasses document_changed_impl, so drop the preview-scaled
  // document and retained move caches here too or a zoomed-out drag previews
  // stale content.
  clear_preview_scaled_document();
  invalidate_retained_move_caches();
  mask_display_image_ = QImage();
  mask_display_image_layer_ = 0;
  mask_display_image_channel_ = 0;
  mask_display_image_revision_ = 0;
  invalidate_display_mip_cache();
  refresh_free_transform_preview_caches();
  notify_document_changed();
  if (isVisible()) {
    update();
  }
}

void CanvasWidget::document_changed_async_preview() {
  document_changed_async_preview_impl(false);
}

void CanvasWidget::layer_visibility_changed(LayerId id) {
  if (document_ != nullptr && preview_scaled_document_) {
    const auto* real = std::as_const(*document_).find_layer(id);
    auto* scaled = preview_scaled_document_->find_layer(id);
    if (real != nullptr && scaled != nullptr) {
      scaled->set_visible(real->visible());
    } else {
      clear_preview_scaled_document();
    }
  }
  document_changed_async_preview_impl(true);
}

void CanvasWidget::document_changed_async_preview_impl(bool preserve_scaled_document) {
  clear_transform_commit_hold();
  cancel_move_commit_job();
  if (document_ == nullptr || render_cache_.isNull() ||
      render_cache_.size() != QSize(document_->width(), document_->height())) {
    document_changed();
    return;
  }

  // Same bypass as document_changed(): see the invalidation note there.
  if (!preserve_scaled_document) {
    clear_preview_scaled_document();
  }
  invalidate_retained_move_caches();
  refresh_free_transform_preview_caches();
  notify_document_changed();
  if (!isVisible()) {
    cancel_async_render_cache_refresh();
    render_cache_dirty_ = true;
    invalidate_display_mip_cache();
    return;
  }

  // Paint may hold the previous image, but exact-pixel readers must refresh.
  render_cache_dirty_ = true;
  async_render_cache_explicit_hold_ = true;
  if (async_render_cache_in_flight_) {
    async_render_cache_pending_ = true;
    return;
  }
  if (!async_render_cache_start_queued_) {
    async_render_cache_start_queued_ = true;
    QTimer::singleShot(0, this, [this] {
      async_render_cache_start_queued_ = false;
      if (async_render_cache_explicit_hold_ && !async_render_cache_in_flight_) {
        start_async_render_cache_refresh();
      }
    });
  }
}

void CanvasWidget::force_refresh() {
  if (document_ == nullptr) {
    return;
  }

  clear_transform_commit_hold();
  cancel_move_commit_job();
  cancel_async_render_cache_refresh();
  // Bypasses document_changed_impl: see the invalidation note in
  // document_changed().
  clear_preview_scaled_document();
  invalidate_retained_move_caches();
  refresh_free_transform_preview_caches();
  render_cache_ = render_document_image_with_processing();
  quantize_image_for_palette_display(render_cache_);
  render_cache_dirty_ = render_cache_.isNull();
  mask_display_image_ = QImage();
  mask_display_image_layer_ = 0;
  mask_display_image_channel_ = 0;
  mask_display_image_revision_ = 0;
  move_preview_patches_.clear();
  move_preview_patches_delta_.reset();
  ++render_cache_diagnostics_.full_refreshes;
  ++render_cache_diagnostics_.forced_refreshes;
  invalidate_display_mip_cache();
  refresh_curves_clipping_preview();
  if (status_callback_) {
    status_callback_(tr("Forced refresh"));
  }
  update();
}

void CanvasWidget::document_changed(QRect document_rect) {
  document_changed_impl(QRegion(document_rect), false);
}

void CanvasWidget::document_changed(QRegion document_region) {
  document_changed_impl(std::move(document_region), false);
}

void CanvasWidget::document_changed_effect_bounds(QRect document_rect) {
  document_changed_impl(QRegion(document_rect), true);
}

void CanvasWidget::document_changed_effect_bounds(QRegion document_region) {
  document_changed_impl(std::move(document_region), true);
}

void CanvasWidget::grayscale_target_changed(QRect document_rect, DocumentChangeReason reason) {
  active_edit_target_changed_impl(QRegion(document_rect), reason);
}

void CanvasWidget::active_edit_target_changed_impl(QRegion document_region, DocumentChangeReason reason) {
  if (paint_stroke_history_armed_) {
    // Every write the stroke reports (dabs, wet edges, airbrush ticks) lands
    // here, so the union is the stroke's per-stroke history footprint.
    paint_stroke_history_event_.document_dirty |= document_region.boundingRect();
  }
  // Mask/channel edit branches below return without document_changed_impl;
  // invalidate the retained move caches up front (harmlessly repeated when
  // the plain branch falls through to document_changed_impl).
  clear_transform_commit_hold();
  cancel_move_commit_job();
  invalidate_retained_move_caches();
  if (quick_mask_active_) {
    const auto canvas_rect = document_ != nullptr
                                 ? QRect(0, 0, document_->width(), document_->height())
                                 : QRect();
    document_region = document_region.intersected(canvas_rect);
    if (!document_region.isEmpty()) {
      quick_mask_edit_dirty_ += document_region;
      ++quick_mask_revision_;
      if (!mask_display_image_.isNull() && document_ != nullptr &&
          mask_display_image_.size() ==
              QSize(document_->width(), document_->height())) {
        mask_display_image_revision_ = quick_mask_revision_;
      }
      refresh_mask_display_image(document_region);
    }
    if (reason != DocumentChangeReason::BrushStrokePreview) {
      finish_quick_mask_edit();
    }
    if (!isVisible() || document_region.isEmpty() || zoom_ < 1.0) {
      update();
      return;
    }
    QRegion widget_region;
    for (const auto& rect : document_region) {
      widget_region += widget_repaint_rect_for_document_rect(rect);
    }
    update(widget_region);
    return;
  }
  if (layer_edit_target_ == LayerEditTarget::SmartFilterMask) {
    if (!editing_smart_filter_mask()) {
      clear_smart_filter_mask_edit_target();
      return;
    }
    const auto canvas_rect = QRect(0, 0, document_->width(), document_->height());
    document_region = document_region.intersected(canvas_rect);
    if (!document_region.isEmpty()) {
      smart_filter_mask_edit_dirty_ += document_region;
      ++smart_filter_mask_revision_;
      if (!mask_display_image_.isNull() && mask_display_image_.size() == canvas_rect.size()) {
        mask_display_image_revision_ = smart_filter_mask_revision_;
      }
      refresh_mask_display_image(document_region);
    }
    if (reason != DocumentChangeReason::BrushStrokePreview) {
      finish_smart_filter_mask_edit();
    }
    if (!isVisible() || document_region.isEmpty() || zoom_ < 1.0) {
      update();
      return;
    }
    QRegion widget_region;
    for (const auto& rect : document_region) {
      widget_region += widget_repaint_rect_for_document_rect(rect);
    }
    update(widget_region);
    return;
  }
  if (!editing_document_channel()) {
    document_changed_impl(std::move(document_region), false, reason);
    return;
  }

  const auto canvas_rect = document_ != nullptr ? QRect(0, 0, document_->width(), document_->height()) : QRect();
  document_region = document_region.intersected(canvas_rect);
  if (document_region.isEmpty() && !canvas_rect.isEmpty()) {
    document_region = QRegion(canvas_rect);
  }
  if (const auto* channel = active_document_channel_const();
      channel != nullptr && !mask_display_image_.isNull() && mask_display_image_channel_ == channel->id()) {
    // The mutable channel accessor has already advanced its revision. The
    // cached image still represents the previous revision everywhere except
    // `document_region`, which is patched below, so advance the cache key and
    // retain the bounded refresh instead of rebuilding the full canvas.
    mask_display_image_revision_ = channel->content_revision();
  }
  refresh_mask_display_image(document_region);
  notify_document_changed(reason);
  if (!isVisible()) {
    return;
  }
  if (zoom_ < 1.0 || document_region.isEmpty()) {
    update();
    return;
  }
  QRegion widget_region;
  for (const auto& rect : document_region) {
    widget_region += widget_repaint_rect_for_document_rect(rect);
  }
  update(widget_region);
}

void CanvasWidget::document_changed_impl(QRegion document_region, bool includes_effect_bounds,
                                         DocumentChangeReason reason) {
  // A freshly armed commit hold belongs to exactly this notification (the
  // transform commit arms it right before its document_changed call); any
  // other document change makes the held frame stale and drops it.
  if (transform_commit_hold_fresh_) {
    transform_commit_hold_fresh_ = false;
  } else {
    clear_transform_commit_hold();
  }
  if (move_commit_job_.has_value()) {
    // The pending accurate patches would land on top of this change: drop the
    // job and let this invalidation cover its stale region as well.
    if (!document_region.isEmpty()) {
      document_region += move_commit_job_->region;
    }
    cancel_move_commit_job();
  }
  if (layer_edit_target_ == LayerEditTarget::SmartFilterMask && !editing_smart_filter_mask()) {
    // Layer deletion can occur without replacing the Document object. Drop the
    // canvas-owned buffer before another layer can become active and inherit it.
    clear_smart_filter_mask_edit_target();
  }
  // Any document change invalidates the preview-scaled document; the next
  // zoomed-out drag rebuilds it lazily. The retained move base/proxy go the
  // same way (deferred to the release when a drag is in flight).
  clear_preview_scaled_document();
  invalidate_retained_move_caches();
  cancel_async_render_cache_refresh();
  refresh_free_transform_preview_caches();
  if (mask_display_mode_ != MaskDisplayMode::None) {
    const bool component_preview = CanvasWidget::is_component_preview(layer_edit_target_);
    if (document_region.isEmpty() || component_preview) {
      mask_display_image_ = QImage();
      mask_display_image_layer_ = 0;
      mask_display_image_channel_ = 0;
      mask_display_image_revision_ = 0;
    } else {
      refresh_mask_display_image(document_region);
    }
  }
  const auto mark_full_dirty = [this, reason] {
    render_cache_dirty_ = true;
    invalidate_display_mip_cache();
    notify_document_changed(reason);
    if (isVisible()) {
      update();
    }
  };
  if (!isVisible()) {
    mark_full_dirty();
    return;
  }
  if (document_region.isEmpty()) {
    mark_full_dirty();
    return;
  }

  if (render_cache_dirty_ || render_cache_.isNull()) {
    mark_full_dirty();
    return;
  }

  if (document_ != nullptr) {
    const auto style_padding = includes_effect_bounds ? 0 : document_effect_padding(*document_);
    if (style_padding > 0) {
      document_region = outset_region(document_region, style_padding);
    }
    document_region = document_region.intersected(QRect(0, 0, document_->width(), document_->height()));
    if (document_region.isEmpty()) {
      notify_document_changed(reason);
      return;
    }
    const auto area = region_area(document_region);
    const auto canvas_area = static_cast<std::int64_t>(document_->width()) * static_cast<std::int64_t>(document_->height());
    if (canvas_area > 0 && area * 2 > canvas_area) {
      mark_full_dirty();
      return;
    }
    if (document_region.rectCount() > kMaxDirtyRegionRects) {
      mark_full_dirty();
      return;
    }
  }

  const auto started_render_operation = !processing_operation_active();
  if (started_render_operation) {
    begin_processing_operation();
  }
  refresh_render_cache_region(document_region);
  if (started_render_operation) {
    end_processing_operation();
  }
  notify_document_changed(reason);
  {
    // Zoomed out, the patched display mip changes only the blocks under the
    // region (patch_display_mip_cache); the repaint rect covers the smooth
    // scale's reach into the neighbours. A whole-widget repaint here made
    // every brush frame redraw the full view (GitHub issue 165).
    QRegion widget_region;
    for (const auto& rect : document_region) {
      widget_region += widget_repaint_rect_for_document_rect(rect);
    }
    if (tiling_ghosts_visible(rect())) {
      // Seamless tiling mode: the ghost copies of the dirty region must repaint too.
      const auto offsets = tiling_direct_offsets(rect());
      if (offsets.empty()) {
        // Textured-fill territory (many small tiles): exact replicated rects would miss
        // the rounded grid pitch, so repaint the whole widget.
        update();
        return;
      }
      for (const auto offset : offsets) {
        const QPoint delta(offset.x() * document_->width(), offset.y() * document_->height());
        for (const auto& rect : document_region) {
          widget_region += widget_repaint_rect_for_document_rect(rect.translated(delta));
        }
      }
    }
    update(widget_region);
  }
}

void CanvasWidget::paintEvent(QPaintEvent* event) {
  ZoomTraceScope trace("paint", zoom_);
  QPainter painter(this);
  const auto exposed_rect = event != nullptr ? event->rect() : rect();
  painter.fillRect(exposed_rect, backdrop_color());

  if (document_ == nullptr || document_->width() == 0 || document_->height() == 0) {
    painter.setPen(theme().canvas_empty_text);
    painter.drawText(rect(), Qt::AlignCenter, tr("No document"));
    return;
  }

  const QRectF exact_target_rect(widget_position_f(QPointF(0.0, 0.0)),
                                 widget_position_f(QPointF(document_->width(), document_->height())));
  const bool pixel_aligned_view = uses_pixel_aligned_view(zoom_);
  QRect pixel_aligned_target_rect;
  if (pixel_aligned_view) {
    const auto top_left = widget_position(QPoint(0, 0));
    const auto bottom_right = widget_position(QPoint(document_->width(), document_->height()));
    pixel_aligned_target_rect =
        QRect(top_left, QSize(bottom_right.x() - top_left.x(), bottom_right.y() - top_left.y()));
  }
  const QRectF target_rect = pixel_aligned_view ? QRectF(pixel_aligned_target_rect) : exact_target_rect;
  draw_canvas_backdrop(painter, target_rect, exposed_rect);

  prepare_vector_preview();
  transform_vector_preview_drawn_ = false;
  if (warp_drag_preview_pending_) {
    // The latest handle position of a warp drag, rendered once per frame however
    // many mouse moves arrived since the last one (set_warp_handle_document_position).
    refresh_warp_preview_cache();
  }

  if (!processing_render_wait_active_) {
    if (async_render_cache_explicit_hold_) {
      // Explicit asynchronous edits already queued a snapshot refresh. Native
      // pointer events continue while this previous frame stays on screen.
    } else if (should_defer_full_refresh_to_async()) {
      // Keep the previous frame on screen while the recomposite runs in the
      // background; the completion lambda swaps the cache and repaints. This is
      // what stops big documents (>= overlay scale) from flashing checkerboard
      // on every full invalidation (add layer, undo, blend change, ...).
      if (async_render_cache_in_flight_) {
        // Only an invalidation that outdated the running snapshot (its
        // generation bumped) needs a second pass. A plain repaint must
        // not mark it pending: the badge animation repaints every 80 ms,
        // so a refresh slower than the badge delay (Undo after a filter
        // on a 16-bit photo) was discarded and restarted at every
        // completion, spinning "Processing..." forever.
        if (async_render_cache_in_flight_generation_ != async_render_cache_generation_) {
          async_render_cache_pending_ = true;
        }
      } else {
        start_async_render_cache_refresh();
      }
    } else if (should_defer_first_render_to_async()) {
      // Deep documents with no previous frame: compose in the background and
      // show the spinner instead of freezing the first paint for seconds.
      if (!async_render_cache_in_flight_) {
        start_async_render_cache_refresh();
      }
      if (!processing_animation_timer_.isActive()) {
        processing_animation_timer_.start(kProcessingAnimationIntervalMs, this);
      }
    } else {
      ensure_render_cache();
    }
  }

  if (tiling_preview_enabled_) {
    // Ghost tiles go under everything: the center tile draws over the 1 px overlap and
    // every overlay stays center-only on top.
    draw_tiling_preview(painter, target_rect, pixel_aligned_view, exposed_rect);
  }

  const bool deep_pixel_renderer = uses_deep_zoom_pixel_renderer(view_zoom());
  const auto draw_scaled_image = [&painter, &target_rect, pixel_aligned_view,
                                  &pixel_aligned_target_rect, this, exposed_rect](const QImage& image) {
    if (!image.isNull()) {
      const QImage& unproofed_image =
          (&image == &render_cache_ && zoom_ < 1.0)
              ? display_image_for_zoom()
              : (&image == &curves_clipping_preview_image_ && zoom_ < 1.0)
                    ? curves_clipping_display_image_for_zoom()
                    : (&image == &move_base_cache_ && zoom_ < 1.0)
                          ? move_base_display_image_for_zoom()
                          : (&image == &transform_base_cache_ && zoom_ < 1.0)
                                ? transform_base_display_image_for_zoom()
                                : (&image == &warp_base_cache_ && zoom_ < 1.0)
                                      ? warp_base_display_image_for_zoom()
                                      : image;
      // The clipping preview is a diagnostic picture of the composite, not artwork.
      const QImage& display_image =
          &image == &curves_clipping_preview_image_ ? unproofed_image : proof_display_image(unproofed_image);
      if (uses_deep_zoom_pixel_renderer(view_zoom())) {
        draw_deep_zoom_image(painter, display_image, exposed_rect);
      } else if (pixel_aligned_view) {
        painter.drawImage(pixel_aligned_target_rect, display_image, display_image.rect());
      } else {
        painter.drawImage(target_rect, display_image, QRectF(display_image.rect()));
      }
    }
  };
  const auto draw_document_patch = [&painter, &target_rect, pixel_aligned_view, &pixel_aligned_target_rect, this,
                                    exposed_rect](const RenderedDocumentPatch& source_patch, bool clear_under_patch) {
    if (source_patch.image.isNull() || source_patch.document_rect.isEmpty()) {
      return;
    }
    // A cheap shallow copy unless the proof view is on.
    const RenderedDocumentPatch patch{source_patch.document_rect, proof_display_image(source_patch.image)};

    const auto patch_target = widget_rect_for_document_rect(QRectF(patch.document_rect));
    const auto patch_exposed = patch_target.toAlignedRect().intersected(exposed_rect);
    if (patch_exposed.isEmpty()) {
      return;
    }

    // Preview-scaled move patches: the image IS the mip for its full-res rect
    // (composited from the scaled document, same source as the scaled base),
    // so it draws directly - no checkerboard wipe (the base excludes the
    // moving layers) and no re-downscale.
    if ((moving_layer_ && !moving_layers_.empty() && move_preview_patches_scale_level_ > 0) ||
        (warping_layer_ && warp_preview_patches_scale_level_ > 0)) {
      painter.drawImage(patch_target, patch.image, QRectF(patch.image.rect()));
      return;
    }

    // The checkerboard is only needed to wipe the previous (stale) backdrop out
    // from under the patch. When the base image already excludes the moving
    // layer there is nothing to wipe, and painting the checkerboard here leaves
    // a faint rectangular seam at the patch edges (a "ghost" of the patch
    // bounds). In that case draw the patch directly over the prepared base.
    if (clear_under_patch) {
      draw_canvas_backdrop(painter, target_rect, patch_exposed);
    }
    if (uses_deep_zoom_pixel_renderer(view_zoom())) {
      draw_deep_zoom_pixels(painter, patch.image, QRect(patch.document_rect.topLeft(), patch.image.size()),
                            patch_exposed);
      return;
    }

    // When the zoomed-out view renders from the display mip chain, drawing the
    // full-resolution patch with plain bilinear scaling resamples with a
    // different phase than the box-filtered mips around it, which makes the
    // artwork inside the patch appear to shift by a pixel or two until the
    // next full repaint. Downscale mip-grid-aligned patches with the same
    // successive halvings and map them through the same mip-space transform so
    // the patch pixels match the surrounding mip render exactly.
    const auto patch_mip_level = display_mip_level_for_zoom(view_zoom());
    if (!pixel_aligned_view && patch_mip_level > 0 && document_ != nullptr) {
      const int block = 1 << patch_mip_level;
      const auto& rect = patch.document_rect;
      const bool aligned = rect.x() % block == 0 && rect.y() % block == 0 &&
                           (rect.width() % block == 0 || rect.x() + rect.width() == document_->width()) &&
                           (rect.height() % block == 0 || rect.y() + rect.height() == document_->height());
      if (aligned) {
        const auto mip = downscaled_to_mip_level(
            patch.image.convertToFormat(QImage::Format_ARGB32_Premultiplied), patch_mip_level);
        if (!mip.isNull()) {
          const double mip_width = mip_dimension(document_->width(), patch_mip_level);
          const double mip_height = mip_dimension(document_->height(), patch_mip_level);
          const QRectF mip_target(
              target_rect.x() + target_rect.width() * ((rect.x() >> patch_mip_level) / mip_width),
              target_rect.y() + target_rect.height() * ((rect.y() >> patch_mip_level) / mip_height),
              target_rect.width() * (mip.width() / mip_width),
              target_rect.height() * (mip.height() / mip_height));
          painter.drawImage(mip_target, mip, QRectF(mip.rect()));
          return;
        }
      }
    }

    if (pixel_aligned_view && document_ != nullptr && document_->width() > 0 && document_->height() > 0) {
      // Placed by the base image's own mapping (pixel_aligned_target_rect), so a
      // patch pixel lands exactly on the base pixel it replaces. Rounding the
      // patch's own float rect (toAlignedRect, or widget_position per corner) put
      // it a pixel off the base under a fractional pan: outward rounding stretched
      // it, and std::round sends a negative half-pixel pan the other way.
      const auto& base = pixel_aligned_target_rect;
      const double scale_x = static_cast<double>(base.width()) / static_cast<double>(document_->width());
      const double scale_y = static_cast<double>(base.height()) / static_cast<double>(document_->height());
      const auto& rect = patch.document_rect;
      painter.drawImage(QRectF(base.x() + rect.x() * scale_x, base.y() + rect.y() * scale_y,
                               rect.width() * scale_x, rect.height() * scale_y),
                        patch.image, QRectF(patch.image.rect()));
    } else {
      painter.drawImage(patch_target, patch.image, QRectF(patch.image.rect()));
    }
  };
  const bool draw_transform_overlay =
      transforming_layer_ && !transform_base_cache_.isNull() &&
      (!transform_source_image_.isNull() || !transform_multi_snapshot_.isNull());
  const bool draw_warp_overlay = warping_layer_ && (!warp_preview_patches_.empty() || !warp_base_cache_.isNull());

  painter.save();
  painter.setClipRect(target_rect);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, uses_smooth_display_scaling(view_zoom(), deep_pixel_renderer));
  if (draw_transform_overlay) {
    // Base excludes the transformed layer; the composited-preview patches
    // (when the layer needs one) draw the transformed result over it, so no
    // checkerboard wipe is needed under them.
    draw_scaled_image(transform_base_cache_);
    for (const auto& patch : transform_preview_patches_) {
      draw_document_patch(patch, false);
    }
    // A shape layer's session shows its transformed path sharp between drags when
    // Dynamic Vector Preview is on (the frame renders the document as committed).
    transform_vector_preview_drawn_ = draw_vector_preview(painter, exposed_rect);
  } else if (draw_warp_overlay) {
    // Base (layer hidden) plus the warped layer's bounded patches: the
    // full-document warp preview composite this replaced was the warp drag's
    // dominant per-move cost.
    draw_scaled_image(warp_base_cache_.isNull() ? render_cache_ : warp_base_cache_);
    for (const auto& patch : warp_preview_patches_) {
      draw_document_patch(patch, warp_base_cache_.isNull());
    }
  } else if (moving_layer_ && !moving_layers_.empty()) {
    const bool proxy_preview =
        move_drag_uses_proxy_preview_ && !move_proxy_image_.isNull() && !move_base_cache_.isNull();
    const bool base_excludes_layer =
        (proxy_preview || !moving_layers_use_outline_preview_) && !move_base_cache_.isNull();
    if (base_excludes_layer) {
      draw_scaled_image(move_base_cache_);
    } else {
      draw_scaled_image(render_cache_);
    }
    if (proxy_preview) {
      // Translated snapshot of the moving subtree. Blend modes against the
      // real backdrop and content clipped at the canvas edge are approximated
      // until release, and at deep-zoom levels the plain blit skips the
      // per-pixel renderer's grid (a heavy latch above 800% zoom is a corner
      // case).
      const auto proxy_target =
          widget_rect_for_document_rect(QRectF(move_proxy_document_rect_.translated(move_preview_delta_)));
      const QImage& proxy_image = proof_display_image(move_proxy_image_);
      // The snapshot holds the moving content without its artboard's clip: clip the blit
      // to the artboard it lands in, as the release will (docs/artboards.md).
      const auto clip = move_proxy_clip_document_rect(move_preview_delta_);
      if (clip.has_value()) {
        painter.save();
        painter.setClipRect(widget_rect_for_document_rect(QRectF(*clip)), Qt::IntersectClip);
      }
      painter.drawImage(proxy_target, proxy_image, QRectF(proxy_image.rect()));
      if (clip.has_value()) {
        painter.restore();
      }
    } else {
      for (const auto& patch : move_preview_patches_) {
        draw_document_patch(patch, !base_excludes_layer);
      }
    }
  } else if (move_commit_job_.has_value() && !move_commit_hold_base_.isNull() &&
             !(deep_pixel_renderer && move_commit_hold_scale_level_ > 0)) {
    // Deferred Move commit: the accurate patches are still rendering, so keep
    // the commit-time preview frame (base plus the moved content at its
    // committed place). Transient by construction: the raw smooth downscale
    // at zoom < 1 is acceptable, and a mip-sized hold skips the deep-zoom
    // per-pixel renderer like the transform hold does.
    draw_scaled_image(move_commit_hold_base_);
    if (!move_commit_hold_proxy_.isNull()) {
      const QImage& hold_proxy = proof_display_image(move_commit_hold_proxy_);
      if (move_commit_hold_proxy_clip_.has_value()) {
        painter.save();
        painter.setClipRect(widget_rect_for_document_rect(QRectF(*move_commit_hold_proxy_clip_)), Qt::IntersectClip);
      }
      painter.drawImage(widget_rect_for_document_rect(QRectF(move_commit_hold_proxy_rect_)), hold_proxy,
                        QRectF(hold_proxy.rect()));
      if (move_commit_hold_proxy_clip_.has_value()) {
        painter.restore();
      }
    } else {
      for (const auto& patch : move_commit_hold_patches_) {
        if (!patch.image.isNull() && !patch.document_rect.isEmpty()) {
          const QImage& hold_patch = proof_display_image(patch.image);
          painter.drawImage(widget_rect_for_document_rect(QRectF(patch.document_rect)), hold_patch,
                            QRectF(hold_patch.rect()));
        }
      }
    }
  } else if (patch_tool_dragging_ && !patch_tool_source_image_.isNull()) {
    // Patch drag: the untouched composite plus a raw translated blit of the
    // frozen snapshot (Photoshop also previews unhealed pixels; the heal is
    // one-shot at release).
    draw_scaled_image(render_cache_);
    draw_patch_tool_drag_preview(painter);
  } else {
    // Committed Free Transform whose render-cache refresh has not landed yet:
    // keep the commit-time preview frame on screen instead of the stale
    // pre-commit cache (which flashed the layer at its old geometry). The
    // processing-operation check covers the synchronous region-patch route,
    // whose event pump can deliver paints while the cache flags still read
    // clean. Transient by construction, so the raw smooth downscale at
    // zoom < 1 (the hold has no mip chain) is acceptable; a mip-sized hold
    // skips the deep-zoom per-pixel renderer, which expects full-res sources.
    const bool commit_hold_engaged =
        !transform_commit_hold_image_.isNull() &&
        (render_cache_dirty_ || async_render_cache_in_flight_ || processing_operation_active()) &&
        !(deep_pixel_renderer && transform_commit_hold_scale_level_ > 0);
    if (commit_hold_engaged) {
      draw_scaled_image(transform_commit_hold_image_);
    } else {
      if (!transform_commit_hold_image_.isNull() && render_settled()) {
        clear_transform_commit_hold();
      }
      draw_scaled_image(curves_clipping_mode_.has_value() && !curves_clipping_preview_image_.isNull()
                            ? curves_clipping_preview_image_
                            : render_cache_);
      transform_vector_preview_drawn_ = draw_vector_preview(painter, exposed_rect) && transforming_layer_;
    }
  }
  if (!curves_clipping_mode_.has_value()) {
    if (channel_view_.group == ChannelViewGroup::Color && channel_view_visible_count(channel_view_) == 2) {
      // Channel visibility (docs/channels.md): two of Red, Green and Blue shown zero the
      // third over the drawn frame, so the cost follows the exposed screen area and every
      // render path (stroke patches, drag previews) is covered.
      painter.save();
      painter.setCompositionMode(QPainter::CompositionMode_Multiply);
      const auto shown = [this](int index) {
        return channel_view_shows(channel_view_, ChannelViewGroup::Color, index) ? 255 : 0;
      };
      painter.fillRect(target_rect.intersected(QRectF(exposed_rect)), QColor(shown(0), shown(1), shown(2)));
      painter.restore();
    }
    draw_mask_display_overlay(painter, target_rect, pixel_aligned_view, pixel_aligned_target_rect);
  }
  painter.restore();

  const auto draw_layer_outline = [&](const QRect& outline_rect) {
    if (!outline_rect.isEmpty()) {
      draw_move_layer_outline(painter, widget_position(outline_rect.topLeft()),
                              widget_position(outline_rect.bottomRight() + QPoint(1, 1)));
    }
  };
  if (moving_layer_ && !moving_layers_.empty() && !draw_transform_overlay) {
    for (const auto& moving_layer : moving_layers_) {
      draw_layer_outline(moving_layer_outline_rect(moving_layer, move_preview_delta_));
    }
  }
  if (tool_ == CanvasTool::Move && !moving_layer_ && !draw_transform_overlay && move_hover_outline_rect_.has_value() &&
      extra_shown(ViewExtra::LayerOutlines)) {
    draw_layer_outline(*move_hover_outline_rect_);
  }
  // A selection rectangle outlines what releasing it would select, with the
  // hover outline (GitHub issue 171).
  if (tool_ == CanvasTool::Move && move_layer_selection_gesture_.has_value()) {
    const auto& gesture = *move_layer_selection_gesture_;
    for (const auto id : gesture.preview_ids) {
      if (const auto found = gesture.target_outlines.find(id); found != gesture.target_outlines.end()) {
        draw_layer_outline(found->second);
      }
    }
  }
  if (transforming_layer_) {
    draw_free_transform(painter);
  } else if (warping_layer_) {
    draw_warp_transform(painter);
  } else {
    draw_move_transform_controls(painter);
  }
  draw_grid_overlay(painter, target_rect, exposed_rect);
  draw_guides_overlay(painter);
  draw_artboard_overlay(painter);
  draw_move_snap_guides(painter);
  // With artboards the canvas has no fixed size of its own, so only the artboards are
  // outlined (GitHub issue 278).
  const auto border_rect = target_rect.adjusted(0.5, 0.5, -0.5, -0.5);
  if (!border_rect.isEmpty() && (document_ == nullptr || !document_has_artboards(std::as_const(*document_)))) {
    painter.setPen(theme().canvas_document_border);
    painter.drawRect(border_rect);
  }
  draw_selection_overlay(painter);
  draw_marquee_resize_handles(painter);
  draw_patch_tool_drag_outline(painter);
  draw_quick_select_stroke_overlay(painter);
  draw_object_select_overlay(painter);
  draw_spot_heal_stroke_overlay(painter);
  draw_red_eye_overlay(painter);
  draw_pen_overlay(painter);
  draw_path_edit_overlay(painter);
  draw_shape_preview(painter, exposed_rect);
  draw_crop_overlay(painter);
  draw_perspective_crop_overlay(painter);
  draw_move_layer_selection(painter);
  draw_drag_size_readout(painter);
  draw_transform_drag_readout(painter);
  draw_text_rect_preview(painter);
  draw_clone_source_overlay(painter);
  if ((tool_ == CanvasTool::Clone || tool_ == CanvasTool::Healing) && painting_ &&
      retouch_source_indicator_.has_value()) {
    // While painting, a small crosshair follows the point the stroke samples (GitHub
    // issue 380), in the marker's dark halo and light ink.
    const auto center = widget_position_f(QPointF(*retouch_source_indicator_) + QPointF(0.5, 0.5));
    constexpr double kArm = 7.0;
    constexpr double kGap = 2.0;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    for (const auto& pen : {QPen(QColor(20, 23, 28, 200), 3.0), QPen(QColor(245, 248, 252, 235), 1.4)}) {
      painter.setPen(pen);
      painter.drawLine(QPointF(center.x() - kArm, center.y()), QPointF(center.x() - kGap, center.y()));
      painter.drawLine(QPointF(center.x() + kGap, center.y()), QPointF(center.x() + kArm, center.y()));
      painter.drawLine(QPointF(center.x(), center.y() - kArm), QPointF(center.x(), center.y() - kGap));
      painter.drawLine(QPointF(center.x(), center.y() + kGap), QPointF(center.x(), center.y() + kArm));
    }
    painter.restore();
  } else if ((tool_ == CanvasTool::Clone || tool_ == CanvasTool::Healing) && clone_source_set_) {
    const auto center = widget_position_f(QPointF(clone_source_point_) + QPointF(0.5, 0.5));
    constexpr double kRadius = 7.0;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(Qt::NoBrush);
    // Draw a dark halo first, then a bright marker, so the clone source stays
    // legible over either light or dark artwork.
    for (const auto& pen : {QPen(QColor(20, 23, 28, 200), 3.0), QPen(QColor(245, 248, 252, 235), 1.4)}) {
      painter.setPen(pen);
      painter.drawEllipse(center, kRadius, kRadius);
      painter.drawLine(QPointF(center.x() - kRadius - 3.0, center.y()), QPointF(center.x() - 2.0, center.y()));
      painter.drawLine(QPointF(center.x() + 2.0, center.y()), QPointF(center.x() + kRadius + 3.0, center.y()));
      painter.drawLine(QPointF(center.x(), center.y() - kRadius - 3.0), QPointF(center.x(), center.y() - 2.0));
      painter.drawLine(QPointF(center.x(), center.y() + 2.0), QPointF(center.x(), center.y() + kRadius + 3.0));
    }
    painter.restore();
  }
  draw_preview_point_handles(painter);
  draw_zoom_preview(painter);
  draw_rulers(painter);
  draw_brush_hover_outline(painter);
  draw_eyedropper_sample_outline(painter);
  draw_sample_loupe(painter);
  draw_stroke_leash_overlay(painter);
  draw_brush_adjust_overlay(painter);
  draw_processing_overlay(painter);
  if (vertical_scroll_bar_ != nullptr && vertical_scroll_bar_->isVisible() &&
      horizontal_scroll_bar_ != nullptr && horizontal_scroll_bar_->isVisible()) {
    // The corner square between the bars reads as scroll chrome, like Photoshop.
    painter.fillRect(QRect(width() - vertical_scroll_bar_->width(), height() - horizontal_scroll_bar_->height(),
                           vertical_scroll_bar_->width(), horizontal_scroll_bar_->height()),
                     theme().canvas_scrollbar_track);
  }
}

QImage CanvasWidget::render_document_image() const {
  return qimage_from_document(*document_, true).convertToFormat(QImage::Format_RGBA8888);
}

void CanvasWidget::ensure_render_cache() {
  ZoomTraceScope trace("ensure_render_cache", zoom_);
  if (document_ == nullptr) {
    return;
  }
  if (!render_cache_dirty_ && render_cache_.size() == QSize(document_->width(), document_->height())) {
    return;
  }

  cancel_async_render_cache_refresh();
  render_cache_ = render_document_image_with_processing();
  quantize_image_for_palette_display(render_cache_);
  render_cache_dirty_ = false;
  ++render_cache_diagnostics_.full_refreshes;
  invalidate_display_mip_cache();
  refresh_curves_clipping_preview();
  if (CanvasWidget::is_component_preview(layer_edit_target_)) {
    mask_display_image_ = QImage();
  }
}

QImage CanvasWidget::render_document_image_with_processing(const std::vector<LayerId>& hidden_layer_ids) {
  if (document_ == nullptr) {
    return {};
  }
  const auto render = [](const Document& document, const std::vector<LayerId>& hidden) {
    const auto image = hidden.empty() ? qimage_from_document(document, true)
                                      : qimage_from_document_rect_with_hidden_layers(
                                            document, QRect(0, 0, document.width(), document.height()), true, hidden);
    return image.convertToFormat(QImage::Format_RGBA8888);
  };
  const auto canvas_area = static_cast<std::int64_t>(document_->width()) *
                           static_cast<std::int64_t>(document_->height());
  if (!processing_operation_active() && canvas_area < processing_overlay_dirty_area_threshold()) {
    return hidden_layer_ids.empty() ? render_document_image() : render(*document_, hidden_layer_ids);
  }

#if defined(Q_OS_WASM) && defined(__EMSCRIPTEN_PTHREADS__)
  // paintEvent reaches here for the synchronous first paint of a pixel-huge
  // document. wait_for_processing_operation waits in a nested event loop on
  // wasm, which is not safe inside a paint, so render inline instead: the
  // main-thread strip fan-out is clamped to the idle pthread pool
  // (max_blocking_fanout_workers), so this blocks the tab but cannot wedge.
  // The same inline path serves a dry pool, where launch_async itself would
  // need a lazy Worker spawn this blocked path cannot rely on.
  const auto idle_pool = patchy::idle_prespawned_pool_workers();
  if (paintingActive() || idle_pool < 1) {
    return render(*document_, hidden_layer_ids);
  }
  // Keep the awaited compute's strip fan-out inside the pre-spawned pool:
  // one worker for the compute, two of race margin (see worker_budget.hpp).
  const patchy::BlockingFanoutBudgetScope fanout_budget(std::max(0, idle_pool - 3));
#endif

  auto* document = document_;
  auto future = launch_async([document, hidden = hidden_layer_ids, render] {
    if (const auto delay = processing_render_test_delay_ms(); delay > 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    }
    return render(*document, hidden);
  });
  const auto overlay_shown = wait_for_processing_operation(
      [&future] { return future.wait_for(std::chrono::milliseconds(16)) == std::future_status::ready; }, true);
  try {
    auto image = future.get();
    if (overlay_shown) {
      hide_processing_overlay();
    }
    return image;
  } catch (...) {
    if (overlay_shown) {
      hide_processing_overlay();
    }
    throw;
  }
}

void CanvasWidget::start_async_render_cache_refresh() {
  if (document_ == nullptr) {
    return;
  }
  auto document_snapshot = std::make_shared<Document>(*document_);
  const QSize snapshot_size(document_snapshot->width(), document_snapshot->height());
  async_render_cache_in_flight_ = true;
  async_render_cache_pending_ = false;
  note_background_refresh_state();
  const auto generation = ++async_render_cache_generation_;
  async_render_cache_in_flight_generation_ = generation;
  auto* app = QApplication::instance();
  QPointer<CanvasWidget> widget(this);
  run_tracked_background_worker([app, widget, generation, snapshot_size, document_snapshot = std::move(document_snapshot)] {
    auto image = std::make_shared<QImage>();
    try {
      if (const auto delay = processing_render_test_delay_ms(); delay > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(delay));
      }
      *image = qimage_from_document(*document_snapshot, true).convertToFormat(QImage::Format_RGBA8888);
    } catch (...) {
      image.reset();
    }
    if (app == nullptr) {
      return;
    }
    QMetaObject::invokeMethod(
        app,
        [widget, generation, snapshot_size, image = std::move(image)]() mutable {
          if (widget == nullptr) {
            return;
          }
          const auto has_pending = widget->async_render_cache_pending_;
          widget->async_render_cache_in_flight_ = false;
          widget->async_render_cache_pending_ = false;
          if (has_pending) {
            widget->start_async_render_cache_refresh();
            return;
          }
          widget->note_background_refresh_state();
          if (generation != widget->async_render_cache_generation_ || image == nullptr || image->isNull() ||
              widget->document_ == nullptr ||
              snapshot_size != QSize(widget->document_->width(), widget->document_->height())) {
            if (generation == widget->async_render_cache_generation_) {
              widget->async_render_cache_explicit_hold_ = false;
              widget->update();
            }
            return;
          }
          // Installing here is content-correct even mid-drag or mid-wait:
          // every mutation path either bumps the generation or sets pending
          // (the move precommit-patch commit), so an uncancelled, non-pending
          // completion always matches the current document. The retained move
          // base/proxy derive from that same content, so this must NOT call
          // invalidate_retained_move_caches().
          widget->quantize_image_for_palette_display(*image);
          widget->render_cache_ = std::move(*image);
          widget->render_cache_dirty_ = false;
          widget->async_render_cache_explicit_hold_ = false;
          ++widget->render_cache_diagnostics_.full_refreshes;
          widget->invalidate_display_mip_cache();
          widget->refresh_curves_clipping_preview();
          if (CanvasWidget::is_component_preview(widget->layer_edit_target_)) {
            widget->mask_display_image_ = QImage();
          }
          if (widget->isVisible()) {
            widget->update();
          }
        },
        Qt::QueuedConnection);
  });
}

void CanvasWidget::cancel_async_render_cache_refresh() noexcept {
  invalidate_vector_preview();
  ++async_render_cache_generation_;
  async_render_cache_pending_ = false;
  async_render_cache_explicit_hold_ = false;
}

std::vector<RenderedDocumentPatch> CanvasWidget::render_document_patches_with_processing(
    const QRegion& document_region, const std::vector<std::pair<LayerId, Rect>>& layer_bounds,
    bool force_processing_wait) {
  if (document_ == nullptr) {
    return {};
  }
  const auto use_processing_wait = force_processing_wait || processing_operation_active() ||
                                   dirty_region_should_use_processing_wait(document_region);
  if (!use_processing_wait) {
    return layer_bounds.empty()
               ? qimage_patches_from_document_region(*document_, document_region, true)
               : qimage_patches_from_document_region_with_layer_bounds(*document_, document_region, true, layer_bounds);
  }

#if defined(Q_OS_WASM) && defined(__EMSCRIPTEN_PTHREADS__)
  // Same guards as render_document_image_with_processing: inline inside a
  // paint (the wasm wait nests an event loop) and inline on a dry pool
  // (launch_async would need a lazy Worker spawn); otherwise budget the
  // awaited compute's fan-outs to the pre-spawned pool.
  const auto idle_pool = patchy::idle_prespawned_pool_workers();
  if (paintingActive() || idle_pool < 1) {
    return layer_bounds.empty()
               ? qimage_patches_from_document_region(*document_, document_region, true)
               : qimage_patches_from_document_region_with_layer_bounds(*document_, document_region, true, layer_bounds);
  }
  const patchy::BlockingFanoutBudgetScope fanout_budget(std::max(0, idle_pool - 3));
#endif

  auto* document = document_;
  auto region = document_region;
  auto bounds = layer_bounds;
  auto future = launch_async([document, region = std::move(region), bounds = std::move(bounds)] {
    if (const auto delay = processing_render_test_delay_ms(); delay > 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    }
    return bounds.empty()
               ? qimage_patches_from_document_region(*document, region, true)
               : qimage_patches_from_document_region_with_layer_bounds(*document, region, true, bounds);
  });
  const auto overlay_shown = wait_for_processing_operation(
      [&future] { return future.wait_for(std::chrono::milliseconds(16)) == std::future_status::ready; }, true);
  try {
    auto patches = future.get();
    if (overlay_shown) {
      hide_processing_overlay();
    }
    return patches;
  } catch (...) {
    if (overlay_shown) {
      hide_processing_overlay();
    }
    throw;
  }
}

bool CanvasWidget::wait_for_processing_operation(std::function<bool()> operation_ready, bool allow_overlay) {
  ++render_cache_diagnostics_.awaited_renders;
  const auto previous_wait_active = processing_render_wait_active_;
  processing_render_wait_active_ = true;
  const auto start_temporary_operation = allow_overlay && !processing_operation_active();
  if (start_temporary_operation) {
    begin_processing_operation();
  }
#if defined(Q_OS_WASM) && defined(__EMSCRIPTEN_PTHREADS__)
  // The desktop loop below would pin the browser main thread outside the
  // event loop (operation_ready blocks in future.wait_for, and the wasm tick
  // deliberately never pumps). While it spins, Emscripten cannot finish a
  // lazily spawned pthread: the new Worker's load handshake arrives on the
  // main thread's JS event loop, which never runs. Any worker compute that
  // fans out past the idle pre-spawned pool (the free-transform release
  // renders a 7 Mpx layer with two 16-way fan-outs) then blocks forever on
  // threads that never start, and the wait spins forever: the tab hard
  // freezes. Waiting in a nested event loop instead (the same shape
  // run_filter_compute_with_progress uses) suspends the main thread via
  // Asyncify, so spawns complete and the processing overlay actually paints.
  // Callers reachable from paintEvent must not get here (a nested exec inside
  // a paint is not safe); they render inline when paintingActive().
  if (!operation_ready()) {
    QEventLoop wait_loop;
    QTimer poll_timer;
    // operation_ready blocks for up to 16 ms per call, so a 16 ms interval
    // would put the main thread at ~100% duty and starve the very event-loop
    // turns this wait exists to provide (worker 'loaded' handshakes and
    // 'cleanupThread' pool returns are plain JS tasks). 100 ms keeps the tab
    // responsive; the added completion latency is noise against renders that
    // take this path at all.
    poll_timer.setInterval(100);
    QObject::connect(&poll_timer, &QTimer::timeout, &wait_loop, [&] {
      if (allow_overlay) {
        tick_processing_operation();
      }
      if (operation_ready()) {
        wait_loop.quit();
      }
    });
    poll_timer.start();
    wait_loop.exec(QEventLoop::ExcludeUserInputEvents);
  }
#else
  while (!operation_ready()) {
    if (allow_overlay) {
      tick_processing_operation();
    } else {
      QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 16);
    }
  }
#endif
  if (start_temporary_operation) {
    end_processing_operation();
  }
  processing_render_wait_active_ = previous_wait_active;
#ifdef Q_OS_WASM
  if (!processing_render_wait_active_) {
    // Input that arrived while this wait's compute kept the main thread busy
    // was only queued by the platform plugin, so its browser defaults ran
    // un-prevented; a press default blurs Qt's DOM focus helper and every
    // hotkey dies until DOM focus returns (docs/wasm-input.md). Heal when the
    // outermost wait unwinds; no-op unless focus actually fell to the body.
    wasm_files::restore_qt_dom_focus();
  }
#endif
  if (!processing_render_wait_active_ && deferred_wait_release_.has_value()) {
    // Replay the release parked by mouseReleaseEvent on the next main-loop
    // turn, after the input handler that started this wait has fully
    // unwound. A stray release with no live gesture no-ops in every branch,
    // so replaying is always safe; if another wait is active by then, the
    // guard simply parks it again.
    QTimer::singleShot(0, this, [this] {
      if (!deferred_wait_release_.has_value()) {
        return;
      }
      const auto release = *deferred_wait_release_;
      deferred_wait_release_.reset();
      QMouseEvent replay(QEvent::MouseButtonRelease, release.position, release.position,
                         release.global_position, release.button, release.buttons, release.modifiers);
      QApplication::sendEvent(this, &replay);
    });
  }
  return start_temporary_operation;
}

bool CanvasWidget::dirty_region_should_use_processing_wait(const QRegion& document_region) const noexcept {
  if (document_ == nullptr || document_region.isEmpty()) {
    return false;
  }
  return region_area(document_region) >= processing_overlay_dirty_area_threshold();
}

void CanvasWidget::refresh_render_cache_rect(QRect document_rect) {
  refresh_render_cache_region(QRegion(document_rect));
}

void CanvasWidget::refresh_render_cache_region(const QRegion& document_region) {
  if (document_ == nullptr || render_cache_.isNull()) {
    return;
  }

  const auto clipped = document_region.intersected(QRect(0, 0, document_->width(), document_->height()));
  if (clipped.isEmpty()) {
    return;
  }

  auto patches = render_document_patches_with_processing(clipped, {}, false);
  for (auto& patch : patches) {
    patch.image = patch.image.convertToFormat(QImage::Format_RGBA8888);
  }
  if (patch_render_cache_patches(patches)) {
    ++render_cache_diagnostics_.dirty_region_batches;
    render_cache_diagnostics_.dirty_region_rects += static_cast<int>(patches.size());
    render_cache_diagnostics_.dirty_region_pixels += static_cast<std::uint64_t>(region_area(clipped));
  } else if (!patches.empty()) {
    // Unreachable by construction (the region is pre-clipped and the patches
    // render at exactly those rects), but if validation ever rejects the set,
    // leave the cache marked dirty so the next paint re-renders instead of
    // showing a stale region as settled.
    render_cache_dirty_ = true;
  }
}

bool CanvasWidget::patch_render_cache_rect(QRect document_rect, const QImage& partial) {
  return patch_render_cache_patches({RenderedDocumentPatch{document_rect, partial}});
}

bool CanvasWidget::patch_render_cache_patches(const std::vector<RenderedDocumentPatch>& patches) {
  if (document_ == nullptr || render_cache_.isNull()) {
    return false;
  }

  if (patches.empty()) {
    return false;
  }

  // Validate every patch before painting any: a skipped patch used to leave
  // its rect permanently stale while the cache was still marked clean.
  // Failing closed hands the whole region to the caller's full-invalidation
  // fallback instead.
  const QRect canvas_rect(0, 0, document_->width(), document_->height());
  for (const auto& patch : patches) {
    const auto document_rect = patch.document_rect.intersected(canvas_rect);
    if (patch.image.isNull() || document_rect.isEmpty() ||
        patch.image.size() != patch.document_rect.size() || document_rect != patch.document_rect) {
      return false;
    }
  }

  const auto palette_display = document_->palette_editing().has_value();
  // A proofed copy of the full composite is patched alongside it and re-keyed, so a brush
  // stroke under the proof view costs its own rect, not the whole document.
  const auto proofed_composite =
      std::find_if(proofed_images_.begin(), proofed_images_.end(),
                   [key = render_cache_.cacheKey()](const ProofedImage& entry) { return entry.source_key == key; });
  const bool patch_proofed_composite = proof_display_transform_ != nullptr &&
                                       proofed_composite != proofed_images_.end() &&
                                       proofed_composite->image.size() == render_cache_.size();
  {
    QPainter painter(&render_cache_);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    std::optional<QPainter> proofed_painter;
    if (patch_proofed_composite) {
      proofed_painter.emplace(&proofed_composite->image);
      proofed_painter->setCompositionMode(QPainter::CompositionMode_Source);
    }
    for (const auto& patch : patches) {
      QImage shown = patch.image;
      if (palette_display) {
        quantize_image_for_palette_display(shown);
      }
      painter.drawImage(patch.document_rect.topLeft(), shown);
      if (proofed_painter.has_value()) {
        QImage proofed = shown.convertToFormat(QImage::Format_RGBA8888);
        for (int y = 0; y < proofed.height(); ++y) {
          proof_display_transform_->apply(proofed.scanLine(y), static_cast<std::size_t>(proofed.width()));
        }
        proofed_painter->drawImage(patch.document_rect.topLeft(), proofed);
      }
    }
  }
  if (patch_proofed_composite) {
    proofed_composite->source_key = render_cache_.cacheKey();
  }
  render_cache_dirty_ = false;
  render_cache_diagnostics_.partial_patches += static_cast<int>(patches.size());
  patch_display_mip_cache(patches);
  refresh_curves_clipping_preview();
  return true;
}

void CanvasWidget::patch_display_mip_cache(const std::vector<RenderedDocumentPatch>& patches) {
  // Rebuilding the chain here cost the whole composite on the next zoomed-out
  // paint: once per brush frame (GitHub issue 165). Each level instead
  // recomputes only the blocks under the patches, bit-identical to a rebuild.
  // PATCHY_DISPLAY_MIP_PATCH=0 restores the rebuild for diagnosis.
  static const bool patching_disabled = qEnvironmentVariableIntValue("PATCHY_DISPLAY_MIP_PATCH") == 0 &&
                                        qEnvironmentVariableIsSet("PATCHY_DISPLAY_MIP_PATCH");
  if (patching_disabled || display_mip_cache_.empty() || display_mip_source_size_ != render_cache_.size()) {
    invalidate_display_mip_cache();
    return;
  }
  tiling_tile_pixmap_ = QPixmap();
  tiling_tile_pixmap_size_ = QSize();
  for (const auto& patch : patches) {
    auto rect = patch.document_rect.intersected(render_cache_.rect());
    const QImage* source = &render_cache_;
    for (auto& level : display_mip_cache_) {
      rect = halved_display_mip_rect(rect);
      if (rect.isEmpty()) {
        break;
      }
      refresh_halved_display_mip(*source, level, rect);
      source = &level;
    }
  }
}

QImage CanvasWidget::current_composite() const {
  if (document_ == nullptr || render_cache_dirty_ || render_cache_.isNull() ||
      render_cache_.size() != QSize(document_->width(), document_->height())) {
    return {};
  }
  return render_cache_;
}

int CanvasWidget::display_mip_levels_for_testing() const noexcept {
  return static_cast<int>(display_mip_cache_.size());
}

bool CanvasWidget::display_mips_match_rebuild_for_testing() const {
  const QImage* previous = &render_cache_;
  QImage rebuilt;
  for (const auto& mip : display_mip_cache_) {
    rebuilt = halved_display_mip(*previous);
    if (rebuilt != mip) {
      return false;
    }
    previous = &mip;
  }
  return true;
}

void CanvasWidget::invalidate_display_mip_cache() noexcept {
  display_mip_cache_.clear();
  display_mip_source_size_ = QSize();
  // The tiling-preview tile pixmap derives from the same composite, and every
  // render_cache_ content change funnels through this invalidation.
  tiling_tile_pixmap_ = QPixmap();
  tiling_tile_pixmap_size_ = QSize();
}

void CanvasWidget::refresh_curves_clipping_preview() {
  curves_clipping_display_mip_cache_.clear();
  curves_clipping_display_mip_source_key_ = 0;
  if (!curves_clipping_mode_.has_value() || render_cache_.isNull()) {
    curves_clipping_preview_image_ = QImage();
    return;
  }
  curves_clipping_preview_image_ = render_curves_clipping_preview(
      render_cache_, *curves_clipping_mode_, curves_clipping_channel_);
}

void CanvasWidget::ensure_move_base_cache() {
  if (!move_base_cache_.isNull() || document_ == nullptr || moving_layers_.empty()) {
    return;
  }

  // Hide via render overrides, not set_visible toggles: the toggles bumped
  // every moving layer's revision each drag, cold-invalidating the style-mask
  // and alpha-bounds caches (mirrors rebuild_transform_base_cache).
  std::vector<LayerId> hidden;
  hidden.reserve(moving_layers_.size());
  for (const auto& moving_layer : moving_layers_) {
    hidden.push_back(moving_layer.id);
  }

  const QRect canvas_rect(0, 0, document_->width(), document_->height());
  // Display-resolution compositing: when zoomed out, build the base from the
  // preview-scaled document at the display mip level - the whole scaled base
  // costs 4^level less than the full-res canvas.
  if (const auto composite_level = preview_composite_level_for_zoom(view_zoom()); composite_level >= 1) {
    if (auto* scaled_document = preview_scaled_document_for_level(composite_level)) {
      const QRect scaled_canvas(0, 0, scaled_document->width(), scaled_document->height());
      auto base = qimage_from_document_rect_with_hidden_layers_banded(*scaled_document, scaled_canvas, true, hidden)
                      .convertToFormat(QImage::Format_RGBA8888);
      if (!base.isNull()) {
        move_base_cache_ = std::move(base);
        move_base_cache_scale_level_ = composite_level;
        return;
      }
    }
  }
  move_base_cache_scale_level_ = 0;
  // The full-res base patches the render cache, which must be exact first.
  wait_for_move_commit_job();
  // Recompositing the whole document (with the moving layers hidden) is very
  // slow on heavy PSDs and caused a multi-second hitch at the start of a drag.
  // Instead reuse the already-composited render cache and only re-render the
  // small region the moving layers currently occupy with them hidden - the
  // moving layers only contribute within that region, so the rest of the cache
  // is already correct.
  // The banded variant spreads these preview-only renders across workers: the
  // hole rects sit far below the 4 Mpx strip gate but can cross an expensive
  // styled stack, and this build is most of the latch hitch when a heavy drag
  // switches to the proxy.
  if (render_cache_dirty_ || render_cache_.isNull() || render_cache_.size() != canvas_rect.size()) {
    move_base_cache_ = qimage_from_document_rect_with_hidden_layers_banded(*document_, canvas_rect, true, hidden)
                           .convertToFormat(QImage::Format_RGBA8888);
    return;
  }

  QRegion old_region;
  for (const auto& moving_layer : moving_layers_) {
    const auto* layer = std::as_const(*document_).find_layer(moving_layer.id);
    if (layer == nullptr) {
      continue;
    }
    const auto rect = moving_layer_effect_rect(*layer, moving_layer, QPoint()).intersected(canvas_rect);
    if (!rect.isEmpty()) {
      old_region += rect;
    }
  }

  QImage base = render_cache_.convertToFormat(QImage::Format_ARGB32_Premultiplied);
  if (!old_region.isEmpty()) {
    QPainter painter(&base);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    for (const auto& rect : old_region) {
      const auto cleared = qimage_from_document_rect_with_hidden_layers_banded(*document_, rect, true, hidden);
      if (cleared.isNull()) {
        continue;
      }
      painter.drawImage(rect.topLeft(), cleared.convertToFormat(QImage::Format_ARGB32_Premultiplied));
    }
    painter.end();
  }
  move_base_cache_ = std::move(base);
}

bool CanvasWidget::can_hold_move_commit_preview(QPoint commit_delta) const noexcept {
  if constexpr (kBackgroundWorkRunsInline) {
    return false;  // no worker to defer to
  }
  if (move_base_cache_.isNull() || moving_layers_use_outline_preview_) {
    return false;
  }
  if (move_drag_uses_proxy_preview_) {
    // A canvas-clipped proxy (the usual case for a big styled layer) is missing
    // the content that enters from off-canvas; the hold shows a transient gap
    // there until the patches land, which beats freezing for the render.
    return !move_proxy_image_.isNull();
  }
  return !move_preview_patches_.empty() && move_preview_patches_rendered_delta_.has_value() &&
         *move_preview_patches_rendered_delta_ == commit_delta;
}

void CanvasWidget::arm_move_commit_hold(QPoint commit_delta) {
  move_commit_hold_base_ = move_base_cache_;
  move_commit_hold_scale_level_ = move_base_cache_scale_level_;
  if (move_drag_uses_proxy_preview_ && !move_proxy_image_.isNull()) {
    move_commit_hold_proxy_ = move_proxy_image_;
    move_commit_hold_proxy_rect_ = move_proxy_document_rect_.translated(commit_delta);
    move_commit_hold_proxy_clip_ = move_proxy_clip_document_rect(commit_delta);
    move_commit_hold_patches_.clear();
  } else {
    move_commit_hold_proxy_ = QImage();
    move_commit_hold_proxy_rect_ = QRect();
    move_commit_hold_proxy_clip_.reset();
    move_commit_hold_patches_ = move_preview_patches_;
  }
}

void CanvasWidget::clear_move_commit_hold() noexcept {
  move_commit_hold_base_ = QImage();
  move_commit_hold_scale_level_ = 0;
  move_commit_hold_proxy_ = QImage();
  move_commit_hold_proxy_rect_ = QRect();
  move_commit_hold_proxy_clip_.reset();
  move_commit_hold_patches_.clear();
}

void CanvasWidget::start_move_commit_job(const QRegion& document_region) {
  if (document_ == nullptr) {
    return;
  }
  auto region = document_region.intersected(QRect(0, 0, document_->width(), document_->height()));
  if (move_commit_job_.has_value()) {
    // One job over both regions: the earlier result is stale now.
    region += move_commit_job_->region;
  }
  if (region.isEmpty()) {
    cancel_move_commit_job();
    return;
  }
  const auto generation = ++move_commit_job_generation_;
  auto snapshot = std::make_shared<const Document>(*document_);
  auto promise = std::make_shared<std::promise<std::vector<RenderedDocumentPatch>>>();
  MoveCommitJob job;
  job.generation = generation;
  job.region = region;
  job.result = promise->get_future().share();
  move_commit_job_ = std::move(job);
  ++render_cache_diagnostics_.move_deferred_commits;
  note_background_refresh_state();
  if (!move_commit_worker_) {
    move_commit_worker_ = std::make_shared<MoveCommitWorker>();
  }
  const auto state = move_commit_worker_;
  {
    std::lock_guard lock(state->mutex);
    if (state->current_cancel) {
      state->current_cancel->store(true);
    }
    if (state->pending) {
      state->pending->promise->set_value({});
    }
    state->pending = MoveCommitWorker::Request{
        generation, std::move(snapshot), region, promise, std::make_shared<std::atomic_bool>(false)};
    if (state->running) {
      return;
    }
    state->running = true;
  }
  auto* app = QApplication::instance();
  QPointer<CanvasWidget> widget(this);
  run_tracked_background_worker([app, widget, state] {
    for (;;) {
      MoveCommitWorker::Request request;
      {
        std::lock_guard lock(state->mutex);
        if (!state->pending) {
          state->running = false;
          state->current_cancel.reset();
          return;
        }
        request = std::move(*state->pending);
        state->pending.reset();
        state->current_cancel = request.cancelled;
      }
      std::vector<RenderedDocumentPatch> patches;
      try {
        if (const auto delay = processing_render_test_delay_ms(); delay > 0) {
          std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        }
        // Cancellation is checked between regions; an already running render
        // finishes safely using its immutable snapshot.
        for (const auto& rect : request.region) {
          if (request.cancelled->load()) break;
          auto rendered = qimage_patches_from_document_region(*request.snapshot, QRegion(rect), true);
          for (auto& patch : rendered) {
            patch.image = patch.image.convertToFormat(QImage::Format_RGBA8888);
            patches.push_back(std::move(patch));
          }
        }
      } catch (...) {
        patches.clear();
      }
      request.promise->set_value(std::move(patches));
      if (app != nullptr) {
        QMetaObject::invokeMethod(app, [widget, completed = request.generation] {
          if (widget != nullptr) widget->finish_move_commit_job(completed);
        }, Qt::QueuedConnection);
      }
    }
  });
}

void CanvasWidget::finish_move_commit_job(std::uint64_t generation) {
  if (!move_commit_job_.has_value() || move_commit_job_->generation != generation) {
    return;  // cancelled or superseded; a later completion owns the cache
  }
  auto job = std::move(*move_commit_job_);
  move_commit_job_.reset();
  clear_move_commit_hold();
  note_background_refresh_state();
  std::vector<RenderedDocumentPatch> patches;
  try {
    patches = job.result.get();
  } catch (...) {
    patches.clear();
  }
  const bool landed = !patches.empty() && !render_cache_dirty_ && document_ != nullptr && !render_cache_.isNull() &&
                      render_cache_.size() == QSize(document_->width(), document_->height()) &&
                      patch_render_cache_patches(patches);
  if (landed) {
    ++render_cache_diagnostics_.move_precommit_patches;
  } else {
    // The region is stale either way; a normal refresh repairs it.
    render_cache_dirty_ = true;
    invalidate_display_mip_cache();
  }
  if (isVisible()) {
    update();
  }
}

void CanvasWidget::cancel_move_commit_job() noexcept {
  if (move_commit_worker_) {
    std::lock_guard lock(move_commit_worker_->mutex);
    if (move_commit_worker_->current_cancel) {
      move_commit_worker_->current_cancel->store(true);
    }
    if (move_commit_worker_->pending) {
      move_commit_worker_->pending->promise->set_value({});
      move_commit_worker_->pending.reset();
    }
  }
  // The queued completion finds no job with its generation and does nothing.
  move_commit_job_.reset();
  clear_move_commit_hold();
  note_background_refresh_state();
}

void CanvasWidget::wait_for_move_commit_job() {
  while (move_commit_job_.has_value()) {
    const auto generation = move_commit_job_->generation;
    auto result = move_commit_job_->result;
    wait_for_processing_operation(
        [&result] { return result.wait_for(std::chrono::milliseconds(16)) == std::future_status::ready; }, true);
    finish_move_commit_job(generation);
  }
}

void CanvasWidget::clear_move_base_cache() noexcept {
  move_base_cache_ = QImage();
  move_base_cache_scale_level_ = 0;
  move_base_display_mip_cache_.clear();
  move_base_display_mip_source_key_ = 0;
}

const QImage& CanvasWidget::display_image_for_zoom() {
  ZoomTraceScope trace("display_mips", zoom_);
  if (render_cache_.isNull() || zoom_ >= 1.0) {
    return render_cache_;
  }

  if (display_mip_cache_.empty() || display_mip_source_size_ != render_cache_.size()) {
    display_mip_cache_.clear();
    display_mip_source_size_ = render_cache_.size();
  }

  const auto target_level = display_mip_level_for_zoom(view_zoom());
  if (target_level <= 0) {
    return render_cache_;
  }

  if (display_mip_cache_.empty()) {
    ++render_cache_diagnostics_.display_mip_rebuilds;
  }
  while (static_cast<int>(display_mip_cache_.size()) < target_level) {
    const auto& previous = display_mip_cache_.empty() ? render_cache_ : display_mip_cache_.back();
    const QSize next_size(std::max(1, (previous.width() + 1) / 2),
                          std::max(1, (previous.height() + 1) / 2));
    if (next_size == previous.size()) {
      break;
    }
    display_mip_cache_.push_back(
        halved_display_mip(previous));
  }

  const auto level = std::min<int>(target_level, static_cast<int>(display_mip_cache_.size()));
  return level <= 0 ? render_cache_ : display_mip_cache_[level - 1];
}

const QImage& CanvasWidget::curves_clipping_display_image_for_zoom() {
  if (curves_clipping_preview_image_.isNull() || zoom_ >= 1.0) {
    return curves_clipping_preview_image_;
  }

  if (curves_clipping_display_mip_cache_.empty() ||
      curves_clipping_display_mip_source_key_ != curves_clipping_preview_image_.cacheKey()) {
    curves_clipping_display_mip_cache_.clear();
    curves_clipping_display_mip_source_key_ = curves_clipping_preview_image_.cacheKey();
  }

  const auto target_level = display_mip_level_for_zoom(view_zoom());
  if (target_level <= 0) {
    return curves_clipping_preview_image_;
  }

  while (static_cast<int>(curves_clipping_display_mip_cache_.size()) < target_level) {
    const auto& previous = curves_clipping_display_mip_cache_.empty()
                               ? curves_clipping_preview_image_
                               : curves_clipping_display_mip_cache_.back();
    const QSize next_size(std::max(1, (previous.width() + 1) / 2),
                          std::max(1, (previous.height() + 1) / 2));
    if (next_size == previous.size()) {
      break;
    }
    curves_clipping_display_mip_cache_.push_back(
        halved_display_mip(previous));
  }

  const auto level =
      std::min<int>(target_level, static_cast<int>(curves_clipping_display_mip_cache_.size()));
  return level <= 0 ? curves_clipping_preview_image_ : curves_clipping_display_mip_cache_[level - 1];
}

// Mirror of display_image_for_zoom for the move-drag base image. The base must
// go through the same mip chain as the regular render cache: drawing it with
// plain bilinear scaling at zoomed-out levels resamples with a different phase
// than the mips that were on screen before the drag, making the artwork under
// the moving layer appear to shift inside every repainted dirty rect.
const QImage& CanvasWidget::move_base_display_image_for_zoom() {
  // A preview-scaled base is already at (or below) display resolution; the
  // painter's smooth transform bridges any remaining factor.
  if (move_base_cache_.isNull() || zoom_ >= 1.0 || move_base_cache_scale_level_ > 0) {
    return move_base_cache_;
  }

  if (move_base_display_mip_cache_.empty() || move_base_display_mip_source_key_ != move_base_cache_.cacheKey()) {
    move_base_display_mip_cache_.clear();
    move_base_display_mip_source_key_ = move_base_cache_.cacheKey();
  }

  const auto target_level = display_mip_level_for_zoom(view_zoom());
  if (target_level <= 0) {
    return move_base_cache_;
  }

  while (static_cast<int>(move_base_display_mip_cache_.size()) < target_level) {
    const auto& previous =
        move_base_display_mip_cache_.empty() ? move_base_cache_ : move_base_display_mip_cache_.back();
    const QSize next_size(std::max(1, (previous.width() + 1) / 2),
                          std::max(1, (previous.height() + 1) / 2));
    if (next_size == previous.size()) {
      break;
    }
    move_base_display_mip_cache_.push_back(
        halved_display_mip(previous));
  }

  const auto level = std::min<int>(target_level, static_cast<int>(move_base_display_mip_cache_.size()));
  return level <= 0 ? move_base_cache_ : move_base_display_mip_cache_[level - 1];
}

// Same mip mirror for the free-transform base image: without it, every
// preview repaint at zoom < 1 smooth-downscaled the full-resolution base,
// which dominated transform-drag frames on large documents.
const QImage& CanvasWidget::transform_base_display_image_for_zoom() {
  // A preview-scaled base is already at (or below) display resolution; the
  // painter's smooth transform bridges any remaining factor.
  if (transform_base_cache_.isNull() || zoom_ >= 1.0 || transform_base_cache_scale_level_ > 0) {
    return transform_base_cache_;
  }

  if (transform_base_display_mip_cache_.empty() ||
      transform_base_display_mip_source_key_ != transform_base_cache_.cacheKey()) {
    transform_base_display_mip_cache_.clear();
    transform_base_display_mip_source_key_ = transform_base_cache_.cacheKey();
  }

  const auto target_level = display_mip_level_for_zoom(view_zoom());
  if (target_level <= 0) {
    return transform_base_cache_;
  }

  while (static_cast<int>(transform_base_display_mip_cache_.size()) < target_level) {
    const auto& previous =
        transform_base_display_mip_cache_.empty() ? transform_base_cache_ : transform_base_display_mip_cache_.back();
    const QSize next_size(std::max(1, (previous.width() + 1) / 2),
                          std::max(1, (previous.height() + 1) / 2));
    if (next_size == previous.size()) {
      break;
    }
    transform_base_display_mip_cache_.push_back(
        halved_display_mip(previous));
  }

  const auto level = std::min<int>(target_level, static_cast<int>(transform_base_display_mip_cache_.size()));
  return level <= 0 ? transform_base_cache_ : transform_base_display_mip_cache_[level - 1];
}

// Same mip mirror for the warp base image (see move_base_display_image_for_zoom
// for the phase rationale). A preview-scaled base is already at (or below)
// display resolution and returns as-is.
const QImage& CanvasWidget::warp_base_display_image_for_zoom() {
  if (warp_base_cache_.isNull() || zoom_ >= 1.0 || warp_base_cache_scale_level_ > 0) {
    return warp_base_cache_;
  }

  if (warp_base_display_mip_cache_.empty() || warp_base_display_mip_source_key_ != warp_base_cache_.cacheKey()) {
    warp_base_display_mip_cache_.clear();
    warp_base_display_mip_source_key_ = warp_base_cache_.cacheKey();
  }

  const auto target_level = display_mip_level_for_zoom(view_zoom());
  if (target_level <= 0) {
    return warp_base_cache_;
  }

  while (static_cast<int>(warp_base_display_mip_cache_.size()) < target_level) {
    const auto& previous =
        warp_base_display_mip_cache_.empty() ? warp_base_cache_ : warp_base_display_mip_cache_.back();
    const QSize next_size(std::max(1, (previous.width() + 1) / 2),
                          std::max(1, (previous.height() + 1) / 2));
    if (next_size == previous.size()) {
      break;
    }
    warp_base_display_mip_cache_.push_back(
        halved_display_mip(previous));
  }

  const auto level = std::min<int>(target_level, static_cast<int>(warp_base_display_mip_cache_.size()));
  return level <= 0 ? warp_base_cache_ : warp_base_display_mip_cache_[level - 1];
}

QColor CanvasWidget::compose_document_pixel(std::int32_t x, std::int32_t y) const {
  if (document_ == nullptr || x < 0 || y < 0 || x >= document_->width() || y >= document_->height()) {
    return Qt::transparent;
  }
  // The displayed composite when it is current: render_cache_ is the CPU compositor's
  // full-resolution image of the document (group opacity, clipping, masks, styles,
  // blend modes, adjustments included), so a pick matches what the user sees and costs
  // one pixel read. Palette mode snaps that image for display and the eyedropper reads
  // the unsnapped composite, so it takes the render path below.
  if (!render_cache_dirty_ && !render_cache_.isNull() &&
      render_cache_.size() == QSize(document_->width(), document_->height()) && palette_snap_context() == nullptr) {
    return render_cache_.pixelColor(x, y);
  }
  // Otherwise the same compositor renders this one pixel (the deep path always did;
  // docs/high-bit-depth.md). Clip rendering equals full rendering, so the result is
  // the displayed pixel once the cache catches up.
  const auto image = qimage_from_document_rect(*document_, QRect(x, y, 1, 1), true);
  return image.isNull() ? QColor(Qt::transparent) : image.pixelColor(0, 0);
}

QImage CanvasWidget::compose_document_rect(QRect area) const {
  if (document_ == nullptr) {
    return {};
  }
  area = area.intersected(QRect(0, 0, document_->width(), document_->height()));
  if (area.isEmpty()) {
    return {};
  }
  // The same two sources as compose_document_pixel, for a sample square.
  if (!render_cache_dirty_ && !render_cache_.isNull() &&
      render_cache_.size() == QSize(document_->width(), document_->height()) && palette_snap_context() == nullptr) {
    return render_cache_.copy(area);
  }
  return qimage_from_document_rect(*document_, area, true);
}

void CanvasWidget::draw_checkerboard(QPainter& painter, const QRectF& rect, QRect exposed_rect) const {
  if (rect.isEmpty()) {
    return;
  }

  constexpr int square = 12;
  const auto aligned = rect.toAlignedRect();
  const auto visible = aligned.intersected(exposed_rect);
  if (visible.isEmpty()) {
    return;
  }
  painter.save();
  // Intersect, never replace: the tiling-preview backdrop calls this under a clip that
  // excludes the center tile (all pre-existing callers pass rects already inside any
  // active clip, so intersecting changes nothing for them).
  painter.setClipRect(QRectF(visible).intersected(rect), Qt::IntersectClip);
  const auto start_y = aligned.y() + ((visible.y() - aligned.y()) / square) * square;
  const auto start_x = aligned.x() + ((visible.x() - aligned.x()) / square) * square;
  for (int y = start_y; y < visible.y() + visible.height(); y += square) {
    for (int x = start_x; x < visible.x() + visible.width(); x += square) {
      const bool dark = (((x - aligned.x()) / square) + ((y - aligned.y()) / square)) % 2 == 0;
      painter.fillRect(QRect(x, y, square, square), dark ? checkerboard_dark_square() : checkerboard_light_square());
    }
  }
  painter.restore();
}

// Above this many visible ghost tiles the paint switches from exact per-tile drawImage
// calls to one textured fill from a cached tile pixmap (thousands of tiny blits would not
// keep up with painting), and partial updates widen to the full widget (the textured grid
// pitch is rounded, so exact replicated dirty rects would miss far tiles by a pixel).
constexpr int kTilingDirectDrawLimit = 24;

bool CanvasWidget::tiling_ghosts_visible(QRect widget_rect) const {
  if (!tiling_preview_enabled_ || document_ == nullptr || document_->width() <= 0 ||
      document_->height() <= 0 || widget_rect.isEmpty()) {
    return false;
  }
  // Below ~4 widget px per tile the tiling is visual mush; the preview window's Fit view
  // is the tool for that far out.
  if (static_cast<double>(document_->width()) * zoom_ < 4.0 ||
      static_cast<double>(document_->height()) * zoom_ < 4.0) {
    return false;
  }
  const QRectF center(widget_position_f(QPointF(0.0, 0.0)),
                      widget_position_f(QPointF(document_->width(), document_->height())));
  return !center.contains(QRectF(widget_rect));
}

std::vector<QPoint> CanvasWidget::tiling_direct_offsets(QRect widget_rect) const {
  std::vector<QPoint> offsets;
  if (!tiling_ghosts_visible(widget_rect)) {
    return offsets;
  }
  const double tile_w = static_cast<double>(document_->width()) * zoom_;
  const double tile_h = static_cast<double>(document_->height()) * zoom_;
  const auto min_i = static_cast<std::int64_t>(std::floor((widget_rect.left() - pan_.x()) / tile_w));
  const auto max_i = static_cast<std::int64_t>(std::floor((widget_rect.right() - pan_.x()) / tile_w));
  const auto min_j = static_cast<std::int64_t>(std::floor((widget_rect.top() - pan_.y()) / tile_h));
  const auto max_j = static_cast<std::int64_t>(std::floor((widget_rect.bottom() - pan_.y()) / tile_h));
  if ((max_i - min_i + 1) * (max_j - min_j + 1) > kTilingDirectDrawLimit + 1) {
    return offsets;  // textured-fill territory; the +1 accounts for the center tile
  }
  for (auto j = min_j; j <= max_j; ++j) {
    for (auto i = min_i; i <= max_i; ++i) {
      if (i != 0 || j != 0) {
        offsets.emplace_back(static_cast<int>(i), static_cast<int>(j));
      }
    }
  }
  return offsets;
}

// Ghost tiles for the in-canvas seamless tiling mode: wrap copies of the committed
// composite (render_cache_, or its display mip when zoomed out) around the document.
// Session previews (move patches, transform/warp/curves) deliberately stay on the center
// tile — ghosts catch up on commit — and every overlay draws center-only on top of this.
void CanvasWidget::draw_tiling_preview(QPainter& painter, const QRectF& target_rect,
                                       bool pixel_aligned_view, QRect exposed_rect) {
  if (!tiling_ghosts_visible(exposed_rect)) {
    return;
  }
  const QImage& display_image = proof_display_image(zoom_ < 1.0 ? display_image_for_zoom() : render_cache_);
  if (display_image.isNull()) {
    return;
  }
  const auto aligned_center = target_rect.toAlignedRect();
  painter.save();
  painter.setRenderHint(QPainter::SmoothPixmapTransform,
                        uses_smooth_display_scaling(view_zoom(), uses_deep_zoom_pixel_renderer(view_zoom())));
  // Keep ghosts strictly outside the center tile (the document draw stays untouched), but
  // let them overlap its outermost pixel: the center draws after us and covers it, and at
  // fractional zooms that overlap fills what would otherwise be a background hairline
  // between the aligned exclusion rect and the exact center edge.
  QRegion ghost_clip(exposed_rect);
  ghost_clip -= aligned_center.adjusted(1, 1, -1, -1);
  painter.setClipRegion(ghost_clip);

  const auto width = document_->width();
  const auto height = document_->height();
  const auto ghost_rect = [&](QPoint offset) -> QRectF {
    // The same corner mapping the center tile uses, translated in document space, so
    // pixel-aligned integer-zoom seams are exact.
    if (pixel_aligned_view) {
      const auto top_left = widget_position(QPoint(offset.x() * width, offset.y() * height));
      const auto bottom_right =
          widget_position(QPoint((offset.x() + 1) * width, (offset.y() + 1) * height));
      return QRectF(QRect(top_left, QSize(bottom_right.x() - top_left.x(), bottom_right.y() - top_left.y())));
    }
    return QRectF(widget_position_f(QPointF(offset.x() * width, offset.y() * height)),
                  widget_position_f(QPointF((offset.x() + 1) * width, (offset.y() + 1) * height)));
  };

  if (const auto offsets = tiling_direct_offsets(exposed_rect); !offsets.empty()) {
    for (const auto offset : offsets) {
      const auto rect = ghost_rect(offset);
      draw_checkerboard(painter, rect, exposed_rect);
      painter.drawImage(rect, display_image, QRectF(display_image.rect()));
    }
    painter.restore();
    return;
  }

  // Textured-fill path for grids of small tiles: one cached tile pixmap, one fillRect.
  const QSize tile_size(std::max(1, aligned_center.width()), std::max(1, aligned_center.height()));
  if (tiling_tile_pixmap_.isNull() || tiling_tile_pixmap_size_ != tile_size) {
    tiling_tile_pixmap_ = QPixmap::fromImage(display_image.scaled(
        tile_size, Qt::IgnoreAspectRatio, zoom_ < 1.0 ? Qt::SmoothTransformation : Qt::FastTransformation));
    tiling_tile_pixmap_size_ = tile_size;
  }
  // Checkerboard backdrop phase-locked to the center tile's grid (period = 2 squares).
  constexpr int kCheckerPeriod = 24;
  const auto floor_to = [](int value, int step) {
    return value >= 0 ? (value / step) * step : -(((-value) + step - 1) / step) * step;
  };
  const int backdrop_left =
      aligned_center.left() + floor_to(exposed_rect.left() - aligned_center.left(), kCheckerPeriod);
  const int backdrop_top =
      aligned_center.top() + floor_to(exposed_rect.top() - aligned_center.top(), kCheckerPeriod);
  draw_checkerboard(painter,
                    QRectF(backdrop_left, backdrop_top, exposed_rect.right() - backdrop_left + 1,
                           exposed_rect.bottom() - backdrop_top + 1),
                    exposed_rect);
  painter.setBrushOrigin(aligned_center.topLeft());
  painter.fillRect(exposed_rect, QBrush(tiling_tile_pixmap_));
  painter.restore();
}

void CanvasWidget::draw_deep_zoom_image(QPainter& painter, const QImage& image, QRect exposed_rect) const {
  if (document_ == nullptr || image.isNull() || !uses_deep_zoom_pixel_renderer(view_zoom())) {
    return;
  }
  draw_deep_zoom_pixels(painter, image, QRect(0, 0, image.width(), image.height()), exposed_rect);
}

void CanvasWidget::draw_deep_zoom_pixels(QPainter& painter, const QImage& image, QRect document_rect,
                                         QRect exposed_rect, bool grid_only) const {
  if (image.isNull() || document_rect.isEmpty() || image.size() != document_rect.size() || zoom_ <= 0.0 ||
      (grid_only && !(pixel_grid_visible_ && extra_shown(ViewExtra::PixelGrid)))) {
    return;
  }
  const auto top_left = widget_position(document_rect.topLeft());
  const auto bottom_right = widget_position(document_rect.bottomRight() + QPoint(1, 1));
  const auto visible_target = QRect(top_left, bottom_right - QPoint(1, 1)).intersected(exposed_rect);
  if (visible_target.isEmpty()) {
    return;
  }
  // Only the document pixels under the exposed rect, plus one each side for rounding.
  const auto first_x = std::clamp(
      static_cast<int>(std::floor((static_cast<double>(visible_target.left()) - pan_.x()) / zoom_)) - 1,
      document_rect.left(), document_rect.right() + 1);
  const auto first_y = std::clamp(
      static_cast<int>(std::floor((static_cast<double>(visible_target.top()) - pan_.y()) / zoom_)) - 1,
      document_rect.top(), document_rect.bottom() + 1);
  const auto end_x = std::clamp(
      static_cast<int>(std::ceil((static_cast<double>(visible_target.right() + 1) - pan_.x()) / zoom_)) + 1,
      document_rect.left(), document_rect.right() + 1);
  const auto end_y = std::clamp(
      static_cast<int>(std::ceil((static_cast<double>(visible_target.bottom() + 1) - pan_.y()) / zoom_)) + 1,
      document_rect.top(), document_rect.bottom() + 1);
  if (first_x >= end_x || first_y >= end_y) {
    return;
  }
  std::vector<int> edges_x;
  std::vector<int> edges_y;
  for (int x = first_x; x <= end_x; ++x) {
    edges_x.push_back(widget_position(QPoint(x, 0)).x());
  }
  for (int y = first_y; y <= end_y; ++y) {
    edges_y.push_back(widget_position(QPoint(0, y)).y());
  }
  const auto ratio = painter.device() != nullptr ? painter.device()->devicePixelRatioF() : 1.0;
  QPoint device_origin;
  auto raster = deep_zoom_raster(
      image.copy(first_x - document_rect.x(), first_y - document_rect.y(), end_x - first_x, end_y - first_y),
      QPoint(first_x, first_y), edges_x, edges_y, ratio,
      pixel_grid_visible_ && extra_shown(ViewExtra::PixelGrid), device_origin, grid_only);
  if (raster.isNull()) {
    return;
  }
  raster.setDevicePixelRatio(ratio);
  painter.save();
  painter.setClipRect(visible_target);
  painter.setRenderHint(QPainter::Antialiasing, false);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
  painter.drawImage(QPointF(device_origin.x() / ratio, device_origin.y() / ratio), raster);
  painter.restore();
}

void CanvasWidget::draw_grid_overlay(QPainter& painter, const QRectF& target_rect, QRect exposed_rect) const {
  if (!grid_visible_ || !extra_shown(ViewExtra::Grid) || document_ == nullptr || target_rect.isEmpty()) {
    return;
  }

  const auto visible_target = target_rect.toAlignedRect().intersected(exposed_rect);
  if (visible_target.isEmpty()) {
    return;
  }

  const auto major_x = grid_cycle_pixels(document_->grid_settings().horizontal_cycle_32);
  const auto major_y = grid_cycle_pixels(document_->grid_settings().vertical_cycle_32);
  const auto subdivisions = std::max(1, grid_subdivisions_);
  const auto minor_x = major_x / static_cast<double>(subdivisions);
  const auto minor_y = major_y / static_cast<double>(subdivisions);
  const bool deep_pixel_grid = uses_deep_zoom_pixel_renderer(view_zoom());
  const auto displayed_major_x = deep_pixel_grid ? std::max(1.0, std::round(major_x)) : major_x;
  const auto displayed_major_y = deep_pixel_grid ? std::max(1.0, std::round(major_y)) : major_y;

  painter.save();
  painter.setClipRect(QRectF(visible_target).intersected(target_rect));
  painter.setRenderHint(QPainter::Antialiasing, false);
  const auto draw_line_at_position = [this, &painter, &visible_target](double position, bool vertical) {
    if (vertical) {
      const auto x = pixel_aligned_coordinate(widget_position_f(QPointF(position, 0.0)).x(), zoom_);
      painter.drawLine(QPointF(x, visible_target.top()), QPointF(x, visible_target.bottom()));
    } else {
      const auto y = pixel_aligned_coordinate(widget_position_f(QPointF(0.0, position)).y(), zoom_);
      painter.drawLine(QPointF(visible_target.left(), y), QPointF(visible_target.right(), y));
    }
  };
  const auto draw_lines = [this, &painter, &visible_target](double spacing, bool vertical, QColor color,
                                                           Qt::PenStyle style,
                                                           const auto& draw_line_at_position) {
    if (spacing <= 0.0 || spacing * zoom_ < 3.0) {
      return;
    }
    QPen pen(color, 1.0, style);
    pen.setCosmetic(true);
    painter.setPen(pen);
    const auto limit = vertical ? document_->width() : document_->height();
    const auto end = static_cast<double>(limit);
    const auto visible_start =
        vertical ? (static_cast<double>(visible_target.left()) - pan_.x()) / zoom_
                 : (static_cast<double>(visible_target.top()) - pan_.y()) / zoom_;
    const auto visible_end =
        vertical ? (static_cast<double>(visible_target.right() + 1) - pan_.x()) / zoom_
                 : (static_cast<double>(visible_target.bottom() + 1) - pan_.y()) / zoom_;
    const auto first = std::max(0.0, std::floor(visible_start / spacing) * spacing);
    const auto last = std::min(end, std::ceil(visible_end / spacing) * spacing);
    for (double position = first; position <= last + 0.0001; position += spacing) {
      draw_line_at_position(position, vertical);
    }
  };
  const auto draw_deep_subdivision_lines = [this, &painter, &visible_target, subdivisions,
                                           &draw_line_at_position](double major_spacing, bool vertical, QColor color,
                                                                   Qt::PenStyle style) {
    if (subdivisions <= 1 || major_spacing <= 0.0 || major_spacing * zoom_ < 3.0) {
      return;
    }

    std::vector<int> offsets;
    offsets.reserve(static_cast<std::size_t>(subdivisions - 1));
    int last_offset = -1;
    const auto major_pixels = std::max(1, static_cast<int>(std::lround(major_spacing)));
    if (major_pixels <= 1) {
      return;
    }
    for (int index = 1; index < subdivisions; ++index) {
      const auto offset = std::clamp(static_cast<int>(std::lround(
                                       static_cast<double>(index) * static_cast<double>(major_pixels) /
                                       static_cast<double>(subdivisions))),
                                     1, major_pixels - 1);
      if (offset != last_offset) {
        offsets.push_back(offset);
        last_offset = offset;
      }
    }
    if (offsets.empty()) {
      return;
    }

    QPen pen(color, 1.0, style);
    pen.setCosmetic(true);
    painter.setPen(pen);
    const auto limit = vertical ? document_->width() : document_->height();
    const auto visible_start =
        vertical ? (static_cast<double>(visible_target.left()) - pan_.x()) / zoom_
                 : (static_cast<double>(visible_target.top()) - pan_.y()) / zoom_;
    const auto visible_end =
        vertical ? (static_cast<double>(visible_target.right() + 1) - pan_.x()) / zoom_
                 : (static_cast<double>(visible_target.bottom() + 1) - pan_.y()) / zoom_;
    const auto first_cycle = std::floor(visible_start / static_cast<double>(major_pixels)) * major_pixels;
    const auto last_cycle = std::ceil(visible_end / static_cast<double>(major_pixels)) * major_pixels;
    for (double cycle = first_cycle; cycle <= last_cycle + 0.0001; cycle += static_cast<double>(major_pixels)) {
      for (const auto offset : offsets) {
        const auto position = cycle + static_cast<double>(offset);
        if (position < 0.0 || position > static_cast<double>(limit)) {
          continue;
        }
        draw_line_at_position(position, vertical);
      }
    }
  };

  auto minor_color = grid_color_;
  minor_color.setAlpha(std::clamp(grid_color_.alpha() / 2, 24, 120));
  auto major_color = grid_color_;
  major_color.setAlpha(std::clamp(grid_color_.alpha(), 45, 220));
  auto pixel_color = grid_color_;
  pixel_color.setAlpha(std::clamp(grid_color_.alpha() / 2, 32, 110));
  if (subdivisions > 1) {
    if (deep_pixel_grid) {
      const auto minor_style = static_cast<double>(subdivisions) >= displayed_major_x ? Qt::SolidLine
                                                                                      : (grid_style_ == 0
                                                                                             ? Qt::DotLine
                                                                                             : Qt::DashLine);
      draw_deep_subdivision_lines(displayed_major_x, true,
                                  minor_style == Qt::SolidLine ? pixel_color : minor_color, minor_style);
      const auto minor_style_y = static_cast<double>(subdivisions) >= displayed_major_y ? Qt::SolidLine
                                                                                        : (grid_style_ == 0
                                                                                               ? Qt::DotLine
                                                                                               : Qt::DashLine);
      draw_deep_subdivision_lines(displayed_major_y, false,
                                  minor_style_y == Qt::SolidLine ? pixel_color : minor_color, minor_style_y);
    } else {
      draw_lines(minor_x, true, minor_color, grid_style_ == 0 ? Qt::DotLine : Qt::DashLine,
                 draw_line_at_position);
      draw_lines(minor_y, false, minor_color, grid_style_ == 0 ? Qt::DotLine : Qt::DashLine,
                 draw_line_at_position);
    }
  }
  draw_lines(displayed_major_x, true, major_color, grid_style_ == 0 ? Qt::SolidLine : Qt::DotLine,
             draw_line_at_position);
  draw_lines(displayed_major_y, false, major_color, grid_style_ == 0 ? Qt::SolidLine : Qt::DotLine,
             draw_line_at_position);
  painter.restore();
}

void CanvasWidget::draw_guides_overlay(QPainter& painter) const {
  // A guide being dragged draws even while hidden, so the drag is visible.
  if (((!guides_visible_ || !extra_shown(ViewExtra::Guides)) && !dragging_guide_) || document_ == nullptr) {
    return;
  }

  painter.save();
  // Every guide spans the whole view, pasteboard included, like Photoshop's: a guide
  // placed off the canvas is still a guide (GitHub issue 231). The rulers paint over the
  // ends afterwards; a guide whose line falls outside the widget is skipped.
  const QRectF view(0.0, 0.0, static_cast<double>(width()), static_cast<double>(height()));
  auto draw_guide = [this, &painter, view](GuideOrientation orientation, std::int32_t position_32,
                                           bool selected, bool transient, bool remove) {
    auto color = selected ? QColor(255, 235, 105, 230) : guide_color_;
    if (transient && remove) {
      color = QColor(255, 96, 96, 210);
    }
    QPen pen(color, selected ? 2.0 : 1.0, transient && remove ? Qt::DashLine : Qt::SolidLine);
    pen.setCosmetic(true);
    painter.setPen(pen);
    const auto pixels = static_cast<double>(position_32) / 32.0;
    if (orientation == GuideOrientation::Vertical) {
      const auto x = pixel_aligned_coordinate(widget_position_f(QPointF(pixels, 0.0)).x(), zoom_);
      if (x >= view.left() - 2.0 && x <= view.right() + 2.0) {
        painter.drawLine(QPointF(x, view.top()), QPointF(x, view.bottom()));
      }
    } else {
      const auto y = pixel_aligned_coordinate(widget_position_f(QPointF(0.0, pixels)).y(), zoom_);
      if (y >= view.top() - 2.0 && y <= view.bottom() + 2.0) {
        painter.drawLine(QPointF(view.left(), y), QPointF(view.right(), y));
      }
    }
  };

  for (int index = 0; index < static_cast<int>(document_->guides().size()); ++index) {
    const auto& guide = document_->guides()[static_cast<std::size_t>(index)];
    draw_guide(guide.orientation, guide.position_32, index == selected_guide_index_, false, false);
  }
  if (dragging_guide_) {
    draw_guide(guide_drag_orientation_, guide_drag_position_32_, true, true, guide_drag_remove_);
  }
  painter.restore();
}

void CanvasWidget::draw_rulers(QPainter& painter) const {
  if (!rulers_visible_ || document_ == nullptr) {
    return;
  }

  painter.save();
  painter.fillRect(QRect(0, 0, width(), kTopRulerHeight), theme().ruler_bar_bg);
  painter.fillRect(QRect(0, 0, kLeftRulerWidth, height()), theme().ruler_bar_bg);
  painter.fillRect(QRect(0, 0, kLeftRulerWidth, kTopRulerHeight), theme().ruler_corner_bg);
  painter.setPen(theme().ruler_edge);
  painter.drawLine(0, kTopRulerHeight - 1, width(), kTopRulerHeight - 1);
  painter.drawLine(kLeftRulerWidth - 1, 0, kLeftRulerWidth - 1, height());

  QFont label_font = font();
  label_font.setPointSize(std::max(7, label_font.pointSize() - 2));
  painter.setFont(label_font);
  const QFontMetrics metrics(label_font);
  QPen tick_pen(theme().ruler_tick);
  tick_pen.setCosmetic(true);
  painter.setPen(tick_pen);

  // Ticks live in ruler-unit space per axis; iterating integer minor indices keeps
  // the major test exact for fractional steps.
  const auto pixels_per_unit_for_axis = [this](bool horizontal) {
    return ruler_pixels_per_unit(horizontal);
  };
  const auto tick_label = [this](double value) {
    if (ruler_unit_ == MeasurementUnit::Pixels) {
      return QString::number(std::llround(value));
    }
    return QString::number(value, 'g', 6);
  };

  const auto draw_axis = [&](bool horizontal) {
    const auto pixels_per_unit = pixels_per_unit_for_axis(horizontal);
    const auto steps = ruler_tick_steps(ruler_unit_, pixels_per_unit * zoom_);
    const auto minor = steps.major / steps.subdivisions;
    const auto origin = document_position_f(QPointF(0.0, 0.0));
    const auto start_units = (horizontal ? origin.x() : origin.y()) / pixels_per_unit;
    const auto end_units =
        (horizontal ? document_position_f(QPointF(width(), 0.0)).x()
                    : document_position_f(QPointF(0.0, height())).y()) /
        pixels_per_unit;
    const auto extent = static_cast<double>(horizontal ? document_->width() : document_->height());
    const auto first_index = static_cast<long long>(std::floor(start_units / minor)) - 1;
    const auto last_index = static_cast<long long>(std::ceil(end_units / minor)) + 1;
    for (auto index = first_index; index <= last_index; ++index) {
      const auto value = static_cast<double>(index) * minor;
      const auto document_coordinate = value * pixels_per_unit;
      if (document_coordinate < 0.0 || document_coordinate > extent) {
        continue;
      }
      const bool is_major =
          ((index % steps.subdivisions) + steps.subdivisions) % steps.subdivisions == 0;
      const int length = is_major ? 11 : 6;
      if (horizontal) {
        const auto x = widget_position_f(QPointF(document_coordinate, 0.0)).x();
        painter.drawLine(QPointF(x, kTopRulerHeight - 1), QPointF(x, kTopRulerHeight - 1 - length));
        if (is_major) {
          painter.drawText(QPointF(x + 3.0, static_cast<double>(metrics.ascent() + 2)), tick_label(value));
        }
      } else {
        const auto y = widget_position_f(QPointF(0.0, document_coordinate)).y();
        painter.drawLine(QPointF(kLeftRulerWidth - 1, y), QPointF(kLeftRulerWidth - 1 - length, y));
        if (is_major) {
          painter.drawText(QRectF(2.0, y + 2.0, kLeftRulerWidth - 8.0, metrics.height()), Qt::AlignRight,
                           tick_label(value));
        }
      }
    }
  };
  draw_axis(true);
  draw_axis(false);
  painter.restore();
}

void CanvasWidget::set_move_preview_requested(bool requested) {
  if (move_preview_requested_ == requested) return;
  move_preview_requested_ = requested;
  if (requested && !processing_animation_timer_.isActive()) {
    processing_animation_timer_.start(kProcessingAnimationIntervalMs, this);
  }
  // This is a paint-only hint: never enter a processing wait or pump events
  // from the pointer handler. Cancellation hides it even if its worker remains.
  update();
}

void CanvasWidget::begin_preview_render() {
  if (++preview_renders_in_flight_ != 1) {
    return;
  }
  preview_render_progress_ = -1;
  preview_render_refining_ = false;
  preview_render_started_.start();
  // The animation timer keeps itself alive while a preview is in flight (see
  // timerEvent); its first tick past the badge delay paints the overlay, so
  // no separate wake-up is needed for an otherwise idle canvas.
  if (!processing_animation_timer_.isActive()) {
    processing_animation_timer_.start(kProcessingAnimationIntervalMs, this);
  }
}

void CanvasWidget::end_preview_render() {
  if (preview_renders_in_flight_ <= 0) {
    return;
  }
  if (--preview_renders_in_flight_ != 0) {
    return;
  }
  preview_render_progress_ = -1;
  preview_render_refining_ = false;
  if (preview_render_started_.isValid() && preview_render_started_.elapsed() >= processing_overlay_delay_ms()) {
    update();
  }
}

void CanvasWidget::set_preview_render_refining(bool refining) {
  if (refining == preview_render_refining_) {
    return;
  }
  preview_render_refining_ = refining;
  if (preview_render_overlay_visible()) {
    update();
  }
}

void CanvasWidget::set_preview_render_progress(int percent) {
  percent = percent < 0 ? -1 : std::min(percent, 100);
  if (percent == preview_render_progress_) {
    return;
  }
  preview_render_progress_ = percent;
  if (preview_render_overlay_visible()) {
    update();
  }
}

QString CanvasWidget::preview_render_message() const {
  const QString message = preview_render_refining_ ? tr("Refining preview...") : tr("Rendering preview...");
  return preview_render_progress_ < 0
             ? message
             : message + QLatin1Char(' ') + QString::number(preview_render_progress_) + percent_suffix();
}

bool CanvasWidget::background_refresh_overlay_visible() const noexcept {
  return (async_render_cache_in_flight_ || move_commit_job_.has_value()) && background_refresh_started_.isValid() &&
         background_refresh_started_.elapsed() >= processing_overlay_delay_ms();
}

// Called at every transition of the async refresh and the deferred Move commit
// job. The animation timer keeps itself alive while either is in flight (see
// timerEvent); its first tick past the delay paints the badge.
void CanvasWidget::note_background_refresh_state() {
  if (async_render_cache_in_flight_ || move_commit_job_.has_value()) {
    if (!background_refresh_started_.isValid()) {
      background_refresh_started_.start();
    }
    if (!processing_animation_timer_.isActive()) {
      processing_animation_timer_.start(kProcessingAnimationIntervalMs, this);
    }
  } else {
    background_refresh_started_.invalidate();
  }
}

bool CanvasWidget::preview_render_overlay_visible() const {
  // An outline-only Move needs immediate feedback, including while the mouse
  // is stationary. Other preview operations keep their existing badge delay.
  if (moving_layer_ && move_preview_requested_) return true;
  return preview_renders_in_flight_ > 0 && preview_render_started_.isValid() &&
         preview_render_started_.elapsed() >= processing_overlay_delay_ms();
}

void CanvasWidget::draw_processing_overlay(QPainter& painter) const {
  const auto first_render_wait = first_render_spinner_active();
  const auto preview_render_wait =
      !processing_overlay_visible_ && !first_render_wait && preview_render_overlay_visible();
  const auto background_refresh_wait = !processing_overlay_visible_ && !first_render_wait && !preview_render_wait &&
                                       background_refresh_overlay_visible();
  if (!processing_overlay_visible_ && !first_render_wait && !preview_render_wait && !background_refresh_wait) {
    return;
  }
  const QString overlay_message = processing_overlay_visible_ ? processing_overlay_message_
                                  : preview_render_wait      ? preview_render_message()
                                                             : tr("Processing...");

  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);

  auto label_font = font();
  label_font.setBold(true);
  const QFontMetrics label_metrics(label_font);
  constexpr int kPanelMargin = 12;
  constexpr int kPanelHeight = 50;
  constexpr int kSpinnerColumnWidth = 52;
  constexpr int kTextRightPadding = 18;
  const auto max_panel_width = std::max(1, width() - kPanelMargin * 2);
  const auto min_panel_width = std::min(168, max_panel_width);
  const auto desired_panel_width = label_metrics.horizontalAdvance(overlay_message) +
                                   kSpinnerColumnWidth + kTextRightPadding;
  const QSize panel_size(std::min(max_panel_width, std::max(min_panel_width, desired_panel_width)), kPanelHeight);
  const auto desired_top = (rulers_visible_ ? kTopRulerHeight : 0) + kPanelMargin;
  const auto max_top = std::max(kPanelMargin, height() - panel_size.height() - kPanelMargin);
  const QRect panel_rect(QPoint((width() - panel_size.width()) / 2, std::min(desired_top, max_top)),
                         panel_size);
  QPainterPath panel_path;
  panel_path.addRoundedRect(QRectF(panel_rect), 8.0, 8.0);
  auto hud_fill = theme().canvas_hud_bg;
  hud_fill.setAlpha(236);
  painter.fillPath(panel_path, hud_fill);
  painter.setPen(QPen(theme().canvas_hud_border, 1));
  painter.drawPath(panel_path);

  const QPointF spinner_center(panel_rect.left() + 36.0, panel_rect.center().y());
  constexpr int kSegments = 12;
  for (int index = 0; index < kSegments; ++index) {
    const auto phase = (index + processing_animation_frame_) % kSegments;
    const auto alpha = 55 + phase * 15;
    const auto angle = (static_cast<double>(index) / static_cast<double>(kSegments)) * 2.0 * kPi;
    const QPointF inner(spinner_center.x() + std::cos(angle) * 8.0,
                        spinner_center.y() + std::sin(angle) * 8.0);
    const QPointF outer(spinner_center.x() + std::cos(angle) * 15.0,
                        spinner_center.y() + std::sin(angle) * 15.0);
    auto spinner = theme().canvas_hud_spinner;
    spinner.setAlpha(std::clamp(alpha, 55, 220));
    QPen pen(spinner, 2.8, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(pen);
    painter.drawLine(inner, outer);
  }

  painter.setFont(label_font);
  painter.setPen(theme().canvas_hud_text);
  const QRect text_rect(panel_rect.left() + kSpinnerColumnWidth, panel_rect.top(),
                        std::max(0, panel_rect.width() - kSpinnerColumnWidth - kTextRightPadding),
                        panel_rect.height());
  painter.drawText(text_rect, Qt::AlignVCenter | Qt::AlignLeft,
                   label_metrics.elidedText(overlay_message, Qt::ElideRight, text_rect.width()));
  painter.restore();
}

void CanvasWidget::show_processing_overlay(QString message) {
  processing_overlay_message_ = message.isEmpty() ? tr("Processing...") : std::move(message);
  const auto was_visible = processing_overlay_visible_;
  processing_overlay_visible_ = true;
  if (!processing_animation_timer_.isActive()) {
    processing_animation_timer_.start(kProcessingAnimationIntervalMs, this);
  }
  if (!was_visible) {
    ++render_cache_diagnostics_.processing_overlays_shown;
  }
  update();
#ifndef Q_OS_WASM
  // Deliver the scheduled paint so the overlay is visible before the caller
  // blocks. On wasm the overlay paints when the nested wait loop suspends,
  // and a pump here would only dispatch Qt timers (the web-drop drain, the
  // picker poll) into the middle of the blocked operation.
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 16);
#endif
}

void CanvasWidget::hide_processing_overlay() {
  if (!processing_overlay_visible_) {
    return;
  }
  processing_overlay_visible_ = false;
  processing_overlay_message_.clear();
  if (preview_renders_in_flight_ == 0 && !move_preview_requested_) {
    processing_animation_timer_.stop();
  }
  update();
}

void CanvasWidget::refresh_mask_display_image(QRegion document_region) {
  if ((!quick_mask_active_ && mask_display_mode_ == MaskDisplayMode::None) ||
      document_ == nullptr) {
    return;
  }

  const QSize document_size(document_->width(), document_->height());
  if (document_size.isEmpty()) {
    mask_display_image_ = QImage();
    mask_display_image_layer_ = 0;
    mask_display_image_channel_ = 0;
    mask_display_image_revision_ = 0;
    return;
  }

  const bool component_preview =
      !quick_mask_active_ &&
      (CanvasWidget::is_component_preview(layer_edit_target_));
  const bool smart_filter_mask_target =
      !quick_mask_active_ && layer_edit_target_ == LayerEditTarget::SmartFilterMask;
  const bool smart_filter_mask = smart_filter_mask_target && editing_smart_filter_mask();
  const auto* channel =
      quick_mask_active_ ? nullptr : active_document_channel_const();
  const auto source_revision =
      quick_mask_active_
          ? quick_mask_revision_
          : smart_filter_mask ? smart_filter_mask_revision_
          : channel != nullptr ? channel->content_revision()
                               : std::uint64_t{0};
  const Layer* layer = nullptr;
  if (!quick_mask_active_ && !component_preview && !smart_filter_mask_target && channel == nullptr &&
      document_->active_layer_id().has_value()) {
    // Use const access throughout so inspecting the mask does not bump layer
    // render revisions on every repaint.
    layer = static_cast<const Layer*>(document_->find_layer(*document_->active_layer_id()));
  }
  if (smart_filter_mask_target && !smart_filter_mask) {
    mask_display_image_ = QImage();
    mask_display_image_layer_ = 0;
    mask_display_image_channel_ = 0;
    mask_display_image_revision_ = 0;
    return;
  }
  const bool vector_mask_display = layer_edit_target_ == LayerEditTarget::VectorMask &&
                                   layer != nullptr && layer->vector_mask() != nullptr;
  if (!quick_mask_active_ && !component_preview && !smart_filter_mask_target && channel == nullptr &&
      (layer == nullptr || (!layer->mask().has_value() && !vector_mask_display))) {
    mask_display_image_ = QImage();
    mask_display_image_layer_ = 0;
    mask_display_image_channel_ = 0;
    mask_display_image_revision_ = 0;
    return;
  }

  const auto source_layer_id = layer != nullptr ? layer->id() : LayerId{0};
  const auto source_channel_id = channel != nullptr ? channel->id() : ChannelId{0};
  if (mask_display_image_.isNull() || mask_display_image_.size() != document_size ||
      mask_display_image_layer_ != source_layer_id || mask_display_image_channel_ != source_channel_id ||
      mask_display_image_revision_ != source_revision) {
    mask_display_image_ = QImage(document_size, QImage::Format_ARGB32_Premultiplied);
    mask_display_image_layer_ = source_layer_id;
    mask_display_image_channel_ = source_channel_id;
    mask_display_image_revision_ = source_revision;
    document_region = QRegion(QRect(QPoint(), document_size));
  } else {
    document_region = document_region.intersected(QRect(QPoint(), document_size));
    if (document_region.isEmpty()) {
      return;
    }
  }

  if (component_preview) {
    if (render_cache_.isNull() || render_cache_.size() != document_size) {
      return;
    }
    if (is_cmyk_plate(layer_edit_target_)) {
      // An ink plate (docs/cmyk.md): the composite over white paper, separated once per
      // composite and working space, drawn dark where the plate prints ink.
      const auto space = working_cmyk_space();
      const auto width = static_cast<std::size_t>(document_size.width());
      const auto pixel_count = width * static_cast<std::size_t>(document_size.height());
      if (plate_inks_.size() != pixel_count * 4U || plate_inks_source_key_ != render_cache_.cacheKey() ||
          plate_inks_space_ != space) {
        const auto straight = render_cache_.convertToFormat(QImage::Format_RGBA8888);
        std::vector<std::uint8_t> on_paper(pixel_count * 4U);
        for (int y = 0; y < document_size.height(); ++y) {
          const auto* from = straight.constScanLine(y);
          auto* to = on_paper.data() + static_cast<std::size_t>(y) * width * 4U;
          for (std::size_t x = 0; x < width; ++x) {
            const auto alpha = static_cast<int>(from[x * 4U + 3U]);
            for (std::size_t color = 0; color < 3; ++color) {
              to[x * 4U + color] = static_cast<std::uint8_t>(
                  (static_cast<int>(from[x * 4U + color]) * alpha + 255 * (255 - alpha) + 127) / 255);
            }
            to[x * 4U + 3U] = 255;
          }
        }
        plate_inks_ = space->separate_rgba8(on_paper);
        plate_inks_source_key_ = render_cache_.cacheKey();
        plate_inks_space_ = space;
      }
      if (channel_view_.group == ChannelViewGroup::Ink && channel_view_visible_count(channel_view_) > 1) {
        // Several plates shown: each visible ink alone on paper, multiplied, the way New
        // Layer from Channel Color stacks them (docs/channels.md, "Channel visibility").
        if (plate_ink_colors_space_ != space) {
          for (std::size_t ink_plate = 0; ink_plate < plate_ink_colors_.size(); ++ink_plate) {
            for (std::size_t level = 0; level < 256; ++level) {
              CmykPercent ink;
              const auto percent = static_cast<double>(level) * 100.0 / 255.0;
              (ink_plate == 0   ? ink.cyan
               : ink_plate == 1 ? ink.magenta
               : ink_plate == 2 ? ink.yellow
                                : ink.black) = percent;
              const auto color = space->rgb_from_cmyk(ink);
              plate_ink_colors_[ink_plate][level] = qRgb(color.red, color.green, color.blue);
            }
          }
          plate_ink_colors_space_ = space;
        }
        for (const auto& rect : document_region) {
          for (int y = rect.top(); y <= rect.bottom(); ++y) {
            const auto* inks = plate_inks_.data() + static_cast<std::size_t>(y) * width * 4U;
            auto* row = reinterpret_cast<QRgb*>(mask_display_image_.scanLine(y));
            for (int x = rect.left(); x <= rect.right(); ++x) {
              int red = 255;
              int green = 255;
              int blue = 255;
              for (int ink_plate = 0; ink_plate < 4; ++ink_plate) {
                if (!channel_view_shows(channel_view_, ChannelViewGroup::Ink, ink_plate)) {
                  continue;
                }
                const auto ink = plate_ink_colors_[static_cast<std::size_t>(ink_plate)]
                                                  [inks[static_cast<std::size_t>(x) * 4U +
                                                        static_cast<std::size_t>(ink_plate)]];
                red = (red * qRed(ink) + 127) / 255;
                green = (green * qGreen(ink) + 127) / 255;
                blue = (blue * qBlue(ink) + 127) / 255;
              }
              row[x] = qRgb(red, green, blue);
            }
          }
        }
        return;
      }
      const std::size_t plate = layer_edit_target_ == LayerEditTarget::ComponentCyan      ? 0
                                : layer_edit_target_ == LayerEditTarget::ComponentMagenta ? 1
                                : layer_edit_target_ == LayerEditTarget::ComponentYellow  ? 2
                                                                                          : 3;
      for (const auto& rect : document_region) {
        for (int y = rect.top(); y <= rect.bottom(); ++y) {
          const auto* inks = plate_inks_.data() + static_cast<std::size_t>(y) * width * 4U;
          auto* row = reinterpret_cast<QRgb*>(mask_display_image_.scanLine(y));
          for (int x = rect.left(); x <= rect.right(); ++x) {
            const auto value = 255 - static_cast<int>(inks[static_cast<std::size_t>(x) * 4U + plate]);
            row[x] = qRgb(value, value, value);
          }
        }
      }
      return;
    }
    const auto source = render_cache_.convertToFormat(QImage::Format_RGBA8888);
    const auto component = layer_edit_target_ == LayerEditTarget::ComponentRed     ? 0
                           : layer_edit_target_ == LayerEditTarget::ComponentGreen ? 1
                                                                                   : 2;
    for (const auto& rect : document_region) {
      for (int y = rect.top(); y <= rect.bottom(); ++y) {
        const auto* source_row = source.constScanLine(y);
        auto* row = reinterpret_cast<QRgb*>(mask_display_image_.scanLine(y));
        for (int x = rect.left(); x <= rect.right(); ++x) {
          const auto value = source_row[static_cast<std::size_t>(x) * 4U + static_cast<std::size_t>(component)];
          row[x] = qRgb(value, value, value);
        }
      }
    }
    return;
  }

  const bool grayscale =
      !quick_mask_active_ && mask_display_mode_ == MaskDisplayMode::Grayscale;
  const PixelBuffer* pixels = nullptr;
  QRect bounds;
  std::uint8_t default_value = 0;
  QColor overlay_color(255, 0, 0);
  float overlay_opacity = 0.5F;
  bool overlay_selected_areas = false;
  if (quick_mask_active_) {
    pixels = &quick_mask_pixels_;
    bounds = QRect(0, 0, pixels->width(), pixels->height());
  } else if (smart_filter_mask) {
    pixels = &smart_filter_mask_pixels_;
    bounds = QRect(0, 0, pixels->width(), pixels->height());
  } else if (channel != nullptr) {
    pixels = &channel->pixels();
    bounds = QRect(0, 0, pixels->width(), pixels->height());
    const auto& display = channel->display_info();
    overlay_color = QColor(display.color.red, display.color.green, display.color.blue);
    overlay_opacity = std::clamp(display.opacity, 0.0F, 1.0F);
    overlay_selected_areas = display.color_indicates == DocumentChannelColorIndicates::SelectedAreas;
  } else if (vector_mask_display) {
    const auto& mask = *layer->vector_mask();
    pixels = &mask.cache;
    bounds = QRect(mask.cache_bounds.x, mask.cache_bounds.y, mask.cache_bounds.width,
                   mask.cache_bounds.height);
    default_value = 0;
  } else {
    const auto& mask = *layer->mask();
    pixels = &mask.pixels;
    bounds = QRect(mask.bounds.x, mask.bounds.y, mask.bounds.width, mask.bounds.height);
    default_value = mask.default_color;
  }
  const bool pixels_usable = pixels != nullptr && !pixels->empty() && pixels->format() == PixelFormat::gray8();
  for (const auto& rect : document_region) {
    for (int y = rect.top(); y <= rect.bottom(); ++y) {
      auto* row = reinterpret_cast<QRgb*>(mask_display_image_.scanLine(y));
      for (int x = rect.left(); x <= rect.right(); ++x) {
        const auto value = pixels_usable && bounds.contains(x, y)
                               ? *pixels->pixel(x - bounds.x(), y - bounds.y())
                               : default_value;
        if (grayscale) {
          row[x] = qRgb(value, value, value);
        } else if (!quick_mask_active_ && !smart_filter_mask && channel == nullptr) {
          // Preserve the historical layer-mask overlay byte-for-byte. Integer
          // division intentionally maps a fully hidden pixel to alpha 127.
          const auto alpha = static_cast<QRgb>(255 - value) / 2U;
          row[x] = (alpha << 24U) | (alpha << 16U);
        } else {
          const auto coverage = overlay_selected_areas ? value : static_cast<std::uint8_t>(255U - value);
          const auto alpha = static_cast<std::uint8_t>(std::clamp(
              std::lround(static_cast<double>(coverage) * overlay_opacity), 0L, 255L));
          const auto premultiply = [alpha](int component) {
            return static_cast<QRgb>((component * static_cast<int>(alpha) + 127) / 255);
          };
          row[x] = (static_cast<QRgb>(alpha) << 24U) |
                   (premultiply(overlay_color.red()) << 16U) |
                   (premultiply(overlay_color.green()) << 8U) |
                   premultiply(overlay_color.blue());
        }
      }
    }
  }
}

void CanvasWidget::draw_mask_display_overlay(QPainter& painter, const QRectF& target_rect, bool pixel_aligned_view,
                                             QRect pixel_aligned_target_rect) {
  if ((!quick_mask_active_ && mask_display_mode_ == MaskDisplayMode::None) ||
      document_ == nullptr) {
    return;
  }
  LayerId expected_layer_id = 0;
  if (quick_mask_active_) {
    // The temporary mask is always shown as a red masked-area overlay.
  } else if (layer_edit_target_ == LayerEditTarget::SmartFilterMask) {
    if (!editing_smart_filter_mask()) {
      return;
    }
  } else if (layer_edit_target_ == LayerEditTarget::Mask) {
    if (!document_->active_layer_id().has_value()) {
      return;
    }
    const auto* layer = static_cast<const Layer*>(document_->find_layer(*document_->active_layer_id()));
    if (layer == nullptr || !layer->mask().has_value()) {
      return;
    }
    expected_layer_id = layer->id();
    if (mask_display_mode_ == MaskDisplayMode::Overlay && layer->mask()->disabled) {
      // A disabled mask hides nothing, so there is no coverage to mark.
      return;
    }
  } else if (layer_edit_target_ == LayerEditTarget::DocumentChannel) {
    if (active_document_channel_const() == nullptr) {
      return;
    }
  } else if (!is_component_preview(layer_edit_target_) ||
             (!is_cmyk_plate(layer_edit_target_) && channel_view_visible_count(channel_view_) > 1)) {
    // Several color channels shown draw the composite with the hidden one zeroed instead.
    return;
  }

  const QSize document_size(document_->width(), document_->height());
  const auto* channel =
      quick_mask_active_ ? nullptr : active_document_channel_const();
  const auto expected_revision =
      quick_mask_active_
          ? quick_mask_revision_
          : layer_edit_target_ == LayerEditTarget::SmartFilterMask
                ? smart_filter_mask_revision_
          : channel != nullptr ? channel->content_revision()
                               : std::uint64_t{0};
  if (mask_display_image_.isNull() || mask_display_image_.size() != document_size ||
      (expected_layer_id != 0 && mask_display_image_layer_ != expected_layer_id) ||
      (channel != nullptr && mask_display_image_channel_ != channel->id()) ||
      mask_display_image_revision_ != expected_revision) {
    refresh_mask_display_image(QRegion(QRect(QPoint(), document_size)));
  }
  if (mask_display_image_.isNull()) {
    return;
  }
  if (pixel_aligned_view) {
    painter.drawImage(pixel_aligned_target_rect, mask_display_image_, mask_display_image_.rect());
  } else {
    painter.drawImage(target_rect, mask_display_image_, QRectF(mask_display_image_.rect()));
  }
}

}  // namespace patchy::ui
