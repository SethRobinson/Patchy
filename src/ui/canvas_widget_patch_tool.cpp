// CanvasWidget's Patch tool: draw a freehand outline (the shared lasso
// machinery in the events TU) or use an existing selection, then drag the
// region and commit a heal on release. The tool is strictly user-directed:
// the dragged offset is the ONLY source choice, the drag preview is a raw
// translated copy of a frozen snapshot, and all healing runs ONCE on release
// using the classic healing membrane of the expired US 6587592
// (core/heal_membrane.hpp): boundary tone differences interpolated across the
// interior, plus the dragged source texture. No patch search, no
// synthesis-by-example, no reshuffling, no content-driven source selection,
// no gradient-domain compositing of source gradients, and no live per-move
// classification may be added: those families are claimed by Adobe's active
// PatchMatch patents (US 8285055, US 8340463, US 8355592, into 2031),
// US 9058699 (to 2029), and US 8050498 (to Nov 3, 2029). See
// docs/legal-constraints.md and the dated record in docs/patent-research.md.

#include "ui/canvas_widget.hpp"
#include "ui/canvas_widget_shared.hpp"

#include "core/blend_math.hpp"
#include "core/heal_membrane.hpp"
#include "core/pixel_tools.hpp"
#include "core/worker_budget.hpp"
#include "ui/edit_conversions.hpp"
#include "ui/qt_geometry.hpp"
#include "ui/selection_outline.hpp"

#include <QApplication>
#include <QPainter>
#include <QPen>
#include <QTransform>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <future>
#include <thread>
#include <vector>

namespace patchy::ui {

namespace {

// Once the healed area is at least this large the commit fans out over row
// strips; the per-pixel cost (16 snapshot reads through the ring tones) is
// lower than the transform resampler's, so the gate sits below its 1 Mpx.
constexpr std::int64_t kPatchToolParallelArea = 262'144;

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

