#pragma once

#include "core/pattern_resource.hpp"
#include "core/vector_shape.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

// Deterministic scanline rasterizer for vector paths (fills; the stroker
// builds on it). Inputs are quantized once to 24.8 fixed point, and every
// coverage computation after that is integer-only, so output is bit-identical
// across toolchains (the cross-platform determinism rule). Anti-aliasing is
// exact-area cell coverage (the FreeType-smooth family, reimplemented from
// the algorithm idea with Patchy's own conventions).
//
// Semantics pinned against Photoshop 27.8 (docs/vector-tools.md):
// - subpaths sharing a shape_group rasterize together under EVEN-ODD winding;
// - groups combine sequentially by their op over accumulated coverage
//   (soft-boolean: add/union, subtract, intersect, xor);
// - a first group with op Subtract starts from a fully-covered canvas; any
//   other first op yields exactly the group's own coverage;
// - open subpaths fill their implied closing chord;
// - an EMPTY path means "cover everything" (fill layers without a mask).
namespace patchy {

struct CoverageBuffer {
  Rect bounds{};        // document space
  PixelBuffer pixels{}; // gray8, bounds.width x bounds.height
};

struct VectorRasterOptions {
  Rect clip{};  // usually the canvas rect; coverage is clipped to it
};

// Rasterizes the whole path (groups + combine ops) into gray8 coverage with
// tight bounds. Returns an empty buffer when nothing is covered.
[[nodiscard]] CoverageBuffer rasterize_vector_path(const VectorPath& path,
                                                   const VectorRasterOptions& options);

// The document-space polyline the fill and stroke rasterizers work from
// (adaptive cubic flattening on the 1/256 lattice, anchors included; a
// closed subpath omits the repeated closing point). Simplify Path refits it.
[[nodiscard]] std::vector<std::array<double, 2>> flatten_subpath_polyline(const PathSubpath& subpath);

// Vector-mask coverage: the path's coverage with the mask's inverted flag
// applied (inverted masks cover clip minus path).
[[nodiscard]] CoverageBuffer rasterize_vector_mask_coverage(const LayerVectorMask& mask, Rect clip);

// Stroke coverage for the path with the stroke's width/caps/joins/dashes and
// alignment applied (inside/outside clip a double-width band against the fill
// region, with original-width dash caps on each side). Ignores stroke.enabled so callers can
// preview; returns empty coverage for degenerate widths.
[[nodiscard]] CoverageBuffer rasterize_vector_stroke(const VectorPath& path, const VectorStroke& stroke,
                                                     const VectorRasterOptions& options);

struct ShapeRasterResult {
  Rect bounds{};
  PixelBuffer pixels{};  // straight-alpha RGBA8
  // Split planes over the same `bounds` for style compositing: interior
  // overlay effects apply to the FILL plane and the vector stroke composites
  // above them (PS 2026 probes fx-sofi-*, docs/vector-tools.md). Populated
  // only when a fill AND a Normal-blend stroke both rendered; empty
  // otherwise, in which case the compositor keeps the combined-plane path.
  PixelBuffer fill_pixels{};
  PixelBuffer stroke_pixels{};
  // `pixels` with alpha replaced by the shape's coverage, over the same `bounds`: the
  // silhouette layer effects use (VectorShapeContent::effect_matte_cache). Populated
  // only when a gradient or pattern fill leaves coverage and painted alpha different.
  PixelBuffer matte_pixels{};
};

// Optional paint geometry for a render whose clip is not the canvas: a
// clipped display tile, or the document bake's extended domain. Coverage
// still uses the requested clip; gradients and patterns keep their canvas
// anchor, and the aligned fill/stroke bounds when given, instead of
// restarting inside the clip. An absent fill/stroke rect aligns to the
// painted coverage as usual.
struct VectorPaintBounds {
  Rect canvas;
  std::optional<Rect> fill;
  std::optional<Rect> stroke;
};

// Rasterizes fill coverage and paints the fill appearance (solid, gradient
// via the shared blend_math shading, pattern via PatternTileSampler).
// `layer_for_pattern_anchor` supplies the fxrp anchor for linked pattern
// fills (may be null: anchors at the document origin). Stroke rendering
// arrives with the stroker; this paints the fill only when
// content.stroke.fill_enabled allows it.
[[nodiscard]] ShapeRasterResult rasterize_vector_shape(const VectorShapeContent& content, Rect canvas,
                                                       const PatternStore* patterns,
                                                       const Layer* layer_for_pattern_anchor,
                                                       const VectorPaintBounds* paint_bounds = nullptr);

// Bakes the layer's vector shape into pixels()/bounds() and stamps the
// raster-status metadata (the text-layer "pixels are a cache" contract). The
// compositor never rasterizes - these run at edit/import time only. No-ops
// when the layer carries no vector shape / vector mask.
void update_vector_shape_raster(Layer& layer, Rect canvas, const PatternStore* patterns);
// For a shape layer that keeps the pixels its file carried (a PSD import whose raster
// has visible alpha is not re-baked): works out the effect silhouette from the shape
// and stores it beside those pixels, or clears it when coverage and alpha agree.
void refresh_vector_shape_effect_matte(Layer& layer, Rect canvas, const PatternStore* patterns = nullptr);

// 16 and 32-bit rendering (docs/high-bit-depth.md): a gradient-only shape's pixels at
// `depth`, over the layer's own bounds, with unrounded gradient colors (interpolated in
// linear light for 32-bit) and unrounded stop opacity. Fill layers and gradient shapes
// without a stroke, feather, density or parts qualify (an unaligned gradient only when
// the fill covers its whole bake domain); null for everything else, whose 8-bit raster
// the deep compositor reads instead. Cached by content revision.
[[nodiscard]] std::shared_ptr<const PixelBuffer> deep_gradient_fill_raster(const Layer& layer, BitDepth depth);
// Regenerates the vector mask's grayscale cache from its path.
void update_vector_mask_raster(Layer& layer, Rect canvas);

}  // namespace patchy
