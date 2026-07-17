// CanvasWidget's pixel drawing tools, split out of canvas_widget.cpp: the
// Line/Gradient/Rectangle/Ellipse pixel commit paths and flood fill, their
// grayscale-target twins with the shared mask-shape renderer, the shape
// options and shape_drag_rect constraint math with the in-flight shape /
// text-rect previews and the drag size readout, and the eyedropper color
// picking. Pure function moves from canvas_widget.cpp; behavior must stay
// identical.

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
#include "core/vector_shape.hpp"
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

std::uint8_t channel_from_color(QColor color, int channel) {
  switch (channel) {
    case 0:
      return static_cast<std::uint8_t>(color.red());
    case 1:
      return static_cast<std::uint8_t>(color.green());
    case 2:
      return static_cast<std::uint8_t>(color.blue());
    default:
      return static_cast<std::uint8_t>(color.alpha());
  }
}

std::optional<QColor> screen_color_at_global_position(QPoint global_position) {
  QScreen* screen = QGuiApplication::screenAt(global_position);
  if (screen == nullptr) {
    screen = QGuiApplication::primaryScreen();
  }
  if (screen == nullptr) {
    return std::nullopt;
  }

  const QPoint screen_position = global_position - screen->geometry().topLeft();
  const QPixmap sample = screen->grabWindow(0, screen_position.x(), screen_position.y(), 1, 1);
  if (sample.isNull()) {
    return std::nullopt;
  }

  const auto image = sample.toImage();
  if (!image.rect().contains(0, 0)) {
    return std::nullopt;
  }
  return image.pixelColor(0, 0);
}

}  // namespace

void CanvasWidget::set_fill_shapes(bool fill_shapes) noexcept {
  fill_shapes_ = fill_shapes;
}

bool CanvasWidget::fill_shapes() const noexcept {
  return fill_shapes_;
}

void CanvasWidget::set_shape_corner_radius(int radius) noexcept {
  shape_corner_radius_ = std::max(0, radius);
  if (drawing_shape_ && tool_ == CanvasTool::Rectangle) {
    update();
  }
}

int CanvasWidget::shape_corner_radius() const noexcept {
  return shape_corner_radius_;
}

void CanvasWidget::set_vector_tool_mode(VectorToolMode mode) noexcept {
  vector_tool_mode_ = mode;
}

VectorToolMode CanvasWidget::vector_tool_mode() const noexcept {
  return vector_tool_mode_;
}

void CanvasWidget::set_vector_shape_drawn_callback(
    std::function<void(patchy::LiveShapeKind, QRectF, QPointF, QPointF)> callback) {
  vector_shape_drawn_callback_ = std::move(callback);
}

void CanvasWidget::set_fill_opacity(int opacity) noexcept {
  fill_opacity_ = std::clamp(opacity, 1, 100);
}

int CanvasWidget::fill_opacity() const noexcept {
  return fill_opacity_;
}

void CanvasWidget::set_fill_softness(int softness) noexcept {
  fill_softness_ = std::clamp(softness, 0, 100);
}

int CanvasWidget::fill_softness() const noexcept {
  return fill_softness_;
}

void CanvasWidget::set_shape_style(MarqueeStyle style) noexcept {
  shape_style_ = style;
}

CanvasWidget::MarqueeStyle CanvasWidget::shape_style() const noexcept {
  return shape_style_;
}

void CanvasWidget::set_shape_fixed_size(int width, int height) noexcept {
  shape_fixed_size_ = QSize(std::clamp(width, 1, 30000), std::clamp(height, 1, 30000));
}

QSize CanvasWidget::shape_fixed_size() const noexcept {
  return shape_fixed_size_;
}

QRect CanvasWidget::shape_drag_rect(QPoint anchor, QPoint current) const {
  // Geometry rect for an in-flight Rectangle/Ellipse drag, folding in the options-bar
  // Style (Normal / Fixed Ratio / Fixed Size), the Shift square constraint, and Alt
  // draw-from-center. The rect is an inclusive pixel rect: commit it via
  // (topLeft(), bottomRight()) so draw_rectangle/draw_ellipse re-derive exactly this
  // rect through normalized_rect(). Mirrors marquee_selection_rect(), kept separate on
  // purpose: the marquee variant embeds selection-only concerns (edge-clamped square
  // anchoring, Alt-at-press from-center gating) and is pinned by its own tests.
  if (tool_ != CanvasTool::Rectangle && tool_ != CanvasTool::Ellipse) {
    return normalized_rect(anchor, current);
  }
  QRect rect;
  if (shape_from_center_) {
    // Alt held mid-drag: the press point is the center, so the shape grows
    // symmetrically and is twice the drag extent.
    if (shape_style_ == MarqueeStyle::FixedSize) {
      rect = QRect(anchor - QPoint(shape_fixed_size_.width() / 2, shape_fixed_size_.height() / 2),
                   shape_fixed_size_);
    } else {
      const auto delta = current - anchor;
      auto half_w = std::abs(delta.x());
      auto half_h = std::abs(delta.y());
      if (shape_style_ == MarqueeStyle::FixedRatio) {
        const auto ratio = std::max(1.0, static_cast<double>(shape_fixed_size_.width())) /
                           std::max(1.0, static_cast<double>(shape_fixed_size_.height()));
        if (static_cast<double>(half_w) / std::max(1, half_h) > ratio) {
          half_h = std::max(0, static_cast<int>(std::round(half_w / ratio)));
        } else {
          half_w = std::max(0, static_cast<int>(std::round(half_h * ratio)));
        }
      } else if (shape_square_constrained_) {
        half_w = half_h = std::min(half_w, half_h);
      }
      rect = QRect(anchor.x() - half_w, anchor.y() - half_h, half_w * 2, half_h * 2);
    }
  } else if (shape_style_ == MarqueeStyle::FixedSize) {
    // A click places the exact W x H shape extending down-right; the drag is ignored.
    rect = QRect(anchor, shape_fixed_size_);
  } else if (shape_style_ == MarqueeStyle::FixedRatio) {
    const auto ratio = std::max(1.0, static_cast<double>(shape_fixed_size_.width())) /
                       std::max(1.0, static_cast<double>(shape_fixed_size_.height()));
    const auto delta = current - anchor;
    auto width = std::max(1, std::abs(delta.x()));
    auto height = std::max(1, std::abs(delta.y()));
    if (static_cast<double>(width) / static_cast<double>(height) > ratio) {
      width = std::max(1, static_cast<int>(std::round(height * ratio)));
    } else {
      height = std::max(1, static_cast<int>(std::round(width / ratio)));
    }
    rect = QRect(anchor, anchor + QPoint(delta.x() < 0 ? -width : width,
                                         delta.y() < 0 ? -height : height))
               .normalized();
  } else if (shape_square_constrained_) {
    // Shift forces a 1:1 square (circle for the ellipse), sized to the smaller drag axis.
    const auto dx = current.x() - anchor.x();
    const auto dy = current.y() - anchor.y();
    const auto side = std::max(1, std::min(std::abs(dx), std::abs(dy)));
    rect = normalized_rect(anchor, anchor + QPoint(dx < 0 ? -side : side, dy < 0 ? -side : side));
  } else {
    rect = normalized_rect(anchor, current);
  }
  if (rect.width() < 1) {
    rect.setWidth(1);
  }
  if (rect.height() < 1) {
    rect.setHeight(1);
  }
  return rect;
}

