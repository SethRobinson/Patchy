// CanvasWidget's rendering implementation, split out of canvas_widget.cpp:
// paintEvent, the render invalidation and processing-operation plumbing (the
// document_changed overload family, force_refresh, and the processing overlay),
// the composite render cache with its async refresh and patch renderers, the
// display mip / curves-clipping / move-base display caches, and the static
// overlay painters (checkerboard, deep zoom, grid, guides, rulers, and the
// mask display overlay). Pure function moves from canvas_widget.cpp; behavior
// must stay identical.

#include "ui/canvas_widget.hpp"
#include "ui/canvas_widget_shared.hpp"

#include "core/adjustment_layer.hpp"
#include "core/blend_math.hpp"
#include "core/layer_metadata.hpp"
#include "core/smart_object.hpp"
#include "core/smart_filter.hpp"
#include "core/layer_render_utils.hpp"
#include "core/layer_tree.hpp"
#include "core/pixel_tools.hpp"
#include "core/quick_select.hpp"
#include "ui/edit_conversions.hpp"
#include "ui/image_document_io.hpp"
#include "ui/qt_geometry.hpp"
#include "ui/smart_object_render.hpp"
#include "ui/tool_cursors.hpp"

#include <QApplication>
#include <QCursor>
#include <QEnterEvent>
#include <QEventLoop>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QInputDevice>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QMenu>
#include <QMetaObject>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPointingDevice>
#include <QPolygon>
#include <QPolygonF>
#include <QPointer>
#include <QRadialGradient>
#include <QResizeEvent>
#include <QScreen>
#include <QSet>
#include <QTabletEvent>
#include <QTimerEvent>
#include <QTransform>
#include <QWheelEvent>
#include <QRandomGenerator>
#include <QtGlobal>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <future>
#include <functional>
#include <iostream>
#include <limits>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

constexpr std::int64_t kProcessingOverlayDirtyAreaThreshold = 8'000'000;
constexpr int kProcessingOverlayDelayMs = 1000;
constexpr int kProcessingAnimationIntervalMs = 80;
constexpr int kMaxDirtyRegionRects = 64;

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
    image = image.scaled(next_size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(image.format());
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

void compose_layer_pixel(const Layer& layer, std::int32_t x, std::int32_t y, std::array<float, 3>& out,
                         float& out_alpha) {
  if (!layer.visible() || layer.opacity() <= 0.0F) {
    return;
  }

  if (layer.kind() == LayerKind::Group) {
    for (const auto& child : layer.children()) {
      compose_layer_pixel(child, x, y, out, out_alpha);
    }
    return;
  }

  if (layer.kind() == LayerKind::Adjustment) {
    const auto settings = adjustment_settings_from_layer(layer);
    if (!settings.has_value() || !adjustment_has_effect(*settings) || out_alpha <= 0.0F) {
      return;
    }
    if (!layer.bounds().empty() && !layer.bounds().contains(x, y)) {
      return;
    }
    const auto amount = layer_mask_alpha_at(layer, x, y) * layer.opacity();
    if (amount <= 0.0F) {
      return;
    }
    const auto adjusted =
        apply_adjustment_to_color(RgbColor{clamp_byte(out[0]), clamp_byte(out[1]), clamp_byte(out[2])}, *settings);
    out[0] = static_cast<float>(adjusted.red) * amount + out[0] * (1.0F - amount);
    out[1] = static_cast<float>(adjusted.green) * amount + out[1] * (1.0F - amount);
    out[2] = static_cast<float>(adjusted.blue) * amount + out[2] * (1.0F - amount);
    return;
  }

  if (layer.kind() != LayerKind::Pixel) {
    return;
  }

  const auto& pixels = layer.pixels();
  if (pixels.empty() || pixels.format().bit_depth != BitDepth::UInt8 || pixels.format().channels < 3) {
    return;
  }

  const auto bounds = layer.bounds();
  const auto local_x = x - bounds.x;
  const auto local_y = y - bounds.y;
  if (local_x < 0 || local_y < 0 || local_x >= pixels.width() || local_y >= pixels.height()) {
    return;
  }

  const auto* src = pixels.pixel(local_x, local_y);
  const auto source_alpha = pixels.format().channels >= 4 ? static_cast<float>(src[3]) / 255.0F : 1.0F;
  const auto alpha = source_alpha * layer_mask_alpha_at(layer, x, y) * layer.opacity();
  if (alpha <= 0.0F) {
    return;
  }

  const auto next_alpha = alpha + out_alpha * (1.0F - alpha);
  const std::array<std::uint8_t, 3> src_rgb = {src[0], src[1], src[2]};
  const std::array<std::uint8_t, 3> dst_rgb = {clamp_byte(out[0]), clamp_byte(out[1]), clamp_byte(out[2])};
  const auto blended = composite_blended_rgb(src_rgb, dst_rgb, layer.blend_mode(), alpha, out_alpha);
  for (int channel = 0; channel < 3; ++channel) {
    out[channel] = next_alpha > 0.0F ? static_cast<float>(blended[static_cast<std::size_t>(channel)]) : 0.0F;
  }
  out_alpha = next_alpha;
}

}  // namespace

