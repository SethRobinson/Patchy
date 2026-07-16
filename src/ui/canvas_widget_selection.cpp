// CanvasWidget's selection implementation, split out of canvas_widget.cpp: the
// per-tool combine-mode machinery and marquee/feather/antialias options, the
// Select-menu commands (select all/invert/clear/reselect) and Quick Mask
// enter/exit/fill, the selection region operations (expand/contract/border/
// grow/similar, layer-opaque and layer-mask selects, grayscale round-trips),
// the marching-ants outline cache and selection overlay, the marquee/lasso
// mask builders, and the selection combine/snapshot/history plumbing. Pure
// function moves from canvas_widget.cpp; behavior must stay identical.

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

QRect rect_from_anchor_and_size(QPoint anchor, int signed_width, int signed_height) {
  const auto width = std::max(1, std::abs(signed_width));
  const auto height = std::max(1, std::abs(signed_height));
  const auto x = signed_width < 0 ? anchor.x() - width : anchor.x();
  const auto y = signed_height < 0 ? anchor.y() - height : anchor.y();
  return QRect(x, y, width, height);
}

QPoint edge_clamped_document_point(const Document& document, QPoint point) {
  return QPoint(std::clamp(point.x(), 0, std::max(0, document.width())),
                std::clamp(point.y(), 0, std::max(0, document.height())));
}

QRegion region_from_alpha_mask(const QImage& mask, QRect bounds, std::uint8_t minimum_alpha = 1U) {
  if (mask.isNull() || bounds.isEmpty() || mask.format() != QImage::Format_Grayscale8 ||
      mask.size() != bounds.size()) {
    return {};
  }

  std::vector<QRect> runs;
  runs.reserve(static_cast<std::size_t>(std::max(1, bounds.height())));
  for (int y = 0; y < mask.height(); ++y) {
    const auto* row = mask.constScanLine(y);
    int run_start = -1;
    for (int x = 0; x <= mask.width(); ++x) {
      const bool selected = x < mask.width() && row[x] >= minimum_alpha;
      if (selected && run_start < 0) {
        run_start = x;
      } else if (!selected && run_start >= 0) {
        runs.emplace_back(bounds.x() + run_start, bounds.y() + y, x - run_start, 1);
        run_start = -1;
      }
    }
  }

  QRegion region;
  if (!runs.empty()) {
    region.setRects(runs.data(), static_cast<int>(runs.size()));
  }
  return region;
}

bool mask_has_partial_alpha(const QImage& mask) {
  if (mask.isNull() || mask.format() != QImage::Format_Grayscale8) {
    return false;
  }
  for (int y = 0; y < mask.height(); ++y) {
    const auto* row = mask.constScanLine(y);
    for (int x = 0; x < mask.width(); ++x) {
      if (row[x] != 0U && row[x] != 255U) {
        return true;
      }
    }
  }
  return false;
}

QImage alpha_from_painted_image(const QImage& painted) {
  QImage alpha(painted.size(), QImage::Format_Grayscale8);
  alpha.fill(0);
  const auto source = painted.convertToFormat(QImage::Format_ARGB32);
  for (int y = 0; y < source.height(); ++y) {
    const auto* src = reinterpret_cast<const QRgb*>(source.constScanLine(y));
    auto* dst = alpha.scanLine(y);
    for (int x = 0; x < source.width(); ++x) {
      dst[x] = static_cast<std::uint8_t>(qAlpha(src[x]));
    }
  }
  return alpha;
}

QImage shape_mask_from_path(const QPainterPath& path, QRect bounds, int feather_radius, bool antialias) {
  bounds = bounds.normalized();
  if (bounds.isEmpty()) {
    return {};
  }

  QImage painted(bounds.size(), QImage::Format_ARGB32_Premultiplied);
  painted.fill(Qt::transparent);
  QPainter painter(&painted);
  painter.setRenderHint(QPainter::Antialiasing, antialias);
  painter.translate(-bounds.topLeft());
  painter.fillPath(path, Qt::white);
  painter.end();

  auto mask = alpha_from_painted_image(painted);
  if (feather_radius > 0) {
    mask = feather_blur_mask(std::move(mask), feather_radius);
  }
  return mask;
}

QImage rectangle_selection_mask(QRect rect, QRect bounds, int feather_radius) {
  bounds = bounds.normalized();
  rect = rect.normalized();
  if (rect.isEmpty() || bounds.isEmpty()) {
    return {};
  }

  QPainterPath path;
  path.addRect(QRectF(rect));
  return shape_mask_from_path(path, bounds, feather_radius, feather_radius > 0);
}

std::uint8_t alpha_at(const QImage& mask, QRect bounds, QPoint document_point) {
  if (mask.isNull() || mask.format() != QImage::Format_Grayscale8 || !bounds.contains(document_point)) {
    return 0;
  }
  const auto local = document_point - bounds.topLeft();
  if (local.x() < 0 || local.y() < 0 || local.x() >= mask.width() || local.y() >= mask.height()) {
    return 0;
  }
  return mask.constScanLine(local.y())[local.x()];
}

}  // namespace

int CanvasWidget::selection_tool_index(CanvasTool tool) noexcept {
  switch (tool) {
    case CanvasTool::Marquee:
      return 0;
    case CanvasTool::EllipticalMarquee:
      return 1;
    case CanvasTool::Lasso:
      return 2;
    case CanvasTool::MagneticLasso:
      return 3;
    case CanvasTool::MagicWand:
      return 4;
    case CanvasTool::QuickSelect:
      return 5;
    default:
      return -1;
  }
}

void CanvasWidget::set_selection_mode(SelectionMode mode) noexcept {
  selection_mode_ = mode;
  if (const auto index = selection_tool_index(tool_); index >= 0) {
    selection_modes_per_tool_[static_cast<std::size_t>(index)] = mode;
  }
  // Reflect the new combine mode in the cursor badge right away.
  update_tool_cursor();
  notify_selection_mode_changed();
}

CanvasWidget::SelectionMode CanvasWidget::selection_mode() const noexcept {
  return selection_mode_;
}

CanvasWidget::SelectionMode CanvasWidget::effective_selection_mode() const noexcept {
  return selection_operation(QApplication::keyboardModifiers());
}

CanvasWidget::SelectionMode CanvasWidget::selection_mode_for_tool(CanvasTool tool) const noexcept {
  const auto index = selection_tool_index(tool);
  return index >= 0 ? selection_modes_per_tool_[static_cast<std::size_t>(index)] : SelectionMode::Replace;
}

void CanvasWidget::set_selection_mode_for_tool(CanvasTool tool, SelectionMode mode) noexcept {
  const auto index = selection_tool_index(tool);
  if (index < 0) {
    return;
  }
  selection_modes_per_tool_[static_cast<std::size_t>(index)] = mode;
  if (tool_ == tool) {
    selection_mode_ = mode;
    update_tool_cursor();
    notify_selection_mode_changed();
  }
}

void CanvasWidget::notify_selection_mode_changed() {
  // Reports the tool's stored combine mode (tool switch / explicit mode choice).
  // The live Shift/Alt override is reported separately from the key event filter,
  // so a stale global modifier state can never mask an explicit choice.
  if (selection_mode_changed_callback_) {
    selection_mode_changed_callback_(selection_mode_);
  }
}

void CanvasWidget::set_marquee_style(MarqueeStyle style) noexcept {
  marquee_style_ = style;
}

CanvasWidget::MarqueeStyle CanvasWidget::marquee_style() const noexcept {
  return marquee_style_;
}