void CanvasWidget::draw_shape_preview(QPainter& painter, QRect exposed_rect) {
  if (!drawing_shape_) {
    return;
  }

  auto doc_a = shape_start_;
  auto doc_b = shape_current_;
  if (tool_ == CanvasTool::Rectangle || tool_ == CanvasTool::Ellipse ||
      tool_ == CanvasTool::CustomShape) {
    const auto rect = shape_drag_rect(shape_start_, shape_current_);
    doc_a = rect.topLeft();
    doc_b = rect.bottomRight();
  }
  const auto a = widget_position(doc_a);
  const auto b = widget_position(doc_b);

  // Shape mode previews with the ACTUAL appearance (options-bar fill and
  // stroke), like Photoshop; Path and Pixels modes keep their previews, and
  // so do the raster-committing edit targets (masks, channels, quick mask)
  // and the vector-mask target (its drags append mask subpaths, not a fill).
  const bool vector_shape_tool = tool_ == CanvasTool::Line || tool_ == CanvasTool::Rectangle ||
                                 tool_ == CanvasTool::Ellipse || tool_ == CanvasTool::Polygon ||
                                 tool_ == CanvasTool::CustomShape;
  if (vector_shape_tool && vector_tool_mode_ == VectorToolMode::Shape &&
      layer_edit_target_ == LayerEditTarget::Content && !quick_mask_active_ &&
      !editing_smart_filter_mask() && shape_preview_appearance_callback_) {
    if (const auto appearance = shape_preview_appearance_callback_();
        appearance.has_value() && draw_shape_appearance_preview(painter, *appearance)) {
      return;
    }
  }

  // The vector-only tools preview as an accent outline (the committed result
  // is a shape layer or path, never raster paint).
  if (tool_ == CanvasTool::Polygon || tool_ == CanvasTool::CustomShape) {
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(116, 192, 255), 1.2));
    painter.setBrush(Qt::NoBrush);
    if (tool_ == CanvasTool::Polygon) {
      const auto subpath = polygon_drag_subpath(QPointF(shape_start_), QPointF(shape_current_));
      QPolygonF outline;
      for (const auto& anchor : subpath.anchors) {
        outline << widget_position_f(QPointF(anchor.anchor_x, anchor.anchor_y));
      }
      if (!outline.isEmpty()) {
        painter.drawPolygon(outline);
      }
    } else {
      painter.drawRect(QRectF(QPointF(a), QPointF(b)));
    }
    painter.restore();
    return;
  }

  // A document-channel preview is not an RGB paint preview. The committed edit
  // first converts the paint color to gray, then presents that gray either by
  // itself or through the channel's colored overlay. Build the prospective
  // value/coverage image at viewport resolution so the preview follows those
  // same rules without doing work proportional to the document pixel count.
  const auto* preview_channel =
      quick_mask_active_ ? nullptr : active_document_channel_const();
  if (quick_mask_active_ || editing_smart_filter_mask() ||
      (layer_edit_target_ == LayerEditTarget::DocumentChannel &&
       preview_channel != nullptr &&
       preview_channel->kind() == DocumentChannelKind::Alpha)) {
    const QRectF exact_target_rect(widget_position_f(QPointF(0.0, 0.0)),
                                   widget_position_f(QPointF(document_->width(), document_->height())));
    const bool pixel_aligned_view = uses_pixel_aligned_view(zoom_);
    QRect pixel_aligned_target_rect;
    if (pixel_aligned_view) {
      const auto top_left = widget_position(QPoint(0, 0));
      const auto bottom_right = widget_position(QPoint(document_->width(), document_->height()));
      pixel_aligned_target_rect =
          QRect(top_left, QSize(bottom_right.x() - top_left.x(), bottom_right.y() - top_left.y()));
    }
    const QRectF target_rect = pixel_aligned_view ? QRectF(pixel_aligned_target_rect) : exact_target_rect;
    const auto preview_rect = target_rect.toAlignedRect().intersected(exposed_rect).intersected(rect());
    if (!preview_rect.isEmpty()) {
      QImage source(preview_rect.size(), QImage::Format_ARGB32_Premultiplied);
      source.fill(Qt::transparent);
      {
        QPainter source_painter(&source);
        source_painter.translate(-preview_rect.topLeft());
        source_painter.setClipRect(target_rect);
        if (selection_clips_grayscale_edits()) {
          QRegion widget_selection;
          for (const auto& selection_rect : selection_) {
            widget_selection += widget_rect_for_document_rect(selection_rect);
          }
          source_painter.setClipRegion(widget_selection, Qt::IntersectClip);
        }
        source_painter.setRenderHint(QPainter::Antialiasing, true);

        if (tool_ == CanvasTool::Gradient) {
          QLinearGradient linear_gradient(a, b);
          const auto radius =
              std::max(1.0, std::hypot(static_cast<double>(b.x() - a.x()), static_cast<double>(b.y() - a.y())));
          QRadialGradient radial_gradient(a, radius);
          auto* gradient = gradient_method_ == GradientMethod::Radial ? static_cast<QGradient*>(&radial_gradient)
                                                                      : static_cast<QGradient*>(&linear_gradient);
          for (const auto& stop : effective_gradient_stops()) {
            const auto value = mask_value_from_color(QColor(stop.color.r, stop.color.g, stop.color.b));
            const auto alpha = static_cast<int>(std::clamp(
                std::lround(static_cast<double>(stop.color.a) * static_cast<double>(gradient_opacity_) / 100.0),
                0L, 255L));
            gradient->setColorAt(gradient_reverse_ ? 1.0 - static_cast<double>(stop.location)
                                                   : static_cast<double>(stop.location),
                                 QColor(value, value, value, alpha));
          }
          source_painter.fillRect(target_rect, QBrush(*gradient));
        } else {
          const auto value = mask_value_from_color(primary_color_);
          const auto alpha = static_cast<int>(
              std::clamp(std::lround(255.0 * static_cast<double>(brush_opacity_) / 100.0), 0L, 255L));
          const QColor gray(value, value, value, alpha);
          if (tool_ == CanvasTool::Line) {
            QPen pen(gray);
            pen.setWidth(std::max(1, static_cast<int>(std::round(static_cast<double>(brush_size_) * zoom_))));
            pen.setCapStyle(Qt::RoundCap);
            pen.setJoinStyle(Qt::RoundJoin);
            pen.setCosmetic(false);
            source_painter.setPen(pen);
            source_painter.setBrush(Qt::NoBrush);
            source_painter.drawLine(a, b);
          } else if (tool_ == CanvasTool::Rectangle) {
            if (fill_shapes_) {
              source_painter.setPen(Qt::NoPen);
              source_painter.setBrush(gray);
            } else {
              QPen pen(gray);
              pen.setWidth(std::max(1, static_cast<int>(std::round(static_cast<double>(brush_size_) * zoom_))));
              pen.setCosmetic(false);
              source_painter.setPen(pen);
              source_painter.setBrush(Qt::NoBrush);
            }
            const auto shape_rect = normalized_rect(a, b);
            if (shape_corner_radius_ > 0) {
              const auto radius = static_cast<double>(shape_corner_radius_) * zoom_;
              const auto clamped = std::min(radius, std::min(shape_rect.width(), shape_rect.height()) / 2.0);
              source_painter.drawRoundedRect(shape_rect, clamped, clamped);
            } else {
              source_painter.drawRect(shape_rect);
            }
          } else if (tool_ == CanvasTool::Ellipse) {
            if (fill_shapes_) {
              source_painter.setPen(Qt::NoPen);
              source_painter.setBrush(gray);
            } else {
              QPen pen(gray);
              pen.setWidth(std::max(1, static_cast<int>(std::round(static_cast<double>(brush_size_) * zoom_))));
              pen.setCosmetic(false);
              source_painter.setPen(pen);
              source_painter.setBrush(Qt::NoBrush);
            }
            source_painter.drawEllipse(normalized_rect(a, b));
          }
        }
      }

      if (quick_mask_active_ || mask_display_mode_ == MaskDisplayMode::Overlay) {
        QImage base(preview_rect.size(), QImage::Format_ARGB32_Premultiplied);
        base.fill(QColor(36, 38, 41));
        {
          QPainter base_painter(&base);
          base_painter.translate(-preview_rect.topLeft());
          draw_checkerboard(base_painter, target_rect, preview_rect);
          if (!render_cache_.isNull()) {
            const auto& display_image = zoom_ < 1.0 ? display_image_for_zoom() : render_cache_;
            base_painter.setRenderHint(
                QPainter::SmoothPixmapTransform,
                uses_smooth_display_scaling(zoom_, uses_deep_zoom_pixel_renderer(zoom_)));
            if (pixel_aligned_view) {
              base_painter.drawImage(pixel_aligned_target_rect, display_image, display_image.rect());
            } else {
              base_painter.drawImage(target_rect, display_image, QRectF(display_image.rect()));
            }
          }
        }

        const auto display = preview_channel != nullptr
                                 ? preview_channel->display_info()
                                 : DocumentChannelDisplayInfo{};
        const QColor overlay =
            quick_mask_active_
                ? QColor(255, 0, 0)
                : QColor(display.color.red, display.color.green,
                         display.color.blue);
        const bool selected_areas =
            !quick_mask_active_ &&
            display.color_indicates ==
                DocumentChannelColorIndicates::SelectedAreas;
        const auto overlay_opacity =
            quick_mask_active_ ? 0.5F
                               : std::clamp(display.opacity, 0.0F, 1.0F);
        for (int y = 0; y < source.height(); ++y) {
          auto* source_row = reinterpret_cast<QRgb*>(source.scanLine(y));
          const auto* base_row = reinterpret_cast<const QRgb*>(base.constScanLine(y));
          for (int x = 0; x < source.width(); ++x) {
            const auto paint = qUnpremultiply(source_row[x]);
            const auto paint_alpha = qAlpha(paint);
            if (paint_alpha == 0) {
              continue;
            }
            const auto value = qRed(paint);
            const auto coverage = selected_areas ? value : 255 - value;
            const auto channel_alpha = static_cast<int>(std::clamp(
                std::lround(static_cast<double>(coverage) * static_cast<double>(overlay_opacity)), 0L, 255L));
            const auto background = qUnpremultiply(base_row[x]);
            const auto blend = [channel_alpha](int foreground, int background_component) {
              return (foreground * channel_alpha + background_component * (255 - channel_alpha) + 127) / 255;
            };
            source_row[x] = qPremultiply(qRgba(blend(overlay.red(), qRed(background)),
                                                  blend(overlay.green(), qGreen(background)),
                                                  blend(overlay.blue(), qBlue(background)), paint_alpha));
          }
        }
      }
      if (quick_mask_active_ || mask_display_mode_ == MaskDisplayMode::Overlay ||
          mask_display_mode_ == MaskDisplayMode::Grayscale) {
        // For a mask-only view the current grayscale image is already on the
        // canvas; this translucent prospective paint is layered over it.
        painter.drawImage(preview_rect.topLeft(), source);
      }
    }

    if (tool_ == CanvasTool::Gradient) {
      const auto stops = effective_gradient_stops();
      painter.setPen(QPen(QColor(230, 235, 242), 1));
      const auto first =
          gradient_color_at(stops, static_cast<float>(gradient_opacity_) / 100.0F, gradient_reverse_, 0.0);
      const auto last =
          gradient_color_at(stops, static_cast<float>(gradient_opacity_) / 100.0F, gradient_reverse_, 1.0);
      painter.setBrush(QColor(first.r, first.g, first.b, first.a));
      painter.drawEllipse(a, 4, 4);
      painter.setBrush(QColor(last.r, last.g, last.b, last.a));
      painter.drawEllipse(b, 4, 4);
    }
    return;
  }

  if (tool_ == CanvasTool::Gradient) {
    QGradient* raw_gradient = nullptr;
    QLinearGradient linear_gradient(a, b);
    const auto radius = std::max(1.0, std::hypot(static_cast<double>(b.x() - a.x()), static_cast<double>(b.y() - a.y())));
    QRadialGradient radial_gradient(a, radius);
    raw_gradient = gradient_method_ == GradientMethod::Radial ? static_cast<QGradient*>(&radial_gradient)
                                                              : static_cast<QGradient*>(&linear_gradient);
    const auto stops = effective_gradient_stops();
    for (const auto& stop : stops) {
      auto color = stop.color;
      color.a = static_cast<std::uint8_t>(
          std::clamp(std::lround(static_cast<float>(color.a) * static_cast<float>(gradient_opacity_) / 100.0F), 0L,
                     255L));
      raw_gradient->setColorAt(gradient_reverse_ ? 1.0 - static_cast<double>(stop.location)
                                                 : static_cast<double>(stop.location),
                                QColor(color.r, color.g, color.b, color.a));
    }

    QRect document_rect;
    if (editing_grayscale_target()) {
      document_rect =
          QRect(0, 0, document_ != nullptr ? document_->width() : 0, document_ != nullptr ? document_->height() : 0);
    } else if (const auto layer_rect = active_layer_document_rect()) {
      document_rect = *layer_rect;
    }
    if (document_ != nullptr) {
      document_rect = document_rect.intersected(QRect(0, 0, document_->width(), document_->height()));
    }
    if (has_selection() && selected_document_rect().has_value()) {
      document_rect = document_rect.intersected(*selected_document_rect());
    }
    if (!document_rect.isEmpty()) {
      const auto fill_rect = widget_rect_for_document_rect(QRectF(document_rect));
      painter.save();
      painter.setClipRect(fill_rect);
      if (has_selection()) {
        QRegion widget_selection;
        for (const auto& rect : selection_.intersected(document_rect)) {
          widget_selection += widget_rect_for_document_rect(rect);
        }
        painter.setClipRegion(widget_selection, Qt::IntersectClip);
      }
      painter.fillRect(fill_rect, QBrush(*raw_gradient));
      painter.restore();
    }

    painter.setPen(QPen(QColor(230, 235, 242), 1));
    const auto first = gradient_color_at(stops, static_cast<float>(gradient_opacity_) / 100.0F, gradient_reverse_, 0.0);
    const auto last = gradient_color_at(stops, static_cast<float>(gradient_opacity_) / 100.0F, gradient_reverse_, 1.0);
    painter.setBrush(QColor(first.r, first.g, first.b, first.a));
    painter.drawEllipse(a, 4, 4);
    painter.setBrush(QColor(last.r, last.g, last.b, last.a));
    painter.drawEllipse(b, 4, 4);
  } else if (tool_ == CanvasTool::Line) {
    QPen pen(primary_color_);
    pen.setWidth(std::max(1, static_cast<int>(std::round(static_cast<double>(brush_size_) * zoom_))));
    pen.setCosmetic(false);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawLine(a, b);
  } else if (tool_ == CanvasTool::Rectangle) {
    if (fill_shapes_) {
      painter.setPen(Qt::NoPen);
      painter.setBrush(QColor(primary_color_.red(), primary_color_.green(), primary_color_.blue(),
                              std::clamp(brush_opacity_ * 2, 60, 180)));
    } else {
      QPen pen(primary_color_);
      pen.setWidth(std::max(1, static_cast<int>(std::round(static_cast<double>(brush_size_) * zoom_))));
      pen.setCosmetic(false);
      painter.setPen(pen);
      painter.setBrush(Qt::NoBrush);
    }
    const auto preview_rect = normalized_rect(a, b);
    if (shape_corner_radius_ > 0) {
      const auto radius = static_cast<double>(shape_corner_radius_) * zoom_;
      const auto clamped = std::min(radius, std::min(preview_rect.width(), preview_rect.height()) / 2.0);
      painter.drawRoundedRect(preview_rect, clamped, clamped);
    } else {
      painter.drawRect(preview_rect);
    }
  } else if (tool_ == CanvasTool::Ellipse) {
    if (fill_shapes_) {
      painter.setPen(Qt::NoPen);
      painter.setBrush(QColor(primary_color_.red(), primary_color_.green(), primary_color_.blue(),
                              std::clamp(brush_opacity_ * 2, 60, 180)));
    } else {
      QPen pen(primary_color_);
      pen.setWidth(std::max(1, static_cast<int>(std::round(static_cast<double>(brush_size_) * zoom_))));
      pen.setCosmetic(false);
      painter.setPen(pen);
      painter.setBrush(Qt::NoBrush);
    }
    painter.drawEllipse(normalized_rect(a, b));
  }
}

