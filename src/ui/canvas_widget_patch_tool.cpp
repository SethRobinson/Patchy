// CanvasWidget's Patch tool and Content-Aware Move: draw a freehand outline (the
// shared lasso machinery in the events TU) or use an existing selection, then
// drag the region and commit a heal on release. Both are strictly user-directed:
// the dragged offset is the ONLY source choice for the dragged region, the drag
// shows at most a raw ghost of the frozen snapshot at the pointer offset (never
// anything inside the original spot: Adobe US 9575641 / US 10332291, to 2033),
// Content-Aware Move's hole fill is the shape-derived map of core/spot_heal.hpp,
// and all healing runs ONCE on release using the classic healing membrane of
// the expired US 6587592 (core/heal_membrane.hpp): boundary tone differences interpolated across the
// interior, plus the dragged source texture. No PatchMatch-style offset
// propagation or perturbation, no reshuffling, no gradient-domain compositing
// of source gradients, and no live per-move classification may be added:
// those are claimed by Adobe's active patents (US 8285055, US 8340463,
// US 8355592, into 2031), US 9058699 (to 2029), and US 8050498 (to Nov 3,
// 2029). A content-driven source search, if ever added, must be the exhaustive
// exemplar search docs/legal-constraints.md clears; see the dated records in
// docs/patent-research.md and docs/patent-research-inpainting.md.

#include "ui/canvas_widget.hpp"

#include "core/blend_math.hpp"
#include "core/exemplar_inpaint.hpp"
#include "core/heal_membrane.hpp"
#include "core/layer.hpp"
#include "core/palette.hpp"
#include "core/pixel_buffer.hpp"
#include "core/pixel_tools.hpp"
#include "core/spot_heal.hpp"
#include "core/worker_budget.hpp"
#include "ui/background_workers.hpp"
#include "ui/canvas_types.hpp"
#include "ui/canvas_widget_shared.hpp"
#include "ui/main_window_shared.hpp"
#include "ui/qt_geometry.hpp"
#include "ui/selection_outline.hpp"

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QRegion>
#include <QTransform>
#include <Qt>
#include <QtGlobal>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <future>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

// Once the healed area is at least this large the commit fans out over row
// strips; the per-pixel cost (16 snapshot reads through the ring tones) is
// lower than the transform resampler's, so the gate sits below its 1 Mpx.
constexpr std::int64_t kPatchToolParallelArea = 262'144;

// Content-Aware Move fills the old spot this many pixels past the selection, so the
// object's own fringe (a glow, a soft shadow edge, anti-aliased outline pixels a loose
// selection just misses) is replaced too instead of seeding the fill from the ring.
constexpr int kContentAwareMoveHoleGrow = 6;

// Grows a coverage mask by `radius` cells (a square max filter, rows then columns): the
// grown cells become fully covered, the original coverage is kept.
void grow_hole_mask(std::vector<std::uint8_t>& mask, int width, int height, int radius) {
  std::vector<std::uint8_t> rows(mask.size(), 0U);
  for (int y = 0; y < height; ++y) {
    const auto* in = mask.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
    auto* out = rows.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
    for (int x = 0; x < width; ++x) {
      bool any = false;
      for (int k = std::max(0, x - radius); k <= std::min(width - 1, x + radius) && !any; ++k) {
        any = in[k] != 0U;
      }
      out[x] = any ? 255U : 0U;
    }
  }
  for (int x = 0; x < width; ++x) {
    for (int y = 0; y < height; ++y) {
      bool any = false;
      for (int k = std::max(0, y - radius); k <= std::min(height - 1, y + radius) && !any; ++k) {
        any = rows[static_cast<std::size_t>(k) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] != 0U;
      }
      auto& cell = mask[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
      if (any && cell == 0U) {
        cell = 255U;
      }
    }
  }
}

// Content-Aware Move's New Layer destination: the pixels of `after` that differ from
// `before` (the same layer before the move; a pixel outside `before`'s bounds was
// transparent), on an RGBA buffer of the same depth cropped to the changed pixels.
// Unchanged pixels stay transparent, so the result over the untouched original shows
// exactly the moved state on an opaque layer. False when nothing changed.
bool changed_pixels_of_layer(const PixelBuffer& before, Rect before_bounds, const PixelBuffer& after,
                             Rect after_bounds, PixelBuffer& out, Rect& out_bounds) {
  const auto format = after.format();
  if (before.format() != format || after.empty()) {
    return false;
  }
  const auto pixel_bytes = bytes_per_pixel(format);
  const auto channel_bytes = bytes_per_channel(format.bit_depth);
  const auto changed = [&](std::int32_t x, std::int32_t y) {
    const auto* now = after.pixel(x, y);
    const auto bx = after_bounds.x + x - before_bounds.x;
    const auto by = after_bounds.y + y - before_bounds.y;
    if (bx >= 0 && by >= 0 && bx < before.width() && by < before.height()) {
      return std::memcmp(now, before.pixel(bx, by), pixel_bytes) != 0;
    }
    return std::any_of(now, now + pixel_bytes, [](std::uint8_t byte) { return byte != 0U; });
  };
  std::int32_t left = after.width();
  std::int32_t top = after.height();
  std::int32_t right = -1;
  std::int32_t bottom = -1;
  for (std::int32_t y = 0; y < after.height(); ++y) {
    for (std::int32_t x = 0; x < after.width(); ++x) {
      if (changed(x, y)) {
        left = std::min(left, x);
        right = std::max(right, x);
        top = std::min(top, y);
        bottom = std::max(bottom, y);
      }
    }
  }
  if (right < left || bottom < top) {
    return false;
  }
  auto out_format = format;
  out_format.channels = 4;
  out = PixelBuffer(right - left + 1, bottom - top + 1, out_format);
  out.clear(0);
  out_bounds = Rect{after_bounds.x + left, after_bounds.y + top, right - left + 1, bottom - top + 1};
  const auto color_bytes = channel_bytes * 3U;
  for (std::int32_t y = top; y <= bottom; ++y) {
    for (std::int32_t x = left; x <= right; ++x) {
      if (!changed(x, y)) {
        continue;
      }
      const auto* now = after.pixel(x, y);
      auto* dst = out.pixel(x - left, y - top);
      if (format.channels >= 4) {
        std::memcpy(dst, now, channel_bytes * 4U);
        continue;
      }
      // An alpha-less layer is opaque everywhere.
      std::memcpy(dst, now, color_bytes);
      if (format.bit_depth == BitDepth::Float32) {
        const float one = 1.0F;
        std::memcpy(dst + color_bytes, &one, sizeof(one));
      } else {
        std::memset(dst + color_bytes, 0xFF, channel_bytes);
      }
    }
  }
  return true;
}

}  // namespace

