// CanvasWidget's Spot Healing stroke engine and the Remove Object command (the
// same heal run on the selection instead of a brush footprint). The drag only
// accumulates a soft brush footprint (Grayscale8 mask + the overlay polyline);
// the heal runs ONCE in finish_spot_heal_stroke() after the gesture ends: one
// coherent rigid source mapping from core/spot_heal.hpp (chosen from the
// footprint's SHAPE alone, never from pixel content) supplies the texture, and
// the classic healing membrane of the expired US 6587592
// (core/heal_membrane.hpp) interpolates the boundary tone differences across
// the interior. Remove Object's nearest-edge mode takes only the user's repeat
// count, which walks the geometrically valid candidates in their fixed order;
// its content-aware mode is the exhaustive exemplar search of
// core/exemplar_inpaint.hpp, whose variation number (the dialog's Reroll) is a
// per-patch near-best pick from a fresh full scan, never a perturbed or
// propagated offset. Do not add PatchMatch-style offset propagation or
// perturbation, reshuffling, or gradient-domain compositing of source
// gradients: those are claimed by Adobe's active patents (US 8285055,
// US 8340463, US 8355592, into 2031) and US 9058699 (to 2029); a
// content-driven source search must be the exhaustive exemplar search
// docs/legal-constraints.md clears. Live classify-and-display during input is
// claimed by US 8050498 (to Nov 3, 2029), so the drag shows only the raw
// footprint overlay, and the Remove Object dialog (main_window_layer_ops.cpp)
// runs a fill only on a button click or a settled slider value, never per
// slider move or pointer move. See docs/legal-constraints.md and the dated
// records in docs/patent-research.md and docs/patent-research-inpainting.md.

#include "ui/canvas_widget.hpp"

#include "core/blend_math.hpp"
#include "core/exemplar_inpaint.hpp"
#include "core/heal_membrane.hpp"
#include "core/layer.hpp"
#include "core/layer_render_utils.hpp"
#include "core/palette.hpp"
#include "core/pixel_buffer.hpp"
#include "core/pixel_depth.hpp"
#include "core/pixel_tools.hpp"
#include "core/spot_heal.hpp"
#include "core/worker_budget.hpp"
#include "ui/background_workers.hpp"
#include "ui/canvas_widget_shared.hpp"
#include "ui/qt_geometry.hpp"

#include <QAction>
#include <QColor>
#include <QImage>
#include <QList>
#include <QPainter>
#include <QPen>
#include <QPoint>
#include <QPointF>
#include <QPolygonF>
#include <QRect>
#include <QRegion>
#include <QSize>
#include <QString>
#include <Qt>
#include <QtGlobal>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

// Once the healed area is at least this large the write fans out over row
// strips (the Patch tool's gate; every healed pixel is a pure function of the
// snapshot, the mask, the map, and the solved membrane, so the strips are
// byte-identical to the sequential walk).
constexpr std::int64_t kHealParallelArea = 262'144;

}  // namespace

void CanvasWidget::begin_spot_heal_stroke(QPoint document_point, std::optional<QPointF> connect_from) {
  spot_healing_stroke_active_ = true;
  spot_heal_footprint_ = QImage();
  spot_heal_footprint_bounds_ = QRect();
  spot_heal_stroke_points_.clear();
  if (connect_from.has_value()) {
    const auto from = connect_from->toPoint();
    spot_heal_stroke_points_ << QPointF(from);
    stamp_spot_heal_segment(from, document_point);
  } else {
    stamp_spot_heal_segment(document_point, document_point);
  }
  spot_heal_stroke_points_ << QPointF(document_point);
  spot_heal_last_document_point_ = document_point;
}

void CanvasWidget::extend_spot_heal_stroke(QPoint document_point) {
  if (!spot_healing_stroke_active_ || document_point == spot_heal_last_document_point_) {
    return;
  }
  stamp_spot_heal_segment(spot_heal_last_document_point_, document_point);
  spot_heal_last_document_point_ = document_point;
  spot_heal_stroke_points_ << QPointF(document_point);
  update();
}

void CanvasWidget::cancel_spot_heal_stroke() {
  spot_healing_stroke_active_ = false;
  spot_heal_footprint_ = QImage();
  spot_heal_footprint_bounds_ = QRect();
  spot_heal_stroke_points_.clear();
  spot_heal_source_cache_ = QImage();
}

// Stamps one drag segment as a soft procedural capsule (distance-to-segment
// through the shared brush_coverage falloff, max-combined), so the footprint
// keeps the brush's Soft skirt - that skirt is what feathers the heal into its
// surroundings at commit. Stamping clips to the canvas; strokes that wander
// off simply contribute less.
void CanvasWidget::stamp_spot_heal_segment(QPoint from, QPoint to) {
  if (document_ == nullptr) {
    return;
  }
  if (spot_heal_footprint_.isNull()) {
    spot_heal_footprint_ = QImage(document_->width(), document_->height(), QImage::Format_Grayscale8);
    if (spot_heal_footprint_.isNull()) {
      return;
    }
    spot_heal_footprint_.fill(0);
  }
  const auto radius = std::max(1, std::max(1, brush_size_) / 2);
  const auto pad = radius + 2;
  const auto segment_rect = QRect(QPoint(std::min(from.x(), to.x()) - pad, std::min(from.y(), to.y()) - pad),
                                  QPoint(std::max(from.x(), to.x()) + pad, std::max(from.y(), to.y()) + pad))
                                .intersected(QRect(0, 0, document_->width(), document_->height()));
  if (segment_rect.isEmpty()) {
    return;
  }

  const QPointF a(from);
  const QPointF b(to);
  const auto ab = b - a;
  const auto ab_length_squared = ab.x() * ab.x() + ab.y() * ab.y();
  for (int y = segment_rect.top(); y <= segment_rect.bottom(); ++y) {
    auto* row = spot_heal_footprint_.scanLine(y);
    for (int x = segment_rect.left(); x <= segment_rect.right(); ++x) {
      const QPointF point(x, y);
      auto t = 0.0;
      if (ab_length_squared > 0.0) {
        const auto ap = point - a;
        t = std::clamp((ap.x() * ab.x() + ap.y() * ab.y()) / ab_length_squared, 0.0, 1.0);
      }
      const auto nearest = a + t * ab;
      const auto dx = point.x() - nearest.x();
      const auto dy = point.y() - nearest.y();
      const auto coverage = brush_coverage(dx * dx + dy * dy, radius, brush_softness_);
      if (coverage <= 0.0F) {
        continue;
      }
      const auto value = static_cast<std::uint8_t>(std::lround(coverage * 255.0F));
      row[x] = std::max(row[x], value);
    }
  }
  spot_heal_footprint_bounds_ =
      spot_heal_footprint_bounds_.isNull() ? segment_rect : spot_heal_footprint_bounds_.united(segment_rect);
}

