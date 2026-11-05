// CanvasWidget's drawing for a rotated view (docs/view-navigation-rotation.md). An
// upright view never reaches this file: paintEvent keeps its axis-aligned path, with
// its pixel-aligned placement and deep-zoom raster, bit for bit. A rotated view draws
// the same images under the view transform instead: the painter speaks document
// coordinates, so every image and patch lands on its document rect whatever the angle.
// Work per frame stays proportional to the exposed screen area.

#include "ui/canvas_widget.hpp"

#include "core/artboard.hpp"
#include "ui/canvas_widget_shared.hpp"
#include "ui/theme_palette.hpp"

#include <QPainter>
#include <QPainterPath>
#include <QPen>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace patchy::ui {

namespace {

// Ghost tiles drawn one by one under rotation; past this the far ring is skipped
// (the preview window's Fit view shows the whole tiling).
constexpr std::int64_t kRotatedTilingTileLimit = 48;

QPainterPath closed_polygon_path(const QPolygonF& polygon) {
  QPainterPath path;
  path.addPolygon(polygon);
  path.closeSubpath();
  return path;
}

}  // namespace

void CanvasWidget::draw_image_over_document_rect(QPainter& painter, const QRectF& document_rect,
                                                 const QImage& image) const {
  if (image.isNull() || document_rect.isEmpty()) {
    return;
  }
  if (view_axis_aligned()) {
    painter.drawImage(widget_rect_for_document_rect(document_rect), image, QRectF(image.rect()));
    return;
  }
  painter.save();
  painter.setTransform(view_transform().to_widget(), true);
  painter.drawImage(document_rect, image, QRectF(image.rect()));
  painter.restore();
}

void CanvasWidget::draw_rotated_checkerboard(QPainter& painter, const QRectF& document_rect,
                                             QRect exposed_rect) const {
  // Screen-aligned squares, like the upright view's, inside the rect's on-screen outline.
  const auto outline = closed_polygon_path(view_transform().map_document_rect(document_rect));
  painter.save();
  painter.setClipPath(outline, Qt::IntersectClip);
  draw_checkerboard(painter, outline.boundingRect(), exposed_rect);
  painter.restore();
}

void CanvasWidget::draw_rotated_canvas_backdrop(QPainter& painter, QRect exposed_rect) const {
  if (document_ == nullptr) {
    return;
  }
  if (!document_has_artboards(std::as_const(*document_))) {
    draw_rotated_checkerboard(painter, QRectF(0.0, 0.0, document_->width(), document_->height()), exposed_rect);
    return;
  }
  // The pasteboard is already down (fill_pasteboard covers the exposed rect); each
  // visible artboard shows the checkerboard.
  for (const auto* artboard : document_artboards(std::as_const(*document_))) {
    if (!artboard->visible()) {
      continue;
    }
    const auto& rect = artboard->artboard()->rect;
    draw_rotated_checkerboard(painter, QRectF(rect.x, rect.y, rect.width, rect.height), exposed_rect);
  }
}

