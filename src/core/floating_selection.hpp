#pragma once

// Free Transform of the selected pixels (GitHub issue 399; docs/free-transform.md,
// "Transforming the selected pixels"): the selection is lifted out of a pixel layer
// into a floating buffer, transformed, and composited back onto the layer.
//
// Both functions work on straight RGBA rows at the buffers' own depth (8, 16 or
// 32-bit; the 32-bit rows stay linear), so a hard selection lifts and lands back
// byte for byte.

#include "core/layer.hpp"
#include "core/pixel_buffer.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace patchy {

struct LiftedSelection {
  // The selected pixels over `floating_bounds` (document space): the layer's
  // pixels with alpha times the selection coverage, always with an alpha channel.
  PixelBuffer floating;
  Rect floating_bounds{};
  // The layer's pixels over its own bounds with the selection taken out: alpha
  // times (1 - coverage), or, with `fill` set, the color blended toward `fill` by
  // the coverage with alpha kept (a transparency-locked layer, the Background).
  PixelBuffer remainder;
};

// `coverage` is a gray8 plane over `coverage_bounds` (document space, 255 = fully
// selected; outside it nothing is selected). `fill` is an 8-bit sRGB color.
// The floating rect is `coverage_bounds` clipped to the layer; it is empty when
// they do not meet.
[[nodiscard]] LiftedSelection lift_selected_pixels(const PixelBuffer& pixels, Rect bounds,
                                                   const PixelBuffer& coverage, Rect coverage_bounds,
                                                   const std::optional<std::array<std::uint8_t, 3>>& fill);

// `floating` over `base` with the Normal blend, into a new buffer in `base`'s
// format covering `output_bounds` (document space; outside both inputs it is
// transparent). The two buffers share a bit depth. `preserve_alpha` keeps the
// base's alpha and blends only color by the floating alpha (transparency lock).
[[nodiscard]] PixelBuffer composite_floating_pixels(const PixelBuffer& base, Rect base_bounds,
                                                    const PixelBuffer& floating, Rect floating_bounds,
                                                    Rect output_bounds, bool preserve_alpha);

}  // namespace patchy
