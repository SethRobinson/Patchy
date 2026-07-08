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
      // Round the s*d/255 product BEFORE doubling (Photoshop/Aseprite's un8 multiply);
      // rounding the doubled product instead is off by one on half the inputs.
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

// PDF-spec non-separable blend components (the algorithm Photoshop and Aseprite share):
// luma-weighted set_lum with clipping and min/mid/max-based set_sat. Double math with a
// final lround keeps the output toolchain-deterministic.
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

float linear_gradient_position(Rect bounds, std::int32_t x, std::int32_t y, float angle_degrees) {
  const auto radians = angle_degrees * kPi / 180.0F;
  const auto dx = std::cos(radians);
  const auto dy = -std::sin(radians);
  const std::array<std::pair<float, float>, 4> corners = {
      std::pair<float, float>{static_cast<float>(bounds.x), static_cast<float>(bounds.y)},
      std::pair<float, float>{static_cast<float>(bounds.x + bounds.width), static_cast<float>(bounds.y)},
      std::pair<float, float>{static_cast<float>(bounds.x), static_cast<float>(bounds.y + bounds.height)},
      std::pair<float, float>{static_cast<float>(bounds.x + bounds.width),
                              static_cast<float>(bounds.y + bounds.height)}};
  auto minimum = corners.front().first * dx + corners.front().second * dy;
  auto maximum = minimum;
  for (const auto& corner : corners) {
    const auto projection = corner.first * dx + corner.second * dy;
    minimum = std::min(minimum, projection);
    maximum = std::max(maximum, projection);
  }
  const auto projection = (static_cast<float>(x) + 0.5F) * dx + (static_cast<float>(y) + 0.5F) * dy;
  return (projection - minimum) / std::max(0.0001F, maximum - minimum);
}

}  // namespace

std::uint8_t clamp_byte(float value) {
  return static_cast<std::uint8_t>(std::clamp(std::lround(value), 0L, 255L));
}

float clamp_unit(float value) {
  return std::clamp(value, 0.0F, 1.0F);
}

std::array<std::uint8_t, 3> blend_rgb(std::array<std::uint8_t, 3> source,
                                      std::array<std::uint8_t, 3> destination, BlendMode mode) {
  // The four non-separable modes use the PDF-spec luma-based algorithm shared by Photoshop
  // and Aseprite (July 2026: this replaced an HSL-lightness approximation for
  // Saturation/Luminosity; the compositor blend table was re-pinned deliberately).
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
      const auto t = (position - left.location) / span;
      return left.opacity + (right.opacity - left.opacity) * t;
    }
  }
  return stops.back().opacity;
}

RgbColor gradient_color(const LayerStyleGradient& gradient, float position) {
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
      const auto t = (position - left.location) / span;
      return RgbColor{clamp_byte(static_cast<float>(left.color.red) +
                                 (static_cast<float>(right.color.red) - static_cast<float>(left.color.red)) * t),
                      clamp_byte(static_cast<float>(left.color.green) +
                                 (static_cast<float>(right.color.green) - static_cast<float>(left.color.green)) * t),
                      clamp_byte(static_cast<float>(left.color.blue) +
                                 (static_cast<float>(right.color.blue) - static_cast<float>(left.color.blue)) * t)};
    }
  }
  return stops.back().color;
}

float gradient_position(const LayerStyleGradient& gradient, Rect bounds, std::int32_t x, std::int32_t y) {
  const auto center_x = static_cast<float>(bounds.x) + static_cast<float>(bounds.width) * 0.5F;
  const auto center_y = static_cast<float>(bounds.y) + static_cast<float>(bounds.height) * 0.5F;
  const auto px = static_cast<float>(x) + 0.5F;
  const auto py = static_cast<float>(y) + 0.5F;
  float position = 0.0F;
  switch (gradient.type) {
    case LayerStyleGradientType::Radial: {
      const auto dx = (px - center_x) / std::max(1.0F, static_cast<float>(bounds.width) * 0.5F);
      const auto dy = (py - center_y) / std::max(1.0F, static_cast<float>(bounds.height) * 0.5F);
      position = std::sqrt(dx * dx + dy * dy);
      break;
    }
    case LayerStyleGradientType::Angle: {
      position = (std::atan2(py - center_y, px - center_x) + kPi) / (2.0F * kPi);
      break;
    }
    case LayerStyleGradientType::Reflected:
      position = std::abs(linear_gradient_position(bounds, x, y, gradient.angle_degrees) * 2.0F - 1.0F);
      break;
    case LayerStyleGradientType::Diamond: {
      const auto dx = std::abs(px - center_x) / std::max(1.0F, static_cast<float>(bounds.width) * 0.5F);
      const auto dy = std::abs(py - center_y) / std::max(1.0F, static_cast<float>(bounds.height) * 0.5F);
      position = std::max(dx, dy);
      break;
    }
    case LayerStyleGradientType::Linear:
      position = linear_gradient_position(bounds, x, y, gradient.angle_degrees);
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