CanvasWidget::RenderCacheDiagnostics CanvasWidget::render_cache_diagnostics() const noexcept {
  return render_cache_diagnostics_;
}

bool CanvasWidget::render_settled() const noexcept {
  return !render_cache_dirty_ && !async_render_cache_in_flight_;
}

bool CanvasWidget::should_defer_full_refresh_to_async() const noexcept {
  if (!render_cache_dirty_ || document_ == nullptr || processing_operation_active()) {
    return false;
  }
  if (render_cache_.isNull() ||
      render_cache_.size() != QSize(document_->width(), document_->height())) {
    return false;
  }
  const auto canvas_area =
      static_cast<std::int64_t>(document_->width()) * static_cast<std::int64_t>(document_->height());
  return canvas_area >= kProcessingOverlayDirtyAreaThreshold;
}

bool CanvasWidget::processing_overlay_visible() const noexcept {
  return processing_overlay_visible_;
}

bool CanvasWidget::processing_operation_active() const noexcept {
  return processing_operation_depth_ > 0;
}

void CanvasWidget::begin_processing_operation(QString message) {
  if (processing_operation_depth_ == 0) {
    processing_operation_started_ = std::chrono::steady_clock::now();
    processing_operation_owns_overlay_ = false;
    processing_overlay_message_ = message.isEmpty() ? tr("Processing...") : std::move(message);
  }
  ++processing_operation_depth_;
}

void CanvasWidget::tick_processing_operation() {
  if (processing_operation_depth_ <= 0) {
    return;
  }
  if (!processing_overlay_visible_) {
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - processing_operation_started_)
                                .count();
    if (elapsed_ms >= processing_overlay_delay_ms()) {
      show_processing_overlay(processing_overlay_message_);
      processing_operation_owns_overlay_ = true;
    }
  }
  if (processing_overlay_visible_) {
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 16);
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
}

void CanvasWidget::notify_document_changed(DocumentChangeReason reason) {
  if (document_changed_reason_callback_) {
    document_changed_reason_callback_(reason);
  } else if (document_changed_callback_) {
    document_changed_callback_();
  }
}

void CanvasWidget::document_changed() {
  cancel_async_render_cache_refresh();
  render_cache_dirty_ = true;
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
  if (document_ == nullptr || render_cache_.isNull() ||
      render_cache_.size() != QSize(document_->width(), document_->height())) {
    document_changed();
    return;
  }

  refresh_free_transform_preview_caches();
  notify_document_changed();
  if (!isVisible()) {
    cancel_async_render_cache_refresh();
    render_cache_dirty_ = true;
    invalidate_display_mip_cache();
    return;
  }

  if (async_render_cache_in_flight_) {
    async_render_cache_pending_ = true;
    return;
  }
  start_async_render_cache_refresh();
}

void CanvasWidget::force_refresh() {
  if (document_ == nullptr) {
    return;
  }

  cancel_async_render_cache_refresh();
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
      widget_region += widget_rect_for_document_rect(rect);
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
      widget_region += widget_rect_for_document_rect(rect);
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
    widget_region += widget_rect_for_document_rect(rect);
  }
  update(widget_region);
}

