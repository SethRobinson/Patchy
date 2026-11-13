#pragma once

#include "core/pattern_resource.hpp"
#include "core/pixel_tools.hpp"
#include "core/vector_raster.hpp"
#include "core/vector_shape.hpp"

// The shape tools' Pixels mode with a shape layer's fill and stroke (GitHub issue 716): what
// to paint, and the renderer for everything the classic signed-distance renderer
// (draw_rectangle / draw_ellipse) does not draw. The shape is drawn by rasterize_vector_shape,
// the shape layer's own renderer, so solid, gradient and pattern paints, dashes, caps, corners
// and stroke alignment are the ones a shape layer gets, for any path (Rectangle, Ellipse,
// Polygon, Custom Shape). docs/tools.md, "Shapes in Pixels mode".
namespace patchy {

struct PixelShapeStyle {
  // What paints the inside: kind None draws no fill.
  VectorFill fill{.kind = VectorFillKind::None};
  // The outline: drawn when it is enabled, wider than 0 and its paint (content) is not None.
  // Width, alignment, caps, corners, dashes and dash offset are the shape layer's.
  VectorStroke stroke{};
  // The paints' linear-light multiplier on a 32-bit layer (EditOptions::primary_gain): the
  // foreground's Intensity when every drawn paint is the foreground color, else 1.
  float gain{1.0F};
};

[[nodiscard]] bool pixel_shape_style_draws_fill(const PixelShapeStyle& style) noexcept;
[[nodiscard]] bool pixel_shape_style_draws_stroke(const PixelShapeStyle& style) noexcept;

// True for the two styles the classic renderer has always drawn on a rectangle or an ellipse:
// a solid fill alone, or a solid stroke alone that is centered on the outline, undashed and a
// whole number of pixels wide. Those keep draw_rectangle / draw_ellipse, bit for bit
// (EditOptions::fill_shapes picks which); every other style goes to draw_styled_shape.
[[nodiscard]] bool pixel_shape_style_is_classic(const PixelShapeStyle& style) noexcept;

// The path of a Pixels-mode rectangle or ellipse: the whole pixels of `rect`, the box the
// classic renderer measures, with the rectangle's corner radius.
[[nodiscard]] VectorPath pixel_shape_path(Rect rect, ShapeKind kind, int corner_radius);

// Soft for the styled renderer, as a feather (gaussian sigma, pixels): the classic renderer
// eases coverage over a band of half options.brush_size times Soft; a fifth of that band has
// the same 10% to 90% width. 0 while the band is under a pixel.
[[nodiscard]] double pixel_shape_soft_feather(const EditOptions& options) noexcept;

// The shape as a shape layer would bake it over `canvas`: straight-alpha RGBA8 with the fill,
// the stroke over it, and `feather`. Empty when nothing is covered. The layer writer below
// and the canvas's mask and channel writer composite from it.
[[nodiscard]] ShapeRasterResult pixel_shape_raster(const VectorPath& path, const PixelShapeStyle& style,
                                                   double feather, Rect canvas, const PatternStore* patterns);

// Paints the shape into the layer, each pixel once at the opacity in options.primary's alpha.
// `options` supplies Soft (with brush_size, the stroke's width), the selection, the
// transparency lock and the palette constraint, as for draw_rectangle. `patterns` holds the
// tiles of pattern paints (may be null without one). Returns the dirty rect.
[[nodiscard]] Rect draw_styled_shape(Document& document, LayerId layer_id, const VectorPath& path,
                                     const EditOptions& options, const PixelShapeStyle& style,
                                     const PatternStore* patterns, bool erase);

}  // namespace patchy