bool CanvasWidget::draw_shape_appearance_preview(QPainter& painter,
                                                 const ShapePreviewAppearance& appearance) {
  // Approximation notes: stroke draws centered regardless of the committed
  // inside alignment, and line arrowheads appear at commit; both stay close
  // enough for a live drag preview.
  QPainterPath path;
  const auto to_widget = [this](QPointF document_point) {
    return widget_position_f(document_point);
  };
  const auto drag_rect = shape_drag_rect(shape_start_, shape_current_);
  const QRectF widget_rect(to_widget(QPointF(drag_rect.topLeft())),
                           to_widget(QPointF(drag_rect.bottomRight())));
  switch (tool_) {
    case CanvasTool::Rectangle: {
      if (shape_corner_radius_ > 0) {
        const auto radius = static_cast<double>(shape_corner_radius_) * zoom_;
        const auto clamped =
            std::min(radius, std::min(widget_rect.width(), widget_rect.height()) / 2.0);
        path.addRoundedRect(widget_rect, clamped, clamped);
      } else {
        path.addRect(widget_rect);
      }
      break;
    }
    case CanvasTool::Ellipse:
      path.addEllipse(widget_rect);
      break;
    case CanvasTool::Line: {
      // The committed line is a filled band of the weight; preview the same
      // band with a flat-capped pen path.
      QPainterPath spine;
      spine.moveTo(to_widget(QPointF(shape_start_)));
      spine.lineTo(to_widget(QPointF(shape_current_)));
      QPainterPathStroker stroker;
      stroker.setWidth(std::max(1.0, static_cast<double>(appearance.line_weight) * zoom_));
      stroker.setCapStyle(Qt::FlatCap);
      path = stroker.createStroke(spine);
      break;
    }
    case CanvasTool::Polygon: {
      const auto subpath = polygon_drag_subpath(QPointF(shape_start_), QPointF(shape_current_));
      if (subpath.anchors.size() < 3) {
        return false;
      }
      QPolygonF outline;
      for (const auto& anchor : subpath.anchors) {
        outline << to_widget(QPointF(anchor.anchor_x, anchor.anchor_y));
      }
      path.addPolygon(outline);
      path.closeSubpath();
      break;
    }
    case CanvasTool::CustomShape: {
      if (custom_shape_path_ == nullptr || custom_shape_path_->subpaths.empty() ||
          drag_rect.width() <= 0 || drag_rect.height() <= 0) {
        return false;
      }
      // The stamp scales the unit-box shape into the drag rect.
      for (const auto& subpath : custom_shape_path_->subpaths) {
        if (subpath.anchors.empty()) {
          continue;
        }
        const auto map = [&](double x, double y) {
          return to_widget(QPointF(drag_rect.x() + x * drag_rect.width(),
                                   drag_rect.y() + y * drag_rect.height()));
        };
        const auto& anchors = subpath.anchors;
        path.moveTo(map(anchors[0].anchor_x, anchors[0].anchor_y));
        const auto anchor_count = anchors.size();
        const auto segment_count = subpath.closed ? anchor_count : anchor_count - 1;
        for (std::size_t i = 0; i < segment_count; ++i) {
          const auto& from = anchors[i];
          const auto& to = anchors[(i + 1) % anchor_count];
          path.cubicTo(map(from.out_x, from.out_y), map(to.in_x, to.in_y),
                       map(to.anchor_x, to.anchor_y));
        }
        path.closeSubpath();
      }
      break;
    }
    default:
      return false;
  }
  if (path.isEmpty()) {
    return false;
  }
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setPen(Qt::NoPen);
  if (appearance.fill.isValid()) {
    painter.setBrush(appearance.fill);
  } else {
    painter.setBrush(Qt::NoBrush);
  }
  painter.drawPath(path);
  if (appearance.stroke_enabled && appearance.stroke.isValid() && appearance.stroke_width > 0.0) {
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(appearance.stroke, std::max(0.5, appearance.stroke_width * zoom_)));
    painter.drawPath(path);
  }
  painter.restore();
  return true;
}

