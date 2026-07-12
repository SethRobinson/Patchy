#pragma once

#include "core/adjustment_layer.hpp"
#include "core/blend_math.hpp"
#include "core/layer_render_utils.hpp"
#include "core/pattern_resource.hpp"
#include "core/style_contour.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
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
  std::optional<bool> visible{};
};

struct CompositeSample {
  RgbColor color{};
  float alpha{0.0F};
};

[[nodiscard]] inline bool layer_has_rendered_blend_if(const Layer& layer) noexcept {
  return layer.blend_if_payload_status() == BlendIfPayloadStatus::Supported &&
         !blend_if_is_identity(layer.blend_if());
}

[[nodiscard]] inline bool blend_if_has_underlying_ranges(const LayerBlendIf& settings) noexcept {
  const BlendIfThresholds identity;
  return std::any_of(settings.channels.begin(), settings.channels.end(), [&](const BlendIfChannelRanges& channel) {
    return channel.underlying_layer != identity;
  });
}

[[nodiscard]] inline bool layer_has_rendered_underlying_blend_if(const Layer& layer) noexcept {
  return layer_has_rendered_blend_if(layer) && blend_if_has_underlying_ranges(layer.blend_if());
}

[[nodiscard]] inline bool layers_have_rendered_blend_if(const std::vector<Layer>& layers) noexcept {
  for (const auto& layer : layers) {
    if (layer_has_rendered_blend_if(layer) ||
        (layer.kind() == LayerKind::Group && layers_have_rendered_blend_if(layer.children()))) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] inline bool layers_have_rendered_underlying_blend_if(const std::vector<Layer>& layers) noexcept {
  for (const auto& layer : layers) {
    if (layer_has_rendered_underlying_blend_if(layer) ||
        (layer.kind() == LayerKind::Group && layers_have_rendered_underlying_blend_if(layer.children()))) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] inline float blend_if_underlying_alpha_factor(const LayerBlendIf& settings,
                                                             CompositeSample underlying) noexcept {
  // Photoshop treats the transparent part of a partially covered backdrop as
  // passing the Underlying Layer test. Only the covered fraction is tested
  // against the destination color.
  const auto destination_alpha = clamp_unit(underlying.alpha);
  return (1.0F - destination_alpha) +
         destination_alpha *
             (static_cast<float>(blend_if_underlying_alpha_byte(settings, underlying.color)) / 255.0F);
}

[[nodiscard]] inline float blend_if_source_alpha_factor(const LayerBlendIf& settings,
                                                        RgbColor source) noexcept {
  return static_cast<float>(blend_if_source_alpha_byte(settings, source)) / 255.0F;
}

// Blend If must inspect the layer stack as it stood before any effect from the
// current layer was drawn. Capturing the touched rectangle also keeps the
// result stable while the current layer composites pixel by pixel.
class CompositeSnapshot {
public:
  CompositeSnapshot() = default;

  template <typename Target>
  CompositeSnapshot(const Target& source, Rect rect)
      : rect_(rect),
        rgb_(static_cast<std::size_t>(std::max(0, rect.width)) *
                 static_cast<std::size_t>(std::max(0, rect.height)) * 3U,
             0),
        alpha_(static_cast<std::size_t>(std::max(0, rect.width)) *
                   static_cast<std::size_t>(std::max(0, rect.height)),
               0.0F) {
    for (std::int32_t y = 0; y < rect_.height; ++y) {
      for (std::int32_t x = 0; x < rect_.width; ++x) {
        const auto index =
            static_cast<std::size_t>(y) * static_cast<std::size_t>(rect_.width) + static_cast<std::size_t>(x);
        const auto sample = source.sample_color(rect_.x + x, rect_.y + y);
        rgb_[index * 3U + 0U] = sample.color.red;
        rgb_[index * 3U + 1U] = sample.color.green;
        rgb_[index * 3U + 2U] = sample.color.blue;
        alpha_[index] = clamp_unit(sample.alpha);
      }
    }
  }

  [[nodiscard]] CompositeSample sample_color(std::int32_t x, std::int32_t y) const noexcept {
    x -= rect_.x;
    y -= rect_.y;
    if (x < 0 || y < 0 || x >= rect_.width || y >= rect_.height) {
      return {};
    }
    const auto index =
        static_cast<std::size_t>(y) * static_cast<std::size_t>(rect_.width) + static_cast<std::size_t>(x);
    const auto* rgb = rgb_.data() + index * 3U;
    return CompositeSample{RgbColor{rgb[0], rgb[1], rgb[2]}, alpha_[index]};
  }

private:
  Rect rect_{};
  std::vector<std::uint8_t> rgb_;
  std::vector<float> alpha_;
};

// ---------------------------------------------------------------------------
// Optional cache hook for the expensive per-effect float masks (distance
// transforms, spread expansions, interior blurs). A provider that returns a
// hit lets a renderer skip the whole mask prep; on a miss the renderer
// computes the mask over its FULL domain (not the legacy draw-clipped window)
// and offers it back. Masks depend only on layer-local content, so entries
// keyed by content_revision survive layer moves. full_domain_allowed gates
// byte-stability: full renders must produce identical bytes with and without
// a provider, so full-domain masks are only used where the legacy windowed
// domain would have been the full domain anyway (see the UI-side provider).

enum class StyleMaskKind : std::uint8_t {
  DropShadow,
  OuterGlow,
  InnerShadow,
  InnerGlow,
  BevelHeight,
  Stroke,
  Satin,
};

struct StyleMaskEntry {
  std::vector<float> primary;
  // BevelHeight keeps the alpha mask alongside the height mask.
  std::vector<float> secondary;
};

class StyleMaskProvider {
public:
  virtual ~StyleMaskProvider() = default;
  // May the renderer swap its legacy draw-clipped mask window for `domain`
  // (document space)? Must be false whenever that could change full-render
  // output bytes.
  [[nodiscard]] virtual bool full_domain_allowed(Rect domain) const = 0;
  [[nodiscard]] virtual std::shared_ptr<const StyleMaskEntry> fetch(const Layer& layer, StyleMaskKind kind,
                                                                    std::uint32_t effect_index, Rect domain,
                                                                    Rect bounds,
                                                                    std::optional<Rect> mask_bounds) = 0;
  virtual void store(const Layer& layer, StyleMaskKind kind, std::uint32_t effect_index, Rect domain, Rect bounds,
                     std::optional<Rect> mask_bounds, std::shared_ptr<const StyleMaskEntry> entry) = 0;
};

// Shared miss/hit flow: returns the mask (cached or computed) plus the domain
// it covers. compute(domain) must return the prepared primary/secondary masks
// for exactly that domain.
template <typename ComputeFn>
std::pair<std::shared_ptr<const StyleMaskEntry>, Rect> style_mask_for_render(
    StyleMaskProvider* provider, const Layer& layer, StyleMaskKind kind, std::uint32_t effect_index,
    Rect full_domain, Rect gate_rect, Rect legacy_domain, Rect bounds, std::optional<Rect> mask_bounds,
    ComputeFn&& compute) {
  if (provider != nullptr && provider->full_domain_allowed(gate_rect)) {
    if (auto cached = provider->fetch(layer, kind, effect_index, full_domain, bounds, mask_bounds);
        cached != nullptr) {
      return {std::move(cached), full_domain};
    }
    // A null fetch may have latched the key as in-flight; store() (entry or
    // null) MUST follow or concurrent renders of this effect block forever.
    std::shared_ptr<StyleMaskEntry> computed;
    try {
      computed = std::make_shared<StyleMaskEntry>(compute(full_domain));
    } catch (...) {
      provider->store(layer, kind, effect_index, full_domain, bounds, mask_bounds, nullptr);
      throw;
    }
    provider->store(layer, kind, effect_index, full_domain, bounds, mask_bounds, computed);
    return {std::move(computed), full_domain};
  }
  return {std::make_shared<StyleMaskEntry>(compute(legacy_domain)), legacy_domain};
}

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

inline bool layer_visible_for_render(const Layer& layer,
                                     const std::vector<LayerBoundsOverride>* overrides) {
  if (const auto* override = layer_override_for_render(layer, overrides);
      override != nullptr && override->visible.has_value()) {
    return *override->visible;
  }
  return layer.visible();
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

// 1D squared-distance lower-envelope pass (Felzenszwalb & Huttenlocher). `f` is the
// per-sample seed cost, `d` receives min over q' of (q - q')^2 + f[q']. `v`/`z` are
// scratch of size n / n+1. Envelope intersections are computed in double so the
// integer-valued inputs stay exact; no RNG, no toolchain-dependent math.
inline void squared_distance_transform_1d(const float* f, float* d, int* v, double* z, int n) {
  int k = 0;
  v[0] = 0;
  z[0] = -1.0e30;
  z[1] = 1.0e30;
  for (int q = 1; q < n; ++q) {
    const auto fq = static_cast<double>(f[q]) + static_cast<double>(q) * static_cast<double>(q);
    auto intersection =
        (fq - (static_cast<double>(f[v[k]]) + static_cast<double>(v[k]) * static_cast<double>(v[k]))) /
        (2.0 * q - 2.0 * v[k]);
    while (intersection <= z[k]) {
      --k;
      intersection =
          (fq - (static_cast<double>(f[v[k]]) + static_cast<double>(v[k]) * static_cast<double>(v[k]))) /
          (2.0 * q - 2.0 * v[k]);
    }
    ++k;
    v[k] = q;
    z[k] = intersection;
    z[k + 1] = 1.0e30;
  }
  k = 0;
  for (int q = 0; q < n; ++q) {
    while (z[k + 1] < static_cast<double>(q)) {
      ++k;
    }
    const auto dx = static_cast<float>(q - v[k]);
    d[q] = dx * dx + f[v[k]];
  }
}

// Exact squared Euclidean distance transform. On entry `field` holds 0 at source
// pixels and a large sentinel (>= kEdtUnreached) elsewhere; on return every pixel
// holds the exact squared distance to its nearest source (sentinel-sized where no
// source exists at all).
constexpr float kEdtUnreached = 1.0e20F;

inline void exact_squared_distance_transform(std::vector<float>& field, int width, int height) {
  if (width <= 0 || height <= 0) {
    return;
  }
  const auto n = std::max(width, height);
  std::vector<float> f(static_cast<std::size_t>(n), 0.0F);
  std::vector<float> d(static_cast<std::size_t>(n), 0.0F);
  std::vector<int> v(static_cast<std::size_t>(n), 0);
  std::vector<double> z(static_cast<std::size_t>(n) + 1U, 0.0);
  for (int x = 0; x < width; ++x) {
    for (int y = 0; y < height; ++y) {
      f[static_cast<std::size_t>(y)] = field[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + x];
    }
    squared_distance_transform_1d(f.data(), d.data(), v.data(), z.data(), height);
    for (int y = 0; y < height; ++y) {
      field[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + x] = d[static_cast<std::size_t>(y)];
    }
  }
  for (int y = 0; y < height; ++y) {
    auto* row = field.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
    std::copy(row, row + width, f.data());
    squared_distance_transform_1d(f.data(), d.data(), v.data(), z.data(), width);
    std::copy(d.data(), d.data() + width, row);
  }
}

// Distance in pixels from every pixel to the nearest painted (`alpha > 0`) or
// unpainted (`alpha == 0`) pixel of `base`, per `sources_are_painted`. Pixels with
// no source anywhere read as a huge distance (band coverage 0).
inline std::vector<float> stroke_distance_field(const std::vector<float>& base, int width, int height,
                                                bool sources_are_painted) {
  std::vector<float> field(base.size(), kEdtUnreached);
  for (std::size_t index = 0; index < base.size(); ++index) {
    if ((base[index] > 0.0F) == sources_are_painted) {
      field[index] = 0.0F;
    }
  }
  exact_squared_distance_transform(field, width, height);
  for (auto& value : field) {
    value = std::sqrt(value);
  }
  return field;
}

// The binary contour sits half a pixel past the last source pixel center, and the
// 1px anti-aliasing ramp is centered on the band edge, so a band reaching `band`
// pixels from the contour fully covers center distances d <= band and fades out by
// d = band + 1. Calibrated against Photoshop 2026: integer sizes on axis-aligned
// edges reproduce the legacy binary dilation exactly.
constexpr float kStrokeContourOffset = 1.0F;

inline float stroke_band_coverage(float distance, float band) noexcept {
  return band <= 0.0F ? 0.0F : clamp_unit(band + kStrokeContourOffset - distance);
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

inline void expand_layer_style_mask_in_place(std::vector<float>& mask, int width, int height, float radius,
                                             float pixels_per_unit);

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

// Photoshop's drop-shadow Spread expands the matte geometrically before blurring
// (probed via COM renders, July 2026): the shadow stays solid out to spread% x size
// with rounded Euclidean corners and only the remaining (1 - spread%) x size is
// blurred. Do not reimplement spread as a post-blur gain: saturating the box blur's
// tail exposes the kernel's rectangular support (per-glyph boxes jutting out of
// qual_rca_pinout.psd's spread-100 label plates) and turns float dust into pixels.
inline void prepare_layer_style_soft_mask(std::vector<float>& mask, int width, int height, float size, float spread) {
  const auto spread_radius = std::max(0.0F, size) * clamp_unit(spread / 100.0F);
  const auto blur_size = std::max(0.0F, size) - spread_radius;
  const auto scale = layer_style_mask_supersample_scale(width, height, size);
  if (scale > 1) {
    const auto scaled_width = width * scale;
    const auto scaled_height = height * scale;
    auto scaled = supersampled_layer_style_mask(mask, width, height, scale);
    expand_layer_style_mask_in_place(scaled, scaled_width, scaled_height, spread_radius, static_cast<float>(scale));
    blur_layer_style_mask_in_place(scaled, scaled_width, scaled_height, blur_size * static_cast<float>(scale));
    downsample_layer_style_mask(scaled, mask, width, height, scale);
    return;
  }

  expand_layer_style_mask_in_place(mask, width, height, spread_radius, 1.0F);
  blur_layer_style_mask_in_place(mask, width, height, blur_size);
}

// The interior effects' historical blur: 3 box passes of half the size each.
inline int interior_style_blur_radius(float size) noexcept {
  return std::max(0, static_cast<int>(std::lround(size * 0.5F)));
}

// Photoshop's inner-shadow/inner-glow Choke is the interior mirror of the
// drop-shadow Spread (COM-probed July 2026 with choke 0/50/100 renders): the
// inverse matte expands with rounded Euclidean corners to choke% x size and only
// the remaining (1 - choke%) x size is blurred, so choke 100 leaves a hard
// Euclidean band exactly `size` deep. Do not reimplement choke as a post-blur
// gain ((1 - blur) / (1 - choke)): amplifying the box blur's tail exposes the
// kernel's square support (a small transparent hole radiates a ~1.5 x size
// rounded box of half-tone dust instead of a size-radius disc). Turns the
// shape's alpha mask into the interior falloff field, 1 at the contour fading to
// 0 inside; choke 0 keeps the historical blur-and-invert bit for bit.
inline void prepare_layer_style_interior_falloff_mask(std::vector<float>& mask, int width, int height, float size,
                                                      float choke) {
  const auto choke_unit = clamp_unit(choke / 100.0F);
  if (choke_unit <= 0.0F) {
    blur_mask_in_place(mask, width, height, interior_style_blur_radius(size), 3);
    for (auto& value : mask) {
      value = clamp_unit(1.0F - value);
    }
    return;
  }

  for (auto& value : mask) {
    value = 1.0F - clamp_unit(value);
  }
  expand_layer_style_mask_in_place(mask, width, height, std::max(0.0F, size) * choke_unit, 1.0F);
  blur_mask_in_place(mask, width, height, interior_style_blur_radius(size * (1.0F - choke_unit)), 3);
  for (auto& value : mask) {
    value = clamp_unit(value);
  }
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

// Chamfer (1 / sqrt(2)) distance to the nearest painted (alpha > 0) pixel of `input`,
// carrying the per-component source strength alongside. Deterministic scan-order float
// relaxation shared by the outer-glow falloff and the drop-shadow spread expansion.
inline void chamfer_distance_and_strengths(const std::vector<float>& input, int width, int height,
                                           std::vector<float>& distances, std::vector<float>& strengths) {
  constexpr float kInfinity = 1.0e20F;
  constexpr float kDiagonalDistance = 1.41421356237F;
  const auto source_strengths = layer_style_source_strengths(input, width, height);
  distances.assign(input.size(), kInfinity);
  strengths.assign(input.size(), 0.0F);
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
}

inline std::vector<float> distance_falloff_mask(const std::vector<float>& input, int width, int height,
                                                float size, float spread) {
  std::vector<float> distances;
  std::vector<float> strengths;
  chamfer_distance_and_strengths(input, width, height, distances, strengths);
  for (std::size_t index = 0; index < distances.size(); ++index) {
    distances[index] = strengths[index] * layer_style_falloff_alpha(distances[index], size, spread);
  }
  return distances;
}

// Expands the matte for the drop-shadow Spread: full component strength out to
// `radius`, then a 1px anti-aliasing ramp (the stroke-band contour convention).
// `radius` is in output pixels; `pixels_per_unit` maps them to mask pixels so the
// supersampled path expands in scaled space while keeping the 1px ramp width.
inline void expand_layer_style_mask_in_place(std::vector<float>& mask, int width, int height, float radius,
                                             float pixels_per_unit) {
  if (radius <= 0.0F || mask.empty()) {
    return;
  }
  std::vector<float> distances;
  std::vector<float> strengths;
  chamfer_distance_and_strengths(mask, width, height, distances, strengths);
  for (std::size_t index = 0; index < mask.size(); ++index) {
    const auto coverage = clamp_unit(radius + 1.0F - distances[index] / pixels_per_unit);
    mask[index] = std::max(mask[index], strengths[index] * coverage);
  }
}

// A bevel technique produces one continuous height field: 0 on the exterior,
// 1 on the interior. Styles decide which side is visible (or reshape it into a
// pillow) without changing the lighting math. Keeping the fractional matte in
// this stage is important: treating every non-zero edge pixel as a binary EDT
// seed creates the one-pixel zipper normals that Smooth is meant to avoid.
inline std::vector<float> bevel_technique_height_mask(const std::vector<float>& alpha_mask, int width, int height,
                                                      const LayerBevelEmboss& bevel) {
  std::vector<float> height_mask(alpha_mask.size(), 0.0F);
  const auto size = std::max(0.01F, bevel.size);
  if (bevel.technique == BevelTechnique::Smooth) {
    height_mask = alpha_mask;
    blur_layer_style_mask_in_place(height_mask, width, height, size);
  } else {
    const auto distance_to_painted = stroke_distance_field(alpha_mask, width, height, true);
    const auto distance_to_clear = stroke_distance_field(alpha_mask, width, height, false);
    for (std::size_t index = 0; index < alpha_mask.size(); ++index) {
      const auto alpha = clamp_unit(alpha_mask[index]);
      const auto inside = 0.5F + 0.5F * clamp_unit(distance_to_clear[index] / size);
      const auto outside = 0.5F - 0.5F * clamp_unit(distance_to_painted[index] / size);
      height_mask[index] = outside * (1.0F - alpha) + inside * alpha;
    }
    if (bevel.technique == BevelTechnique::ChiselSoft) {
      // Chisel Soft retains the exact-distance roof but rounds its pixel-scale
      // facets. It is deliberately much narrower than Smooth's size-wide blur.
      blur_mask_in_place(height_mask, width, height, 1, 1);
    }
  }
  if (bevel.soften > 0.0F) {
    blur_layer_style_mask_in_place(height_mask, width, height, bevel.soften);
  }
  for (auto& value : height_mask) {
    value = clamp_unit(value);
  }
  return height_mask;
}

inline std::vector<float> stroke_alpha_mask(const PixelBuffer& source, Rect bounds, Rect mask_bounds, float size,
                                            LayerStrokePosition position);

template <typename Target>
void render_drop_shadow(Target& destination, const Layer& layer, const PixelBuffer& source, Rect clip, Rect bounds,
                        const LayerDropShadow& shadow, std::optional<Rect> layer_mask_bounds,
                        StyleMaskProvider* masks = nullptr, std::uint32_t effect_index = 0) {
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

  // radius + 2 apron: spread expansion (spread_radius + 1px ramp) plus the remaining
  // blur reach size + 2 at most, so a clipped window renders identically to a full one.
  const auto legacy_mask_bounds = clipped_mask_bounds(effect_bounds, draw_rect, radius + 2);
  const auto [entry, mask_bounds] = style_mask_for_render(
      masks, layer, StyleMaskKind::DropShadow, effect_index, effect_bounds, effect_bounds, legacy_mask_bounds,
      bounds, layer_mask_bounds, [&](Rect domain) {
        StyleMaskEntry computed;
        computed.primary =
            layer_alpha_mask(source, layer, bounds, domain, -offset_x, -offset_y, layer_mask_bounds);
        prepare_layer_style_soft_mask(computed.primary, domain.width, domain.height, shadow.size, shadow.spread);
        return computed;
      });
  const auto width = mask_bounds.width;
  const auto& mask = entry->primary;

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
                       const LayerOuterGlow& glow, std::optional<Rect> layer_mask_bounds,
                       StyleMaskProvider* masks = nullptr, std::uint32_t effect_index = 0) {
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

  const auto legacy_mask_bounds = clipped_mask_bounds(effect_bounds, draw_rect, radius + 1);
  const auto [entry, mask_bounds] = style_mask_for_render(
      masks, layer, StyleMaskKind::OuterGlow, effect_index, effect_bounds, effect_bounds, legacy_mask_bounds,
      bounds, layer_mask_bounds, [&](Rect domain) {
        StyleMaskEntry computed;
        auto base = layer_alpha_mask(source, layer, bounds, domain, 0, 0, layer_mask_bounds);
        computed.primary = distance_falloff_mask(base, domain.width, domain.height, glow.size, glow.spread);
        return computed;
      });
  const auto width = mask_bounds.width;
  const auto& mask = entry->primary;
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
                         const LayerInnerShadow& shadow, std::optional<Rect> layer_mask_bounds,
                         StyleMaskProvider* masks = nullptr, std::uint32_t effect_index = 0) {
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
  const auto choke_unit = clamp_unit(shadow.choke / 100.0F);
  const auto blur_radius = interior_style_blur_radius(shadow.size * (1.0F - choke_unit));
  // The choke = 0 padding must stay exactly the historical one: a wider window
  // shifts the box blur's running-sum rounding, and choke 0 is pinned bit for bit.
  auto sample_padding = blur_radius * 3 + std::max(std::abs(offset_x), std::abs(offset_y)) + 1;
  if (choke_unit > 0.0F) {
    sample_padding += static_cast<int>(std::ceil(std::max(0.0F, shadow.size) * choke_unit)) + 1;
  }
  const auto full_domain = outset_rect(bounds, sample_padding);
  const auto legacy_mask_bounds = clipped_mask_bounds(full_domain, draw_rect, sample_padding);
  const auto [entry, mask_bounds] = style_mask_for_render(
      masks, layer, StyleMaskKind::InnerShadow, effect_index, full_domain, bounds, legacy_mask_bounds, bounds,
      layer_mask_bounds, [&](Rect domain) {
        StyleMaskEntry computed;
        computed.primary =
            layer_alpha_mask(source, layer, bounds, domain, -offset_x, -offset_y, layer_mask_bounds);
        prepare_layer_style_interior_falloff_mask(computed.primary, domain.width, domain.height, shadow.size,
                                                  shadow.choke);
        return computed;
      });
  const auto width = mask_bounds.width;
  const auto& shifted_mask = entry->primary;

  const auto source_mask = layer_alpha_mask(source, layer, bounds, draw_rect, 0, 0, layer_mask_bounds);
  const auto source_mask_width = draw_rect.width;
  for (std::int32_t y = draw_rect.y; y < draw_rect.y + draw_rect.height; ++y) {
    for (std::int32_t x = draw_rect.x; x < draw_rect.x + draw_rect.width; ++x) {
      const auto source_alpha =
          source_mask[static_cast<std::size_t>((y - draw_rect.y) * source_mask_width + (x - draw_rect.x))];
      if (source_alpha <= 0.0F) {
        continue;
      }
      const auto falloff_alpha =
          shifted_mask[static_cast<std::size_t>((y - mask_bounds.y) * width + (x - mask_bounds.x))];
      const auto shadow_alpha = source_alpha * falloff_alpha * shadow.opacity * layer.opacity();
      destination.composite_color(x, y, shadow.color, shadow_alpha, shadow.blend_mode);
    }
  }
}

template <typename Target>
void render_inner_glow(Target& destination, const Layer& layer, const PixelBuffer& source, Rect clip, Rect bounds,
                       const LayerInnerGlow& glow, std::optional<Rect> layer_mask_bounds,
                       StyleMaskProvider* masks = nullptr, std::uint32_t effect_index = 0) {
  if (!glow.enabled || glow.opacity <= 0.0F || glow.size <= 0.0F) {
    return;
  }
  const auto draw_rect = intersect_rect(clip, bounds);
  if (draw_rect.empty()) {
    return;
  }

  const auto choke_unit = clamp_unit(glow.choke / 100.0F);
  const auto blur_radius = interior_style_blur_radius(glow.size * (1.0F - choke_unit));
  // The choke = 0 padding must stay exactly the historical one: a wider window
  // shifts the box blur's running-sum rounding, and choke 0 is pinned bit for bit.
  auto sample_padding = blur_radius * 3 + 1;
  if (choke_unit > 0.0F) {
    sample_padding += static_cast<int>(std::ceil(std::max(0.0F, glow.size) * choke_unit)) + 1;
  }
  const auto full_domain = outset_rect(bounds, sample_padding);
  const auto legacy_mask_bounds = clipped_mask_bounds(full_domain, draw_rect, sample_padding);
  const auto [entry, mask_bounds] = style_mask_for_render(
      masks, layer, StyleMaskKind::InnerGlow, effect_index, full_domain, bounds, legacy_mask_bounds, bounds,
      layer_mask_bounds, [&](Rect domain) {
        StyleMaskEntry computed;
        computed.primary = layer_alpha_mask(source, layer, bounds, domain, 0, 0, layer_mask_bounds);
        if (glow.source == LayerInnerGlowSource::Center && choke_unit <= 0.0F) {
          // The historical Center-source path: the blurred matte itself is the glow field.
          blur_mask_in_place(computed.primary, domain.width, domain.height, blur_radius, 3);
        } else {
          prepare_layer_style_interior_falloff_mask(computed.primary, domain.width, domain.height, glow.size,
                                                    glow.choke);
          if (glow.source == LayerInnerGlowSource::Center) {
            // Center source with choke: Photoshop erodes the matte geometrically, so the
            // glow retreats to the choked core (COM-probed: choke 100 leaves a hard
            // Euclidean erosion by the full size).
            for (auto& value : computed.primary) {
              value = clamp_unit(1.0F - value);
            }
          }
        }
        return computed;
      });
  const auto width = mask_bounds.width;
  const auto& falloff_mask = entry->primary;

  const auto source_mask = layer_alpha_mask(source, layer, bounds, draw_rect, 0, 0, layer_mask_bounds);
  const auto source_mask_width = draw_rect.width;
  for (std::int32_t y = draw_rect.y; y < draw_rect.y + draw_rect.height; ++y) {
    for (std::int32_t x = draw_rect.x; x < draw_rect.x + draw_rect.width; ++x) {
      const auto source_alpha =
          source_mask[static_cast<std::size_t>((y - draw_rect.y) * source_mask_width + (x - draw_rect.x))];
      if (source_alpha <= 0.0F) {
        continue;
      }
      const auto source_factor =
          falloff_mask[static_cast<std::size_t>((y - mask_bounds.y) * width + (x - mask_bounds.x))];
      const auto glow_alpha = source_alpha * source_factor * glow.opacity * layer.opacity();
      destination.composite_color(x, y, glow.color, glow_alpha, glow.blend_mode);
    }
  }
}

inline int satin_tent_peak(float size) noexcept {
  if (size <= 0.0F) {
    return 0;
  }
  return std::max(2, static_cast<int>(std::lround(size)));
}

// Photoshop's Satin blur is the exact separable tent [1..N..1] / N^2,
// N=max(2, Size), rather than the three-box approximation shared by the other
// soft layer effects. Prefix sums keep the exact kernel bounded to O(pixels)
// even at large Size values. Size zero bypasses the blur altogether.
inline void blur_satin_tent_mask_in_place(std::vector<float>& mask, int width, int height, float size) {
  const auto peak = satin_tent_peak(size);
  if (peak == 0 || width <= 0 || height <= 0 || mask.empty()) {
    return;
  }

  const auto convolve_lines = [peak](const std::vector<float>& input, std::vector<float>& output, int line_count,
                                     int line_length, std::size_t line_step, std::size_t sample_step) {
    std::vector<double> prefix(static_cast<std::size_t>(line_length) + 1U, 0.0);
    std::vector<double> weighted_prefix(static_cast<std::size_t>(line_length) + 1U, 0.0);
    const auto divisor = static_cast<double>(peak) * static_cast<double>(peak);
    for (int line = 0; line < line_count; ++line) {
      const auto base = static_cast<std::size_t>(line) * line_step;
      prefix[0] = 0.0;
      weighted_prefix[0] = 0.0;
      for (int position = 0; position < line_length; ++position) {
        const auto value = static_cast<double>(input[base + static_cast<std::size_t>(position) * sample_step]);
        prefix[static_cast<std::size_t>(position) + 1U] = prefix[static_cast<std::size_t>(position)] + value;
        weighted_prefix[static_cast<std::size_t>(position) + 1U] =
            weighted_prefix[static_cast<std::size_t>(position)] + static_cast<double>(position) * value;
      }

      for (int position = 0; position < line_length; ++position) {
        const auto left = std::max(0, position - peak + 1);
        const auto right = std::min(line_length, position + peak);
        const auto left_sum = prefix[static_cast<std::size_t>(position) + 1U] - prefix[static_cast<std::size_t>(left)];
        const auto left_weighted = weighted_prefix[static_cast<std::size_t>(position) + 1U] -
                                   weighted_prefix[static_cast<std::size_t>(left)];
        const auto right_sum = prefix[static_cast<std::size_t>(right)] -
                               prefix[static_cast<std::size_t>(position) + 1U];
        const auto right_weighted = weighted_prefix[static_cast<std::size_t>(right)] -
                                    weighted_prefix[static_cast<std::size_t>(position) + 1U];
        const auto numerator = (static_cast<double>(peak - position) * left_sum + left_weighted) +
                               (static_cast<double>(peak + position) * right_sum - right_weighted);
        output[base + static_cast<std::size_t>(position) * sample_step] =
            static_cast<float>(numerator / divisor);
      }
    }
  };

  std::vector<float> horizontal(mask.size(), 0.0F);
  std::vector<float> output(mask.size(), 0.0F);
  convolve_lines(mask, horizontal, height, width, static_cast<std::size_t>(width), 1U);
  convolve_lines(horizontal, output, width, height, 1U, static_cast<std::size_t>(width));
  mask.swap(output);
}

inline std::vector<float> satin_alpha_mask(const PixelBuffer& source, const Layer& layer, Rect bounds,
                                           Rect mask_bounds, int offset_x, int offset_y, float size, bool invert,
                                           std::optional<Rect> layer_mask_bounds) {
  // Two copies of the layer matte move in opposite directions. Photoshop blurs
  // their signed difference before taking its absolute value; folding first
  // would incorrectly join overlapping lobes.
  auto mask = layer_alpha_mask(source, layer, bounds, mask_bounds, offset_x, offset_y, layer_mask_bounds);
  const auto opposite =
      layer_alpha_mask(source, layer, bounds, mask_bounds, -offset_x, -offset_y, layer_mask_bounds);
  for (std::size_t index = 0; index < mask.size(); ++index) {
    mask[index] -= opposite[index];
  }
  blur_satin_tent_mask_in_place(mask, mask_bounds.width, mask_bounds.height, size);
  for (auto& value : mask) {
    value = clamp_unit(std::abs(value));
    if (invert) {
      value = 1.0F - value;
    }
  }
  return mask;
}

struct PreparedSatin {
  const LayerSatin* effect{nullptr};
  std::shared_ptr<const StyleMaskEntry> entry;
  Rect mask_bounds{};
};

inline PreparedSatin prepare_satin(const Layer& layer, const PixelBuffer& source, Rect draw_rect, Rect bounds,
                                   const LayerSatin& satin, std::optional<Rect> layer_mask_bounds,
                                   StyleMaskProvider* masks, std::uint32_t effect_index) {
  constexpr float kPi = 3.14159265358979323846F;
  const auto radians = (180.0F - satin.angle_degrees) * kPi / 180.0F;
  // Photoshop clamps a requested zero Distance to one pixel. Keeping the
  // offset integral matches its raster descriptor and Patchy's other effects.
  const auto distance = std::max(1.0F, satin.distance);
  const auto offset_x = static_cast<int>(std::lround(std::cos(radians) * distance));
  const auto offset_y = static_cast<int>(std::lround(std::sin(radians) * distance));
  const auto peak = satin_tent_peak(satin.size);
  const auto sample_padding = peak > 0 ? peak - 1 : 0;
  const auto full_domain = outset_rect(bounds, sample_padding);
  const auto legacy_mask_bounds = clipped_mask_bounds(full_domain, draw_rect, sample_padding);
  auto [entry, mask_bounds] = style_mask_for_render(
      masks, layer, StyleMaskKind::Satin, effect_index, full_domain, bounds, legacy_mask_bounds, bounds,
      layer_mask_bounds, [&](Rect domain) {
        StyleMaskEntry computed;
        computed.primary = satin_alpha_mask(source, layer, bounds, domain, offset_x, offset_y, satin.size,
                                            satin.invert, layer_mask_bounds);
        return computed;
      });
  return PreparedSatin{&satin, std::move(entry), mask_bounds};
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
      destination.composite_color(x, y, gradient_color_dithered(fill.gradient, position, x, y), alpha,
                                  fill.blend_mode);
    }
  }
}

struct PatternSampleRgba {
  RgbColor color{};
  float alpha{0.0F};
};

// Wrap-tiled pattern sampler in document space. Anchoring follows the Photoshop
// probes: "Link with Layer" anchors the grid at the layer's fxrp effects
// reference point (updated by PS when a layer moves), unlinked at the document
// origin; the descriptor phase adds on top in both cases. Filtering was fitted
// against PS 2026 scale probes: 100% tiles nearest (byte-crisp), magnification
// interpolates linearly between texels on the INTEGER grid, and minification
// box-averages the source footprint each output pixel covers.
class PatternTileSampler {
public:
  PatternTileSampler(const PixelBuffer& tile, const Layer& layer, float scale, float angle_degrees,
                     bool link_with_layer, float phase_x, float phase_y)
      : tile_(&tile) {
    double anchor_x = phase_x;
    double anchor_y = phase_y;
    if (link_with_layer) {
      const auto reference = layer_effects_reference_point(layer);
      anchor_x += reference[0];
      anchor_y += reference[1];
    }
    anchor_x_ = anchor_x;
    anchor_y_ = anchor_y;
    const auto clamped_scale = std::max(0.01, static_cast<double>(scale));
    inverse_scale_ = 1.0 / clamped_scale;
    constexpr double kPi = 3.14159265358979323846;
    const auto radians = static_cast<double>(angle_degrees) * kPi / 180.0;
    rotated_ = std::abs(radians) > 1e-9;
    cosine_ = std::cos(radians);
    sine_ = std::sin(radians);
    nearest_ = !rotated_ && std::abs(clamped_scale - 1.0) < 1e-6;
    // Rotated minification falls back to the linear tap (rare; box footprints
    // do not stay axis-aligned under rotation).
    box_filter_ = !nearest_ && !rotated_ && clamped_scale < 1.0;
  }

  [[nodiscard]] PatternSampleRgba sample(std::int32_t x, std::int32_t y) const noexcept {
    const auto width = tile_->width();
    const auto height = tile_->height();
    if (box_filter_) {
      // Output pixel [x, x+1) covers source [(x - a) / s, (x + 1 - a) / s):
      // average texels with their covered fractions (PS's minification fit).
      const auto u0 = (static_cast<double>(x) - anchor_x_) * inverse_scale_;
      const auto u1 = (static_cast<double>(x) + 1.0 - anchor_x_) * inverse_scale_;
      const auto v0 = (static_cast<double>(y) - anchor_y_) * inverse_scale_;
      const auto v1 = (static_cast<double>(y) + 1.0 - anchor_y_) * inverse_scale_;
      double sum_r = 0.0;
      double sum_g = 0.0;
      double sum_b = 0.0;
      double sum_a = 0.0;
      double total = 0.0;
      const auto first_x = static_cast<std::int64_t>(std::floor(u0));
      const auto last_x = static_cast<std::int64_t>(std::ceil(u1)) - 1;
      const auto first_y = static_cast<std::int64_t>(std::floor(v0));
      const auto last_y = static_cast<std::int64_t>(std::ceil(v1)) - 1;
      for (auto ty = first_y; ty <= last_y; ++ty) {
        const auto cover_y = std::min(v1, static_cast<double>(ty) + 1.0) - std::max(v0, static_cast<double>(ty));
        if (cover_y <= 0.0) {
          continue;
        }
        const auto row = wrap_index(ty, height);
        for (auto tx = first_x; tx <= last_x; ++tx) {
          const auto cover_x =
              std::min(u1, static_cast<double>(tx) + 1.0) - std::max(u0, static_cast<double>(tx));
          if (cover_x <= 0.0) {
            continue;
          }
          const auto weight = cover_x * cover_y;
          const auto value = texel(wrap_index(tx, width), row);
          sum_r += weight * value.color.red;
          sum_g += weight * value.color.green;
          sum_b += weight * value.color.blue;
          sum_a += weight * value.alpha;
          total += weight;
        }
      }
      PatternSampleRgba result;
      if (total <= 0.0) {
        return result;
      }
      result.color.red = static_cast<std::uint8_t>(std::clamp(std::lround(sum_r / total), 0L, 255L));
      result.color.green = static_cast<std::uint8_t>(std::clamp(std::lround(sum_g / total), 0L, 255L));
      result.color.blue = static_cast<std::uint8_t>(std::clamp(std::lround(sum_b / total), 0L, 255L));
      result.alpha = clamp_unit(static_cast<float>(sum_a / total));
      return result;
    }

    if (nearest_) {
      // 100%: the tile grid lands on pixel cells directly (P1/P2b byte-exact).
      const auto ix = wrap_index(
          static_cast<std::int64_t>(std::floor(static_cast<double>(x) + 0.5 - anchor_x_)), width);
      const auto iy = wrap_index(
          static_cast<std::int64_t>(std::floor(static_cast<double>(y) + 0.5 - anchor_y_)), height);
      return texel(ix, iy);
    }
    // Magnification sample positions fitted against the PS scale probe:
    // src = (x - anchor) / scale, texel grid at integer coordinates.
    auto u = static_cast<double>(x) - anchor_x_;
    auto v = static_cast<double>(y) - anchor_y_;
    if (rotated_) {
      const auto ru = u * cosine_ + v * sine_;  // inverse rotation into tile space
      const auto rv = -u * sine_ + v * cosine_;
      u = ru;
      v = rv;
    }
    u *= inverse_scale_;
    v *= inverse_scale_;
    // Magnification: linear taps between texels on the integer grid (PS fit).
    const auto floor_u = std::floor(u);
    const auto floor_v = std::floor(v);
    const auto fraction_x = static_cast<float>(u - floor_u);
    const auto fraction_y = static_cast<float>(v - floor_v);
    const auto x0 = wrap_index(static_cast<std::int64_t>(floor_u), width);
    const auto y0 = wrap_index(static_cast<std::int64_t>(floor_v), height);
    const auto x1 = x0 + 1 == width ? 0 : x0 + 1;
    const auto y1 = y0 + 1 == height ? 0 : y0 + 1;
    const auto top_left = texel(x0, y0);
    const auto top_right = texel(x1, y0);
    const auto bottom_left = texel(x0, y1);
    const auto bottom_right = texel(x1, y1);
    const auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
    const auto blend_channel = [&](float tl, float tr, float bl, float br) {
      return lerp(lerp(tl, tr, fraction_x), lerp(bl, br, fraction_x), fraction_y);
    };
    PatternSampleRgba result;
    result.color.red = static_cast<std::uint8_t>(std::clamp(
        std::lround(blend_channel(top_left.color.red, top_right.color.red, bottom_left.color.red,
                                  bottom_right.color.red)),
        0L, 255L));
    result.color.green = static_cast<std::uint8_t>(std::clamp(
        std::lround(blend_channel(top_left.color.green, top_right.color.green, bottom_left.color.green,
                                  bottom_right.color.green)),
        0L, 255L));
    result.color.blue = static_cast<std::uint8_t>(std::clamp(
        std::lround(blend_channel(top_left.color.blue, top_right.color.blue, bottom_left.color.blue,
                                  bottom_right.color.blue)),
        0L, 255L));
    result.alpha = clamp_unit(blend_channel(top_left.alpha, top_right.alpha, bottom_left.alpha,
                                            bottom_right.alpha));
    return result;
  }

  // Blend-If-calibrated integer luminance weights (299/590/111), 0..1.
  [[nodiscard]] float sample_luminance(std::int32_t x, std::int32_t y) const noexcept {
    const auto value = sample(x, y);
    const auto weighted = 299L * value.color.red + 590L * value.color.green + 111L * value.color.blue;
    return static_cast<float>(weighted) / 255000.0F;
  }

private:
  [[nodiscard]] static std::int32_t wrap_index(std::int64_t value, std::int32_t extent) noexcept {
    const auto wrapped = value % extent;
    return static_cast<std::int32_t>(wrapped < 0 ? wrapped + extent : wrapped);
  }

  [[nodiscard]] PatternSampleRgba texel(std::int32_t x, std::int32_t y) const noexcept {
    const auto* px = tile_->pixel(x, y);
    PatternSampleRgba result;
    result.color = RgbColor{px[0], px[1], px[2]};
    result.alpha = tile_->format().channels >= 4 ? static_cast<float>(px[3]) / 255.0F : 1.0F;
    return result;
  }

  const PixelBuffer* tile_{nullptr};
  double anchor_x_{0.0};
  double anchor_y_{0.0};
  double inverse_scale_{1.0};
  double cosine_{1.0};
  double sine_{0.0};
  bool rotated_{false};
  bool nearest_{true};
  bool box_filter_{false};
};

template <typename Target>
void render_pattern_overlay(Target& destination, const Layer& layer, const PixelBuffer& source, Rect clip,
                            Rect bounds, const LayerPatternOverlay& overlay,
                            std::optional<Rect> layer_mask_bounds, const PatternStore* patterns) {
  if (!overlay.enabled || overlay.opacity <= 0.0F || patterns == nullptr) {
    return;
  }
  const auto* resource = patterns->find(overlay.pattern_id);
  if (resource == nullptr || resource->tile.empty()) {
    return;  // unresolvable pattern renders nothing, like Photoshop
  }
  const auto draw_rect = intersect_rect(clip, bounds);
  if (draw_rect.empty()) {
    return;
  }
  const PatternTileSampler sampler(resource->tile, layer, overlay.scale, overlay.angle_degrees,
                                   overlay.link_with_layer, overlay.phase_x, overlay.phase_y);
  const auto source_mask = layer_alpha_mask(source, layer, bounds, draw_rect, 0, 0, layer_mask_bounds);
  const auto source_mask_width = draw_rect.width;
  for (std::int32_t y = draw_rect.y; y < draw_rect.y + draw_rect.height; ++y) {
    for (std::int32_t x = draw_rect.x; x < draw_rect.x + draw_rect.width; ++x) {
      const auto source_alpha =
          source_mask[static_cast<std::size_t>((y - draw_rect.y) * source_mask_width + (x - draw_rect.x))];
      if (source_alpha <= 0.0F) {
        continue;
      }
      const auto sample = sampler.sample(x, y);
      const auto alpha = source_alpha * sample.alpha * overlay.opacity * layer.opacity();
      if (alpha <= 0.0F) {
        continue;
      }
      destination.composite_color(x, y, sample.color, alpha, overlay.blend_mode);
    }
  }
}

template <typename Target>
void render_bevel_emboss(Target& destination, const Layer& layer, const PixelBuffer& source, Rect clip, Rect bounds,
                         const LayerBevelEmboss& bevel, std::optional<Rect> layer_mask_bounds,
                         StyleMaskProvider* masks = nullptr, std::uint32_t effect_index = 0,
                         const PatternStore* patterns = nullptr,
                         const std::vector<LayerStroke>* strokes = nullptr) {
  if (!bevel.enabled || bevel.size <= 0.0F ||
      (bevel.highlight_opacity <= 0.0F && bevel.shadow_opacity <= 0.0F)) {
    return;
  }
  const auto stroke_emboss = bevel.style == BevelEmbossStyleKind::StrokeEmboss;
  if (stroke_emboss &&
      (strokes == nullptr || std::none_of(strokes->begin(), strokes->end(), [](const LayerStroke& stroke) {
         return stroke.enabled && stroke.opacity > 0.0F && stroke.size > 0.0F;
       }))) {
    return;
  }

  auto stroke_padding = 0;
  if (stroke_emboss) {
    for (const auto& stroke : *strokes) {
      if (stroke.enabled && stroke.opacity > 0.0F && stroke.size > 0.0F) {
        stroke_padding = std::max(stroke_padding, static_cast<int>(std::ceil(stroke.size)) + 1);
      }
    }
  }
  const auto exterior_style = bevel.style == BevelEmbossStyleKind::OuterBevel ||
                              bevel.style == BevelEmbossStyleKind::Emboss ||
                              bevel.style == BevelEmbossStyleKind::PillowEmboss;
  const auto effect_padding = layer_style_falloff_radius(bevel.size + bevel.soften) + stroke_padding + 2;
  const auto effect_bounds = (exterior_style || stroke_emboss) ? outset_rect(bounds, effect_padding) : bounds;
  const auto draw_rect = intersect_rect(clip, effect_bounds);
  if (draw_rect.empty()) {
    return;
  }

  constexpr float kPi = 3.14159265358979323846F;
  const auto sample_padding = effect_padding + 1;
  const auto angle = (180.0F - bevel.angle_degrees) * kPi / 180.0F;
  const auto altitude = std::clamp(bevel.altitude_degrees, 0.0F, 90.0F) * kPi / 180.0F;
  const auto horizontal = std::cos(altitude);
  const auto light_x = -std::cos(angle) * horizontal;
  const auto light_y = -std::sin(angle) * horizontal;
  const auto light_z = std::sin(altitude);
  const auto normal_scale = std::clamp(bevel.depth, 0.01F, 10.0F) * std::max(1.0F, bevel.size);
  const auto direction = bevel.direction_up ? 1.0F : -1.0F;
  const auto full_domain = outset_rect(bounds, sample_padding);
  const auto legacy_mask_bounds = clipped_mask_bounds(full_domain, draw_rect, sample_padding);
  // Contour/texture parameters fold INTO the cached height mask. That is safe
  // because style edits go through the revision-bumping mutable layer_style()
  // accessor, so the provider's content_revision-keyed entries can never serve
  // a stale sub-option state.
  const auto [entry, mask_bounds] = style_mask_for_render(
      masks, layer, StyleMaskKind::BevelHeight, effect_index, full_domain, effect_bounds, legacy_mask_bounds, bounds,
      layer_mask_bounds, [&](Rect domain) {
        StyleMaskEntry computed;
        if (stroke_emboss) {
          computed.secondary.assign(static_cast<std::size_t>(domain.width) * domain.height, 0.0F);
          for (const auto& stroke : *strokes) {
            if (!stroke.enabled || stroke.opacity <= 0.0F || stroke.size <= 0.0F) {
              continue;
            }
            const auto stroke_mask = stroke_alpha_mask(source, bounds, domain, stroke.size, stroke.position);
            const auto stroke_radius = std::max(1, static_cast<int>(std::ceil(stroke.size)));
            const auto stroke_effect_bounds = stroke.position == LayerStrokePosition::Inside
                                                  ? bounds
                                                  : outset_rect(bounds, stroke_radius + 1);
            for (std::int32_t local_y = 0; local_y < domain.height; ++local_y) {
              for (std::int32_t local_x = 0; local_x < domain.width; ++local_x) {
                const auto index = static_cast<std::size_t>(local_y) * domain.width + local_x;
                auto alpha = stroke_mask[index] * clamp_unit(stroke.opacity);
                if (alpha > 0.0F && stroke.uses_gradient) {
                  const auto position = gradient_position(stroke.gradient, stroke_effect_bounds,
                                                          domain.x + local_x, domain.y + local_y);
                  alpha *= gradient_stop_opacity(stroke.gradient, position);
                }
                if (alpha > 0.0F && layer.mask().has_value() && !layer.mask()->disabled) {
                  alpha *= layer_mask_alpha_for_render(layer, domain.x + local_x, domain.y + local_y,
                                                       layer_mask_bounds);
                }
                computed.secondary[index] = alpha + computed.secondary[index] * (1.0F - alpha);
              }
            }
          }
        } else {
          computed.secondary = layer_alpha_mask(source, layer, bounds, domain, 0, 0, layer_mask_bounds);
        }
        computed.primary =
            bevel_technique_height_mask(computed.secondary, domain.width, domain.height, bevel);
        if (bevel.style == BevelEmbossStyleKind::PillowEmboss) {
          for (auto& value : computed.primary) {
            value = std::abs(value * 2.0F - 1.0F);
          }
        }
        if (bevel.contour.enabled && !style_contour_is_linear(bevel.contour.contour)) {
          // The Contour sub-option reshapes the bevel's cross-section: the
          // normalized edge profile (0 at the contour, 1 on the interior
          // plateau) remaps through the curve, windowed by Range (smaller
          // ranges compress the curve into the fraction of the profile nearest
          // the edge). Linear stays bit-identical to the plain bevel.
          const auto contour_lut = build_style_contour_lut(bevel.contour.contour);
          const auto range = std::clamp(bevel.contour.range, 0.01F, 1.0F);
          for (std::size_t index = 0; index < computed.primary.size(); ++index) {
            const auto remapped = sample_style_contour_lut(
                contour_lut, clamp_unit(computed.primary[index] / range), bevel.contour.anti_aliased);
            computed.primary[index] = remapped;
          }
        }
        if (bevel.texture.enabled && patterns != nullptr) {
          if (const auto* resource = patterns->find(bevel.texture.pattern_id);
              resource != nullptr && !resource->tile.empty()) {
            // Texture embosses the whole face: pattern luminance perturbs the
            // height field before normals. PS calibration (checker probes):
            // DARK texels are raised by default (Invert flips), and the bump
            // plane is smoothed so texel plateaus become domes/pits whose
            // slopes shade the whole cell, not just its edges.
            constexpr float kTextureAmplitude = 3.0F;
            const PatternTileSampler sampler(resource->tile, layer, bevel.texture.scale, 0.0F,
                                             bevel.texture.link_with_layer, bevel.texture.phase_x,
                                             bevel.texture.phase_y);
            const auto plane_size = computed.primary.size();
            std::vector<float> bump(plane_size, 0.0F);
            for (std::int32_t local_y = 0; local_y < domain.height; ++local_y) {
              for (std::int32_t local_x = 0; local_x < domain.width; ++local_x) {
                const auto index = static_cast<std::size_t>(local_y) * static_cast<std::size_t>(domain.width) +
                                   static_cast<std::size_t>(local_x);
                const auto luminance = sampler.sample_luminance(domain.x + local_x, domain.y + local_y);
                bump[index] = bevel.texture.invert ? luminance - 0.5F : 0.5F - luminance;
              }
            }
            // Separable box blur (deterministic fixed-order float sums).
            const auto box_pass = [&](bool horizontal) {
              constexpr std::int32_t blur_radius = 1;
              std::vector<float> blurred(plane_size, 0.0F);
              const auto limit = horizontal ? domain.width : domain.height;
              const auto lines = horizontal ? domain.height : domain.width;
              for (std::int32_t line = 0; line < lines; ++line) {
                for (std::int32_t position = 0; position < limit; ++position) {
                  float sum = 0.0F;
                  std::int32_t count = 0;
                  for (std::int32_t offset = -blur_radius; offset <= blur_radius; ++offset) {
                    const auto sample = position + offset;
                    if (sample < 0 || sample >= limit) {
                      continue;
                    }
                    const auto index = horizontal
                                           ? static_cast<std::size_t>(line) * domain.width + sample
                                           : static_cast<std::size_t>(sample) * domain.width + line;
                    sum += bump[index];
                    ++count;
                  }
                  const auto index = horizontal
                                         ? static_cast<std::size_t>(line) * domain.width + position
                                         : static_cast<std::size_t>(position) * domain.width + line;
                  blurred[index] = count > 0 ? sum / static_cast<float>(count) : 0.0F;
                }
              }
              bump.swap(blurred);
            };
            box_pass(true);
            box_pass(false);
            for (std::size_t index = 0; index < plane_size; ++index) {
              float texture_coverage = 0.0F;
              if (bevel.style == BevelEmbossStyleKind::InnerBevel ||
                  bevel.style == BevelEmbossStyleKind::StrokeEmboss) {
                texture_coverage = computed.secondary[index];
              } else {
                texture_coverage = 1.0F - std::abs(clamp_unit(computed.primary[index]) * 2.0F - 1.0F);
              }
              computed.primary[index] +=
                  bump[index] * bevel.texture.depth * kTextureAmplitude * clamp_unit(texture_coverage);
            }
          }
        }
        return computed;
      });
  const auto& alpha_mask = entry->secondary;
  const auto& height_mask = entry->primary;
  const auto mask_width = mask_bounds.width;
  const auto mask_height = mask_bounds.height;
  const auto gloss_is_linear = style_contour_is_linear(bevel.gloss_contour);
  std::array<std::uint8_t, 256> gloss_lut{};
  if (!gloss_is_linear) {
    gloss_lut = build_style_contour_lut(bevel.gloss_contour);
  }

  for (std::int32_t y = draw_rect.y; y < draw_rect.y + draw_rect.height; ++y) {
    for (std::int32_t x = draw_rect.x; x < draw_rect.x + draw_rect.width; ++x) {
      const auto local_x = x - mask_bounds.x;
      const auto local_y = y - mask_bounds.y;
      const auto mask_index = static_cast<std::size_t>(local_y) * static_cast<std::size_t>(mask_width) +
                              static_cast<std::size_t>(local_x);
      const auto matte_alpha = clamp_unit(alpha_mask[mask_index]);
      float effect_alpha = 0.0F;
      switch (bevel.style) {
        case BevelEmbossStyleKind::InnerBevel:
        case BevelEmbossStyleKind::StrokeEmboss:
          effect_alpha = matte_alpha;
          break;
        case BevelEmbossStyleKind::OuterBevel:
          effect_alpha = 1.0F - matte_alpha;
          break;
        case BevelEmbossStyleKind::Emboss:
        case BevelEmbossStyleKind::PillowEmboss:
          effect_alpha = 1.0F;
          break;
      }
      if (effect_alpha <= 0.0F) {
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
      const auto normal_z = 1.0F / std::max(0.0001F, length);
      // Full 3D light: the (nz - 1) * lz term subtracts the flat-face ambient so
      // level surfaces stay untouched while every slope loses altitude light.
      // This is what shades slopes running PARALLEL to the light (PS darkens the
      // left/right miters under a 90-degree light; a pure 2D dot product cannot).
      auto lighting = normal_x * light_x + normal_y * light_y + (normal_z - 1.0F) * light_z;
      if (!gloss_is_linear) {
        // Gloss Contour remaps the signed lighting scalar before the
        // highlight/shadow split; Linear short-circuits so plain bevels stay
        // bit-identical to the historical render.
        const auto remapped = sample_style_contour_lut(
            gloss_lut, clamp_unit((lighting + 1.0F) * 0.5F), bevel.gloss_anti_aliased);
        lighting = remapped * 2.0F - 1.0F;
      }
      if (layer_mask_clips_effect_output(layer)) {
        effect_alpha *= layer_mask_alpha_for_render(layer, x, y, layer_mask_bounds);
      }
      if (lighting > 0.0F) {
        destination.composite_color(x, y, bevel.highlight_color,
                                    clamp_unit(lighting) * effect_alpha * bevel.highlight_opacity * layer.opacity(),
                                    bevel.highlight_blend_mode);
      } else if (lighting < 0.0F) {
        destination.composite_color(x, y, bevel.shadow_color,
                                    clamp_unit(-lighting) * effect_alpha * bevel.shadow_opacity * layer.opacity(),
                                    bevel.shadow_blend_mode);
      }
    }
  }
}

inline std::vector<float> stroke_alpha_mask(const PixelBuffer& source, Rect bounds, Rect mask_bounds, float size,
                                            LayerStrokePosition position) {
  // Photoshop derives the stroke from the layer's pixel coverage alone, treating any
  // painted pixel as inside the shape: the stroke fills the (dilated) binary shape and
  // the layer's own pixels cover it according to their alpha, so semi-transparent fills
  // let it show through. The layer mask never reshapes this contour — it only attenuates
  // the stroke where it lands (applied by the caller). Verified against Photoshop 2026.
  //
  // The band is measured with an exact Euclidean distance field from the binary contour:
  // Outside reaches `size` px outward, Inside `size` px inward, Center `size/2` px each
  // way (the legacy dilation used the full size both ways, rendering Center at double
  // width). Coverage is `alpha * in-band + (1 - alpha) * out-band` — the sum keeps the
  // band seamless across anti-aliased contour pixels where alpha is fractional.
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

  const auto band_out = position == LayerStrokePosition::Inside   ? 0.0F
                        : position == LayerStrokePosition::Center ? size * 0.5F
                                                                  : size;
  const auto band_in = position == LayerStrokePosition::Outside  ? 0.0F
                       : position == LayerStrokePosition::Center ? size * 0.5F
                                                                 : size;
  std::vector<float> outside_distance;
  std::vector<float> inside_distance;
  if (band_out > 0.0F) {
    outside_distance = stroke_distance_field(base, width, height, true);
  }
  if (band_in > 0.0F) {
    inside_distance = stroke_distance_field(base, width, height, false);
  }

  std::vector<float> mask(base.size(), 0.0F);
  for (std::size_t index = 0; index < base.size(); ++index) {
    const auto center_alpha = base[index];
    const auto outside_coverage =
        outside_distance.empty() ? 0.0F : stroke_band_coverage(outside_distance[index], band_out);
    const auto inside_coverage =
        inside_distance.empty() ? 0.0F : stroke_band_coverage(inside_distance[index], band_in);
    mask[index] = clamp_unit(center_alpha * inside_coverage + (1.0F - center_alpha) * outside_coverage);
  }
  return mask;
}

template <typename Target>
void render_stroke(Target& destination, const Layer& layer, const PixelBuffer& source, Rect clip, Rect bounds,
                   const LayerStroke& stroke, std::optional<Rect> layer_mask_bounds,
                   StyleMaskProvider* masks = nullptr, std::uint32_t effect_index = 0) {
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
  const auto legacy_mask_bounds = clipped_mask_bounds(full_mask_bounds, draw_rect, radius + 1);
  const auto [entry, mask_bounds] = style_mask_for_render(
      masks, layer, StyleMaskKind::Stroke, effect_index, full_mask_bounds, full_mask_bounds, legacy_mask_bounds,
      bounds, layer_mask_bounds, [&](Rect domain) {
        StyleMaskEntry computed;
        computed.primary = stroke_alpha_mask(source, bounds, domain, stroke.size, stroke.position);
        return computed;
      });
  const auto& mask = entry->primary;
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
        color = gradient_color_dithered(stroke.gradient, position, x, y);
        alpha *= gradient_stop_opacity(stroke.gradient, position);
      }
      destination.composite_color(x, y, color, alpha, stroke.blend_mode);
    }
  }
}

template <typename Target>
void composite_layer(Target& destination, const Layer& layer, Rect clip,
                     const std::vector<LayerBoundsOverride>* overrides = nullptr,
                     bool throw_on_unsupported_pixel_format = false, StyleMaskProvider* masks = nullptr,
                     const CompositeSnapshot* blend_if_backdrop = nullptr,
                     const PatternStore* patterns = nullptr);

template <typename Target>
void composite_adjustment_layer(Target& destination, const Layer& layer, Rect clip,
                                const std::vector<LayerBoundsOverride>* overrides) {
  if (!layer_visible_for_render(layer, overrides) || layer.opacity() <= 0.0F) {
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
  // Channel-separable adjustments collapse to an exact per-channel LUT; the
  // per-pixel settings math (pow() for Levels gamma, per pixel, per channel)
  // dominated patch renders under adjustment stacks before this.
  const auto lut = build_adjustment_lut(*settings);
  const auto has_blend_if = layer_has_rendered_blend_if(layer);
  const auto blend_if = has_blend_if ? layer.blend_if() : LayerBlendIf{};
  for (std::int32_t y = draw_rect.y; y < draw_rect.y + draw_rect.height; ++y) {
    for (std::int32_t x = draw_rect.x; x < draw_rect.x + draw_rect.width; ++x) {
      auto amount = layer_mask_alpha_for_render(layer, x, y, layer_mask_bounds) * layer.opacity();
      if (amount <= 0.0F) {
        continue;
      }
      if (has_blend_if) {
        const auto underlying = destination.sample_color(x, y);
        auto adjusted = apply_adjustment_to_color(underlying.color, *settings);
        if (lut.has_value()) {
          adjusted = RgbColor{lut->red[underlying.color.red], lut->green[underlying.color.green],
                              lut->blue[underlying.color.blue]};
        }
        amount *= blend_if_source_alpha_factor(blend_if, adjusted) *
                  blend_if_underlying_alpha_factor(blend_if, underlying);
        if (amount <= 0.0F) {
          continue;
        }
      }
      if constexpr (requires { destination.adjust_color(x, y, *lut, amount); }) {
        if (lut.has_value()) {
          destination.adjust_color(x, y, *lut, amount);
          continue;
        }
      }
      destination.adjust_color(x, y, *settings, amount);
    }
  }
}

template <typename Target>
void composite_pixel_layer(Target& destination, const Layer& layer, Rect clip,
                           const std::vector<LayerBoundsOverride>* overrides,
                           bool throw_on_unsupported_pixel_format, StyleMaskProvider* masks = nullptr,
                           const CompositeSnapshot* blend_if_backdrop_override = nullptr,
                           const PatternStore* patterns = nullptr) {
  if (!layer_visible_for_render(layer, overrides) || layer.opacity() <= 0.0F || layer.kind() != LayerKind::Pixel) {
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
  const auto draw_rect = intersect_rect(clip, bounds);
  const auto has_blend_if = layer_has_rendered_blend_if(layer);
  const auto blend_if = has_blend_if ? layer.blend_if() : LayerBlendIf{};
  const auto has_underlying_blend_if = has_blend_if && blend_if_has_underlying_ranges(blend_if);
  std::optional<CompositeSnapshot> owned_blend_if_backdrop;
  const CompositeSnapshot* blend_if_backdrop = blend_if_backdrop_override;
  if (has_underlying_blend_if && blend_if_backdrop == nullptr && !draw_rect.empty()) {
    owned_blend_if_backdrop.emplace(destination, draw_rect);
    blend_if_backdrop = &*owned_blend_if_backdrop;
  }
  if (style.effects_visible) {
    for (std::uint32_t index = 0; index < style.drop_shadows.size(); ++index) {
      const auto& shadow = style.drop_shadows[index];
      profile_compositor_step(destination, layer, "drop_shadow", clip, [&] {
        render_drop_shadow(destination, layer, source, clip, bounds, shadow, layer_mask_bounds, masks, index);
      });
    }
    for (std::uint32_t index = 0; index < style.outer_glows.size(); ++index) {
      const auto& glow = style.outer_glows[index];
      profile_compositor_step(destination, layer, "outer_glow", clip, [&] {
        render_outer_glow(destination, layer, source, clip, bounds, glow, layer_mask_bounds, masks, index);
      });
    }
  }

  std::vector<PreparedSatin> prepared_satins;
  if (!draw_rect.empty() && style.effects_visible) {
    prepared_satins.reserve(style.satins.size());
    for (std::uint32_t index = 0; index < style.satins.size(); ++index) {
      const auto& satin = style.satins[index];
      if (!satin.enabled || satin.opacity <= 0.0F) {
        continue;
      }
      profile_compositor_step(destination, layer, "satin", clip, [&] {
        prepared_satins.push_back(
            prepare_satin(layer, source, draw_rect, bounds, satin, layer_mask_bounds, masks, index));
      });
    }
  }
  if (!draw_rect.empty()) {
    profile_compositor_step(destination, layer, "base_pixels", draw_rect, [&] {
      const auto format = source.format();
      const auto channels = format.channels;
      const auto* source_bytes = source.data().data();
      const auto source_stride = source.stride_bytes();
      const auto has_enabled_mask = layer.mask().has_value() && !layer.mask()->disabled;
      bool composited_by_target = false;
      if (!has_blend_if && !has_enabled_mask && prepared_satins.empty() &&
          layer.blend_mode() == BlendMode::Normal) {
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
            auto alpha = source_alpha * layer_mask_alpha_for_render(layer, x, y, layer_mask_bounds) *
                         layer.opacity();
            if (alpha <= 0.0F) {
              continue;
            }

            if constexpr (requires { destination.record_clip_coverage(x, y, alpha); }) {
              destination.record_clip_coverage(x, y, alpha);
            }

            const auto source_color = RgbColor{src[0], src[1], src[2]};
            if (has_blend_if) {
              alpha *= blend_if_source_alpha_factor(blend_if, source_color);
              if (has_underlying_blend_if) {
                alpha *= blend_if_underlying_alpha_factor(blend_if, blend_if_backdrop->sample_color(x, y));
              }
              if (alpha <= 0.0F) {
                continue;
              }
            }

            std::array<std::uint8_t, 3> styled_color{src[0], src[1], src[2]};
            if (!has_blend_if) {
              for (const auto& prepared : prepared_satins) {
                const auto mask_index =
                    static_cast<std::size_t>(y - prepared.mask_bounds.y) *
                        static_cast<std::size_t>(prepared.mask_bounds.width) +
                    static_cast<std::size_t>(x - prepared.mask_bounds.x);
                const auto coverage =
                    prepared.entry->primary[mask_index] * clamp_unit(prepared.effect->opacity);
                if (coverage <= 0.0F) {
                  continue;
                }
                const auto& color = prepared.effect->color;
                styled_color = composite_blended_rgb({color.red, color.green, color.blue}, styled_color,
                                                      prepared.effect->blend_mode, coverage, 1.0F);
              }
            }
            destination.composite_color(x, y, RgbColor{styled_color[0], styled_color[1], styled_color[2]}, alpha,
                                        layer.blend_mode());
          }
        }
      }
    });
  }

  // Satin is normally folded into the base color to preserve Patchy's
  // established identity-path bytes. Photoshop does not gate layer effects
  // with Blend If, however, so a Blend-If layer renders Satin as its own
  // interior effect using the original (ungated) layer matte.
  if (has_blend_if && !draw_rect.empty() && !prepared_satins.empty()) {
    profile_compositor_step(destination, layer, "satin_effect", draw_rect, [&] {
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
          const auto source_alpha =
              (channels >= 4 ? static_cast<float>(src[3]) / 255.0F : 1.0F) *
              layer_mask_alpha_for_render(layer, x, y, layer_mask_bounds) * layer.opacity();
          if (source_alpha <= 0.0F) {
            continue;
          }
          for (const auto& prepared : prepared_satins) {
            const auto mask_index =
                static_cast<std::size_t>(y - prepared.mask_bounds.y) *
                    static_cast<std::size_t>(prepared.mask_bounds.width) +
                static_cast<std::size_t>(x - prepared.mask_bounds.x);
            const auto alpha =
                source_alpha * prepared.entry->primary[mask_index] * clamp_unit(prepared.effect->opacity);
            if (alpha > 0.0F) {
              destination.composite_color(x, y, prepared.effect->color, alpha, prepared.effect->blend_mode);
            }
          }
        }
      }
    });
  }

  if (style.effects_visible) {
    for (std::uint32_t index = 0; index < style.inner_shadows.size(); ++index) {
      const auto& shadow = style.inner_shadows[index];
      profile_compositor_step(destination, layer, "inner_shadow", clip, [&] {
        render_inner_shadow(destination, layer, source, clip, bounds, shadow, layer_mask_bounds, masks, index);
      });
    }
    for (std::uint32_t index = 0; index < style.inner_glows.size(); ++index) {
      const auto& glow = style.inner_glows[index];
      profile_compositor_step(destination, layer, "inner_glow", clip, [&] {
        render_inner_glow(destination, layer, source, clip, bounds, glow, layer_mask_bounds, masks, index);
      });
    }
    // Overlay stacking pinned against Photoshop 2026 (pairwise 100%-opacity
    // probes): pattern under gradient under color, i.e. Color Overlay paints
    // last. The historical color-then-gradient order was inverted vs PS.
    for (const auto& overlay : style.pattern_overlays) {
      profile_compositor_step(destination, layer, "pattern_overlay", clip, [&] {
        render_pattern_overlay(destination, layer, source, clip, bounds, overlay, layer_mask_bounds, patterns);
      });
    }
    for (const auto& fill : style.gradient_fills) {
      profile_compositor_step(destination, layer, "gradient_fill", clip, [&] {
        render_gradient_fill(destination, layer, source, clip, bounds, fill, layer_mask_bounds);
      });
    }
    for (const auto& overlay : style.color_overlays) {
      profile_compositor_step(destination, layer, "color_overlay", clip, [&] {
        render_color_overlay(destination, layer, source, clip, bounds, overlay, layer_mask_bounds);
      });
    }
    for (std::uint32_t index = 0; index < style.bevels.size(); ++index) {
      const auto& bevel = style.bevels[index];
      if (bevel.style == BevelEmbossStyleKind::StrokeEmboss) {
        continue;
      }
      profile_compositor_step(destination, layer, "bevel_emboss", clip, [&] {
        render_bevel_emboss(destination, layer, source, clip, bounds, bevel, layer_mask_bounds, masks, index,
                            patterns, &style.strokes);
      });
    }
    for (std::uint32_t index = 0; index < style.strokes.size(); ++index) {
      const auto& stroke = style.strokes[index];
      profile_compositor_step(destination, layer, "stroke", clip, [&] {
        render_stroke(destination, layer, source, clip, bounds, stroke, layer_mask_bounds, masks, index);
      });
    }
    // Stroke Emboss shades the rendered Stroke effect itself, so it must paint
    // after the stroke base instead of being covered by it.
    for (std::uint32_t index = 0; index < style.bevels.size(); ++index) {
      const auto& bevel = style.bevels[index];
      if (bevel.style != BevelEmbossStyleKind::StrokeEmboss) {
        continue;
      }
      profile_compositor_step(destination, layer, "stroke_emboss", clip, [&] {
        render_bevel_emboss(destination, layer, source, clip, bounds, bevel, layer_mask_bounds, masks, index,
                            patterns, &style.strokes);
      });
    }
  }
}

