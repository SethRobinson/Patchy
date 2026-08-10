// CanvasWidget's Spot Healing stroke engine. The drag only accumulates a soft
// brush footprint (Grayscale8 mask + the overlay polyline); the heal runs ONCE
// in finish_spot_heal_stroke() after the gesture ends: fixed mirror geometry
// from core/spot_heal.hpp picks each covered pixel's source just outside the
// footprint boundary, and the classic frequency-separation tone match
// (healing_sample) carries that texture into the rim tone. The source choice
// depends only on the footprint's shape, never on pixel content. Do not add
// patch search, synthesis-by-example, reshuffling, gradient-domain solves, or
// content-driven source selection: those families are claimed by Adobe's
// active PatchMatch patents (US 8285055, US 8340463, US 8355592, into 2031)
// and gradient-domain compositing (US 9058699, to 2029); live classify-and-
// display during brush input is claimed by US 8050498 (to Nov 3, 2029), so
// the drag shows only the raw footprint overlay. Classic user-directed
// healing (US 6587592) expired in 2021. See docs/legal-constraints.md.

#include "ui/canvas_widget.hpp"
#include "ui/canvas_widget_shared.hpp"

#include "core/blend_math.hpp"
#include "core/pixel_tools.hpp"
#include "core/spot_heal.hpp"
#include "ui/edit_conversions.hpp"
#include "ui/qt_geometry.hpp"

#include <QPainter>
#include <QPen>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

namespace patchy::ui {

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

  begin_processing_operation();
  const auto field =
      spot_heal_mirror_sources(mask.data(), to_core_rect(bounds), document_->width(), document_->height());
  if (field.empty()) {
    end_processing_operation();
    drop_stroke_state();
    report_status_error(tr("Spot healing needs unpainted pixels around the stroke to sample"));
    update();
    return;
  }

  if (!begin_edit(tr("Spot healing"))) {
    end_processing_operation();
    drop_stroke_state();
    update();
    return;
  }
  auto* layer = active_pixel_layer();
  if (layer == nullptr) {
    end_processing_operation();
    drop_stroke_state();
    return;
  }

  const auto* palette_snap = palette_snap_for_edits();
  const auto lock_transparent_pixels = active_layer_locks_transparent_pixels();
  if (!lock_transparent_pixels) {
    patchy::expand_layer_to_include_rect(*layer, to_core_rect(bounds));
  }
  auto& pixels = layer->pixels();
  const auto layer_bounds = layer->bounds();
  const auto layer_rect = to_qrect(layer_bounds);
  const auto channels = pixels.format().channels;
  const auto tone_radius =
      std::max(1, (std::max(1, brush_size_) * (9 - std::clamp(healing_diffusion_, 1, 7)) + 15) / 16);

  QRect dirty;
  for (int y = 0; y < height; ++y) {
    tick_processing_operation();
    for (int x = 0; x < width; ++x) {
      const auto index = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                         static_cast<std::size_t>(x);
      const auto footprint_coverage = mask[index];
      if (footprint_coverage == 0U) {
        continue;
      }
      const QPoint document_point(bounds.left() + x, bounds.top() + y);
      if (!layer_rect.contains(document_point) || !selection_allows(document_point)) {
        continue;
      }
      auto coverage = static_cast<float>(footprint_coverage) / 255.0F;
      if (has_selection()) {
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

      auto row = pixels.row(document_point.y() - layer_bounds.y);
      auto* dst = row.data() + static_cast<std::size_t>(document_point.x() - layer_bounds.x) * channels;
      if (lock_transparent_pixels && channels >= 4 && dst[3] == 0) {
        continue;
      }
      // Texture from the mirror source, tone matched AT THE RIM POINT rather
      // than at the pixel itself: the pixel sits inside the defect, so its own
      // ring tone would preserve the blemish it is meant to remove.
      const QPoint source(field.source_x[index], field.source_y[index]);
      const QPoint rim(field.rim_x[index], field.rim_y[index]);
      const auto healed = healing_sample(source_snapshot, source, rim, tone_radius);
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
  end_processing_operation();
  drop_stroke_state();
  if (!dirty.isEmpty()) {
    active_edit_target_changed_impl(QRegion(dirty), DocumentChangeReason::BrushStrokeFinished);
  } else {
    notify_document_changed(DocumentChangeReason::BrushStrokeFinished);
  }
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

}  // namespace patchy::ui
