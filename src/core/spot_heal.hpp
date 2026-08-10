#pragma once

#include "core/layer.hpp"

#include <cstdint>
#include <vector>

namespace patchy {

// Spot Healing source geometry: for every pixel covered by a stroke-footprint
// mask, pick the document pixel that heals it by MIRRORING across the nearest
// footprint boundary (nearest outside pixel found with a deterministic
// two-pass nearest-point transform, then reflected past the rim by a fixed
// margin). The choice depends only on the mask's SHAPE, never on pixel
// content: there is no patch search, no synthesis-by-example, no
// gradient-domain solve, and no content-driven source selection. Those
// families are claimed by Adobe's active PatchMatch patents (US 8285055,
// US 8340463, US 8355592, into 2031) and gradient-domain compositing
// (US 9058699, to 2029); classic user-directed healing (US 6587592) expired
// in 2021. See docs/legal-constraints.md before changing anything here.

// Per-pixel source and rim points for one footprint, all in document space.
// `source_*` is where the healed texture is read from; `rim_*` is the
// known-good point just outside the boundary nearest the pixel, used as the
// healing tone-match destination. Meaningful only where the input mask is
// non-zero; uncovered pixels hold their own coordinates. empty() reports the
// degenerate footprint that covers its entire bounds (nothing to sample).
struct SpotHealSourceField {
  Rect bounds{};
  std::vector<std::int32_t> source_x;
  std::vector<std::int32_t> source_y;
  std::vector<std::int32_t> rim_x;
  std::vector<std::int32_t> rim_y;

  [[nodiscard]] bool empty() const noexcept { return source_x.empty(); }
};

// `mask` is row-major 8-bit coverage over `bounds` (bounds.width *
// bounds.height bytes, 0 = outside the footprint). `bounds` must already be
// clipped to the canvas; every produced point is clamped to the canvas, and
// off-canvas never acts as a source (an edge footprint mirrors from the
// interior side). `margin` is how far past the boundary the mirror and rim
// points land, in pixels.
[[nodiscard]] SpotHealSourceField spot_heal_mirror_sources(const std::uint8_t* mask, Rect bounds,
                                                           std::int32_t canvas_width,
                                                           std::int32_t canvas_height,
                                                           std::int32_t margin = 2);

}  // namespace patchy
