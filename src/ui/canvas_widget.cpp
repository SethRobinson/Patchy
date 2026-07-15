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

QImage qimage_from_flat_composite_pixels(const PixelBuffer& pixels) {
  if (pixels.empty() || pixels.format().bit_depth != BitDepth::UInt8 || pixels.format().channels < 3) {
    return {};
  }

  QImage image(pixels.width(), pixels.height(), QImage::Format_RGBA8888);
  const auto channels = pixels.format().channels;
  const auto source_stride = pixels.stride_bytes();
  const auto* source_bytes = pixels.data().data();
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    const auto* source_row = source_bytes + static_cast<std::size_t>(y) * source_stride;
    auto* target_row = image.scanLine(y);
    if (channels >= 4) {
      std::memcpy(target_row, source_row, static_cast<std::size_t>(pixels.width()) * 4U);
      continue;
    }
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto* src = source_row + static_cast<std::size_t>(x) * channels;
      auto* dst = target_row + static_cast<std::size_t>(x) * 4U;
      dst[0] = src[0];
      dst[1] = src[1];
      dst[2] = src[2];
      dst[3] = 255;
    }
  }
  return image;
}

// The hit-test walks must not count as mutations: descending through the
// non-const Layer::children() accessor bumps every visited group's revisions
// on every Move-tool hover and press, silently invalidating the revision-keyed
// thumbnail and style-mask caches (same bug class as Document::find_layer_recursive).
// Walk const here; the topmost_*_at wrappers cast the hit.
const Layer* topmost_pixel_layer_at_recursive(const std::vector<Layer>& layers, QPoint document_point,
                                              bool require_visible_pixel, bool skip_locked,
                                              LayerLockFlags ancestor_lock_flags = kLayerLockNone) {
  for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
    const auto& layer = *it;
    if (!layer.visible() || layer.opacity() <= 0.0F) {
      continue;
    }
    const auto effective_lock_flags = ancestor_lock_flags | patchy::layer_lock_flags(layer);
    if (layer.kind() == LayerKind::Group) {
      if (const auto* found =
              topmost_pixel_layer_at_recursive(layer.children(), document_point, require_visible_pixel, skip_locked,
                                               effective_lock_flags);
          found != nullptr) {
        return found;
      }
      continue;
    }
    if (skip_locked && (effective_lock_flags & kLayerLockImagePixels) != kLayerLockNone) {
      continue;
    }
    if (pixel_layer_contains_document_point(layer, document_point, require_visible_pixel)) {
      return &layer;
    }
  }
  return nullptr;
}

const Layer* topmost_move_layer_at_recursive(const std::vector<Layer>& layers, QPoint document_point, bool skip_locked,
                                             LayerLockFlags ancestor_lock_flags = kLayerLockNone) {
  for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
    const auto& layer = *it;
    if (!layer.visible() || layer.opacity() <= 0.0F) {
      continue;
    }
    const auto effective_lock_flags = ancestor_lock_flags | patchy::layer_lock_flags(layer);
    if (layer.kind() == LayerKind::Group) {
      if (const auto* found = topmost_move_layer_at_recursive(layer.children(), document_point, skip_locked,
                                                              effective_lock_flags);
          found != nullptr) {
        return found;
      }
      continue;
    }
    if (skip_locked && (effective_lock_flags & kLayerLockPosition) != kLayerLockNone) {
      continue;
    }
    if (move_layer_contains_document_point(layer, document_point)) {
      return &layer;
    }
  }
  return nullptr;
}

const Layer* topmost_text_layer_at_recursive(const std::vector<Layer>& layers, QPoint document_point) {
  for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
    const auto& layer = *it;
    if (!layer.visible()) {
      continue;
    }
    if (layer.kind() == LayerKind::Group) {
      if (const auto* found = topmost_text_layer_at_recursive(layer.children(), document_point); found != nullptr) {
        return found;
      }
      continue;
    }
    if (layer_is_text(layer) && layer.bounds().contains(document_point.x(), document_point.y())) {
      return &layer;
    }
  }
  return nullptr;
}

bool expand_mask_to_include_rect(LayerMask& mask, QRect document_rect, QSize canvas_size) {
  document_rect = document_rect.normalized().intersected(QRect(QPoint(), canvas_size));
  if (document_rect.isEmpty()) {
    return false;
  }

  const auto current = QRect(mask.bounds.x, mask.bounds.y, mask.bounds.width, mask.bounds.height);
  if (!mask.pixels.empty() && current.contains(document_rect)) {
    return true;
  }

  const auto expanded = (mask.pixels.empty() ? document_rect : current.united(document_rect))
                            .intersected(QRect(QPoint(), canvas_size));
  if (expanded.isEmpty()) {
    return false;
  }

  PixelBuffer next(expanded.width(), expanded.height(), PixelFormat::gray8());
  next.clear(mask.default_color);
  if (!mask.pixels.empty() && mask.pixels.format() == PixelFormat::gray8()) {
    const auto copy_rect = current.intersected(expanded);
    for (int y = copy_rect.top(); y <= copy_rect.bottom(); ++y) {
      for (int x = copy_rect.left(); x <= copy_rect.right(); ++x) {
        *next.pixel(x - expanded.x(), y - expanded.y()) = *mask.pixels.pixel(x - current.x(), y - current.y());
      }
    }
  }

  mask.bounds = Rect{expanded.x(), expanded.y(), expanded.width(), expanded.height()};
  mask.pixels = std::move(next);
  return true;
}

}  // namespace

CanvasWidget::CanvasWidget(QWidget* parent) : QWidget(parent) {
  setAutoFillBackground(false);
  setMouseTracking(true);
  setTabletTracking(true);
  setFocusPolicy(Qt::StrongFocus);
  // Watch the whole app for modifier-key changes so the selection cursor badge
  // updates the instant Shift/Alt change even when the canvas does not hold
  // keyboard focus (the key events otherwise go to whichever panel has focus).
  qApp->installEventFilter(this);
  selection_timer_.start(120, this);
  pen_proximity_clock_.start();
}

