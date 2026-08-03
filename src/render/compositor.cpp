#include "render/compositor.hpp"

#include "core/blend_math.hpp"
#include "core/environment.hpp"
#include "core/worker_budget.hpp"
#include "render/layer_compositor.hpp"

#include <algorithm>
#include <array>
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

  // Blended row: float-exact transcription of composite_color above (the
  // general loop's only remaining work for a layer with no per-pixel gates
  // beyond coverage), with the bounds-validated pixel() lookup hoisted to one
  // per row. Same contract as the QImageCompositeTarget kernel: the mode
  // dispatch goes through the real blend_rgb, the mix keeps
  // composite_blended_rgb's exact expression tree (blend reads the CLAMPED
  // destination alpha, the plane update reads the stored value), and the only
  // deviation is skipping the division at an output alpha of exactly 1.0F,
  // which IEEE division makes byte-neutral. This feeds the PSD writer's merged
  // image, so psd_layered_writer_bytes_are_stable pins it directly. No integer
  // math: the integer and float paths round differently by design.
  void composite_blended_row(std::int32_t x, std::int32_t y, const std::uint8_t* source_row, const float* mask_row,
                             std::int32_t width, std::uint16_t channels, float opacity, BlendMode mode) {
    if (source_row == nullptr || width <= 0 || channels < 3) {
      return;
    }

    auto local_x = x - origin_x_;
    const auto local_y = y - origin_y_;
    if (local_y < 0 || local_y >= destination_.height() || local_x >= destination_.width()) {
      return;
    }
    if (local_x < 0) {
      const auto skip = -local_x;
      if (skip >= width) {
        return;
      }
      source_row += static_cast<std::size_t>(skip) * channels;
      if (mask_row != nullptr) {
        mask_row += skip;
      }
      width -= skip;
      local_x = 0;
    }
    width = std::min(width, destination_.width() - local_x);
    if (width <= 0) {
      return;
    }

    auto* dst = destination_.pixel(local_x, local_y);
    auto index = static_cast<std::size_t>(local_y) * static_cast<std::size_t>(destination_.width()) +
                 static_cast<std::size_t>(local_x);
    for (std::int32_t offset = 0; offset < width; ++offset, dst += 3U, ++index) {
      const auto* src = source_row + static_cast<std::size_t>(offset) * channels;
      const auto source_alpha = channels >= 4 ? static_cast<float>(src[3]) / 255.0F : 1.0F;
      auto alpha = mask_row != nullptr ? source_alpha * mask_row[offset] * opacity : source_alpha * opacity;
      if (alpha <= 0.0F) {
        continue;
      }
      alpha = clamp_unit(alpha);
      auto& destination_alpha = alpha_[index];
      const std::array<std::uint8_t, 3> src_rgb{src[0], src[1], src[2]};
      const std::array<std::uint8_t, 3> dst_rgb{dst[0], dst[1], dst[2]};
      const auto da = clamp_unit(destination_alpha);
      const auto output_alpha = alpha + da * (1.0F - alpha);
      if (output_alpha <= 0.0F) {
        // Unreachable with alpha > 0; kept so the replica stays complete
        // (composite_blended_rgb returns black here).
        dst[0] = 0;
        dst[1] = 0;
        dst[2] = 0;
      } else {
        const auto blended = blend_rgb(src_rgb, dst_rgb, mode);
        const bool unit_output = output_alpha == 1.0F;
        for (std::size_t channel = 0; channel < 3U; ++channel) {
          const auto source_value = static_cast<float>(src_rgb[channel]);
          const auto destination_value = static_cast<float>(dst_rgb[channel]);
          const auto blended_value = static_cast<float>(blended[channel]);
          const auto numerator = source_value * alpha * (1.0F - da) + blended_value * alpha * da +
                                 destination_value * da * (1.0F - alpha);
          dst[channel] = clamp_byte(unit_output ? numerator : numerator / output_alpha);
        }
      }
      destination_alpha = alpha + destination_alpha * (1.0F - alpha);
    }
  }

  void composite_special_fill_color(std::int32_t x, std::int32_t y, RgbColor color,
                                    float source_coverage, float fill_opacity, float layer_opacity,
                                    BlendMode mode) {
    x -= origin_x_;
    y -= origin_y_;
    if (source_coverage <= 0.0F || fill_opacity <= 0.0F || layer_opacity <= 0.0F || x < 0 || y < 0 ||
        x >= destination_.width() || y >= destination_.height()) {
      return;
    }
    auto* dst = destination_.pixel(x, y);
    auto& destination_alpha = alpha_[static_cast<std::size_t>(y) * static_cast<std::size_t>(destination_.width()) +
                                     static_cast<std::size_t>(x)];
    const auto result = composite_special_fill_rgb(
        {color.red, color.green, color.blue}, {dst[0], dst[1], dst[2]}, mode, source_coverage,
        fill_opacity, layer_opacity, destination_alpha);
    dst[0] = result.color[0];
    dst[1] = result.color[1];
    dst[2] = result.color[2];
    destination_alpha = result.alpha;
  }

  [[nodiscard]] render_detail::CompositeSample sample_color(std::int32_t x, std::int32_t y) const noexcept {
    x -= origin_x_;
    y -= origin_y_;
    if (x < 0 || y < 0 || x >= destination_.width() || y >= destination_.height()) {
      return {};
    }
    const auto index =
        static_cast<std::size_t>(y) * static_cast<std::size_t>(destination_.width()) + static_cast<std::size_t>(x);
    const auto* pixel = destination_.pixel(x, y);
    return render_detail::CompositeSample{RgbColor{pixel[0], pixel[1], pixel[2]}, alpha_[index]};
  }

  // Direct overwrite for render_detail::fade_toward_snapshot (pass-through
  // group opacity); source-over cannot reduce coverage.
  void store_color(std::int32_t x, std::int32_t y, RgbColor color, float alpha) {
    x -= origin_x_;
    y -= origin_y_;
    if (x < 0 || y < 0 || x >= destination_.width() || y >= destination_.height()) {
      return;
    }
    auto* dst = destination_.pixel(x, y);
    dst[0] = color.red;
    dst[1] = color.green;
    dst[2] = color.blue;
    alpha_[static_cast<std::size_t>(y) * static_cast<std::size_t>(destination_.width()) +
           static_cast<std::size_t>(x)] = clamp_unit(alpha);
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

  // The accumulated coverage plane, quantized for callers that persist it (the PSD
  // writer's merged "Transparency" channel).
  [[nodiscard]] std::vector<std::uint8_t> alpha_bytes() const {
    std::vector<std::uint8_t> bytes(alpha_.size());
    for (std::size_t i = 0; i < alpha_.size(); ++i) {
      bytes[i] = clamp_byte(alpha_[i] * 255.0F);
    }
    return bytes;
  }

private:
  PixelBuffer& destination_;
  std::int32_t origin_x_{0};
  std::int32_t origin_y_{0};
  std::vector<float> alpha_;
};

}  // namespace