// Release-time heal: one undo entry, one write per covered pixel. All
// computation deliberately happens here, after the input gesture ends (see the
// constraint comment at the top of this file).
void CanvasWidget::finish_spot_heal_stroke() {
  if (!spot_healing_stroke_active_) {
    return;
  }
  spot_healing_stroke_active_ = false;
  const auto source_snapshot = spot_heal_source_cache_;
  const auto drop_stroke_state = [this] {
    spot_heal_footprint_ = QImage();
    spot_heal_footprint_bounds_ = QRect();
    spot_heal_stroke_points_.clear();
    spot_heal_source_cache_ = QImage();
  };
  if (document_ == nullptr || spot_heal_footprint_.isNull() || source_snapshot.isNull() ||
      source_snapshot.format() != QImage::Format_RGBA8888) {
    drop_stroke_state();
    return;
  }
  const auto bounds =
      spot_heal_footprint_bounds_.intersected(QRect(0, 0, document_->width(), document_->height()));
  if (bounds.isEmpty()) {
    drop_stroke_state();
    return;
  }

  const auto width = bounds.width();
  const auto height = bounds.height();
  std::vector<std::uint8_t> mask(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
  for (int y = 0; y < height; ++y) {
    std::memcpy(mask.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width),
                spot_heal_footprint_.constScanLine(bounds.top() + y) + bounds.left(),
                static_cast<std::size_t>(width));
  }

  const auto source_map =
      spot_heal_source_map(mask.data(), to_core_rect(bounds), document_->width(), document_->height());
  if (!source_map.valid) {
    drop_stroke_state();
    report_status_error(tr("Spot healing needs unpainted pixels around the stroke to sample"));
    update();
    return;
  }

  if (!begin_edit(tr("Spot healing"))) {
    drop_stroke_state();
    update();
    return;
  }
  auto* layer = active_pixel_layer();
  if (layer == nullptr) {
    drop_stroke_state();
    return;
  }

  heal_mask_from_surroundings(bounds, mask, source_snapshot, source_map, *layer, /*clip_to_selection=*/true);
  drop_stroke_state();
}

// Edit > Remove Object: the selection is the footprint and its own feather
// (plus the requested extra feather) is the write's skirt. ContentAware fills
// a copy of the snapshot with the exhaustive exemplar search
// (core/exemplar_inpaint.hpp) and writes it through the shared heal with an
// identity map (the membrane then solves to zero, so the fill lands as
// computed). NearestEdge is the stroke's source map plus membrane; running it
// again on the same selection walks the geometry-only candidates (the user's
// repeat is the only thing that changes the pick).
CanvasWidget::RemoveObjectResult CanvasWidget::remove_object_in_selection(RemoveObjectMethod method, int attempt,
                                                                          bool record_history) {
  RemoveObjectOptions options;
  options.method = method;
  options.attempt = attempt;
  options.record_history = record_history;
  return remove_object_in_selection(options);
}

void CanvasWidget::set_remove_object_requested_callback(std::function<void()> callback) {
  remove_object_requested_callback_ = std::move(callback);
}