[[nodiscard]] inline bool layer_clipped_for_render(const Layer& layer) noexcept {
  // Groups can never be clipped (Photoshop's rule); defensive against stray flags.
  return layer.clipped() && layer.kind() != LayerKind::Group;
}

[[nodiscard]] inline bool layer_is_clip_base(const Layer& layer) noexcept {
  // Only composited-content layers host a clipping group; a clipped run above a
  // group or adjustment layer renders unclipped (defensive).
  return layer.kind() == LayerKind::Pixel;
}

// Isolated buffer for one Photoshop clipping group. The base layer composites
// in normally, then freeze_clip() locks the clipping shape. Identity layers
// preserve Patchy's historical accumulated-alpha shape; Blend-If bases use a
// separately recorded original content/mask coverage so gating the base does
// not hide clipped members. Clipped members blend against the base's COLOR at
// full strength
// (destination alpha 1 - Photoshop's default "Blend Clipped Layers as Group"
// semantics) without growing coverage, and a clipped adjustment layer's
// adjust_color touches only masked pixels. merge_into() then lays the ensemble
// into the real destination with the base's blend mode; the base's own opacity
// is already folded into the frozen alpha, so the group fades as a unit.
class IsolatedClipGroupTarget {
public:
  explicit IsolatedClipGroupTarget(Rect rect, bool use_original_clip_coverage = false)
      : rect_(rect),
        rgb_(static_cast<std::size_t>(std::max(0, rect.width)) * static_cast<std::size_t>(std::max(0, rect.height)) *
                 3U,
             0),
        alpha_(static_cast<std::size_t>(std::max(0, rect.width)) * static_cast<std::size_t>(std::max(0, rect.height)),
               0.0F),
        clip_alpha_(alpha_.size(), 0.0F),
        use_original_clip_coverage_(use_original_clip_coverage) {}

