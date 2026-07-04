#pragma once

#include "core/brush_dynamics.hpp"

#include <cstdint>
#include <vector>

namespace patchy {

// A sampled (bitmap) brush tip: an 8-bit coverage mask at its source resolution, row-major,
// width * height bytes. 255 = fully opaque. Tips are grayscale coverage only (like Photoshop
// sampled brushes); color always comes from the paint color at stroke time.
struct BrushTip {
  std::int32_t width{0};
  std::int32_t height{0};
  std::vector<std::uint8_t> mask;
  double default_spacing{0.25};  // dab spacing as a fraction of the brush diameter

  [[nodiscard]] bool empty() const noexcept { return width <= 0 || height <= 0 || mask.empty(); }
};

// Box-filtered power-of-two reductions of a tip (level 0 = source). Built once per tip
// selection so rescaling to a live brush size only reads O(target²) pixels instead of the
// full source resolution (ABR tips can be 2500px+).
struct BrushTipMipChain {
  std::vector<BrushTip> levels;

  [[nodiscard]] bool empty() const noexcept { return levels.empty() || levels.front().empty(); }
};

[[nodiscard]] BrushTipMipChain build_brush_tip_mips(BrushTip tip);

// A tip resampled so its larger dimension equals the requested brush size (aspect preserved).
// anchor_* is the dab center in scaled-tip pixel coordinates.
struct ScaledBrushTip {
  std::int32_t width{0};
  std::int32_t height{0};
  double anchor_x{0.0};
  double anchor_y{0.0};
  std::vector<std::uint8_t> mask;

  [[nodiscard]] bool empty() const noexcept { return width <= 0 || height <= 0 || mask.empty(); }
};

[[nodiscard]] ScaledBrushTip make_scaled_brush_tip(const BrushTipMipChain& mips, int target_size);

// Feathers the stamp's edges outward by roughly `feather_pixels` (three separable box-blur
// passes ≈ a gaussian). The buffer grows by the feather on every side and the anchor shifts to
// match, so dab placement is unchanged. This is how the brush Soft setting applies to bitmap
// tips.
void soften_scaled_brush_tip(ScaledBrushTip& tip, int feather_pixels);

// Carries dab-spacing progress across the short segments the canvas stroke smoother emits, so
// dab placement is uniform along the whole stroke instead of clustering at segment joins. Also
// owns the per-stroke dynamics state: the RNG (seeded from EditOptions::brush_dynamics.seed on
// the stroke's first dab) and the fade/direction context. Reset by assigning a fresh struct.
struct BrushTipStrokeState {
  bool initialized{false};
  double residual_distance{0.0};  // distance from the next segment's start to the next dab
  BrushDynamicsRng rng;
  BrushDynamicsStrokeContext dynamics;
};

}  // namespace patchy