void CanvasWidget::set_document(Document* document) {
  set_document_internal(document, /*preserve_frame_for_same_size=*/false);
}

void CanvasWidget::set_document_for_history_restore(Document* document, bool normal_composite_unchanged) {
  set_document_internal(document, /*preserve_frame_for_same_size=*/true, normal_composite_unchanged);
}

void CanvasWidget::set_document_internal(Document* document, bool preserve_frame_for_same_size,
                                         bool normal_composite_unchanged) {
  clear_transient_read_interaction();
  curves_clipping_mode_.reset();
  curves_clipping_channel_.reset();
  curves_clipping_preview_image_ = QImage();
  curves_clipping_display_mip_cache_.clear();
  curves_clipping_display_mip_source_key_ = 0;
  const auto old_transform_controls_rect = move_transform_controls_rect();
  const bool render_cache_was_dirty = render_cache_dirty_;
  const bool preserve_frame = preserve_frame_for_same_size && document != nullptr && !render_cache_.isNull() &&
                              render_cache_.size() == QSize(document->width(), document->height());
  const bool preserve_quick_mask =
      preserve_frame_for_same_size && quick_mask_active_ && document != nullptr &&
      document_ != nullptr && document_->width() == document->width() &&
      document_->height() == document->height();
  const bool quick_mask_was_cleared = quick_mask_active_ && !preserve_quick_mask;
  if (quick_mask_was_cleared) {
    quick_mask_active_ = false;
    quick_mask_pixels_ = {};
    primary_color_ = quick_mask_saved_primary_;
    secondary_color_ = quick_mask_saved_secondary_;
    quick_mask_edit_before_.reset();
    quick_mask_edit_label_.clear();
    quick_mask_edit_dirty_ = QRegion();
  }
  // A Smart Filter mask edit buffer is tied to a layer instance in the old
  // document. History restoration installs a different Document object, so the
  // host must explicitly resync the target after it has resolved that owner.
  smart_filter_mask_pixels_ = {};
  smart_filter_mask_owner_id_ = 0;
  smart_filter_mask_edit_before_.reset();
  smart_filter_mask_edit_label_.clear();
  smart_filter_mask_edit_dirty_ = QRegion();
  const auto restore_channel_id =
      preserve_frame_for_same_size && layer_edit_target_ == LayerEditTarget::DocumentChannel
          ? active_document_channel_id_
          : ChannelId{0};
  const auto restore_component_target =
      preserve_frame_for_same_size &&
              (layer_edit_target_ == LayerEditTarget::ComponentRed ||
               layer_edit_target_ == LayerEditTarget::ComponentGreen ||
               layer_edit_target_ == LayerEditTarget::ComponentBlue)
          ? layer_edit_target_
          : LayerEditTarget::Content;
  const auto restore_channel_display_mode = mask_display_mode_;
  cancel_free_transform();
  if (warping_layer_) {
    reset_warp_state();
  }
  move_drag_pending_ = false;
  moving_layer_ = false;
  moving_layers_.clear();
  move_preview_delta_ = QPoint();
  move_preview_patches_.clear();
  move_preview_patches_delta_.reset();
  moving_layers_use_outline_preview_ = false;
  clear_move_base_cache();
  document_ = document;
  set_move_transform_controls_layer(std::nullopt);
  selected_guide_index_ = -1;
  dragging_guide_ = false;
  creating_guide_ = false;
  guide_drag_remove_ = false;
  layer_edit_target_ = LayerEditTarget::Content;
  active_document_channel_id_ = 0;
  mask_display_mode_ = MaskDisplayMode::None;
  mask_display_image_ = QImage();
  mask_display_image_layer_ = 0;
  mask_display_image_channel_ = 0;
  mask_display_image_revision_ = 0;
  if (restore_channel_id != 0 && document_ != nullptr &&
      static_cast<const Document*>(document_)->find_channel(restore_channel_id) != nullptr) {
    layer_edit_target_ = LayerEditTarget::DocumentChannel;
    active_document_channel_id_ = restore_channel_id;
    mask_display_mode_ = restore_channel_display_mode == MaskDisplayMode::None
                             ? MaskDisplayMode::Grayscale
                             : restore_channel_display_mode;
  } else if (restore_component_target != LayerEditTarget::Content) {
    layer_edit_target_ = restore_component_target;
    mask_display_mode_ = MaskDisplayMode::Grayscale;
  }
  if (!preserve_frame) {
    render_cache_ = QImage();
    render_cache_diagnostics_ = {};
  }
  const bool keep_normal_composite_cache = preserve_frame && normal_composite_unchanged;
  render_cache_dirty_ = keep_normal_composite_cache ? render_cache_was_dirty : true;
  if (!preserve_frame && document_ != nullptr && document_->metadata().psd_flat_composite.has_value()) {
    const auto& flat_composite = *document_->metadata().psd_flat_composite;
    // RGB-only PSD compatibility composites cannot preserve transparency, so
    // using them as a cache seed hides checkerboards until the next refresh.
    if (flat_composite.format().channels >= 4 && flat_composite.width() == document_->width() &&
        flat_composite.height() == document_->height()) {
      render_cache_ = qimage_from_flat_composite_pixels(flat_composite);
      quantize_image_for_palette_display(render_cache_);
      render_cache_dirty_ = render_cache_.isNull();
    }
    document_->metadata().psd_flat_composite.reset();
  }
  if (!keep_normal_composite_cache) {
    invalidate_display_mip_cache();
  }
  clear_move_hover_outline();
  update_move_transform_controls_dirty(old_transform_controls_rect);
  smudge_state_ = {};
  cancel_quick_select_stroke();
  reset_axis_constrained_stroke();
  last_stroke_end_document_.reset();
  if (brush_adjust_dragging_) {
    end_brush_adjust_drag(false);
  }
  if (isVisible()) {
    constrain_pan();
  }
  update();
  if (quick_mask_was_cleared && quick_mask_changed_callback_) {
    quick_mask_changed_callback_();
  }
}