PixelBuffer Compositor::flatten_rgb8(const Document& document, std::vector<std::uint8_t>* merged_alpha) const {
  PixelBuffer output(document.width(), document.height(), PixelFormat::rgb8());
  output.clear(0);
  const auto canvas = Rect::from_size(document.width(), document.height());
  if (merged_alpha != nullptr) {
    merged_alpha->assign(static_cast<std::size_t>(std::max(0, document.width())) *
                             static_cast<std::size_t>(std::max(0, document.height())),
                         0);
  }

  // Same strip parallelism as the UI renderer (see docs/performance.md "Parallel
  // strip rendering"): strips only read the document and write private buffers, and clip
  // compositing is equivalent to a full walk. Small flattens (every compositor
  // pixel test) keep the sequential path byte for byte, as does
  // PATCHY_RENDER_SINGLE_THREADED=1.
  const auto area = static_cast<std::int64_t>(document.width()) * static_cast<std::int64_t>(document.height());
  const auto hardware_threads = static_cast<int>(std::thread::hardware_concurrency());
  // max_blocking_fanout_workers: this thread blocks on the strip joins below,
  // so on the wasm main thread the fan-out must fit the idle pthread pool or
  // it deadlocks the tab; fewer strips (or the sequential path) is the
  // correct degradation and the bytes are identical for any strip count.
  const auto strips = max_blocking_fanout_workers(
      std::clamp(std::min(document.height() / 128, hardware_threads), 1, 16));
  const bool parallel =
      strips >= 2 && area >= 4'000'000 && !environment_variable_is_set("PATCHY_RENDER_SINGLE_THREADED");
  if (parallel) {
    struct StripResult {
      PixelBuffer pixels;
      std::vector<std::uint8_t> alpha;
    };
    struct StripJob {
      Rect clip{};
      std::future<StripResult> result;
    };
    const bool want_alpha = merged_alpha != nullptr;
    std::vector<StripJob> jobs;
    jobs.reserve(static_cast<std::size_t>(strips));
    const auto rows_per_strip = (document.height() + strips - 1) / strips;
    for (std::int32_t start = 0; start < document.height(); start += rows_per_strip) {
      const auto rows = std::min(rows_per_strip, document.height() - start);
      const Rect strip_clip{0, start, document.width(), rows};
      jobs.push_back(StripJob{strip_clip, std::async(std::launch::async, [&document, strip_clip, want_alpha] {
                                PixelBuffer strip(strip_clip.width, strip_clip.height, PixelFormat::rgb8());
                                strip.clear(0);
                                Rgb8PixelBufferTarget target(strip, 0.0F, strip_clip.x, strip_clip.y);
                                render_detail::composite_layers(target, document.layers(), strip_clip, nullptr,
                                                                true, nullptr,
                                                                &document.metadata().patterns);
                                return StripResult{std::move(strip),
                                                   want_alpha ? target.alpha_bytes() : std::vector<std::uint8_t>{}};
                              })});
    }
    for (auto& job : jobs) {
      const auto strip = job.result.get();
      const auto row_bytes = static_cast<std::size_t>(strip.pixels.width()) * 3U;
      for (std::int32_t row = 0; row < strip.pixels.height(); ++row) {
        std::memcpy(output.pixel(0, job.clip.y + row), strip.pixels.pixel(0, row), row_bytes);
      }
      if (merged_alpha != nullptr) {
        std::memcpy(merged_alpha->data() +
                        static_cast<std::size_t>(job.clip.y) * static_cast<std::size_t>(document.width()),
                    strip.alpha.data(), strip.alpha.size());
      }
    }
    return output;
  }

  Rgb8PixelBufferTarget target(output, 0.0F);
  render_detail::composite_layers(target, document.layers(), canvas, nullptr, true, nullptr,
                                  &document.metadata().patterns);
  if (merged_alpha != nullptr) {
    *merged_alpha = target.alpha_bytes();
  }
  return output;
}

}  // namespace patchy
