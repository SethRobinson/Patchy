// Helpers shared by the canvas_widget_*.cpp translation units (see
// canvas_widget_shared.hpp). Pure function moves from canvas_widget.cpp;
// behavior must stay identical.

#include "ui/canvas_widget_shared.hpp"

#include "core/blend_math.hpp"
#include "core/layer.hpp"
#include "core/layer_metadata.hpp"
#include "core/layer_render_utils.hpp"
#include "core/pixel_buffer.hpp"
#include "core/pixel_depth.hpp"
#include "core/pixel_tools.hpp"
#include "core/smart_filter.hpp"
#include "core/smart_object.hpp"
#include "ui/canvas_types.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/edit_conversions.hpp"
#include "ui/qt_geometry.hpp"

#include <QColor>
#include <QImage>
#include <QPainter>
#include <QPen>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QRegion>
#include <QSize>
#include <QSysInfo>
#include <Qt>
#include <QtGlobal>

#include <algorithm>
#include <string>
#include <set>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace patchy::ui {

bool layer_has_movable_pixels(const Layer& layer) {
  if (layer.kind() != LayerKind::Pixel && !layer_is_text(layer)) {
    return false;
  }
  // A smart object whose SoLd could not be parsed (e.g. an ObAr warp mesh) has no
  // translatable quad: moving its pixels would desync them from the verbatim blocks
  // Photoshop re-renders from, so it stays pinned until rasterized.
  if (layer_is_smart_object(layer) && smart_object_lock_reason(layer) == "unparsed") {
    return false;
  }
  if (const auto* stack = layer.smart_filter_stack();
      stack != nullptr &&
      stack->support == SmartFilterStackSupport::Unsupported) {
    return false;
  }
  // Any depth: moving and transforming handle 16/32-bit pixels (docs/high-bit-depth.md).
  const auto& pixels = layer.pixels();
  return !pixels.empty() && pixels.format().channels >= 3;
}

bool move_layer_requires_smart_filter_rerender(const Layer& layer) {
  const auto* stack = layer.smart_filter_stack();
  return layer_is_smart_object(layer) && stack != nullptr &&
         stack->support == SmartFilterStackSupport::Supported;
}

std::optional<QRect> opaque_pixel_local_rect(const Layer& layer) {
  const auto& pixels = layer.pixels();
  if (pixels.empty() || pixels.format().channels < 3) {
    return std::nullopt;
  }
  const auto bounds = visible_alpha_local_bounds(layer);
  if (!bounds.has_value()) {
    return std::nullopt;
  }
  return QRect(bounds->x, bounds->y, bounds->width, bounds->height);
}

namespace {

std::optional<Rect> opaque_pixel_document_bounds(const Layer& layer);

}  // namespace

