#include "core/layer_render_utils.hpp"

#include "core/blend_math.hpp"
#include "core/pixel_buffer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace patchy {

namespace {

constexpr int kExpensiveStylePadding = 96;

int layer_style_falloff_radius(float size) noexcept {
  return std::max(0, static_cast<int>(std::ceil(std::max(0.0F, size))));
}

}  // namespace

Rect outset_rect(Rect rect, int amount) noexcept {
  return Rect{rect.x - amount, rect.y - amount, rect.width + amount * 2, rect.height + amount * 2};
}

Rect clipped_mask_bounds(Rect full_bounds, Rect draw_rect, int sample_padding) noexcept {
  return intersect_rect(full_bounds, outset_rect(draw_rect, std::max(0, sample_padding)));
}

Rect layer_pixel_bounds(const Layer& layer) {
  const auto& source = layer.pixels();
  return layer.bounds().empty() ? Rect::from_size(source.width(), source.height()) : layer.bounds();
}

std::optional<Rect> visible_alpha_local_bounds(const PixelBuffer& pixels) {
  if (pixels.empty() || pixels.format().bit_depth != BitDepth::UInt8) {
    return std::nullopt;
  }
  if (pixels.format().channels < 4) {
    return Rect::from_size(pixels.width(), pixels.height());
  }

  std::int32_t min_x = pixels.width();
  std::int32_t min_y = pixels.height();
  std::int32_t max_x = -1;
  std::int32_t max_y = -1;
  const auto* bytes = pixels.data().data();
  const auto stride = pixels.stride_bytes();
  const auto channels = pixels.format().channels;
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    const auto* row = bytes + static_cast<std::size_t>(y) * stride;
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      if (row[static_cast<std::size_t>(x) * channels + 3U] == 0U) {
        continue;
      }
      min_x = std::min(min_x, x);
      min_y = std::min(min_y, y);
      max_x = std::max(max_x, x);
      max_y = std::max(max_y, y);
    }
  }
  if (max_x < min_x || max_y < min_y) {
    return std::nullopt;
  }
  return Rect{min_x, min_y, max_x - min_x + 1, max_y - min_y + 1};
}

std::optional<Rect> layer_visible_alpha_bounds(const PixelBuffer& pixels, Rect bounds) {
  const auto local_bounds = visible_alpha_local_bounds(pixels);
  if (!local_bounds.has_value()) {
    return std::nullopt;
  }
  return Rect{bounds.x + local_bounds->x, bounds.y + local_bounds->y, local_bounds->width, local_bounds->height};
}

std::optional<Rect> layer_visible_alpha_bounds(const Layer& layer, Rect bounds) {
  return layer_visible_alpha_bounds(layer.pixels(), bounds);
}

int layer_style_effect_padding(const LayerStyle& style) noexcept {
  if (!style.effects_visible || style.empty()) {
    return 0;
  }

  int padding = 0;
  constexpr double kRadiansPerDegree = 3.14159265358979323846 / 180.0;
  for (const auto& shadow : style.drop_shadows) {
    if (!shadow.enabled || shadow.opacity <= 0.0F) {
      continue;
    }
    const auto radians = (180.0 - static_cast<double>(shadow.angle_degrees)) * kRadiansPerDegree;
    const auto offset_x = static_cast<int>(std::lround(std::cos(radians) * shadow.distance));
    const auto offset_y = static_cast<int>(std::lround(std::sin(radians) * shadow.distance));
    padding = std::max(padding, std::abs(offset_x) + std::abs(offset_y) + layer_style_falloff_radius(shadow.size) + 2);
  }
  for (const auto& glow : style.outer_glows) {
    if (!glow.enabled || glow.opacity <= 0.0F || glow.size <= 0.0F) {
      continue;
    }
    padding = std::max(padding, layer_style_falloff_radius(glow.size) + 2);
  }
  for (const auto& satin : style.satins) {
    if (satin.enabled && satin.opacity > 0.0F && satin.size > 0.0F) {
      padding = std::max(padding, std::max(1, static_cast<int>(std::ceil(satin.size))) + 1);
    }
  }
  for (const auto& stroke : style.strokes) {
    if (stroke.enabled && stroke.opacity > 0.0F && stroke.size > 0.0F) {
      padding = std::max(padding, std::max(1, static_cast<int>(std::ceil(stroke.size))) + 1);
    }
  }
  return padding;
}

