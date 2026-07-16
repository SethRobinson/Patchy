#pragma once

#include "core/pattern_resource.hpp"
#include "core/vector_shape.hpp"

#include <cstdint>
#include <optional>

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

// Vector-mask coverage: the path's coverage with the mask's inverted flag
// applied (inverted masks cover clip minus path).
[[nodiscard]] CoverageBuffer rasterize_vector_mask_coverage(const LayerVectorMask& mask, Rect clip);

// Stroke coverage for the path with the stroke's width/caps/joins/dashes and
// alignment applied (inside/outside clip against the path's fill region, the
// centered-double-width construction). Ignores stroke.enabled so callers can
// preview; returns empty coverage for degenerate widths.
[[nodiscard]] CoverageBuffer rasterize_vector_stroke(const VectorPath& path, const VectorStroke& stroke,
                                                     const VectorRasterOptions& options);

struct ShapeRasterResult {
  Rect bounds{};
  PixelBuffer pixels{};  // straight-alpha RGBA8
};

// Rasterizes fill coverage and paints the fill appearance (solid, gradient
// via the shared blend_math shading, pattern via PatternTileSampler).
// `layer_for_pattern_anchor` supplies the fxrp anchor for linked pattern
// fills (may be null: anchors at the document origin). Stroke rendering
// arrives with the stroker; this paints the fill only when
// content.stroke.fill_enabled allows it.
[[nodiscard]] ShapeRasterResult rasterize_vector_shape(const VectorShapeContent& content, Rect canvas,
                                                       const PatternStore* patterns,
                                                       const Layer* layer_for_pattern_anchor);

// Bakes the layer's vector shape into pixels()/bounds() and stamps the
// raster-status metadata (the text-layer "pixels are a cache" contract). The
// compositor never rasterizes - these run at edit/import time only. No-ops
// when the layer carries no vector shape / vector mask.
void update_vector_shape_raster(Layer& layer, Rect canvas, const PatternStore* patterns);
// Regenerates the vector mask's grayscale cache from its path.
void update_vector_mask_raster(Layer& layer, Rect canvas);

}  // namespace patchy
