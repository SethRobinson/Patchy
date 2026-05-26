#include "render/compositor.hpp"

#include "core/blend_math.hpp"
#include "render/layer_compositor.hpp"

#include <array>
#include <stdexcept>

namespace photoslop {

namespace {

class Rgb8PixelBufferTarget {
public:
  explicit Rgb8PixelBufferTarget(PixelBuffer& destination) : destination_(destination) {
    if (destination_.format() != PixelFormat::rgb8()) {
      throw std::invalid_argument("The starter compositor currently supports RGB8 destinations only");
    }
  }

  void composite_color(std::int32_t x, std::int32_t y, RgbColor color, float alpha, BlendMode mode) {
    alpha = clamp_unit(alpha);
    if (alpha <= 0.0F || x < 0 || y < 0 || x >= destination_.width() || y >= destination_.height()) {
      return;
    }

    auto* dst = destination_.pixel(x, y);
    const std::array<std::uint8_t, 3> src_rgb{color.red, color.green, color.blue};
    const std::array<std::uint8_t, 3> dst_rgb{dst[0], dst[1], dst[2]};
    const auto blended = blend_rgb(src_rgb, dst_rgb, mode);
    for (int channel = 0; channel < 3; ++channel) {
      dst[channel] =
          clamp_byte(static_cast<float>(blended[static_cast<std::size_t>(channel)]) * alpha +
                     static_cast<float>(dst[channel]) * (1.0F - alpha));
    }
  }

private:
  PixelBuffer& destination_;
};

}  // namespace

PixelBuffer Compositor::flatten_rgb8(const Document& document) const {
  PixelBuffer output(document.width(), document.height(), PixelFormat::rgb8());
  output.clear(0);

  Rgb8PixelBufferTarget target(output);
  const auto canvas = Rect::from_size(document.width(), document.height());
  for (const auto& layer : document.layers()) {
    render_detail::composite_layer(target, layer, canvas, nullptr, true);
  }
  return output;
}

void Compositor::composite_layer(PixelBuffer& destination, const Layer& layer, Rect clip) const {
  Rgb8PixelBufferTarget target(destination);
  render_detail::composite_layer(target, layer, clip, nullptr, true);
}

void Compositor::composite_pixels(PixelBuffer& destination, const Layer& layer, Rect clip) const {
  Rgb8PixelBufferTarget target(destination);
  render_detail::composite_pixel_layer(target, layer, clip, nullptr, true);
}

}  // namespace photoslop