void CanvasWidget::paint_rotated_document(QPainter& painter, QRect exposed_rect, bool draw_transform_overlay,
                                          bool draw_warp_overlay) {
  const auto view = view_transform();
  const QRectF document_rect(0.0, 0.0, document_->width(), document_->height());
  draw_rotated_canvas_backdrop(painter, exposed_rect);
  if (tiling_preview_enabled_) {
    draw_rotated_tiling_preview(painter, exposed_rect);
  }

  painter.save();
  painter.setRenderHint(QPainter::SmoothPixmapTransform, uses_smooth_rotated_display_scaling(view_zoom()));
  painter.setTransform(view.to_widget(), true);
  // From here on the painter speaks document coordinates.
  painter.setClipRect(document_rect, Qt::IntersectClip);

  const auto draw_full = [&](const QImage& image) {
    if (image.isNull()) {
      return;
    }
    const QImage& source = display_source_for_zoom(image);
    // The clipping preview is a diagnostic picture of the composite, not artwork.
    const QImage& shown = &image == &curves_clipping_preview_image_ ? source : proof_display_image(source);
    painter.drawImage(document_rect, shown, QRectF(shown.rect()));
  };
  const auto draw_patch = [&](const RenderedDocumentPatch& source_patch, bool clear_under_patch) {
    if (source_patch.image.isNull() || source_patch.document_rect.isEmpty()) {
      return;
    }
    const QRectF rect(source_patch.document_rect);
    if (clear_under_patch) {
      painter.save();
      painter.resetTransform();
      draw_rotated_checkerboard(painter, rect, exposed_rect);
      painter.restore();
    }
    const QImage image = proof_display_image(source_patch.image);
    painter.drawImage(rect, image, QRectF(image.rect()));
  };
  const auto draw_clipped = [&](const std::optional<QRect>& clip, const auto& draw) {
    if (clip.has_value()) {
      painter.save();
      painter.setClipRect(QRectF(*clip), Qt::IntersectClip);
    }
    draw();
    if (clip.has_value()) {
      painter.restore();
    }
  };

  if (draw_transform_overlay) {
    draw_full(transform_base_cache_);
    for (const auto& patch : transform_preview_patches_) {
      draw_patch(patch, false);
    }
  } else if (draw_warp_overlay) {
    draw_full(warp_base_cache_.isNull() ? render_cache_ : warp_base_cache_);
    for (const auto& patch : warp_preview_patches_) {
      draw_patch(patch, warp_base_cache_.isNull());
    }
  } else if (moving_layer_ && !moving_layers_.empty()) {
    const bool proxy_preview =
        move_drag_uses_proxy_preview_ && !move_proxy_image_.isNull() && !move_base_cache_.isNull();
    const bool base_excludes_layer =
        (proxy_preview || !moving_layers_use_outline_preview_) && !move_base_cache_.isNull();
    draw_full(base_excludes_layer ? move_base_cache_ : render_cache_);
    if (proxy_preview) {
      const QImage& proxy_image = proof_display_image(move_proxy_image_);
      draw_clipped(move_proxy_clip_document_rect(move_preview_delta_), [&] {
        painter.drawImage(QRectF(move_proxy_document_rect_.translated(move_preview_delta_)), proxy_image,
                          QRectF(proxy_image.rect()));
      });
    } else {
      for (const auto& patch : move_preview_patches_) {
        draw_patch(patch, !base_excludes_layer);
      }
    }
  } else if (move_commit_job_.has_value() && !move_commit_hold_base_.isNull()) {
    draw_full(move_commit_hold_base_);
    if (!move_commit_hold_proxy_.isNull()) {
      const QImage& hold_proxy = proof_display_image(move_commit_hold_proxy_);
      draw_clipped(move_commit_hold_proxy_clip_, [&] {
        painter.drawImage(QRectF(move_commit_hold_proxy_rect_), hold_proxy, QRectF(hold_proxy.rect()));
      });
    } else {
      for (const auto& patch : move_commit_hold_patches_) {
        draw_patch(patch, false);
      }
    }
  } else if (patch_tool_dragging_ && !patch_tool_source_image_.isNull()) {
    draw_full(render_cache_);
    if (!patch_tool_drag_proxy_image_.isNull() &&
        (patch_tool_drag_content_aware_move_ || patch_tool_mode_ != PatchToolMode::Source)) {
      painter.drawImage(QRectF(patch_tool_drag_mask_bounds_.translated(patch_tool_drag_delta_)),
                        patch_tool_drag_proxy_image_, QRectF(patch_tool_drag_proxy_image_.rect()));
    }
  } else {
    const bool commit_hold_engaged =
        !transform_commit_hold_image_.isNull() &&
        (render_cache_dirty_ || async_render_cache_in_flight_ || processing_operation_active());
    if (commit_hold_engaged) {
      draw_full(transform_commit_hold_image_);
    } else {
      if (!transform_commit_hold_image_.isNull() && render_settled()) {
        clear_transform_commit_hold();
      }
      draw_full(curves_clipping_mode_.has_value() && !curves_clipping_preview_image_.isNull()
                    ? curves_clipping_preview_image_
                    : render_cache_);
    }
  }
  if (!curves_clipping_mode_.has_value()) {
    if (channel_view_.group == ChannelViewGroup::Color && channel_view_visible_count(channel_view_) == 2) {
      painter.save();
      painter.setCompositionMode(QPainter::CompositionMode_Multiply);
      const auto shown = [this](int index) {
        return channel_view_shows(channel_view_, ChannelViewGroup::Color, index) ? 255 : 0;
      };
      painter.fillRect(document_rect, QColor(shown(0), shown(1), shown(2)));
      painter.restore();
    }
    if (const auto* mask_image = mask_display_overlay_image(); mask_image != nullptr) {
      painter.drawImage(document_rect, *mask_image, QRectF(mask_image->rect()));
    }
  }
  painter.restore();
}

