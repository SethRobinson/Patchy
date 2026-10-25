#pragma once

#include "render/layer_compositor.hpp"

#include <QImage>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>

namespace patchy::ui {

// The QImage compositor target behind every document render in the UI (RGB888 or
// RGBA8888 destination, document-space coordinates through an origin). Its compositor
// entry points are instantiated once, in layer_compositor_qimage.cpp (see
// PATCHY_COMPOSITOR_TARGET_INSTANTIATIONS in render/layer_compositor.hpp); its bytes are
// pinned by the composite corpus render digests and the compositor canaries.
class QImageCompositeTarget {
public:
  QImageCompositeTarget(QImage& destination, bool preserve_alpha, std::int32_t origin_x, std::int32_t origin_y)
      : destination_(destination), preserve_alpha_(preserve_alpha), origin_x_(origin_x), origin_y_(origin_y) {}

  void set_profile_layer_path(std::string path) {
    profile_layer_path_ = std::move(path);
  }

  void composite_color(std::int32_t x, std::int32_t y, RgbColor color, float alpha, BlendMode mode) {
    alpha = clamp_unit(alpha);
    const auto image_x = x - origin_x_;
    const auto image_y = y - origin_y_;
    if (alpha <= 0.0F || image_x < 0 || image_y < 0 || image_x >= destination_.width() ||
        image_y >= destination_.height()) {
      return;
    }

    auto* dst = destination_.scanLine(image_y) + static_cast<std::size_t>(image_x) * (preserve_alpha_ ? 4U : 3U);
    const auto da = preserve_alpha_ ? static_cast<float>(dst[3]) / 255.0F : 1.0F;
    const auto out_a = preserve_alpha_ ? (alpha + da * (1.0F - alpha)) : 1.0F;
    const std::array<std::uint8_t, 3> src = {color.red, color.green, color.blue};
    const std::array<std::uint8_t, 3> dst_rgb = {dst[0], dst[1], dst[2]};
    const auto blended = composite_blended_rgb(src, dst_rgb, mode, alpha, da);
    dst[0] = blended[0];
    dst[1] = blended[1];
    dst[2] = blended[2];
    if (preserve_alpha_) {
      dst[3] = clamp_byte(out_a * 255.0F);
    }
  }

  void composite_special_fill_color(std::int32_t x, std::int32_t y, RgbColor color,
                                    float source_coverage, float fill_opacity, float layer_opacity,
                                    BlendMode mode) {
    const auto image_x = x - origin_x_;
    const auto image_y = y - origin_y_;
    if (source_coverage <= 0.0F || fill_opacity <= 0.0F || layer_opacity <= 0.0F || image_x < 0 ||
        image_y < 0 || image_x >= destination_.width() || image_y >= destination_.height()) {
      return;
    }
    auto* dst = destination_.scanLine(image_y) + static_cast<std::size_t>(image_x) * (preserve_alpha_ ? 4U : 3U);
    const auto destination_alpha = preserve_alpha_ ? static_cast<float>(dst[3]) / 255.0F : 1.0F;
    const auto result = composite_special_fill_rgb(
        {color.red, color.green, color.blue}, {dst[0], dst[1], dst[2]}, mode, source_coverage,
        fill_opacity, layer_opacity, destination_alpha);
    dst[0] = result.color[0];
    dst[1] = result.color[1];
    dst[2] = result.color[2];
    if (preserve_alpha_) {
      dst[3] = clamp_byte(result.alpha * 255.0F);
    }
  }

  [[nodiscard]] render_detail::CompositeSample sample_color(std::int32_t x, std::int32_t y) const noexcept {
    const auto image_x = x - origin_x_;
    const auto image_y = y - origin_y_;
    if (image_x < 0 || image_y < 0 || image_x >= destination_.width() || image_y >= destination_.height()) {
      return {};
    }
    const auto* pixel =
        destination_.constScanLine(image_y) + static_cast<std::size_t>(image_x) * (preserve_alpha_ ? 4U : 3U);
    return render_detail::CompositeSample{RgbColor{pixel[0], pixel[1], pixel[2]},
                                          preserve_alpha_ ? static_cast<float>(pixel[3]) / 255.0F : 1.0F};
  }

