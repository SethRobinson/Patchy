#include "formats/raw_tone.hpp"

#include <algorithm>
#include <cmath>

namespace patchy::raw {

namespace {

double smoothstep(double edge0, double edge1, double x) {
  const auto t = std::clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
  return t * t * (3.0 - 2.0 * t);
}

// Exact inverse of the unit smoothstep x^2 (3 - 2x) on [0, 1] (the closed form of the
// relevant cubic root); used to REDUCE contrast with the mirror-image curve of the one
// that increases it.
double inverse_smoothstep(double x) {
  const auto clamped = std::clamp(x, 0.0, 1.0);
  return 0.5 - std::sin(std::asin(1.0 - 2.0 * clamped) / 3.0);
}

}  // namespace

std::array<std::uint16_t, 65536> build_natural_profile_lut(int processing_version) {
  // Self-authored photographic curve, not a camera/vendor profile. Work in sRGB
  // code values: retain a toe, open midtones, and roll highlights gently to white.
  constexpr std::array x{0.0, 0.04, 0.10, 0.20, 0.35, 0.50, 0.70, 0.85, 1.0};
  const auto y = processing_version <= 2 ?
      std::array{0.0, 0.018, 0.065, 0.20, 0.43, 0.64, 0.83, 0.93, 1.0} :
      std::array{0.0, 0.016, 0.070, 0.245, 0.50, 0.73, 0.895, 0.958, 1.0};
  std::array<double, x.size() - 1> slopes{};
  for (std::size_t i = 0; i < slopes.size(); ++i)
    slopes[i] = (y[i + 1] - y[i]) / (x[i + 1] - x[i]);
  std::array<double, x.size()> tangents{};
  tangents.front() = slopes.front();
  tangents.back() = slopes.back();
  for (std::size_t i = 1; i + 1 < x.size(); ++i)
    tangents[i] = 2 * slopes[i - 1] * slopes[i] / (slopes[i - 1] + slopes[i]);
  std::array<std::uint16_t, 65536> lut{};
  std::size_t segment = 0;
  for (std::size_t i = 0; i < lut.size(); ++i) {
    const double value = i / 65535.0;
    while (segment + 2 < x.size() && value > x[segment + 1]) ++segment;
    const double width = x[segment + 1] - x[segment];
    const double t = (value - x[segment]) / width;
    const double t2 = t * t, t3 = t2 * t;
    const double mapped = (2*t3 - 3*t2 + 1) * y[segment] +
        (t3 - 2*t2 + t) * width * tangents[segment] +
        (-2*t3 + 3*t2) * y[segment + 1] +
        (t3 - t2) * width * tangents[segment + 1];
    lut[i] = static_cast<std::uint16_t>(std::lround(std::clamp(mapped, 0.0, 1.0) * 65535.0));
  }
  return lut;
}

void apply_natural_profile(std::array<std::uint16_t, 3>& rgb,
                           const std::array<std::uint16_t, 65536>& lut, int processing_version) {
  const double luma = 0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2];
  if (luma <= 0.0) return;
  const double mapped = lut[static_cast<std::size_t>(std::lround(luma))];
  const double low = *std::min_element(rgb.begin(), rgb.end());
  const double high = *std::max_element(rgb.begin(), rgb.end());
  const double saturation = high > 0.0 ? (high - low) / high : 0.0;
  // Scale all color differences together to retain hue. A small color enhancement
  // fades on already-saturated colors; bound it before rounding so highlights do
  // not clip individual channels or change the order of the channels.
  const double color_strength = processing_version <= 2 ? 0.12 : 0.24;
  double scale = mapped / luma * (1.0 + color_strength * (1.0 - saturation));
  if (high > luma) scale = std::min(scale, (65535.0 - mapped) / (high - luma));
  if (low < luma) scale = std::min(scale, mapped / (luma - low));
  for (auto& channel : rgb)
    channel = static_cast<std::uint16_t>(std::lround(std::clamp(mapped + (channel - luma) * scale, 0.0, 65535.0)));
}

std::array<std::uint16_t, 65536> build_tone_lut(const ToneParams& params) {
  std::array<std::uint16_t, 65536> lut;
  const auto shadows_amount = std::clamp(params.shadows, -100.0, 100.0) / 100.0;
  const auto highlights_amount = std::clamp(params.highlights, -100.0, 100.0) / 100.0;
  const auto contrast_amount = std::clamp(params.contrast, -100.0, 100.0) / 100.0;

  for (int index = 0; index < 65536; ++index) {
    auto x = static_cast<double>(index) / 65535.0;

    if (shadows_amount != 0.0) {
      // Bell over the shadow band: zero at pure black (pinned) and fully faded by the
      // midtones, peaking around 0.15.
      const auto weight = smoothstep(0.0, 0.15, x) * (1.0 - smoothstep(0.15, 0.65, x));
      x = std::clamp(x + shadows_amount * 0.30 * weight, 0.0, 1.0);
    }
    if (highlights_amount != 0.0) {
      // Ramp over the highlight range; deliberately still active at 1.0 so negative
      // values visibly dim blown whites (detail reconstruction is the recovery mode's
      // job, this is tonal compression).
      const auto weight = smoothstep(0.35, 0.85, x);
      x = std::clamp(x + highlights_amount * 0.25 * weight, 0.0, 1.0);
    }
    if (contrast_amount > 0.0) {
      x = x + (smoothstep(0.0, 1.0, x) - x) * contrast_amount;
    } else if (contrast_amount < 0.0) {
      x = x + (inverse_smoothstep(x) - x) * -contrast_amount;
    }

    lut[static_cast<std::size_t>(index)] =
        static_cast<std::uint16_t>(std::lround(std::clamp(x, 0.0, 1.0) * 65535.0));
  }
  return lut;
}

void apply_color(std::span<std::uint16_t> interleaved_rgb, double saturation, double vibrance) {
  const auto saturation_amount = std::clamp(saturation, -100.0, 100.0) / 100.0;
  const auto vibrance_amount = std::clamp(vibrance, -100.0, 100.0) / 100.0;
  if (saturation_amount == 0.0 && vibrance_amount == 0.0) {
    return;
  }
  const auto pixel_count = interleaved_rgb.size() / 3;
  for (std::size_t pixel = 0; pixel < pixel_count; ++pixel) {
    auto* channels = interleaved_rgb.data() + pixel * 3;
    const auto red = channels[0] / 65535.0;
    const auto green = channels[1] / 65535.0;
    const auto blue = channels[2] / 65535.0;
    const auto luma = 0.2126 * red + 0.7152 * green + 0.0722 * blue;
    const auto maximum = std::max({red, green, blue});
    const auto minimum = std::min({red, green, blue});
    const auto pixel_saturation = maximum > 0.0 ? (maximum - minimum) / maximum : 0.0;
    const auto factor =
        std::max(0.0, 1.0 + saturation_amount + vibrance_amount * (1.0 - pixel_saturation));
    const std::array<double, 3> values = {red, green, blue};
    for (int channel = 0; channel < 3; ++channel) {
      const auto adjusted =
          std::clamp(luma + (values[static_cast<std::size_t>(channel)] - luma) * factor, 0.0, 1.0);
      channels[channel] = static_cast<std::uint16_t>(std::lround(adjusted * 65535.0));
    }
  }
}

}  // namespace patchy::raw
