#include "core/hdr_color.hpp"

#include "core/pixel_depth.hpp"

#include <algorithm>
#include <cmath>

namespace patchy {

namespace {

std::uint8_t encoded_byte(double linear) noexcept {
  return static_cast<std::uint8_t>(std::clamp(std::lround(srgb_encode(std::clamp(linear, 0.0, 1.0)) * 255.0), 0L, 255L));
}

}  // namespace

double clamp_color_intensity_stops(double stops) noexcept {
  if (!std::isfinite(stops)) {
    return 0.0;
  }
  return std::clamp(stops, kMinColorIntensityStops, kMaxColorIntensityStops);
}

float color_intensity_gain(double stops) noexcept {
  return static_cast<float>(std::exp2(clamp_color_intensity_stops(stops)));
}

LinearRgb hdr_linear_color(const HdrColor& color) noexcept {
  const auto gain = std::exp2(clamp_color_intensity_stops(color.stops));
  LinearRgb linear{};
  for (std::size_t c = 0; c < 3U; ++c) {
    linear[c] = srgb_decode(static_cast<double>(color.base[c]) / 255.0) * gain;
  }
  return linear;
}

HdrColor hdr_color_from_linear(const LinearRgb& linear) noexcept {
  LinearRgb value{};
  for (std::size_t c = 0; c < 3U; ++c) {
    value[c] = std::isfinite(linear[c]) ? std::max(0.0, linear[c]) : 0.0;
  }
  const auto brightest = std::max({value[0], value[1], value[2]});
  HdrColor color;
  // A hair above 1.0 is float noise in a display-range color, not a stop of light.
  if (brightest > 1.0 + 1e-6) {
    color.stops = clamp_color_intensity_stops(std::log2(brightest));
  }
  const auto gain = std::exp2(color.stops);
  for (std::size_t c = 0; c < 3U; ++c) {
    color.base[c] = encoded_byte(value[c] / gain);
  }
  return color;
}

std::array<std::uint8_t, 3> hdr_preview_color(const HdrColor& color, double offset_stops) noexcept {
  auto shifted = color;
  shifted.stops = color.stops + (std::isfinite(offset_stops) ? offset_stops : 0.0);
  // The preview may reach past the offered range (a +3 swatch at +9 stops): scale directly.
  const auto gain = std::exp2(std::clamp(shifted.stops, -40.0, 40.0));
  std::array<std::uint8_t, 3> display{};
  for (std::size_t c = 0; c < 3U; ++c) {
    display[c] = encoded_byte(srgb_decode(static_cast<double>(color.base[c]) / 255.0) * gain);
  }
  return display;
}

}  // namespace patchy
