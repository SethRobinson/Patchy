#pragma once

#include "core/pixel_tools.hpp"
#include "core/vector_shape.hpp"

#include <cstdint>
#include <vector>

// The Rectangle and Ellipse tools' Pixels mode with a fill and a stroke of their own
// (GitHub issue 716): what to paint, and the renderer for everything the classic
// signed-distance renderer (draw_rectangle / draw_ellipse) does not draw. Geometry comes
// from the live-shape generators and coverage from the vector fill and stroke rasterizers,
// so dashes and stroke alignment are the ones a shape layer gets (docs/tools.md,
// "Shapes in Pixels mode").
namespace patchy {

struct PixelShapeStyle {
  bool fill{false};
  bool stroke{true};
  // Alpha is the paint opacity, as EditOptions::primary's. The gains are the colors'
  // linear-light multipliers on a 32-bit layer (EditOptions::primary_gain).
  EditColor fill_color{};
  EditColor stroke_color{};
  float fill_gain{1.0F};
  float stroke_gain{1.0F};
  VectorStrokeAlignment alignment{VectorStrokeAlignment::Center};
  StrokeDashPreset dashes{StrokeDashPreset::Solid};
};

// True for the two styles the classic renderer has always drawn: a fill alone, or a solid
// stroke alone centered on the outline. Those keep draw_rectangle / draw_ellipse, bit for
// bit (EditOptions::fill_shapes picks which); every other style goes to draw_styled_shape.
[[nodiscard]] bool pixel_shape_style_is_classic(const PixelShapeStyle& style) noexcept;

// The stroke draw_styled_shape rasterizes: options.brush_size wide, the style's alignment
// and dash preset (with the caps the preset sets), round corners like the classic outline.
[[nodiscard]] VectorStroke pixel_shape_stroke(const EditOptions& options, const PixelShapeStyle& style);

// Fill and stroke coverage of the shape over `bounds` (inside `clip`), 0..255 row-major.
// A plane the style does not draw is empty. options.brush_softness blurs both planes
// (the classic renderer's Soft band, as a gaussian); the layer writer below and the
// canvas's mask and channel writer composite from it.
struct PixelShapeCoverage {
  Rect bounds{};
  std::vector<std::uint8_t> fill;
  std::vector<std::uint8_t> stroke;
};
[[nodiscard]] PixelShapeCoverage pixel_shape_coverage(Rect rect, ShapeKind kind, const EditOptions& options,
                                                      const PixelShapeStyle& style, Rect clip);

// Paints the shape into the layer: the stroke over the fill, flattened first so each pixel
// is written once at the style's opacity. `options` supplies the stroke width
// (brush_size), Soft, the corner radius, the selection, the transparency lock and the
// palette constraint, as for draw_rectangle. Returns the dirty rect.
[[nodiscard]] Rect draw_styled_shape(Document& document, LayerId layer_id, Rect rect, const EditOptions& options,
                                     const PixelShapeStyle& style, ShapeKind kind, bool erase);

}  // namespace patchy