void CanvasWidget::set_tool(CanvasTool tool) {
  const auto tool_changed = tool_ != tool;
  const auto old_transform_controls_rect = move_transform_controls_rect();
  if (tool_changed) {
    cancel_magnetic_lasso();
    finish_free_transform();
    finish_warp_transform();
    move_drag_pending_ = false;
    moving_layer_ = false;
    moving_layers_.clear();
    move_preview_delta_ = QPoint();
    move_preview_patches_.clear();
    move_preview_patches_delta_.reset();
    moving_layers_use_outline_preview_ = false;
    clear_move_base_cache();
    set_move_transform_controls_layer(std::nullopt);
    clear_move_hover_outline();
  }
  tool_ = tool;
  // Each selection tool keeps its own combine mode; surface this tool's stored
  // mode so the cursor badge and Options bar follow when switching tools.
  if (const auto index = selection_tool_index(tool_); index >= 0) {
    selection_mode_ = selection_modes_per_tool_[static_cast<std::size_t>(index)];
  }
  update_tool_cursor();
  if (tool_changed) {
    update_move_transform_controls_dirty(old_transform_controls_rect);
    update();
    notify_transform_controls_changed();
    notify_selection_mode_changed();
  }
}

CanvasTool CanvasWidget::tool() const noexcept {
  return tool_;
}

void CanvasWidget::set_edit_locked(bool locked) noexcept {
  if (edit_locked_ == locked) {
    return;
  }
  edit_locked_ = locked;
  if (edit_locked_) {
    clear_move_hover_outline();
    move_drag_pending_ = false;
    moving_layer_ = false;
    moving_layers_.clear();
    move_preview_delta_ = QPoint();
    move_preview_patches_.clear();
    move_preview_patches_delta_.reset();
    moving_layers_use_outline_preview_ = false;
    clear_move_base_cache();
    dragging_text_rect_ = false;
    selecting_ = false;
    lassoing_ = false;
    cancel_magnetic_lasso();
    cancel_quick_select_stroke();
    moving_selection_ = false;
    drawing_shape_ = false;
    dragging_guide_ = false;
    creating_guide_ = false;
    guide_drag_remove_ = false;
    reset_axis_constrained_stroke();
  }
  update_tool_cursor();
  update();
}

bool CanvasWidget::edit_locked() const noexcept {
  return edit_locked_;
}

void CanvasWidget::set_layer_edit_target(LayerEditTarget target) noexcept {
  if (layer_edit_target_ == target) {
    return;
  }
  if (layer_edit_target_ == LayerEditTarget::SmartFilterMask &&
      target != LayerEditTarget::SmartFilterMask) {
    // Generic layer/channel switching is an exit path. Pending mask pixels are
    // temporary and must not leak onto a subsequently selected layer.
    smart_filter_mask_pixels_ = {};
    smart_filter_mask_owner_id_ = 0;
    smart_filter_mask_edit_before_.reset();
    smart_filter_mask_edit_label_.clear();
    smart_filter_mask_edit_dirty_ = QRegion();
    ++smart_filter_mask_revision_;
    mask_display_mode_ = MaskDisplayMode::None;
    mask_display_image_ = QImage();
    mask_display_image_layer_ = 0;
    mask_display_image_channel_ = 0;
    mask_display_image_revision_ = 0;
  }
  layer_edit_target_ = target;
  if (target != LayerEditTarget::DocumentChannel) {
    active_document_channel_id_ = 0;
  }
  if (target == LayerEditTarget::ComponentRed || target == LayerEditTarget::ComponentGreen ||
      target == LayerEditTarget::ComponentBlue) {
    mask_display_mode_ = MaskDisplayMode::Grayscale;
    mask_display_image_ = QImage();
    mask_display_image_layer_ = 0;
    mask_display_image_channel_ = 0;
    mask_display_image_revision_ = 0;
  }
  clear_brush_stroke_tracking();
  update_tool_cursor();
  update();
}

CanvasWidget::LayerEditTarget CanvasWidget::layer_edit_target() const noexcept {
  return layer_edit_target_;
}

void CanvasWidget::set_document_channel_edit_target(ChannelId id, MaskDisplayMode mode) {
  const auto* channel = document_ != nullptr ? static_cast<const Document*>(document_)->find_channel(id) : nullptr;
  if (channel == nullptr) {
    set_layer_edit_target(LayerEditTarget::Content);
    set_mask_display_mode(MaskDisplayMode::None);
    return;
  }
  if (layer_edit_target_ == LayerEditTarget::SmartFilterMask) {
    clear_smart_filter_mask_edit_target();
  }
  layer_edit_target_ = LayerEditTarget::DocumentChannel;
  active_document_channel_id_ = id;
  mask_display_mode_ = mode == MaskDisplayMode::None ? MaskDisplayMode::Grayscale : mode;
  mask_display_image_ = QImage();
  mask_display_image_layer_ = 0;
  mask_display_image_channel_ = 0;
  mask_display_image_revision_ = 0;
  clear_brush_stroke_tracking();
  update_tool_cursor();
  update();
}

void CanvasWidget::set_component_channel_preview(LayerEditTarget component) {
  if (component != LayerEditTarget::ComponentRed && component != LayerEditTarget::ComponentGreen &&
      component != LayerEditTarget::ComponentBlue) {
    set_layer_edit_target(LayerEditTarget::Content);
    set_mask_display_mode(MaskDisplayMode::None);
    return;
  }
  set_layer_edit_target(component);
}

std::optional<ChannelId> CanvasWidget::active_document_channel_id() const noexcept {
  if (layer_edit_target_ != LayerEditTarget::DocumentChannel || active_document_channel_id_ == 0) {
    return std::nullopt;
  }
  return active_document_channel_id_;
}