bool pixel_layer_contains_document_point(const Layer& layer, QPoint document_point, bool require_visible_pixel) {
  if (!layer.visible() || layer.opacity() <= 0.0F || layer.kind() != LayerKind::Pixel) {
    return false;
  }
  const auto& pixels = layer.pixels();
  if (pixels.empty() || pixels.format().channels < 3) {
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
    const auto source_alpha =
        pixels.format().bit_depth == BitDepth::UInt8
            ? (pixels.format().channels >= 4 ? pixels.pixel(local_x, local_y)[3] : 255)
            : static_cast<int>(std::lround(pixel_alpha_at(pixels, local_x, local_y) * 255.0F));
    const auto mask_alpha =
        static_cast<int>(std::round(layer_mask_alpha_at(layer, document_point.x(), document_point.y()) * 255.0F));
    if (std::min(static_cast<int>(source_alpha), mask_alpha) < 8) {
      return false;
    }
  }
  return true;
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

bool move_layer_rect_contains_document_point(const Layer& layer, QPoint document_point) {
  if (!layer.visible() || layer.opacity() <= 0.0F || !layer_has_movable_pixels(layer)) {
    return false;
  }
  const auto bounds = move_layer_outline_bounds(layer);
  return bounds.has_value() && bounds->contains(document_point.x(), document_point.y());
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

namespace {

std::optional<Rect> opaque_pixel_document_bounds(const Layer& layer) {
  const auto local_rect = opaque_pixel_local_rect(layer);
  if (!local_rect.has_value()) {
    return std::nullopt;
  }

  const auto bounds = layer.bounds();
  return Rect{bounds.x + local_rect->x(), bounds.y + local_rect->y(), local_rect->width(), local_rect->height()};
}

}  // namespace

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

NarrowedLayerEdit::NarrowedLayerEdit(Layer& layer, std::function<void()> changed)
    : layer_(layer), changed_(std::move(changed)) {
  deep_ = std::as_const(layer).pixels();
  bounds_before_ = layer.bounds();
  narrowed_before_ = convert_pixel_buffer_depth(deep_, BitDepth::UInt8, SampleKind::Color);
  layer.pixels() = narrowed_before_;
}

NarrowedLayerEdit::~NarrowedLayerEdit() {
  const auto after = std::as_const(layer_).pixels();
  const auto bounds = layer_.bounds();
  const auto depth = deep_.format().bit_depth;
  const bool linear = depth == BitDepth::Float32;
  PixelBuffer result(after.width(), after.height(), with_bit_depth(after.format(), depth));
  result.clear(0);
  const auto widened = [linear, &after](std::int32_t x, std::int32_t y) {
    auto value = load_pixel(after.format(), after.pixel(x, y));
    if (linear) {
      for (std::size_t c = 0; c < 3U; ++c) {
        value[c] = static_cast<float>(srgb_decode(static_cast<double>(value[c]) / 255.0) * 255.0);
      }
    }
    return value;
  };
  for (std::int32_t y = 0; y < after.height(); ++y) {
    for (std::int32_t x = 0; x < after.width(); ++x) {
      const auto document_x = bounds.x + x;
      const auto document_y = bounds.y + y;
      const auto old_x = document_x - bounds_before_.x;
      const auto old_y = document_y - bounds_before_.y;
      const bool inside = old_x >= 0 && old_y >= 0 && old_x < deep_.width() && old_y < deep_.height();
      std::array<float, 4> value{};
      if (inside && load_pixel(after.format(), after.pixel(x, y)) ==
                        load_pixel(narrowed_before_.format(), narrowed_before_.pixel(old_x, old_y))) {
        value = load_pixel(deep_.format(), deep_.pixel(old_x, old_y));
      } else {
        value = widened(x, y);
      }
      store_pixel(result.format(), result.pixel(x, y), value);
    }
  }
  layer_.pixels() = std::move(result);
  if (changed_) {
    changed_();
  }
}

void blend_mask_at(PixelFormat format, std::uint8_t* px, std::uint8_t value, float coverage) {
  if (format.bit_depth == BitDepth::UInt8) {
    *px = blend_mask_value(*px, value, coverage);
    return;
  }
  coverage = std::clamp(coverage, 0.0F, 1.0F);
  const auto current = mask_sample_at(format, px);
  const auto blended = static_cast<float>(value) * coverage + current * (1.0F - coverage);
  if (format.bit_depth == BitDepth::UInt16) {
    const auto stored = static_cast<std::uint16_t>(std::clamp(std::lround(blended * 257.0F), 0L, 65535L));
    std::memcpy(px, &stored, sizeof(stored));
  } else {
    const auto stored = std::clamp(blended / 255.0F, 0.0F, 1.0F);
    std::memcpy(px, &stored, sizeof(stored));
  }
}

float mask_sample_at(PixelFormat format, const std::uint8_t* px) {
  switch (format.bit_depth) {
    case BitDepth::UInt8:
      return static_cast<float>(*px);
    case BitDepth::UInt16: {
      std::uint16_t value = 0;
      std::memcpy(&value, px, sizeof(value));
      return static_cast<float>(value) / 257.0F;
    }
    case BitDepth::Float32: {
      float value = 0.0F;
      std::memcpy(&value, px, sizeof(value));
      return std::clamp(std::isfinite(value) ? value : 0.0F, 0.0F, 1.0F) * 255.0F;
    }
  }
  return 0.0F;
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

std::array<double, 3> healing_ring_tone(const QImage& snapshot, QPoint center, int radius) {
  constexpr std::array<std::array<int, 2>, 8> kDirections{{
      {{-1, -1}}, {{0, -1}}, {{1, -1}}, {{-1, 0}},
      {{1, 0}},   {{-1, 1}}, {{0, 1}},  {{1, 1}},
  }};
  std::array<double, 3> sum{};
  double alpha_weight = 0.0;
  for (const auto& direction : kDirections) {
    const auto x = std::clamp(center.x() + direction[0] * radius, 0, snapshot.width() - 1);
    const auto y = std::clamp(center.y() + direction[1] * radius, 0, snapshot.height() - 1);
    const auto* pixel = snapshot.constScanLine(y) + static_cast<std::size_t>(x) * 4U;
    const auto alpha = static_cast<double>(pixel[3]) / 255.0;
    alpha_weight += alpha;
    for (std::size_t channel = 0; channel < sum.size(); ++channel) {
      sum[channel] += static_cast<double>(pixel[channel]) * alpha;
    }
  }
  if (alpha_weight > std::numeric_limits<double>::epsilon()) {
    for (auto& channel : sum) {
      channel /= alpha_weight;
    }
    return sum;
  }

  const auto x = std::clamp(center.x(), 0, snapshot.width() - 1);
  const auto y = std::clamp(center.y(), 0, snapshot.height() - 1);
  const auto* pixel = snapshot.constScanLine(y) + static_cast<std::size_t>(x) * 4U;
  return {static_cast<double>(pixel[0]), static_cast<double>(pixel[1]), static_cast<double>(pixel[2])};
}

std::array<std::uint8_t, 4> healing_sample(const QImage& snapshot, QPoint source, QPoint destination,
                                           int tone_radius) {
  const auto source_tone = healing_ring_tone(snapshot, source, tone_radius);
  const auto destination_tone = healing_ring_tone(snapshot, destination, tone_radius);
  const auto* source_pixel = snapshot.constScanLine(source.y()) + static_cast<std::size_t>(source.x()) * 4U;
  std::array<std::uint8_t, 4> result{};
  for (std::size_t channel = 0; channel < 3; ++channel) {
    // Classic frequency-separation healing: carry sampled detail into the
    // destination's local tone. This is deliberately a fixed local operation,
    // not patch search, synthesis, or a gradient-domain optimization.
    result[channel] = clamp_byte(destination_tone[channel] + static_cast<double>(source_pixel[channel]) -
                                 source_tone[channel]);
  }
  result[3] = source_pixel[3];
  return result;
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

QImage active_layer_sample_image(const Layer& layer, QSize document_size) {
  return active_layer_sample_region(layer, QRect(QPoint(), document_size));
}

QImage active_layer_sample_region(const Layer& layer, QRect region) {
  QImage image(region.size(), QImage::Format_RGBA8888);
  image.fill(Qt::transparent);
  if (region.isEmpty() || !layer.visible() || layer.opacity() <= 0.0F) {
    return image;
  }

  const auto& pixels = layer.pixels();
  if (pixels.empty() || pixels.format().channels < 3) {
    return image;
  }
  if (pixels.format().bit_depth != BitDepth::UInt8) {
    // 16/32-bit: sample the display bytes (the picker reads 8-bit colors).
    const auto deep_bounds = layer_pixel_bounds(layer);
    const auto deep_rect = to_qrect(deep_bounds).intersected(region);
    for (int y = deep_rect.top(); y <= deep_rect.bottom() && !deep_rect.isEmpty(); ++y) {
      auto* output = image.scanLine(y - region.top()) + static_cast<std::size_t>(deep_rect.left() - region.left()) * 4U;
      for (int x = deep_rect.left(); x <= deep_rect.right(); ++x) {
        const auto bytes = display_rgba8_at(pixels, x - deep_bounds.x, y - deep_bounds.y);
        const auto alpha = static_cast<float>(bytes[3]) / 255.0F * layer_mask_alpha_at(layer, x, y) * layer.opacity();
        if (alpha > 0.0F) {
          output[0] = bytes[0];
          output[1] = bytes[1];
          output[2] = bytes[2];
          output[3] = clamp_byte(alpha * 255.0F);
        }
        output += 4;
      }
    }
    return image;
  }

  const auto bounds = layer_pixel_bounds(layer);
  const auto draw_rect = to_qrect(bounds).intersected(region);
  if (draw_rect.isEmpty()) {
    return image;
  }

  const auto channels = pixels.format().channels;
  for (int y = draw_rect.top(); y <= draw_rect.bottom(); ++y) {
    auto* output = image.scanLine(y - region.top()) + static_cast<std::size_t>(draw_rect.left() - region.left()) * 4U;
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

void apply_fill_settings(EditOptions& options, const CanvasWidget& canvas) {
  constexpr double kFillMaxFeatherPixels = 50.0;
  options.primary.a = static_cast<std::uint8_t>(std::clamp(
      std::lround(static_cast<double>(options.primary.a) * std::clamp(canvas.fill_opacity(), 1, 100) / 100.0), 1L,
      255L));
  options.fill_softness_feather = std::clamp(canvas.fill_softness(), 0, 100) / 100.0 * kFillMaxFeatherPixels;
  options.flood_tolerance = std::clamp(canvas.fill_tolerance(), 0, 255);
  options.flood_contiguous = canvas.fill_contiguous();
}

EditOptions edit_options(QColor primary, QColor secondary, int brush_size, int brush_opacity, int brush_softness,
                         bool fill_shapes, bool lock_transparent_pixels, const CanvasWidget& canvas,
                         int brush_roundness, double brush_angle_degrees) {
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
  options.palette_snap = canvas.palette_snap_for_edits();
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
    // Both per-pixel lookups go through selection_alpha_at, which rasterizes a
    // many-span region once; QRegion::contains would scan every span per pixel.
    options.selection_mask = [&canvas](std::int32_t x, std::int32_t y) {
      return canvas.selection_alpha_at(QPoint(x, y)) != 0U;
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

namespace {

QImage box_blur_mask(const QImage& source, int radius) {
  if (source.isNull() || source.format() != QImage::Format_Grayscale8 || radius <= 0) {
    return source;
  }

  const auto width = source.width();
  const auto height = source.height();
  if (width <= 0 || height <= 0) {
    return source;
  }

  radius = std::clamp(radius, 0, kMaxSelectionFeatherRadius);
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
  return std::max(1, (std::clamp(feather_radius, 0, kMaxSelectionFeatherRadius) + 1) / 2);
}

}  // namespace

QRect normalized_rect(QPoint a, QPoint b) {
  return QRect(a, b).normalized();
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

constexpr double kPixelAlignedZoom = 1.0;
// The deep-zoom renderer, and with it the pixel grid (GitHub issue 239), starts at 600%:
// there each document pixel spans at least six screen pixels, so a one-pixel line between
// pixels separates them without hiding them.
constexpr double kDeepZoomPixelRendererZoom = 6.0;

double grid_cycle_pixels(std::int32_t cycle_32) noexcept {
  return static_cast<double>(std::max<std::int32_t>(1, cycle_32)) / 32.0;
}

bool uses_pixel_aligned_view(double zoom) noexcept {
  return zoom >= kPixelAlignedZoom;
}

bool uses_deep_zoom_pixel_renderer(double zoom) noexcept {
  return zoom >= kDeepZoomPixelRendererZoom;
}

bool zoom_trace_enabled() {
  static const bool enabled = qEnvironmentVariableIsSet("PATCHY_ZOOM_TRACE");
  return enabled;
}

// The display interpolation rule, in view zoom (GitHub issue 210). Zoomed out the
// canvas always interpolates. Between 100% and 400% nearest-neighbor makes document
// pixels alternate between two screen sizes (1 and 2 screen pixels at 150%), which
// draws edges as irregular stairs, so fractional zooms there interpolate. Whole zooms
// (100%, 200%, 300%) map each pixel to an exact block and stay crisp, as does
// everything from 400% up, where pixels read as blocks and users edit them one by one.
constexpr double kSmoothMagnificationLimit = 4.0;
constexpr double kWholeZoomTolerance = 0.001;

bool uses_smooth_display_scaling(double zoom, bool deep_pixel_renderer) noexcept {
  if (deep_pixel_renderer || !std::isfinite(zoom)) {
    return false;
  }
  if (zoom < 1.0) {
    return true;
  }
  if (zoom >= kSmoothMagnificationLimit) {
    return false;
  }
  return std::abs(zoom - std::round(zoom)) > kWholeZoomTolerance;
}

QRgb pixel_grid_line_color(QRgb pixel) noexcept {
  // The pixel as it shows over a light backdrop (the transparency checkerboard's tone),
  // then pushed toward black when light or toward white when dark, which keeps its hue:
  // white gets a 200 gray line, black a 63 gray one.
  constexpr int kBackdrop = 224;
  const int uncovered = 255 - qAlpha(pixel);
  auto red = qRed(pixel) + uncovered * kBackdrop / 255;
  auto green = qGreen(pixel) + uncovered * kBackdrop / 255;
  auto blue = qBlue(pixel) + uncovered * kBackdrop / 255;
  if ((54 * red + 183 * green + 19 * blue) >> 8 >= 128) {
    red -= red * 56 / 256;
    green -= green * 56 / 256;
    blue -= blue * 56 / 256;
  } else {
    red += (255 - red) * 64 / 256;
    green += (255 - green) * 64 / 256;
    blue += (255 - blue) * 64 / 256;
  }
  return qRgb(red, green, blue);
}

QImage deep_zoom_raster(const QImage& source, QPoint document_origin, std::span<const int> edges_x,
                        std::span<const int> edges_y, double device_pixel_ratio, bool pixel_grid,
                        QPoint& device_origin) {
  const auto columns = static_cast<int>(edges_x.size()) - 1;
  const auto rows = static_cast<int>(edges_y.size()) - 1;
  if (columns <= 0 || rows <= 0 || source.width() < columns || source.height() < rows ||
      !(device_pixel_ratio > 0.0)) {
    return {};
  }
  // qRound is the rounding an aliased fillRect of the logical rect gets, so every
  // overlay drawn at widget_position lines up with these cells.
  const auto to_device = [device_pixel_ratio](int logical) { return qRound(logical * device_pixel_ratio); };
  const auto left = to_device(edges_x.front());
  const auto top = to_device(edges_y.front());
  const auto width = to_device(edges_x.back()) - left;
  const auto height = to_device(edges_y.back()) - top;
  if (width <= 0 || height <= 0) {
    return {};
  }
  // Device column -> source column, and whether it is the first device column of its
  // cell (a grid line; none along document column 0). Rows likewise.
  const auto map_axis = [&](std::span<const int> edges, int start, int count, int document_start,
                            std::vector<int>& cell_of, std::vector<char>& line) {
    cell_of.assign(static_cast<std::size_t>(count), 0);
    line.assign(static_cast<std::size_t>(count), 0);
    for (std::size_t cell = 0; cell + 1 < edges.size(); ++cell) {
      const auto from = std::clamp(to_device(edges[cell]) - start, 0, count);
      const auto to = std::clamp(to_device(edges[cell + 1]) - start, 0, count);
      for (auto device = from; device < to; ++device) {
        cell_of[static_cast<std::size_t>(device)] = static_cast<int>(cell);
      }
      if (from < to && pixel_grid && document_start + static_cast<int>(cell) != 0) {
        line[static_cast<std::size_t>(from)] = 1;
      }
    }
  };
  std::vector<int> column_of;
  std::vector<char> column_line;
  std::vector<int> row_of;
  std::vector<char> row_line;
  map_axis(edges_x, left, width, document_origin.x(), column_of, column_line);
  map_axis(edges_y, top, height, document_origin.y(), row_of, row_line);

  const auto pixels = source.format() == QImage::Format_ARGB32_Premultiplied
                          ? source
                          : source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
  QImage raster(width, height, QImage::Format_ARGB32_Premultiplied);
  std::vector<QRgb> tones(static_cast<std::size_t>(columns));
  int toned_row = -1;
  for (int y = 0; y < height; ++y) {
    const auto row = row_of[static_cast<std::size_t>(y)];
    const auto* in = reinterpret_cast<const QRgb*>(pixels.constScanLine(row));
    auto* out = reinterpret_cast<QRgb*>(raster.scanLine(y));
    if (pixel_grid && toned_row != row) {
      for (int column = 0; column < columns; ++column) {
        tones[static_cast<std::size_t>(column)] = pixel_grid_line_color(in[column]);
      }
      toned_row = row;
    }
    if (row_line[static_cast<std::size_t>(y)] != 0) {
      for (int x = 0; x < width; ++x) {
        out[x] = tones[static_cast<std::size_t>(column_of[static_cast<std::size_t>(x)])];
      }
      continue;
    }
    for (int x = 0; x < width; ++x) {
      const auto column = static_cast<std::size_t>(column_of[static_cast<std::size_t>(x)]);
      out[x] = column_line[static_cast<std::size_t>(x)] != 0 ? tones[column] : in[column];
    }
  }
  device_origin = QPoint(left, top);
  return raster;
}

QString default_new_layer_name(const Document& document) {
  std::set<std::string> existing_names;
  std::function<void(const std::vector<Layer>&)> collect_names = [&](const std::vector<Layer>& layers) {
    for (const auto& layer : layers) {
      existing_names.insert(layer.name());
      collect_names(layer.children());
    }
  };
  collect_names(document.layers());

  int suffix = static_cast<int>(document.layers().size()) + 1;
  QString name;
  do {
    name = QObject::tr("Layer %1").arg(suffix++);
  } while (existing_names.contains(name.toStdString()));
  return name;
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

int preview_composite_level_for_zoom(double zoom) noexcept {
  // The display mip level, clamped: previews composite at exactly the
  // resolution the display path would downscale a full-res composite to, so
  // scaled previews carry no extra upscaling blur - only the (preview-only)
  // composite-order divergence of blending downscaled sources. The clamp
  // bounds the scaled-document build cost at extreme zoom-out.
  return std::min(display_mip_level_for_zoom(zoom), 3);
}

namespace {

// How one display-mip step averages a 4-byte pixel format: straight-alpha
// formats weight their color by alpha (the premultiplied mean, unpremultiplied
// once); premultiplied and opaque formats average every byte as it is.
struct DisplayMipLayout {
  bool straight_alpha{false};
  int alpha_index{3};
};

std::optional<DisplayMipLayout> display_mip_layout(QImage::Format format) noexcept {
  switch (format) {
    case QImage::Format_RGBA8888:
      return DisplayMipLayout{true, 3};
    case QImage::Format_ARGB32:
      return DisplayMipLayout{true, QSysInfo::ByteOrder == QSysInfo::LittleEndian ? 3 : 0};
    case QImage::Format_RGBA8888_Premultiplied:
    case QImage::Format_ARGB32_Premultiplied:
    case QImage::Format_RGBX8888:
    case QImage::Format_RGB32:
      return DisplayMipLayout{};
    default:
      return std::nullopt;
  }
}

void average_display_mip_pixel(const std::array<const std::uint8_t*, 4>& block, int count,
                               DisplayMipLayout layout, std::uint8_t* out) noexcept {
  const auto half = static_cast<std::uint32_t>(count / 2);
  if (!layout.straight_alpha) {
    for (int channel = 0; channel < 4; ++channel) {
      std::uint32_t sum = 0;
      for (int i = 0; i < count; ++i) {
        sum += block[static_cast<std::size_t>(i)][channel];
      }
      out[channel] = static_cast<std::uint8_t>((sum + half) / static_cast<std::uint32_t>(count));
    }
    return;
  }
  std::uint32_t alpha_sum = 0;
  for (int i = 0; i < count; ++i) {
    alpha_sum += block[static_cast<std::size_t>(i)][layout.alpha_index];
  }
  out[layout.alpha_index] = static_cast<std::uint8_t>((alpha_sum + half) / static_cast<std::uint32_t>(count));
  for (int channel = 0; channel < 4; ++channel) {
    if (channel == layout.alpha_index) {
      continue;
    }
    if (alpha_sum == 0) {
      out[channel] = 0;
      continue;
    }
    std::uint32_t weighted = 0;
    for (int i = 0; i < count; ++i) {
      const auto* pixel = block[static_cast<std::size_t>(i)];
      weighted += static_cast<std::uint32_t>(pixel[channel]) * pixel[layout.alpha_index];
    }
    out[channel] = static_cast<std::uint8_t>((weighted + alpha_sum / 2) / alpha_sum);
  }
}

// Writes target_rect of target from source; source and target share a format
// that display_mip_layout supports.
void halve_display_mip_rows(const QImage& source, QImage& target, QRect target_rect, DisplayMipLayout layout) {
  const int source_width = source.width();
  const int source_height = source.height();
  for (int y = target_rect.top(); y <= target_rect.bottom(); ++y) {
    const int top = y * 2;
    const bool two_rows = top + 1 < source_height;
    const auto* row0 = source.constScanLine(top);
    const auto* row1 = source.constScanLine(two_rows ? top + 1 : top);
    auto* out = target.scanLine(y);
    for (int x = target_rect.left(); x <= target_rect.right(); ++x) {
      const int left = x * 2;
      const bool two_columns = left + 1 < source_width;
      std::array<const std::uint8_t*, 4> block{};
      int count = 0;
      block[static_cast<std::size_t>(count++)] = row0 + static_cast<std::ptrdiff_t>(left) * 4;
      if (two_columns) {
        block[static_cast<std::size_t>(count++)] = row0 + static_cast<std::ptrdiff_t>(left + 1) * 4;
      }
      if (two_rows) {
        block[static_cast<std::size_t>(count++)] = row1 + static_cast<std::ptrdiff_t>(left) * 4;
        if (two_columns) {
          block[static_cast<std::size_t>(count++)] = row1 + static_cast<std::ptrdiff_t>(left + 1) * 4;
        }
      }
      average_display_mip_pixel(block, count, layout, out + static_cast<std::ptrdiff_t>(x) * 4);
    }
  }
}

}  // namespace

QImage halved_display_mip(const QImage& source) {
  if (source.isNull()) {
    return {};
  }
  const auto layout = display_mip_layout(source.format());
  if (!layout.has_value()) {
    return halved_display_mip(source.convertToFormat(QImage::Format_ARGB32_Premultiplied));
  }
  QImage target(std::max(1, (source.width() + 1) / 2), std::max(1, (source.height() + 1) / 2), source.format());
  if (target.isNull()) {
    return {};
  }
  halve_display_mip_rows(source, target, target.rect(), *layout);
  return target;
}

QRect halved_display_mip_rect(QRect source_rect) noexcept {
  if (source_rect.isEmpty()) {
    return {};
  }
  return QRect(QPoint(source_rect.left() >> 1, source_rect.top() >> 1),
               QPoint(source_rect.right() >> 1, source_rect.bottom() >> 1));
}

void refresh_halved_display_mip(const QImage& source, QImage& target, QRect target_rect) {
  target_rect = target_rect.intersected(target.rect());
  if (source.isNull() || target_rect.isEmpty() ||
      target.size() != QSize(std::max(1, (source.width() + 1) / 2), std::max(1, (source.height() + 1) / 2))) {
    return;
  }
  if (const auto layout = display_mip_layout(source.format());
      layout.has_value() && target.format() == source.format()) {
    halve_display_mip_rows(source, target, target_rect, *layout);
    return;
  }
  // An unsupported source went through ARGB32_Premultiplied: convert only the
  // block rows and columns this rect reads, then copy the result in.
  const QRect source_rect = QRect(target_rect.left() * 2, target_rect.top() * 2, target_rect.width() * 2,
                                  target_rect.height() * 2)
                                .intersected(source.rect());
  const auto partial = halved_display_mip(source.copy(source_rect));
  if (partial.isNull() || partial.format() != target.format()) {
    return;
  }
  for (int y = 0; y < target_rect.height(); ++y) {
    std::memcpy(target.scanLine(target_rect.top() + y) + static_cast<std::ptrdiff_t>(target_rect.left()) * 4,
                partial.constScanLine(y), static_cast<std::size_t>(target_rect.width()) * 4U);
  }
}

QRect rect_aligned_to_mip_grid(QRect rect, int level) noexcept {
  const int block = 1 << level;
  const int left = (rect.left() / block) * block;
  const int top = (rect.top() / block) * block;
  const int right = ((rect.right() / block) + 1) * block - 1;
  const int bottom = ((rect.bottom() / block) + 1) * block - 1;
  return QRect(QPoint(left, top), QPoint(right, bottom));
}

double next_zoom_ladder_step(double view_zoom, bool zoom_in) noexcept {
  // Photoshop's ladder, as fractions: 0.56%, 1%, 1.5%, 2%, 3%, 4%, 5%, 6.25%,
  // 8.33%, 12.5%, 16.67%, 25%, 33.33%, 50%, 66.67%, 100%, 200% ... 800%, 1200%,
  // 1600%, 3200%, 6400%, 12800%. Below 0.56%, down to the 0.01% floor, Patchy's own
  // 1-2-5 rungs.
  static constexpr double kLadder[] = {0.0001, 0.0002, 0.0005, 0.001, 0.002, 0.003, 0.004,
                                       0.0056, 0.01,      0.015,     0.02,      0.03,      0.04,      0.05,
                                       0.0625, 1.0 / 12.0, 0.125,    1.0 / 6.0, 0.25,      1.0 / 3.0, 0.5,
                                       2.0 / 3.0, 1.0,    2.0,       3.0,       4.0,       5.0,       6.0,
                                       7.0,    8.0,       12.0,      16.0,      32.0,      64.0,      128.0};
  constexpr double kOnRungTolerance = 0.005;  // relative
  if (!(view_zoom > 0.0) || !std::isfinite(view_zoom)) {
    return 1.0;
  }
  if (zoom_in) {
    const auto floor = view_zoom * (1.0 + kOnRungTolerance);
    for (const auto rung : kLadder) {
      if (rung > floor) {
        return rung;
      }
    }
    return kLadder[std::size(kLadder) - 1];
  }
  const auto ceiling = view_zoom * (1.0 - kOnRungTolerance);
  for (auto index = std::size(kLadder); index-- > 0;) {
    if (kLadder[index] < ceiling) {
      return kLadder[index];
    }
  }
  return kLadder[0];
}

QRect center_anchored_handle_rect(QRect start, bool moves_x, bool moves_y, QPoint point,
                                  double target_ratio) noexcept {
  const double center_x = start.x() + start.width() / 2.0;
  const double center_y = start.y() + start.height() / 2.0;
  double half_width = moves_x ? std::abs(point.x() - center_x) : start.width() / 2.0;
  double half_height = moves_y ? std::abs(point.y() - center_y) : start.height() / 2.0;
  half_width = std::max(0.5, half_width);
  half_height = std::max(0.5, half_height);
  if (target_ratio > 0.0) {
    if (moves_x && moves_y) {
      if (half_width / half_height > target_ratio) {
        half_width = half_height * target_ratio;
      } else {
        half_height = half_width / target_ratio;
      }
    } else if (moves_x) {
      half_height = half_width / target_ratio;
    } else {
      half_width = half_height * target_ratio;
    }
  }
  const auto width = std::max(1, static_cast<int>(std::lround(half_width * 2.0)));
  const auto height = std::max(1, static_cast<int>(std::lround(half_height * 2.0)));
  return QRect(static_cast<int>(std::lround(center_x - width / 2.0)),
               static_cast<int>(std::lround(center_y - height / 2.0)), width, height);
}

QRect preview_scaled_document_rect(QRect rect, int level) noexcept {
  if (rect.isEmpty() || level <= 0) {
    return rect;
  }
  const int scale = 1 << level;
  const int x0 = rect.x() >> level;
  const int y0 = rect.y() >> level;
  const int x1 = (rect.x() + rect.width() + scale - 1) >> level;
  const int y1 = (rect.y() + rect.height() + scale - 1) >> level;
  return QRect(x0, y0, x1 - x0, y1 - y0);
}

int live_preview_frame_latch_ms() noexcept {
  constexpr int kLivePreviewFrameLatchMs = 100;
  bool ok = false;
  const auto value = qEnvironmentVariableIntValue("PATCHY_MOVE_LIVE_LATCH_MS", &ok);
  return ok ? std::max(0, value) : kLivePreviewFrameLatchMs;
}

void paint_selection_mode_badge(QPainter& painter, CanvasWidget::SelectionMode mode, QPointF center) {
  using SelectionMode = CanvasWidget::SelectionMode;
  if (mode == SelectionMode::Replace) {
    return;
  }
  constexpr double kGlyphHalf = 3.0;
  const auto stroke = [&](const QColor& color, double width) {
    painter.setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap));
    switch (mode) {
      case SelectionMode::Add:
        painter.drawLine(center + QPointF(-kGlyphHalf, 0.0), center + QPointF(kGlyphHalf, 0.0));
        painter.drawLine(center + QPointF(0.0, -kGlyphHalf), center + QPointF(0.0, kGlyphHalf));
        break;
      case SelectionMode::Subtract:
        painter.drawLine(center + QPointF(-kGlyphHalf, 0.0), center + QPointF(kGlyphHalf, 0.0));
        break;
      case SelectionMode::Intersect:
        painter.drawLine(center + QPointF(-kGlyphHalf, -kGlyphHalf), center + QPointF(kGlyphHalf, kGlyphHalf));
        painter.drawLine(center + QPointF(-kGlyphHalf, kGlyphHalf), center + QPointF(kGlyphHalf, -kGlyphHalf));
        break;
      case SelectionMode::Replace:
        break;
    }
  };
  stroke(kSelectionCursorHalo, kSelectionCursorHaloWidth);
  stroke(kSelectionCursorInk, kSelectionCursorWidth);
}

bool tool_uses_alt_left_for_color_pick(CanvasTool tool) noexcept {
  // Rectangle/Ellipse are deliberately absent: Alt means draw-from-center there
  // (Photoshop parity), and the temporary-eyedropper cursor would fight it.
  switch (tool) {
    case CanvasTool::Brush:
    case CanvasTool::MixerBrush:
    case CanvasTool::PatternStamp:
    case CanvasTool::Smudge:
    case CanvasTool::Eraser:
    case CanvasTool::Gradient:
    case CanvasTool::Line:
    case CanvasTool::Fill:
      return true;
    default:
      return false;
  }
}

QPoint clamped_document_point(const Document& document, QPoint point) {
  return QPoint(std::clamp(point.x(), 0, std::max(0, document.width() - 1)),
                std::clamp(point.y(), 0, std::max(0, document.height() - 1)));
}

// The tools whose Size/Soft options-bar controls apply; these accept the
// Photoshop-style Alt+Right-drag brush resize gesture.
bool tool_supports_brush_adjust_drag(CanvasTool tool) noexcept {
  switch (tool) {
    case CanvasTool::Brush:
    case CanvasTool::MixerBrush:
    case CanvasTool::PatternStamp:
    case CanvasTool::Clone:
    case CanvasTool::Healing:
    case CanvasTool::SpotHealing:
    case CanvasTool::Smudge:
    case CanvasTool::Dodge:
    case CanvasTool::Burn:
    case CanvasTool::Sponge:
    case CanvasTool::BlurBrush:
    case CanvasTool::SharpenBrush:
    case CanvasTool::Eraser:
    case CanvasTool::Line:
    case CanvasTool::Rectangle:
    case CanvasTool::Ellipse:
      return true;
    default:
      return false;
  }
}

bool tool_uses_brush_footprint_cursor(CanvasTool tool) noexcept {
  switch (tool) {
    case CanvasTool::Brush:
    case CanvasTool::MixerBrush:
    case CanvasTool::PatternStamp:
    case CanvasTool::Clone:
    case CanvasTool::Healing:
    case CanvasTool::SpotHealing:
    case CanvasTool::Smudge:
    case CanvasTool::Dodge:
    case CanvasTool::Burn:
    case CanvasTool::Sponge:
    case CanvasTool::BlurBrush:
    case CanvasTool::SharpenBrush:
    case CanvasTool::Eraser:
      return true;
    default:
      return false;
  }
}

bool tool_paints_with_brush_tip(CanvasTool tool) noexcept {
  switch (tool) {
    case CanvasTool::Brush:
    case CanvasTool::MixerBrush:
    case CanvasTool::PatternStamp:
    case CanvasTool::Eraser:
      return true;
    default:
      return false;
  }
}

}  // namespace patchy::ui
