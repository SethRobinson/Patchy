#pragma once

#include "core/adjustment_layer.hpp"
#include "core/blend_math.hpp"
#include "core/layer_render_utils.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace patchy::render_detail {

struct LayerBoundsOverride {
  LayerId layer_id{};
  Rect bounds{};
  const PixelBuffer* pixels{nullptr};
  std::optional<Rect> mask_bounds{};
};

inline const LayerBoundsOverride* layer_override_for_render(const Layer& layer,
                                                            const std::vector<LayerBoundsOverride>* overrides) {
  if (overrides == nullptr) {
    return nullptr;
  }
  const auto found = std::find_if(overrides->begin(), overrides->end(), [&layer](const LayerBoundsOverride& override) {
    return override.layer_id == layer.id();
  });
  return found == overrides->end() ? nullptr : &*found;
}

inline Rect layer_bounds_for_render(const Layer& layer, const std::vector<LayerBoundsOverride>* overrides) {
  if (const auto* override = layer_override_for_render(layer, overrides); override != nullptr) {
    return override->bounds;
  }
  return layer_pixel_bounds(layer);
}

inline const PixelBuffer& layer_pixels_for_render(const Layer& layer,
                                                  const std::vector<LayerBoundsOverride>* overrides) {
  if (const auto* override = layer_override_for_render(layer, overrides);
      override != nullptr && override->pixels != nullptr) {
    return *override->pixels;
  }
  return layer.pixels();
}

inline Rect adjustment_bounds_for_render(const Layer& layer, const std::vector<LayerBoundsOverride>* overrides) {
  if (const auto* override = layer_override_for_render(layer, overrides); override != nullptr) {
    return override->bounds;
  }
  return layer.bounds();
}

inline std::optional<Rect> layer_mask_bounds_for_render(const Layer& layer,
                                                        const std::vector<LayerBoundsOverride>* overrides) {
  if (const auto* override = layer_override_for_render(layer, overrides); override != nullptr) {
    return override->mask_bounds;
  }
  return std::nullopt;
}

inline float layer_mask_alpha_for_render(const Layer& layer, std::int32_t x, std::int32_t y,
                                         std::optional<Rect> mask_bounds) {
  return mask_bounds.has_value() ? layer_mask_alpha_at(layer, x, y, *mask_bounds) : layer_mask_alpha_at(layer, x, y);
}

// Photoshop's "Layer Mask Hides Effects" blending option ('lmgm'): when set, the layer
// mask additionally clips effect output where it lands. Only exterior effects (drop
// shadow, outer glow, outside strokes) can place output beyond the masked shape;
// interior effects are already confined by their mask-shaped source.
inline bool layer_mask_clips_effect_output(const Layer& layer) {
  return layer.layer_style().layer_mask_hides_effects && layer.mask().has_value() && !layer.mask()->disabled;
}

template <typename Target, typename Callback>
inline void profile_compositor_step(Target& destination, const Layer& layer, const char* step, Rect rect,
                                    Callback&& callback) {
  if constexpr (requires(Target& target, const char* name, const Layer& profiled_layer, Rect profiled_rect,
                         double elapsed_ms) {
                  target.profile_compositor_step(name, profiled_layer, profiled_rect, elapsed_ms);
                }) {
    const auto started = std::chrono::steady_clock::now();
    callback();
    const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    destination.profile_compositor_step(step, layer, rect, elapsed);
  } else {
    callback();
  }
}

inline void max_filter_row(const std::vector<float>& input, std::vector<float>& output,
                           std::vector<int>& candidates, int width, int source_y, int radius) {
  const auto row_offset = static_cast<std::size_t>(source_y) * static_cast<std::size_t>(width);
  candidates.clear();
  std::size_t first_candidate = 0;
  int next_x = 0;
  for (int x = 0; x < width; ++x) {
    const auto add_until = std::min(width - 1, x + radius);
    while (next_x <= add_until) {
      const auto value = input[row_offset + static_cast<std::size_t>(next_x)];
      while (candidates.size() > first_candidate &&
             input[row_offset + static_cast<std::size_t>(candidates.back())] <= value) {
        candidates.pop_back();
      }
      candidates.push_back(next_x);
      ++next_x;
    }

    const auto remove_before = x - radius;
    while (first_candidate < candidates.size() && candidates[first_candidate] < remove_before) {
      ++first_candidate;
    }
    output[static_cast<std::size_t>(x)] =
        first_candidate >= candidates.size()
            ? 0.0F
            : input[row_offset + static_cast<std::size_t>(candidates[first_candidate])];
  }
}