bool CanvasWidget::editing_document_channel() const noexcept {
  return layer_edit_target_ == LayerEditTarget::DocumentChannel && active_document_channel_const() != nullptr;
}

bool CanvasWidget::document_channel_is_editable() const noexcept {
  const auto* channel = active_document_channel_const();
  return channel != nullptr && channel->kind() == DocumentChannelKind::Alpha;
}

void CanvasWidget::set_mask_display_mode(MaskDisplayMode mode) {
  if (mask_display_mode_ == mode) {
    return;
  }
  mask_display_mode_ = mode;
  mask_display_image_ = QImage();
  mask_display_image_layer_ = 0;
  mask_display_image_channel_ = 0;
  mask_display_image_revision_ = 0;
  update();
}

CanvasWidget::MaskDisplayMode CanvasWidget::mask_display_mode() const noexcept {
  return mask_display_mode_;
}

void CanvasWidget::invalidate_mask_display() {
  mask_display_image_ = QImage();
  mask_display_image_layer_ = 0;
  mask_display_image_channel_ = 0;
  mask_display_image_revision_ = 0;
  if (mask_display_mode_ != MaskDisplayMode::None) {
    update();
  }
}

void CanvasWidget::set_auto_select_layer(bool enabled) noexcept {
  auto_select_layer_ = enabled;
  clear_move_hover_outline();
}

bool CanvasWidget::auto_select_layer() const noexcept {
  return auto_select_layer_;
}

void CanvasWidget::set_primary_color(QColor color) {
  if (color.alpha() == 0) {
    color.setAlpha(255);
  }
  if (const auto* snap = editing_grayscale_target() ? nullptr : palette_snap_context();
      snap != nullptr) {
    const auto snapped = snap->lut->snap(static_cast<std::uint8_t>(color.red()),
                                         static_cast<std::uint8_t>(color.green()),
                                         static_cast<std::uint8_t>(color.blue()));
    color = QColor(snapped.red, snapped.green, snapped.blue, color.alpha());
  }
  primary_color_ = color;
}

QColor CanvasWidget::primary_color() const noexcept {
  return primary_color_;
}

void CanvasWidget::set_secondary_color(QColor color) {
  if (color.alpha() == 0) {
    color.setAlpha(255);
  }
  if (const auto* snap = editing_grayscale_target() ? nullptr : palette_snap_context();
      snap != nullptr) {
    const auto snapped = snap->lut->snap(static_cast<std::uint8_t>(color.red()),
                                         static_cast<std::uint8_t>(color.green()),
                                         static_cast<std::uint8_t>(color.blue()));
    color = QColor(snapped.red, snapped.green, snapped.blue, color.alpha());
  }
  secondary_color_ = color;
}

QColor CanvasWidget::secondary_color() const noexcept {
  return secondary_color_;
}

void CanvasWidget::set_gradient_method(GradientMethod method) noexcept {
  gradient_method_ = method;
}

GradientMethod CanvasWidget::gradient_method() const noexcept {
  return gradient_method_;
}

void CanvasWidget::set_gradient_reverse(bool reverse) noexcept {
  gradient_reverse_ = reverse;
}

bool CanvasWidget::gradient_reverse() const noexcept {
  return gradient_reverse_;
}

void CanvasWidget::set_gradient_opacity(int opacity) noexcept {
  gradient_opacity_ = std::clamp(opacity, 0, 100);
}

int CanvasWidget::gradient_opacity() const noexcept {
  return gradient_opacity_;
}

void CanvasWidget::set_gradient_stops(std::optional<std::vector<GradientStop>> stops) {
  if (stops.has_value()) {
    auto normalized = normalized_gradient_stops(*stops);
    if (normalized.size() < 2U) {
      normalized = effective_gradient_stops();
    }
    gradient_stops_ = std::move(normalized);
  } else {
    gradient_stops_.reset();
  }
}

const std::optional<std::vector<GradientStop>>& CanvasWidget::gradient_stops() const noexcept {
  return gradient_stops_;
}

std::vector<GradientStop> CanvasWidget::effective_gradient_stops() const {
  if (gradient_stops_.has_value() && gradient_stops_->size() >= 2U) {
    return normalized_gradient_stops(*gradient_stops_);
  }
  auto primary = edit_color(primary_color_);
  primary.a = 255;
  auto secondary = edit_color(secondary_color_);
  secondary.a = 255;
  return normalized_gradient_stops({GradientStop{0.0F, primary}, GradientStop{1.0F, secondary}});
}

void CanvasWidget::set_clone_aligned(bool aligned) noexcept {
  if (clone_aligned_ == aligned) {
    return;
  }
  clone_aligned_ = aligned;
  clone_aligned_offset_set_ = false;
}

bool CanvasWidget::clone_aligned() const noexcept {
  return clone_aligned_;
}

void CanvasWidget::set_healing_diffusion(int diffusion) noexcept {
  healing_diffusion_ = std::clamp(diffusion, 1, 7);
}

int CanvasWidget::healing_diffusion() const noexcept {
  return healing_diffusion_;
}

void CanvasWidget::set_show_transform_controls(bool enabled) noexcept {
  const auto old_transform_controls_rect = move_transform_controls_rect();
  show_transform_controls_ = enabled;
  if (!enabled) {
    cancel_free_transform();
    set_move_transform_controls_layer(std::nullopt);
  }
  update_move_transform_controls_dirty(old_transform_controls_rect);
  clear_move_hover_outline();
  notify_transform_controls_changed();
}

bool CanvasWidget::show_transform_controls() const noexcept {
  return show_transform_controls_;
}

std::optional<QRect> CanvasWidget::active_layer_document_rect() const noexcept {
  const auto* layer = active_pixel_layer();
  if (layer == nullptr) {
    return std::nullopt;
  }
  return to_qrect(layer->bounds());
}