CanvasWidget::RemoveObjectJob CanvasWidget::prepare_remove_object(const RemoveObjectOptions& options) {
  RemoveObjectJob job;
  job.options = options;
  const auto refuse = [&](QString message) {
    job.error = std::move(message);
    report_status_error(job.error);
    return job;
  };
  if (document_ == nullptr) {
    return refuse(tr("Remove Object needs an open document"));
  }
  if (!has_selection()) {
    return refuse(tr("Remove Object needs a selection: select the area to remove first"));
  }
  // The extra feather is a gaussian of sigma `feather` over the coverage,
  // Photoshop's mask-feather approximation (three box passes); its full
  // support is the sum of the box radii, and the bounds grow by that much so
  // the blurred skirt fits inside them.
  const auto feather = std::clamp(options.feather, 0, 250);
  const auto feather_radii = feather > 0 ? patchy::mask_feather_box_radii(static_cast<double>(feather))
                                         : std::array<std::int32_t, 3>{0, 0, 0};
  const auto feather_reach = feather_radii[0] + feather_radii[1] + feather_radii[2];
  // Padded by one cell (plus the feather's reach) so the mask carries an
  // uncovered ring: the source map finds its nearest boundary there and the
  // membrane gets its Dirichlet cells (a selection flush with the canvas edge
  // simply has none there).
  const QRect canvas_rect(0, 0, document_->width(), document_->height());
  const auto pad = 1 + feather_reach;
  const auto bounds = selection_.boundingRect().adjusted(-pad, -pad, pad, pad).intersected(canvas_rect);
  if (bounds.isEmpty() || selection_.boundingRect().intersected(canvas_rect).isEmpty()) {
    return refuse(tr("Remove Object needs a selection on the canvas"));
  }
  if (!can_begin_pixel_edit(options.record_history)) {
    // Interactive runs let the precheck report its own refusal (lock, layer
    // kind, rasterize prompt); the script path gets the plain error instead.
    job.error = tr("Remove Object needs an editable pixel layer");
    return job;
  }

  // The selection's coverage over the padded bounds, feather included (the
  // Patch tool's mask recipe; the ring reads as 0).
  const auto width = bounds.width();
  const auto height = bounds.height();
  std::vector<std::uint8_t> mask(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
  if (selection_mask_alpha_.isNull()) {
    const auto hard = hard_mask_from_region(selection_, bounds);
    for (int y = 0; y < height; ++y) {
      std::memcpy(mask.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width), hard.constScanLine(y),
                  static_cast<std::size_t>(width));
    }
  } else {
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        mask[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] =
            selection_alpha_at(QPoint(bounds.left() + x, bounds.top() + y));
      }
    }
  }
  if (feather > 0) {
    // Blur the coverage outward on a 16-bit plane (the mask-feather passes are
    // edge-clamped; the padded ring is 0, so nothing bleeds past the bounds
    // unless the selection is flush with the canvas edge, where the clamp
    // repeats the coverage as the selection's own feather would).
    std::vector<std::uint16_t> plane(mask.size());
    for (std::size_t i = 0; i < mask.size(); ++i) {
      plane[i] = static_cast<std::uint16_t>(mask[i] * 257U);
    }
    patchy::mask_feather_blur(plane, width, height, feather_radii);
    for (std::size_t i = 0; i < mask.size(); ++i) {
      mask[i] = static_cast<std::uint8_t>((plane[i] + 128U) / 257U);
    }
  }

  auto snapshot = retouch_source_snapshot();
  if (snapshot.isNull() || snapshot.format() != QImage::Format_RGBA8888 ||
      snapshot.bytesPerLine() != snapshot.width() * 4) {
    return refuse(tr("Remove Object could not read the document pixels"));
  }
  job.bounds = bounds;
  job.mask = std::move(mask);
  job.snapshot = std::move(snapshot);
  job.valid = true;
  return job;
}

// Static and self-contained: reads the job's own copies only, so it can run
// on a worker thread while the UI stays live. Cancellation is polled per
// copied patch.
CanvasWidget::RemoveObjectComputed CanvasWidget::compute_remove_object(
    const RemoveObjectJob& job, const std::atomic<bool>* cancel, const std::function<void(int)>& progress_percent) {
  RemoveObjectComputed computed;
  if (!job.valid || job.options.method != RemoveObjectMethod::ContentAware) {
    return computed;
  }
  computed.filled = job.snapshot.copy();
  ExemplarInpaintOptions inpaint_options;
  inpaint_options.patch_size = 9;
  // Reach grows with the selection so the sources for a wide object are in
  // the window, capped so the exhaustive scan stays interactive.
  inpaint_options.search_radius = std::clamp(std::max(job.bounds.width(), job.bounds.height()) / 2 + 32, 48, 96);
  inpaint_options.attempt = std::max(0, job.options.attempt);
  inpaint_options.single_threaded = qEnvironmentVariableIsSet("PATCHY_RENDER_SINGLE_THREADED");
  inpaint_options.cancel = cancel;
  int last_percent = -1;
  computed.inpaint = exemplar_inpaint(
      computed.filled.bits(), computed.filled.width(), computed.filled.height(), job.mask.data(),
      to_core_rect(job.bounds), inpaint_options, [&](std::int64_t done, std::int64_t total) {
        if (!progress_percent) {
          return;
        }
        const auto percent = static_cast<int>(total > 0 ? done * 100 / total : 100);
        if (percent != last_percent) {
          last_percent = percent;
          progress_percent(percent);
        }
      });
  if (computed.inpaint.cancelled) {
    computed.cancelled = true;
    computed.filled = QImage();
    return computed;
  }
  if (!computed.inpaint.filled) {
    // No fully known source patch in reach (the selection fills the
    // neighbourhood): the shape-derived mirror still has something to say.
    computed.fell_back = true;
    computed.filled = QImage();
    return computed;
  }
  if (job.options.tone_match > 0) {
    // Frequency separation through the cleared membrane: the fill keeps its
    // grain, its low-pass follows its own edge values smoothly (the
    // brightness polygons where fill fronts meet go; the fill's own tone
    // stays, since only hole cells are read). 0 keeps the raw fill.
    exemplar_match_tone(computed.filled.bits(), computed.filled.width(), computed.filled.height(), job.mask.data(),
                        to_core_rect(job.bounds), 24, std::clamp(job.options.tone_match, 0, 100));
  }
  return computed;
}

