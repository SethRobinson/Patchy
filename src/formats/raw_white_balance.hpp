#pragma once

#include <array>
#include <optional>

namespace patchy::raw {

// Conversions between photographer-facing white balance (correlated color temperature in
// kelvin + green/magenta tint) and the camera-space channel multipliers LibRaw consumes.
// All math is plain double arithmetic with fixed constants so results are stable across
// toolchains (do not byte-pin outputs regardless; see the AGENTS.md universal invariants).

struct WhiteBalance {
  double temperature_k{5500.0};
  // Positive = magenta, negative = green, roughly matching the ACR/Lightroom slider
  // direction and scale (about -150..150 useful range).
  double tint{0.0};
};

// The camera's XYZ(D65) -> camera-space matrix, rows = camera channels R,G,B,G2 (LibRaw's
// cam_xyz layout). A fourth all-zero row means the camera has no distinct second green.
using CameraMatrix = std::array<std::array<double, 3>, 4>;

// CIE 1931 xy chromaticity of a blackbody/daylight illuminant at `temperature_k`
// (Kim et al. cubic spline approximation below 4000 K limit handling; CIE daylight locus
// 4000-25000 K). Valid input range is clamped to [1667, 25000].
[[nodiscard]] std::array<double, 2> chromaticity_for_temperature(double temperature_k);

// Camera channel multipliers (normalized so the green multiplier is 1) that neutralize a
// surface lit by the given white balance, through the camera matrix. Returns R,G,B,G2
// (G2 mirrors G when the camera has no distinct second green row).
[[nodiscard]] std::array<double, 4> multipliers_for_white_balance(const WhiteBalance& wb,
                                                                  const CameraMatrix& cam_xyz);

// Inverse of multipliers_for_white_balance: the temperature/tint whose multipliers best
// reproduce `multipliers` (bisection on temperature, direct solve for tint). Returns
// nullopt when the multipliers are degenerate (zero/negative channels).
[[nodiscard]] std::optional<WhiteBalance> white_balance_for_multipliers(
    const std::array<double, 4>& multipliers, const CameraMatrix& cam_xyz);

}  // namespace patchy::raw