void CanvasWidget::document_changed_impl(QRegion document_region, bool includes_effect_bounds,
                                         DocumentChangeReason reason) {
  if (layer_edit_target_ == LayerEditTarget::SmartFilterMask && !editing_smart_filter_mask()) {
    // Layer deletion can occur without replacing the Document object. Drop the
    // canvas-owned buffer before another layer can become active and inherit it.
    clear_smart_filter_mask_edit_target();
  }
  cancel_async_render_cache_refresh();
  refresh_free_transform_preview_caches();
  if (mask_display_mode_ != MaskDisplayMode::None) {
    const bool component_preview = layer_edit_target_ == LayerEditTarget::ComponentRed ||
                                   layer_edit_target_ == LayerEditTarget::ComponentGreen ||
                                   layer_edit_target_ == LayerEditTarget::ComponentBlue;
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
  if (zoom_ < 1.0) {
    update();
  } else {
    QRegion widget_region;
    for (const auto& rect : document_region) {
      widget_region += widget_rect_for_document_rect(rect);
    }
    update(widget_region);
  }
}

void CanvasWidget::paintEvent(QPaintEvent* event) {
  ZoomTraceScope trace("paint", zoom_);
  QPainter painter(this);
  const auto exposed_rect = event != nullptr ? event->rect() : rect();
  painter.fillRect(exposed_rect, QColor(36, 38, 41));

  if (document_ == nullptr || document_->width() == 0 || document_->height() == 0) {
    painter.setPen(QColor(170, 176, 184));
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
  draw_checkerboard(painter, target_rect, exposed_rect);

  if (!processing_render_wait_active_) {
    if (should_defer_full_refresh_to_async()) {
      // Keep the previous frame on screen while the recomposite runs in the
      // background; the completion lambda swaps the cache and repaints. This is
      // what stops big documents (>= overlay scale) from flashing checkerboard
      // on every full invalidation (add layer, undo, blend change, ...).
      if (async_render_cache_in_flight_) {
        async_render_cache_pending_ = true;
      } else {
        start_async_render_cache_refresh();
      }
    } else {
      ensure_render_cache();
    }
  }

  const bool deep_pixel_renderer = uses_deep_zoom_pixel_renderer(zoom_);
  const auto draw_scaled_image = [&painter, &target_rect, pixel_aligned_view,
                                  &pixel_aligned_target_rect, this, exposed_rect](const QImage& image) {
    if (!image.isNull()) {
      const QImage& display_image =
          (&image == &render_cache_ && zoom_ < 1.0)
              ? display_image_for_zoom()
              : (&image == &curves_clipping_preview_image_ && zoom_ < 1.0)
                    ? curves_clipping_display_image_for_zoom()
                    : (&image == &move_base_cache_ && zoom_ < 1.0) ? move_base_display_image_for_zoom() : image;
      if (uses_deep_zoom_pixel_renderer(zoom_)) {
        draw_deep_zoom_image(painter, display_image, exposed_rect);
      } else if (pixel_aligned_view) {
        painter.drawImage(pixel_aligned_target_rect, display_image, display_image.rect());
      } else {
        painter.drawImage(target_rect, display_image, QRectF(display_image.rect()));
      }
    }
  };
  const auto draw_document_patch = [&painter, &target_rect, pixel_aligned_view, this,
                                    exposed_rect](const RenderedDocumentPatch& patch, bool clear_under_patch) {
    if (patch.image.isNull() || patch.document_rect.isEmpty()) {
      return;
    }

    const auto patch_target = widget_rect_for_document_rect(QRectF(patch.document_rect));
    const auto patch_exposed = patch_target.toAlignedRect().intersected(exposed_rect);
    if (patch_exposed.isEmpty()) {
      return;
    }

    // The checkerboard is only needed to wipe the previous (stale) backdrop out
    // from under the patch. When the base image already excludes the moving
    // layer there is nothing to wipe, and painting the checkerboard here leaves
    // a faint rectangular seam at the patch edges (a "ghost" of the patch
    // bounds). In that case draw the patch directly over the prepared base.
    if (clear_under_patch) {
      draw_checkerboard(painter, target_rect, patch_exposed);
    }
    if (uses_deep_zoom_pixel_renderer(zoom_)) {
      painter.save();
      painter.setClipRect(patch_exposed);
      painter.setRenderHint(QPainter::Antialiasing, false);
      painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
      for (int y = 0; y < patch.image.height(); ++y) {
        const auto document_y = patch.document_rect.y() + y;
        const auto top = widget_position(QPoint(0, document_y)).y();
        const auto bottom = widget_position(QPoint(0, document_y + 1)).y();
        if (bottom <= top) {
          continue;
        }
        for (int x = 0; x < patch.image.width(); ++x) {
          const auto color = patch.image.pixelColor(x, y);
          if (color.alpha() == 0) {
            continue;
          }
          const auto document_x = patch.document_rect.x() + x;
          const auto left = widget_position(QPoint(document_x, 0)).x();
          const auto right = widget_position(QPoint(document_x + 1, 0)).x();
          if (right <= left) {
            continue;
          }
          painter.fillRect(QRect(left, top, right - left, bottom - top), color);
        }
      }
      painter.restore();
      return;
    }

    // When the zoomed-out view renders from the display mip chain, drawing the
    // full-resolution patch with plain bilinear scaling resamples with a
    // different phase than the box-filtered mips around it, which makes the
    // artwork inside the patch appear to shift by a pixel or two until the
    // next full repaint. Downscale mip-grid-aligned patches with the same
    // successive halvings and map them through the same mip-space transform so
    // the patch pixels match the surrounding mip render exactly.
    const auto patch_mip_level = display_mip_level_for_zoom(zoom_);
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

    if (pixel_aligned_view) {
      painter.drawImage(patch_target.toAlignedRect(), patch.image, patch.image.rect());
    } else {
      painter.drawImage(patch_target, patch.image, QRectF(patch.image.rect()));
    }
  };
  const bool draw_transform_overlay =
      transforming_layer_ && !transform_source_image_.isNull() &&
      (!transform_base_cache_.isNull() || !transform_composited_preview_cache_.isNull());
  const bool draw_warp_overlay = warping_layer_ && (!warp_preview_cache_.isNull() || !warp_base_cache_.isNull());

  painter.save();
  painter.setClipRect(target_rect);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, uses_smooth_display_scaling(zoom_, deep_pixel_renderer));
  if (draw_transform_overlay) {
    if (!transform_composited_preview_cache_.isNull()) {
      draw_scaled_image(transform_composited_preview_cache_);
    } else {
      draw_scaled_image(transform_base_cache_);
    }
  } else if (draw_warp_overlay) {
    if (!warp_preview_cache_.isNull()) {
      draw_scaled_image(warp_preview_cache_);
    } else {
      draw_scaled_image(warp_base_cache_);
    }
  } else if (moving_layer_ && !moving_layers_.empty()) {
    const bool base_excludes_layer = !moving_layers_use_outline_preview_ && !move_base_cache_.isNull();
    if (base_excludes_layer) {
      draw_scaled_image(move_base_cache_);
    } else {
      draw_scaled_image(render_cache_);
    }
    for (const auto& patch : move_preview_patches_) {
      draw_document_patch(patch, !base_excludes_layer);
    }
  } else {
    draw_scaled_image(curves_clipping_mode_.has_value() && !curves_clipping_preview_image_.isNull()
                          ? curves_clipping_preview_image_
                          : render_cache_);
  }
  if (!curves_clipping_mode_.has_value()) {
    draw_mask_display_overlay(painter, target_rect, pixel_aligned_view, pixel_aligned_target_rect);
  }
  painter.restore();

  if (moving_layer_ && !moving_layers_.empty() && !draw_transform_overlay) {
    painter.setPen(QPen(QColor(95, 170, 255), 1, Qt::DashLine));
    for (const auto& moving_layer : moving_layers_) {
      const auto outline_rect = moving_layer_outline_rect(moving_layer, move_preview_delta_);
      if (!outline_rect.isEmpty()) {
        const QRect layer_target(widget_position(outline_rect.topLeft()),
                                 widget_position(outline_rect.bottomRight() + QPoint(1, 1)));
        painter.drawRect(layer_target.normalized().adjusted(0, 0, -1, -1));
      }
    }
  }
  if (tool_ == CanvasTool::Move && !moving_layer_ && !draw_transform_overlay && move_hover_outline_rect_.has_value()) {
    const auto outline_rect = *move_hover_outline_rect_;
    if (!outline_rect.isEmpty()) {
      painter.setPen(QPen(QColor(95, 170, 255), 1, Qt::DashLine));
      const QRect hover_target(widget_position(outline_rect.topLeft()),
                               widget_position(outline_rect.bottomRight() + QPoint(1, 1)));
      painter.drawRect(hover_target.normalized().adjusted(0, 0, -1, -1));
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
  painter.setPen(QColor(95, 101, 110));
  const auto border_rect = target_rect.adjusted(0.5, 0.5, -0.5, -0.5);
  if (!border_rect.isEmpty()) {
    painter.drawRect(border_rect);
  }
  draw_selection_overlay(painter);
  draw_quick_select_stroke_overlay(painter);
  draw_shape_preview(painter, exposed_rect);
  draw_drag_size_readout(painter);
  draw_text_rect_preview(painter);
  if ((tool_ == CanvasTool::Clone || tool_ == CanvasTool::Healing) && clone_source_set_) {
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
  draw_zoom_preview(painter);
  draw_rulers(painter);
  draw_brush_hover_outline(painter);
  draw_brush_adjust_overlay(painter);
  draw_processing_overlay(painter);
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

  render_cache_ = render_document_image_with_processing();
  quantize_image_for_palette_display(render_cache_);
  render_cache_dirty_ = false;
  ++render_cache_diagnostics_.full_refreshes;
  invalidate_display_mip_cache();
  refresh_curves_clipping_preview();
  if (layer_edit_target_ == LayerEditTarget::ComponentRed ||
      layer_edit_target_ == LayerEditTarget::ComponentGreen ||
      layer_edit_target_ == LayerEditTarget::ComponentBlue) {
    mask_display_image_ = QImage();
  }
}

QImage CanvasWidget::render_document_image_with_processing() {
  if (document_ == nullptr) {
    return {};
  }
  const auto canvas_area = static_cast<std::int64_t>(document_->width()) *
                           static_cast<std::int64_t>(document_->height());
  if (!processing_operation_active() && canvas_area < processing_overlay_dirty_area_threshold()) {
    return render_document_image();
  }

  auto* document = document_;
  auto future = std::async(std::launch::async, [document] {
    if (const auto delay = processing_render_test_delay_ms(); delay > 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    }
    return qimage_from_document(*document, true).convertToFormat(QImage::Format_RGBA8888);
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
  const auto generation = ++async_render_cache_generation_;
  auto* app = QApplication::instance();
  QPointer<CanvasWidget> widget(this);
  std::thread([app, widget, generation, snapshot_size, document_snapshot = std::move(document_snapshot)] {
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
          if (generation != widget->async_render_cache_generation_ || image == nullptr || image->isNull() ||
              widget->document_ == nullptr ||
              snapshot_size != QSize(widget->document_->width(), widget->document_->height())) {
            return;
          }
          widget->quantize_image_for_palette_display(*image);
          widget->render_cache_ = std::move(*image);
          widget->render_cache_dirty_ = false;
          ++widget->render_cache_diagnostics_.full_refreshes;
          widget->invalidate_display_mip_cache();
          widget->refresh_curves_clipping_preview();
          if (widget->layer_edit_target_ == LayerEditTarget::ComponentRed ||
              widget->layer_edit_target_ == LayerEditTarget::ComponentGreen ||
              widget->layer_edit_target_ == LayerEditTarget::ComponentBlue) {
            widget->mask_display_image_ = QImage();
          }
          if (widget->isVisible()) {
            widget->update();
          }
        },
        Qt::QueuedConnection);
  }).detach();
}

void CanvasWidget::cancel_async_render_cache_refresh() noexcept {
  ++async_render_cache_generation_;
  async_render_cache_pending_ = false;
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

  auto* document = document_;
  auto region = document_region;
  auto bounds = layer_bounds;
  auto future = std::async(std::launch::async, [document, region = std::move(region), bounds = std::move(bounds)] {
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
  const auto previous_wait_active = processing_render_wait_active_;
  processing_render_wait_active_ = true;
  const auto start_temporary_operation = allow_overlay && !processing_operation_active();
  if (start_temporary_operation) {
    begin_processing_operation();
  }
  while (!operation_ready()) {
    if (allow_overlay) {
      tick_processing_operation();
    } else {
      QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 16);
    }
  }
  if (start_temporary_operation) {
    end_processing_operation();
  }
  processing_render_wait_active_ = previous_wait_active;
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

  const auto palette_display = document_->palette_editing().has_value();
  QPainter painter(&render_cache_);
  painter.setCompositionMode(QPainter::CompositionMode_Source);
  int patched = 0;
  for (const auto& patch : patches) {
    if (patch.image.isNull()) {
      continue;
    }
    const auto document_rect = patch.document_rect.intersected(QRect(0, 0, document_->width(), document_->height()));
    if (document_rect.isEmpty() || patch.image.size() != patch.document_rect.size() ||
        document_rect != patch.document_rect) {
      continue;
    }
    if (palette_display) {
      QImage quantized = patch.image;
      quantize_image_for_palette_display(quantized);
      painter.drawImage(document_rect.topLeft(), quantized);
    } else {
      painter.drawImage(document_rect.topLeft(), patch.image);
    }
    ++patched;
  }
  if (patched <= 0) {
    return false;
  }
  render_cache_dirty_ = false;
  render_cache_diagnostics_.partial_patches += patched;
  invalidate_display_mip_cache();
  refresh_curves_clipping_preview();
  return true;
}

void CanvasWidget::invalidate_display_mip_cache() noexcept {
  display_mip_cache_.clear();
  display_mip_source_size_ = QSize();
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

  const auto hide_moving_layers = [this]() {
    std::vector<std::pair<Layer*, bool>> restore;
    restore.reserve(moving_layers_.size());
    for (const auto& moving_layer : moving_layers_) {
      if (auto* layer = document_->find_layer(moving_layer.id)) {
        restore.emplace_back(layer, layer->visible());
        layer->set_visible(false);
      }
    }
    return restore;
  };
  const auto restore_layers = [](const std::vector<std::pair<Layer*, bool>>& restore) {
    for (const auto& [layer, was_visible] : restore) {
      layer->set_visible(was_visible);
    }
  };

  const QRect canvas_rect(0, 0, document_->width(), document_->height());
  // Recompositing the whole document (with the moving layers hidden) is very
  // slow on heavy PSDs and caused a multi-second hitch at the start of a drag.
  // Instead reuse the already-composited render cache and only re-render the
  // small region the moving layers currently occupy with them hidden - the
  // moving layers only contribute within that region, so the rest of the cache
  // is already correct.
  if (render_cache_dirty_ || render_cache_.isNull() || render_cache_.size() != canvas_rect.size()) {
    const auto restore = hide_moving_layers();
    move_base_cache_ = render_document_image();
    restore_layers(restore);
    return;
  }

  QRegion old_region;
  for (const auto& moving_layer : moving_layers_) {
    auto* layer = document_->find_layer(moving_layer.id);
    if (layer == nullptr) {
      continue;
    }
    const auto rect =
        to_qrect(layer_bounds_with_effects(*layer, moving_layer.original_bounds)).intersected(canvas_rect);
    if (!rect.isEmpty()) {
      old_region += rect;
    }
  }

  QImage base = render_cache_.convertToFormat(QImage::Format_ARGB32_Premultiplied);
  if (!old_region.isEmpty()) {
    const auto restore = hide_moving_layers();
    QPainter painter(&base);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    for (const auto& rect : old_region) {
      const auto cleared = qimage_from_document_rect(*document_, rect, true);
      if (cleared.isNull()) {
        continue;
      }
      painter.drawImage(rect.topLeft(), cleared.convertToFormat(QImage::Format_ARGB32_Premultiplied));
    }
    painter.end();
    restore_layers(restore);
  }
  move_base_cache_ = std::move(base);
}

void CanvasWidget::clear_move_base_cache() noexcept {
  move_base_cache_ = QImage();
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

  const auto target_level = display_mip_level_for_zoom(zoom_);
  if (target_level <= 0) {
    return render_cache_;
  }

  while (static_cast<int>(display_mip_cache_.size()) < target_level) {
    const auto& previous = display_mip_cache_.empty() ? render_cache_ : display_mip_cache_.back();
    const QSize next_size(std::max(1, (previous.width() + 1) / 2),
                          std::max(1, (previous.height() + 1) / 2));
    if (next_size == previous.size()) {
      break;
    }
    display_mip_cache_.push_back(
        previous.scaled(next_size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(previous.format()));
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

  const auto target_level = display_mip_level_for_zoom(zoom_);
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
        previous.scaled(next_size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
            .convertToFormat(previous.format()));
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
  if (move_base_cache_.isNull() || zoom_ >= 1.0) {
    return move_base_cache_;
  }

  if (move_base_display_mip_cache_.empty() || move_base_display_mip_source_key_ != move_base_cache_.cacheKey()) {
    move_base_display_mip_cache_.clear();
    move_base_display_mip_source_key_ = move_base_cache_.cacheKey();
  }

  const auto target_level = display_mip_level_for_zoom(zoom_);
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
        previous.scaled(next_size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(previous.format()));
  }

  const auto level = std::min<int>(target_level, static_cast<int>(move_base_display_mip_cache_.size()));
  return level <= 0 ? move_base_cache_ : move_base_display_mip_cache_[level - 1];
}

QColor CanvasWidget::compose_document_pixel(std::int32_t x, std::int32_t y) const {
  std::array<float, 3> out = {0.0F, 0.0F, 0.0F};
  float out_alpha = 0.0F;
  if (document_ == nullptr) {
    return Qt::transparent;
  }

  for (const auto& layer : document_->layers()) {
    compose_layer_pixel(layer, x, y, out, out_alpha);
  }

  return QColor(clamp_byte(out[0]), clamp_byte(out[1]), clamp_byte(out[2]), clamp_byte(out_alpha * 255.0F));
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
  painter.setClipRect(QRectF(visible).intersected(rect));
  const auto start_y = aligned.y() + ((visible.y() - aligned.y()) / square) * square;
  const auto start_x = aligned.x() + ((visible.x() - aligned.x()) / square) * square;
  for (int y = start_y; y < visible.y() + visible.height(); y += square) {
    for (int x = start_x; x < visible.x() + visible.width(); x += square) {
      const bool dark = (((x - aligned.x()) / square) + ((y - aligned.y()) / square)) % 2 == 0;
      painter.fillRect(QRect(x, y, square, square), dark ? QColor(188, 188, 188) : QColor(236, 236, 236));
    }
  }
  painter.restore();
}

void CanvasWidget::draw_deep_zoom_image(QPainter& painter, const QImage& image, QRect exposed_rect) const {
  if (document_ == nullptr || image.isNull() || !uses_deep_zoom_pixel_renderer(zoom_)) {
    return;
  }

  const QRect target_rect(widget_position(QPoint(0, 0)),
                          QSize(widget_position(QPoint(document_->width(), document_->height())).x() -
                                    widget_position(QPoint(0, 0)).x(),
                                widget_position(QPoint(document_->width(), document_->height())).y() -
                                    widget_position(QPoint(0, 0)).y()));
  const auto visible_target = target_rect.intersected(exposed_rect);
  if (visible_target.isEmpty()) {
    return;
  }

  const auto source_left = std::clamp(
      static_cast<int>(std::floor((static_cast<double>(visible_target.left()) - pan_.x()) / zoom_)) - 1,
      0, image.width());
  const auto source_top = std::clamp(
      static_cast<int>(std::floor((static_cast<double>(visible_target.top()) - pan_.y()) / zoom_)) - 1,
      0, image.height());
  const auto source_right = std::clamp(
      static_cast<int>(std::ceil((static_cast<double>(visible_target.right() + 1) - pan_.x()) / zoom_)) + 1,
      0, image.width());
  const auto source_bottom = std::clamp(
      static_cast<int>(std::ceil((static_cast<double>(visible_target.bottom() + 1) - pan_.y()) / zoom_)) + 1,
      0, image.height());
  if (source_left >= source_right || source_top >= source_bottom) {
    return;
  }

  painter.save();
  painter.setClipRect(visible_target);
  painter.setRenderHint(QPainter::Antialiasing, false);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
  for (int y = source_top; y < source_bottom; ++y) {
    const auto top = widget_position(QPoint(0, y)).y();
    const auto bottom = widget_position(QPoint(0, y + 1)).y();
    if (bottom <= top) {
      continue;
    }
    for (int x = source_left; x < source_right; ++x) {
      const auto color = image.pixelColor(x, y);
      if (color.alpha() == 0) {
        continue;
      }
      const auto left = widget_position(QPoint(x, 0)).x();
      const auto right = widget_position(QPoint(x + 1, 0)).x();
      if (right <= left) {
        continue;
      }
      painter.fillRect(QRect(left, top, right - left, bottom - top), color);
    }
  }
  painter.restore();
}

void CanvasWidget::draw_grid_overlay(QPainter& painter, const QRectF& target_rect, QRect exposed_rect) const {
  if (!grid_visible_ || document_ == nullptr || target_rect.isEmpty()) {
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
  const bool deep_pixel_grid = uses_deep_zoom_pixel_renderer(zoom_);
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
  if ((!guides_visible_ && !dragging_guide_) || document_ == nullptr) {
    return;
  }

  painter.save();
  const auto raw_top_left = widget_position_f(QPointF(0.0, 0.0));
  const auto raw_bottom_right = widget_position_f(QPointF(document_->width(), document_->height()));
  const QPointF top_left(pixel_aligned_coordinate(raw_top_left.x(), zoom_),
                         pixel_aligned_coordinate(raw_top_left.y(), zoom_));
  const QPointF bottom_right(pixel_aligned_coordinate(raw_bottom_right.x(), zoom_),
                             pixel_aligned_coordinate(raw_bottom_right.y(), zoom_));
  auto draw_guide = [this, &painter, top_left, bottom_right](GuideOrientation orientation,
                                                            std::int32_t position_32, bool selected,
                                                            bool transient, bool remove) {
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
      painter.drawLine(QPointF(x, top_left.y()), QPointF(x, bottom_right.y()));
    } else {
      const auto y = pixel_aligned_coordinate(widget_position_f(QPointF(0.0, pixels)).y(), zoom_);
      painter.drawLine(QPointF(top_left.x(), y), QPointF(bottom_right.x(), y));
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
  painter.fillRect(QRect(0, 0, width(), kTopRulerHeight), QColor(42, 45, 49));
  painter.fillRect(QRect(0, 0, kLeftRulerWidth, height()), QColor(42, 45, 49));
  painter.fillRect(QRect(0, 0, kLeftRulerWidth, kTopRulerHeight), QColor(35, 38, 42));
  painter.setPen(QColor(78, 82, 90));
  painter.drawLine(0, kTopRulerHeight - 1, width(), kTopRulerHeight - 1);
  painter.drawLine(kLeftRulerWidth - 1, 0, kLeftRulerWidth - 1, height());

  QFont label_font = font();
  label_font.setPointSize(std::max(7, label_font.pointSize() - 2));
  painter.setFont(label_font);
  const QFontMetrics metrics(label_font);
  QPen tick_pen(QColor(185, 190, 198));
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

void CanvasWidget::draw_processing_overlay(QPainter& painter) const {
  if (!processing_overlay_visible_) {
    return;
  }

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
  const auto desired_panel_width = label_metrics.horizontalAdvance(processing_overlay_message_) +
                                   kSpinnerColumnWidth + kTextRightPadding;
  const QSize panel_size(std::min(max_panel_width, std::max(min_panel_width, desired_panel_width)), kPanelHeight);
  const auto desired_top = (rulers_visible_ ? kTopRulerHeight : 0) + kPanelMargin;
  const auto max_top = std::max(kPanelMargin, height() - panel_size.height() - kPanelMargin);
  const QRect panel_rect(QPoint((width() - panel_size.width()) / 2, std::min(desired_top, max_top)),
                         panel_size);
  QPainterPath panel_path;
  panel_path.addRoundedRect(QRectF(panel_rect), 8.0, 8.0);
  painter.fillPath(panel_path, QColor(31, 35, 41, 236));
  painter.setPen(QPen(QColor(78, 86, 96), 1));
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
    QPen pen(QColor(238, 244, 250, std::clamp(alpha, 55, 220)), 2.8, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(pen);
    painter.drawLine(inner, outer);
  }

  painter.setFont(label_font);
  painter.setPen(QColor(238, 242, 247));
  const QRect text_rect(panel_rect.left() + kSpinnerColumnWidth, panel_rect.top(),
                        std::max(0, panel_rect.width() - kSpinnerColumnWidth - kTextRightPadding),
                        panel_rect.height());
  painter.drawText(text_rect, Qt::AlignVCenter | Qt::AlignLeft,
                   label_metrics.elidedText(processing_overlay_message_, Qt::ElideRight, text_rect.width()));
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
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 16);
}

void CanvasWidget::hide_processing_overlay() {
  if (!processing_overlay_visible_) {
    return;
  }
  processing_overlay_visible_ = false;
  processing_overlay_message_.clear();
  processing_animation_timer_.stop();
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
      (layer_edit_target_ == LayerEditTarget::ComponentRed ||
       layer_edit_target_ == LayerEditTarget::ComponentGreen ||
       layer_edit_target_ == LayerEditTarget::ComponentBlue);
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
  if (!quick_mask_active_ && !component_preview && !smart_filter_mask_target && channel == nullptr &&
      (layer == nullptr || !layer->mask().has_value())) {
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
  } else if (layer_edit_target_ != LayerEditTarget::ComponentRed &&
             layer_edit_target_ != LayerEditTarget::ComponentGreen &&
             layer_edit_target_ != LayerEditTarget::ComponentBlue) {
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
