#pragma once

// Paint colors for 32-bit documents (docs/high-bit-depth.md, "Color choice at 32 bits").
// A foreground or background color is an 8-bit sRGB base color plus an intensity in stops:
// its linear value is the decoded base times 2^stops, so +1 stop doubles the light and a
// 32-bit layer can be painted above 1.0. 8 and 16-bit documents use the base color alone.

#include <array>
#include <cstdint>

namespace patchy {

// The Intensity range the color window and the Color panel offer, in stops.
inline constexpr double kMinColorIntensityStops = -10.0;
inline constexpr double kMaxColorIntensityStops = 10.0;

// Linear-light RGB with 1.0 as display white, unbounded above.
using LinearRgb = std::array<double, 3>;

// The foreground's and background's Intensity in stops (the canvas and the main window keep
// one pair each, beside their 8-bit colors).
struct PaintIntensity {
  double foreground{0.0};
  double background{0.0};
  friend bool operator==(const PaintIntensity&, const PaintIntensity&) = default;
};

struct HdrColor {
  std::array<std::uint8_t, 3> base{0, 0, 0};  // sRGB, what 8 and 16-bit documents paint
  double stops{0.0};
};

// The intensity clamped to the offered range; a non-finite value is 0.
[[nodiscard]] double clamp_color_intensity_stops(double stops) noexcept;
// 2^stops as a float multiplier of linear light (1 at 0 stops).
[[nodiscard]] float color_intensity_gain(double stops) noexcept;
// The linear value a 32-bit layer paints: the decoded base times 2^stops.
[[nodiscard]] LinearRgb hdr_linear_color(const HdrColor& color) noexcept;
// The other way: a linear value as a base color and an intensity. Values within display
// range keep 0 stops (the base is the sRGB encoding, rounded); brighter ones get the stops
// that bring their brightest channel to 1.0, so a picked overbright pixel keeps its light.
// Negative and non-finite channels read as 0; the stops clamp to the offered range.
[[nodiscard]] HdrColor hdr_color_from_linear(const LinearRgb& linear) noexcept;
// What `color` looks like `offset_stops` brighter (or darker) on an SDR display: the
// linear value scaled and sRGB-encoded, clipped at white. The stop-preview swatches.
[[nodiscard]] std::array<std::uint8_t, 3> hdr_preview_color(const HdrColor& color, double offset_stops) noexcept;

}  // namespace patchy