  // Direct overwrite for render_detail::fade_toward_snapshot (pass-through
  // group opacity); source-over cannot reduce coverage.
  void store_color(std::int32_t x, std::int32_t y, RgbColor color, float alpha) {
    const auto image_x = x - origin_x_;
    const auto image_y = y - origin_y_;
    if (image_x < 0 || image_y < 0 || image_x >= destination_.width() || image_y >= destination_.height()) {
      return;
    }
    auto* dst = destination_.scanLine(image_y) + static_cast<std::size_t>(image_x) * (preserve_alpha_ ? 4U : 3U);
    dst[0] = color.red;
    dst[1] = color.green;
    dst[2] = color.blue;
    if (preserve_alpha_) {
      dst[3] = clamp_byte(clamp_unit(alpha) * 255.0F);
    }
  }

  void composite_source_row(std::int32_t x, std::int32_t y, const std::uint8_t* source_row, std::int32_t width,
                            std::uint16_t channels, float opacity) {
    if (source_row == nullptr || width <= 0 || channels < 3) {
      return;
    }

    auto image_x = x - origin_x_;
    const auto image_y = y - origin_y_;
    if (image_y < 0 || image_y >= destination_.height() || image_x >= destination_.width()) {
      return;
    }
    if (image_x < 0) {
      const auto skip = -image_x;
      if (skip >= width) {
        return;
      }
      source_row += static_cast<std::size_t>(skip) * channels;
      width -= skip;
      image_x = 0;
    }
    width = std::min(width, destination_.width() - image_x);
    if (width <= 0) {
      return;
    }

    const auto opacity_alpha = std::clamp(static_cast<int>(std::lround(clamp_unit(opacity) * 255.0F)), 0, 255);
    if (opacity_alpha <= 0) {
      return;
    }

    auto* dst = destination_.scanLine(image_y) + static_cast<std::size_t>(image_x) * (preserve_alpha_ ? 4U : 3U);
    for (std::int32_t index = 0; index < width; ++index) {
      const auto* src = source_row + static_cast<std::size_t>(index) * channels;
      auto source_alpha = channels >= 4 ? static_cast<int>(src[3]) : 255;
      source_alpha = (source_alpha * opacity_alpha + 127) / 255;
      if (source_alpha <= 0) {
        dst += preserve_alpha_ ? 4U : 3U;
        continue;
      }

      if (!preserve_alpha_) {
        if (source_alpha >= 255) {
          dst[0] = src[0];
          dst[1] = src[1];
          dst[2] = src[2];
        } else {
          dst[0] = static_cast<std::uint8_t>((static_cast<int>(src[0]) * source_alpha +
                                              static_cast<int>(dst[0]) * (255 - source_alpha) + 127) /
                                             255);
          dst[1] = static_cast<std::uint8_t>((static_cast<int>(src[1]) * source_alpha +
                                              static_cast<int>(dst[1]) * (255 - source_alpha) + 127) /
                                             255);
          dst[2] = static_cast<std::uint8_t>((static_cast<int>(src[2]) * source_alpha +
                                              static_cast<int>(dst[2]) * (255 - source_alpha) + 127) /
                                             255);
        }
        dst += 3U;
        continue;
      }

      const auto destination_alpha = static_cast<int>(dst[3]);
      const auto output_alpha = (source_alpha * 255 + destination_alpha * (255 - source_alpha) + 127) / 255;
      if (source_alpha >= 255) {
        dst[0] = src[0];
        dst[1] = src[1];
        dst[2] = src[2];
        dst[3] = 255;
      } else if (destination_alpha <= 0) {
        dst[0] = src[0];
        dst[1] = src[1];
        dst[2] = src[2];
        dst[3] = static_cast<std::uint8_t>(source_alpha);
      } else if (output_alpha > 0) {
        const auto denominator = output_alpha * 255;
        const auto inverse_source_alpha = 255 - source_alpha;
        for (int channel = 0; channel < 3; ++channel) {
          const auto numerator = static_cast<int>(src[channel]) * source_alpha * 255 +
                                 static_cast<int>(dst[channel]) * destination_alpha * inverse_source_alpha;
          dst[channel] = static_cast<std::uint8_t>((numerator + denominator / 2) / denominator);
        }
        dst[3] = static_cast<std::uint8_t>(output_alpha);
      }
      dst += 4U;
    }
  }