void CanvasWidget::set_patch_tool_mode(PatchToolMode mode) noexcept {
  patch_tool_mode_ = mode;
}

CanvasWidget::PatchToolMode CanvasWidget::patch_tool_mode() const noexcept {
  return patch_tool_mode_;
}

void CanvasWidget::set_patch_tool_transparent(bool enabled) noexcept {
  patch_tool_transparent_ = enabled;
}

bool CanvasWidget::patch_tool_transparent() const noexcept {
  return patch_tool_transparent_;
}

void CanvasWidget::set_content_aware_move_mode(ContentAwareMoveMode mode) noexcept {
  content_aware_move_mode_ = mode;
}

CanvasWidget::ContentAwareMoveMode CanvasWidget::content_aware_move_mode() const noexcept {
  return content_aware_move_mode_;
}

void CanvasWidget::set_content_aware_move_destination(ContentAwareMoveDestination destination) noexcept {
  content_aware_move_destination_ = destination;
}

CanvasWidget::ContentAwareMoveDestination CanvasWidget::content_aware_move_destination() const noexcept {
  return content_aware_move_destination_;
}

void CanvasWidget::set_content_aware_move_object_selection(bool enabled) noexcept {
  content_aware_move_object_selection_ = enabled;
}

bool CanvasWidget::content_aware_move_object_selection() const noexcept {
  return content_aware_move_object_selection_;
}

bool CanvasWidget::begin_patch_tool_drag(QPoint document_point) {
  if (document_ == nullptr || selection_.isEmpty()) {
    return false;
  }
  const QRect canvas_rect(0, 0, document_->width(), document_->height());
  const auto bounds = selection_.boundingRect().intersected(canvas_rect);
  if (bounds.isEmpty()) {
    return false;
  }
  // One frozen snapshot per gesture (the clone/heal rule): the preview blits
  // from it and the commit samples it; nothing recomposites per move.
  patch_tool_source_image_ = retouch_source_snapshot();
  if (patch_tool_source_image_.isNull()) {
    return false;
  }

  // The selection's coverage over its bounds, feather included; this mask is
  // the patch region for the commit and the Destination proxy.
  patch_tool_drag_mask_bounds_ = bounds;
  if (selection_mask_alpha_.isNull()) {
    patch_tool_drag_mask_ = hard_mask_from_region(selection_, bounds);
  } else {
    patch_tool_drag_mask_ = QImage(bounds.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < bounds.height(); ++y) {
      auto* row = patch_tool_drag_mask_.scanLine(y);
      for (int x = 0; x < bounds.width(); ++x) {
        row[x] = selection_alpha_at(QPoint(bounds.left() + x, bounds.top() + y));
      }
    }
  }

  patch_tool_outline_path_ = QPainterPath();
  for (const auto& loop : trace_selection_outlines(selection_)) {
    patch_tool_outline_path_.addPolygon(loop.points);
    patch_tool_outline_path_.closeSubpath();
  }

  // The ghost Patch Destination and Content-Aware Move draw at the pointer offset: the
  // snapshot cut to the soft mask.
  patch_tool_drag_proxy_image_ = QImage(bounds.size(), QImage::Format_RGBA8888);
  patch_tool_drag_proxy_image_.fill(Qt::transparent);
  for (int y = 0; y < bounds.height(); ++y) {
    const auto* mask_row = patch_tool_drag_mask_.constScanLine(y);
    const auto* source_row = patch_tool_source_image_.constScanLine(bounds.top() + y);
    auto* proxy_row = patch_tool_drag_proxy_image_.scanLine(y);
    for (int x = 0; x < bounds.width(); ++x) {
      const auto mask_alpha = mask_row[x];
      if (mask_alpha == 0U) {
        continue;
      }
      const auto* src = source_row + static_cast<std::size_t>(bounds.left() + x) * 4U;
      auto* dst = proxy_row + static_cast<std::size_t>(x) * 4U;
      dst[0] = src[0];
      dst[1] = src[1];
      dst[2] = src[2];
      dst[3] = static_cast<std::uint8_t>(static_cast<unsigned>(src[3]) * mask_alpha / 255U);
    }
  }

  patch_tool_drag_origin_ = document_point;
  patch_tool_drag_delta_ = QPoint(0, 0);
  patch_tool_dragging_ = true;
  patch_tool_drag_content_aware_move_ = tool_ == CanvasTool::ContentAwareMove;
  selection_edges_visible_ = true;
  setCursor(Qt::SizeAllCursor);
  return true;
}

void CanvasWidget::update_patch_tool_drag(QPoint document_point) {
  if (!patch_tool_dragging_ || document_ == nullptr) {
    return;
  }
  const auto clamped = clamped_document_point(*document_, document_point);
  const auto new_delta = clamped - patch_tool_drag_origin_;
  if (new_delta == patch_tool_drag_delta_) {
    return;
  }
  const auto bounds = patch_tool_drag_mask_bounds_;
  const auto dirty_document = bounds.united(bounds.translated(patch_tool_drag_delta_))
                                  .united(bounds.translated(new_delta));
  patch_tool_drag_delta_ = new_delta;
  update(widget_rect_for_document_rect(QRectF(dirty_document)).toAlignedRect().adjusted(-2, -2, 2, 2));
}

