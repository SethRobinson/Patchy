#pragma once

#include "core/adjustment_layer.hpp"
#include "core/blend_math.hpp"
#include "core/layer_render_utils.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace patchy::render_detail {

struct LayerBoundsOverride {
  LayerId layer_id{};
  Rect bounds{};
};

inline Rect layer_bounds_for_render(const Layer& layer, const std::vector<LayerBoundsOverride>* overrides) {
  if (overrides != nullptr) {
    const auto found = std::find_if(overrides->begin(), overrides->end(), [&layer](const LayerBoundsOverride& override) {
      return override.layer_id == layer.id();
    });
    if (found != overrides->end()) {
      return found->bounds;
    }
  }
  return layer_pixel_bounds(layer);
}

inline std::vector<float> dilate_mask(const std::vector<float>& input, int width, int height, int radius) {
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

inline void box_blur_mask_into(const std::vector<float>& input, std::vector<float>& horizontal,
                               std::vector<float>& output, int width, int height, int radius) {
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

inline void blur_mask_in_place(std::vector<float>& mask, int width, int height, int radius, int passes) {
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

template <typename Target>
void render_drop_shadow(Target& destination, const Layer& layer, Rect clip, Rect bounds,
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
  const auto effect_bounds = outset_rect(bounds, padding);
  const auto draw_rect = intersect_rect(clip, effect_bounds);
  if (draw_rect.empty()) {
    return;
  }

  const auto mask_bounds = clipped_mask_bounds(effect_bounds, draw_rect, blur_radius * 3 + 1);
  const auto width = mask_bounds.width;
  const auto height = mask_bounds.height;
  auto mask = layer_alpha_mask(layer, bounds, mask_bounds, -offset_x, -offset_y);
  if (spread_radius > 0) {
    mask = dilate_mask(mask, width, height, spread_radius);
  }
  blur_mask_in_place(mask, width, height, blur_radius, 3);

  for (std::int32_t y = draw_rect.y; y < draw_rect.y + draw_rect.height; ++y) {
    for (std::int32_t x = draw_rect.x; x < draw_rect.x + draw_rect.width; ++x) {
      const auto alpha = mask[static_cast<std::size_t>((y - mask_bounds.y) * width + (x - mask_bounds.x))] *
                         shadow.opacity * layer.opacity();
      destination.composite_color(x, y, shadow.color, alpha, shadow.blend_mode);
    }
  }
}

template <typename Target>
void render_outer_glow(Target& destination, const Layer& layer, Rect clip, Rect bounds, const LayerOuterGlow& glow) {
  if (!glow.enabled || glow.opacity <= 0.0F || glow.size <= 0.0F) {
    return;
  }
  const auto blur_radius = std::max(0, static_cast<int>(std::lround(glow.size * 0.5F)));
  const auto spread_radius = std::max(0, static_cast<int>(std::lround(glow.size * clamp_unit(glow.spread / 100.0F))));
  const auto blur_padding = blur_radius * 3;
  const auto padding = blur_padding + spread_radius + 2;
  const auto effect_bounds = outset_rect(bounds, padding);
  const auto draw_rect = intersect_rect(clip, effect_bounds);
  if (draw_rect.empty()) {
    return;
  }

  const auto mask_bounds = clipped_mask_bounds(effect_bounds, draw_rect, blur_radius * 3 + 1);
  const auto width = mask_bounds.width;
  const auto height = mask_bounds.height;
  auto mask = layer_alpha_mask(layer, bounds, mask_bounds);
  if (spread_radius > 0) {
    mask = dilate_mask(mask, width, height, spread_radius);
  }
  blur_mask_in_place(mask, width, height, blur_radius, 3);
  const auto source_mask = layer_alpha_mask(layer, bounds, draw_rect);
  const auto source_mask_width = draw_rect.width;

  for (std::int32_t y = draw_rect.y; y < draw_rect.y + draw_rect.height; ++y) {
    for (std::int32_t x = draw_rect.x; x < draw_rect.x + draw_rect.width; ++x) {
      const auto source_alpha =
          source_mask[static_cast<std::size_t>((y - draw_rect.y) * source_mask_width + (x - draw_rect.x))];
      const auto glow_alpha = mask[static_cast<std::size_t>((y - mask_bounds.y) * width + (x - mask_bounds.x))] *
                              (1.0F - source_alpha) * glow.opacity * layer.opacity();
      destination.composite_color(x, y, glow.color, glow_alpha, glow.blend_mode);
    }
  }
}

template <typename Target>
void render_color_overlay(Target& destination, const Layer& layer, Rect clip, Rect bounds,
                          const LayerColorOverlay& overlay) {
  if (!overlay.enabled || overlay.opacity <= 0.0F) {
    return;
  }
  const auto draw_rect = intersect_rect(clip, bounds);
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
      destination.composite_color(x, y, overlay.color, source_alpha * overlay.opacity * layer.opacity(),
                                  overlay.blend_mode);
    }
  }
}

template <typename Target>
void render_gradient_fill(Target& destination, const Layer& layer, Rect clip, Rect bounds,
                          const LayerGradientFill& fill) {
  if (!fill.enabled || fill.opacity <= 0.0F) {
    return;
  }
  const auto draw_rect = intersect_rect(clip, bounds);
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
      destination.composite_color(x, y, gradient_color(fill.gradient, position), alpha, fill.blend_mode);
    }
  }
}

