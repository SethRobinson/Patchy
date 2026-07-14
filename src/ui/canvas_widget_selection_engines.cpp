// CanvasWidget's selection engines, split out of canvas_widget.cpp: the Magic
// Wand flood select with its tolerance/contiguous/sample-all-layers options,
// the Quick Select stroke lifecycle (seed stamping during the drag, the
// release-time segmentation in finish_quick_select_stroke, the footprint
// overlay and the brush cursor) with its size/sample/enhance-edge options, and
// the Magnetic Lasso live-wire engine (anchor tracing and cooling, the
// magnetic close, cancel, and its width/contrast/frequency options). The
// Quick Select patent-constraint comments (Adobe US 8050498: the stroke is
// classified ONCE on mouse-release, never live during the drag) moved here
// verbatim and remain binding - see AGENTS.md "Legal constraints (patents and
// licensing)" and docs/selection-tools.md before changing anything here. Pure
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

}  // namespace

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

void CanvasWidget::set_quick_select_size(int size) {
  const QRect previous_outline =
      brush_hover_position_valid_ && brush_outline_uses_overlay() ? brush_hover_outline_rect() : QRect();
  quick_select_size_ = std::clamp(size, 1, 512);
  update_tool_cursor();
  invalidate_brush_hover_outline(previous_outline);
}

int CanvasWidget::quick_select_size() const noexcept {
  return quick_select_size_;
}

void CanvasWidget::set_quick_select_sample_all_layers(bool enabled) noexcept {
  quick_select_sample_all_layers_ = enabled;
}

bool CanvasWidget::quick_select_sample_all_layers() const noexcept {
  return quick_select_sample_all_layers_;
}

void CanvasWidget::set_quick_select_enhance_edge(bool enabled) noexcept {
  quick_select_enhance_edge_ = enabled;
}

