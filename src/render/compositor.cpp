#include "render/compositor.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <utility>
#include <vector>

namespace photoslop {

namespace {

std::uint8_t clamp_byte(float value) {
  return static_cast<std::uint8_t>(std::clamp(std::lround(value), 0L, 255L));
}

float clamp_unit(float value) {
  return std::clamp(value, 0.0F, 1.0F);
}

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

std::array<std::uint8_t, 3> blend_rgb(std::array<std::uint8_t, 3> src, std::array<std::uint8_t, 3> dst,
                                      BlendMode mode) {
  if (mode == BlendMode::Saturation || mode == BlendMode::Luminosity) {
    const auto src_hsl = rgb_to_hsl(src);
    auto dst_hsl = rgb_to_hsl(dst);
    if (mode == BlendMode::Saturation) {
      dst_hsl.saturation = src_hsl.saturation;
    } else {
      dst_hsl.lightness = src_hsl.lightness;
    }
    return hsl_to_rgb(dst_hsl);
  }
  return {blend_channel(src[0], dst[0], mode), blend_channel(src[1], dst[1], mode),
          blend_channel(src[2], dst[2], mode)};
}

Rect intersect(Rect a, Rect b) {
  const auto left = std::max(a.x, b.x);
  const auto top = std::max(a.y, b.y);
  const auto right = std::min(a.x + a.width, b.x + b.width);
  const auto bottom = std::min(a.y + a.height, b.y + b.height);
  return Rect{left, top, std::max(0, right - left), std::max(0, bottom - top)};
}

Rect expand(Rect rect, int amount) {
  return Rect{rect.x - amount, rect.y - amount, rect.width + amount * 2, rect.height + amount * 2};
}

Rect clipped_mask_bounds(Rect full_bounds, Rect draw_rect, int sample_padding) {
  return intersect(full_bounds, expand(draw_rect, std::max(0, sample_padding)));
}

Rect layer_pixel_bounds(const Layer& layer) {
  const auto& source = layer.pixels();
  return layer.bounds().empty() ? Rect::from_size(source.width(), source.height()) : layer.bounds();
}

float layer_alpha_at(const Layer& layer, Rect bounds, std::int32_t x, std::int32_t y) {
  const auto& source = layer.pixels();
  const auto sx = x - bounds.x;
  const auto sy = y - bounds.y;
  if (sx < 0 || sy < 0 || sx >= source.width() || sy >= source.height()) {
    return 0.0F;
  }
  const auto format = source.format();
  if (format.channels < 4) {
    return 1.0F;
  }
  const auto* pixel = source.data().data() + static_cast<std::size_t>(sy) * source.stride_bytes() +
                      static_cast<std::size_t>(sx) * format.channels;
  return static_cast<float>(pixel[3]) / 255.0F;
}

std::vector<float> layer_alpha_mask(const Layer& layer, Rect bounds, Rect mask_bounds,
                                    std::int32_t sample_offset_x = 0, std::int32_t sample_offset_y = 0) {
  if (mask_bounds.empty()) {
    return {};
  }

  const auto& source = layer.pixels();
  const auto width = mask_bounds.width;
  const auto height = mask_bounds.height;
  std::vector<float> mask(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0.0F);
  if (source.empty()) {
    return mask;
  }

  const auto format = source.format();
  const auto source_left = bounds.x - sample_offset_x;
  const auto source_top = bounds.y - sample_offset_y;
  const auto source_right = bounds.x + source.width() - sample_offset_x;
  const auto source_bottom = bounds.y + source.height() - sample_offset_y;
  const auto draw_left = std::max(mask_bounds.x, source_left);
  const auto draw_top = std::max(mask_bounds.y, source_top);
  const auto draw_right = std::min(mask_bounds.x + mask_bounds.width, source_right);
  const auto draw_bottom = std::min(mask_bounds.y + mask_bounds.height, source_bottom);
  if (draw_left >= draw_right || draw_top >= draw_bottom) {
    return mask;
  }

  if (format.channels < 4) {
    for (std::int32_t y = draw_top; y < draw_bottom; ++y) {
      auto* output = mask.data() + static_cast<std::size_t>(y - mask_bounds.y) * width + (draw_left - mask_bounds.x);
      std::fill(output, output + (draw_right - draw_left), 1.0F);
    }
    return mask;
  }

  const auto* bytes = source.data().data();
  const auto stride = source.stride_bytes();
  for (std::int32_t y = draw_top; y < draw_bottom; ++y) {
    const auto sy = y + sample_offset_y - bounds.y;
    const auto* source_row = bytes + static_cast<std::size_t>(sy) * stride;
    auto* output = mask.data() + static_cast<std::size_t>(y - mask_bounds.y) * width + (draw_left - mask_bounds.x);
    for (std::int32_t x = draw_left; x < draw_right; ++x) {
      const auto sx = x + sample_offset_x - bounds.x;
      const auto* pixel = source_row + static_cast<std::size_t>(sx) * format.channels;
      *output++ = static_cast<float>(pixel[3]) / 255.0F;
    }
  }
  return mask;
}

void composite_color(PixelBuffer& destination, std::int32_t x, std::int32_t y, RgbColor color, float alpha,
                     BlendMode mode) {
  alpha = clamp_unit(alpha);
  if (alpha <= 0.0F) {
    return;
  }
  auto* dst = destination.pixel(x, y);
  const std::array<std::uint8_t, 3> src_rgb{color.red, color.green, color.blue};
  const std::array<std::uint8_t, 3> dst_rgb{dst[0], dst[1], dst[2]};
  const auto blended = blend_rgb(src_rgb, dst_rgb, mode);
  for (int channel = 0; channel < 3; ++channel) {
    dst[channel] =
        clamp_byte(static_cast<float>(blended[static_cast<std::size_t>(channel)]) * alpha +
                   static_cast<float>(dst[channel]) * (1.0F - alpha));
  }
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

float linear_gradient_position(Rect bounds, std::int32_t x, std::int32_t y, float angle_degrees) {
  constexpr float kPi = 3.14159265358979323846F;
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
      constexpr float kPi = 3.14159265358979323846F;
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

std::vector<float> dilate_mask(const std::vector<float>& input, int width, int height, int radius) {
  if (radius <= 0) {
    return input;
  }
  std::vector<std::pair<int, int>> offsets;
  offsets.reserve(static_cast<std::size_t>((radius * 2 + 1) * (radius * 2 + 1)));
  offsets.emplace_back(0, 0);
  const auto radius_squared = radius * radius;
  for (int dy = -radius; dy <= radius; ++dy) {
    for (int dx = -radius; dx <= radius; ++dx) {
      if ((dx != 0 || dy != 0) && dx * dx + dy * dy <= radius_squared) {
        offsets.emplace_back(dx, dy);
      }
    }
  }

  std::vector<float> output(input.size(), 0.0F);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      float maximum = 0.0F;
      for (const auto [dx, dy] : offsets) {
        const auto sx = x + dx;
        const auto sy = y + dy;
        if (sx >= 0 && sy >= 0 && sx < width && sy < height) {
          maximum = std::max(maximum, input[static_cast<std::size_t>(sy * width + sx)]);
          if (maximum >= 1.0F) {
            break;
          }
        }
      }
      output[static_cast<std::size_t>(y * width + x)] = maximum;
    }
  }
  return output;
}

void box_blur_mask_into(const std::vector<float>& input, std::vector<float>& horizontal, std::vector<float>& output,
                        int width, int height, int radius) {
  for (int y = 0; y < height; ++y) {
    float sum = 0.0F;
    int count = 0;
    for (int x = -radius; x <= radius; ++x) {
      if (x >= 0 && x < width) {
        sum += input[static_cast<std::size_t>(y * width + x)];
        ++count;
      }
    }
    for (int x = 0; x < width; ++x) {
      horizontal[static_cast<std::size_t>(y * width + x)] = sum / static_cast<float>(std::max(1, count));
      const auto remove_x = x - radius;
      const auto add_x = x + radius + 1;
      if (remove_x >= 0 && remove_x < width) {
        sum -= input[static_cast<std::size_t>(y * width + remove_x)];
        --count;
      }
      if (add_x >= 0 && add_x < width) {
        sum += input[static_cast<std::size_t>(y * width + add_x)];
        ++count;
      }
    }
  }

  for (int x = 0; x < width; ++x) {
    float sum = 0.0F;
    int count = 0;
    for (int y = -radius; y <= radius; ++y) {
      if (y >= 0 && y < height) {
        sum += horizontal[static_cast<std::size_t>(y * width + x)];
        ++count;
      }
    }
    for (int y = 0; y < height; ++y) {
      output[static_cast<std::size_t>(y * width + x)] = sum / static_cast<float>(std::max(1, count));
      const auto remove_y = y - radius;
      const auto add_y = y + radius + 1;
      if (remove_y >= 0 && remove_y < height) {
        sum -= horizontal[static_cast<std::size_t>(remove_y * width + x)];
        --count;
      }
      if (add_y >= 0 && add_y < height) {
        sum += horizontal[static_cast<std::size_t>(add_y * width + x)];
        ++count;
      }
    }
  }
}

void blur_mask_in_place(std::vector<float>& mask, int width, int height, int radius, int passes) {
  if (radius <= 0 || passes <= 0 || mask.empty()) {
    return;
  }
  std::vector<float> horizontal(mask.size(), 0.0F);
  std::vector<float> output(mask.size(), 0.0F);
  for (int pass = 0; pass < passes; ++pass) {
    box_blur_mask_into(mask, horizontal, output, width, height, radius);
    mask.swap(output);
  }
}

void render_drop_shadow(PixelBuffer& destination, const Layer& layer, Rect clip, Rect bounds,
                        const LayerDropShadow& shadow) {
  if (!shadow.enabled || shadow.opacity <= 0.0F) {
    return;
  }
  constexpr float kPi = 3.14159265358979323846F;
  const auto radians = (180.0F - shadow.angle_degrees) * kPi / 180.0F;
  const auto offset_x = static_cast<int>(std::lround(std::cos(radians) * shadow.distance));
  const auto offset_y = static_cast<int>(std::lround(std::sin(radians) * shadow.distance));
  const auto blur_radius = std::max(0, static_cast<int>(std::lround(shadow.size * 0.5F)));
  const auto spread_radius = std::max(0, static_cast<int>(std::lround(shadow.size * clamp_unit(shadow.spread / 100.0F))));
  const auto blur_padding = blur_radius * 3;
  const auto padding = std::abs(offset_x) + std::abs(offset_y) + blur_padding + spread_radius + 2;
  const auto effect_bounds = expand(bounds, padding);
  const auto draw_rect = intersect(clip, effect_bounds);
  if (draw_rect.empty()) {
    return;
  }

  const auto mask_bounds = clipped_mask_bounds(effect_bounds, draw_rect, blur_radius * 3 + 1);
  const auto width = mask_bounds.width;
  const auto height = mask_bounds.height;
  auto mask = layer_alpha_mask(layer, bounds, mask_bounds, -offset_x, -offset_y);
  blur_mask_in_place(mask, width, height, blur_radius, 3);

  for (std::int32_t y = draw_rect.y; y < draw_rect.y + draw_rect.height; ++y) {
    for (std::int32_t x = draw_rect.x; x < draw_rect.x + draw_rect.width; ++x) {
      const auto alpha = mask[static_cast<std::size_t>((y - mask_bounds.y) * width + (x - mask_bounds.x))] *
                         shadow.opacity * layer.opacity();
      composite_color(destination, x, y, shadow.color, alpha, shadow.blend_mode);
    }
  }
}

void render_outer_glow(PixelBuffer& destination, const Layer& layer, Rect clip, Rect bounds,
                       const LayerOuterGlow& glow) {
  if (!glow.enabled || glow.opacity <= 0.0F || glow.size <= 0.0F) {
    return;
  }
  const auto blur_radius = std::max(0, static_cast<int>(std::lround(glow.size * 0.5F)));
  const auto blur_padding = blur_radius * 3;
  const auto padding = blur_padding + 2;
  const auto effect_bounds = expand(bounds, padding);
  const auto draw_rect = intersect(clip, effect_bounds);
  if (draw_rect.empty()) {
    return;
  }

  const auto mask_bounds = clipped_mask_bounds(effect_bounds, draw_rect, blur_radius * 3 + 1);
  const auto width = mask_bounds.width;
  const auto height = mask_bounds.height;
  auto mask = layer_alpha_mask(layer, bounds, mask_bounds);
  blur_mask_in_place(mask, width, height, blur_radius, 3);
  const auto spread = std::clamp(glow.spread / std::max(1.0F, glow.size), 0.0F, 1.0F);
  const auto falloff_exponent = std::max(0.12F, 1.0F - spread * 0.88F);
  if (spread > 0.0F) {
    for (auto& alpha : mask) {
      alpha = clamp_unit(std::pow(clamp_unit(alpha), falloff_exponent));
    }
  }
  const auto source_mask = layer_alpha_mask(layer, bounds, draw_rect);
  const auto source_mask_width = draw_rect.width;

  for (std::int32_t y = draw_rect.y; y < draw_rect.y + draw_rect.height; ++y) {
    for (std::int32_t x = draw_rect.x; x < draw_rect.x + draw_rect.width; ++x) {
      const auto source_alpha =
          source_mask[static_cast<std::size_t>((y - draw_rect.y) * source_mask_width + (x - draw_rect.x))];
      const auto glow_alpha = mask[static_cast<std::size_t>((y - mask_bounds.y) * width + (x - mask_bounds.x))] *
                              (1.0F - source_alpha) * glow.opacity * layer.opacity();
      composite_color(destination, x, y, glow.color, glow_alpha, glow.blend_mode);
    }
  }
}

void render_gradient_fill(PixelBuffer& destination, const Layer& layer, Rect clip, Rect bounds,
                          const LayerGradientFill& fill) {
  if (!fill.enabled || fill.opacity <= 0.0F) {
    return;
  }
  const auto draw_rect = intersect(clip, bounds);
  if (draw_rect.empty()) {
    return;
  }
  const auto source_mask = layer_alpha_mask(layer, bounds, draw_rect);
  const auto source_mask_width = draw_rect.width;
  for (std::int32_t y = draw_rect.y; y < draw_rect.y + draw_rect.height; ++y) {
    for (std::int32_t x = draw_rect.x; x < draw_rect.x + draw_rect.width; ++x) {
      const auto source_alpha =
          source_mask[static_cast<std::size_t>((y - draw_rect.y) * source_mask_width + (x - draw_rect.x))];
      if (source_alpha <= 0.0F) {
        continue;
      }
      const auto position = gradient_position(fill.gradient, bounds, x, y);
      const auto alpha = source_alpha * fill.opacity * layer.opacity() * gradient_stop_opacity(fill.gradient, position);
      composite_color(destination, x, y, gradient_color(fill.gradient, position), alpha, fill.blend_mode);
    }
  }
}

void render_bevel_emboss(PixelBuffer& destination, const Layer& layer, Rect clip, Rect bounds,
                         const LayerBevelEmboss& bevel) {
  if (!bevel.enabled || bevel.size <= 0.0F ||
      (bevel.highlight_opacity <= 0.0F && bevel.shadow_opacity <= 0.0F)) {
    return;
  }
  const auto draw_rect = intersect(clip, bounds);
  if (draw_rect.empty()) {
    return;
  }

  constexpr float kPi = 3.14159265358979323846F;
  const auto sample_radius = std::max(1, static_cast<int>(std::lround(bevel.size)));
  const auto angle = (180.0F - bevel.angle_degrees) * kPi / 180.0F;
  const auto altitude = std::clamp(bevel.altitude_degrees, 0.0F, 90.0F) * kPi / 180.0F;
  const auto horizontal = std::cos(altitude);
  const auto light_x = -std::cos(angle) * horizontal;
  const auto light_y = -std::sin(angle) * horizontal;
  const auto normal_scale = std::clamp(bevel.depth, 0.01F, 10.0F);
  const auto direction = bevel.direction_up ? 1.0F : -1.0F;
  const auto mask_bounds = clipped_mask_bounds(expand(bounds, sample_radius), draw_rect, sample_radius);
  const auto alpha_mask = layer_alpha_mask(layer, bounds, mask_bounds);
  const auto mask_width = mask_bounds.width;
  const auto mask_alpha_at = [&alpha_mask, mask_bounds, mask_width](std::int32_t x, std::int32_t y) {
    if (x < mask_bounds.x || y < mask_bounds.y || x >= mask_bounds.x + mask_bounds.width ||
        y >= mask_bounds.y + mask_bounds.height) {
      return 0.0F;
    }
    return alpha_mask[static_cast<std::size_t>((y - mask_bounds.y) * mask_width + (x - mask_bounds.x))];
  };

  for (std::int32_t y = draw_rect.y; y < draw_rect.y + draw_rect.height; ++y) {
    for (std::int32_t x = draw_rect.x; x < draw_rect.x + draw_rect.width; ++x) {
      const auto source_alpha = mask_alpha_at(x, y);
      if (source_alpha <= 0.0F) {
        continue;
      }
      const auto left = mask_alpha_at(x - sample_radius, y);
      const auto right = mask_alpha_at(x + sample_radius, y);
      const auto top = mask_alpha_at(x, y - sample_radius);
      const auto bottom = mask_alpha_at(x, y + sample_radius);
      auto normal_x = (left - right) * normal_scale * direction;
      auto normal_y = (top - bottom) * normal_scale * direction;
      const auto length = std::sqrt(normal_x * normal_x + normal_y * normal_y + 1.0F);
      normal_x /= std::max(0.0001F, length);
      normal_y /= std::max(0.0001F, length);
      const auto lighting = normal_x * light_x + normal_y * light_y;
      if (lighting > 0.0F) {
        composite_color(destination, x, y, bevel.highlight_color,
                        clamp_unit(lighting) * source_alpha * bevel.highlight_opacity * layer.opacity(),
                        bevel.highlight_blend_mode);
      } else if (lighting < 0.0F) {
        composite_color(destination, x, y, bevel.shadow_color,
                        clamp_unit(-lighting) * source_alpha * bevel.shadow_opacity * layer.opacity(),
                        bevel.shadow_blend_mode);
      }
    }
  }
}

std::vector<float> stroke_alpha_mask(const Layer& layer, Rect bounds, Rect mask_bounds, int radius,
                                     LayerStrokePosition position) {
  auto base = layer_alpha_mask(layer, bounds, mask_bounds);
  const auto width = mask_bounds.width;
  const auto height = mask_bounds.height;
  std::vector<float> outside;
  std::vector<float> inside;
  if (position == LayerStrokePosition::Outside || position == LayerStrokePosition::Center) {
    outside = dilate_mask(base, width, height, radius);
  }
  if (position == LayerStrokePosition::Inside || position == LayerStrokePosition::Center) {
    std::vector<float> inverse(base.size(), 0.0F);
    for (std::size_t index = 0; index < base.size(); ++index) {
      inverse[index] = 1.0F - base[index];
    }
    inside = dilate_mask(inverse, width, height, radius);
  }

  std::vector<float> mask(base.size(), 0.0F);
  for (std::size_t index = 0; index < base.size(); ++index) {
    const auto center_alpha = base[index];
    const auto outside_alpha = outside.empty() ? 0.0F : outside[index];
    const auto inside_alpha = inside.empty() ? 0.0F : inside[index];
    switch (position) {
      case LayerStrokePosition::Outside:
        mask[index] = clamp_unit((1.0F - center_alpha) * outside_alpha);
        break;
      case LayerStrokePosition::Inside:
        mask[index] = clamp_unit(center_alpha * inside_alpha);
        break;
      case LayerStrokePosition::Center:
        mask[index] = clamp_unit(std::max(center_alpha * inside_alpha, (1.0F - center_alpha) * outside_alpha));
        break;
    }
  }
  return mask;
}

void render_stroke(PixelBuffer& destination, const Layer& layer, Rect clip, Rect bounds, const LayerStroke& stroke) {
  if (!stroke.enabled || stroke.opacity <= 0.0F || stroke.size <= 0.0F) {
    return;
  }
  const auto radius = std::max(1, static_cast<int>(std::ceil(stroke.size)));
  const auto full_mask_bounds = expand(bounds, radius + 1);
  const auto effect_bounds = stroke.position == LayerStrokePosition::Inside ? bounds : full_mask_bounds;
  const auto draw_rect = intersect(clip, effect_bounds);
  if (draw_rect.empty()) {
    return;
  }
  const auto mask_bounds = clipped_mask_bounds(full_mask_bounds, draw_rect, radius + 1);
  const auto mask = stroke_alpha_mask(layer, bounds, mask_bounds, radius, stroke.position);
  const auto mask_width = mask_bounds.width;
  for (std::int32_t y = draw_rect.y; y < draw_rect.y + draw_rect.height; ++y) {
    for (std::int32_t x = draw_rect.x; x < draw_rect.x + draw_rect.width; ++x) {
      const auto mask_alpha = mask[static_cast<std::size_t>((y - mask_bounds.y) * mask_width + (x - mask_bounds.x))];
      if (mask_alpha <= 0.0F) {
        continue;
      }
      auto color = stroke.color;
      auto alpha = mask_alpha * stroke.opacity * layer.opacity();
      if (stroke.uses_gradient) {
        const auto position = gradient_position(stroke.gradient, effect_bounds, x, y);
        color = gradient_color(stroke.gradient, position);
        alpha *= gradient_stop_opacity(stroke.gradient, position);
      }
      composite_color(destination, x, y, color, alpha, stroke.blend_mode);
    }
  }
}

}  // namespace

PixelBuffer Compositor::flatten_rgb8(const Document& document) const {
  PixelBuffer output(document.width(), document.height(), PixelFormat::rgb8());
  output.clear(0);

  const auto canvas = Rect::from_size(document.width(), document.height());
  for (const auto& layer : document.layers()) {
    composite_layer(output, layer, canvas);
  }
  return output;
}

void Compositor::composite_layer(PixelBuffer& destination, const Layer& layer, Rect clip) const {
  if (!layer.visible() || layer.opacity() <= 0.0F) {
    return;
  }

  if (layer.kind() == LayerKind::Group) {
    for (const auto& child : layer.children()) {
      composite_layer(destination, child, clip);
    }
    return;
  }

  composite_pixels(destination, layer, clip);
}

void Compositor::composite_pixels(PixelBuffer& destination, const Layer& layer, Rect clip) const {
  const auto& source = layer.pixels();
  if (source.empty()) {
    return;
  }
  if (destination.format() != PixelFormat::rgb8() || source.format().bit_depth != BitDepth::UInt8 ||
      source.format().channels < 3) {
    throw std::invalid_argument("The starter compositor currently supports RGB/RGBA 8-bit layers only");
  }

  const auto layer_bounds = layer_pixel_bounds(layer);
  const auto draw_rect = intersect(clip, layer_bounds);
  const auto& style = layer.layer_style();
  if (style.effects_visible) {
    for (const auto& shadow : style.drop_shadows) {
      render_drop_shadow(destination, layer, clip, layer_bounds, shadow);
    }
    for (const auto& glow : style.outer_glows) {
      render_outer_glow(destination, layer, clip, layer_bounds, glow);
    }
  }

  if (!draw_rect.empty()) {
    const auto format = source.format();
    const auto channels = format.channels;
    const auto* source_bytes = source.data().data();
    const auto source_stride = source.stride_bytes();
    for (std::int32_t y = draw_rect.y; y < draw_rect.y + draw_rect.height; ++y) {
      const auto sy = y - layer_bounds.y;
      const auto* source_row = source_bytes + static_cast<std::size_t>(sy) * source_stride;
      for (std::int32_t x = draw_rect.x; x < draw_rect.x + draw_rect.width; ++x) {
        const auto sx = x - layer_bounds.x;
        const auto* src = source_row + static_cast<std::size_t>(sx) * channels;
        const auto source_alpha = channels >= 4 ? static_cast<float>(src[3]) / 255.0F : 1.0F;
        composite_color(destination, x, y, RgbColor{src[0], src[1], src[2]}, source_alpha * layer.opacity(),
                        layer.blend_mode());
      }
    }
  }

  if (style.effects_visible) {
    for (const auto& fill : style.gradient_fills) {
      render_gradient_fill(destination, layer, clip, layer_bounds, fill);
    }
    for (const auto& bevel : style.bevels) {
      render_bevel_emboss(destination, layer, clip, layer_bounds, bevel);
    }
    for (const auto& stroke : style.strokes) {
      render_stroke(destination, layer, clip, layer_bounds, stroke);
    }
  }
}

}  // namespace photoslop
