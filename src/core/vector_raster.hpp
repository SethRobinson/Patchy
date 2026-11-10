#pragma once

#include <span>

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
// Semantics pinned against Photoshop 27.8 (docs/vector-psd-encodings.md):
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
  bool align_stroke_edges{false};  // Photoshop's native sn2P / Align Edges flag
  // Curves outside the clip are flattened to their chords, stroke pieces outside it are
  // skipped, and a combined path's stroke outline uses a sweep and a per-thread cache
  // (same pixels, far less work for a zoomed shape), and a path that is one pixel-aligned
  // rectangle is filled without scan conversion. True turns all of that off: the tests'
  // reference (docs/vector-preview.md, "Clipped flattening").
  bool reference_geometry{false};
};

// Rasterizes the whole path (groups + combine ops) into gray8 coverage with
// tight bounds. Returns an empty buffer when nothing is covered.
[[nodiscard]] CoverageBuffer rasterize_vector_path(const VectorPath& path,
                                                   const VectorRasterOptions& options);

// The document-space polyline the fill and stroke rasterizers work from
// (adaptive cubic flattening on the 1/256 lattice, anchors included; a
// closed subpath omits the repeated closing point). Simplify Path refits it.
[[nodiscard]] std::vector<std::array<double, 2>> flatten_subpath_polyline(const PathSubpath& subpath);

// The outline of the path's combined fill (each group's even-odd fill combined in order by the
// groups' ops, the rasterizer's rule; open subpaths close with their chord) as closed polylines
// on the 1/256 lattice, inside on the left in y-down coordinates (outer loops clockwise, holes
// counterclockwise). The same boundary walk the stroker follows. Merge Shapes refits it.
[[nodiscard]] std::vector<std::vector<std::array<double, 2>>> vector_path_fill_contours(const VectorPath& path);

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
  // above them (PS 2026 probes fx-sofi-*, docs/layer-effects-render.md). Populated
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

// The clip update_vector_shape_raster draws a shape's coverage in: the bake domain (the
// canvas, grown to hold a shape that reaches past it) and the feather's reach around it.
// A layer-aligned gradient spans its paint's coverage bounds inside this clip, and a fill
// with Density under 100% covers all of it, so a renderer that draws the same shape at
// another scale (Dynamic Vector Preview) takes the gradient's box from here.
[[nodiscard]] Rect vector_shape_coverage_domain(const VectorShapeContent& content, Rect canvas);

// Bakes the layer's vector shape into pixels()/bounds() and stamps the
// raster-status metadata (the text-layer "pixels are a cache" contract). The
// compositor never rasterizes - these run at edit/import time only. No-ops
// when the layer carries no vector shape / vector mask.
void update_vector_shape_raster(Layer& layer, Rect canvas, const PatternStore* patterns);
// The rectangle a gradient fill's placement is measured over (gradient_position's bounds), as
// update_vector_shape_raster bakes it: the canvas without Align with Layer; with it, the
// visible user-mask bounds of a pathless fill layer (fill_layer_mask_bounds), else the
// fill's coverage bounds. Rasterizes the path, so callers cache it (the Gradient tool's
// on-canvas handles, GitHub issue 663).
[[nodiscard]] Rect gradient_fill_alignment_bounds(const Layer& layer, Rect canvas);
// A draft of update_vector_shape_raster for a pathless 8-bit Gradient Fill layer, for the
// Gradient tool's handle drags (GitHub issue 663 follow-up): the ramp position is evaluated
// exactly on a 4-pixel grid and interpolated between (exactly per pixel where a cell spans
// an Angle seam or a sharp turn), colors come from a 4096-entry table and dither is off, so it
// costs a small fraction of the exact bake and differs from it by about a level. The layer's
// shape must already hold the new gradient; `gradient_bounds` is
// gradient_fill_alignment_bounds for it. False, with nothing changed, when the layer is
// not a full-canvas gradient fill (a path, feather, density, parts) or the canvas is small
// enough to bake exactly; the drag's release always bakes exactly.
bool update_gradient_fill_draft_raster(Layer& layer, Rect canvas, Rect gradient_bounds);
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
// The same coverage, re-rasterized only inside the `dirty` rects (where an edited
// path's boundary swept): pixels outside them keep the previous cache, the cache
// bounds grow to cover them. Byte-identical to update_vector_mask_raster where the
// two caches overlap. Falls back to the full update for a feathered mask, an
// empty path or a missing cache. Interactive path drags use it per move.
void update_vector_mask_raster_region(Layer& layer, Rect canvas, std::span<const Rect> dirty);

}  // namespace patchy