bool CanvasWidget::quick_select_enhance_edge() const noexcept {
  return quick_select_enhance_edge_;
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
        combine_selection_from_mask(feather_bounds, std::move(padded));
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

void CanvasWidget::begin_quick_select_stroke(QPoint document_point) {
  quick_selecting_ = true;
  quick_select_seed_mask_ = QImage();
  quick_select_seed_bounds_ = QRect();
  quick_select_stroke_points_.clear();
  quick_select_stroke_points_ << QPointF(document_point);
  quick_select_last_document_point_ = document_point;
  stamp_quick_select_segment(document_point, document_point);
}

void CanvasWidget::extend_quick_select_stroke(QPoint document_point) {
  if (!quick_selecting_ || document_point == quick_select_last_document_point_) {
    return;
  }
  stamp_quick_select_segment(quick_select_last_document_point_, document_point);
  quick_select_last_document_point_ = document_point;
  quick_select_stroke_points_ << QPointF(document_point);
  update();
}

void CanvasWidget::cancel_quick_select_stroke() {
  quick_selecting_ = false;
  quick_select_seed_mask_ = QImage();
  quick_select_seed_bounds_ = QRect();
  quick_select_stroke_points_.clear();
}

// Rasterizes one drag segment of the brush footprint into the doc-sized seed mask (a round-cap
// stroke gives an exact capsule, no dab spacing artifacts). Painting clips to the canvas, so
// strokes that wander into the grey margin simply contribute fewer seeds.
void CanvasWidget::stamp_quick_select_segment(QPoint from, QPoint to) {
  if (document_ == nullptr) {
    return;
  }
  if (quick_select_seed_mask_.isNull()) {
    quick_select_seed_mask_ = QImage(document_->width(), document_->height(), QImage::Format_Grayscale8);
    if (quick_select_seed_mask_.isNull()) {
      return;
    }
    quick_select_seed_mask_.fill(0);
  }
  QPainter painter(&quick_select_seed_mask_);
  painter.setRenderHint(QPainter::Antialiasing, false);
  const auto size = std::max(1, quick_select_size_);
  const auto radius = static_cast<double>(size) / 2.0;
  if (from == to) {
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::white);
    painter.drawEllipse(QPointF(from) + QPointF(0.5, 0.5), radius, radius);
  } else {
    painter.setPen(QPen(Qt::white, size, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawLine(QPointF(from) + QPointF(0.5, 0.5), QPointF(to) + QPointF(0.5, 0.5));
  }
  // Tiny brushes can rasterise to nothing with antialiasing off; pin the endpoints.
  painter.setPen(Qt::NoPen);
  painter.fillRect(QRect(from, QSize(1, 1)), Qt::white);
  painter.fillRect(QRect(to, QSize(1, 1)), Qt::white);
  painter.end();
  const auto pad = size / 2 + 2;
  const auto segment_rect = QRect(QPoint(std::min(from.x(), to.x()), std::min(from.y(), to.y())),
                                  QPoint(std::max(from.x(), to.x()), std::max(from.y(), to.y())))
                                .adjusted(-pad, -pad, pad, pad);
  quick_select_seed_bounds_ =
      quick_select_seed_bounds_.isNull() ? segment_rect : quick_select_seed_bounds_.united(segment_rect);
}

// Runs the release-time segmentation and commits the stroke as one undo entry. The whole
// classification deliberately happens here, after the input gesture ends: live classify-and-
// display while the brush input is still being received is claimed by Adobe's US 8050498
// (active until Nov 3, 2029) — until then the drag shows only the raw footprint overlay.
void CanvasWidget::finish_quick_select_stroke() {
  if (!quick_selecting_) {
    return;
  }
  quick_selecting_ = false;
  const auto clear_before_edit = [this] {
    selection_before_edit_ = QRegion();
    selection_display_region_before_edit_ = QRegion();
    selection_mask_before_edit_bounds_ = {};
    selection_mask_before_edit_alpha_ = QImage();
  };
  const auto drop_stroke_state = [this] {
    quick_select_seed_mask_ = QImage();
    quick_select_seed_bounds_ = QRect();
    quick_select_stroke_points_.clear();
  };
  if (document_ == nullptr || quick_select_seed_mask_.isNull()) {
    drop_stroke_state();
    clear_before_edit();
    return;
  }

  QImage source_image;
  if (quick_select_sample_all_layers_) {
    ensure_render_cache();
    source_image = render_cache_;
  } else {
    const auto* layer = active_pixel_layer();
    if (layer == nullptr) {
      if (status_callback_) {
        status_callback_(tr("Select a pixel layer before using Quick Select"));
      }
      drop_stroke_state();
      clear_before_edit();
      update();
      return;
    }
    source_image = active_layer_wand_sample_image(*layer, QSize(document_->width(), document_->height()));
  }
  if (source_image.isNull() || source_image.format() != QImage::Format_RGBA8888) {
    drop_stroke_state();
    clear_before_edit();
    return;
  }

  const auto width = source_image.width();
  const auto height = source_image.height();
  begin_processing_operation();

  // Copy the stamped footprint out of its QImage (rows are 4-byte padded).
  std::vector<std::uint8_t> seeds(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
  for (int y = 0; y < height; ++y) {
    std::memcpy(seeds.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width),
                quick_select_seed_mask_.constScanLine(y), static_cast<std::size_t>(width));
  }

  // The pre-stroke selection, thresholded to a hard mask (the solve is binary; the combine
  // below replays the combine mode against the soft before-edit state, so a feathered existing
  // selection keeps its soft edges everywhere the stroke did not touch).
  std::vector<std::uint8_t> base;
  const std::uint8_t* base_pixels = nullptr;
  if (selection_operation_ != SelectionMode::Replace && !selection_.isEmpty()) {
    base.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
    if (!selection_mask_alpha_.isNull() && !selection_mask_bounds_.isEmpty()) {
      const auto bounds = selection_mask_bounds_.intersected(QRect(0, 0, width, height));
      for (int y = bounds.top(); y <= bounds.bottom(); ++y) {
        const auto* row = selection_mask_alpha_.constScanLine(y - selection_mask_bounds_.top());
        auto* out = base.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
        for (int x = bounds.left(); x <= bounds.right(); ++x) {
          out[x] = row[x - selection_mask_bounds_.left()] >= 128 ? 255 : 0;
        }
      }
    } else {
      for (const QRect& rect : selection_) {
        const auto clipped = rect.intersected(QRect(0, 0, width, height));
        if (clipped.isEmpty()) {
          continue;
        }
        for (int y = clipped.top(); y <= clipped.bottom(); ++y) {
          std::memset(base.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                          static_cast<std::size_t>(clipped.left()),
                      255, static_cast<std::size_t>(clipped.width()));
        }
      }
    }
    base_pixels = base.data();
  }

  patchy::QuickSelectParams params;
  params.brush_radius = std::max(1, quick_select_size_ / 2);
  params.subtract = selection_operation_ == SelectionMode::Subtract;
  params.enhance_edge = quick_select_enhance_edge_;
  const auto seed_bounds = quick_select_seed_bounds_.intersected(QRect(0, 0, width, height));
  const auto result = patchy::quick_select_segment(
      source_image.constBits(), width, height, source_image.bytesPerLine(), base_pixels, seeds.data(),
      patchy::Rect{seed_bounds.x(), seed_bounds.y(), seed_bounds.width(), seed_bounds.height()}, params);
  drop_stroke_state();

  if (result.empty()) {
    end_processing_operation();
    restore_selection_before_edit();
    record_selection_history(tr("Quick Select"), selection_snapshot_before_edit());
    clear_before_edit();
    update();
    return;
  }

  std::vector<QRect> run_rects;
  run_rects.reserve(result.delta_runs.size());
  qulonglong changed_pixels = 0;
  for (const auto& run : result.delta_runs) {
    run_rects.push_back(QRect(run.x0, run.y, run.x1 - run.x0 + 1, 1));
    changed_pixels += static_cast<qulonglong>(run.x1 - run.x0 + 1);
  }
  QRegion delta_region;
  delta_region.setRects(run_rects.data(), static_cast<int>(run_rects.size()));

  if (selection_feather_radius_ > 0) {
    const QRect hard_bounds(result.delta_bounds.x, result.delta_bounds.y, result.delta_bounds.width,
                            result.delta_bounds.height);
    QImage hard_mask(hard_bounds.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < hard_bounds.height(); ++y) {
      std::memcpy(hard_mask.scanLine(y),
                  result.delta_mask.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(hard_bounds.width()),
                  static_cast<std::size_t>(hard_bounds.width()));
    }
    const auto feather = selection_feather_radius_;
    const auto padding = feather_mask_padding(feather);
    auto feather_bounds = hard_bounds.adjusted(-padding, -padding, padding, padding);
    feather_bounds = feather_bounds.intersected(QRect(0, 0, width, height));
    if (!feather_bounds.isEmpty()) {
      QImage padded(feather_bounds.size(), QImage::Format_Grayscale8);
      padded.fill(0);
      QPainter painter(&padded);
      painter.drawImage(hard_bounds.topLeft() - feather_bounds.topLeft(), hard_mask);
      painter.end();
      padded = feather_blur_mask(std::move(padded), feather);
      combine_selection_from_mask(feather_bounds, std::move(padded));
    } else {
      combine_selection_from_region(delta_region);
    }
  } else {
    combine_selection_from_region(delta_region);
  }
  end_processing_operation();

  if (status_callback_) {
    status_callback_(params.subtract ? tr("Quick Select removed %1 px").arg(changed_pixels)
                                     : tr("Quick Select selected %1 px").arg(changed_pixels));
  }
  record_selection_history(tr("Quick Select"), selection_snapshot_before_edit());
  clear_before_edit();
  // Photoshop parity: a stroke in New mode leaves the tool in Add so the next stroke grows the
  // selection instead of replacing it.
  if (selection_operation_ == SelectionMode::Replace && !selection_.isEmpty()) {
    set_selection_mode(SelectionMode::Add);
  }
  update();
}

// The translucent capsule trail shown while a Quick Select stroke is being drawn. This is the
// raw brush footprint only — no classification result is computed or shown mid-drag (see
// finish_quick_select_stroke for why).
void CanvasWidget::draw_quick_select_stroke_overlay(QPainter& painter) const {
  if (!quick_selecting_ || quick_select_stroke_points_.isEmpty()) {
    return;
  }
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);
  QPolygonF widget_points;
  widget_points.reserve(quick_select_stroke_points_.size());
  for (const auto& point : quick_select_stroke_points_) {
    widget_points << widget_position_f(point + QPointF(0.5, 0.5));
  }
  const auto footprint_width = std::max(3.0, static_cast<double>(quick_select_size_) * zoom_);
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

// Zoom-scaled brush circle with the combine-mode badge in its center (Photoshop-style).
// Rebuilt per call like the Brush cursor: it depends on the live size and zoom.
QCursor CanvasWidget::quick_select_cursor(SelectionMode mode) const {
  const auto diameter =
      std::max(3, static_cast<int>(std::round(static_cast<double>(quick_select_size_) * zoom_)));
  const auto extent = std::clamp(diameter + 5, 21, 160);
  QPixmap pixmap(extent, extent);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  const QPoint center(extent / 2, extent / 2);
  const auto radius = std::max(2, std::min(diameter, extent - 5) / 2);
  painter.setPen(QPen(QColor(25, 25, 25), 1));
  painter.setBrush(Qt::NoBrush);
  painter.drawEllipse(center, radius, radius);
  painter.setPen(QPen(QColor(255, 255, 255), 1));
  painter.drawEllipse(center, std::max(1, radius - 1), std::max(1, radius - 1));
  if (mode == SelectionMode::Replace) {
    painter.setPen(QPen(QColor(255, 255, 255), 1));
    painter.drawLine(center + QPoint(-3, 0), center + QPoint(3, 0));
    painter.drawLine(center + QPoint(0, -3), center + QPoint(0, 3));
  } else {
    paint_selection_mode_badge(painter, mode, QPointF(center));
  }
  painter.end();
  return QCursor(pixmap, center.x(), center.y());
}

void CanvasWidget::start_magnetic_lasso(QPoint document_point, Qt::KeyboardModifiers modifiers) {
  ensure_render_cache();
  if (render_cache_.isNull() || render_cache_.format() != QImage::Format_RGBA8888) {
    return;
  }
  // The engine holds a non-owning pointer, so keep our own shared handle on the
  // trace-start composite: later render_cache_ refreshes reassign that member
  // without touching this copy's buffer. Edges deliberately come from the
  // trace-start image for the whole trace.
  magnetic_source_image_ = render_cache_;
  magnetic_engine_.set_image(magnetic_source_image_.constBits(), magnetic_source_image_.width(),
                             magnetic_source_image_.height(), magnetic_source_image_.bytesPerLine());
  patchy::MagneticLassoParams params;
  params.width = magnetic_lasso_width_;
  params.edge_contrast = magnetic_lasso_edge_contrast_;
  magnetic_engine_.set_params(params);
  magnetic_lassoing_ = true;
  selection_edges_visible_ = true;
  // The combine mode latches at the starting click; Shift/Alt during the trace
  // never re-latch (the trace itself owns the pointer until the path closes).
  selection_operation_ = selection_operation(modifiers);
  const auto snapped = magnetic_snap(document_point);
  magnetic_anchors_.clear();
  magnetic_anchor_path_index_.clear();
  magnetic_committed_path_.clear();
  magnetic_live_path_.clear();
  magnetic_anchors_ << snapped;
  magnetic_anchor_path_index_ << 0;
  magnetic_committed_path_ << snapped;
  magnetic_engine_.set_anchor({snapped.x(), snapped.y()});
}

QPoint CanvasWidget::magnetic_snap(QPoint document_point) const {
  const auto snapped = magnetic_engine_.snap({document_point.x(), document_point.y()});
  return {snapped.x, snapped.y};
}

void CanvasWidget::extract_magnetic_live_path(QPoint document_point, bool snap_target) {
  const auto snapped = snap_target ? magnetic_snap(document_point) : document_point;
  const auto path = magnetic_engine_.path_to({snapped.x(), snapped.y()});
  magnetic_live_path_.clear();
  magnetic_live_path_.reserve(static_cast<int>(path.size()));
  for (const auto& p : path) {
    magnetic_live_path_ << QPoint(p.x, p.y);
  }
}

int CanvasWidget::magnetic_anchor_spacing() const noexcept {
  // Frequency 0..100 maps to an auto-anchor spacing of 48..8 SCREEN pixels
  // (the Photoshop default 57 lands near 26). Screen pixels, not document
  // pixels: Photoshop drops fastening points by traced screen distance, so
  // the boxes stay equally dense at any zoom.
  return std::clamp(48 - (40 * magnetic_lasso_frequency_) / 100, 6, 48);
}

void CanvasWidget::cool_magnetic_live_path() {
  // Boundary cooling, v1: a live segment more than two spacings long has a
  // stable prefix; freeze that prefix into the committed path and re-anchor at
  // its end. The remaining suffix stays a valid anchor->cursor path, so no
  // re-extraction is needed here (the next hover move re-solves anyway).
  // Spacing converts from screen to document pixels by the zoom: at high zoom
  // anchors must drop every few DOCUMENT pixels or a whole screenful of
  // tracing stays one long unanchored segment that re-solves and swings on
  // every move (the July 2026 "no little boxes appear" bug).
  const auto spacing = std::max(
      2, static_cast<int>(std::lround(magnetic_anchor_spacing() / std::max(0.01, zoom_))));
  while (magnetic_live_path_.size() > 2 * spacing + 1) {
    for (int i = 1; i <= spacing; ++i) {
      magnetic_committed_path_ << magnetic_live_path_[i];
    }
    const auto anchor = magnetic_committed_path_.last();
    magnetic_anchors_ << anchor;
    magnetic_anchor_path_index_ << magnetic_committed_path_.size() - 1;
    magnetic_live_path_.remove(0, spacing);
    magnetic_engine_.set_anchor({anchor.x(), anchor.y()});
  }
}

void CanvasWidget::add_magnetic_anchor() {
  if (magnetic_live_path_.size() < 2) {
    return;
  }
  for (int i = 1; i < magnetic_live_path_.size(); ++i) {
    magnetic_committed_path_ << magnetic_live_path_[i];
  }
  const auto anchor = magnetic_committed_path_.last();
  magnetic_anchors_ << anchor;
  magnetic_anchor_path_index_ << magnetic_committed_path_.size() - 1;
  magnetic_live_path_.clear();
  magnetic_engine_.set_anchor({anchor.x(), anchor.y()});
}

void CanvasWidget::pop_magnetic_anchor() {
  if (!magnetic_lassoing_) {
    return;
  }
  if (magnetic_anchors_.size() <= 1) {
    cancel_magnetic_lasso();
    return;
  }
  magnetic_anchors_.removeLast();
  magnetic_anchor_path_index_.removeLast();
  const auto keep = magnetic_anchor_path_index_.last() + 1;
  magnetic_committed_path_.remove(keep, magnetic_committed_path_.size() - keep);
  const auto anchor = magnetic_committed_path_.last();
  magnetic_engine_.set_anchor({anchor.x(), anchor.y()});
  magnetic_live_path_.clear();
  if (document_ != nullptr) {
    // Reconnect the wire to wherever the cursor currently rests. Deliberately no
    // cooling pass: Backspace with a distant stationary cursor must not
    // instantly re-drop the anchor it just removed.
    extract_magnetic_live_path(clamped_document_point(*document_, document_position(last_mouse_position_)));
  }
  update();
}

void CanvasWidget::finish_magnetic_lasso(bool magnetic_close) {
  if (!magnetic_lassoing_) {
    return;
  }
  auto polygon = magnetic_committed_path_;
  for (int i = 1; i < magnetic_live_path_.size(); ++i) {
    polygon << magnetic_live_path_[i];
  }
  // Photoshop parity: double-click/Enter close the border with a MAGNETIC
  // segment back to the start (Alt = straight). A straight close slices across
  // the artwork whenever the finish point is far from the start. Must run
  // before cancel_magnetic_lasso() drops the engine's image. In featureless
  // ground path_to's flat fallback degrades this to the straight line anyway.
  if (magnetic_close && polygon.size() >= 2) {
    const auto last = polygon.last();
    const auto first = polygon.first();
    if ((last - first).manhattanLength() > 2) {
      magnetic_engine_.set_anchor({last.x(), last.y()});
      const auto closing = magnetic_engine_.path_to({first.x(), first.y()});
      // Anti-retrace: when start and finish sit on the SAME edge, the cheapest
      // "magnetic" close is the traced boundary run backwards, which collapses
      // the polygon to sliver selections under winding fill (the July 2026
      // "two tiny areas" bug). If the closing segment mostly hugs the traced
      // path, drop it and let the implicit straight close connect the ends.
      QSet<qint64> traced_cells;
      for (const auto& point : polygon) {
        traced_cells.insert((static_cast<qint64>(point.x() >> 2) << 32) |
                            static_cast<quint32>(point.y() >> 2));
      }
      const auto near_traced = [&traced_cells](const patchy::PointI32& p) {
        for (int cy = -1; cy <= 1; ++cy) {
          for (int cx = -1; cx <= 1; ++cx) {
            if (traced_cells.contains((static_cast<qint64>((p.x >> 2) + cx) << 32) |
                                      static_cast<quint32>((p.y >> 2) + cy))) {
              return true;
            }
          }
        }
        return false;
      };
      // The stretch beside each endpoint always overlaps the trace; judge the middle.
      const auto skip = std::min<std::size_t>(8, closing.size() / 4);
      std::size_t checked = 0;
      std::size_t overlapping = 0;
      for (std::size_t i = skip; i + skip < closing.size(); ++i) {
        ++checked;
        if (near_traced(closing[i])) {
          ++overlapping;
        }
      }
      const bool retraces = checked > 0 && overlapping * 2 > checked;
      if (!retraces) {
        for (std::size_t i = 1; i + 1 < closing.size(); ++i) {
          polygon << QPoint(closing[i].x, closing[i].y);
        }
      }
    }
  }
  cancel_magnetic_lasso();
  if (document_ == nullptr || polygon.size() < 3) {
    return;
  }
  // From here this is the Lasso commit path: the final gap closes implicitly
  // (QRegion/QPainterPath) and the result combines under the operation latched
  // at the starting click. The traced boundary is all diagonal staircase, so
  // with Anti-alias on it must commit through the mask path for partial edge
  // coverage - the QRegion path is hard-edged (same rule as the marquee's
  // rounded corners).
  selection_before_edit_ = selection_;
  selection_display_region_before_edit_ = selection_display_region_;
  selection_mask_before_edit_bounds_ = selection_mask_bounds_;
  selection_mask_before_edit_alpha_ = selection_mask_alpha_;
  if (selection_feather_radius_ > 0 || selection_antialias_) {
    // The traced boundary is a dense integer pixel chain: nearly every segment
    // is grid-aligned, so rasterizing it directly gives the anti-aliaser
    // nothing to smooth and deletes cut hard stairs. A 1-2-1 neighbor average
    // turns staircase runs into oblique fractional segments (axis-aligned runs
    // are unchanged, so straight edges stay crisp) and the AA mask picks up
    // Photoshop-like partial coverage along slopes.
    auto smoothed = QPolygonF(polygon);
    if (smoothed.size() >= 8) {
      // Two closed-loop 1-2-1 passes (~a 1-4-6-4-1 kernel): shallow staircases
      // gain fractional coordinates in nearly every column, so the whole slope
      // anti-aliases instead of just the step corners. Support is +-2 chain
      // pixels, so deliberate corners round by at most ~1 px.
      for (int pass = 0; pass < 2; ++pass) {
        const auto source = smoothed;
        const auto count = source.size();
        for (int i = 0; i < count; ++i) {
          const auto& previous = source[(i + count - 1) % count];
          const auto& current = source[i];
          const auto& next = source[(i + 1) % count];
          smoothed[i] = QPointF(previous.x() + 2.0 * current.x() + next.x(),
                                previous.y() + 2.0 * current.y() + next.y()) /
                        4.0;
        }
      }
    }
    QRect mask_bounds;
    auto mask = lasso_selection_mask(smoothed, mask_bounds);
    combine_selection_from_mask(mask_bounds, std::move(mask));
  } else {
    auto region = QRegion(polygon, Qt::WindingFill);
    region = region.intersected(QRegion(QRect(0, 0, document_->width(), document_->height())));
    combine_selection_from_region(region);
  }
  record_selection_history(tr("Magnetic Lasso"), selection_snapshot_before_edit());
  selection_before_edit_ = QRegion();
  selection_display_region_before_edit_ = QRegion();
  selection_mask_before_edit_bounds_ = {};
  selection_mask_before_edit_alpha_ = QImage();
  update();
}

void CanvasWidget::cancel_magnetic_lasso() {
  if (!magnetic_lassoing_) {
    return;
  }
  magnetic_lassoing_ = false;
  magnetic_anchors_.clear();
  magnetic_anchor_path_index_.clear();
  magnetic_committed_path_.clear();
  magnetic_live_path_.clear();
  magnetic_engine_.set_image(nullptr, 0, 0, 0);
  magnetic_source_image_ = QImage();
  update();
}

void CanvasWidget::set_magnetic_lasso_width(int width) noexcept {
  magnetic_lasso_width_ = std::clamp(width, 1, 256);
  if (magnetic_lassoing_) {
    patchy::MagneticLassoParams params;
    params.width = magnetic_lasso_width_;
    params.edge_contrast = magnetic_lasso_edge_contrast_;
    magnetic_engine_.set_params(params);
  }
}

int CanvasWidget::magnetic_lasso_width() const noexcept {
  return magnetic_lasso_width_;
}

void CanvasWidget::set_magnetic_lasso_edge_contrast(int contrast) noexcept {
  magnetic_lasso_edge_contrast_ = std::clamp(contrast, 1, 100);
  if (magnetic_lassoing_) {
    patchy::MagneticLassoParams params;
    params.width = magnetic_lasso_width_;
    params.edge_contrast = magnetic_lasso_edge_contrast_;
    magnetic_engine_.set_params(params);
  }
}

int CanvasWidget::magnetic_lasso_edge_contrast() const noexcept {
  return magnetic_lasso_edge_contrast_;
}

void CanvasWidget::set_magnetic_lasso_frequency(int frequency) noexcept {
  magnetic_lasso_frequency_ = std::clamp(frequency, 0, 100);
}

int CanvasWidget::magnetic_lasso_frequency() const noexcept {
  return magnetic_lasso_frequency_;
}

bool CanvasWidget::magnetic_lasso_active() const noexcept {
  return magnetic_lassoing_;
}

int CanvasWidget::magnetic_lasso_anchor_count() const noexcept {
  return magnetic_anchors_.size();
}

}  // namespace patchy::ui