void CanvasWidget::draw_drag_size_readout(QPainter& painter) const {
  // Photoshop-style live W x H readout while dragging out a shape or a
  // rectangular/elliptical marquee, drawn just below-right of the drag corner.
  QRect rect;
  QPoint corner;
  if (drawing_shape_ && (tool_ == CanvasTool::Rectangle || tool_ == CanvasTool::Ellipse)) {
    rect = shape_drag_rect(shape_start_, shape_current_);
    corner = shape_current_;
  } else if (selecting_ && (tool_ == CanvasTool::Marquee || tool_ == CanvasTool::EllipticalMarquee)) {
    rect = marquee_selection_rect(selection_start_, selection_current_);
    corner = selection_current_;
  } else {
    return;
  }
  const auto readout = tr("%1 x %2 px").arg(rect.width()).arg(rect.height());
  const auto anchor = widget_position(corner);
  const auto metrics = painter.fontMetrics();
  const auto text_width = metrics.horizontalAdvance(readout);
  QPointF text_position(static_cast<double>(anchor.x()) + 16.0,
                        static_cast<double>(anchor.y()) + 24.0 + static_cast<double>(metrics.ascent()));
  text_position.setX(std::clamp(text_position.x(), 4.0, std::max(4.0, static_cast<double>(width() - text_width - 4))));
  text_position.setY(std::clamp(text_position.y(), static_cast<double>(metrics.ascent()) + 4.0,
                                std::max(static_cast<double>(metrics.ascent()) + 4.0,
                                         static_cast<double>(height()) - static_cast<double>(metrics.descent()) - 4.0)));
  // Same outlined-text treatment as draw_brush_adjust_readout so it reads on any artwork.
  painter.setPen(QColor(20, 23, 28, 220));
  for (const auto& offset : {QPointF(-1.0, 0.0), QPointF(1.0, 0.0), QPointF(0.0, -1.0), QPointF(0.0, 1.0)}) {
    painter.drawText(text_position + offset, readout);
  }
  painter.setPen(QColor(245, 248, 252));
  painter.drawText(text_position, readout);
}

