// Helpers shared by the canvas_widget_*.cpp translation units (see
// canvas_widget_shared.hpp). Pure function moves from canvas_widget.cpp;
// behavior must stay identical.

#include "ui/canvas_widget_shared.hpp"

#include "core/layer_metadata.hpp"
#include "core/layer_render_utils.hpp"
#include "core/smart_filter.hpp"
#include "core/smart_object.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/edit_conversions.hpp"
#include "ui/qt_geometry.hpp"

#include <QImage>
#include <QPainter>
#include <QPen>
#include <QRegion>

#include <algorithm>
#include <cmath>
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
  const auto& pixels = layer.pixels();
  return !pixels.empty() && pixels.format().bit_depth == BitDepth::UInt8 && pixels.format().channels >= 3;
}

bool move_layer_requires_smart_filter_rerender(const Layer& layer) {
  const auto* stack = layer.smart_filter_stack();
  return layer_is_smart_object(layer) && stack != nullptr &&
         stack->support == SmartFilterStackSupport::Supported;
}

std::optional<QRect> opaque_pixel_local_rect(const Layer& layer) {
  const auto& pixels = layer.pixels();
  if (pixels.empty() || pixels.format().bit_depth != BitDepth::UInt8 || pixels.format().channels < 3) {
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
constexpr double kDeepZoomPixelRendererZoom = 8.0;

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
    case CanvasTool::Clone:
    case CanvasTool::Healing:
    case CanvasTool::Smudge:
    case CanvasTool::Eraser:
    case CanvasTool::Line:
    case CanvasTool::Rectangle:
    case CanvasTool::Ellipse:
      return true;
    default:
      return false;
  }
}

}  // namespace patchy::ui
