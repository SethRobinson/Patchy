#include "core/background_eraser.hpp"

#include "core/pixel_depth.hpp"
#include "core/rect_utils.hpp"

#include <algorithm>
#include <cstddef>

namespace patchy {

std::array<std::uint8_t, 4> background_eraser_pixel_rgba8(std::array<std::uint8_t, 4> original, float weight,
                                                          bool to_background_color, EditColor background) noexcept {
  weight = std::clamp(weight, 0.0F, 1.0F);
  if (weight <= 0.0F) {
    return original;
  }
  auto result = original;
  if (!to_background_color) {
    result[3] = weight >= 1.0F ? std::uint8_t{0} : clamp_byte(static_cast<float>(original[3]) * (1.0F - weight));
    return result;
  }
  const std::array<std::uint8_t, 3> paint{background.r, background.g, background.b};
  for (std::size_t channel = 0; channel < 3U; ++channel) {
    result[channel] = weight >= 1.0F
                          ? paint[channel]
                          : clamp_byte(static_cast<float>(paint[channel]) * weight +
                                       static_cast<float>(original[channel]) * (1.0F - weight));
  }
  return result;
}

std::array<float, 4> background_eraser_pixel_deep(std::array<float, 4> original, float weight,
                                                  bool to_background_color,
                                                  std::array<float, 3> background) noexcept {
  weight = std::clamp(weight, 0.0F, 1.0F);
  if (weight <= 0.0F) {
    return original;
  }
  auto result = original;
  if (!to_background_color) {
    result[3] = original[3] * (1.0F - weight);
    return result;
  }
  for (std::size_t channel = 0; channel < 3U; ++channel) {
    result[channel] = background[channel] * weight + original[channel] * (1.0F - weight);
  }
  return result;
}

BackgroundEraserStroke::BackgroundEraserStroke(const PixelBuffer& source, const BackgroundEraserOptions& options,
                                               ColorMatchSampling sampling, EditColor background,
                                               bool lock_transparent_pixels, float background_gain)
    : match_(source, options.match(), sampling, background),
      background_(background),
      to_background_color_(lock_transparent_pixels || source.format().channels < 4) {
  const bool linear = source.format().bit_depth == BitDepth::Float32;
  const auto channel = [&](std::uint8_t value) {
    return linear ? static_cast<float>(srgb_decode(static_cast<double>(value) / 255.0) * 255.0) * background_gain
                  : static_cast<float>(value);
  };
  deep_background_ = {channel(background.r), channel(background.g), channel(background.b)};
}

Rect BackgroundEraserStroke::dab(PixelBuffer& target, std::int32_t center_x, std::int32_t center_y, Rect rect,
                                 std::span<const float> footprint, const SelectionCoverage& selection) {
  const auto& source = match_.source();
  if (target.width() != source.width() || target.height() != source.height() || target.format() != source.format()) {
    return {};
  }
  // The shared match: which pixels to erase and how strongly (0 leaves a pixel alone).
  const auto hit = match_.dab(center_x, center_y, rect, footprint, selection);
  if (hit.rect.empty()) {
    return {};
  }
  const auto clipped = hit.rect;

  const auto format = source.format();
  const bool deep = format.bit_depth != BitDepth::UInt8;
  const bool has_alpha = format.channels >= 4;
  const auto* palette = match_.palette_snap();
  std::uint8_t* target_bytes = nullptr;  // fetched on the first write: data() detaches shared storage
  const auto stride = target.stride_bytes();
  const auto pixel_bytes = bytes_per_pixel(format);
  Rect dirty;
  for (std::int32_t row = 0; row < clipped.height; ++row) {
    const auto y = clipped.y + row;
    std::int32_t dirty_left = clipped.width;
    std::int32_t dirty_right = -1;
    for (std::int32_t column = 0; column < clipped.width; ++column) {
      const auto weight = hit.weights[static_cast<std::size_t>(row) * static_cast<std::size_t>(clipped.width) +
                                      static_cast<std::size_t>(column)];
      if (weight <= 0.0F) {
        continue;
      }
      const auto x = clipped.x + column;
      if (target_bytes == nullptr) {
        target_bytes = target.data().data();
      }
      auto* destination = target_bytes + static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x) * pixel_bytes;
      const auto* original = source.pixel(x, y);
      bool changed = false;
      if (deep) {
        const auto before = load_pixel(format, destination);
        store_pixel(format, destination,
                    background_eraser_pixel_deep(load_pixel(format, original), weight, to_background_color_,
                                                 deep_background_));
        changed = load_pixel(format, destination) != before;
      } else {
        const std::array<std::uint8_t, 4> start{original[0], original[1], original[2],
                                                has_alpha ? original[3] : std::uint8_t{255}};
        auto erased = background_eraser_pixel_rgba8(start, weight, to_background_color_, background_);
        if (palette != nullptr && to_background_color_) {
          // RGB only: the alpha stays under the lock, and the palette threshold would change it.
          snap_pixel_to_palette(erased.data(), 3, *palette);
        }
        const auto channels = static_cast<std::size_t>(has_alpha ? 4 : 3);
        for (std::size_t channel = 0; channel < channels; ++channel) {
          changed = changed || destination[channel] != erased[channel];
          destination[channel] = erased[channel];
        }
      }
      if (changed) {
        dirty_left = std::min(dirty_left, column);
        dirty_right = std::max(dirty_right, column);
      }
    }
    if (dirty_left <= dirty_right) {
      dirty = unite_rect(dirty, Rect{clipped.x + dirty_left, y, dirty_right - dirty_left + 1, 1});
    }
  }
  return dirty;
}

Rect BackgroundEraserStroke::round_dab(PixelBuffer& target, double center_x, double center_y, int diameter,
                                       int softness, const SelectionCoverage& selection) {
  const auto tip = color_match_round_tip(center_x, center_y, diameter, softness);
  return dab(target, tip.hot_x, tip.hot_y, tip.rect, tip.footprint, selection);
}

}  // namespace patchy