void CanvasWidget::draw_text_rect_preview(QPainter& painter) const {
  if (!dragging_text_rect_) {
    return;
  }

  const auto preview_rect = QRect(widget_position(text_rect_start_), widget_position(text_rect_current_)).normalized();
  if (preview_rect.width() < 2 && preview_rect.height() < 2) {
    return;
  }

  painter.save();
  painter.setBrush(QColor(65, 135, 220, 28));
  painter.setPen(Qt::NoPen);
  painter.drawRect(preview_rect);
  QPen border(QColor(95, 170, 255), 1.0);
  border.setCosmetic(true);
  border.setDashPattern({5.0, 4.0});
  painter.setBrush(Qt::NoBrush);
  painter.setPen(border);
  painter.drawRect(preview_rect.adjusted(0, 0, -1, -1));
  painter.restore();
}

void CanvasWidget::draw_pixel(Layer& layer, QPoint document_point, QColor color, bool erase) {
  if (!document_contains(document_point) || !selection_allows(document_point)) {
    return;
  }

  auto& pixels = layer.pixels();
  if (pixels.format().bit_depth != BitDepth::UInt8 || pixels.format().channels < 3) {
    return;
  }

  const auto local = layer_position(layer, document_point);
  if (local.x() < 0 || local.y() < 0 || local.x() >= pixels.width() || local.y() >= pixels.height()) {
    return;
  }

  auto* px = pixels.pixel(local.x(), local.y());
  if (erase) {
    if (pixels.format().channels >= 4) {
      px[3] = 0;
    } else {
      px[0] = channel_from_color(secondary_color_, 0);
      px[1] = channel_from_color(secondary_color_, 1);
      px[2] = channel_from_color(secondary_color_, 2);
    }
    return;
  }

  px[0] = channel_from_color(color, 0);
  px[1] = channel_from_color(color, 1);
  px[2] = channel_from_color(color, 2);
  if (pixels.format().channels >= 4) {
    px[3] = channel_from_color(color, 3);
  }
}

