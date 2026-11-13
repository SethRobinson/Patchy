// CanvasWidget's Color Replacement tool (GitHub issue 720, docs/color-replacement.md). A stroke
// is a row of brush dabs a quarter of the brush apart; each dab compares the layer's stroke-start
// pixels under the tip with one sampled color and paints the foreground color over the ones
// that match (core/color_replace.hpp). The pixels are written at once and nothing about the
// stroke is kept afterwards.
//
// Legal boundary (docs/legal-constraints-selection.md, "Color Replacement tool"): one sampled point in
// display RGB with the Magic Wand's tolerance metric. Never a Lab point or area model, a
// Gaussian around the sample, histogram normalization, a stored spatial or range mask, or a
// replacement that can be adjusted after it is painted.

#include "ui/canvas_widget.hpp"

#include "core/color_replace.hpp"
#include "core/hdr_color.hpp"
#include "core/layer.hpp"
#include "core/pixel_tools.hpp"
#include "ui/canvas_widget_shared.hpp"
#include "ui/qt_geometry.hpp"

#include <QColor>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QRegion>
#include <QString>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

[[nodiscard]] patchy::EditColor edit_color_of(const QColor& color) {
  return {static_cast<std::uint8_t>(color.red()), static_cast<std::uint8_t>(color.green()),
          static_cast<std::uint8_t>(color.blue()), 255};
}

// One dab's tip box around `center` (document pixels) and its coverage, row-major.
struct DabFootprint {
  QRect rect;
  std::vector<float> coverage;
};

void fill_dab_footprint(DabFootprint& footprint, QPointF center, double reach, const patchy::EditOptions& options) {
  const auto half = static_cast<int>(std::ceil(reach));
  const auto hot_x = static_cast<int>(std::floor(center.x() + 0.5));
  const auto hot_y = static_cast<int>(std::floor(center.y() + 0.5));
  footprint.rect = QRect(hot_x - half, hot_y - half, 2 * half + 1, 2 * half + 1);
  footprint.coverage.resize(static_cast<std::size_t>(footprint.rect.width()) *
                            static_cast<std::size_t>(footprint.rect.height()));
  auto* out = footprint.coverage.data();
  for (int y = footprint.rect.top(); y <= footprint.rect.bottom(); ++y) {
    for (int x = footprint.rect.left(); x <= footprint.rect.right(); ++x) {
      *out++ = patchy::brush_dab_coverage(static_cast<double>(x) - center.x(), static_cast<double>(y) - center.y(),
                                          options);
    }
  }
}

}  // namespace

void CanvasWidget::set_color_replace_options(const patchy::ColorReplaceOptions& options) noexcept {
  color_replace_options_ = options;
  color_replace_options_.tolerance =
      std::clamp(options.tolerance, patchy::kMinColorReplaceTolerance, patchy::kMaxColorReplaceTolerance);
}

const patchy::ColorReplaceOptions& CanvasWidget::color_replace_options() const noexcept {
  return color_replace_options_;
}

void CanvasWidget::set_color_replace_sampling(patchy::ColorReplaceSampling sampling) noexcept {
  color_replace_sampling_ = sampling;
}

patchy::ColorReplaceSampling CanvasWidget::color_replace_sampling() const noexcept {
  return color_replace_sampling_;
}

