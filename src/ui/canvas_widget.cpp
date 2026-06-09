#include "ui/canvas_widget.hpp"

#include "core/adjustment_layer.hpp"
#include "core/blend_math.hpp"
#include "core/layer_metadata.hpp"
#include "core/layer_render_utils.hpp"
#include "core/layer_tree.hpp"
#include "core/pixel_tools.hpp"
#include "ui/edit_conversions.hpp"
#include "ui/image_document_io.hpp"
#include "ui/qt_geometry.hpp"

#include <QApplication>
#include <QCursor>
#include <QEnterEvent>
#include <QEventLoop>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QInputDevice>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPointingDevice>
#include <QPolygon>
#include <QPolygonF>
#include <QPointer>
#include <QRadialGradient>
#include <QResizeEvent>
#include <QTabletEvent>
#include <QTimerEvent>
#include <QTransform>
#include <QWheelEvent>
#include <QtGlobal>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cmath>
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

constexpr double kPi = 3.14159265358979323846;
constexpr double kMinZoom = 0.05;
constexpr double kMaxZoom = 128.0;
constexpr double kPixelAlignedZoom = 1.0;
constexpr double kDeepZoomPixelRendererZoom = 8.0;
constexpr double kMinimumVisibleDocumentFraction = 0.10;
constexpr int kTopRulerHeight = 24;
constexpr int kLeftRulerWidth = 32;
constexpr double kSnapToleranceScreenPixels = 8.0;
constexpr int kMagicWandCursorSize = 24;
constexpr int kMagicWandCursorHotspotX = 6;
constexpr int kMagicWandCursorHotspotY = 6;
constexpr double kMinimumTransformScalePercent = 0.01;
constexpr std::int64_t kMoveOutlineDirtyAreaThreshold = 4'000'000;
constexpr std::int64_t kStyledMoveOutlineDirtyAreaThreshold = 1'000'000;
constexpr std::int64_t kProcessingOverlayDirtyAreaThreshold = 8'000'000;
constexpr int kProcessingOverlayDelayMs = 1000;
constexpr int kProcessingAnimationIntervalMs = 80;
constexpr int kMaxDirtyRegionRects = 64;

