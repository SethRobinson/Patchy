#include "core/blend_math.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace photoslop {

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
    case BlendMode::Saturation:
    case BlendMode::Luminosity:
      return src;
  }
  return src;
}

struct HslColor {
  float hue{0.0F};
  float saturation{0.0F};
  float lightness{0.0F};
};

HslColor rgb_to_hsl(std::array<std::uint8_t, 3> rgb) {
  const auto red = static_cast<float>(rgb[0]) / 255.0F;
  const auto green = static_cast<float>(rgb[1]) / 255.0F;
  const auto blue = static_cast<float>(rgb[2]) / 255.0F;
  const auto maximum = std::max({red, green, blue});
  const auto minimum = std::min({red, green, blue});
  const auto delta = maximum - minimum;
  HslColor hsl;
  hsl.lightness = (maximum + minimum) * 0.5F;
  if (delta <= 0.0F) {
    return hsl;
  }
  hsl.saturation = hsl.lightness > 0.5F ? delta / (2.0F - maximum - minimum) : delta / (maximum + minimum);
  if (maximum == red) {
    hsl.hue = (green - blue) / delta + (green < blue ? 6.0F : 0.0F);
  } else if (maximum == green) {
    hsl.hue = (blue - red) / delta + 2.0F;
  } else {
    hsl.hue = (red - green) / delta + 4.0F;
  }
  hsl.hue /= 6.0F;
  return hsl;
}

float hue_to_rgb(float p, float q, float t) {
  if (t < 0.0F) {
    t += 1.0F;
  }
  if (t > 1.0F) {
    t -= 1.0F;
  }
  if (t < 1.0F / 6.0F) {
    return p + (q - p) * 6.0F * t;
  }
  if (t < 0.5F) {
    return q;
  }
  if (t < 2.0F / 3.0F) {
    return p + (q - p) * (2.0F / 3.0F - t) * 6.0F;
  }
  return p;
}

std::array<std::uint8_t, 3> hsl_to_rgb(HslColor hsl) {
  hsl.hue = hsl.hue - std::floor(hsl.hue);
  hsl.saturation = clamp_unit(hsl.saturation);
  hsl.lightness = clamp_unit(hsl.lightness);
  if (hsl.saturation <= 0.0F) {
    const auto gray = clamp_byte(hsl.lightness * 255.0F);
    return {gray, gray, gray};
  }
  const auto q = hsl.lightness < 0.5F ? hsl.lightness * (1.0F + hsl.saturation)
                                      : hsl.lightness + hsl.saturation - hsl.lightness * hsl.saturation;
  const auto p = 2.0F * hsl.lightness - q;
  return {clamp_byte(hue_to_rgb(p, q, hsl.hue + 1.0F / 3.0F) * 255.0F),
          clamp_byte(hue_to_rgb(p, q, hsl.hue) * 255.0F),
          clamp_byte(hue_to_rgb(p, q, hsl.hue - 1.0F / 3.0F) * 255.0F)};
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
  if (mode == BlendMode::Saturation || mode == BlendMode::Luminosity) {
    const auto src_hsl = rgb_to_hsl(source);
    auto dst_hsl = rgb_to_hsl(destination);
    if (mode == BlendMode::Saturation) {
      dst_hsl.saturation = src_hsl.saturation;
    } else {
      dst_hsl.lightness = src_hsl.lightness;
    }
    return hsl_to_rgb(dst_hsl);
  }
  return {blend_channel(source[0], destination[0], mode), blend_channel(source[1], destination[1], mode),
          blend_channel(source[2], destination[2], mode)};
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

}  // namespace photoslop
