// CanvasWidget's free-transform and warp implementation, split out of
// canvas_widget.cpp: the free-transform session (begin/cancel/finish/commit,
// preview caches, controls state), the transform controls drawing and hit
// testing, the entire warp feature, and the resample_transformed_rgba8 /
// resample_warped_rgba8 resamplers they share. Free transform and warp share
// the pending-warp session state, so both live in this one translation unit.
// Pure function moves from canvas_widget.cpp; behavior must stay identical.

#include "ui/canvas_widget.hpp"

#include "core/document.hpp"
#include "core/floating_selection.hpp"
#include "core/layer.hpp"
#include "core/layer_metadata.hpp"
#include "core/layer_render_utils.hpp"
#include "core/layer_tree.hpp"
#include "core/pixel_buffer.hpp"
#include "core/pixel_depth.hpp"
#include "core/pixel_grid.hpp"
#include "core/pixel_tools.hpp"
#include "core/rect_utils.hpp"
#include "core/resample.hpp"
#include "core/smart_filter.hpp"
#include "core/smart_object.hpp"
#include "core/vector_shape.hpp"
#include "core/warp_mesh.hpp"
#include "core/worker_budget.hpp"
#include "render/layer_knockout.hpp"
#include "render/layer_render_facade.hpp"
#include "ui/background_workers.hpp"
#include "ui/canvas_types.hpp"
#include "ui/canvas_widget_shared.hpp"
#include "ui/edit_conversions.hpp"
#include "ui/image_document_io.hpp"
#include "ui/measurement_units.hpp"
#include "ui/qt_geometry.hpp"
#include "ui/smart_object_render.hpp"
#include "ui/tool_cursors.hpp"

#include <QColor>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPoint>
#include <QPointF>
#include <QPolygonF>
#include <QRect>
#include <QRectF>
#include <QRegion>
#include <QSize>
#include <QSizeF>
#include <QString>
#include <QTimer>
#include <QTransform>
#include <Qt>
#include <QtGlobal>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <future>
#include <limits>
#include <memory>
#include <optional>
#include <ratio>
#include <span>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

// An unlinked vector mask stays put while its layer transforms (the raster mask's rule,
// transformed_linked_raster_mask): run `transform` and put such a mask back.
template <typename Transform>
void transform_keeping_unlinked_vector_mask(Layer& layer, Transform&& transform) {
  std::optional<LayerVectorMask> kept;
  if (const auto* mask = std::as_const(layer).vector_mask(); mask != nullptr && mask->unlinked) {
    kept = *mask;
  }
  transform();
  if (kept.has_value()) {
    layer.set_vector_mask(std::move(*kept));
  }
}

constexpr double kMinimumTransformScalePercent = 0.01;
// A numeric rotation this small still counts as axis-aligned for pixel-grid snapping.
constexpr double kPixelGridSnapAngleTolerance = 0.01;

// Photoshop lands an axis-aligned transform on the pixel grid by rounding each destination
// edge (halves up): 4.75..35.25 becomes 5..35, so a typed X of 3.4 moves the layer by 3 and
// a 152.5% width of 30.5 px comes out 30 wide (PS 27.9 COM captures, September 2026). A
// rotated box cannot sit on the grid and is left alone.
QRectF snap_transform_rect_to_pixel_grid(const QRectF& rect) {
  const auto left = snap_to_pixel_grid(rect.left());
  const auto top = snap_to_pixel_grid(rect.top());
  const auto right = std::max(left + 1.0, snap_to_pixel_grid(rect.right()));
  const auto bottom = std::max(top + 1.0, snap_to_pixel_grid(rect.bottom()));
  return QRectF(QPointF(left, top), QPointF(right, bottom));
}

// Latch thresholds for the drag-time proxy preview, measured on the larger of
// the unclipped transformed-source AABB (what resample_transformed_rgba8
// rasterizes per mouse-move) and the clipped effect-bounds patch rect (what
// the composited preview re-renders per mouse-move). Distinct from the move
// tool's dirty-rect constants: those meter repaint area, these meter recompute
// area. Styled layers recompute effect masks every move (the override pixels
// change), hence the stricter limit, mirroring the move tool's styled split.
constexpr std::int64_t kTransformProxyAreaThreshold = 4'000'000;
constexpr std::int64_t kStyledTransformProxyAreaThreshold = 1'000'000;
// The proxy itself stays bounded so the latched blit is cheap at any zoom.
constexpr std::int64_t kTransformProxyMaxPixels = 4'000'000;
// Row fan-out gates for the transform resamplers. They run per mouse-move on
// the composited preview path, and the bicubic kernel costs about 60 ns per
// output pixel on one thread, so the old 1 Mpx / 128-row gate left every
// sub-megapixel layer (most layers of a 1080p document) at 50+ ms per frame
// (GitHub #13). A worker launch is tens of microseconds; 64 K pixels of
// bicubic is milliseconds. Byte-identical at any split (pure per-pixel map).
constexpr std::int64_t kResampleParallelMinArea = 65'536;
constexpr int kResampleRowsPerWorker = 32;

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

// Whether an enabled raster mask can change what the layer renders. A
// reveal-all mask (white everywhere, white default: what Add Layer Mask
// creates) cannot, so it must not push the transform preview off the cheap
// rotated blit onto the composited path. Runs at session start and on Layer
// Style refreshes, never per frame; the scan exits at the first non-white byte.
bool raster_mask_affects_render(const LayerMask& mask) {
  if (mask.disabled) {
    return false;
  }
  if (mask.default_color != 255 || mask.pixels.format() != PixelFormat::gray8()) {
    return true;
  }
  for (int y = 0; y < mask.pixels.height(); ++y) {
    const auto row = mask.pixels.row(y);
    if (std::any_of(row.begin(), row.end(), [](std::uint8_t value) { return value != 255; })) {
      return true;
    }
  }
  return false;
}

bool layer_needs_composited_transform_preview(const Layer& layer) {
  return std::abs(layer.opacity() - 1.0F) > 0.001F || std::abs(layer.fill_opacity() - 1.0F) > 0.001F ||
         layer.blend_mode() != BlendMode::Normal ||
         (layer.mask().has_value() && raster_mask_affects_render(*layer.mask())) ||
         patchy::layer_has_enabled_vector_mask(layer) ||
         (layer.layer_style().effects_visible && !layer.layer_style().empty());
}

// Composite order is storage order bottom-up, depth-first through groups; `found`
// flips when the walk passes the target, and any visible non-group layer visited
// after that draws over it.
bool visible_content_composites_above(const std::vector<Layer>& layers, LayerId id, bool& found) {
  for (const auto& layer : layers) {
    if (layer.id() == id) {
      found = true;
      continue;
    }
    if (!layer.visible()) {
      continue;
    }
    if (layer.kind() == LayerKind::Group) {
      if (visible_content_composites_above(layer.children(), id, found)) {
        return true;
      }
      continue;
    }
    if (found) {
      return true;
    }
  }
  return false;
}

// GitHub issue 72: the plain source blit paints the transformed layer over a base
// that only hid the layer, so everything composited above it sat underneath the
// preview for the whole drag (a shape under other layers looked as if it had
// jumped to the top until commit). Any visible content above the target sends
// the session down the stacked-patch path, which renders the real order.
bool layer_has_visible_content_above(const Document& document, LayerId id) {
  bool found = false;
  return visible_content_composites_above(document.layers(), id, found);
}

bool transform_preview_needs_compositing(const Document& document, const Layer& layer) {
  // A layer on an artboard clips to it (docs/artboards.md): the raw blit has no clip.
  return layer_needs_composited_transform_preview(layer) || layer_has_visible_content_above(document, layer.id()) ||
         artboard_owning_layer(document, layer.id()) != nullptr;
}

// The sibling list holding `id`, searched depth-first; nullptr when absent.
const std::vector<Layer>* sibling_list_holding(const std::vector<Layer>& layers, LayerId id) {
  for (const auto& layer : layers) {
    if (layer.id() == id) {
      return &layers;
    }
    if (layer.kind() == LayerKind::Group) {
      if (const auto* found = sibling_list_holding(layer.children(), id); found != nullptr) {
        return found;
      }
    }
  }
  return nullptr;
}

bool layer_has_rendered_advanced_blending(const Layer& layer) {
  return (layer.blend_if_payload_status() == BlendIfPayloadStatus::Supported &&
          !blend_if_is_identity(layer.blend_if())) ||
         (layer.channel_restriction_supported() && layer.restricted_channels() != 0U) ||
         render_detail::layer_knockout_mode(layer) != 0U;
}

// The clipped members of a clipping base: the consecutive clipped non-group
// siblings right above it (the compositor's run rule, composite_sibling_layers).
std::vector<const Layer*> clip_run_members(const Document& document, const Layer& base) {
  std::vector<const Layer*> members;
  const auto* siblings = sibling_list_holding(document.layers(), base.id());
  if (siblings == nullptr || base.kind() != LayerKind::Pixel || base.clipped()) {
    return members;
  }
  auto it = std::find_if(siblings->begin(), siblings->end(),
                         [&](const Layer& layer) { return layer.id() == base.id(); });
  for (++it; it != siblings->end() && it->clipped() && it->kind() != LayerKind::Group; ++it) {
    members.push_back(&*it);
  }
  return members;
}

// GitHub #398: a clipping base whose members only paint over it (Normal
// members, no adjustments) and with nothing visible above its run previews a
// drag exactly as the compositor merges the run, up to resampling: the base at
// its opacity times Fill, the members' own composite drawn source-atop (they
// blend against the base color at full strength and take its coverage), then
// the ensemble over a base cache that already hides the whole run. Anything
// the atop blit cannot carry (a styled, masked, blended or Blend If base,
// adjustment or non-Normal members, content above, an ancestor that is not a
// plain pass-through or full-strength Normal folder, artboards, non-8-bit-RGB
// documents) keeps the composited patches.
bool transform_clip_group_preview_possible(const Document& document, const Layer& layer) {
  if (document.palette_editing() || document.format().color_mode != ColorMode::RGB ||
      document.format().bit_depth != BitDepth::UInt8) {
    return false;
  }
  if (layer.kind() != LayerKind::Pixel || layer.clipped() || layer.blend_mode() != BlendMode::Normal ||
      (layer.layer_style().effects_visible && !layer.layer_style().empty()) ||
      (layer.mask().has_value() && raster_mask_affects_render(*layer.mask())) ||
      patchy::layer_has_enabled_vector_mask(layer) || layer_has_rendered_advanced_blending(layer) ||
      artboard_owning_layer(document, layer.id()) != nullptr) {
    return false;
  }
  const auto members = clip_run_members(document, layer);
  bool any_visible = false;
  for (const auto* member : members) {
    if (!member->visible()) {
      continue;
    }
    any_visible = true;
    if (member->kind() == LayerKind::Adjustment || member->blend_mode() != BlendMode::Normal ||
        layer_has_rendered_advanced_blending(*member)) {
      return false;
    }
  }
  if (!any_visible || layer_has_visible_content_above(document, members.back()->id())) {
    return false;
  }
  std::vector<LayerId> ancestors;
  if (!collect_layer_ancestor_groups(document.layers(), layer.id(), ancestors)) {
    return false;
  }
  for (const auto id : ancestors) {
    const auto* group = document.find_layer(id);
    if (group == nullptr ||
        (group->blend_mode() != BlendMode::Normal && group->blend_mode() != BlendMode::PassThrough) ||
        group->opacity() < 1.0F || group->fill_opacity() < 1.0F ||
        (group->layer_style().effects_visible && !group->layer_style().empty()) ||
        (group->mask().has_value() && raster_mask_affects_render(*group->mask())) ||
        patchy::layer_has_enabled_vector_mask(*group) || layer_has_rendered_advanced_blending(*group)) {
      return false;
    }
  }
  return true;
}

// Smallest sub-rect of a gray8 mask buffer holding every pixel that differs
// from `default_color`; empty when the whole buffer reads as the default.
QRect non_default_mask_local_rect(const PixelBuffer& pixels, std::uint8_t default_color) {
  if (pixels.format().bit_depth != BitDepth::UInt8) {
    // 16/32-bit masks: compare the samples on the deep scale.
    int deep_left = pixels.width();
    int deep_right = -1;
    int deep_top = -1;
    int deep_bottom = -1;
    std::vector<float> row(static_cast<std::size_t>(pixels.width()));
    const auto background = static_cast<float>(default_color);
    for (int y = 0; y < pixels.height(); ++y) {
      load_coverage_row(pixels, y, 0, pixels.width(), row);
      for (int x = 0; x < pixels.width(); ++x) {
        if (row[static_cast<std::size_t>(x)] != background) {
          deep_left = std::min(deep_left, x);
          deep_right = std::max(deep_right, x);
          deep_top = deep_top < 0 ? y : deep_top;
          deep_bottom = y;
        }
      }
    }
    return deep_right < deep_left ? QRect() : QRect(QPoint(deep_left, deep_top), QPoint(deep_right, deep_bottom));
  }
  int left = pixels.width();
  int right = -1;
  int top = -1;
  int bottom = -1;
  for (int y = 0; y < pixels.height(); ++y) {
    const auto row = pixels.row(y);
    int row_left = 0;
    while (row_left < pixels.width() && row[static_cast<std::size_t>(row_left)] == default_color) {
      ++row_left;
    }
    if (row_left == pixels.width()) {
      continue;
    }
    int row_right = pixels.width() - 1;
    while (row[static_cast<std::size_t>(row_right)] == default_color) {
      --row_right;
    }
    left = std::min(left, row_left);
    right = std::max(right, row_right);
    if (top < 0) {
      top = y;
    }
    bottom = y;
  }
  return right < left ? QRect() : QRect(QPoint(left, top), QPoint(right, bottom));
}

// The linked raster mask's non-default crop, or nullopt when the layer has no
// linked gray8 raster mask to take along. Only the non-default part of the
// source can change a transformed result: everything outside it reads
// default_color before and after, exactly what an out-of-source sample
// returns. A freshly added reveal-all mask (uniformly its default) therefore
// transforms to itself (empty `pixels`), and a canvas-sized mask with a small
// painted area costs only that area per preview frame. This scans the whole
// stored buffer once; the preview paths cache the result per content revision
// (CanvasWidget::transform_linked_mask_source) so no drag frame rescans it.
std::optional<TransformLinkedMaskSource> linked_mask_source(const Layer& layer) {
  const auto& stored_mask = layer.mask();
  if (!stored_mask.has_value() || stored_mask->pixels.empty() || stored_mask->pixels.format().channels != 1U ||
      !layer_mask_linked(layer)) {
    return std::nullopt;
  }
  TransformLinkedMaskSource source;
  source.content_revision = layer.content_revision();
  source.default_color = stored_mask->default_color;
  const auto keep = non_default_mask_local_rect(stored_mask->pixels, stored_mask->default_color);
  if (keep.isEmpty()) {
    return source;
  }
  source.bounds =
      Rect{stored_mask->bounds.x + keep.x(), stored_mask->bounds.y + keep.y(), keep.width(), keep.height()};
  if (keep.width() == stored_mask->pixels.width() && keep.height() == stored_mask->pixels.height()) {
    source.pixels = stored_mask->pixels;  // copy-on-write share, no copy
    return source;
  }
  PixelBuffer cropped(keep.width(), keep.height(), stored_mask->pixels.format());
  const auto sample_bytes = bytes_per_pixel(stored_mask->pixels.format());
  for (int y = 0; y < keep.height(); ++y) {
    const auto source_row = stored_mask->pixels.row(keep.y() + y);
    std::copy_n(source_row.begin() + static_cast<std::ptrdiff_t>(keep.x() * sample_bytes),
                static_cast<std::size_t>(keep.width()) * sample_bytes, cropped.row(y).begin());
  }
  source.pixels = std::move(cropped);
  return source;
}

// The layer's linked raster mask mapped through a Free Transform delta, or
// nullopt when there is nothing to take along (no linked raster mask, or one
// that is uniformly its default and so transforms to itself). Out-of-source
// samples read default_color, so a rotated reveal-all mask keeps its surround.
// With `trim` the result is cut back to its non-default extent: the resampled
// AABB of a rotated mask is mostly default-colored corners, and leaving them in
// would roughly double the buffer with every further rotation. Commits trim;
// preview frames skip that scan and copy because the untrimmed mask renders
// identically (outside its non-default extent it holds default_color, which is
// also what the compositor reads beyond the mask bounds).
std::optional<LayerMask> transformed_linked_raster_mask(const Layer& layer, const TransformLinkedMaskSource& source,
                                                        const QTransform& delta,
                                                        CanvasWidget::TransformInterpolation interpolation,
                                                        bool trim) {
  const auto& stored_mask = layer.mask();
  if (!stored_mask.has_value() || source.pixels.empty()) {
    return std::nullopt;
  }
  // Left-to-right composition: crop-local -> document, then the delta.
  auto resampled = resample_transformed_gray8(source.pixels, source.default_color,
                                              QTransform::fromTranslate(source.bounds.x, source.bounds.y) * delta,
                                              interpolation);
  auto updated = *stored_mask;
  if (!trim) {
    updated.pixels = std::move(resampled.pixels);
    updated.bounds = resampled.bounds;
    return updated;
  }
  const auto keep = non_default_mask_local_rect(resampled.pixels, source.default_color);
  if (keep.isEmpty()) {
    // Everything reads as the default: one default pixel at the old origin
    // keeps the mask (and its default color) without the dead buffer.
    PixelBuffer single(1, 1, PixelFormat::gray8());
    single.clear(source.default_color);
    updated.pixels = std::move(single);
    updated.bounds = Rect{resampled.bounds.x, resampled.bounds.y, 1, 1};
    return updated;
  }
  if (keep.width() == resampled.pixels.width() && keep.height() == resampled.pixels.height()) {
    updated.pixels = std::move(resampled.pixels);
    updated.bounds = resampled.bounds;
    return updated;
  }
  PixelBuffer trimmed(keep.width(), keep.height(), PixelFormat::gray8());
  for (int y = 0; y < keep.height(); ++y) {
    const auto source_row = std::as_const(resampled.pixels).row(keep.y() + y);
    std::copy_n(source_row.begin() + keep.x(), keep.width(), trimmed.row(y).begin());
  }
  updated.pixels = std::move(trimmed);
  updated.bounds = Rect{resampled.bounds.x + keep.x(), resampled.bounds.y + keep.y(), keep.width(), keep.height()};
  return updated;
}

// Commit-path form: one fresh crop, trimmed result. Bytes are pinned by the
// transform-commit suites; the preview paths share the resample above.
std::optional<LayerMask> transformed_linked_raster_mask(const Layer& layer, const QTransform& delta,
                                                        CanvasWidget::TransformInterpolation interpolation) {
  const auto source = linked_mask_source(layer);
  if (!source.has_value()) {
    return std::nullopt;
  }
  return transformed_linked_raster_mask(layer, *source, delta, interpolation, true);
}

