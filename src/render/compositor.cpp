#include "render/compositor.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace photoslop {

namespace {

std::uint8_t clamp_byte(float value) {
  return static_cast<std::uint8_t>(std::clamp(std::lround(value), 0L, 255L));
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
    case BlendMode::Difference:
      return static_cast<std::uint8_t>(std::abs(static_cast<int>(dst) - static_cast<int>(src)));
  }
  return src;
}

Rect intersect(Rect a, Rect b) {
  const auto left = std::max(a.x, b.x);
  const auto top = std::max(a.y, b.y);
  const auto right = std::min(a.x + a.width, b.x + b.width);
  const auto bottom = std::min(a.y + a.height, b.y + b.height);
  return Rect{left, top, std::max(0, right - left), std::max(0, bottom - top)};
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

  const auto layer_bounds = layer.bounds().empty()
                                ? Rect::from_size(source.width(), source.height())
                                : layer.bounds();
  const auto draw_rect = intersect(clip, layer_bounds);
  if (draw_rect.empty()) {
    return;
  }

  for (std::int32_t y = draw_rect.y; y < draw_rect.y + draw_rect.height; ++y) {
    for (std::int32_t x = draw_rect.x; x < draw_rect.x + draw_rect.width; ++x) {
      const auto sx = x - layer_bounds.x;
      const auto sy = y - layer_bounds.y;
      if (sx < 0 || sy < 0 || sx >= source.width() || sy >= source.height()) {
        continue;
      }

      const auto* src = source.pixel(sx, sy);
      auto* dst = destination.pixel(x, y);
      const auto source_alpha = source.format().channels >= 4 ? static_cast<float>(src[3]) / 255.0F : 1.0F;
      const auto alpha = source_alpha * layer.opacity();

      for (int channel = 0; channel < 3; ++channel) {
        const auto blended = blend_channel(src[channel], dst[channel], layer.blend_mode());
        dst[channel] = clamp_byte(static_cast<float>(blended) * alpha + static_cast<float>(dst[channel]) * (1.0F - alpha));
      }
    }
  }
}

}  // namespace photoslop
