#include "core/photo_develop.hpp"

#include "core/pixel_depth.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace patchy {

namespace {

// How far +-100 moves each channel, in stops of linear light, before the luminance
// normalization: temperature moves red and blue apart, tint moves green.
constexpr double kTemperatureStops = 0.6;
constexpr double kTintStops = 0.4;

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

double bounded(double value, double limit) {
  return std::isfinite(value) ? std::clamp(value, -limit, limit) : 0.0;
}

// The sRGB transfer, continued past 0..1 for 32-bit values: linear below zero, the same
// power law above one.
double encode_extended(double linear) {
  if (linear <= 0.0) return linear * 12.92;
  if (linear <= 1.0) return srgb_encode(linear);
  return 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
}

double decode_extended(double encoded) {
  if (encoded <= 0.0) return encoded / 12.92;
  if (encoded <= 1.0) return srgb_decode(encoded);
  return std::pow((encoded + 0.055) / 1.055, 2.4);
}

double color_factor(double red, double green, double blue, double saturation_amount, double vibrance_amount) {
  const auto maximum = std::max({red, green, blue});
  const auto minimum = std::min({red, green, blue});
  const auto pixel_saturation = maximum > 0.0 ? std::clamp((maximum - minimum) / maximum, 0.0, 1.0) : 0.0;
  return std::max(0.0, 1.0 + saturation_amount + vibrance_amount * (1.0 - pixel_saturation));
}

}  // namespace

double tone_curve_value(double x, const ToneParams& params) {
  const auto shadows_amount = std::clamp(params.shadows, -100.0, 100.0) / 100.0;
  const auto highlights_amount = std::clamp(params.highlights, -100.0, 100.0) / 100.0;
  const auto contrast_amount = std::clamp(params.contrast, -100.0, 100.0) / 100.0;
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
  return std::clamp(x, 0.0, 1.0);
}