  void composite_color(std::int32_t x, std::int32_t y, RgbColor color, float alpha, BlendMode mode) {
    alpha = clamp_unit(alpha);
    x -= rect_.x;
    y -= rect_.y;
    if (alpha <= 0.0F || x < 0 || y < 0 || x >= rect_.width || y >= rect_.height) {
      return;
    }
    const auto index =
        static_cast<std::size_t>(y) * static_cast<std::size_t>(rect_.width) + static_cast<std::size_t>(x);
    auto& destination_alpha = alpha_[index];
    const auto clip_alpha = clip_alpha_[index];
    if (frozen_ && clip_alpha <= 0.0F) {
      return;  // outside the clip mask
    }
    auto* dst = rgb_.data() + index * 3U;
    const std::array<std::uint8_t, 3> src_rgb{color.red, color.green, color.blue};
    const std::array<std::uint8_t, 3> dst_rgb{dst[0], dst[1], dst[2]};
    const auto blended = composite_blended_rgb(src_rgb, dst_rgb, mode, alpha, frozen_ ? 1.0F : destination_alpha);
    for (int channel = 0; channel < 3; ++channel) {
      dst[channel] = blended[static_cast<std::size_t>(channel)];
    }
    if (frozen_) {
      // Clipped members paint at full color strength inside the original base
      // matte, but can restore output coverage that the base's Blend If hid.
      const auto normalized_destination_alpha =
          clip_alpha > 0.0F ? std::min(destination_alpha, clip_alpha) / clip_alpha : 0.0F;
      const auto clipped_output_alpha =
          clip_alpha * (alpha + normalized_destination_alpha * (1.0F - alpha));
      destination_alpha = std::max(destination_alpha, clipped_output_alpha);
    } else {
      destination_alpha = alpha + destination_alpha * (1.0F - alpha);
    }
  }