void CanvasWidget::release_patch_tool_drag(QPoint document_point) {
  if (!patch_tool_dragging_) {
    return;
  }
  update_patch_tool_drag(document_point);
  // A click (or a sub-drag-threshold wiggle) inside the selection is a no-op:
  // the selection stays, nothing is healed.
  const auto widget_distance =
      static_cast<double>(patch_tool_drag_delta_.manhattanLength()) * std::max(zoom_, 1e-6);
  if (patch_tool_drag_delta_.isNull() || widget_distance < QApplication::startDragDistance()) {
    cancel_patch_tool_drag();
    update();
    return;
  }
  commit_patch_tool_drag();
}

void CanvasWidget::cancel_patch_tool_drag() {
  patch_tool_dragging_ = false;
  patch_tool_drag_content_aware_move_ = false;
  patch_tool_drag_delta_ = QPoint(0, 0);
  patch_tool_source_image_ = QImage();
  patch_tool_drag_mask_ = QImage();
  patch_tool_drag_mask_bounds_ = QRect();
  patch_tool_outline_path_ = QPainterPath();
  patch_tool_drag_proxy_image_ = QImage();
  update_tool_cursor();
}

// Release-time heal: one undo entry, every healed pixel a pure function of the
// frozen snapshot, the mask, and the user's drag offset (see the constraint
// comment at the top of this file).
void CanvasWidget::commit_patch_tool_drag() {
  const auto delta = patch_tool_drag_delta_;
  const auto bounds = patch_tool_drag_mask_bounds_;
  const auto mask_image = patch_tool_drag_mask_;
  const auto snapshot = patch_tool_source_image_;
  const auto mode = patch_tool_mode_;
  const auto transparent = patch_tool_transparent_;
  const auto content_aware_move = patch_tool_drag_content_aware_move_;
  cancel_patch_tool_drag();
  if (document_ == nullptr || snapshot.isNull() || mask_image.isNull() || bounds.isEmpty() ||
      delta.isNull()) {
    update();
    return;
  }

  // Stride-free mask copy over its bounds.
  std::vector<std::uint8_t> mask(static_cast<std::size_t>(bounds.width()) *
                                 static_cast<std::size_t>(bounds.height()));
  std::int64_t mask_area = 0;
  for (int y = 0; y < bounds.height(); ++y) {
    const auto* row = mask_image.constScanLine(y);
    auto* out = mask.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(bounds.width());
    for (int x = 0; x < bounds.width(); ++x) {
      out[x] = row[x];
      mask_area += row[x] != 0U ? 1 : 0;
    }
  }
  if (mask_area <= 0) {
    update();
    return;
  }
  if (content_aware_move) {
    commit_content_aware_move(delta, bounds, mask, mask_area, snapshot);
    return;
  }

  const QRect canvas_rect(0, 0, document_->width(), document_->height());
  const auto destination_offset = mode == PatchToolMode::Destination ? delta : QPoint(0, 0);
  const auto destination_bounds = bounds.translated(destination_offset).intersected(canvas_rect);
  if (destination_bounds.isEmpty()) {
    update();
    return;
  }
  if (!begin_edit(tr("Patch"))) {
    update();
    return;
  }
  auto* layer = active_pixel_layer();
  if (layer == nullptr) {
    return;
  }
  // 16/32-bit layers patch a narrowed copy; only the patched pixels come back at 8-bit
  // precision (NarrowedLayerEdit), and the canvas refreshes after the merge.
  std::optional<NarrowedLayerEdit> narrowed;
  if (std::as_const(*layer).pixels().format().bit_depth != BitDepth::UInt8) {
    narrowed.emplace(*layer, [this, destination_bounds] {
      active_edit_target_changed_impl(QRegion(destination_bounds), DocumentChangeReason::BrushStrokeFinished);
    });
  }

  begin_processing_operation();
  const auto source_offset = mode == PatchToolMode::Destination ? -delta : delta;
  write_patch_heal(*layer, bounds, mask, mask_area, destination_offset, source_offset, snapshot, snapshot,
                   transparent);
  if (mode == PatchToolMode::Destination) {
    move_selection_with_patch_drop(delta, tr("Patch"));
  }
  end_processing_operation();
  active_edit_target_changed_impl(QRegion(destination_bounds), DocumentChangeReason::BrushStrokeFinished);
  update();
}