  // Blended row: the general per-pixel loop's semantics for a layer with no
  // per-pixel gates beyond coverage (no blend-if, satin, folded overlays,
  // knockout, special fill, or backdrop pre-blend) hoisted to a row walk.
  // mask_row is the precomputed mask-coverage plane row, or nullptr for
  // unmasked layers. The arithmetic is a verbatim replica of composite_color +
  // composite_blended_rgb - the mode dispatch goes through the real blend_rgb,
  // and the float mix keeps its exact expression tree - so output bytes match
  // the per-pixel path exactly. The one addition: a division by an output
  // alpha of exactly 1.0F is skipped, which IEEE division makes byte-neutral.
  // Do NOT reuse composite_source_row's integer math here: the two paths
  // round differently by design.
  void composite_blended_row(std::int32_t x, std::int32_t y, const std::uint8_t* source_row, const float* mask_row,
                             std::int32_t width, std::uint16_t channels, float opacity, BlendMode mode) {
    if (source_row == nullptr || width <= 0 || channels < 3) {
      return;
    }

    auto image_x = x - origin_x_;
    const auto image_y = y - origin_y_;
    if (image_y < 0 || image_y >= destination_.height() || image_x >= destination_.width()) {
      return;
    }
    if (image_x < 0) {
      const auto skip = -image_x;
      if (skip >= width) {
        return;
      }
      source_row += static_cast<std::size_t>(skip) * channels;
      if (mask_row != nullptr) {
        mask_row += skip;
      }
      width -= skip;
      image_x = 0;
    }
    width = std::min(width, destination_.width() - image_x);
    if (width <= 0) {
      return;
    }

    const std::size_t step = preserve_alpha_ ? 4U : 3U;
    auto* dst = destination_.scanLine(image_y) + static_cast<std::size_t>(image_x) * step;
    for (std::int32_t index = 0; index < width; ++index, dst += step) {
      const auto* src = source_row + static_cast<std::size_t>(index) * channels;
      const auto source_alpha = channels >= 4 ? static_cast<float>(src[3]) / 255.0F : 1.0F;
      auto alpha = mask_row != nullptr ? source_alpha * mask_row[index] * opacity : source_alpha * opacity;
      if (alpha <= 0.0F) {
        continue;
      }
      alpha = clamp_unit(alpha);
      const auto da = preserve_alpha_ ? static_cast<float>(dst[3]) / 255.0F : 1.0F;
      // composite_blended_rgb's output_alpha; composite_color's out_a is the
      // same expression, so one value serves both the mix and the alpha write.
      const auto output_alpha = alpha + da * (1.0F - alpha);
      if (output_alpha <= 0.0F) {
        // Unreachable with alpha > 0; kept so the replica stays complete.
        dst[0] = 0;
        dst[1] = 0;
        dst[2] = 0;
        if (preserve_alpha_) {
          dst[3] = clamp_byte(output_alpha * 255.0F);
        }
        continue;
      }
      const std::array<std::uint8_t, 3> src_rgb = {src[0], src[1], src[2]};
      const std::array<std::uint8_t, 3> dst_rgb = {dst[0], dst[1], dst[2]};
      const auto blended = blend_rgb(src_rgb, dst_rgb, mode);
      const bool unit_output = output_alpha == 1.0F;
      for (std::size_t channel = 0; channel < 3; ++channel) {
        const auto source_value = static_cast<float>(src_rgb[channel]);
        const auto destination_value = static_cast<float>(dst_rgb[channel]);
        const auto blended_value = static_cast<float>(blended[channel]);
        const auto numerator = source_value * alpha * (1.0F - da) + blended_value * alpha * da +
                               destination_value * da * (1.0F - alpha);
        dst[channel] = clamp_byte(unit_output ? numerator : numerator / output_alpha);
      }
      if (preserve_alpha_) {
        dst[3] = clamp_byte(output_alpha * 255.0F);
      }
    }
  }