void CanvasWidget::set_marquee_fixed_size(int width, int height) noexcept {
  marquee_fixed_size_ = QSize(std::clamp(width, 1, 30000), std::clamp(height, 1, 30000));
}

QSize CanvasWidget::marquee_fixed_size() const noexcept {
  return marquee_fixed_size_;
}

void CanvasWidget::set_marquee_corner_radius(int pixels) noexcept {
  marquee_corner_radius_ = std::clamp(pixels, 0, 512);
}

int CanvasWidget::marquee_corner_radius() const noexcept {
  return marquee_corner_radius_;
}

void CanvasWidget::set_selection_feather_radius(int pixels) noexcept {
  selection_feather_radius_ = std::clamp(pixels, 0, 250);
}

int CanvasWidget::selection_feather_radius() const noexcept {
  return selection_feather_radius_;
}

void CanvasWidget::set_selection_antialias(bool enabled) noexcept {
  selection_antialias_ = enabled;
}

bool CanvasWidget::selection_antialias() const noexcept {
  return selection_antialias_;
}

void CanvasWidget::select_all() {
  if (document_ == nullptr) {
    return;
  }
  set_selection_from_region(QRegion(QRect(0, 0, document_->width(), document_->height())));
  selection_edges_visible_ = true;
  update();
}

void CanvasWidget::invert_selection() {
  if (document_ == nullptr) {
    return;
  }
  if (quick_mask_active_) {
    auto bytes = quick_mask_pixels_.data();
    for (auto& value : bytes) {
      value = static_cast<std::uint8_t>(255U - value);
    }
    ++quick_mask_revision_;
    refresh_mask_display_image(
        QRegion(QRect(0, 0, document_->width(), document_->height())));
    if (quick_mask_changed_callback_) {
      quick_mask_changed_callback_();
    }
    if (status_callback_) {
      status_callback_(tr("Inverted Quick Mask"));
    }
    update();
    return;
  }
  const QRegion canvas_region(QRect(0, 0, document_->width(), document_->height()));
  set_selection_from_region(canvas_region.subtracted(selection_));
  if (status_callback_) {
    status_callback_(tr("Inverted selection"));
  }
  update();
}

void CanvasWidget::clear_selection() {
  if (!selection_.isEmpty()) {
    last_cleared_selection_ = selection_;
    last_cleared_selection_display_region_ = selection_display_region_;
    last_cleared_selection_mask_bounds_ = selection_mask_bounds_;
    last_cleared_selection_mask_alpha_ = selection_mask_alpha_;
  }
  set_selection_from_region(QRegion());
  selection_edges_visible_ = true;
  update();
}

void CanvasWidget::reselect() {
  if (document_ == nullptr || last_cleared_selection_.isEmpty()) {
    return;
  }
  invalidate_selection_outline();
  selection_ = last_cleared_selection_.intersected(QRect(0, 0, document_->width(), document_->height()));
  selection_display_region_ =
      last_cleared_selection_display_region_.intersected(QRect(0, 0, document_->width(), document_->height()));
  if (selection_display_region_.isEmpty() && !selection_.isEmpty()) {
    selection_display_region_ = selection_;
  }
  selection_mask_bounds_ = last_cleared_selection_mask_bounds_.intersected(QRect(0, 0, document_->width(), document_->height()));
  selection_mask_alpha_ = last_cleared_selection_mask_alpha_;
  refresh_info_display();
  if (status_callback_) {
    status_callback_(tr("Reselected previous selection"));
  }
  update();
}

bool CanvasWidget::quick_mask_active() const noexcept {
  return quick_mask_active_;
}

void CanvasWidget::invalidate_quick_mask_display() noexcept {
  mask_display_image_ = QImage();
  mask_display_image_layer_ = 0;
  mask_display_image_channel_ = 0;
  mask_display_image_revision_ = 0;
}

void CanvasWidget::set_quick_mask_active(bool active) {
  if (quick_mask_active_ == active || document_ == nullptr) {
    return;
  }
  if (active) {
    if (layer_edit_target_ == LayerEditTarget::SmartFilterMask) {
      clear_smart_filter_mask_edit_target();
    }
    quick_mask_pixels_ = selection_as_grayscale();
    quick_mask_saved_primary_ = primary_color_;
    quick_mask_saved_secondary_ = secondary_color_;
    primary_color_ = Qt::black;
    secondary_color_ = Qt::white;
    quick_mask_active_ = true;
    ++quick_mask_revision_;
    quick_mask_edit_before_.reset();
    quick_mask_edit_label_.clear();
    quick_mask_edit_dirty_ = QRegion();
    // A temporary selection channel is exclusive with saved/component channel
    // viewing. Layer content becomes the target again when Quick Mask exits.
    layer_edit_target_ = LayerEditTarget::Content;
    active_document_channel_id_ = 0;
    mask_display_mode_ = MaskDisplayMode::None;
  } else {
    finish_quick_mask_edit();
    apply_grayscale_to_selection(quick_mask_pixels_);
    quick_mask_active_ = false;
    quick_mask_pixels_ = {};
    primary_color_ = quick_mask_saved_primary_;
    secondary_color_ = quick_mask_saved_secondary_;
    quick_mask_edit_before_.reset();
    quick_mask_edit_label_.clear();
    quick_mask_edit_dirty_ = QRegion();
  }
  invalidate_quick_mask_display();
  invalidate_selection_outline();
  refresh_info_display();
  update_tool_cursor();
  update();
  if (quick_mask_changed_callback_) {
    quick_mask_changed_callback_();
  }
}

const PixelBuffer& CanvasWidget::quick_mask_pixels() const noexcept {
  return quick_mask_pixels_;
}

std::uint64_t CanvasWidget::quick_mask_revision() const noexcept {
  return quick_mask_revision_;
}

QRect CanvasWidget::fill_quick_mask(QColor color, QString history_label) {
  if (!quick_mask_active_ || document_ == nullptr ||
      !begin_edit(std::move(history_label))) {
    return {};
  }
  const auto dirty = fill_active_layer_mask(color);
  active_edit_target_changed_impl(QRegion(dirty),
                                  DocumentChangeReason::Immediate);
  return dirty;
}

void CanvasWidget::expand_selection(int pixels) {
  if (document_ == nullptr || selection_.isEmpty() || pixels <= 0) {
    return;
  }
  const QRect canvas_rect(0, 0, document_->width(), document_->height());
  set_selection_from_region(expanded_region(selection_, pixels, canvas_rect));
  if (status_callback_) {
    status_callback_(tr("Expanded selection by %1 px").arg(pixels));
  }
  update();
}

void CanvasWidget::contract_selection(int pixels) {
  if (document_ == nullptr || selection_.isEmpty() || pixels <= 0) {
    return;
  }
  pixels = std::clamp(pixels, 0, 250);
  const QRect canvas_rect(0, 0, document_->width(), document_->height());
  const QRegion canvas_region(canvas_rect);
  const auto padded_canvas_rect = canvas_rect.adjusted(-pixels, -pixels, pixels, pixels);
  const auto outside = QRegion(padded_canvas_rect).subtracted(selection_);
  set_selection_from_region(canvas_region.subtracted(expanded_region(outside, pixels, padded_canvas_rect)));
  if (status_callback_) {
    status_callback_(tr("Contracted selection by %1 px").arg(pixels));
  }
  update();
}