// One segment of a Color Replacement stroke (local_adjustment_brush_segment hands it over): the
// first dab at the press, then one every quarter of the brush diameter, measured by distance so
// the pointer's event rate does not matter. Work is bounded to each dab's box.
QRect CanvasWidget::color_replace_brush_segment(QPoint from, QPoint to) {
  auto* layer = active_pixel_layer();
  if (document_ == nullptr || layer == nullptr) {
    return {};
  }
  const auto& start_pixels = std::as_const(*layer).pixels();
  if (start_pixels.empty() || start_pixels.format().channels < 3) {
    return {};
  }
  const auto layer_bounds = std::as_const(*layer).bounds();
  if (!color_replace_stroke_.has_value()) {
    const auto gain = start_pixels.format().bit_depth == BitDepth::Float32
                          ? patchy::color_intensity_gain(paint_intensity_.foreground)
                          : 1.0F;
    color_replace_stroke_.emplace(start_pixels, color_replace_options_, color_replace_sampling_,
                                  edit_color_of(primary_color_), edit_color_of(secondary_color_), gain);
    color_replace_stroke_->set_palette_snap(palette_snap_for_edits());
  }

  // The current tip as the Brush stamps it, without dynamics: a bitmap tip, or the procedural
  // Round or Square under the tip shape's angle and roundness.
  const auto brush = effective_brush_input();
  const auto scaled_tip = scaled_brush_tip_for(brush.size, brush.softness);
  patchy::EditOptions options;
  options.brush_size = brush.size;
  options.brush_softness = brush.softness;
  options.brush_roundness = brush.roundness;
  options.brush_angle_degrees = brush.angle_degrees;
  options.brush_shape = brush_tip_ == nullptr ? brush_shape_ : patchy::BrushShape::Round;
  options.brush_tip = scaled_tip.get();
  auto reach = static_cast<double>(std::max(1, brush.size) / 2) * 1.42 + 2.0;
  if (scaled_tip != nullptr) {
    reach = std::hypot(std::max(scaled_tip->anchor_x, scaled_tip->width - scaled_tip->anchor_x),
                       std::max(scaled_tip->anchor_y, scaled_tip->height - scaled_tip->anchor_y)) +
            2.0;
  }
  const auto spacing = std::max(1.0, static_cast<double>(std::max(1, brush.size)) * 0.25);

  const auto paint_rect =
      QRect(0, 0, document_->width(), document_->height()).intersected(to_qrect(layer_bounds));
  if (paint_rect.isEmpty()) {
    return {};
  }
  // Pixels of the layer outside the canvas stay as they are, and the selection limits the rest.
  const bool selection_active = has_selection();
  const patchy::ColorReplaceStroke::SelectionCoverage coverage = [&](std::int32_t x, std::int32_t y) {
    const QPoint point(x + layer_bounds.x, y + layer_bounds.y);
    if (!paint_rect.contains(point)) {
      return 0.0F;
    }
    if (!selection_active) {
      return 1.0F;
    }
    return selection_allows(point) ? static_cast<float>(selection_alpha_at(point)) / 255.0F : 0.0F;
  };

  auto& pixels = layer->pixels();
  QRect dirty;
  DabFootprint footprint;
  const auto dab = [&](double center_x, double center_y) {
    fill_dab_footprint(footprint, QPointF(center_x, center_y), reach, options);
    if (!footprint.rect.intersects(paint_rect)) {
      return;
    }
    const patchy::Rect rect{footprint.rect.x() - layer_bounds.x, footprint.rect.y() - layer_bounds.y,
                            footprint.rect.width(), footprint.rect.height()};
    const auto hot = footprint.rect.center();
    const auto changed = color_replace_stroke_->dab(pixels, hot.x() - layer_bounds.x, hot.y() - layer_bounds.y, rect,
                                                    footprint.coverage, coverage);
    render_cache_diagnostics_.toning_segment_pixels +=
        static_cast<std::int64_t>(footprint.rect.width()) * footprint.rect.height();
    if (!changed.empty()) {
      dirty = dirty.united(
          QRect(changed.x + layer_bounds.x, changed.y + layer_bounds.y, changed.width, changed.height));
    }
  };

  if (!toning_dab_placed_) {
    dab(static_cast<double>(from.x()), static_cast<double>(from.y()));
    toning_dab_placed_ = true;
    toning_dab_distance_ = 0.0;
  }
  const auto dx = static_cast<double>(to.x() - from.x());
  const auto dy = static_cast<double>(to.y() - from.y());
  const auto length = std::sqrt(dx * dx + dy * dy);
  if (length > 0.0) {
    auto along = spacing - toning_dab_distance_;
    while (along <= length) {
      dab(static_cast<double>(from.x()) + dx * along / length, static_cast<double>(from.y()) + dy * along / length);
      along += spacing;
    }
    toning_dab_distance_ = length - (along - spacing);
  }
  tick_processing_operation();
  return dirty;
}

