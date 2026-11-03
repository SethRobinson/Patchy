#pragma once

#include "core/layer.hpp"
#include "core/pixel_buffer.hpp"

#include <cstdint>

// Select > Smooth and Select > Feather on a selection's 8-bit coverage plane (GitHub issue
// 614, docs/selection-modify.md). Integer math only, so every toolchain gives the same bytes.
namespace patchy {

inline constexpr int kMinSelectionSmoothRadius = 1;
inline constexpr int kMaxSelectionSmoothRadius = 500;
inline constexpr double kMinSelectionFeatherCommandRadius = 0.1;
inline constexpr double kMaxSelectionFeatherCommandRadius = 1000.0;

// Majority filter over a (2r+1) square window: a pixel ends up selected when more than half of
// the coverage in its window is, so corners round off and specks and holes narrower than about
// the radius disappear. Straight edges stay where they were. The window is clipped to the
// canvas (pixels outside it neither vote nor count), so a selection touching the canvas edge
// keeps that edge. Diagonal and curved edges come out anti-aliased (the vote's margin, scaled
// by the window width, gives a one-pixel ramp); a soft edge wider than the window steepens.
//
// `coverage` is gray8 over `area` (document pixels, inside a canvas of canvas_width by
// canvas_height); everything outside `area` counts as unselected. Callers pass the selection's
// bounds grown by the radius (clipped to the canvas), the only place the result can be
// non-zero. Cost is O(area) for any radius; memory is one row of 32-bit sums.
[[nodiscard]] PixelBuffer smooth_selection_mask(const PixelBuffer& coverage, Rect area, std::int32_t canvas_width,
                                                std::int32_t canvas_height, int radius);

// The box radius of each of Feather's three blur passes for a feather radius: (r + 1) / 2 as
// a real number (the marquee Feather option's rule at odd radii), r itself below 1 px.
[[nodiscard]] double selection_feather_pass_radius(double radius);

// How far a feather of `radius` spreads past the hard edge: grow the area by this before
// calling feather_selection_mask.
[[nodiscard]] int selection_feather_padding(double radius);

// Softens the coverage edge with three horizontal and vertical box passes of
// selection_feather_pass_radius (fractional radii weight the two outer taps), approximating a
// gaussian. Pixels outside the buffer count as unselected. O(area) for any radius.
void feather_selection_mask(PixelBuffer& coverage, double radius);

}  // namespace patchy