void CanvasWidget::border_selection(int pixels) {
  if (document_ == nullptr || selection_.isEmpty() || pixels <= 0) {
    return;
  }
  const QRect canvas_rect(0, 0, document_->width(), document_->height());
  const QRegion canvas_region(canvas_rect);
  const auto outside = expanded_region(selection_, pixels, canvas_rect);
  const auto outside_of_selection = canvas_region.subtracted(selection_);
  const auto inside = canvas_region.subtracted(expanded_region(outside_of_selection, pixels, canvas_rect));
  set_selection_from_region(outside.subtracted(inside));
  if (status_callback_) {
    status_callback_(tr("Selected %1 px border").arg(pixels));
  }
  update();
}

void CanvasWidget::select_layer_opaque_pixels(LayerId layer_id) {
  if (document_ == nullptr) {
    return;
  }
  const auto* layer = document_->find_layer(layer_id);
  if (layer == nullptr || layer->kind() != LayerKind::Pixel) {
    report_status_error(tr("Select a pixel layer first"));
    return;
  }

  const QRect canvas_rect(0, 0, document_->width(), document_->height());
  const auto& pixels = layer->pixels();
  const auto layer_bounds = to_qrect(layer->bounds());
  const auto selection_bounds = layer_bounds.intersected(canvas_rect);
  if (selection_bounds.isEmpty() || pixels.empty()) {
    set_selection_from_region({});
  } else {
    QImage alpha(selection_bounds.size(), QImage::Format_Grayscale8);
    const auto channels = static_cast<std::size_t>(pixels.format().channels);
    for (int y = 0; y < selection_bounds.height(); ++y) {
      auto* destination = alpha.scanLine(y);
      const auto source_y = selection_bounds.y() + y - layer_bounds.y();
      const auto source_x = selection_bounds.x() - layer_bounds.x();
      const auto source = pixels.row(source_y);
      if (channels >= 4U) {
        for (int x = 0; x < selection_bounds.width(); ++x) {
          destination[x] = source[(static_cast<std::size_t>(source_x + x) * channels) + 3U];
        }
      } else {
        std::fill_n(destination, selection_bounds.width(), std::uint8_t{255});
      }
    }
    auto layer_region = region_from_alpha_mask(alpha, selection_bounds);
    set_selection_from_mask(std::move(layer_region), selection_bounds, std::move(alpha));
  }
  selection_edges_visible_ = true;
  if (status_callback_) {
    status_callback_(selection_.isEmpty() ? tr("Layer has no opaque pixels") : tr("Selected layer opacity"));
  }
  update();
}

void CanvasWidget::select_layer_mask_pixels(LayerId layer_id) {
  if (document_ == nullptr) {
    return;
  }
  const auto* layer = document_->find_layer(layer_id);
  if (layer == nullptr || !layer->mask().has_value()) {
    report_status_error(tr("Layer has no mask"));
    return;
  }

  const QRect canvas_rect(0, 0, document_->width(), document_->height());
  const auto& mask = *layer->mask();
  const auto stored_bounds = to_qrect(mask.bounds);
  const auto selection_bounds =
      mask.default_color != 0U ? canvas_rect : stored_bounds.intersected(canvas_rect);
  if (selection_bounds.isEmpty()) {
    set_selection_from_region({});
  } else {
    QImage alpha(selection_bounds.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < alpha.height(); ++y) {
      std::fill_n(alpha.scanLine(y), alpha.width(), mask.default_color);
    }
    if (!mask.pixels.empty() && mask.pixels.format() == PixelFormat::gray8()) {
      const auto copy_bounds = stored_bounds.intersected(selection_bounds);
      for (int y = copy_bounds.top(); y <= copy_bounds.bottom(); ++y) {
        const auto source = mask.pixels.row(y - stored_bounds.y());
        auto* destination = alpha.scanLine(y - selection_bounds.y());
        const auto source_x = copy_bounds.x() - stored_bounds.x();
        const auto destination_x = copy_bounds.x() - selection_bounds.x();
        std::copy_n(source.begin() + source_x, copy_bounds.width(), destination + destination_x);
      }
    }
    auto mask_region = region_from_alpha_mask(alpha, selection_bounds);
    set_selection_from_mask(std::move(mask_region), selection_bounds, std::move(alpha));
  }
  selection_edges_visible_ = true;
  if (status_callback_) {
    status_callback_(selection_.isEmpty() ? tr("Layer mask has no selected pixels") : tr("Selected layer mask"));
  }
  update();
}

void CanvasWidget::select_active_layer_opaque_pixels() {
  if (document_ == nullptr || !document_->active_layer_id().has_value()) {
    return;
  }
  select_layer_opaque_pixels(*document_->active_layer_id());
}

QRect CanvasWidget::fill_active_layer_mask(QColor color) {
  if (document_ == nullptr) {
    return {};
  }
  const auto clip_selection = selection_clips_grayscale_edits();
  auto affected = clip_selection && selected_document_rect().has_value()
                      ? *selected_document_rect()
                      : QRect(0, 0, document_->width(), document_->height());
  affected = affected.intersected(QRect(0, 0, document_->width(), document_->height()));
  if (affected.isEmpty()) {
    return {};
  }
  auto target = active_grayscale_edit_target(affected);
  if (!target.has_value() || target->pixels == nullptr) {
    return {};
  }

  const auto bounds = target->bounds;
  const auto value = mask_value_from_color(color);
  QRect dirty;
  if (clip_selection && !selection_has_partial_alpha()) {
    const auto selected = selection_.intersected(QRegion(affected));
    for (const auto& rect : selected) {
      const auto clipped = rect.intersected(bounds);
      if (clipped.isEmpty()) {
        continue;
      }
      for (int y = clipped.top(); y <= clipped.bottom(); ++y) {
        for (int x = clipped.left(); x <= clipped.right(); ++x) {
          auto* px = target->pixels->pixel(x - bounds.x(), y - bounds.y());
          *px = blend_mask_value(*px, value, 1.0F);
        }
        tick_processing_operation();
      }
      dirty = dirty.united(clipped);
    }
    return dirty;
  }

  for (int y = affected.top(); y <= affected.bottom(); ++y) {
    for (int x = affected.left(); x <= affected.right(); ++x) {
      const QPoint document_point(x, y);
      if (!selection_allows(document_point)) {
        continue;
      }
      auto coverage = 1.0F;
      if (clip_selection) {
        coverage = static_cast<float>(selection_alpha_at(document_point)) / 255.0F;
      }
      if (coverage <= 0.0F) {
        continue;
      }
      auto* px = target->pixels->pixel(x - bounds.x(), y - bounds.y());
      *px = blend_mask_value(*px, value, coverage);
      dirty = dirty.united(QRect(document_point, QSize(1, 1)));
    }
    tick_processing_operation();
  }
  return dirty;
}

QRect CanvasWidget::clear_active_layer_mask() {
  return fill_active_layer_mask(Qt::black);
}

PixelBuffer CanvasWidget::selection_as_grayscale() const {
  if (document_ == nullptr) {
    return {};
  }
  if (quick_mask_active_ && !quick_mask_pixels_.empty()) {
    return quick_mask_pixels_;
  }
  PixelBuffer pixels(document_->width(), document_->height(), PixelFormat::gray8());
  pixels.clear(0);
  if (selection_.isEmpty()) {
    return pixels;
  }

  const QRect canvas_rect(0, 0, document_->width(), document_->height());
  if (!selection_mask_alpha_.isNull()) {
    const QRect image_bounds(selection_mask_bounds_.topLeft(), selection_mask_alpha_.size());
    const auto copy_bounds = image_bounds.intersected(canvas_rect);
    for (int y = copy_bounds.top(); y <= copy_bounds.bottom(); ++y) {
      const auto* source = selection_mask_alpha_.constScanLine(y - image_bounds.y());
      auto destination = pixels.row(y);
      const auto source_x = copy_bounds.x() - image_bounds.x();
      std::copy_n(source + source_x, copy_bounds.width(), destination.begin() + copy_bounds.x());
    }
    return pixels;
  }

  for (const auto& rect : selection_.intersected(canvas_rect)) {
    for (int y = rect.top(); y <= rect.bottom(); ++y) {
      auto row = pixels.row(y);
      std::fill(row.begin() + rect.left(), row.begin() + rect.right() + 1, std::uint8_t{255});
    }
  }
  return pixels;
}

