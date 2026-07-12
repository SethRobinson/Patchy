#pragma once

#include "core/layer.hpp"

#include <array>
#include <cstdint>

namespace patchy {

// True when the contour is the identity mapping Photoshop calls "Linear":
// either no points or exactly the (0,0)-(255,255) ramp (corner flags ignored —
// a two-point curve has no interior to shape).
[[nodiscard]] bool style_contour_is_linear(const StyleContour& contour) noexcept;

// 256-entry lookup table for a layer-effect contour. Same interpolation family
// as Curves 2.0's build_curve_lut (natural cubic through the control points,
// zero second derivative at run ends, clamped outside the first/last input,
// nearest-byte rounding), generalized for contours: coordinates are doubles and
// corner points split the curve into independent smooth runs (a two-point run
// degenerates to a straight line, which is how the polyline presets like Cone
// work). The Curves builder itself stays untouched — its output is pinned
// byte-exactly against Photoshop and its integer point model rejects the
// corner/duplicate shapes contours allow.
[[nodiscard]] std::array<std::uint8_t, 256> build_style_contour_lut(const StyleContour& contour);

// Samples a contour LUT at t in [0,1], returning [0,1]. Anti-aliased evaluation
// interpolates linearly between adjacent LUT entries; non-anti-aliased uses the
// quantized nearest entry (visible banding on stepped curves, matching the
// Photoshop checkbox).
[[nodiscard]] float sample_style_contour_lut(const std::array<std::uint8_t, 256>& lut, float t,
                                             bool anti_aliased) noexcept;

}  // namespace patchy
