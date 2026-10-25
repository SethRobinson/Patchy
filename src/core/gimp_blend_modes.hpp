#pragma once

#include "core/layer.hpp"

#include <array>

namespace patchy {

// GIMP's own color-model and linear-light blend modes (docs/blend-modes.md, "GIMP modes";
// GIMP .xcf import, docs/xcf.md): HSV Hue/Saturation/Value and HSL Color, CIE LCh(ab)
// Hue/Chroma/Color/Lightness, Luminance and Luma Darken/Lighten Only, and Multiply,
// Addition, Subtract and Divide blended in linear light. Each matched GIMP 3.2's renders
// within one level on 8-bit samples. Grain Extract and Grain Merge are per-channel byte
// kernels in blend_math.cpp and are not handled here.
[[nodiscard]] bool blend_mode_is_gimp_color_model(BlendMode mode) noexcept;

// One pixel: `source` and `destination` are display-encoded sRGB in 0..1; returns the
// blended display-encoded sRGB, clamped to 0..1.
[[nodiscard]] std::array<double, 3> gimp_blend_unit(const std::array<double, 3>& source,
                                                    const std::array<double, 3>& destination, BlendMode mode) noexcept;

}  // namespace patchy