inline std::vector<float> dilate_mask(const std::vector<float>& input, int width, int height, int radius) {
  if (radius <= 0) {
    return input;
  }
  const auto radius_squared = radius * radius;
  std::vector<float> output(input.size(), 0.0F);
  std::vector<float> row_max(static_cast<std::size_t>(std::max(0, width)), 0.0F);
  std::vector<int> candidates;
  candidates.reserve(static_cast<std::size_t>(std::max(0, width)));
  for (int dy = -radius; dy <= radius; ++dy) {
    const auto row_radius = static_cast<int>(std::floor(std::sqrt(static_cast<float>(radius_squared - dy * dy))));
    const auto target_start_y = std::max(0, -dy);
    const auto target_end_y = std::min(height, height - dy);
    for (int target_y = target_start_y; target_y < target_end_y; ++target_y) {
      const auto source_y = target_y + dy;
      max_filter_row(input, row_max, candidates, width, source_y, row_radius);
      auto* output_row = output.data() + static_cast<std::size_t>(target_y) * static_cast<std::size_t>(width);
      for (int x = 0; x < width; ++x) {
        output_row[x] = std::max(output_row[x], row_max[static_cast<std::size_t>(x)]);
      }
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

inline int layer_style_falloff_radius(float size) noexcept {
  return std::max(0, static_cast<int>(std::ceil(std::max(0.0F, size))));
}

inline void blur_layer_style_mask_in_place(std::vector<float>& mask, int width, int height, float size) {
  const auto support = layer_style_falloff_radius(size);
  if (support <= 0 || mask.empty()) {
    return;
  }

  const auto passes = std::min(3, support);
  const auto base_radius = support / passes;
  const auto extra_radius_passes = support % passes;
  std::vector<float> horizontal(mask.size(), 0.0F);
  std::vector<float> output(mask.size(), 0.0F);
  for (int pass = 0; pass < passes; ++pass) {
    const auto radius = base_radius + (pass < extra_radius_passes ? 1 : 0);
    if (radius <= 0) {
      continue;
    }
    box_blur_mask_into(mask, horizontal, output, width, height, radius);
    mask.swap(output);
  }
}

inline void apply_layer_style_spread_in_place(std::vector<float>& mask, float spread) {
  const auto spread_unit = clamp_unit(spread / 100.0F);
  if (spread_unit <= 0.0F || mask.empty()) {
    return;
  }
  if (spread_unit >= 0.999F) {
    for (auto& alpha : mask) {
      alpha = alpha > 0.0F ? 1.0F : 0.0F;
    }
    return;
  }

  const auto scale = 1.0F / (1.0F - spread_unit);
  for (auto& alpha : mask) {
    alpha = clamp_unit(alpha * scale);
  }
}

inline int layer_style_mask_supersample_scale(int width, int height, float size) noexcept {
  if (size <= 0.0F || width <= 0 || height <= 0) {
    return 1;
  }
  constexpr std::int64_t kMaxSupersampledPixels = 8'000'000;
  constexpr int kScale = 2;
  const auto high_res_pixels = static_cast<std::int64_t>(width) * static_cast<std::int64_t>(height) * kScale * kScale;
  return high_res_pixels <= kMaxSupersampledPixels ? kScale : 1;
}

inline float mask_sample_or_zero(const std::vector<float>& mask, int width, int height, int x, int y) noexcept {
  if (x < 0 || y < 0 || x >= width || y >= height) {
    return 0.0F;
  }
  return mask[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
}

inline float bilinear_mask_sample(const std::vector<float>& mask, int width, int height, float x, float y) noexcept {
  const auto x0 = static_cast<int>(std::floor(x));
  const auto y0 = static_cast<int>(std::floor(y));
  const auto tx = x - static_cast<float>(x0);
  const auto ty = y - static_cast<float>(y0);
  const auto x1 = x0 + 1;
  const auto y1 = y0 + 1;

  const auto top = mask_sample_or_zero(mask, width, height, x0, y0) * (1.0F - tx) +
                   mask_sample_or_zero(mask, width, height, x1, y0) * tx;
  const auto bottom = mask_sample_or_zero(mask, width, height, x0, y1) * (1.0F - tx) +
                      mask_sample_or_zero(mask, width, height, x1, y1) * tx;
  return top * (1.0F - ty) + bottom * ty;
}

inline std::vector<float> supersampled_layer_style_mask(const std::vector<float>& mask, int width, int height,
                                                        int scale) {
  const auto scaled_width = width * scale;
  const auto scaled_height = height * scale;
  std::vector<float> scaled(static_cast<std::size_t>(scaled_width) * static_cast<std::size_t>(scaled_height), 0.0F);
  for (int y = 0; y < scaled_height; ++y) {
    const auto source_y = (static_cast<float>(y) + 0.5F) / static_cast<float>(scale) - 0.5F;
    auto* row = scaled.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(scaled_width);
    for (int x = 0; x < scaled_width; ++x) {
      const auto source_x = (static_cast<float>(x) + 0.5F) / static_cast<float>(scale) - 0.5F;
      row[x] = bilinear_mask_sample(mask, width, height, source_x, source_y);
    }
  }
  return scaled;
}

inline void downsample_layer_style_mask(const std::vector<float>& scaled, std::vector<float>& mask, int width,
                                        int height, int scale) {
  const auto scaled_width = width * scale;
  const auto divisor = static_cast<float>(scale * scale);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      float sum = 0.0F;
      for (int sub_y = 0; sub_y < scale; ++sub_y) {
        const auto* row = scaled.data() +
                          static_cast<std::size_t>(y * scale + sub_y) * static_cast<std::size_t>(scaled_width) +
                          static_cast<std::size_t>(x * scale);
        for (int sub_x = 0; sub_x < scale; ++sub_x) {
          sum += row[sub_x];
        }
      }
      mask[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] =
          clamp_unit(sum / divisor);
    }
  }
}

inline void prepare_layer_style_soft_mask(std::vector<float>& mask, int width, int height, float size, float spread) {
  const auto scale = layer_style_mask_supersample_scale(width, height, size);
  if (scale > 1) {
    const auto scaled_width = width * scale;
    const auto scaled_height = height * scale;
    auto scaled = supersampled_layer_style_mask(mask, width, height, scale);
    blur_layer_style_mask_in_place(scaled, scaled_width, scaled_height, size * static_cast<float>(scale));
    apply_layer_style_spread_in_place(scaled, spread);
    downsample_layer_style_mask(scaled, mask, width, height, scale);
    return;
  }

  blur_layer_style_mask_in_place(mask, width, height, size);
  apply_layer_style_spread_in_place(mask, spread);
}

inline float smoothstep_unit(float value) noexcept {
  value = clamp_unit(value);
  return value * value * (3.0F - 2.0F * value);
}

inline float layer_style_falloff_alpha(float distance, float size, float spread) noexcept {
  const auto radius = std::max(0.0F, size);
  if (radius <= 0.0F) {
    return distance <= 0.0F ? 1.0F : 0.0F;
  }
  if (distance > radius) {
    return 0.0F;
  }

  const auto spread_unit = clamp_unit(spread / 100.0F);
  const auto solid_radius = radius * spread_unit;
  if (distance <= solid_radius || spread_unit >= 0.999F) {
    return 1.0F;
  }

  const auto fade_width = std::max(0.001F, radius - solid_radius);
  return 1.0F - smoothstep_unit((distance - solid_radius) / fade_width);
}

inline void relax_distance(float& distance, float& strength, float candidate_distance,
                           float candidate_strength) noexcept {
  if (candidate_strength <= 0.0F) {
    return;
  }
  constexpr float kEqualDistanceTolerance = 0.001F;
  if (candidate_distance + kEqualDistanceTolerance < distance ||
      (std::abs(candidate_distance - distance) <= kEqualDistanceTolerance && candidate_strength > strength)) {
    distance = candidate_distance;
    strength = candidate_strength;
  }
}

inline std::vector<float> layer_style_source_strengths(const std::vector<float>& input, int width, int height) {
  std::vector<float> strengths(input.size(), 0.0F);
  std::vector<std::uint8_t> visited(input.size(), 0U);
  std::vector<std::size_t> stack;
  std::vector<std::size_t> component;
  stack.reserve(256);
  component.reserve(256);

  for (std::size_t start = 0; start < input.size(); ++start) {
    if (visited[start] != 0U || input[start] <= 0.0F) {
      continue;
    }

    stack.clear();
    component.clear();
    stack.push_back(start);
    visited[start] = 1U;
    float component_strength = clamp_unit(input[start]);
    while (!stack.empty()) {
      const auto index = stack.back();
      stack.pop_back();
      component.push_back(index);
      component_strength = std::max(component_strength, clamp_unit(input[index]));

      const auto x = static_cast<int>(index % static_cast<std::size_t>(width));
      const auto y = static_cast<int>(index / static_cast<std::size_t>(width));
      for (int ny = std::max(0, y - 1); ny <= std::min(height - 1, y + 1); ++ny) {
        for (int nx = std::max(0, x - 1); nx <= std::min(width - 1, x + 1); ++nx) {
          if (nx == x && ny == y) {
            continue;
          }
          const auto neighbor =
              static_cast<std::size_t>(ny) * static_cast<std::size_t>(width) + static_cast<std::size_t>(nx);
          if (visited[neighbor] != 0U || input[neighbor] <= 0.0F) {
            continue;
          }
          visited[neighbor] = 1U;
          stack.push_back(neighbor);
        }
      }
    }

    for (const auto index : component) {
      strengths[index] = component_strength;
    }
  }
  return strengths;
}

inline std::vector<float> distance_falloff_mask(const std::vector<float>& input, int width, int height,
                                                float size, float spread) {
  constexpr float kInfinity = 1.0e20F;
  constexpr float kDiagonalDistance = 1.41421356237F;
  const auto source_strengths = layer_style_source_strengths(input, width, height);
  std::vector<float> distances(input.size(), kInfinity);
  std::vector<float> strengths(input.size(), 0.0F);
  for (std::size_t index = 0; index < input.size(); ++index) {
    if (input[index] > 0.0F) {
      distances[index] = 0.0F;
      strengths[index] = clamp_unit(source_strengths[index]);
    }
  }

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto index = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
      auto& distance = distances[index];
      auto& strength = strengths[index];
      if (x > 0) {
        const auto candidate = index - 1U;
        relax_distance(distance, strength, distances[candidate] + 1.0F, strengths[candidate]);
      }
      if (y > 0) {
        const auto candidate = index - static_cast<std::size_t>(width);
        relax_distance(distance, strength, distances[candidate] + 1.0F, strengths[candidate]);
      }
      if (x > 0 && y > 0) {
        const auto candidate = index - static_cast<std::size_t>(width) - 1U;
        relax_distance(distance, strength, distances[candidate] + kDiagonalDistance, strengths[candidate]);
      }
      if (x + 1 < width && y > 0) {
        const auto candidate = index - static_cast<std::size_t>(width) + 1U;
        relax_distance(distance, strength, distances[candidate] + kDiagonalDistance, strengths[candidate]);
      }
    }
  }

  for (int y = height - 1; y >= 0; --y) {
    for (int x = width - 1; x >= 0; --x) {
      const auto index = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
      auto& distance = distances[index];
      auto& strength = strengths[index];
      if (x + 1 < width) {
        const auto candidate = index + 1U;
        relax_distance(distance, strength, distances[candidate] + 1.0F, strengths[candidate]);
      }
      if (y + 1 < height) {
        const auto candidate = index + static_cast<std::size_t>(width);
        relax_distance(distance, strength, distances[candidate] + 1.0F, strengths[candidate]);
      }
      if (x + 1 < width && y + 1 < height) {
        const auto candidate = index + static_cast<std::size_t>(width) + 1U;
        relax_distance(distance, strength, distances[candidate] + kDiagonalDistance, strengths[candidate]);
      }
      if (x > 0 && y + 1 < height) {
        const auto candidate = index + static_cast<std::size_t>(width) - 1U;
        relax_distance(distance, strength, distances[candidate] + kDiagonalDistance, strengths[candidate]);
      }
    }
  }

  for (std::size_t index = 0; index < distances.size(); ++index) {
    distances[index] = strengths[index] * layer_style_falloff_alpha(distances[index], size, spread);
  }
  return distances;
}