  [[nodiscard]] CompositeSample sample_color(std::int32_t x, std::int32_t y) const noexcept {
    x -= rect_.x;
    y -= rect_.y;
    if (x < 0 || y < 0 || x >= rect_.width || y >= rect_.height) {
      return {};
    }
    const auto index =
        static_cast<std::size_t>(y) * static_cast<std::size_t>(rect_.width) + static_cast<std::size_t>(x);
    const auto* rgb = rgb_.data() + index * 3U;
    return CompositeSample{RgbColor{rgb[0], rgb[1], rgb[2]}, alpha_[index]};
  }

  void record_clip_coverage(std::int32_t x, std::int32_t y, float alpha) noexcept {
    if (frozen_ || !use_original_clip_coverage_) {
      return;
    }
    x -= rect_.x;
    y -= rect_.y;
    if (x < 0 || y < 0 || x >= rect_.width || y >= rect_.height) {
      return;
    }
    const auto index =
        static_cast<std::size_t>(y) * static_cast<std::size_t>(rect_.width) + static_cast<std::size_t>(x);
    clip_alpha_[index] = std::max(clip_alpha_[index], clamp_unit(alpha));
  }

  void adjust_color(std::int32_t x, std::int32_t y, const AdjustmentSettings& settings, float amount) {
    amount = clamp_unit(amount);
    x -= rect_.x;
    y -= rect_.y;
    if (amount <= 0.0F || x < 0 || y < 0 || x >= rect_.width || y >= rect_.height) {
      return;
    }
    const auto index =
        static_cast<std::size_t>(y) * static_cast<std::size_t>(rect_.width) + static_cast<std::size_t>(x);
    if (alpha_[index] <= 0.0F) {
      return;
    }
    auto* dst = rgb_.data() + index * 3U;
    const auto adjusted = apply_adjustment_to_color(RgbColor{dst[0], dst[1], dst[2]}, settings);
    dst[0] = clamp_byte(static_cast<float>(adjusted.red) * amount + static_cast<float>(dst[0]) * (1.0F - amount));
    dst[1] = clamp_byte(static_cast<float>(adjusted.green) * amount + static_cast<float>(dst[1]) * (1.0F - amount));
    dst[2] = clamp_byte(static_cast<float>(adjusted.blue) * amount + static_cast<float>(dst[2]) * (1.0F - amount));
  }