QRect CanvasWidget::draw_line(QPoint from, QPoint to, bool erase) {
  if (editing_grayscale_target()) {
    return draw_mask_line(from, to, erase);
  }
  if (document_ == nullptr || !document_->active_layer_id().has_value()) {
    return {};
  }

  auto options = edit_options(primary_color_, secondary_color_, brush_size_, brush_opacity_, brush_softness_,
                              fill_shapes_,
                              active_layer_locks_transparent_pixels(), *this);
  if (brush_opacity_ < 100) {
    options.stroke_pixel_gate = [this](std::int32_t x, std::int32_t y) {
      return brush_stroke_pixels_.insert(stroke_pixel_key(x, y)).second;
    };
  }
  return to_qrect(patchy::draw_line(*document_, *document_->active_layer_id(), from.x(), from.y(), to.x(), to.y(),
                                       options, erase));
}

QRect CanvasWidget::draw_gradient(QPoint from, QPoint to) {
  if (editing_grayscale_target()) {
    return draw_mask_gradient(from, to);
  }
  if (document_ == nullptr || !document_->active_layer_id().has_value()) {
    return {};
  }

  return to_qrect(patchy::draw_gradient(
      *document_, *document_->active_layer_id(), from.x(), from.y(), to.x(), to.y(),
      edit_options(primary_color_, secondary_color_, brush_size_, brush_opacity_, brush_softness_, fill_shapes_,
                   active_layer_locks_transparent_pixels(), *this),
      current_gradient_options()));
}

GradientOptions CanvasWidget::current_gradient_options() const {
  GradientOptions gradient;
  gradient.method = gradient_method_;
  gradient.reverse = gradient_reverse_;
  gradient.opacity = static_cast<float>(gradient_opacity_) / 100.0F;
  gradient.stops = effective_gradient_stops();
  return gradient;
}

QRect CanvasWidget::draw_rectangle(QPoint from, QPoint to, bool erase) {
  if (editing_grayscale_target()) {
    return draw_mask_rectangle(from, to, erase);
  }
  if (document_ == nullptr || !document_->active_layer_id().has_value()) {
    return {};
  }

  const auto rect = normalized_rect(from, to);
  auto options = edit_options(primary_color_, secondary_color_, brush_size_, brush_opacity_, brush_softness_,
                              fill_shapes_,
                              active_layer_locks_transparent_pixels(), *this);
  options.shape_corner_radius = shape_corner_radius_;
  // Single-pass shape rendering composites each pixel once, so no buildup gate is needed; the gate
  // only matters for the crisp 1px-outline legacy path, which paints overlapping line segments.
  if (brush_opacity_ < 100) {
    options.stroke_pixel_gate = [this](std::int32_t x, std::int32_t y) {
      return brush_stroke_pixels_.insert(stroke_pixel_key(x, y)).second;
    };
  }
  return to_qrect(patchy::draw_rectangle(*document_, *document_->active_layer_id(), to_core_rect(rect),
                                            options, erase));
}

QRect CanvasWidget::draw_ellipse(QPoint from, QPoint to, bool erase) {
  if (editing_grayscale_target()) {
    return draw_mask_ellipse(from, to, erase);
  }
  if (document_ == nullptr || !document_->active_layer_id().has_value()) {
    return {};
  }

  const auto rect = normalized_rect(from, to);
  auto options = edit_options(primary_color_, secondary_color_, brush_size_, brush_opacity_, brush_softness_,
                              fill_shapes_,
                              active_layer_locks_transparent_pixels(), *this);
  // Only the crisp 1px-outline legacy path needs the buildup gate (see draw_rectangle).
  if (brush_opacity_ < 100) {
    options.stroke_pixel_gate = [this](std::int32_t x, std::int32_t y) {
      return brush_stroke_pixels_.insert(stroke_pixel_key(x, y)).second;
    };
  }
  return to_qrect(patchy::draw_ellipse(*document_, *document_->active_layer_id(), to_core_rect(rect),
                                          options, erase));
}