std::array<std::uint16_t, 65536> build_tone_lut(const ToneParams& params) {
  std::array<std::uint16_t, 65536> lut;
  for (int index = 0; index < 65536; ++index) {
    lut[static_cast<std::size_t>(index)] = static_cast<std::uint16_t>(
        std::lround(tone_curve_value(static_cast<double>(index) / 65535.0, params) * 65535.0));
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

PhotoDevelopParams normalize_photo_develop_params(PhotoDevelopParams params) noexcept {
  params.temperature = bounded(params.temperature, 100.0);
  params.tint = bounded(params.tint, 100.0);
  params.exposure_ev = bounded(params.exposure_ev, kPhotoDevelopMaxExposureEv);
  params.contrast = bounded(params.contrast, 100.0);
  params.highlights = bounded(params.highlights, 100.0);
  params.shadows = bounded(params.shadows, 100.0);
  params.saturation = bounded(params.saturation, 100.0);
  params.vibrance = bounded(params.vibrance, 100.0);
  return params;
}

std::array<double, 3> photo_white_balance_gains(double temperature, double tint) noexcept {
  const auto warm = bounded(temperature, 100.0) / 100.0 * kTemperatureStops;
  const auto magenta = bounded(tint, 100.0) / 100.0 * kTintStops;
  if (warm == 0.0 && magenta == 0.0) return {1.0, 1.0, 1.0};  // exact, whatever the weights sum to
  std::array<double, 3> gains = {std::exp2(warm), std::exp2(-magenta), std::exp2(-warm)};
  const auto white_luminance = 0.2126 * gains[0] + 0.7152 * gains[1] + 0.0722 * gains[2];
  for (auto& gain : gains) gain /= white_luminance;
  return gains;
}

std::pair<double, double> photo_auto_white_balance(std::array<double, 3> mean_linear_rgb) noexcept {
  constexpr double kMinimum = 1e-6;
  for (const auto channel : mean_linear_rgb) {
    if (!std::isfinite(channel) || channel < kMinimum) return {0.0, 0.0};
  }
  // Gains r = 2^(a t), g = 2^(-c m), b = 2^(-a t) (up to the shared normalization) make
  // the mean neutral when r R = g G = b B.
  const auto log_red = std::log2(mean_linear_rgb[0]);
  const auto log_green = std::log2(mean_linear_rgb[1]);
  const auto log_blue = std::log2(mean_linear_rgb[2]);
  const auto warm = (log_blue - log_red) / 2.0;
  const auto magenta = log_green - log_red - warm;
  return {std::clamp(warm / kTemperatureStops * 100.0, -100.0, 100.0),
          std::clamp(magenta / kTintStops * 100.0, -100.0, 100.0)};
}

std::array<double, 3> photo_mean_linear_rgb(const PixelBuffer& pixels) {
  std::array<double, 3> sum{};
  double weight = 0.0;
  if (pixels.empty() || pixels.format().color_mode != ColorMode::RGB || pixels.format().channels < 3) return sum;
  std::vector<float> row(static_cast<std::size_t>(pixels.width()) * 4U);
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    load_rgba_row(pixels, y, 0, pixels.width(), DeepDomain::Linear, row);
    for (std::size_t x = 0; x < static_cast<std::size_t>(pixels.width()); ++x) {
      const auto alpha = static_cast<double>(row[x * 4 + 3]) / kDeepScale;
      for (std::size_t c = 0; c < 3; ++c) sum[c] += alpha * row[x * 4 + c] / kDeepScale;
      weight += alpha;
    }
  }
  if (weight > 0.0) {
    for (auto& channel : sum) channel /= weight;
  }
  return sum;
}

PhotoDeveloper::PhotoDeveloper(const PhotoDevelopParams& params) : params_(normalize_photo_develop_params(params)) {
  const auto gains = photo_white_balance_gains(params_.temperature, params_.tint);
  const auto exposure = std::exp2(params_.exposure_ev);
  for (std::size_t c = 0; c < 3; ++c) scale_[c] = gains[c] * exposure;
  scale_active_ = params_.temperature != 0.0 || params_.tint != 0.0 || params_.exposure_ev != 0.0;
  const ToneParams tone{params_.contrast, params_.highlights, params_.shadows};
  tone_active_ = !tone.is_neutral();
  color_stage_ = params_.saturation != 0.0 || params_.vibrance != 0.0;
  channel_stage_ = scale_active_ || tone_active_;
  if (tone_active_) {
    const auto lut = build_tone_lut(tone);
    tone_lut_.assign(lut.begin(), lut.end());
  }
  if (!channel_stage_) return;
  for (std::size_t c = 0; c < 3; ++c) {
    auto& table = channel_luts_[c];
    table.resize(65536);
    for (std::size_t index = 0; index < table.size(); ++index) {
      if (!scale_active_) {
        table[index] = tone_lut_[index];
        continue;
      }
      auto encoded = srgb_encode(srgb_decode(static_cast<double>(index) / 65535.0) * scale_[c]);
      if (tone_active_) encoded = tone_at(encoded);
      table[index] = static_cast<std::uint16_t>(std::lround(std::clamp(encoded, 0.0, 1.0) * 65535.0));
    }
  }
}

double PhotoDeveloper::tone_at(double encoded) const noexcept {
  if (encoded <= 0.0) return encoded;
  if (encoded >= 1.0) return tone_lut_.back() / 65535.0 + (encoded - 1.0);
  const auto position = encoded * 65535.0;
  const auto index = std::min<std::size_t>(static_cast<std::size_t>(position), 65534U);
  const auto fraction = position - static_cast<double>(index);
  return (tone_lut_[index] + (tone_lut_[index + 1] - static_cast<double>(tone_lut_[index])) * fraction) / 65535.0;
}

void PhotoDeveloper::apply_linear(std::array<float, 3>& rgb) const noexcept {
  if (is_identity()) return;
  std::array<double, 3> values{};
  for (std::size_t c = 0; c < 3; ++c) {
    if (!std::isfinite(rgb[c])) return;
    values[c] = scale_active_ ? rgb[c] * scale_[c] : rgb[c];
  }
  if (tone_active_ || color_stage_) {
    for (auto& value : values) {
      value = encode_extended(value);
      if (tone_active_) value = tone_at(value);
    }
    if (color_stage_) {
      const auto luma = 0.2126 * values[0] + 0.7152 * values[1] + 0.0722 * values[2];
      const auto factor = color_factor(values[0], values[1], values[2], params_.saturation / 100.0,
                                       params_.vibrance / 100.0);
      for (auto& value : values) value = std::max(0.0, luma + (value - luma) * factor);
    }
    for (auto& value : values) value = decode_extended(value);
  }
  for (std::size_t c = 0; c < 3; ++c) rgb[c] = static_cast<float>(values[c]);
}

void PhotoDeveloper::apply_row(PixelBuffer& pixels, std::int32_t y, std::int32_t x_begin, std::int32_t x_end) const {
  const auto format = pixels.format();
  if (is_identity() || format.color_mode != ColorMode::RGB || format.channels < 3 || y < 0 || y >= pixels.height())
    return;
  x_begin = std::max(0, x_begin);
  x_end = std::min(pixels.width(), x_end);
  const auto sample_bytes = bytes_per_channel(format.bit_depth);
  const auto pixel_bytes = bytes_per_pixel(format);
  auto* px = pixels.row(y).data() + static_cast<std::size_t>(std::max(0, x_begin)) * pixel_bytes;
  for (std::int32_t x = x_begin; x < x_end; ++x, px += pixel_bytes) {
    switch (format.bit_depth) {
      case BitDepth::UInt8: {
        std::array<std::uint16_t, 3> rgb = {widen_u8_to_u16(px[0]), widen_u8_to_u16(px[1]), widen_u8_to_u16(px[2])};
        if (channel_stage_) {
          for (std::size_t c = 0; c < 3; ++c) rgb[c] = channel_luts_[c][rgb[c]];
        }
        if (color_stage_) apply_color(rgb, params_.saturation, params_.vibrance);
        for (std::size_t c = 0; c < 3; ++c) px[c] = narrow_u16_to_u8(rgb[c]);
        break;
      }
      case BitDepth::UInt16: {
        std::array<std::uint16_t, 3> rgb{};
        std::memcpy(rgb.data(), px, 3 * sample_bytes);
        if (channel_stage_) {
          for (std::size_t c = 0; c < 3; ++c) rgb[c] = channel_luts_[c][rgb[c]];
        }
        if (color_stage_) apply_color(rgb, params_.saturation, params_.vibrance);
        std::memcpy(px, rgb.data(), 3 * sample_bytes);
        break;
      }
      case BitDepth::Float32: {
        std::array<float, 3> rgb{};
        std::memcpy(rgb.data(), px, 3 * sample_bytes);
        apply_linear(rgb);
        std::memcpy(px, rgb.data(), 3 * sample_bytes);
        break;
      }
    }
  }
}

void PhotoDeveloper::apply(PixelBuffer& pixels) const {
  if (is_identity()) return;
  for (std::int32_t y = 0; y < pixels.height(); ++y) apply_row(pixels, y, 0, pixels.width());
}

}  // namespace patchy