  void adjust_color(std::int32_t x, std::int32_t y, const AdjustmentLut& lut, float amount) {
    amount = clamp_unit(amount);
    x -= rect_.x;
    y -= rect_.y;
    if (amount <= 0.0F || x < 0 || y < 0 || x >= rect_.width || y >= rect_.height) {
      return;
    }
    const auto index =
        static_cast<std::size_t>(y) * static_cast<std::size_t>(rect_.width) + static_cast<std::size_t>(x);
    if (alpha_[index] <= 0.0F) {
      return;
    }
    auto* dst = rgb_.data() + index * 3U;
    dst[0] = clamp_byte(static_cast<float>(lut.red[dst[0]]) * amount + static_cast<float>(dst[0]) * (1.0F - amount));
    dst[1] =
        clamp_byte(static_cast<float>(lut.green[dst[1]]) * amount + static_cast<float>(dst[1]) * (1.0F - amount));
    dst[2] = clamp_byte(static_cast<float>(lut.blue[dst[2]]) * amount + static_cast<float>(dst[2]) * (1.0F - amount));
  }

  void freeze_clip() noexcept {
    if (!use_original_clip_coverage_) {
      // Preserve the historical/default path byte for byte: before Blend If,
      // Patchy deliberately let base-layer styles contribute to the frozen
      // clipping shape. Only Blend-If bases need Photoshop's original matte.
      clip_alpha_ = alpha_;
    }
    frozen_ = true;
  }

