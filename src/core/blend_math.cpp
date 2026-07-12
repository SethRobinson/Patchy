#include "core/blend_math.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace patchy {

namespace {

constexpr float kPi = 3.14159265358979323846F;

std::uint8_t soft_light_channel(std::uint8_t src, std::uint8_t dst) {
  const auto source = static_cast<float>(src) / 255.0F;
  const auto base = static_cast<float>(dst) / 255.0F;
  float blended = base;
  if (source <= 0.5F) {
    blended = base - (1.0F - 2.0F * source) * base * (1.0F - base);
  } else {
    const auto d = base <= 0.25F ? ((16.0F * base - 12.0F) * base + 4.0F) * base : std::sqrt(base);
    blended = base + (2.0F * source - 1.0F) * (d - base);
  }
  return clamp_byte(blended * 255.0F);
}

std::uint8_t blend_channel(std::uint8_t src, std::uint8_t dst, BlendMode mode) {
  switch (mode) {
    case BlendMode::PassThrough:
    case BlendMode::Normal:
      return src;
    case BlendMode::Multiply:
      return static_cast<std::uint8_t>((static_cast<int>(src) * static_cast<int>(dst)) / 255);
    case BlendMode::Screen:
      return static_cast<std::uint8_t>(255 - ((255 - static_cast<int>(src)) * (255 - static_cast<int>(dst))) / 255);
    case BlendMode::Overlay:
      if (dst < 128) {
        return static_cast<std::uint8_t>((2 * static_cast<int>(src) * static_cast<int>(dst)) / 255);
      }
      return static_cast<std::uint8_t>(255 - (2 * (255 - static_cast<int>(src)) * (255 - static_cast<int>(dst))) / 255);
    case BlendMode::Darken:
      return std::min(src, dst);
    case BlendMode::Lighten:
      return std::max(src, dst);
    case BlendMode::ColorDodge:
      return src == 255 ? 255
                        : static_cast<std::uint8_t>(
                              std::min(255, (static_cast<int>(dst) * 255) / (255 - static_cast<int>(src))));
    case BlendMode::ColorBurn:
      return src == 0 ? 0
                      : static_cast<std::uint8_t>(
                            255 - std::min(255, ((255 - static_cast<int>(dst)) * 255) / static_cast<int>(src)));
    case BlendMode::HardLight:
      if (src < 128) {
        return static_cast<std::uint8_t>((2 * static_cast<int>(src) * static_cast<int>(dst)) / 255);
      }
      return static_cast<std::uint8_t>(255 - (2 * (255 - static_cast<int>(src)) * (255 - static_cast<int>(dst))) / 255);
    case BlendMode::SoftLight:
      return soft_light_channel(src, dst);
    case BlendMode::Difference:
      return static_cast<std::uint8_t>(std::abs(static_cast<int>(dst) - static_cast<int>(src)));
    case BlendMode::LinearBurn:
      return static_cast<std::uint8_t>(std::clamp(static_cast<int>(src) + static_cast<int>(dst) - 255, 0, 255));
    case BlendMode::PinLight:
      if (src < 128) {
        return std::min<std::uint8_t>(dst, static_cast<std::uint8_t>(std::clamp(2 * static_cast<int>(src), 0, 255)));
      }
      return std::max<std::uint8_t>(
          dst, static_cast<std::uint8_t>(std::clamp(2 * (static_cast<int>(src) - 128), 0, 255)));
    case BlendMode::Exclusion:
    // Round the s*d/255 product BEFORE doubling (Photoshop/Aseprite's un8
    // multiply); rounding the doubled product instead is off by one on half the
    // inputs.
      return static_cast<std::uint8_t>(
          static_cast<int>(src) + static_cast<int>(dst) -
          2 * ((static_cast<int>(src) * static_cast<int>(dst) + 127) / 255));
    case BlendMode::LinearDodge:
      return static_cast<std::uint8_t>(std::min(255, static_cast<int>(src) + static_cast<int>(dst)));
    case BlendMode::Subtract:
      return static_cast<std::uint8_t>(std::max(0, static_cast<int>(dst) - static_cast<int>(src)));
    case BlendMode::Divide:
      return src == 0 ? 255
                      : static_cast<std::uint8_t>(std::min(
                            255, (static_cast<int>(dst) * 255 + static_cast<int>(src) / 2) / static_cast<int>(src)));
    case BlendMode::Saturation:
    case BlendMode::Luminosity:
    case BlendMode::Hue:
    case BlendMode::Color:
      return src;
  }
  return src;
}

// PDF-spec non-separable blend components (the algorithm Photoshop and Aseprite
// share): luma-weighted set_lum with clipping and min/mid/max-based set_sat.
// Double math with a final lround keeps the output toolchain-deterministic.
struct RgbDouble {
  double r{0.0};
  double g{0.0};
  double b{0.0};
};

double pdf_lum(const RgbDouble& c) {
  return 0.3 * c.r + 0.59 * c.g + 0.11 * c.b;
}

RgbDouble pdf_clip_color(RgbDouble c) {
  const auto l = pdf_lum(c);
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

RgbDouble pdf_set_lum(RgbDouble c, double l) {
  const auto d = l - pdf_lum(c);
  c.r += d;
  c.g += d;
  c.b += d;
  return pdf_clip_color(c);
}

double pdf_sat(const RgbDouble& c) {
  return std::max({c.r, c.g, c.b}) - std::min({c.r, c.g, c.b});
}

RgbDouble pdf_set_sat(RgbDouble c, double s) {
  double* channels[3] = {&c.r, &c.g, &c.b};
  std::sort(std::begin(channels), std::end(channels),
            [](const double* lhs, const double* rhs) { return *lhs < *rhs; });
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

float midpoint_remap(float value, float midpoint) {
  value = std::clamp(value, 0.0F, 1.0F);
  if (midpoint == 0.5F) {
    return value;
  }
  midpoint = std::clamp(midpoint, 0.0001F, 0.9999F);
  return value <= midpoint
             ? 0.5F * value / midpoint
             : 0.5F + 0.5F * (value - midpoint) / (1.0F - midpoint);
}

double srgb_to_linear(double value) {
  value = std::clamp(value, 0.0, 1.0);
  return value <= 0.04045 ? value / 12.92
                          : std::pow((value + 0.055) / 1.055, 2.4);
}

double linear_to_srgb(double value) {
  value = std::clamp(value, 0.0, 1.0);
  return value <= 0.0031308 ? value * 12.92
                            : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055;
}

struct Oklab {
  double l{};
  double a{};
  double b{};
};

Oklab rgb_to_oklab(RgbColor color) {
  const auto r = srgb_to_linear(color.red / 255.0);
  const auto g = srgb_to_linear(color.green / 255.0);
  const auto b = srgb_to_linear(color.blue / 255.0);
  const auto l =
      std::cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
  const auto m =
      std::cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
  const auto s =
      std::cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
  return Oklab{0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
               1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
               0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s};
}

RgbColor oklab_to_rgb(Oklab color) {
  const auto l_ = color.l + 0.3963377774 * color.a + 0.2158037573 * color.b;
  const auto m_ = color.l - 0.1055613458 * color.a - 0.0638541728 * color.b;
  const auto s_ = color.l - 0.0894841775 * color.a - 1.2914855480 * color.b;
  const auto l = l_ * l_ * l_;
  const auto m = m_ * m_ * m_;
  const auto s = s_ * s_ * s_;
  const auto r = 4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s;
  const auto g = -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s;
  const auto b = -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s;
  return RgbColor{clamp_byte(static_cast<float>(linear_to_srgb(r) * 255.0)),
                  clamp_byte(static_cast<float>(linear_to_srgb(g) * 255.0)),
                  clamp_byte(static_cast<float>(linear_to_srgb(b) * 255.0))};
}

double catmull_rom(double p0, double p1, double p2, double p3, double t) {
  const auto t2 = t * t;
  const auto t3 = t2 * t;
  return 0.5 * ((2.0 * p1) + (-p0 + p2) * t +
                (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t2 +
                (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t3);
}

std::uint64_t splitmix64(std::uint64_t value) {
  value += 0x9e3779b97f4a7c15ULL;
  value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31U);
}

double unit_hash(std::uint64_t value) {
  return static_cast<double>(splitmix64(value) >> 11U) *
         (1.0 / 9007199254740992.0);
}

double smooth_noise(std::uint32_t seed, int channel, double coordinate,
                    double roughness) {
  const auto cell = static_cast<std::int64_t>(std::floor(coordinate));
  const auto fraction = coordinate - static_cast<double>(cell);
  const auto smooth = fraction * fraction * (3.0 - 2.0 * fraction);
  const auto sample = [&](std::int64_t index) {
    return unit_hash(
        static_cast<std::uint64_t>(index) ^
        (static_cast<std::uint64_t>(seed) << 1U) ^
        (static_cast<std::uint64_t>(channel + 1) * 0xd6e8feb86659fd93ULL));
  };
  const auto base = sample(cell) + (sample(cell + 1) - sample(cell)) * smooth;
  // High roughness retains more local variation; low roughness trends toward a
  // broad neutral ramp without introducing platform RNG differences.
  return 0.5 + (base - 0.5) * std::clamp(roughness, 0.0, 1.0);
}

double gradient_noise_channel(const GradientNoiseSettings &noise, int channel,
                              double position) {
  const auto roughness = static_cast<double>(noise.roughness) / 4096.0;
  double total = 0.0;
  double weight = 0.0;
  auto amplitude = 1.0;
  auto frequency = 4.0 + roughness * 60.0;
  for (int octave = 0; octave < 4; ++octave) {
    total += smooth_noise(noise.seed + static_cast<std::uint32_t>(octave * 977),
                          channel, position * frequency, roughness) *
             amplitude;
    weight += amplitude;
    amplitude *= 0.5;
    frequency *= 2.0;
  }
  const auto value = total / std::max(0.0001, weight);
  const auto index = static_cast<std::size_t>(std::clamp(channel, 0, 3));
  const auto minimum = noise.minimum[index] / 100.0;
  const auto maximum = noise.maximum[index] / 100.0;
  return std::clamp(minimum + (maximum - minimum) * value, 0.0, 1.0);
}

}  // namespace

std::uint8_t clamp_byte(float value) {
  return static_cast<std::uint8_t>(std::clamp(std::lround(value), 0L, 255L));
}

float clamp_unit(float value) {
  return std::clamp(value, 0.0F, 1.0F);
}

float blend_if_threshold_factor(const BlendIfThresholds& thresholds, std::uint8_t value) noexcept {
  if (value < thresholds.black_low || value > thresholds.white_high) {
    return 0.0F;
  }
  if (value < thresholds.black_high) {
    // Photoshop's split handles include both byte endpoints in the feather.
    // This makes the first retained value 1/(span+1), rather than zero, while
    // joined handles keep their hard-cutoff behavior.
    const auto width = static_cast<int>(thresholds.black_high) - static_cast<int>(thresholds.black_low) + 1;
    return static_cast<float>(static_cast<int>(value) - static_cast<int>(thresholds.black_low) + 1) /
           static_cast<float>(width);
  }
  if (value > thresholds.white_low) {
    const auto width = static_cast<int>(thresholds.white_high) - static_cast<int>(thresholds.white_low) + 1;
    return static_cast<float>(static_cast<int>(thresholds.white_high) - static_cast<int>(value) + 1) /
           static_cast<float>(width);
  }
  return 1.0F;
}

std::uint8_t blend_if_gray_value(RgbColor color) noexcept {
  // Photoshop's composite RGB control follows the PDF-family perceived-luma
  // weights. Fixed thousandths reproduce the PS 2026 primary/secondary-color
  // probes without toolchain-dependent floating-point tie behavior.
  return static_cast<std::uint8_t>((299 * static_cast<int>(color.red) + 590 * static_cast<int>(color.green) +
                                    111 * static_cast<int>(color.blue) + 500) /
                                   1000);
}

namespace {

float blend_if_color_factor(const LayerBlendIf& settings, RgbColor color, bool source) noexcept {
  const std::array<std::uint8_t, 4> values{blend_if_gray_value(color), color.red, color.green, color.blue};
  float factor = 1.0F;
  for (std::size_t channel = 0; channel < settings.channels.size(); ++channel) {
    const auto& ranges = settings.channels[channel];
    factor *= blend_if_threshold_factor(source ? ranges.this_layer : ranges.underlying_layer, values[channel]);
    if (factor <= 0.0F) {
      return 0.0F;
    }
  }
  return factor;
}

std::uint8_t blend_if_threshold_alpha_byte(const BlendIfThresholds& thresholds,
                                           std::uint8_t value) noexcept {
  if (value < thresholds.black_low || value > thresholds.white_high) {
    return 0;
  }
  if (value < thresholds.black_high) {
    const auto numerator = static_cast<int>(value) - static_cast<int>(thresholds.black_low) + 1;
    const auto denominator =
        static_cast<int>(thresholds.black_high) - static_cast<int>(thresholds.black_low) + 1;
    return static_cast<std::uint8_t>((numerator * 255) / denominator);
  }
  if (value > thresholds.white_low) {
    const auto numerator = static_cast<int>(thresholds.white_high) - static_cast<int>(value) + 1;
    const auto denominator =
        static_cast<int>(thresholds.white_high) - static_cast<int>(thresholds.white_low) + 1;
    return static_cast<std::uint8_t>((numerator * 255) / denominator);
  }
  return 255;
}

std::uint8_t blend_if_color_alpha_byte(const LayerBlendIf& settings, RgbColor color, bool source) noexcept {
  const std::array<std::uint8_t, 4> values{blend_if_gray_value(color), color.red, color.green, color.blue};
  int factor = 255;
  for (std::size_t channel = 0; channel < settings.channels.size(); ++channel) {
    const auto& ranges = settings.channels[channel];
    const auto channel_factor = blend_if_threshold_alpha_byte(
        source ? ranges.this_layer : ranges.underlying_layer, values[channel]);
    factor = (factor * static_cast<int>(channel_factor)) / 255;
    if (factor <= 0) {
      return 0;
    }
  }
  return static_cast<std::uint8_t>(factor);
}

}  // namespace

float blend_if_source_factor(const LayerBlendIf& settings, RgbColor source) noexcept {
  return blend_if_color_factor(settings, source, true);
}

float blend_if_underlying_factor(const LayerBlendIf& settings, RgbColor underlying) noexcept {
  return blend_if_color_factor(settings, underlying, false);
}

std::uint8_t blend_if_source_alpha_byte(const LayerBlendIf& settings, RgbColor source) noexcept {
  return blend_if_color_alpha_byte(settings, source, true);
}

std::uint8_t blend_if_underlying_alpha_byte(const LayerBlendIf& settings, RgbColor underlying) noexcept {
  return blend_if_color_alpha_byte(settings, underlying, false);
}

std::array<std::uint8_t, 3> blend_rgb(std::array<std::uint8_t, 3> source,
                                      std::array<std::uint8_t, 3> destination, BlendMode mode) {
  // The four non-separable modes use the PDF-spec luma-based algorithm shared
  // by Photoshop and Aseprite (July 2026: this replaced an HSL-lightness
  // approximation for Saturation/Luminosity; the compositor blend table was
  // re-pinned deliberately).
  if (mode == BlendMode::Saturation || mode == BlendMode::Luminosity || mode == BlendMode::Hue ||
      mode == BlendMode::Color) {
    const RgbDouble src{source[0] / 255.0, source[1] / 255.0, source[2] / 255.0};
    const RgbDouble dst{destination[0] / 255.0, destination[1] / 255.0, destination[2] / 255.0};
    RgbDouble result;
    switch (mode) {
      case BlendMode::Hue:
        result = pdf_set_lum(pdf_set_sat(src, pdf_sat(dst)), pdf_lum(dst));
        break;
      case BlendMode::Saturation:
        result = pdf_set_lum(pdf_set_sat(dst, pdf_sat(src)), pdf_lum(dst));
        break;
      case BlendMode::Color:
        result = pdf_set_lum(src, pdf_lum(dst));
        break;
      case BlendMode::Luminosity:
      default:
        result = pdf_set_lum(dst, pdf_lum(src));
        break;
    }
    return {clamp_byte(static_cast<float>(result.r * 255.0)), clamp_byte(static_cast<float>(result.g * 255.0)),
            clamp_byte(static_cast<float>(result.b * 255.0))};
  }
  return {blend_channel(source[0], destination[0], mode), blend_channel(source[1], destination[1], mode),
          blend_channel(source[2], destination[2], mode)};
}

std::array<std::uint8_t, 3> composite_blended_rgb(std::array<std::uint8_t, 3> source,
                                                  std::array<std::uint8_t, 3> destination, BlendMode mode,
                                                  float source_alpha, float destination_alpha) {
  source_alpha = clamp_unit(source_alpha);
  destination_alpha = clamp_unit(destination_alpha);
  const auto output_alpha = source_alpha + destination_alpha * (1.0F - source_alpha);
  if (output_alpha <= 0.0F) {
    return {0, 0, 0};
  }

  const auto blended = blend_rgb(source, destination, mode);
  std::array<std::uint8_t, 3> output = {};
  for (std::size_t channel = 0; channel < output.size(); ++channel) {
    const auto source_value = static_cast<float>(source[channel]);
    const auto destination_value = static_cast<float>(destination[channel]);
    const auto blended_value = static_cast<float>(blended[channel]);
    output[channel] =
        clamp_byte((source_value * source_alpha * (1.0F - destination_alpha) +
                    blended_value * source_alpha * destination_alpha +
                    destination_value * destination_alpha * (1.0F - source_alpha)) /
                   output_alpha);
  }
  return output;
}

float gradient_stop_opacity(const LayerStyleGradient& gradient, float position) {
  if (gradient.form == GradientDefinitionForm::Noise) {
    return gradient.noise.add_transparency
               ? static_cast<float>(
                     gradient_noise_channel(gradient.noise, 3, position))
               : 1.0F;
  }
  if (gradient.alpha_stops.empty()) {
    return 1.0F;
  }
  const auto& stops = gradient.alpha_stops;
  if (position <= stops.front().location) {
    return stops.front().opacity;
  }
  if (position >= stops.back().location) {
    return stops.back().opacity;
  }
  for (std::size_t index = 1; index < stops.size(); ++index) {
    const auto& right = stops[index];
    const auto& left = stops[index - 1U];
    if (position <= right.location) {
      const auto span = std::max(0.0001F, right.location - left.location);
      auto t = (position - left.location) / span;
      // Keep the historical default path instruction-for-instruction identical.
      // Non-default Photoshop midpoints remap the segment around its 50% value.
      if (right.midpoint != 0.5F) {
        t = midpoint_remap(t,right. midpoint);
      }
      return left.opacity + (right.opacity - left.opacity) * t;
    }
  }
  return stops.back().opacity;
}

RgbColor gradient_color(const LayerStyleGradient& gradient, float position) {
  if (gradient.form == GradientDefinitionForm::Noise) {
    auto noise_channel = [&](int channel) {
      return gradient_noise_channel(gradient.noise, channel, position);
    };
    if (gradient.noise.color_model == GradientNoiseColorModel::HSB) {
      const auto h = noise_channel(0);
      const auto s = noise_channel(1);
      const auto v = noise_channel(2);
      const auto sector = h * 6.0;
      const auto index = static_cast<int>(std::floor(sector)) % 6;
      const auto fraction = sector - std::floor(sector);
      const auto p = v * (1.0 - s);
      const auto q = v * (1.0 - fraction * s);
      const auto t = v * (1.0 - (1.0 - fraction) * s);
      std::array<double, 3> rgb{};
      switch (index) {
      case 0:
        rgb = {v, t, p};
        break;
      case 1:
        rgb = {q, v, p};
        break;
      case 2:
        rgb = {p, v, t};
        break;
      case 3:
        rgb = {p, q, v};
        break;
      case 4:
        rgb = {t, p, v};
        break;
      default:
        rgb = {v, p, q};
        break;
      }
      return RgbColor{clamp_byte(static_cast<float>(rgb[0] * 255.0)),
                      clamp_byte(static_cast<float>(rgb[1] * 255.0)),
                      clamp_byte(static_cast<float>(rgb[2] * 255.0))};
    }
    // RGB is native. Lab noise is kept editable and deterministic; render its
    // bounded channels through an OKLab-shaped approximation.
    if (gradient.noise.color_model == GradientNoiseColorModel::Lab) {
      return oklab_to_rgb(Oklab{noise_channel(0), noise_channel(1) - 0.5,
                                noise_channel(2) - 0.5});
    }
    return RgbColor{clamp_byte(static_cast<float>(noise_channel(0) * 255.0)),
                    clamp_byte(static_cast<float>(noise_channel(1) * 255.0)),
                    clamp_byte(static_cast<float>(noise_channel(2) * 255.0))};
  }
  if (gradient.color_stops.empty()) {
    const auto value = clamp_byte(position * 255.0F);
    return RgbColor{value, value, value};
  }
  const auto& stops = gradient.color_stops;
  if (position <= stops.front().location) {
    return stops.front().color;
  }
  if (position >= stops.back().location) {
    return stops.back().color;
  }
  for (std::size_t index = 1; index < stops.size(); ++index) {
    const auto& right = stops[index];
    const auto& left = stops[index - 1U];
    if (position <= right.location) {
      const auto span = std::max(0.0001F, right.location - left.location);
      auto t = (position - left.location) / span;
      if (right.midpoint != 0.5F) {
        t = midpoint_remap(t, right. midpoint);
      }
      if (gradient.interpolation == GradientInterpolationMethod::Perceptual) {
        const auto a = rgb_to_oklab(left.color);
        const auto b = rgb_to_oklab(right.color);
        return oklab_to_rgb(Oklab{a.l + (b.l - a.l) *
        t, a.a + (b.a - a.a) * t,
                                  a.b + (b.b - a.b) * t});
      }
      if (gradient.interpolation == GradientInterpolationMethod::Linear) {
        auto channel = [t](std::uint8_t a, std::uint8_t b) {
          const auto value =
              srgb_to_linear(a / 255.0) +
              (srgb_to_linear(b / 255.0) - srgb_to_linear(a / 255.0)) * t;
          return clamp_byte(static_cast<float>(linear_to_srgb(value) * 255.0));
        };
        return RgbColor{channel(left.color.red, right.color.red),
                        channel(left.color.green, right.color.green),
                        channel(left.color.blue, right.color.blue)};
      }
      const auto linear_channel = [t](double a, double b) {
        return a + (b - a) * t;
      };
      const auto previous = index > 1 ? stops[index - 2U].color : left.color;
      const auto next =
          index + 1U < stops.size() ? stops[index + 1U].color : right.color;
      const auto smoothness =
          stops.size() > 2U ? static_cast<double>(gradient.smoothness) / 4096.0
                            : 0.0;
      auto channel = [&](std::uint8_t p0, std::uint8_t p1, std::uint8_t p2,
                         std::uint8_t p3) {
        const auto linear = linear_channel(p1, p2);
        const auto cubic = catmull_rom(p0, p1, p2, p3, t);
        return clamp_byte(
            static_cast<float>(linear + (cubic - linear) * smoothness));
      };
      return RgbColor{
          channel(previous.red,left.color.red,right.color.red, next.red),
          channel(previous.green,left.color.green, right.color.green,
                  next.green),
          channel(previous.blue,left.color.blue,right.color.blue, next.blue)};
    }
  }
  return stops.back().color;
}

RgbColor gradient_color_dithered(const LayerStyleGradient& gradient,
                                 float position, std::int32_t x,
                                 std::int32_t y) {
  auto color = gradient_color(gradient, position);
  if (!gradient.dither) {
    return color;
  }
  const auto hash = splitmix64(
      static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) |
      (static_cast<std::uint64_t>(static_cast<std::uint32_t>(y)) << 32U));
  const auto delta = static_cast<int>((hash >> 61U) & 3U) - 1;
  const auto adjust = [delta](std::uint8_t value) {
    return static_cast<std::uint8_t>(
        std::clamp(static_cast<int>(value) + delta, 0, 255));
  };
  color.red = adjust(color.red);
  color.green = adjust(color.green);
  color.blue = adjust(color.blue);
  return color;
}

float gradient_position(const LayerStyleGradient &gradient, Rect bounds, std::int32_t x, std::int32_t y) {
  const auto center_x = static_cast<float>(bounds.x) + static_cast<float>(bounds.width) *
                            ( 0.5F + gradient.offset_x_percent / 100.0F);
  const auto center_y = static_cast<float>(bounds.y) + static_cast<float>(bounds.height) *
                            ( 0.5F + gradient.offset_y_percent / 100.0F);
  const auto px = static_cast<float>(x) + 0.5F;
  const auto py = static_cast<float>(y) + 0.5F;
  const auto radians = gradient.angle_degrees * kPi / 180.0F;
  const auto local_x =
      (px - center_x) * std::cos(radians) - (py - center_y) * std::sin(radians);
  const auto local_y =
      (px - center_x) * std::sin(radians) + (py - center_y) * std::cos(radians);
  float position = 0.0F;
  switch (gradient.type) {
    case LayerStyleGradientType::Radial: {
      const auto dx =
        local_x / std::max(1.0F, static_cast<float>(bounds.width) * 0.5F);
      const auto dy =
        local_y / std::max(1.0F, static_cast<float>(bounds.height) * 0.5F);
      position = std::sqrt(dx * dx + dy * dy);
      break;
    }
    case LayerStyleGradientType::Angle: {
      position = (std::atan2(local_y, local_x) + kPi) / (2.0F * kPi);
      break;
    }
    case LayerStyleGradientType::Reflected:
      position = std::abs(
        0.5F + local_x / std::max(1.0F, static_cast<float>(bounds.width)));
    position = std::abs(position * 2.0F - 1.0F);
      break;
    case LayerStyleGradientType::Diamond: {
      const auto dx = std::abs(local_x) / std::max(1.0F, static_cast<float>(bounds.width) * 0.5F);
      const auto dy = std::abs(local_y) / std::max(1.0F, static_cast<float>(bounds.height) * 0.5F);
      position = std::max(dx, dy);
      break;
    }
    case LayerStyleGradientType::Linear:
      position =
        0.5F + local_x / std::max(1.0F, static_cast<float>(bounds.width));
      break;
  }
  const auto scale = std::max(0.01F, gradient.scale);
  position = 0.5F + (position - 0.5F) / scale;
  if (gradient.reverse) {
    position = 1.0F - position;
  }
  return clamp_unit(position);
}

}  // namespace patchy
