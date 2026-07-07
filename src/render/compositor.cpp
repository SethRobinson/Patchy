#include "render/compositor.hpp"

#include "core/blend_math.hpp"
#include "render/layer_compositor.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <future>
#include <stdexcept>
#include <thread>
#include <vector>

namespace patchy {

namespace {

class Rgb8PixelBufferTarget {
public:
  // origin_x/origin_y let a strip-sized destination receive document-space
  // composite coordinates (the strip covers rows starting at origin_y).
  explicit Rgb8PixelBufferTarget(PixelBuffer& destination, float initial_alpha, std::int32_t origin_x = 0,
                                 std::int32_t origin_y = 0)
      : destination_(destination), origin_x_(origin_x), origin_y_(origin_y),
        alpha_(static_cast<std::size_t>(std::max(0, destination.width())) *
                   static_cast<std::size_t>(std::max(0, destination.height())),
               clamp_unit(initial_alpha)) {
    if (destination_.format() != PixelFormat::rgb8()) {
      throw std::invalid_argument("The starter compositor currently supports RGB8 destinations only");
    }
  }

  void composite_color(std::int32_t x, std::int32_t y, RgbColor color, float alpha, BlendMode mode) {
    alpha = clamp_unit(alpha);
    x -= origin_x_;
    y -= origin_y_;
    if (alpha <= 0.0F || x < 0 || y < 0 || x >= destination_.width() || y >= destination_.height()) {
      return;
    }

    auto* dst = destination_.pixel(x, y);
    auto& destination_alpha = alpha_[static_cast<std::size_t>(y) * static_cast<std::size_t>(destination_.width()) +
                                     static_cast<std::size_t>(x)];
    const std::array<std::uint8_t, 3> src_rgb{color.red, color.green, color.blue};
    const std::array<std::uint8_t, 3> dst_rgb{dst[0], dst[1], dst[2]};
    const auto blended = composite_blended_rgb(src_rgb, dst_rgb, mode, alpha, destination_alpha);
    for (int channel = 0; channel < 3; ++channel) {
      dst[channel] = blended[static_cast<std::size_t>(channel)];
    }
    destination_alpha = alpha + destination_alpha * (1.0F - alpha);
  }

  void adjust_color(std::int32_t x, std::int32_t y, const AdjustmentSettings& settings, float amount) {
    amount = clamp_unit(amount);
    x -= origin_x_;
    y -= origin_y_;
    if (amount <= 0.0F || x < 0 || y < 0 || x >= destination_.width() || y >= destination_.height()) {
      return;
    }

    const auto index =
        static_cast<std::size_t>(y) * static_cast<std::size_t>(destination_.width()) + static_cast<std::size_t>(x);
    if (alpha_[index] <= 0.0F) {
      return;
    }

    auto* dst = destination_.pixel(x, y);
    const auto adjusted = apply_adjustment_to_color(RgbColor{dst[0], dst[1], dst[2]}, settings);
    dst[0] = clamp_byte(static_cast<float>(adjusted.red) * amount + static_cast<float>(dst[0]) * (1.0F - amount));
    dst[1] = clamp_byte(static_cast<float>(adjusted.green) * amount + static_cast<float>(dst[1]) * (1.0F - amount));
    dst[2] = clamp_byte(static_cast<float>(adjusted.blue) * amount + static_cast<float>(dst[2]) * (1.0F - amount));
  }