void CanvasWidget::draw_rotated_tiling_preview(QPainter& painter, QRect exposed_rect) {
  if (document_ == nullptr || document_->width() <= 0 || document_->height() <= 0) {
    return;
  }
  const double width = document_->width();
  const double height = document_->height();
  if (width * zoom_ < 4.0 || height * zoom_ < 4.0) {
    return;
  }
  const QImage& display_image = proof_display_image(zoom_ < 1.0 ? display_image_for_zoom() : render_cache_);
  if (display_image.isNull()) {
    return;
  }
  const auto view = view_transform();
  const auto visible = view.document_bounds_of_widget_rect(QRectF(exposed_rect));
  const auto min_i = static_cast<std::int64_t>(std::floor(visible.left() / width));
  const auto max_i = static_cast<std::int64_t>(std::floor(visible.right() / width));
  const auto min_j = static_cast<std::int64_t>(std::floor(visible.top() / height));
  const auto max_j = static_cast<std::int64_t>(std::floor(visible.bottom() / height));
  if ((max_i - min_i + 1) * (max_j - min_j + 1) > kRotatedTilingTileLimit) {
    return;
  }
  painter.save();
  painter.setRenderHint(QPainter::SmoothPixmapTransform, uses_smooth_rotated_display_scaling(view_zoom()));
  for (auto j = min_j; j <= max_j; ++j) {
    for (auto i = min_i; i <= max_i; ++i) {
      if (i == 0 && j == 0) {
        continue;
      }
      const QRectF tile(static_cast<double>(i) * width, static_cast<double>(j) * height, width, height);
      draw_rotated_checkerboard(painter, tile, exposed_rect);
      painter.save();
      painter.setTransform(view.to_widget(), true);
      painter.drawImage(tile, display_image, QRectF(display_image.rect()));
      painter.restore();
    }
  }
  painter.restore();
}

void CanvasWidget::draw_rotated_grid_overlay(QPainter& painter, QRect exposed_rect) const {
  const bool grid_shown = grid_visible_ && extra_shown(ViewExtra::Grid);
  const bool pixel_grid_shown = pixel_grid_drawn();
  if ((!grid_shown && !pixel_grid_shown) || document_ == nullptr) {
    return;
  }
  const auto view = view_transform();
  const QRectF document_rect(0.0, 0.0, document_->width(), document_->height());
  const auto visible = view.document_bounds_of_widget_rect(QRectF(exposed_rect)).intersected(document_rect);
  if (visible.isEmpty()) {
    return;
  }
  const auto major_x = grid_cycle_pixels(document_->grid_settings().horizontal_cycle_32);
  const auto major_y = grid_cycle_pixels(document_->grid_settings().vertical_cycle_32);
  const auto subdivisions = std::max(1, grid_subdivisions_);

  painter.save();
  painter.setClipPath(closed_polygon_path(view.map_document_rect(document_rect)), Qt::IntersectClip);
  painter.setRenderHint(QPainter::Antialiasing, true);
  const auto draw_lines = [&](double spacing, bool vertical, QColor color, Qt::PenStyle style) {
    if (spacing <= 0.0 || spacing * zoom_ < 3.0) {
      return;
    }
    QPen pen(color, 1.0, style);
    pen.setCosmetic(true);
    painter.setPen(pen);
    const auto start = vertical ? visible.left() : visible.top();
    const auto end = vertical ? visible.right() : visible.bottom();
    const auto first = std::floor(start / spacing) * spacing;
    for (double position = first; position <= end + 0.0001; position += spacing) {
      if (vertical) {
        painter.drawLine(view.map_to_widget(QPointF(position, visible.top())),
                         view.map_to_widget(QPointF(position, visible.bottom())));
      } else {
        painter.drawLine(view.map_to_widget(QPointF(visible.left(), position)),
                         view.map_to_widget(QPointF(visible.right(), position)));
      }
    }
  };
  auto minor_color = grid_color_;
  minor_color.setAlpha(std::clamp(grid_color_.alpha() / 2, 24, 120));
  auto major_color = grid_color_;
  major_color.setAlpha(std::clamp(grid_color_.alpha(), 45, 220));
  if (grid_shown && subdivisions > 1) {
    const auto minor_style = grid_style_ == 0 ? Qt::DotLine : Qt::DashLine;
    draw_lines(major_x / subdivisions, true, minor_color, minor_style);
    draw_lines(major_y / subdivisions, false, minor_color, minor_style);
  }
  const auto major_style = grid_style_ == 0 ? Qt::SolidLine : Qt::DotLine;
  if (grid_shown) {
    draw_lines(major_x, true, major_color, major_style);
    draw_lines(major_y, false, major_color, major_style);
  }
  if (pixel_grid_shown) {
    // View > Pixel Grid under rotation: one thin line per pixel edge in the grid color
    // (the upright view's per-cell tones need its axis-aligned raster).
    auto pixel_color = grid_color_;
    pixel_color.setAlpha(std::clamp(grid_color_.alpha() / 2, 32, 110));
    draw_lines(1.0, true, pixel_color, Qt::SolidLine);
    draw_lines(1.0, false, pixel_color, Qt::SolidLine);
  }
  painter.restore();
}

void CanvasWidget::draw_rotated_document_border(QPainter& painter) const {
  if (document_ == nullptr || document_has_artboards(std::as_const(*document_))) {
    return;
  }
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);
  QPen pen(theme().canvas_document_border, 1.0);
  pen.setCosmetic(true);
  painter.setPen(pen);
  painter.setBrush(Qt::NoBrush);
  painter.drawPolygon(view_transform().map_document_rect(QRectF(0.0, 0.0, document_->width(), document_->height())));
  painter.restore();
}

}  // namespace patchy::ui