QRect CanvasWidget::flood_fill(QPoint start) {
  if (editing_grayscale_target()) {
    return flood_fill_mask(start);
  }
  if (document_ == nullptr || !document_->active_layer_id().has_value()) {
    return {};
  }

  return to_qrect(patchy::flood_fill(
      *document_, *document_->active_layer_id(), start.x(), start.y(),
      edit_options(primary_color_, secondary_color_, brush_size_, brush_opacity_, brush_softness_, fill_shapes_,
                   active_layer_locks_transparent_pixels(), *this)));
}

QRect CanvasWidget::draw_mask_line(QPoint from, QPoint to, bool erase) {
  return draw_mask_brush_segment(from, to, erase);
}

QRect CanvasWidget::draw_mask_gradient(QPoint from, QPoint to) {
  if (document_ == nullptr) {
    return {};
  }

  const auto clip_selection = selection_clips_grayscale_edits();
  auto affected = clip_selection && selected_document_rect().has_value()
                      ? *selected_document_rect()
                      : QRect(0, 0, document_->width(), document_->height());
  affected = affected.intersected(QRect(0, 0, document_->width(), document_->height()));
  auto target = active_grayscale_edit_target(affected);
  if (affected.isEmpty() || !target.has_value() || target->pixels == nullptr) {
    return {};
  }

  const auto bounds = target->bounds;
  affected = affected.intersected(bounds);
  if (affected.isEmpty()) {
    return {};
  }

  const auto gradient = current_gradient_options();
  const auto stops = normalized_gradient_stops(gradient.stops);
  const auto dx = static_cast<double>(to.x() - from.x());
  const auto dy = static_cast<double>(to.y() - from.y());
  const auto length_squared = dx * dx + dy * dy;
  const auto radius = std::sqrt(length_squared);

  QRect dirty;
  for (int y = affected.top(); y <= affected.bottom(); ++y) {
    for (int x = affected.left(); x <= affected.right(); ++x) {
      const QPoint document_point(x, y);
      if (!selection_allows(document_point)) {
        continue;
      }
      double t = 0.0;
      switch (gradient.method) {
        case GradientMethod::Radial:
          t = radius <= 0.0 ? 0.0
                            : std::sqrt(static_cast<double>(x - from.x()) * static_cast<double>(x - from.x()) +
                                        static_cast<double>(y - from.y()) * static_cast<double>(y - from.y())) /
                                  radius;
          break;
        case GradientMethod::Linear:
          t = length_squared <= 0.0
                  ? 0.0
                  : ((static_cast<double>(x - from.x()) * dx + static_cast<double>(y - from.y()) * dy) /
                     length_squared);
          break;
      }
      const auto color = gradient_color_at(stops, gradient.opacity, gradient.reverse, t);
      auto coverage = static_cast<float>(color.a) / 255.0F;
      if (clip_selection) {
        coverage *= static_cast<float>(selection_alpha_at(document_point)) / 255.0F;
      }
      if (coverage <= 0.0F) {
        continue;
      }
      const auto value = mask_value_from_color(QColor(color.r, color.g, color.b));
      auto* px = target->pixels->pixel(x - bounds.x(), y - bounds.y());
      *px = blend_mask_value(*px, value, coverage);
      dirty = dirty.united(QRect(document_point, QSize(1, 1)));
    }
    tick_processing_operation();
  }
  return dirty;
}

QRect CanvasWidget::draw_mask_rectangle(QPoint from, QPoint to, bool erase) {
  if (!fill_shapes_ && shape_corner_radius_ <= 0 && std::max(1, brush_size_) <= 1) {
    const auto rect = normalized_rect(from, to);
    QRect dirty;
    dirty = dirty.united(draw_mask_line(rect.topLeft(), rect.topRight(), erase));
    dirty = dirty.united(draw_mask_line(rect.topRight(), rect.bottomRight(), erase));
    dirty = dirty.united(draw_mask_line(rect.bottomRight(), rect.bottomLeft(), erase));
    dirty = dirty.united(draw_mask_line(rect.bottomLeft(), rect.topLeft(), erase));
    return dirty;
  }
  return render_mask_shape(normalized_rect(from, to), erase, patchy::ShapeKind::Rectangle);
}

QRect CanvasWidget::draw_mask_ellipse(QPoint from, QPoint to, bool erase) {
  if (!fill_shapes_ && std::max(1, brush_size_) <= 1) {
    const auto rect = normalized_rect(from, to);
    constexpr int kSamples = 720;
    const auto rx = std::max(1.0, static_cast<double>(rect.width()) / 2.0);
    const auto ry = std::max(1.0, static_cast<double>(rect.height()) / 2.0);
    const auto cx = static_cast<double>(rect.x()) + rx;
    const auto cy = static_cast<double>(rect.y()) + ry;
    QPoint previous(static_cast<int>(std::round(cx + rx)), static_cast<int>(std::round(cy)));
    QRect dirty;
    for (int i = 1; i <= kSamples; ++i) {
      const auto angle = (static_cast<double>(i) / static_cast<double>(kSamples)) * 2.0 * kPi;
      QPoint current(static_cast<int>(std::round(cx + std::cos(angle) * rx)),
                     static_cast<int>(std::round(cy + std::sin(angle) * ry)));
      dirty = dirty.united(draw_mask_line(previous, current, erase));
      previous = current;
    }
    return dirty;
  }
  return render_mask_shape(normalized_rect(from, to), erase, patchy::ShapeKind::Ellipse);
}

