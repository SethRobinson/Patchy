#pragma once

#include "render/layer_compositor.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>

// The PixelBuffer compositor targets the flatten, PSD-merge and BMP writers render
// through. Each is instantiated once (layer_compositor_rgb8.cpp,
// layer_compositor_rgba8.cpp, layer_compositor_rgb8_per_pixel.cpp); see the
// PATCHY_COMPOSITOR_TARGET_INSTANTIATIONS note in layer_compositor.hpp. Every byte these
// targets write is pinned (psd_layered_writer_bytes_are_stable, the composite corpus
// flatten digests, the BMP round-trip tests): keep the bodies verbatim.
namespace patchy {

// RGB8 destination with a float coverage plane: the PSD merged image (Compositor::flatten_rgb8)
// and its strips. Exposes composite_blended_row, so gate-free layers take the row kernel
// (docs/performance.md "Float row kernels").
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
      throw std::invalid_argument(PATCHY_TRANSLATE_NOOP("QObject", "The starter compositor currently supports RGB8 destinations only"));
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

// Straight-alpha RGBA8 destination, stateless: document_flatten and the BMP RGBA writer.
// `origin`: the document point the destination's (0, 0) stands for (a rect flatten).
class Rgba8PixelBufferTarget {
public:
  // `origin`: the document point the destination's (0, 0) stands for (a rect flatten).
  explicit Rgba8PixelBufferTarget(PixelBuffer& destination, std::int32_t origin_x = 0, std::int32_t origin_y = 0)
      : destination_(destination), origin_x_(origin_x), origin_y_(origin_y) {}

  void composite_color(std::int32_t x, std::int32_t y, RgbColor color, float alpha, BlendMode mode) {
    x -= origin_x_;
    y -= origin_y_;
    alpha = clamp_unit(alpha);
    if (alpha <= 0.0F || x < 0 || y < 0 || x >= destination_.width() || y >= destination_.height()) {
      return;
    }
    auto* dst = destination_.pixel(x, y);
    const auto destination_alpha = static_cast<float>(dst[3]) / 255.0F;
    const std::array<std::uint8_t, 3> source_rgb{color.red, color.green, color.blue};
    const std::array<std::uint8_t, 3> destination_rgb{dst[0], dst[1], dst[2]};
    const auto blended = composite_blended_rgb(source_rgb, destination_rgb, mode, alpha, destination_alpha);
    dst[0] = blended[0];
    dst[1] = blended[1];
    dst[2] = blended[2];
    dst[3] = clamp_byte((alpha + destination_alpha * (1.0F - alpha)) * 255.0F);
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
    const auto result = composite_special_fill_rgb(
        {color.red, color.green, color.blue}, {dst[0], dst[1], dst[2]}, mode, source_coverage,
        fill_opacity, layer_opacity, static_cast<float>(dst[3]) / 255.0F);
    dst[0] = result.color[0]; dst[1] = result.color[1]; dst[2] = result.color[2];
    dst[3] = clamp_byte(result.alpha * 255.0F);
  }

  [[nodiscard]] render_detail::CompositeSample sample_color(std::int32_t x, std::int32_t y) const noexcept {
    x -= origin_x_;
    y -= origin_y_;
    if (x < 0 || y < 0 || x >= destination_.width() || y >= destination_.height()) {
      return {};
    }
    const auto* pixel = destination_.pixel(x, y);
    return render_detail::CompositeSample{RgbColor{pixel[0], pixel[1], pixel[2]},
                                          static_cast<float>(pixel[3]) / 255.0F};
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
    dst[3] = clamp_byte(clamp_unit(alpha) * 255.0F);
  }

  void adjust_color(std::int32_t x, std::int32_t y, const AdjustmentSettings& settings, float amount) {
    x -= origin_x_;
    y -= origin_y_;
    amount = clamp_unit(amount);
    if (amount <= 0.0F || x < 0 || y < 0 || x >= destination_.width() || y >= destination_.height()) {
      return;
    }
    auto* dst = destination_.pixel(x, y);
    if (dst[3] == 0) {
      return;
    }
    const auto adjusted = apply_adjustment_to_color(RgbColor{dst[0], dst[1], dst[2]}, settings);
    dst[0] = clamp_byte(static_cast<float>(adjusted.red) * amount + static_cast<float>(dst[0]) * (1.0F - amount));
    dst[1] = clamp_byte(static_cast<float>(adjusted.green) * amount + static_cast<float>(dst[1]) * (1.0F - amount));
    dst[2] = clamp_byte(static_cast<float>(adjusted.blue) * amount + static_cast<float>(dst[2]) * (1.0F - amount));
  }