void CanvasWidget::apply_grayscale_to_selection(const PixelBuffer& pixels) {
  if (document_ == nullptr || pixels.format() != PixelFormat::gray8() ||
      pixels.width() != document_->width() || pixels.height() != document_->height()) {
    return;
  }
  QImage alpha(document_->width(), document_->height(), QImage::Format_Grayscale8);
  std::vector<QRect> runs;
  for (int y = 0; y < document_->height(); ++y) {
    const auto source = pixels.row(y);
    auto* destination = alpha.scanLine(y);
    std::copy(source.begin(), source.end(), destination);
    int run_start = -1;
    for (int x = 0; x < document_->width(); ++x) {
      if (source[static_cast<std::size_t>(x)] != 0U && run_start < 0) {
        run_start = x;
      }
      if ((source[static_cast<std::size_t>(x)] == 0U || x + 1 == document_->width()) && run_start >= 0) {
        const auto run_end = source[static_cast<std::size_t>(x)] == 0U ? x : x + 1;
        runs.emplace_back(run_start, y, run_end - run_start, 1);
        run_start = -1;
      }
    }
  }
  QRegion region;
  if (!runs.empty()) {
    region.setRects(runs.data(), static_cast<int>(runs.size()));
  }
  set_selection_from_mask(std::move(region), QRect(0, 0, document_->width(), document_->height()),
                          std::move(alpha));
}

void CanvasWidget::replace_selection_from_grayscale(const PixelBuffer& pixels, QString history_label) {
  const auto before = capture_selection_snapshot();
  apply_grayscale_to_selection(pixels);
  record_selection_history(std::move(history_label), before);
  refresh_info_display();
  update();
}

void CanvasWidget::grow_selection() {
  if (document_ == nullptr || selection_.isEmpty()) {
    report_status_error(tr("Make a selection before growing"));
    return;
  }

  ensure_render_cache();
  if (render_cache_.isNull()) {
    return;
  }

  const auto& flattened = render_cache_;
  const auto width = flattened.width();
  const auto height = flattened.height();
  const auto total_pixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  const auto pixel = [&flattened](int x, int y) {
    return flattened.constScanLine(y) + static_cast<std::size_t>(x) * 4U;
  };

  std::array<std::int64_t, 4> sum = {0, 0, 0, 0};
  std::int64_t sample_count = 0;
  for (const auto& rect : selection_) {
    const auto clipped = rect.intersected(QRect(0, 0, width, height));
    for (int y = clipped.top(); y <= clipped.bottom(); ++y) {
      for (int x = clipped.left(); x <= clipped.right(); ++x) {
        const auto* color = pixel(x, y);
        sum[0] += color[0];
        sum[1] += color[1];
        sum[2] += color[2];
        sum[3] += color[3];
        ++sample_count;
      }
    }
  }
  if (sample_count <= 0) {
    return;
  }

  const std::array<int, 4> target = {static_cast<int>(sum[0] / sample_count),
                                     static_cast<int>(sum[1] / sample_count),
                                     static_cast<int>(sum[2] / sample_count),
                                     static_cast<int>(sum[3] / sample_count)};
  const auto tolerance_squared = wand_tolerance_ * wand_tolerance_ * 4;
  const auto matches = [&](int x, int y) {
    const auto* color = pixel(x, y);
    const auto dr = static_cast<int>(color[0]) - target[0];
    const auto dg = static_cast<int>(color[1]) - target[1];
    const auto db = static_cast<int>(color[2]) - target[2];
    const auto da = static_cast<int>(color[3]) - target[3];
    return dr * dr + dg * dg + db * db + da * da <= tolerance_squared;
  };

  std::vector<std::uint8_t> selected(total_pixels);
  std::vector<int> queue;
  queue.reserve(std::min<std::size_t>(total_pixels, 1'000'000U));
  auto min_x = std::numeric_limits<int>::max();
  auto min_y = std::numeric_limits<int>::max();
  auto max_x = std::numeric_limits<int>::min();
  auto max_y = std::numeric_limits<int>::min();
  int count = 0;
  const auto mark = [&](int x, int y) {
    const auto index = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
    if (selected[index] != 0U) {
      return false;
    }
    selected[index] = 1U;
    queue.push_back(y * width + x);
    min_x = std::min(min_x, x);
    min_y = std::min(min_y, y);
    max_x = std::max(max_x, x);
    max_y = std::max(max_y, y);
    ++count;
    return true;
  };

  for (const auto& rect : selection_) {
    const auto clipped = rect.intersected(QRect(0, 0, width, height));
    for (int y = clipped.top(); y <= clipped.bottom(); ++y) {
      for (int x = clipped.left(); x <= clipped.right(); ++x) {
        mark(x, y);
      }
    }
  }

  for (std::size_t offset = 0; offset < queue.size(); ++offset) {
    const auto index_value = queue[offset];
    const auto x = index_value % width;
    const auto y = index_value / width;
    const auto maybe_mark = [&](int nx, int ny) {
      const auto index = static_cast<std::size_t>(ny) * static_cast<std::size_t>(width) + static_cast<std::size_t>(nx);
      if (selected[index] == 0U && matches(nx, ny)) {
        mark(nx, ny);
      }
    };
    if (x + 1 < width) {
      maybe_mark(x + 1, y);
    }
    if (x > 0) {
      maybe_mark(x - 1, y);
    }
    if (y + 1 < height) {
      maybe_mark(x, y + 1);
    }
    if (y > 0) {
      maybe_mark(x, y - 1);
    }
  }

  set_selection_from_region(region_from_mask(selected, width, height, min_x, min_y, max_x, max_y));
  if (status_callback_) {
    status_callback_(tr("Grew selection to %1 px").arg(count));
  }
  update();
}