inline std::vector<float> inner_bevel_height_mask(const std::vector<float>& alpha_mask, int width, int height,
                                                  float size) {
  std::vector<float> transparent_mask(alpha_mask.size(), 0.0F);
  for (std::size_t index = 0; index < alpha_mask.size(); ++index) {
    transparent_mask[index] = 1.0F - clamp_unit(alpha_mask[index]);
  }

  auto edge_nearness = distance_falloff_mask(transparent_mask, width, height, size, 0.0F);
  std::vector<float> height_mask(alpha_mask.size(), 0.0F);
  for (std::size_t index = 0; index < alpha_mask.size(); ++index) {
    height_mask[index] = clamp_unit(alpha_mask[index]) * (1.0F - edge_nearness[index]);
  }
  return height_mask;
}

template <typename Target>
void render_drop_shadow(Target& destination, const Layer& layer, const PixelBuffer& source, Rect clip, Rect bounds,
                        const LayerDropShadow& shadow, std::optional<Rect> layer_mask_bounds) {
  if (!shadow.enabled || shadow.opacity <= 0.0F) {
    return;
  }
  constexpr float kPi = 3.14159265358979323846F;
  const auto radians = (180.0F - shadow.angle_degrees) * kPi / 180.0F;
  const auto offset_x = static_cast<int>(std::lround(std::cos(radians) * shadow.distance));
  const auto offset_y = static_cast<int>(std::lround(std::sin(radians) * shadow.distance));
  const auto source_bounds = layer_visible_alpha_bounds(source, bounds);
  if (!source_bounds.has_value()) {
    return;
  }
  const auto radius = layer_style_falloff_radius(shadow.size);
  const auto shifted_bounds =
      Rect{source_bounds->x + offset_x, source_bounds->y + offset_y, source_bounds->width, source_bounds->height};
  const auto effect_bounds = outset_rect(shifted_bounds, radius + 2);
  const auto draw_rect = intersect_rect(clip, effect_bounds);
  if (draw_rect.empty()) {
    return;
  }

  const auto mask_bounds = clipped_mask_bounds(effect_bounds, draw_rect, radius + 1);
  const auto width = mask_bounds.width;
  const auto height = mask_bounds.height;
  auto mask = layer_alpha_mask(source, layer, bounds, mask_bounds, -offset_x, -offset_y, layer_mask_bounds);
  prepare_layer_style_soft_mask(mask, width, height, shadow.size, shadow.spread);

  const auto clip_to_mask = layer_mask_clips_effect_output(layer);
  for (std::int32_t y = draw_rect.y; y < draw_rect.y + draw_rect.height; ++y) {
    for (std::int32_t x = draw_rect.x; x < draw_rect.x + draw_rect.width; ++x) {
      auto alpha = mask[static_cast<std::size_t>((y - mask_bounds.y) * width + (x - mask_bounds.x))] *
                   shadow.opacity * layer.opacity();
      if (clip_to_mask) {
        alpha *= layer_mask_alpha_for_render(layer, x, y, layer_mask_bounds);
      }
      destination.composite_color(x, y, shadow.color, alpha, shadow.blend_mode);
    }
  }
}

