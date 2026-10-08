#include "core/blend_math_deep.hpp"

#include <algorithm>
#include <cmath>

namespace patchy {
namespace {

// Formulas run on the unit scale (1 = full) and return to the deep scale at the end.
float unit(float deep) noexcept {
  return deep / 255.0F;
}

float soft_light(float s, float d) noexcept {
  if (s <= 0.5F) {
    return d - (1.0F - 2.0F * s) * d * (1.0F - d);
  }
  const auto g = d <= 0.25F ? ((16.0F * d - 12.0F) * d + 4.0F) * d : std::sqrt(d);
  return d + (2.0F * s - 1.0F) * (g - d);
}

float vivid_light(float s, float d) noexcept {
  if (s <= 0.0F) {
    return 0.0F;
  }
  if (s >= 1.0F) {
    return 1.0F;
  }
  if (s < 0.5F) {
    return std::max(0.0F, 1.0F - (1.0F - d) / (2.0F * s));
  }
  return std::min(1.0F, d / (2.0F * (1.0F - s)));
}

// Separable kernel on unit values. `bounded` clamps inputs to 0..1 first: every mode
// in the Encoded domain, and the modes whose formula assumes a 0..1 range anywhere.
float blend_channel_unit(float s, float d, BlendMode mode, bool linear) noexcept {
  switch (mode) {
    case BlendMode::PassThrough:
    case BlendMode::Normal:
    case BlendMode::Dissolve:
      return s;
    case BlendMode::Multiply:
      return s * d;
    case BlendMode::Darken:
      return std::min(s, d);
    case BlendMode::Lighten:
      return std::max(s, d);
    case BlendMode::LinearDodge:
      return linear ? s + d : std::min(1.0F, s + d);
    case BlendMode::Difference:
      return std::abs(d - s);
    case BlendMode::Subtract:
      return std::max(0.0F, d - s);
    case BlendMode::Divide:
      if (d <= 0.0F) {
        return 0.0F;
      }
      if (s <= 0.0F) {
        return linear ? d * 65535.0F : 1.0F;
      }
      return linear ? d / s : std::min(1.0F, d / s);
    default:
      break;
  }
  s = std::clamp(s, 0.0F, 1.0F);
  d = std::clamp(d, 0.0F, 1.0F);
  switch (mode) {
    case BlendMode::Screen:
      return 1.0F - (1.0F - s) * (1.0F - d);
    case BlendMode::Overlay:
      return d < 0.5F ? 2.0F * s * d : 1.0F - 2.0F * (1.0F - s) * (1.0F - d);
    case BlendMode::ColorDodge:
      if (d <= 0.0F) {
        return 0.0F;
      }
      return s >= 1.0F ? 1.0F : std::min(1.0F, d / (1.0F - s));
    case BlendMode::ColorBurn:
      if (d >= 1.0F) {
        return 1.0F;
      }
      return s <= 0.0F ? 0.0F : 1.0F - std::min(1.0F, (1.0F - d) / s);
    case BlendMode::HardLight:
      return s < 0.5F ? 2.0F * s * d : 1.0F - 2.0F * (1.0F - s) * (1.0F - d);
    case BlendMode::SoftLight:
      return soft_light(s, d);
    case BlendMode::LinearBurn:
      return std::max(0.0F, s + d - 1.0F);
    case BlendMode::PinLight:
      return s < 0.5F ? std::min(d, 2.0F * s) : std::max(d, 2.0F * s - 1.0F);
    case BlendMode::Exclusion:
      return s + d - 2.0F * s * d;
    case BlendMode::VividLight:
      return vivid_light(s, d);
    case BlendMode::LinearLight:
      return std::clamp(d + 2.0F * s - 1.0F, 0.0F, 1.0F);
    case BlendMode::HardMix:
      return vivid_light(s, d) > 0.5F ? 1.0F : 0.0F;
    default:
      return s;
  }
}

struct Rgb {
  double r;
  double g;
  double b;
};

double lum(const Rgb& c) {
  return 0.3 * c.r + 0.59 * c.g + 0.11 * c.b;
}

Rgb clip_color(Rgb c, bool linear) {
  if (linear) {
    // Photoshop 2026's 32-bit non-separable modes do not clip at all: Luminosity over the
    // deep corpus ramps leaves values above 1 and below 0 (blend-luminosity-32).
    return c;
  }
  const auto l = lum(c);
  const auto n = std::min({c.r, c.g, c.b});
  const auto x = std::max({c.r, c.g, c.b});
  if (n < 0.0 && l - n > 0.0) {
    c.r = l + (c.r - l) * l / (l - n);
    c.g = l + (c.g - l) * l / (l - n);
    c.b = l + (c.b - l) * l / (l - n);
  }
  if (x > 1.0 && x - l > 0.0) {
    c.r = l + (c.r - l) * (1.0 - l) / (x - l);
    c.g = l + (c.g - l) * (1.0 - l) / (x - l);
    c.b = l + (c.b - l) * (1.0 - l) / (x - l);
  }
  return c;
}

Rgb set_lum(Rgb c, double l, bool linear) {
  const auto d = l - lum(c);
  return clip_color(Rgb{c.r + d, c.g + d, c.b + d}, linear);
}

double sat(const Rgb& c) {
  return std::max({c.r, c.g, c.b}) - std::min({c.r, c.g, c.b});
}

Rgb set_sat(Rgb c, double s) {
  double* channels[3] = {&c.r, &c.g, &c.b};
  std::sort(std::begin(channels), std::end(channels), [](const double* a, const double* b) { return *a < *b; });
  auto& minimum = *channels[0];
  auto& mid = *channels[1];
  auto& maximum = *channels[2];
  if (maximum > minimum) {
    mid = (mid - minimum) * s / (maximum - minimum);
    maximum = s;
  } else {
    mid = 0.0;
    maximum = 0.0;
  }
  minimum = 0.0;
  return c;
}

float encoded_unit(float deep, DeepDomain domain) noexcept {
  return domain == DeepDomain::Linear ? static_cast<float>(srgb_encode(deep / 255.0F)) : std::clamp(deep / 255.0F, 0.0F, 1.0F);
}

float threshold_factor(const BlendIfThresholds& thresholds, float value) noexcept {
  const auto black_low = static_cast<float>(thresholds.black_low);
  const auto black_high = static_cast<float>(thresholds.black_high);
  const auto white_low = static_cast<float>(thresholds.white_low);
  const auto white_high = static_cast<float>(thresholds.white_high);
  // The continuous form of blend_if_threshold_factor: values are on the 0..255 scale
  // and the split-handle feathers keep its inclusive-endpoint (+1) width.
  if (value < black_low - 0.5F || value > white_high + 0.5F) {
    return 0.0F;
  }
  if (value < black_high) {
    return std::clamp((value - black_low + 1.0F) / (black_high - black_low + 1.0F), 0.0F, 1.0F);
  }
  if (value > white_low) {
    return std::clamp((white_high - value + 1.0F) / (white_high - white_low + 1.0F), 0.0F, 1.0F);
  }
  return 1.0F;
}

float blend_if_factor(const LayerBlendIf& settings, DeepChannels color, DeepDomain domain, bool source) noexcept {
  const auto r = encoded_unit(color[0], domain) * 255.0F;
  const auto g = encoded_unit(color[1], domain) * 255.0F;
  const auto b = encoded_unit(color[2], domain) * 255.0F;
  const std::array<float, 4> values{0.299F * r + 0.590F * g + 0.111F * b, r, g, b};
  float factor = 1.0F;
  for (std::size_t channel = 0; channel < settings.channels.size(); ++channel) {
    const auto& ranges = settings.channels[channel];
    factor *= threshold_factor(source ? ranges.this_layer : ranges.underlying_layer, values[channel]);
    if (factor <= 0.0F) {
      return 0.0F;
    }
  }
  return factor;
}

}  // namespace

DeepChannels blend_rgb_deep(DeepChannels source, DeepChannels destination, BlendMode mode, DeepDomain domain) {
  const bool linear = domain == DeepDomain::Linear;
  if (mode == BlendMode::DarkerColor || mode == BlendMode::LighterColor) {
    const auto luma = [](const DeepChannels& c) { return 0.30F * c[0] + 0.59F * c[1] + 0.11F * c[2]; };
    const bool pick_source =
        mode == BlendMode::DarkerColor ? luma(source) < luma(destination) : luma(source) > luma(destination);
    return pick_source ? source : destination;
  }
  if (mode == BlendMode::Saturation || mode == BlendMode::Luminosity || mode == BlendMode::Hue ||
      mode == BlendMode::Color) {
    const Rgb s{unit(source[0]), unit(source[1]), unit(source[2])};
    const Rgb d{unit(destination[0]), unit(destination[1]), unit(destination[2])};
    Rgb result{};
    switch (mode) {
      case BlendMode::Hue:
        result = set_lum(set_sat(s, sat(d)), lum(d), linear);
        break;
      case BlendMode::Saturation:
        result = set_lum(set_sat(d, sat(s)), lum(d), linear);
        break;
      case BlendMode::Color:
        result = set_lum(s, lum(d), linear);
        break;
      default:
        result = set_lum(d, lum(s), linear);
        break;
    }
    return {static_cast<float>(result.r * 255.0), static_cast<float>(result.g * 255.0),
            static_cast<float>(result.b * 255.0)};
  }
  DeepChannels out{};
  for (std::size_t c = 0; c < 3U; ++c) {
    out[c] = blend_channel_unit(unit(source[c]), unit(destination[c]), mode, linear) * 255.0F;
  }
  return out;
}

DeepChannels composite_blended_rgb_deep(DeepChannels source, DeepChannels destination, BlendMode mode,
                                        float source_alpha, float destination_alpha, DeepDomain domain) {
  source_alpha = clamp_unit(source_alpha);
  destination_alpha = clamp_unit(destination_alpha);
  const auto output_alpha = source_alpha + destination_alpha * (1.0F - source_alpha);
  if (output_alpha <= 0.0F) {
    return {0.0F, 0.0F, 0.0F};
  }
  const auto blended = blend_rgb_deep(source, destination, mode, domain);
  DeepChannels output{};
  for (std::size_t c = 0; c < 3U; ++c) {
    output[c] = (source[c] * source_alpha * (1.0F - destination_alpha) + blended[c] * source_alpha * destination_alpha +
                 destination[c] * destination_alpha * (1.0F - source_alpha)) /
                output_alpha;
  }
  return output;
}

DeepFillCompositeResult composite_special_fill_rgb_deep(DeepChannels source, DeepChannels destination,
                                                        BlendMode mode, float source_coverage, float fill_opacity,
                                                        float layer_opacity, float destination_alpha,
                                                        DeepDomain domain) {
  source_coverage = clamp_unit(source_coverage);
  fill_opacity = clamp_unit(fill_opacity);
  layer_opacity = clamp_unit(layer_opacity);
  destination_alpha = clamp_unit(destination_alpha);
  DeepChannels blend{};
  if (mode == BlendMode::LinearLight) {
    // d + (2s - 1) * fill, the kernel's own term faded.
    for (std::size_t c = 0; c < 3U; ++c) {
      blend[c] = std::clamp(destination[c] + (2.0F * source[c] - 255.0F) * fill_opacity, 0.0F, 255.0F);
    }
  } else if (mode == BlendMode::VividLight) {
    for (std::size_t c = 0; c < 3U; ++c) {
      const auto s = std::clamp(unit(source[c]), 0.0F, 1.0F);
      const auto d = std::clamp(unit(destination[c]), 0.0F, 1.0F);
      float value = d;
      if (s >= 0.5F) {
        const auto doubled = (2.0F * s - 1.0F) * fill_opacity;  // dodge term faded toward 0
        value = doubled >= 1.0F ? 1.0F : std::min(1.0F, d / (1.0F - doubled));
      } else {
        const auto doubled = 1.0F - (1.0F - 2.0F * s) * fill_opacity;  // burn term faded toward 1
        value = doubled <= 0.0F ? 0.0F : std::max(0.0F, 1.0F - (1.0F - d) / doubled);
      }
      blend[c] = value * 255.0F;
    }
  } else if (mode == BlendMode::HardMix) {
    // The steep ramp the 8-bit kernel fades into: (d - (1 - s) * f) / (1 - f).
    for (std::size_t c = 0; c < 3U; ++c) {
      const auto s = std::clamp(unit(source[c]), 0.0F, 1.0F);
      const auto d = std::clamp(unit(destination[c]), 0.0F, 1.0F);
      const auto f = std::min(fill_opacity, 0.999F);
      blend[c] = std::clamp((d - (1.0F - s) * f) / (1.0F - f), 0.0F, 1.0F) * 255.0F;
    }
  } else {
    const bool white_neutral = mode == BlendMode::ColorBurn || mode == BlendMode::LinearBurn;
    DeepChannels faded{};
    for (std::size_t c = 0; c < 3U; ++c) {
      const auto neutral = white_neutral ? 255.0F : 0.0F;
      faded[c] = neutral + (source[c] - neutral) * fill_opacity;
    }
    blend = blend_rgb_deep(faded, destination, mode, domain);
  }
  const auto effective_alpha = source_coverage * fill_opacity * layer_opacity;
  const auto overlap_alpha = source_coverage * layer_opacity;
  const auto output_alpha = effective_alpha + destination_alpha * (1.0F - effective_alpha);
  DeepFillCompositeResult result;
  result.alpha = output_alpha;
  if (output_alpha <= 0.0F) {
    return result;
  }
  for (std::size_t c = 0; c < 3U; ++c) {
    result.color[c] = (source[c] * effective_alpha * (1.0F - destination_alpha) +
                       blend[c] * overlap_alpha * destination_alpha +
                       destination[c] * destination_alpha * (1.0F - overlap_alpha)) /
                      output_alpha;
  }
  return result;
}

float blend_if_source_factor_deep(const LayerBlendIf& settings, DeepChannels source, DeepDomain domain) noexcept {
  return blend_if_factor(settings, source, domain, true);
}

float blend_if_underlying_factor_deep(const LayerBlendIf& settings, DeepChannels underlying,
                                      DeepDomain domain) noexcept {
  return blend_if_factor(settings, underlying, domain, false);
}

}  // namespace patchy