CanvasWidget::RemoveObjectResult CanvasWidget::commit_remove_object(const RemoveObjectJob& job,
                                                                    const RemoveObjectComputed& computed) {
  RemoveObjectResult result;
  auto method = job.options.method;
  result.method = method;
  const auto refuse = [&](QString message) {
    result.error = std::move(message);
    report_status_error(result.error);
    return result;
  };
  if (!job.valid) {
    result.error = job.error;
    return result;
  }
  if (document_ == nullptr) {
    return refuse(tr("Remove Object needs an open document"));
  }
  if (computed.cancelled) {
    result.error = tr("Remove Object was cancelled");
    return result;
  }
  const auto& bounds = job.bounds;
  const auto& mask = job.mask;
  const auto variation = std::max(0, job.options.attempt);
  bool fell_back = false;
  if (method == RemoveObjectMethod::ContentAware && (computed.fell_back || computed.filled.isNull())) {
    method = RemoveObjectMethod::NearestEdge;
    fell_back = true;
  }
  result.method = method;

  SpotHealSourceMap source_map;
  if (method == RemoveObjectMethod::NearestEdge) {
    // Repeat detection: the same bounds and coverage as the previous run means
    // "try the next source". FNV-1a over the mask bytes; undo does not reset
    // it, so Undo / Remove Object / Undo / Remove Object walks the candidates.
    std::uint64_t mask_hash = 14695981039346656037ULL;
    for (const auto byte : mask) {
      mask_hash ^= byte;
      mask_hash *= 1099511628211ULL;
    }
    const bool same_selection = remove_object_has_last_ && remove_object_last_bounds_ == bounds &&
                                remove_object_last_mask_hash_ == mask_hash;
    if (job.options.attempt >= 0) {
      remove_object_attempt_ = job.options.attempt;
    } else {
      remove_object_attempt_ = same_selection ? remove_object_attempt_ + 1 : 0;
    }
    remove_object_has_last_ = true;
    remove_object_last_bounds_ = bounds;
    remove_object_last_mask_hash_ = mask_hash;
    source_map = spot_heal_source_map(mask.data(), to_core_rect(bounds), document_->width(), document_->height(), 2,
                                      remove_object_attempt_);
    if (!source_map.valid) {
      return refuse(tr("Remove Object needs unselected pixels around the selection to sample"));
    }
  } else {
    // Identity: the filled copy already holds the result, and outside the
    // hole it equals the snapshot, so the membrane's boundary offsets are 0.
    source_map.valid = true;
    source_map.mirrored = false;
    source_map.shift = 0.0;
    source_map.candidate_count = 1;
  }
  if (job.options.record_history && !begin_edit(tr("Remove Object"))) {
    result.error = tr("Remove Object needs an editable pixel layer");
    return result;
  }
  auto* layer = active_pixel_layer();
  if (layer == nullptr) {
    return refuse(tr("Remove Object needs a pixel layer"));
  }

  heal_mask_from_surroundings(bounds, mask, method == RemoveObjectMethod::ContentAware ? computed.filled : job.snapshot,
                              source_map, *layer, /*clip_to_selection=*/false);
  result.applied = true;
  if (method == RemoveObjectMethod::ContentAware) {
    result.patches = computed.inpaint.patches;
    result.source_index = 1;
    result.source_count = 1;
    result.attempt = variation;
    if (status_callback_) {
      status_callback_(variation > 0 ? tr("Removed object with content-aware fill, variation %1 (%2 patches)")
                                           .arg(variation + 1)
                                           .arg(result.patches)
                                     : tr("Removed object with content-aware fill (%1 patches)").arg(result.patches));
    }
  } else {
    const auto count = std::max(1, source_map.candidate_count);
    result.source_count = count;
    result.source_index = ((remove_object_attempt_ % count) + count) % count + 1;
    result.attempt = result.source_index - 1;
    if (status_callback_) {
      status_callback_(fell_back ? tr("Remove Object found no clean source patches nearby; used the nearest edge "
                                      "instead (source %1 of %2)")
                                       .arg(result.source_index)
                                       .arg(result.source_count)
                                 : tr("Removed object with source %1 of %2. Run again to try another.")
                                       .arg(result.source_index)
                                       .arg(result.source_count));
    }
  }
  update();
  return result;
}

// The synchronous command: prepare and commit on this thread, the fill on a
// worker under the progress overlay ("Removing object... N%"). The main
// thread waits in wait_for_processing_operation and polls the percent the
// worker publishes. Computing here and pumping from the progress callback
// painted fine on desktop, but on wasm no pump can paint (the browser gets
// no turn until the main thread suspends in an event loop; docs/wasm.md),
// so a long fill looked like a frozen tab with no overlay at all. The wait's
// nested loop suspends properly there and ticks the overlay everywhere.
CanvasWidget::RemoveObjectResult CanvasWidget::remove_object_in_selection(const RemoveObjectOptions& options) {
  const auto job = prepare_remove_object(options);
  if (!job.valid) {
    RemoveObjectResult result;
    result.method = options.method;
    result.error = job.error;
    return result;
  }
  RemoveObjectComputed computed;
  if (options.method == RemoveObjectMethod::ContentAware) {
    begin_processing_operation(tr("Removing object..."));
    const auto progress = std::make_shared<std::atomic<int>>(-1);
    const auto compute = [&job, progress] {
      return compute_remove_object(job, nullptr,
                                   [progress](int percent) { progress->store(percent, std::memory_order_relaxed); });
    };
    bool run_inline = false;
#if defined(Q_OS_WASM) && defined(__EMSCRIPTEN_PTHREADS__)
    // Same pool contract as the processing renders: no idle pre-spawned
    // worker means the blocked path cannot rely on a lazy spawn, and the
    // candidate scan's own strip fan-out must fit the pool without the
    // compute's worker (worker_budget.hpp).
    const auto idle_pool = patchy::idle_prespawned_pool_workers();
    run_inline = idle_pool < 1;
    const patchy::BlockingFanoutBudgetScope fanout_budget(run_inline ? -1 : std::max(0, idle_pool - 3));
#endif
    try {
      if (run_inline) {
        computed = compute();
      } else {
        auto future = launch_async(compute);
        int last_percent = -1;
        wait_for_processing_operation([&] {
          const auto percent = progress->load(std::memory_order_relaxed);
          if (percent >= 0 && percent != last_percent) {
            last_percent = percent;
            set_processing_operation_message(tr("Removing object... %1%").arg(percent));
          }
          return future.wait_for(std::chrono::milliseconds(16)) == std::future_status::ready;
        });
        computed = future.get();
      }
    } catch (...) {
      end_processing_operation();
      throw;
    }
    end_processing_operation();
  }
  return commit_remove_object(job, computed);
}