int layer_effect_padding(const Layer& layer) noexcept {
  int padding = 0;
  if (layer.kind() == LayerKind::Group) {
    for (const auto& child : layer.children()) {
      padding = std::max(padding, layer_effect_padding(child));
    }
    return padding;
  }
  return layer_style_effect_padding(layer.layer_style());
}

int document_effect_padding(const Document& document) noexcept {
  int padding = 0;
  for (const auto& layer : document.layers()) {
    padding = std::max(padding, layer_effect_padding(layer));
  }
  return padding;
}

Rect layer_bounds_with_effects(const Layer& layer, Rect bounds) noexcept {
  const auto padding = layer_effect_padding(layer);
  return bounds.empty() || padding <= 0 ? bounds : outset_rect(bounds, padding);
}

Rect layer_render_bounds(const Layer& layer) noexcept {
  if (layer.kind() == LayerKind::Group) {
    Rect bounds;
    for (const auto& child : layer.children()) {
      bounds = unite_rect(bounds, layer_render_bounds(child));
    }
    return bounds;
  }
  return layer_bounds_with_effects(layer, layer.bounds());
}

bool layer_style_preview_is_expensive(const Layer& layer, Rect document_bounds) noexcept {
  const auto padding = layer_effect_padding(layer);
  if (padding <= 0 || document_bounds.empty()) {
    return false;
  }
  if (padding >= kExpensiveStylePadding) {
    return true;
  }

  const auto clipped_bounds = intersect_rect(document_bounds, layer_render_bounds(layer));
  const auto clipped_area = static_cast<std::int64_t>(clipped_bounds.width) * clipped_bounds.height;
  const auto document_area = static_cast<std::int64_t>(document_bounds.width) * document_bounds.height;
  return document_area > 0 && clipped_area * 4 >= document_area;
}

float layer_mask_alpha_at(const Layer& layer, std::int32_t x, std::int32_t y) {
  const auto& mask = layer.mask();
  if (!mask.has_value() || mask->disabled) {
    return 1.0F;
  }
  return layer_mask_alpha_at(layer, x, y, mask->bounds);
}

float layer_mask_alpha_at(const Layer& layer, std::int32_t x, std::int32_t y, Rect mask_bounds) {
  const auto& mask = layer.mask();
  if (!mask.has_value() || mask->disabled) {
    return 1.0F;
  }
  if (mask->pixels.empty() || mask->pixels.format() != PixelFormat::gray8()) {
    return static_cast<float>(mask->default_color) / 255.0F;
  }
  if (!mask_bounds.contains(x, y)) {
    return static_cast<float>(mask->default_color) / 255.0F;
  }

  const auto local_x = x - mask_bounds.x;
  const auto local_y = y - mask_bounds.y;
  if (local_x < 0 || local_y < 0 || local_x >= mask->pixels.width() || local_y >= mask->pixels.height()) {
    return static_cast<float>(mask->default_color) / 255.0F;
  }
  return static_cast<float>(*mask->pixels.pixel(local_x, local_y)) / 255.0F;
}

std::vector<float> layer_alpha_mask(const PixelBuffer& source, const Layer& layer, Rect bounds, Rect mask_bounds,
                                    std::int32_t sample_offset_x, std::int32_t sample_offset_y,
                                    std::optional<Rect> layer_mask_bounds) {
  if (mask_bounds.empty()) {
    return {};
  }

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
      for (std::int32_t x = draw_left; x < draw_right; ++x) {
        *output++ = layer_mask_bounds.has_value()
                        ? layer_mask_alpha_at(layer, x + sample_offset_x, y + sample_offset_y, *layer_mask_bounds)
                        : layer_mask_alpha_at(layer, x + sample_offset_x, y + sample_offset_y);
      }
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
      const auto mask_alpha =
          layer_mask_bounds.has_value()
              ? layer_mask_alpha_at(layer, x + sample_offset_x, y + sample_offset_y, *layer_mask_bounds)
              : layer_mask_alpha_at(layer, x + sample_offset_x, y + sample_offset_y);
      *output++ = (static_cast<float>(pixel[3]) / 255.0F) * mask_alpha;
    }
  }
  return mask;
}

std::vector<float> layer_alpha_mask(const Layer& layer, Rect bounds, Rect mask_bounds, std::int32_t sample_offset_x,
                                    std::int32_t sample_offset_y, std::optional<Rect> layer_mask_bounds) {
  return layer_alpha_mask(layer.pixels(), layer, bounds, mask_bounds, sample_offset_x, sample_offset_y,
                          layer_mask_bounds);
}

}  // namespace patchy