// Proxy source for a masked layer. A linked raster mask rides the transform
// rigidly, so multiplying the source alpha by the layer's mask coverage over
// the source's document rect (once per latch, O(source pixels)) lets the
// latched proxy show the masked silhouette instead of the whole layer; a
// vector mask folds in through the same coverage plane, since the commit maps
// its path along too. An unlinked raster mask stays put, so the layer keeps the
// plain approximation. The accurate patches return at release either way.
QImage source_image_with_mask_coverage(const QImage& source, const Layer& layer, QRectF original_rect) {
  if (source.isNull()) {
    return source;
  }
  const auto& mask = layer.mask();
  const bool raster_enabled = mask.has_value() && !mask->disabled;
  if (raster_enabled && !layer_mask_linked(layer)) {
    return source;
  }
  const bool raster_renders = raster_enabled && raster_mask_affects_render(*mask);
  if (!raster_renders && !patchy::layer_has_enabled_vector_mask(layer)) {
    return source;
  }
  const Rect draw_rect{static_cast<std::int32_t>(std::lround(original_rect.x())),
                       static_cast<std::int32_t>(std::lround(original_rect.y())), source.width(), source.height()};
  const auto plane = patchy::render_detail::build_mask_coverage_plane(layer, draw_rect, std::nullopt);
  QImage masked = source.convertToFormat(QImage::Format_RGBA8888);
  for (int y = 0; y < masked.height(); ++y) {
    auto* row = masked.scanLine(y);
    const auto* coverage = plane.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(masked.width());
    for (int x = 0; x < masked.width(); ++x) {
      auto& alpha = row[static_cast<std::size_t>(x) * 4U + 3U];
      alpha = static_cast<std::uint8_t>(
          std::lround(static_cast<float>(alpha) * std::clamp(coverage[x], 0.0F, 1.0F)));
    }
  }
  return masked;
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

// Free Transform's box on a shape layer hugs the ink (fill plus any stroke
// reaching past the path), but the commit transforms only the path: the stroke
// keeps its width, as in Photoshop. Mapping the path through the box delta
// therefore landed the redrawn ink off the dragged box by the stroke overhang
// times the scale change, so even the fixed corner crept and the error grew
// with every transform. This maps the path's own box onto the dragged box
// inset by the same per-side overhang instead, so the redrawn ink fills the
// dragged box. The inset happens in the box's unrotated frame and the result
// turns about the box center like free_transform_delta. Degenerate boxes (a
// straight line, an overhang wider than the new box) keep the plain delta.
QTransform shape_free_transform_delta(QRectF original_rect, QRectF current_rect, double angle_degrees,
                                      double scale_x_sign, double scale_y_sign, const VectorPathBounds& path) {
  const QRectF path_rect(path.left, path.top, path.right - path.left, path.bottom - path.top);
  auto left = path_rect.left() - original_rect.left();
  auto top = path_rect.top() - original_rect.top();
  auto right = original_rect.right() - path_rect.right();
  auto bottom = original_rect.bottom() - path_rect.bottom();
  // A mirrored axis carries each side's overhang to the opposite edge.
  if (scale_x_sign < 0.0) {
    std::swap(left, right);
  }
  if (scale_y_sign < 0.0) {
    std::swap(top, bottom);
  }
  const QRectF target(current_rect.left() + left, current_rect.top() + top, current_rect.width() - left - right,
                      current_rect.height() - top - bottom);
  if (path_rect.width() < 1.0 || path_rect.height() < 1.0 || target.width() < 1.0 || target.height() < 1.0) {
    return free_transform_delta(original_rect, current_rect, angle_degrees, scale_x_sign, scale_y_sign);
  }
  QTransform transform;
  transform.translate(current_rect.center().x(), current_rect.center().y());
  transform.rotate(angle_degrees);
  transform.translate(target.center().x() - current_rect.center().x(), target.center().y() - current_rect.center().y());
  transform.scale(scale_x_sign * target.width() / path_rect.width(), scale_y_sign * target.height() / path_rect.height());
  transform.translate(-path_rect.center().x(), -path_rect.center().y());
  return transform;
}

// A Distort/Perspective session delta (QTransform's m13/m23 are the projective row).
bool transform_has_perspective(const QTransform& transform) {
  return std::abs(transform.m13()) > 1e-12 || std::abs(transform.m23()) > 1e-12;
}

// QTransform is row-vector; the core homographies are column-vector row-major.
std::array<double, 9> homography_from_qtransform(const QTransform& transform) {
  return {transform.m11(), transform.m21(), transform.m31(), transform.m12(), transform.m22(),
          transform.m32(), transform.m13(), transform.m23(), transform.m33()};
}

bool transform_delta_is_identity(const QTransform& transform) {
  return std::abs(transform.m11() - 1.0) < 1e-9 && std::abs(transform.m22() - 1.0) < 1e-9 &&
         std::abs(transform.m12()) < 1e-9 && std::abs(transform.m21()) < 1e-9 &&
         std::abs(transform.m31()) < 1e-9 && std::abs(transform.m32()) < 1e-9 &&
         std::abs(transform.m13()) < 1e-12 && std::abs(transform.m23()) < 1e-12;
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

// A resample's source, read once: the samplers below run per tap (16 taps a
// bicubic pixel), and QImage's accessors are out-of-line calls (GitHub #298).
struct SampleSource {
  explicit SampleSource(const QImage& image)
      : bits(image.constBits()),
        stride(static_cast<std::ptrdiff_t>(image.bytesPerLine())),
        width(image.width()),
        height(image.height()),
        float_pixels(image.format() == QImage::Format_RGBA32FPx4) {}
  [[nodiscard]] const uchar* row(int y) const noexcept { return bits + static_cast<std::ptrdiff_t>(y) * stride; }

  const uchar* bits;
  std::ptrdiff_t stride;
  int width;
  int height;
  bool float_pixels;
};

PremultipliedSample premultiplied_pixel(const SampleSource& image, int x, int y) {
  if (x < 0 || y < 0 || x >= image.width || y >= image.height) {
    return {};
  }
  if (image.float_pixels) {
    // 16/32-bit layers (docs/high-bit-depth.md): the same premultiplied sample on the
    // 0..255 scale, from straight float values (linear ones may exceed 1).
    const auto* pixel = reinterpret_cast<const float*>(image.row(y)) + static_cast<std::size_t>(x) * 4U;
    const auto alpha = static_cast<double>(std::clamp(pixel[3], 0.0F, 1.0F)) * 255.0;
    return PremultipliedSample{static_cast<double>(pixel[0]) * alpha, static_cast<double>(pixel[1]) * alpha,
                               static_cast<double>(pixel[2]) * alpha, alpha};
  }
  const auto* pixel = image.row(y) + x * 4;
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

PremultipliedSample sample_nearest(const SampleSource& image, QPointF source_point) {
  if (source_point.x() < 0.0 || source_point.y() < 0.0 || source_point.x() >= image.width ||
      source_point.y() >= image.height) {
    return {};
  }
  return premultiplied_pixel(image, static_cast<int>(std::floor(source_point.x())),
                             static_cast<int>(std::floor(source_point.y())));
}

PremultipliedSample sample_bilinear(const SampleSource& image, QPointF source_point) {
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

// The cubic tap weight per method, all from core/resample.hpp so Free Transform and
// Image Size agree kernel for kernel: Bicubic keeps patchy::cubic_weight (Catmull-Rom
// spelled literally, the pinned expression), Smoother and Sharper take their (B, C)
// from the Image Size kernel table. Automatic is resolved before sampling.
double cubic_tap_weight(CanvasWidget::TransformInterpolation interpolation, double distance) {
  switch (interpolation) {
    case CanvasWidget::TransformInterpolation::BicubicSmoother: {
      const auto kernel = resample_kernel(ResampleMethod::BicubicSmoother);
      return cubic_bc_weight(distance, kernel.b, kernel.c);
    }
    case CanvasWidget::TransformInterpolation::BicubicSharper: {
      const auto kernel = resample_kernel(ResampleMethod::BicubicSharper);
      return cubic_bc_weight(distance, kernel.b, kernel.c);
    }
    default:
      return cubic_weight(distance);
  }
}

// Automatic follows Image Size's rule by output area: Sharper for a reduction, Smoother
// for an enlargement, Bicubic at 1:1 (`area_scale` is output area over source area).
CanvasWidget::TransformInterpolation resolve_automatic_interpolation(
    CanvasWidget::TransformInterpolation interpolation, double area_scale) {
  if (interpolation != CanvasWidget::TransformInterpolation::Automatic) {
    return interpolation;
  }
  constexpr double kEqualAreaTolerance = 1e-9;
  if (area_scale < 1.0 - kEqualAreaTolerance) {
    return CanvasWidget::TransformInterpolation::BicubicSharper;
  }
  if (area_scale > 1.0 + kEqualAreaTolerance) {
    return CanvasWidget::TransformInterpolation::BicubicSmoother;
  }
  return CanvasWidget::TransformInterpolation::Bicubic;
}

PremultipliedSample sample_bicubic(const SampleSource& image, QPointF source_point,
                                   CanvasWidget::TransformInterpolation interpolation) {
  const auto x = source_point.x() - 0.5;
  const auto y = source_point.y() - 0.5;
  const auto base_x = static_cast<int>(std::floor(x));
  const auto base_y = static_cast<int>(std::floor(y));
  PremultipliedSample total;
  for (int yy = -1; yy <= 2; ++yy) {
    const auto wy = cubic_tap_weight(interpolation, y - static_cast<double>(base_y + yy));
    for (int xx = -1; xx <= 2; ++xx) {
      const auto wx = cubic_tap_weight(interpolation, x - static_cast<double>(base_x + xx));
      total = add_weighted(total, premultiplied_pixel(image, base_x + xx, base_y + yy), wx * wy);
    }
  }
  return total;
}

std::uint8_t clamp_sample_channel(double value) {
  return static_cast<std::uint8_t>(std::clamp(std::lround(value), 0L, 255L));
}

// The straight deep format a 16/32-bit layer image comes back in (qimage_from_pixel_buffer
// makes Format_RGBA64 or Format_RGBA32FPx4); nullopt for 8-bit images.
std::optional<QImage::Format> deep_qimage_format(QImage::Format format) {
  switch (format) {
    case QImage::Format_RGBA64:
    case QImage::Format_RGBA64_Premultiplied:
    case QImage::Format_RGBX64:
      return QImage::Format_RGBA64;
    case QImage::Format_RGBA32FPx4:
    case QImage::Format_RGBA32FPx4_Premultiplied:
    case QImage::Format_RGBX32FPx4:
      return QImage::Format_RGBA32FPx4;
    default:
      return std::nullopt;
  }
}

// The x extent of a warp cell's corners' convex hull within the horizontal strip
// [strip_top, strip_bottom], or nullopt when the hull misses the strip. The hull of
// four points is the union of the six segments between them, so the extent is the
// extent of those segments clipped to the strip.
std::optional<std::array<double, 2>> convex_cell_span_on_row(const std::array<QPointF, 4>& corners,
                                                             double strip_top, double strip_bottom) {
  double low = std::numeric_limits<double>::infinity();
  double high = -std::numeric_limits<double>::infinity();
  for (std::size_t a = 0; a < corners.size(); ++a) {
    for (std::size_t b = a + 1; b < corners.size(); ++b) {
      const auto& p = corners[a];
      const auto& q = corners[b];
      const double y_min = std::min(p.y(), q.y());
      const double y_max = std::max(p.y(), q.y());
      if (y_max < strip_top || y_min > strip_bottom) {
        continue;
      }
      const double dy = q.y() - p.y();
      if (std::abs(dy) < 1e-12) {
        low = std::min({low, p.x(), q.x()});
        high = std::max({high, p.x(), q.x()});
        continue;
      }
      for (const double y : {std::clamp(strip_top, y_min, y_max), std::clamp(strip_bottom, y_min, y_max)}) {
        const double x = p.x() + (q.x() - p.x()) * ((y - p.y()) / dy);
        low = std::min(low, x);
        high = std::max(high, x);
      }
    }
  }
  if (!(low <= high)) {
    return std::nullopt;
  }
  return std::array<double, 2>{low, high};
}

}  // namespace

// Declared in canvas_widget.hpp: shared with the smart-object preview renderer, so it
// lives outside the anonymous namespace (the sampling helpers above stay file-local).
TransformedImage resample_transformed_rgba8(const QImage& source, const QTransform& source_to_document,
                                            CanvasWidget::TransformInterpolation interpolation) {
  interpolation = resolve_automatic_interpolation(interpolation, std::abs(source_to_document.determinant()));
  // A 16/32-bit layer's image (qimage_from_pixel_buffer) resamples in float and comes
  // back in its own format, so pixels_from_image_native restores the layer's depth.
  const auto deep_format = deep_qimage_format(source.format());
  const auto converted = source.convertToFormat(deep_format.has_value() ? QImage::Format_RGBA32FPx4
                                                                        : QImage::Format_RGBA8888);
  const auto mapped = source_to_document.mapRect(QRectF(0.0, 0.0, converted.width(), converted.height()));
  const auto left = static_cast<int>(std::floor(mapped.left()));
  const auto top = static_cast<int>(std::floor(mapped.top()));
  const auto right = static_cast<int>(std::ceil(mapped.right()));
  const auto bottom = static_cast<int>(std::ceil(mapped.bottom()));
  QImage transformed(std::max(1, right - left), std::max(1, bottom - top),
                     deep_format.has_value() ? QImage::Format_RGBA32FPx4 : QImage::Format_RGBA8888);
  transformed.fill(Qt::transparent);
  const bool deep = deep_format.has_value();

  bool invertible = false;
  const auto document_to_source = source_to_document.inverted(&invertible);
  if (!invertible) {
    const auto bounds = Rect{left, top, transformed.width(), transformed.height()};
    return TransformedImage{std::move(transformed), bounds};
  }

  // Every output pixel is a pure function of (source, inverse transform, x, y),
  // so splitting the destination rows across workers produces byte-identical
  // results to the sequential walk; commit and preview share this function and
  // both stay pinned. The single detach up front matters: concurrent
  // scanLine() calls from workers would race on QImage's copy-on-write.
  auto* transformed_bits = transformed.bits();
  const auto transformed_stride = static_cast<std::size_t>(transformed.bytesPerLine());
  const SampleSource sampled(converted);
  const auto resample_rows = [&sampled, &document_to_source, interpolation, transformed_bits, transformed_stride,
                              left, top, deep, width = transformed.width()](int row_begin, int row_end) {
    for (int y = row_begin; y < row_end; ++y) {
      auto* row = transformed_bits + static_cast<std::size_t>(y) * transformed_stride;
      for (int x = 0; x < width; ++x) {
        const auto source_point = document_to_source.map(QPointF(static_cast<double>(left + x) + 0.5,
                                                                 static_cast<double>(top + y) + 0.5));
        PremultipliedSample sample;
        switch (interpolation) {
          case CanvasWidget::TransformInterpolation::NearestNeighbor:
            sample = sample_nearest(sampled, source_point);
            break;
          case CanvasWidget::TransformInterpolation::Bilinear:
            sample = sample_bilinear(sampled, source_point);
            break;
          case CanvasWidget::TransformInterpolation::Bicubic:
          case CanvasWidget::TransformInterpolation::BicubicSmoother:
          case CanvasWidget::TransformInterpolation::BicubicSharper:
          case CanvasWidget::TransformInterpolation::Automatic:  // resolved above
            sample = sample_bicubic(sampled, source_point, interpolation);
            break;
        }

        if (deep) {
          auto* pixel = reinterpret_cast<float*>(row) + static_cast<std::size_t>(x) * 4U;
          const auto alpha = std::clamp(sample.a, 0.0, 255.0);
          pixel[3] = static_cast<float>(alpha / 255.0);
          // Un-premultiplied; overshoot below 0 clamps as the 8-bit path does, values
          // above full scale stay (32-bit linear light is unbounded).
          const auto unpremultiply = [alpha](double value) {
            return alpha <= 0.0 ? 0.0F : static_cast<float>(std::max(0.0, value / alpha));
          };
          pixel[0] = unpremultiply(sample.r);
          pixel[1] = unpremultiply(sample.g);
          pixel[2] = unpremultiply(sample.b);
          continue;
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
  };

  const auto area = static_cast<std::int64_t>(transformed.width()) * transformed.height();
  const auto hardware_threads = patchy::hardware_worker_threads();
  // max_blocking_fanout_workers: this thread blocks on the row futures, so on
  // the wasm main thread the fan-out must fit the idle pthread pool.
  const auto workers = patchy::max_blocking_fanout_workers(
      std::clamp(std::min(transformed.height() / kResampleRowsPerWorker, hardware_threads), 1, 16));
  if (area >= kResampleParallelMinArea && workers >= 2 &&
      !qEnvironmentVariableIsSet("PATCHY_RENDER_SINGLE_THREADED")) {
    std::vector<std::future<void>> strips;
    strips.reserve(static_cast<std::size_t>(workers));
    const auto rows_per_strip = (transformed.height() + workers - 1) / workers;
    for (int start = 0; start < transformed.height(); start += rows_per_strip) {
      const auto end = std::min(start + rows_per_strip, transformed.height());
      strips.push_back(std::async(std::launch::async, resample_rows, start, end));
    }
    for (auto& strip : strips) {
      strip.get();
    }
  } else {
    resample_rows(0, transformed.height());
  }

  const auto bounds = Rect{left, top, transformed.width(), transformed.height()};
  if (deep_format.has_value() && *deep_format != QImage::Format_RGBA32FPx4) {
    transformed = transformed.convertToFormat(*deep_format);
  }
  return TransformedImage{std::move(transformed), bounds};
}

TransformedImage resample_warped_rgba8(const QImage& source, const WarpSurfaceGrid& grid,
                                       CanvasWidget::TransformInterpolation interpolation,
                                       const std::optional<Rect>& clip) {
  // 16/32-bit layer images warp in float and come back in their own format, as in
  // resample_transformed_rgba8.
  const auto deep_format = deep_qimage_format(source.format());
  const auto converted = source.convertToFormat(deep_format.has_value() ? QImage::Format_RGBA32FPx4
                                                                        : QImage::Format_RGBA8888);
  const auto [min_x_it, max_x_it] = std::minmax_element(grid.doc_xs.begin(), grid.doc_xs.end());
  const auto [min_y_it, max_y_it] = std::minmax_element(grid.doc_ys.begin(), grid.doc_ys.end());
  if (min_x_it == grid.doc_xs.end() || min_y_it == grid.doc_ys.end()) {
    return TransformedImage{QImage(), Rect{}};
  }
  auto left = static_cast<int>(std::floor(*min_x_it)) - 1;
  auto top = static_cast<int>(std::floor(*min_y_it)) - 1;
  auto right = static_cast<int>(std::ceil(*max_x_it)) + 1;
  auto bottom = static_cast<int>(std::ceil(*max_y_it)) + 1;
  // A warp has no single scale; its unclipped output extent over the source extent
  // stands in (taken before the clip, so a clipped preview resolves Automatic the
  // way the full render does).
  const auto source_area = static_cast<double>(std::max(1, converted.width())) * std::max(1, converted.height());
  interpolation = resolve_automatic_interpolation(
      interpolation, static_cast<double>(std::max(1, right - left)) * std::max(1, bottom - top) / source_area);
  if (clip.has_value()) {
    // Output pixels outside the clip are skipped, never computed: each pixel is a
    // pure function of its own position, so the kept ones match the full render.
    left = std::max(left, clip->x);
    top = std::max(top, clip->y);
    right = std::min(right, clip->x + clip->width);
    bottom = std::min(bottom, clip->y + clip->height);
    if (right <= left || bottom <= top) {
      return TransformedImage{QImage(), Rect{}};
    }
  }
  QImage transformed(std::max(1, right - left), std::max(1, bottom - top),
                     deep_format.has_value() ? QImage::Format_RGBA32FPx4 : QImage::Format_RGBA8888);
  transformed.fill(Qt::transparent);
  std::vector<std::uint8_t> covered(static_cast<std::size_t>(transformed.width()) * transformed.height(), 0);

  const SampleSource sampled(converted);
  const auto sample_at = [&sampled, interpolation](QPointF source_point) {
    switch (interpolation) {
      case CanvasWidget::TransformInterpolation::NearestNeighbor:
        return sample_nearest(sampled, source_point);
      case CanvasWidget::TransformInterpolation::Bilinear:
        return sample_bilinear(sampled, source_point);
      default:
        return sample_bicubic(sampled, source_point, interpolation);
    }
  };

  // Rows split across workers. Each worker walks every cell in the same row-major
  // order but writes only its own band of output rows, so every pixel sees the same
  // sequence of cells as the sequential walk and first-writer-wins on folds resolves
  // identically: the result is byte-identical for any worker count. The single
  // detach up front matters: concurrent scanLine() calls would race on QImage's
  // copy-on-write bookkeeping.
  auto* transformed_bits = transformed.bits();
  const auto transformed_stride = static_cast<std::size_t>(transformed.bytesPerLine());
  const auto transformed_width = transformed.width();
  std::atomic<std::uint64_t> cell_pixel_tests{0};
  const auto resample_band = [&](int band_top, int band_bottom) {
    std::uint64_t band_tests = 0;
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
        const int py_start = std::max(band_top, static_cast<int>(std::floor(cell_min_y)));
        const int py_end = std::min(band_bottom, static_cast<int>(std::ceil(cell_max_y)) + 1);
        const std::array<QPointF, 4> corners{QPointF(grid.doc_xs[i00], grid.doc_ys[i00]),
                                             QPointF(grid.doc_xs[i10], grid.doc_ys[i10]),
                                             QPointF(grid.doc_xs[i11], grid.doc_ys[i11]),
                                             QPointF(grid.doc_xs[i01], grid.doc_ys[i01])};
        for (int py = py_start; py < py_end; ++py) {
          // A bilinear cell lies inside its corners' convex hull, so only the hull's
          // span on this pixel row can invert. A handle dragged far stretches cells
          // into long slivers whose bounding boxes cover most of the output; walking
          // the whole box tested every cell against most pixels (GitHub issue 299).
          // The span keeps a pixel of slack each side, so the result is unchanged.
          const auto span = convex_cell_span_on_row(corners, static_cast<double>(py), static_cast<double>(py + 1));
          if (!span.has_value()) {
            continue;
          }
          const int row_start = std::max(px_start, static_cast<int>(std::floor((*span)[0])) - 1);
          const int row_end = std::min(px_end, static_cast<int>(std::ceil((*span)[1])) + 1);
          auto* row = transformed_bits + static_cast<std::size_t>(py - top) * transformed_stride;
          auto* coverage_row = covered.data() + static_cast<std::size_t>(py - top) * transformed_width;
          band_tests += static_cast<std::uint64_t>(std::max(0, row_end - row_start));
          for (int px = row_start; px < row_end; ++px) {
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
            if (deep_format.has_value()) {
              auto* deep_pixel = reinterpret_cast<float*>(row) + static_cast<std::size_t>(px - left) * 4U;
              const auto deep_alpha = std::clamp(sample.a, 0.0, 255.0);
              const auto unpremultiply = [deep_alpha](double value) {
                return deep_alpha <= 0.0 ? 0.0F : static_cast<float>(std::max(0.0, value / deep_alpha));
              };
              deep_pixel[0] = unpremultiply(sample.r);
              deep_pixel[1] = unpremultiply(sample.g);
              deep_pixel[2] = unpremultiply(sample.b);
              deep_pixel[3] = static_cast<float>(deep_alpha / 255.0);
              coverage_row[px - left] = 1;
              continue;
            }
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
    cell_pixel_tests.fetch_add(band_tests);
  };

  const auto area = static_cast<std::int64_t>(transformed.width()) * transformed.height();
  const auto workers = patchy::max_blocking_fanout_workers(
      std::clamp(std::min(transformed.height() / kResampleRowsPerWorker, patchy::hardware_worker_threads()), 1, 16));
  if (area >= kResampleParallelMinArea && workers >= 2 &&
      !qEnvironmentVariableIsSet("PATCHY_RENDER_SINGLE_THREADED")) {
    std::vector<std::future<void>> bands;
    bands.reserve(static_cast<std::size_t>(workers));
    const auto rows_per_band = (transformed.height() + workers - 1) / workers;
    for (int start = top; start < bottom; start += rows_per_band) {
      bands.push_back(std::async(std::launch::async, resample_band, start, std::min(start + rows_per_band, bottom)));
    }
    for (auto& band : bands) {
      band.get();
    }
  } else {
    resample_band(top, bottom);
  }
  const auto bounds = Rect{left, top, transformed.width(), transformed.height()};
  if (deep_format.has_value() && *deep_format != QImage::Format_RGBA32FPx4) {
    transformed = transformed.convertToFormat(*deep_format);
  }
  return TransformedImage{std::move(transformed), bounds, cell_pixel_tests.load()};
}

TransformedMask resample_transformed_gray8(const PixelBuffer& source, std::uint8_t default_color,
                                           const QTransform& source_to_document,
                                           CanvasWidget::TransformInterpolation interpolation) {
  const auto source_width = source.width();
  const auto source_height = source.height();
  const auto mapped = source_to_document.mapRect(QRectF(0.0, 0.0, source_width, source_height));
  const auto left = static_cast<int>(std::floor(mapped.left()));
  const auto top = static_cast<int>(std::floor(mapped.top()));
  const auto right = static_cast<int>(std::ceil(mapped.right()));
  const auto bottom = static_cast<int>(std::ceil(mapped.bottom()));
  // A 16/32-bit mask resamples on its float samples and stays at its depth.
  const bool deep = source.format().bit_depth != BitDepth::UInt8;
  PixelBuffer transformed(std::max(1, right - left), std::max(1, bottom - top),
                          deep ? source.format() : PixelFormat::gray8());
  if (deep) {
    const std::vector<float> background(static_cast<std::size_t>(transformed.width()), static_cast<float>(default_color));
    for (int y = 0; y < transformed.height(); ++y) {
      store_coverage_row(transformed, y, 0, transformed.width(), background);
    }
  } else {
    transformed.clear(default_color);
  }
  const auto bounds = Rect{left, top, transformed.width(), transformed.height()};
  interpolation = resolve_automatic_interpolation(interpolation, std::abs(source_to_document.determinant()));

  bool invertible = false;
  const auto document_to_source = source_to_document.inverted(&invertible);
  if (!invertible || source.empty()) {
    return TransformedMask{std::move(transformed), bounds};
  }

  const auto* source_bits = source.data().data();
  const auto source_stride = source.stride_bytes();
  std::vector<float> deep_plane;
  if (deep) {
    deep_plane.resize(static_cast<std::size_t>(source_width) * static_cast<std::size_t>(source_height));
    for (int y = 0; y < source_height; ++y) {
      load_coverage_row(source, y, 0, source_width,
                        std::span<float>(deep_plane).subspan(static_cast<std::size_t>(y) * source_width, source_width));
    }
  }
  const auto* deep_samples = deep_plane.data();
  const auto sample_gray = [source_bits, source_stride, source_width, source_height, default_color, deep,
                            deep_samples](int x, int y) {
    if (x < 0 || y < 0 || x >= source_width || y >= source_height) {
      return static_cast<double>(default_color);
    }
    if (deep) {
      return static_cast<double>(deep_samples[static_cast<std::size_t>(y) * source_width + x]);
    }
    return static_cast<double>(
        source_bits[static_cast<std::size_t>(y) * source_stride + static_cast<std::size_t>(x)]);
  };

  // Same parallel contract as resample_transformed_rgba8: every output pixel is
  // a pure function of (source, inverse transform, x, y), so row strips are
  // byte-identical to the sequential walk. The single detach up front matters:
  // concurrent data() calls from workers would race on the copy-on-write bytes.
  auto* transformed_bits = transformed.data().data();
  const auto transformed_stride = transformed.stride_bytes();
  const auto deep_depth = transformed.format().bit_depth;
  const auto resample_rows = [&sample_gray, &document_to_source, interpolation, transformed_bits,
                              transformed_stride, left, top, source_width, source_height, default_color, deep,
                              deep_depth, width = transformed.width()](int row_begin, int row_end) {
    for (int y = row_begin; y < row_end; ++y) {
      auto* row = transformed_bits + static_cast<std::size_t>(y) * transformed_stride;
      for (int x = 0; x < width; ++x) {
        const auto source_point = document_to_source.map(QPointF(static_cast<double>(left + x) + 0.5,
                                                                 static_cast<double>(top + y) + 0.5));
        double value = 0.0;
        switch (interpolation) {
          case CanvasWidget::TransformInterpolation::NearestNeighbor: {
            if (source_point.x() < 0.0 || source_point.y() < 0.0 || source_point.x() >= source_width ||
                source_point.y() >= source_height) {
              value = static_cast<double>(default_color);
            } else {
              value = sample_gray(static_cast<int>(std::floor(source_point.x())),
                                  static_cast<int>(std::floor(source_point.y())));
            }
            break;
          }
          case CanvasWidget::TransformInterpolation::Bilinear: {
            const auto sx = source_point.x() - 0.5;
            const auto sy = source_point.y() - 0.5;
            const auto x0 = static_cast<int>(std::floor(sx));
            const auto y0 = static_cast<int>(std::floor(sy));
            const auto tx = sx - static_cast<double>(x0);
            const auto ty = sy - static_cast<double>(y0);
            value = sample_gray(x0, y0) * (1.0 - tx) * (1.0 - ty) +
                    sample_gray(x0 + 1, y0) * tx * (1.0 - ty) +
                    sample_gray(x0, y0 + 1) * (1.0 - tx) * ty + sample_gray(x0 + 1, y0 + 1) * tx * ty;
            break;
          }
          case CanvasWidget::TransformInterpolation::Bicubic:
          case CanvasWidget::TransformInterpolation::BicubicSmoother:
          case CanvasWidget::TransformInterpolation::BicubicSharper:
          case CanvasWidget::TransformInterpolation::Automatic: {  // resolved above
            const auto sx = source_point.x() - 0.5;
            const auto sy = source_point.y() - 0.5;
            const auto base_x = static_cast<int>(std::floor(sx));
            const auto base_y = static_cast<int>(std::floor(sy));
            for (int yy = -1; yy <= 2; ++yy) {
              const auto wy = cubic_tap_weight(interpolation, sy - static_cast<double>(base_y + yy));
              for (int xx = -1; xx <= 2; ++xx) {
                const auto wx = cubic_tap_weight(interpolation, sx - static_cast<double>(base_x + xx));
                value += sample_gray(base_x + xx, base_y + yy) * wx * wy;
              }
            }
            break;
          }
        }
        if (deep) {
          const auto clamped = std::clamp(value, 0.0, 255.0);
          if (deep_depth == BitDepth::Float32) {
            const auto stored = static_cast<float>(clamped / 255.0);
            std::memcpy(row + static_cast<std::size_t>(x) * 4U, &stored, sizeof(stored));
          } else {
            const auto stored = static_cast<std::uint16_t>(std::lround(clamped * 257.0));
            std::memcpy(row + static_cast<std::size_t>(x) * 2U, &stored, sizeof(stored));
          }
          continue;
        }
        row[x] = clamp_sample_channel(value);
      }
    }
  };

  const auto area = static_cast<std::int64_t>(transformed.width()) * transformed.height();
  const auto hardware_threads = patchy::hardware_worker_threads();
  const auto workers = patchy::max_blocking_fanout_workers(
      std::clamp(std::min(transformed.height() / kResampleRowsPerWorker, hardware_threads), 1, 16));
  if (area >= kResampleParallelMinArea && workers >= 2 &&
      !qEnvironmentVariableIsSet("PATCHY_RENDER_SINGLE_THREADED")) {
    std::vector<std::future<void>> strips;
    strips.reserve(static_cast<std::size_t>(workers));
    const auto rows_per_strip = (transformed.height() + workers - 1) / workers;
    for (int start = 0; start < transformed.height(); start += rows_per_strip) {
      const auto end = std::min(start + rows_per_strip, transformed.height());
      strips.push_back(std::async(std::launch::async, resample_rows, start, end));
    }
    for (auto& strip : strips) {
      strip.get();
    }
  } else {
    resample_rows(0, transformed.height());
  }

  return TransformedMask{std::move(transformed), bounds};
}

bool CanvasWidget::begin_free_transform() {
  if (layer_edit_target_ == LayerEditTarget::SmartFilterMask) {
    report_status_error(tr("This tool is unavailable while editing a Smart Filter mask"));
    return false;
  }
  if (warping_layer_) {
    // Single session: switching modes keeps the pending warp (Photoshop behavior).
    return switch_warp_to_free_transform();
  }
  // A selected folder or a multi-layer selection transforms as one flattened
  // target set (Photoshop behavior). Exactly one non-group root keeps the
  // single-layer path below byte-for-byte; its commit bytes are pinned.
  if (auto collection = collect_free_transform_targets(); !collection.use_single_layer_path) {
    return begin_free_transform_multi(std::move(collection));
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
    report_status_error(tr("Select an editable pixel layer to transform"));
    return false;
  }
  // With a selection on a pixel layer only the selected pixels transform (GitHub
  // issue 399). The Background's position lock keeps the layer in place, not its
  // pixels, so its selection transforms too (the hole fills with the background color).
  const auto floating_rect = floating_selection_rect_for_layer(*layer);
  const bool floating_on_background =
      floating_rect.has_value() &&
      background_layer(std::as_const(*document_).layers(), document_->width(), document_->height()) == layer;
  if (layer_effectively_locks_position(*layer) && !floating_on_background) {
    show_layer_position_locked_message();
    return false;
  }
  if (layer_is_smart_object(*layer) && !smart_object_lock_reason(*layer).empty()) {
    // Scaling or rotating a preview-locked smart object (warp / smart filters /
    // external) would desync the pixels from what Photoshop re-renders; integrity
    // first. Plain moves stay allowed through the move tool.
    report_status_error(tr("This smart object is preview-only and can't be transformed. Rasterize the layer first."));
    return false;
  }
  if (!vector_lock_reason(*layer).empty()) {
    report_status_error(tr("This layer's vector data is preserved but can't be edited."));
    return false;
  }
  const auto opaque_rect = opaque_pixel_local_rect(*layer);
  if (!opaque_rect.has_value()) {
    report_status_error(tr("Layer has no opaque pixels to transform"));
    return false;
  }
  // A text layer transforms its whole raster (a box layer's frame, a point layer's tight
  // bounds): the rect the passive Move-tool controls draw (move_layer_transform_local_rect)
  // and what Photoshop frames for paragraph text. A handle grabbed on that passive frame must
  // start the session on the SAME rect: the drag sets the rect corner to the absolute mouse
  // position, so a session started on the smaller ink rect stretched the ink out to the frame
  // corner under the cursor on the first mouse move (box text went "instantly giant").
  const std::optional<QRect> local_transform_rect =
      layer_is_text(*layer) ? move_layer_transform_local_rect(*layer).value_or(*opaque_rect) : *opaque_rect;
  std::optional<TransformFloatingSelection> floating;
  if (floating_rect.has_value()) {
    floating = lift_floating_selection(*layer, *floating_rect);
    if (!floating.has_value()) {
      return false;
    }
  }

  transforming_layer_ = true;
  clear_transform_session_steps();
  transform_corner_offsets_ = {};
  transform_distort_mode_ = TransformDistortMode::None;
  dragging_transform_ = false;
  transform_layer_id_ = layer->id();
  set_move_transform_controls_layer(std::nullopt);
  const auto bounds = layer->bounds();
  transform_original_rect_ =
      floating.has_value()
          ? QRectF(*floating_rect)
          : QRectF(bounds.x + local_transform_rect->x(), bounds.y + local_transform_rect->y(),
                   local_transform_rect->width(), local_transform_rect->height());
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
  transform_base_cache_scale_level_ = 0;
  transform_base_display_mip_cache_.clear();
  transform_base_display_mip_source_key_ = 0;
  transform_preview_patches_.clear();
  transform_preview_patches_rect_ = QRect();
  transform_drag_uses_proxy_preview_ = false;
  transform_live_frame_slow_ = false;
  transform_proxy_image_ = QImage();
  transform_drag_uses_clip_group_preview_ = false;
  transform_clip_group_image_ = QImage();
  transform_clip_group_rect_ = QRect();
  transform_mask_sources_.clear();
  transform_proxy_layer_opacity_ = 1.0;
  // A floating selection always previews composited: the floating pixels land on
  // the layer's own remaining pixels, which a blit over the base cannot show.
  transform_floating_ = std::move(floating);
  transform_requires_composited_preview_ =
      transform_floating_.has_value() || transform_preview_needs_compositing(*document_, *layer);
  transform_clip_group_preview_possible_ =
      !transform_floating_.has_value() && transform_clip_group_preview_possible(*document_, *layer);
  setCursor(Qt::ArrowCursor);
  update();
  notify_transform_controls_changed();
  if (status_callback_) {
    status_callback_(shift_keeps_transform_aspect_
                         ? tr("Drag handles to transform. Shift keeps aspect ratio.")
                         : tr("Drag handles to transform. Shift resizes freely."));
  }
  return true;
}

std::optional<QRect> CanvasWidget::floating_selection_rect_for_layer(const Layer& layer) const {
  if (document_ == nullptr || selection_.isEmpty() || quick_mask_active_ || transform_ignores_selection_ ||
      layer_edit_target_ != LayerEditTarget::Content || layer.kind() != LayerKind::Pixel ||
      layer_pixels_are_procedural(layer) || !layer_has_movable_pixels(layer)) {
    return std::nullopt;
  }
  const auto opaque_rect = opaque_pixel_local_rect(layer);
  if (!opaque_rect.has_value()) {
    return QRect();
  }
  const auto bounds = layer.bounds();
  return opaque_rect->translated(bounds.x, bounds.y).intersected(selection_.boundingRect());
}

std::optional<CanvasWidget::TransformFloatingSelection> CanvasWidget::lift_floating_selection(const Layer& layer,
                                                                                              QRect floating_rect) {
  if (document_ == nullptr) {
    return std::nullopt;
  }
  if (layer_effectively_locks_image_pixels(layer)) {
    show_layer_pixels_locked_message();
    return std::nullopt;
  }
  const auto report_empty = [this] { report_status_error(tr("The selected area is empty.")); };
  if (floating_rect.isEmpty()) {
    report_empty();
    return std::nullopt;
  }
  const auto& document = std::as_const(*document_);
  // The whole selection's coverage (the commit moves all of it, also where the
  // layer is transparent), clipped to the canvas like the selection itself.
  const auto coverage_rect = selection_.boundingRect().intersected(QRect(0, 0, document.width(), document.height()));
  TransformFloatingSelection floating;
  floating.coverage = selection_coverage_buffer(coverage_rect);
  floating.coverage_bounds = to_core_rect(coverage_rect);
  // The Background (no alpha) and transparency-locked layers keep their alpha: the
  // hole fills with the background color, as Cut does there.
  floating.preserve_alpha = layer_effectively_locks_transparent_pixels(document.layers(), layer.id()) ||
                            background_layer(document.layers(), document.width(), document.height()) == &layer;
  std::optional<std::array<std::uint8_t, 3>> fill;
  if (floating.preserve_alpha) {
    const auto color = secondary_color();
    fill = std::array<std::uint8_t, 3>{static_cast<std::uint8_t>(color.red()), static_cast<std::uint8_t>(color.green()),
                                       static_cast<std::uint8_t>(color.blue())};
  }
  auto lifted = lift_selected_pixels(layer.pixels(), layer.bounds(), floating.coverage, floating.coverage_bounds, fill);
  const auto floating_local =
      floating_rect.translated(-lifted.floating_bounds.x, -lifted.floating_bounds.y)
          .intersected(QRect(0, 0, lifted.floating.width(), lifted.floating.height()));
  if (lifted.floating.empty() || floating_local != floating_rect.translated(-lifted.floating_bounds.x,
                                                                            -lifted.floating_bounds.y) ||
      !visible_alpha_local_bounds(lifted.floating).has_value()) {
    report_empty();
    return std::nullopt;
  }
  floating.floating = qimage_from_pixel_buffer(lifted.floating).copy(floating_local);
  floating.remainder = std::move(lifted.remainder);
  floating.remainder_bounds = layer.bounds();
  if (floating.floating.isNull()) {
    report_empty();
    return std::nullopt;
  }
  return floating;
}

// The document the floating-selection previews render: a copy-on-write copy
// (O(layer count)) whose session layer holds the pixels left behind.
Document CanvasWidget::floating_selection_preview_document() const {
  Document copy = *document_;
  if (transform_floating_.has_value() && transform_layer_id_.has_value()) {
    if (auto* layer = copy.find_layer(*transform_layer_id_); layer != nullptr) {
      layer->set_pixels(transform_floating_->remainder);
      layer->set_bounds(transform_floating_->remainder_bounds);
    }
  }
  return copy;
}

bool CanvasWidget::free_transform_floats_selection() const noexcept {
  return transforming_layer_ && transform_floating_.has_value();
}

// Resolves the current panel selection to Free Transform targets. Exactly one
// non-group root defers to the single-layer path; a folder or a multi-root
// selection flattens to transformable leaves the way movable_layer_ids does,
// except that a leaf the transform cannot take along (position lock, preview-
// locked smart object, vector lock, unsupported Smart Filter stack) refuses the
// WHOLE session instead of being skipped: scaling the rest of a folder around a
// pinned member would tear the artwork apart.
CanvasWidget::TransformTargetCollection CanvasWidget::collect_free_transform_targets() const {
  TransformTargetCollection collection;
  if (document_ == nullptr) {
    collection.refusal = tr("Select an editable pixel layer to transform");
    return collection;
  }
  std::vector<LayerId> roots;
  if (!selected_layer_ids_.empty()) {
    roots = root_drop_layer_ids(std::as_const(*document_).layers(), selected_layer_ids_);
  } else if (const auto active = document_->active_layer_id(); active.has_value()) {
    roots.push_back(*active);
  }
  if (roots.empty()) {
    collection.refusal = tr("Select an editable pixel layer to transform");
    return collection;
  }
  if (roots.size() == 1U) {
    const auto* root = std::as_const(*document_).find_layer(roots.front());
    // A movable preview-locked smart object (non-affine quad, unsupported
    // warp, external, legacy) takes the multi path too: the single-layer path
    // refuses it, but the multi commit maps its quads per corner exactly.
    const auto movable_locked_smart_object = root != nullptr && layer_has_movable_pixels(*root) &&
                                             layer_is_smart_object(*root) &&
                                             !smart_object_lock_reason(*root).empty();
    if (root == nullptr || (root->kind() != LayerKind::Group && !movable_locked_smart_object)) {
      collection.use_single_layer_path = true;
      return collection;
    }
  }

  const auto ancestor_style_info = collect_ancestor_group_style_info(std::as_const(*document_).layers());
  const auto has_linked_raster_mask = [](const Layer& layer) {
    return layer.mask().has_value() && !layer.mask()->pixels.empty() && layer_mask_linked(layer);
  };
  const auto add_mask_only = [&collection, &has_linked_raster_mask](const Layer& layer) {
    if (has_linked_raster_mask(layer) || layer.vector_mask() != nullptr) {
      collection.mask_only_ids.push_back(layer.id());
    }
  };

  const std::function<bool(const Layer&, LayerLockFlags)> walk = [&](const Layer& layer,
                                                                     LayerLockFlags ancestor_flags) -> bool {
    const auto effective_flags = ancestor_flags | patchy::layer_lock_flags(layer);
    if (layer.kind() == LayerKind::Group) {
      add_mask_only(layer);
      for (const auto& child : layer.children()) {
        if (!walk(child, effective_flags)) {
          return false;
        }
      }
      return true;
    }
    if (layer.kind() == LayerKind::Adjustment) {
      // No pixels of its own; its masks position the effect and ride along.
      collection.mask_only_ids.push_back(layer.id());
      return true;
    }
    if (!layer_has_movable_pixels(layer)) {
      if (layer_is_smart_object(layer) && !smart_object_lock_reason(layer).empty()) {
        collection.refusal =
            tr("This smart object is preview-only and can't be transformed. Rasterize the layer first.");
        return false;
      }
      if (const auto* stack = layer.smart_filter_stack();
          stack != nullptr && stack->support == SmartFilterStackSupport::Unsupported) {
        collection.refusal = tr("Select an editable pixel layer to transform");
        return false;
      }
      // Empty or non-editable pixels: nothing to transform, but linked masks
      // still ride along.
      add_mask_only(layer);
      return true;
    }
    if ((effective_flags & kLayerLockPosition) != kLayerLockNone) {
      collection.position_lock_refusal = true;
      return false;
    }
    if (layer_is_smart_object(layer) && !smart_object_placement_from_layer(layer).has_value()) {
      // Preview-locked-but-parsed smart objects (non-affine quads, unsupported
      // warps, external, legacy) ARE transformable: their quads map per corner;
      // a linked one re-renders from its file, the others keep the resampled
      // pixels in place of a re-render, like Photoshop.
      // Without a parsed quad nothing can ride the transform, so refuse.
      collection.refusal =
          tr("This smart object is preview-only and can't be transformed. Rasterize the layer first.");
      return false;
    }
    if (!vector_lock_reason(layer).empty()) {
      collection.refusal = tr("This layer's vector data is preserved but can't be edited.");
      return false;
    }
    const auto local_rect = move_layer_transform_local_rect(layer);
    if (!local_rect.has_value() || local_rect->isEmpty()) {
      add_mask_only(layer);
      return true;
    }
    if (std::any_of(collection.targets.begin(), collection.targets.end(),
                    [&layer](const TransformTarget& target) { return target.id == layer.id(); })) {
      return true;
    }
    // Selected groups flatten to leaves, so a style on the folder itself is
    // invisible to the per-leaf check: fold every styled ancestor's expense and
    // padding back into each leaf's entry (the MovingLayer pattern).
    auto expensive_style = layer.layer_style().effects_visible && !layer.layer_style().empty();
    int ancestor_effect_padding = 0;
    if (const auto found = ancestor_style_info.find(layer.id()); found != ancestor_style_info.end()) {
      expensive_style = expensive_style || found->second.styled;
      ancestor_effect_padding = found->second.effect_padding;
    }
    collection.targets.push_back(TransformTarget{layer.id(), layer.bounds(), *local_rect, QImage(),
                                                 expensive_style, ancestor_effect_padding});
    return true;
  };

  for (const auto root_id : roots) {
    const auto* root = std::as_const(*document_).find_layer(root_id);
    if (root == nullptr) {
      continue;
    }
    if (!walk(*root, patchy::layer_ancestor_lock_flags(std::as_const(*document_).layers(), root_id))) {
      return collection;
    }
  }
  if (collection.targets.empty()) {
    collection.refusal = tr("Select an editable pixel layer to transform");
    return collection;
  }
  collection.root_ids = std::move(roots);
  std::sort(collection.root_ids.begin(), collection.root_ids.end());
  return collection;
}

QRectF CanvasWidget::transform_targets_content_union(const std::vector<TransformTarget>& targets) {
  QRectF union_rect;
  for (const auto& target : targets) {
    const QRectF content_rect(target.original_bounds.x + target.source_local_rect.x(),
                              target.original_bounds.y + target.source_local_rect.y(),
                              target.source_local_rect.width(), target.source_local_rect.height());
    union_rect = union_rect.isNull() ? content_rect : union_rect.united(content_rect);
  }
  return union_rect;
}

bool CanvasWidget::begin_free_transform_multi(TransformTargetCollection collection) {
  if (collection.position_lock_refusal) {
    show_layer_position_locked_message();
    return false;
  }
  if (!collection.refusal.isEmpty() || collection.targets.empty()) {
    report_status_error(!collection.refusal.isEmpty() ? collection.refusal
                                                      : tr("Select an editable pixel layer to transform"));
    return false;
  }

  const auto union_rect = transform_targets_content_union(collection.targets);
  if (union_rect.isEmpty()) {
    report_status_error(tr("Layer has no opaque pixels to transform"));
    return false;
  }

  transforming_layer_ = true;
  clear_transform_session_steps();
  transform_corner_offsets_ = {};
  transform_distort_mode_ = TransformDistortMode::None;
  dragging_transform_ = false;
  transform_layer_id_.reset();
  transform_targets_ = std::move(collection.targets);
  transform_mask_only_ids_ = std::move(collection.mask_only_ids);
  transform_session_root_ids_ = std::move(collection.root_ids);
  set_move_transform_controls_layer(std::nullopt);
  transform_original_rect_ = union_rect;
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
  transform_source_local_rect_ = QRect();
  transform_base_cache_ = QImage();
  transform_base_cache_scale_level_ = 0;
  transform_base_display_mip_cache_.clear();
  transform_base_display_mip_source_key_ = 0;
  transform_preview_patches_.clear();
  transform_preview_patches_rect_ = QRect();
  transform_drag_uses_proxy_preview_ = false;
  transform_live_frame_slow_ = false;
  transform_proxy_image_ = QImage();
  transform_drag_uses_clip_group_preview_ = false;
  transform_clip_group_image_ = QImage();
  transform_clip_group_rect_ = QRect();
  transform_mask_sources_.clear();
  transform_proxy_layer_opacity_ = 1.0;
  transform_multi_snapshot_ = QImage();
  transform_multi_snapshot_rect_ = QRect();
  transform_multi_snapshot_scale_level_ = 0;
  transform_multi_snapshot_clip_.reset();
  // The subtree snapshot bakes intra-set blending, masks, and styles, and every
  // non-drag refresh renders accurate patches, so multi sessions always use the
  // composited-preview machinery.
  transform_requires_composited_preview_ = true;
  transform_clip_group_preview_possible_ = false;
  setCursor(Qt::ArrowCursor);
  update();
  notify_transform_controls_changed();
  if (status_callback_) {
    status_callback_(shift_keeps_transform_aspect_
                         ? tr("Drag handles to transform. Shift keeps aspect ratio.")
                         : tr("Drag handles to transform. Shift resizes freely."));
  }
  return true;
}

bool CanvasWidget::free_transform_is_multi_target() const noexcept {
  return transforming_layer_ && !transform_targets_.empty();
}

// The commit-time preview composition, rendered once in document space at the
// base cache's scale level: the base (targets hidden) plus the accurate
// preview patches when present, else the same approximate blit the live
// preview drew (multi snapshot / latched proxy / plain source). Carries the
// session's approximation contract into the one deferred-refresh window after
// commit; the refresh that lands replaces it with the exact composite.
QImage CanvasWidget::compose_transform_commit_hold_image() const {
  if (!transforming_layer_ || document_ == nullptr || transform_base_cache_.isNull()) {
    return {};
  }
  const bool multi = !transform_targets_.empty();
  const bool have_patches = !transform_preview_patches_.empty();
  const bool proxy = !multi && transform_drag_uses_proxy_preview_ && !transform_proxy_image_.isNull();
  const bool have_blit = multi ? !transform_multi_snapshot_.isNull() && !transform_multi_snapshot_rect_.isEmpty()
                               : proxy || !transform_source_image_.isNull();
  if (!have_patches && !have_blit) {
    // Nothing stands in for the targets; the base alone would show them
    // missing, which is worse than the stale frame this hold replaces.
    return {};
  }

  auto hold = transform_base_cache_.convertToFormat(QImage::Format_ARGB32_Premultiplied);
  if (hold.isNull()) {
    return {};
  }
  QPainter painter(&hold);
  // Document space -> hold pixels; the base may be a preview-scaled mip.
  const auto scale = 1.0 / static_cast<double>(1 << transform_base_cache_scale_level_);
  painter.scale(scale, scale);
  const bool smooth = transform_interpolation_ != TransformInterpolation::NearestNeighbor;
  if (have_patches) {
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    for (const auto& patch : transform_preview_patches_) {
      if (!patch.image.isNull() && !patch.document_rect.isEmpty()) {
        painter.drawImage(QRectF(patch.document_rect), patch.image, QRectF(patch.image.rect()));
      }
    }
  } else if (multi) {
    // draw_free_transform's snapshot blit, minus the document-to-widget map.
    if (transform_multi_snapshot_clip_.has_value()) {
      painter.setClipRect(QRectF(*transform_multi_snapshot_clip_), Qt::IntersectClip);
    }
    painter.setRenderHint(QPainter::SmoothPixmapTransform, smooth);
    const auto delta = transform_session_delta();
    painter.setTransform(delta, true);
    painter.drawImage(QRectF(transform_multi_snapshot_rect_), transform_multi_snapshot_,
                      QRectF(transform_multi_snapshot_.rect()));
  } else {
    // The single-layer proxy/source blit in document units.
    painter.setRenderHint(QPainter::SmoothPixmapTransform, smooth);
    const auto& source = proxy ? transform_proxy_image_ : transform_preview_source_image();
    if (proxy) {
      painter.setOpacity(transform_proxy_layer_opacity_);
    }
    if (transform_is_distorted()) {
      painter.setTransform(transform_session_image_to_document(source), true);
      painter.drawImage(QRectF(source.rect()), source, QRectF(source.rect()));
      return hold;
    }
    const auto rect = transform_current_rect_;
    painter.translate(rect.center());
    painter.rotate(transform_angle_);
    painter.scale(transform_scale_x_sign_, transform_scale_y_sign_);
    const QRectF local_rect(-rect.width() / 2.0, -rect.height() / 2.0, rect.width(), rect.height());
    painter.drawImage(local_rect, source, QRectF(source.rect()));
  }
  return hold;
}

void CanvasWidget::arm_transform_commit_hold() {
  auto hold = compose_transform_commit_hold_image();
  if (hold.isNull()) {
    return;
  }
  transform_commit_hold_image_ = std::move(hold);
  transform_commit_hold_scale_level_ = transform_base_cache_scale_level_;
  transform_commit_hold_fresh_ = true;
}

void CanvasWidget::disarm_transform_commit_hold_if_settled() {
  if (render_settled() && !processing_operation_active()) {
    clear_transform_commit_hold();
  }
}

void CanvasWidget::clear_transform_commit_hold() {
  transform_commit_hold_image_ = QImage();
  transform_commit_hold_scale_level_ = 0;
  transform_commit_hold_fresh_ = false;
}

void CanvasWidget::reset_free_transform_session_state() {
  transforming_layer_ = false;
  clear_transform_session_steps();
  transform_corner_offsets_ = {};
  transform_distort_mode_ = TransformDistortMode::None;
  transform_drag_kind_ = TransformDragKind::Scale;
  dragging_transform_ = false;
  transform_layer_id_.reset();
  transform_drag_handle_ = TransformHandle::None;
  transform_scale_x_sign_ = 1.0;
  transform_scale_y_sign_ = 1.0;
  transform_drag_start_scale_x_sign_ = 1.0;
  transform_drag_start_scale_y_sign_ = 1.0;
  transform_base_cache_ = QImage();
  transform_base_cache_scale_level_ = 0;
  transform_base_display_mip_cache_.clear();
  transform_base_display_mip_source_key_ = 0;
  transform_source_image_ = QImage();
  transform_preview_patches_.clear();
  transform_preview_patches_rect_ = QRect();
  transform_drag_uses_proxy_preview_ = false;
  transform_live_frame_slow_ = false;
  transform_preview_patches_banded_ = false;
  transform_proxy_image_ = QImage();
  transform_drag_uses_clip_group_preview_ = false;
  transform_clip_group_image_ = QImage();
  transform_clip_group_rect_ = QRect();
  transform_mask_sources_.clear();
  transform_proxy_layer_opacity_ = 1.0;
  transform_requires_composited_preview_ = false;
  transform_clip_group_preview_possible_ = false;
  transform_source_local_rect_ = QRect();
  transform_targets_.clear();
  transform_mask_only_ids_.clear();
  transform_session_root_ids_.clear();
  transform_multi_snapshot_ = QImage();
  transform_multi_snapshot_rect_ = QRect();
  transform_multi_snapshot_scale_level_ = 0;
  transform_multi_snapshot_clip_.reset();
  transform_floating_.reset();
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

// The document rect a Move-tool Free Transform would start on for the current
// selection: the single target's session rect, or the union of a folder's or
// multi-selection's flattened target set. Empty when a session is in flight or
// the session would refuse (position lock, no pixel targets), so the passive
// controls hide and the double-click stays inert in the same cases.
std::optional<QRectF> CanvasWidget::move_transform_target_rect() const {
  if (document_ == nullptr || tool_ != CanvasTool::Move || moving_layer_ || transforming_layer_ ||
      dragging_transform_) {
    return std::nullopt;
  }

  std::optional<LayerId> target_layer_id;
  if (selected_layer_ids_.empty()) {
    target_layer_id = document_->active_layer_id();
    if (!target_layer_id.has_value()) {
      return std::nullopt;
    }
  } else if (selected_layer_ids_.size() == 1U) {
    target_layer_id = selected_layer_ids_.front();
  }
  if (target_layer_id.has_value()) {
    const auto* layer = document_->find_layer(*target_layer_id);
    if (layer == nullptr) {
      return std::nullopt;
    }
    if (layer->kind() != LayerKind::Group) {
      if (layer_effectively_locks_position(*layer)) {
        return std::nullopt;
      }
      // With a selection the box frames what a handle grab would float.
      if (const auto floating_rect = floating_selection_rect_for_layer(*layer); floating_rect.has_value()) {
        if (floating_rect->isEmpty()) {
          return std::nullopt;
        }
        return QRectF(*floating_rect);
      }
      return transform_controls_rect_for_layer(*layer);
    }
  }

  // A selected folder or a multi-layer selection frames the union of the
  // flattened target set Free Transform would take (grabbing a handle starts
  // that session), and hides when that session would refuse.
  const auto collection = collect_free_transform_targets();
  if (!collection.refusal.isEmpty() || collection.position_lock_refusal || collection.targets.empty()) {
    return std::nullopt;
  }
  const auto union_rect = transform_targets_content_union(collection.targets);
  if (union_rect.isEmpty()) {
    return std::nullopt;
  }
  return union_rect;
}

std::optional<QRectF> CanvasWidget::move_transform_controls_rect() const {
  if (!show_transform_controls_) {
    return std::nullopt;
  }
  return move_transform_target_rect();
}

std::vector<LayerId> CanvasWidget::free_transform_snap_exclude_ids() const {
  std::vector<LayerId> ids;
  if (!transforming_layer_) {
    return ids;
  }
  if (!transform_targets_.empty()) {
    ids.reserve(transform_targets_.size());
    for (const auto& target : transform_targets_) {
      ids.push_back(target.id);
    }
    return ids;
  }
  if (transform_layer_id_.has_value()) {
    ids.push_back(*transform_layer_id_);
  }
  return ids;
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
  if (!transforming_layer_ || document_ == nullptr) {
    return false;
  }
  if (!transform_targets_.empty()) {
    if (!transform_targets_.front().source_image.isNull()) {
      if (transform_preview_patches_.empty()) {
        refresh_transform_multi_preview_cache(false);
      }
      return true;
    }
    for (auto& target : transform_targets_) {
      const auto* layer = std::as_const(*document_).find_layer(target.id);
      if (layer == nullptr || target.source_local_rect.isEmpty()) {
        continue;
      }
      target.source_image = qimage_from_pixel_buffer(layer->pixels()).copy(target.source_local_rect);
    }
    if (transform_targets_.front().source_image.isNull()) {
      return false;
    }
    rebuild_transform_base_cache();
    ensure_transform_multi_snapshot();
    refresh_transform_multi_preview_cache(false);
    return true;
  }
  if (!transform_layer_id_.has_value()) {
    return false;
  }
  if (!transform_source_image_.isNull()) {
    if (transform_requires_composited_preview_ && transform_preview_patches_.empty()) {
      refresh_transform_composited_preview_cache();
    }
    return true;
  }
  auto* layer = document_->find_layer(*transform_layer_id_);
  if (layer == nullptr || transform_source_local_rect_.isEmpty()) {
    return false;
  }

  transform_source_image_ =
      transform_floating_.has_value()
          ? transform_floating_->floating
          : qimage_from_pixel_buffer(std::as_const(*layer).pixels()).copy(transform_source_local_rect_);
  rebuild_transform_base_cache();
  refresh_transform_composited_preview_cache();
  return !transform_source_image_.isNull();
}

const QImage& CanvasWidget::transform_preview_source_image() const {
  if (transform_source_image_.format() != QImage::Format_RGBA32FPx4) {
    return transform_source_image_;
  }
  const float exposure = document_ != nullptr ? std::as_const(*document_).color_state().view_exposure_stops : 0.0F;
  if (transform_source_display_image_.isNull() || transform_source_display_key_ != transform_source_image_.cacheKey() ||
      transform_source_display_exposure_ != exposure) {
    transform_source_display_image_ = display_image_from_deep_qimage(transform_source_image_, exposure);
    transform_source_display_key_ = transform_source_image_.cacheKey();
    transform_source_display_exposure_ = exposure;
  }
  return transform_source_display_image_;
}

// Builds the with-the-layer-hidden backdrop for the transform session. Like
// ensure_move_base_cache, the full recomposite (which caused a visible hitch
// at drag start on heavy documents) is only the fallback: when the render
// cache is current, reuse it and re-render just the region the layer (plus
// its effects) occupies, with the layer hidden via a visibility override so
// no revision-bumping visibility toggle is needed.
void CanvasWidget::rebuild_transform_base_cache() {
  wait_for_move_commit_job();  // the base patches the render cache, which must be exact
  transform_base_cache_ = QImage();
  transform_base_cache_scale_level_ = 0;
  transform_base_display_mip_cache_.clear();
  transform_base_display_mip_source_key_ = 0;
  if (document_ == nullptr) {
    return;
  }
  // Hidden set plus the region that content (with effects and styled-ancestor
  // padding) occupies: one layer for the single session, every target leaf for
  // the multi session. Preview-only; the commit path never reads this cache.
  std::vector<LayerId> hidden;
  QRect layers_effect_rect;
  // A floating selection keeps its layer in the base, holding the pixels left behind.
  std::optional<Document> floating_document;
  if (!transform_targets_.empty()) {
    hidden.reserve(transform_targets_.size());
    for (const auto& target : transform_targets_) {
      const auto* target_layer = std::as_const(*document_).find_layer(target.id);
      if (target_layer == nullptr) {
        continue;
      }
      hidden.push_back(target.id);
      auto with_effects = layer_bounds_with_effects(*target_layer, target_layer->bounds());
      if (!with_effects.empty() && target.ancestor_effect_padding > 0) {
        with_effects = outset_rect(with_effects, target.ancestor_effect_padding);
      }
      layers_effect_rect = layers_effect_rect.united(to_qrect(with_effects));
    }
    if (hidden.empty()) {
      return;
    }
  } else if (transform_layer_id_.has_value()) {
    const auto* layer = std::as_const(*document_).find_layer(*transform_layer_id_);
    if (layer == nullptr) {
      return;
    }
    if (transform_floating_.has_value()) {
      floating_document.emplace(floating_selection_preview_document());
    } else {
      hidden.push_back(*transform_layer_id_);
    }
    layers_effect_rect = to_qrect(layer_bounds_with_effects(*layer, layer->bounds()));
  } else {
    return;
  }
  const Document& base_document = floating_document.has_value() ? *floating_document : *document_;

  const QRect canvas_rect(0, 0, document_->width(), document_->height());
  // Display-resolution compositing: when zoomed out, build the base from the
  // preview-scaled document (4^level less work than a full-res canvas). The
  // scaled document holds the layer as it is, so a floating selection skips it.
  if (const auto composite_level = preview_composite_level_for_zoom(view_zoom());
      composite_level >= 1 && !floating_document.has_value()) {
    if (auto* scaled_document = preview_scaled_document_for_level(composite_level)) {
      const QRect scaled_canvas(0, 0, scaled_document->width(), scaled_document->height());
      auto base = qimage_from_document_rect_with_hidden_layers_banded(*scaled_document, scaled_canvas, true, hidden)
                      .convertToFormat(QImage::Format_RGBA8888);
      if (!base.isNull()) {
        transform_base_cache_ = std::move(base);
        transform_base_cache_scale_level_ = composite_level;
        ++render_cache_diagnostics_.transform_scaled_bases;
        return;
      }
    }
  }
  if (render_cache_dirty_ || render_cache_.isNull() || render_cache_.size() != canvas_rect.size()) {
    transform_base_cache_ = qimage_from_document_rect_with_hidden_layers(base_document, canvas_rect, true, hidden)
                                .convertToFormat(QImage::Format_RGBA8888);
    return;
  }

  const auto layer_rect = layers_effect_rect.intersected(canvas_rect);
  QImage base = render_cache_.convertToFormat(QImage::Format_ARGB32_Premultiplied);
  if (!layer_rect.isEmpty()) {
    const auto cleared = qimage_from_document_rect_with_hidden_layers(base_document, layer_rect, true, hidden);
    if (!cleared.isNull()) {
      QPainter painter(&base);
      painter.setCompositionMode(QPainter::CompositionMode_Source);
      painter.drawImage(layer_rect.topLeft(), cleared.convertToFormat(QImage::Format_ARGB32_Premultiplied));
    }
  }
  transform_base_cache_ = std::move(base);
}

// One composited snapshot of only the target subtree, for the multi-target
// drag blit: ensure_move_proxy_image's structure (hide every non-target
// pixel-bearing leaf, keep groups and adjustment layers rendering, so styled
// ancestor folders bake their effects around the target silhouette and the
// snapshot's colors stay close to the final composite). Blends against the
// real backdrop and content clipped at the canvas edge stay approximate until
// the release/numeric refresh renders accurate patches.
void CanvasWidget::clip_painter_to_transform_artboard(QPainter& painter) const {
  // The single-layer blits hold the layer alone: clip them to its artboard, as the
  // composite will (docs/artboards.md).
  if (document_ == nullptr || !transform_layer_id_.has_value()) {
    return;
  }
  if (const auto* owner = artboard_owning_layer(std::as_const(*document_), *transform_layer_id_); owner != nullptr) {
    const auto& rect = owner->artboard()->rect;
    painter.setClipRect(widget_rect_for_document_rect(QRectF(rect.x, rect.y, rect.width, rect.height)),
                        Qt::IntersectClip);
  }
}

bool CanvasWidget::ensure_transform_multi_snapshot() {
  if (!transform_multi_snapshot_.isNull()) {
    return true;
  }
  if (document_ == nullptr || transform_targets_.empty()) {
    return false;
  }

  const QRect canvas_rect(0, 0, document_->width(), document_->height());
  QRect snapshot_rect;
  for (const auto& target : transform_targets_) {
    const auto* layer = std::as_const(*document_).find_layer(target.id);
    if (layer == nullptr) {
      continue;
    }
    auto with_effects = layer_bounds_with_effects(*layer, layer->bounds());
    if (!with_effects.empty() && target.ancestor_effect_padding > 0) {
      with_effects = outset_rect(with_effects, target.ancestor_effect_padding);
    }
    snapshot_rect = snapshot_rect.united(to_qrect(with_effects));
  }
  snapshot_rect = snapshot_rect.intersected(canvas_rect);
  if (snapshot_rect.isEmpty()) {
    return false;
  }
  const auto composite_level = preview_composite_level_for_zoom(view_zoom());
  Document* scaled_document = composite_level >= 1 ? preview_scaled_document_for_level(composite_level) : nullptr;
  if (scaled_document != nullptr) {
    snapshot_rect = rect_aligned_to_mip_grid(snapshot_rect, composite_level).intersected(canvas_rect);
  }

  const auto is_target = [this](LayerId id) {
    return std::any_of(transform_targets_.begin(), transform_targets_.end(),
                       [id](const TransformTarget& target) { return target.id == id; });
  };
  std::vector<LayerId> hidden_leaves;
  const std::function<void(const Layer&)> collect_hidden = [&](const Layer& layer) {
    if (layer.kind() == LayerKind::Group) {
      for (const auto& child : layer.children()) {
        collect_hidden(child);
      }
      return;
    }
    if (layer.kind() != LayerKind::Adjustment && !is_target(layer.id())) {
      hidden_leaves.push_back(layer.id());
    }
  };
  for (const auto& layer : std::as_const(*document_).layers()) {
    collect_hidden(layer);
  }

  // Artboards holding targets render as plain groups (no paper, no clip baked in); the
  // blit clips to the one artboard they share, or not at all when they span several.
  const Document& base_source = scaled_document != nullptr ? *scaled_document : std::as_const(*document_);
  std::optional<Document> unclipped;
  transform_multi_snapshot_clip_.reset();
  std::optional<std::optional<LayerId>> common_owner;
  bool mixed_owners = false;
  for (const auto& target : transform_targets_) {
    const auto* owner = artboard_owning_layer(std::as_const(*document_), target.id);
    const std::optional<LayerId> owner_id = owner != nullptr ? std::optional<LayerId>(owner->id()) : std::nullopt;
    if (common_owner.has_value() && *common_owner != owner_id) {
      mixed_owners = true;
    }
    common_owner = owner_id;
    if (owner != nullptr) {
      if (!unclipped.has_value()) {
        unclipped.emplace(base_source);
      }
      for (auto& top : unclipped->layers()) {
        if (top.id() == owner->id()) {
          top.clear_artboard();
        }
      }
    }
  }
  if (!mixed_owners && common_owner.has_value() && common_owner->has_value()) {
    if (const auto* owner = std::as_const(*document_).find_layer(**common_owner); owner != nullptr) {
      const auto& rect = owner->artboard()->rect;
      transform_multi_snapshot_clip_ = QRect(rect.x, rect.y, rect.width, rect.height);
    }
  }
  const Document& snapshot_source = unclipped.has_value() ? *unclipped : base_source;
  // Banded: the snapshot is bounded but can cross the whole styled stack
  // (preview-only, so the band divergence class is acceptable).
  auto snapshot =
      scaled_document != nullptr
          ? qimage_from_document_rect_with_hidden_layers_banded(
                snapshot_source, preview_scaled_document_rect(snapshot_rect, composite_level), true, hidden_leaves)
          : qimage_from_document_rect_with_hidden_layers_banded(snapshot_source, snapshot_rect, true, hidden_leaves);
  if (snapshot.isNull()) {
    return false;
  }
  const auto rendered_area =
      static_cast<std::int64_t>(snapshot.width()) * static_cast<std::int64_t>(snapshot.height());
  if (rendered_area > kTransformProxyMaxPixels) {
    const auto scale = std::sqrt(static_cast<double>(kTransformProxyMaxPixels) / static_cast<double>(rendered_area));
    const QSize snapshot_size(std::max(1, static_cast<int>(std::lround(snapshot.width() * scale))),
                              std::max(1, static_cast<int>(std::lround(snapshot.height() * scale))));
    snapshot = snapshot.scaled(snapshot_size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  }
  transform_multi_snapshot_ = snapshot.convertToFormat(QImage::Format_ARGB32_Premultiplied);
  transform_multi_snapshot_rect_ = snapshot_rect;
  transform_multi_snapshot_scale_level_ = scaled_document != nullptr ? composite_level : 0;
  return !transform_multi_snapshot_.isNull();
}

// Multi-target twin of refresh_transform_composited_preview_cache: resamples
// every target through the one shared session delta and renders the union of
// their (ancestor-padded) effect rects with all the transformed pixels
// substituted at once. Runs at release, numeric edits, nudges, interpolation
// changes, and Layer Style live edits; drags blit the subtree snapshot instead.
void CanvasWidget::refresh_transform_multi_preview_cache(bool processing_wait) {
  transform_preview_patches_.clear();
  transform_preview_patches_rect_ = QRect();
  if (!transforming_layer_ || document_ == nullptr || transform_targets_.empty() ||
      transform_targets_.front().source_image.isNull()) {
    return;
  }

  const auto delta = transform_session_delta();
  struct TargetJob {
    const Layer* layer{nullptr};
    LayerId id{};
    QImage source;
    QTransform source_to_document;
    int ancestor_effect_padding{0};
  };
  std::vector<TargetJob> jobs;
  jobs.reserve(transform_targets_.size());
  for (const auto& target : transform_targets_) {
    const auto* layer = std::as_const(*document_).find_layer(target.id);
    if (layer == nullptr || target.source_image.isNull()) {
      continue;
    }
    // QTransform composes left-to-right: translate the source into document
    // space FIRST, then apply the shared delta.
    jobs.push_back(TargetJob{
        layer, target.id, target.source_image,
        QTransform::fromTranslate(target.original_bounds.x + target.source_local_rect.x(),
                                  target.original_bounds.y + target.source_local_rect.y()) *
            delta,
        target.ancestor_effect_padding});
  }
  if (jobs.empty()) {
    return;
  }
  // Linked raster masks (members, the folder's own, adjustment riders) preview
  // at their transformed position; their crops come from the session cache on
  // the main thread (the compute may run on a worker) and share copy-on-write.
  std::vector<std::pair<LayerId, TransformLinkedMaskSource>> mask_sources;
  const auto collect_mask_source = [&](LayerId id) {
    const auto* layer = std::as_const(*document_).find_layer(id);
    if (layer == nullptr) {
      return;
    }
    if (const auto* source = transform_linked_mask_source(*layer); source != nullptr) {
      mask_sources.emplace_back(id, *source);
    }
  };
  for (const auto& job : jobs) {
    collect_mask_source(job.id);
  }
  for (const auto id : transform_mask_only_ids_) {
    collect_mask_source(id);
  }

  const auto compute = [document = document_, jobs = std::move(jobs), delta, mask_sources = std::move(mask_sources),
                        interpolation = transform_interpolation_]()
      -> std::pair<std::vector<RenderedDocumentPatch>, QRect> {
    const QRect canvas_rect(0, 0, document->width(), document->height());
    // The transformed masks ride a copy-on-write document copy, the way the
    // single-layer preview does (render overrides carry mask bounds only).
    std::optional<Document> masked_document;
    for (const auto& [id, source] : mask_sources) {
      const auto* layer = std::as_const(*document).find_layer(id);
      if (layer == nullptr) {
        continue;
      }
      auto mask = transformed_linked_raster_mask(*layer, source, delta, interpolation, false);
      if (!mask.has_value()) {
        continue;
      }
      if (!masked_document.has_value()) {
        masked_document.emplace(*document);
      }
      if (auto* masked_layer = masked_document->find_layer(id); masked_layer != nullptr) {
        masked_layer->set_mask(std::move(*mask));
      }
    }
    std::vector<PixelBuffer> transformed_pixels;
    transformed_pixels.reserve(jobs.size());  // stable addresses for the overrides
    std::vector<LayerPixelsOverrideSpec> overrides;
    overrides.reserve(jobs.size());
    QRegion patch_region;
    for (const auto& job : jobs) {
      const auto transformed_result = resample_transformed_rgba8(job.source, job.source_to_document, interpolation);
      if (transformed_result.image.isNull()) {
        continue;
      }
      transformed_pixels.push_back(pixels_from_image_native(transformed_result.image));
      overrides.push_back(LayerPixelsOverrideSpec{job.id, transformed_result.bounds, &transformed_pixels.back()});
      auto with_effects = layer_bounds_with_effects(*job.layer, transformed_result.bounds);
      if (!with_effects.empty() && job.ancestor_effect_padding > 0) {
        with_effects = outset_rect(with_effects, job.ancestor_effect_padding);
      }
      const auto patch_rect = to_qrect(with_effects).intersected(canvas_rect);
      if (!patch_rect.isEmpty()) {
        patch_region += patch_rect;
      }
    }
    if (patch_region.isEmpty() || overrides.empty()) {
      return {};
    }
    auto patches = qimage_patches_from_document_region_with_layer_pixel_overrides(
        masked_document.has_value() ? *masked_document : *document, patch_region, true, overrides);
    return {std::move(patches), patch_region.boundingRect()};
  };

  std::pair<std::vector<RenderedDocumentPatch>, QRect> result;
  bool run_inline = !processing_wait;
#if defined(Q_OS_WASM) && defined(__EMSCRIPTEN_PTHREADS__)
  // Same pool contract as the single-layer refresh above: no idle pre-spawned
  // worker means the blocked path cannot rely on a lazy spawn, and the
  // compute's own fan-outs must fit the pool without the compute's worker.
  const auto idle_pool = patchy::idle_prespawned_pool_workers();
  if (idle_pool < 1) {
    run_inline = true;
  }
  const patchy::BlockingFanoutBudgetScope fanout_budget(
      processing_wait && !run_inline ? std::max(0, idle_pool - 3) : -1);
#endif
  if (!run_inline) {
    auto future = launch_async(compute);
    const auto overlay_shown = wait_for_processing_operation(
        [&future] { return future.wait_for(std::chrono::milliseconds(16)) == std::future_status::ready; }, true);
    result = future.get();
    if (overlay_shown) {
      hide_processing_overlay();
    }
  } else {
    result = compute();
  }
  transform_preview_patches_ = std::move(result.first);
  transform_preview_patches_rect_ = result.second;
}

void CanvasWidget::refresh_transform_composited_preview_cache(bool processing_wait) {
  transform_preview_patches_banded_ = false;
  if (!transform_targets_.empty()) {
    refresh_transform_multi_preview_cache(processing_wait);
    return;
  }
  transform_preview_patches_.clear();
  transform_preview_patches_rect_ = QRect();
  if (!transform_requires_composited_preview_ || !transforming_layer_ || document_ == nullptr ||
      !transform_layer_id_.has_value() || transform_source_image_.isNull()) {
    return;
  }
  const auto* layer = std::as_const(*document_).find_layer(*transform_layer_id_);
  if (layer == nullptr) {
    return;
  }
  // The linked mask's crop comes from the session cache on the main thread
  // (the compute may run on a worker at release) and shares copy-on-write.
  std::optional<TransformLinkedMaskSource> mask_source;
  if (const auto* cached = transform_linked_mask_source(*layer); cached != nullptr && !transform_floating_.has_value()) {
    mask_source = *cached;
  }
  // A floating selection renders over the document holding the pixels left
  // behind; the layer override is those pixels with the transformed ones on top,
  // cropped to what the patch can read (its rect plus the effects' reach). The
  // layer mask stays where it is.
  struct FloatingPreview {
    std::shared_ptr<const Document> document;
    PixelBuffer remainder;
    Rect remainder_bounds{};
    bool preserve_alpha{false};
  };
  std::optional<FloatingPreview> floating_preview;
  if (transform_floating_.has_value()) {
    floating_preview = FloatingPreview{std::make_shared<const Document>(floating_selection_preview_document()),
                                       transform_floating_->remainder, transform_floating_->remainder_bounds,
                                       transform_floating_->preserve_alpha};
  }

  // Region-limited: the layer (plus its effects) can only contribute inside
  // its transformed bounds, and everywhere else transform_base_cache_ already
  // holds the final composite. Re-rendering the full canvas here on every
  // mouse-move was the transform preview's dominant cost on large documents.
  const bool live_drag = dragging_transform_ && !processing_wait;
  const auto compute = [document = document_, layer, layer_id = *transform_layer_id_,
                        source = transform_source_image_, mask_source, floating_preview,
                        source_to_document =
                            transform_session_source_to_document(transform_source_image_.size()),
                        delta = transform_session_delta(),
                        live_drag,
                        interpolation =
                            transform_interpolation_]() -> std::pair<std::vector<RenderedDocumentPatch>, QRect> {
    const auto transformed_result = resample_transformed_rgba8(source, source_to_document, interpolation);
    if (transformed_result.image.isNull()) {
      return {};
    }
    const auto transformed_pixels = pixels_from_image_native(transformed_result.image);
    const QRect canvas_rect(0, 0, document->width(), document->height());
    const auto patch_rect =
        to_qrect(layer_bounds_with_effects(*layer, transformed_result.bounds)).intersected(canvas_rect);
    if (patch_rect.isEmpty()) {
      return {};
    }
    // A linked raster mask previews at its transformed position too. The
    // render overrides carry mask bounds only, so the transformed mask rides a
    // copy of the document: the copy is O(layer count) with copy-on-write pixel
    // buffers, never O(pixels), which is why it stays per frame.
    std::optional<Document> masked_document;
    if (mask_source.has_value()) {
      if (auto mask = transformed_linked_raster_mask(*layer, *mask_source, delta, interpolation, false);
          mask.has_value()) {
        masked_document.emplace(*document);
        if (auto* masked_layer = masked_document->find_layer(layer_id); masked_layer != nullptr) {
          masked_layer->set_mask(std::move(*mask));
        }
      }
    }
    if (floating_preview.has_value()) {
      const auto extent = floating_preview->preserve_alpha
                              ? floating_preview->remainder_bounds
                              : unite_rect(floating_preview->remainder_bounds, transformed_result.bounds);
      const auto override_bounds = intersect_rect(layer_bounds_with_effects(*layer, to_core_rect(patch_rect)), extent);
      const auto override_pixels = composite_floating_pixels(
          floating_preview->remainder, floating_preview->remainder_bounds, transformed_pixels, transformed_result.bounds,
          override_bounds, floating_preview->preserve_alpha);
      auto patches = live_drag ? qimage_patch_from_document_rect_with_layer_pixels_banded(
                                     *floating_preview->document, patch_rect, true, layer_id, override_pixels,
                                     override_bounds)
                               : qimage_patches_from_document_region_with_layer_pixels(
                                     *floating_preview->document, QRegion(patch_rect), true, layer_id,
                                     override_pixels, override_bounds);
      return {std::move(patches), patch_rect};
    }
    const auto& render_document = masked_document.has_value() ? *masked_document : *document;
    // Drag frames band the patch across workers: a single rect under 4 Mpx
    // otherwise composites on one thread (about 70 ms per mouse-move for a
    // masked 1152x648 layer, GitHub #13). Preview-only by contract; the
    // release, numeric, and nudge refreshes keep the exact unbanded render.
    auto patches = live_drag ? qimage_patch_from_document_rect_with_layer_pixels_banded(
                                   render_document, patch_rect, true, layer_id, transformed_pixels,
                                   transformed_result.bounds)
                             : qimage_patches_from_document_region_with_layer_pixels(
                                   render_document, QRegion(patch_rect), true, layer_id, transformed_pixels,
                                   transformed_result.bounds);
    return {std::move(patches), patch_rect};
  };

  std::pair<std::vector<RenderedDocumentPatch>, QRect> result;
  bool run_inline = !processing_wait;
#if defined(Q_OS_WASM) && defined(__EMSCRIPTEN_PTHREADS__)
  // With no idle pre-spawned pool worker, launch_async itself would need a
  // lazy Worker spawn, which cannot be relied on from a blocked path; run
  // inline instead (main-thread fan-outs clamp to the pool and cannot wedge).
  const auto idle_pool = patchy::idle_prespawned_pool_workers();
  if (idle_pool < 1) {
    run_inline = true;
  }
  // The compute's own fan-outs (resample, then the patch render) must fit the
  // pool WITHOUT the workers the compute already consumed: finished pthreads
  // only return to the pool when the main thread's JS event loop runs their
  // 'cleanupThread' message, so back-to-back fan-outs cannot reuse each
  // other's workers mid-wait. One for the compute, two race margin.
  const patchy::BlockingFanoutBudgetScope fanout_budget(
      processing_wait && !run_inline ? std::max(0, idle_pool - 3) : -1);
#endif
  if (!run_inline) {
    // Release-time restore after a proxy-latched drag: same worker + overlay
    // wait the move tool's release uses, so a long styled render shows the
    // processing spinner instead of silently freezing the UI.
    auto future = launch_async(compute);
    const auto overlay_shown = wait_for_processing_operation(
        [&future] { return future.wait_for(std::chrono::milliseconds(16)) == std::future_status::ready; }, true);
    result = future.get();
    if (overlay_shown) {
      hide_processing_overlay();
    }
  } else {
    const auto compute_start = std::chrono::steady_clock::now();
    result = compute();
    const auto compute_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - compute_start).count();
    // Only live (no-wait) refreshes feed the escape hatch; the release-time
    // accurate render is expected to be slow and must not poison the flag.
    if (!processing_wait && compute_ms > live_preview_frame_latch_ms()) {
      transform_live_frame_slow_ = true;
    }
  }
  transform_preview_patches_ = std::move(result.first);
  transform_preview_patches_rect_ = result.second;
  transform_preview_patches_banded_ = live_drag;
}

// Main-thread lookup of the session's cached linked-mask crop for `layer`,
// rebuilt when the layer's content revision moved. Returns nullptr when the
// layer has no linked raster mask to take along.
const TransformLinkedMaskSource* CanvasWidget::transform_linked_mask_source(const Layer& layer) {
  const auto found = transform_mask_sources_.find(layer.id());
  if (found != transform_mask_sources_.end() && found->second.content_revision == layer.content_revision()) {
    return &found->second;
  }
  auto source = linked_mask_source(layer);
  if (!source.has_value()) {
    transform_mask_sources_.erase(layer.id());
    return nullptr;
  }
  auto& slot = transform_mask_sources_[layer.id()];
  slot = std::move(*source);
  return &slot;
}

bool CanvasWidget::transform_drag_should_use_proxy_preview() const {
  if (document_ == nullptr || !transform_layer_id_.has_value() || transform_source_image_.isNull()) {
    return false;
  }
  const auto* layer = std::as_const(*document_).find_layer(*transform_layer_id_);
  if (layer == nullptr) {
    return false;
  }
  // Time escape hatch: the area gates below cannot price the stack the
  // transform crosses; a measured slow composited-preview frame latches the
  // proxy regardless of area (mirrors the move tool's hatch).
  if (transform_live_frame_slow_) {
    return true;
  }

  const auto transformed_rect =
      transform_session_source_to_document(transform_source_image_.size())
          .mapRect(QRectF(0.0, 0.0, transform_source_image_.width(), transform_source_image_.height()))
          .toAlignedRect();
  const auto resample_area =
      static_cast<std::int64_t>(transformed_rect.width()) * static_cast<std::int64_t>(transformed_rect.height());

  const QRect canvas_rect(0, 0, document_->width(), document_->height());
  const auto patch_rect = to_qrect(layer_bounds_with_effects(*layer, Rect{transformed_rect.x(), transformed_rect.y(),
                                                                          transformed_rect.width(),
                                                                          transformed_rect.height()}))
                              .intersected(canvas_rect);
  const auto patch_area =
      static_cast<std::int64_t>(patch_rect.width()) * static_cast<std::int64_t>(patch_rect.height());

  const auto area = std::max(resample_area, patch_area);
  const bool styled = layer->layer_style().effects_visible && !layer->layer_style().empty();
  // The area gates price a frame on the 16-worker reference machine (a 4 Mpx
  // composited preview runs about 85 ms there, just under the time hatch).
  // Every stage of a composited frame fans out per row, so its cost scales
  // with the worker count: a 4-thread laptop pays the same frame about four
  // times longer, so it latches at a quarter of the area (GitHub #15).
  const auto worker_share = static_cast<double>(std::clamp(patchy::hardware_worker_threads(), 1, 16)) / 16.0;
  const auto threshold = static_cast<std::int64_t>(
      static_cast<double>(styled ? kStyledTransformProxyAreaThreshold : kTransformProxyAreaThreshold) * worker_share);
  return area >= threshold;
}

void CanvasWidget::ensure_transform_proxy_image() {
  transform_proxy_layer_opacity_ = 1.0;
  if (document_ != nullptr && transform_layer_id_.has_value()) {
    if (const auto* layer = std::as_const(*document_).find_layer(*transform_layer_id_); layer != nullptr) {
      transform_proxy_layer_opacity_ =
          static_cast<double>(layer->opacity()) * static_cast<double>(layer->fill_opacity());
    }
  }
  if (!transform_proxy_image_.isNull() || transform_source_image_.isNull()) {
    return;
  }
  QImage source = transform_preview_source_image();
  if (document_ != nullptr && transform_layer_id_.has_value() && !transform_floating_.has_value()) {
    if (const auto* layer = std::as_const(*document_).find_layer(*transform_layer_id_); layer != nullptr) {
      source = source_image_with_mask_coverage(source, *layer, transform_original_rect_);
    }
  }
  const auto source_area = static_cast<std::int64_t>(source.width()) * static_cast<std::int64_t>(source.height());
  if (source_area <= kTransformProxyMaxPixels) {
    transform_proxy_image_ = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    return;
  }
  const auto scale = std::sqrt(static_cast<double>(kTransformProxyMaxPixels) / static_cast<double>(source_area));
  const QSize proxy_size(std::max(1, static_cast<int>(std::lround(source.width() * scale))),
                         std::max(1, static_cast<int>(std::lround(source.height() * scale))));
  transform_proxy_image_ = source.scaled(proxy_size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                               .convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

// The clipped members' own composite over transparency (members unclipped on
// a copy-on-write document copy, every other leaf hidden), over their extent
// on the canvas, built once per session at the first clip-group drag frame.
// Bounded like the transform proxy. False when there is nothing to draw.
bool CanvasWidget::ensure_transform_clip_group_image() {
  if (!transform_clip_group_image_.isNull()) {
    return true;
  }
  if (document_ == nullptr || !transform_layer_id_.has_value()) {
    return false;
  }
  const auto* base = std::as_const(*document_).find_layer(*transform_layer_id_);
  if (base == nullptr) {
    return false;
  }
  const auto members = clip_run_members(*document_, *base);
  Document unclipped(*document_);
  std::vector<LayerId> member_ids;
  QRect rect;
  for (const auto* member : members) {
    if (!member->visible()) {
      continue;
    }
    member_ids.push_back(member->id());
    rect = rect.united(to_qrect(layer_bounds_with_effects(*member, layer_pixel_bounds(*member))));
    if (auto* copy = unclipped.find_layer(member->id()); copy != nullptr) {
      copy->set_clipped(false);
    }
  }
  rect = rect.intersected(QRect(0, 0, document_->width(), document_->height()));
  if (member_ids.empty() || rect.isEmpty()) {
    return false;
  }
  std::vector<LayerId> hidden;
  const std::function<void(const std::vector<Layer>&)> collect = [&](const std::vector<Layer>& layers) {
    for (const auto& layer : layers) {
      if (layer.kind() == LayerKind::Group) {
        collect(layer.children());
      } else if (std::find(member_ids.begin(), member_ids.end(), layer.id()) == member_ids.end()) {
        hidden.push_back(layer.id());
      }
    }
  };
  collect(std::as_const(unclipped).layers());
  auto image = qimage_from_document_rect_with_hidden_layers_banded(unclipped, rect, true, hidden)
                   .convertToFormat(QImage::Format_ARGB32_Premultiplied);
  if (image.isNull()) {
    return false;
  }
  const auto area = static_cast<std::int64_t>(image.width()) * static_cast<std::int64_t>(image.height());
  if (area > kTransformProxyMaxPixels) {
    const auto scale = std::sqrt(static_cast<double>(kTransformProxyMaxPixels) / static_cast<double>(area));
    image = image.scaled(std::max(1, static_cast<int>(std::lround(image.width() * scale))),
                         std::max(1, static_cast<int>(std::lround(image.height() * scale))), Qt::IgnoreAspectRatio,
                         Qt::SmoothTransformation);
  }
  transform_clip_group_image_ = std::move(image);
  transform_clip_group_rect_ = rect;
  transform_proxy_layer_opacity_ = static_cast<double>(base->opacity()) * static_cast<double>(base->fill_opacity());
  return true;
}

// Paints the clip-group drag preview: the transformed source at the base's
// opacity times Fill into an offscreen image over its widget extent, the
// members' snapshot drawn source-atop (members over the base color, inside the
// base's coverage: the compositor's frozen clip merge), then the ensemble over
// the base cache. Per frame the work is two blits over the transformed extent.
void CanvasWidget::draw_transform_clip_group_preview(QPainter& painter) const {
  const auto& source = transform_preview_source_image();
  if (source.isNull()) {
    return;
  }
  const QTransform document_to_widget(zoom_, 0.0, 0.0, zoom_, pan_.x(), pan_.y());
  const auto source_to_widget = transform_session_image_to_document(source) * document_to_widget;
  const auto target =
      source_to_widget.mapRect(QRectF(source.rect())).toAlignedRect().adjusted(-1, -1, 1, 1).intersected(rect());
  if (target.isEmpty()) {
    return;
  }
  const auto device_ratio = painter.device() != nullptr ? painter.device()->devicePixelRatioF() : 1.0;
  QImage group(target.size() * device_ratio, QImage::Format_ARGB32_Premultiplied);
  group.setDevicePixelRatio(device_ratio);
  group.fill(Qt::transparent);
  {
    QPainter group_painter(&group);
    group_painter.setRenderHint(QPainter::SmoothPixmapTransform,
                                transform_interpolation_ != TransformInterpolation::NearestNeighbor);
    group_painter.translate(-target.topLeft());
    group_painter.save();
    group_painter.setOpacity(transform_proxy_layer_opacity_);
    group_painter.setTransform(source_to_widget, true);
    group_painter.drawImage(QRectF(source.rect()), source, QRectF(source.rect()));
    group_painter.restore();
    group_painter.setCompositionMode(QPainter::CompositionMode_SourceAtop);
    group_painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    group_painter.drawImage(widget_rect_for_document_rect(QRectF(transform_clip_group_rect_)),
                            transform_clip_group_image_, QRectF(transform_clip_group_image_.rect()));
  }
  painter.drawImage(target.topLeft(), group);
}

// Drag-time preview refresh: heavy composited previews latch onto the bounded
// proxy blit for the rest of the drag (the accurate patches come back with the
// release-time refresh); everything else keeps the full-quality path. Only the
// three update_free_transform_preview branches route here, so numeric edits,
// nudges, interpolation changes, and Layer Style live edits can never latch.
void CanvasWidget::refresh_transform_preview_for_drag() {
  if (!transform_targets_.empty()) {
    // Multi-target drags always blit the subtree snapshot (the move-proxy
    // contract). Setting the latch flag routes the shared release path through
    // the accurate patch refresh (with the overlay wait) once the drag ends;
    // the single-path proxy diagnostics counter is deliberately not bumped.
    transform_drag_uses_proxy_preview_ = true;
    transform_preview_patches_.clear();
    transform_preview_patches_rect_ = QRect();
    return;
  }
  if (dragging_transform_ && transform_requires_composited_preview_ && transform_clip_group_preview_possible_ &&
      ensure_transform_clip_group_image()) {
    // GitHub #398: a clipping base blits itself and atop-composites the
    // members' snapshot in paint; the release renders the accurate patches.
    if (!transform_drag_uses_clip_group_preview_) {
      transform_drag_uses_clip_group_preview_ = true;
      ++render_cache_diagnostics_.transform_clip_group_previews;
    }
    transform_preview_patches_.clear();
    transform_preview_patches_rect_ = QRect();
    return;
  }
  if (dragging_transform_ && transform_requires_composited_preview_) {
    if (!transform_drag_uses_proxy_preview_ && transform_drag_should_use_proxy_preview()) {
      transform_drag_uses_proxy_preview_ = true;
      ++render_cache_diagnostics_.transform_proxy_previews;
      ensure_transform_proxy_image();
    }
    if (transform_drag_uses_proxy_preview_) {
      // Re-cleared every move so a mid-drag external refresh cannot leave a
      // stale composited patch under the proxy.
      transform_preview_patches_.clear();
      transform_preview_patches_rect_ = QRect();
      return;
    }
  }
  refresh_transform_composited_preview_cache();
}

// Document rect the active transform preview draws into: the rotated quad's
// bounding box (the cheap blit and the controls), unioned with the composited
// patches when present.
QRect CanvasWidget::transform_preview_document_rect() const {
  const auto center = transform_current_rect_.center();
  QTransform rotation;
  rotation.translate(center.x(), center.y());
  rotation.rotate(transform_angle_);
  rotation.translate(-center.x(), -center.y());
  auto rect = rotation.mapRect(transform_current_rect_).toAlignedRect().adjusted(-1, -1, 1, 1);
  if (transforming_layer_ && transform_is_distorted()) {
    const auto quad = transform_quad_for_offsets(transform_corner_offsets_);
    rect = QPolygonF({quad[0], quad[1], quad[2], quad[3]}).boundingRect().toAlignedRect().adjusted(-1, -1, 1, 1);
  }
  if (!transform_preview_patches_rect_.isEmpty()) {
    rect = rect.united(transform_preview_patches_rect_);
  }
  if (!transform_targets_.empty() && !transform_multi_snapshot_rect_.isEmpty()) {
    // The multi drag blit covers the delta-mapped snapshot rect (effects spill
    // included), which the rotated box alone does not.
    const auto delta = transform_session_delta();
    rect = rect.united(delta.mapRect(QRectF(transform_multi_snapshot_rect_)).toAlignedRect().adjusted(-1, -1, 1, 1));
  }
  // The path overlay of a transformed shape layer rides the same delta.
  if (const auto overlay = path_overlay_preview_document_rect(); !overlay.isEmpty()) {
    rect = rect.united(overlay.toAlignedRect());
  }
  return rect;
}

// Bounded repaint for a preview change: old quad/patches area plus the new
// one, padded for the handles, dashed outline, and the rotate stem that draw
// in widget space around the quad. The full-widget update() this replaces
// repainted (and at zoom < 1 re-downscaled) the entire canvas per mouse-move.
void CanvasWidget::update_transform_preview_region(QRect previous_document_rect) {
  const auto current_document_rect = transform_preview_document_rect();
  QRect dirty;
  if (!previous_document_rect.isEmpty()) {
    dirty = dirty.united(widget_rect_for_document_rect(QRectF(previous_document_rect)).toAlignedRect());
  }
  if (!current_document_rect.isEmpty()) {
    dirty = dirty.united(widget_rect_for_document_rect(QRectF(current_document_rect)).toAlignedRect());
  }
  if (dirty.isEmpty()) {
    update();
    return;
  }
  update(dirty.adjusted(-48, -48, 48, 48));
}

void CanvasWidget::refresh_free_transform_preview_caches() {
  if (!transforming_layer_ || document_ == nullptr) {
    return;
  }
  if (!transform_targets_.empty()) {
    if (transform_targets_.front().source_image.isNull()) {
      return;  // session not prepared yet; nothing baked to refresh
    }
    // Live Layer Style edits can change any member: rebuild the base, drop and
    // re-render the subtree snapshot, and re-render the accurate patches.
    rebuild_transform_base_cache();
    transform_multi_snapshot_ = QImage();
    transform_multi_snapshot_rect_ = QRect();
    transform_multi_snapshot_scale_level_ = 0;
    transform_multi_snapshot_clip_.reset();
    ensure_transform_multi_snapshot();
    refresh_transform_multi_preview_cache(false);
    if (isVisible()) {
      update();
    }
    return;
  }
  if (!transform_layer_id_.has_value() || transform_source_image_.isNull()) {
    return;
  }
  auto* layer = document_->find_layer(*transform_layer_id_);
  if (layer == nullptr) {
    return;
  }
  // The Layer Style dialog previews edits live while a transform can still be
  // active, so the snapshots baked at transform start (and whether the preview
  // needs compositing at all) must be rebuilt from the current document state.
  // The base cache rebuilds in BOTH regimes: the composited preview now draws
  // patches over it instead of a full-canvas recomposite.
  transform_requires_composited_preview_ =
      transform_floating_.has_value() || transform_preview_needs_compositing(*document_, *layer);
  transform_clip_group_preview_possible_ =
      !transform_floating_.has_value() && transform_clip_group_preview_possible(*document_, *layer);
  transform_clip_group_image_ = QImage();  // a member's live style edit changes the snapshot
  transform_clip_group_rect_ = QRect();
  rebuild_transform_base_cache();
  refresh_transform_composited_preview_cache();
  if (isVisible()) {
    update();
  }
}

bool CanvasWidget::free_transform_active() const noexcept {
  return transforming_layer_;
}

QTransform CanvasWidget::free_transform_preview_delta() const {
  if (!transforming_layer_) {
    return {};
  }
  return transform_session_delta();
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
  if (transforming_layer_) {
    // The marker moves to the new pivot; repaint the box without waiting for a drag.
    update_transform_preview_region(transform_preview_document_rect());
  }
  notify_transform_controls_changed();
}

CanvasAnchor CanvasWidget::transform_reference_point() const noexcept {
  return transform_reference_point_;
}

void CanvasWidget::set_shift_keeps_transform_aspect(bool enabled) noexcept {
  shift_keeps_transform_aspect_ = enabled;
}

bool CanvasWidget::shift_keeps_transform_aspect() const noexcept {
  return shift_keeps_transform_aspect_;
}

void CanvasWidget::set_show_transform_drag_values(bool enabled) noexcept {
  show_transform_drag_values_ = enabled;
}

bool CanvasWidget::show_transform_drag_values() const noexcept {
  return show_transform_drag_values_;
}

void CanvasWidget::set_snap_transforms_to_pixel_grid(bool enabled) noexcept {
  snap_transforms_to_pixel_grid_ = enabled;
}

bool CanvasWidget::snap_transforms_to_pixel_grid() const noexcept {
  return snap_transforms_to_pixel_grid_;
}

std::optional<CanvasWidget::DragReadout> CanvasWidget::transform_drag_readout() const {
  if (!show_transform_drag_values_) {
    return std::nullopt;
  }
  DragReadout readout;
  if (dragging_guide_) {
    // Guide drag: the guide's position in the ruler unit, measured the way the
    // ruler along its axis measures it. Nothing while the drop would remove it.
    if (document_ == nullptr || guide_drag_remove_) {
      return std::nullopt;
    }
    const bool vertical = guide_drag_orientation_ == GuideOrientation::Vertical;
    const auto pixels = static_cast<double>(guide_drag_position_32_) / 32.0;
    const auto value = pixels / std::max(ruler_pixels_per_unit(vertical), 1e-9);
    // Guides sit on 1/32 px steps: whole pixels print plainly, fractions with two places.
    const auto decimals = ruler_unit_ == MeasurementUnit::Pixels
                              ? (guide_drag_position_32_ % 32 == 0 ? 0 : 2)
                              : measurement_unit_decimals(ruler_unit_);
    const auto position = format_measurement(value, ruler_unit_, decimals);
    readout.lines << (vertical ? tr("Guide X: %1") : tr("Guide Y: %1")).arg(position);
    readout.canvas_lines << (vertical ? tr("X: %1") : tr("Y: %1")).arg(position);
    return readout;
  }
  if (moving_layer_ && move_readout_base_rect_.has_value()) {
    // Move drag: the reference point of the moving set's box, plus the delta.
    const auto rect = move_readout_base_rect_->translated(QPointF(move_preview_delta_));
    const auto position = rect.center() + anchor_offset_from_center(rect.size(), transform_reference_point_);
    readout.lines << tr("X: %1  Y: %2").arg(format_pixels(position.x()), format_pixels(position.y()));
    readout.lines << tr("Change X: %1  Y: %2")
                         .arg(format_pixels(move_preview_delta_.x(), 0, true),
                              format_pixels(move_preview_delta_.y(), 0, true));
    // The canvas panel keeps the artwork clear: only the offset, no label.
    readout.canvas_lines << tr("X: %1  Y: %2")
                                .arg(format_pixels(move_preview_delta_.x(), 0, true),
                                     format_pixels(move_preview_delta_.y(), 0, true));
    return readout;
  }
  if (!dragging_transform_ || !transforming_layer_) {
    return std::nullopt;
  }
  switch (transform_drag_handle_) {
    case TransformHandle::Move: {
      const auto position = transform_reference_position(transform_current_rect_, transform_angle_);
      const auto delta = transform_current_rect_.center() - transform_drag_start_rect_.center();
      readout.lines << tr("X: %1  Y: %2").arg(format_pixels(position.x(), 1), format_pixels(position.y(), 1));
      readout.lines << tr("Change X: %1  Y: %2").arg(format_pixels(delta.x(), 1, true), format_pixels(delta.y(), 1, true));
      readout.canvas_lines << tr("X: %1  Y: %2").arg(format_pixels(delta.x(), 1, true), format_pixels(delta.y(), 1, true));
      return readout;
    }
    case TransformHandle::Rotate:
      readout.lines << tr("Angle: %1").arg(format_degrees(transform_angle_));
      readout.lines << tr("Change: %1").arg(format_degrees(transform_angle_ - transform_start_angle_, 1, true));
      readout.canvas_lines << format_degrees(transform_angle_ - transform_start_angle_, 1, true);
      return readout;
    case TransformHandle::None:
      return std::nullopt;
    default: {
      // The options bar's W and H (a distortion included, GitHub issue 353).
      const auto original_width = std::max(1.0, transform_original_rect_.width());
      const auto original_height = std::max(1.0, transform_original_rect_.height());
      const auto distortion = transform_distortion_scale_factors();
      const auto width = transform_current_rect_.width() * distortion.x();
      const auto height = transform_current_rect_.height() * distortion.y();
      readout.lines << tr("W: %1  H: %2").arg(format_pixels(std::abs(width), 1), format_pixels(std::abs(height), 1));
      const auto percentages = tr("%1 x %2")
                                   .arg(format_percent(transform_scale_x_sign_ * width / original_width * 100.0),
                                        format_percent(transform_scale_y_sign_ * height / original_height * 100.0));
      readout.lines << percentages;
      readout.canvas_lines << percentages;
      return readout;
    }
  }
  return std::nullopt;
}

std::optional<QRectF> CanvasWidget::moving_layers_readout_base_rect() const {
  std::optional<QRectF> base;
  for (const auto& moving_layer : moving_layers_) {
    const auto bounds = moving_layer.original_opaque_bounds.value_or(moving_layer.original_bounds);
    if (bounds.width <= 0 || bounds.height <= 0) {
      continue;
    }
    const QRectF rect(bounds.x, bounds.y, bounds.width, bounds.height);
    base = base.has_value() ? base->united(rect) : rect;
  }
  return base;
}

bool CanvasWidget::transform_drag_keeps_aspect(Qt::KeyboardModifiers modifiers) const noexcept {
  // Shift always selects whichever mode the preference does not, so the two
  // states stay reachable however the preference is set.
  return ((modifiers & Qt::ShiftModifier) != 0) == shift_keeps_transform_aspect_;
}

bool CanvasWidget::transform_drag_scales_about_reference(Qt::KeyboardModifiers modifiers) const noexcept {
  return (modifiers & Qt::AltModifier) != 0;
}

std::optional<CanvasWidget::TransformControlsState> CanvasWidget::transform_controls_state() const {
  std::optional<QRectF> rect;
  QRectF original_rect;
  double angle = 0.0;
  QPointF distortion(1.0, 1.0);
  const bool active = transforming_layer_;
  if (active) {
    rect = transform_current_rect_;
    original_rect = transform_original_rect_;
    angle = transform_angle_;
    distortion = transform_distortion_scale_factors();
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
      (active ? transform_scale_x_sign_ : 1.0) * (rect->width() / original_rect.width()) * distortion.x() * 100.0,
      (active ? transform_scale_y_sign_ : 1.0) * (rect->height() / original_rect.height()) * distortion.y() * 100.0,
      angle,
      transform_interpolation_,
      original_rect.size(),
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
  const auto step_before = current_transform_session_step();
  // The fields show the distorted shape's W and H (transform_controls_state);
  // the box takes them without the distortion's own share.
  const auto distortion = transform_distortion_scale_factors();
  scale_x_percent /= distortion.x();
  scale_y_percent /= distortion.y();

  const auto scale_x_sign = transform_scale_sign(scale_x_percent, transform_scale_x_sign_);
  const auto scale_y_sign = transform_scale_sign(scale_y_percent, transform_scale_y_sign_);
  const auto width = std::max(1.0, transform_original_rect_.width() *
                                       std::max(kMinimumTransformScalePercent, std::abs(scale_x_percent)) / 100.0);
  const auto height = std::max(1.0, transform_original_rect_.height() *
                                        std::max(kMinimumTransformScalePercent, std::abs(scale_y_percent)) / 100.0);
  const auto anchor_offset =
      rotate_offset(anchor_offset_from_center(QSizeF(width, height), transform_reference_point_), rotation_degrees);
  const auto center = reference_position - anchor_offset;
  const auto previous_preview_rect = transform_preview_document_rect();
  transform_current_rect_ = QRectF(center.x() - width / 2.0, center.y() - height / 2.0, width, height);
  if (snap_transforms_to_pixel_grid_ && std::abs(rotation_degrees) <= kPixelGridSnapAngleTolerance) {
    // Typed fractions (and unit conversions such as 1 cm at 300 ppi) snap the way Photoshop
    // does; the options bar re-reads the snapped rect, so the field shows what was applied.
    // Integer inputs snap to themselves, which keeps every pinned commit byte-identical.
    transform_current_rect_ = snap_transform_rect_to_pixel_grid(transform_current_rect_);
  }
  transform_scale_x_sign_ = scale_x_sign;
  transform_scale_y_sign_ = scale_y_sign;
  transform_angle_ = rotation_degrees;
  record_transform_session_step(step_before);
  refresh_transform_composited_preview_cache();
  update_transform_preview_region(previous_preview_rect);
  notify_transform_controls_changed();
  return true;
}

CanvasWidget::TransformSessionStep CanvasWidget::current_transform_session_step() const {
  TransformSessionStep step;
  if (warping_layer_) {
    step.mesh = warp_mesh_;
    step.warp_style = warp_style_;
    step.warp_style_value = warp_style_value_;
  } else {
    step.rect = transform_current_rect_;
    step.angle = transform_angle_;
    step.scale_x_sign = transform_scale_x_sign_;
    step.scale_y_sign = transform_scale_y_sign_;
    step.corner_offsets = transform_corner_offsets_;
  }
  return step;
}

bool CanvasWidget::transform_session_steps_equal(const TransformSessionStep& a, const TransformSessionStep& b) {
  return a.rect == b.rect && a.angle == b.angle && a.scale_x_sign == b.scale_x_sign &&
         a.scale_y_sign == b.scale_y_sign && a.corner_offsets == b.corner_offsets && a.mesh.u_order == b.mesh.u_order && a.mesh.v_order == b.mesh.v_order &&
         a.mesh.xs == b.mesh.xs && a.mesh.ys == b.mesh.ys && a.warp_style == b.warp_style &&
         a.warp_style_value == b.warp_style_value;
}

void CanvasWidget::record_transform_session_step(const TransformSessionStep& before, bool nudge) {
  if (!transforming_layer_ && !warping_layer_) {
    return;
  }
  if (transform_session_steps_equal(before, current_transform_session_step())) {
    return;
  }
  transform_session_redo_steps_.clear();
  if (!(nudge && transform_session_last_step_was_nudge_ && !transform_session_undo_steps_.empty())) {
    transform_session_undo_steps_.push_back(before);
  }
  transform_session_last_step_was_nudge_ = nudge;
}

void CanvasWidget::restore_transform_session_step(const TransformSessionStep& step) {
  transform_session_last_step_was_nudge_ = false;
  if (warping_layer_) {
    warp_mesh_ = step.mesh;
    warp_style_ = step.warp_style;
    warp_style_value_ = step.warp_style_value;
    refresh_warp_preview_cache();
    update();
    notify_transform_controls_changed();
    return;
  }
  if (!prepare_free_transform_source()) {
    return;
  }
  // The numeric-edit refresh: full-quality patches over the old-union-new region.
  const auto previous_preview_rect = transform_preview_document_rect();
  transform_current_rect_ = step.rect;
  transform_angle_ = step.angle;
  transform_scale_x_sign_ = step.scale_x_sign;
  transform_scale_y_sign_ = step.scale_y_sign;
  transform_corner_offsets_ = step.corner_offsets;
  refresh_transform_composited_preview_cache();
  update_transform_preview_region(previous_preview_rect);
  update();
  notify_transform_controls_changed();
}

void CanvasWidget::clear_transform_session_steps() {
  transform_session_undo_steps_.clear();
  transform_session_redo_steps_.clear();
  transform_session_last_step_was_nudge_ = false;
}

bool CanvasWidget::undo_transform_session_step() {
  if ((!transforming_layer_ && !warping_layer_) || dragging_transform_ || dragging_warp_handle_ ||
      transform_session_undo_steps_.empty()) {
    return false;
  }
  auto step = std::move(transform_session_undo_steps_.back());
  transform_session_undo_steps_.pop_back();
  transform_session_redo_steps_.push_back(current_transform_session_step());
  restore_transform_session_step(step);
  return true;
}

bool CanvasWidget::redo_transform_session_step() {
  if ((!transforming_layer_ && !warping_layer_) || dragging_transform_ || dragging_warp_handle_ ||
      transform_session_redo_steps_.empty()) {
    return false;
  }
  auto step = std::move(transform_session_redo_steps_.back());
  transform_session_redo_steps_.pop_back();
  transform_session_undo_steps_.push_back(current_transform_session_step());
  restore_transform_session_step(step);
  return true;
}

int CanvasWidget::transform_session_undo_step_count() const noexcept {
  return static_cast<int>(transform_session_undo_steps_.size());
}

int CanvasWidget::transform_session_redo_step_count() const noexcept {
  return static_cast<int>(transform_session_redo_steps_.size());
}

bool CanvasWidget::transform_session_has_changes() const {
  if (warping_layer_) {
    return warp_entry_changed_ || warp_mesh_.xs != warp_original_mesh_.xs || warp_mesh_.ys != warp_original_mesh_.ys;
  }
  if (transforming_layer_) {
    return transform_has_pending_warp_ || transform_current_rect_ != transform_original_rect_ ||
           transform_angle_ != 0.0 || transform_scale_x_sign_ != 1.0 || transform_scale_y_sign_ != 1.0 ||
           transform_is_distorted();
  }
  return false;
}

bool CanvasWidget::transform_is_distorted() const noexcept {
  return std::any_of(transform_corner_offsets_.begin(), transform_corner_offsets_.end(),
                     [](const QPointF& offset) { return offset.x() != 0.0 || offset.y() != 0.0; });
}

QTransform CanvasWidget::transform_affine_delta() const {
  return free_transform_delta(transform_original_rect_, transform_current_rect_, transform_angle_,
                              transform_scale_x_sign_, transform_scale_y_sign_);
}

std::array<QPointF, 4> CanvasWidget::transform_original_corners() const {
  const auto& rect = transform_original_rect_;
  return {QPointF(rect.x(), rect.y()), QPointF(rect.x() + rect.width(), rect.y()),
          QPointF(rect.x() + rect.width(), rect.y() + rect.height()), QPointF(rect.x(), rect.y() + rect.height())};
}

QPointF CanvasWidget::transform_distortion_scale_factors() const noexcept {
  // GitHub issue 353: Distort and Perspective change the shape without the box, so
  // W and H read the quad. The offsets sit in the original box's frame, so the
  // factors are linear in the box scale and a typed W or H round-trips exactly.
  // A skew slides sides along themselves and keeps both at 1 (Photoshop's W and H
  // do not move for a skew either).
  const auto& o = transform_corner_offsets_;
  const auto width = transform_original_rect_.width();
  const auto height = transform_original_rect_.height();
  if (width <= 0.0 || height <= 0.0) {
    return {1.0, 1.0};
  }
  const auto factor = [](double value) {
    // Never zero: the setter divides by it. A convex quad keeps opposite sides apart.
    constexpr double kMinimum = 1e-4;
    return std::abs(value) < kMinimum ? std::copysign(kMinimum, value) : value;
  };
  const auto x = (width + ((o[1].x() - o[0].x()) + (o[2].x() - o[3].x())) / 2.0) / width;
  const auto y = (height + ((o[3].y() - o[0].y()) + (o[2].y() - o[1].y())) / 2.0) / height;
  return {factor(x), factor(y)};
}

std::array<QPointF, 4> CanvasWidget::transform_quad_for_offsets(const std::array<QPointF, 4>& offsets) const {
  const auto affine = transform_affine_delta();
  const auto corners = transform_original_corners();
  std::array<QPointF, 4> quad{};
  for (std::size_t i = 0; i < quad.size(); ++i) {
    quad[i] = affine.map(corners[i] + offsets[i]);
  }
  return quad;
}

QTransform CanvasWidget::transform_session_delta() const {
  const auto affine = transform_affine_delta();
  if (!transform_is_distorted()) {
    return affine;
  }
  const auto corners = transform_original_corners();
  const auto quad = transform_quad_for_offsets(transform_corner_offsets_);
  QTransform homography;
  if (!QTransform::quadToQuad(QPolygonF({corners[0], corners[1], corners[2], corners[3]}),
                              QPolygonF({quad[0], quad[1], quad[2], quad[3]}), homography)) {
    return affine;
  }
  return homography;
}

QTransform CanvasWidget::transform_session_source_to_document(QSize source_size) const {
  if (!transform_is_distorted()) {
    return transform_source_to_document(source_size, transform_current_rect_, transform_angle_,
                                        transform_scale_x_sign_, transform_scale_y_sign_);
  }
  // The source image spans the original box exactly (the undistorted map at the
  // session start), so source -> original box, then the session homography.
  QTransform source_to_original;
  source_to_original.translate(transform_original_rect_.x(), transform_original_rect_.y());
  source_to_original.scale(std::max(1.0, transform_original_rect_.width()) / std::max(1, source_size.width()),
                           std::max(1.0, transform_original_rect_.height()) / std::max(1, source_size.height()));
  return source_to_original * transform_session_delta();
}

QTransform CanvasWidget::transform_session_image_to_document(const QImage& image) const {
  const auto source_size = transform_source_image_.size();
  return QTransform::fromScale(static_cast<double>(source_size.width()) / std::max(1, image.width()),
                               static_cast<double>(source_size.height()) / std::max(1, image.height())) *
         transform_session_source_to_document(source_size);
}

int CanvasWidget::transform_corner_index_for_handle(TransformHandle handle) const noexcept {
  // Handles sit on the box's on-screen corners; a flip puts the layer's own
  // opposite corner there.
  bool local_left = false;
  bool local_top = false;
  switch (handle) {
    case TransformHandle::TopLeft:
      local_left = true;
      local_top = true;
      break;
    case TransformHandle::TopRight:
      local_top = true;
      break;
    case TransformHandle::BottomRight:
      break;
    case TransformHandle::BottomLeft:
      local_left = true;
      break;
    default:
      return -1;
  }
  const bool left = (transform_scale_x_sign_ > 0.0) == local_left;
  const bool top = (transform_scale_y_sign_ > 0.0) == local_top;
  return top ? (left ? 0 : 1) : (left ? 3 : 2);
}

std::pair<CanvasWidget::TransformHandle, CanvasWidget::TransformHandle> CanvasWidget::transform_edge_corner_handles(
    TransformHandle edge) {
  using Handle = TransformHandle;
  switch (edge) {
    case Handle::Top:
      return {Handle::TopLeft, Handle::TopRight};
    case Handle::Right:
      return {Handle::TopRight, Handle::BottomRight};
    case Handle::Bottom:
      return {Handle::BottomRight, Handle::BottomLeft};
    case Handle::Left:
      return {Handle::BottomLeft, Handle::TopLeft};
    default:
      return {Handle::None, Handle::None};
  }
}

bool CanvasWidget::transform_handle_is_corner(TransformHandle handle) noexcept {
  using Handle = TransformHandle;
  return handle == Handle::TopLeft || handle == Handle::TopRight || handle == Handle::BottomRight ||
         handle == Handle::BottomLeft;
}

bool CanvasWidget::transform_handle_is_edge(TransformHandle handle) noexcept {
  using Handle = TransformHandle;
  return handle == Handle::Top || handle == Handle::Right || handle == Handle::Bottom || handle == Handle::Left;
}

namespace {

// A parallelogram is an affine image of the box (a skew); anything else needs
// a perspective map.
bool quad_is_parallelogram(const std::array<QPointF, 4>& quad) {
  const auto a = quad[0] + quad[2];
  const auto b = quad[1] + quad[3];
  const auto scale = std::max({1.0, std::abs(quad[0].x()), std::abs(quad[0].y()), std::abs(quad[2].x()),
                               std::abs(quad[2].y())});
  return std::abs(a.x() - b.x()) <= 1e-6 * scale && std::abs(a.y() - b.y()) <= 1e-6 * scale;
}

// +1 or -1 for a strictly convex quad (the winding of its corners), 0 for a concave,
// self-crossing or collapsed one. A projective map onto anything but a convex quad
// sends part of the image across the horizon, far outside the quad.
int convex_quad_winding(const std::array<QPointF, 4>& quad) {
  double scale = 1.0;
  for (const auto& corner : quad) {
    scale = std::max({scale, std::abs(corner.x()), std::abs(corner.y())});
  }
  int winding = 0;
  for (std::size_t i = 0; i < quad.size(); ++i) {
    const auto a = quad[i];
    const auto b = quad[(i + 1U) % quad.size()];
    const auto c = quad[(i + 2U) % quad.size()];
    const auto cross = (b.x() - a.x()) * (c.y() - b.y()) - (b.y() - a.y()) * (c.x() - b.x());
    if (std::abs(cross) <= 1e-9 * scale * scale) {
      return 0;
    }
    const int sign = cross > 0.0 ? 1 : -1;
    if (winding == 0) {
      winding = sign;
    } else if (sign != winding) {
      return 0;
    }
  }
  return winding;
}

double segment_distance(QPointF point, QPointF a, QPointF b, double* parameter) {
  const auto ab = b - a;
  const auto length_squared = ab.x() * ab.x() + ab.y() * ab.y();
  if (length_squared < 1e-12) {
    *parameter = 0.0;
    return std::hypot(point.x() - a.x(), point.y() - a.y());
  }
  const auto ap = point - a;
  *parameter = (ap.x() * ab.x() + ap.y() * ab.y()) / length_squared;
  return std::abs(ap.x() * ab.y() - ap.y() * ab.x()) / std::sqrt(length_squared);
}

}  // namespace

QPointF CanvasWidget::distorted_transform_handle_position(TransformHandle handle) const {
  const auto quad = transform_quad_for_offsets(transform_corner_offsets_);
  const auto corner_at = [&](TransformHandle corner) {
    return widget_position_f(quad[static_cast<std::size_t>(transform_corner_index_for_handle(corner))]);
  };
  if (transform_handle_is_corner(handle)) {
    return corner_at(handle);
  }
  if (transform_handle_is_edge(handle)) {
    const auto [first, second] = transform_edge_corner_handles(handle);
    return (corner_at(first) + corner_at(second)) / 2.0;
  }
  if (handle == TransformHandle::Rotate) {
    // The stem leaves the top edge's middle along the box's own "up".
    const auto up = rotate_offset(QPointF(0.0, -1.0), transform_angle_);
    return distorted_transform_handle_position(TransformHandle::Top) + up * 32.0;
  }
  QPointF centroid;
  for (const auto& corner : quad) {
    centroid += widget_position_f(corner);
  }
  return centroid / 4.0;
}

double CanvasWidget::distorted_transform_handle_direction(TransformHandle handle) const {
  return transform_quad_handle_direction({distorted_transform_handle_position(TransformHandle::TopLeft),
                                          distorted_transform_handle_position(TransformHandle::TopRight),
                                          distorted_transform_handle_position(TransformHandle::BottomRight),
                                          distorted_transform_handle_position(TransformHandle::BottomLeft)},
                                         handle);
}

CanvasWidget::TransformHandle CanvasWidget::distorted_transform_handle_at(QPoint widget_point) const {
  return quad_handle_at({distorted_transform_handle_position(TransformHandle::TopLeft),
                         distorted_transform_handle_position(TransformHandle::TopRight),
                         distorted_transform_handle_position(TransformHandle::BottomRight),
                         distorted_transform_handle_position(TransformHandle::BottomLeft)},
                        distorted_transform_handle_position(TransformHandle::Rotate), QPointF(widget_point));
}

QPointF CanvasWidget::quad_handle_position(const std::array<QPointF, 4>& corners, TransformHandle handle) {
  switch (handle) {
    case TransformHandle::TopLeft:
      return corners[0];
    case TransformHandle::TopRight:
      return corners[1];
    case TransformHandle::BottomRight:
      return corners[2];
    case TransformHandle::BottomLeft:
      return corners[3];
    case TransformHandle::Top:
    case TransformHandle::Right:
    case TransformHandle::Bottom:
    case TransformHandle::Left: {
      const auto [first, second] = transform_edge_corner_handles(handle);
      return (quad_handle_position(corners, first) + quad_handle_position(corners, second)) / 2.0;
    }
    default:
      return (corners[0] + corners[1] + corners[2] + corners[3]) / 4.0;
  }
}

CanvasWidget::TransformHandle CanvasWidget::quad_handle_at(const std::array<QPointF, 4>& corners,
                                                           std::optional<QPointF> rotate_stem, QPointF point) {
  // The EdgesAndCorners zones (transform_handle_at) on the true quad: a Move core
  // first, then the nearest handle square, the edge bands, the inside, and the
  // rotate band just outside each corner.
  constexpr double kHandleHit = 14.0;
  constexpr double kEdgeGrabOutside = 6.0;
  constexpr double kCornerRotateReach = 28.8;
  const std::array<TransformHandle, 4> corner_handles = {TransformHandle::TopLeft, TransformHandle::TopRight,
                                                         TransformHandle::BottomRight, TransformHandle::BottomLeft};
  const std::array<TransformHandle, 4> edges = {TransformHandle::Top, TransformHandle::Right,
                                                TransformHandle::Bottom, TransformHandle::Left};
  const auto position_of = [&corners, &rotate_stem](TransformHandle handle) {
    return handle == TransformHandle::Rotate ? rotate_stem.value_or(QPointF()) : quad_handle_position(corners, handle);
  };
  QPolygonF polygon;
  double shortest_edge = std::numeric_limits<double>::max();
  for (const auto& corner : corners) {
    polygon << corner;
  }
  for (const auto edge : edges) {
    const auto [first, second] = transform_edge_corner_handles(edge);
    const auto along = position_of(second) - position_of(first);
    shortest_edge = std::min(shortest_edge, std::hypot(along.x(), along.y()));
  }
  const bool inside = polygon.containsPoint(point, Qt::OddEvenFill);
  const auto inner = std::min(kHandleHit / 2.0, shortest_edge / 6.0);
  const auto edge_distance = [&](TransformHandle edge, double* parameter) {
    const auto [first, second] = transform_edge_corner_handles(edge);
    return segment_distance(point, position_of(first), position_of(second), parameter);
  };
  if (inside) {
    bool core = true;
    for (const auto edge : edges) {
      double parameter = 0.0;
      if (edge_distance(edge, &parameter) <= inner) {
        core = false;
      }
    }
    if (core) {
      return TransformHandle::Move;
    }
  }
  auto nearest = TransformHandle::None;
  auto nearest_distance = 0.0;
  for (const auto handle : {TransformHandle::Rotate, TransformHandle::TopLeft, TransformHandle::Top,
                            TransformHandle::TopRight, TransformHandle::Right, TransformHandle::BottomRight,
                            TransformHandle::Bottom, TransformHandle::BottomLeft, TransformHandle::Left}) {
    if (handle == TransformHandle::Rotate && !rotate_stem.has_value()) {
      continue;
    }
    const auto position = position_of(handle);
    if (std::abs(point.x() - position.x()) > kHandleHit / 2.0 || std::abs(point.y() - position.y()) > kHandleHit / 2.0) {
      continue;
    }
    if (handle == TransformHandle::Rotate) {
      return handle;
    }
    const auto distance = std::hypot(point.x() - position.x(), point.y() - position.y());
    if (nearest == TransformHandle::None || distance < nearest_distance) {
      nearest = handle;
      nearest_distance = distance;
    }
  }
  if (nearest != TransformHandle::None) {
    return nearest;
  }
  for (const auto edge : edges) {
    double parameter = 0.0;
    const auto distance = edge_distance(edge, &parameter);
    if (parameter >= 0.0 && parameter <= 1.0 && distance <= (inside ? inner : kEdgeGrabOutside)) {
      return edge;
    }
  }
  if (inside) {
    return TransformHandle::Move;
  }
  for (const auto corner : corner_handles) {
    const auto position = position_of(corner);
    if (std::hypot(point.x() - position.x(), point.y() - position.y()) <= kCornerRotateReach) {
      return TransformHandle::Rotate;
    }
  }
  return TransformHandle::None;
}

void CanvasWidget::draw_distorted_transform_controls(QPainter& painter) const {
  QPolygonF polygon;
  for (const auto corner : {TransformHandle::TopLeft, TransformHandle::TopRight, TransformHandle::BottomRight,
                            TransformHandle::BottomLeft}) {
    polygon << distorted_transform_handle_position(corner);
  }
  painter.save();
  painter.setBrush(Qt::NoBrush);
  painter.setPen(QPen(QColor(95, 170, 255), 1.0, Qt::DashLine));
  painter.drawPolygon(polygon);
  painter.setPen(QPen(QColor(95, 170, 255), 1.0));
  painter.drawLine(distorted_transform_handle_position(TransformHandle::Top),
                   distorted_transform_handle_position(TransformHandle::Rotate));
  constexpr double kHandleSize = 8.0;
  painter.setPen(QPen(QColor(10, 14, 20), 1.0));
  for (const auto handle : {TransformHandle::TopLeft, TransformHandle::Top, TransformHandle::TopRight,
                            TransformHandle::Right, TransformHandle::BottomRight, TransformHandle::Bottom,
                            TransformHandle::BottomLeft, TransformHandle::Left, TransformHandle::Rotate}) {
    const auto point = distorted_transform_handle_position(handle);
    painter.setBrush(handle == TransformHandle::Rotate ? QColor(95, 170, 255) : QColor(245, 248, 252));
    painter.drawRect(QRectF(point.x() - kHandleSize / 2.0, point.y() - kHandleSize / 2.0, kHandleSize, kHandleSize));
  }
  painter.restore();
}

void CanvasWidget::apply_session_transform_cursor(QPoint widget_point, Qt::KeyboardModifiers modifiers) {
  if (!transforming_layer_) {
    return;
  }
  transform_cursor_modifiers_ = modifiers;
  // Over a corner or side whose drag the held modifiers (or the Edit > Transform
  // mode) turn into skew, distort or perspective, show that gesture first.
  if (const auto hovered = transform_handle_at(widget_point);
      transform_handle_is_corner(hovered) || transform_handle_is_edge(hovered)) {
    const auto kind = transform_drag_kind_for(hovered, modifiers);
    const bool refused = transform_handle_is_corner(hovered) && kind != TransformDragKind::Scale &&
                         !free_transform_accepts_non_affine();
    if (!refused && apply_transform_gesture_cursor(kind)) {
      return;
    }
  }
  if (!transform_is_distorted()) {
    set_transform_cursor_at(widget_point, transform_handle_at(widget_point), transform_current_rect_,
                            transform_angle_);
    return;
  }
  const auto handle = transform_handle_for_session_mode(distorted_transform_handle_at(widget_point));
  if (handle == TransformHandle::Rotate) {
    const auto stem = distorted_transform_handle_position(TransformHandle::Rotate);
    if (std::abs(widget_point.x() - stem.x()) <= 7.0 && std::abs(widget_point.y() - stem.y()) <= 7.0) {
      setCursor(rotate_cursor());
      return;
    }
    // The corner the pointer is nearest picks the rotate cursor's orientation.
    auto best = TransformHandle::TopLeft;
    auto best_distance = std::numeric_limits<double>::max();
    for (const auto corner : {TransformHandle::TopLeft, TransformHandle::TopRight, TransformHandle::BottomRight,
                              TransformHandle::BottomLeft}) {
      const auto position = distorted_transform_handle_position(corner);
      const auto distance = std::hypot(widget_point.x() - position.x(), widget_point.y() - position.y());
      if (distance < best_distance) {
        best = corner;
        best_distance = distance;
      }
    }
    setCursor(corner_rotate_cursor(distorted_transform_handle_direction(best)));
    return;
  }
  if (transform_handle_is_corner(handle) || transform_handle_is_edge(handle)) {
    // Right's base direction is 0 degrees, so the handle's own direction picks the shape.
    set_transform_cursor_for_handle(TransformHandle::Right, distorted_transform_handle_direction(handle));
    return;
  }
  set_transform_cursor_for_handle(handle);
}

CanvasWidget::TransformDragKind CanvasWidget::transform_drag_kind_for(TransformHandle handle,
                                                                      Qt::KeyboardModifiers modifiers) const noexcept {
  return transform_drag_kind_for_mode(handle, modifiers, transform_distort_mode_);
}

CanvasWidget::TransformDragKind CanvasWidget::transform_drag_kind_for_mode(TransformHandle handle,
                                                                           Qt::KeyboardModifiers modifiers,
                                                                           TransformDistortMode mode) noexcept {
  const bool control = (modifiers & Qt::ControlModifier) != 0;
  const bool alt = (modifiers & Qt::AltModifier) != 0;
  const bool shift = (modifiers & Qt::ShiftModifier) != 0;
  if (transform_handle_is_corner(handle)) {
    if (control && alt && shift) {
      return TransformDragKind::PerspectiveCorner;
    }
    if (control) {
      return TransformDragKind::DistortCorner;
    }
    switch (mode) {
      case TransformDistortMode::Distort:
        return TransformDragKind::DistortCorner;
      case TransformDistortMode::Perspective:
        return TransformDragKind::PerspectiveCorner;
      case TransformDistortMode::Skew:
        return TransformDragKind::SkewCorner;
      case TransformDistortMode::None:
      case TransformDistortMode::Scale:
      case TransformDistortMode::Rotate:
        break;
    }
    return TransformDragKind::Scale;
  }
  if (transform_handle_is_edge(handle)) {
    if (control) {
      return shift ? TransformDragKind::SlideEdge : TransformDragKind::MoveEdge;
    }
    switch (mode) {
      case TransformDistortMode::Distort:
        return TransformDragKind::MoveEdge;
      case TransformDistortMode::Skew:
      case TransformDistortMode::Perspective:
        return TransformDragKind::SlideEdge;
      case TransformDistortMode::None:
      case TransformDistortMode::Scale:
      case TransformDistortMode::Rotate:
        break;
    }
  }
  return TransformDragKind::Scale;
}

bool CanvasWidget::begin_transform_distort_drag(TransformHandle handle, Qt::KeyboardModifiers modifiers,
                                                QPointF document_press) {
  // Recorded for every drag: a mid-drag gesture switch restarts from it.
  transform_drag_start_corner_offsets_ = transform_corner_offsets_;
  transform_drag_kind_ = transform_drag_kind_for(handle, modifiers);
  if (transform_drag_kind_ == TransformDragKind::Scale) {
    return true;
  }
  // Moving one edge keeps a parallelogram (a skew, affine); moving one corner
  // makes a general quad, which a type layer cannot take.
  if (transform_handle_is_corner(handle) && !free_transform_accepts_non_affine()) {
    transform_drag_kind_ = TransformDragKind::Scale;
    report_status_error(tr("Rasterize the layer to distort it"));
    return false;
  }
  const auto quad = transform_quad_for_offsets(transform_corner_offsets_);
  QPointF grabbed;
  if (transform_handle_is_corner(handle)) {
    grabbed = quad[static_cast<std::size_t>(transform_corner_index_for_handle(handle))];
  } else {
    const auto [first, second] = transform_edge_corner_handles(handle);
    grabbed = (quad[static_cast<std::size_t>(transform_corner_index_for_handle(first))] +
               quad[static_cast<std::size_t>(transform_corner_index_for_handle(second))]) /
              2.0;
  }
  transform_drag_grab_offset_ = grabbed - document_press;
  return true;
}

bool CanvasWidget::retarget_transform_drag(Qt::KeyboardModifiers modifiers) {
  const auto handle = transform_drag_handle_;
  if (!dragging_transform_ || !(transform_handle_is_corner(handle) || transform_handle_is_edge(handle))) {
    return false;
  }
  auto kind = transform_drag_kind_for(handle, modifiers);
  if (kind != TransformDragKind::Scale && transform_handle_is_corner(handle) && !free_transform_accepts_non_affine()) {
    kind = TransformDragKind::Scale;
  }
  if (kind == transform_drag_kind_) {
    return false;
  }
  // Back to the box as it was at the press, then the new gesture from the same
  // press point: holding Ctrl distorts, letting go resizes, with no new press.
  const auto previous_preview_rect = transform_preview_document_rect();
  transform_current_rect_ = transform_drag_start_rect_;
  transform_angle_ = transform_start_angle_;
  transform_scale_x_sign_ = transform_drag_start_scale_x_sign_;
  transform_scale_y_sign_ = transform_drag_start_scale_y_sign_;
  transform_corner_offsets_ = transform_drag_start_corner_offsets_;
  transform_drag_grab_offset_ = transform_handle_grab_offset(handle, transform_drag_start_rect_, transform_start_angle_,
                                                             transform_drag_start_point_);
  begin_transform_distort_drag(handle, modifiers, transform_drag_start_point_);
  refresh_transform_preview_for_drag();
  update_transform_preview_region(previous_preview_rect);
  return true;
}

bool CanvasWidget::apply_move_box_handle_cursor(QPoint widget_point, Qt::KeyboardModifiers modifiers) {
  // Kept for cursor refreshes without an event (apply_move_tool_cursor).
  transform_cursor_modifiers_ = modifiers;
  const auto rect = move_transform_controls_rect();
  if (!rect.has_value()) {
    return false;
  }
  const auto handle = transform_handle_at(widget_point, *rect, 0.0, TransformHitZones::EdgesAndCorners);
  const bool corner = transform_handle_is_corner(handle);
  if (!corner && !transform_handle_is_edge(handle)) {
    return false;
  }
  if ((modifiers & Qt::ControlModifier) != 0 && document_ != nullptr) {
    const auto kind = transform_drag_kind_for_mode(handle, modifiers, TransformDistortMode::None);
    // Type takes no non-affine quad, so a corner there keeps the resize cursor
    // (the press refuses with a status message, as in a Free Transform session).
    bool refused = false;
    if (corner && kind != TransformDragKind::Scale) {
      const auto& document = std::as_const(*document_);
      for (const auto id : movable_layer_ids()) {
        if (const auto* layer = document.find_layer(id); layer != nullptr && layer_is_text(*layer)) {
          refused = true;
          break;
        }
      }
    }
    if (!refused && apply_transform_gesture_cursor(kind)) {
      return true;
    }
  }
  set_transform_cursor_at(widget_point, handle, *rect, 0.0);
  return true;
}

void CanvasWidget::update_free_transform_distort(QPointF document_point, Qt::KeyboardModifiers modifiers) {
  // Alt (read live, like the scale handles' Alt) mirrors the drag about the center:
  // the diagonally opposite corner, or the opposite side, moves the other way.
  const bool symmetric = (modifiers & Qt::AltModifier) != 0;
  const auto previous_preview_rect = transform_preview_document_rect();
  // The box's affine stays fixed during these drags; the pointer travel moves
  // corners in the layer's own frame (offsets), so a later scale or rotate of
  // the box carries the distortion with it.
  bool invertible = false;
  const auto inverse = transform_affine_delta().inverted(&invertible);
  if (!invertible) {
    return;
  }
  const auto start = transform_drag_start_point_ + transform_drag_grab_offset_;
  const auto now = snapped_document_point_f(document_point + transform_drag_grab_offset_);
  const auto delta = inverse.map(now) - inverse.map(start);
  if (transform_drag_kind_ == TransformDragKind::Scale) {
    return;
  }
  const auto [first_handle, second_handle] = transform_edge_corner_handles(transform_drag_handle_);
  const auto offsets = transform_distorted_offsets(
      transform_drag_kind_, transform_corner_index_for_handle(transform_drag_handle_),
      {transform_corner_index_for_handle(first_handle), transform_corner_index_for_handle(second_handle)}, delta,
      symmetric, transform_drag_start_corner_offsets_, transform_original_corners());
  // Never fold the quad: a frame that would make it concave, self-crossing or
  // collapsed, or turn it inside out, keeps the last good shape (the pointer can
  // swing back). A folded quad's homography throws pixels across the horizon, far
  // outside the box and the repaint (GitHub issue 225, Seth's "skew with vigor").
  const auto start_winding = convex_quad_winding(transform_quad_for_offsets(transform_drag_start_corner_offsets_));
  if (const auto winding = convex_quad_winding(transform_quad_for_offsets(offsets));
      winding == 0 || (start_winding != 0 && winding != start_winding)) {
    return;
  }
  transform_corner_offsets_ = offsets;
  refresh_transform_preview_for_drag();
  update_transform_preview_region(previous_preview_rect);
  update_drag_readout_region();
  notify_transform_controls_changed();
}

std::array<QPointF, 4> CanvasWidget::transform_distorted_offsets(TransformDragKind kind, int corner,
                                                                 std::pair<int, int> edge_corners, QPointF delta,
                                                                 bool symmetric,
                                                                 const std::array<QPointF, 4>& start_offsets,
                                                                 const std::array<QPointF, 4>& original_corners) {
  auto offsets = start_offsets;
  const auto at = [&offsets](int index) -> QPointF& { return offsets[static_cast<std::size_t>(index)]; };
  switch (kind) {
    case TransformDragKind::DistortCorner:
      at(corner) += delta;
      if (symmetric) {
        at((corner + 2) % 4) -= delta;
      }
      break;
    case TransformDragKind::SkewCorner:
      // The corner slides along one of its two sides (the one the pointer follows).
      if (std::abs(delta.x()) >= std::abs(delta.y())) {
        at(corner).rx() += delta.x();
      } else {
        at(corner).ry() += delta.y();
      }
      break;
    case TransformDragKind::PerspectiveCorner: {
      // The corner sharing the side moves the opposite way: a symmetric trapezoid.
      static constexpr std::array<int, 4> kHorizontalPartner = {1, 0, 3, 2};
      static constexpr std::array<int, 4> kVerticalPartner = {3, 2, 1, 0};
      if (std::abs(delta.x()) >= std::abs(delta.y())) {
        at(corner).rx() += delta.x();
        at(kHorizontalPartner[static_cast<std::size_t>(corner)]).rx() -= delta.x();
      } else {
        at(corner).ry() += delta.y();
        at(kVerticalPartner[static_cast<std::size_t>(corner)]).ry() -= delta.y();
      }
      break;
    }
    case TransformDragKind::MoveEdge:
    case TransformDragKind::SlideEdge: {
      const auto [first, second] = edge_corners;
      auto travel = delta;
      if (kind == TransformDragKind::SlideEdge) {
        const auto& corners = original_corners;
        const auto along = (corners[static_cast<std::size_t>(second)] + start_offsets[static_cast<std::size_t>(second)]) -
                           (corners[static_cast<std::size_t>(first)] + start_offsets[static_cast<std::size_t>(first)]);
        const auto length_squared = along.x() * along.x() + along.y() * along.y();
        travel = length_squared > 1e-12
                     ? along * ((delta.x() * along.x() + delta.y() * along.y()) / length_squared)
                     : QPointF();
      }
      at(first) += travel;
      at(second) += travel;
      if (symmetric) {
        at((first + 2) % 4) -= travel;
        at((second + 2) % 4) -= travel;
      }
      break;
    }
    case TransformDragKind::Scale:
      break;
  }
  return offsets;
}

int CanvasWidget::transform_quad_winding(const std::array<QPointF, 4>& quad) {
  return convex_quad_winding(quad);
}

double CanvasWidget::transform_quad_handle_direction(const std::array<QPointF, 4>& handle_corners,
                                                     TransformHandle handle) {
  const auto corner_position = [&](TransformHandle corner) {
    switch (corner) {
      case TransformHandle::TopLeft:
        return handle_corners[0];
      case TransformHandle::TopRight:
        return handle_corners[1];
      case TransformHandle::BottomRight:
        return handle_corners[2];
      default:
        return handle_corners[3];
    }
  };
  const auto centroid = (handle_corners[0] + handle_corners[1] + handle_corners[2] + handle_corners[3]) / 4.0;
  QPointF direction;
  if (transform_handle_is_edge(handle)) {
    const auto [first, second] = transform_edge_corner_handles(handle);
    const auto along = corner_position(second) - corner_position(first);
    direction = QPointF(along.y(), -along.x());
    const auto outward = (corner_position(first) + corner_position(second)) / 2.0 - centroid;
    if (direction.x() * outward.x() + direction.y() * outward.y() < 0.0) {
      direction = -direction;
    }
  } else {
    direction = corner_position(handle) - centroid;
  }
  return std::atan2(direction.y(), direction.x()) * 180.0 / kPi;
}

bool CanvasWidget::apply_transform_gesture_cursor(TransformDragKind kind) {
  if (kind == TransformDragKind::Scale) {
    return false;
  }
  setCursor(transform_gesture_cursor(kind == TransformDragKind::SkewCorner || kind == TransformDragKind::SlideEdge
                                         ? TransformGestureCursor::Skew
                                     : kind == TransformDragKind::PerspectiveCorner
                                         ? TransformGestureCursor::Perspective
                                         : TransformGestureCursor::Distort));
  return true;
}

bool CanvasWidget::transform_session_box_contains(QPoint widget_point) const {
  if (!transforming_layer_) {
    return false;
  }
  const auto quad = transform_quad_for_offsets(transform_corner_offsets_);
  QPolygonF polygon;
  for (const auto& corner : quad) {
    polygon << widget_position_f(corner);
  }
  return polygon.containsPoint(QPointF(widget_point), Qt::OddEvenFill);
}

bool CanvasWidget::free_transform_accepts_non_affine() const {
  if (!transforming_layer_ || document_ == nullptr) {
    return false;
  }
  const auto& document = std::as_const(*document_);
  // Type stays editable only under affine maps; vector paths and smart object
  // placements (nonAffineTransform) take a perspective quad. Pixels and masks
  // resample through it.
  const auto accepts = [](const Layer& layer) { return !layer_is_text(layer); };
  if (!transform_targets_.empty()) {
    for (const auto& target : transform_targets_) {
      if (const auto* layer = document.find_layer(target.id); layer != nullptr && !accepts(*layer)) {
        return false;
      }
    }
    for (const auto id : transform_mask_only_ids_) {
      if (const auto* layer = document.find_layer(id); layer != nullptr && layer->vector_mask() != nullptr) {
        return false;
      }
    }
    return true;
  }
  const auto* layer = transform_layer_id_.has_value() ? document.find_layer(*transform_layer_id_) : nullptr;
  return layer != nullptr && accepts(*layer);
}

QString CanvasWidget::apply_scripted_free_transform(
    LayerId layer_id, const std::function<std::array<QPointF, 4>(const std::array<QPointF, 4>& box)>& place,
    std::optional<TransformInterpolation> interpolation) {
  if (document_ == nullptr || transforming_layer_ || warping_layer_ || path_transform_active_) {
    return tr("Select an editable pixel layer to transform");
  }
  // The interactive session, minus its side channels: refusals are collected for the
  // script instead of the status bar, the run's own snapshot replaces the commit's
  // history entry, and the missing-font prompt (a dialog) is skipped.
  QString error;
  const auto saved_error_callback = std::exchange(error_status_callback_, [&error](QString message) {
    if (error.isEmpty()) {
      error = std::move(message);
    }
  });
  const auto saved_status_callback = std::exchange(status_callback_, nullptr);
  const auto saved_before_edit = std::exchange(before_edit_callback_, nullptr);
  const auto saved_missing_font = std::exchange(missing_font_transform_confirm_callback_, nullptr);
  const auto saved_interpolation = transform_interpolation_;
  const auto saved_selection = selected_layer_ids_;
  if (interpolation.has_value()) {
    transform_interpolation_ = *interpolation;
  }
  set_selected_layer_ids({layer_id});
  bool committed = false;
  if (begin_free_transform()) {
    if (set_free_transform_quad(place(free_transform_quad()))) {
      commit_free_transform();
      committed = true;
    } else {
      cancel_free_transform();
    }
  }
  set_selected_layer_ids(saved_selection);
  transform_interpolation_ = saved_interpolation;
  missing_font_transform_confirm_callback_ = saved_missing_font;
  before_edit_callback_ = saved_before_edit;
  status_callback_ = saved_status_callback;
  error_status_callback_ = saved_error_callback;
  if (!committed && error.isEmpty()) {
    error = tr("Select an editable pixel layer to transform");
  }
  return error;
}

bool CanvasWidget::begin_free_transform_mode(TransformDistortMode mode) {
  if (!transforming_layer_ && !begin_free_transform()) {
    return false;
  }
  transform_distort_mode_ = mode;
  notify_transform_controls_changed();
  return true;
}

CanvasWidget::TransformDistortMode CanvasWidget::free_transform_distort_mode() const noexcept {
  return transforming_layer_ ? transform_distort_mode_ : TransformDistortMode::None;
}

std::array<QPointF, 4> CanvasWidget::free_transform_quad() const {
  if (!transforming_layer_) {
    return {};
  }
  return transform_quad_for_offsets(transform_corner_offsets_);
}

bool CanvasWidget::set_free_transform_quad(const std::array<QPointF, 4>& corners) {
  if (!transforming_layer_) {
    return false;
  }
  for (const auto& corner : corners) {
    if (!std::isfinite(corner.x()) || !std::isfinite(corner.y())) {
      return false;
    }
  }
  if (convex_quad_winding(corners) == 0) {
    report_status_error(tr("Transform corners must form a convex shape"));
    return false;
  }
  if (!quad_is_parallelogram(corners) && !free_transform_accepts_non_affine()) {
    report_status_error(tr("Rasterize the layer to distort it"));
    return false;
  }
  bool invertible = false;
  const auto inverse = transform_affine_delta().inverted(&invertible);
  if (!invertible || !prepare_free_transform_source()) {
    return false;
  }
  const auto step_before = current_transform_session_step();
  const auto previous_preview_rect = transform_preview_document_rect();
  const auto original = transform_original_corners();
  for (std::size_t i = 0; i < corners.size(); ++i) {
    transform_corner_offsets_[i] = inverse.map(corners[i]) - original[i];
  }
  record_transform_session_step(step_before);
  refresh_transform_composited_preview_cache();
  update_transform_preview_region(previous_preview_rect);
  update();
  notify_transform_controls_changed();
  return true;
}

void CanvasWidget::set_transform_cursor_for_handle(TransformHandle handle, double angle_degrees) {
  // A resize handle's outward direction in screen degrees (y down, clockwise),
  // turned with the box, picks the nearest of the four resize cursors.
  double direction = 0.0;
  switch (handle) {
    case TransformHandle::Move:
      setCursor(Qt::SizeAllCursor);
      return;
    case TransformHandle::Rotate:
      setCursor(rotate_cursor());
      return;
    case TransformHandle::None:
      setCursor(Qt::ArrowCursor);
      return;
    case TransformHandle::Right:
      direction = 0.0;
      break;
    case TransformHandle::BottomRight:
      direction = 45.0;
      break;
    case TransformHandle::Bottom:
      direction = 90.0;
      break;
    case TransformHandle::BottomLeft:
      direction = 135.0;
      break;
    case TransformHandle::Left:
      direction = 180.0;
      break;
    case TransformHandle::TopLeft:
      direction = 225.0;
      break;
    case TransformHandle::Top:
      direction = 270.0;
      break;
    case TransformHandle::TopRight:
      direction = 315.0;
      break;
  }
  const auto step = static_cast<long>(std::lround((direction + angle_degrees) / 45.0));
  static constexpr std::array<Qt::CursorShape, 4> kResizeCursors = {Qt::SizeHorCursor, Qt::SizeFDiagCursor,
                                                                    Qt::SizeVerCursor, Qt::SizeBDiagCursor};
  setCursor(kResizeCursors[static_cast<std::size_t>(((step % 4) + 4) % 4)]);
}

void CanvasWidget::set_transform_cursor_at(QPoint widget_point, TransformHandle handle, QRectF document_rect,
                                           double angle_degrees) {
  if (handle == TransformHandle::Rotate) {
    setCursor(transform_rotate_cursor_at(widget_point, document_rect, angle_degrees, true));
  } else {
    set_transform_cursor_for_handle(handle, angle_degrees);
  }
}

QCursor CanvasWidget::transform_rotate_cursor_at(QPoint widget_point, QRectF document_rect, double angle_degrees,
                                                 bool stem_counts) const {
  if (stem_counts) {
    constexpr double kStemHit = 14.0;
    const auto stem = transform_handle_position(TransformHandle::Rotate, document_rect, angle_degrees);
    if (std::abs(widget_point.x() - stem.x()) <= kStemHit / 2.0 &&
        std::abs(widget_point.y() - stem.y()) <= kStemHit / 2.0) {
      return rotate_cursor();
    }
  }
  // The corner on the pointer's side of the box (in the box's own frame), whose
  // outward direction, turned with the box, picks one of four cursors.
  const auto widget_rect = widget_rect_for_document_rect(document_rect);
  const auto local = rotate_offset(QPointF(widget_point) - widget_rect.center(), -angle_degrees);
  const auto right = local.x() >= 0.0;
  const auto below = local.y() >= 0.0;
  const auto corner_direction = right ? (below ? 45.0 : 315.0) : (below ? 135.0 : 225.0);
  return corner_rotate_cursor(corner_direction + angle_degrees);
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

  if (transform_vector_preview_drawn_) {
    // Dynamic Vector Preview drew the transformed shape sharp; a resampled blit would
    // cover it with document pixels.
  } else if (!transform_targets_.empty()) {
    if (!transform_multi_snapshot_.isNull() && !transform_multi_snapshot_rect_.isEmpty() &&
        transform_preview_patches_.empty()) {
      // Multi-target drag preview: the one subtree snapshot mapped through the
      // session delta (the move-proxy contract; blend modes against the real
      // backdrop, canvas clipping, and effect geometry stay approximate until
      // the release/numeric refresh renders the accurate patches).
      painter.save();
      if (transform_multi_snapshot_clip_.has_value()) {
        painter.setClipRect(widget_rect_for_document_rect(QRectF(*transform_multi_snapshot_clip_)), Qt::IntersectClip);
      }
      painter.setRenderHint(QPainter::SmoothPixmapTransform,
                            transform_interpolation_ != TransformInterpolation::NearestNeighbor);
      const auto delta = transform_session_delta();
      const QTransform document_to_widget(zoom_, 0.0, 0.0, zoom_, pan_.x(), pan_.y());
      painter.setTransform(delta * document_to_widget, true);
      painter.drawImage(QRectF(transform_multi_snapshot_rect_), transform_multi_snapshot_,
                        QRectF(transform_multi_snapshot_.rect()));
      painter.restore();
    }
  } else if (transform_drag_uses_clip_group_preview_ && !transform_clip_group_image_.isNull() &&
             transform_preview_patches_.empty()) {
    draw_transform_clip_group_preview(painter);
  } else if (transform_drag_uses_proxy_preview_ && !transform_proxy_image_.isNull() && transform_preview_patches_.empty()) {
    // Latched heavy drag: the bounded proxy stands in for the composited
    // patches. Unlike the plain source blit below it applies the layer's
    // opacity and scale-sign flips, matching what the release-time patches
    // will show; blend mode, masks, and styles stay approximated until then.
    painter.save();
    clip_painter_to_transform_artboard(painter);
    painter.setRenderHint(QPainter::SmoothPixmapTransform,
                          transform_interpolation_ != TransformInterpolation::NearestNeighbor);
    painter.setOpacity(transform_proxy_layer_opacity_);
    if (transform_is_distorted()) {
      // Skew/Distort/Perspective: the quad's homography maps the proxy directly.
      const QTransform document_to_widget(zoom_, 0.0, 0.0, zoom_, pan_.x(), pan_.y());
      painter.setTransform(transform_session_image_to_document(transform_proxy_image_) * document_to_widget, true);
      painter.drawImage(QRectF(transform_proxy_image_.rect()), transform_proxy_image_,
                        QRectF(transform_proxy_image_.rect()));
    } else {
      painter.translate(rect.center());
      painter.rotate(transform_angle_);
      painter.scale(transform_scale_x_sign_, transform_scale_y_sign_);
      const QRectF local_rect(-rect.width() / 2.0, -rect.height() / 2.0, rect.width(), rect.height());
      painter.drawImage(local_rect, transform_proxy_image_, QRectF(transform_proxy_image_.rect()));
    }
    painter.restore();
  } else if (!transform_source_image_.isNull() && transform_preview_patches_.empty()) {
    painter.save();
    clip_painter_to_transform_artboard(painter);
    painter.setRenderHint(QPainter::SmoothPixmapTransform,
                          transform_interpolation_ != TransformInterpolation::NearestNeighbor);
    const auto& source = transform_preview_source_image();
    if (transform_is_distorted()) {
      const QTransform document_to_widget(zoom_, 0.0, 0.0, zoom_, pan_.x(), pan_.y());
      painter.setTransform(transform_session_image_to_document(source) * document_to_widget, true);
      painter.drawImage(QRectF(source.rect()), source, QRectF(source.rect()));
    } else {
      painter.translate(rect.center());
      painter.rotate(transform_angle_);
      // Apply the scale-sign flips like the proxy path above: without them, dragging a
      // handle across the anchor showed unmirrored text/pixels for the whole drag and the
      // mirror only appeared at commit.
      painter.scale(transform_scale_x_sign_, transform_scale_y_sign_);
      const QRectF local_rect(-rect.width() / 2.0, -rect.height() / 2.0, rect.width(), rect.height());
      painter.drawImage(local_rect, source, QRectF(source.rect()));
    }
    painter.restore();
  }

  if (transform_is_distorted()) {
    draw_distorted_transform_controls(painter);
  } else {
    draw_transform_controls(painter, transform_current_rect_, transform_angle_);
  }

  // Reference point marker (Photoshop's circle with a crosshair): the pivot the
  // rotate handle, Alt scaling, and the numeric fields turn about. Session only,
  // like Photoshop; the passive Move box stays a plain frame. Same halo-plus-bright
  // treatment as the clone source marker so it reads on any art.
  const auto reference = widget_position_f(transform_reference_position(transform_current_rect_, transform_angle_));
  constexpr double kMarkerRadius = 4.5;
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setBrush(Qt::NoBrush);
  for (const auto& pen : {QPen(QColor(10, 14, 20, 200), 3.0), QPen(QColor(245, 248, 252, 235), 1.2)}) {
    painter.setPen(pen);
    painter.drawEllipse(reference, kMarkerRadius, kMarkerRadius);
    painter.drawLine(QPointF(reference.x() - kMarkerRadius - 4.0, reference.y()),
                     QPointF(reference.x() + kMarkerRadius + 4.0, reference.y()));
    painter.drawLine(QPointF(reference.x(), reference.y() - kMarkerRadius - 4.0),
                     QPointF(reference.x(), reference.y() + kMarkerRadius + 4.0));
  }
  painter.restore();
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

  draw_transform_handle_squares(painter, document_rect, angle_degrees, /*include_rotate=*/true);
}

void CanvasWidget::draw_transform_handle_squares(QPainter& painter, QRectF document_rect, double angle_degrees,
                                                 bool include_rotate) const {
  constexpr double kHandleSize = 8.0;
  const std::array<TransformHandle, 9> handles = {
      TransformHandle::TopLeft,    TransformHandle::Top,    TransformHandle::TopRight,
      TransformHandle::Right,      TransformHandle::BottomRight, TransformHandle::Bottom,
      TransformHandle::BottomLeft, TransformHandle::Left,   TransformHandle::Rotate};
  painter.save();
  painter.setPen(QPen(QColor(10, 14, 20), 1.0));
  for (const auto handle : handles) {
    if (handle == TransformHandle::Rotate && !include_rotate) {
      continue;
    }
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

CanvasWidget::TransformHandle CanvasWidget::transform_handle_at(QPoint widget_point, TransformHitZones zones) const {
  if (!transforming_layer_) {
    return TransformHandle::None;
  }
  if (transform_is_distorted()) {
    return transform_handle_for_session_mode(distorted_transform_handle_at(widget_point));
  }
  return transform_handle_for_session_mode(
      transform_handle_at(widget_point, transform_current_rect_, transform_angle_, zones));
}

CanvasWidget::TransformHandle CanvasWidget::transform_handle_for_session_mode(TransformHandle handle) const noexcept {
  switch (transform_distort_mode_) {
    case TransformDistortMode::Rotate:
      // Grabbing a corner or side swings the box about the reference point; the
      // inside still moves it.
      return transform_handle_is_corner(handle) || transform_handle_is_edge(handle) ? TransformHandle::Rotate
                                                                                     : handle;
    case TransformDistortMode::Scale:
      return handle == TransformHandle::Rotate ? TransformHandle::None : handle;
    case TransformDistortMode::None:
    case TransformDistortMode::Skew:
    case TransformDistortMode::Distort:
    case TransformDistortMode::Perspective:
      break;
  }
  return handle;
}

CanvasWidget::TransformHandle CanvasWidget::transform_handle_at(QPoint widget_point, QRectF document_rect,
                                                                double angle_degrees, TransformHitZones zones) const {
  constexpr double kHandleHit = 14.0;
  // EdgesAndCorners (GitHub issue 176), in widget pixels of the box's own frame:
  // each edge resizes within kEdgeGrabOutside outside its line and up to half a
  // handle square inside it, and a press within kCornerRotateReach of a corner,
  // outside the box, rotates. Inside, the edge band shrinks to a third of the
  // half-extent, so a small box keeps a Move core that wins over its handle
  // squares (which used to cover all of it); from 42 px up the core starts
  // exactly where the squares end, leaving the handle hits as they were.
  constexpr double kEdgeGrabOutside = 6.0;
  constexpr double kCornerRotateReach = 28.8;  // 24 px plus 20 percent: the band was hard to grab
  const bool forgiving = zones == TransformHitZones::EdgesAndCorners;
  QPointF local;
  double half_width = 0.0;
  double half_height = 0.0;
  double inner_x = 0.0;
  double inner_y = 0.0;
  if (forgiving) {
    const auto widget_rect = widget_rect_for_document_rect(document_rect);
    half_width = widget_rect.width() / 2.0;
    half_height = widget_rect.height() / 2.0;
    local = rotate_offset(QPointF(widget_point) - widget_rect.center(), -angle_degrees);
    inner_x = std::min(kHandleHit / 2.0, half_width / 3.0);
    inner_y = std::min(kHandleHit / 2.0, half_height / 3.0);
    if (std::abs(local.x()) < half_width - inner_x && std::abs(local.y()) < half_height - inner_y) {
      return TransformHandle::Move;
    }
  }
  const std::array<TransformHandle, 9> handles = {
      TransformHandle::Rotate,     TransformHandle::TopLeft, TransformHandle::Top,
      TransformHandle::TopRight,   TransformHandle::Right,   TransformHandle::BottomRight,
      TransformHandle::Bottom,     TransformHandle::BottomLeft, TransformHandle::Left};
  // On a box under 42 px the squares overlap; the forgiving zones take the
  // nearest hit square (a corner beside its corner, not a neighboring edge).
  auto nearest = TransformHandle::None;
  auto nearest_distance = 0.0;
  for (const auto handle : handles) {
    const auto point = transform_handle_position(handle, document_rect, angle_degrees);
    const QRectF hit_rect(point.x() - kHandleHit / 2.0, point.y() - kHandleHit / 2.0, kHandleHit, kHandleHit);
    if (!hit_rect.contains(widget_point)) {
      continue;
    }
    if (!forgiving || handle == TransformHandle::Rotate) {
      return handle;
    }
    const auto distance = std::hypot(point.x() - widget_point.x(), point.y() - widget_point.y());
    if (nearest == TransformHandle::None || distance < nearest_distance) {
      nearest = handle;
      nearest_distance = distance;
    }
  }
  if (nearest != TransformHandle::None) {
    return nearest;
  }

  if (forgiving) {
    const auto within_x = std::abs(local.x()) <= half_width;
    const auto within_y = std::abs(local.y()) <= half_height;
    // `outward` is the side's outward sign; positive offsets lie outside the box.
    const auto near_line = [](double coordinate, double line, double outward, double inward) {
      const auto offset = (coordinate - line) * outward;
      return offset <= kEdgeGrabOutside && offset >= -inward;
    };
    const auto left = within_y && near_line(local.x(), -half_width, -1.0, inner_x);
    const auto right = within_y && near_line(local.x(), half_width, 1.0, inner_x);
    const auto top = within_x && near_line(local.y(), -half_height, -1.0, inner_y);
    const auto bottom = within_x && near_line(local.y(), half_height, 1.0, inner_y);
    if (top && left) {
      return TransformHandle::TopLeft;
    }
    if (top && right) {
      return TransformHandle::TopRight;
    }
    if (bottom && left) {
      return TransformHandle::BottomLeft;
    }
    if (bottom && right) {
      return TransformHandle::BottomRight;
    }
    if (top) {
      return TransformHandle::Top;
    }
    if (bottom) {
      return TransformHandle::Bottom;
    }
    if (left) {
      return TransformHandle::Left;
    }
    if (right) {
      return TransformHandle::Right;
    }
    if (within_x && within_y) {
      return TransformHandle::Move;
    }
    const auto corner_distance = std::hypot(std::abs(local.x()) - half_width, std::abs(local.y()) - half_height);
    return corner_distance <= kCornerRotateReach ? TransformHandle::Rotate : TransformHandle::None;
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

QPointF CanvasWidget::transform_handle_grab_offset(TransformHandle handle, QRectF document_rect,
                                                  double angle_degrees, QPointF document_press) const {
  const QPointF half(document_rect.width() / 2.0, document_rect.height() / 2.0);
  const auto press = rotate_offset(document_press - document_rect.center(), -angle_degrees);
  QPointF offset;
  switch (handle) {
    case TransformHandle::TopLeft:
      offset = -half - press;
      break;
    case TransformHandle::Top:
      offset = QPointF(0.0, -half.y() - press.y());
      break;
    case TransformHandle::TopRight:
      offset = QPointF(half.x() - press.x(), -half.y() - press.y());
      break;
    case TransformHandle::Right:
      offset = QPointF(half.x() - press.x(), 0.0);
      break;
    case TransformHandle::BottomRight:
      offset = half - press;
      break;
    case TransformHandle::Bottom:
      offset = QPointF(0.0, half.y() - press.y());
      break;
    case TransformHandle::BottomLeft:
      offset = QPointF(-half.x() - press.x(), half.y() - press.y());
      break;
    case TransformHandle::Left:
      offset = QPointF(-half.x() - press.x(), 0.0);
      break;
    case TransformHandle::None:
    case TransformHandle::Move:
    case TransformHandle::Rotate:
      return {};
  }
  return rotate_offset(offset, angle_degrees);
}

void CanvasWidget::update_free_transform_preview(QPointF document_point, Qt::KeyboardModifiers modifiers) {
  // Ctrl (or Shift with it) pressed or released mid-drag switches between resize
  // and skew/distort live (GitHub issue 351).
  retarget_transform_drag(modifiers);
  if (transform_drag_kind_ != TransformDragKind::Scale) {
    update_free_transform_distort(document_point, modifiers);
    return;
  }
  if (transform_drag_handle_ != TransformHandle::Rotate) {
    // A scale handle grabbed beside its line follows the pointer from where it
    // sits (the offset is zero for Move), so the press never jumps the edge.
    document_point = snapped_document_point_f(document_point + transform_drag_grab_offset_);
  }
  const auto previous_preview_rect = transform_preview_document_rect();
  auto rect = transform_drag_start_rect_;

  if (transform_drag_handle_ == TransformHandle::Move) {
    // The end point above is whole-pixel (snapped_document_point_f rounds
    // before it snaps), so the start rounds the same way: a pointer resting on
    // a half pixel (a fractional pan at 100%) otherwise turned a motionless
    // press and release inside the box, the first half of the double-click
    // that commits, into a 1 px nudge, and every drag overshot by one.
    const QPointF rounded_start(static_cast<double>(std::lround(transform_drag_start_point_.x())),
                                static_cast<double>(std::lround(transform_drag_start_point_.y())));
    rect.translate(document_point - rounded_start);
    transform_current_rect_ = rect;
    refresh_transform_preview_for_drag();
    update_transform_preview_region(previous_preview_rect);
    update_drag_readout_region();
    notify_transform_controls_changed();
    return;
  }

  if (transform_drag_handle_ == TransformHandle::Rotate) {
    // Pivot on the reference point (Photoshop): the point the options-bar combo
    // names stays fixed and the box center swings around it. With a the anchor
    // offset, C0 - pivot = -R(start)a, so the new center C1 = pivot - R(new)a and
    // transform_reference_position(new rect, new angle) is the pivot again.
    const auto pivot = transform_reference_position(transform_drag_start_rect_, transform_start_angle_);
    const auto start_vector = transform_drag_start_point_ - pivot;
    const auto now_vector = document_point - pivot;
    if (std::hypot(start_vector.x(), start_vector.y()) > 1e-6 && std::hypot(now_vector.x(), now_vector.y()) > 1e-6) {
      const auto start = std::atan2(start_vector.y(), start_vector.x());
      const auto now = std::atan2(now_vector.y(), now_vector.x());
      auto degrees = transform_start_angle_ + ((now - start) * 180.0 / kPi);
      if ((modifiers & Qt::ShiftModifier) != 0) {
        degrees = std::round(degrees / 15.0) * 15.0;
      }
      const auto center =
          pivot + rotate_offset(transform_drag_start_rect_.center() - pivot, degrees - transform_start_angle_);
      rect.moveCenter(center);
      transform_current_rect_ = rect;
      transform_angle_ = degrees;
    }
    refresh_transform_preview_for_drag();
    update_transform_preview_region(previous_preview_rect);
    update_drag_readout_region();
    notify_transform_controls_changed();
    return;
  }

  // Scale handles run in the box's own frame: the (snapped) pointer is unrotated
  // about the drag-start center, the edge/corner math happens on an axis-aligned
  // local rect, and the resulting center rotates back out. At angle 0 this is
  // the world-space math exactly, so unrotated drags are byte-identical.
  const auto start_center = transform_drag_start_rect_.center();
  const auto start_size = transform_drag_start_rect_.size();
  const auto local_point = rotate_offset(document_point - start_center, -transform_start_angle_);
  const QRectF start_local(-start_size.width() / 2.0, -start_size.height() / 2.0, start_size.width(),
                           start_size.height());
  auto local = start_local;
  switch (transform_drag_handle_) {
    case TransformHandle::TopLeft:
      local.setTopLeft(local_point);
      break;
    case TransformHandle::Top:
      local.setTop(local_point.y());
      break;
    case TransformHandle::TopRight:
      local.setTopRight(local_point);
      break;
    case TransformHandle::Right:
      local.setRight(local_point.x());
      break;
    case TransformHandle::BottomRight:
      local.setBottomRight(local_point);
      break;
    case TransformHandle::Bottom:
      local.setBottom(local_point.y());
      break;
    case TransformHandle::BottomLeft:
      local.setBottomLeft(local_point);
      break;
    case TransformHandle::Left:
      local.setLeft(local_point.x());
      break;
    case TransformHandle::None:
    case TransformHandle::Move:
    case TransformHandle::Rotate:
      break;
  }

  auto raw_rect = local;
  const auto corner_handle = transform_drag_handle_ == TransformHandle::TopLeft ||
                             transform_drag_handle_ == TransformHandle::TopRight ||
                             transform_drag_handle_ == TransformHandle::BottomRight ||
                             transform_drag_handle_ == TransformHandle::BottomLeft;
  const bool keeps_aspect = corner_handle && transform_drag_keeps_aspect(modifiers) && start_size.height() > 0.0;
  if (keeps_aspect) {
    QPointF anchor;
    QPointF corner;
    switch (transform_drag_handle_) {
      case TransformHandle::TopLeft:
        anchor = start_local.bottomRight();
        corner = start_local.topLeft();
        break;
      case TransformHandle::TopRight:
        anchor = start_local.bottomLeft();
        corner = start_local.topRight();
        break;
      case TransformHandle::BottomLeft:
        anchor = start_local.topRight();
        corner = start_local.bottomLeft();
        break;
      case TransformHandle::BottomRight:
      default:
        anchor = start_local.topLeft();
        corner = start_local.bottomRight();
        break;
    }

    // The shared scale is the pointer's distance from the anchor projected
    // onto the box diagonal (per axis in absolute value), so every pixel of
    // travel scales at the same rate whichever way the pointer leans. Taking
    // the axis the pointer pulled harder instead scaled `aspect` times faster
    // along the short side, jumped in rate when the winning axis switched,
    // and ignored the other axis outright (pulling the corner in on one axis
    // could still grow the box). Each axis keeps its own sign from the side
    // of the anchor the pointer is on, so a straight pull across the anchor
    // mirrors that axis alone.
    const auto diagonal = corner - anchor;
    const auto span_x = std::abs(diagonal.x());
    const auto span_y = std::abs(diagonal.y());
    const auto offset = local_point - anchor;
    const auto factor = std::max(1.0 / std::max(1.0, std::min(span_x, span_y)),
                                 (std::abs(offset.x()) * span_x + std::abs(offset.y()) * span_y) /
                                     (span_x * span_x + span_y * span_y));
    const auto sign_x = (offset.x() < 0.0) == (diagonal.x() < 0.0) ? 1.0 : -1.0;
    const auto sign_y = (offset.y() < 0.0) == (diagonal.y() < 0.0) ? 1.0 : -1.0;
    // Write the aspect-locked corner back through the same setters as the
    // non-Shift path so the dragged-corner/anchor relationship is preserved.
    // Building a QRectF directly from the anchor would invert width/height for
    // handles whose anchor is not the top-left, which the flip detection below
    // would then misread as a mirror (a 180° flip when both axes invert).
    const QPointF locked_corner(anchor.x() + sign_x * factor * diagonal.x(),
                                anchor.y() + sign_y * factor * diagonal.y());
    switch (transform_drag_handle_) {
      case TransformHandle::TopLeft:
        local.setTopLeft(locked_corner);
        break;
      case TransformHandle::TopRight:
        local.setTopRight(locked_corner);
        break;
      case TransformHandle::BottomLeft:
        local.setBottomLeft(locked_corner);
        break;
      case TransformHandle::BottomRight:
      default:
        local.setBottomRight(locked_corner);
        break;
    }
    raw_rect = local;
  }

  if (transform_drag_scales_about_reference(modifiers)) {
    // Alt: scale about the reference point (Photoshop). Per moving axis the
    // factor is how far the handle travelled relative to its distance from the
    // reference; scaling the start box about r by f maps the center to r(1 - f).
    // A reference sitting on the dragged edge has no symmetric meaning, so that
    // axis keeps the plain result.
    const auto reference = anchor_offset_from_center(start_size, transform_reference_point_);
    const bool moves_x = transform_drag_handle_ != TransformHandle::Top && transform_drag_handle_ != TransformHandle::Bottom;
    const bool moves_y = transform_drag_handle_ != TransformHandle::Left && transform_drag_handle_ != TransformHandle::Right;
    const auto handle_start = [&]() -> QPointF {
      switch (transform_drag_handle_) {
        case TransformHandle::TopLeft:
          return start_local.topLeft();
        case TransformHandle::Top:
          return QPointF(0.0, start_local.top());
        case TransformHandle::TopRight:
          return start_local.topRight();
        case TransformHandle::Right:
          return QPointF(start_local.right(), 0.0);
        case TransformHandle::BottomRight:
          return start_local.bottomRight();
        case TransformHandle::Bottom:
          return QPointF(0.0, start_local.bottom());
        case TransformHandle::BottomLeft:
          return start_local.bottomLeft();
        case TransformHandle::Left:
          return QPointF(start_local.left(), 0.0);
        case TransformHandle::None:
        case TransformHandle::Move:
        case TransformHandle::Rotate:
          break;
      }
      return QPointF();
    }();
    std::optional<double> factor_x;
    std::optional<double> factor_y;
    if (moves_x && std::abs(handle_start.x() - reference.x()) >= 1.0) {
      factor_x = (local_point.x() - reference.x()) / (handle_start.x() - reference.x());
    }
    if (moves_y && std::abs(handle_start.y() - reference.y()) >= 1.0) {
      factor_y = (local_point.y() - reference.y()) / (handle_start.y() - reference.y());
    }
    if (keeps_aspect) {
      // One shared magnitude, the same projection rule as the plain corner
      // drag above (per-axis factors weighted by the squared reference-to-
      // handle spans); each axis keeps its own sign.
      if (factor_x.has_value() && factor_y.has_value()) {
        const auto span_x = handle_start.x() - reference.x();
        const auto span_y = handle_start.y() - reference.y();
        const auto magnitude = (std::abs(*factor_x) * span_x * span_x + std::abs(*factor_y) * span_y * span_y) /
                               (span_x * span_x + span_y * span_y);
        factor_x = *factor_x < 0.0 ? -magnitude : magnitude;
        factor_y = *factor_y < 0.0 ? -magnitude : magnitude;
      } else {
        // The reference sits on one dragged edge: the other axis's factor
        // drives both.
        const auto shared = factor_x.has_value() ? factor_x : factor_y;
        factor_x = shared;
        factor_y = shared;
      }
    }
    if (factor_x.has_value()) {
      const auto width = start_size.width() * *factor_x;
      const auto center_x = reference.x() * (1.0 - *factor_x);
      raw_rect.setLeft(center_x - width / 2.0);
      raw_rect.setRight(center_x + width / 2.0);
    }
    if (factor_y.has_value()) {
      const auto height = start_size.height() * *factor_y;
      const auto center_y = reference.y() * (1.0 - *factor_y);
      raw_rect.setTop(center_y - height / 2.0);
      raw_rect.setBottom(center_y + height / 2.0);
    }
    local = raw_rect;
  }

  transform_scale_x_sign_ = transform_drag_start_scale_x_sign_ * (raw_rect.width() < 0.0 ? -1.0 : 1.0);
  transform_scale_y_sign_ = transform_drag_start_scale_y_sign_ * (raw_rect.height() < 0.0 ? -1.0 : 1.0);
  local = local.normalized();

  if (local.width() < 1.0) {
    local.setWidth(1.0);
  }
  if (local.height() < 1.0) {
    local.setHeight(1.0);
  }
  const auto center = start_center + rotate_offset(local.center(), transform_start_angle_);
  rect = QRectF(center.x() - local.width() / 2.0, center.y() - local.height() / 2.0, local.width(), local.height());
  transform_current_rect_ = rect;
  refresh_transform_preview_for_drag();
  update_transform_preview_region(previous_preview_rect);
  update_drag_readout_region();
  notify_transform_controls_changed();
}

void CanvasWidget::commit_free_transform() {
  // Photoshop warns before transforming type whose font is missing (GitHub issue 185); the
  // owner asks before anything is written, and a refusal cancels the session untouched.
  if (transforming_layer_ && document_ != nullptr && missing_font_transform_confirm_callback_) {
    const bool geometry_changed =
        transform_scale_x_sign_ < 0.0 || transform_scale_y_sign_ < 0.0 || std::abs(transform_angle_) > 0.01 ||
        transform_is_distorted() ||
        std::lround(transform_current_rect_.left()) != std::lround(transform_original_rect_.left()) ||
        std::lround(transform_current_rect_.top()) != std::lround(transform_original_rect_.top()) ||
        std::lround(transform_current_rect_.width()) != std::lround(transform_original_rect_.width()) ||
        std::lround(transform_current_rect_.height()) != std::lround(transform_original_rect_.height());
    std::vector<LayerId> ids;
    for (const auto& target : transform_targets_) {
      ids.push_back(target.id);
    }
    if (ids.empty() && transform_layer_id_.has_value()) {
      ids.push_back(*transform_layer_id_);
    }
    if (geometry_changed && !ids.empty() && !missing_font_transform_confirm_callback_(ids)) {
      cancel_free_transform();
      return;
    }
  }
  if (transforming_layer_ && !transform_targets_.empty()) {
    // Multi-target session; pending warp cannot coexist (warp refuses these).
    commit_free_transform_multi();
    return;
  }
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
  if (transform_floating_.has_value()) {
    commit_free_transform_floating();
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
                                  transform_session_source_to_document(transform_source_image_.size()),
                                  transform_interpolation_);
  auto transformed = transformed_result.image;
  auto new_bounds = transformed_result.bounds;

  const auto original_transform_bounds =
      Rect{static_cast<std::int32_t>(std::round(transform_original_rect_.left())),
           static_cast<std::int32_t>(std::round(transform_original_rect_.top())),
           static_cast<std::int32_t>(std::round(transform_original_rect_.width())),
           static_cast<std::int32_t>(std::round(transform_original_rect_.height()))};
  const auto orientation_changed = transform_scale_x_sign_ < 0.0 || transform_scale_y_sign_ < 0.0;
  const auto changed = orientation_changed || std::abs(transform_angle_) > 0.01 || transform_is_distorted() ||
                       new_bounds.x != original_transform_bounds.x ||
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
    layer->set_pixels(pixels_from_image_native(transformed));
    layer->set_bounds(new_bounds);
    if (text_layer) {
      const auto delta = affine_from_qtransform(transform_session_delta());
      layer->metadata()[kLayerMetadataTextTransform] =
          serialize_layer_affine_transform(compose_layer_affine_transform(delta, original_text_transform));
      layer->metadata()[kLayerMetadataTextRasterStatus] = "patchy_raster";
      // Replace the resampled (and therefore blocky on scale-up) bitmap with glyphs re-rasterized
      // through the composed transform, so transformed text stays crisp like Photoshop.
      if (text_layer_transform_render_callback_ && text_layer_transform_render_callback_(*transform_layer_id_)) {
        new_bounds = layer->bounds();
      }
    } else if (layer_is_vector_shape(*layer) || layer->vector_mask() != nullptr) {
      // The text-layer pattern for vectors: apply the affine to the path
      // model and re-rasterize crisply (replacing the resampled pixels). A
      // shape layer's own path maps so its unscaled stroke fills the box.
      const auto delta = shape_layer_commit_delta(*layer);
      const std::array<double, 6> matrix{delta.m11(), delta.m12(), delta.m21(),
                                         delta.m22(), delta.dx(),  delta.dy()};
      transform_keeping_unlinked_vector_mask(*layer, [&] {
        if (transform_has_perspective(delta)) {
          patchy::transform_layer_vector_data_projective(*document_, *layer, homography_from_qtransform(delta),
                                                         Rect::from_size(document_->width(), document_->height()));
        } else {
          patchy::transform_layer_vector_data(
              *document_, *layer, matrix,
              Rect::from_size(document_->width(), document_->height()));
        }
      });
      new_bounds = layer->bounds();
    } else if (layer_is_smart_object(*layer) && smart_object_lock_reason(*layer).empty()) {
      // The text-layer pattern for placed content: compose the delta into the
      // placement quad, then re-render crisply from the embedded source (the
      // resampled pixels committed above stay as the fallback).
      if (const auto placement = smart_object_placement_from_layer(*layer); placement.has_value()) {
        // A Distort/Perspective quad (or a perspective placement) follows
        // Photoshop's rule: nonAffineTransform = the corners, Trnf = their bounds.
        const auto updated =
            map_smart_object_placement(*placement, homography_from_qtransform(transform_session_delta()));
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
    // A linked raster mask follows the layer through the same delta (Photoshop
    // behavior; an unlinked mask stays put). Every layer type takes this path:
    // the mask is document-space data independent of how the pixels re-render.
    if (auto updated_mask = transformed_linked_raster_mask(
            std::as_const(*layer),
            transform_session_delta(),
            transform_interpolation_);
        updated_mask.has_value()) {
      layer->set_mask(std::move(*updated_mask));
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

  if (changed && !smart_filter_rerender_failed) {
    arm_transform_commit_hold();
  }
  reset_free_transform_session_state();
  update_tool_cursor();
  document_changed(to_qrect(old_bounds).united(to_qrect(new_bounds)));
  disarm_transform_commit_hold_if_settled();
  if (smart_filter_rerender_failed) {
    report_status_error(tr("Could not rebuild the Smart Filter preview and cache"));
  } else if (status_callback_) {
    status_callback_(changed ? tr("Transformed layer")
                             : tr("Free Transform cancelled"));
  }
  notify_transform_controls_changed();
}

// Floating-selection commit (GitHub issue 399): the transformed pixels land on
// what the lift left behind, and the selection follows through the same delta.
// One history entry; its snapshot holds the untouched layer and the selection
// the session started from.
void CanvasWidget::commit_free_transform_floating() {
  auto* layer = document_->find_layer(*transform_layer_id_);
  if (layer == nullptr) {
    cancel_free_transform();
    return;
  }
  const auto source_to_document = transform_session_source_to_document(transform_source_image_.size());
  const auto transformed_result =
      resample_transformed_rgba8(transform_source_image_, source_to_document, transform_interpolation_);
  const auto new_bounds = transformed_result.bounds;
  const auto original_bounds = Rect{static_cast<std::int32_t>(std::round(transform_original_rect_.left())),
                                    static_cast<std::int32_t>(std::round(transform_original_rect_.top())),
                                    static_cast<std::int32_t>(std::round(transform_original_rect_.width())),
                                    static_cast<std::int32_t>(std::round(transform_original_rect_.height()))};
  const auto changed = transform_scale_x_sign_ < 0.0 || transform_scale_y_sign_ < 0.0 ||
                       std::abs(transform_angle_) > 0.01 || transform_is_distorted() || new_bounds != original_bounds;
  if (!changed) {
    reset_free_transform_session_state();
    update_tool_cursor();
    update();
    if (status_callback_) {
      status_callback_(tr("Free Transform cancelled"));
    }
    notify_transform_controls_changed();
    return;
  }

  const auto delta = transform_session_delta();
  const auto interpolation = transform_interpolation_;
  if (before_edit_callback_) {
    before_edit_callback_(tr("Free Transform"));
  }
  const auto& floating = *transform_floating_;
  const auto old_bounds = layer->bounds();
  const auto output_bounds =
      floating.preserve_alpha ? floating.remainder_bounds : unite_rect(floating.remainder_bounds, new_bounds);
  const auto transformed_pixels = transformed_result.image.isNull()
                                      ? PixelBuffer()
                                      : pixels_from_image_native(transformed_result.image);
  layer->set_pixels(composite_floating_pixels(floating.remainder, floating.remainder_bounds, transformed_pixels,
                                              new_bounds, output_bounds, floating.preserve_alpha));
  layer->set_bounds(output_bounds);
  const auto moved_selection = resample_transformed_gray8(
      floating.coverage, 0U,
      QTransform::fromTranslate(floating.coverage_bounds.x, floating.coverage_bounds.y) * delta, interpolation);

  arm_transform_commit_hold();
  reset_free_transform_session_state();
  set_selection_from_coverage(to_qrect(moved_selection.bounds), moved_selection.pixels);
  update_tool_cursor();
  document_changed(to_qrect(old_bounds).united(to_qrect(output_bounds)));
  disarm_transform_commit_hold_if_settled();
  update();
  if (status_callback_) {
    status_callback_(tr("Transformed layer"));
  }
  notify_transform_controls_changed();
}

bool CanvasWidget::free_transform_vector_preview_possible() const {
  // The session's pixel source is built lazily (the first drag), so it is not required here.
  if (!transforming_layer_ || document_ == nullptr || !transform_targets_.empty() || !transform_layer_id_.has_value() ||
      transform_has_pending_warp_) {
    return false;
  }
  // The commit's vector branch alone: text and smart objects re-render other content, and a
  // linked raster mask resamples, so those sessions keep the pixel preview.
  const auto* layer = std::as_const(*document_).find_layer(*transform_layer_id_);
  return layer != nullptr && layer_is_vector_shape(*layer) && !layer_is_text(*layer) && !layer_is_smart_object(*layer) &&
         !layer->mask().has_value();
}

std::optional<std::pair<LayerId, QTransform>> CanvasWidget::free_transform_vector_preview_key() const {
  if (!free_transform_vector_preview_possible()) {
    return std::nullopt;
  }
  const auto* layer = std::as_const(*document_).find_layer(*transform_layer_id_);
  return std::pair<LayerId, QTransform>{layer->id(), shape_layer_commit_delta(*layer)};
}

Document CanvasWidget::free_transform_vector_preview_document(const std::pair<LayerId, QTransform>& key) const {
  auto preview = *document_;  // copy-on-write pixels: only the transformed shape re-bakes
  auto* layer = preview.find_layer(key.first);
  if (layer == nullptr || key.second.isIdentity()) {
    return preview;
  }
  const auto& delta = key.second;
  const auto canvas = Rect::from_size(preview.width(), preview.height());
  transform_keeping_unlinked_vector_mask(*layer, [&] {
    if (transform_has_perspective(delta)) {
      patchy::transform_layer_vector_data_projective(preview, *layer, homography_from_qtransform(delta), canvas);
    } else {
      patchy::transform_layer_vector_data(
          preview, *layer, {delta.m11(), delta.m12(), delta.m21(), delta.m22(), delta.dx(), delta.dy()}, canvas);
    }
  });
  return preview;
}

QTransform CanvasWidget::shape_layer_commit_delta(const Layer& layer) const {
  const auto* shape = layer_is_vector_shape(layer) ? layer.vector_shape() : nullptr;
  const auto path_bounds = shape != nullptr ? shape->path.bounds() : std::nullopt;
  return path_bounds.has_value() && !transform_is_distorted()
             ? shape_free_transform_delta(transform_original_rect_, transform_current_rect_, transform_angle_,
                                          transform_scale_x_sign_, transform_scale_y_sign_, *path_bounds)
             : transform_session_delta();
}

// Multi-target commit: one shared affine delta applied per leaf with the same
// per-type branches as the single-layer commit above, plus linked raster masks
// (the folder's own mask, member masks, adjustment masks) through the same
// transformed_linked_raster_mask the single-layer commit uses. One undo
// entry ("Free Transform") for the whole set; a required Smart Filter re-render
// makes the commit transactional exactly like the single path.
void CanvasWidget::commit_free_transform_multi() {
  if (!transforming_layer_ || document_ == nullptr || transform_targets_.empty() ||
      transform_targets_.front().source_image.isNull()) {
    cancel_free_transform();
    return;
  }

  const auto delta = transform_session_delta();
  const std::array<double, 6> matrix{delta.m11(), delta.m12(), delta.m21(),
                                     delta.m22(), delta.dx(),  delta.dy()};
  const auto orientation_changed = transform_scale_x_sign_ < 0.0 || transform_scale_y_sign_ < 0.0;
  const auto changed =
      orientation_changed || std::abs(transform_angle_) > 0.01 || transform_is_distorted() ||
      std::lround(transform_current_rect_.left()) != std::lround(transform_original_rect_.left()) ||
      std::lround(transform_current_rect_.top()) != std::lround(transform_original_rect_.top()) ||
      std::lround(transform_current_rect_.width()) != std::lround(transform_original_rect_.width()) ||
      std::lround(transform_current_rect_.height()) != std::lround(transform_original_rect_.height());

  const QRect canvas_rect(0, 0, document_->width(), document_->height());
  QRect dirty_rect = transform_multi_snapshot_rect_.united(transform_preview_patches_rect_);
  const auto accumulate_effect_rect = [&dirty_rect](const Layer& layer, Rect bounds, int ancestor_padding) {
    auto with_effects = layer_bounds_with_effects(layer, bounds);
    if (!with_effects.empty() && ancestor_padding > 0) {
      with_effects = outset_rect(with_effects, ancestor_padding);
    }
    if (!with_effects.empty()) {
      dirty_rect = dirty_rect.united(to_qrect(with_effects));
    }
  };
  // Shape paths and vector masks: the affine path, or the projective one for a
  // Distort/Perspective quad.
  const auto transform_vector_data = [this, &delta, &matrix](Layer& layer) {
    const auto canvas = Rect::from_size(document_->width(), document_->height());
    transform_keeping_unlinked_vector_mask(layer, [&] {
      if (transform_has_perspective(delta)) {
        patchy::transform_layer_vector_data_projective(*document_, layer, homography_from_qtransform(delta), canvas);
      } else {
        patchy::transform_layer_vector_data(*document_, layer, matrix, canvas);
      }
    });
  };
  const auto transform_linked_raster_mask = [this, &delta, &dirty_rect](Layer& layer) {
    auto updated = transformed_linked_raster_mask(std::as_const(layer), delta, transform_interpolation_);
    if (!updated.has_value()) {
      return;
    }
    dirty_rect = dirty_rect.united(to_qrect(std::as_const(layer).mask()->bounds));
    dirty_rect = dirty_rect.united(to_qrect(updated->bounds));
    layer.set_mask(std::move(*updated));
  };

  bool transactional_smart_filter = false;
  if (changed) {
    for (const auto& target : transform_targets_) {
      if (const auto* layer = std::as_const(*document_).find_layer(target.id);
          layer != nullptr && move_layer_requires_smart_filter_rerender(*layer)) {
        transactional_smart_filter = true;
        break;
      }
    }
  }
  std::optional<Document> rollback_document;
  if (transactional_smart_filter) {
    rollback_document.emplace(*document_);
  } else if (changed && before_edit_callback_) {
    before_edit_callback_(tr("Free Transform"));
  }

  bool smart_filter_rerender_failed = false;
  bool rerender_kept_resampled = false;  // a linked file could not be re-read; its resampled pixels stay
  if (changed) {
    for (const auto& target : transform_targets_) {
      auto* layer = document_->find_layer(target.id);
      if (layer == nullptr || target.source_image.isNull()) {
        continue;
      }
      accumulate_effect_rect(*layer, std::as_const(*layer).bounds(), target.ancestor_effect_padding);

      const auto text_layer = layer_is_text(std::as_const(*layer));
      const auto old_bounds = std::as_const(*layer).bounds();
      const auto original_text_transform =
          text_layer ? stored_text_transform_for_layer(*layer).value_or(identity_text_transform_for_rect(
                           QRectF(old_bounds.x, old_bounds.y, old_bounds.width, old_bounds.height)))
                     : LayerAffineTransform{};

      // Left-to-right composition: source-local -> document, then the delta.
      const auto source_to_document =
          QTransform::fromTranslate(target.original_bounds.x + target.source_local_rect.x(),
                                    target.original_bounds.y + target.source_local_rect.y()) *
          delta;
      const auto transformed_result =
          resample_transformed_rgba8(target.source_image, source_to_document, transform_interpolation_);
      layer->set_pixels(pixels_from_image_native(transformed_result.image));
      layer->set_bounds(transformed_result.bounds);
      if (text_layer) {
        layer->metadata()[kLayerMetadataTextTransform] = serialize_layer_affine_transform(
            compose_layer_affine_transform(affine_from_qtransform(delta), original_text_transform));
        layer->metadata()[kLayerMetadataTextRasterStatus] = "patchy_raster";
        // Replace the resampled bitmap with glyphs re-rasterized through the
        // composed transform, matching the single-layer commit.
        if (text_layer_transform_render_callback_) {
          text_layer_transform_render_callback_(target.id);
        }
      } else if (layer_is_vector_shape(std::as_const(*layer)) || layer->vector_mask() != nullptr) {
        transform_vector_data(*layer);
      } else if (layer_is_smart_object(std::as_const(*layer))) {
        if (const auto placement = smart_object_placement_from_layer(*layer); placement.has_value()) {
          // Perspective placements map their real quad; Trnf becomes its bounds
          // (map_smart_object_placement), the additive writer fallback being
          // exact only for translations.
          const auto updated = map_smart_object_placement(*placement, homography_from_qtransform(delta));
          store_smart_object_placement(*layer, updated);
          mark_layer_smart_object_block_dirty(*layer);
          layer->metadata()[kLayerMetadataSmartObjectRasterStatus] = kSmartObjectRasterStatusPatchy;
          // Editable and LINKED ("external") placements re-render from their source
          // (the host resolves the linked file; a vector file rasterizes at the new
          // scale). A missing or unreadable linked file leaves the resampled pixels
          // committed above in place, and the host reports the file.
          if (const auto lock = smart_object_lock_reason(*layer); lock.empty() || lock == "external") {
            if (smart_object_transform_render_callback_ && smart_object_transform_render_callback_(target.id)) {
              // Bounds refreshed by the re-render.
            } else if (transactional_smart_filter) {
              smart_filter_rerender_failed = true;
            } else {
              rerender_kept_resampled = true;
            }
          }
          // Warp-, filter- and legacy-locked layers keep the resampled pixels (no
          // re-render exists); the mapped quads keep the SoLd geometry consistent.
        } else if (transactional_smart_filter) {
          smart_filter_rerender_failed = true;
        }
      }
      transform_linked_raster_mask(*layer);
      accumulate_effect_rect(*layer, std::as_const(*layer).bounds(), target.ancestor_effect_padding);
      if (smart_filter_rerender_failed) {
        break;
      }
    }

    // Mask-only riders: groups' own masks, adjustment masks, and empty-pixel
    // leaves whose linked masks still follow the set.
    if (!smart_filter_rerender_failed) {
      for (const auto mask_only_id : transform_mask_only_ids_) {
        auto* layer = document_->find_layer(mask_only_id);
        if (layer == nullptr) {
          continue;
        }
        transform_linked_raster_mask(*layer);
        if (layer->vector_mask() != nullptr) {
          transform_vector_data(*layer);
          accumulate_effect_rect(*layer, std::as_const(*layer).bounds(), 0);
        }
      }
    }
  }

  if (smart_filter_rerender_failed && rollback_document.has_value()) {
    *document_ = std::move(*rollback_document);
  } else if (transactional_smart_filter && rollback_document.has_value()) {
    // Same dance as the single-layer commit: the undo snapshot must capture the
    // pre-edit document even though the mutation had to be attempted first.
    auto committed_document = *document_;
    *document_ = std::move(*rollback_document);
    if (before_edit_callback_) {
      before_edit_callback_(tr("Free Transform"));
    }
    *document_ = std::move(committed_document);
  }

  if (changed && !smart_filter_rerender_failed) {
    arm_transform_commit_hold();
  }
  reset_free_transform_session_state();
  update_tool_cursor();
  document_changed(dirty_rect.isEmpty() ? canvas_rect : dirty_rect.intersected(canvas_rect));
  disarm_transform_commit_hold_if_settled();
  if (smart_filter_rerender_failed) {
    report_status_error(tr("Could not rebuild the Smart Filter preview and cache"));
  } else if (status_callback_ && !rerender_kept_resampled) {
    // A kept resampled preview leaves the host's report (the missing linked file) on
    // the status bar instead of announcing plain success.
    status_callback_(changed ? tr("Transformed layers") : tr("Free Transform cancelled"));
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
  const auto delta = transform_session_delta();
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
  if (changed && !smart_filter_rerender_failed) {
    arm_transform_commit_hold();
  }
  reset_free_transform_session_state();
  clear_pending_warp();
  update_tool_cursor();
  document_changed(to_qrect(old_bounds).united(to_qrect(new_bounds)));
  disarm_transform_commit_hold_if_settled();
  if (smart_filter_rerender_failed) {
    report_status_error(tr("Could not rebuild the Smart Filter preview and cache"));
  } else if (status_callback_) {
    status_callback_(changed ? tr("Warped layer")
                             : tr("Warp Transform cancelled"));
  }
  notify_transform_controls_changed();
}

bool CanvasWidget::begin_warp_transform() {
  if (layer_edit_target_ == LayerEditTarget::SmartFilterMask) {
    report_status_error(tr("This tool is unavailable while editing a Smart Filter mask"));
    return false;
  }
  if (warping_layer_) {
    return true;
  }
  if (free_transform_is_multi_target()) {
    // Refused BEFORE any teardown so the pending multi-target transform stays
    // alive (the same contract as the per-layer refusals below).
    report_status_error(tr("Warp works on a single layer. Select one layer to warp."));
    return false;
  }
  if (free_transform_floats_selection()) {
    // Warp takes whole layers; refused before teardown like the multi case.
    report_status_error(tr("Warp works on the whole layer. Apply this transform first."));
    return false;
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
    report_status_error(tr("Select an editable pixel layer to warp"));
    return false;
  }
  if (layer_effectively_locks_position(*layer)) {
    show_layer_position_locked_message();
    return false;
  }
  if (layer_is_text(*layer)) {
    report_status_error(tr("Text layers use Warp Text (the Type tool's Warp... button). For a custom mesh, "
                           "convert to a smart object or rasterize first."));
    return false;
  }
  // A linked ("external") placement warps like an embedded one: its file is the
  // source. Warp, filter and legacy locks have no source to bake from.
  if (const auto lock = layer_is_smart_object(*layer) ? smart_object_lock_reason(*layer) : std::string();
      !lock.empty() && lock != "external") {
    report_status_error(tr("This smart object is preview-only and can't be warped. Rasterize the layer first."));
    return false;
  }
  if (layer_is_vector_shape(*layer) || layer_has_enabled_vector_mask(*layer)) {
    report_status_error(tr("Shape layers and vector masks can't be warped. Convert to a smart object or rasterize first."));
    return false;
  }
  // Photoshop's warped placements keep Trnf == nonAffineTransform (the hull box);
  // a cage over a perspective placement has no file form here yet.
  if (layer_is_smart_object(*layer)) {
    const auto placement = smart_object_placement_from_layer(*layer);
    const bool perspective_placement = placement.has_value() && placement->non_affine_transform.has_value() &&
                                       placement->non_affine_transform != placement->transform;
    if (perspective_placement || (transforming_layer_ && transform_has_perspective(transform_session_delta()))) {
      report_status_error(tr("A smart object in perspective can't be warped. Rasterize the layer first."));
      return false;
    }
  }

  WarpMeshGrid start_mesh;
  std::array<double, 9> content_to_document{1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
  const bool smart_object = layer_is_smart_object(*layer);
  if (smart_object) {
    const auto placement = smart_object_placement_from_layer(*layer);
    const auto uuid = smart_object_source_uuid(*layer);
    const auto* source =
        placement.has_value() ? document_->metadata().smart_objects.find(uuid) : nullptr;
    // The host resolves a linked file against the document's folder; without a host
    // only embedded bytes decode.
    QString decode_error;
    std::optional<QImage> decoded;
    if (source != nullptr) {
      decoded = smart_object_source_image_callback_
                    ? smart_object_source_image_callback_(layer->id(), &decode_error)
                    : decode_smart_object_source_image(*source);
    }
    if (!placement.has_value() || !decoded.has_value() || decoded->isNull()) {
      report_status_error(decode_error.isEmpty() ? tr("This smart object's contents can't be decoded for warping")
                                                 : decode_error);
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
      report_status_error(tr("Layer has no pixels to warp"));
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
    const auto delta = transform_session_delta();
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
  clear_transform_session_steps();
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
  clear_warp_preview_caches();
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
  const auto step_before = current_transform_session_step();
  if (style == QStringLiteral("warpCustom")) {
    warp_style_ = style;
    warp_style_value_ = value;
    record_transform_session_step(step_before);
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
  record_transform_session_step(step_before);
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
  if (!std::isfinite(content[0]) || !std::isfinite(content[1])) {
    return;
  }
  auto candidate = warp_mesh_;
  candidate.xs[static_cast<std::size_t>(index)] = content[0];
  candidate.ys[static_cast<std::size_t>(index)] = content[1];
  if (!warp_mesh_placement_is_sane(candidate, warp_mesh_)) {
    // Past the horizon of a perspective stage the surface runs off to infinity, and
    // far past the canvas it only stretches into slivers: the handle stays at its
    // last good position, as a Free Transform corner does at a fold.
    return;
  }
  warp_mesh_ = std::move(candidate);
  const bool style_changed = warp_style_ != QStringLiteral("warpCustom") || warp_style_value_ != 0.0;
  warp_style_ = QStringLiteral("warpCustom");
  warp_style_value_ = 0.0;
  if (dragging_warp_handle_) {
    // Mouse moves can arrive faster than a preview renders (high-rate pointers,
    // Wayland's uncompressed motion); rendering each one queued them up and the
    // canvas fell seconds behind the pointer (GitHub #298). paintEvent renders
    // the latest mesh once per frame, and the options bar refreshes only when the
    // style flips to Custom (the release refreshes it again).
    warp_drag_preview_pending_ = true;
    update();
    if (style_changed) {
      notify_transform_controls_changed();
    }
    return;
  }
  refresh_warp_preview_cache();
  update();
  notify_transform_controls_changed();
}

bool CanvasWidget::prepare_warp_source() {
  if (!warping_layer_ || document_ == nullptr || !warp_layer_id_.has_value()) {
    return false;
  }
  const auto* layer = std::as_const(*document_).find_layer(*warp_layer_id_);
  if (layer == nullptr || warp_source_image_.isNull()) {
    return false;
  }
  // resample_warped_rgba8 converts an 8-bit source to RGBA8888 on every call;
  // converting once here makes the per-move conversion a no-op. A 16/32-bit
  // layer image keeps its format: the resampler warps it in float and returns
  // that format, so the commit writes the layer back at its own depth.
  if (!deep_qimage_format(warp_source_image_.format()).has_value()) {
    warp_source_image_ = warp_source_image_.convertToFormat(QImage::Format_RGBA8888);
  }
  warp_source_mips_.clear();
  ensure_warp_base_cache();
  refresh_warp_preview_cache();
  return true;
}

void CanvasWidget::ensure_warp_base_cache() {
  if (!warping_layer_ || document_ == nullptr || !warp_layer_id_.has_value() || !warp_base_cache_.isNull()) {
    return;
  }
  // Hidden via render overrides (set_visible toggles bumped revisions and
  // cold-invalidated the style-mask caches), banded across workers, and at
  // zoom <= 50% composited from the preview-scaled document.
  warp_base_cache_scale_level_ = 0;
  warp_base_display_mip_cache_.clear();
  warp_base_display_mip_source_key_ = 0;
  const std::vector<LayerId> hidden{*warp_layer_id_};
  if (const auto composite_level = preview_composite_level_for_zoom(view_zoom()); composite_level >= 1) {
    if (auto* scaled_document = preview_scaled_document_for_level(composite_level)) {
      const QRect scaled_canvas(0, 0, scaled_document->width(), scaled_document->height());
      auto base = qimage_from_document_rect_with_hidden_layers_banded(*scaled_document, scaled_canvas, true, hidden)
                      .convertToFormat(QImage::Format_RGBA8888);
      if (!base.isNull()) {
        warp_base_cache_ = std::move(base);
        warp_base_cache_scale_level_ = composite_level;
      }
    }
  }
  if (warp_base_cache_.isNull()) {
    const QRect canvas_rect(0, 0, document_->width(), document_->height());
    warp_base_cache_ = qimage_from_document_rect_with_hidden_layers_banded(*document_, canvas_rect, true, hidden)
                           .convertToFormat(QImage::Format_RGBA8888);
  }
}

void CanvasWidget::clear_warp_preview_caches() {
  warp_base_cache_ = QImage();
  warp_base_cache_scale_level_ = 0;
  warp_base_display_mip_cache_.clear();
  warp_base_display_mip_source_key_ = 0;
  warp_preview_patches_.clear();
  warp_preview_patches_scale_level_ = 0;
  warp_preview_covered_rect_ = QRect();
  warp_source_mips_.clear();
  warp_drag_preview_pending_ = false;
}

const QImage& CanvasWidget::warp_source_for_level(int level) {
  static const QImage kNull;
  if (level <= 0) {
    return warp_source_image_;
  }
  while (static_cast<int>(warp_source_mips_.size()) < level) {
    const QImage& previous = warp_source_mips_.empty() ? warp_source_image_ : warp_source_mips_.back();
    if (previous.isNull()) {
      return kNull;
    }
    QImage next;
    if (deep_qimage_format(previous.format()).has_value()) {
      // 16/32-bit sources keep their depth; the 8-bit box filter below has no
      // deep layout.
      next = previous.scaled(std::max(1, (previous.width() + 1) / 2), std::max(1, (previous.height() + 1) / 2),
                             Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
      if (!next.isNull() && next.format() != previous.format()) {
        next = next.convertToFormat(previous.format());
      }
    } else {
      // RGBA8888 straight alpha (prepare_warp_source): alpha-weighted 2x2 boxes, so
      // transparent texels never bleed color into the mip.
      next = halved_display_mip(previous);
    }
    if (next.isNull()) {
      return kNull;
    }
    warp_source_mips_.push_back(std::move(next));
  }
  return warp_source_mips_[static_cast<std::size_t>(level - 1)];
}

QRect CanvasWidget::warp_preview_view_rect(double margin_fraction) const {
  if (document_ == nullptr) {
    return {};
  }
  const QRect canvas_rect(0, 0, document_->width(), document_->height());
  if (width() <= 0 || height() <= 0) {
    return canvas_rect;
  }
  // All four corners: the bounding box stays right under a rotated view.
  double left = std::numeric_limits<double>::max();
  double top = std::numeric_limits<double>::max();
  double right = std::numeric_limits<double>::lowest();
  double bottom = std::numeric_limits<double>::lowest();
  for (const auto corner : {QPointF(0.0, 0.0), QPointF(width(), 0.0), QPointF(width(), height()),
                            QPointF(0.0, height())}) {
    const auto point = document_position_f(corner);
    left = std::min(left, point.x());
    top = std::min(top, point.y());
    right = std::max(right, point.x());
    bottom = std::max(bottom, point.y());
  }
  const QRectF view(QPointF(left, top), QPointF(right, bottom));
  // Four extra pixels cover the smooth display scaling at the view edge.
  const auto margin_x = view.width() * margin_fraction + 4.0;
  const auto margin_y = view.height() * margin_fraction + 4.0;
  return view.adjusted(-margin_x, -margin_y, margin_x, margin_y).toAlignedRect().intersected(canvas_rect);
}

void CanvasWidget::schedule_warp_preview_view_refresh() {
  if (!warping_layer_ || dragging_warp_handle_ || warp_preview_view_refresh_pending_) {
    return;
  }
  warp_preview_view_refresh_pending_ = true;
  // Deferred so a burst of wheel steps or pan moves re-renders once.
  QTimer::singleShot(0, this, [this] {
    warp_preview_view_refresh_pending_ = false;
    refresh_warp_preview_for_view();
  });
}

void CanvasWidget::refresh_warp_preview_for_view() {
  if (!warping_layer_ || dragging_warp_handle_ || document_ == nullptr || warp_source_image_.isNull()) {
    return;
  }
  const auto level = preview_composite_level_for_zoom(view_zoom());
  bool changed = false;
  if (warp_base_cache_scale_level_ > level) {
    // Zoomed in past the base's level: a coarser base would stay blurry for the
    // rest of the session.
    warp_base_cache_ = QImage();
    ensure_warp_base_cache();
    changed = true;
  }
  if (level != warp_preview_patches_scale_level_ ||
      !warp_preview_covered_rect_.contains(warp_preview_view_rect(0.0))) {
    refresh_warp_preview_cache();
    changed = true;
  }
  if (changed && isVisible()) {
    update();
  }
}

bool CanvasWidget::warp_mesh_placement_is_sane(const WarpMeshGrid& candidate, const WarpMeshGrid& current) const {
  if (candidate.xs.empty() || candidate.xs.size() != candidate.ys.size() || current.xs.empty() ||
      current.xs.size() != current.ys.size()) {
    return false;
  }
  const auto& map = warp_content_to_document_;
  const auto hull_corners = [](const WarpMeshGrid& mesh) {
    const auto [min_x, max_x] = std::minmax_element(mesh.xs.begin(), mesh.xs.end());
    const auto [min_y, max_y] = std::minmax_element(mesh.ys.begin(), mesh.ys.end());
    return std::array<std::array<double, 2>, 4>{std::array<double, 2>{*min_x, *min_y},
                                                std::array<double, 2>{*max_x, *min_y},
                                                std::array<double, 2>{*max_x, *max_y},
                                                std::array<double, 2>{*min_x, *max_y}};
  };
  // The projective denominator is linear in content space, so its sign over the hull
  // box (which holds the whole surface) is settled by the four corners. The current
  // hull's centre gives the sign of the visible side.
  const auto denominator = [&map](const std::array<double, 2>& point) {
    return map[6] * point[0] + map[7] * point[1] + map[8];
  };
  const auto current_corners = hull_corners(current);
  std::array<double, 2> centre{0.0, 0.0};
  for (const auto& corner : current_corners) {
    centre[0] += corner[0] / 4.0;
    centre[1] += corner[1] / 4.0;
  }
  const double reference = denominator(centre);
  if (std::abs(reference) < 1e-12) {
    return false;
  }
  const auto candidate_corners = hull_corners(candidate);
  const auto lowest_ratio = [&](const std::array<std::array<double, 2>, 4>& corners) {
    double lowest = std::numeric_limits<double>::infinity();
    for (const auto& corner : corners) {
      lowest = std::min(lowest, denominator(corner) / reference);
    }
    return lowest;
  };
  if (const auto candidate_ratio = lowest_ratio(candidate_corners);
      candidate_ratio <= 1e-3 && candidate_ratio < lowest_ratio(current_corners)) {
    return false;
  }
  const auto document_box = [&map](const std::array<std::array<double, 2>, 4>& corners) {
    double left = std::numeric_limits<double>::infinity();
    double top = std::numeric_limits<double>::infinity();
    double right = -std::numeric_limits<double>::infinity();
    double bottom = -std::numeric_limits<double>::infinity();
    for (const auto& corner : corners) {
      const auto mapped = apply_homography(map, corner[0], corner[1]);
      left = std::min(left, mapped[0]);
      top = std::min(top, mapped[1]);
      right = std::max(right, mapped[0]);
      bottom = std::max(bottom, mapped[1]);
    }
    return std::array<double, 4>{left, top, right, bottom};
  };
  const auto box = document_box(candidate_corners);
  if (!std::isfinite(box[0]) || !std::isfinite(box[1]) || !std::isfinite(box[2]) || !std::isfinite(box[3])) {
    return false;
  }
  const double canvas_width = document_ != nullptr ? document_->width() : 0.0;
  const double canvas_height = document_ != nullptr ? document_->height() : 0.0;
  const double margin = std::max(4096.0, 2.0 * std::max(canvas_width, canvas_height));
  if (box[0] >= -margin && box[1] >= -margin && box[2] <= canvas_width + margin &&
      box[3] <= canvas_height + margin) {
    return true;
  }
  // A layer that already reaches past the limit may still be warped inward.
  const auto now = document_box(current_corners);
  return box[0] >= now[0] && box[1] >= now[1] && box[2] <= now[2] && box[3] <= now[3];
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
  warp_drag_preview_pending_ = false;
  warp_preview_patches_.clear();
  warp_preview_patches_scale_level_ = 0;
  warp_preview_covered_rect_ = QRect();
  if (!warping_layer_ || document_ == nullptr || !warp_layer_id_.has_value() || warp_source_image_.isNull()) {
    return;
  }
  // View resolution (GitHub #200): rendering the whole layer at full resolution per
  // handle move took seconds a frame on large images. The preview renders at the
  // display's composite level from the preview-scaled document (the base comes from
  // the same document) and covers only the document area on screen. A drag frame
  // samples bilinearly through 16 px cells; the resting frame (release, presets,
  // view changes) uses the session's interpolation and the commit's 4 px cells over
  // the view plus half a view per side, so ordinary pans stay inside it. The commit
  // still bakes the full-resolution source (bake_warp_into_layer).
  const bool interactive = dragging_warp_handle_;
  const QRect canvas_rect(0, 0, document_->width(), document_->height());
  int level = preview_composite_level_for_zoom(view_zoom());
  if (interactive && level == 0 && view_zoom() < 1.0) {
    // Between 50% and 100% the display level is full resolution, so a drag frame
    // rendered up to twice the pixels on screen: the whole layer of a fit photo on
    // a HiDPI screen (GitHub #298). Drag frames go one level down; the resting
    // frame and the commit stay full resolution.
    level = 1;
  }
  Document* scaled_document = level >= 1 ? preview_scaled_document_for_level(level) : nullptr;
  const QImage* source = &warp_source_image_;
  if (scaled_document != nullptr) {
    source = &warp_source_for_level(level);
  }
  if (scaled_document == nullptr || source->isNull()) {
    level = 0;
    scaled_document = nullptr;
    source = &warp_source_image_;
  }
  const Document& composite_document = scaled_document != nullptr ? *scaled_document : std::as_const(*document_);
  const auto* layer = composite_document.find_layer(*warp_layer_id_);
  if (layer == nullptr) {
    return;
  }
  auto covered = warp_preview_view_rect(interactive ? 0.0 : 0.5);
  if (level >= 1) {
    covered = rect_aligned_to_mip_grid(covered, level).intersected(canvas_rect);
  }
  warp_preview_covered_rect_ = covered;
  if (covered.isEmpty()) {
    return;
  }
  const double scale = 1.0 / static_cast<double>(1 << level);
  auto quad = warp_document_quad();
  for (auto& value : quad) {
    value *= scale;
  }
  const auto grid = build_warp_surface_grid(warp_mesh_, quad, warp_source_image_.width() * scale,
                                            warp_source_image_.height() * scale, interactive ? 16.0 : 4.0,
                                            interactive ? 64 : 128);
  if (!grid.has_value()) {
    return;
  }
  const auto [min_x, max_x] = std::minmax_element(grid->doc_xs.begin(), grid->doc_xs.end());
  const auto [min_y, max_y] = std::minmax_element(grid->doc_ys.begin(), grid->doc_ys.end());
  const auto grid_left = static_cast<int>(std::floor(*min_x)) - 1;
  const auto grid_top = static_cast<int>(std::floor(*min_y)) - 1;
  const Rect grid_bounds{grid_left, grid_top, static_cast<int>(std::ceil(*max_x)) + 1 - grid_left,
                         static_cast<int>(std::ceil(*max_y)) + 1 - grid_top};
  // Layer effects reach past the pixels that cast them (a shadow's offset, a glow's
  // spread), so the resample keeps that reach around the covered area.
  const auto effect_bounds = layer_bounds_with_effects(*layer, grid_bounds);
  const auto reach = std::max({grid_bounds.x - effect_bounds.x, grid_bounds.y - effect_bounds.y,
                               effect_bounds.x + effect_bounds.width - grid_bounds.x - grid_bounds.width,
                               effect_bounds.y + effect_bounds.height - grid_bounds.y - grid_bounds.height, 0});
  const auto scaled_covered = level >= 1 ? preview_scaled_document_rect(covered, level) : covered;
  const auto clip = scaled_covered.adjusted(-reach, -reach, reach, reach);
  auto interpolation = transform_interpolation_;
  if (interactive && interpolation != TransformInterpolation::NearestNeighbor) {
    interpolation = TransformInterpolation::Bilinear;
  }
  const auto warped = resample_warped_rgba8(*source, *grid, interpolation,
                                            Rect{clip.x(), clip.y(), clip.width(), clip.height()});
  if (warped.image.isNull()) {
    return;
  }
  render_cache_diagnostics_.warp_preview_last_pixels =
      static_cast<std::uint64_t>(warped.image.width()) * static_cast<std::uint64_t>(warped.image.height());
  render_cache_diagnostics_.warp_preview_last_cell_tests = warped.cell_pixel_tests;
  const auto warped_pixels = pixels_from_image_native(warped.image);
  // Region-limited over the base cache (which excludes the layer): the warped
  // content only contributes inside its own effect bounds, so recompositing
  // the whole document per handle move - the warp drag's dominant cost - is
  // replaced by one bounded patch render.
  const QRect composite_canvas(0, 0, composite_document.width(), composite_document.height());
  const auto patch_rect =
      to_qrect(layer_bounds_with_effects(*layer, warped.bounds)).intersected(composite_canvas).intersected(scaled_covered);
  if (patch_rect.isEmpty()) {
    return;
  }
  warp_preview_patches_ = qimage_patches_from_document_region_with_layer_pixels(
      composite_document, QRegion(patch_rect), true, *warp_layer_id_, warped_pixels, warped.bounds);
  for (auto& patch : warp_preview_patches_) {
    patch.image = patch.image.convertToFormat(QImage::Format_RGBA8888);
    if (level >= 1) {
      // Full-res document_rect for the update/paint math; the image is its mip.
      patch.document_rect = QRect(patch.document_rect.x() << level, patch.document_rect.y() << level,
                                  patch.document_rect.width() << level, patch.document_rect.height() << level)
                                .intersected(canvas_rect);
    }
  }
  warp_preview_patches_scale_level_ = level;
  ++render_cache_diagnostics_.warp_preview_renders;
  if (level >= 1) {
    ++render_cache_diagnostics_.warp_scaled_previews;
  }
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
  if (smart_filter_rerender_failed) {
    report_status_error(tr("Could not rebuild the Smart Filter preview and cache"));
  } else if (status_callback_) {
    status_callback_(changed ? tr("Warped layer")
                             : tr("Warp Transform cancelled"));
  }
  notify_transform_controls_changed();
}

void CanvasWidget::reset_warp_state() {
  warping_layer_ = false;
  clear_transform_session_steps();
  dragging_warp_handle_ = false;
  warp_drag_index_ = -1;
  warp_layer_id_.reset();
  warp_target_smart_object_ = false;
  warp_mesh_ = WarpMeshGrid{};
  warp_original_mesh_ = WarpMeshGrid{};
  warp_style_ = QStringLiteral("warpCustom");
  warp_style_value_ = 0.0;
  warp_source_image_ = QImage();
  clear_warp_preview_caches();
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
      layer.set_pixels(pixels_from_image_native(baked.image));
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
  clear_transform_session_steps();
  transform_corner_offsets_ = {};
  transform_distort_mode_ = TransformDistortMode::None;
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
  transform_preview_patches_.clear();
  transform_preview_patches_rect_ = QRect();
  transform_drag_uses_proxy_preview_ = false;
  transform_live_frame_slow_ = false;
  transform_proxy_image_ = QImage();
  transform_drag_uses_clip_group_preview_ = false;
  transform_clip_group_image_ = QImage();
  transform_clip_group_rect_ = QRect();
  transform_mask_sources_.clear();
  transform_proxy_layer_opacity_ = 1.0;
  transform_requires_composited_preview_ = transform_preview_needs_compositing(*document_, *layer);
  transform_clip_group_preview_possible_ = transform_clip_group_preview_possible(*document_, *layer);
  rebuild_transform_base_cache();
  if (transform_requires_composited_preview_) {
    refresh_transform_composited_preview_cache();
  }
  setCursor(Qt::ArrowCursor);
  update();
  notify_transform_controls_changed();
  if (status_callback_) {
    status_callback_(shift_keeps_transform_aspect_
                         ? tr("Drag handles to transform. Shift keeps aspect ratio.")
                         : tr("Drag handles to transform. Shift resizes freely."));
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
  const auto delta = transform_session_delta();
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
  clear_transform_session_steps();
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
  clear_warp_preview_caches();
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