template <typename Target>
void render_outer_glow(Target& destination, const Layer& layer, const PixelBuffer& source, Rect clip, Rect bounds,
                       const LayerOuterGlow& glow, std::optional<Rect> layer_mask_bounds) {
  if (!glow.enabled || glow.opacity <= 0.0F || glow.size <= 0.0F) {
    return;
  }
  const auto source_bounds = layer_visible_alpha_bounds(source, bounds);
  if (!source_bounds.has_value()) {
    return;
  }
  const auto radius = layer_style_falloff_radius(glow.size);
  const auto effect_bounds = outset_rect(*source_bounds, radius + 2);
  const auto draw_rect = intersect_rect(clip, effect_bounds);
  if (draw_rect.empty()) {
    return;
  }

  const auto mask_bounds = clipped_mask_bounds(effect_bounds, draw_rect, radius + 1);
  const auto width = mask_bounds.width;
  const auto height = mask_bounds.height;
  auto mask = layer_alpha_mask(source, layer, bounds, mask_bounds, 0, 0, layer_mask_bounds);
  mask = distance_falloff_mask(mask, width, height, glow.size, glow.spread);
  const auto source_mask = layer_alpha_mask(source, layer, bounds, draw_rect, 0, 0, layer_mask_bounds);
  const auto source_mask_width = draw_rect.width;

  const auto clip_to_mask = layer_mask_clips_effect_output(layer);
  for (std::int32_t y = draw_rect.y; y < draw_rect.y + draw_rect.height; ++y) {
    for (std::int32_t x = draw_rect.x; x < draw_rect.x + draw_rect.width; ++x) {
      const auto source_alpha =
          source_mask[static_cast<std::size_t>((y - draw_rect.y) * source_mask_width + (x - draw_rect.x))];
      auto glow_alpha = mask[static_cast<std::size_t>((y - mask_bounds.y) * width + (x - mask_bounds.x))] *
                        (1.0F - source_alpha) * glow.opacity * layer.opacity();
      if (clip_to_mask) {
        glow_alpha *= layer_mask_alpha_for_render(layer, x, y, layer_mask_bounds);
      }
      destination.composite_color(x, y, glow.color, glow_alpha, glow.blend_mode);
    }
  }
}

