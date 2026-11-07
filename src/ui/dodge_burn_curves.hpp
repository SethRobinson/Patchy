#pragma once

// Dodge and Burn range curves (GitHub issue 661; docs/brushes-local-adjustments.md, "Dodge and Burn
// ranges"). Each curve is the change in level (0..255 scale) a full-strength stroke gives
// a tone, sampled at 17 evenly spaced tones (0, 1/16, ..., 1) and joined by a Catmull-Rom
// spline. The values are a least-squares fit to Photoshop 2026's Dodge and Burn on a gray
// ramp, measured at every Range and Exposure 10, 25, 50 and 100 with Protect Tones on and
// off (desk job 1731, November 7, 2026). Fixed per tone: nothing here reads the image
// beyond the pixel being changed (patent boundary at local_adjustment_brush_segment).

#include <algorithm>
#include <array>
#include <cmath>

namespace patchy::ui::dodge_burn {

using Knots = std::array<double, 17>;

// Protect Tones on: applied to the pixel's lightness, the same offset on every channel.
inline constexpr Knots kProtectDodgeShadows{0.0, 7.2, 12.9, 16.7, 18.5, 18.5, 16.7, 13.9, 10.7,
                                            7.3, 4.5, 2.4,  1.0,  0.2,  0.0,  0.0,  0.0};
inline constexpr Knots kProtectDodgeMidtones{0.0,  0.7,  3.4,  8.3,  14.1, 20.7, 26.9, 31.7, 34.6,
                                             35.0, 33.1, 29.2, 23.9, 17.9, 11.7, 5.7,  0.0};
inline constexpr Knots kProtectDodgeHighlights{0.0,  0.0,  0.0,  0.5,  2.9,   9.3,   20.6,  34.7, 48.8,
                                               61.4, 72.9, 83.3, 93.2, 100.0, 100.0, 100.0, 100.0};
inline constexpr Knots kProtectBurnShadows{0.0,   -14.4, -25.7, -33.3, -36.9, -36.8, -33.6, -28.0, -21.2,
                                           -14.7, -9.2,  -4.9,  -2.3,  -1.0,  0.0,   0.0,   0.0};
inline constexpr Knots kProtectBurnMidtones{0.0,   -0.7,  -3.4,  -8.3,  -14.1, -20.7, -26.9, -31.7, -34.6,
                                            -35.0, -33.1, -29.2, -23.9, -17.9, -11.7, -5.7,  0.0};
inline constexpr Knots kProtectBurnHighlights{0.0,   0.0,   0.0,   0.0,   -1.2,  -3.8,  -8.2,  -13.8, -19.5,
                                              -24.6, -29.1, -33.2, -37.0, -40.8, -44.2, -47.6, -50.5};

// Protect Tones off: applied to each channel on its own.
inline constexpr Knots kDodgeShadows{108.3, 101.8, 95.1, 88.2, 81.5, 74.7, 67.9, 61.2, 54.4,
                                     47.6,  40.9,  34.1, 27.2, 20.5, 13.7, 7.1,  0.0};
inline constexpr Knots kDodgeMidtones{0.0,  30.5, 46.6, 53.4, 54.5, 53.4, 51.4, 48.6, 45.0,
                                      40.7, 35.9, 30.7, 25.1, 19.3, 13.1, 6.7,  0.0};
inline constexpr Knots kDodgeHighlights{0.0,  10.3, 20.9, 31.7,  42.5,  53.3,  64.2,  75.3, 85.7,
                                        91.1, 91.6, 94.8, 97.7, 101.2, 104.7, 108.1, 111.5};
inline constexpr Knots kBurnShadows{-111.4, -108.0, -104.6, -101.3, -97.8, -94.9, -91.6, -91.0, -85.7,
                                    -75.2,  -64.2,  -53.3,  -42.5,  -31.7, -20.9, -10.3, 0.0};
inline constexpr Knots kBurnMidtones{0.0,   -12.8, -23.6, -33.0, -40.6, -46.6, -50.8, -53.3, -54.2,
                                     -53.3, -50.8, -46.6, -40.6, -33.0, -23.6, -12.9, 0.0};
inline constexpr Knots kBurnHighlights{0.0,   -7.1,  -13.7, -20.5, -27.3, -34.1, -40.8, -47.6,  -54.4,
                                       -61.2, -68.0, -74.7, -81.5, -88.3, -95.1, -101.7, -108.3};

// The curve's value at tone `t` (0..1, clamped): a uniform Catmull-Rom spline through the
// knots, end knots repeated. Only multiplies and adds, so it is the same on every toolchain.
[[nodiscard]] inline double curve_at(const Knots& knots, double t) noexcept {
  const auto x = std::clamp(t, 0.0, 1.0) * 16.0;
  const auto index = std::min(static_cast<int>(x), 15);
  const auto f = x - static_cast<double>(index);
  const auto at = [&knots](int i) { return knots[static_cast<std::size_t>(std::clamp(i, 0, 16))]; };
  const auto p0 = at(index - 1);
  const auto p1 = at(index);
  const auto p2 = at(index + 1);
  const auto p3 = at(index + 2);
  return 0.5 * (2.0 * p1 + (p2 - p0) * f + (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * f * f +
                (3.0 * p1 - p0 - 3.0 * p2 + p3) * f * f * f);
}

// How much of the curve a stroke at Strength `strength` (0..1) applies. Photoshop's
// Exposure builds up over a stroke's overlapping dabs; these match its measured response
// for a hard brush at the default 25% spacing: 1 - (1 - s)^4 with Protect Tones and
// 1 - (1 - s)^1.75 without (square roots only, so the same on every toolchain).
[[nodiscard]] inline double stroke_amount(double strength, bool protect_tones) noexcept {
  const auto rest = 1.0 - std::clamp(strength, 0.0, 1.0);
  if (protect_tones) {
    const auto square = rest * rest;
    return 1.0 - square * square;
  }
  const auto root = std::sqrt(rest);
  return 1.0 - rest * root * std::sqrt(root);
}

}  // namespace patchy::ui::dodge_burn