template <typename Target>
void render_bevel_emboss(Target& destination, const Layer& layer, Rect clip, Rect bounds,
                         const LayerBevelEmboss& bevel) {
  if (!bevel.enabled || bevel.size <= 0.0F ||
      (bevel.highlight_opacity <= 0.0F && bevel.shadow_opacity <= 0.0F)) {
    return;
  }
  const auto draw_rect = intersect_rect(clip, bounds);
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
  const auto mask_bounds = clipped_mask_bounds(outset_rect(bounds, sample_radius), draw_rect, sample_radius);
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
        destination.composite_color(x, y, bevel.highlight_color,
                                    clamp_unit(lighting) * source_alpha * bevel.highlight_opacity * layer.opacity(),
                                    bevel.highlight_blend_mode);
      } else if (lighting < 0.0F) {
        destination.composite_color(x, y, bevel.shadow_color,
                                    clamp_unit(-lighting) * source_alpha * bevel.shadow_opacity * layer.opacity(),
                                    bevel.shadow_blend_mode);
      }
    }
  }
}

inline std::vector<float> stroke_alpha_mask(const Layer& layer, Rect bounds, Rect mask_bounds, int radius,
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

template <typename Target>
void render_stroke(Target& destination, const Layer& layer, Rect clip, Rect bounds, const LayerStroke& stroke) {
  if (!stroke.enabled || stroke.opacity <= 0.0F || stroke.size <= 0.0F) {
    return;
  }
  const auto radius = std::max(1, static_cast<int>(std::ceil(stroke.size)));
  const auto full_mask_bounds = outset_rect(bounds, radius + 1);
  const auto effect_bounds = stroke.position == LayerStrokePosition::Inside ? bounds : full_mask_bounds;
  const auto draw_rect = intersect_rect(clip, effect_bounds);
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
      destination.composite_color(x, y, color, alpha, stroke.blend_mode);
    }
  }
}

template <typename Target>
void composite_layer(Target& destination, const Layer& layer, Rect clip,
                     const std::vector<LayerBoundsOverride>* overrides = nullptr,
                     bool throw_on_unsupported_pixel_format = false);

template <typename Target>
void composite_adjustment_layer(Target& destination, const Layer& layer, Rect clip) {
  if (!layer.visible() || layer.opacity() <= 0.0F) {
    return;
  }
  const auto settings = adjustment_settings_from_layer(layer);
  if (!settings.has_value() || !adjustment_has_effect(*settings)) {
    return;
  }

  auto draw_rect = clip;
  if (!layer.bounds().empty()) {
    draw_rect = intersect_rect(draw_rect, layer.bounds());
  }
  if (draw_rect.empty()) {
    return;
  }

  for (std::int32_t y = draw_rect.y; y < draw_rect.y + draw_rect.height; ++y) {
    for (std::int32_t x = draw_rect.x; x < draw_rect.x + draw_rect.width; ++x) {
      const auto amount = layer_mask_alpha_at(layer, x, y) * layer.opacity();
      if (amount > 0.0F) {
        destination.adjust_color(x, y, *settings, amount);
      }
    }
  }
}