// Content-Aware Move's release, run once (docs/content-aware-move.md). Nothing was
// written or shown in the old spot during the drag; only the ghost followed the pointer.
// That keeps the tool outside every independent claim of Adobe US 9575641 and its
// continuation US 10332291 (to 2033), which need the original spot updated with the
// content of the current selection location during the drag. The hole is filled only
// by the shape-derived rigid map (spot_heal_source_map) plus the membrane: no patch or
// exemplar search, no structure or color adaptation settings (docs/legal-constraints.md).
void CanvasWidget::commit_content_aware_move(QPoint delta, QRect bounds, const std::vector<std::uint8_t>& mask,
                                             std::int64_t mask_area, const QImage& snapshot) {
  const QRect canvas_rect(0, 0, document_->width(), document_->height());
  const auto destination_bounds = bounds.translated(delta).intersected(canvas_rect);
  if (destination_bounds.isEmpty()) {
    update();
    return;
  }
  const bool extend = content_aware_move_mode_ == ContentAwareMoveMode::Extend;

  // Move: the hole's mask padded by one uncovered cell (the source map's boundary and
  // the membrane's Dirichlet ring), and the drop region passed as an excluded shape so
  // the fill never samples where the object is going.
  QRect hole_bounds;
  std::vector<std::uint8_t> hole_mask;
  SpotHealSourceMap hole_map;
  if (!extend) {
    const auto grow = kContentAwareMoveHoleGrow;
    hole_bounds = bounds.adjusted(-1 - grow, -1 - grow, 1 + grow, 1 + grow).intersected(canvas_rect);
    hole_mask.assign(static_cast<std::size_t>(hole_bounds.width()) * static_cast<std::size_t>(hole_bounds.height()),
                     0U);
    for (int y = 0; y < bounds.height(); ++y) {
      const auto hole_y = bounds.top() + y - hole_bounds.top();
      std::memcpy(hole_mask.data() + static_cast<std::size_t>(hole_y) * static_cast<std::size_t>(hole_bounds.width()) +
                      static_cast<std::size_t>(bounds.left() - hole_bounds.left()),
                  mask.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(bounds.width()),
                  static_cast<std::size_t>(bounds.width()));
    }
    if (grow > 0) {
      grow_hole_mask(hole_mask, hole_bounds.width(), hole_bounds.height(), grow);
    }
    hole_map = spot_heal_source_map(hole_mask.data(), to_core_rect(hole_bounds), document_->width(),
                                    document_->height(), 2, 0, mask.data(), to_core_rect(bounds.translated(delta)));
    if (!hole_map.valid) {
      report_status_error(tr("Content-Aware Move needs unselected pixels around the selection to fill from"));
      update();
      return;
    }
  }
  // The old spot's fill (docs/content-aware-move.md), computed on a worker under the
  // progress overlay before the edit opens: the whole-hole block match when it finds a
  // close match, else Remove Object's exemplar fill plus its tone match, both with the
  // drop region excluded from the sources; the shape-derived map above is the last
  // fallback when neither finds a clean source.
  ContentAwareMoveHoleFill hole_fill;
  if (!extend) {
    hole_fill = content_aware_move_hole_fill(hole_bounds, hole_mask, snapshot, bounds.translated(delta), mask);
  }

  if (!begin_edit(tr("Content-Aware Move"))) {
    update();
    return;
  }
  auto* layer = active_pixel_layer();
  if (layer == nullptr) {
    return;
  }
  // New Layer (docs/content-aware-move.md#destination): the move runs on the active layer
  // as always, then every pixel it changed goes to a new layer above and the layer gets
  // its pixels back, inside this one history step.
  const bool to_new_layer = content_aware_move_destination_ == ContentAwareMoveDestination::NewLayer;
  const auto source_layer_id = layer->id();
  const auto before_pixels = to_new_layer ? std::as_const(*layer).pixels() : PixelBuffer{};
  const auto before_bounds = std::as_const(*layer).bounds();
  auto tone_snapshot = snapshot;
  if (!extend) {
    // The hole first: the moved object is not on the canvas yet, and the map skips the
    // drop region. Then the copy's boundary tone comes from the filled document, so a
    // drop overlapping the old spot blends into the fill, not into the object.
    if (hole_fill.block) {
      // The block match's offset, healed in like a Patch Source drag to that offset.
      const auto dx = static_cast<double>(hole_fill.offset.x());
      const auto dy = static_cast<double>(hole_fill.offset.y());
      const auto length = std::sqrt(dx * dx + dy * dy);
      SpotHealSourceMap translation;
      translation.valid = true;
      translation.mirrored = false;
      translation.direction_x = dx / length;
      translation.direction_y = dy / length;
      translation.shift = length;
      translation.candidate_count = 1;
      heal_mask_from_surroundings(hole_bounds, hole_mask, snapshot, translation, *layer, /*clip_to_selection=*/false,
                                  content_aware_move_edge_band(bounds));
    } else if (!hole_fill.filled.isNull()) {
      // Identity: the filled copy equals the snapshot outside the hole, so the membrane's
      // boundary offsets are 0 and the fill lands as computed (Remove Object's write).
      SpotHealSourceMap identity;
      identity.valid = true;
      identity.mirrored = false;
      identity.shift = 0.0;
      identity.candidate_count = 1;
      heal_mask_from_surroundings(hole_bounds, hole_mask, hole_fill.filled, identity, *layer,
                                  /*clip_to_selection=*/false);
    } else {
      heal_mask_from_surroundings(hole_bounds, hole_mask, snapshot, hole_map, *layer, /*clip_to_selection=*/false);
    }
    tone_snapshot = retouch_source_snapshot();
    if (tone_snapshot.isNull()) {
      tone_snapshot = snapshot;
    }
    layer = active_pixel_layer();
    if (layer == nullptr) {
      return;
    }
  }
  {
    // 16/32-bit layers heal a narrowed copy, as the Patch tool does.
    std::optional<NarrowedLayerEdit> narrowed;
    if (std::as_const(*layer).pixels().format().bit_depth != BitDepth::UInt8) {
      narrowed.emplace(*layer, [this, destination_bounds] {
        active_edit_target_changed_impl(QRegion(destination_bounds), DocumentChangeReason::BrushStrokeFinished);
      });
    }
    begin_processing_operation();
    write_patch_heal(*layer, bounds, mask, mask_area, delta, -delta, snapshot, tone_snapshot,
                     /*transparent=*/false, content_aware_move_edge_band(bounds));
    end_processing_operation();
  }
  if (to_new_layer) {
    split_content_aware_move_to_new_layer(source_layer_id, before_pixels, before_bounds);
  }
  move_selection_with_patch_drop(delta, tr("Content-Aware Move"));
  active_edit_target_changed_impl(QRegion(destination_bounds), DocumentChangeReason::BrushStrokeFinished);
  update();
}

void CanvasWidget::split_content_aware_move_to_new_layer(LayerId source_id, const PixelBuffer& before_pixels,
                                                         Rect before_bounds) {
  auto* source = document_->find_layer(source_id);
  if (source == nullptr) {
    return;
  }
  PixelBuffer changed;
  Rect changed_bounds{};
  if (!changed_pixels_of_layer(before_pixels, before_bounds, std::as_const(*source).pixels(),
                               std::as_const(*source).bounds(), changed, changed_bounds)) {
    return;
  }
  // Back to the untouched original (set_pixels bumps every revision).
  source->set_pixels(before_pixels);
  source->set_bounds(before_bounds);
  Layer result(document_->allocate_layer_id(), tr("Content-Aware Move").toStdString(), std::move(changed));
  result.set_bounds(changed_bounds);
  const auto result_id = result.id();
  insert_layer_after_anchor(*document_, std::move(result), source_id);
  document_->set_active_layer(result_id);
  if (layers_moved_callback_) {
    layers_moved_callback_({result_id});  // the host refreshes its panels for the new row
  }
  request_layer_selection({result_id}, result_id);
  document_changed(to_qrect(before_bounds).united(to_qrect(changed_bounds)));
}