void CanvasWidget::select_similar_to_selection() {
  if (document_ == nullptr || selection_.isEmpty()) {
    report_status_error(tr("Make a selection before selecting similar pixels"));
    return;
  }

  ensure_render_cache();
  if (render_cache_.isNull()) {
    return;
  }

  const auto& flattened = render_cache_;
  const auto width = flattened.width();
  const auto height = flattened.height();
  const auto total_pixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  const auto pixel = [&flattened](int x, int y) {
    return flattened.constScanLine(y) + static_cast<std::size_t>(x) * 4U;
  };

  std::array<std::int64_t, 4> sum = {0, 0, 0, 0};
  std::int64_t sample_count = 0;
  for (const auto& rect : selection_) {
    const auto clipped = rect.intersected(QRect(0, 0, width, height));
    for (int y = clipped.top(); y <= clipped.bottom(); ++y) {
      for (int x = clipped.left(); x <= clipped.right(); ++x) {
        const auto* color = pixel(x, y);
        sum[0] += color[0];
        sum[1] += color[1];
        sum[2] += color[2];
        sum[3] += color[3];
        ++sample_count;
      }
    }
  }
  if (sample_count <= 0) {
    return;
  }

  const std::array<int, 4> target = {static_cast<int>(sum[0] / sample_count),
                                     static_cast<int>(sum[1] / sample_count),
                                     static_cast<int>(sum[2] / sample_count),
                                     static_cast<int>(sum[3] / sample_count)};
  const auto tolerance_squared = wand_tolerance_ * wand_tolerance_ * 4;
  std::vector<std::uint8_t> selected(total_pixels);
  auto min_x = std::numeric_limits<int>::max();
  auto min_y = std::numeric_limits<int>::max();
  auto max_x = std::numeric_limits<int>::min();
  auto max_y = std::numeric_limits<int>::min();
  int count = 0;

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto* color = pixel(x, y);
      const auto dr = static_cast<int>(color[0]) - target[0];
      const auto dg = static_cast<int>(color[1]) - target[1];
      const auto db = static_cast<int>(color[2]) - target[2];
      const auto da = static_cast<int>(color[3]) - target[3];
      if (dr * dr + dg * dg + db * db + da * da > tolerance_squared) {
        continue;
      }
      selected[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] = 1U;
      min_x = std::min(min_x, x);
      min_y = std::min(min_y, y);
      max_x = std::max(max_x, x);
      max_y = std::max(max_y, y);
      ++count;
    }
  }

  set_selection_from_region(region_from_mask(selected, width, height, min_x, min_y, max_x, max_y));
  if (status_callback_) {
    status_callback_(tr("Selected %1 similar px").arg(count));
  }
  update();
}

std::optional<QRect> CanvasWidget::selected_document_rect() const noexcept {
  if (selection_.isEmpty()) {
    return std::nullopt;
  }
  return selection_.boundingRect();
}

const QRegion& CanvasWidget::selected_document_region() const noexcept {
  return selection_;
}

std::uint8_t CanvasWidget::selection_alpha_at(QPoint point) const noexcept {
  if (selection_.isEmpty()) {
    return 0;
  }
  if (!selection_mask_alpha_.isNull()) {
    return alpha_at(selection_mask_alpha_, selection_mask_bounds_, point);
  }
  return selection_.contains(point) ? 255 : 0;
}

bool CanvasWidget::selection_has_partial_alpha() const noexcept {
  return !selection_mask_alpha_.isNull();
}

bool CanvasWidget::has_selection() const noexcept {
  return !selection_.isEmpty();
}

bool CanvasWidget::selection_contains(QPoint point) const noexcept {
  return selection_.isEmpty() || selection_alpha_at(point) != 0U;
}

namespace {

// The classic Photoshop ants: alternating black/white dashes that crawl along
// the path as the offset advances. The black pass is drawn solid underneath
// the animated white dashes rather than as the complementary dash: both render
// identical pixels along the stroke (the visible black runs are exactly the
// gaps of the white pattern, so they appear to march too), and the solid
// underlay keeps the dark outline continuous however the dash phase lands on
// short subpaths.
void stroke_marching_ants(QPainter& painter, const QPainterPath& path, int dash_offset,
                          double dash_length = 4.0) {
  QPen black(QColor(18, 20, 24), 1.0);
  black.setCosmetic(true);
  QPen white(QColor(248, 250, 253), 1.0);
  white.setDashPattern({dash_length, dash_length});
  white.setDashOffset(dash_offset);
  white.setCosmetic(true);
  painter.setBrush(Qt::NoBrush);
  painter.setPen(black);
  painter.drawPath(path);
  painter.setPen(white);
  painter.drawPath(path);
}

void stroke_marching_ants(QPainter& painter, const QPolygon& polyline, int dash_offset) {
  QPainterPath path;
  path.addPolygon(QPolygonF(polyline));
  stroke_marching_ants(painter, path, dash_offset);
}

}  // namespace

void CanvasWidget::invalidate_selection_outline() noexcept {
  selection_outline_dirty_ = true;
  selection_outline_screen_valid_ = false;
}

void CanvasWidget::ensure_selection_outline_screen_path() const {
  const auto viewport = rect();
  if (selection_outline_screen_valid_ && selection_outline_screen_zoom_ == zoom_ &&
      selection_outline_screen_pan_ == pan_ && selection_outline_screen_viewport_ == viewport) {
    return;
  }
  const auto& outline_region = selection_display_region_.isEmpty() ? selection_ : selection_display_region_;
  const auto padded_viewport = QRectF(viewport).adjusted(-2.0, -2.0, 2.0, 2.0);
  if (zoom_ < 1.0) {
    // Below 100% the outline is resolved at device resolution like the artwork
    // itself, so it is retraced whenever zoom/pan/viewport change and the
    // document-space loop cache stays untouched (and stays dirty).
    const auto device_loops = trace_device_selection_outlines(outline_region, zoom_, pan_, padded_viewport);
    selection_outline_screen_paths_ =
        build_selection_outline_screen_paths(device_loops, 1.0, QPointF(0.0, 0.0), padded_viewport);
  } else {
    if (selection_outline_dirty_) {
      selection_outline_loops_ = trace_selection_outlines(outline_region);
      selection_outline_dirty_ = false;
    }
    selection_outline_screen_paths_ =
        build_selection_outline_screen_paths(selection_outline_loops_, zoom_, pan_, padded_viewport);
  }
  selection_outline_screen_zoom_ = zoom_;
  selection_outline_screen_pan_ = pan_;
  selection_outline_screen_viewport_ = viewport;
  selection_outline_screen_valid_ = true;
}

void CanvasWidget::draw_selection_overlay(QPainter& painter) const {
  if (!quick_mask_active_ && !selection_.isEmpty() &&
      selection_edges_visible_) {
    ensure_selection_outline_screen_path();
    if (!selection_outline_screen_paths_.marching.isEmpty()) {
      stroke_marching_ants(painter, selection_outline_screen_paths_.marching, selection_dash_offset_);
    }
    if (!selection_outline_screen_paths_.pinpoint.isEmpty()) {
      // Loops shorter than one 4-4 dash period would spend whole phases fully
      // covered by the white dash and blink out; a finer pattern keeps a dark
      // edge on them at every phase.
      stroke_marching_ants(painter, selection_outline_screen_paths_.pinpoint, selection_dash_offset_, 2.0);
    }
  }

  if (lassoing_ && lasso_points_.size() > 1) {
    QPolygon preview;
    preview.reserve(lasso_points_.size());
    for (const auto& point : lasso_points_) {
      preview << widget_position(point);
    }
    stroke_marching_ants(painter, preview, selection_dash_offset_);
  }

  if (magnetic_lassoing_ && magnetic_committed_path_.size() + magnetic_live_path_.size() > 1) {
    // Committed path + live segment as one ant trail; the live path's first
    // point duplicates the last committed point, so skip it.
    QPolygon preview;
    preview.reserve(magnetic_committed_path_.size() + magnetic_live_path_.size());
    for (const auto& point : magnetic_committed_path_) {
      preview << widget_position(point);
    }
    for (int i = 1; i < magnetic_live_path_.size(); ++i) {
      preview << widget_position(magnetic_live_path_[i]);
    }
    stroke_marching_ants(painter, preview, selection_dash_offset_);
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);
    // Photoshop-style fastening boxes: a white square with a dark border, big
    // enough to read as a marker rather than a stray pixel.
    for (const auto& anchor : magnetic_anchors_) {
      const auto p = widget_position(anchor);
      painter.fillRect(QRect(p.x() - 3, p.y() - 3, 7, 7), QColor(20, 24, 28));
      painter.fillRect(QRect(p.x() - 2, p.y() - 2, 5, 5), QColor(245, 247, 250));
    }
    painter.restore();
  }

  // Add/Subtract/Intersect defer the combine to release (like the Lasso), so the
  // existing selection stays put mid-drag and the candidate shape is shown as its
  // own marching-ants outline. (Replace shows it live as the selection edge.)
  if (selecting_ && selection_operation_ != SelectionMode::Replace &&
      (tool_ == CanvasTool::Marquee || tool_ == CanvasTool::EllipticalMarquee)) {
    // Trace the candidate edge exactly where the committed selection will land:
    // the right/bottom edges sit one document pixel past the inclusive rect, the
    // same convention the marching-ants outline above uses. (Do not route through
    // widget_rect_for_document_rect(QRect) — that inflates the rect by 2px for
    // hover/hit outlines, which would leave the preview sitting proud of the
    // real selection.)
    const auto doc_rect = marquee_selection_rect(selection_start_, selection_current_);
    const QRectF widget_rect(widget_position_f(QPointF(doc_rect.left(), doc_rect.top())),
                             widget_position_f(QPointF(doc_rect.right() + 1, doc_rect.bottom() + 1)));
    QPainterPath preview;
    if (tool_ == CanvasTool::EllipticalMarquee) {
      preview.addEllipse(widget_rect);
    } else if (const auto radius = marquee_effective_corner_radius(doc_rect); radius > 0.0) {
      preview.addRoundedRect(widget_rect, radius * zoom_, radius * zoom_);
    } else {
      preview.addRect(widget_rect);
    }
    stroke_marching_ants(painter, preview, selection_dash_offset_);
  }
}

