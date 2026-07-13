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