template <typename Target>
void composite_pixel_layer(Target& destination, const Layer& layer, Rect clip,
                           const std::vector<LayerBoundsOverride>* overrides,
                           bool throw_on_unsupported_pixel_format) {
  if (!layer.visible() || layer.opacity() <= 0.0F || layer.kind() != LayerKind::Pixel) {
    return;
  }

  const auto& source = layer.pixels();
  if (source.empty()) {
    return;
  }
  if (source.format().bit_depth != BitDepth::UInt8 || source.format().channels < 3) {
    if (throw_on_unsupported_pixel_format) {
      throw std::invalid_argument("The starter compositor currently supports RGB/RGBA 8-bit layers only");
    }
    return;
  }

  const auto bounds = layer_bounds_for_render(layer, overrides);
  const auto& style = layer.layer_style();
  if (style.effects_visible) {
    for (const auto& shadow : style.drop_shadows) {
      render_drop_shadow(destination, layer, clip, bounds, shadow);
    }
    for (const auto& glow : style.outer_glows) {
      render_outer_glow(destination, layer, clip, bounds, glow);
    }
  }

  const auto draw_rect = intersect_rect(clip, bounds);
  if (!draw_rect.empty()) {
    const auto format = source.format();
    const auto channels = format.channels;
    const auto* source_bytes = source.data().data();
    const auto source_stride = source.stride_bytes();
    for (std::int32_t y = draw_rect.y; y < draw_rect.y + draw_rect.height; ++y) {
      const auto sy = y - bounds.y;
      const auto* source_row = source_bytes + static_cast<std::size_t>(sy) * source_stride;
      for (std::int32_t x = draw_rect.x; x < draw_rect.x + draw_rect.width; ++x) {
        const auto sx = x - bounds.x;
        const auto* src = source_row + static_cast<std::size_t>(sx) * channels;
        const auto source_alpha = channels >= 4 ? static_cast<float>(src[3]) / 255.0F : 1.0F;
        const auto alpha = source_alpha * layer_mask_alpha_at(layer, x, y) * layer.opacity();
        destination.composite_color(x, y, RgbColor{src[0], src[1], src[2]}, alpha, layer.blend_mode());
      }
    }
  }

  if (style.effects_visible) {
    for (const auto& overlay : style.color_overlays) {
      render_color_overlay(destination, layer, clip, bounds, overlay);
    }
    for (const auto& fill : style.gradient_fills) {
      render_gradient_fill(destination, layer, clip, bounds, fill);
    }
    for (const auto& bevel : style.bevels) {
      render_bevel_emboss(destination, layer, clip, bounds, bevel);
    }
    for (const auto& stroke : style.strokes) {
      render_stroke(destination, layer, clip, bounds, stroke);
    }
  }
}

template <typename Target>
void composite_layer(Target& destination, const Layer& layer, Rect clip,
                     const std::vector<LayerBoundsOverride>* overrides,
                     bool throw_on_unsupported_pixel_format) {
  if (!layer.visible() || layer.opacity() <= 0.0F) {
    return;
  }

  if (layer.kind() == LayerKind::Group) {
    for (const auto& child : layer.children()) {
      composite_layer(destination, child, clip, overrides, throw_on_unsupported_pixel_format);
    }
    return;
  }

  if (layer.kind() == LayerKind::Adjustment) {
    composite_adjustment_layer(destination, layer, clip);
    return;
  }

  composite_pixel_layer(destination, layer, clip, overrides, throw_on_unsupported_pixel_format);
}

}  // namespace patchy::render_detail