QRect CanvasWidget::marquee_selection_rect(QPoint anchor, QPoint current) const {
  QRect rect;
  if (marquee_from_center_) {
    // Draw-from-center (Alt with no existing selection): the press point is the
    // center, so the rectangle grows symmetrically and is twice the drag extent.
    if (marquee_style_ == MarqueeStyle::FixedSize) {
      rect = QRect(anchor - QPoint(marquee_fixed_size_.width() / 2, marquee_fixed_size_.height() / 2),
                   marquee_fixed_size_);
    } else {
      const auto delta = current - anchor;
      auto half_w = std::abs(delta.x());
      auto half_h = std::abs(delta.y());
      if (marquee_style_ == MarqueeStyle::FixedRatio) {
        const auto ratio = std::max(1.0, static_cast<double>(marquee_fixed_size_.width())) /
                           std::max(1.0, static_cast<double>(marquee_fixed_size_.height()));
        if (static_cast<double>(half_w) / std::max(1, half_h) > ratio) {
          half_h = std::max(0, static_cast<int>(std::round(half_w / ratio)));
        } else {
          half_w = std::max(0, static_cast<int>(std::round(half_h * ratio)));
        }
      } else if (selection_square_constrained_) {
        half_w = half_h = std::min(half_w, half_h);
      }
      rect = QRect(anchor.x() - half_w, anchor.y() - half_h, half_w * 2, half_h * 2);
    }
  } else if (marquee_style_ == MarqueeStyle::FixedSize) {
    rect = QRect(anchor, marquee_fixed_size_);
  } else if (marquee_style_ == MarqueeStyle::FixedRatio) {
    const auto ratio =
        std::max(1.0, static_cast<double>(marquee_fixed_size_.width())) /
        std::max(1.0, static_cast<double>(marquee_fixed_size_.height()));
    const auto delta = current - anchor;
    auto width = std::max(1, std::abs(delta.x()));
    auto height = std::max(1, std::abs(delta.y()));
    if (static_cast<double>(width) / static_cast<double>(height) > ratio) {
      width = std::max(1, static_cast<int>(std::round(height * ratio)));
    } else {
      height = std::max(1, static_cast<int>(std::round(width / ratio)));
    }
    const auto signed_width = delta.x() < 0 ? -width : width;
    const auto signed_height = delta.y() < 0 ? -height : height;
    rect = QRect(anchor, anchor + QPoint(signed_width, signed_height)).normalized();
  } else if (selection_square_constrained_) {
    // 1:1 square (circle for the ellipse), sized to the smaller drag axis and
    // anchored at the canvas edge so a drag begun in the grey area starts there.
    const auto square_anchor = document_ != nullptr ? edge_clamped_document_point(*document_, anchor) : anchor;
    const auto delta = current - square_anchor;
    const auto side = std::max(1, std::min(std::abs(delta.x()), std::abs(delta.y())));
    const auto signed_width = delta.x() < 0 ? -side : side;
    const auto signed_height = delta.y() < 0 ? -side : side;
    rect = rect_from_anchor_and_size(square_anchor, signed_width, signed_height);
  } else {
    rect = normalized_rect(anchor, current);
  }
  // Keep at least 1px per axis: QRect::normalized() leaves a 0-size rect when the
  // cursor lands exactly 1px before the anchor, blanking the selection mid-drag.
  if (rect.width() < 1) {
    rect.setWidth(1);
  }
  if (rect.height() < 1) {
    rect.setHeight(1);
  }
  return rect;
}

double CanvasWidget::marquee_effective_corner_radius(QRect rect) const noexcept {
  if (tool_ != CanvasTool::Marquee || marquee_corner_radius_ <= 0) {
    return 0.0;
  }
  // A radius past half the rectangle collapses opposing arcs into each other;
  // clamping keeps the shape a stadium/circle instead of a malformed path.
  return std::min({static_cast<double>(marquee_corner_radius_), rect.width() / 2.0, rect.height() / 2.0});
}

QRegion CanvasWidget::marquee_selection_region(QPoint anchor, QPoint current) const {
  if (document_ == nullptr) {
    return {};
  }

  const auto rect = marquee_selection_rect(anchor, current);
  QRegion marquee;
  if (tool_ == CanvasTool::EllipticalMarquee) {
    marquee = QRegion(rect, QRegion::Ellipse);
  } else if (const auto radius = marquee_effective_corner_radius(rect); radius > 0.0) {
    QPainterPath path;
    path.addRoundedRect(QRectF(rect), radius, radius);
    marquee = QRegion(path.toFillPolygon().toPolygon(), Qt::WindingFill);
  } else {
    marquee = QRegion(rect);
  }
  return marquee.intersected(QRect(0, 0, document_->width(), document_->height()));
}

QImage CanvasWidget::marquee_selection_mask(QPoint anchor, QPoint current, QRect& bounds) const {
  bounds = {};
  if (document_ == nullptr) {
    return {};
  }

  const auto canvas_rect = QRect(0, 0, document_->width(), document_->height());
  const auto rect = marquee_selection_rect(anchor, current);
  const auto feather = selection_feather_radius_;
  const auto padding = feather_mask_padding(feather);
  bounds = rect.adjusted(-padding, -padding, padding, padding).intersected(canvas_rect);
  if (bounds.isEmpty()) {
    return {};
  }

  if (tool_ == CanvasTool::Marquee) {
    const auto radius = marquee_effective_corner_radius(rect);
    if (radius <= 0.0) {
      return rectangle_selection_mask(rect, bounds, feather);
    }
    QPainterPath path;
    path.addRoundedRect(QRectF(rect), radius, radius);
    return shape_mask_from_path(path, bounds, feather, selection_antialias_ || feather > 0);
  }

  QPainterPath path;
  if (tool_ == CanvasTool::EllipticalMarquee) {
    path.addEllipse(QRectF(rect));
  } else {
    path.addRect(QRectF(rect));
  }
  return shape_mask_from_path(path, bounds, feather, selection_antialias_ || feather > 0);
}