template <typename Target>
void render_inner_shadow(Target& destination, const Layer& layer, const PixelBuffer& source, Rect clip, Rect bounds,
                         const LayerInnerShadow& shadow, std::optional<Rect> layer_mask_bounds) {
  if (!shadow.enabled || shadow.opacity <= 0.0F || shadow.size <= 0.0F) {
    return;
  }
  const auto draw_rect = intersect_rect(clip, bounds);
  if (draw_rect.empty()) {
    return;
  }

  constexpr float kPi = 3.14159265358979323846F;
  const auto radians = (180.0F - shadow.angle_degrees) * kPi / 180.0F;
  const auto offset_x = static_cast<int>(std::lround(std::cos(radians) * shadow.distance));
  const auto offset_y = static_cast<int>(std::lround(std::sin(radians) * shadow.distance));
  const auto blur_radius = std::max(0, static_cast<int>(std::lround(shadow.size * 0.5F)));
  const auto sample_padding = blur_radius * 3 + std::max(std::abs(offset_x), std::abs(offset_y)) + 1;
  const auto mask_bounds = clipped_mask_bounds(outset_rect(bounds, sample_padding), draw_rect, sample_padding);
  const auto width = mask_bounds.width;
  const auto height = mask_bounds.height;
  auto shifted_mask = layer_alpha_mask(source, layer, bounds, mask_bounds, -offset_x, -offset_y, layer_mask_bounds);
  blur_mask_in_place(shifted_mask, width, height, blur_radius, 3);

  const auto source_mask = layer_alpha_mask(source, layer, bounds, draw_rect, 0, 0, layer_mask_bounds);
  const auto source_mask_width = draw_rect.width;
  const auto choke_scale = std::max(0.01F, 1.0F - clamp_unit(shadow.choke / 100.0F));
  for (std::int32_t y = draw_rect.y; y < draw_rect.y + draw_rect.height; ++y) {
    for (std::int32_t x = draw_rect.x; x < draw_rect.x + draw_rect.width; ++x) {
      const auto source_alpha =
          source_mask[static_cast<std::size_t>((y - draw_rect.y) * source_mask_width + (x - draw_rect.x))];
      if (source_alpha <= 0.0F) {
        continue;
      }
      const auto shifted_alpha =
          shifted_mask[static_cast<std::size_t>((y - mask_bounds.y) * width + (x - mask_bounds.x))];
      const auto shadow_alpha =
          source_alpha * clamp_unit((1.0F - shifted_alpha) / choke_scale) * shadow.opacity * layer.opacity();
      destination.composite_color(x, y, shadow.color, shadow_alpha, shadow.blend_mode);
    }
  }
}

