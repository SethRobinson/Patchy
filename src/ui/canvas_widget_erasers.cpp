// CanvasWidget's Background Eraser and Magic Eraser (GitHub issue 727, docs/eraser-tools.md).
//
// The Background Eraser is a brush: its stroke is the Color Replacement tool's
// (color_match_brush_segment in canvas_widget_color_replace.cpp), with each dab erasing the
// pixels that match one sampled color instead of repainting them (core/background_eraser.hpp).
// This file holds its options and its script stroke. The Magic Eraser is one click that erases
// the region the Fill tool would flood from that point (core magic_erase).
//
// Both follow the Eraser's rule under a transparency lock (a locked Background, Lock
// Transparent Pixels): the pixels move toward the background color and keep their alpha. An
// unlocked layer without alpha gains one and erases to transparency (docs/tools.md).
//
// Legal boundary (docs/legal-constraints-selection.md, "Color Replacement tool"): the Background Eraser
// stays in the form published in 1999. One sampled point in display RGB with the Magic Wand's
// tolerance metric, the pixels erased at once. Never a Lab point or area model, a Gaussian
// around the sample, histogram normalization, a stored spatial or range mask, an erase that
// can be adjusted afterwards, or a color correction of the pixels that stay.

#include "ui/canvas_widget.hpp"

#include "core/background_eraser.hpp"
#include "core/hdr_color.hpp"
#include "core/layer.hpp"
#include "core/pixel_tools.hpp"
#include "ui/canvas_widget_shared.hpp"
#include "ui/qt_geometry.hpp"

#include <QColor>
#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QRegion>
#include <QString>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace patchy::ui {

void CanvasWidget::set_background_eraser_options(const patchy::BackgroundEraserOptions& options) noexcept {
  background_eraser_options_ = options;
  background_eraser_options_.tolerance =
      std::clamp(options.tolerance, patchy::kMinColorMatchTolerance, patchy::kMaxColorMatchTolerance);
}

const patchy::BackgroundEraserOptions& CanvasWidget::background_eraser_options() const noexcept {
  return background_eraser_options_;
}

void CanvasWidget::set_background_eraser_sampling(patchy::ColorMatchSampling sampling) noexcept {
  background_eraser_sampling_ = sampling;
}

patchy::ColorMatchSampling CanvasWidget::background_eraser_sampling() const noexcept {
  return background_eraser_sampling_;
}

void CanvasWidget::set_magic_eraser_options(const MagicEraserOptions& options) noexcept {
  magic_eraser_options_ = options;
  magic_eraser_options_.tolerance = std::clamp(options.tolerance, 0, 255);
  magic_eraser_options_.opacity = std::clamp(options.opacity, 0, 100);
}

const CanvasWidget::MagicEraserOptions& CanvasWidget::magic_eraser_options() const noexcept {
  return magic_eraser_options_;
}

// The tool for scripts (layer.eraseBackground): one stroke through `points` (document pixels)
// with a procedural round tip, on the active pixel layer. The caller owns the history entry.
CanvasWidget::EraserToolResult CanvasWidget::apply_background_eraser_stroke(const BackgroundEraserScriptStroke& stroke) {
  EraserToolResult result;
  if (document_ == nullptr) {
    result.error = tr("%1 needs an open document").arg(tr("Background Eraser"));
    return result;
  }
  if (editing_grayscale_target()) {
    result.error = tr("%1 is unavailable while editing a mask or channel").arg(tr("Background Eraser"));
    return result;
  }
  auto* layer = can_begin_pixel_edit(/*report=*/false) ? active_pixel_layer() : nullptr;
  if (layer == nullptr || std::as_const(*layer).pixels().empty() ||
      std::as_const(*layer).pixels().format().channels < 3 || stroke.points.empty()) {
    result.error = tr("%1 needs an editable pixel layer").arg(tr("Background Eraser"));
    return result;
  }
  const bool locked = active_layer_locks_transparent_pixels();
  if (!locked && std::as_const(*layer).pixels().format().channels == 3) {
    patchy::ensure_layer_alpha_for_erase(*layer);
  }
  const auto layer_bounds = std::as_const(*layer).bounds();
  const auto coverage = color_match_stroke_coverage(layer_bounds);

  const QColor& background = stroke.background;
  patchy::BackgroundEraserStroke engine(
      std::as_const(*layer).pixels(), stroke.options, stroke.sampling,
      patchy::EditColor{static_cast<std::uint8_t>(background.red()), static_cast<std::uint8_t>(background.green()),
                        static_cast<std::uint8_t>(background.blue()), 255},
      locked);
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

// The Magic Eraser's click (the press in canvas_widget_events.cpp and the script call): the
// Fill tool's flood region, erased. The caller has begun the edit.
QRect CanvasWidget::magic_erase_pixels(QPoint document_point, const MagicEraserOptions& options,
                                       const QColor& background) {
  if (document_ == nullptr || !document_->active_layer_id().has_value() || options.opacity <= 0) {
    return {};  // Opacity 0 erases nothing; the core floors a written opacity at 1/255
  }
  // The Eraser's own options: Opacity rides primary.a, the transparency lock picks between
  // transparency and the background color, and the selection limits the region.
  auto edit = edit_options(primary_color_, background, brush_size_, std::clamp(options.opacity, 1, 100),
                           brush_softness_, false, active_layer_locks_transparent_pixels(), *this);
  edit.flood_tolerance = std::clamp(options.tolerance, 0, 255);
  edit.flood_contiguous = options.contiguous;

  patchy::FloodSampleImage composite;
  if (options.sample_all_layers) {
    // The colors come from the document as it is displayed, as the Magic Wand reads them.
    wait_for_move_commit_job();  // exact pixels: a deferred Move commit may still be rendering
    ensure_render_cache();
    if (render_cache_.isNull() || render_cache_.format() != QImage::Format_RGBA8888 ||
        render_cache_.width() != document_->width() || render_cache_.height() != document_->height()) {
      return {};
    }
    composite = patchy::FloodSampleImage{render_cache_.constBits(), render_cache_.width(), render_cache_.height(),
                                         static_cast<std::size_t>(render_cache_.bytesPerLine())};
  }
  return to_qrect(patchy::magic_erase(*document_, *document_->active_layer_id(), document_point.x(),
                                      document_point.y(), edit, options.anti_alias,
                                      options.sample_all_layers ? &composite : nullptr));
}

// The tool for scripts (layer.magicErase): one click on the active pixel layer. The caller
// owns the history entry.
CanvasWidget::EraserToolResult CanvasWidget::apply_magic_eraser(QPoint document_point,
                                                                const MagicEraserOptions& options,
                                                                const QColor& background) {
  EraserToolResult result;
  if (document_ == nullptr) {
    result.error = tr("%1 needs an open document").arg(tr("Magic Eraser"));
    return result;
  }
  if (editing_grayscale_target()) {
    result.error = tr("%1 is unavailable while editing a mask or channel").arg(tr("Magic Eraser"));
    return result;
  }
  const auto* layer = can_begin_pixel_edit(/*report=*/false) ? active_pixel_layer() : nullptr;
  if (layer == nullptr || layer->pixels().empty() || layer->pixels().format().channels < 3) {
    result.error = tr("%1 needs an editable pixel layer").arg(tr("Magic Eraser"));
    return result;
  }
  result.ok = true;
  result.changed = magic_erase_pixels(document_point, options, background);
  if (!result.changed.isEmpty()) {
    active_edit_target_changed_impl(QRegion(result.changed));
  }
  return result;
}

}  // namespace patchy::ui