QImage CanvasWidget::lasso_selection_mask(const QPolygon& polygon, QRect& bounds) const {
  return lasso_selection_mask(QPolygonF(polygon), bounds);
}

QImage CanvasWidget::lasso_selection_mask(const QPolygonF& polygon, QRect& bounds) const {
  bounds = {};
  if (document_ == nullptr || polygon.size() < 3) {
    return {};
  }

  const auto canvas_rect = QRect(0, 0, document_->width(), document_->height());
  const auto feather = selection_feather_radius_;
  const auto padding = feather_mask_padding(feather);
  bounds = polygon.boundingRect()
               .toAlignedRect()
               .adjusted(-padding, -padding, padding, padding)
               .intersected(canvas_rect);
  if (bounds.isEmpty()) {
    return {};
  }

  QPainterPath path;
  path.addPolygon(polygon);
  path.closeSubpath();
  return shape_mask_from_path(path, bounds, feather, selection_antialias_ || feather > 0);
}

CanvasWidget::SelectionMode CanvasWidget::selection_operation(Qt::KeyboardModifiers modifiers) const noexcept {
  // With nothing selected there is nothing to add to / subtract from, so Shift
  // and Alt do not switch the combine mode (they act as geometry constraints
  // instead: Shift = square, Alt = draw from center). The badge and Options-bar
  // buttons therefore stay on the tool's own stored mode.
  if (selection_.isEmpty()) {
    return selection_mode_;
  }
  const auto add = modifiers.testFlag(Qt::ShiftModifier);
  const auto subtract = modifiers.testFlag(Qt::AltModifier);
  if (add && subtract) {
    return SelectionMode::Intersect;
  }
  if (add) {
    return SelectionMode::Add;
  }
  if (subtract) {
    return SelectionMode::Subtract;
  }
  return selection_mode_;
}

QRegion CanvasWidget::combine_selection(const QRegion& candidate) const {
  if (candidate.isEmpty()) {
    return selection_before_edit_;
  }

  if (selection_before_edit_.isEmpty()) {
    return selection_operation_ == SelectionMode::Subtract ? QRegion() : candidate;
  }

  switch (selection_operation_) {
    case SelectionMode::Replace:
      return candidate;
    case SelectionMode::Add:
      return selection_before_edit_.united(candidate);
    case SelectionMode::Intersect: {
      return selection_before_edit_.intersected(candidate);
    }
    case SelectionMode::Subtract:
      return selection_before_edit_.subtracted(candidate);
  }
  return candidate;
}

void CanvasWidget::set_selection_from_region(QRegion selection) {
  invalidate_selection_outline();
  if (document_ != nullptr) {
    selection = selection.intersected(QRect(0, 0, document_->width(), document_->height()));
  }
  selection_ = std::move(selection);
  selection_display_region_ = selection_;
  selection_mask_bounds_ = {};
  selection_mask_alpha_ = QImage();
  refresh_info_display();
}

void CanvasWidget::set_selection_from_mask(QRegion selection, QRect mask_bounds, QImage mask_alpha) {
  invalidate_selection_outline();
  if (document_ != nullptr) {
    const QRect canvas_rect(0, 0, document_->width(), document_->height());
    selection = selection.intersected(canvas_rect);
    mask_bounds = mask_bounds.intersected(canvas_rect);
  }
  selection_ = std::move(selection);
  if (selection_.isEmpty() || mask_alpha.isNull() || !mask_has_partial_alpha(mask_alpha)) {
    selection_display_region_ = selection_;
    selection_mask_bounds_ = {};
    selection_mask_alpha_ = QImage();
    refresh_info_display();
    return;
  }
  selection_mask_bounds_ = mask_bounds;
  selection_display_region_ = region_from_alpha_mask(mask_alpha, mask_bounds, 128U);
  if (selection_display_region_.isEmpty()) {
    selection_display_region_ = selection_;
  }
  selection_mask_alpha_ = std::move(mask_alpha);
  refresh_info_display();
}

void CanvasWidget::update_selection_square_constraint(Qt::KeyboardModifiers modifiers) {
  const bool shift_now = (modifiers & Qt::ShiftModifier) != 0;
  if (!shift_now) {
    selection_shift_released_since_press_ = true;
  }
  // Shift after the drag starts constrains; Shift held from press is "add", so it
  // only constrains after being released and pressed again.
  selection_square_constrained_ =
      shift_now && (!selection_shift_at_press_ || selection_shift_released_since_press_);
}

void CanvasWidget::refresh_active_marquee_selection() {
  if (!selecting_ || spacebar_repositioning_drag_rect_) {
    return;
  }
  // Only Replace touches the live selection mid-drag; the combine modes defer to
  // release, so a square-constraint toggle just redraws the candidate outline.
  if (selection_operation_ == SelectionMode::Replace) {
    combine_selection_from_region(marquee_selection_region(selection_start_, selection_current_));
  }
  emit_info_for_widget_position(last_mouse_position_);
  update();
}

bool CanvasWidget::can_move_selection_at(QPoint document_point, Qt::KeyboardModifiers modifiers) const {
  // Photoshop lets any selection tool drag an existing selection by grabbing
  // inside it, but only in Replace mode (Shift/Alt/Add/Subtract keep editing the
  // selection's shape instead of moving it).
  const bool selection_tool =
      tool_ == CanvasTool::Marquee || tool_ == CanvasTool::EllipticalMarquee || tool_ == CanvasTool::Lasso;
  return selection_tool && !selection_.isEmpty() &&
         selection_operation(modifiers) == SelectionMode::Replace && selection_.contains(document_point);
}

void CanvasWidget::apply_selection_move(QPoint delta) {
  // Translate the snapshot taken at the press by the cumulative delta so dragging
  // partway off-canvas and back is lossless (the setters re-clip to the canvas).
  if (selection_mask_before_edit_alpha_.isNull()) {
    set_selection_from_region(selection_before_edit_.translated(delta));
  } else {
    set_selection_from_mask(selection_before_edit_.translated(delta),
                            selection_mask_before_edit_bounds_.translated(delta),
                            selection_mask_before_edit_alpha_);
  }
}

void CanvasWidget::nudge_selection(QPoint delta) {
  if (selection_.isEmpty()) {
    return;
  }
  const auto before = capture_selection_snapshot();
  if (selection_mask_alpha_.isNull()) {
    set_selection_from_region(selection_.translated(delta));
  } else {
    set_selection_from_mask(selection_.translated(delta), selection_mask_bounds_.translated(delta),
                            selection_mask_alpha_);
  }
  emit_info_for_widget_position(last_mouse_position_);
  update();
  // Coalesce consecutive nudges (incl. key auto-repeat) into one undo step that
  // returns to the position before the run started.
  record_selection_history(tr("Move Selection"), before, /*coalesce=*/true);
}

void CanvasWidget::restore_selection_before_edit() {
  invalidate_selection_outline();
  selection_ = selection_before_edit_;
  selection_display_region_ = selection_display_region_before_edit_;
  if (selection_display_region_.isEmpty() && !selection_.isEmpty()) {
    selection_display_region_ = selection_;
  }
  selection_mask_bounds_ = selection_mask_before_edit_bounds_;
  selection_mask_alpha_ = selection_mask_before_edit_alpha_;
  refresh_info_display();
}

CanvasWidget::SelectionSnapshot CanvasWidget::capture_selection_snapshot() const {
  return SelectionSnapshot{
      selection_, selection_display_region_, selection_mask_bounds_,
      selection_mask_alpha_,
      quick_mask_active_ ? std::optional<PixelBuffer>{quick_mask_pixels_}
                         : std::nullopt};
}