bool CanvasWidget::smart_filter_mask_pixels_are_valid(const PixelBuffer& pixels) const noexcept {
  return document_ != nullptr && pixels.format() == PixelFormat::gray8() &&
         pixels.width() == document_->width() && pixels.height() == document_->height();
}

bool CanvasWidget::set_smart_filter_mask_edit_target(LayerId owner_id, PixelBuffer pixels,
                                                     MaskDisplayMode mode) {
  const auto* owner = document_ != nullptr && owner_id != 0
                          ? static_cast<const Document*>(document_)->find_layer(owner_id)
                          : nullptr;
  if (quick_mask_active_ || owner == nullptr || !smart_filter_mask_pixels_are_valid(pixels)) {
    return false;
  }

  cancel_smart_filter_mask_edit();
  smart_filter_mask_owner_id_ = owner_id;
  smart_filter_mask_pixels_ = std::move(pixels);
  ++smart_filter_mask_revision_;
  smart_filter_mask_edit_before_.reset();
  smart_filter_mask_edit_label_.clear();
  smart_filter_mask_edit_dirty_ = QRegion();
  layer_edit_target_ = LayerEditTarget::SmartFilterMask;
  active_document_channel_id_ = 0;
  mask_display_mode_ = mode;
  invalidate_mask_display();
  clear_brush_stroke_tracking();
  update_tool_cursor();
  update();
  return true;
}

bool CanvasWidget::resync_smart_filter_mask_edit_target(LayerId owner_id, PixelBuffer pixels) {
  const auto mode = layer_edit_target_ == LayerEditTarget::SmartFilterMask
                        ? mask_display_mode_
                        : MaskDisplayMode::Overlay;
  return set_smart_filter_mask_edit_target(owner_id, std::move(pixels), mode);
}

void CanvasWidget::clear_smart_filter_mask_edit_target() {
  if (layer_edit_target_ != LayerEditTarget::SmartFilterMask &&
      smart_filter_mask_owner_id_ == 0 && smart_filter_mask_pixels_.empty()) {
    return;
  }

  cancel_smart_filter_mask_edit();
  if (painting_) {
    painting_ = false;
    reset_brush_smoothing();
    reset_axis_constrained_stroke();
  }
  if (drawing_shape_) {
    drawing_shape_ = false;
  }
  clear_brush_stroke_tracking();
  smart_filter_mask_pixels_ = {};
  smart_filter_mask_owner_id_ = 0;
  smart_filter_mask_edit_before_.reset();
  smart_filter_mask_edit_label_.clear();
  smart_filter_mask_edit_dirty_ = QRegion();
  ++smart_filter_mask_revision_;
  if (layer_edit_target_ == LayerEditTarget::SmartFilterMask) {
    layer_edit_target_ = LayerEditTarget::Content;
  }
  mask_display_mode_ = MaskDisplayMode::None;
  invalidate_mask_display();
  update_tool_cursor();
  update();
}

bool CanvasWidget::editing_smart_filter_mask() const noexcept {
  if (document_ == nullptr || layer_edit_target_ != LayerEditTarget::SmartFilterMask ||
      smart_filter_mask_owner_id_ == 0 || !smart_filter_mask_pixels_are_valid(smart_filter_mask_pixels_)) {
    return false;
  }
  return static_cast<const Document*>(document_)->find_layer(smart_filter_mask_owner_id_) != nullptr;
}

std::optional<LayerId> CanvasWidget::smart_filter_mask_owner_id() const noexcept {
  return editing_smart_filter_mask() ? std::optional<LayerId>{smart_filter_mask_owner_id_}
                                     : std::nullopt;
}

const PixelBuffer& CanvasWidget::smart_filter_mask_pixels() const noexcept {
  return smart_filter_mask_pixels_;
}

std::uint64_t CanvasWidget::smart_filter_mask_revision() const noexcept {
  return smart_filter_mask_revision_;
}

QRect CanvasWidget::fill_smart_filter_mask(QColor color, QString history_label) {
  if (!editing_smart_filter_mask() || !begin_edit(std::move(history_label))) {
    return {};
  }
  const auto dirty = fill_active_layer_mask(color);
  active_edit_target_changed_impl(QRegion(dirty), DocumentChangeReason::Immediate);
  return dirty;
}

QRect CanvasWidget::invert_smart_filter_mask(QString history_label) {
  if (!editing_smart_filter_mask() || !begin_edit(std::move(history_label))) {
    return {};
  }
  auto bytes = smart_filter_mask_pixels_.data();
  for (auto& value : bytes) {
    value = static_cast<std::uint8_t>(255U - value);
  }
  const auto dirty = QRect(0, 0, document_->width(), document_->height());
  active_edit_target_changed_impl(QRegion(dirty), DocumentChangeReason::Immediate);
  return dirty;
}

void CanvasWidget::finish_smart_filter_mask_edit() {
  if (!smart_filter_mask_edit_before_.has_value()) {
    return;
  }
  auto before = std::move(*smart_filter_mask_edit_before_);
  const auto owner_id = smart_filter_mask_owner_id_;
  auto label = std::move(smart_filter_mask_edit_label_);
  auto pixels = smart_filter_mask_pixels_;
  auto dirty = smart_filter_mask_edit_dirty_;
  smart_filter_mask_edit_before_.reset();
  smart_filter_mask_edit_label_.clear();
  smart_filter_mask_edit_dirty_ = QRegion();

  const auto* owner = document_ != nullptr && owner_id != 0
                          ? static_cast<const Document*>(document_)->find_layer(owner_id)
                          : nullptr;
  bool committed = dirty.isEmpty();
  if (owner != nullptr && !dirty.isEmpty() && smart_filter_mask_committed_callback_) {
    committed = smart_filter_mask_committed_callback_(owner_id, std::move(label), std::move(pixels),
                                                       std::move(dirty));
  }
  if (!committed && layer_edit_target_ == LayerEditTarget::SmartFilterMask &&
      smart_filter_mask_owner_id_ == owner_id) {
    // Native FEid regeneration is fail-closed. Keep the temporary canvas in
    // lockstep with the last accepted model state when the host rejects a
    // gesture instead of leaving an uncommitted mask visible.
    smart_filter_mask_pixels_ = std::move(before);
    ++smart_filter_mask_revision_;
    invalidate_mask_display();
    update();
  }
}

