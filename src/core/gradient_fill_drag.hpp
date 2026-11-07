#pragma once

#include "core/layer.hpp"
#include "core/pixel_tools.hpp"
#include "core/rect_utils.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace patchy {

// The Gradient tool's Fill Layer mode (GitHub issue 580; docs/gradients.md, "Gradient tool
// Fill Layer mode"): the Gradient Fill whose GdFl geometry (gradient_position with
// GradientSpanBasis::CenterChord over `canvas`) renders what draw_gradient paints for the same
// drag and stops. Align with Layer is off, so a mask on the layer never moves the ramp; the
// stops keep their colors and alpha (the tool's Opacity is the caller's layer opacity) and
// Classic smoothness 0 interpolates them linearly like the tool. A zero-length drag returns
// the canvas-wide default placement; callers skip it.
[[nodiscard]] LayerStyleGradient gradient_fill_from_drag(const std::vector<GradientStop>& stops,
                                                         GradientMethod method, bool reverse, Rect canvas,
                                                         std::int32_t x0, std::int32_t y0, std::int32_t x1,
                                                         std::int32_t y1);

// The Gradient tool's two-track gradient (GitHub issue 663, one gradient editor).
// gradient_from_tool_stops reads the old flat stops (tools/gradientStops, scripts): each stop
// becomes a color stop (its color, midpoint and Intensity) and an opacity stop (its alpha and
// midpoint) at the same location, smoothness 0, so it renders what the flat stops did.
[[nodiscard]] LayerStyleGradient gradient_from_tool_stops(const std::vector<GradientStop>& stops);
// The tool's gradient with its Foreground and Background stops taking the current colors and
// their 32-bit Intensity (the kinds stay live in the tool, so a color change follows).
[[nodiscard]] LayerStyleGradient resolved_tool_gradient(const GradientDefinition& definition, EditColor foreground,
                                                        EditColor background, float foreground_intensity,
                                                        float background_intensity);
// gradient_fill_from_drag for the tool's two-track gradient (resolved): the fill takes its
// stops, midpoints, smoothness and form as they are (no conversion), Intensity dropped.
[[nodiscard]] LayerStyleGradient gradient_fill_from_drag(const LayerStyleGradient& resolved, GradientMethod method,
                                                         bool reverse, Rect canvas, std::int32_t x0, std::int32_t y0,
                                                         std::int32_t x1, std::int32_t y1);

// The Gradient tool's flat stops for a gradient (GitHub issue 663 follow-up): a Solid
// gradient keeps its own stops, the color and opacity stop locations merged into one list
// (each merged stop takes the color and opacity the gradient has there) and each midpoint
// kept on its stop. Where one merged segment would need two different midpoints (a color
// and an opacity midpoint that disagree, or a midpoint whose segment another list splits),
// that segment alone is written out in eight straight pieces. The tool interpolates
// straight, so smoothness is not carried. Noise gradients are sampled at 65 points. The
// Foreground and Background kinds must already be resolved; intensity stays 0.
[[nodiscard]] std::vector<GradientStop> gradient_tool_stops_from_gradient(const LayerStyleGradient& gradient);

// On-canvas editing of a Gradient Fill (GitHub issue 663; docs/gradients-fill-handles.md, "Gradient Fill
// handles"). The line runs from the start handle (the stops' location 0, or 1 when reversed)
// to the end handle in document pixels, the drag that gradient_fill_from_drag would turn into
// this placement over `bounds` (the canvas, or the aligned layer bounds with Align with Layer).
struct GradientFillLine {
  double start_x{0.0};
  double start_y{0.0};
  double end_x{0.0};
  double end_y{0.0};

  friend bool operator==(const GradientFillLine&, const GradientFillLine&) = default;
};

// The inverse of the placement gradient_fill_from_drag stores: the start and end handles of
// `gradient` (type, angle, scale, offsets) over `bounds`. Linear runs through the center;
// the other styles start at it (Angle and Diamond half a pixel back, their pixel-center
// sampling). Angle keeps the length its Scale records, though it renders without one.
[[nodiscard]] GradientFillLine gradient_fill_line(const LayerStyleGradient& gradient, Rect bounds);

// Stores the placement whose gradient_fill_line over `bounds` is `line`: angle, scale (clamped
// to the dialog's 1% to 1000%) and X/Y offsets. Stops, type, reverse and alignment stay. A
// line of no length changes nothing.
void place_gradient_fill_line(LayerStyleGradient& gradient, Rect bounds, const GradientFillLine& line);

// Where (x, y) projects onto `line` as a stop location (0 at the start, 1 at the end, not
// clamped), mirrored when `reverse` puts location 0 at the end handle.
[[nodiscard]] double gradient_fill_line_location(const GradientFillLine& line, bool reverse, double x, double y);
// The document point of stop location `location` on `line` (the inverse of the above).
[[nodiscard]] std::pair<double, double> gradient_fill_line_point(const GradientFillLine& line, bool reverse,
                                                                 double location);

// Stop edits shared by the canvas handles. Locations clamp to 0..1 and both stop lists stay
// sorted by location (stable, so equal locations keep their order); a midpoint belongs to its
// destination stop and travels with it. Each returns the edited stop's index after sorting.
// Adding a color stop takes the gradient's color at that location (Classic, as rendered).
[[nodiscard]] std::size_t add_gradient_color_stop(LayerStyleGradient& gradient, float location);
[[nodiscard]] std::size_t add_gradient_alpha_stop(LayerStyleGradient& gradient, float location);
[[nodiscard]] std::size_t move_gradient_color_stop(LayerStyleGradient& gradient, std::size_t index, float location);
[[nodiscard]] std::size_t move_gradient_alpha_stop(LayerStyleGradient& gradient, std::size_t index, float location);
// Removes a stop; false (and no change) when it would leave fewer than two.
bool remove_gradient_color_stop(LayerStyleGradient& gradient, std::size_t index);
bool remove_gradient_alpha_stop(LayerStyleGradient& gradient, std::size_t index);
// A destination stop's midpoint as a fraction of the way from its previous stop, clamped
// like the Edit Gradient Stops dialog (5% to 95%). Index 0 has no midpoint: no change.
void set_gradient_color_midpoint(LayerStyleGradient& gradient, std::size_t index, float midpoint);
void set_gradient_alpha_midpoint(LayerStyleGradient& gradient, std::size_t index, float midpoint);

}  // namespace patchy