template <typename Target>
void render_inner_glow(Target& destination, const Layer& layer, const PixelBuffer& source, Rect clip, Rect bounds,
                       const LayerInnerGlow& glow, std::optional<Rect> layer_mask_bounds) {
  if (!glow.enabled || glow.opacity <= 0.0F || glow.size <= 0.0F) {
    return;
  }
  const auto draw_rect = intersect_rect(clip, bounds);
  if (draw_rect.empty()) {
    return;
  }

  const auto blur_radius = std::max(0, static_cast<int>(std::lround(glow.size * 0.5F)));
  const auto sample_padding = blur_radius * 3 + 1;
  const auto mask_bounds = clipped_mask_bounds(outset_rect(bounds, sample_padding), draw_rect, sample_padding);
  const auto width = mask_bounds.width;
  const auto height = mask_bounds.height;
  auto blurred_mask = layer_alpha_mask(source, layer, bounds, mask_bounds, 0, 0, layer_mask_bounds);
  blur_mask_in_place(blurred_mask, width, height, blur_radius, 3);

  const auto source_mask = layer_alpha_mask(source, layer, bounds, draw_rect, 0, 0, layer_mask_bounds);
  const auto source_mask_width = draw_rect.width;
  const auto choke_scale = std::max(0.01F, 1.0F - clamp_unit(glow.choke / 100.0F));
  for (std::int32_t y = draw_rect.y; y < draw_rect.y + draw_rect.height; ++y) {
    for (std::int32_t x = draw_rect.x; x < draw_rect.x + draw_rect.width; ++x) {
      const auto source_alpha =
          source_mask[static_cast<std::size_t>((y - draw_rect.y) * source_mask_width + (x - draw_rect.x))];
      if (source_alpha <= 0.0F) {
        continue;
      }
      const auto blur_alpha =
          blurred_mask[static_cast<std::size_t>((y - mask_bounds.y) * width + (x - mask_bounds.x))];
      const auto source_factor = glow.source == LayerInnerGlowSource::Center
                                     ? blur_alpha
                                     : clamp_unit((1.0F - blur_alpha) / choke_scale);
      const auto glow_alpha = source_alpha * source_factor * glow.opacity * layer.opacity();
      destination.composite_color(x, y, glow.color, glow_alpha, glow.blend_mode);
    }
  }
}

