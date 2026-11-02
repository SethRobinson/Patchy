#pragma once

#include "core/layer.hpp"
#include "core/pixel_tools.hpp"
#include "core/rect_utils.hpp"

#include <cstdint>
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

}  // namespace patchy