  void adjust_color(std::int32_t x, std::int32_t y, const AdjustmentLut& lut, float amount) {
    x -= origin_x_;
    y -= origin_y_;
    amount = clamp_unit(amount);
    if (amount <= 0.0F || x < 0 || y < 0 || x >= destination_.width() || y >= destination_.height()) {
      return;
    }
    auto* dst = destination_.pixel(x, y);
    if (dst[3] == 0) {
      return;
    }
    dst[0] = clamp_byte(static_cast<float>(lut.red[dst[0]]) * amount + static_cast<float>(dst[0]) * (1.0F - amount));
    dst[1] =
        clamp_byte(static_cast<float>(lut.green[dst[1]]) * amount + static_cast<float>(dst[1]) * (1.0F - amount));
    dst[2] = clamp_byte(static_cast<float>(lut.blue[dst[2]]) * amount + static_cast<float>(dst[2]) * (1.0F - amount));
  }

private:
  PixelBuffer& destination_;
  std::int32_t origin_x_{0};
  std::int32_t origin_y_{0};
};

// The BMP RGB writer's target: Rgb8PixelBufferTarget without the composite_blended_row
// kernel (and without an origin), so BMP export keeps every layer on the general
// per-pixel loop it was pinned on. The two are method-for-method identical otherwise;
// docs/refactor-backlog.md records what a merge needs.
class Rgb8PerPixelTarget {
public:
  explicit Rgb8PerPixelTarget(PixelBuffer& destination, float initial_alpha)
      : destination_(destination),
        alpha_(static_cast<std::size_t>(destination.width()) * static_cast<std::size_t>(destination.height()),
               clamp_unit(initial_alpha)) {
    if (destination_.format() != PixelFormat::rgb8()) {
      throw std::invalid_argument(PATCHY_TRANSLATE_NOOP("QObject", "BMP RGB render target requires RGB8 pixels"));
    }
  }

  void composite_color(std::int32_t x, std::int32_t y, RgbColor color, float alpha, BlendMode mode) {
    alpha = clamp_unit(alpha);
    if (alpha <= 0.0F || x < 0 || y < 0 || x >= destination_.width() || y >= destination_.height()) {
      return;
    }

    auto* dst = destination_.pixel(x, y);
    auto& destination_alpha =
        alpha_[static_cast<std::size_t>(y) * static_cast<std::size_t>(destination_.width()) +
               static_cast<std::size_t>(x)];
    const std::array<std::uint8_t, 3> source_rgb{color.red, color.green, color.blue};
    const std::array<std::uint8_t, 3> destination_rgb{dst[0], dst[1], dst[2]};
    const auto blended = composite_blended_rgb(source_rgb, destination_rgb, mode, alpha, destination_alpha);
    dst[0] = blended[0];
    dst[1] = blended[1];
    dst[2] = blended[2];
    destination_alpha = alpha + destination_alpha * (1.0F - alpha);
  }

  void composite_special_fill_color(std::int32_t x, std::int32_t y, RgbColor color,
                                    float source_coverage, float fill_opacity, float layer_opacity,
                                    BlendMode mode) {
    if (source_coverage <= 0.0F || fill_opacity <= 0.0F || layer_opacity <= 0.0F || x < 0 || y < 0 ||
        x >= destination_.width() || y >= destination_.height()) return;
    auto* dst = destination_.pixel(x, y);
    auto& destination_alpha = alpha_[static_cast<std::size_t>(y) * static_cast<std::size_t>(destination_.width()) +
                                     static_cast<std::size_t>(x)];
    const auto result = composite_special_fill_rgb(
        {color.red, color.green, color.blue}, {dst[0], dst[1], dst[2]}, mode, source_coverage,
        fill_opacity, layer_opacity, destination_alpha);
    dst[0] = result.color[0]; dst[1] = result.color[1]; dst[2] = result.color[2];
    destination_alpha = result.alpha;
  }

  [[nodiscard]] render_detail::CompositeSample sample_color(std::int32_t x, std::int32_t y) const noexcept {
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

  void adjust_color(std::int32_t x, std::int32_t y, const AdjustmentLut& lut, float amount) {
    amount = clamp_unit(amount);
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
  std::vector<float> alpha_;
};

}  // namespace patchy

PATCHY_COMPOSITOR_TARGET_INSTANTIATIONS(extern, patchy::Rgb8PixelBufferTarget)
PATCHY_COMPOSITOR_TARGET_INSTANTIATIONS(extern, patchy::Rgba8PixelBufferTarget)
PATCHY_COMPOSITOR_TARGET_INSTANTIATIONS(extern, patchy::Rgb8PerPixelTarget)