  template <typename Target>
  void merge_into(Target& destination, BlendMode mode) const {
    for (std::int32_t y = 0; y < rect_.height; ++y) {
      for (std::int32_t x = 0; x < rect_.width; ++x) {
        const auto index =
            static_cast<std::size_t>(y) * static_cast<std::size_t>(rect_.width) + static_cast<std::size_t>(x);
        const auto alpha = alpha_[index];
        if (alpha <= 0.0F) {
          continue;
        }
        const auto* px = rgb_.data() + index * 3U;
        destination.composite_color(rect_.x + x, rect_.y + y, RgbColor{px[0], px[1], px[2]}, alpha, mode);
      }
    }
  }

  template <typename Target>
  void merge_layer_into(Target& destination, const Layer& layer, const LayerBlendIf& blend_if,
                        const CompositeSnapshot* backdrop, std::optional<Rect> layer_mask_bounds) const {
    const auto mode = layer.blend_mode() == BlendMode::PassThrough ? BlendMode::Normal : layer.blend_mode();
    const auto has_underlying_blend_if = blend_if_has_underlying_ranges(blend_if);
    for (std::int32_t y = 0; y < rect_.height; ++y) {
      for (std::int32_t x = 0; x < rect_.width; ++x) {
        const auto index =
            static_cast<std::size_t>(y) * static_cast<std::size_t>(rect_.width) + static_cast<std::size_t>(x);
        auto alpha = alpha_[index] * layer.opacity() *
                     layer_mask_alpha_for_render(layer, rect_.x + x, rect_.y + y, layer_mask_bounds);
        if (alpha <= 0.0F) {
          continue;
        }
        const auto* px = rgb_.data() + index * 3U;
        const auto color = RgbColor{px[0], px[1], px[2]};
        alpha *= blend_if_source_alpha_factor(blend_if, color);
        if (has_underlying_blend_if) {
          alpha *= blend_if_underlying_alpha_factor(
              blend_if, backdrop->sample_color(rect_.x + x, rect_.y + y));
        }
        if (alpha > 0.0F) {
          destination.composite_color(rect_.x + x, rect_.y + y, color, alpha, mode);
        }
      }
    }
  }

private:
  Rect rect_{};
  std::vector<std::uint8_t> rgb_;
  std::vector<float> alpha_;
  std::vector<float> clip_alpha_;
  bool use_original_clip_coverage_{false};
  bool frozen_{false};
};

