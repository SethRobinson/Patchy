#pragma once

#include "core/photo_develop.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace patchy::raw {

enum class RenderingProfile { Neutral, Natural };

// Patchy's fixed photographic rendering, independent of the adjustment sliders.
// A monotone tone curve with hue-preserving, gamut-bounded color enhancement.
[[nodiscard]] std::array<std::uint16_t, 65536> build_natural_profile_lut(int processing_version = 3);
void apply_natural_profile(std::array<std::uint16_t, 3>& rgb,
                          const std::array<std::uint16_t, 65536>& lut, int processing_version = 3);

// Tone/color adjustments applied to LibRaw's 16-bit develop output before the final 8-bit
// bake (LibRaw's own pipeline has no contrast/shadows/highlights/saturation parameters).
// They live in core so the Camera Raw Filter shares them (core/photo_develop.hpp).
using patchy::ToneParams;
using patchy::apply_color;
using patchy::build_tone_lut;
using patchy::tone_curve_value;

}  // namespace patchy::raw