template <typename Target>
void render_color_overlay(Target& destination, const Layer& layer, const PixelBuffer& source, Rect clip, Rect bounds,
                          const LayerColorOverlay& overlay, std::optional<Rect> layer_mask_bounds) {
  if (!overlay.enabled || overlay.opacity <= 0.0F) {
    return;
  }
  const auto draw_rect = intersect_rect(clip, bounds);
  if (draw_rect.empty()) {
    return;
  }
  const auto source_mask = layer_alpha_mask(source, layer, bounds, draw_rect, 0, 0, layer_mask_bounds);
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
void render_gradient_fill(Target& destination, const Layer& layer, const PixelBuffer& source, Rect clip, Rect bounds,
                          const LayerGradientFill& fill, std::optional<Rect> layer_mask_bounds) {
  if (!fill.enabled || fill.opacity <= 0.0F) {
    return;
  }
  const auto draw_rect = intersect_rect(clip, bounds);
  if (draw_rect.empty()) {
    return;
  }
  const auto source_mask = layer_alpha_mask(source, layer, bounds, draw_rect, 0, 0, layer_mask_bounds);
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
void render_bevel_emboss(Target& destination, const Layer& layer, const PixelBuffer& source, Rect clip, Rect bounds,
                         const LayerBevelEmboss& bevel, std::optional<Rect> layer_mask_bounds) {
  if (!bevel.enabled || bevel.size <= 0.0F ||
      (bevel.highlight_opacity <= 0.0F && bevel.shadow_opacity <= 0.0F)) {
    return;
  }
  const auto draw_rect = intersect_rect(clip, bounds);
  if (draw_rect.empty()) {
    return;
  }

  constexpr float kPi = 3.14159265358979323846F;
  const auto sample_padding = layer_style_falloff_radius(bevel.size) + 1;
  const auto angle = (180.0F - bevel.angle_degrees) * kPi / 180.0F;
  const auto altitude = std::clamp(bevel.altitude_degrees, 0.0F, 90.0F) * kPi / 180.0F;
  const auto horizontal = std::cos(altitude);
  const auto light_x = -std::cos(angle) * horizontal;
  const auto light_y = -std::sin(angle) * horizontal;
  const auto normal_scale = std::clamp(bevel.depth, 0.01F, 10.0F) * std::max(1.0F, bevel.size);
  const auto direction = bevel.direction_up ? 1.0F : -1.0F;
  const auto mask_bounds = clipped_mask_bounds(outset_rect(bounds, sample_padding), draw_rect, sample_padding);
  const auto alpha_mask = layer_alpha_mask(source, layer, bounds, mask_bounds, 0, 0, layer_mask_bounds);
  const auto mask_width = mask_bounds.width;
  const auto mask_height = mask_bounds.height;
  const auto height_mask = inner_bevel_height_mask(alpha_mask, mask_width, mask_height, bevel.size);

  for (std::int32_t y = draw_rect.y; y < draw_rect.y + draw_rect.height; ++y) {
    for (std::int32_t x = draw_rect.x; x < draw_rect.x + draw_rect.width; ++x) {
      const auto local_x = x - mask_bounds.x;
      const auto local_y = y - mask_bounds.y;
      const auto mask_index = static_cast<std::size_t>(local_y) * static_cast<std::size_t>(mask_width) +
                              static_cast<std::size_t>(local_x);
      const auto source_alpha = alpha_mask[mask_index];
      if (source_alpha <= 0.0F) {
        continue;
      }
      const auto left = mask_sample_or_zero(height_mask, mask_width, mask_height, local_x - 1, local_y);
      const auto right = mask_sample_or_zero(height_mask, mask_width, mask_height, local_x + 1, local_y);
      const auto top = mask_sample_or_zero(height_mask, mask_width, mask_height, local_x, local_y - 1);
      const auto bottom = mask_sample_or_zero(height_mask, mask_width, mask_height, local_x, local_y + 1);
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

inline std::vector<float> stroke_alpha_mask(const PixelBuffer& source, Rect bounds, Rect mask_bounds, int radius,
                                            LayerStrokePosition position) {
  // Photoshop derives the stroke from the layer's pixel coverage alone, treating any
  // painted pixel as inside the shape: the stroke fills the (dilated) binary shape and
  // the layer's own pixels cover it according to their alpha, so semi-transparent fills
  // let it show through. The layer mask never reshapes this contour — it only attenuates
  // the stroke where it lands (applied by the caller). Verified against Photoshop 2026.
  const auto width = mask_bounds.width;
  const auto height = mask_bounds.height;
  std::vector<float> base(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0.0F);
  if (!source.empty() && source.format().channels >= 4) {
    const auto format = source.format();
    const auto* bytes = source.data().data();
    const auto stride = source.stride_bytes();
    const auto draw_left = std::max(mask_bounds.x, bounds.x);
    const auto draw_top = std::max(mask_bounds.y, bounds.y);
    const auto draw_right = std::min(mask_bounds.x + mask_bounds.width, bounds.x + source.width());
    const auto draw_bottom = std::min(mask_bounds.y + mask_bounds.height, bounds.y + source.height());
    for (std::int32_t y = draw_top; y < draw_bottom; ++y) {
      const auto* source_row = bytes + static_cast<std::size_t>(y - bounds.y) * stride;
      auto* output = base.data() + static_cast<std::size_t>(y - mask_bounds.y) * width + (draw_left - mask_bounds.x);
      for (std::int32_t x = draw_left; x < draw_right; ++x) {
        const auto* pixel = source_row + static_cast<std::size_t>(x - bounds.x) * format.channels;
        *output++ = static_cast<float>(pixel[3]) / 255.0F;
      }
    }
  } else if (!source.empty()) {
    // Opaque formats: the shape is the layer bounds.
    const auto draw_left = std::max(mask_bounds.x, bounds.x);
    const auto draw_top = std::max(mask_bounds.y, bounds.y);
    const auto draw_right = std::min(mask_bounds.x + mask_bounds.width, bounds.x + source.width());
    const auto draw_bottom = std::min(mask_bounds.y + mask_bounds.height, bounds.y + source.height());
    for (std::int32_t y = draw_top; y < draw_bottom; ++y) {
      auto* output = base.data() + static_cast<std::size_t>(y - mask_bounds.y) * width + (draw_left - mask_bounds.x);
      for (std::int32_t x = draw_left; x < draw_right; ++x) {
        *output++ = 1.0F;
      }
    }
  }

  std::vector<float> binary(base.size(), 0.0F);
  std::vector<float> outside;
  std::vector<float> inside;
  if (position == LayerStrokePosition::Outside || position == LayerStrokePosition::Center) {
    for (std::size_t index = 0; index < base.size(); ++index) {
      binary[index] = base[index] > 0.0F ? 1.0F : 0.0F;
    }
    outside = dilate_mask(binary, width, height, radius);
  }
  if (position == LayerStrokePosition::Inside || position == LayerStrokePosition::Center) {
    for (std::size_t index = 0; index < base.size(); ++index) {
      binary[index] = base[index] < 1.0F ? 1.0F : 0.0F;
    }
    inside = dilate_mask(binary, width, height, radius);
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
void render_stroke(Target& destination, const Layer& layer, const PixelBuffer& source, Rect clip, Rect bounds,
                   const LayerStroke& stroke, std::optional<Rect> layer_mask_bounds) {
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
  const auto mask = stroke_alpha_mask(source, bounds, mask_bounds, radius, stroke.position);
  const auto mask_width = mask_bounds.width;
  const auto layer_has_mask = layer.mask().has_value() && !layer.mask()->disabled;
  for (std::int32_t y = draw_rect.y; y < draw_rect.y + draw_rect.height; ++y) {
    for (std::int32_t x = draw_rect.x; x < draw_rect.x + draw_rect.width; ++x) {
      auto mask_alpha = mask[static_cast<std::size_t>((y - mask_bounds.y) * mask_width + (x - mask_bounds.x))];
      if (layer_has_mask && mask_alpha > 0.0F) {
        // The layer mask attenuates the stroke where it lands rather than reshaping it.
        mask_alpha *= layer_mask_alpha_for_render(layer, x, y, layer_mask_bounds);
      }
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
void composite_adjustment_layer(Target& destination, const Layer& layer, Rect clip,
                                const std::vector<LayerBoundsOverride>* overrides) {
  if (!layer.visible() || layer.opacity() <= 0.0F) {
    return;
  }
  const auto settings = adjustment_settings_from_layer(layer);
  if (!settings.has_value() || !adjustment_has_effect(*settings)) {
    return;
  }

  auto draw_rect = clip;
  const auto bounds = adjustment_bounds_for_render(layer, overrides);
  if (!bounds.empty()) {
    draw_rect = intersect_rect(draw_rect, bounds);
  }
  if (draw_rect.empty()) {
    return;
  }

  const auto layer_mask_bounds = layer_mask_bounds_for_render(layer, overrides);
  for (std::int32_t y = draw_rect.y; y < draw_rect.y + draw_rect.height; ++y) {
    for (std::int32_t x = draw_rect.x; x < draw_rect.x + draw_rect.width; ++x) {
      const auto amount = layer_mask_alpha_for_render(layer, x, y, layer_mask_bounds) * layer.opacity();
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

  const auto& source = layer_pixels_for_render(layer, overrides);
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
  const auto layer_mask_bounds = layer_mask_bounds_for_render(layer, overrides);
  const auto& style = layer.layer_style();
  if (style.effects_visible) {
    for (const auto& shadow : style.drop_shadows) {
      profile_compositor_step(destination, layer, "drop_shadow", clip, [&] {
        render_drop_shadow(destination, layer, source, clip, bounds, shadow, layer_mask_bounds);
      });
    }
    for (const auto& glow : style.outer_glows) {
      profile_compositor_step(destination, layer, "outer_glow", clip, [&] {
        render_outer_glow(destination, layer, source, clip, bounds, glow, layer_mask_bounds);
      });
    }
  }

  const auto draw_rect = intersect_rect(clip, bounds);
  if (!draw_rect.empty()) {
    profile_compositor_step(destination, layer, "base_pixels", draw_rect, [&] {
      const auto format = source.format();
      const auto channels = format.channels;
      const auto* source_bytes = source.data().data();
      const auto source_stride = source.stride_bytes();
      const auto has_enabled_mask = layer.mask().has_value() && !layer.mask()->disabled;
      bool composited_by_target = false;
      if (!has_enabled_mask && layer.blend_mode() == BlendMode::Normal) {
        if constexpr (requires(Target& target, std::int32_t x, std::int32_t y, const std::uint8_t* row,
                                std::int32_t width, std::uint16_t channel_count, float opacity) {
                        target.composite_source_row(x, y, row, width, channel_count, opacity);
                      }) {
          for (std::int32_t y = draw_rect.y; y < draw_rect.y + draw_rect.height; ++y) {
            const auto sy = y - bounds.y;
            const auto sx = draw_rect.x - bounds.x;
            const auto* source_row =
                source_bytes + static_cast<std::size_t>(sy) * source_stride + static_cast<std::size_t>(sx) * channels;
            destination.composite_source_row(draw_rect.x, y, source_row, draw_rect.width, channels, layer.opacity());
          }
          composited_by_target = true;
        }
      }
      if (!composited_by_target) {
        for (std::int32_t y = draw_rect.y; y < draw_rect.y + draw_rect.height; ++y) {
          const auto sy = y - bounds.y;
          const auto* source_row = source_bytes + static_cast<std::size_t>(sy) * source_stride;
          for (std::int32_t x = draw_rect.x; x < draw_rect.x + draw_rect.width; ++x) {
            const auto sx = x - bounds.x;
            const auto* src = source_row + static_cast<std::size_t>(sx) * channels;
            const auto source_alpha = channels >= 4 ? static_cast<float>(src[3]) / 255.0F : 1.0F;
            const auto alpha = source_alpha * layer_mask_alpha_for_render(layer, x, y, layer_mask_bounds) *
                               layer.opacity();
            destination.composite_color(x, y, RgbColor{src[0], src[1], src[2]}, alpha, layer.blend_mode());
          }
        }
      }
    });
  }

  if (style.effects_visible) {
    for (const auto& shadow : style.inner_shadows) {
      profile_compositor_step(destination, layer, "inner_shadow", clip, [&] {
        render_inner_shadow(destination, layer, source, clip, bounds, shadow, layer_mask_bounds);
      });
    }
    for (const auto& glow : style.inner_glows) {
      profile_compositor_step(destination, layer, "inner_glow", clip, [&] {
        render_inner_glow(destination, layer, source, clip, bounds, glow, layer_mask_bounds);
      });
    }
    for (const auto& overlay : style.color_overlays) {
      profile_compositor_step(destination, layer, "color_overlay", clip, [&] {
        render_color_overlay(destination, layer, source, clip, bounds, overlay, layer_mask_bounds);
      });
    }
    for (const auto& fill : style.gradient_fills) {
      profile_compositor_step(destination, layer, "gradient_fill", clip, [&] {
        render_gradient_fill(destination, layer, source, clip, bounds, fill, layer_mask_bounds);
      });
    }
    for (const auto& bevel : style.bevels) {
      profile_compositor_step(destination, layer, "bevel_emboss", clip, [&] {
        render_bevel_emboss(destination, layer, source, clip, bounds, bevel, layer_mask_bounds);
      });
    }
    for (const auto& stroke : style.strokes) {
      profile_compositor_step(destination, layer, "stroke", clip, [&] {
        render_stroke(destination, layer, source, clip, bounds, stroke, layer_mask_bounds);
      });
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
    composite_adjustment_layer(destination, layer, clip, overrides);
    return;
  }

  composite_pixel_layer(destination, layer, clip, overrides, throw_on_unsupported_pixel_format);
}

}  // namespace patchy::render_detail