void CanvasWidget::cancel_smart_filter_mask_edit() {
  if (!smart_filter_mask_edit_before_.has_value()) {
    return;
  }
  smart_filter_mask_pixels_ = std::move(*smart_filter_mask_edit_before_);
  smart_filter_mask_edit_before_.reset();
  smart_filter_mask_edit_label_.clear();
  smart_filter_mask_edit_dirty_ = QRegion();
  ++smart_filter_mask_revision_;
  invalidate_mask_display();
  update();
}

void CanvasWidget::set_before_edit_callback(std::function<void(QString)> callback) {
  before_edit_callback_ = std::move(callback);
}

void CanvasWidget::set_selection_history_callback(std::function<void(QString, SelectionSnapshot, bool)> callback) {
  selection_history_callback_ = std::move(callback);
}

void CanvasWidget::set_quick_mask_changed_callback(
    std::function<void()> callback) {
  quick_mask_changed_callback_ = std::move(callback);
}

void CanvasWidget::set_smart_filter_mask_committed_callback(
    std::function<bool(LayerId, QString, PixelBuffer, QRegion)> callback) {
  smart_filter_mask_committed_callback_ = std::move(callback);
}

void CanvasWidget::set_selection_mode_changed_callback(std::function<void(SelectionMode)> callback) {
  selection_mode_changed_callback_ = std::move(callback);
}

void CanvasWidget::set_color_picked_callback(std::function<void(QColor)> callback) {
  color_picked_callback_ = std::move(callback);
}

void CanvasWidget::set_transient_read_interaction(
    std::function<void(const CanvasReadGesture&)> callback, QCursor cursor) {
  clear_transient_read_interaction();
  transient_read_callback_ = std::move(callback);
  transient_read_cursor_ = std::move(cursor);
  update_tool_cursor();
}

void CanvasWidget::clear_transient_read_interaction() {
  auto callback = transient_read_callback_;
  const auto was_dragging = transient_read_dragging_;
  transient_read_dragging_ = false;
  transient_read_callback_ = {};
  if (QWidget::mouseGrabber() == this) {
    releaseMouse();
  }
  if (was_dragging && callback) {
    callback(CanvasReadGesture{document_position(last_mouse_position_), mapToGlobal(last_mouse_position_),
                               Qt::NoModifier, CanvasReadPhase::Cancel});
  }
  update_tool_cursor();
}

bool CanvasWidget::has_transient_read_interaction() const noexcept {
  return static_cast<bool>(transient_read_callback_);
}

void CanvasWidget::set_curves_clipping_preview(std::optional<CurvesClippingMode> mode,
                                               std::optional<CurvesChannel> channel) {
  curves_clipping_mode_ = mode;
  curves_clipping_channel_ = mode.has_value() ? channel : std::nullopt;
  if (mode.has_value()) {
    const bool cache_needs_refresh =
        document_ != nullptr &&
        (render_cache_dirty_ || render_cache_.size() != QSize(document_->width(), document_->height()));
    if (cache_needs_refresh) {
      // ensure_render_cache refreshes the clipping image after replacing the
      // source cache, so do not scan it a second time here.
      ensure_render_cache();
    } else {
      refresh_curves_clipping_preview();
    }
  } else {
    refresh_curves_clipping_preview();
  }
  update();
}

std::optional<CurvesClippingMode> CanvasWidget::curves_clipping_preview_mode() const noexcept {
  return curves_clipping_mode_;
}

void CanvasWidget::set_brush_settings_changed_callback(std::function<void()> callback) {
  brush_settings_changed_callback_ = std::move(callback);
}

void CanvasWidget::set_pen_button_action_callback(std::function<void(PenButtonAction)> callback) {
  pen_button_action_callback_ = std::move(callback);
}

void CanvasWidget::set_text_requested_callback(std::function<void(QPoint, QRect)> callback) {
  text_requested_callback_ = std::move(callback);
}

void CanvasWidget::set_active_layer_changed_callback(std::function<void(LayerId)> callback) {
  active_layer_changed_callback_ = std::move(callback);
}

void CanvasWidget::set_status_callback(std::function<void(QString)> callback) {
  status_callback_ = std::move(callback);
}

void CanvasWidget::set_info_callback(std::function<void(CanvasInfoState)> callback) {
  info_callback_ = std::move(callback);
}

void CanvasWidget::refresh_info_display() const {
  emit_info_for_widget_position(mapFromGlobal(QCursor::pos()));
}

void CanvasWidget::set_document_changed_callback(std::function<void()> callback) {
  document_changed_callback_ = std::move(callback);
  document_changed_reason_callback_ = nullptr;
}

void CanvasWidget::set_document_changed_callback(std::function<void(DocumentChangeReason)> callback) {
  document_changed_reason_callback_ = std::move(callback);
  document_changed_callback_ = nullptr;
}

void CanvasWidget::set_view_changed_callback(std::function<void()> callback) {
  view_changed_callback_ = std::move(callback);
}

void CanvasWidget::set_transform_controls_changed_callback(std::function<void()> callback) {
  transform_controls_changed_callback_ = std::move(callback);
}

void CanvasWidget::set_smart_object_transform_render_callback(std::function<bool(LayerId)> callback) {
  smart_object_transform_render_callback_ = std::move(callback);
}

void CanvasWidget::set_text_layer_transform_render_callback(std::function<bool(LayerId)> callback) {
  text_layer_transform_render_callback_ = std::move(callback);
}

