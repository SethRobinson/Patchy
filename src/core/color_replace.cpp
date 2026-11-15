#include "core/color_replace.hpp"

#include "core/pixel_depth.hpp"
#include "core/rect_utils.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace patchy {

std::string_view color_replace_mode_token(ColorReplaceMode mode) noexcept {
  switch (mode) {
    case ColorReplaceMode::Hue:
      return "hue";
    case ColorReplaceMode::Saturation:
      return "saturation";
    case ColorReplaceMode::Color:
      return "color";
    case ColorReplaceMode::Luminosity:
      return "luminosity";
  }
  return "color";
}

std::optional<ColorReplaceMode> color_replace_mode_from_token(std::string_view token) noexcept {
  for (const auto mode : {ColorReplaceMode::Hue, ColorReplaceMode::Saturation, ColorReplaceMode::Color,
                          ColorReplaceMode::Luminosity}) {
    if (token == color_replace_mode_token(mode)) {
      return mode;
    }
  }
  return std::nullopt;
}

BlendMode color_replace_blend_mode(ColorReplaceMode mode) noexcept {
  switch (mode) {
    case ColorReplaceMode::Hue:
      return BlendMode::Hue;
    case ColorReplaceMode::Saturation:
      return BlendMode::Saturation;
    case ColorReplaceMode::Color:
      return BlendMode::Color;
    case ColorReplaceMode::Luminosity:
      return BlendMode::Luminosity;
  }
  return BlendMode::Color;
}

std::array<std::uint8_t, 4> color_replace_pixel_rgba8(std::array<std::uint8_t, 4> original, EditColor foreground,
                                                      ColorReplaceMode mode, float weight) noexcept {
  weight = std::clamp(weight, 0.0F, 1.0F);
  if (weight <= 0.0F) {
    return original;
  }
  const auto blended = blend_rgb({foreground.r, foreground.g, foreground.b}, {original[0], original[1], original[2]},
                                 color_replace_blend_mode(mode));
  auto result = original;
  for (std::size_t channel = 0; channel < 3U; ++channel) {
    result[channel] = weight >= 1.0F
                          ? blended[channel]
                          : clamp_byte(static_cast<float>(original[channel]) +
                                       (static_cast<float>(blended[channel]) - static_cast<float>(original[channel])) *
                                           weight);
  }
  return result;
}

std::array<float, 4> color_replace_pixel_deep(std::array<float, 4> original, DeepChannels paint,
                                              ColorReplaceMode mode, float weight, DeepDomain domain) noexcept {
  weight = std::clamp(weight, 0.0F, 1.0F);
  if (weight <= 0.0F) {
    return original;
  }
  const auto blended =
      blend_rgb_deep(paint, {original[0], original[1], original[2]}, color_replace_blend_mode(mode), domain);
  auto result = original;
  for (std::size_t channel = 0; channel < 3U; ++channel) {
    result[channel] = original[channel] + (blended[channel] - original[channel]) * weight;
  }
  return result;
}

ColorReplaceStroke::ColorReplaceStroke(const PixelBuffer& source, const ColorReplaceOptions& options,
                                       ColorReplaceSampling sampling, EditColor foreground, EditColor background,
                                       float foreground_gain)
    : match_(source, options.match(), sampling, background), mode_(options.mode), foreground_(foreground) {
  domain_ = source.format().bit_depth == BitDepth::Float32 ? DeepDomain::Linear : DeepDomain::Encoded;
  const auto paint_channel = [&](std::uint8_t value) {
    return domain_ == DeepDomain::Linear
               ? static_cast<float>(srgb_decode(static_cast<double>(value) / 255.0) * 255.0) * foreground_gain
               : static_cast<float>(value);
  };
  deep_paint_ = {paint_channel(foreground.r), paint_channel(foreground.g), paint_channel(foreground.b)};
}

Rect ColorReplaceStroke::dab(PixelBuffer& target, std::int32_t center_x, std::int32_t center_y, Rect rect,
                             std::span<const float> footprint, const SelectionCoverage& selection) {
  const auto& source = match_.source();
  if (target.width() != source.width() || target.height() != source.height() || target.format() != source.format()) {
    return {};
  }
  // The shared match: which pixels to write and how strongly (0 leaves a pixel alone).
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
                    color_replace_pixel_deep(load_pixel(format, original), deep_paint_, mode_, weight, domain_));
        changed = load_pixel(format, destination) != before;
      } else {
        const std::array<std::uint8_t, 4> start{original[0], original[1], original[2],
                                                has_alpha ? original[3] : std::uint8_t{255}};
        auto painted = color_replace_pixel_rgba8(start, foreground_, mode_, weight);
        if (palette != nullptr) {
          // RGB only: the tool never changes alpha, and the palette threshold would.
          snap_pixel_to_palette(painted.data(), 3, *palette);
        }
        changed = destination[0] != painted[0] || destination[1] != painted[1] || destination[2] != painted[2];
        destination[0] = painted[0];
        destination[1] = painted[1];
        destination[2] = painted[2];
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

Rect ColorReplaceStroke::round_dab(PixelBuffer& target, double center_x, double center_y, int diameter, int softness,
                                   const SelectionCoverage& selection) {
  const auto tip = color_match_round_tip(center_x, center_y, diameter, softness);
  return dab(target, tip.hot_x, tip.hot_y, tip.rect, tip.footprint, selection);
}

}  // namespace patchy