// Composite one sibling list, folding Photoshop clipping groups: a base layer
// plus the consecutive clipped() siblings above it composite into an isolated
// buffer and merge with the base's blend mode. composite_one renders a single
// non-run layer, so the UI path keeps its cached-style fast path for the common
// unclipped case.
template <typename Target, typename CompositeOne>
void composite_sibling_layers(Target& destination, const std::vector<Layer>& siblings, Rect clip,
                              const std::vector<LayerBoundsOverride>* overrides,
                              bool throw_on_unsupported_pixel_format, StyleMaskProvider* masks,
                              CompositeOne&& composite_one, const PatternStore* patterns = nullptr) {
  std::size_t index = 0;
  while (index < siblings.size()) {
    const Layer& layer = siblings[index];
    std::size_t run_end = index + 1;
    if (!layer_clipped_for_render(layer)) {
      // Only an unclipped layer can start a run; an orphaned clipped layer at
      // the bottom of a sibling list falls through and renders unclipped.
      while (run_end < siblings.size() && layer_clipped_for_render(siblings[run_end])) {
        ++run_end;
      }
    }
    if (run_end == index + 1 || !layer_is_clip_base(layer)) {
      composite_one(destination, layer);
      ++index;
      continue;
    }
    // Photoshop: a hidden or zero-opacity base hides the whole clipping group.
    if (!layer_visible_for_render(layer, overrides) || layer.opacity() <= 0.0F) {
      index = run_end;
      continue;
    }
    const auto group_rect =
        intersect_rect(clip, layer_bounds_with_effects(layer, layer_bounds_for_render(layer, overrides)));
    if (group_rect.empty()) {
      index = run_end;
      continue;
    }
    std::optional<CompositeSnapshot> base_backdrop;
    if (layer_has_rendered_underlying_blend_if(layer)) {
      base_backdrop.emplace(destination, group_rect);
    }
    IsolatedClipGroupTarget group(group_rect, layer_has_rendered_blend_if(layer));
    composite_layer(group, layer, group_rect, overrides, throw_on_unsupported_pixel_format, masks,
                    base_backdrop.has_value() ? &*base_backdrop : nullptr, patterns);
    group.freeze_clip();
    for (std::size_t member = index + 1; member < run_end; ++member) {
      composite_layer(group, siblings[member], group_rect, overrides, throw_on_unsupported_pixel_format, masks,
                      nullptr, patterns);
    }
    group.merge_into(destination, layer.blend_mode());
    index = run_end;
  }
}

