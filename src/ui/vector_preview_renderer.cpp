#include "ui/vector_preview_renderer.hpp"

#include "core/layer_metadata.hpp"
#include "core/layer_render_utils.hpp"
#include "core/smart_object.hpp"
#include "core/vector_raster.hpp"
#include "render/raster_view_context.hpp"
#include "ui/image_document_io.hpp"

#include <QCoreApplication>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <utility>

namespace patchy::ui {
namespace {

constexpr double kCoordinateLimit = 1'000'000.0;

bool safe_number(double value) { return std::isfinite(value) && std::abs(value) <= kCoordinateLimit; }

double stroke_padding(const VectorStroke& stroke) {
  return stroke.enabled ? stroke.width * (stroke.join == VectorStrokeJoin::Miter
      ? std::max(2.0, stroke.miter_limit) : 2.0) : 0.0;
}

void check_path(const VectorPath& path, double padding = 0.0) {
  if (!safe_number(padding) || padding < 0.0) { throw VectorPreviewFallback::Coordinates; }
  for (const auto& subpath : path.subpaths) {
    for (const auto& a : subpath.anchors) {
      for (const double value : {a.anchor_x, a.anchor_y, a.in_x, a.in_y, a.out_x, a.out_y}) {
        if (!safe_number(value) || std::abs(value) + padding > kCoordinateLimit) {
          throw VectorPreviewFallback::Coordinates;
        }
      }
    }
  }
}

Layer copy_render_properties(const Layer& source) {
  Layer layer(source.id(), {}, source.kind());
  if (source.kind() == LayerKind::Pixel) { layer.set_pixels(source.pixels()); }
  layer.set_bounds(source.bounds());
  layer.set_visible(source.visible());
  layer.set_clipped(source.clipped());
  layer.set_opacity(source.opacity());
  layer.set_fill_opacity(source.fill_opacity());
  layer.set_blend_mode(source.blend_mode());
  layer.set_blend_if_payload(source.raw_psd_blending_ranges(), source.blend_if_rgb_compatible());
  if (source.channel_restriction_supported()) { layer.set_restricted_channels(source.restricted_channels()); }
  else { layer.set_channel_restriction_unsupported(); }
  layer.layer_style() = source.layer_style();
  // Adjustment definitions and pattern reference points are render inputs.
  // PSD tagged blocks and Smart Object payloads are deliberately not copied.
  layer.metadata() = source.metadata();
  const auto reference = layer_effects_reference_point(source);
  set_layer_effects_reference_point(layer, reference[0], reference[1]);
  if (source.mask()) { layer.set_mask(*source.mask()); }
  if (const auto* mask = source.vector_mask()) { layer.set_vector_mask(*mask); }
  return layer;
}

void copy_nodes(const std::vector<Layer>& layers, std::vector<VectorPreviewNode>& out, bool& has_vectors) {
  for (const auto& source : layers) {
    VectorPreviewNode node;
    node.layer = copy_render_properties(source);
    node.source_bounds = source.kind() == LayerKind::Group ? layer_render_bounds(source) : layer_pixel_bounds(source);
    node.fill_bounds = node.stroke_bounds = node.source_bounds;
    if (source.visible() && source.opacity() > 0.0F) {
      if (source.kind() == LayerKind::Group) {
        copy_nodes(source.children(), node.children, has_vectors);
      } else if (const auto* shape = source.vector_shape(); shape && !layer_is_text(source) &&
                 !layer_is_smart_object(source) && vector_lock_reason(source).empty() &&
                 source.smart_filter_stack() == nullptr) {
        node.shape.emplace();
        node.shape->path = shape->path;
        node.shape->path_disabled = shape->path_disabled;
        node.shape->path_inverted = shape->path_inverted;
        node.shape->fill = shape->fill;
        node.shape->stroke = shape->stroke;
        // Split source planes are shared until the worker determines paint
        // bounds. They never become the enlarged shape's raster cache.
        node.shape->fill_cache = shape->fill_cache;
        node.shape->stroke_cache = shape->stroke_cache;
        const auto bounds = shape->path.bounds();
        const bool complement = shape->path_disabled || shape->path_inverted ||
            (!shape->path.subpaths.empty() && shape->path.subpaths.front().op == PathCombineOp::Subtract);
        if (bounds && !complement) {
          const double padding = stroke_padding(shape->stroke);
          node.bounds = QRectF(QPointF(bounds->left - padding, bounds->top - padding),
                               QPointF(bounds->right + padding, bounds->bottom + padding));
        }
        has_vectors = true;
      }
      if (const auto* mask = source.vector_mask(); mask && !mask->disabled) { has_vectors = true; }
    }
    // Hidden/empty clip bases still delimit clipping runs in the compositor.
    out.push_back(std::move(node));
  }
}

QRectF mapped_rect(Rect bounds, const VectorPreviewView& view) {
  return {QPointF(bounds.x, bounds.y) * view.scale + view.offset,
          QSizeF(bounds.width, bounds.height) * view.scale};
}

Rect checked_rect(QRectF rect) {
  if (!safe_number(rect.left()) || !safe_number(rect.top()) ||
      !safe_number(rect.right()) || !safe_number(rect.bottom())) { throw VectorPreviewFallback::Coordinates; }
  const auto r = rect.toAlignedRect();
  return {r.x(), r.y(), r.width(), r.height()};
}

template <typename T>
void scale_value(T& value, double scale) {
  const double scaled = static_cast<double>(value) * scale;
  if (!safe_number(scaled)) { throw VectorPreviewFallback::Coordinates; }
  value = static_cast<T>(scaled);
}

template <typename T>
void transform_pattern(T& scale, T& phase_x, T& phase_y, bool linked, const VectorPreviewView& view) {
  scale_value(scale, view.scale);
  scale_value(phase_x, view.scale);
  scale_value(phase_y, view.scale);
  if (!linked) {
    phase_x += static_cast<T>(view.offset.x());
    phase_y += static_cast<T>(view.offset.y());
  }
  if (!safe_number(phase_x) || !safe_number(phase_y)) { throw VectorPreviewFallback::Coordinates; }
}

void transform_paint(VectorFill& fill, const VectorPreviewView& view) {
  if (fill.kind == VectorFillKind::Pattern) {
    transform_pattern(fill.pattern_scale, fill.pattern_phase_x, fill.pattern_phase_y, fill.pattern_linked, view);
  }
}

int transform_style(LayerStyle& style, const VectorPreviewView& view) {
  if (!style.effects_visible) { return 0; }
  double reach = 0.0;
  const auto soft = [&](auto& effect) {
    if (!effect.enabled) { return; }
    scale_value(effect.distance, view.scale);
    scale_value(effect.size, view.scale);
    if (effect.enabled) { reach = std::max(reach, std::abs(effect.distance) + 2.0 * effect.size + 4.0); }
  };
  for (auto& effect : style.drop_shadows) { soft(effect); }
  for (auto& effect : style.inner_shadows) { soft(effect); }
  for (auto& effect : style.satins) { soft(effect); }
  const auto glow = [&](auto& effect) {
    if (!effect.enabled) { return; }
    scale_value(effect.size, view.scale);
    if (effect.enabled) { reach = std::max(reach, 2.0 * effect.size + 4.0); }
  };
  for (auto& effect : style.outer_glows) { glow(effect); }
  for (auto& effect : style.inner_glows) { glow(effect); }
  for (auto& effect : style.strokes) { glow(effect); }
  for (auto& effect : style.bevels) {
    if (!effect.enabled) { continue; }
    scale_value(effect.size, view.scale);
    scale_value(effect.soften, view.scale);
    if (effect.enabled) { reach = std::max(reach, 2.0 * (effect.size + effect.soften) + 4.0); }
    auto& texture = effect.texture;
    transform_pattern(texture.scale, texture.phase_x, texture.phase_y, texture.link_with_layer, view);
  }
  for (auto& pattern : style.pattern_overlays) {
    if (!pattern.enabled) { continue; }
    transform_pattern(pattern.scale, pattern.phase_x, pattern.phase_y, pattern.link_with_layer, view);
  }
  if (!safe_number(reach)) { throw VectorPreviewFallback::Coordinates; }
  return style.effects_visible ? static_cast<int>(std::ceil(reach)) : 0;
}

struct RasterBudget {
  std::uint64_t retained{};
  std::uint64_t peak{};
  std::uint64_t workspace{};
  void check(std::uint64_t temporary) {
    if (retained > kVectorPreviewRasterBudget || temporary > kVectorPreviewRasterBudget - retained) {
      throw VectorPreviewFallback::Memory;
    }
    peak = std::max(peak, retained + temporary);
  }
  void check_area(Rect area, std::uint64_t bytes_per_pixel) {
    const auto pixels = static_cast<std::uint64_t>(area.width) * area.height;
    if (bytes_per_pixel > kVectorPreviewRasterBudget ||
        pixels > kVectorPreviewRasterBudget / bytes_per_pixel) { throw VectorPreviewFallback::Memory; }
    check(pixels * bytes_per_pixel);
  }
  void check_workspace(std::uint64_t temporary) {
    workspace = std::max(workspace, temporary);
    check(workspace);
  }
  void keep(const PixelBuffer& pixels) {
    check(pixels.byte_size());
    retained += pixels.byte_size();
    check(workspace);
  }
};

std::uint64_t compositor_workspace_per_pixel(const LayerStyle& style) {
  if (!style.effects_visible || style.empty()) { return 64U; }
  const auto effects = style.drop_shadows.size() + style.inner_shadows.size() + style.outer_glows.size() +
      style.inner_glows.size() + style.strokes.size() + style.bevels.size() + style.satins.size();
  return 256U + 64U * effects;
}

Layer native_group_tree(const VectorPreviewNode& node, std::uint64_t& workspace) {
  auto layer = node.layer;
  if (node.shape) { layer.set_vector_shape(*node.shape); }
  std::uint64_t children_workspace = 0;
  for (const auto& child : node.children) {
    std::uint64_t child_workspace = 0;
    layer.children().push_back(native_group_tree(child, child_workspace));
    children_workspace = std::max(children_workspace, child_workspace);
  }
  workspace = compositor_workspace_per_pixel(std::as_const(layer).layer_style()) + children_workspace;
  return layer;
}

Rect native_gradient_bounds(const VectorShapeContent& shape, Rect canvas, bool stroke, RasterBudget& budget) {
  if (!stroke && (shape.path_disabled || shape.path.empty())) { return canvas; }
  check_path(shape.path, stroke ? stroke_padding(shape.stroke) : 0.0);
  const bool complement = shape.path_disabled || shape.path_inverted || shape.path.empty() ||
      shape.path.subpaths.front().op == PathCombineOp::Subtract;
  Rect reservation = canvas;
  if (!complement) {
    if (const auto bounds = shape.path.bounds()) {
      const double padding = stroke ? stroke_padding(shape.stroke) + 2.0 : 2.0;
      reservation = intersect_rect(canvas, checked_rect(QRectF(
          QPointF(bounds->left - padding, bounds->top - padding),
          QPointF(bounds->right + padding, bounds->bottom + padding))));
    }
  }
  if (reservation.empty()) { return {}; }
  budget.check_area(reservation, stroke ? 64U : 16U);
  if (stroke) { return rasterize_vector_stroke(shape.path, shape.stroke, {canvas}).bounds; }
  if (shape.path_inverted) {
    LayerVectorMask mask;
    mask.path = shape.path; mask.inverted = true;
    return rasterize_vector_mask_coverage(mask, canvas).bounds;
  }
  return rasterize_vector_path(shape.path, {canvas}).bounds;
}

int transform_nodes(std::vector<VectorPreviewNode>& nodes, const VectorPreviewView& view, Rect native_canvas,
                    render_detail::RasterViewContext& context, RasterBudget& budget, const PatternStore& patterns,
                    int inherited_padding = 0) {
  int padding = inherited_padding;
  for (auto& node : nodes) {
    const auto& source = std::as_const(node.layer);
    if (!source.visible() || source.opacity() <= 0.0F) { continue; }
    const auto original_style = source.layer_style();
    const bool aligned_paint = original_style.effects_visible && (
        std::any_of(original_style.gradient_fills.begin(), original_style.gradient_fills.end(),
                    [](const auto& fill) { return fill.enabled && fill.gradient.align_with_layer; }) ||
        std::any_of(original_style.strokes.begin(), original_style.strokes.end(),
                    [](const auto& stroke) { return stroke.enabled && stroke.uses_gradient && stroke.gradient.align_with_layer; }));
    Rect native_visible = node.source_bounds;
    if (aligned_paint && source.kind() == LayerKind::Group && !node.source_bounds.empty()) {
      std::uint64_t workspace = 0;
      const auto native_group = native_group_tree(node, workspace);
      budget.check_area(node.source_bounds, workspace);
      native_visible = group_visible_alpha_bounds(native_group, node.source_bounds, patterns);
    } else if (aligned_paint && source.kind() == LayerKind::Pixel) {
      native_visible = layer_visible_alpha_bounds(source, node.source_bounds).value_or(node.source_bounds);
    }
    const auto reference = layer_effects_reference_point(source);
    const double reference_x = reference[0] * view.scale + view.offset.x();
    const double reference_y = reference[1] * view.scale + view.offset.y();
    if (!safe_number(reference_x) || !safe_number(reference_y)) { throw VectorPreviewFallback::Coordinates; }
    set_layer_effects_reference_point(node.layer, reference_x, reference_y);
    int own_padding = transform_style(node.layer.layer_style(), view);
    if (const auto* mask = source.vector_mask(); mask && !mask->disabled) {
      auto updated = *mask;
      transform_vector_path(updated.path, {view.scale, 0, 0, view.scale, view.offset.x(), view.offset.y()});
      scale_value(updated.feather, view.scale);
      check_path(updated.path);
      own_padding += static_cast<int>(std::ceil(updated.feather * 6.0)) + 2;
      updated.cache = {};
      node.layer.set_vector_mask(std::move(updated));
    }
    const int combined_padding = inherited_padding + own_padding;
    if (combined_padding > kCoordinateLimit) { throw VectorPreviewFallback::Coordinates; }
    padding = std::max(padding, combined_padding);
    if (node.bounds) {
      *node.bounds = QRectF(node.bounds->topLeft() * view.scale + view.offset, node.bounds->size() * view.scale);
    } else if (!node.shape && source.kind() != LayerKind::Group && source.kind() != LayerKind::Adjustment) {
      node.bounds = mapped_rect(node.source_bounds, view);
    }
    if (node.bounds && !node.bounds->adjusted(-combined_padding - 2, -combined_padding - 2,
                                             combined_padding + 2, combined_padding + 2)
                           .intersects(QRectF(QPointF(), QSizeF(view.pixels)))) {
      node.layer.set_visible(false);
      continue;
    }
    const auto full = checked_rect(mapped_rect(node.source_bounds, view));
    const auto visible = checked_rect(mapped_rect(native_visible, view));
    context.appearance.emplace(source.id(), render_detail::RasterViewAppearance{
        full, visible, {}});
    if (source.kind() == LayerKind::Group) {
      padding = std::max(padding, transform_nodes(node.children, view, native_canvas, context, budget, patterns, combined_padding));
    }
    if (node.shape) {
      auto& shape = *node.shape;
      if (!shape.fill_cache.empty()) {
        node.fill_bounds = layer_visible_alpha_bounds(shape.fill_cache, node.source_bounds).value_or(node.source_bounds);
      }
      if (!shape.stroke_cache.empty()) {
        node.stroke_bounds = layer_visible_alpha_bounds(shape.stroke_cache, node.source_bounds).value_or(node.source_bounds);
      }
      context.appearance.at(source.id()).fill_visible_bounds = checked_rect(mapped_rect(node.fill_bounds, view));
      // Paint transparency must not shrink a gradient's geometry. Interior
      // overlays still use painted alpha, while the native fill/stroke ramp
      // uses the unpainted coverage bounds of the document-resolution bake.
      if (shape.fill.kind == VectorFillKind::Gradient && shape.fill.gradient.align_with_layer) {
        node.fill_bounds = native_gradient_bounds(shape, native_canvas, false, budget);
      }
      if (shape.stroke.enabled && shape.stroke.content.kind == VectorFillKind::Gradient &&
          shape.stroke.content.gradient.align_with_layer) {
        node.stroke_bounds = native_gradient_bounds(shape, native_canvas, true, budget);
      }
      node.fill_bounds = checked_rect(mapped_rect(node.fill_bounds, view));
      node.stroke_bounds = checked_rect(mapped_rect(node.stroke_bounds, view));
      shape.fill_cache = {}; shape.stroke_cache = {};
      transform_vector_path(shape.path, {view.scale, 0, 0, view.scale, view.offset.x(), view.offset.y()});
      scale_value(shape.stroke.width, view.scale);
      if (!safe_number(shape.stroke.dash_offset * shape.stroke.width) ||
          std::any_of(shape.stroke.dashes.begin(), shape.stroke.dashes.end(), [&](double dash) {
            return !safe_number(dash * shape.stroke.width);
          })) { throw VectorPreviewFallback::Coordinates; }
      check_path(shape.path, stroke_padding(shape.stroke));
      transform_paint(shape.fill, view);
      transform_paint(shape.stroke.content, view);
    }
  }
  return padding;
}


PixelBuffer sample_pixels(const PixelBuffer& source, Rect bounds, Rect clip, const VectorPreviewView& view,
                          RasterBudget& budget, bool mask = false, std::uint8_t default_color = 0) {
  const auto channels = mask ? 1U : 4U;
  budget.check_workspace(static_cast<std::uint64_t>(clip.width) * clip.height * channels);
  PixelBuffer output(clip.width, clip.height, mask ? PixelFormat::gray8() : PixelFormat::rgba8());
  for (int y = 0; y < clip.height; ++y) {
    auto row = output.row(y);
    const double sy = std::floor((clip.y + y + 0.5 - view.offset.y()) / view.scale) - bounds.y;
    for (int x = 0; x < clip.width; ++x) {
      const double sx = std::floor((clip.x + x + 0.5 - view.offset.x()) / view.scale) - bounds.x;
      auto* pixel = row.data() + x * channels;
      if (sx < 0 || sy < 0 || sx >= source.width() || sy >= source.height() || source.empty()) {
        if (mask) { pixel[0] = default_color; }
        continue;
      }
      const auto* sample = source.pixel(static_cast<int>(sx), static_cast<int>(sy));
      if (mask) { pixel[0] = sample[0]; }
      else if (source.format().bit_depth == BitDepth::UInt8 && source.format().channels >= 3) {
        std::memcpy(pixel, sample, 3);
        pixel[3] = source.format().channels >= 4 ? sample[3] : 255;
      }
    }
  }
  budget.keep(output);
  return output;
}

void raster_nodes(const std::vector<VectorPreviewNode>& nodes, std::vector<Layer>& layers, Rect area,
                  const VectorPreviewView& view, Rect canvas, const PatternStore& patterns,
                  RasterBudget& budget, int depth, const std::atomic_bool* cancelled) {
  const auto area_pixels = static_cast<std::uint64_t>(area.width) * area.height;
  for (const auto& node : nodes) {
    if (cancelled && cancelled->load(std::memory_order_relaxed)) { return; }
    const auto& source = node.layer;
    auto layer = source;
    // Never retain full-size source planes in the compositor's temporary tree.
    if (source.kind() == LayerKind::Pixel) { layer.set_pixels({}); }
    layer.clear_mask();
    layer.clear_vector_mask();
    if (!source.visible() || source.opacity() <= 0.0F ||
        (node.bounds && !node.bounds->adjusted(-2, -2, 2, 2).intersects(QRectF(area.x, area.y, area.width, area.height)))) {
      layers.push_back(std::move(layer));
      continue;
    }
    const bool styled = source.layer_style().effects_visible && !source.layer_style().empty();
    const auto workspace_per_pixel = compositor_workspace_per_pixel(source.layer_style());
    budget.check_workspace(area_pixels * workspace_per_pixel * static_cast<std::uint64_t>(depth + 1));
    if (source.kind() == LayerKind::Group) {
      raster_nodes(node.children, layer.children(), area, view, canvas, patterns, budget, depth + 1, cancelled);
    } else if (node.shape) {
      const VectorPaintBounds paint_bounds{canvas, node.fill_bounds, node.stroke_bounds};
      auto raster = rasterize_vector_shape(*node.shape, intersect_rect(area, canvas), &patterns, &source, &paint_bounds);
      budget.keep(raster.pixels);
      layer.set_pixels(std::move(raster.pixels));
      layer.set_bounds(raster.bounds);
      // Interior effects tint the fill, then the native vector stroke is
      // stamped above them. Keep the compositor's split-plane contract.
      if (styled && (!raster.fill_pixels.empty() || !raster.stroke_pixels.empty())) {
        budget.keep(raster.fill_pixels); budget.keep(raster.stroke_pixels);
        VectorShapeContent shape;
        shape.stroke = node.shape->stroke;
        shape.fill_cache = std::move(raster.fill_pixels);
        shape.stroke_cache = std::move(raster.stroke_pixels);
        layer.set_vector_shape(std::move(shape));
      }
    } else if (source.kind() == LayerKind::Pixel) {
      const auto clipped = mapped_rect(node.source_bounds, view).intersected(QRectF(area.x, area.y, area.width, area.height));
      if (!clipped.isEmpty()) {
        const auto bounds = checked_rect(clipped);
        auto pixels = sample_pixels(source.pixels(), node.source_bounds, bounds, view, budget);
        layer.set_pixels(std::move(pixels));
        layer.set_bounds(bounds);
      }
    } else {
      layer.set_bounds(checked_rect(mapped_rect(node.source_bounds, view)));
    }
    if (const auto& mask = source.mask(); mask && !mask->disabled) {
      auto pixels = sample_pixels(mask->pixels, mask->bounds, area, view, budget, true, mask->default_color);
      layer.set_mask(LayerMask{area, std::move(pixels), mask->default_color, false});
    }
    if (const auto* mask = source.vector_mask(); mask && !mask->disabled) {
      layer.set_vector_mask(*mask);
      update_vector_mask_raster(layer, area);
      budget.keep(std::as_const(layer).vector_mask()->cache);
    }
    layers.push_back(std::move(layer));
  }
}

}  // namespace

QString vector_preview_fallback_text(VectorPreviewFallback reason) {
  const auto tr = [](const char* text) { return QCoreApplication::translate("VectorPreview", text); };
  switch (reason) {
    case VectorPreviewFallback::None: return {};
    case VectorPreviewFallback::Content:
    case VectorPreviewFallback::Paint:
    case VectorPreviewFallback::Masks:
    case VectorPreviewFallback::Blending:
    case VectorPreviewFallback::Effects:
      return tr("Dynamic Vector Preview: using the document's pixel view.");
    case VectorPreviewFallback::Coordinates: return tr("Pixel view: vector coordinates exceed the preview range at this zoom.");
    case VectorPreviewFallback::Memory: return tr("Pixel view: Dynamic Vector Preview reached its memory limit.");
    case VectorPreviewFallback::Failed: return tr("Pixel view: Dynamic Vector Preview could not render this view.");
  }
  return {};
}

VectorPreviewScene build_vector_preview_scene(const Document& document) {
  VectorPreviewScene scene;
  scene.canvas = {document.width(), document.height()};
  if (document.palette_editing() || document.format().color_mode != ColorMode::RGB ||
      document.format().bit_depth != BitDepth::UInt8) {
    scene.fallback = VectorPreviewFallback::Content;
  } else {
    scene.patterns = document.metadata().patterns;
    copy_nodes(document.layers(), scene.layers, scene.has_vectors);
  }
  return scene;
}

VectorPreviewResult render_vector_preview(const VectorPreviewScene& scene, const VectorPreviewView& view,
                                         std::uint64_t retained_frame_bytes, const std::atomic_bool* cancelled) {
  VectorPreviewResult result;
  const auto start = std::chrono::steady_clock::now();
  RasterBudget budget{retained_frame_bytes, retained_frame_bytes};
  try {
    if (scene.fallback != VectorPreviewFallback::None) { throw scene.fallback; }
    if (view.pixels.isEmpty() || !std::isfinite(view.scale) || view.scale <= 0.0 ||
        !std::isfinite(view.offset.x()) || !std::isfinite(view.offset.y())) { throw VectorPreviewFallback::Coordinates; }
    const auto output_bytes = static_cast<std::uint64_t>(view.pixels.width()) * view.pixels.height() * 4U;
    budget.check(output_bytes);
    budget.retained += output_bytes;
    auto nodes = scene.layers;
    render_detail::RasterViewContext context;
    const int padding = transform_nodes(nodes, view, {0, 0, scene.canvas.width(), scene.canvas.height()},
                                        context, budget, scene.patterns);
    const auto canvas = checked_rect(mapped_rect({0, 0, scene.canvas.width(), scene.canvas.height()}, view));
    const render_detail::ScopedRasterViewContext scope(context);
    QImage image(view.pixels, QImage::Format_RGBA8888);
    if (image.isNull()) { throw VectorPreviewFallback::Memory; }
    image.fill(Qt::transparent);
    for (int y = 0; y < view.pixels.height(); y += kVectorPreviewTileSize) {
      for (int x = 0; x < view.pixels.width(); x += kVectorPreviewTileSize) {
        if (cancelled && cancelled->load(std::memory_order_relaxed)) { return result; }
        const Rect tile{x, y, std::min(kVectorPreviewTileSize, view.pixels.width() - x),
                        std::min(kVectorPreviewTileSize, view.pixels.height() - y)};
        const auto area = outset_rect(tile, padding);
        const auto base_bytes = budget.retained;
        budget.workspace = 0;
        // Surfaces, including effect/feather halos, are checked before
        // rasterization. No enlarged full-document bitmap is allocated.
        budget.check_workspace(static_cast<std::uint64_t>(area.width) * area.height * 64U);
        Document scratch(view.pixels.width(), view.pixels.height(), PixelFormat::rgba8());
        scratch.metadata().patterns = scene.patterns;
        raster_nodes(nodes, scratch.layers(), area, view, canvas, scene.patterns, budget, 0, cancelled);
        if (cancelled && cancelled->load(std::memory_order_relaxed)) { return result; }
        const auto rendered = qimage_from_document_rect(scratch, QRect(x, y, tile.width, tile.height), true);
        if (rendered.isNull()) { throw VectorPreviewFallback::Memory; }
        for (int row = 0; row < tile.height; ++row) {
          std::memcpy(image.scanLine(y + row) + x * 4, rendered.constScanLine(row), static_cast<std::size_t>(tile.width) * 4U);
        }
        budget.retained = base_bytes;
      }
    }
    result.image = std::move(image);
  } catch (VectorPreviewFallback reason) { result.fallback = reason;
  } catch (const std::bad_alloc&) { result.fallback = VectorPreviewFallback::Memory;
  } catch (...) { result.fallback = VectorPreviewFallback::Failed; }
  result.peak_raster_bytes = budget.peak;
  result.elapsed_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  return result;
}

}  // namespace patchy::ui
