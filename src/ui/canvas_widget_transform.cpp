// CanvasWidget's free-transform and warp implementation, split out of
// canvas_widget.cpp: the free-transform session (begin/cancel/finish/commit,
// preview caches, controls state), the transform controls drawing and hit
// testing, the entire warp feature, and the resample_transformed_rgba8 /
// resample_warped_rgba8 resamplers they share. Free transform and warp share
// the pending-warp session state, so both live in this one translation unit.
// Pure function moves from canvas_widget.cpp; behavior must stay identical.

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

constexpr double kMinimumTransformScalePercent = 0.01;

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

bool layer_needs_composited_transform_preview(const Layer& layer) {
  return std::abs(layer.opacity() - 1.0F) > 0.001F || std::abs(layer.fill_opacity() - 1.0F) > 0.001F ||
         layer.blend_mode() != BlendMode::Normal ||
         (layer.mask().has_value() && !layer.mask()->disabled) ||
         (layer.layer_style().effects_visible && !layer.layer_style().empty());
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

bool transform_delta_is_identity(const QTransform& transform) {
  return std::abs(transform.m11() - 1.0) < 1e-9 && std::abs(transform.m22() - 1.0) < 1e-9 &&
         std::abs(transform.m12()) < 1e-9 && std::abs(transform.m21()) < 1e-9 &&
         std::abs(transform.m31()) < 1e-9 && std::abs(transform.m32()) < 1e-9;
}

// Composes a document-space affine ONTO a content->document homography (apply the
// homography first, then the affine). The warp arrays are column-vector row-major
// (x' = m0 x + m1 y + m2, ...); QTransform is row-vector, so its elements
// transpose into that layout.
std::array<double, 9> compose_affine_over_homography(const QTransform& affine,
                                                     const std::array<double, 9>& homography) {
  const std::array<double, 9> a{affine.m11(), affine.m21(), affine.m31(),
                                affine.m12(), affine.m22(), affine.m32(),
                                affine.m13(), affine.m23(), affine.m33()};
  std::array<double, 9> out{};
  for (int row = 0; row < 3; ++row) {
    for (int column = 0; column < 3; ++column) {
      out[static_cast<std::size_t>(row * 3 + column)] =
          a[static_cast<std::size_t>(row * 3)] * homography[static_cast<std::size_t>(column)] +
          a[static_cast<std::size_t>(row * 3 + 1)] * homography[static_cast<std::size_t>(3 + column)] +
          a[static_cast<std::size_t>(row * 3 + 2)] * homography[static_cast<std::size_t>(6 + column)];
    }
  }
  return out;
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

}  // namespace

// Declared in canvas_widget.hpp: shared with the smart-object preview renderer, so it
// lives outside the anonymous namespace (the sampling helpers above stay file-local).
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

TransformedImage resample_warped_rgba8(const QImage& source, const WarpSurfaceGrid& grid,
                                       CanvasWidget::TransformInterpolation interpolation) {
  const auto converted = source.convertToFormat(QImage::Format_RGBA8888);
  const auto [min_x_it, max_x_it] = std::minmax_element(grid.doc_xs.begin(), grid.doc_xs.end());
  const auto [min_y_it, max_y_it] = std::minmax_element(grid.doc_ys.begin(), grid.doc_ys.end());
  if (min_x_it == grid.doc_xs.end() || min_y_it == grid.doc_ys.end()) {
    return TransformedImage{QImage(), Rect{}};
  }
  const auto left = static_cast<int>(std::floor(*min_x_it)) - 1;
  const auto top = static_cast<int>(std::floor(*min_y_it)) - 1;
  const auto right = static_cast<int>(std::ceil(*max_x_it)) + 1;
  const auto bottom = static_cast<int>(std::ceil(*max_y_it)) + 1;
  QImage transformed(std::max(1, right - left), std::max(1, bottom - top), QImage::Format_RGBA8888);
  transformed.fill(Qt::transparent);
  std::vector<std::uint8_t> covered(static_cast<std::size_t>(transformed.width()) * transformed.height(), 0);

  const auto sample_at = [&converted, interpolation](QPointF source_point) {
    switch (interpolation) {
      case CanvasWidget::TransformInterpolation::NearestNeighbor:
        return sample_nearest(converted, source_point);
      case CanvasWidget::TransformInterpolation::Bilinear:
        return sample_bilinear(converted, source_point);
      default:
        return sample_bicubic(converted, source_point);
    }
  };

  for (int cell_row = 0; cell_row + 1 < grid.rows; ++cell_row) {
    for (int cell_column = 0; cell_column + 1 < grid.columns; ++cell_column) {
      const auto i00 = static_cast<std::size_t>(cell_row * grid.columns + cell_column);
      const auto i10 = i00 + 1;
      const auto i01 = i00 + static_cast<std::size_t>(grid.columns);
      const auto i11 = i01 + 1;
      const double cell_min_x = std::min({grid.doc_xs[i00], grid.doc_xs[i10], grid.doc_xs[i11], grid.doc_xs[i01]});
      const double cell_max_x = std::max({grid.doc_xs[i00], grid.doc_xs[i10], grid.doc_xs[i11], grid.doc_xs[i01]});
      const double cell_min_y = std::min({grid.doc_ys[i00], grid.doc_ys[i10], grid.doc_ys[i11], grid.doc_ys[i01]});
      const double cell_max_y = std::max({grid.doc_ys[i00], grid.doc_ys[i10], grid.doc_ys[i11], grid.doc_ys[i01]});
      const int px_start = std::max(left, static_cast<int>(std::floor(cell_min_x)));
      const int px_end = std::min(right, static_cast<int>(std::ceil(cell_max_x)) + 1);
      const int py_start = std::max(top, static_cast<int>(std::floor(cell_min_y)));
      const int py_end = std::min(bottom, static_cast<int>(std::ceil(cell_max_y)) + 1);
      for (int py = py_start; py < py_end; ++py) {
        auto* row = transformed.scanLine(py - top);
        auto* coverage_row = covered.data() + static_cast<std::size_t>(py - top) * transformed.width();
        for (int px = px_start; px < px_end; ++px) {
          if (coverage_row[px - left] != 0) {
            continue;  // first writer wins on folds (row-major cell order)
          }
          const auto st = invert_bilinear_cell(px + 0.5, py + 0.5, grid.doc_xs[i00], grid.doc_ys[i00],
                                               grid.doc_xs[i10], grid.doc_ys[i10], grid.doc_xs[i11],
                                               grid.doc_ys[i11], grid.doc_xs[i01], grid.doc_ys[i01]);
          if (!st.has_value()) {
            continue;
          }
          const double s = (*st)[0];
          const double t = (*st)[1];
          const double source_x = (1.0 - t) * ((1.0 - s) * grid.source_xs[i00] + s * grid.source_xs[i10]) +
                                  t * ((1.0 - s) * grid.source_xs[i01] + s * grid.source_xs[i11]);
          const double source_y = (1.0 - t) * ((1.0 - s) * grid.source_ys[i00] + s * grid.source_ys[i10]) +
                                  t * ((1.0 - s) * grid.source_ys[i01] + s * grid.source_ys[i11]);
          const auto sample = sample_at(QPointF(source_x, source_y));
          auto* pixel = row + static_cast<std::ptrdiff_t>(px - left) * 4;
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
          coverage_row[px - left] = 1;
        }
      }
    }
  }
  const auto bounds = Rect{left, top, transformed.width(), transformed.height()};
  return TransformedImage{std::move(transformed), bounds};
}

bool CanvasWidget::begin_free_transform() {
  if (layer_edit_target_ == LayerEditTarget::SmartFilterMask) {
    if (status_callback_) {
      status_callback_(tr("This tool is unavailable while editing a Smart Filter mask"));
    }
    return false;
  }
  if (warping_layer_) {
    // Single session: switching modes keeps the pending warp (Photoshop behavior).
    return switch_warp_to_free_transform();
  }
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
  if (layer_is_smart_object(*layer) && !smart_object_lock_reason(*layer).empty()) {
    // Scaling or rotating a preview-locked smart object (warp / smart filters /
    // external) would desync the pixels from what Photoshop re-renders; integrity
    // first. Plain moves stay allowed through the move tool.
    if (status_callback_) {
      status_callback_(tr("This smart object is preview-only and can't be transformed. Rasterize the layer first."));
    }
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

void CanvasWidget::reset_free_transform_session_state() {
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
}

void CanvasWidget::clear_pending_warp() {
  transform_has_pending_warp_ = false;
  pending_warp_changed_ = false;
  pending_warp_smart_object_ = false;
  pending_warp_mesh_ = WarpMeshGrid{};
  pending_warp_original_mesh_ = WarpMeshGrid{};
  pending_warp_content_to_document_ = {1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
  pending_warp_content_width_ = 0.0;
  pending_warp_content_height_ = 0.0;
  pending_warp_style_ = QStringLiteral("warpCustom");
  pending_warp_style_value_ = 0.0;
  pending_warp_source_image_ = QImage();
}

void CanvasWidget::cancel_free_transform() {
  if (!transforming_layer_) {
    return;
  }
  reset_free_transform_session_state();
  clear_pending_warp();
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

  transform_source_image_ =
      qimage_from_pixel_buffer(std::as_const(*layer).pixels()).copy(transform_source_local_rect_);
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

void CanvasWidget::refresh_free_transform_preview_caches() {
  if (!transforming_layer_ || document_ == nullptr || !transform_layer_id_.has_value() ||
      transform_source_image_.isNull()) {
    return;
  }
  auto* layer = document_->find_layer(*transform_layer_id_);
  if (layer == nullptr) {
    return;
  }
  // The Layer Style dialog previews edits live while a transform can still be
  // active, so the snapshots baked at transform start (and whether the preview
  // needs compositing at all) must be rebuilt from the current document state.
  transform_requires_composited_preview_ = layer_needs_composited_transform_preview(*layer);
  if (!transform_requires_composited_preview_) {
    const auto was_visible = layer->visible();
    layer->set_visible(false);
    transform_base_cache_ = render_document_image();
    layer->set_visible(was_visible);
  }
  refresh_transform_composited_preview_cache();
  if (isVisible()) {
    update();
  }
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
  if (transform_has_pending_warp_) {
    // The affine stage rides a pending warp (the single-session toggle): compose
    // both into one bake instead of resampling the baked preview again.
    commit_free_transform_with_pending_warp();
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
  const bool transactional_smart_filter =
      changed && move_layer_requires_smart_filter_rerender(*layer);
  std::optional<Document> rollback_document;
  if (transactional_smart_filter) {
    rollback_document.emplace(*document_);
  } else if (changed && before_edit_callback_) {
    before_edit_callback_(tr("Free Transform"));
  }
  bool smart_filter_rerender_failed = false;
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
    } else if (layer_is_smart_object(*layer) && smart_object_lock_reason(*layer).empty()) {
      // The text-layer pattern for placed content: compose the delta into the
      // placement quad, then re-render crisply from the embedded source (the
      // resampled pixels committed above stay as the fallback).
      if (const auto placement = smart_object_placement_from_layer(*layer); placement.has_value()) {
        const auto delta = free_transform_delta(transform_original_rect_, transform_current_rect_, transform_angle_,
                                                transform_scale_x_sign_, transform_scale_y_sign_);
        auto updated = *placement;
        for (std::size_t i = 0; i < 8U; i += 2U) {
          const auto mapped = delta.map(QPointF(placement->transform[i], placement->transform[i + 1U]));
          updated.transform[i] = mapped.x();
          updated.transform[i + 1U] = mapped.y();
        }
        store_smart_object_placement(*layer, updated);
        mark_layer_smart_object_block_dirty(*layer);
        layer->metadata()[kLayerMetadataSmartObjectRasterStatus] = kSmartObjectRasterStatusPatchy;
        if (smart_object_transform_render_callback_ &&
            smart_object_transform_render_callback_(*transform_layer_id_)) {
          new_bounds = layer->bounds();
        } else if (transactional_smart_filter) {
          smart_filter_rerender_failed = true;
        }
      } else if (transactional_smart_filter) {
        smart_filter_rerender_failed = true;
      }
    }
  }

  if (smart_filter_rerender_failed && rollback_document.has_value()) {
    *document_ = std::move(*rollback_document);
  } else if (transactional_smart_filter && rollback_document.has_value()) {
    auto committed_document = *document_;
    *document_ = std::move(*rollback_document);
    if (before_edit_callback_) {
      before_edit_callback_(tr("Free Transform"));
    }
    *document_ = std::move(committed_document);
  }

  reset_free_transform_session_state();
  update_tool_cursor();
  document_changed(to_qrect(old_bounds).united(to_qrect(new_bounds)));
  if (status_callback_) {
    status_callback_(smart_filter_rerender_failed
                         ? tr("Could not rebuild the Smart Filter preview and cache")
                         : changed ? tr("Transformed layer")
                                   : tr("Free Transform cancelled"));
  }
  notify_transform_controls_changed();
}

void CanvasWidget::commit_free_transform_with_pending_warp() {
  auto* layer = document_ != nullptr && transform_layer_id_.has_value()
                    ? document_->find_layer(*transform_layer_id_)
                    : nullptr;
  if (layer == nullptr) {
    cancel_free_transform();
    return;
  }
  const auto layer_id = *transform_layer_id_;
  const auto delta = free_transform_delta(transform_original_rect_, transform_current_rect_, transform_angle_,
                                          transform_scale_x_sign_, transform_scale_y_sign_);
  const bool stage_changed = !transform_delta_is_identity(delta);
  const bool changed = pending_warp_changed_ || stage_changed;
  auto content_to_document = pending_warp_content_to_document_;
  if (stage_changed) {
    content_to_document = compose_affine_over_homography(delta, content_to_document);
  }
  const auto old_bounds = layer->bounds();
  auto new_bounds = old_bounds;
  const bool transactional_smart_filter =
      changed && move_layer_requires_smart_filter_rerender(*layer);
  std::optional<Document> rollback_document;
  if (transactional_smart_filter) {
    rollback_document.emplace(*document_);
  }
  bool smart_filter_rerender_failed = false;
  if (changed) {
    if (!transactional_smart_filter && before_edit_callback_) {
      before_edit_callback_(tr("Warp Transform"));
    }
    // ONE bake from the original content source through mesh + composed map: the
    // baked preview the affine stage displayed never resamples into the document.
    const auto baked = bake_warp_into_layer(
        *layer, pending_warp_mesh_, content_to_document,
        pending_warp_content_width_, pending_warp_content_height_,
        pending_warp_source_image_, pending_warp_smart_object_, layer_id,
        new_bounds);
    smart_filter_rerender_failed = transactional_smart_filter && !baked;
  }
  if (smart_filter_rerender_failed && rollback_document.has_value()) {
    *document_ = std::move(*rollback_document);
  } else if (transactional_smart_filter && rollback_document.has_value()) {
    auto committed_document = *document_;
    *document_ = std::move(*rollback_document);
    if (before_edit_callback_) {
      before_edit_callback_(tr("Warp Transform"));
    }
    *document_ = std::move(committed_document);
  }
  reset_free_transform_session_state();
  clear_pending_warp();
  update_tool_cursor();
  document_changed(to_qrect(old_bounds).united(to_qrect(new_bounds)));
  if (status_callback_) {
    status_callback_(smart_filter_rerender_failed
                         ? tr("Could not rebuild the Smart Filter preview and cache")
                         : changed ? tr("Warped layer")
                                   : tr("Warp Transform cancelled"));
  }
  notify_transform_controls_changed();
}

bool CanvasWidget::begin_warp_transform() {
  if (layer_edit_target_ == LayerEditTarget::SmartFilterMask) {
    if (status_callback_) {
      status_callback_(tr("This tool is unavailable while editing a Smart Filter mask"));
    }
    return false;
  }
  if (warping_layer_) {
    return true;
  }
  if (transforming_layer_ && transform_has_pending_warp_) {
    // Toggling back into the cage: resume the stashed warp session, composing
    // the affine stage's edits into its content->document map.
    return resume_pending_warp_session();
  }
  Layer* layer = nullptr;
  if (transforming_layer_ && transform_layer_id_.has_value() && document_ != nullptr) {
    // Mode switch from an active free transform: the session's layer stays the
    // target. All refusal guards below run BEFORE the free-transform session is
    // torn down, so a refused switch (text layer, locked smart object, decode
    // failure) keeps the pending transform alive instead of discarding it.
    layer = document_->find_layer(*transform_layer_id_);
  }
  if (layer == nullptr && document_ != nullptr && selected_layer_ids_.size() == 1U) {
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
      status_callback_(tr("Select an editable pixel layer to warp"));
    }
    return false;
  }
  if (layer_effectively_locks_position(*layer)) {
    show_layer_position_locked_message();
    return false;
  }
  if (layer_is_text(*layer)) {
    if (status_callback_) {
      status_callback_(tr("Text layers use Warp Text (the Type tool's Warp... button). For a custom mesh, "
                          "convert to a smart object or rasterize first."));
    }
    return false;
  }
  if (layer_is_smart_object(*layer) && !smart_object_lock_reason(*layer).empty()) {
    if (status_callback_) {
      status_callback_(tr("This smart object is preview-only and can't be warped. Rasterize the layer first."));
    }
    return false;
  }

  WarpMeshGrid start_mesh;
  std::array<double, 9> content_to_document{1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
  const bool smart_object = layer_is_smart_object(*layer);
  if (smart_object) {
    const auto placement = smart_object_placement_from_layer(*layer);
    const auto uuid = smart_object_source_uuid(*layer);
    const auto* source =
        placement.has_value() ? document_->metadata().smart_objects.find(uuid) : nullptr;
    auto decoded = source != nullptr ? decode_smart_object_source_image(*source) : std::nullopt;
    if (!placement.has_value() || !decoded.has_value() || decoded->isNull()) {
      if (status_callback_) {
        status_callback_(tr("This smart object's contents can't be decoded for warping"));
      }
      return false;
    }
    warp_content_width_ = placement->width > 0.0 ? placement->width : decoded->width();
    warp_content_height_ = placement->height > 0.0 ? placement->height : decoded->height();
    double hull_left = 0.0;
    double hull_top = 0.0;
    double hull_right = warp_content_width_;
    double hull_bottom = warp_content_height_;
    const auto existing = smart_object_warp_from_layer(*layer);
    if (existing.has_value() && !existing->mesh_xs.empty()) {
      WarpMeshGrid stored;
      stored.u_order = existing->u_order;
      stored.v_order = existing->v_order;
      stored.xs = existing->mesh_xs;
      stored.ys = existing->mesh_ys;
      // The renderer maps the STORED mesh's hull onto the quad, so the editing map
      // derives from that hull; elevation to the 4x4 working cage preserves the
      // surface but not necessarily the hull.
      const auto [min_x, max_x] = std::minmax_element(stored.xs.begin(), stored.xs.end());
      const auto [min_y, max_y] = std::minmax_element(stored.ys.begin(), stored.ys.end());
      hull_left = *min_x;
      hull_top = *min_y;
      hull_right = *max_x;
      hull_bottom = *max_y;
      if (existing->bounds_right - existing->bounds_left > 0.0) {
        warp_content_width_ = existing->bounds_right - existing->bounds_left;
      }
      if (existing->bounds_bottom - existing->bounds_top > 0.0) {
        warp_content_height_ = existing->bounds_bottom - existing->bounds_top;
      }
      start_mesh = elevate_warp_mesh_to_cubic(stored);
    } else {
      start_mesh = identity_warp_mesh(0.0, 0.0, warp_content_width_, warp_content_height_, 4, 4);
    }
    const auto mapping =
        homography_from_rect_to_quad(hull_left, hull_top, hull_right, hull_bottom, placement->transform);
    if (!mapping.has_value()) {
      return false;
    }
    content_to_document = *mapping;
    warp_source_image_ = *decoded;
  } else {
    warp_source_image_ = qimage_from_pixel_buffer(std::as_const(*layer).pixels());
    if (warp_source_image_.isNull() || warp_source_image_.width() <= 0 || warp_source_image_.height() <= 0) {
      if (status_callback_) {
        status_callback_(tr("Layer has no pixels to warp"));
      }
      return false;
    }
    warp_content_width_ = warp_source_image_.width();
    warp_content_height_ = warp_source_image_.height();
    start_mesh = identity_warp_mesh(0.0, 0.0, warp_content_width_, warp_content_height_, 4, 4);
    const auto bounds = layer->bounds();
    content_to_document = {1.0, 0.0, static_cast<double>(bounds.x),
                           0.0, 1.0, static_cast<double>(bounds.y),
                           0.0, 0.0, 1.0};
  }

  // Single session: a pending free-transform stage composes into the cage's map
  // instead of being discarded, so Ctrl+T scale/rotate + warp commit as one bake.
  bool entry_changed = false;
  if (transforming_layer_) {
    const auto delta = free_transform_delta(transform_original_rect_, transform_current_rect_, transform_angle_,
                                            transform_scale_x_sign_, transform_scale_y_sign_);
    if (!transform_delta_is_identity(delta)) {
      content_to_document = compose_affine_over_homography(delta, content_to_document);
      entry_changed = true;
    }
  }
  const auto document_to_content = invert_homography(content_to_document);
  if (!document_to_content.has_value()) {
    return false;  // keeps any pending free-transform session alive
  }
  if (transforming_layer_) {
    reset_free_transform_session_state();
  }

  warping_layer_ = true;
  dragging_warp_handle_ = false;
  warp_drag_index_ = -1;
  warp_layer_id_ = layer->id();
  warp_target_smart_object_ = smart_object;
  warp_mesh_ = start_mesh;
  warp_original_mesh_ = start_mesh;
  warp_content_to_document_ = content_to_document;
  warp_document_to_content_ = *document_to_content;
  warp_entry_changed_ = entry_changed;
  warp_style_ = QStringLiteral("warpCustom");
  warp_style_value_ = 0.0;
  warp_base_cache_ = QImage();
  warp_preview_cache_ = QImage();
  set_move_transform_controls_layer(std::nullopt);
  prepare_warp_source();
  setCursor(Qt::ArrowCursor);
  update();
  notify_transform_controls_changed();
  if (status_callback_) {
    status_callback_(tr("Drag the warp grid handles. Enter applies, Esc cancels."));
  }
  return true;
}

void CanvasWidget::cancel_warp_transform() {
  if (!warping_layer_) {
    return;
  }
  reset_warp_state();
  update_tool_cursor();
  update();
  if (status_callback_) {
    status_callback_(tr("Warp Transform cancelled"));
  }
  notify_transform_controls_changed();
}

void CanvasWidget::finish_warp_transform() {
  if (!warping_layer_) {
    return;
  }
  commit_warp_transform();
}

bool CanvasWidget::warp_transform_active() const noexcept {
  return warping_layer_;
}

void CanvasWidget::apply_warp_style_preset(const QString& style, double value) {
  if (!warping_layer_) {
    return;
  }
  if (style == QStringLiteral("warpCustom")) {
    warp_style_ = style;
    warp_style_value_ = value;
    notify_transform_controls_changed();
    return;
  }
  const auto generated = generate_style_warp_mesh(style.toStdString(), value, false, warp_content_width_,
                                                  warp_content_height_);
  if (!generated.has_value()) {
    return;
  }
  warp_mesh_ = elevate_warp_mesh_to_cubic(*generated);
  warp_style_ = style;
  warp_style_value_ = value;
  refresh_warp_preview_cache();
  update();
  notify_transform_controls_changed();
}

QString CanvasWidget::warp_style_preset() const {
  return warp_style_;
}

double CanvasWidget::warp_style_preset_value() const noexcept {
  return warp_style_value_;
}

int CanvasWidget::warp_handle_count() const noexcept {
  return warping_layer_ ? static_cast<int>(warp_mesh_.xs.size()) : 0;
}

QPointF CanvasWidget::warp_handle_document_position(int index) const {
  if (!warping_layer_ || index < 0 || index >= static_cast<int>(warp_mesh_.xs.size())) {
    return {};
  }
  const auto mapped = apply_homography(warp_content_to_document_, warp_mesh_.xs[static_cast<std::size_t>(index)],
                                       warp_mesh_.ys[static_cast<std::size_t>(index)]);
  return QPointF(mapped[0], mapped[1]);
}

void CanvasWidget::set_warp_handle_document_position(int index, QPointF document_point) {
  if (!warping_layer_ || index < 0 || index >= static_cast<int>(warp_mesh_.xs.size())) {
    return;
  }
  const auto content =
      apply_homography(warp_document_to_content_, document_point.x(), document_point.y());
  warp_mesh_.xs[static_cast<std::size_t>(index)] = content[0];
  warp_mesh_.ys[static_cast<std::size_t>(index)] = content[1];
  warp_style_ = QStringLiteral("warpCustom");
  warp_style_value_ = 0.0;
  refresh_warp_preview_cache();
  update();
  notify_transform_controls_changed();
}

bool CanvasWidget::prepare_warp_source() {
  if (!warping_layer_ || document_ == nullptr || !warp_layer_id_.has_value()) {
    return false;
  }
  auto* layer = document_->find_layer(*warp_layer_id_);
  if (layer == nullptr || warp_source_image_.isNull()) {
    return false;
  }
  if (warp_base_cache_.isNull()) {
    const auto was_visible = layer->visible();
    layer->set_visible(false);
    warp_base_cache_ = render_document_image();
    layer->set_visible(was_visible);
  }
  refresh_warp_preview_cache();
  return true;
}

std::array<double, 8> CanvasWidget::warp_document_quad() const {
  const auto [min_x, max_x] = std::minmax_element(warp_mesh_.xs.begin(), warp_mesh_.xs.end());
  const auto [min_y, max_y] = std::minmax_element(warp_mesh_.ys.begin(), warp_mesh_.ys.end());
  const auto top_left = apply_homography(warp_content_to_document_, *min_x, *min_y);
  const auto top_right = apply_homography(warp_content_to_document_, *max_x, *min_y);
  const auto bottom_right = apply_homography(warp_content_to_document_, *max_x, *max_y);
  const auto bottom_left = apply_homography(warp_content_to_document_, *min_x, *max_y);
  return {top_left[0],     top_left[1],     top_right[0],   top_right[1],
          bottom_right[0], bottom_right[1], bottom_left[0], bottom_left[1]};
}

void CanvasWidget::refresh_warp_preview_cache() {
  warp_preview_cache_ = QImage();
  if (!warping_layer_ || document_ == nullptr || !warp_layer_id_.has_value() || warp_source_image_.isNull()) {
    return;
  }
  // Interactive quality: 16 px cells (commit re-renders at 4 px).
  const auto grid = build_warp_surface_grid(warp_mesh_, warp_document_quad(), warp_source_image_.width(),
                                            warp_source_image_.height(), 16.0, 64);
  if (!grid.has_value()) {
    return;
  }
  const auto warped = resample_warped_rgba8(warp_source_image_, *grid, transform_interpolation_);
  if (warped.image.isNull()) {
    return;
  }
  const auto warped_pixels = pixels_from_image_rgba(warped.image);
  warp_preview_cache_ =
      qimage_from_document_rect_with_layer_pixels(*document_, QRect(0, 0, document_->width(), document_->height()),
                                                  true, *warp_layer_id_, warped_pixels, warped.bounds)
          .convertToFormat(QImage::Format_RGBA8888);
}

int CanvasWidget::warp_handle_at(QPoint widget_point) const {
  if (!warping_layer_) {
    return -1;
  }
  constexpr double kHandleHit = 14.0;
  for (int index = 0; index < static_cast<int>(warp_mesh_.xs.size()); ++index) {
    const auto point = widget_position_f(warp_handle_document_position(index));
    const QRectF hit_rect(point.x() - kHandleHit / 2.0, point.y() - kHandleHit / 2.0, kHandleHit, kHandleHit);
    if (hit_rect.contains(widget_point)) {
      return index;
    }
  }
  return -1;
}

void CanvasWidget::draw_warp_transform(QPainter& painter) const {
  if (!warping_layer_) {
    return;
  }
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);
  const auto widget_point_for_content = [this](double x, double y) {
    const auto document_point = apply_homography(warp_content_to_document_, x, y);
    return widget_position_f(QPointF(document_point[0], document_point[1]));
  };
  // The control cage: u- and v-direction cubics through the control points (the
  // classic warp grid), drawn in widget space.
  painter.setBrush(Qt::NoBrush);
  painter.setPen(QPen(QColor(95, 170, 255), 1.0));
  const int u_order = warp_mesh_.u_order;
  const int v_order = warp_mesh_.v_order;
  const auto control = [this](int row, int column) {
    const auto index = static_cast<std::size_t>(row * warp_mesh_.u_order + column);
    return QPointF(warp_mesh_.xs[index], warp_mesh_.ys[index]);
  };
  for (int row = 0; row < v_order; ++row) {
    QPainterPath path;
    const auto p0 = control(row, 0);
    path.moveTo(widget_point_for_content(p0.x(), p0.y()));
    const auto p1 = control(row, std::min(1, u_order - 1));
    const auto p2 = control(row, std::min(2, u_order - 1));
    const auto p3 = control(row, u_order - 1);
    path.cubicTo(widget_point_for_content(p1.x(), p1.y()), widget_point_for_content(p2.x(), p2.y()),
                 widget_point_for_content(p3.x(), p3.y()));
    painter.drawPath(path);
  }
  for (int column = 0; column < u_order; ++column) {
    QPainterPath path;
    const auto p0 = control(0, column);
    path.moveTo(widget_point_for_content(p0.x(), p0.y()));
    const auto p1 = control(std::min(1, v_order - 1), column);
    const auto p2 = control(std::min(2, v_order - 1), column);
    const auto p3 = control(v_order - 1, column);
    path.cubicTo(widget_point_for_content(p1.x(), p1.y()), widget_point_for_content(p2.x(), p2.y()),
                 widget_point_for_content(p3.x(), p3.y()));
    painter.drawPath(path);
  }
  // Handles: corners largest, edge handles medium, interior smallest.
  painter.setPen(QPen(QColor(10, 14, 20), 1.0));
  for (int row = 0; row < v_order; ++row) {
    for (int column = 0; column < u_order; ++column) {
      const bool corner = (row == 0 || row == v_order - 1) && (column == 0 || column == u_order - 1);
      const bool edge = row == 0 || row == v_order - 1 || column == 0 || column == u_order - 1;
      const double size = corner ? 8.0 : (edge ? 7.0 : 6.0);
      const auto index = row * u_order + column;
      const auto point = widget_position_f(warp_handle_document_position(index));
      const QRectF handle_rect(point.x() - size / 2.0, point.y() - size / 2.0, size, size);
      painter.setBrush(index == warp_drag_index_ && dragging_warp_handle_ ? QColor(95, 170, 255)
                                                                          : QColor(245, 248, 252));
      if (corner) {
        painter.drawRect(handle_rect);
      } else {
        painter.drawEllipse(handle_rect);
      }
    }
  }
  painter.restore();
}

void CanvasWidget::commit_warp_transform() {
  if (!warping_layer_ || document_ == nullptr || !warp_layer_id_.has_value() || warp_source_image_.isNull()) {
    cancel_warp_transform();
    return;
  }
  auto* layer = document_->find_layer(*warp_layer_id_);
  if (layer == nullptr) {
    cancel_warp_transform();
    return;
  }
  const bool changed = warp_entry_changed_ || warp_mesh_.xs != warp_original_mesh_.xs ||
                       warp_mesh_.ys != warp_original_mesh_.ys;
  const auto old_bounds = layer->bounds();
  auto new_bounds = old_bounds;
  const bool transactional_smart_filter =
      changed && move_layer_requires_smart_filter_rerender(*layer);
  std::optional<Document> rollback_document;
  if (transactional_smart_filter) {
    rollback_document.emplace(*document_);
  }
  bool smart_filter_rerender_failed = false;
  if (changed) {
    if (!transactional_smart_filter && before_edit_callback_) {
      before_edit_callback_(tr("Warp Transform"));
    }
    const auto baked = bake_warp_into_layer(
        *layer, warp_mesh_, warp_content_to_document_, warp_content_width_,
        warp_content_height_, warp_source_image_, warp_target_smart_object_,
        *warp_layer_id_, new_bounds);
    smart_filter_rerender_failed = transactional_smart_filter && !baked;
  }
  if (smart_filter_rerender_failed && rollback_document.has_value()) {
    *document_ = std::move(*rollback_document);
  } else if (transactional_smart_filter && rollback_document.has_value()) {
    auto committed_document = *document_;
    *document_ = std::move(*rollback_document);
    if (before_edit_callback_) {
      before_edit_callback_(tr("Warp Transform"));
    }
    *document_ = std::move(committed_document);
  }
  reset_warp_state();
  update_tool_cursor();
  document_changed(to_qrect(old_bounds).united(to_qrect(new_bounds)));
  if (status_callback_) {
    status_callback_(smart_filter_rerender_failed
                         ? tr("Could not rebuild the Smart Filter preview and cache")
                         : changed ? tr("Warped layer")
                                   : tr("Warp Transform cancelled"));
  }
  notify_transform_controls_changed();
}

void CanvasWidget::reset_warp_state() {
  warping_layer_ = false;
  dragging_warp_handle_ = false;
  warp_drag_index_ = -1;
  warp_layer_id_.reset();
  warp_target_smart_object_ = false;
  warp_mesh_ = WarpMeshGrid{};
  warp_original_mesh_ = WarpMeshGrid{};
  warp_style_ = QStringLiteral("warpCustom");
  warp_style_value_ = 0.0;
  warp_source_image_ = QImage();
  warp_base_cache_ = QImage();
  warp_preview_cache_ = QImage();
  warp_entry_changed_ = false;
}

bool CanvasWidget::bake_warp_into_layer(Layer& layer, const WarpMeshGrid& mesh,
                                        const std::array<double, 9>& content_to_document, double content_width,
                                        double content_height, const QImage& source_image, bool smart_object,
                                        LayerId layer_id, Rect& new_bounds) {
  const auto [min_x, max_x] = std::minmax_element(mesh.xs.begin(), mesh.xs.end());
  const auto [min_y, max_y] = std::minmax_element(mesh.ys.begin(), mesh.ys.end());
  const auto top_left = apply_homography(content_to_document, *min_x, *min_y);
  const auto top_right = apply_homography(content_to_document, *max_x, *min_y);
  const auto bottom_right = apply_homography(content_to_document, *max_x, *max_y);
  const auto bottom_left = apply_homography(content_to_document, *min_x, *max_y);
  const std::array<double, 8> quad{top_left[0],     top_left[1],     top_right[0],   top_right[1],
                                   bottom_right[0], bottom_right[1], bottom_left[0], bottom_left[1]};
  const auto grid = build_warp_surface_grid(mesh, quad, source_image.width(), source_image.height(), 4.0, 128);
  bool baked_pixels = false;
  if (grid.has_value()) {
    const auto baked = resample_warped_rgba8(source_image, *grid, transform_interpolation_);
    if (!baked.image.isNull()) {
      layer.set_pixels(pixels_from_image_rgba(baked.image));
      layer.set_bounds(baked.bounds);
      new_bounds = baked.bounds;
      baked_pixels = true;
    }
  }
  if (smart_object) {
    const bool requires_smart_filter_rerender =
        move_layer_requires_smart_filter_rerender(layer);
    // Non-destructive: the mesh + hull quad go into the placement metadata, the
    // SoLd regenerates on save, and the callback re-renders crisply from source
    // (the 4 px bake above stays as the fallback).
    SmartObjectWarp warp;
    warp.style = "warpCustom";
    warp.value = 0.0;
    warp.rotate = "Hrzn";
    warp.bounds_top = 0.0;
    warp.bounds_left = 0.0;
    warp.bounds_bottom = content_height;
    warp.bounds_right = content_width;
    warp.u_order = mesh.u_order;
    warp.v_order = mesh.v_order;
    warp.mesh_xs = mesh.xs;
    warp.mesh_ys = mesh.ys;
    layer.metadata()[kLayerMetadataSmartObjectWarp] = serialize_smart_object_warp(warp);
    if (const auto placement = smart_object_placement_from_layer(layer); placement.has_value()) {
      auto updated = *placement;
      updated.transform = quad;
      store_smart_object_placement(layer, updated);
    }
    mark_layer_smart_object_block_dirty(layer);
    layer.metadata()[kLayerMetadataSmartObjectRasterStatus] = kSmartObjectRasterStatusPatchy;
    if (smart_object_transform_render_callback_ &&
        smart_object_transform_render_callback_(layer_id)) {
      new_bounds = layer.bounds();
    } else if (requires_smart_filter_rerender) {
      return false;
    }
  }
  return baked_pixels || smart_object;
}

bool CanvasWidget::switch_warp_to_free_transform() {
  if (!warping_layer_) {
    return false;
  }
  if (document_ == nullptr || !warp_layer_id_.has_value()) {
    cancel_warp_transform();
    return false;
  }
  auto* layer = document_->find_layer(*warp_layer_id_);
  if (layer == nullptr) {
    cancel_warp_transform();
    return false;
  }
  const bool mesh_changed = warp_mesh_.xs != warp_original_mesh_.xs || warp_mesh_.ys != warp_original_mesh_.ys;
  if (!mesh_changed && !warp_entry_changed_) {
    // Nothing pending: a plain free transform from the layer's stored state keeps
    // the historical (byte-exact) affine path.
    reset_warp_state();
    const bool started = begin_free_transform();
    if (!started) {
      update_tool_cursor();
      update();
      notify_transform_controls_changed();
    }
    return started;
  }

  // Bake the pending warp once at commit quality: it becomes the affine stage's
  // preview source. The eventual commit re-bakes from the ORIGINAL source through
  // the composed map, so this image never double-resamples into the document.
  const auto quad = warp_document_quad();
  const auto grid = build_warp_surface_grid(warp_mesh_, quad, warp_source_image_.width(),
                                            warp_source_image_.height(), 4.0, 128);
  if (!grid.has_value()) {
    return false;  // stay in the warp session
  }
  const auto baked = resample_warped_rgba8(warp_source_image_, *grid, transform_interpolation_);
  if (baked.image.isNull() || baked.bounds.width <= 0 || baked.bounds.height <= 0) {
    return false;
  }

  transform_has_pending_warp_ = true;
  pending_warp_changed_ = true;
  pending_warp_smart_object_ = warp_target_smart_object_;
  pending_warp_mesh_ = warp_mesh_;
  pending_warp_original_mesh_ = warp_original_mesh_;
  pending_warp_content_to_document_ = warp_content_to_document_;
  pending_warp_content_width_ = warp_content_width_;
  pending_warp_content_height_ = warp_content_height_;
  pending_warp_style_ = warp_style_;
  pending_warp_style_value_ = warp_style_value_;
  pending_warp_source_image_ = warp_source_image_;
  const auto layer_id = *warp_layer_id_;
  reset_warp_state();

  transforming_layer_ = true;
  dragging_transform_ = false;
  transform_layer_id_ = layer_id;
  set_move_transform_controls_layer(std::nullopt);
  transform_original_rect_ =
      QRectF(baked.bounds.x, baked.bounds.y, baked.bounds.width, baked.bounds.height);
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
  transform_source_image_ = baked.image;
  transform_source_local_rect_ = QRect(0, 0, baked.image.width(), baked.image.height());
  transform_composited_preview_cache_ = QImage();
  transform_requires_composited_preview_ = layer_needs_composited_transform_preview(*layer);
  {
    const auto was_visible = layer->visible();
    layer->set_visible(false);
    transform_base_cache_ = render_document_image();
    layer->set_visible(was_visible);
  }
  if (transform_requires_composited_preview_) {
    refresh_transform_composited_preview_cache();
  }
  setCursor(Qt::ArrowCursor);
  update();
  notify_transform_controls_changed();
  if (status_callback_) {
    status_callback_(tr("Drag handles to transform. Shift keeps aspect ratio."));
  }
  return true;
}

bool CanvasWidget::resume_pending_warp_session() {
  if (!transforming_layer_ || !transform_has_pending_warp_ || document_ == nullptr ||
      !transform_layer_id_.has_value()) {
    return false;
  }
  auto* layer = document_->find_layer(*transform_layer_id_);
  if (layer == nullptr) {
    cancel_free_transform();
    return false;
  }
  const auto delta = free_transform_delta(transform_original_rect_, transform_current_rect_, transform_angle_,
                                          transform_scale_x_sign_, transform_scale_y_sign_);
  const bool stage_changed = !transform_delta_is_identity(delta);
  auto content_to_document = pending_warp_content_to_document_;
  if (stage_changed) {
    content_to_document = compose_affine_over_homography(delta, content_to_document);
  }
  const auto document_to_content = invert_homography(content_to_document);
  if (!document_to_content.has_value()) {
    return false;  // keep the affine stage alive
  }
  const auto layer_id = *transform_layer_id_;
  const bool entry_changed = pending_warp_changed_ || stage_changed;

  warping_layer_ = true;
  dragging_warp_handle_ = false;
  warp_drag_index_ = -1;
  warp_layer_id_ = layer_id;
  warp_target_smart_object_ = pending_warp_smart_object_;
  warp_mesh_ = pending_warp_mesh_;
  warp_original_mesh_ = pending_warp_original_mesh_;
  warp_content_to_document_ = content_to_document;
  warp_document_to_content_ = *document_to_content;
  warp_entry_changed_ = entry_changed;
  warp_style_ = pending_warp_style_;
  warp_style_value_ = pending_warp_style_value_;
  warp_source_image_ = pending_warp_source_image_;
  warp_base_cache_ = QImage();
  warp_preview_cache_ = QImage();
  reset_free_transform_session_state();
  clear_pending_warp();
  set_move_transform_controls_layer(std::nullopt);
  prepare_warp_source();
  setCursor(Qt::ArrowCursor);
  update();
  notify_transform_controls_changed();
  if (status_callback_) {
    status_callback_(tr("Drag the warp grid handles. Enter applies, Esc cancels."));
  }
  return true;
}

}  // namespace patchy::ui