// Photoshop parity for Patch Destination (and Content-Aware Move): the selection follows
// the copy to the drop location, as its own history step after the pixel step.
// Translates the live selection (the nudge_selection pattern); the before-edit latch was
// already cleared by the outline gesture that built the selection.
void CanvasWidget::move_selection_with_patch_drop(QPoint delta, const QString& history_label) {
  const auto before = capture_selection_snapshot();
  if (selection_mask_alpha_.isNull()) {
    set_selection_from_region(selection_.translated(delta));
  } else {
    set_selection_from_mask(selection_.translated(delta), selection_mask_bounds_.translated(delta),
                            selection_mask_alpha_);
  }
  record_selection_history(history_label, before);
}

int CanvasWidget::content_aware_move_edge_band(QRect bounds) {
  return std::clamp(std::min(bounds.width(), bounds.height()) / 4, 6, 48);
}

CanvasWidget::ContentAwareMoveHoleFill CanvasWidget::content_aware_move_hole_fill(
    QRect hole_bounds, const std::vector<std::uint8_t>& hole_mask, const QImage& snapshot, QRect drop_bounds,
    const std::vector<std::uint8_t>& drop_mask) {
  if (snapshot.isNull() || snapshot.format() != QImage::Format_RGBA8888 ||
      snapshot.bytesPerLine() != snapshot.width() * 4) {
    return {};
  }
  ExemplarInpaintOptions options;
  options.patch_size = 9;
  options.search_radius = std::clamp(std::max(hole_bounds.width(), hole_bounds.height()) / 2 + 32, 48, 96);
  options.single_threaded = qEnvironmentVariableIsSet("PATCHY_RENDER_SINGLE_THREADED");
  options.excluded = drop_mask.data();
  options.excluded_bounds = to_core_rect(drop_bounds);
  const auto progress = std::make_shared<std::atomic<int>>(-1);
  const auto compute = [&]() -> ContentAwareMoveHoleFill {
    ContentAwareMoveHoleFill fill;
    {
      const auto search = std::clamp(2 * std::max(hole_bounds.width(), hole_bounds.height()), 96, 320);
      const auto match = exemplar_block_match(snapshot.constBits(), snapshot.width(), snapshot.height(),
                                              hole_mask.data(), to_core_rect(hole_bounds), 8, search,
                                              drop_mask.data(), to_core_rect(drop_bounds));
      // Take the block only when its band matches about as well as the surroundings vary
      // (up to three times their own neighbour-to-neighbour difference, plus 8 levels per
      // channel for plain walls): busy grass differs from any shifted grass, but a block
      // that brings a curtain into a window does not match. Otherwise the patch-wise fill
      // below does better.
      if (match.found && match.difference <= 3 * match.texture + 24 * match.samples) {
        fill.block = true;
        fill.offset = QPoint(match.dx, match.dy);
        return fill;
      }
    }
    auto filled = snapshot.copy();
    const auto result = exemplar_inpaint(filled.bits(), filled.width(), filled.height(), hole_mask.data(),
                                         to_core_rect(hole_bounds), options,
                                         [progress](std::int64_t done, std::int64_t total) {
                                           progress->store(static_cast<int>(total > 0 ? done * 100 / total : 100),
                                                           std::memory_order_relaxed);
                                         });
    if (!result.filled) {
      return fill;
    }
    exemplar_match_tone(filled.bits(), filled.width(), filled.height(), hole_mask.data(), to_core_rect(hole_bounds),
                        24, 100);
    fill.filled = std::move(filled);
    return fill;
  };
  ContentAwareMoveHoleFill filled;
  begin_processing_operation(tr("Moving object..."));
  bool run_inline = false;
#if defined(Q_OS_WASM) && defined(__EMSCRIPTEN_PTHREADS__)
  // Remove Object's pool contract (worker_budget.hpp, docs/healing.md).
  const auto idle_pool = patchy::idle_prespawned_pool_workers();
  run_inline = idle_pool < 1;
  const patchy::BlockingFanoutBudgetScope fanout_budget(run_inline ? -1 : std::max(0, idle_pool - 3));
#endif
  try {
    if (run_inline) {
      filled = compute();
    } else {
      auto future = launch_async(compute);
      int last_percent = -1;
      wait_for_processing_operation([&] {
        const auto percent = progress->load(std::memory_order_relaxed);
        if (percent >= 0 && percent != last_percent) {
          last_percent = percent;
          set_processing_operation_message(tr("Moving object... %1%").arg(percent));
        }
        return future.wait_for(std::chrono::milliseconds(16)) == std::future_status::ready;
      });
      filled = future.get();
    }
  } catch (...) {
    end_processing_operation();
    throw;
  }
  end_processing_operation();
  return filled;
}