void CanvasWidget::set_selected_layer_ids(std::vector<LayerId> layer_ids) {
  const auto old_transform_controls_rect = move_transform_controls_rect();
  const auto keeps_active_transform =
      transforming_layer_ && transform_layer_id_.has_value() && layer_ids.size() == 1U &&
      layer_ids.front() == *transform_layer_id_;
  if (transforming_layer_ && !keeps_active_transform) {
    finish_free_transform();
  }
  const auto keeps_active_warp = warping_layer_ && warp_layer_id_.has_value() && layer_ids.size() == 1U &&
                                 layer_ids.front() == *warp_layer_id_;
  if (warping_layer_ && !keeps_active_warp) {
    finish_warp_transform();
  }
  if (move_transform_controls_layer_id_.has_value() &&
      !(layer_ids.size() == 1U && layer_ids.front() == *move_transform_controls_layer_id_)) {
    set_move_transform_controls_layer(std::nullopt);
  }
  selected_layer_ids_ = std::move(layer_ids);
  clear_guide_selection();
  update_move_transform_controls_dirty(old_transform_controls_rect);
  notify_transform_controls_changed();
}

bool CanvasWidget::document_contains(QPoint point) const noexcept {
  return document_ != nullptr && point.x() >= 0 && point.y() >= 0 && point.x() < document_->width() &&
         point.y() < document_->height();
}

bool CanvasWidget::selection_allows(QPoint point) const noexcept {
  return quick_mask_active_ || selection_contains(point);
}

bool CanvasWidget::selection_clips_grayscale_edits() const noexcept {
  return !quick_mask_active_ && has_selection();
}

Layer* CanvasWidget::active_pixel_layer() const noexcept {
  if (document_ == nullptr || !document_->active_layer_id().has_value()) {
    return nullptr;
  }

  auto* layer = document_->find_layer(*document_->active_layer_id());
  if (layer == nullptr || layer->kind() != LayerKind::Pixel) {
    return nullptr;
  }
  return layer;
}

LayerMask* CanvasWidget::active_layer_mask() const noexcept {
  if (document_ == nullptr || !document_->active_layer_id().has_value()) {
    return nullptr;
  }

  auto* layer = document_->find_layer(*document_->active_layer_id());
  if (layer == nullptr || !layer->mask().has_value()) {
    return nullptr;
  }
  return &*layer->mask();
}

bool CanvasWidget::editing_layer_mask() const noexcept {
  if (layer_edit_target_ != LayerEditTarget::Mask || document_ == nullptr ||
      !document_->active_layer_id().has_value()) {
    return false;
  }
  // Const walk on purpose: this predicate runs from read-only paths (color
  // setters, cursor updates, begin_edit preconditions) and the non-const
  // mask() accessor bumps revisions on ACCESS (AGENTS.md "Reads must not bump
  // layer revisions") — merely picking a color while a mask was targeted
  // invalidated the layer's thumbnail and style-mask caches. Writers keep
  // going through active_layer_mask(), whose bump is load-bearing.
  const auto* layer = std::as_const(*document_).find_layer(*document_->active_layer_id());
  return layer != nullptr && layer->mask().has_value();
}

DocumentChannel* CanvasWidget::active_document_channel() const noexcept {
  if (document_ == nullptr || layer_edit_target_ != LayerEditTarget::DocumentChannel ||
      active_document_channel_id_ == 0) {
    return nullptr;
  }
  return document_->find_channel(active_document_channel_id_);
}

const DocumentChannel* CanvasWidget::active_document_channel_const() const noexcept {
  if (document_ == nullptr || layer_edit_target_ != LayerEditTarget::DocumentChannel ||
      active_document_channel_id_ == 0) {
    return nullptr;
  }
  return static_cast<const Document*>(document_)->find_channel(active_document_channel_id_);
}

std::optional<CanvasWidget::GrayscaleEditTarget> CanvasWidget::active_grayscale_edit_target(QRect required_rect) {
  if (document_ == nullptr) {
    return std::nullopt;
  }
  if (quick_mask_active_) {
    if (quick_mask_pixels_.format() != PixelFormat::gray8() ||
        quick_mask_pixels_.width() != document_->width() ||
        quick_mask_pixels_.height() != document_->height()) {
      return std::nullopt;
    }
    return GrayscaleEditTarget{
        &quick_mask_pixels_,
        QRect(0, 0, document_->width(), document_->height())};
  }
  if (layer_edit_target_ == LayerEditTarget::SmartFilterMask) {
    if (!editing_smart_filter_mask()) {
      return std::nullopt;
    }
    return GrayscaleEditTarget{
        &smart_filter_mask_pixels_, QRect(0, 0, document_->width(), document_->height())};
  }
  if (editing_layer_mask()) {
    auto* mask = active_layer_mask();
    if (mask == nullptr ||
        (!required_rect.isEmpty() &&
         !expand_mask_to_include_rect(*mask, required_rect, QSize(document_->width(), document_->height())))) {
      return std::nullopt;
    }
    return GrayscaleEditTarget{&mask->pixels,
                               QRect(mask->bounds.x, mask->bounds.y, mask->bounds.width, mask->bounds.height)};
  }
  auto* channel = active_document_channel();
  if (channel == nullptr || channel->kind() != DocumentChannelKind::Alpha) {
    return std::nullopt;
  }
  auto& pixels = channel->pixels();
  if (pixels.format() != PixelFormat::gray8() || pixels.width() != document_->width() ||
      pixels.height() != document_->height()) {
    return std::nullopt;
  }
  return GrayscaleEditTarget{&pixels, QRect(0, 0, document_->width(), document_->height())};
}

bool CanvasWidget::editing_grayscale_target() const noexcept {
  return quick_mask_active_ || editing_smart_filter_mask() || editing_layer_mask() ||
         (editing_document_channel() && document_channel_is_editable());
}