  // Destination-mode preview proxy: the snapshot cut to the soft mask.
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
  cancel_patch_tool_drag();
  if (document_ == nullptr || snapshot.isNull() || mask_image.isNull() || bounds.isEmpty() ||
      delta.isNull()) {
    update();
    return;
  }
  const QRect canvas_rect(0, 0, document_->width(), document_->height());
  const auto destination_offset = mode == PatchToolMode::Destination ? delta : QPoint(0, 0);
  const auto destination_bounds = bounds.translated(destination_offset).intersected(canvas_rect);
  if (destination_bounds.isEmpty()) {
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

  if (!begin_edit(tr("Patch"))) {
    update();
    return;
  }
  auto* layer = active_pixel_layer();
  if (layer == nullptr) {
    return;
  }

  begin_processing_operation();
  const auto lock_transparent_pixels = active_layer_locks_transparent_pixels();
  if (!lock_transparent_pixels) {
    patchy::expand_layer_to_include_rect(*layer, to_core_rect(destination_bounds));
  }
  auto& pixels = layer->pixels();
  const auto layer_bounds = layer->bounds();
  const auto layer_rect = to_qrect(layer_bounds);
  const auto channels = pixels.format().channels;
  const auto* palette_snap = palette_snap_for_edits();

  // Transparent mode's detail-extraction radius: the region's area-equivalent
  // disc diameter through the classic formula at its default strength (the
  // membrane below needs no radius at all).
  const auto equivalent_diameter = std::clamp(
      static_cast<int>(std::lround(2.0 * std::sqrt(static_cast<double>(mask_area) / kPi))), 4, kMaxBrushSize);
  const auto tone_radius = std::max(2, (equivalent_diameter * 4 + 15) / 16);

  const auto source_offset = mode == PatchToolMode::Destination ? -delta : delta;
  const auto mask_offset = destination_offset;
  const auto snapshot_pixel = [&](std::int32_t x, std::int32_t y) {
    const auto clamped_x = std::clamp(x, 0, canvas_rect.width() - 1);
    const auto clamped_y = std::clamp(y, 0, canvas_rect.height() - 1);
    return snapshot.constScanLine(clamped_y) + static_cast<std::size_t>(clamped_x) * 4U;
  };

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
        const auto* source = snapshot_pixel(solve_bounds.left() + x + source_offset.x(),
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
      const auto* source = snapshot_pixel(doc_x + source_offset.x(), doc_y + source_offset.y());
      if (transparent) {
        for (int channel = 0; channel < 3; ++channel) {
          membrane[index * 3U + static_cast<std::size_t>(channel)] = static_cast<std::int16_t>(
              static_cast<int>(transparent_low_pass[index * 3U + static_cast<std::size_t>(channel)]) -
              static_cast<int>(source[channel]));
        }
      } else {
        const auto* destination = snapshot_pixel(doc_x, doc_y);
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
        auto row = pixels.row(document_point.y() - layer_bounds.y);
        auto* dst = row.data() + static_cast<std::size_t>(document_point.x() - layer_bounds.x) * channels;
        if (lock_transparent_pixels && channels >= 4 && dst[3] == 0) {
          continue;
        }

        std::array<std::uint8_t, 4> healed{};
        const auto* source_pixel =
            snapshot.constScanLine(source_point.y()) + static_cast<std::size_t>(source_point.x()) * 4U;
        if (transparent) {
          // Texture-only transfer: the source's high-pass detail (source
          // minus its box-blurred local mean) over the destination pixel,
          // with the membrane fading the detail's edge residual to zero at
          // the boundary; tone and alpha stay the destination's.
          const auto solve_index =
              static_cast<std::size_t>(document_point.y() - solve_bounds.top()) *
                  static_cast<std::size_t>(solve_width) +
              static_cast<std::size_t>(document_point.x() - solve_bounds.left());
          const auto* destination_pixel = snapshot.constScanLine(document_point.y()) +
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
  const auto hardware_threads = static_cast<int>(std::thread::hardware_concurrency());
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

  if (mode == PatchToolMode::Destination) {
    // Photoshop parity: the selection follows the copy to the drop location,
    // as its own history step after the pixel step. Translates the live
    // selection (the nudge_selection pattern) - the before-edit latch was
    // already cleared by the outline gesture that built the selection.
    const auto before = capture_selection_snapshot();
    if (selection_mask_alpha_.isNull()) {
      set_selection_from_region(selection_.translated(delta));
    } else {
      set_selection_from_mask(selection_.translated(delta), selection_mask_bounds_.translated(delta),
                              selection_mask_alpha_);
    }
    record_selection_history(tr("Patch"), before);
  }
  end_processing_operation();
  active_edit_target_changed_impl(QRegion(destination_bounds), DocumentChangeReason::BrushStrokeFinished);
  update();
}

// Raw translated copy of the frozen snapshot - Photoshop also previews the
// unhealed pixels while dragging; the heal itself is one-shot at release.
void CanvasWidget::draw_patch_tool_drag_preview(QPainter& painter) const {
  if (!patch_tool_dragging_ || patch_tool_source_image_.isNull()) {
    return;
  }
  const auto bounds = patch_tool_drag_mask_bounds_;
  if (patch_tool_mode_ == PatchToolMode::Source) {
    if (patch_tool_outline_path_.isEmpty()) {
      return;
    }
    painter.save();
    const auto origin = widget_position_f(QPointF(0.0, 0.0));
    const QTransform document_to_widget(zoom_, 0.0, 0.0, zoom_, origin.x(), origin.y());
    painter.setClipPath(document_to_widget.map(patch_tool_outline_path_), Qt::IntersectClip);
    painter.drawImage(widget_rect_for_document_rect(QRectF(bounds)), patch_tool_source_image_,
                      QRectF(bounds.translated(patch_tool_drag_delta_)));
    painter.restore();
  } else {
    if (patch_tool_drag_proxy_image_.isNull()) {
      return;
    }
    painter.drawImage(widget_rect_for_document_rect(QRectF(bounds.translated(patch_tool_drag_delta_))),
                      patch_tool_drag_proxy_image_, QRectF(patch_tool_drag_proxy_image_.rect()));
  }
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