// The tool for scripts (layer.replaceColor): one stroke through `points` (document pixels) with
// a procedural round tip, on the active pixel layer. The caller owns the history entry.
CanvasWidget::ColorReplaceResult CanvasWidget::apply_color_replace_stroke(const ColorReplaceScriptStroke& stroke) {
  ColorReplaceResult result;
  if (document_ == nullptr) {
    result.error = tr("Color Replacement needs an open document");
    return result;
  }
  if (editing_grayscale_target()) {
    result.error = tr("Color Replacement is unavailable while editing a mask or channel");
    return result;
  }
  if (!can_begin_pixel_edit(/*report=*/false)) {
    result.error = tr("Color Replacement needs an editable pixel layer");
    return result;
  }
  auto* layer = active_pixel_layer();
  if (layer == nullptr || std::as_const(*layer).pixels().empty() ||
      std::as_const(*layer).pixels().format().channels < 3 || stroke.points.empty()) {
    result.error = tr("Color Replacement needs an editable pixel layer");
    return result;
  }
  const auto layer_bounds = std::as_const(*layer).bounds();
  const auto paint_rect =
      QRect(0, 0, document_->width(), document_->height()).intersected(to_qrect(layer_bounds));
  const bool selection_active = has_selection();
  const patchy::ColorReplaceStroke::SelectionCoverage coverage = [&](std::int32_t x, std::int32_t y) {
    const QPoint point(x + layer_bounds.x, y + layer_bounds.y);
    if (!paint_rect.contains(point)) {
      return 0.0F;
    }
    if (!selection_active) {
      return 1.0F;
    }
    return selection_allows(point) ? static_cast<float>(selection_alpha_at(point)) / 255.0F : 0.0F;
  };

  patchy::ColorReplaceStroke engine(std::as_const(*layer).pixels(), stroke.options, stroke.sampling,
                                    edit_color_of(stroke.foreground), edit_color_of(stroke.background));
  engine.set_palette_snap(palette_snap_for_edits());
  auto& pixels = layer->pixels();
  const auto size = std::clamp(stroke.size, 1, kMaxBrushSize);
  const auto softness = std::clamp(stroke.softness, 0, 100);
  const auto spacing = std::max(1.0, static_cast<double>(size) * 0.25);
  patchy::Rect changed;
  const auto dab = [&](QPointF point) {
    const auto dab_changed = engine.round_dab(pixels, point.x() - layer_bounds.x, point.y() - layer_bounds.y, size,
                                              softness, coverage);
    if (dab_changed.empty()) {
      return;
    }
    if (changed.empty()) {
      changed = dab_changed;
      return;
    }
    const auto left = std::min(changed.x, dab_changed.x);
    const auto top = std::min(changed.y, dab_changed.y);
    const auto right = std::max(changed.x + changed.width, dab_changed.x + dab_changed.width);
    const auto bottom = std::max(changed.y + changed.height, dab_changed.y + dab_changed.height);
    changed = {left, top, right - left, bottom - top};
  };
  dab(stroke.points.front());
  double carried = 0.0;
  for (std::size_t index = 1; index < stroke.points.size(); ++index) {
    const auto from = stroke.points[index - 1];
    const auto to = stroke.points[index];
    const auto length = std::hypot(to.x() - from.x(), to.y() - from.y());
    if (length <= 0.0) {
      continue;
    }
    auto along = spacing - carried;
    while (along <= length) {
      dab(from + (to - from) * (along / length));
      along += spacing;
    }
    carried = length - (along - spacing);
  }
  result.ok = true;
  if (!changed.empty()) {
    result.changed = QRect(changed.x + layer_bounds.x, changed.y + layer_bounds.y, changed.width, changed.height);
    active_edit_target_changed_impl(QRegion(result.changed));
  }
  return result;
}

}  // namespace patchy::ui