// The healing membrane plus the write, shared by the stroke and the command:
// boundary cells (coverage 0) carry the destination-minus-source offsets; the
// solve interpolates them across the interior, so the pasted texture's tone
// bends smoothly into everything around the region instead of ring-matching
// per pixel.
QRect CanvasWidget::heal_mask_from_surroundings(QRect bounds, const std::vector<std::uint8_t>& mask,
                                                const QImage& snapshot, const SpotHealSourceMap& source_map,
                                                Layer& layer, bool clip_to_selection) {
  if (std::as_const(layer).pixels().format().bit_depth != BitDepth::UInt8) {
    // 16/32-bit layers heal a narrowed copy; only the healed pixels come back at
    // 8-bit precision (NarrowedLayerEdit).
    QRect healed;
    {
      const NarrowedLayerEdit narrowed(layer, {});
      healed = heal_mask_from_surroundings(bounds, mask, snapshot, source_map, layer, clip_to_selection);
    }
    if (!healed.isEmpty()) {
      active_edit_target_changed_impl(QRegion(healed), DocumentChangeReason::BrushStrokeFinished);
    }
    return healed;
  }
  const auto width = bounds.width();
  const auto height = bounds.height();
  begin_processing_operation();
  const auto* palette_snap = palette_snap_for_edits();
  const auto lock_transparent_pixels = active_layer_locks_transparent_pixels();
  if (!lock_transparent_pixels) {
    patchy::expand_layer_to_include_rect(layer, to_core_rect(bounds));
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

  const auto canvas_width = document_->width();
  const auto canvas_height = document_->height();
  const auto snapshot_pixel = [&](std::int32_t x, std::int32_t y) {
    const auto clamped_x = std::clamp(x, 0, canvas_width - 1);
    const auto clamped_y = std::clamp(y, 0, canvas_height - 1);
    return snapshot.constScanLine(clamped_y) + static_cast<std::size_t>(clamped_x) * 4U;
  };

  const auto cells = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  std::vector<std::uint8_t> interior(cells);
  std::vector<std::int16_t> offsets(cells * 3U);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto index = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                         static_cast<std::size_t>(x);
      interior[index] = mask[index] != 0U ? 1U : 0U;
      if (mask[index] != 0U) {
        continue;
      }
      const auto doc_x = bounds.left() + x;
      const auto doc_y = bounds.top() + y;
      const auto [sx, sy] = source_map.map(doc_x, doc_y);
      const auto* destination = snapshot_pixel(doc_x, doc_y);
      const auto* source = snapshot_pixel(sx, sy);
      for (int channel = 0; channel < 3; ++channel) {
        offsets[index * 3U + static_cast<std::size_t>(channel)] = static_cast<std::int16_t>(
            static_cast<int>(destination[channel]) - static_cast<int>(source[channel]));
      }
    }
  }
  tick_processing_operation();
  patchy::solve_heal_membrane(interior.data(), width, height, offsets.data());

  const bool clip = clip_to_selection && has_selection();
  // Pure per-pixel function writing disjoint rows, so the strip fan-out below
  // is byte-identical to the sequential walk.
  const auto heal_rows = [&](int row_begin, int row_end, bool allow_ticks) {
    QRect dirty;
    for (int y = row_begin; y < row_end; ++y) {
      if (allow_ticks) {
        tick_processing_operation();
      }
      for (int x = 0; x < width; ++x) {
        const auto index = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                           static_cast<std::size_t>(x);
        const auto footprint_coverage = mask[index];
        if (footprint_coverage == 0U) {
          continue;
        }
        const QPoint document_point(bounds.left() + x, bounds.top() + y);
        if (!layer_rect.contains(document_point)) {
          continue;
        }
        auto coverage = static_cast<float>(footprint_coverage) / 255.0F;
        if (clip) {
          coverage *= static_cast<float>(selection_alpha_at(document_point)) / 255.0F;
        }
        if (coverage <= 0.0F) {
          continue;
        }
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
        const auto [sx, sy] = source_map.map(document_point.x(), document_point.y());
        const auto* source = snapshot_pixel(sx, sy);
        std::array<std::uint8_t, 4> healed{};
        for (int channel = 0; channel < 3; ++channel) {
          healed[static_cast<std::size_t>(channel)] = clamp_byte(
              static_cast<float>(source[channel]) +
              static_cast<float>(offsets[index * 3U + static_cast<std::size_t>(channel)]));
        }
        healed[3] = source[3];
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
        dirty = dirty.united(QRect(document_point, QSize(1, 1)));
      }
    }
    return dirty;
  };

  const auto area = static_cast<std::int64_t>(width) * height;
  pixel_bytes = pixels.data();  // detaches shared storage on this thread
  const auto hardware_threads = patchy::hardware_worker_threads();
  // max_blocking_fanout_workers: this thread blocks on the row futures, so on
  // the wasm main thread the fan-out must fit the idle pthread pool.
  const auto workers =
      patchy::max_blocking_fanout_workers(std::clamp(std::min(height / 64, hardware_threads), 1, 16));
  QRect dirty;
  if (area >= kHealParallelArea && workers >= 2 && !qEnvironmentVariableIsSet("PATCHY_RENDER_SINGLE_THREADED")) {
    std::vector<std::future<QRect>> strips;
    strips.reserve(static_cast<std::size_t>(workers));
    const auto rows_per_strip = (height + workers - 1) / workers;
    for (int start = 0; start < height; start += rows_per_strip) {
      const auto end = std::min(start + rows_per_strip, height);
      strips.push_back(std::async(std::launch::async, heal_rows, start, end, false));
    }
    for (auto& strip : strips) {
      dirty = dirty.united(strip.get());
    }
  } else {
    dirty = heal_rows(0, height, true);
  }
  end_processing_operation();
  if (!dirty.isEmpty()) {
    active_edit_target_changed_impl(QRegion(dirty), DocumentChangeReason::BrushStrokeFinished);
  } else {
    notify_document_changed(DocumentChangeReason::BrushStrokeFinished);
  }
  return dirty;
}