void CanvasWidget::write_patch_heal(Layer& layer, QRect bounds, const std::vector<std::uint8_t>& mask,
                                    std::int64_t mask_area, QPoint destination_offset, QPoint source_offset,
                                    const QImage& source_snapshot, const QImage& tone_snapshot, bool transparent,
                                    int edge_band) {
  const QRect canvas_rect(0, 0, document_->width(), document_->height());
  const auto destination_bounds = bounds.translated(destination_offset).intersected(canvas_rect);
  if (destination_bounds.isEmpty()) {
    return;
  }
  const auto lock_transparent_pixels = active_layer_locks_transparent_pixels();
  if (!lock_transparent_pixels) {
    patchy::expand_layer_to_include_rect(layer, to_core_rect(destination_bounds));
  }
  auto& pixels = layer.pixels();
  const auto layer_bounds = layer.bounds();
  const auto layer_rect = to_qrect(layer_bounds);
  const auto channels = pixels.format().channels;
  // The rows below write through this span, never through the buffer's
  // accessors: a non-const access from a worker strip would detach the
  // copy-on-write storage concurrently (the undo snapshot shares it until the
  // first mutation), each strip copying the bytes while another strip's
  // replacement frees them. The span is taken right before the write, after
  // the last event pump, so nothing pumped can have re-shared the bytes.
  std::span<std::uint8_t> pixel_bytes;
  const auto stride = pixels.stride_bytes();
  const auto* palette_snap = palette_snap_for_edits();

  // Transparent mode's detail-extraction radius: the region's area-equivalent
  // disc diameter through the classic formula at its default strength (the
  // membrane below needs no radius at all).
  const auto equivalent_diameter = std::clamp(
      static_cast<int>(std::lround(2.0 * std::sqrt(static_cast<double>(mask_area) / kPi))), 4, kMaxBrushSize);
  const auto tone_radius = std::max(2, (equivalent_diameter * 4 + 15) / 16);

  const auto mask_offset = destination_offset;
  const auto clamped_pixel = [&](const QImage& image, std::int32_t x, std::int32_t y) {
    const auto clamped_x = std::clamp(x, 0, canvas_rect.width() - 1);
    const auto clamped_y = std::clamp(y, 0, canvas_rect.height() - 1);
    return image.constScanLine(clamped_y) + static_cast<std::size_t>(clamped_x) * 4U;
  };
  const auto source_pixel_at = [&](std::int32_t x, std::int32_t y) { return clamped_pixel(source_snapshot, x, y); };
  const auto tone_pixel_at = [&](std::int32_t x, std::int32_t y) { return clamped_pixel(tone_snapshot, x, y); };

  const auto solve_bounds = destination_bounds.adjusted(-1, -1, 1, 1).intersected(canvas_rect);
  const auto solve_width = solve_bounds.width();
  const auto solve_height = solve_bounds.height();

  // Transparent option: a true high-pass of the dragged source (source minus
  // its separable box-blurred local mean) laid over the untouched
  // destination. The earlier sparse 8-sample ring wildly misestimated the
  // local mean on textured content, turning the detail term into clamped
  // noise.
  std::vector<std::uint8_t> transparent_low_pass;
  if (transparent) {
    const auto cells = static_cast<std::size_t>(solve_width) * static_cast<std::size_t>(solve_height);
    std::vector<std::uint8_t> source_patch(cells * 3U);
    for (int y = 0; y < solve_height; ++y) {
      for (int x = 0; x < solve_width; ++x) {
        const auto* source = source_pixel_at(solve_bounds.left() + x + source_offset.x(),
                                             solve_bounds.top() + y + source_offset.y());
        const auto index = (static_cast<std::size_t>(y) * static_cast<std::size_t>(solve_width) +
                            static_cast<std::size_t>(x)) *
                           3U;
        source_patch[index] = source[0];
        source_patch[index + 1U] = source[1];
        source_patch[index + 2U] = source[2];
      }
    }
    const auto blur_radius = tone_radius;
    // Horizontal then vertical mean via per-line prefix sums with clamped
    // windows; integer division keeps it deterministic.
    std::vector<std::uint8_t> horizontal(cells * 3U);
    std::vector<std::int32_t> prefix((static_cast<std::size_t>(std::max(solve_width, solve_height)) + 1U) * 3U);
    for (int y = 0; y < solve_height; ++y) {
      const auto row_base = static_cast<std::size_t>(y) * static_cast<std::size_t>(solve_width);
      prefix[0] = prefix[1] = prefix[2] = 0;
      for (int x = 0; x < solve_width; ++x) {
        for (int channel = 0; channel < 3; ++channel) {
          prefix[(static_cast<std::size_t>(x) + 1U) * 3U + static_cast<std::size_t>(channel)] =
              prefix[static_cast<std::size_t>(x) * 3U + static_cast<std::size_t>(channel)] +
              source_patch[(row_base + static_cast<std::size_t>(x)) * 3U + static_cast<std::size_t>(channel)];
        }
      }
      for (int x = 0; x < solve_width; ++x) {
        const auto x0 = std::max(0, x - blur_radius);
        const auto x1 = std::min(solve_width - 1, x + blur_radius);
        const auto count = x1 - x0 + 1;
        for (int channel = 0; channel < 3; ++channel) {
          const auto sum = prefix[(static_cast<std::size_t>(x1) + 1U) * 3U + static_cast<std::size_t>(channel)] -
                           prefix[static_cast<std::size_t>(x0) * 3U + static_cast<std::size_t>(channel)];
          horizontal[(row_base + static_cast<std::size_t>(x)) * 3U + static_cast<std::size_t>(channel)] =
              static_cast<std::uint8_t>(sum / count);
        }
      }
    }
    transparent_low_pass.resize(cells * 3U);
    for (int x = 0; x < solve_width; ++x) {
      prefix[0] = prefix[1] = prefix[2] = 0;
      for (int y = 0; y < solve_height; ++y) {
        const auto index = (static_cast<std::size_t>(y) * static_cast<std::size_t>(solve_width) +
                            static_cast<std::size_t>(x)) *
                           3U;
        for (int channel = 0; channel < 3; ++channel) {
          prefix[(static_cast<std::size_t>(y) + 1U) * 3U + static_cast<std::size_t>(channel)] =
              prefix[static_cast<std::size_t>(y) * 3U + static_cast<std::size_t>(channel)] +
              horizontal[index + static_cast<std::size_t>(channel)];
        }
      }
      for (int y = 0; y < solve_height; ++y) {
        const auto y0 = std::max(0, y - blur_radius);
        const auto y1 = std::min(solve_height - 1, y + blur_radius);
        const auto count = y1 - y0 + 1;
        const auto index = (static_cast<std::size_t>(y) * static_cast<std::size_t>(solve_width) +
                            static_cast<std::size_t>(x)) *
                           3U;
        for (int channel = 0; channel < 3; ++channel) {
          const auto sum = prefix[(static_cast<std::size_t>(y1) + 1U) * 3U + static_cast<std::size_t>(channel)] -
                           prefix[static_cast<std::size_t>(y0) * 3U + static_cast<std::size_t>(channel)];
          transparent_low_pass[index + static_cast<std::size_t>(channel)] =
              static_cast<std::uint8_t>(sum / count);
        }
      }
    }
    tick_processing_operation();
  }

  // The healing membrane, for every path: offsets on the ring of uncovered
  // cells around the region are interpolated harmonically across the
  // interior. Source/Destination modes use destination-minus-source (the
  // dragged texture's tone bends smoothly into the destination); Transparent
  // uses the negated boundary detail, which cancels the high-pass residual at
  // the selection edge so the overlay fades in seamlessly instead of printing
  // the outline. Solved over the destination bounds padded by one so a
  // Dirichlet ring exists wherever the canvas allows.
  const auto cells = static_cast<std::size_t>(solve_width) * static_cast<std::size_t>(solve_height);
  std::vector<std::uint8_t> interior(cells);
  std::vector<std::int16_t> membrane(cells * 3U);
  for (int y = 0; y < solve_height; ++y) {
    for (int x = 0; x < solve_width; ++x) {
      const auto index = static_cast<std::size_t>(y) * static_cast<std::size_t>(solve_width) +
                         static_cast<std::size_t>(x);
      const auto doc_x = solve_bounds.left() + x;
      const auto doc_y = solve_bounds.top() + y;
      const auto mask_x = doc_x - mask_offset.x() - bounds.left();
      const auto mask_y = doc_y - mask_offset.y() - bounds.top();
      const auto coverage =
          mask_x >= 0 && mask_y >= 0 && mask_x < bounds.width() && mask_y < bounds.height()
              ? mask[static_cast<std::size_t>(mask_y) * static_cast<std::size_t>(bounds.width()) +
                     static_cast<std::size_t>(mask_x)]
              : std::uint8_t{0};
      interior[index] = coverage != 0U ? 1U : 0U;
      if (coverage != 0U) {
        continue;
      }
      const auto* source = source_pixel_at(doc_x + source_offset.x(), doc_y + source_offset.y());
      if (transparent) {
        for (int channel = 0; channel < 3; ++channel) {
          membrane[index * 3U + static_cast<std::size_t>(channel)] = static_cast<std::int16_t>(
              static_cast<int>(transparent_low_pass[index * 3U + static_cast<std::size_t>(channel)]) -
              static_cast<int>(source[channel]));
        }
      } else {
        const auto* destination = tone_pixel_at(doc_x, doc_y);
        for (int channel = 0; channel < 3; ++channel) {
          membrane[index * 3U + static_cast<std::size_t>(channel)] = static_cast<std::int16_t>(
              static_cast<int>(destination[channel]) - static_cast<int>(source[channel]));
        }
      }
    }
  }
  tick_processing_operation();
  patchy::solve_heal_membrane(interior.data(), solve_width, solve_height, membrane.data());
  tick_processing_operation();
  if (edge_band > 0) {
    patchy::fade_heal_offsets_to_edge_band(interior.data(), solve_width, solve_height, membrane.data(), edge_band);
  }

  // For each destination pixel p: coverage from the mask (at p in Source mode,
  // at p - delta in Destination mode) and source pixel s at the user-dragged
  // offset. Pure per-pixel function writing disjoint rows, so the strip
  // fan-out below is byte-identical to the sequential walk.
  const auto heal_rows = [&](int row_begin, int row_end, bool allow_ticks) {
    for (int y = row_begin; y < row_end; ++y) {
      if (allow_ticks) {
        tick_processing_operation();
      }
      for (int x = destination_bounds.left(); x <= destination_bounds.right(); ++x) {
        const QPoint document_point(x, y);
        const auto mask_x = x - mask_offset.x() - bounds.left();
        const auto mask_y = y - mask_offset.y() - bounds.top();
        if (mask_x < 0 || mask_y < 0 || mask_x >= bounds.width() || mask_y >= bounds.height()) {
          continue;
        }
        const auto mask_alpha =
            mask[static_cast<std::size_t>(mask_y) * static_cast<std::size_t>(bounds.width()) +
                 static_cast<std::size_t>(mask_x)];
        if (mask_alpha == 0U) {
          continue;
        }
        if (!layer_rect.contains(document_point)) {
          continue;
        }
        const QPoint source_point = document_point + source_offset;
        if (!canvas_rect.contains(source_point)) {
          continue;
        }
        auto coverage = static_cast<float>(mask_alpha) / 255.0F;
        if (palette_snap != nullptr) {
          if (coverage < palette_snap->coverage_threshold) {
            continue;
          }
          coverage = 1.0F;
        }
        auto* dst = pixel_bytes.data() +
                    static_cast<std::size_t>(document_point.y() - layer_bounds.y) * stride +
                    static_cast<std::size_t>(document_point.x() - layer_bounds.x) * channels;
        if (lock_transparent_pixels && channels >= 4 && dst[3] == 0) {
          continue;
        }

        std::array<std::uint8_t, 4> healed{};
        const auto* source_pixel = source_snapshot.constScanLine(source_point.y()) +
                                   static_cast<std::size_t>(source_point.x()) * 4U;
        if (transparent) {
          // Texture-only transfer: the source's high-pass detail (source
          // minus its box-blurred local mean) over the destination pixel,
          // with the membrane fading the detail's edge residual to zero at
          // the boundary; tone and alpha stay the destination's.
          const auto solve_index =
              static_cast<std::size_t>(document_point.y() - solve_bounds.top()) *
                  static_cast<std::size_t>(solve_width) +
              static_cast<std::size_t>(document_point.x() - solve_bounds.left());
          const auto* destination_pixel = tone_snapshot.constScanLine(document_point.y()) +
                                          static_cast<std::size_t>(document_point.x()) * 4U;
          for (std::size_t channel = 0; channel < 3; ++channel) {
            const auto detail =
                static_cast<int>(source_pixel[channel]) -
                static_cast<int>(transparent_low_pass[solve_index * 3U + channel]) +
                static_cast<int>(membrane[solve_index * 3U + channel]);
            healed[channel] =
                clamp_byte(static_cast<float>(static_cast<int>(destination_pixel[channel]) + detail));
          }
          healed[3] = destination_pixel[3];
        } else {
          // Source and Destination modes share the membrane: the dragged
          // texture plus the interpolated boundary tone difference.
          const auto solve_index =
              static_cast<std::size_t>(document_point.y() - solve_bounds.top()) *
                  static_cast<std::size_t>(solve_width) +
              static_cast<std::size_t>(document_point.x() - solve_bounds.left());
          for (std::size_t channel = 0; channel < 3; ++channel) {
            healed[channel] = clamp_byte(static_cast<float>(source_pixel[channel]) +
                                         static_cast<float>(membrane[solve_index * 3U + channel]));
          }
          healed[3] = source_pixel[3];
        }

        if (channels >= 4 && !lock_transparent_pixels) {
          blend_straight_rgba(dst, healed.data(), coverage);
        } else {
          const auto effective_opacity = coverage * (static_cast<float>(healed[3]) / 255.0F);
          if (effective_opacity <= 0.0F) {
            continue;
          }
          dst[0] = clamp_byte(static_cast<float>(healed[0]) * effective_opacity +
                              static_cast<float>(dst[0]) * (1.0F - effective_opacity));
          dst[1] = clamp_byte(static_cast<float>(healed[1]) * effective_opacity +
                              static_cast<float>(dst[1]) * (1.0F - effective_opacity));
          dst[2] = clamp_byte(static_cast<float>(healed[2]) * effective_opacity +
                              static_cast<float>(dst[2]) * (1.0F - effective_opacity));
        }
        if (palette_snap != nullptr) {
          patchy::snap_pixel_to_palette(dst, channels, *palette_snap);
        }
      }
    }
  };

  const auto area = static_cast<std::int64_t>(destination_bounds.width()) * destination_bounds.height();
  pixel_bytes = pixels.data();  // detaches shared storage on this thread
  const auto hardware_threads = patchy::hardware_worker_threads();
  // max_blocking_fanout_workers: this thread blocks on the row futures, so on
  // the wasm main thread the fan-out must fit the idle pthread pool.
  const auto workers = patchy::max_blocking_fanout_workers(
      std::clamp(std::min(destination_bounds.height() / 64, hardware_threads), 1, 16));
  if (area >= kPatchToolParallelArea && workers >= 2 &&
      !qEnvironmentVariableIsSet("PATCHY_RENDER_SINGLE_THREADED")) {
    std::vector<std::future<void>> strips;
    strips.reserve(static_cast<std::size_t>(workers));
    const auto rows_per_strip = (destination_bounds.height() + workers - 1) / workers;
    for (int start = destination_bounds.top(); start <= destination_bounds.bottom(); start += rows_per_strip) {
      const auto end = std::min(start + rows_per_strip, destination_bounds.bottom() + 1);
      strips.push_back(std::async(std::launch::async, heal_rows, start, end, false));
    }
    for (auto& strip : strips) {
      strip.get();
    }
  } else {
    heal_rows(destination_bounds.top(), destination_bounds.bottom() + 1, true);
  }
}