// Shared single-pass renderer for layer-mask shapes — mirrors the core pixel-layer render_shape so
// mask outlines/fills get the same antialiased edges, Soft feathering and rounded corners, with no
// brush-stamp buildup.
QRect CanvasWidget::render_mask_shape(QRect rect, bool erase, patchy::ShapeKind kind) {
  if (document_ == nullptr) {
    return {};
  }
  rect = rect.normalized();
  if (rect.width() <= 0 || rect.height() <= 0) {
    return {};
  }

  patchy::EditOptions options;
  options.fill_shapes = fill_shapes_;
  options.brush_size = brush_size_;
  options.brush_softness = brush_softness_;
  if (kind == patchy::ShapeKind::Rectangle) {
    options.shape_corner_radius = shape_corner_radius_;
  }
  const auto params = patchy::make_shape_coverage_params(to_core_rect(rect), options, kind);
  const auto margin = params.fill ? (params.band * 0.5 + 1.0)
                                  : (params.half_thickness + params.band * 0.5 + 1.0);
  const auto m = static_cast<int>(std::ceil(margin));
  const auto shape_bbox = rect.adjusted(-m, -m, m, m);
  const auto affected = shape_bbox.intersected(QRect(0, 0, document_->width(), document_->height()));
  if (affected.isEmpty()) {
    return {};
  }

  auto target = active_grayscale_edit_target(affected);
  if (!target.has_value() || target->pixels == nullptr) {
    return {};
  }
  const auto bounds = target->bounds;
  const auto value = erase ? std::uint8_t{0} : mask_value_from_color(primary_color_);
  const auto opacity = static_cast<float>(brush_opacity_) / 100.0F;
  QRect dirty;
  for (int y = affected.top(); y <= affected.bottom(); ++y) {
    for (int x = affected.left(); x <= affected.right(); ++x) {
      const QPoint document_point(x, y);
      if (!selection_allows(document_point)) {
        continue;
      }
      auto coverage = opacity * patchy::shape_pixel_coverage(params, x, y);
      if (selection_clips_grayscale_edits()) {
        coverage *= static_cast<float>(selection_alpha_at(document_point)) / 255.0F;
      }
      if (coverage <= 0.0F) {
        continue;
      }
      auto* px = target->pixels->pixel(x - bounds.x(), y - bounds.y());
      *px = blend_mask_value(*px, value, coverage);
      dirty = dirty.united(QRect(document_point, QSize(1, 1)));
    }
    tick_processing_operation();
  }
  return dirty;
}

QRect CanvasWidget::flood_fill_mask(QPoint start) {
  if (document_ == nullptr || !document_contains(start) || !selection_allows(start)) {
    return {};
  }
  auto grayscale_target = active_grayscale_edit_target(QRect(0, 0, document_->width(), document_->height()));
  if (!grayscale_target.has_value() || grayscale_target->pixels == nullptr) {
    return {};
  }

  const auto bounds = grayscale_target->bounds;
  auto* pixels = grayscale_target->pixels;
  const QPoint local_start(start.x() - bounds.x(), start.y() - bounds.y());
  if (local_start.x() < 0 || local_start.y() < 0 || local_start.x() >= pixels->width() ||
      local_start.y() >= pixels->height()) {
    return {};
  }

  const auto target = *pixels->pixel(local_start.x(), local_start.y());
  const auto replacement = mask_value_from_color(primary_color_);
  if (target == replacement) {
    return {};
  }

  std::queue<QPoint> queue;
  std::vector<std::uint8_t> visited(static_cast<std::size_t>(pixels->width()) *
                                    static_cast<std::size_t>(pixels->height()));
  queue.push(local_start);
  QRect dirty;
  std::size_t progress_counter = 0;
  while (!queue.empty()) {
    ++progress_counter;
    if (progress_counter % 4096U == 0U) {
      tick_processing_operation();
    }
    const auto local = queue.front();
    queue.pop();
    if (local.x() < 0 || local.y() < 0 || local.x() >= pixels->width() || local.y() >= pixels->height()) {
      continue;
    }
    const auto index = static_cast<std::size_t>(local.y()) * static_cast<std::size_t>(pixels->width()) +
                       static_cast<std::size_t>(local.x());
    if (visited[index] != 0U) {
      continue;
    }
    visited[index] = 1U;
    const QPoint document_point(bounds.x() + local.x(), bounds.y() + local.y());
    if (!selection_allows(document_point)) {
      continue;
    }
    auto* px = pixels->pixel(local.x(), local.y());
    if (*px != target) {
      continue;
    }
    *px = replacement;
    dirty = dirty.united(QRect(document_point, QSize(1, 1)));
    queue.push(local + QPoint(1, 0));
    queue.push(local + QPoint(-1, 0));
    queue.push(local + QPoint(0, 1));
    queue.push(local + QPoint(0, -1));
  }
  return dirty;
}

void CanvasWidget::begin_color_pick(QPoint widget_position, QPoint global_position) {
  color_picking_ = true;
  grabMouse();
  update_color_pick(widget_position, global_position);
}

void CanvasWidget::update_color_pick(QPoint widget_position, QPoint global_position) {
  const auto point = document_position(widget_position);
  if (document_contains(point)) {
    pick_color(point);
    return;
  }

  if (const auto picked = screen_color_at_global_position(global_position); picked.has_value()) {
    set_picked_color(*picked);
  }
}

void CanvasWidget::end_color_pick() {
  if (!color_picking_) {
    return;
  }
  color_picking_ = false;
  if (QWidget::mouseGrabber() == this) {
    releaseMouse();
  }
  update_tool_cursor();
}

void CanvasWidget::set_picked_color(QColor picked) {
  if (!picked.isValid()) {
    return;
  }
  picked.setAlpha(255);
  primary_color_ = picked;
  if (color_picked_callback_) {
    color_picked_callback_(picked);
  }
}

void CanvasWidget::pick_color(QPoint point) {
  if (document_ == nullptr || !document_contains(point)) {
    return;
  }

  set_picked_color(compose_document_pixel(point.x(), point.y()));
}

}  // namespace patchy::ui