// The Healing Brush's release-time heal (docs/healing.md "Classic Healing Brush"). The
// stroke painted the raw source as a preview (clone_brush_stamp_segment) and recorded each
// pixel's coverage, opacity included, in brush_stroke_alpha_caps_. Here the footprint is
// healed once: the destination-minus-source offsets on the cells just outside it (source =
// pixel + the stroke's Alt-click/Aligned offset, both read from the frozen gesture
// snapshot) are interpolated across it by the healing membrane of the expired US 6587592
// (core/heal_membrane.hpp, the OFFSET field only, no source gradients), and the healed
// color (source + membrane) is written over the stroke-start pixels by that coverage with
// the Mode. Diffusion sets how far along the boundary the offsets are averaged before the
// solve (Diffusion - 1 pixels), so film grain on the rim does not print into the heal.
// The source mapping is the user's offset alone: no patch search, no per-move heal or
// preview (US 8050498; docs/legal-constraints.md).
QRect CanvasWidget::finish_healing_brush_stroke() {
  if (!healing_membrane_stroke_) {
    return {};
  }
  healing_membrane_stroke_ = false;
  auto original = std::move(healing_stroke_original_pixels_);
  healing_stroke_original_pixels_.reset();
  const auto original_bounds = healing_stroke_original_bounds_;
  auto* layer = active_pixel_layer();
  if (document_ == nullptr || layer == nullptr || !original.has_value()) {
    return {};
  }
  const QRect canvas_rect(0, 0, document_->width(), document_->height());
  const auto footprint = healing_stroke_footprint_.intersected(canvas_rect);
  healing_stroke_footprint_ = QRect();
  const auto format = std::as_const(*layer).pixels().format();
  const bool deep = format.bit_depth != BitDepth::UInt8;
  if (footprint.isEmpty() || format.channels < 3 || (deep ? clone_source_deep_.empty() : clone_source_cache_.isNull())) {
    return {};
  }

  const int smoothing = std::clamp(healing_diffusion_, 1, 7) - 1;
  const int pad = smoothing + 1;
  const auto grid = footprint.adjusted(-pad, -pad, pad, pad).intersected(canvas_rect);
  const auto width = grid.width();
  const auto height = grid.height();
  const auto cells = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  const auto cell_index = [width](int x, int y) {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
  };
  // The footprint's coverage, read once here: the stroke map is not safe to share with
  // the worker strips below.
  std::vector<float> amount(cells, 0.0F);
  std::vector<std::uint8_t> interior(cells, 0U);
  for (int y = footprint.top(); y <= footprint.bottom(); ++y) {
    for (int x = footprint.left(); x <= footprint.right(); ++x) {
      if (const auto* cap = brush_stroke_alpha_caps_.find(x, y); cap != nullptr && *cap > 0.0F) {
        const auto index = cell_index(x - grid.left(), y - grid.top());
        amount[index] = std::min(*cap, 1.0F);
        interior[index] = 1U;
      }
    }
  }

  begin_processing_operation();
  const auto offset = clone_source_offset_;
  // 16/32-bit offsets carry four extra bits through the integer solver (it is linear,
  // so the scale divides back out); 8-bit stays on whole levels like Spot Healing.
  const int scale = deep ? 16 : 1;
  const auto canvas_width = canvas_rect.width();
  const auto canvas_height = canvas_rect.height();
  const auto& source_deep = clone_source_deep_;
  const auto& source_image = clone_source_cache_;
  const auto snapshot = [&](std::int32_t x, std::int32_t y) {
    x = std::clamp(x, 0, canvas_width - 1);
    y = std::clamp(y, 0, canvas_height - 1);
    if (deep) {
      return load_pixel(source_deep.format(), source_deep.pixel(x, y));
    }
    const auto* px = source_image.constScanLine(y) + static_cast<std::size_t>(x) * 4U;
    return std::array<float, 4>{static_cast<float>(px[0]), static_cast<float>(px[1]), static_cast<float>(px[2]),
                                static_cast<float>(px[3])};
  };
  std::vector<std::int32_t> raw(cells * 3U, 0);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto index = cell_index(x, y);
      if (interior[index] != 0U) {
        continue;
      }
      const auto doc_x = grid.left() + x;
      const auto doc_y = grid.top() + y;
      const auto destination = snapshot(doc_x, doc_y);
      const auto source = snapshot(doc_x + offset.x(), doc_y + offset.y());
      for (std::size_t channel = 0; channel < 3U; ++channel) {
        const auto value = std::lround((destination[channel] - source[channel]) * static_cast<float>(scale));
        raw[index * 3U + channel] = static_cast<std::int32_t>(std::clamp<long>(value, -32767L, 32767L));
      }
    }
  }
  std::vector<std::int16_t> offsets(cells * 3U);
  for (std::size_t i = 0; i < offsets.size(); ++i) {
    offsets[i] = static_cast<std::int16_t>(raw[i]);
  }
  if (smoothing > 0) {
    // Box means of the boundary offsets over the boundary cells within `smoothing`
    // (integral images, integer sums, round half away from zero), for the cells the
    // solve reads: those beside the footprint.
    const auto integral_width = static_cast<std::size_t>(width) + 1U;
    std::vector<std::int64_t> sums((static_cast<std::size_t>(height) + 1U) * integral_width * 3U, 0);
    std::vector<std::int64_t> counts((static_cast<std::size_t>(height) + 1U) * integral_width, 0);
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        const auto index = cell_index(x, y);
        const auto at = (static_cast<std::size_t>(y) + 1U) * integral_width + static_cast<std::size_t>(x) + 1U;
        const auto up = at - integral_width;
        const bool boundary = interior[index] == 0U;
        counts[at] = counts[up] + counts[at - 1U] - counts[up - 1U] + (boundary ? 1 : 0);
        for (std::size_t channel = 0; channel < 3U; ++channel) {
          sums[at * 3U + channel] = sums[up * 3U + channel] + sums[(at - 1U) * 3U + channel] -
                                    sums[(up - 1U) * 3U + channel] + (boundary ? raw[index * 3U + channel] : 0);
        }
      }
    }
    const auto box = [&](const std::vector<std::int64_t>& table, std::size_t stride, std::size_t channel, int x0,
                         int y0, int x1, int y1) {
      const auto row0 = static_cast<std::size_t>(y0) * integral_width;
      const auto row1 = (static_cast<std::size_t>(y1) + 1U) * integral_width;
      const auto col0 = static_cast<std::size_t>(x0);
      const auto col1 = static_cast<std::size_t>(x1) + 1U;
      return table[(row1 + col1) * stride + channel] - table[(row0 + col1) * stride + channel] -
             table[(row1 + col0) * stride + channel] + table[(row0 + col0) * stride + channel];
    };
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        const auto index = cell_index(x, y);
        if (interior[index] != 0U) {
          continue;
        }
        const bool beside_footprint = (x > 0 && interior[index - 1U] != 0U) ||
                                      (x + 1 < width && interior[index + 1U] != 0U) ||
                                      (y > 0 && interior[index - static_cast<std::size_t>(width)] != 0U) ||
                                      (y + 1 < height && interior[index + static_cast<std::size_t>(width)] != 0U);
        if (!beside_footprint) {
          continue;
        }
        const auto x0 = std::max(0, x - smoothing);
        const auto y0 = std::max(0, y - smoothing);
        const auto x1 = std::min(width - 1, x + smoothing);
        const auto y1 = std::min(height - 1, y + smoothing);
        const auto count = box(counts, 1U, 0U, x0, y0, x1, y1);
        for (std::size_t channel = 0; channel < 3U; ++channel) {
          const auto sum = box(sums, 3U, channel, x0, y0, x1, y1);
          const auto mean = sum >= 0 ? (sum + count / 2) / count : -((-sum + count / 2) / count);
          offsets[index * 3U + channel] = static_cast<std::int16_t>(std::clamp<std::int64_t>(mean, -32767, 32767));
        }
      }
    }
  }
  tick_processing_operation();
  patchy::solve_heal_membrane(interior.data(), width, height, offsets.data());

  const auto* palette_snap = palette_snap_for_edits();
  const auto lock_transparent_pixels = active_layer_locks_transparent_pixels();
  const auto paint_mode = PaintMode::of(healing_mode_blend(healing_mode_));
  const auto domain = deep_domain_for(format.bit_depth);
  auto& pixels = layer->pixels();
  const auto layer_bounds = layer->bounds();
  const auto layer_rect = to_qrect(layer_bounds);
  const auto channels = format.channels;
  const auto pixel_size = bytes_per_pixel(format);
  const auto stride = pixels.stride_bytes();
  const auto& original_pixels = std::as_const(*original);
  const auto original_rect = to_qrect(original_bounds);
  // Writes go through this span only (taken after the last event pump; see
  // heal_mask_from_surroundings for the copy-on-write rule).
  std::span<std::uint8_t> pixel_bytes;
  const auto heal_rows = [&](int row_begin, int row_end, bool allow_ticks) {
    QRect dirty;
    for (int y = row_begin; y < row_end; ++y) {
      if (allow_ticks) {
        tick_processing_operation();
      }
      for (int x = footprint.left(); x <= footprint.right(); ++x) {
        const auto index = cell_index(x - grid.left(), y - grid.top());
        if (interior[index] == 0U) {
          continue;
        }
        const QPoint document_point(x, y);
        if (!layer_rect.contains(document_point)) {
          continue;
        }
        auto* dst = pixel_bytes.data() + static_cast<std::size_t>(y - layer_bounds.y) * stride +
                    static_cast<std::size_t>(x - layer_bounds.x) * pixel_size;
        // The pixel before the stroke (transparent where the layer grew under it).
        std::array<float, 4> before{0.0F, 0.0F, 0.0F, 0.0F};
        if (original_rect.contains(document_point)) {
          before = load_pixel(original_pixels.format(),
                              original_pixels.pixel(x - original_bounds.x, y - original_bounds.y));
        }
        if (channels < 4) {
          before[3] = 255.0F;
        }
        const auto source = snapshot(x + offset.x(), y + offset.y());
        std::array<float, 4> healed{};
        for (std::size_t channel = 0; channel < 3U; ++channel) {
          const auto value = source[channel] + static_cast<float>(offsets[index * 3U + channel]) /
                                                   static_cast<float>(scale);
          healed[channel] = format.bit_depth == BitDepth::Float32 ? std::max(0.0F, value)
                                                                    : std::clamp(value, 0.0F, 255.0F);
        }
        healed[3] = source[3];
        const auto coverage = amount[index];
        std::array<float, 4> value = before;
        if (!deep) {
          // 8-bit: the bytes the classic write and the paint compositor take.
          std::array<std::uint8_t, 4> out{clamp_byte(before[0]), clamp_byte(before[1]), clamp_byte(before[2]),
                                          clamp_byte(before[3])};
          const std::array<std::uint8_t, 4> healed_bytes{clamp_byte(healed[0]), clamp_byte(healed[1]),
                                                         clamp_byte(healed[2]), clamp_byte(healed[3])};
          if (!paint_mode.is_normal()) {
            out = patchy::composite_paint_rgba8(out, {healed_bytes[0], healed_bytes[1], healed_bytes[2]},
                                                coverage * (static_cast<float>(healed_bytes[3]) / 255.0F),
                                                paint_mode, x, y, channels >= 4, lock_transparent_pixels);
          } else if (channels >= 4 && !lock_transparent_pixels) {
            blend_straight_rgba(out.data(), healed_bytes.data(), coverage);
          } else {
            const auto effective = coverage * (static_cast<float>(healed_bytes[3]) / 255.0F);
            for (std::size_t channel = 0; channel < 3U; ++channel) {
              out[channel] = clamp_byte(static_cast<float>(healed_bytes[channel]) * effective +
                                        static_cast<float>(out[channel]) * (1.0F - effective));
            }
          }
          std::copy_n(out.begin(), std::min<std::size_t>(channels, 4U), dst);
          if (palette_snap != nullptr) {
            patchy::snap_pixel_to_palette(dst, channels, *palette_snap);
          }
        } else {
          if (!paint_mode.is_normal()) {
            value = patchy::composite_paint_deep(before, {healed[0], healed[1], healed[2]},
                                                 coverage * (healed[3] / 255.0F), paint_mode, x, y, channels >= 4,
                                                 lock_transparent_pixels, domain);
          } else if (channels >= 4 && !lock_transparent_pixels) {
            // blend_straight_rgba in float, as the deep Clone stamp writes.
            const auto source_alpha = healed[3] / 255.0F;
            const auto destination_alpha = before[3] / 255.0F;
            const auto out_alpha = source_alpha * coverage + destination_alpha * (1.0F - coverage);
            if (coverage >= 0.999F) {
              value = healed;
            } else if (out_alpha <= 0.0F) {
              value = {healed[0], healed[1], healed[2], 0.0F};
            } else {
              for (std::size_t channel = 0; channel < 3U; ++channel) {
                value[channel] = (healed[channel] * source_alpha * coverage +
                                  before[channel] * destination_alpha * (1.0F - coverage)) /
                                 out_alpha;
              }
              value[3] = out_alpha * 255.0F;
            }
          } else {
            const auto effective = coverage * (healed[3] / 255.0F);
            for (std::size_t channel = 0; channel < 3U; ++channel) {
              value[channel] = healed[channel] * effective + before[channel] * (1.0F - effective);
            }
          }
          store_pixel(format, dst, value);
        }
        dirty = dirty.united(QRect(document_point, QSize(1, 1)));
      }
    }
    return dirty;
  };

  const auto area = static_cast<std::int64_t>(footprint.width()) * footprint.height();
  pixel_bytes = pixels.data();  // detaches shared storage on this thread
  const auto hardware_threads = patchy::hardware_worker_threads();
  const auto workers = patchy::max_blocking_fanout_workers(
      std::clamp(std::min(footprint.height() / 64, hardware_threads), 1, 16));
  QRect dirty;
  if (area >= kHealParallelArea && workers >= 2 && !qEnvironmentVariableIsSet("PATCHY_RENDER_SINGLE_THREADED")) {
    std::vector<std::future<QRect>> strips;
    strips.reserve(static_cast<std::size_t>(workers));
    const auto rows_per_strip = (footprint.height() + workers - 1) / workers;
    for (int start = footprint.top(); start <= footprint.bottom(); start += rows_per_strip) {
      const auto end = std::min(start + rows_per_strip, footprint.bottom() + 1);
      strips.push_back(std::async(std::launch::async, heal_rows, start, end, false));
    }
    for (auto& strip : strips) {
      dirty = dirty.united(strip.get());
    }
  } else {
    dirty = heal_rows(footprint.top(), footprint.bottom() + 1, true);
  }
  end_processing_operation();
  return dirty;
}