template <typename Target>
void composite_layers(Target& destination, const std::vector<Layer>& layers, Rect clip,
                      const std::vector<LayerBoundsOverride>* overrides = nullptr,
                      bool throw_on_unsupported_pixel_format = false, StyleMaskProvider* masks = nullptr,
                      const PatternStore* patterns = nullptr) {
  composite_sibling_layers(
      destination, layers, clip, overrides, throw_on_unsupported_pixel_format, masks,
      [&](Target& target, const Layer& layer) {
        composite_layer(target, layer, clip, overrides, throw_on_unsupported_pixel_format, masks, nullptr,
                        patterns);
      },
      patterns);
}

template <typename Target>
void composite_layer(Target& destination, const Layer& layer, Rect clip,
                     const std::vector<LayerBoundsOverride>* overrides,
                     bool throw_on_unsupported_pixel_format, StyleMaskProvider* masks,
                     const CompositeSnapshot* blend_if_backdrop, const PatternStore* patterns) {
  if (!layer_visible_for_render(layer, overrides) || layer.opacity() <= 0.0F) {
    return;
  }

  if (layer.kind() == LayerKind::Group) {
    if (layer_has_rendered_blend_if(layer)) {
      const auto blend_if = layer.blend_if();
      std::optional<CompositeSnapshot> backdrop;
      if (blend_if_has_underlying_ranges(blend_if)) {
        backdrop.emplace(destination, clip);
      }
      IsolatedClipGroupTarget isolated(clip);
      composite_layers(isolated, layer.children(), clip, overrides, throw_on_unsupported_pixel_format, masks,
                       patterns);
      isolated.merge_layer_into(destination, layer, blend_if, backdrop.has_value() ? &*backdrop : nullptr,
                                layer_mask_bounds_for_render(layer, overrides));
      return;
    }
    composite_layers(destination, layer.children(), clip, overrides, throw_on_unsupported_pixel_format, masks,
                     patterns);
    return;
  }

  if (layer.kind() == LayerKind::Adjustment) {
    composite_adjustment_layer(destination, layer, clip, overrides);
    return;
  }

  composite_pixel_layer(destination, layer, clip, overrides, throw_on_unsupported_pixel_format, masks,
                        blend_if_backdrop, patterns);
}

}  // namespace patchy::render_detail