void CanvasWidget::apply_selection_snapshot(const SelectionSnapshot& snapshot) {
  invalidate_selection_outline();
  selection_ = snapshot.selection;
  selection_display_region_ = snapshot.display_region;
  if (selection_display_region_.isEmpty() && !selection_.isEmpty()) {
    selection_display_region_ = selection_;
  }
  selection_mask_bounds_ = snapshot.mask_bounds;
  selection_mask_alpha_ = snapshot.mask_alpha;
  if (quick_mask_active_) {
    if (snapshot.quick_mask_pixels.has_value()) {
      quick_mask_pixels_ = *snapshot.quick_mask_pixels;
    } else {
      quick_mask_active_ = false;
      quick_mask_pixels_ = selection_as_grayscale();
      quick_mask_active_ = true;
    }
    ++quick_mask_revision_;
    invalidate_quick_mask_display();
    if (quick_mask_changed_callback_) {
      quick_mask_changed_callback_();
    }
  } else if (snapshot.quick_mask_pixels.has_value()) {
    apply_grayscale_to_selection(*snapshot.quick_mask_pixels);
  }
  refresh_info_display();
  update();
}

void CanvasWidget::set_selection_dash_offset_for_testing(int offset) {
  selection_timer_.stop();
  selection_dash_offset_ = ((offset % 8) + 8) % 8;
  update();
}

CanvasWidget::SelectionSnapshot CanvasWidget::selection_snapshot_before_edit() const {
  return SelectionSnapshot{selection_before_edit_, selection_display_region_before_edit_,
                           selection_mask_before_edit_bounds_,
                           selection_mask_before_edit_alpha_, std::nullopt};
}

namespace {
bool pixel_buffers_equal(const PixelBuffer& a, const PixelBuffer& b) {
  return a.width() == b.width() && a.height() == b.height() &&
         a.format() == b.format() &&
         std::equal(a.data().begin(), a.data().end(), b.data().begin(),
                    b.data().end());
}

bool selection_snapshots_equal(const CanvasWidget::SelectionSnapshot& a,
                               const CanvasWidget::SelectionSnapshot& b) {
  // Compare the committed selection region and any soft-edge mask; the display
  // region is derived from these, so it does not need its own comparison.
  if (a.selection != b.selection || a.mask_bounds != b.mask_bounds ||
      a.mask_alpha != b.mask_alpha ||
      a.quick_mask_pixels.has_value() != b.quick_mask_pixels.has_value()) {
    return false;
  }
  return !a.quick_mask_pixels.has_value() ||
         pixel_buffers_equal(*a.quick_mask_pixels, *b.quick_mask_pixels);
}
}  // namespace

void CanvasWidget::record_selection_history(QString label, const SelectionSnapshot& before, bool coalesce) {
  if (!selection_history_callback_) {
    return;
  }
  if (selection_snapshots_equal(before, capture_selection_snapshot())) {
    return;
  }
  selection_history_callback_(std::move(label), before, coalesce);
}

void CanvasWidget::run_selection_command(QString label, const std::function<void()>& command) {
  const auto before = capture_selection_snapshot();
  command();
  record_selection_history(std::move(label), before);
}

void CanvasWidget::finish_quick_mask_edit() {
  if (!quick_mask_edit_before_.has_value()) {
    return;
  }
  auto before = std::move(*quick_mask_edit_before_);
  auto label = std::move(quick_mask_edit_label_);
  quick_mask_edit_before_.reset();
  quick_mask_edit_label_.clear();
  quick_mask_edit_dirty_ = QRegion();
  record_selection_history(std::move(label), before);
  if (quick_mask_changed_callback_) {
    quick_mask_changed_callback_();
  }
}

void CanvasWidget::combine_selection_from_region(const QRegion& candidate) {
  if (selection_mask_before_edit_alpha_.isNull()) {
    set_selection_from_region(combine_selection(candidate));
    return;
  }
  const auto candidate_bounds = candidate.boundingRect();
  combine_selection_from_mask(candidate, candidate_bounds, hard_mask_from_region(candidate, candidate_bounds));
}

void CanvasWidget::combine_selection_from_mask(QRect candidate_bounds, QImage candidate_alpha) {
  auto candidate = region_from_alpha_mask(candidate_alpha, candidate_bounds);
  combine_selection_from_mask(std::move(candidate), candidate_bounds, std::move(candidate_alpha));
}

void CanvasWidget::combine_selection_from_mask(QRegion candidate, QRect candidate_bounds, QImage candidate_alpha) {
  if (candidate.isEmpty() || candidate_bounds.isEmpty() || candidate_alpha.isNull()) {
    if (selection_operation_ == SelectionMode::Replace) {
      set_selection_from_region(QRegion());
    } else {
      restore_selection_before_edit();
    }
    return;
  }

  if (selection_mask_before_edit_alpha_.isNull() && selection_operation_ == SelectionMode::Replace) {
    set_selection_from_mask(candidate, candidate_bounds, std::move(candidate_alpha));
    return;
  }

  if (selection_mask_before_edit_alpha_.isNull() && selection_operation_ != SelectionMode::Replace &&
      selection_before_edit_.isEmpty()) {
    set_selection_from_mask(selection_operation_ == SelectionMode::Subtract ? QRegion() : candidate, candidate_bounds,
                            selection_operation_ == SelectionMode::Subtract ? QImage() : std::move(candidate_alpha));
    return;
  }

  QRect bounds = candidate_bounds;
  if (!selection_mask_before_edit_alpha_.isNull()) {
    bounds = bounds.united(selection_mask_before_edit_bounds_);
  } else if (!selection_before_edit_.isEmpty()) {
    bounds = bounds.united(selection_before_edit_.boundingRect());
  }
  if (document_ != nullptr) {
    bounds = bounds.intersected(QRect(0, 0, document_->width(), document_->height()));
  }
  if (bounds.isEmpty()) {
    set_selection_from_region({});
    return;
  }

  QImage combined(bounds.size(), QImage::Format_Grayscale8);
  combined.fill(0);
  for (int y = 0; y < bounds.height(); ++y) {
    auto* dst = combined.scanLine(y);
    const auto document_y = bounds.y() + y;
    for (int x = 0; x < bounds.width(); ++x) {
      const QPoint point(bounds.x() + x, document_y);
      const auto base_alpha =
          selection_mask_before_edit_alpha_.isNull()
              ? static_cast<std::uint8_t>(selection_before_edit_.contains(point) ? 255 : 0)
              : alpha_at(selection_mask_before_edit_alpha_, selection_mask_before_edit_bounds_, point);
      const auto candidate_value = alpha_at(candidate_alpha, candidate_bounds, point);

      std::uint8_t result = 0;
      switch (selection_operation_) {
        case SelectionMode::Replace:
          result = candidate_value;
          break;
        case SelectionMode::Add:
          result = std::max(base_alpha, candidate_value);
          break;
        case SelectionMode::Intersect:
          result = std::min(base_alpha, candidate_value);
          break;
        case SelectionMode::Subtract:
          result = static_cast<std::uint8_t>((static_cast<int>(base_alpha) * (255 - candidate_value)) / 255);
          break;
      }
      dst[x] = result;
    }
  }

  // Compute the region before the call: as a sibling argument of
  // std::move(combined) it would read the image after the move emptied it
  // (argument evaluation order is unspecified; MSVC goes right-to-left).
  auto combined_region = region_from_alpha_mask(combined, bounds);
  set_selection_from_mask(std::move(combined_region), bounds, std::move(combined));
}

}  // namespace patchy::ui