bool CanvasWidget::active_layer_locks_transparent_pixels() const noexcept {
  const auto* layer = active_pixel_layer();
  return layer != nullptr && layer_locks_transparent_pixels(*layer);
}

bool CanvasWidget::active_layer_locks_image_pixels() const noexcept {
  if (document_ == nullptr || !document_->active_layer_id().has_value()) {
    return false;
  }
  return patchy::layer_effectively_locks_image_pixels(document_->layers(), *document_->active_layer_id());
}

bool CanvasWidget::active_layer_locks_position() const noexcept {
  if (document_ == nullptr || !document_->active_layer_id().has_value()) {
    return false;
  }
  return patchy::layer_effectively_locks_position(document_->layers(), *document_->active_layer_id());
}

bool CanvasWidget::layer_effectively_locks_image_pixels(const Layer& layer) const noexcept {
  return document_ != nullptr && patchy::layer_effectively_locks_image_pixels(document_->layers(), layer.id());
}

bool CanvasWidget::layer_effectively_locks_position(const Layer& layer) const noexcept {
  return document_ != nullptr && patchy::layer_effectively_locks_position(document_->layers(), layer.id());
}

void CanvasWidget::show_layer_pixels_locked_message() const {
  if (status_callback_) {
    status_callback_(tr("Layer pixels are locked."));
  }
}

void CanvasWidget::show_layer_position_locked_message() const {
  if (status_callback_) {
    status_callback_(tr("Layer position is locked."));
  }
}

void CanvasWidget::show_edit_locked_message() const {
  if (status_callback_) {
    status_callback_(tr("Finish the open dialog before editing the document"));
  }
}

Layer* CanvasWidget::topmost_pixel_layer_at(QPoint document_point, bool require_visible_pixel,
                                            bool skip_locked) const noexcept {
  if (document_ == nullptr) {
    return nullptr;
  }

  return const_cast<Layer*>(topmost_pixel_layer_at_recursive(std::as_const(*document_).layers(), document_point,
                                                             require_visible_pixel, skip_locked));
}

Layer* CanvasWidget::topmost_move_layer_at(QPoint document_point, bool skip_locked) const noexcept {
  if (document_ == nullptr) {
    return nullptr;
  }

  return const_cast<Layer*>(
      topmost_move_layer_at_recursive(std::as_const(*document_).layers(), document_point, skip_locked));
}

Layer* CanvasWidget::topmost_text_layer_at(QPoint document_point) const noexcept {
  if (document_ == nullptr) {
    return nullptr;
  }

  return const_cast<Layer*>(topmost_text_layer_at_recursive(std::as_const(*document_).layers(), document_point));
}

void CanvasWidget::activate_layer(Layer& layer) {
  if (document_ == nullptr) {
    return;
  }
  if (layer_edit_target_ == LayerEditTarget::SmartFilterMask) {
    clear_smart_filter_mask_edit_target();
  }
  const auto old_transform_controls_rect = move_transform_controls_rect();
  if (transforming_layer_ && (!transform_layer_id_.has_value() || *transform_layer_id_ != layer.id())) {
    finish_free_transform();
  }
  if (move_transform_controls_layer_id_.has_value() && *move_transform_controls_layer_id_ != layer.id()) {
    set_move_transform_controls_layer(std::nullopt);
  }
  clear_guide_selection();
  if (!document_->active_layer_id().has_value() || *document_->active_layer_id() != layer.id()) {
    document_->set_active_layer(layer.id());
  }
  layer_edit_target_ = LayerEditTarget::Content;
  if (active_layer_changed_callback_) {
    active_layer_changed_callback_(layer.id());
  }
  update_move_transform_controls_dirty(old_transform_controls_rect);
}

QPoint CanvasWidget::layer_position(const Layer& layer, QPoint document_point) const noexcept {
  const auto bounds = layer.bounds();
  return QPoint(document_point.x() - bounds.x, document_point.y() - bounds.y);
}

QRect CanvasWidget::widget_rect_for_document_rect(QRect document_rect) const {
  const auto top_left = widget_position(document_rect.topLeft());
  const auto bottom_right = widget_position(document_rect.bottomRight() + QPoint(1, 1));
  return QRect(top_left, bottom_right).normalized().adjusted(-2, -2, 2, 2);
}

QRectF CanvasWidget::widget_rect_for_document_rect(QRectF document_rect) const {
  const auto top_left = widget_position_f(document_rect.topLeft());
  const auto bottom_right = widget_position_f(document_rect.bottomRight());
  return QRectF(top_left, bottom_right).normalized();
}

void CanvasWidget::emit_info_for_widget_position(QPoint widget_position) const {
  if (!info_callback_) {
    return;
  }

  CanvasInfoState info;
  const auto document_point = document_position(widget_position);
  info.document_point = document_point;
  info.inside_document = document_contains(document_point);
  if (info.inside_document) {
    info.color = compose_document_pixel(document_point.x(), document_point.y());
  }
  if (document_ != nullptr && selecting_) {
    info.active_rect = marquee_selection_region(selection_start_, selection_current_).boundingRect();
    info.active_rect_label = tr("Selection");
  } else if (document_ != nullptr && drawing_shape_) {
    info.active_rect = shape_drag_rect(shape_start_, snapped_document_point(document_point));
    info.active_rect_label = tr("Shape");
  } else if (document_ != nullptr && dragging_text_rect_) {
    info.active_rect = normalized_rect(text_rect_start_, snapped_document_point(document_point));
    info.active_rect_label = tr("Text");
  } else if (document_ != nullptr && zooming_) {
    info.active_rect = normalized_rect(zoom_start_, document_point);
    info.active_rect_label = tr("Zoom");
  } else if (document_ != nullptr && !selection_.isEmpty()) {
    info.active_rect = selection_.boundingRect();
    info.active_rect_label = selection_.rectCount() == 1 ? tr("Selection") : tr("Selection bounds");
  }
  info_callback_(std::move(info));
}

}  // namespace patchy::ui