// The translucent capsule trail shown while a Spot Healing stroke is being
// drawn - the raw footprint only, like Quick Select's overlay; no heal result
// is computed or shown mid-drag.
void CanvasWidget::draw_spot_heal_stroke_overlay(QPainter& painter) const {
  if (!spot_healing_stroke_active_ || spot_heal_stroke_points_.isEmpty()) {
    return;
  }
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);
  QPolygonF widget_points;
  widget_points.reserve(spot_heal_stroke_points_.size());
  for (const auto& point : spot_heal_stroke_points_) {
    widget_points << widget_position_f(point + QPointF(0.5, 0.5));
  }
  const auto footprint_width = std::max(3.0, static_cast<double>(std::max(1, brush_size_)) * zoom_);
  const QColor fill(90, 170, 255, 70);
  if (widget_points.size() == 1) {
    painter.setPen(Qt::NoPen);
    painter.setBrush(fill);
    painter.drawEllipse(widget_points.front(), footprint_width / 2.0, footprint_width / 2.0);
  } else {
    painter.setPen(QPen(fill, footprint_width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawPolyline(widget_points);
  }
  painter.restore();
}

void CanvasWidget::set_selection_context_actions_callback(std::function<QList<QAction*>()> callback) {
  selection_context_actions_callback_ = std::move(callback);
}

void CanvasWidget::set_selection_tool_context_actions_callback(std::function<QList<QAction*>()> callback) {
  selection_tool_context_actions_callback_ = std::move(callback);
}

void CanvasWidget::set_zoom_tool_context_actions_callback(std::function<QList<QAction*>()> callback) {
  zoom_tool_context_actions_callback_ = std::move(callback);
}

void CanvasWidget::set_screen_mode_context_actions_callback(std::function<QList<QAction*>()> callback) {
  screen_mode_context_actions_callback_ = std::move(callback);
}

void CanvasWidget::set_unused_escape_callback(std::function<bool()> callback) {
  unused_escape_callback_ = std::move(callback);
}

void CanvasWidget::set_shape_context_actions_callback(std::function<QList<QAction*>()> callback) {
  shape_context_actions_callback_ = std::move(callback);
}

void CanvasWidget::set_layer_context_actions_callback(std::function<QList<QAction*>()> callback) {
  layer_context_actions_callback_ = std::move(callback);
}

void CanvasWidget::set_path_context_actions_callback(std::function<QList<QAction*>()> callback) {
  path_context_actions_callback_ = std::move(callback);
}

}  // namespace patchy::ui