  void composite_image(const QImage& image, Rect bounds, Rect clip) {
    if (image.isNull() || image.format() != QImage::Format_RGBA8888) {
      return;
    }
    const auto draw_rect = intersect_rect(clip, bounds);
    if (draw_rect.empty()) {
      return;
    }
    for (std::int32_t y = draw_rect.y; y < draw_rect.y + draw_rect.height; ++y) {
      const auto source_y = y - bounds.y;
      const auto source_x = draw_rect.x - bounds.x;
      const auto* row = image.constScanLine(source_y) + static_cast<std::size_t>(source_x) * 4U;
      composite_source_row(draw_rect.x, y, row, draw_rect.width, 4, 1.0F);
    }
  }

  void adjust_color(std::int32_t x, std::int32_t y, const AdjustmentSettings& settings, float amount) {
    amount = clamp_unit(amount);
    const auto image_x = x - origin_x_;
    const auto image_y = y - origin_y_;
    if (amount <= 0.0F || image_x < 0 || image_y < 0 || image_x >= destination_.width() ||
        image_y >= destination_.height()) {
      return;
    }

    auto* dst = destination_.scanLine(image_y) + static_cast<std::size_t>(image_x) * (preserve_alpha_ ? 4U : 3U);
    if (preserve_alpha_ && dst[3] == 0) {
      return;
    }
    const auto adjusted = apply_adjustment_to_color(RgbColor{dst[0], dst[1], dst[2]}, settings);
    dst[0] = clamp_byte(static_cast<float>(adjusted.red) * amount + static_cast<float>(dst[0]) * (1.0F - amount));
    dst[1] = clamp_byte(static_cast<float>(adjusted.green) * amount + static_cast<float>(dst[1]) * (1.0F - amount));
    dst[2] = clamp_byte(static_cast<float>(adjusted.blue) * amount + static_cast<float>(dst[2]) * (1.0F - amount));
  }

  // Identical blend to the settings variant; the LUT already IS the adjusted
  // per-channel value, so the two paths are bit-identical (build_adjustment_lut).
  void adjust_color(std::int32_t x, std::int32_t y, const AdjustmentLut& lut, float amount) {
    amount = clamp_unit(amount);
    const auto image_x = x - origin_x_;
    const auto image_y = y - origin_y_;
    if (amount <= 0.0F || image_x < 0 || image_y < 0 || image_x >= destination_.width() ||
        image_y >= destination_.height()) {
      return;
    }

    auto* dst = destination_.scanLine(image_y) + static_cast<std::size_t>(image_x) * (preserve_alpha_ ? 4U : 3U);
    if (preserve_alpha_ && dst[3] == 0) {
      return;
    }
    dst[0] = clamp_byte(static_cast<float>(lut.red[dst[0]]) * amount + static_cast<float>(dst[0]) * (1.0F - amount));
    dst[1] =
        clamp_byte(static_cast<float>(lut.green[dst[1]]) * amount + static_cast<float>(dst[1]) * (1.0F - amount));
    dst[2] = clamp_byte(static_cast<float>(lut.blue[dst[2]]) * amount + static_cast<float>(dst[2]) * (1.0F - amount));
  }

  // Defined in image_document_io.cpp beside the PATCHY_RENDER_PROFILE helpers it reads.
  void profile_compositor_step(const char* step, const Layer& layer, Rect rect, double elapsed_ms) const;

private:
  QImage& destination_;
  bool preserve_alpha_{false};
  std::int32_t origin_x_{0};
  std::int32_t origin_y_{0};
  std::string profile_layer_path_;
};

}  // namespace patchy::ui

PATCHY_COMPOSITOR_TARGET_INSTANTIATIONS(extern, patchy::ui::QImageCompositeTarget)
