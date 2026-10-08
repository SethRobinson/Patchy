#pragma once

// Float blend math for 16 and 32-bit documents (docs/high-bit-depth.md). Values are on
// the deep scale (core/pixel_depth.hpp: 255 = full scale). The 8-bit kernels in
// core/blend_math.cpp are calibrated to Photoshop's integer rounding and stay the
// only path for 8-bit documents; these are the same formulas without the rounding.
// In the Linear domain (32-bit documents) values may exceed 255: the separable modes
// that are defined on unbounded values (Normal, Multiply, Darken, Lighten, Linear
// Dodge, Difference, Subtract, Divide, Dissolve) do not clamp there.

#include "core/blend_math.hpp"
#include "core/pixel_depth.hpp"

#include <array>

namespace patchy {

using DeepChannels = std::array<float, 3>;

[[nodiscard]] DeepChannels blend_rgb_deep(DeepChannels source, DeepChannels destination, BlendMode mode,
                                          DeepDomain domain);
[[nodiscard]] DeepChannels composite_blended_rgb_deep(DeepChannels source, DeepChannels destination,
                                                      BlendMode mode, float source_alpha,
                                                      float destination_alpha, DeepDomain domain);

struct DeepFillCompositeResult {
  DeepChannels color{};
  float alpha{0.0F};
};
// composite_special_fill_rgb without the integer kernels: the source fades toward the
// mode's neutral by Fill (the three light modes fade their own terms), and the result
// takes the shared special-Fill alpha split.
[[nodiscard]] DeepFillCompositeResult composite_special_fill_rgb_deep(
    DeepChannels source, DeepChannels destination, BlendMode mode, float source_coverage, float fill_opacity,
    float layer_opacity, float destination_alpha, DeepDomain domain);

// Blend If on deep values: the 8-bit thresholds are deep-scale values, and the feather
// is the continuous form of blend_if_threshold_factor. Linear-domain colors compare
// in their display encoding, which is what Photoshop's sliders show.
[[nodiscard]] float blend_if_source_factor_deep(const LayerBlendIf& settings, DeepChannels source,
                                                DeepDomain domain) noexcept;
[[nodiscard]] float blend_if_underlying_factor_deep(const LayerBlendIf& settings, DeepChannels underlying,
                                                    DeepDomain domain) noexcept;

}  // namespace patchy