// The drag shows no heal. Patch Source draws nothing here, only the moving outline
// (draw_patch_tool_drag_outline): showing the dragged-to content inside the original
// spot while dragging is a step of Adobe US 10332291 claim 1 (to 2033;
// docs/legal-constraints.md), so the original spot stays as it is until release.
// Patch Destination and Content-Aware Move draw a raw ghost of the region (the frozen
// snapshot cut to the mask) at the pointer offset; the heal is one-shot at release.
void CanvasWidget::draw_patch_tool_drag_preview(QPainter& painter) const {
  if (!patch_tool_dragging_ || patch_tool_drag_proxy_image_.isNull()) {
    return;
  }
  if (!patch_tool_drag_content_aware_move_ && patch_tool_mode_ == PatchToolMode::Source) {
    return;
  }
  const auto bounds = patch_tool_drag_mask_bounds_;
  painter.drawImage(widget_rect_for_document_rect(QRectF(bounds.translated(patch_tool_drag_delta_))),
                    patch_tool_drag_proxy_image_, QRectF(patch_tool_drag_proxy_image_.rect()));
}

// The dragged region's outline at its current offset, stroked with the ants
// colors (static dash phase; the original selection keeps its own animated
// ants underneath, as in Photoshop).
void CanvasWidget::draw_patch_tool_drag_outline(QPainter& painter) const {
  if (!patch_tool_dragging_ || patch_tool_outline_path_.isEmpty() || patch_tool_drag_delta_.isNull()) {
    return;
  }
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, false);
  const auto origin = widget_position_f(QPointF(patch_tool_drag_delta_));
  const QTransform document_to_widget(zoom_, 0.0, 0.0, zoom_, origin.x(), origin.y());
  const auto path = document_to_widget.map(patch_tool_outline_path_);
  painter.setBrush(Qt::NoBrush);
  // The marching-ants black/white pair is deliberately theme-exempt, like the
  // selection overlay itself.
  painter.setPen(QPen(QColor(0, 0, 0), 0));
  painter.drawPath(path);
  QPen dash(QColor(255, 255, 255), 0);
  dash.setDashPattern({4.0, 4.0});
  painter.setPen(dash);
  painter.drawPath(path);
  painter.restore();
}

}  // namespace patchy::ui