  // Bit-identical to the settings variant (build_adjustment_lut).
  void adjust_color(std::int32_t x, std::int32_t y, const AdjustmentLut& lut, float amount) {
    amount = clamp_unit(amount);
    x -= origin_x_;
    y -= origin_y_;
    if (amount <= 0.0F || x < 0 || y < 0 || x >= destination_.width() || y >= destination_.height()) {
      return;
    }

    const auto index =
        static_cast<std::size_t>(y) * static_cast<std::size_t>(destination_.width()) + static_cast<std::size_t>(x);
    if (alpha_[index] <= 0.0F) {
      return;
    }

    auto* dst = destination_.pixel(x, y);
    dst[0] = clamp_byte(static_cast<float>(lut.red[dst[0]]) * amount + static_cast<float>(dst[0]) * (1.0F - amount));
    dst[1] =
        clamp_byte(static_cast<float>(lut.green[dst[1]]) * amount + static_cast<float>(dst[1]) * (1.0F - amount));
    dst[2] = clamp_byte(static_cast<float>(lut.blue[dst[2]]) * amount + static_cast<float>(dst[2]) * (1.0F - amount));
  }

private:
  PixelBuffer& destination_;
  std::int32_t origin_x_{0};
  std::int32_t origin_y_{0};
  std::vector<float> alpha_;
};

}  // namespace

PixelBuffer Compositor::flatten_rgb8(const Document& document) const {
  PixelBuffer output(document.width(), document.height(), PixelFormat::rgb8());
  output.clear(0);
  const auto canvas = Rect::from_size(document.width(), document.height());

  // Same strip parallelism as the UI renderer (see AGENTS.md "Profiling stress
  // test"): strips only read the document and write private buffers, and clip
  // compositing is equivalent to a full walk. Small flattens (every compositor
  // pixel test) keep the sequential path byte for byte, as does
  // PATCHY_RENDER_SINGLE_THREADED=1.
  const auto area = static_cast<std::int64_t>(document.width()) * static_cast<std::int64_t>(document.height());
  const auto hardware_threads = static_cast<int>(std::thread::hardware_concurrency());
  const auto strips = std::clamp(std::min(document.height() / 128, hardware_threads), 1, 16);
  const bool parallel = strips >= 2 && area >= 4'000'000 && std::getenv("PATCHY_RENDER_SINGLE_THREADED") == nullptr;
  if (parallel) {
    struct StripJob {
      Rect clip{};
      std::future<PixelBuffer> pixels;
    };
    std::vector<StripJob> jobs;
    jobs.reserve(static_cast<std::size_t>(strips));
    const auto rows_per_strip = (document.height() + strips - 1) / strips;
    for (std::int32_t start = 0; start < document.height(); start += rows_per_strip) {
      const auto rows = std::min(rows_per_strip, document.height() - start);
      const Rect strip_clip{0, start, document.width(), rows};
      jobs.push_back(StripJob{strip_clip, std::async(std::launch::async, [&document, strip_clip] {
                                PixelBuffer strip(strip_clip.width, strip_clip.height, PixelFormat::rgb8());
                                strip.clear(0);
                                Rgb8PixelBufferTarget target(strip, 0.0F, strip_clip.x, strip_clip.y);
                                for (const auto& layer : document.layers()) {
                                  render_detail::composite_layer(target, layer, strip_clip, nullptr, true);
                                }
                                return strip;
                              })});
    }
    for (auto& job : jobs) {
      const auto strip = job.pixels.get();
      const auto row_bytes = static_cast<std::size_t>(strip.width()) * 3U;
      for (std::int32_t row = 0; row < strip.height(); ++row) {
        std::memcpy(output.pixel(0, job.clip.y + row), strip.pixel(0, row), row_bytes);
      }
    }
    return output;
  }

  Rgb8PixelBufferTarget target(output, 0.0F);
  for (const auto& layer : document.layers()) {
    render_detail::composite_layer(target, layer, canvas, nullptr, true);
  }
  return output;
}

void Compositor::composite_layer(PixelBuffer& destination, const Layer& layer, Rect clip) const {
  Rgb8PixelBufferTarget target(destination, 1.0F);
  render_detail::composite_layer(target, layer, clip, nullptr, true);
}

void Compositor::composite_pixels(PixelBuffer& destination, const Layer& layer, Rect clip) const {
  Rgb8PixelBufferTarget target(destination, 1.0F);
  render_detail::composite_pixel_layer(target, layer, clip, nullptr, true);
}

}  // namespace patchy