bool render_trace_enabled() noexcept {
  static const bool enabled = qEnvironmentVariableIsSet("PATCHY_RENDER_TRACE");
  return enabled;
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

QCursor magic_wand_cursor() {
  static const QCursor cursor = [] {
    QPixmap pixmap(kMagicWandCursorSize, kMagicWandCursorSize);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    const QPointF hotspot(kMagicWandCursorHotspotX, kMagicWandCursorHotspotY);

    painter.setPen(QPen(QColor(18, 20, 24), 4.2, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(hotspot + QPointF(3.0, 3.0), QPointF(21.0, 21.0));
    painter.setPen(QPen(QColor(245, 248, 252), 2.1, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(hotspot + QPointF(3.0, 3.0), QPointF(21.0, 21.0));

    painter.setPen(QPen(QColor(18, 20, 24), 3.0, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(hotspot + QPointF(-5.0, 0.0), hotspot + QPointF(-1.5, 0.0));
    painter.drawLine(hotspot + QPointF(1.5, 0.0), hotspot + QPointF(5.0, 0.0));
    painter.drawLine(hotspot + QPointF(0.0, -5.0), hotspot + QPointF(0.0, -1.5));
    painter.drawLine(hotspot + QPointF(0.0, 1.5), hotspot + QPointF(0.0, 5.0));
    painter.setPen(QPen(QColor(80, 170, 255), 1.4, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(hotspot + QPointF(-5.0, 0.0), hotspot + QPointF(-1.5, 0.0));
    painter.drawLine(hotspot + QPointF(1.5, 0.0), hotspot + QPointF(5.0, 0.0));
    painter.drawLine(hotspot + QPointF(0.0, -5.0), hotspot + QPointF(0.0, -1.5));
    painter.drawLine(hotspot + QPointF(0.0, 1.5), hotspot + QPointF(0.0, 5.0));

    painter.setPen(QPen(QColor(80, 170, 255), 1.5, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPointF(17.0, 4.0), QPointF(17.0, 8.0));
    painter.drawLine(QPointF(15.0, 6.0), QPointF(19.0, 6.0));
    painter.drawLine(QPointF(21.0, 10.0), QPointF(21.0, 13.0));
    painter.drawLine(QPointF(19.5, 11.5), QPointF(22.5, 11.5));
    painter.end();

    return QCursor(pixmap, kMagicWandCursorHotspotX, kMagicWandCursorHotspotY);
  }();
  return cursor;
}

double guide_position_pixels(const DocumentGuide& guide) noexcept {
  return static_cast<double>(guide.position_32) / 32.0;
}

std::int32_t guide_position_32(double pixels) noexcept {
  return static_cast<std::int32_t>(std::lround(pixels * 32.0));
}

double grid_cycle_pixels(std::int32_t cycle_32) noexcept {
  return static_cast<double>(std::max<std::int32_t>(1, cycle_32)) / 32.0;
}

bool uses_pixel_aligned_view(double zoom) noexcept {
  return zoom >= kPixelAlignedZoom;
}

bool uses_deep_zoom_pixel_renderer(double zoom) noexcept {
  return zoom >= kDeepZoomPixelRendererZoom;
}

bool uses_smooth_display_scaling(double zoom, bool deep_pixel_renderer) noexcept {
  if (deep_pixel_renderer) {
    return false;
  }
  return zoom < 1.0;
}

int display_mip_level_for_zoom(double zoom) noexcept {
  if (zoom >= 1.0 || zoom <= 0.0 || !std::isfinite(zoom)) {
    return 0;
  }

  int level = 0;
  double level_factor = 1.0;
  while (level < 24 && zoom * level_factor * 2.0 <= 1.0) {
    ++level;
    level_factor *= 2.0;
  }
  return level;
}

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

double pixel_aligned_coordinate(double coordinate, double zoom) noexcept {
  return uses_pixel_aligned_view(zoom) ? std::round(coordinate) : coordinate;
}

double ruler_major_tick_interval(double zoom) noexcept {
  constexpr std::array<double, 16> intervals{1.0,   2.0,   5.0,   10.0,   20.0,   50.0,
                                             100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0,
                                             10000.0, 20000.0, 50000.0, 100000.0};
  for (const auto interval : intervals) {
    if (interval * zoom >= 52.0) {
      return interval;
    }
  }
  return intervals.back();
}

double ruler_minor_tick_interval(double zoom) noexcept {
  return std::max(1.0, ruler_major_tick_interval(zoom) / 5.0);
}

void append_rect_snap_candidates(QRect rect, std::vector<double>& x_candidates, std::vector<double>& y_candidates) {
  if (rect.isEmpty()) {
    return;
  }
  const auto left = static_cast<double>(rect.left());
  const auto top = static_cast<double>(rect.top());
  const auto right = static_cast<double>(rect.right() + 1);
  const auto bottom = static_cast<double>(rect.bottom() + 1);
  x_candidates.push_back(left);
  x_candidates.push_back((left + right) * 0.5);
  x_candidates.push_back(right);
  y_candidates.push_back(top);
  y_candidates.push_back((top + bottom) * 0.5);
  y_candidates.push_back(bottom);
}

void append_layer_snap_candidates(const std::vector<Layer>& layers, std::vector<double>& x_candidates,
                                  std::vector<double>& y_candidates) {
  for (const auto& layer : layers) {
    if (!layer.visible()) {
      continue;
    }
    append_rect_snap_candidates(to_qrect(layer_render_bounds(layer)), x_candidates, y_candidates);
    if (layer.kind() == LayerKind::Group) {
      append_layer_snap_candidates(layer.children(), x_candidates, y_candidates);
    }
  }
}

double constrained_document_axis(double pan, double viewport_span, double document_span) noexcept {
  if (!std::isfinite(pan) || viewport_span <= 0.0 || document_span <= 0.0) {
    return pan;
  }

  const auto minimum_visible =
      std::max(1.0, std::min(viewport_span, document_span) * kMinimumVisibleDocumentFraction);
  return std::clamp(pan, minimum_visible - document_span, viewport_span - minimum_visible);
}

std::uint8_t channel_from_color(QColor color, int channel) {
  switch (channel) {
    case 0:
      return static_cast<std::uint8_t>(color.red());
    case 1:
      return static_cast<std::uint8_t>(color.green());
    case 2:
      return static_cast<std::uint8_t>(color.blue());
    default:
      return static_cast<std::uint8_t>(color.alpha());
  }
}

QRect normalized_rect(QPoint a, QPoint b) {
  return QRect(a, b).normalized();
}

double point_distance(QPointF a, QPointF b) {
  return std::hypot(a.x() - b.x(), a.y() - b.y());
}

QPointF midpoint(QPointF a, QPointF b) {
  return QPointF((a.x() + b.x()) * 0.5, (a.y() + b.y()) * 0.5);
}

QPointF quadratic_point(QPointF start, QPointF control, QPointF end, double t) {
  const auto inverse = 1.0 - t;
  const auto start_weight = inverse * inverse;
  const auto control_weight = 2.0 * inverse * t;
  const auto end_weight = t * t;
  return QPointF(start.x() * start_weight + control.x() * control_weight + end.x() * end_weight,
                 start.y() * start_weight + control.y() * control_weight + end.y() * end_weight);
}

QRect united_dirty_rect(QRect a, QRect b) {
  if (a.isEmpty()) {
    return b;
  }
  if (b.isEmpty()) {
    return a;
  }
  return a.united(b);
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

template <typename Callback>
void visit_pixel_line(QPoint from, QPoint to, Callback&& callback) {
  auto x0 = from.x();
  auto y0 = from.y();
  const auto x1 = to.x();
  const auto y1 = to.y();
  const auto dx = std::abs(x1 - x0);
  const auto sx = x0 < x1 ? 1 : -1;
  const auto dy = -std::abs(y1 - y0);
  const auto sy = y0 < y1 ? 1 : -1;
  auto error = dx + dy;

  while (true) {
    callback(QPoint(x0, y0));
    if (x0 == x1 && y0 == y1) {
      break;
    }
    const auto doubled_error = error * 2;
    if (doubled_error >= dy) {
      error += dy;
      x0 += sx;
    }
    if (doubled_error <= dx) {
      error += dx;
      y0 += sy;
    }
  }
}

bool same_pixel(const std::uint8_t* pixel, const std::vector<std::uint8_t>& target, std::uint16_t channels) {
  for (std::uint16_t channel = 0; channel < channels; ++channel) {
    if (pixel[channel] != target[channel]) {
      return false;
    }
  }
  return true;
}

float brush_coverage(double distance_squared, int radius, int softness) {
  if (radius <= 0) {
    return distance_squared <= 0.0 ? 1.0F : 0.0F;
  }

  const auto radius_squared = static_cast<double>(radius) * static_cast<double>(radius);
  if (distance_squared > radius_squared) {
    return 0.0F;
  }

  softness = std::clamp(softness, 0, 100);
  if (softness <= 0) {
    return 1.0F;
  }

  const auto edge_width = std::max(1.0, static_cast<double>(radius) * static_cast<double>(softness) / 100.0);
  const auto inner_radius = std::max(0.0, static_cast<double>(radius) - edge_width);
  const auto distance = std::sqrt(distance_squared);
  if (distance <= inner_radius) {
    return 1.0F;
  }
  const auto t = std::clamp((distance - inner_radius) / edge_width, 0.0, 1.0);
  const auto smooth = t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
  return static_cast<float>(1.0 - smooth);
}

float brush_shape_coverage(double distance_x, double distance_y, int radius, int softness, int roundness_percent,
                           double angle_degrees) {
  roundness_percent = std::clamp(roundness_percent, 1, 100);
  if (radius <= 0 || roundness_percent >= 99) {
    return brush_coverage(distance_x * distance_x + distance_y * distance_y, radius, softness);
  }

  const auto major_radius = static_cast<double>(radius);
  const auto minor_radius = std::max(0.5, major_radius * static_cast<double>(roundness_percent) / 100.0);
  const auto angle = angle_degrees * kPi / 180.0;
  const auto c = std::cos(angle);
  const auto s = std::sin(angle);
  const auto major_axis = c * distance_x + s * distance_y;
  const auto minor_axis = -s * distance_x + c * distance_y;
  const auto normalized_distance_squared =
      (major_axis * major_axis) / (major_radius * major_radius) +
      (minor_axis * minor_axis) / (minor_radius * minor_radius);
  return brush_coverage(normalized_distance_squared * major_radius * major_radius, radius, softness);
}

void blend_straight_rgba(std::uint8_t* dst, const std::uint8_t* src, float amount) {
  amount = std::clamp(amount, 0.0F, 1.0F);
  if (amount <= 0.0F) {
    return;
  }
  if (amount >= 0.999F) {
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
    dst[3] = src[3];
    return;
  }

  const auto source_alpha = static_cast<float>(src[3]) / 255.0F;
  const auto destination_alpha = static_cast<float>(dst[3]) / 255.0F;
  const auto out_alpha = source_alpha * amount + destination_alpha * (1.0F - amount);
  if (out_alpha <= 0.0F) {
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
    dst[3] = 0;
    return;
  }

  for (int channel = 0; channel < 3; ++channel) {
    const auto source_premultiplied = static_cast<float>(src[channel]) * source_alpha;
    const auto destination_premultiplied = static_cast<float>(dst[channel]) * destination_alpha;
    dst[channel] =
        clamp_byte((source_premultiplied * amount + destination_premultiplied * (1.0F - amount)) / out_alpha);
  }
  dst[3] = clamp_byte(out_alpha * 255.0F);
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

bool pixel_layer_contains_document_point(const Layer& layer, QPoint document_point, bool require_visible_pixel) {
  if (!layer.visible() || layer.opacity() <= 0.0F || layer.kind() != LayerKind::Pixel) {
    return false;
  }
  const auto& pixels = layer.pixels();
  if (pixels.empty() || pixels.format().bit_depth != BitDepth::UInt8 || pixels.format().channels < 3) {
    return false;
  }
  const auto bounds = layer.bounds();
  if (!bounds.contains(document_point.x(), document_point.y())) {
    return false;
  }
  const auto local_x = document_point.x() - bounds.x;
  const auto local_y = document_point.y() - bounds.y;
  if (local_x < 0 || local_y < 0 || local_x >= pixels.width() || local_y >= pixels.height()) {
    return false;
  }
  if (require_visible_pixel) {
    const auto source_alpha = pixels.format().channels >= 4 ? pixels.pixel(local_x, local_y)[3] : 255;
    const auto mask_alpha =
        static_cast<int>(std::round(layer_mask_alpha_at(layer, document_point.x(), document_point.y()) * 255.0F));
    if (std::min(static_cast<int>(source_alpha), mask_alpha) < 8) {
      return false;
    }
  }
  return true;
}

std::optional<QRect> opaque_pixel_local_rect(const Layer& layer);
std::optional<Rect> opaque_pixel_document_bounds(const Layer& layer);

bool layer_has_movable_pixels(const Layer& layer) {
  if (layer.kind() != LayerKind::Pixel && !layer_is_text(layer)) {
    return false;
  }
  const auto& pixels = layer.pixels();
  return !pixels.empty() && pixels.format().bit_depth == BitDepth::UInt8 && pixels.format().channels >= 3;
}

bool move_layer_contains_document_point(const Layer& layer, QPoint document_point) {
  if (!layer.visible() || layer.opacity() <= 0.0F || !layer_has_movable_pixels(layer)) {
    return false;
  }
  if (layer_is_text(layer)) {
    return layer.bounds().contains(document_point.x(), document_point.y());
  }
  return pixel_layer_contains_document_point(layer, document_point, true);
}

std::optional<Rect> move_layer_outline_bounds(const Layer& layer) {
  if (!layer_has_movable_pixels(layer)) {
    return std::nullopt;
  }
  if (layer_is_text(layer)) {
    const auto bounds = layer.bounds();
    if (bounds.empty()) {
      return std::nullopt;
    }
    return bounds;
  }
  return opaque_pixel_document_bounds(layer);
}

bool move_layer_has_expensive_style(const Layer& layer) {
  const auto& style = layer.layer_style();
  return style.effects_visible && !style.empty();
}

std::optional<QRect> move_layer_transform_local_rect(const Layer& layer) {
  if (!layer_has_movable_pixels(layer)) {
    return std::nullopt;
  }
  if (layer_is_text(layer)) {
    const auto bounds = layer.bounds();
    if (bounds.empty()) {
      return std::nullopt;
    }
    return QRect(0, 0, bounds.width, bounds.height);
  }
  return opaque_pixel_local_rect(layer);
}

Layer* topmost_pixel_layer_at_recursive(std::vector<Layer>& layers, QPoint document_point, bool require_visible_pixel,
                                        bool skip_locked,
                                        LayerLockFlags ancestor_lock_flags = kLayerLockNone) {
  for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
    auto& layer = *it;
    if (!layer.visible() || layer.opacity() <= 0.0F) {
      continue;
    }
    const auto effective_lock_flags = ancestor_lock_flags | patchy::layer_lock_flags(layer);
    if (layer.kind() == LayerKind::Group) {
      if (auto* found =
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

Layer* topmost_move_layer_at_recursive(std::vector<Layer>& layers, QPoint document_point, bool skip_locked,
                                       LayerLockFlags ancestor_lock_flags = kLayerLockNone) {
  for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
    auto& layer = *it;
    if (!layer.visible() || layer.opacity() <= 0.0F) {
      continue;
    }
    const auto effective_lock_flags = ancestor_lock_flags | patchy::layer_lock_flags(layer);
    if (layer.kind() == LayerKind::Group) {
      if (auto* found = topmost_move_layer_at_recursive(layer.children(), document_point, skip_locked,
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

Layer* topmost_text_layer_at_recursive(std::vector<Layer>& layers, QPoint document_point) {
  for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
    auto& layer = *it;
    if (!layer.visible()) {
      continue;
    }
    if (layer.kind() == LayerKind::Group) {
      if (auto* found = topmost_text_layer_at_recursive(layer.children(), document_point); found != nullptr) {
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

QPoint clamped_document_point(const Document& document, QPoint point) {
  return QPoint(std::clamp(point.x(), 0, std::max(0, document.width() - 1)),
                std::clamp(point.y(), 0, std::max(0, document.height() - 1)));
}

std::uint8_t mask_value_from_color(QColor color) {
  return static_cast<std::uint8_t>(
      std::clamp((color.red() * 30 + color.green() * 59 + color.blue() * 11) / 100, 0, 255));
}

std::uint8_t blend_mask_value(std::uint8_t current, std::uint8_t value, float coverage) {
  coverage = std::clamp(coverage, 0.0F, 1.0F);
  return static_cast<std::uint8_t>(
      std::clamp(static_cast<int>(std::lround(static_cast<float>(value) * coverage +
                                              static_cast<float>(current) * (1.0F - coverage))),
                 0, 255));
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

QRegion region_from_mask(const std::vector<std::uint8_t>& selected, int width, int height,
                         int min_x, int min_y, int max_x, int max_y) {
  if (selected.empty() || width <= 0 || height <= 0 || min_x > max_x || min_y > max_y) {
    return {};
  }

  min_x = std::clamp(min_x, 0, width - 1);
  max_x = std::clamp(max_x, 0, width - 1);
  min_y = std::clamp(min_y, 0, height - 1);
  max_y = std::clamp(max_y, 0, height - 1);

  std::vector<QRect> runs;
  runs.reserve(static_cast<std::size_t>(std::max(1, max_y - min_y + 1)));
  for (int y = min_y; y <= max_y; ++y) {
    int run_start = -1;
    for (int x = min_x; x <= max_x + 1; ++x) {
      const bool is_selected =
          x <= max_x && selected[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                 static_cast<std::size_t>(x)] != 0U;
      if (is_selected && run_start < 0) {
        run_start = x;
      } else if (!is_selected && run_start >= 0) {
        runs.push_back(QRect(run_start, y, x - run_start, 1));
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

QImage hard_mask_from_region(const QRegion& region, QRect bounds) {
  bounds = bounds.normalized();
  if (region.isEmpty() || bounds.isEmpty()) {
    return {};
  }

  QImage mask(bounds.size(), QImage::Format_Grayscale8);
  mask.fill(0);
  for (const auto& rect : region.intersected(bounds)) {
    const auto local = rect.translated(-bounds.topLeft()).intersected(QRect(QPoint(), bounds.size()));
    for (int y = local.top(); y <= local.bottom(); ++y) {
      auto* row = mask.scanLine(y);
      std::fill(row + local.left(), row + local.right() + 1, static_cast<uchar>(255));
    }
  }
  return mask;
}

QImage box_blur_mask(const QImage& source, int radius) {
  if (source.isNull() || source.format() != QImage::Format_Grayscale8 || radius <= 0) {
    return source;
  }

  const auto width = source.width();
  const auto height = source.height();
  if (width <= 0 || height <= 0) {
    return source;
  }

  radius = std::clamp(radius, 0, 250);
  const auto window = radius * 2 + 1;
  QImage horizontal(source.size(), QImage::Format_Grayscale8);
  QImage blurred(source.size(), QImage::Format_Grayscale8);

  for (int y = 0; y < height; ++y) {
    const auto* src = source.constScanLine(y);
    auto* dst = horizontal.scanLine(y);
    int sum = 0;
    for (int offset = -radius; offset <= radius; ++offset) {
      if (offset >= 0 && offset < width) {
        sum += src[offset];
      }
    }
    for (int x = 0; x < width; ++x) {
      dst[x] = static_cast<uchar>(sum / window);
      const auto remove_x = x - radius;
      const auto add_x = x + radius + 1;
      if (remove_x >= 0 && remove_x < width) {
        sum -= src[remove_x];
      }
      if (add_x >= 0 && add_x < width) {
        sum += src[add_x];
      }
    }
  }

  for (int x = 0; x < width; ++x) {
    int sum = 0;
    for (int offset = -radius; offset <= radius; ++offset) {
      if (offset >= 0 && offset < height) {
        sum += horizontal.constScanLine(offset)[x];
      }
    }
    for (int y = 0; y < height; ++y) {
      blurred.scanLine(y)[x] = static_cast<uchar>(sum / window);
      const auto remove_y = y - radius;
      const auto add_y = y + radius + 1;
      if (remove_y >= 0 && remove_y < height) {
        sum -= horizontal.constScanLine(remove_y)[x];
      }
      if (add_y >= 0 && add_y < height) {
        sum += horizontal.constScanLine(add_y)[x];
      }
    }
  }

  return blurred;
}

int feather_blur_pass_radius(int feather_radius) {
  return std::max(1, (std::clamp(feather_radius, 0, 250) + 1) / 2);
}

int feather_mask_padding(int feather_radius) {
  if (feather_radius <= 0) {
    return 0;
  }
  return feather_blur_pass_radius(feather_radius) * 3 + 1;
}

QImage feather_blur_mask(QImage mask, int feather_radius) {
  if (mask.isNull() || feather_radius <= 0) {
    return mask;
  }
  const auto pass_radius = feather_blur_pass_radius(feather_radius);
  for (int pass = 0; pass < 3; ++pass) {
    mask = box_blur_mask(mask, pass_radius);
  }
  return mask;
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

QImage layer_source_image(const Layer& layer) {
  const auto& pixels = layer.pixels();
  QImage image(pixels.width(), pixels.height(), QImage::Format_RGBA8888);
  image.fill(Qt::transparent);
  if (pixels.empty() || pixels.format().bit_depth != BitDepth::UInt8 || pixels.format().channels < 3) {
    return image;
  }

  for (int y = 0; y < pixels.height(); ++y) {
    for (int x = 0; x < pixels.width(); ++x) {
      const auto* px = pixels.pixel(x, y);
      image.setPixelColor(x, y, QColor(px[0], px[1], px[2], pixels.format().channels >= 4 ? px[3] : 255));
    }
  }
  return image;
}

bool layer_needs_composited_transform_preview(const Layer& layer) {
  return std::abs(layer.opacity() - 1.0F) > 0.001F || layer.blend_mode() != BlendMode::Normal ||
         (layer.mask().has_value() && !layer.mask()->disabled) ||
         (layer.layer_style().effects_visible && !layer.layer_style().empty());
}

QImage active_layer_wand_sample_image(const Layer& layer, QSize document_size) {
  QImage image(document_size, QImage::Format_RGBA8888);
  image.fill(Qt::transparent);
  if (document_size.isEmpty() || !layer.visible() || layer.opacity() <= 0.0F) {
    return image;
  }

  const auto& pixels = layer.pixels();
  if (pixels.empty() || pixels.format().bit_depth != BitDepth::UInt8 || pixels.format().channels < 3) {
    return image;
  }

  const auto bounds = layer_pixel_bounds(layer);
  const QRect canvas_rect(QPoint(), document_size);
  const auto draw_rect = to_qrect(bounds).intersected(canvas_rect);
  if (draw_rect.isEmpty()) {
    return image;
  }

  const auto channels = pixels.format().channels;
  for (int y = draw_rect.top(); y <= draw_rect.bottom(); ++y) {
    auto* output = image.scanLine(y) + static_cast<std::size_t>(draw_rect.left()) * 4U;
    for (int x = draw_rect.left(); x <= draw_rect.right(); ++x) {
      const auto* src = pixels.pixel(x - bounds.x, y - bounds.y);
      const auto source_alpha = channels >= 4 ? static_cast<float>(src[3]) / 255.0F : 1.0F;
      const auto alpha = source_alpha * layer_mask_alpha_at(layer, x, y) * layer.opacity();
      if (alpha > 0.0F) {
        output[0] = src[0];
        output[1] = src[1];
        output[2] = src[2];
        output[3] = clamp_byte(alpha * 255.0F);
      }
      output += 4;
    }
  }
  return image;
}

std::optional<QRect> opaque_pixel_local_rect(const Layer& layer) {
  const auto& pixels = layer.pixels();
  if (pixels.empty() || pixels.format().bit_depth != BitDepth::UInt8 || pixels.format().channels < 3) {
    return std::nullopt;
  }
  if (pixels.format().channels < 4) {
    return QRect(0, 0, pixels.width(), pixels.height());
  }

  int min_x = pixels.width();
  int min_y = pixels.height();
  int max_x = -1;
  int max_y = -1;
  for (int y = 0; y < pixels.height(); ++y) {
    for (int x = 0; x < pixels.width(); ++x) {
      if (pixels.pixel(x, y)[3] == 0) {
        continue;
      }
      min_x = std::min(min_x, x);
      min_y = std::min(min_y, y);
      max_x = std::max(max_x, x);
      max_y = std::max(max_y, y);
    }
  }

  if (max_x < min_x || max_y < min_y) {
    return std::nullopt;
  }
  return QRect(min_x, min_y, max_x - min_x + 1, max_y - min_y + 1);
}

std::optional<Rect> opaque_pixel_document_bounds(const Layer& layer) {
  const auto local_rect = opaque_pixel_local_rect(layer);
  if (!local_rect.has_value()) {
    return std::nullopt;
  }

  const auto bounds = layer.bounds();
  return Rect{bounds.x + local_rect->x(), bounds.y + local_rect->y(), local_rect->width(), local_rect->height()};
}

LayerAffineTransform affine_from_qtransform(const QTransform& transform) {
  return LayerAffineTransform{transform.m11(), transform.m12(), transform.m21(),
                              transform.m22(), transform.dx(),  transform.dy()};
}

LayerAffineTransform identity_text_transform_for_rect(QRectF rect) {
  return LayerAffineTransform{1.0, 0.0, 0.0, 1.0, rect.left(), rect.top()};
}

std::optional<LayerAffineTransform> stored_text_transform_for_layer(const Layer& layer) {
  const auto patchy_transform = layer.metadata().find(kLayerMetadataTextTransform);
  if (patchy_transform != layer.metadata().end()) {
    return parse_layer_affine_transform(patchy_transform->second);
  }
  const auto psd_transform = layer.metadata().find(kLayerMetadataPsdTextTransform);
  if (psd_transform != layer.metadata().end()) {
    return parse_layer_affine_transform(psd_transform->second);
  }
  return std::nullopt;
}

QTransform free_transform_delta(QRectF original_rect, QRectF current_rect, double angle_degrees,
                                double scale_x_sign, double scale_y_sign) {
  const auto original_width = std::max(1.0, original_rect.width());
  const auto original_height = std::max(1.0, original_rect.height());
  QTransform transform;
  transform.translate(current_rect.center().x(), current_rect.center().y());
  transform.rotate(angle_degrees);
  transform.scale(scale_x_sign * std::max(1.0, current_rect.width()) / original_width,
                  scale_y_sign * std::max(1.0, current_rect.height()) / original_height);
  transform.translate(-original_rect.center().x(), -original_rect.center().y());
  return transform;
}

QPointF anchor_offset_from_center(QSizeF size, CanvasAnchor anchor) {
  const auto half_width = size.width() / 2.0;
  const auto half_height = size.height() / 2.0;
  switch (anchor) {
    case CanvasAnchor::TopLeft:
      return QPointF(-half_width, -half_height);
    case CanvasAnchor::Top:
      return QPointF(0.0, -half_height);
    case CanvasAnchor::TopRight:
      return QPointF(half_width, -half_height);
    case CanvasAnchor::Left:
      return QPointF(-half_width, 0.0);
    case CanvasAnchor::Center:
      return QPointF(0.0, 0.0);
    case CanvasAnchor::Right:
      return QPointF(half_width, 0.0);
    case CanvasAnchor::BottomLeft:
      return QPointF(-half_width, half_height);
    case CanvasAnchor::Bottom:
      return QPointF(0.0, half_height);
    case CanvasAnchor::BottomRight:
      return QPointF(half_width, half_height);
  }
  return QPointF(0.0, 0.0);
}

QPointF rotate_offset(QPointF offset, double angle_degrees) {
  const auto radians = angle_degrees * kPi / 180.0;
  const auto c = std::cos(radians);
  const auto s = std::sin(radians);
  return QPointF(offset.x() * c - offset.y() * s, offset.x() * s + offset.y() * c);
}

QTransform transform_source_to_document(QSize source_size, QRectF current_rect, double angle_degrees,
                                        double scale_x_sign, double scale_y_sign) {
  QTransform transform;
  transform.translate(current_rect.center().x(), current_rect.center().y());
  transform.rotate(angle_degrees);
  transform.scale(scale_x_sign * std::max(1.0, current_rect.width()) / std::max(1, source_size.width()),
                  scale_y_sign * std::max(1.0, current_rect.height()) / std::max(1, source_size.height()));
  transform.translate(-static_cast<double>(source_size.width()) / 2.0,
                      -static_cast<double>(source_size.height()) / 2.0);
  return transform;
}

double transform_scale_sign(double percent, double fallback_sign) noexcept {
  if (percent < 0.0) {
    return -1.0;
  }
  if (percent > 0.0) {
    return 1.0;
  }
  return fallback_sign < 0.0 ? -1.0 : 1.0;
}

struct PremultipliedSample {
  double r{0.0};
  double g{0.0};
  double b{0.0};
  double a{0.0};
};

PremultipliedSample premultiplied_pixel(const QImage& image, int x, int y) {
  if (x < 0 || y < 0 || x >= image.width() || y >= image.height()) {
    return {};
  }
  const auto* pixel = image.constScanLine(y) + x * 4;
  const auto alpha = static_cast<double>(pixel[3]);
  return PremultipliedSample{static_cast<double>(pixel[0]) * alpha / 255.0,
                             static_cast<double>(pixel[1]) * alpha / 255.0,
                             static_cast<double>(pixel[2]) * alpha / 255.0,
                             alpha};
}

PremultipliedSample add_weighted(PremultipliedSample total, PremultipliedSample sample, double weight) {
  total.r += sample.r * weight;
  total.g += sample.g * weight;
  total.b += sample.b * weight;
  total.a += sample.a * weight;
  return total;
}

PremultipliedSample sample_nearest(const QImage& image, QPointF source_point) {
  if (source_point.x() < 0.0 || source_point.y() < 0.0 || source_point.x() >= image.width() ||
      source_point.y() >= image.height()) {
    return {};
  }
  return premultiplied_pixel(image, static_cast<int>(std::floor(source_point.x())),
                             static_cast<int>(std::floor(source_point.y())));
}

PremultipliedSample sample_bilinear(const QImage& image, QPointF source_point) {
  const auto x = source_point.x() - 0.5;
  const auto y = source_point.y() - 0.5;
  const auto x0 = static_cast<int>(std::floor(x));
  const auto y0 = static_cast<int>(std::floor(y));
  const auto tx = x - static_cast<double>(x0);
  const auto ty = y - static_cast<double>(y0);

  PremultipliedSample total;
  total = add_weighted(total, premultiplied_pixel(image, x0, y0), (1.0 - tx) * (1.0 - ty));
  total = add_weighted(total, premultiplied_pixel(image, x0 + 1, y0), tx * (1.0 - ty));
  total = add_weighted(total, premultiplied_pixel(image, x0, y0 + 1), (1.0 - tx) * ty);
  total = add_weighted(total, premultiplied_pixel(image, x0 + 1, y0 + 1), tx * ty);
  return total;
}

double cubic_weight(double distance) {
  const auto x = std::abs(distance);
  if (x < 1.0) {
    return (1.5 * x * x * x) - (2.5 * x * x) + 1.0;
  }
  if (x < 2.0) {
    return (-0.5 * x * x * x) + (2.5 * x * x) - (4.0 * x) + 2.0;
  }
  return 0.0;
}

PremultipliedSample sample_bicubic(const QImage& image, QPointF source_point) {
  const auto x = source_point.x() - 0.5;
  const auto y = source_point.y() - 0.5;
  const auto base_x = static_cast<int>(std::floor(x));
  const auto base_y = static_cast<int>(std::floor(y));
  PremultipliedSample total;
  for (int yy = -1; yy <= 2; ++yy) {
    const auto wy = cubic_weight(y - static_cast<double>(base_y + yy));
    for (int xx = -1; xx <= 2; ++xx) {
      const auto wx = cubic_weight(x - static_cast<double>(base_x + xx));
      total = add_weighted(total, premultiplied_pixel(image, base_x + xx, base_y + yy), wx * wy);
    }
  }
  return total;
}

std::uint8_t clamp_sample_channel(double value) {
  return static_cast<std::uint8_t>(std::clamp(std::lround(value), 0L, 255L));
}

struct TransformedImage {
  QImage image;
  Rect bounds{};
};

TransformedImage resample_transformed_rgba8(const QImage& source, const QTransform& source_to_document,
                                            CanvasWidget::TransformInterpolation interpolation) {
  const auto converted = source.convertToFormat(QImage::Format_RGBA8888);
  const auto mapped = source_to_document.mapRect(QRectF(0.0, 0.0, converted.width(), converted.height()));
  const auto left = static_cast<int>(std::floor(mapped.left()));
  const auto top = static_cast<int>(std::floor(mapped.top()));
  const auto right = static_cast<int>(std::ceil(mapped.right()));
  const auto bottom = static_cast<int>(std::ceil(mapped.bottom()));
  QImage transformed(std::max(1, right - left), std::max(1, bottom - top), QImage::Format_RGBA8888);
  transformed.fill(Qt::transparent);

  bool invertible = false;
  const auto document_to_source = source_to_document.inverted(&invertible);
  if (!invertible) {
    const auto bounds = Rect{left, top, transformed.width(), transformed.height()};
    return TransformedImage{std::move(transformed), bounds};
  }

  for (int y = 0; y < transformed.height(); ++y) {
    auto* row = transformed.scanLine(y);
    for (int x = 0; x < transformed.width(); ++x) {
      const auto source_point = document_to_source.map(QPointF(static_cast<double>(left + x) + 0.5,
                                                               static_cast<double>(top + y) + 0.5));
      PremultipliedSample sample;
      switch (interpolation) {
        case CanvasWidget::TransformInterpolation::NearestNeighbor:
          sample = sample_nearest(converted, source_point);
          break;
        case CanvasWidget::TransformInterpolation::Bilinear:
          sample = sample_bilinear(converted, source_point);
          break;
        case CanvasWidget::TransformInterpolation::Bicubic:
          sample = sample_bicubic(converted, source_point);
          break;
      }

      auto* pixel = row + x * 4;
      const auto alpha = clamp_sample_channel(sample.a);
      pixel[3] = alpha;
      if (alpha == 0) {
        pixel[0] = 0;
        pixel[1] = 0;
        pixel[2] = 0;
      } else {
        pixel[0] = clamp_sample_channel(sample.r * 255.0 / static_cast<double>(alpha));
        pixel[1] = clamp_sample_channel(sample.g * 255.0 / static_cast<double>(alpha));
        pixel[2] = clamp_sample_channel(sample.b * 255.0 / static_cast<double>(alpha));
      }
    }
  }

  const auto bounds = Rect{left, top, transformed.width(), transformed.height()};
  return TransformedImage{std::move(transformed), bounds};
}

EditOptions edit_options(QColor primary, QColor secondary, int brush_size, int brush_opacity, int brush_softness,
                         bool fill_shapes, bool lock_transparent_pixels, const CanvasWidget& canvas,
                         int brush_roundness = 100, double brush_angle_degrees = 0.0) {
  EditOptions options;
  primary.setAlpha(std::clamp(static_cast<int>(std::round(255.0 * static_cast<double>(brush_opacity) / 100.0)), 1, 255));
  options.primary = edit_color(primary);
  options.secondary = edit_color(secondary);
  options.brush_size = brush_size;
  options.brush_softness = brush_softness;
  options.brush_roundness = std::clamp(brush_roundness, 1, 100);
  options.brush_angle_degrees = brush_angle_degrees;
  options.fill_shapes = fill_shapes;
  options.lock_transparent_pixels = lock_transparent_pixels;
  options.progress_callback = [&canvas] {
    const_cast<CanvasWidget&>(canvas).tick_processing_operation();
  };
  if (canvas.selected_document_rect().has_value()) {
    options.selection = to_core_rect(*canvas.selected_document_rect());
    const auto region = canvas.selected_document_region();
    if (!canvas.selection_has_partial_alpha()) {
      options.selection_scan_rects.reserve(static_cast<std::size_t>(region.rectCount()));
      for (const auto& rect : region) {
        options.selection_scan_rects.push_back(to_core_rect(rect));
      }
    }
    options.selection_mask = [region](std::int32_t x, std::int32_t y) {
      return region.contains(QPoint(x, y));
    };
    options.selection_coverage = [&canvas](std::int32_t x, std::int32_t y) {
      return static_cast<float>(canvas.selection_alpha_at(QPoint(x, y))) / 255.0F;
    };
  }
  return options;
}

std::uint64_t stroke_pixel_key(std::int32_t x, std::int32_t y) noexcept {
  return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(y)) << 32U) |
         static_cast<std::uint32_t>(x);
}

bool tool_uses_alt_left_for_color_pick(CanvasTool tool) noexcept {
  switch (tool) {
    case CanvasTool::Brush:
    case CanvasTool::Smudge:
    case CanvasTool::Eraser:
    case CanvasTool::Gradient:
    case CanvasTool::Line:
    case CanvasTool::Rectangle:
    case CanvasTool::Ellipse:
    case CanvasTool::Fill:
      return true;
    default:
      return false;
  }
}

}  // namespace

CanvasWidget::CanvasWidget(QWidget* parent) : QWidget(parent) {
  setAutoFillBackground(false);
  setMouseTracking(true);
  setTabletTracking(true);
  setFocusPolicy(Qt::StrongFocus);
  selection_timer_.start(120, this);
}

void CanvasWidget::set_document(Document* document) {
  const auto old_transform_controls_rect = move_transform_controls_rect();
  cancel_free_transform();
  move_drag_pending_ = false;
  moving_layer_ = false;
  moving_layers_.clear();
  move_preview_delta_ = QPoint();
  move_preview_patches_.clear();
  move_preview_patches_delta_.reset();
  moving_layers_use_outline_preview_ = false;
  move_base_cache_ = QImage();
  document_ = document;
  set_move_transform_controls_layer(std::nullopt);
  selected_guide_index_ = -1;
  dragging_guide_ = false;
  creating_guide_ = false;
  guide_drag_remove_ = false;
  layer_edit_target_ = LayerEditTarget::Content;
  render_cache_ = QImage();
  render_cache_dirty_ = true;
  render_cache_diagnostics_ = {};
  if (document_ != nullptr && document_->metadata().psd_flat_composite.has_value()) {
    const auto& flat_composite = *document_->metadata().psd_flat_composite;
    // RGB-only PSD compatibility composites cannot preserve transparency, so
    // using them as a cache seed hides checkerboards until the next refresh.
    if (flat_composite.format().channels >= 4 && flat_composite.width() == document_->width() &&
        flat_composite.height() == document_->height()) {
      render_cache_ = qimage_from_flat_composite_pixels(flat_composite);
      render_cache_dirty_ = render_cache_.isNull();
    }
    document_->metadata().psd_flat_composite.reset();
  }
  invalidate_display_mip_cache();
  clear_move_hover_outline();
  update_move_transform_controls_dirty(old_transform_controls_rect);
  smudge_state_ = {};
  reset_axis_constrained_stroke();
  if (isVisible()) {
    constrain_pan();
  }
  update();
}

double CanvasWidget::zoom() const noexcept {
  return zoom_;
}

void CanvasWidget::set_zoom(double zoom) {
  const auto clamped = std::clamp(zoom, kMinZoom, kMaxZoom);
  if (std::abs(clamped - zoom_) < 0.0001) {
    return;
  }
  zoom_ = clamped;
  constrain_pan();
  update_tool_cursor();
  update();
  notify_view_changed();
}

void CanvasWidget::zoom_at_widget_point(QPointF widget_position, double factor) {
  if (factor <= 0.0 || !std::isfinite(factor)) {
    return;
  }
  const QPointF document_anchor((widget_position.x() - pan_.x()) / zoom_,
                                (widget_position.y() - pan_.y()) / zoom_);
  const auto old_zoom = zoom_;
  zoom_ = std::clamp(zoom_ * factor, kMinZoom, kMaxZoom);
  if (std::abs(old_zoom - zoom_) < 0.0001) {
    return;
  }
  pan_ = QPointF(widget_position.x() - document_anchor.x() * zoom_,
                 widget_position.y() - document_anchor.y() * zoom_);
  constrain_pan();
  update_tool_cursor();
  update();
  notify_view_changed();
}

void CanvasWidget::fit_to_view() {
  if (document_ == nullptr || document_->width() <= 0 || document_->height() <= 0 || width() <= 0 || height() <= 0) {
    return;
  }

  const auto available_width = std::max(1.0, static_cast<double>(width() - 80));
  const auto available_height = std::max(1.0, static_cast<double>(height() - 80));
  zoom_ = std::clamp(std::min(available_width / static_cast<double>(document_->width()),
                              available_height / static_cast<double>(document_->height())),
                     kMinZoom, kMaxZoom);
  pan_ = QPointF((static_cast<double>(width()) - static_cast<double>(document_->width()) * zoom_) / 2.0,
                 (static_cast<double>(height()) - static_cast<double>(document_->height()) * zoom_) / 2.0);
  constrain_pan();
  update();
  notify_view_changed();
}

void CanvasWidget::zoom_to_document_rect(QRect document_rect) {
  if (document_ == nullptr || document_->width() <= 0 || document_->height() <= 0 || width() <= 0 || height() <= 0) {
    return;
  }

  document_rect = document_rect.normalized().intersected(QRect(0, 0, document_->width(), document_->height()));
  if (document_rect.width() <= 1 || document_rect.height() <= 1) {
    return;
  }

  const auto available_width = std::max(1.0, static_cast<double>(width() - 80));
  const auto available_height = std::max(1.0, static_cast<double>(height() - 80));
  zoom_ = std::clamp(std::min(available_width / static_cast<double>(document_rect.width()),
                              available_height / static_cast<double>(document_rect.height())),
                     kMinZoom, kMaxZoom);
  pan_ = QPointF((static_cast<double>(width()) - static_cast<double>(document_rect.width()) * zoom_) / 2.0 -
                     static_cast<double>(document_rect.x()) * zoom_,
                 (static_cast<double>(height()) - static_cast<double>(document_rect.height()) * zoom_) / 2.0 -
                     static_cast<double>(document_rect.y()) * zoom_);
  constrain_pan();
  update_tool_cursor();
  update();
  notify_view_changed();
}

void CanvasWidget::set_spacebar_panning(bool enabled) {
  if (spacebar_panning_ == enabled) {
    return;
  }
  spacebar_panning_ = enabled;
  if (!panning_) {
    update_tool_cursor();
  }
}

bool CanvasWidget::begin_pan_at_global_position(QPoint global_position) {
  if (document_ == nullptr || document_->width() <= 0 || document_->height() <= 0) {
    return false;
  }
  clear_move_hover_outline();
  last_mouse_position_ = mapFromGlobal(global_position);
  panning_ = true;
  setCursor(Qt::ClosedHandCursor);
  return true;
}

bool CanvasWidget::pan_to_global_position(QPoint global_position) {
  if (!panning_) {
    return false;
  }
  clear_move_hover_outline();
  const auto position = mapFromGlobal(global_position);
  const auto delta = position - last_mouse_position_;
  const auto old_pan = pan_;
  pan_ += QPointF(delta);
  constrain_pan();
  last_mouse_position_ = position;
  if (pan_ != old_pan) {
    update();
    notify_view_changed();
  }
  return true;
}

bool CanvasWidget::end_pan() {
  if (!panning_) {
    return false;
  }
  panning_ = false;
  update_tool_cursor();
  return true;
}

void CanvasWidget::set_tool(CanvasTool tool) {
  const auto tool_changed = tool_ != tool;
  const auto old_transform_controls_rect = move_transform_controls_rect();
  if (tool_changed) {
    finish_free_transform();
    move_drag_pending_ = false;
    moving_layer_ = false;
    moving_layers_.clear();
    move_preview_delta_ = QPoint();
    move_preview_patches_.clear();
    move_preview_patches_delta_.reset();
    moving_layers_use_outline_preview_ = false;
    move_base_cache_ = QImage();
    set_move_transform_controls_layer(std::nullopt);
    clear_move_hover_outline();
  }
  tool_ = tool;
  update_tool_cursor();
  if (tool_changed) {
    update_move_transform_controls_dirty(old_transform_controls_rect);
    update();
    notify_transform_controls_changed();
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
    move_base_cache_ = QImage();
    dragging_text_rect_ = false;
    selecting_ = false;
    lassoing_ = false;
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
  layer_edit_target_ = target;
  clear_brush_stroke_tracking();
  update_tool_cursor();
}

CanvasWidget::LayerEditTarget CanvasWidget::layer_edit_target() const noexcept {
  return layer_edit_target_;
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
  primary_color_ = color;
}

QColor CanvasWidget::primary_color() const noexcept {
  return primary_color_;
}

void CanvasWidget::set_secondary_color(QColor color) {
  if (color.alpha() == 0) {
    color.setAlpha(255);
  }
  secondary_color_ = color;
}

QColor CanvasWidget::secondary_color() const noexcept {
  return secondary_color_;
}

void CanvasWidget::set_brush_size(int size) {
  brush_size_ = std::clamp(size, 1, 256);
  update_tool_cursor();
}

int CanvasWidget::brush_size() const noexcept {
  return brush_size_;
}

void CanvasWidget::set_brush_opacity(int opacity) {
  brush_opacity_ = std::clamp(opacity, 1, 100);
}

int CanvasWidget::brush_opacity() const noexcept {
  return brush_opacity_;
}

void CanvasWidget::set_brush_softness(int softness) {
  brush_softness_ = std::clamp(softness, 0, 100);
}

int CanvasWidget::brush_softness() const noexcept {
  return brush_softness_;
}

void CanvasWidget::set_brush_build_up(bool build_up) noexcept {
  brush_build_up_ = build_up;
}

bool CanvasWidget::brush_build_up() const noexcept {
  return brush_build_up_;
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

void CanvasWidget::set_pen_input_settings(PenInputSettings settings) noexcept {
  settings.pressure_size_min_percent = std::clamp(settings.pressure_size_min_percent, 1, 100);
  settings.pressure_opacity_min_percent = std::clamp(settings.pressure_opacity_min_percent, 1, 100);
  settings.tilt_min_roundness_percent = std::clamp(settings.tilt_min_roundness_percent, 1, 100);
  pen_input_settings_ = settings;
  if (!pen_input_settings_.enabled) {
    active_pen_input_sample_.reset();
  }
}

const CanvasWidget::PenInputSettings& CanvasWidget::pen_input_settings() const noexcept {
  return pen_input_settings_;
}

std::optional<CanvasWidget::PenInputSample> CanvasWidget::last_pen_input_sample() const {
  return last_pen_input_sample_;
}

void CanvasWidget::set_wand_tolerance(int tolerance) {
  wand_tolerance_ = std::clamp(tolerance, 0, 255);
}

int CanvasWidget::wand_tolerance() const noexcept {
  return wand_tolerance_;
}

void CanvasWidget::set_wand_contiguous(bool enabled) noexcept {
  wand_contiguous_ = enabled;
}

bool CanvasWidget::wand_contiguous() const noexcept {
  return wand_contiguous_;
}

void CanvasWidget::set_wand_sample_all_layers(bool enabled) noexcept {
  wand_sample_all_layers_ = enabled;
}

bool CanvasWidget::wand_sample_all_layers() const noexcept {
  return wand_sample_all_layers_;
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

void CanvasWidget::set_fill_shapes(bool fill_shapes) noexcept {
  fill_shapes_ = fill_shapes;
}

void CanvasWidget::set_selection_mode(SelectionMode mode) noexcept {
  selection_mode_ = mode;
}

CanvasWidget::SelectionMode CanvasWidget::selection_mode() const noexcept {
  return selection_mode_;
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

bool CanvasWidget::begin_free_transform() {
  Layer* layer = nullptr;
  if (document_ != nullptr && selected_layer_ids_.size() == 1U) {
    layer = document_->find_layer(selected_layer_ids_.front());
    if (layer != nullptr && !layer_has_movable_pixels(*layer)) {
      layer = nullptr;
    }
  }
  if (layer == nullptr) {
    layer = active_pixel_layer();
  }
  if (document_ == nullptr || layer == nullptr || !layer_has_movable_pixels(*layer)) {
    if (status_callback_) {
      status_callback_(tr("Select an editable pixel layer to transform"));
    }
    return false;
  }
  if (layer_effectively_locks_position(*layer)) {
    show_layer_position_locked_message();
    return false;
  }
  const auto local_transform_rect = opaque_pixel_local_rect(*layer);
  if (!local_transform_rect.has_value()) {
    if (status_callback_) {
      status_callback_(tr("Layer has no opaque pixels to transform"));
    }
    return false;
  }

  transforming_layer_ = true;
  dragging_transform_ = false;
  transform_layer_id_ = layer->id();
  set_move_transform_controls_layer(std::nullopt);
  const auto bounds = layer->bounds();
  transform_original_rect_ =
      QRectF(bounds.x + local_transform_rect->x(), bounds.y + local_transform_rect->y(), local_transform_rect->width(),
             local_transform_rect->height());
  transform_current_rect_ = transform_original_rect_;
  transform_drag_start_rect_ = transform_current_rect_;
  transform_drag_start_point_ = {};
  transform_drag_handle_ = TransformHandle::None;
  transform_angle_ = 0.0;
  transform_start_angle_ = 0.0;
  transform_scale_x_sign_ = 1.0;
  transform_scale_y_sign_ = 1.0;
  transform_drag_start_scale_x_sign_ = 1.0;
  transform_drag_start_scale_y_sign_ = 1.0;
  transform_source_image_ = QImage();
  transform_source_local_rect_ = *local_transform_rect;
  transform_base_cache_ = QImage();
  transform_composited_preview_cache_ = QImage();
  transform_requires_composited_preview_ = layer_needs_composited_transform_preview(*layer);
  setCursor(Qt::ArrowCursor);
  update();
  notify_transform_controls_changed();
  if (status_callback_) {
    status_callback_(tr("Drag handles to transform. Shift keeps aspect ratio."));
  }
  return true;
}

void CanvasWidget::cancel_free_transform() {
  if (!transforming_layer_) {
    return;
  }
  transforming_layer_ = false;
  dragging_transform_ = false;
  transform_layer_id_.reset();
  transform_drag_handle_ = TransformHandle::None;
  transform_scale_x_sign_ = 1.0;
  transform_scale_y_sign_ = 1.0;
  transform_drag_start_scale_x_sign_ = 1.0;
  transform_drag_start_scale_y_sign_ = 1.0;
  transform_base_cache_ = QImage();
  transform_source_image_ = QImage();
  transform_composited_preview_cache_ = QImage();
  transform_requires_composited_preview_ = false;
  transform_source_local_rect_ = QRect();
  update_tool_cursor();
  update();
  notify_transform_controls_changed();
}

void CanvasWidget::finish_free_transform() {
  if (!transforming_layer_) {
    return;
  }
  commit_free_transform();
}

std::optional<QRectF> CanvasWidget::transform_controls_rect_for_layer(const Layer& layer) const {
  const auto local_rect = move_layer_transform_local_rect(layer);
  if (!local_rect.has_value() || local_rect->isEmpty()) {
    return std::nullopt;
  }
  const auto bounds = layer.bounds();
  return QRectF(bounds.x + local_rect->x(), bounds.y + local_rect->y(), local_rect->width(), local_rect->height());
}

std::optional<QRectF> CanvasWidget::move_transform_controls_rect() const {
  if (document_ == nullptr || tool_ != CanvasTool::Move || !show_transform_controls_ || moving_layer_ ||
      transforming_layer_ || dragging_transform_) {
    return std::nullopt;
  }

  std::optional<LayerId> target_layer_id;
  if (selected_layer_ids_.empty()) {
    target_layer_id = document_->active_layer_id();
  } else if (selected_layer_ids_.size() == 1U) {
    target_layer_id = selected_layer_ids_.front();
  } else {
    return std::nullopt;
  }
  if (!target_layer_id.has_value()) {
    return std::nullopt;
  }
  const auto* layer = document_->find_layer(*target_layer_id);
  if (layer == nullptr || layer_effectively_locks_position(*layer)) {
    return std::nullopt;
  }
  return transform_controls_rect_for_layer(*layer);
}

void CanvasWidget::set_move_transform_controls_layer(std::optional<LayerId> layer_id) {
  const auto old_rect = move_transform_controls_rect();
  move_transform_controls_layer_id_ = layer_id;
  update_move_transform_controls_dirty(old_rect);
  notify_transform_controls_changed();
}

void CanvasWidget::notify_transform_controls_changed() {
  if (transform_controls_changed_callback_) {
    transform_controls_changed_callback_();
  }
}

QPointF CanvasWidget::transform_reference_position(QRectF document_rect, double angle_degrees) const {
  return document_rect.center() +
         rotate_offset(anchor_offset_from_center(document_rect.size(), transform_reference_point_), angle_degrees);
}

void CanvasWidget::update_move_transform_controls_dirty(std::optional<QRectF> old_rect) {
  const auto new_rect = move_transform_controls_rect();
  if (old_rect == new_rect) {
    return;
  }

  QRect dirty;
  if (old_rect.has_value()) {
    dirty = dirty.united(widget_rect_for_document_rect(*old_rect).toAlignedRect());
  }
  if (new_rect.has_value()) {
    dirty = dirty.united(widget_rect_for_document_rect(*new_rect).toAlignedRect());
  }
  if (!dirty.isEmpty()) {
    update(dirty.adjusted(-40, -40, 40, 40));
  } else {
    update();
  }
}

bool CanvasWidget::prepare_free_transform_source() {
  if (!transforming_layer_ || document_ == nullptr || !transform_layer_id_.has_value()) {
    return false;
  }
  if (!transform_source_image_.isNull()) {
    if (transform_requires_composited_preview_ && transform_composited_preview_cache_.isNull()) {
      refresh_transform_composited_preview_cache();
    }
    return true;
  }
  auto* layer = document_->find_layer(*transform_layer_id_);
  if (layer == nullptr || transform_source_local_rect_.isEmpty()) {
    return false;
  }

  transform_source_image_ = layer_source_image(*layer).copy(transform_source_local_rect_);
  const auto was_visible = layer->visible();
  layer->set_visible(false);
  transform_base_cache_ = render_document_image();
  layer->set_visible(was_visible);
  refresh_transform_composited_preview_cache();
  return !transform_source_image_.isNull();
}

void CanvasWidget::refresh_transform_composited_preview_cache() {
  transform_composited_preview_cache_ = QImage();
  if (!transform_requires_composited_preview_ || !transforming_layer_ || document_ == nullptr ||
      !transform_layer_id_.has_value() || transform_source_image_.isNull()) {
    return;
  }
  const auto* layer = document_->find_layer(*transform_layer_id_);
  if (layer == nullptr) {
    return;
  }

  const auto transformed_result =
      resample_transformed_rgba8(transform_source_image_,
                                  transform_source_to_document(transform_source_image_.size(), transform_current_rect_,
                                                               transform_angle_, transform_scale_x_sign_,
                                                               transform_scale_y_sign_),
                                  transform_interpolation_);
  if (transformed_result.image.isNull()) {
    return;
  }

  const auto transformed_pixels = pixels_from_image_rgba(transformed_result.image);
  transform_composited_preview_cache_ =
      qimage_from_document_rect_with_layer_pixels(*document_, QRect(0, 0, document_->width(), document_->height()), true,
                                                  *transform_layer_id_, transformed_pixels, transformed_result.bounds)
          .convertToFormat(QImage::Format_RGBA8888);
}

bool CanvasWidget::free_transform_active() const noexcept {
  return transforming_layer_;
}

void CanvasWidget::set_transform_interpolation(TransformInterpolation interpolation) noexcept {
  if (transform_interpolation_ == interpolation) {
    return;
  }
  transform_interpolation_ = interpolation;
  if (transforming_layer_) {
    refresh_transform_composited_preview_cache();
    update();
  }
  notify_transform_controls_changed();
}

CanvasWidget::TransformInterpolation CanvasWidget::transform_interpolation() const noexcept {
  return transform_interpolation_;
}

void CanvasWidget::set_transform_reference_point(CanvasAnchor anchor) noexcept {
  if (transform_reference_point_ == anchor) {
    return;
  }
  transform_reference_point_ = anchor;
  notify_transform_controls_changed();
}

CanvasAnchor CanvasWidget::transform_reference_point() const noexcept {
  return transform_reference_point_;
}

std::optional<CanvasWidget::TransformControlsState> CanvasWidget::transform_controls_state() const {
  std::optional<QRectF> rect;
  QRectF original_rect;
  double angle = 0.0;
  const bool active = transforming_layer_;
  if (active) {
    rect = transform_current_rect_;
    original_rect = transform_original_rect_;
    angle = transform_angle_;
  } else {
    rect = move_transform_controls_rect();
    if (rect.has_value()) {
      original_rect = *rect;
    }
  }

  if (!rect.has_value() || rect->isEmpty() || original_rect.width() <= 0.0 || original_rect.height() <= 0.0) {
    return std::nullopt;
  }

  return TransformControlsState{
      active,
      transform_reference_point_,
      transform_reference_position(*rect, angle),
      (active ? transform_scale_x_sign_ : 1.0) * (rect->width() / original_rect.width()) * 100.0,
      (active ? transform_scale_y_sign_ : 1.0) * (rect->height() / original_rect.height()) * 100.0,
      angle,
      transform_interpolation_,
  };
}

bool CanvasWidget::set_transform_controls_state(QPointF reference_position, double scale_x_percent,
                                                double scale_y_percent, double rotation_degrees) {
  if (!std::isfinite(reference_position.x()) || !std::isfinite(reference_position.y()) ||
      !std::isfinite(scale_x_percent) || !std::isfinite(scale_y_percent) ||
      !std::isfinite(rotation_degrees)) {
    return false;
  }
  if (!transforming_layer_ && !begin_free_transform()) {
    return false;
  }
  if (!prepare_free_transform_source()) {
    cancel_free_transform();
    return false;
  }

  const auto scale_x_sign = transform_scale_sign(scale_x_percent, transform_scale_x_sign_);
  const auto scale_y_sign = transform_scale_sign(scale_y_percent, transform_scale_y_sign_);
  const auto width = std::max(1.0, transform_original_rect_.width() *
                                       std::max(kMinimumTransformScalePercent, std::abs(scale_x_percent)) / 100.0);
  const auto height = std::max(1.0, transform_original_rect_.height() *
                                        std::max(kMinimumTransformScalePercent, std::abs(scale_y_percent)) / 100.0);
  const auto anchor_offset =
      rotate_offset(anchor_offset_from_center(QSizeF(width, height), transform_reference_point_), rotation_degrees);
  const auto center = reference_position - anchor_offset;
  transform_current_rect_ = QRectF(center.x() - width / 2.0, center.y() - height / 2.0, width, height);
  transform_scale_x_sign_ = scale_x_sign;
  transform_scale_y_sign_ = scale_y_sign;
  transform_angle_ = rotation_degrees;
  refresh_transform_composited_preview_cache();
  update();
  notify_transform_controls_changed();
  return true;
}

std::optional<QRect> CanvasWidget::active_layer_document_rect() const noexcept {
  const auto* layer = active_pixel_layer();
  if (layer == nullptr) {
    return std::nullopt;
  }
  return to_qrect(layer->bounds());
}

CanvasWidget::RenderCacheDiagnostics CanvasWidget::render_cache_diagnostics() const noexcept {
  return render_cache_diagnostics_;
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
  invalidate_display_mip_cache();
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
  render_cache_ = render_document_image_with_processing();
  render_cache_dirty_ = render_cache_.isNull();
  move_preview_patches_.clear();
  move_preview_patches_delta_.reset();
  ++render_cache_diagnostics_.full_refreshes;
  ++render_cache_diagnostics_.forced_refreshes;
  invalidate_display_mip_cache();
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

void CanvasWidget::document_changed_impl(QRegion document_region, bool includes_effect_bounds,
                                         DocumentChangeReason reason) {
  cancel_async_render_cache_refresh();
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

void CanvasWidget::set_transform_cursor_for_handle(TransformHandle handle) {
  switch (handle) {
    case TransformHandle::Move:
      setCursor(Qt::SizeAllCursor);
      break;
    case TransformHandle::Rotate:
      setCursor(Qt::CrossCursor);
      break;
    case TransformHandle::Top:
    case TransformHandle::Bottom:
      setCursor(Qt::SizeVerCursor);
      break;
    case TransformHandle::Left:
    case TransformHandle::Right:
      setCursor(Qt::SizeHorCursor);
      break;
    case TransformHandle::TopLeft:
    case TransformHandle::BottomRight:
      setCursor(Qt::SizeFDiagCursor);
      break;
    case TransformHandle::TopRight:
    case TransformHandle::BottomLeft:
      setCursor(Qt::SizeBDiagCursor);
      break;
    case TransformHandle::None:
      setCursor(Qt::ArrowCursor);
      break;
  }
}

bool CanvasWidget::constrain_pan() noexcept {
  if (document_ == nullptr || document_->width() <= 0 || document_->height() <= 0 || width() <= 0 || height() <= 0) {
    return false;
  }

  const auto constrained =
      QPointF(constrained_document_axis(pan_.x(), static_cast<double>(width()),
                                        static_cast<double>(document_->width()) * zoom_),
              constrained_document_axis(pan_.y(), static_cast<double>(height()),
                                        static_cast<double>(document_->height()) * zoom_));
  if (constrained == pan_) {
    return false;
  }

  pan_ = constrained;
  return true;
}

void CanvasWidget::notify_view_changed() {
  if (view_changed_callback_) {
    view_changed_callback_();
  }
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
  selection_ = last_cleared_selection_.intersected(QRect(0, 0, document_->width(), document_->height()));
  selection_display_region_ =
      last_cleared_selection_display_region_.intersected(QRect(0, 0, document_->width(), document_->height()));
  if (selection_display_region_.isEmpty() && !selection_.isEmpty()) {
    selection_display_region_ = selection_;
  }
  selection_mask_bounds_ = last_cleared_selection_mask_bounds_.intersected(QRect(0, 0, document_->width(), document_->height()));
  selection_mask_alpha_ = last_cleared_selection_mask_alpha_;
  if (status_callback_) {
    status_callback_(tr("Reselected previous selection"));
  }
  update();
}

void CanvasWidget::set_selection_edges_visible(bool visible) noexcept {
  if (selection_edges_visible_ == visible) {
    return;
  }
  selection_edges_visible_ = visible;
  update();
}

bool CanvasWidget::selection_edges_visible() const noexcept {
  return selection_edges_visible_;
}

void CanvasWidget::toggle_selection_edges_visible() {
  set_selection_edges_visible(!selection_edges_visible_);
  if (status_callback_) {
    status_callback_(selection_edges_visible_ ? tr("Showing selection edges") : tr("Hiding selection edges"));
  }
}

void CanvasWidget::set_rulers_visible(bool visible) noexcept {
  if (rulers_visible_ == visible) {
    return;
  }
  rulers_visible_ = visible;
  update();
}

bool CanvasWidget::rulers_visible() const noexcept {
  return rulers_visible_;
}

void CanvasWidget::set_grid_visible(bool visible) noexcept {
  if (grid_visible_ == visible) {
    return;
  }
  grid_visible_ = visible;
  update();
}

bool CanvasWidget::grid_visible() const noexcept {
  return grid_visible_;
}

void CanvasWidget::set_guides_visible(bool visible) noexcept {
  if (guides_visible_ == visible) {
    return;
  }
  guides_visible_ = visible;
  update();
}

bool CanvasWidget::guides_visible() const noexcept {
  return guides_visible_;
}

void CanvasWidget::set_guides_locked(bool locked) noexcept {
  guides_locked_ = locked;
  update_tool_cursor();
}

bool CanvasWidget::guides_locked() const noexcept {
  return guides_locked_;
}

void CanvasWidget::set_snap_enabled(bool enabled) noexcept {
  snap_enabled_ = enabled;
}

bool CanvasWidget::snap_enabled() const noexcept {
  return snap_enabled_;
}

void CanvasWidget::set_snap_to_guides(bool enabled) noexcept {
  snap_to_guides_ = enabled;
}

bool CanvasWidget::snap_to_guides() const noexcept {
  return snap_to_guides_;
}

void CanvasWidget::set_snap_to_grid(bool enabled) noexcept {
  snap_to_grid_ = enabled;
}

bool CanvasWidget::snap_to_grid() const noexcept {
  return snap_to_grid_;
}

void CanvasWidget::set_snap_to_document(bool enabled) noexcept {
  snap_to_document_ = enabled;
}

bool CanvasWidget::snap_to_document() const noexcept {
  return snap_to_document_;
}

void CanvasWidget::set_snap_to_layers(bool enabled) noexcept {
  snap_to_layers_ = enabled;
}

bool CanvasWidget::snap_to_layers() const noexcept {
  return snap_to_layers_;
}

void CanvasWidget::set_snap_to_selection(bool enabled) noexcept {
  snap_to_selection_ = enabled;
}

bool CanvasWidget::snap_to_selection() const noexcept {
  return snap_to_selection_;
}

void CanvasWidget::set_grid_subdivisions(int subdivisions) noexcept {
  grid_subdivisions_ = std::clamp(subdivisions, 1, 64);
  update();
}

int CanvasWidget::grid_subdivisions() const noexcept {
  return grid_subdivisions_;
}

void CanvasWidget::set_grid_style(int style) noexcept {
  grid_style_ = std::clamp(style, 0, 1);
  update();
}

int CanvasWidget::grid_style() const noexcept {
  return grid_style_;
}

void CanvasWidget::set_grid_color(QColor color) noexcept {
  if (!color.isValid()) {
    return;
  }
  grid_color_ = color;
  update();
}

QColor CanvasWidget::grid_color() const noexcept {
  return grid_color_;
}

void CanvasWidget::set_guide_color(QColor color) noexcept {
  if (!color.isValid()) {
    return;
  }
  guide_color_ = color;
  update();
}

QColor CanvasWidget::guide_color() const noexcept {
  return guide_color_;
}

void CanvasWidget::add_guide(GuideOrientation orientation, std::int32_t position_32) {
  if (document_ == nullptr) {
    return;
  }
  const auto limit_32 = (orientation == GuideOrientation::Vertical ? document_->width() : document_->height()) * 32;
  DocumentGuide guide{orientation, std::clamp(position_32, 0, limit_32)};
  if (before_edit_callback_) {
    before_edit_callback_(tr("New Guide"));
  }
  document_->guides().push_back(guide);
  selected_guide_index_ = static_cast<int>(document_->guides().size()) - 1;
  document_changed();
}

void CanvasWidget::clear_guides() {
  if (document_ == nullptr || document_->guides().empty() || guides_locked_) {
    return;
  }
  if (before_edit_callback_) {
    before_edit_callback_(tr("Clear Guides"));
  }
  document_->guides().clear();
  selected_guide_index_ = -1;
  document_changed();
}

void CanvasWidget::clear_selected_guides() {
  if (document_ == nullptr || guides_locked_ || selected_guide_index_ < 0 ||
      selected_guide_index_ >= static_cast<int>(document_->guides().size())) {
    return;
  }
  if (before_edit_callback_) {
    before_edit_callback_(tr("Clear Selected Guide"));
  }
  document_->guides().erase(document_->guides().begin() + selected_guide_index_);
  selected_guide_index_ = -1;
  document_changed();
}

bool CanvasWidget::has_selected_guides() const noexcept {
  return document_ != nullptr && selected_guide_index_ >= 0 &&
         selected_guide_index_ < static_cast<int>(document_->guides().size());
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
    if (status_callback_) {
      status_callback_(tr("Select a pixel layer first"));
    }
    return;
  }

  const auto& pixels = layer->pixels();
  const auto bounds = layer->bounds();
  std::vector<QRect> runs;
  runs.reserve(static_cast<std::size_t>(std::max(1, pixels.height())));
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    int run_start = -1;
    for (std::int32_t x = 0; x <= pixels.width(); ++x) {
      bool selected = false;
      if (x < pixels.width()) {
        const auto* px = pixels.pixel(x, y);
        selected = pixels.format().channels >= 4 ? px[3] != 0 : true;
      }
      if (selected && run_start < 0) {
        run_start = x;
      } else if (!selected && run_start >= 0) {
        runs.emplace_back(bounds.x + run_start, bounds.y + y, x - run_start, 1);
        run_start = -1;
      }
    }
  }

  QRegion layer_region;
  if (!runs.empty()) {
    layer_region.setRects(runs.data(), static_cast<int>(runs.size()));
  }
  const QRect canvas_rect(0, 0, document_->width(), document_->height());
  set_selection_from_region(layer_region.intersected(canvas_rect));
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
    if (status_callback_) {
      status_callback_(tr("Layer has no mask"));
    }
    return;
  }

  const auto& mask = *layer->mask();
  const QRect canvas_rect(0, 0, document_->width(), document_->height());
  QRegion mask_region;
  if (!mask.pixels.empty() && mask.pixels.format() == PixelFormat::gray8()) {
    std::vector<QRect> runs;
    runs.reserve(static_cast<std::size_t>(std::max(1, mask.pixels.height())));
    for (std::int32_t y = 0; y < mask.pixels.height(); ++y) {
      int run_start = -1;
      for (std::int32_t x = 0; x <= mask.pixels.width(); ++x) {
        const bool selected = x < mask.pixels.width() && *mask.pixels.pixel(x, y) != 0U;
        if (selected && run_start < 0) {
          run_start = x;
        } else if (!selected && run_start >= 0) {
          runs.emplace_back(mask.bounds.x + run_start, mask.bounds.y + y, x - run_start, 1);
          run_start = -1;
        }
      }
    }
    if (!runs.empty()) {
      mask_region.setRects(runs.data(), static_cast<int>(runs.size()));
    }
  }

  if (mask.default_color != 0U) {
    const auto mask_bounds = to_qrect(mask.bounds).intersected(canvas_rect);
    set_selection_from_region(QRegion(canvas_rect).subtracted(mask_bounds).united(mask_region));
  } else {
    set_selection_from_region(mask_region);
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
  auto* mask = active_layer_mask();
  if (document_ == nullptr || mask == nullptr) {
    return {};
  }
  auto affected = has_selection() && selected_document_rect().has_value()
                      ? *selected_document_rect()
                      : QRect(0, 0, document_->width(), document_->height());
  affected = affected.intersected(QRect(0, 0, document_->width(), document_->height()));
  if (affected.isEmpty() ||
      !expand_mask_to_include_rect(*mask, affected, QSize(document_->width(), document_->height()))) {
    return {};
  }

  const auto bounds = QRect(mask->bounds.x, mask->bounds.y, mask->bounds.width, mask->bounds.height);
  const auto value = mask_value_from_color(color);
  QRect dirty;
  if (has_selection() && !selection_has_partial_alpha()) {
    const auto selected = selection_.intersected(QRegion(affected));
    for (const auto& rect : selected) {
      const auto clipped = rect.intersected(bounds);
      if (clipped.isEmpty()) {
        continue;
      }
      for (int y = clipped.top(); y <= clipped.bottom(); ++y) {
        for (int x = clipped.left(); x <= clipped.right(); ++x) {
          auto* px = mask->pixels.pixel(x - bounds.x(), y - bounds.y());
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
      if (has_selection()) {
        coverage = static_cast<float>(selection_alpha_at(document_point)) / 255.0F;
      }
      if (coverage <= 0.0F) {
        continue;
      }
      auto* px = mask->pixels.pixel(x - bounds.x(), y - bounds.y());
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

void CanvasWidget::grow_selection() {
  if (document_ == nullptr || selection_.isEmpty()) {
    if (status_callback_) {
      status_callback_(tr("Make a selection before growing"));
    }
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
    if (status_callback_) {
      status_callback_(tr("Make a selection before selecting similar pixels"));
    }
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

QPoint CanvasWidget::widget_position_for_document_point(QPoint document_position) const {
  return widget_position(document_position);
}

void CanvasWidget::set_before_edit_callback(std::function<void(QString)> callback) {
  before_edit_callback_ = std::move(callback);
}

void CanvasWidget::set_color_picked_callback(std::function<void(QColor)> callback) {
  color_picked_callback_ = std::move(callback);
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
  if (move_transform_controls_layer_id_.has_value() &&
      !(layer_ids.size() == 1U && layer_ids.front() == *move_transform_controls_layer_id_)) {
    set_move_transform_controls_layer(std::nullopt);
  }
  selected_layer_ids_ = std::move(layer_ids);
  clear_guide_selection();
  update_move_transform_controls_dirty(old_transform_controls_rect);
  notify_transform_controls_changed();
}

void CanvasWidget::paintEvent(QPaintEvent* event) {
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
    ensure_render_cache();
  }

  const bool deep_pixel_renderer = uses_deep_zoom_pixel_renderer(zoom_);
  const auto draw_scaled_image = [&painter, &target_rect, pixel_aligned_view,
                                  &pixel_aligned_target_rect, this, exposed_rect](const QImage& image) {
    if (!image.isNull()) {
      const QImage& display_image = (&image == &render_cache_ && zoom_ < 1.0) ? display_image_for_zoom() : image;
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

    if (pixel_aligned_view) {
      painter.drawImage(patch_target.toAlignedRect(), patch.image, patch.image.rect());
    } else {
      painter.drawImage(patch_target, patch.image, QRectF(patch.image.rect()));
    }
  };
  const bool draw_transform_overlay =
      transforming_layer_ && !transform_source_image_.isNull() &&
      (!transform_base_cache_.isNull() || !transform_composited_preview_cache_.isNull());

  painter.save();
  painter.setClipRect(target_rect);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, uses_smooth_display_scaling(zoom_, deep_pixel_renderer));
  if (draw_transform_overlay) {
    if (!transform_composited_preview_cache_.isNull()) {
      draw_scaled_image(transform_composited_preview_cache_);
    } else {
      draw_scaled_image(transform_base_cache_);
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
    draw_scaled_image(render_cache_);
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
  draw_shape_preview(painter);
  draw_text_rect_preview(painter);
  if (tool_ == CanvasTool::Clone && clone_source_set_) {
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
  draw_processing_overlay(painter);
}

void CanvasWidget::wheelEvent(QWheelEvent* event) {
  const auto wheel_delta = !event->pixelDelta().isNull() ? event->pixelDelta() : event->angleDelta();
  const auto primary_delta = wheel_delta.y() != 0 ? wheel_delta.y() : wheel_delta.x();
  if (primary_delta == 0) {
    event->accept();
    return;
  }

  // Alt+wheel always zooms, in either mode.
  if ((event->modifiers() & Qt::AltModifier) != 0) {
    zoom_at_widget_point(event->position(), primary_delta > 0 ? 1.1 : 0.9);
    event->accept();
    return;
  }

  // In wheel-zoom mode a plain wheel zooms (centered on the cursor) and the
  // modifiers pan; otherwise a plain wheel pans (Photoshop-style navigation).
  // A pen button configured as "scroll" arrives here as a wheel event, so this
  // mode also lets that button zoom.
  if (wheel_zooms_) {
    const auto modifiers = event->modifiers();
    if ((modifiers & Qt::ControlModifier) == 0 && (modifiers & Qt::ShiftModifier) == 0) {
      zoom_at_widget_point(event->position(), primary_delta > 0 ? 1.1 : 0.9);
      event->accept();
      return;
    }
  }

  constexpr double kWheelPanScale = 0.5;
  const auto old_pan = pan_;
  const bool pan_vertically =
      wheel_zooms_ ? (event->modifiers() & Qt::ShiftModifier) == 0
                   : (event->modifiers() & Qt::ControlModifier) != 0;
  if (pan_vertically) {
    pan_.ry() += static_cast<double>(primary_delta) * kWheelPanScale;
  } else {
    pan_.rx() += static_cast<double>(primary_delta) * kWheelPanScale;
  }
  constrain_pan();
  event->accept();
  if (pan_ != old_pan) {
    update();
    notify_view_changed();
  }
}

void CanvasWidget::set_wheel_zooms(bool enabled) noexcept {
  wheel_zooms_ = enabled;
}

bool CanvasWidget::wheel_zooms() const noexcept {
  return wheel_zooms_;
}

void CanvasWidget::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  if (isVisible() && constrain_pan()) {
    update();
    notify_view_changed();
  }
}

void CanvasWidget::mousePressEvent(QMouseEvent* event) {
  if (!handling_tablet_event_) {
    active_pen_input_sample_.reset();
  }
  setFocus(Qt::MouseFocusReason);
  last_mouse_position_ = event->pos();
  emit_info_for_widget_position(event->pos());

  if (spacebar_panning_ || tool_ == CanvasTool::Pan || (event->buttons() & Qt::MiddleButton) != 0 ||
      (event->buttons() & Qt::RightButton) != 0) {
    panning_ = true;
    setCursor(Qt::ClosedHandCursor);
    return;
  }

  if (edit_locked_ && tool_ != CanvasTool::Zoom) {
    show_edit_locked_message();
    event->accept();
    return;
  }

  if (event->button() == Qt::LeftButton && widget_position_in_ruler(event->pos())) {
    begin_new_guide_drag(event->pos());
    event->accept();
    return;
  }

  if (transforming_layer_ && event->button() == Qt::LeftButton) {
    const auto handle = transform_handle_at(event->pos());
    if (handle != TransformHandle::None) {
      if (!prepare_free_transform_source()) {
        cancel_free_transform();
        event->accept();
        return;
      }
      dragging_transform_ = true;
      transform_drag_handle_ = handle;
      transform_drag_start_point_ = document_position_f(event->position());
      transform_drag_start_rect_ = transform_current_rect_;
      transform_start_angle_ = transform_angle_;
      transform_drag_start_scale_x_sign_ = transform_scale_x_sign_;
      transform_drag_start_scale_y_sign_ = transform_scale_y_sign_;
      event->accept();
      return;
    }
    cancel_free_transform();
    event->accept();
    return;
  }

  const auto document_point = document_position(event->pos());
  const auto document_point_f = document_position_f(event->position());
  const auto effective_tool = effective_tool_for_input();
  if (event->button() == Qt::LeftButton) {
    const auto guide_index = guide_at_widget_position(event->pos());
    const auto guide_drag_allowed = tool_ == CanvasTool::Move || event->modifiers().testFlag(Qt::ControlModifier);
    if (guide_index >= 0 && guide_drag_allowed) {
      begin_guide_drag(guide_index, event->pos());
      event->accept();
      return;
    }
    clear_guide_selection();
  }
  if (!document_contains(document_point)) {
    // The Move tool's transform resize and rotate handles can sit outside the
    // document bounds (for example after scaling a layer larger than the
    // canvas). Let a press that lands on one of those handles fall through to
    // the transform-handle hit-test below instead of discarding it here;
    // otherwise the handles can be hovered but never grabbed once they extend
    // past the canvas edge. The interior Move hit still falls back to the
    // normal out-of-bounds behaviour.
    const auto off_canvas_transform_rect = move_transform_controls_rect();
    const auto off_canvas_handle =
        off_canvas_transform_rect.has_value()
            ? transform_handle_at(event->pos(), *off_canvas_transform_rect, 0.0)
            : TransformHandle::None;
    if (off_canvas_handle == TransformHandle::None || off_canvas_handle == TransformHandle::Move) {
      set_move_transform_controls_layer(std::nullopt);
      return;
    }
  }

  if (event->button() == Qt::LeftButton && (event->modifiers() & Qt::AltModifier) != 0 &&
      tool_uses_alt_left_for_color_pick(tool_)) {
    pick_color(document_point);
    return;
  }

  if (effective_tool == CanvasTool::Clone) {
    if (editing_layer_mask()) {
      if (status_callback_) {
        status_callback_(tr("Clone is unavailable while editing a layer mask"));
      }
      return;
    }
    if ((event->modifiers() & Qt::AltModifier) != 0) {
      set_clone_source(document_point);
      return;
    }
    if (!clone_source_set_) {
      if (status_callback_) {
        status_callback_(tr("Alt-click to set a clone source"));
      }
      return;
    }
    if (begin_edit(tr("Clone stamp"))) {
      clone_source_cache_ = render_document_image_with_processing();
      if (!clone_aligned_ || !clone_aligned_offset_set_) {
        clone_source_offset_ = clone_source_point_ - document_point;
        clone_aligned_offset_set_ = clone_aligned_;
      }
      clear_brush_stroke_tracking();
      begin_axis_constrained_stroke(QPointF(document_point));
      painting_ = true;
      last_document_position_ = document_point;
      const auto dirty = clone_brush_at(document_point);
      if (!dirty.isEmpty()) {
        document_changed_impl(QRegion(dirty), false, DocumentChangeReason::BrushStrokePreview);
      }
    }
    return;
  }

  if (tool_ == CanvasTool::Eyedropper) {
    pick_color(document_point);
    return;
  }

  if (tool_ == CanvasTool::Text) {
    if (auto* layer = topmost_text_layer_at(document_point); layer != nullptr) {
      if (layer_effectively_locks_image_pixels(*layer)) {
        show_layer_pixels_locked_message();
        return;
      }
      activate_layer(*layer);
      if (text_requested_callback_) {
        text_requested_callback_(document_point, QRect());
      }
      event->accept();
      update();
      return;
    }
    dragging_text_rect_ = true;
    text_rect_start_ = snapped_document_point(document_point);
    text_rect_current_ = text_rect_start_;
    update();
    return;
  }

  if (tool_ == CanvasTool::Move) {
    const auto passive_transform_rect = move_transform_controls_rect();
    auto passive_handle = TransformHandle::None;
    if (passive_transform_rect.has_value()) {
      passive_handle = transform_handle_at(event->pos(), *passive_transform_rect, 0.0);
      if (passive_handle != TransformHandle::None && passive_handle != TransformHandle::Move) {
        if (begin_free_transform() && prepare_free_transform_source()) {
          dragging_transform_ = true;
          transform_drag_handle_ = passive_handle;
          transform_drag_start_point_ = document_position_f(event->position());
          transform_drag_start_rect_ = transform_current_rect_;
          transform_start_angle_ = transform_angle_;
          transform_drag_start_scale_x_sign_ = transform_scale_x_sign_;
          transform_drag_start_scale_y_sign_ = transform_scale_y_sign_;
        } else {
          cancel_free_transform();
        }
        event->accept();
        return;
      }
    }
    auto* top_clicked_layer = topmost_move_layer_at(document_point, false);
    auto* clicked_layer = topmost_move_layer_at(document_point, true);
    Layer* hit_layer = nullptr;
    Layer* transform_controls_layer = nullptr;
    std::vector<LayerId> layer_ids;
    const auto selected_move_layer_ids = movable_layer_ids();
    if (auto_select_layer_) {
      hit_layer = clicked_layer;
      const auto hit_selected_layer =
          hit_layer != nullptr && !selected_layer_ids_.empty() &&
          std::find(selected_move_layer_ids.begin(), selected_move_layer_ids.end(), hit_layer->id()) !=
              selected_move_layer_ids.end();
      if (hit_selected_layer) {
        layer_ids = selected_move_layer_ids;
        if (selected_layer_ids_.size() < 2U && selected_move_layer_ids.size() == 1U) {
          transform_controls_layer = hit_layer;
        }
      } else if (hit_layer != nullptr) {
        activate_layer(*hit_layer);
        layer_ids.push_back(hit_layer->id());
        transform_controls_layer = hit_layer;
      }
      if (hit_layer == nullptr && selected_layer_ids_.size() < 2U && passive_handle == TransformHandle::Move) {
        layer_ids = selected_move_layer_ids;
      }
    } else if (selected_layer_ids_.size() < 2U) {
      auto target_id = document_->active_layer_id();
      if (!selected_layer_ids_.empty()) {
        target_id = selected_layer_ids_.front();
      }
      if (target_id.has_value()) {
        auto* layer = document_->find_layer(*target_id);
        if (layer != nullptr && move_layer_contains_document_point(*layer, document_point)) {
          transform_controls_layer = layer;
        }
      }
    }
    if (show_transform_controls_ && (auto_select_layer_ || selected_layer_ids_.size() < 2U)) {
      if (transform_controls_layer != nullptr) {
        set_move_transform_controls_layer(transform_controls_layer->id());
      } else if (transform_controls_layer == nullptr && passive_transform_rect.has_value() &&
                 passive_handle == TransformHandle::None) {
        set_move_transform_controls_layer(std::nullopt);
        event->accept();
        return;
      } else if (passive_handle == TransformHandle::Move) {
        // Keep existing controls while the normal Move path handles the drag.
      } else {
        set_move_transform_controls_layer(std::nullopt);
      }
    }
    if (transform_controls_layer == nullptr && passive_transform_rect.has_value() &&
        passive_handle == TransformHandle::None) {
      set_move_transform_controls_layer(std::nullopt);
      event->accept();
      return;
    }
    if (!auto_select_layer_) {
      layer_ids = selected_move_layer_ids;
    }
    if (layer_ids.empty()) {
      if (status_callback_) {
        if (top_clicked_layer != nullptr && layer_effectively_locks_position(*top_clicked_layer)) {
          status_callback_(tr("Layer position is locked."));
        } else {
          status_callback_(tr("Click an editable layer to move"));
        }
      }
      return;
    }
    move_drag_pending_ = true;
    moving_layer_ = false;
    move_start_ = document_point;
    begin_axis_constrained_stroke(QPointF(move_start_));
    move_press_widget_position_ = event->pos();
    move_preview_delta_ = QPoint();
    moving_layers_.clear();
    moving_layers_use_outline_preview_ = false;
    move_base_cache_ = QImage();
    moving_layers_.reserve(layer_ids.size());
    for (const auto id : layer_ids) {
      auto* layer = document_->find_layer(id);
      if (layer != nullptr) {
        moving_layers_.push_back(
            MovingLayer{id, layer->bounds(), move_layer_outline_bounds(*layer), move_layer_has_expensive_style(*layer)});
      }
    }
    move_preview_patches_.clear();
    move_preview_patches_delta_.reset();
    return;
  }

  if (tool_ == CanvasTool::Marquee || tool_ == CanvasTool::EllipticalMarquee) {
    const auto snapped_point = snapped_document_point(document_point);
    selecting_ = true;
    spacebar_repositioning_drag_rect_ = false;
    selection_edges_visible_ = true;
    selection_start_ = snapped_point;
    selection_current_ = snapped_point;
    selection_before_edit_ = selection_;
    selection_display_region_before_edit_ = selection_display_region_;
    selection_mask_before_edit_bounds_ = selection_mask_bounds_;
    selection_mask_before_edit_alpha_ = selection_mask_alpha_;
    selection_operation_ = selection_operation(event->modifiers());
    combine_selection_from_region(marquee_selection_region(selection_start_, selection_current_));
    emit_info_for_widget_position(event->pos());
    update();
    return;
  }

  if (tool_ == CanvasTool::Lasso) {
    lassoing_ = true;
    selection_edges_visible_ = true;
    lasso_points_.clear();
    lasso_points_ << document_point;
    selection_before_edit_ = selection_;
    selection_display_region_before_edit_ = selection_display_region_;
    selection_mask_before_edit_bounds_ = selection_mask_bounds_;
    selection_mask_before_edit_alpha_ = selection_mask_alpha_;
    selection_operation_ = selection_operation(event->modifiers());
    restore_selection_before_edit();
    update();
    return;
  }

  if (tool_ == CanvasTool::MagicWand) {
    selection_edges_visible_ = true;
    selection_before_edit_ = selection_;
    selection_display_region_before_edit_ = selection_display_region_;
    selection_mask_before_edit_bounds_ = selection_mask_bounds_;
    selection_mask_before_edit_alpha_ = selection_mask_alpha_;
    selection_operation_ = selection_operation(event->modifiers());
    begin_processing_operation();
    magic_wand_select(document_point);
    end_processing_operation();
    selection_before_edit_ = QRegion();
    selection_display_region_before_edit_ = QRegion();
    selection_mask_before_edit_bounds_ = {};
    selection_mask_before_edit_alpha_ = QImage();
    return;
  }

  if (tool_ == CanvasTool::Zoom) {
    zooming_ = true;
    zoom_start_ = document_point;
    zoom_current_ = document_point;
    emit_info_for_widget_position(event->pos());
    update();
    return;
  }

  if (tool_ == CanvasTool::Fill) {
    if (begin_edit(tr("Fill"))) {
      begin_processing_operation();
      const auto dirty = flood_fill(document_point);
      tick_processing_operation();
      document_changed(dirty);
      end_processing_operation();
    }
    return;
  }

  if (effective_tool == CanvasTool::Brush || effective_tool == CanvasTool::Smudge ||
      effective_tool == CanvasTool::Eraser) {
    if (effective_tool == CanvasTool::Smudge && editing_layer_mask()) {
      if (status_callback_) {
        status_callback_(tr("Smudge is unavailable while editing a layer mask"));
      }
      return;
    }
    auto label = tr("Erase");
    if (effective_tool == CanvasTool::Brush) {
      label = tr("Brush stroke");
    } else if (effective_tool == CanvasTool::Smudge) {
      label = tr("Smudge");
    }
    if (begin_edit(label)) {
      clear_brush_stroke_tracking();
      smudge_state_ = {};
      const auto stroke_brush_size = effective_brush_input().size;
      begin_axis_constrained_stroke(stroke_brush_size == 1 ? QPointF(document_point) : document_point_f);
      painting_ = true;
      last_document_position_ = document_point;
      last_document_position_f_ = document_point_f;
      if (effective_tool != CanvasTool::Smudge) {
        if (stroke_brush_size == 1) {
          reset_brush_smoothing();
        } else {
          begin_brush_smoothing(document_point_f);
        }
        const auto dirty = draw_brush_at(document_point, effective_tool == CanvasTool::Eraser);
        if (!dirty.isEmpty()) {
          document_changed_impl(QRegion(dirty), false, DocumentChangeReason::BrushStrokePreview);
        }
      } else {
        reset_brush_smoothing();
      }
    }
    return;
  }

  if (tool_ == CanvasTool::Gradient || tool_ == CanvasTool::Line || tool_ == CanvasTool::Rectangle ||
      tool_ == CanvasTool::Ellipse) {
    if (begin_edit(tool_ == CanvasTool::Gradient ? tr("Gradient") : tr("Shape"))) {
      const auto snapped_point = snapped_document_point(document_point);
      clear_brush_stroke_tracking();
      drawing_shape_ = true;
      spacebar_repositioning_drag_rect_ = false;
      shape_start_ = snapped_point;
      shape_current_ = snapped_point;
      update();
    }
  }
}

void CanvasWidget::mouseMoveEvent(QMouseEvent* event) {
  if (!handling_tablet_event_) {
    active_pen_input_sample_.reset();
  }
  emit_info_for_widget_position(event->pos());
  if (panning_) {
    clear_move_hover_outline();
    const auto delta = event->pos() - last_mouse_position_;
    const auto old_pan = pan_;
    pan_ += QPointF(delta);
    constrain_pan();
    last_mouse_position_ = event->pos();
    if (pan_ != old_pan) {
      update();
      notify_view_changed();
    }
    return;
  }

  if (edit_locked_ && !zooming_) {
    clear_move_hover_outline();
    last_mouse_position_ = event->pos();
    event->accept();
    return;
  }

  if (dragging_guide_) {
    clear_move_hover_outline();
    update_guide_drag(event->pos(), event->modifiers());
    last_mouse_position_ = event->pos();
    return;
  }

  if (dragging_transform_) {
    clear_move_hover_outline();
    update_free_transform_preview(document_position_f(event->position()), event->modifiers());
    last_mouse_position_ = event->pos();
    return;
  }

  if (transforming_layer_) {
    clear_move_hover_outline();
    set_transform_cursor_for_handle(transform_handle_at(event->pos()));
    last_mouse_position_ = event->pos();
    return;
  }

  const auto document_point = document_position(event->pos());
  const auto document_point_f = document_position_f(event->position());
  const auto effective_tool = effective_tool_for_input();
  if (dragging_text_rect_) {
    clear_move_hover_outline();
    text_rect_current_ = snapped_document_point(document_point);
    emit_info_for_widget_position(event->pos());
    update();
  } else if (painting_) {
    clear_move_hover_outline();
    QRect dirty;
    if (effective_tool == CanvasTool::Clone) {
      const auto constrained_point = axis_constrained_stroke_point(document_point, event->modifiers());
      dirty = clone_brush_segment(last_document_position_, constrained_point);
      last_document_position_ = constrained_point;
      last_document_position_f_ = QPointF(constrained_point);
    } else if (effective_tool == CanvasTool::Smudge) {
      dirty = smudge_brush_segment(last_document_position_, document_point);
      last_document_position_ = document_point;
      last_document_position_f_ = document_point_f;
    } else if (effective_brush_input().size == 1) {
      const auto constrained_point = axis_constrained_stroke_point(document_point, event->modifiers());
      dirty = draw_brush_segment(last_document_position_, constrained_point, effective_tool == CanvasTool::Eraser);
      last_document_position_ = constrained_point;
      last_document_position_f_ = QPointF(constrained_point);
    } else {
      const auto constrained_point = axis_constrained_stroke_point(document_point_f, event->modifiers());
      dirty = advance_smoothed_brush_stroke(constrained_point, effective_tool == CanvasTool::Eraser);
      last_document_position_ = QPoint(static_cast<int>(std::lround(constrained_point.x())),
                                       static_cast<int>(std::lround(constrained_point.y())));
      last_document_position_f_ = constrained_point;
    }
    if (!dirty.isEmpty()) {
      document_changed_impl(QRegion(dirty), false, DocumentChangeReason::BrushStrokePreview);
    }
  } else if (drawing_shape_) {
    clear_move_hover_outline();
    if (spacebar_repositioning_drag_rect_) {
      const auto delta = document_point - spacebar_reposition_last_document_position_;
      shape_start_ += delta;
      shape_current_ += delta;
      spacebar_reposition_last_document_position_ = document_point;
    } else {
      shape_current_ = snapped_document_point(document_point);
    }
    update();
  } else if (move_drag_pending_ || moving_layer_) {
    std::optional<QRectF> old_transform_controls_rect;
    if (move_drag_pending_) {
      const auto widget_delta = event->pos() - move_press_widget_position_;
      if (widget_delta.manhattanLength() < QApplication::startDragDistance()) {
        last_mouse_position_ = event->pos();
        return;
      }
      old_transform_controls_rect = move_transform_controls_rect();
      move_drag_pending_ = false;
      moving_layer_ = true;
      update_move_transform_controls_dirty(old_transform_controls_rect);
    }
    clear_move_hover_outline();
    const auto old_delta = move_preview_delta_;
    const auto constrained_delta = axis_constrained_move_delta(document_point - move_start_, event->modifiers());
    move_preview_delta_ = axis_constrained_move_delta(snapped_move_delta(constrained_delta), event->modifiers());
    if (move_preview_delta_ == old_delta || document_ == nullptr || moving_layers_.empty()) {
      last_mouse_position_ = event->pos();
      return;
    }
    if (!moving_layers_use_outline_preview_ &&
        moving_layers_should_use_outline_preview(old_delta, move_preview_delta_)) {
      moving_layers_use_outline_preview_ = true;
      move_preview_patches_.clear();
      move_preview_patches_delta_.reset();
      move_base_cache_ = QImage();
    }
    if (moving_layers_use_outline_preview_) {
      move_preview_patches_.clear();
      move_preview_patches_delta_.reset();
      const auto dirty = moving_layers_outline_dirty_rect(old_delta, move_preview_delta_);
      if (!dirty.isEmpty()) {
        update(widget_rect_for_document_rect(dirty));
      }
      last_mouse_position_ = event->pos();
      return;
    }
    ensure_render_cache();
    ensure_move_base_cache();
    const QRegion canvas_region(QRect(0, 0, document_->width(), document_->height()));
    auto previous_preview_region = QRegion();
    for (const auto& patch : move_preview_patches_) {
      if (!patch.document_rect.isEmpty()) {
        previous_preview_region += patch.document_rect;
      }
    }
    auto preview_region = moving_layers_dirty_region(QPoint(), move_preview_delta_).intersected(canvas_region);
    auto update_region = previous_preview_region.united(preview_region).intersected(canvas_region);
    const auto outline_dirty = moving_layers_outline_dirty_rect(old_delta, move_preview_delta_);
    if (!outline_dirty.isEmpty()) {
      update_region += outline_dirty;
      update_region = update_region.intersected(canvas_region);
    }
    if (!preview_region.isEmpty()) {
      move_preview_patches_ = qimage_patches_from_document_region_with_layer_bounds(
          *document_, preview_region, true, moving_layer_bounds(move_preview_delta_));
      for (auto& patch : move_preview_patches_) {
        patch.image = patch.image.convertToFormat(QImage::Format_RGBA8888);
      }
      move_preview_patches_delta_ = move_preview_delta_;
    } else {
      move_preview_patches_.clear();
      move_preview_patches_delta_.reset();
    }
    if (!update_region.isEmpty()) {
      QRegion widget_region;
      for (const auto& rect : update_region) {
        widget_region += widget_rect_for_document_rect(rect);
      }
      update(widget_region);
    }
  } else if (selecting_) {
    clear_move_hover_outline();
    if (spacebar_repositioning_drag_rect_) {
      const auto raw_delta = document_point - spacebar_reposition_origin_document_position_;
      const auto delta = snapped_rect_delta(
          marquee_selection_rect(spacebar_reposition_start_selection_start_,
                                 spacebar_reposition_start_selection_current_),
          raw_delta);
      selection_start_ = spacebar_reposition_start_selection_start_ + delta;
      selection_current_ = spacebar_reposition_start_selection_current_ + delta;
    } else {
      selection_current_ = snapped_marquee_current_point(selection_start_, document_point);
    }
    combine_selection_from_region(marquee_selection_region(selection_start_, selection_current_));
    emit_info_for_widget_position(event->pos());
    update();
  } else if (lassoing_ && document_ != nullptr) {
    clear_move_hover_outline();
    const auto point = clamped_document_point(*document_, document_point);
    if (lasso_points_.isEmpty() || (point - lasso_points_.last()).manhattanLength() >= 1) {
      lasso_points_ << point;
      update();
    }
  } else if (zooming_ && document_ != nullptr) {
    clear_move_hover_outline();
    zoom_current_ = clamped_document_point(*document_, document_point);
    emit_info_for_widget_position(event->pos());
    update();
  } else {
    const auto guide_index = guide_at_widget_position(event->pos());
    const auto guide_drag_allowed = tool_ == CanvasTool::Move || event->modifiers().testFlag(Qt::ControlModifier);
    if (guide_index >= 0 && !guides_locked_ && guide_drag_allowed) {
      clear_move_hover_outline();
      const auto orientation = document_->guides()[static_cast<std::size_t>(guide_index)].orientation;
      setCursor(orientation == GuideOrientation::Vertical ? Qt::SplitHCursor : Qt::SplitVCursor);
    } else {
      if (tool_ == CanvasTool::Move) {
        if (const auto rect = move_transform_controls_rect(); rect.has_value()) {
          const auto handle = transform_handle_at(event->pos(), *rect, 0.0);
          if (handle != TransformHandle::None) {
            set_transform_cursor_for_handle(handle);
            if (handle == TransformHandle::Move && auto_select_layer_) {
              update_move_hover_outline(event->pos(), event->modifiers());
            } else {
              clear_move_hover_outline();
            }
            last_mouse_position_ = event->pos();
            return;
          }
        }
      }
      update_tool_cursor();
      update_move_hover_outline(event->pos(), event->modifiers());
    }
  }

  last_mouse_position_ = event->pos();
}

void CanvasWidget::refresh_tool_cursor() {
  // Re-apply the tool cursor unless an in-progress gesture owns it. Used when
  // the pointer enters the canvas or the canvas/window regains focus, since a
  // pen does not emit move events on its own and Windows may reset the cursor
  // to an arrow when the window is re-activated.
  if (!panning_ && !pen_zoom_dragging_ && !dragging_transform_ && !transforming_layer_) {
    update_tool_cursor();
  }
}

void CanvasWidget::enterEvent(QEnterEvent* event) {
  // Apply the tool cursor as soon as the pointer enters the canvas. With a
  // mouse the cursor is refreshed by the steady stream of move events, but a
  // pen only emits events once it moves, so without this the brush cursor can
  // lag behind the pen when it first comes into proximity over the canvas.
  refresh_tool_cursor();
  QWidget::enterEvent(event);
}

void CanvasWidget::focusInEvent(QFocusEvent* event) {
  refresh_tool_cursor();
  QWidget::focusInEvent(event);
}

void CanvasWidget::leaveEvent(QEvent* event) {
  clear_move_hover_outline();
  QWidget::leaveEvent(event);
}

void CanvasWidget::mouseReleaseEvent(QMouseEvent* event) {
  if (!handling_tablet_event_) {
    active_pen_input_sample_.reset();
  }
  if (panning_) {
    panning_ = false;
    update_tool_cursor();
    return;
  }

  if (edit_locked_ && !zooming_) {
    clear_move_hover_outline();
    event->accept();
    return;
  }

  if (dragging_guide_) {
    finish_guide_drag(event->pos(), event->modifiers());
    return;
  }

  if (dragging_transform_) {
    update_free_transform_preview(document_position_f(event->position()), event->modifiers());
    dragging_transform_ = false;
    transform_drag_handle_ = TransformHandle::None;
    update_tool_cursor();
    update();
    notify_transform_controls_changed();
    return;
  }

  if (painting_) {
    QRect dirty;
    const auto document_point = document_position(event->pos());
    const auto document_point_f = document_position_f(event->position());
    const auto effective_tool = effective_tool_for_input();
    if (effective_tool == CanvasTool::Clone) {
      const auto constrained_point = axis_constrained_stroke_point(document_point, event->modifiers());
      dirty = clone_brush_segment(last_document_position_, constrained_point);
      last_document_position_ = constrained_point;
      last_document_position_f_ = QPointF(constrained_point);
    } else if (effective_tool == CanvasTool::Brush || effective_tool == CanvasTool::Eraser) {
      if (effective_brush_input().size == 1) {
        const auto constrained_point = axis_constrained_stroke_point(document_point, event->modifiers());
        dirty = draw_brush_segment(last_document_position_, constrained_point, effective_tool == CanvasTool::Eraser);
        last_document_position_ = constrained_point;
        last_document_position_f_ = QPointF(constrained_point);
      } else {
        const auto constrained_point = axis_constrained_stroke_point(document_point_f, event->modifiers());
        dirty = finish_smoothed_brush_stroke(constrained_point, effective_tool == CanvasTool::Eraser);
        last_document_position_ = QPoint(static_cast<int>(std::lround(constrained_point.x())),
                                         static_cast<int>(std::lround(constrained_point.y())));
        last_document_position_f_ = constrained_point;
      }
    } else {
      last_document_position_ = document_point;
      last_document_position_f_ = document_point_f;
    }
    painting_ = false;
    clone_source_cache_ = QImage();
    smudge_state_ = {};
    reset_brush_smoothing();
    reset_axis_constrained_stroke();
    clear_brush_stroke_tracking();
    if (!dirty.isEmpty()) {
      document_changed_impl(QRegion(dirty), false, DocumentChangeReason::BrushStrokeFinished);
    } else {
      notify_document_changed(DocumentChangeReason::BrushStrokeFinished);
    }
    return;
  }

  if (dragging_text_rect_) {
    dragging_text_rect_ = false;
    text_rect_current_ = snapped_document_point(document_position(event->pos()));
    const auto rect = normalized_rect(text_rect_start_, text_rect_current_);
    QRect requested_box;
    if (rect.width() >= 16 && rect.height() >= 16) {
      requested_box = rect;
    }
    if (text_requested_callback_) {
      text_requested_callback_(requested_box.isValid() && !requested_box.isEmpty() ? requested_box.topLeft()
                                                                                   : text_rect_start_,
                               requested_box);
    }
    update();
    return;
  }

  if (move_drag_pending_) {
    move_drag_pending_ = false;
    moving_layers_.clear();
    move_preview_delta_ = QPoint();
    move_preview_patches_.clear();
    move_preview_patches_delta_.reset();
    moving_layers_use_outline_preview_ = false;
    move_base_cache_ = QImage();
    reset_axis_constrained_stroke();
    update_move_hover_outline(event->pos(), event->modifiers());
    update();
    return;
  }

  if (moving_layer_) {
    const auto trace_move_release = render_trace_enabled();
    const auto trace_start =
        trace_move_release ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    const auto constrained_delta =
        axis_constrained_move_delta(document_position(event->pos()) - move_start_, event->modifiers());
    move_preview_delta_ = axis_constrained_move_delta(snapped_move_delta(constrained_delta), event->modifiers());
    QRegion dirty_region;
    QRegion patched_region;
    std::vector<RenderedDocumentPatch> precommit_patches;
    bool attempted_precommit_patch = false;
    bool used_precommit_patch = false;
    bool reused_preview_patch = false;
    const auto move_layer_count = moving_layers_.size();
    const auto move_operation_active = !move_preview_delta_.isNull();
    if (move_operation_active) {
      begin_processing_operation();
    }
    if (!move_preview_delta_.isNull()) {
      dirty_region = moving_layers_dirty_region(QPoint(), move_preview_delta_);
      patched_region = dirty_region;
      if (document_ != nullptr) {
        patched_region = patched_region.intersected(QRect(0, 0, document_->width(), document_->height()));
      }
      tick_processing_operation();
      if (before_edit_callback_) {
        before_edit_callback_(moving_layers_.size() > 1U ? tr("Move layers") : tr("Move layer"));
      }
      if (document_ != nullptr && !patched_region.isEmpty() && !render_cache_dirty_ && !render_cache_.isNull() &&
          render_cache_.size() == QSize(document_->width(), document_->height())) {
        attempted_precommit_patch = true;
        if (move_preview_patches_delta_.has_value() && *move_preview_patches_delta_ == move_preview_delta_ &&
            !move_preview_patches_.empty()) {
          precommit_patches = std::move(move_preview_patches_);
          reused_preview_patch = true;
        } else {
          const auto final_bounds = moving_layer_bounds(move_preview_delta_);
          const auto force_processing_wait =
              moving_layers_use_outline_preview_ ||
              std::any_of(moving_layers_.begin(), moving_layers_.end(),
                          [](const MovingLayer& layer) { return layer.expensive_style; });
          precommit_patches =
              render_document_patches_with_processing(patched_region, final_bounds, force_processing_wait);
          for (auto& patch : precommit_patches) {
            patch.image = patch.image.convertToFormat(QImage::Format_RGBA8888);
          }
        }
      }
      for (const auto& moving_layer : moving_layers_) {
        auto* layer = document_->find_layer(moving_layer.id);
        if (layer == nullptr) {
          continue;
        }
        auto new_bounds = moving_layer.original_bounds;
        new_bounds.x += move_preview_delta_.x();
        new_bounds.y += move_preview_delta_.y();
        layer->set_bounds(new_bounds);
        patchy::translate_moved_layer_metadata(*layer, move_preview_delta_.x(), move_preview_delta_.y(),
                                               document_->width(), document_->height());
      }
    }
    moving_layer_ = false;
    move_drag_pending_ = false;
    moving_layers_.clear();
    move_preview_patches_.clear();
    move_preview_patches_delta_.reset();
    moving_layers_use_outline_preview_ = false;
    move_base_cache_ = QImage();
    reset_axis_constrained_stroke();
    update_move_transform_controls_dirty(std::nullopt);
    update_move_hover_outline(event->pos(), event->modifiers());
    if (!dirty_region.isEmpty()) {
      if (!precommit_patches.empty() && patch_render_cache_patches(precommit_patches)) {
        ++render_cache_diagnostics_.move_precommit_patches;
        if (reused_preview_patch) {
          ++render_cache_diagnostics_.move_preview_patch_reuses;
        }
        used_precommit_patch = true;
        notify_document_changed();
        if (zoom_ < 1.0) {
          update();
        } else {
          QRegion widget_region;
          for (const auto& rect : patched_region) {
            widget_region += widget_rect_for_document_rect(rect);
          }
          update(widget_region);
        }
      } else {
        document_changed_effect_bounds(dirty_region);
      }
    } else {
      update();
    }
    if (move_operation_active) {
      end_processing_operation();
    }
    if (trace_move_release) {
      const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                                trace_start);
      const auto trace_dirty = patched_region.boundingRect();
      std::cerr << "PATCHY_RENDER_TRACE move_release dirty=" << trace_dirty.x() << "," << trace_dirty.y() << ","
                << trace_dirty.width() << "," << trace_dirty.height() << " layers=" << move_layer_count
                << " precommit_attempted=" << (attempted_precommit_patch ? 1 : 0)
                << " precommit_patched=" << (used_precommit_patch ? 1 : 0)
                << " preview_reused=" << (reused_preview_patch ? 1 : 0) << " elapsed_ms=" << elapsed.count()
                << '\n';
    }
    return;
  }

  if (selecting_) {
    selecting_ = false;
    const auto document_point = document_position(event->pos());
    if (spacebar_repositioning_drag_rect_) {
      const auto raw_delta = document_point - spacebar_reposition_origin_document_position_;
      const auto delta = snapped_rect_delta(
          marquee_selection_rect(spacebar_reposition_start_selection_start_,
                                 spacebar_reposition_start_selection_current_),
          raw_delta);
      selection_start_ = spacebar_reposition_start_selection_start_ + delta;
      selection_current_ = spacebar_reposition_start_selection_current_ + delta;
      spacebar_repositioning_drag_rect_ = false;
    } else {
      selection_current_ = snapped_marquee_current_point(selection_start_, document_point);
    }
    if (selection_feather_radius_ > 0) {
      QRect mask_bounds;
      auto mask = marquee_selection_mask(selection_start_, selection_current_, mask_bounds);
      combine_selection_from_mask(region_from_alpha_mask(mask, mask_bounds), mask_bounds, std::move(mask));
    } else {
      combine_selection_from_region(marquee_selection_region(selection_start_, selection_current_));
    }
    selection_before_edit_ = QRegion();
    selection_display_region_before_edit_ = QRegion();
    selection_mask_before_edit_bounds_ = {};
    selection_mask_before_edit_alpha_ = QImage();
    emit_info_for_widget_position(event->pos());
    update();
    return;
  }

  if (lassoing_) {
    lassoing_ = false;
    if (document_ != nullptr) {
      const auto point = clamped_document_point(*document_, document_position(event->pos()));
      if (lasso_points_.isEmpty() || lasso_points_.last() != point) {
        lasso_points_ << point;
      }
      if (lasso_points_.size() >= 3) {
        auto lasso_region = QRegion(lasso_points_, Qt::WindingFill);
        lasso_region = lasso_region.intersected(QRegion(QRect(0, 0, document_->width(), document_->height())));
        if (selection_feather_radius_ > 0) {
          QRect mask_bounds;
          auto mask = lasso_selection_mask(lasso_points_, mask_bounds);
          combine_selection_from_mask(region_from_alpha_mask(mask, mask_bounds), mask_bounds, std::move(mask));
        } else {
          combine_selection_from_region(lasso_region);
        }
      } else {
        restore_selection_before_edit();
      }
    }
    selection_before_edit_ = QRegion();
    selection_display_region_before_edit_ = QRegion();
    selection_mask_before_edit_bounds_ = {};
    selection_mask_before_edit_alpha_ = QImage();
    lasso_points_.clear();
    update();
    return;
  }

  if (zooming_) {
    zooming_ = false;
    if (document_ != nullptr) {
      zoom_current_ = clamped_document_point(*document_, document_position(event->pos()));
      const auto widget_drag = (event->pos() - widget_position(zoom_start_)).manhattanLength();
      const auto zoom_rect = normalized_rect(zoom_start_, zoom_current_);
      if (widget_drag >= 8 && zoom_rect.width() > 1 && zoom_rect.height() > 1) {
        zoom_to_document_rect(zoom_rect);
      } else {
        zoom_at_widget_point(event->position(), (event->modifiers() & Qt::AltModifier) != 0 ? 0.5 : 2.0);
      }
    }
    emit_info_for_widget_position(event->pos());
    update();
    return;
  }

  if (drawing_shape_) {
    const auto document_point = document_position(event->pos());
    const auto snapped_point = snapped_document_point(document_point);
    if (spacebar_repositioning_drag_rect_) {
      const auto delta = document_point - spacebar_reposition_last_document_position_;
      shape_start_ += delta;
      shape_current_ += delta;
      spacebar_repositioning_drag_rect_ = false;
    } else {
      shape_current_ = snapped_point;
    }
    const auto erase = false;
    QRect preview_rect = normalized_rect(shape_start_, shape_current_);
    if (document_ != nullptr) {
      const auto margin = std::max(4, brush_size_ + 4);
      preview_rect = preview_rect.adjusted(-margin, -margin, margin, margin)
                         .intersected(QRect(0, 0, document_->width(), document_->height()));
    }
    begin_processing_operation();
    QRect dirty;
    if (tool_ == CanvasTool::Line) {
      dirty = draw_line(shape_start_, shape_current_, erase);
    } else if (tool_ == CanvasTool::Gradient) {
      dirty = draw_gradient(shape_start_, shape_current_);
    } else if (tool_ == CanvasTool::Rectangle) {
      dirty = draw_rectangle(shape_start_, shape_current_, erase);
    } else if (tool_ == CanvasTool::Ellipse) {
      dirty = draw_ellipse(shape_start_, shape_current_, erase);
    }
    tick_processing_operation();
    drawing_shape_ = false;
    clear_brush_stroke_tracking();
    const auto repaint_rect =
        !preview_rect.isEmpty() && !dirty.isEmpty() ? preview_rect.united(dirty)
        : !preview_rect.isEmpty()                  ? preview_rect
                                                   : dirty;
    document_changed(repaint_rect);
    end_processing_operation();
    return;
  }
}

void CanvasWidget::mouseDoubleClickEvent(QMouseEvent* event) {
  const auto document_point = document_position(event->pos());
  if (edit_locked_) {
    show_edit_locked_message();
    event->accept();
    return;
  }
  if (event->button() == Qt::LeftButton && document_contains(document_point)) {
    if (transforming_layer_ && transform_layer_id_.has_value()) {
      const auto transform_hit = transform_handle_at(event->pos());
      const auto text_layer_id = *transform_layer_id_;
      auto* transformed_layer = document_ != nullptr ? document_->find_layer(text_layer_id) : nullptr;
      if (transformed_layer != nullptr && layer_is_text(*transformed_layer) &&
          transform_hit != TransformHandle::None) {
        finish_free_transform();
        if (auto* layer = document_ != nullptr ? document_->find_layer(text_layer_id) : nullptr; layer != nullptr) {
          activate_layer(*layer);
          if (text_requested_callback_) {
            text_requested_callback_(document_point, QRect());
          }
          event->accept();
          return;
        }
      }
    }
    if (transforming_layer_) {
      event->accept();
      return;
    }
    if (auto* layer = topmost_text_layer_at(document_point); layer != nullptr) {
      activate_layer(*layer);
      if (text_requested_callback_) {
        text_requested_callback_(document_point, QRect());
      }
      event->accept();
      return;
    }
  }
  QWidget::mouseDoubleClickEvent(event);
}

void CanvasWidget::tabletEvent(QTabletEvent* event) {
  if (event == nullptr || !pen_input_settings_.enabled) {
    active_pen_input_sample_.reset();
    QWidget::tabletEvent(event);
    return;
  }

  const auto sample = pen_input_sample_from_tablet_event(*event);
  last_pen_input_sample_ = sample;
  active_pen_input_sample_ = sample;
  if (dispatch_tablet_as_mouse(event, sample)) {
    event->accept();
    if (event->type() == QEvent::TabletRelease) {
      active_pen_input_sample_.reset();
    }
    return;
  }

  active_pen_input_sample_.reset();
  event->ignore();
}

void CanvasWidget::keyPressEvent(QKeyEvent* event) {
  if (edit_locked_) {
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
      spacebar_panning_ = true;
      setCursor(Qt::OpenHandCursor);
      event->accept();
      return;
    }
    show_edit_locked_message();
    event->accept();
    return;
  }

  if (dragging_guide_ && event->key() == Qt::Key_Escape) {
    cancel_guide_drag();
    event->accept();
    return;
  }

  if (!guides_locked_ && has_selected_guides() &&
      (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)) {
    clear_selected_guides();
    event->accept();
    return;
  }

  if (!event->isAutoRepeat() && event->key() == Qt::Key_A &&
      event->modifiers() == Qt::ControlModifier) {
    select_all();
    event->accept();
    return;
  }

  if (transforming_layer_) {
    if (event->key() == Qt::Key_Escape) {
      cancel_free_transform();
      event->accept();
      return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
      commit_free_transform();
      event->accept();
      return;
    }
  }

  if (dragging_text_rect_ && event->key() == Qt::Key_Escape) {
    dragging_text_rect_ = false;
    text_rect_current_ = text_rect_start_;
    emit_info_for_widget_position(last_mouse_position_);
    update();
    event->accept();
    return;
  }

  if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
    if (selecting_) {
      spacebar_repositioning_drag_rect_ = true;
      spacebar_reposition_last_document_position_ = document_position(last_mouse_position_);
      spacebar_reposition_origin_document_position_ = spacebar_reposition_last_document_position_;
      spacebar_reposition_start_selection_start_ = selection_start_;
      spacebar_reposition_start_selection_current_ = selection_current_;
      setCursor(Qt::SizeAllCursor);
    } else if (drawing_shape_) {
      spacebar_repositioning_drag_rect_ = true;
      spacebar_reposition_last_document_position_ = document_position(last_mouse_position_);
      setCursor(Qt::SizeAllCursor);
    } else {
      spacebar_panning_ = true;
      setCursor(Qt::OpenHandCursor);
    }
    event->accept();
    return;
  }

  if (!event->isAutoRepeat() && (event->modifiers() == Qt::NoModifier || event->modifiers() == Qt::ShiftModifier)) {
    const auto step = event->modifiers() == Qt::ShiftModifier ? 10 : 1;
    QPoint delta;
    switch (event->key()) {
      case Qt::Key_Left:
        delta = QPoint(-step, 0);
        break;
      case Qt::Key_Right:
        delta = QPoint(step, 0);
        break;
      case Qt::Key_Up:
        delta = QPoint(0, -step);
        break;
      case Qt::Key_Down:
        delta = QPoint(0, step);
        break;
      default:
        break;
    }
    const auto movable_ids = movable_layer_ids();
    if (!delta.isNull() && !movable_ids.empty()) {
      begin_processing_operation();
      tick_processing_operation();
      if (before_edit_callback_) {
        before_edit_callback_(movable_ids.size() >= 2U ? tr("Nudge layers") : tr("Nudge layer"));
      }
      const auto dirty = move_active_layer_by(delta);
      if (!dirty.isEmpty()) {
        document_changed_effect_bounds(dirty);
      }
      end_processing_operation();
      event->accept();
      return;
    } else if (!delta.isNull() && active_layer_locks_position()) {
      show_layer_position_locked_message();
      event->accept();
      return;
    }
  }
  QWidget::keyPressEvent(event);
}

void CanvasWidget::keyReleaseEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
    spacebar_repositioning_drag_rect_ = false;
    spacebar_panning_ = false;
    set_tool(tool_);
    event->accept();
    return;
  }
  QWidget::keyReleaseEvent(event);
}

void CanvasWidget::focusOutEvent(QFocusEvent* event) {
  const auto was_painting = painting_;
  spacebar_repositioning_drag_rect_ = false;
  spacebar_panning_ = false;
  dragging_text_rect_ = false;
  if (was_painting) {
    painting_ = false;
  }
  if (!was_painting && !drawing_shape_) {
    clear_brush_stroke_tracking();
  }
  clone_source_cache_ = QImage();
  smudge_state_ = {};
  reset_brush_smoothing();
  reset_axis_constrained_stroke();
  zooming_ = false;
  set_tool(tool_);
  if (was_painting) {
    clear_brush_stroke_tracking();
    notify_document_changed(DocumentChangeReason::BrushStrokeFinished);
  }
  QWidget::focusOutEvent(event);
}

void CanvasWidget::timerEvent(QTimerEvent* event) {
  if (event->timerId() == processing_animation_timer_.timerId()) {
    processing_animation_frame_ = (processing_animation_frame_ + 1) % 12;
    ++render_cache_diagnostics_.processing_overlay_frames;
    if (processing_overlay_visible_) {
      update();
    } else {
      processing_animation_timer_.stop();
    }
    event->accept();
    return;
  }
  if (event->timerId() == selection_timer_.timerId()) {
    selection_dash_offset_ = (selection_dash_offset_ + 1) % 8;
    if ((!selection_.isEmpty() && selection_edges_visible_) || lassoing_ || zooming_) {
      update();
    }
    event->accept();
    return;
  }
  QWidget::timerEvent(event);
}

QImage CanvasWidget::render_document_image() const {
  return qimage_from_document(*document_, true).convertToFormat(QImage::Format_RGBA8888);
}

void CanvasWidget::ensure_render_cache() {
  if (document_ == nullptr) {
    return;
  }
  if (!render_cache_dirty_ && render_cache_.size() == QSize(document_->width(), document_->height())) {
    return;
  }

  render_cache_ = render_document_image_with_processing();
  render_cache_dirty_ = false;
  ++render_cache_diagnostics_.full_refreshes;
  invalidate_display_mip_cache();
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
          widget->render_cache_ = std::move(*image);
          widget->render_cache_dirty_ = false;
          ++widget->render_cache_diagnostics_.full_refreshes;
          widget->invalidate_display_mip_cache();
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
    painter.drawImage(document_rect.topLeft(), patch.image);
    ++patched;
  }
  if (patched <= 0) {
    return false;
  }
  render_cache_dirty_ = false;
  render_cache_diagnostics_.partial_patches += patched;
  invalidate_display_mip_cache();
  return true;
}

void CanvasWidget::invalidate_display_mip_cache() noexcept {
  display_mip_cache_.clear();
  display_mip_source_size_ = QSize();
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

const QImage& CanvasWidget::display_image_for_zoom() {
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

  const auto major = ruler_major_tick_interval(zoom_);
  const auto minor = ruler_minor_tick_interval(zoom_);
  QFont label_font = font();
  label_font.setPointSize(std::max(7, label_font.pointSize() - 2));
  painter.setFont(label_font);
  const QFontMetrics metrics(label_font);
  QPen tick_pen(QColor(185, 190, 198));
  tick_pen.setCosmetic(true);
  painter.setPen(tick_pen);

  const auto draw_horizontal = [&] {
    const auto start = std::floor(document_position_f(QPointF(0.0, 0.0)).x() / minor) * minor;
    const auto end = document_position_f(QPointF(width(), 0.0)).x();
    for (double value = start; value <= end + minor; value += minor) {
      if (value < 0.0 || value > static_cast<double>(document_->width())) {
        continue;
      }
      const auto x = widget_position_f(QPointF(value, 0.0)).x();
      const bool is_major = std::abs(std::remainder(value, major)) < 0.0001;
      const int length = is_major ? 11 : 6;
      painter.drawLine(QPointF(x, kTopRulerHeight - 1), QPointF(x, kTopRulerHeight - 1 - length));
      if (is_major) {
        const auto label = QString::number(static_cast<int>(std::llround(value)));
        painter.drawText(QPointF(x + 3.0, static_cast<double>(metrics.ascent() + 2)), label);
      }
    }
  };
  const auto draw_vertical = [&] {
    const auto start = std::floor(document_position_f(QPointF(0.0, 0.0)).y() / minor) * minor;
    const auto end = document_position_f(QPointF(0.0, height())).y();
    for (double value = start; value <= end + minor; value += minor) {
      if (value < 0.0 || value > static_cast<double>(document_->height())) {
        continue;
      }
      const auto y = widget_position_f(QPointF(0.0, value)).y();
      const bool is_major = std::abs(std::remainder(value, major)) < 0.0001;
      const int length = is_major ? 11 : 6;
      painter.drawLine(QPointF(kLeftRulerWidth - 1, y), QPointF(kLeftRulerWidth - 1 - length, y));
      if (is_major) {
        const auto label = QString::number(static_cast<int>(std::llround(value)));
        painter.drawText(QRectF(2.0, y + 2.0, kLeftRulerWidth - 8.0, metrics.height()), Qt::AlignRight, label);
      }
    }
  };
  draw_horizontal();
  draw_vertical();
  painter.restore();
}

void CanvasWidget::draw_shape_preview(QPainter& painter) const {
  if (!drawing_shape_) {
    return;
  }

  const auto a = widget_position(shape_start_);
  const auto b = widget_position(shape_current_);
  if (tool_ == CanvasTool::Gradient) {
    QGradient* raw_gradient = nullptr;
    QLinearGradient linear_gradient(a, b);
    const auto radius = std::max(1.0, std::hypot(static_cast<double>(b.x() - a.x()), static_cast<double>(b.y() - a.y())));
    QRadialGradient radial_gradient(a, radius);
    raw_gradient = gradient_method_ == GradientMethod::Radial ? static_cast<QGradient*>(&radial_gradient)
                                                              : static_cast<QGradient*>(&linear_gradient);
    const auto stops = effective_gradient_stops();
    for (const auto& stop : stops) {
      auto color = stop.color;
      color.a = static_cast<std::uint8_t>(
          std::clamp(std::lround(static_cast<float>(color.a) * static_cast<float>(gradient_opacity_) / 100.0F), 0L,
                     255L));
      raw_gradient->setColorAt(gradient_reverse_ ? 1.0 - static_cast<double>(stop.location)
                                                 : static_cast<double>(stop.location),
                                QColor(color.r, color.g, color.b, color.a));
    }
    painter.setPen(QPen(QBrush(*raw_gradient), std::max(2, static_cast<int>(std::round(brush_size_ * zoom_ * 0.35)))));
    painter.drawLine(a, b);
    painter.setPen(QPen(QColor(230, 235, 242), 1));
    const auto first = gradient_color_at(stops, static_cast<float>(gradient_opacity_) / 100.0F, gradient_reverse_, 0.0);
    const auto last = gradient_color_at(stops, static_cast<float>(gradient_opacity_) / 100.0F, gradient_reverse_, 1.0);
    painter.setBrush(QColor(first.r, first.g, first.b, first.a));
    painter.drawEllipse(a, 4, 4);
    painter.setBrush(QColor(last.r, last.g, last.b, last.a));
    painter.drawEllipse(b, 4, 4);
  } else if (tool_ == CanvasTool::Line) {
    QPen pen(primary_color_);
    pen.setWidth(std::max(1, static_cast<int>(std::round(static_cast<double>(brush_size_) * zoom_))));
    pen.setCosmetic(false);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawLine(a, b);
  } else if (tool_ == CanvasTool::Rectangle) {
    if (fill_shapes_) {
      painter.setPen(Qt::NoPen);
      painter.setBrush(QColor(primary_color_.red(), primary_color_.green(), primary_color_.blue(),
                              std::clamp(brush_opacity_ * 2, 60, 180)));
    } else {
      QPen pen(primary_color_);
      pen.setWidth(std::max(1, static_cast<int>(std::round(static_cast<double>(brush_size_) * zoom_))));
      pen.setCosmetic(false);
      painter.setPen(pen);
      painter.setBrush(Qt::NoBrush);
    }
    painter.drawRect(normalized_rect(a, b));
  } else if (tool_ == CanvasTool::Ellipse) {
    if (fill_shapes_) {
      painter.setPen(Qt::NoPen);
      painter.setBrush(QColor(primary_color_.red(), primary_color_.green(), primary_color_.blue(),
                              std::clamp(brush_opacity_ * 2, 60, 180)));
    } else {
      QPen pen(primary_color_);
      pen.setWidth(std::max(1, static_cast<int>(std::round(static_cast<double>(brush_size_) * zoom_))));
      pen.setCosmetic(false);
      painter.setPen(pen);
      painter.setBrush(Qt::NoBrush);
    }
    painter.drawEllipse(normalized_rect(a, b));
  }
}

void CanvasWidget::draw_text_rect_preview(QPainter& painter) const {
  if (!dragging_text_rect_) {
    return;
  }

  const auto preview_rect = QRect(widget_position(text_rect_start_), widget_position(text_rect_current_)).normalized();
  if (preview_rect.width() < 2 && preview_rect.height() < 2) {
    return;
  }

  painter.save();
  painter.setBrush(QColor(65, 135, 220, 28));
  painter.setPen(Qt::NoPen);
  painter.drawRect(preview_rect);
  QPen border(QColor(95, 170, 255), 1.0);
  border.setCosmetic(true);
  border.setDashPattern({5.0, 4.0});
  painter.setBrush(Qt::NoBrush);
  painter.setPen(border);
  painter.drawRect(preview_rect.adjusted(0, 0, -1, -1));
  painter.restore();
}

void CanvasWidget::draw_zoom_preview(QPainter& painter) const {
  if (!zooming_) {
    return;
  }

  const auto preview_rect = QRect(widget_position(zoom_start_), widget_position(zoom_current_)).normalized();
  if (preview_rect.width() < 2 && preview_rect.height() < 2) {
    return;
  }

  painter.save();
  painter.setBrush(QColor(65, 135, 220, 35));
  painter.setPen(Qt::NoPen);
  painter.drawRect(preview_rect);

  QPen dark(QColor(15, 18, 22), 1.0);
  dark.setDashPattern({4.0, 4.0});
  dark.setDashOffset(selection_dash_offset_ + 4);
  dark.setCosmetic(true);
  QPen light(QColor(248, 250, 253), 1.0);
  light.setDashPattern({4.0, 4.0});
  light.setDashOffset(selection_dash_offset_);
  light.setCosmetic(true);
  painter.setBrush(Qt::NoBrush);
  painter.setPen(dark);
  painter.drawRect(preview_rect);
  painter.setPen(light);
  painter.drawRect(preview_rect);
  painter.restore();
}

QPointF CanvasWidget::transform_handle_position(TransformHandle handle) const {
  return transform_handle_position(handle, transform_current_rect_, transform_angle_);
}

QPointF CanvasWidget::transform_handle_position(TransformHandle handle, QRectF document_rect,
                                                double angle_degrees) const {
  const auto rect = widget_rect_for_document_rect(document_rect);
  const auto center = rect.center();
  QPointF local;
  switch (handle) {
    case TransformHandle::TopLeft:
      local = QPointF(-rect.width() / 2.0, -rect.height() / 2.0);
      break;
    case TransformHandle::Top:
      local = QPointF(0.0, -rect.height() / 2.0);
      break;
    case TransformHandle::TopRight:
      local = QPointF(rect.width() / 2.0, -rect.height() / 2.0);
      break;
    case TransformHandle::Right:
      local = QPointF(rect.width() / 2.0, 0.0);
      break;
    case TransformHandle::BottomRight:
      local = QPointF(rect.width() / 2.0, rect.height() / 2.0);
      break;
    case TransformHandle::Bottom:
      local = QPointF(0.0, rect.height() / 2.0);
      break;
    case TransformHandle::BottomLeft:
      local = QPointF(-rect.width() / 2.0, rect.height() / 2.0);
      break;
    case TransformHandle::Left:
      local = QPointF(-rect.width() / 2.0, 0.0);
      break;
    case TransformHandle::Rotate:
      local = QPointF(0.0, -rect.height() / 2.0 - 32.0);
      break;
    case TransformHandle::Move:
    case TransformHandle::None:
      local = QPointF(0.0, 0.0);
      break;
  }

  QTransform transform;
  transform.translate(center.x(), center.y());
  transform.rotate(angle_degrees);
  return transform.map(local);
}

void CanvasWidget::draw_free_transform(QPainter& painter) const {
  if (!transforming_layer_) {
    return;
  }

  const auto rect = widget_rect_for_document_rect(transform_current_rect_);
  if (rect.isEmpty()) {
    return;
  }

  if (!transform_source_image_.isNull() && transform_composited_preview_cache_.isNull()) {
    painter.save();
    painter.setRenderHint(QPainter::SmoothPixmapTransform,
                          transform_interpolation_ != TransformInterpolation::NearestNeighbor);
    painter.translate(rect.center());
    painter.rotate(transform_angle_);
    const QRectF local_rect(-rect.width() / 2.0, -rect.height() / 2.0, rect.width(), rect.height());
    painter.drawImage(local_rect, transform_source_image_, QRectF(transform_source_image_.rect()));
    painter.restore();
  }

  draw_transform_controls(painter, transform_current_rect_, transform_angle_);
}

void CanvasWidget::draw_transform_controls(QPainter& painter, QRectF document_rect, double angle_degrees) const {
  const auto rect = widget_rect_for_document_rect(document_rect);
  if (rect.isEmpty()) {
    return;
  }

  painter.save();
  painter.translate(rect.center());
  painter.rotate(angle_degrees);
  const QRectF local_rect(-rect.width() / 2.0, -rect.height() / 2.0, rect.width(), rect.height());
  painter.setBrush(Qt::NoBrush);
  painter.setPen(QPen(QColor(95, 170, 255), 1.0, Qt::DashLine));
  painter.drawRect(local_rect);
  painter.setPen(QPen(QColor(95, 170, 255), 1.0));
  painter.drawLine(QPointF(0.0, -rect.height() / 2.0), QPointF(0.0, -rect.height() / 2.0 - 32.0));
  painter.restore();

  constexpr double kHandleSize = 8.0;
  const std::array<TransformHandle, 9> handles = {
      TransformHandle::TopLeft,    TransformHandle::Top,    TransformHandle::TopRight,
      TransformHandle::Right,      TransformHandle::BottomRight, TransformHandle::Bottom,
      TransformHandle::BottomLeft, TransformHandle::Left,   TransformHandle::Rotate};
  painter.save();
  painter.setPen(QPen(QColor(10, 14, 20), 1.0));
  for (const auto handle : handles) {
    const auto point = transform_handle_position(handle, document_rect, angle_degrees);
    const QRectF handle_rect(point.x() - kHandleSize / 2.0, point.y() - kHandleSize / 2.0, kHandleSize, kHandleSize);
    painter.setBrush(handle == TransformHandle::Rotate ? QColor(95, 170, 255) : QColor(245, 248, 252));
    painter.drawRect(handle_rect);
  }
  painter.restore();
}

void CanvasWidget::draw_move_transform_controls(QPainter& painter) const {
  const auto rect = move_transform_controls_rect();
  if (!rect.has_value()) {
    return;
  }
  draw_transform_controls(painter, *rect, 0.0);
}

void CanvasWidget::draw_selection_overlay(QPainter& painter) const {
  if (!selection_.isEmpty() && selection_edges_visible_) {
    const auto& outline_region = selection_display_region_.isEmpty() ? selection_ : selection_display_region_;
    QPainterPath outline_path;
    const auto add_vertical_edges = [this, &outline_path](const QRegion& edges, bool right_edge) {
      for (const auto& rect : edges) {
        const auto x = static_cast<double>(right_edge ? rect.right() + 1 : rect.left());
        const auto top = static_cast<double>(rect.top());
        const auto bottom = static_cast<double>(rect.bottom() + 1);
        outline_path.moveTo(widget_position_f(QPointF(x, top)));
        outline_path.lineTo(widget_position_f(QPointF(x, bottom)));
      }
    };
    const auto add_horizontal_edges = [this, &outline_path](const QRegion& edges, bool bottom_edge) {
      for (const auto& rect : edges) {
        const auto y = static_cast<double>(bottom_edge ? rect.bottom() + 1 : rect.top());
        const auto left = static_cast<double>(rect.left());
        const auto right = static_cast<double>(rect.right() + 1);
        outline_path.moveTo(widget_position_f(QPointF(left, y)));
        outline_path.lineTo(widget_position_f(QPointF(right, y)));
      }
    };

    add_vertical_edges(outline_region.subtracted(outline_region.translated(1, 0)), false);
    add_vertical_edges(outline_region.subtracted(outline_region.translated(-1, 0)), true);
    add_horizontal_edges(outline_region.subtracted(outline_region.translated(0, 1)), false);
    add_horizontal_edges(outline_region.subtracted(outline_region.translated(0, -1)), true);

    if (!outline_path.isEmpty()) {
      QPen white(QColor(248, 250, 253), 1.0);
      white.setDashPattern({4.0, 4.0});
      white.setDashOffset(selection_dash_offset_);
      white.setCosmetic(true);
      QPen black(QColor(18, 20, 24), 1.0);
      black.setDashPattern({4.0, 4.0});
      black.setDashOffset(selection_dash_offset_ + 4);
      black.setCosmetic(true);
      painter.setBrush(Qt::NoBrush);
      painter.setPen(black);
      painter.drawPath(outline_path);
      painter.setPen(white);
      painter.drawPath(outline_path);
    }
  }

  if (lassoing_ && lasso_points_.size() > 1) {
    QPolygon preview;
    preview.reserve(lasso_points_.size());
    for (const auto& point : lasso_points_) {
      preview << widget_position(point);
    }
    QPen black(QColor(18, 20, 24), 1.0);
    black.setDashPattern({4.0, 4.0});
    black.setDashOffset(selection_dash_offset_ + 4);
    black.setCosmetic(true);
    QPen white(QColor(248, 250, 253), 1.0);
    white.setDashPattern({4.0, 4.0});
    white.setDashOffset(selection_dash_offset_);
    white.setCosmetic(true);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(black);
    painter.drawPolyline(preview);
    painter.setPen(white);
    painter.drawPolyline(preview);
  }
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

void CanvasWidget::update_tool_cursor() {
  if (spacebar_panning_ || tool_ == CanvasTool::Pan) {
    setCursor(Qt::OpenHandCursor);
    return;
  }
  if (tool_ == CanvasTool::Move) {
    setCursor(Qt::SizeAllCursor);
    return;
  }
  if (tool_ == CanvasTool::MagicWand) {
    setCursor(magic_wand_cursor());
    return;
  }
  if (tool_ == CanvasTool::Text) {
    setCursor(Qt::IBeamCursor);
    return;
  }
  if (tool_ == CanvasTool::Zoom) {
    QPixmap pixmap(24, 24);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(20, 23, 28), 3));
    painter.drawEllipse(QRect(4, 4, 11, 11));
    painter.drawLine(13, 13, 21, 21);
    painter.setPen(QPen(QColor(245, 248, 252), 1.6));
    painter.drawEllipse(QRect(4, 4, 11, 11));
    painter.drawLine(13, 13, 21, 21);
    painter.drawLine(7, 10, 13, 10);
    painter.drawLine(10, 7, 10, 13);
    painter.end();
    setCursor(QCursor(pixmap, 10, 10));
    return;
  }
  if (tool_ == CanvasTool::Brush || tool_ == CanvasTool::Clone || tool_ == CanvasTool::Smudge ||
      tool_ == CanvasTool::Eraser) {
    if (brush_size_ == 1) {
      const auto pixel_extent = std::max(3, static_cast<int>(std::round(zoom_)));
      const auto extent = std::clamp(pixel_extent + 7, 17, 160);
      QPixmap pixmap(extent, extent);
      pixmap.fill(Qt::transparent);
      QPainter painter(&pixmap);
      painter.setRenderHint(QPainter::Antialiasing, false);
      const QPoint center(extent / 2, extent / 2);
      const QRect pixel_rect(center.x() - pixel_extent / 2, center.y() - pixel_extent / 2,
                             pixel_extent, pixel_extent);
      painter.setBrush(Qt::NoBrush);
      painter.setPen(QPen(tool_ == CanvasTool::Eraser ? QColor(255, 255, 255) : QColor(25, 25, 25), 1));
      painter.drawRect(pixel_rect);
      painter.setPen(QPen(tool_ == CanvasTool::Eraser ? QColor(25, 25, 25) : QColor(255, 255, 255), 1));
      painter.drawRect(pixel_rect.adjusted(1, 1, -1, -1));
      painter.drawLine(center + QPoint(-3, 0), center + QPoint(3, 0));
      painter.drawLine(center + QPoint(0, -3), center + QPoint(0, 3));
      painter.end();
      setCursor(QCursor(pixmap, center.x(), center.y()));
      return;
    }
    const auto diameter = std::max(3, static_cast<int>(std::round(static_cast<double>(brush_size_) * zoom_)));
    const auto extent = std::clamp(diameter + 5, 17, 160);
    QPixmap pixmap(extent, extent);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    const QPoint center(extent / 2, extent / 2);
    const auto radius = std::max(2, std::min(diameter, extent - 5) / 2);
    painter.setPen(QPen(tool_ == CanvasTool::Eraser ? QColor(255, 255, 255) : QColor(25, 25, 25), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(center, radius, radius);
    painter.setPen(QPen(tool_ == CanvasTool::Eraser ? QColor(25, 25, 25) : QColor(255, 255, 255), 1));
    painter.drawEllipse(center, std::max(1, radius - 1), std::max(1, radius - 1));
    if (brush_softness_ > 0 && tool_ != CanvasTool::Eraser) {
      const auto edge_width = std::max(1, static_cast<int>(std::round(static_cast<double>(radius) *
                                                                      static_cast<double>(brush_softness_) / 100.0)));
      const auto inner_radius = std::max(1, radius - edge_width);
      QPen softness_pen(QColor(105, 150, 210, 175), 1, Qt::DashLine);
      painter.setPen(softness_pen);
      painter.drawEllipse(center, inner_radius, inner_radius);
    }
    painter.drawLine(center + QPoint(-3, 0), center + QPoint(3, 0));
    painter.drawLine(center + QPoint(0, -3), center + QPoint(0, 3));
    painter.end();
    setCursor(QCursor(pixmap, center.x(), center.y()));
    return;
  }
  setCursor(Qt::CrossCursor);
}

QPoint CanvasWidget::document_position(const QPoint& widget_position) const {
  const auto coordinate_from_widget = [this](int widget_coordinate, double pan, int limit) {
    auto coordinate = static_cast<int>(std::floor((static_cast<double>(widget_coordinate) - pan) / zoom_));
    if (document_ == nullptr || !uses_deep_zoom_pixel_renderer(zoom_)) {
      return coordinate;
    }
    const auto edge = [pan, this](int document_coordinate) {
      return static_cast<int>(std::round(pan + static_cast<double>(document_coordinate) * zoom_));
    };
    while (coordinate > 0 && edge(coordinate) > widget_coordinate) {
      --coordinate;
    }
    while (coordinate < limit && edge(coordinate + 1) <= widget_coordinate) {
      ++coordinate;
    }
    return coordinate;
  };
  const auto x = coordinate_from_widget(widget_position.x(), pan_.x(), document_ != nullptr ? document_->width() : 0);
  const auto y = coordinate_from_widget(widget_position.y(), pan_.y(), document_ != nullptr ? document_->height() : 0);
  return QPoint(x, y);
}

QPointF CanvasWidget::document_position_f(QPointF widget_position) const {
  return QPointF((widget_position.x() - pan_.x()) / zoom_, (widget_position.y() - pan_.y()) / zoom_);
}

QPoint CanvasWidget::widget_position(const QPoint& document_position) const {
  return QPoint(static_cast<int>(std::round(pan_.x() + static_cast<double>(document_position.x()) * zoom_)),
                static_cast<int>(std::round(pan_.y() + static_cast<double>(document_position.y()) * zoom_)));
}

QPointF CanvasWidget::widget_position_f(QPointF document_position) const {
  return QPointF(pan_.x() + document_position.x() * zoom_, pan_.y() + document_position.y() * zoom_);
}

QPoint CanvasWidget::snapped_document_point(QPoint point) const {
  if (document_ == nullptr || !snap_enabled_) {
    return point;
  }

  const auto tolerance = kSnapToleranceScreenPixels / std::max(zoom_, 0.0001);
  auto snapped_x = static_cast<double>(point.x());
  auto snapped_y = static_cast<double>(point.y());
  double best_x = tolerance + 0.0001;
  double best_y = tolerance + 0.0001;

  const auto consider_x = [&](double candidate) {
    const auto distance = std::abs(candidate - static_cast<double>(point.x()));
    if (distance <= best_x) {
      best_x = distance;
      snapped_x = candidate;
    }
  };
  const auto consider_y = [&](double candidate) {
    const auto distance = std::abs(candidate - static_cast<double>(point.y()));
    if (distance <= best_y) {
      best_y = distance;
      snapped_y = candidate;
    }
  };

  if (snap_to_guides_) {
    for (const auto& guide : document_->guides()) {
      if (guide.orientation == GuideOrientation::Vertical) {
        consider_x(guide_position_pixels(guide));
      } else {
        consider_y(guide_position_pixels(guide));
      }
    }
  }

  if (snap_to_grid_ && grid_visible_) {
    const auto step_x = grid_cycle_pixels(document_->grid_settings().horizontal_cycle_32) /
                        static_cast<double>(std::max(1, grid_subdivisions_));
    const auto step_y = grid_cycle_pixels(document_->grid_settings().vertical_cycle_32) /
                        static_cast<double>(std::max(1, grid_subdivisions_));
    if (step_x > 0.0) {
      consider_x(std::round(static_cast<double>(point.x()) / step_x) * step_x);
    }
    if (step_y > 0.0) {
      consider_y(std::round(static_cast<double>(point.y()) / step_y) * step_y);
    }
  }

  if (snap_to_document_) {
    consider_x(0.0);
    consider_x(static_cast<double>(document_->width()) * 0.5);
    consider_x(static_cast<double>(document_->width()));
    consider_y(0.0);
    consider_y(static_cast<double>(document_->height()) * 0.5);
    consider_y(static_cast<double>(document_->height()));
  }

  std::vector<double> x_candidates;
  std::vector<double> y_candidates;
  if (snap_to_selection_ && !selection_.isEmpty()) {
    append_rect_snap_candidates(selection_.boundingRect(), x_candidates, y_candidates);
  }
  if (snap_to_layers_) {
    append_layer_snap_candidates(document_->layers(), x_candidates, y_candidates);
  }
  for (const auto candidate : x_candidates) {
    consider_x(candidate);
  }
  for (const auto candidate : y_candidates) {
    consider_y(candidate);
  }

  return QPoint(static_cast<int>(std::lround(snapped_x)), static_cast<int>(std::lround(snapped_y)));
}

QPointF CanvasWidget::snapped_document_point_f(QPointF point) const {
  const auto snapped = snapped_document_point(QPoint(static_cast<int>(std::lround(point.x())),
                                                    static_cast<int>(std::lround(point.y()))));
  return QPointF(snapped);
}

void CanvasWidget::append_snap_target_candidates(std::vector<double>& x_candidates,
                                                 std::vector<double>& y_candidates) const {
  if (document_ == nullptr) {
    return;
  }

  if (snap_to_guides_) {
    for (const auto& guide : document_->guides()) {
      if (guide.orientation == GuideOrientation::Vertical) {
        x_candidates.push_back(guide_position_pixels(guide));
      } else {
        y_candidates.push_back(guide_position_pixels(guide));
      }
    }
  }
  if (snap_to_document_) {
    x_candidates.push_back(0.0);
    x_candidates.push_back(static_cast<double>(document_->width()) * 0.5);
    x_candidates.push_back(static_cast<double>(document_->width()));
    y_candidates.push_back(0.0);
    y_candidates.push_back(static_cast<double>(document_->height()) * 0.5);
    y_candidates.push_back(static_cast<double>(document_->height()));
  }
  if (snap_to_selection_ && !selection_.isEmpty()) {
    append_rect_snap_candidates(selection_.boundingRect(), x_candidates, y_candidates);
  }
  if (snap_to_layers_) {
    append_layer_snap_candidates(document_->layers(), x_candidates, y_candidates);
  }
}

QPoint CanvasWidget::snapped_rect_delta(QRect source_rect, QPoint raw_delta) const {
  if (document_ == nullptr || !snap_enabled_ || source_rect.isEmpty()) {
    return raw_delta;
  }

  const auto tolerance = kSnapToleranceScreenPixels / std::max(zoom_, 0.0001);
  auto adjusted_x = static_cast<double>(raw_delta.x());
  auto adjusted_y = static_cast<double>(raw_delta.y());
  double best_x = tolerance + 0.0001;
  double best_y = tolerance + 0.0001;

  std::vector<double> target_x;
  std::vector<double> target_y;
  append_snap_target_candidates(target_x, target_y);

  const auto consider_x = [&](double source, double target) {
    const auto correction = target - (source + static_cast<double>(raw_delta.x()));
    const auto distance = std::abs(correction);
    if (distance <= best_x) {
      best_x = distance;
      adjusted_x = static_cast<double>(raw_delta.x()) + correction;
    }
  };
  const auto consider_y = [&](double source, double target) {
    const auto correction = target - (source + static_cast<double>(raw_delta.y()));
    const auto distance = std::abs(correction);
    if (distance <= best_y) {
      best_y = distance;
      adjusted_y = static_cast<double>(raw_delta.y()) + correction;
    }
  };
  const auto consider_grid_x = [&](double source) {
    if (!snap_to_grid_ || !grid_visible_) {
      return;
    }
    const auto step = grid_cycle_pixels(document_->grid_settings().horizontal_cycle_32) /
                      static_cast<double>(std::max(1, grid_subdivisions_));
    if (step > 0.0) {
      consider_x(source, std::round((source + static_cast<double>(raw_delta.x())) / step) * step);
    }
  };
  const auto consider_grid_y = [&](double source) {
    if (!snap_to_grid_ || !grid_visible_) {
      return;
    }
    const auto step = grid_cycle_pixels(document_->grid_settings().vertical_cycle_32) /
                      static_cast<double>(std::max(1, grid_subdivisions_));
    if (step > 0.0) {
      consider_y(source, std::round((source + static_cast<double>(raw_delta.y())) / step) * step);
    }
  };

  const auto left = static_cast<double>(source_rect.left());
  const auto right = static_cast<double>(source_rect.right() + 1);
  const auto top = static_cast<double>(source_rect.top());
  const auto bottom = static_cast<double>(source_rect.bottom() + 1);
  const std::array<double, 3> source_x{left, (left + right) * 0.5, right};
  const std::array<double, 3> source_y{top, (top + bottom) * 0.5, bottom};
  for (const auto source : source_x) {
    for (const auto target : target_x) {
      consider_x(source, target);
    }
    consider_grid_x(source);
  }
  for (const auto source : source_y) {
    for (const auto target : target_y) {
      consider_y(source, target);
    }
    consider_grid_y(source);
  }

  return QPoint(static_cast<int>(std::lround(adjusted_x)), static_cast<int>(std::lround(adjusted_y)));
}

QPoint CanvasWidget::snapped_marquee_current_point(QPoint anchor, QPoint current) const {
  if (document_ == nullptr || !snap_enabled_) {
    return current;
  }

  const auto rect = marquee_selection_rect(anchor, current);
  if (rect.isEmpty()) {
    return current;
  }

  const auto tolerance = kSnapToleranceScreenPixels / std::max(zoom_, 0.0001);
  auto adjusted_x = static_cast<double>(current.x());
  auto adjusted_y = static_cast<double>(current.y());
  double best_x = tolerance + 0.0001;
  double best_y = tolerance + 0.0001;

  std::vector<double> target_x;
  std::vector<double> target_y;
  append_snap_target_candidates(target_x, target_y);

  const auto active_x_edge = current.x() < anchor.x() ? static_cast<double>(rect.left())
                                                      : static_cast<double>(rect.right() + 1);
  const auto active_y_edge = current.y() < anchor.y() ? static_cast<double>(rect.top())
                                                      : static_cast<double>(rect.bottom() + 1);

  const auto consider_x = [&](double target) {
    const auto correction = target - active_x_edge;
    const auto distance = std::abs(correction);
    if (distance <= best_x) {
      best_x = distance;
      adjusted_x = static_cast<double>(current.x()) + correction;
    }
  };
  const auto consider_y = [&](double target) {
    const auto correction = target - active_y_edge;
    const auto distance = std::abs(correction);
    if (distance <= best_y) {
      best_y = distance;
      adjusted_y = static_cast<double>(current.y()) + correction;
    }
  };

  for (const auto target : target_x) {
    consider_x(target);
  }
  for (const auto target : target_y) {
    consider_y(target);
  }
  if (snap_to_grid_ && grid_visible_) {
    const auto step_x = grid_cycle_pixels(document_->grid_settings().horizontal_cycle_32) /
                        static_cast<double>(std::max(1, grid_subdivisions_));
    const auto step_y = grid_cycle_pixels(document_->grid_settings().vertical_cycle_32) /
                        static_cast<double>(std::max(1, grid_subdivisions_));
    if (step_x > 0.0) {
      consider_x(std::round(active_x_edge / step_x) * step_x);
    }
    if (step_y > 0.0) {
      consider_y(std::round(active_y_edge / step_y) * step_y);
    }
  }

  return QPoint(static_cast<int>(std::lround(adjusted_x)), static_cast<int>(std::lround(adjusted_y)));
}

QPoint CanvasWidget::snapped_move_delta(QPoint raw_delta) const {
  if (document_ == nullptr || !snap_enabled_ || moving_layers_.empty()) {
    return raw_delta;
  }

  const auto tolerance = kSnapToleranceScreenPixels / std::max(zoom_, 0.0001);
  auto adjusted_x = static_cast<double>(raw_delta.x());
  auto adjusted_y = static_cast<double>(raw_delta.y());
  double best_x = tolerance + 0.0001;
  double best_y = tolerance + 0.0001;

  std::vector<double> target_x;
  std::vector<double> target_y;
  if (snap_to_guides_) {
    for (const auto& guide : document_->guides()) {
      if (guide.orientation == GuideOrientation::Vertical) {
        target_x.push_back(guide_position_pixels(guide));
      } else {
        target_y.push_back(guide_position_pixels(guide));
      }
    }
  }
  if (snap_to_document_) {
    target_x.push_back(0.0);
    target_x.push_back(static_cast<double>(document_->width()) * 0.5);
    target_x.push_back(static_cast<double>(document_->width()));
    target_y.push_back(0.0);
    target_y.push_back(static_cast<double>(document_->height()) * 0.5);
    target_y.push_back(static_cast<double>(document_->height()));
  }
  if (snap_to_selection_ && !selection_.isEmpty()) {
    append_rect_snap_candidates(selection_.boundingRect(), target_x, target_y);
  }
  if (snap_to_layers_) {
    append_layer_snap_candidates(document_->layers(), target_x, target_y);
  }

  auto consider_x = [&](double source, double target) {
    const auto correction = target - (source + static_cast<double>(raw_delta.x()));
    const auto distance = std::abs(correction);
    if (distance <= best_x) {
      best_x = distance;
      adjusted_x = static_cast<double>(raw_delta.x()) + correction;
    }
  };
  auto consider_y = [&](double source, double target) {
    const auto correction = target - (source + static_cast<double>(raw_delta.y()));
    const auto distance = std::abs(correction);
    if (distance <= best_y) {
      best_y = distance;
      adjusted_y = static_cast<double>(raw_delta.y()) + correction;
    }
  };
  const auto consider_grid_x = [&](double source) {
    if (!snap_to_grid_ || !grid_visible_) {
      return;
    }
    const auto step = grid_cycle_pixels(document_->grid_settings().horizontal_cycle_32) /
                      static_cast<double>(std::max(1, grid_subdivisions_));
    if (step <= 0.0) {
      return;
    }
    consider_x(source, std::round((source + static_cast<double>(raw_delta.x())) / step) * step);
  };
  const auto consider_grid_y = [&](double source) {
    if (!snap_to_grid_ || !grid_visible_) {
      return;
    }
    const auto step = grid_cycle_pixels(document_->grid_settings().vertical_cycle_32) /
                      static_cast<double>(std::max(1, grid_subdivisions_));
    if (step <= 0.0) {
      return;
    }
    consider_y(source, std::round((source + static_cast<double>(raw_delta.y())) / step) * step);
  };

  for (const auto& moving_layer : moving_layers_) {
    const auto rect = moving_layer_outline_rect(moving_layer, QPoint());
    if (rect.isEmpty()) {
      continue;
    }
    const std::array<double, 3> source_x{static_cast<double>(rect.left()),
                                         static_cast<double>(rect.left() + rect.width() / 2.0),
                                         static_cast<double>(rect.right() + 1)};
    const std::array<double, 3> source_y{static_cast<double>(rect.top()),
                                         static_cast<double>(rect.top() + rect.height() / 2.0),
                                         static_cast<double>(rect.bottom() + 1)};
    for (const auto source : source_x) {
      for (const auto target : target_x) {
        consider_x(source, target);
      }
      consider_grid_x(source);
    }
    for (const auto source : source_y) {
      for (const auto target : target_y) {
        consider_y(source, target);
      }
      consider_grid_y(source);
    }
  }

  return QPoint(static_cast<int>(std::lround(adjusted_x)), static_cast<int>(std::lround(adjusted_y)));
}

int CanvasWidget::guide_at_widget_position(QPoint widget_position) const {
  if (document_ == nullptr || !guides_visible_) {
    return -1;
  }
  const auto document_point = document_position_f(QPointF(widget_position));
  if (document_point.x() < 0.0 || document_point.y() < 0.0 ||
      document_point.x() > static_cast<double>(document_->width()) ||
      document_point.y() > static_cast<double>(document_->height())) {
    return -1;
  }

  constexpr double kGuideHitPixels = 4.0;
  const auto tolerance = kGuideHitPixels / std::max(zoom_, 0.0001);
  for (int index = static_cast<int>(document_->guides().size()) - 1; index >= 0; --index) {
    const auto& guide = document_->guides()[static_cast<std::size_t>(index)];
    const auto pixels = guide_position_pixels(guide);
    const auto distance = guide.orientation == GuideOrientation::Vertical
                              ? std::abs(document_point.x() - pixels)
                              : std::abs(document_point.y() - pixels);
    if (distance <= tolerance) {
      return index;
    }
  }
  return -1;
}

GuideOrientation CanvasWidget::guide_orientation_from_ruler(QPoint widget_position) const noexcept {
  if (widget_position.y() < kTopRulerHeight && widget_position.x() >= kLeftRulerWidth) {
    return GuideOrientation::Horizontal;
  }
  return GuideOrientation::Vertical;
}

bool CanvasWidget::widget_position_in_ruler(QPoint widget_position) const noexcept {
  return rulers_visible_ &&
         ((widget_position.y() >= 0 && widget_position.y() < kTopRulerHeight &&
           widget_position.x() >= kLeftRulerWidth) ||
          (widget_position.x() >= 0 && widget_position.x() < kLeftRulerWidth &&
           widget_position.y() >= kTopRulerHeight));
}

void CanvasWidget::clear_guide_selection() noexcept {
  if (selected_guide_index_ < 0) {
    return;
  }
  selected_guide_index_ = -1;
  update();
}

void CanvasWidget::begin_guide_drag(int guide_index, QPoint widget_position) {
  if (document_ == nullptr || guide_index < 0 || guide_index >= static_cast<int>(document_->guides().size())) {
    return;
  }
  selected_guide_index_ = guide_index;
  if (guides_locked_) {
    update();
    return;
  }
  const auto& guide = document_->guides()[static_cast<std::size_t>(guide_index)];
  dragging_guide_ = true;
  creating_guide_ = false;
  guide_drag_remove_ = false;
  guide_drag_orientation_ = guide.orientation;
  guide_drag_position_32_ = guide.position_32;
  guide_drag_original_orientation_ = guide.orientation;
  guide_drag_original_position_32_ = guide.position_32;
  update_guide_drag(widget_position, Qt::NoModifier);
}

void CanvasWidget::begin_new_guide_drag(QPoint widget_position) {
  if (document_ == nullptr) {
    return;
  }
  dragging_guide_ = true;
  creating_guide_ = true;
  guide_drag_remove_ = false;
  selected_guide_index_ = -1;
  guide_drag_orientation_ = guide_orientation_from_ruler(widget_position);
  guide_drag_original_orientation_ = guide_drag_orientation_;
  guide_drag_original_position_32_ = 0;
  guide_drag_position_32_ = 0;
  update_guide_drag(widget_position, Qt::NoModifier);
}

void CanvasWidget::update_guide_drag(QPoint widget_position, Qt::KeyboardModifiers modifiers) {
  if (!dragging_guide_ || document_ == nullptr) {
    return;
  }

  auto orientation = guide_drag_original_orientation_;
  if ((modifiers & Qt::AltModifier) != 0) {
    orientation = orientation == GuideOrientation::Vertical ? GuideOrientation::Horizontal : GuideOrientation::Vertical;
  }
  guide_drag_orientation_ = orientation;

  const auto document_point = document_position_f(QPointF(widget_position));
  const auto axis_position = orientation == GuideOrientation::Vertical ? document_point.x() : document_point.y();
  const auto limit = static_cast<double>(orientation == GuideOrientation::Vertical ? document_->width() : document_->height());
  auto snapped_position = axis_position;

  if ((modifiers & Qt::ShiftModifier) != 0) {
    const auto tick = ruler_minor_tick_interval(zoom_);
    snapped_position = std::round(snapped_position / tick) * tick;
  } else if (snap_enabled_) {
    const auto tolerance = kSnapToleranceScreenPixels / std::max(zoom_, 0.0001);
    double best = tolerance + 0.0001;
    const auto consider = [&](double candidate) {
      const auto distance = std::abs(candidate - axis_position);
      if (distance <= best) {
        best = distance;
        snapped_position = candidate;
      }
    };
    if (snap_to_grid_ && grid_visible_) {
      const auto step =
          (orientation == GuideOrientation::Vertical
               ? grid_cycle_pixels(document_->grid_settings().horizontal_cycle_32)
               : grid_cycle_pixels(document_->grid_settings().vertical_cycle_32)) /
          static_cast<double>(std::max(1, grid_subdivisions_));
      if (step > 0.0) {
        consider(std::round(axis_position / step) * step);
      }
    }
    if (snap_to_document_) {
      consider(0.0);
      consider(limit * 0.5);
      consider(limit);
    }
    std::vector<double> x_candidates;
    std::vector<double> y_candidates;
    if (snap_to_selection_ && !selection_.isEmpty()) {
      append_rect_snap_candidates(selection_.boundingRect(), x_candidates, y_candidates);
    }
    if (snap_to_layers_) {
      append_layer_snap_candidates(document_->layers(), x_candidates, y_candidates);
    }
    const auto& candidates = orientation == GuideOrientation::Vertical ? x_candidates : y_candidates;
    for (const auto candidate : candidates) {
      consider(candidate);
    }
  }

  guide_drag_remove_ = snapped_position < 0.0 || snapped_position > limit;
  snapped_position = std::clamp(snapped_position, 0.0, limit);
  guide_drag_position_32_ = guide_position_32(snapped_position);
  update();
}

void CanvasWidget::finish_guide_drag(QPoint widget_position, Qt::KeyboardModifiers modifiers) {
  if (!dragging_guide_ || document_ == nullptr) {
    return;
  }
  update_guide_drag(widget_position, modifiers);

  if (creating_guide_) {
    if (!guide_drag_remove_) {
      if (before_edit_callback_) {
        before_edit_callback_(tr("New Guide"));
      }
      document_->guides().push_back(DocumentGuide{guide_drag_orientation_, guide_drag_position_32_});
      selected_guide_index_ = static_cast<int>(document_->guides().size()) - 1;
      dragging_guide_ = false;
      creating_guide_ = false;
      document_changed();
      return;
    }
  } else if (selected_guide_index_ >= 0 && selected_guide_index_ < static_cast<int>(document_->guides().size())) {
    const auto changed = guide_drag_remove_ || guide_drag_orientation_ != guide_drag_original_orientation_ ||
                         guide_drag_position_32_ != guide_drag_original_position_32_;
    if (changed && before_edit_callback_) {
      before_edit_callback_(guide_drag_remove_ ? tr("Clear Selected Guide") : tr("Move Guide"));
    }
    if (changed) {
      if (guide_drag_remove_) {
        document_->guides().erase(document_->guides().begin() + selected_guide_index_);
        selected_guide_index_ = -1;
      } else {
        auto& guide = document_->guides()[static_cast<std::size_t>(selected_guide_index_)];
        guide.orientation = guide_drag_orientation_;
        guide.position_32 = guide_drag_position_32_;
      }
      dragging_guide_ = false;
      creating_guide_ = false;
      document_changed();
      return;
    }
  }

  dragging_guide_ = false;
  creating_guide_ = false;
  guide_drag_remove_ = false;
  update();
}

void CanvasWidget::cancel_guide_drag() {
  if (!dragging_guide_) {
    return;
  }
  dragging_guide_ = false;
  creating_guide_ = false;
  guide_drag_remove_ = false;
  update();
}

bool CanvasWidget::document_contains(QPoint point) const noexcept {
  return document_ != nullptr && point.x() >= 0 && point.y() >= 0 && point.x() < document_->width() &&
         point.y() < document_->height();
}

bool CanvasWidget::selection_allows(QPoint point) const noexcept {
  return selection_contains(point);
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
  return layer_edit_target_ == LayerEditTarget::Mask && active_layer_mask() != nullptr;
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

  return topmost_pixel_layer_at_recursive(document_->layers(), document_point, require_visible_pixel, skip_locked);
}

Layer* CanvasWidget::topmost_move_layer_at(QPoint document_point, bool skip_locked) const noexcept {
  if (document_ == nullptr) {
    return nullptr;
  }

  return topmost_move_layer_at_recursive(document_->layers(), document_point, skip_locked);
}

Layer* CanvasWidget::topmost_text_layer_at(QPoint document_point) const noexcept {
  if (document_ == nullptr) {
    return nullptr;
  }

  return topmost_text_layer_at_recursive(document_->layers(), document_point);
}

void CanvasWidget::activate_layer(Layer& layer) {
  if (document_ == nullptr) {
    return;
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
    info.active_rect = normalized_rect(shape_start_, snapped_document_point(document_point));
    info.active_rect_label = tr("Shape");
  } else if (document_ != nullptr && dragging_text_rect_) {
    info.active_rect = normalized_rect(text_rect_start_, snapped_document_point(document_point));
    info.active_rect_label = tr("Text");
  } else if (document_ != nullptr && zooming_) {
    info.active_rect = normalized_rect(zoom_start_, document_point);
    info.active_rect_label = tr("Zoom");
  }
  info_callback_(std::move(info));
}

CanvasWidget::PenInputSample CanvasWidget::pen_input_sample_from_tablet_event(const QTabletEvent& event) const {
  PenInputSample sample;
  sample.widget_position = event.position();
  sample.document_position = document_position_f(event.position());
  sample.button = event.button();
  sample.buttons = event.buttons();
  sample.modifiers = event.modifiers();

  const auto* device = event.pointingDevice();
  const auto capabilities = device != nullptr ? device->capabilities() : QInputDevice::Capabilities{};
  sample.device_id = device != nullptr ? device->uniqueId().numericId() : qint64{-1};
  if (device != nullptr) {
    switch (device->pointerType()) {
      case QPointingDevice::PointerType::Pen:
        sample.pointer_type = PenInputSample::PointerType::Pen;
        break;
      case QPointingDevice::PointerType::Eraser:
        sample.pointer_type = PenInputSample::PointerType::Eraser;
        break;
      case QPointingDevice::PointerType::Cursor:
        sample.pointer_type = PenInputSample::PointerType::Cursor;
        break;
      default:
        sample.pointer_type = PenInputSample::PointerType::Unknown;
        break;
    }
  }

  sample.pressure_available = capabilities.testFlag(QInputDevice::Capability::Pressure);
  sample.pressure = sample.pressure_available && std::isfinite(event.pressure())
                        ? std::clamp(static_cast<float>(event.pressure()), 0.0F, 1.0F)
                        : 1.0F;
  sample.x_tilt = std::isfinite(event.xTilt()) ? static_cast<float>(event.xTilt()) : 0.0F;
  sample.y_tilt = std::isfinite(event.yTilt()) ? static_cast<float>(event.yTilt()) : 0.0F;
  sample.tilt_available = capabilities.testFlag(QInputDevice::Capability::XTilt) ||
                          capabilities.testFlag(QInputDevice::Capability::YTilt);
  sample.tangential_pressure_available =
      capabilities.testFlag(QInputDevice::Capability::TangentialPressure);
  sample.tangential_pressure =
      sample.tangential_pressure_available && std::isfinite(event.tangentialPressure())
          ? std::clamp(static_cast<float>(event.tangentialPressure()), -1.0F, 1.0F)
          : 0.0F;
  sample.rotation_available = capabilities.testFlag(QInputDevice::Capability::Rotation);
  sample.rotation_degrees =
      sample.rotation_available && std::isfinite(event.rotation()) ? static_cast<double>(event.rotation()) : 0.0;
  sample.z_available = capabilities.testFlag(QInputDevice::Capability::ZPosition);
  sample.z = sample.z_available && std::isfinite(event.z()) ? static_cast<float>(event.z()) : 0.0F;
  return sample;
}

PenButtonAction CanvasWidget::pen_action_for_button(Qt::MouseButton button) const noexcept {
  switch (button) {
    case Qt::RightButton:
      return pen_input_settings_.primary_button_action;
    case Qt::MiddleButton:
      return pen_input_settings_.secondary_button_action;
    default:
      return PenButtonAction::None;
  }
}

bool CanvasWidget::tablet_event_should_pan(const PenInputSample& sample, QEvent::Type event_type) const noexcept {
  if (event_type == QEvent::TabletRelease && panning_) {
    return true;
  }
  if (pen_action_for_button(sample.button) == PenButtonAction::PanCanvas) {
    return true;
  }
  for (const auto button : {Qt::RightButton, Qt::MiddleButton}) {
    if ((sample.buttons & button) != 0 && pen_action_for_button(button) == PenButtonAction::PanCanvas) {
      return true;
    }
  }
  return false;
}

bool CanvasWidget::tablet_event_should_zoom(const PenInputSample& sample, QEvent::Type event_type) const noexcept {
  if (event_type == QEvent::TabletRelease && pen_zoom_dragging_) {
    return true;
  }
  if (pen_action_for_button(sample.button) == PenButtonAction::ZoomCanvas) {
    return true;
  }
  for (const auto button : {Qt::RightButton, Qt::MiddleButton}) {
    if ((sample.buttons & button) != 0 && pen_action_for_button(button) == PenButtonAction::ZoomCanvas) {
      return true;
    }
  }
  return false;
}

void CanvasWidget::begin_zoom_drag(QPointF widget_position) {
  pen_zoom_dragging_ = true;
  zoom_drag_anchor_widget_ = widget_position;
  zoom_drag_last_pos_ = widget_position;
  setCursor(Qt::SizeVerCursor);
}

void CanvasWidget::update_zoom_drag(QPointF widget_position) {
  if (!pen_zoom_dragging_) {
    return;
  }
  // Dragging up zooms in, dragging down zooms out, anchored on the press point.
  const auto delta = zoom_drag_last_pos_.y() - widget_position.y();
  zoom_drag_last_pos_ = widget_position;
  if (std::abs(delta) < 0.001) {
    return;
  }
  const auto factor = std::pow(1.01, delta);
  zoom_at_widget_point(zoom_drag_anchor_widget_, factor);
}

void CanvasWidget::end_zoom_drag() {
  if (!pen_zoom_dragging_) {
    return;
  }
  pen_zoom_dragging_ = false;
  update_tool_cursor();
}

bool CanvasWidget::perform_pen_button_action(PenButtonAction action, const PenInputSample& sample) {
  switch (action) {
    case PenButtonAction::PickColor:
      pick_color(sample.document_position.toPoint());
      return true;
    case PenButtonAction::SetCloneSource:
      if (tool_ == CanvasTool::Clone) {
        set_clone_source(sample.document_position.toPoint());
      } else if (status_callback_) {
        status_callback_(tr("Select the Clone tool to set a clone source"));
      }
      return true;
    case PenButtonAction::SwapColors:
    case PenButtonAction::Undo:
    case PenButtonAction::Redo:
    case PenButtonAction::ToggleEraser:
    case PenButtonAction::IncreaseBrushSize:
    case PenButtonAction::DecreaseBrushSize:
      if (pen_button_action_callback_) {
        pen_button_action_callback_(action);
      }
      return true;
    case PenButtonAction::PanCanvas:
    case PenButtonAction::ZoomCanvas:
    case PenButtonAction::None:
      return false;
  }
  return false;
}

bool CanvasWidget::dispatch_tablet_as_mouse(QTabletEvent* event, const PenInputSample& sample) {
  if (event == nullptr) {
    return false;
  }

  QEvent::Type mouse_type = QEvent::None;
  switch (event->type()) {
    case QEvent::TabletPress:
      mouse_type = QEvent::MouseButtonPress;
      break;
    case QEvent::TabletMove:
      mouse_type = QEvent::MouseMove;
      break;
    case QEvent::TabletRelease:
      mouse_type = QEvent::MouseButtonRelease;
      break;
    default:
      return false;
  }

  const auto pan = tablet_event_should_pan(sample, event->type());

  // Zoom is a drag gesture like pan, but the canvas has no equivalent mouse
  // button, so it is handled directly rather than through a synthetic event.
  if (!pan && tablet_event_should_zoom(sample, event->type())) {
    switch (event->type()) {
      case QEvent::TabletPress:
        begin_zoom_drag(sample.widget_position);
        pen_button_suppressing_paint_ = true;
        break;
      case QEvent::TabletMove:
        update_zoom_drag(sample.widget_position);
        break;
      case QEvent::TabletRelease:
        end_zoom_drag();
        pen_button_suppressing_paint_ = false;
        break;
      default:
        break;
    }
    return true;
  }

  // Resolve pen barrel/side buttons to their configured actions. Pan is a drag
  // gesture handled below through the synthetic right-button path; every other
  // mapped action fires once on press and then suppresses painting until the
  // button is released so that moving the pen with the button held does not
  // leave a stray stroke.
  if (!pan) {
    const auto pressed_action = pen_action_for_button(sample.button);
    if (event->type() == QEvent::TabletPress) {
      if (sample.button != Qt::LeftButton && sample.button != Qt::NoButton) {
        pen_button_suppressing_paint_ = true;
        perform_pen_button_action(pressed_action, sample);
        return true;
      }
      // A pen-tip press with no barrel button held should always be able to
      // paint. Clear any stale suppression left over from a barrel release that
      // the tablet driver may have delivered as a plain mouse event.
      if ((sample.buttons & (Qt::RightButton | Qt::MiddleButton)) == 0) {
        pen_button_suppressing_paint_ = false;
      }
    }
    if (pen_button_suppressing_paint_) {
      if (event->type() == QEvent::TabletRelease &&
          (sample.buttons & (Qt::RightButton | Qt::MiddleButton)) == 0) {
        pen_button_suppressing_paint_ = false;
      }
      return true;
    }
  } else {
    pen_button_suppressing_paint_ = false;
  }

  Qt::MouseButton button = Qt::NoButton;
  Qt::MouseButtons buttons = Qt::NoButton;
  if (pan) {
    button = event->type() == QEvent::TabletMove ? Qt::NoButton : Qt::RightButton;
    buttons = event->type() == QEvent::TabletRelease ? Qt::NoButton : Qt::RightButton;
  } else {
    switch (event->type()) {
      case QEvent::TabletPress:
        button = Qt::LeftButton;
        buttons = Qt::LeftButton;
        break;
      case QEvent::TabletMove:
        button = Qt::NoButton;
        buttons = sample.buttons;
        if (painting_ || (sample.buttons & Qt::LeftButton) != 0 ||
            (sample.pressure_available && sample.pressure > 0.0F)) {
          buttons |= Qt::LeftButton;
        }
        break;
      case QEvent::TabletRelease:
        button = Qt::LeftButton;
        buttons = Qt::NoButton;
        break;
      default:
        break;
    }
  }

  QMouseEvent mouse_event(mouse_type, sample.widget_position,
                          QPointF(mapToGlobal(sample.widget_position.toPoint())), button, buttons,
                          sample.modifiers);
  handling_tablet_event_ = true;
  switch (mouse_type) {
    case QEvent::MouseButtonPress:
      mousePressEvent(&mouse_event);
      break;
    case QEvent::MouseMove:
      mouseMoveEvent(&mouse_event);
      break;
    case QEvent::MouseButtonRelease:
      mouseReleaseEvent(&mouse_event);
      break;
    default:
      handling_tablet_event_ = false;
      return false;
  }
  handling_tablet_event_ = false;
  return true;
}

bool CanvasWidget::begin_edit(QString label) {
  if (active_layer_locks_image_pixels()) {
    show_layer_pixels_locked_message();
    return false;
  }

  if (layer_edit_target_ == LayerEditTarget::Mask) {
    if (active_layer_mask() == nullptr) {
      if (status_callback_) {
        status_callback_(tr("Select a layer mask to edit"));
      }
      return false;
    }
    if (before_edit_callback_) {
      before_edit_callback_(label);
    }
    return true;
  }

  auto* layer = active_pixel_layer();
  if (layer == nullptr || layer->pixels().format().bit_depth != BitDepth::UInt8) {
    if (status_callback_) {
      status_callback_(tr("Select an editable 8-bit pixel layer first"));
    }
    return false;
  }
  if (layer_is_text(*layer)) {
    if (status_callback_) {
      status_callback_(tr("Select a normal pixel layer before painting on text"));
    }
    return false;
  }

  if (before_edit_callback_) {
    before_edit_callback_(label);
  }
  return true;
}

CanvasTool CanvasWidget::effective_tool_for_input() const noexcept {
  if (active_pen_input_sample_.has_value() && pen_input_settings_.enabled && pen_input_settings_.use_eraser_tip &&
      active_pen_input_sample_->pointer_type == PenInputSample::PointerType::Eraser) {
    switch (tool_) {
      case CanvasTool::Brush:
      case CanvasTool::Clone:
      case CanvasTool::Smudge:
        return CanvasTool::Eraser;
      default:
        break;
    }
  }
  return tool_;
}

void CanvasWidget::clear_brush_stroke_tracking() noexcept {
  brush_stroke_pixels_.clear();
  brush_stroke_alpha_caps_.clear();
}

void CanvasWidget::begin_axis_constrained_stroke(QPointF document_point) noexcept {
  stroke_constraint_start_ = document_point;
  stroke_constraint_axis_ = StrokeConstraintAxis::None;
}

void CanvasWidget::reset_axis_constrained_stroke() noexcept {
  stroke_constraint_start_ = {};
  stroke_constraint_axis_ = StrokeConstraintAxis::None;
}

QPointF CanvasWidget::axis_constrained_stroke_point(QPointF document_point,
                                                    Qt::KeyboardModifiers modifiers) noexcept {
  if ((modifiers & Qt::ShiftModifier) == 0) {
    stroke_constraint_axis_ = StrokeConstraintAxis::None;
    return document_point;
  }

  if (stroke_constraint_axis_ == StrokeConstraintAxis::None) {
    const auto dx = std::abs(document_point.x() - stroke_constraint_start_.x());
    const auto dy = std::abs(document_point.y() - stroke_constraint_start_.y());
    if (dx <= std::numeric_limits<double>::epsilon() && dy <= std::numeric_limits<double>::epsilon()) {
      return document_point;
    }
    stroke_constraint_axis_ = dx >= dy ? StrokeConstraintAxis::Horizontal : StrokeConstraintAxis::Vertical;
  }

  if (stroke_constraint_axis_ == StrokeConstraintAxis::Horizontal) {
    return QPointF(document_point.x(), stroke_constraint_start_.y());
  }
  return QPointF(stroke_constraint_start_.x(), document_point.y());
}

QPoint CanvasWidget::axis_constrained_stroke_point(QPoint document_point,
                                                   Qt::KeyboardModifiers modifiers) noexcept {
  const auto constrained = axis_constrained_stroke_point(QPointF(document_point), modifiers);
  return QPoint(static_cast<int>(std::lround(constrained.x())),
                static_cast<int>(std::lround(constrained.y())));
}

QPoint CanvasWidget::axis_constrained_move_delta(QPoint raw_delta,
                                                 Qt::KeyboardModifiers modifiers) noexcept {
  const auto constrained =
      axis_constrained_stroke_point(QPointF(move_start_ + raw_delta), modifiers) - QPointF(move_start_);
  return QPoint(static_cast<int>(std::lround(constrained.x())),
                static_cast<int>(std::lround(constrained.y())));
}

void CanvasWidget::begin_brush_smoothing(QPointF document_point) noexcept {
  brush_smoothing_active_ = true;
  brush_smoothing_last_input_position_ = document_point;
  brush_smoothing_last_rendered_position_ = document_point;
}

void CanvasWidget::reset_brush_smoothing() noexcept {
  brush_smoothing_active_ = false;
  brush_smoothing_last_input_position_ = {};
  brush_smoothing_last_rendered_position_ = {};
}

QRect CanvasWidget::advance_smoothed_brush_stroke(QPointF document_point, bool erase) {
  constexpr auto kMinimumMovement = 0.01;
  if (!brush_smoothing_active_) {
    begin_brush_smoothing(document_point);
    return {};
  }
  if (point_distance(brush_smoothing_last_input_position_, document_point) <= kMinimumMovement) {
    return {};
  }

  const auto end = midpoint(brush_smoothing_last_input_position_, document_point);
  const auto dirty = draw_smoothed_brush_curve(brush_smoothing_last_rendered_position_,
                                               brush_smoothing_last_input_position_, end, erase);
  brush_smoothing_last_input_position_ = document_point;
  brush_smoothing_last_rendered_position_ = end;
  return dirty;
}

QRect CanvasWidget::finish_smoothed_brush_stroke(QPointF document_point, bool erase) {
  constexpr auto kMinimumMovement = 0.01;
  if (!brush_smoothing_active_) {
    return {};
  }

  QRect dirty;
  if (point_distance(brush_smoothing_last_rendered_position_, brush_smoothing_last_input_position_) <=
      kMinimumMovement) {
    dirty = draw_brush_segment(brush_smoothing_last_rendered_position_, document_point, erase);
  } else {
    if (point_distance(brush_smoothing_last_input_position_, document_point) > kMinimumMovement) {
      dirty = united_dirty_rect(dirty, advance_smoothed_brush_stroke(document_point, erase));
    }
    if (point_distance(brush_smoothing_last_rendered_position_, document_point) > kMinimumMovement) {
      dirty = united_dirty_rect(
          dirty, draw_smoothed_brush_curve(brush_smoothing_last_rendered_position_,
                                           brush_smoothing_last_input_position_, document_point, erase));
    }
  }
  reset_brush_smoothing();
  return dirty;
}

QRect CanvasWidget::draw_smoothed_brush_curve(QPointF start, QPointF control, QPointF end, bool erase) {
  const auto curve_length = std::max(point_distance(start, end),
                                     point_distance(start, control) + point_distance(control, end));
  const auto step_length = std::max(1.0, static_cast<double>(std::max(1, effective_brush_input().size)) * 0.125);
  const auto steps = std::max(1, static_cast<int>(std::ceil(curve_length / step_length)));

  QRect dirty;
  auto previous = start;
  for (int step = 1; step <= steps; ++step) {
    const auto t = static_cast<double>(step) / static_cast<double>(steps);
    const auto current = quadratic_point(start, control, end, t);
    dirty = united_dirty_rect(dirty, draw_brush_segment(previous, current, erase));
    previous = current;
  }
  return dirty;
}

float CanvasWidget::capped_stroke_coverage(std::int32_t x, std::int32_t y, float coverage, float source_alpha) {
  source_alpha = std::clamp(source_alpha, 1.0F / 255.0F, 1.0F);
  const auto target_alpha = std::clamp(source_alpha * std::clamp(coverage, 0.0F, 1.0F), 0.0F, 1.0F);
  if (target_alpha <= 0.0F) {
    return 0.0F;
  }

  auto& previous_alpha = brush_stroke_alpha_caps_[stroke_pixel_key(x, y)];
  if (target_alpha <= previous_alpha + 0.0005F) {
    return 0.0F;
  }

  const auto incremental_alpha = (target_alpha - previous_alpha) / std::max(0.0005F, 1.0F - previous_alpha);
  previous_alpha = target_alpha;
  return std::clamp(incremental_alpha / source_alpha, 0.0F, 1.0F);
}

void CanvasWidget::install_brush_stroke_coverage_cap(EditOptions& options) {
  const auto source_alpha =
      std::clamp(static_cast<float>(std::clamp<int>(options.primary.a, 1, 255)) / 255.0F, 1.0F / 255.0F, 1.0F);
  options.stroke_coverage_gate = [this, source_alpha](std::int32_t x, std::int32_t y, float coverage) {
    return capped_stroke_coverage(x, y, coverage, source_alpha);
  };
}

CanvasWidget::EffectiveBrushInput CanvasWidget::effective_brush_input() const noexcept {
  EffectiveBrushInput brush{brush_size_, brush_opacity_, brush_softness_, 100, 0.0};
  if (!pen_input_settings_.enabled || !active_pen_input_sample_.has_value()) {
    return brush;
  }

  const auto& sample = *active_pen_input_sample_;
  const auto pressure = sample.pressure_available ? std::clamp(sample.pressure, 0.0F, 1.0F) : 1.0F;
  if (pen_input_settings_.pressure_size) {
    const auto minimum = static_cast<double>(std::clamp(pen_input_settings_.pressure_size_min_percent, 1, 100)) / 100.0;
    const auto scale = minimum + static_cast<double>(pressure) * (1.0 - minimum);
    brush.size = std::clamp(static_cast<int>(std::lround(static_cast<double>(brush_size_) * scale)), 1, 256);
  }
  if (pen_input_settings_.pressure_opacity) {
    const auto minimum =
        static_cast<double>(std::clamp(pen_input_settings_.pressure_opacity_min_percent, 1, 100)) / 100.0;
    const auto scale = minimum + static_cast<double>(pressure) * (1.0 - minimum);
    brush.opacity = std::clamp(static_cast<int>(std::lround(static_cast<double>(brush_opacity_) * scale)), 1, 100);
  }
  if (pen_input_settings_.tilt_shape && sample.tilt_available) {
    const auto tilt = std::clamp(std::hypot(static_cast<double>(sample.x_tilt), static_cast<double>(sample.y_tilt)) /
                                     90.0,
                                 0.0, 1.0);
    const auto minimum_roundness = std::clamp(pen_input_settings_.tilt_min_roundness_percent, 1, 100);
    brush.roundness = std::clamp(static_cast<int>(std::lround(100.0 - tilt * (100.0 - minimum_roundness))), 1, 100);
    if (sample.rotation_available) {
      brush.angle_degrees = sample.rotation_degrees;
    } else if (std::abs(sample.x_tilt) > std::numeric_limits<float>::epsilon() ||
               std::abs(sample.y_tilt) > std::numeric_limits<float>::epsilon()) {
      brush.angle_degrees = std::atan2(static_cast<double>(sample.y_tilt), static_cast<double>(sample.x_tilt)) *
                            180.0 / kPi;
    }
  }
  return brush;
}

EditOptions CanvasWidget::current_brush_edit_options(const EffectiveBrushInput& brush) const {
  return edit_options(primary_color_, secondary_color_, brush.size, brush.opacity, brush.softness, fill_shapes_,
                      active_layer_locks_transparent_pixels(), *this, brush.roundness, brush.angle_degrees);
}

QRect CanvasWidget::draw_brush_segment(QPointF from, QPointF to, bool erase) {
  if (editing_layer_mask()) {
    return draw_mask_brush_segment(from, to, erase);
  }
  if (document_ == nullptr || !document_->active_layer_id().has_value()) {
    return {};
  }

  const auto brush = effective_brush_input();
  auto options = current_brush_edit_options(brush);
  install_brush_stroke_coverage_cap(options);
  return to_qrect(
      patchy::paint_brush_segment(*document_, *document_->active_layer_id(), from.x(), from.y(), to.x(), to.y(),
                                  options, erase));
}

QRect CanvasWidget::draw_brush_segment(QPoint from, QPoint to, bool erase) {
  return draw_brush_segment(QPointF(from), QPointF(to), erase);
}

QRect CanvasWidget::draw_brush_at(QPoint point, bool erase) {
  if (editing_layer_mask()) {
    return draw_mask_brush_at(point, erase);
  }
  if (document_ == nullptr || !document_->active_layer_id().has_value()) {
    return {};
  }

  const auto brush = effective_brush_input();
  auto options = current_brush_edit_options(brush);
  install_brush_stroke_coverage_cap(options);
  return to_qrect(
      patchy::paint_brush(*document_, *document_->active_layer_id(), point.x(), point.y(), options, erase));
}

QRect CanvasWidget::draw_mask_brush_segment(QPointF from, QPointF to, bool erase) {
  auto* mask = active_layer_mask();
  if (document_ == nullptr || mask == nullptr) {
    return {};
  }

  const auto brush = effective_brush_input();
  const auto radius = std::max(1, brush.size) / 2;
  if (radius == 0) {
    const auto start = QPoint(static_cast<int>(std::floor(from.x())), static_cast<int>(std::floor(from.y())));
    const auto end = QPoint(static_cast<int>(std::floor(to.x())), static_cast<int>(std::floor(to.y())));
    auto stroke_rect = QRect(std::min(start.x(), end.x()),
                             std::min(start.y(), end.y()),
                             std::abs(end.x() - start.x()) + 1,
                             std::abs(end.y() - start.y()) + 1)
                           .intersected(QRect(0, 0, document_->width(), document_->height()));
    if (stroke_rect.isEmpty() ||
        !expand_mask_to_include_rect(*mask, stroke_rect, QSize(document_->width(), document_->height()))) {
      return {};
    }

    const auto bounds = QRect(mask->bounds.x, mask->bounds.y, mask->bounds.width, mask->bounds.height);
    const auto paint_value = erase ? std::uint8_t{0} : mask_value_from_color(primary_color_);
    const auto opacity = static_cast<float>(brush.opacity) / 100.0F;
    QRect dirty;
    visit_pixel_line(start, end, [&](QPoint document_point) {
      if (!QRect(0, 0, document_->width(), document_->height()).contains(document_point) ||
          !bounds.contains(document_point) || !selection_allows(document_point)) {
        return;
      }
      auto coverage = has_selection() ? static_cast<float>(selection_alpha_at(document_point)) / 255.0F : 1.0F;
      if (coverage <= 0.0F) {
        return;
      }
      coverage = capped_stroke_coverage(document_point.x(), document_point.y(), coverage, opacity);
      if (coverage <= 0.0F) {
        return;
      }
      coverage *= opacity;

      auto* value = mask->pixels.pixel(document_point.x() - bounds.x(), document_point.y() - bounds.y());
      *value = blend_mask_value(*value, paint_value, coverage);
      dirty = dirty.united(QRect(document_point, QSize(1, 1)));
    });
    return dirty;
  }

  const auto left = static_cast<int>(std::floor(std::min(from.x(), to.x()) - static_cast<double>(radius)));
  const auto top = static_cast<int>(std::floor(std::min(from.y(), to.y()) - static_cast<double>(radius)));
  const auto right = static_cast<int>(std::ceil(std::max(from.x(), to.x()) + static_cast<double>(radius))) + 1;
  const auto bottom = static_cast<int>(std::ceil(std::max(from.y(), to.y()) + static_cast<double>(radius))) + 1;
  auto stroke_rect = QRect(left, top, right - left, bottom - top)
                         .intersected(QRect(0, 0, document_->width(), document_->height()));
  if (stroke_rect.isEmpty() ||
      !expand_mask_to_include_rect(*mask, stroke_rect, QSize(document_->width(), document_->height()))) {
    return {};
  }

  const auto bounds = QRect(mask->bounds.x, mask->bounds.y, mask->bounds.width, mask->bounds.height);
  stroke_rect = stroke_rect.intersected(bounds);
  if (stroke_rect.isEmpty()) {
    return {};
  }

  const auto dx = to.x() - from.x();
  const auto dy = to.y() - from.y();
  const auto segment_length_squared = dx * dx + dy * dy;
  const auto paint_value = erase ? std::uint8_t{0} : mask_value_from_color(primary_color_);
  const auto opacity = static_cast<float>(brush.opacity) / 100.0F;

  QRect dirty;
  for (int y = stroke_rect.top(); y <= stroke_rect.bottom(); ++y) {
    for (int x = stroke_rect.left(); x <= stroke_rect.right(); ++x) {
      const QPoint document_point(x, y);
      if (!selection_allows(document_point)) {
        continue;
      }
      const auto along =
          segment_length_squared <= std::numeric_limits<double>::epsilon()
              ? 0.0
              : std::clamp(((static_cast<double>(x) - from.x()) * dx + (static_cast<double>(y) - from.y()) * dy) /
                                segment_length_squared,
                           0.0, 1.0);
      const auto closest_x = from.x() + dx * along;
      const auto closest_y = from.y() + dy * along;
      const auto distance_x = static_cast<double>(x) - closest_x;
      const auto distance_y = static_cast<double>(y) - closest_y;
      auto coverage = brush_shape_coverage(distance_x, distance_y, radius, brush.softness, brush.roundness,
                                           brush.angle_degrees);
      if (has_selection()) {
        coverage *= static_cast<float>(selection_alpha_at(document_point)) / 255.0F;
      }
      if (coverage <= 0.0F) {
        continue;
      }
      coverage = capped_stroke_coverage(x, y, coverage, opacity);
      if (coverage <= 0.0F) {
        continue;
      }
      coverage *= opacity;

      auto* value = mask->pixels.pixel(x - bounds.x(), y - bounds.y());
      *value = blend_mask_value(*value, paint_value, coverage);
      dirty = dirty.united(QRect(document_point, QSize(1, 1)));
    }
    tick_processing_operation();
  }
  return dirty;
}

QRect CanvasWidget::draw_mask_brush_segment(QPoint from, QPoint to, bool erase) {
  return draw_mask_brush_segment(QPointF(from), QPointF(to), erase);
}

QRect CanvasWidget::draw_mask_brush_at(QPoint point, bool erase) {
  return draw_mask_brush_segment(point, point, erase);
}

QRect CanvasWidget::smudge_brush_segment(QPoint from, QPoint to) {
  if (document_ == nullptr || !document_->active_layer_id().has_value()) {
    return {};
  }

  const auto brush = effective_brush_input();
  auto options = current_brush_edit_options(brush);
  return to_qrect(patchy::smudge_brush_segment(*document_, *document_->active_layer_id(), from.x(), from.y(),
                                                  to.x(), to.y(), options, smudge_state_));
}

void CanvasWidget::set_clone_source(QPoint point) {
  if (!document_contains(point)) {
    return;
  }
  clone_source_point_ = point;
  clone_source_set_ = true;
  clone_aligned_offset_set_ = false;
  update();
  if (status_callback_) {
    status_callback_(tr("Clone source set at %1, %2").arg(point.x()).arg(point.y()));
  }
}

QRect CanvasWidget::clone_brush_at(QPoint point) {
  return clone_brush_segment(point, point);
}

QRect CanvasWidget::clone_brush_segment(QPoint from, QPoint to) {
  auto* layer = active_pixel_layer();
  if (document_ == nullptr || layer == nullptr || clone_source_cache_.isNull() ||
      layer->pixels().format().bit_depth != BitDepth::UInt8 || layer->pixels().format().channels < 3) {
    return {};
  }

  const auto brush = effective_brush_input();
  const auto radius = std::max(1, brush.size) / 2;
  if (radius == 0) {
    const auto path_rect = QRect(std::min(from.x(), to.x()),
                                 std::min(from.y(), to.y()),
                                 std::abs(to.x() - from.x()) + 1,
                                 std::abs(to.y() - from.y()) + 1)
                               .intersected(QRect(0, 0, document_->width(), document_->height()));
    if (path_rect.isEmpty()) {
      return {};
    }

    const auto lock_transparent_pixels = active_layer_locks_transparent_pixels();
    if (!lock_transparent_pixels) {
      patchy::expand_layer_to_include_rect(*layer, to_core_rect(path_rect));
    }

    auto& pixels = layer->pixels();
    const auto bounds = layer->bounds();
    const auto channels = pixels.format().channels;
    const auto opacity = static_cast<float>(brush.opacity) / 100.0F;
    QRect dirty;
    visit_pixel_line(from, to, [&](QPoint document_point) {
      if (!QRect(0, 0, document_->width(), document_->height()).contains(document_point) ||
          !to_qrect(bounds).contains(document_point) || !selection_allows(document_point)) {
        return;
      }
      auto coverage = has_selection() ? static_cast<float>(selection_alpha_at(document_point)) / 255.0F : 1.0F;
      if (coverage <= 0.0F) {
        return;
      }
      const auto source_point = document_point + clone_source_offset_;
      if (source_point.x() < 0 || source_point.y() < 0 || source_point.x() >= clone_source_cache_.width() ||
          source_point.y() >= clone_source_cache_.height()) {
        return;
      }

      auto row = pixels.row(document_point.y() - bounds.y);
      auto* dst = row.data() + static_cast<std::size_t>(document_point.x() - bounds.x) * channels;
      if (lock_transparent_pixels && channels >= 4 && dst[3] == 0) {
        return;
      }
      coverage = capped_stroke_coverage(document_point.x(), document_point.y(), coverage, opacity);
      if (coverage <= 0.0F) {
        return;
      }

      const auto* src = clone_source_cache_.constScanLine(source_point.y()) +
                        static_cast<std::size_t>(source_point.x()) * 4U;
      const auto covered_opacity = opacity * coverage;
      if (channels >= 4 && !lock_transparent_pixels) {
        blend_straight_rgba(dst, src, covered_opacity);
      } else {
        const auto effective_opacity = covered_opacity * (static_cast<float>(src[3]) / 255.0F);
        if (effective_opacity <= 0.0F) {
          return;
        }
        dst[0] = clamp_byte(static_cast<float>(src[0]) * effective_opacity +
                            static_cast<float>(dst[0]) * (1.0F - effective_opacity));
        dst[1] = clamp_byte(static_cast<float>(src[1]) * effective_opacity +
                            static_cast<float>(dst[1]) * (1.0F - effective_opacity));
        dst[2] = clamp_byte(static_cast<float>(src[2]) * effective_opacity +
                            static_cast<float>(dst[2]) * (1.0F - effective_opacity));
      }
      dirty = dirty.united(QRect(document_point, QSize(1, 1)));
    });
    return dirty;
  }

  const auto left = std::min(from.x(), to.x()) - radius;
  const auto top = std::min(from.y(), to.y()) - radius;
  const auto right = std::max(from.x(), to.x()) + radius + 1;
  const auto bottom = std::max(from.y(), to.y()) + radius + 1;
  auto stroke_rect = QRect(left, top, right - left, bottom - top).intersected(
      QRect(0, 0, document_->width(), document_->height()));
  if (stroke_rect.isEmpty()) {
    return {};
  }

  const auto lock_transparent_pixels = active_layer_locks_transparent_pixels();
  if (!lock_transparent_pixels) {
    patchy::expand_layer_to_include_rect(*layer, to_core_rect(stroke_rect));
  }

  auto& pixels = layer->pixels();
  const auto bounds = layer->bounds();
  const auto channels = pixels.format().channels;
  stroke_rect = stroke_rect.intersected(to_qrect(bounds));
  if (stroke_rect.isEmpty()) {
    return {};
  }

  const auto dx = to.x() - from.x();
  const auto dy = to.y() - from.y();
  const auto segment_length_squared = static_cast<double>(dx) * static_cast<double>(dx) +
                                      static_cast<double>(dy) * static_cast<double>(dy);
  const auto opacity = static_cast<float>(brush.opacity) / 100.0F;

  QRect dirty;
  for (int y = stroke_rect.top(); y <= stroke_rect.bottom(); ++y) {
    auto row = pixels.row(y - bounds.y);
    for (int x = stroke_rect.left(); x <= stroke_rect.right(); ++x) {
      const auto along =
          segment_length_squared <= 0.0
              ? 0.0
              : std::clamp((static_cast<double>(x - from.x()) * static_cast<double>(dx) +
                            static_cast<double>(y - from.y()) * static_cast<double>(dy)) /
                               segment_length_squared,
                           0.0, 1.0);
      const auto closest_x = static_cast<double>(from.x()) + static_cast<double>(dx) * along;
      const auto closest_y = static_cast<double>(from.y()) + static_cast<double>(dy) * along;
      const auto distance_x = static_cast<double>(x) - closest_x;
      const auto distance_y = static_cast<double>(y) - closest_y;
      auto coverage = brush_shape_coverage(distance_x, distance_y, radius, brush.softness, brush.roundness,
                                           brush.angle_degrees);
      if (coverage <= 0.0F) {
        continue;
      }
      const QPoint document_point(x, y);
      if (!selection_allows(document_point)) {
        continue;
      }
      if (has_selection()) {
        coverage *= static_cast<float>(selection_alpha_at(document_point)) / 255.0F;
        if (coverage <= 0.0F) {
          continue;
        }
      }

      const auto source_point = document_point + clone_source_offset_;
      if (source_point.x() < 0 || source_point.y() < 0 || source_point.x() >= clone_source_cache_.width() ||
          source_point.y() >= clone_source_cache_.height()) {
        continue;
      }

      const auto local_x = x - bounds.x;
      auto* dst = row.data() + static_cast<std::size_t>(local_x) * channels;
      if (lock_transparent_pixels && channels >= 4 && dst[3] == 0) {
        continue;
      }
      coverage = capped_stroke_coverage(document_point.x(), document_point.y(), coverage, opacity);
      if (coverage <= 0.0F) {
        continue;
      }

      const auto* src = clone_source_cache_.constScanLine(source_point.y()) +
                        static_cast<std::size_t>(source_point.x()) * 4U;
      const auto covered_opacity = opacity * coverage;
      if (channels >= 4 && !lock_transparent_pixels) {
        blend_straight_rgba(dst, src, covered_opacity);
      } else {
        const auto effective_opacity = covered_opacity * (static_cast<float>(src[3]) / 255.0F);
        if (effective_opacity <= 0.0F) {
          continue;
        }
        dst[0] = clamp_byte(static_cast<float>(src[0]) * effective_opacity +
                            static_cast<float>(dst[0]) * (1.0F - effective_opacity));
        dst[1] = clamp_byte(static_cast<float>(src[1]) * effective_opacity +
                            static_cast<float>(dst[1]) * (1.0F - effective_opacity));
        dst[2] = clamp_byte(static_cast<float>(src[2]) * effective_opacity +
                            static_cast<float>(dst[2]) * (1.0F - effective_opacity));
      }
      dirty = dirty.united(QRect(document_point, QSize(1, 1)));
    }
    tick_processing_operation();
  }
  return dirty;
}

void CanvasWidget::draw_pixel(Layer& layer, QPoint document_point, QColor color, bool erase) {
  if (!document_contains(document_point) || !selection_allows(document_point)) {
    return;
  }

  auto& pixels = layer.pixels();
  if (pixels.format().bit_depth != BitDepth::UInt8 || pixels.format().channels < 3) {
    return;
  }

  const auto local = layer_position(layer, document_point);
  if (local.x() < 0 || local.y() < 0 || local.x() >= pixels.width() || local.y() >= pixels.height()) {
    return;
  }

  auto* px = pixels.pixel(local.x(), local.y());
  if (erase) {
    if (pixels.format().channels >= 4) {
      px[3] = 0;
    } else {
      px[0] = channel_from_color(secondary_color_, 0);
      px[1] = channel_from_color(secondary_color_, 1);
      px[2] = channel_from_color(secondary_color_, 2);
    }
    return;
  }

  px[0] = channel_from_color(color, 0);
  px[1] = channel_from_color(color, 1);
  px[2] = channel_from_color(color, 2);
  if (pixels.format().channels >= 4) {
    px[3] = channel_from_color(color, 3);
  }
}

QRect CanvasWidget::draw_line(QPoint from, QPoint to, bool erase) {
  if (editing_layer_mask()) {
    return draw_mask_line(from, to, erase);
  }
  if (document_ == nullptr || !document_->active_layer_id().has_value()) {
    return {};
  }

  auto options = edit_options(primary_color_, secondary_color_, brush_size_, brush_opacity_, brush_softness_,
                              fill_shapes_,
                              active_layer_locks_transparent_pixels(), *this);
  if (brush_opacity_ < 100) {
    options.stroke_pixel_gate = [this](std::int32_t x, std::int32_t y) {
      return brush_stroke_pixels_.insert(stroke_pixel_key(x, y)).second;
    };
  }
  return to_qrect(patchy::draw_line(*document_, *document_->active_layer_id(), from.x(), from.y(), to.x(), to.y(),
                                       options, erase));
}

QRect CanvasWidget::draw_gradient(QPoint from, QPoint to) {
  if (editing_layer_mask()) {
    return draw_mask_gradient(from, to);
  }
  if (document_ == nullptr || !document_->active_layer_id().has_value()) {
    return {};
  }

  return to_qrect(patchy::draw_gradient(
      *document_, *document_->active_layer_id(), from.x(), from.y(), to.x(), to.y(),
      edit_options(primary_color_, secondary_color_, brush_size_, brush_opacity_, brush_softness_, fill_shapes_,
                   active_layer_locks_transparent_pixels(), *this),
      current_gradient_options()));
}

GradientOptions CanvasWidget::current_gradient_options() const {
  GradientOptions gradient;
  gradient.method = gradient_method_;
  gradient.reverse = gradient_reverse_;
  gradient.opacity = static_cast<float>(gradient_opacity_) / 100.0F;
  gradient.stops = effective_gradient_stops();
  return gradient;
}

QRect CanvasWidget::draw_rectangle(QPoint from, QPoint to, bool erase) {
  if (editing_layer_mask()) {
    return draw_mask_rectangle(from, to, erase);
  }
  if (document_ == nullptr || !document_->active_layer_id().has_value()) {
    return {};
  }

  const auto rect = normalized_rect(from, to);
  auto options = edit_options(primary_color_, secondary_color_, brush_size_, brush_opacity_, brush_softness_,
                              fill_shapes_,
                              active_layer_locks_transparent_pixels(), *this);
  if (brush_opacity_ < 100) {
    options.stroke_pixel_gate = [this](std::int32_t x, std::int32_t y) {
      return brush_stroke_pixels_.insert(stroke_pixel_key(x, y)).second;
    };
  }
  return to_qrect(patchy::draw_rectangle(*document_, *document_->active_layer_id(), to_core_rect(rect),
                                            options, erase));
}

QRect CanvasWidget::draw_ellipse(QPoint from, QPoint to, bool erase) {
  if (editing_layer_mask()) {
    return draw_mask_ellipse(from, to, erase);
  }
  if (document_ == nullptr || !document_->active_layer_id().has_value()) {
    return {};
  }

  const auto rect = normalized_rect(from, to);
  auto options = edit_options(primary_color_, secondary_color_, brush_size_, brush_opacity_, brush_softness_,
                              fill_shapes_,
                              active_layer_locks_transparent_pixels(), *this);
  if (brush_opacity_ < 100) {
    options.stroke_pixel_gate = [this](std::int32_t x, std::int32_t y) {
      return brush_stroke_pixels_.insert(stroke_pixel_key(x, y)).second;
    };
  }
  return to_qrect(patchy::draw_ellipse(*document_, *document_->active_layer_id(), to_core_rect(rect),
                                          options, erase));
}

QRect CanvasWidget::flood_fill(QPoint start) {
  if (editing_layer_mask()) {
    return flood_fill_mask(start);
  }
  if (document_ == nullptr || !document_->active_layer_id().has_value()) {
    return {};
  }

  return to_qrect(patchy::flood_fill(
      *document_, *document_->active_layer_id(), start.x(), start.y(),
      edit_options(primary_color_, secondary_color_, brush_size_, brush_opacity_, brush_softness_, fill_shapes_,
                   active_layer_locks_transparent_pixels(), *this)));
}

QRect CanvasWidget::draw_mask_line(QPoint from, QPoint to, bool erase) {
  return draw_mask_brush_segment(from, to, erase);
}

QRect CanvasWidget::draw_mask_gradient(QPoint from, QPoint to) {
  auto* mask = active_layer_mask();
  if (document_ == nullptr || mask == nullptr) {
    return {};
  }

  auto affected = has_selection() && selected_document_rect().has_value()
                      ? *selected_document_rect()
                      : QRect(0, 0, document_->width(), document_->height());
  affected = affected.intersected(QRect(0, 0, document_->width(), document_->height()));
  if (affected.isEmpty() ||
      !expand_mask_to_include_rect(*mask, affected, QSize(document_->width(), document_->height()))) {
    return {};
  }

  const auto bounds = QRect(mask->bounds.x, mask->bounds.y, mask->bounds.width, mask->bounds.height);
  affected = affected.intersected(bounds);
  if (affected.isEmpty()) {
    return {};
  }

  const auto gradient = current_gradient_options();
  const auto stops = normalized_gradient_stops(gradient.stops);
  const auto dx = static_cast<double>(to.x() - from.x());
  const auto dy = static_cast<double>(to.y() - from.y());
  const auto length_squared = dx * dx + dy * dy;
  const auto radius = std::sqrt(length_squared);

  QRect dirty;
  for (int y = affected.top(); y <= affected.bottom(); ++y) {
    for (int x = affected.left(); x <= affected.right(); ++x) {
      const QPoint document_point(x, y);
      if (!selection_allows(document_point)) {
        continue;
      }
      double t = 0.0;
      switch (gradient.method) {
        case GradientMethod::Radial:
          t = radius <= 0.0 ? 0.0
                            : std::sqrt(static_cast<double>(x - from.x()) * static_cast<double>(x - from.x()) +
                                        static_cast<double>(y - from.y()) * static_cast<double>(y - from.y())) /
                                  radius;
          break;
        case GradientMethod::Linear:
          t = length_squared <= 0.0
                  ? 0.0
                  : ((static_cast<double>(x - from.x()) * dx + static_cast<double>(y - from.y()) * dy) /
                     length_squared);
          break;
      }
      const auto color = gradient_color_at(stops, gradient.opacity, gradient.reverse, t);
      auto coverage = static_cast<float>(color.a) / 255.0F;
      if (has_selection()) {
        coverage *= static_cast<float>(selection_alpha_at(document_point)) / 255.0F;
      }
      if (coverage <= 0.0F) {
        continue;
      }
      const auto value = mask_value_from_color(QColor(color.r, color.g, color.b));
      auto* px = mask->pixels.pixel(x - bounds.x(), y - bounds.y());
      *px = blend_mask_value(*px, value, coverage);
      dirty = dirty.united(QRect(document_point, QSize(1, 1)));
    }
    tick_processing_operation();
  }
  return dirty;
}

QRect CanvasWidget::draw_mask_rectangle(QPoint from, QPoint to, bool erase) {
  const auto rect = normalized_rect(from, to).intersected(QRect(0, 0, document_ == nullptr ? 0 : document_->width(),
                                                               document_ == nullptr ? 0 : document_->height()));
  if (rect.isEmpty()) {
    return {};
  }
  if (!fill_shapes_) {
    QRect dirty;
    dirty = dirty.united(draw_mask_line(rect.topLeft(), rect.topRight(), erase));
    dirty = dirty.united(draw_mask_line(rect.topRight(), rect.bottomRight(), erase));
    dirty = dirty.united(draw_mask_line(rect.bottomRight(), rect.bottomLeft(), erase));
    dirty = dirty.united(draw_mask_line(rect.bottomLeft(), rect.topLeft(), erase));
    return dirty;
  }

  auto* mask = active_layer_mask();
  if (document_ == nullptr || mask == nullptr ||
      !expand_mask_to_include_rect(*mask, rect, QSize(document_->width(), document_->height()))) {
    return {};
  }
  const auto bounds = QRect(mask->bounds.x, mask->bounds.y, mask->bounds.width, mask->bounds.height);
  const auto value = erase ? std::uint8_t{0} : mask_value_from_color(primary_color_);
  const auto opacity = static_cast<float>(brush_opacity_) / 100.0F;
  QRect dirty;
  for (int y = rect.top(); y <= rect.bottom(); ++y) {
    for (int x = rect.left(); x <= rect.right(); ++x) {
      const QPoint document_point(x, y);
      if (!selection_allows(document_point)) {
        continue;
      }
      auto coverage = opacity;
      if (has_selection()) {
        coverage *= static_cast<float>(selection_alpha_at(document_point)) / 255.0F;
      }
      if (coverage <= 0.0F) {
        continue;
      }
      auto* px = mask->pixels.pixel(x - bounds.x(), y - bounds.y());
      *px = blend_mask_value(*px, value, coverage);
      dirty = dirty.united(QRect(document_point, QSize(1, 1)));
    }
    tick_processing_operation();
  }
  return dirty;
}

QRect CanvasWidget::draw_mask_ellipse(QPoint from, QPoint to, bool erase) {
  const auto rect = normalized_rect(from, to).intersected(QRect(0, 0, document_ == nullptr ? 0 : document_->width(),
                                                               document_ == nullptr ? 0 : document_->height()));
  if (rect.isEmpty()) {
    return {};
  }
  if (!fill_shapes_) {
    constexpr int kSamples = 720;
    const auto rx = std::max(1.0, static_cast<double>(rect.width()) / 2.0);
    const auto ry = std::max(1.0, static_cast<double>(rect.height()) / 2.0);
    const auto cx = static_cast<double>(rect.x()) + rx;
    const auto cy = static_cast<double>(rect.y()) + ry;
    QPoint previous(static_cast<int>(std::round(cx + rx)), static_cast<int>(std::round(cy)));
    QRect dirty;
    for (int i = 1; i <= kSamples; ++i) {
      const auto angle = (static_cast<double>(i) / static_cast<double>(kSamples)) * 2.0 * kPi;
      QPoint current(static_cast<int>(std::round(cx + std::cos(angle) * rx)),
                     static_cast<int>(std::round(cy + std::sin(angle) * ry)));
      dirty = dirty.united(draw_mask_line(previous, current, erase));
      previous = current;
    }
    return dirty;
  }

  auto* mask = active_layer_mask();
  if (document_ == nullptr || mask == nullptr ||
      !expand_mask_to_include_rect(*mask, rect, QSize(document_->width(), document_->height()))) {
    return {};
  }
  const auto bounds = QRect(mask->bounds.x, mask->bounds.y, mask->bounds.width, mask->bounds.height);
  const auto value = erase ? std::uint8_t{0} : mask_value_from_color(primary_color_);
  const auto opacity = static_cast<float>(brush_opacity_) / 100.0F;
  const auto rx = std::max(1.0, static_cast<double>(rect.width()) / 2.0);
  const auto ry = std::max(1.0, static_cast<double>(rect.height()) / 2.0);
  const auto cx = static_cast<double>(rect.x()) + rx;
  const auto cy = static_cast<double>(rect.y()) + ry;
  QRect dirty;
  for (int y = rect.top(); y <= rect.bottom(); ++y) {
    for (int x = rect.left(); x <= rect.right(); ++x) {
      const auto nx = (static_cast<double>(x) + 0.5 - cx) / rx;
      const auto ny = (static_cast<double>(y) + 0.5 - cy) / ry;
      if (nx * nx + ny * ny > 1.0) {
        continue;
      }
      const QPoint document_point(x, y);
      if (!selection_allows(document_point)) {
        continue;
      }
      auto coverage = opacity;
      if (has_selection()) {
        coverage *= static_cast<float>(selection_alpha_at(document_point)) / 255.0F;
      }
      if (coverage <= 0.0F) {
        continue;
      }
      auto* px = mask->pixels.pixel(x - bounds.x(), y - bounds.y());
      *px = blend_mask_value(*px, value, coverage);
      dirty = dirty.united(QRect(document_point, QSize(1, 1)));
    }
    tick_processing_operation();
  }
  return dirty;
}

QRect CanvasWidget::flood_fill_mask(QPoint start) {
  auto* mask = active_layer_mask();
  if (document_ == nullptr || mask == nullptr || !document_contains(start) || !selection_allows(start) ||
      !expand_mask_to_include_rect(*mask, QRect(0, 0, document_->width(), document_->height()),
                                   QSize(document_->width(), document_->height()))) {
    return {};
  }

  const auto bounds = QRect(mask->bounds.x, mask->bounds.y, mask->bounds.width, mask->bounds.height);
  const QPoint local_start(start.x() - bounds.x(), start.y() - bounds.y());
  if (local_start.x() < 0 || local_start.y() < 0 || local_start.x() >= mask->pixels.width() ||
      local_start.y() >= mask->pixels.height()) {
    return {};
  }

  const auto target = *mask->pixels.pixel(local_start.x(), local_start.y());
  const auto replacement = mask_value_from_color(primary_color_);
  if (target == replacement) {
    return {};
  }

  std::queue<QPoint> queue;
  std::vector<std::uint8_t> visited(static_cast<std::size_t>(mask->pixels.width()) *
                                    static_cast<std::size_t>(mask->pixels.height()));
  queue.push(local_start);
  QRect dirty;
  std::size_t progress_counter = 0;
  while (!queue.empty()) {
    ++progress_counter;
    if (progress_counter % 4096U == 0U) {
      tick_processing_operation();
    }
    const auto local = queue.front();
    queue.pop();
    if (local.x() < 0 || local.y() < 0 || local.x() >= mask->pixels.width() || local.y() >= mask->pixels.height()) {
      continue;
    }
    const auto index = static_cast<std::size_t>(local.y()) * static_cast<std::size_t>(mask->pixels.width()) +
                       static_cast<std::size_t>(local.x());
    if (visited[index] != 0U) {
      continue;
    }
    visited[index] = 1U;
    const QPoint document_point(bounds.x() + local.x(), bounds.y() + local.y());
    if (!selection_allows(document_point)) {
      continue;
    }
    auto* px = mask->pixels.pixel(local.x(), local.y());
    if (*px != target) {
      continue;
    }
    *px = replacement;
    dirty = dirty.united(QRect(document_point, QSize(1, 1)));
    queue.push(local + QPoint(1, 0));
    queue.push(local + QPoint(-1, 0));
    queue.push(local + QPoint(0, 1));
    queue.push(local + QPoint(0, -1));
  }
  return dirty;
}

void CanvasWidget::pick_color(QPoint point) {
  if (document_ == nullptr || !document_contains(point)) {
    return;
  }

  const auto picked = compose_document_pixel(point.x(), point.y());
  primary_color_ = picked;
  if (color_picked_callback_) {
    color_picked_callback_(picked);
  }
}

void CanvasWidget::magic_wand_select(QPoint start) {
  if (document_ == nullptr || !document_contains(start)) {
    return;
  }

  QImage source_image;
  if (wand_sample_all_layers_) {
    ensure_render_cache();
    if (render_cache_.isNull()) {
      return;
    }
    source_image = render_cache_;
  } else {
    const auto* layer = active_pixel_layer();
    if (layer == nullptr) {
      if (status_callback_) {
        status_callback_(tr("Select a pixel layer before using Magic Wand"));
      }
      return;
    }
    source_image = active_layer_wand_sample_image(*layer, QSize(document_->width(), document_->height()));
  }

  if (source_image.isNull() || source_image.format() != QImage::Format_RGBA8888) {
    return;
  }

  const auto width = source_image.width();
  const auto height = source_image.height();
  const auto total_pixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  const auto pixel = [&source_image](int x, int y) {
    return source_image.constScanLine(y) + static_cast<std::size_t>(x) * 4U;
  };
  const auto* target = pixel(start.x(), start.y());
  const int target_red = target[0];
  const int target_green = target[1];
  const int target_blue = target[2];
  const int target_alpha = target[3];
  const auto tolerance_squared = wand_tolerance_ * wand_tolerance_ * 4;
  std::vector<std::uint8_t> selected(total_pixels);
  auto min_x = std::numeric_limits<int>::max();
  auto min_y = std::numeric_limits<int>::max();
  auto max_x = std::numeric_limits<int>::min();
  auto max_y = std::numeric_limits<int>::min();
  int count = 0;

  const auto matches = [&](int x, int y) {
    const auto* color = pixel(x, y);
    const auto dr = static_cast<int>(color[0]) - target_red;
    const auto dg = static_cast<int>(color[1]) - target_green;
    const auto db = static_cast<int>(color[2]) - target_blue;
    const auto da = static_cast<int>(color[3]) - target_alpha;
    return dr * dr + dg * dg + db * db + da * da <= tolerance_squared;
  };

  const auto select_pixel = [&](int x, int y) {
    const auto index = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
    selected[index] = 1U;
    min_x = std::min(min_x, x);
    min_y = std::min(min_y, y);
    max_x = std::max(max_x, x);
    max_y = std::max(max_y, y);
    ++count;
  };

  if (wand_contiguous_) {
    std::vector<std::uint8_t> visited(total_pixels);
    std::vector<int> queue;
    queue.reserve(std::min<std::size_t>(total_pixels, 1'000'000U));
    queue.push_back(start.y() * width + start.x());
    std::size_t progress_counter = 0;
    while (!queue.empty()) {
      ++progress_counter;
      if (progress_counter % 4096U == 0U) {
        tick_processing_operation();
      }
      const auto index_value = queue.back();
      queue.pop_back();
      const auto index = static_cast<std::size_t>(index_value);
      if (visited[index] != 0U) {
        continue;
      }
      visited[index] = 1U;
      const auto x = index_value % width;
      const auto y = index_value / width;
      if (!matches(x, y)) {
        continue;
      }

      select_pixel(x, y);
      if (x + 1 < width && visited[index + 1U] == 0U) {
        queue.push_back(index_value + 1);
      }
      if (x > 0 && visited[index - 1U] == 0U) {
        queue.push_back(index_value - 1);
      }
      if (y + 1 < height && visited[index + static_cast<std::size_t>(width)] == 0U) {
        queue.push_back(index_value + width);
      }
      if (y > 0 && visited[index - static_cast<std::size_t>(width)] == 0U) {
        queue.push_back(index_value - width);
      }
    }
  } else {
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        if (matches(x, y)) {
          select_pixel(x, y);
        }
      }
      tick_processing_operation();
    }
  }

  if (count == 0) {
    if (selection_operation_ == SelectionMode::Replace) {
      set_selection_from_region(QRegion());
    } else {
      restore_selection_before_edit();
    }
  } else {
    const auto wand_region = region_from_mask(selected, width, height, min_x, min_y, max_x, max_y);
    if (selection_feather_radius_ > 0) {
      const auto hard_bounds = wand_region.boundingRect();
      auto hard_mask = hard_mask_from_region(wand_region, hard_bounds);
      const auto feather = selection_feather_radius_;
      const auto padding = feather_mask_padding(feather);
      auto feather_bounds = hard_bounds.adjusted(-padding, -padding, padding, padding);
      feather_bounds = feather_bounds.intersected(QRect(0, 0, document_->width(), document_->height()));
      if (!feather_bounds.isEmpty()) {
        QImage padded(feather_bounds.size(), QImage::Format_Grayscale8);
        padded.fill(0);
        QPainter painter(&padded);
        painter.drawImage(hard_bounds.topLeft() - feather_bounds.topLeft(), hard_mask);
        painter.end();
        padded = feather_blur_mask(std::move(padded), feather);
        combine_selection_from_mask(region_from_alpha_mask(padded, feather_bounds), feather_bounds, std::move(padded));
      } else {
        combine_selection_from_region(wand_region);
      }
    } else {
      combine_selection_from_region(wand_region);
    }
  }
  if (status_callback_) {
    status_callback_(tr("Magic Wand selected %1 px").arg(count));
  }
  update();
}

QRect CanvasWidget::marquee_selection_rect(QPoint anchor, QPoint current) const {
  QRect rect;
  if (marquee_style_ == MarqueeStyle::FixedSize) {
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
  } else {
    rect = normalized_rect(anchor, current);
  }
  return rect;
}

QRegion CanvasWidget::marquee_selection_region(QPoint anchor, QPoint current) const {
  if (document_ == nullptr) {
    return {};
  }

  const auto rect = marquee_selection_rect(anchor, current);
  const auto marquee = tool_ == CanvasTool::EllipticalMarquee ? QRegion(rect, QRegion::Ellipse) : QRegion(rect);
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
    return rectangle_selection_mask(rect, bounds, feather);
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
  bounds = {};
  if (document_ == nullptr || polygon.size() < 3) {
    return {};
  }

  const auto canvas_rect = QRect(0, 0, document_->width(), document_->height());
  const auto feather = selection_feather_radius_;
  const auto padding = feather_mask_padding(feather);
  bounds = polygon.boundingRect().adjusted(-padding, -padding, padding, padding).intersected(canvas_rect);
  if (bounds.isEmpty()) {
    return {};
  }

  QPainterPath path;
  path.addPolygon(QPolygonF(polygon));
  path.closeSubpath();
  return shape_mask_from_path(path, bounds, feather, selection_antialias_ || feather > 0);
}

CanvasWidget::SelectionMode CanvasWidget::selection_operation(Qt::KeyboardModifiers modifiers) const noexcept {
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
  if (document_ != nullptr) {
    selection = selection.intersected(QRect(0, 0, document_->width(), document_->height()));
  }
  selection_ = std::move(selection);
  selection_display_region_ = selection_;
  selection_mask_bounds_ = {};
  selection_mask_alpha_ = QImage();
}

void CanvasWidget::set_selection_from_mask(QRegion selection, QRect mask_bounds, QImage mask_alpha) {
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
    return;
  }
  selection_mask_bounds_ = mask_bounds;
  selection_display_region_ = region_from_alpha_mask(mask_alpha, mask_bounds, 128U);
  if (selection_display_region_.isEmpty()) {
    selection_display_region_ = selection_;
  }
  selection_mask_alpha_ = std::move(mask_alpha);
}

void CanvasWidget::restore_selection_before_edit() {
  selection_ = selection_before_edit_;
  selection_display_region_ = selection_display_region_before_edit_;
  if (selection_display_region_.isEmpty() && !selection_.isEmpty()) {
    selection_display_region_ = selection_;
  }
  selection_mask_bounds_ = selection_mask_before_edit_bounds_;
  selection_mask_alpha_ = selection_mask_before_edit_alpha_;
}

void CanvasWidget::combine_selection_from_region(const QRegion& candidate) {
  if (selection_mask_before_edit_alpha_.isNull()) {
    set_selection_from_region(combine_selection(candidate));
    return;
  }
  const auto candidate_bounds = candidate.boundingRect();
  combine_selection_from_mask(candidate, candidate_bounds, hard_mask_from_region(candidate, candidate_bounds));
}

void CanvasWidget::combine_selection_from_mask(QRegion candidate, QRect candidate_bounds, QImage candidate_alpha) {
  if (candidate.isEmpty() && !candidate_bounds.isEmpty() && !candidate_alpha.isNull()) {
    candidate = QRegion(candidate_bounds);
  }
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

  set_selection_from_mask(region_from_alpha_mask(combined, bounds), bounds, std::move(combined));
}

CanvasWidget::TransformHandle CanvasWidget::transform_handle_at(QPoint widget_point) const {
  if (!transforming_layer_) {
    return TransformHandle::None;
  }
  return transform_handle_at(widget_point, transform_current_rect_, transform_angle_);
}

CanvasWidget::TransformHandle CanvasWidget::transform_handle_at(QPoint widget_point, QRectF document_rect,
                                                                double angle_degrees) const {
  constexpr double kHandleHit = 14.0;
  const std::array<TransformHandle, 9> handles = {
      TransformHandle::Rotate,     TransformHandle::TopLeft, TransformHandle::Top,
      TransformHandle::TopRight,   TransformHandle::Right,   TransformHandle::BottomRight,
      TransformHandle::Bottom,     TransformHandle::BottomLeft, TransformHandle::Left};
  for (const auto handle : handles) {
    const auto point = transform_handle_position(handle, document_rect, angle_degrees);
    const QRectF hit_rect(point.x() - kHandleHit / 2.0, point.y() - kHandleHit / 2.0, kHandleHit, kHandleHit);
    if (hit_rect.contains(widget_point)) {
      return handle;
    }
  }

  QPolygonF polygon;
  polygon << transform_handle_position(TransformHandle::TopLeft, document_rect, angle_degrees)
          << transform_handle_position(TransformHandle::TopRight, document_rect, angle_degrees)
          << transform_handle_position(TransformHandle::BottomRight, document_rect, angle_degrees)
          << transform_handle_position(TransformHandle::BottomLeft, document_rect, angle_degrees);
  QPainterPath path;
  path.addPolygon(polygon);
  if (path.contains(widget_point)) {
    return TransformHandle::Move;
  }
  return TransformHandle::None;
}

void CanvasWidget::update_free_transform_preview(QPointF document_point, Qt::KeyboardModifiers modifiers) {
  if (transform_drag_handle_ != TransformHandle::Rotate) {
    document_point = snapped_document_point_f(document_point);
  }
  auto rect = transform_drag_start_rect_;
  const auto drag_delta = document_point - transform_drag_start_point_;

  if (transform_drag_handle_ == TransformHandle::Move) {
    rect.translate(drag_delta);
    transform_current_rect_ = rect;
    refresh_transform_composited_preview_cache();
    update();
    notify_transform_controls_changed();
    return;
  }

  if (transform_drag_handle_ == TransformHandle::Rotate) {
    const auto center = transform_drag_start_rect_.center();
    const auto start = std::atan2(transform_drag_start_point_.y() - center.y(), transform_drag_start_point_.x() - center.x());
    const auto now = std::atan2(document_point.y() - center.y(), document_point.x() - center.x());
    auto degrees = transform_start_angle_ + ((now - start) * 180.0 / kPi);
    if ((modifiers & Qt::ShiftModifier) != 0) {
      degrees = std::round(degrees / 15.0) * 15.0;
    }
    transform_angle_ = degrees;
    refresh_transform_composited_preview_cache();
    update();
    notify_transform_controls_changed();
    return;
  }

  switch (transform_drag_handle_) {
    case TransformHandle::TopLeft:
      rect.setTopLeft(document_point);
      break;
    case TransformHandle::Top:
      rect.setTop(document_point.y());
      break;
    case TransformHandle::TopRight:
      rect.setTopRight(document_point);
      break;
    case TransformHandle::Right:
      rect.setRight(document_point.x());
      break;
    case TransformHandle::BottomRight:
      rect.setBottomRight(document_point);
      break;
    case TransformHandle::Bottom:
      rect.setBottom(document_point.y());
      break;
    case TransformHandle::BottomLeft:
      rect.setBottomLeft(document_point);
      break;
    case TransformHandle::Left:
      rect.setLeft(document_point.x());
      break;
    case TransformHandle::None:
    case TransformHandle::Move:
    case TransformHandle::Rotate:
      break;
  }

  auto raw_rect = rect;
  const auto corner_handle = transform_drag_handle_ == TransformHandle::TopLeft ||
                             transform_drag_handle_ == TransformHandle::TopRight ||
                             transform_drag_handle_ == TransformHandle::BottomRight ||
                             transform_drag_handle_ == TransformHandle::BottomLeft;
  if (corner_handle && (modifiers & Qt::ShiftModifier) != 0 && transform_drag_start_rect_.height() > 0.0) {
    QPointF anchor;
    switch (transform_drag_handle_) {
      case TransformHandle::TopLeft:
        anchor = transform_drag_start_rect_.bottomRight();
        break;
      case TransformHandle::TopRight:
        anchor = transform_drag_start_rect_.bottomLeft();
        break;
      case TransformHandle::BottomLeft:
        anchor = transform_drag_start_rect_.topRight();
        break;
      case TransformHandle::BottomRight:
      default:
        anchor = transform_drag_start_rect_.topLeft();
        break;
    }

    const auto ratio = transform_drag_start_rect_.width() / transform_drag_start_rect_.height();
    const auto dx = document_point.x() - anchor.x();
    const auto dy = document_point.y() - anchor.y();
    const auto sign_x = dx < 0.0 ? -1.0 : 1.0;
    const auto sign_y = dy < 0.0 ? -1.0 : 1.0;
    auto new_width = std::max(1.0, std::abs(dx));
    auto new_height = std::max(1.0, std::abs(dy));
    if (new_width / ratio > new_height) {
      new_height = new_width / ratio;
    } else {
      new_width = new_height * ratio;
    }
    // Write the aspect-locked corner back through the same setters as the
    // non-Shift path so the dragged-corner/anchor relationship is preserved.
    // Building a QRectF directly from the anchor would invert width/height for
    // handles whose anchor is not the top-left, which the flip detection below
    // would then misread as a mirror (a 180° flip when both axes invert).
    const QPointF locked_corner(anchor.x() + sign_x * new_width, anchor.y() + sign_y * new_height);
    switch (transform_drag_handle_) {
      case TransformHandle::TopLeft:
        rect.setTopLeft(locked_corner);
        break;
      case TransformHandle::TopRight:
        rect.setTopRight(locked_corner);
        break;
      case TransformHandle::BottomLeft:
        rect.setBottomLeft(locked_corner);
        break;
      case TransformHandle::BottomRight:
      default:
        rect.setBottomRight(locked_corner);
        break;
    }
    raw_rect = rect;
  }

  transform_scale_x_sign_ = transform_drag_start_scale_x_sign_ * (raw_rect.width() < 0.0 ? -1.0 : 1.0);
  transform_scale_y_sign_ = transform_drag_start_scale_y_sign_ * (raw_rect.height() < 0.0 ? -1.0 : 1.0);
  rect = rect.normalized();

  if (rect.width() < 1.0) {
    rect.setWidth(1.0);
  }
  if (rect.height() < 1.0) {
    rect.setHeight(1.0);
  }
  transform_current_rect_ = rect;
  refresh_transform_composited_preview_cache();
  update();
  notify_transform_controls_changed();
}

void CanvasWidget::commit_free_transform() {
  if (!transforming_layer_ || document_ == nullptr || !transform_layer_id_.has_value() ||
      transform_source_image_.isNull()) {
    cancel_free_transform();
    return;
  }

  auto* layer = document_->find_layer(*transform_layer_id_);
  if (layer == nullptr) {
    cancel_free_transform();
    return;
  }
  const auto old_bounds = layer->bounds();
  const QRectF old_bounds_rect(old_bounds.x, old_bounds.y, old_bounds.width, old_bounds.height);
  const auto text_layer = layer_is_text(*layer);
  const auto original_text_transform =
      text_layer ? stored_text_transform_for_layer(*layer).value_or(identity_text_transform_for_rect(old_bounds_rect))
                 : LayerAffineTransform{};

  const auto transformed_result =
      resample_transformed_rgba8(transform_source_image_,
                                  transform_source_to_document(transform_source_image_.size(), transform_current_rect_,
                                                               transform_angle_, transform_scale_x_sign_,
                                                               transform_scale_y_sign_),
                                  transform_interpolation_);
  auto transformed = transformed_result.image;
  auto new_bounds = transformed_result.bounds;

  const auto original_transform_bounds =
      Rect{static_cast<std::int32_t>(std::round(transform_original_rect_.left())),
           static_cast<std::int32_t>(std::round(transform_original_rect_.top())),
           static_cast<std::int32_t>(std::round(transform_original_rect_.width())),
           static_cast<std::int32_t>(std::round(transform_original_rect_.height()))};
  const auto orientation_changed = transform_scale_x_sign_ < 0.0 || transform_scale_y_sign_ < 0.0;
  const auto changed = orientation_changed || std::abs(transform_angle_) > 0.01 || new_bounds.x != original_transform_bounds.x ||
                       new_bounds.y != original_transform_bounds.y ||
                       new_bounds.width != original_transform_bounds.width ||
                       new_bounds.height != original_transform_bounds.height;
  if (changed && before_edit_callback_) {
    before_edit_callback_(tr("Free Transform"));
  }
  if (changed) {
    layer->set_pixels(pixels_from_image_rgba(transformed));
    layer->set_bounds(new_bounds);
    if (text_layer) {
      const auto delta = affine_from_qtransform(free_transform_delta(
          transform_original_rect_, transform_current_rect_, transform_angle_, transform_scale_x_sign_, transform_scale_y_sign_));
      layer->metadata()[kLayerMetadataTextTransform] =
          serialize_layer_affine_transform(compose_layer_affine_transform(delta, original_text_transform));
      layer->metadata()[kLayerMetadataTextRasterStatus] = "patchy_raster";
      // Replace the resampled (and therefore blocky on scale-up) bitmap with glyphs re-rasterized
      // through the composed transform, so transformed text stays crisp like Photoshop.
      if (text_layer_transform_render_callback_ && text_layer_transform_render_callback_(*transform_layer_id_)) {
        new_bounds = layer->bounds();
      }
    }
  }

  transforming_layer_ = false;
  dragging_transform_ = false;
  transform_layer_id_.reset();
  transform_drag_handle_ = TransformHandle::None;
  transform_scale_x_sign_ = 1.0;
  transform_scale_y_sign_ = 1.0;
  transform_drag_start_scale_x_sign_ = 1.0;
  transform_drag_start_scale_y_sign_ = 1.0;
  transform_base_cache_ = QImage();
  transform_source_image_ = QImage();
  transform_composited_preview_cache_ = QImage();
  transform_requires_composited_preview_ = false;
  transform_source_local_rect_ = QRect();
  update_tool_cursor();
  document_changed(to_qrect(old_bounds).united(to_qrect(new_bounds)));
  if (status_callback_) {
    status_callback_(changed ? tr("Transformed layer") : tr("Free Transform cancelled"));
  }
  notify_transform_controls_changed();
}

std::vector<LayerId> CanvasWidget::movable_layer_ids() const {
  std::vector<LayerId> ids;
  if (document_ == nullptr) {
    return ids;
  }

  const auto add_if_movable = [this, &ids](const Layer& layer, LayerLockFlags selected_ancestor_lock_flags) {
    if (std::find(ids.begin(), ids.end(), layer.id()) != ids.end()) {
      return;
    }
    if (((selected_ancestor_lock_flags | patchy::layer_effective_lock_flags(document_->layers(), layer.id())) &
         kLayerLockPosition) != kLayerLockNone) {
      return;
    }
    if (!layer_has_movable_pixels(layer)) {
      return;
    }
    ids.push_back(layer.id());
  };

  const std::function<void(const Layer&, LayerLockFlags)> add_movable_layer_tree = [&](const Layer& layer,
                                                                                       LayerLockFlags ancestor_flags) {
    const auto effective_flags = ancestor_flags | patchy::layer_lock_flags(layer);
    if (layer.kind() == LayerKind::Group) {
      for (const auto& child : layer.children()) {
        add_movable_layer_tree(child, effective_flags);
      }
      return;
    }
    add_if_movable(layer, effective_flags);
  };

  auto add_movable_by_id = [&](LayerId id) {
    if (const auto* layer = document_->find_layer(id); layer != nullptr) {
      add_movable_layer_tree(*layer, kLayerLockNone);
    }
  };

  if (!selected_layer_ids_.empty()) {
    for (const auto id : root_drop_layer_ids(document_->layers(), selected_layer_ids_)) {
      add_movable_by_id(id);
    }
  }

  if (ids.empty()) {
    if (const auto active = document_->active_layer_id(); active.has_value()) {
      add_movable_by_id(*active);
    }
  }
  return ids;
}

std::optional<QRect> CanvasWidget::move_hover_outline_rect_at(QPoint widget_position,
                                                              Qt::KeyboardModifiers modifiers) const {
  if (document_ == nullptr || tool_ != CanvasTool::Move || moving_layer_ || transforming_layer_ || dragging_transform_ ||
      panning_ || dragging_guide_ || creating_guide_ || widget_position_in_ruler(widget_position)) {
    return std::nullopt;
  }

  const auto guide_drag_allowed = tool_ == CanvasTool::Move || modifiers.testFlag(Qt::ControlModifier);
  if (guide_drag_allowed && !guides_locked_ && guide_at_widget_position(widget_position) >= 0) {
    return std::nullopt;
  }

  const auto document_point = document_position(widget_position);
  if (!document_contains(document_point)) {
    return std::nullopt;
  }

  auto* hit_layer = topmost_move_layer_at(document_point, true);
  if (hit_layer == nullptr) {
    return std::nullopt;
  }

  const auto selected_move_layer_ids = movable_layer_ids();
  if (!auto_select_layer_) {
    if (std::find(selected_move_layer_ids.begin(), selected_move_layer_ids.end(), hit_layer->id()) ==
        selected_move_layer_ids.end()) {
      return std::nullopt;
    }
  }
  if (show_transform_controls_ && auto_select_layer_) {
    if (!selected_layer_ids_.empty()) {
      if (selected_layer_ids_.size() == 1U && selected_layer_ids_.front() == hit_layer->id()) {
        return std::nullopt;
      }
    } else if (const auto active = document_->active_layer_id(); active.has_value() && *active == hit_layer->id()) {
      return std::nullopt;
    }
  }

  const auto bounds = move_layer_outline_bounds(*hit_layer);
  if (!bounds.has_value()) {
    return std::nullopt;
  }
  const QRect outline(bounds->x, bounds->y, bounds->width, bounds->height);
  if (outline.isEmpty()) {
    return std::nullopt;
  }
  return outline;
}

void CanvasWidget::update_move_hover_outline(QPoint widget_position, Qt::KeyboardModifiers modifiers) {
  const auto next = move_hover_outline_rect_at(widget_position, modifiers);
  if (move_hover_outline_rect_ == next) {
    return;
  }

  QRect dirty;
  if (move_hover_outline_rect_.has_value()) {
    dirty = dirty.united(widget_rect_for_document_rect(*move_hover_outline_rect_));
  }
  if (next.has_value()) {
    dirty = dirty.united(widget_rect_for_document_rect(*next));
  }
  move_hover_outline_rect_ = next;

  if (!dirty.isEmpty()) {
    update(dirty);
  } else {
    update();
  }
}

void CanvasWidget::clear_move_hover_outline() {
  if (!move_hover_outline_rect_.has_value()) {
    return;
  }

  const auto dirty = widget_rect_for_document_rect(*move_hover_outline_rect_);
  move_hover_outline_rect_.reset();
  if (!dirty.isEmpty()) {
    update(dirty);
  } else {
    update();
  }
}

QRect CanvasWidget::moving_layer_outline_rect(const MovingLayer& moving_layer, QPoint delta) const {
  if (!moving_layer.original_opaque_bounds.has_value()) {
    return {};
  }

  auto bounds = *moving_layer.original_opaque_bounds;
  bounds.x += delta.x();
  bounds.y += delta.y();
  return QRect(bounds.x, bounds.y, bounds.width, bounds.height);
}

std::vector<std::pair<LayerId, Rect>> CanvasWidget::moving_layer_bounds(QPoint delta) const {
  std::vector<std::pair<LayerId, Rect>> bounds;
  bounds.reserve(moving_layers_.size());
  for (const auto& moving_layer : moving_layers_) {
    auto moved = moving_layer.original_bounds;
    moved.x += delta.x();
    moved.y += delta.y();
    bounds.emplace_back(moving_layer.id, moved);
  }
  return bounds;
}

QRect CanvasWidget::moving_layers_dirty_rect(QPoint old_delta, QPoint new_delta) const {
  return moving_layers_dirty_region(old_delta, new_delta).boundingRect();
}

QRegion CanvasWidget::moving_layers_dirty_region(QPoint old_delta, QPoint new_delta) const {
  QRegion region;
  if (document_ == nullptr) {
    return region;
  }
  for (const auto& moving_layer : moving_layers_) {
    auto* layer = document_->find_layer(moving_layer.id);
    if (layer == nullptr) {
      continue;
    }
    auto old_bounds = moving_layer.original_bounds;
    old_bounds.x += old_delta.x();
    old_bounds.y += old_delta.y();
    auto new_bounds = moving_layer.original_bounds;
    new_bounds.x += new_delta.x();
    new_bounds.y += new_delta.y();
    const auto old_rect = to_qrect(layer_bounds_with_effects(*layer, old_bounds));
    const auto new_rect = to_qrect(layer_bounds_with_effects(*layer, new_bounds));
    if (!old_rect.isEmpty()) {
      region += old_rect;
    }
    if (!new_rect.isEmpty()) {
      region += new_rect;
    }
  }
  return region;
}

QRect CanvasWidget::moving_layers_outline_dirty_rect(QPoint old_delta, QPoint new_delta) const {
  QRect dirty;
  if (document_ == nullptr) {
    return dirty;
  }
  for (const auto& moving_layer : moving_layers_) {
    const auto old_outline = moving_layer_outline_rect(moving_layer, old_delta);
    if (!old_outline.isEmpty()) {
      dirty = dirty.united(old_outline);
    }
    const auto new_outline = moving_layer_outline_rect(moving_layer, new_delta);
    if (!new_outline.isEmpty()) {
      dirty = dirty.united(new_outline);
    }
  }
  if (dirty.isEmpty()) {
    return dirty;
  }
  return dirty.adjusted(-2, -2, 2, 2).intersected(QRect(0, 0, document_->width(), document_->height()));
}

bool CanvasWidget::moving_layers_should_use_outline_preview(QPoint old_delta, QPoint new_delta) const {
  if (document_ == nullptr || moving_layers_.empty()) {
    return false;
  }
  const auto dirty =
      moving_layers_dirty_rect(old_delta, new_delta).intersected(QRect(0, 0, document_->width(), document_->height()));
  if (dirty.isEmpty()) {
    return false;
  }
  const auto dirty_area = static_cast<std::int64_t>(dirty.width()) * static_cast<std::int64_t>(dirty.height());
  if (dirty_area >= kMoveOutlineDirtyAreaThreshold) {
    return true;
  }
  if (dirty_area < kStyledMoveOutlineDirtyAreaThreshold) {
    return false;
  }
  return std::any_of(moving_layers_.begin(), moving_layers_.end(),
                     [](const MovingLayer& moving_layer) { return moving_layer.expensive_style; });
}

QRegion CanvasWidget::move_active_layer_by(QPoint delta) {
  if (document_ == nullptr || delta.isNull()) {
    return {};
  }
  QRegion dirty;
  for (const auto id : movable_layer_ids()) {
    auto* layer = document_->find_layer(id);
    if (layer == nullptr) {
      continue;
    }
    const auto old_bounds = layer->bounds();
    auto bounds = old_bounds;
    bounds.x += delta.x();
    bounds.y += delta.y();
    layer->set_bounds(bounds);
    patchy::translate_moved_layer_metadata(*layer, delta.x(), delta.y(), document_->width(), document_->height());
    dirty += to_qrect(layer_bounds_with_effects(*layer, old_bounds));
    dirty += to_qrect(layer_bounds_with_effects(*layer, bounds));
  }
  return dirty;
}

}  // namespace patchy::ui
