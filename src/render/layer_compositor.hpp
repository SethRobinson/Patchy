#pragma once

#include "core/adjustment_layer.hpp"
#include "core/blend_math.hpp"
#include "core/layer_render_utils.hpp"
#include "core/pattern_resource.hpp"
#include "core/pattern_sampler.hpp"
#include "core/style_contour.hpp"

#include "render/layer_style_mask_ops.hpp"

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
  return (layer.layer_style().layer_mask_hides_effects && layer.mask().has_value() &&
          !layer.mask()->disabled) ||
         layer_vector_mask_hides_effects(layer);
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

inline std::vector<float> stroke_alpha_mask(const PixelBuffer& source, const Layer& layer, Rect bounds,
                                            Rect mask_bounds, float size, LayerStrokePosition position,
                                            std::optional<Rect> layer_mask_bounds, bool mask_shapes_source);

// Photoshop composites layer-effect planes with the effect's alpha FOLDED into
// the source color toward white for the burn modes, then blends at full
// coverage (COM-probed July 2026 over a gray backdrop: a 50%-opacity black
// linearBurn shadow subtracts an absolute 255 x alpha from every channel and
// colorBurn matches the folded curve exactly, both refuting the plain
// lerp(b, blend(b, color), alpha) model; Normal, Multiply, and Darken keep the
// plain model, and the affine modes — Multiply, Screen, LinearDodge — are
// algebraically identical either way). Every effect draw goes through here;
// layer PIXEL compositing keeps the standard model.
template <typename Target>
inline void composite_effect_color(Target& destination, std::int32_t x, std::int32_t y, RgbColor color,
                                   float alpha, BlendMode mode) {
  if (mode == BlendMode::LinearBurn || mode == BlendMode::ColorBurn) {
    const auto fold = [alpha](std::uint8_t channel) {
      return static_cast<std::uint8_t>(
          std::clamp<long>(std::lround(255.0F - (255.0F - static_cast<float>(channel)) * alpha), 0L, 255L));
    };
    destination.composite_color(x, y, RgbColor{fold(color.red), fold(color.green), fold(color.blue)},
                                alpha > 0.0F ? 1.0F : 0.0F, mode);
    return;
  }
  destination.composite_color(x, y, color, alpha, mode);
}

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
  const auto source_bounds = layer_visible_alpha_bounds(layer, source, bounds);
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
      composite_effect_color(destination, x, y, shadow.color, alpha, shadow.blend_mode);
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
  const auto source_bounds = layer_visible_alpha_bounds(layer, source, bounds);
  if (!source_bounds.has_value()) {
    return;
  }
  const auto radius = layer_style_falloff_radius(glow.size);
  const auto effect_bounds = outset_rect(*source_bounds, radius + 2);
  const auto draw_rect = intersect_rect(clip, effect_bounds);
  if (draw_rect.empty()) {
    return;
  }

  // Softer (Photoshop's default technique) expands by the integer spread radius
  // and blurs with Satin's exact tent kernel (COM-calibrated; see
  // prepare_outer_glow_softer_mask), so a thin stroke's glow peaks well below
  // full opacity. Precise keeps the distance-field falloff.
  const auto softer = glow.technique == LayerGlowTechnique::Softer;
  const auto legacy_mask_bounds = clipped_mask_bounds(effect_bounds, draw_rect, radius + (softer ? 2 : 1));
  const auto [entry, mask_bounds] = style_mask_for_render(
      masks, layer, StyleMaskKind::OuterGlow, effect_index, effect_bounds, effect_bounds, legacy_mask_bounds,
      bounds, layer_mask_bounds, [&](Rect domain) {
        StyleMaskEntry computed;
        auto base = layer_alpha_mask(source, layer, bounds, domain, 0, 0, layer_mask_bounds);
        if (softer) {
          prepare_outer_glow_softer_mask(base, domain.width, domain.height, glow.size, glow.spread, glow.range);
          computed.primary = std::move(base);
        } else {
          computed.primary = distance_falloff_mask(base, domain.width, domain.height, glow.size, glow.spread);
        }
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
      composite_effect_color(destination, x, y, glow.color, glow_alpha, glow.blend_mode);
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
      composite_effect_color(destination, x, y, shadow.color, shadow_alpha, shadow.blend_mode);
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
      composite_effect_color(destination, x, y, glow.color, glow_alpha, glow.blend_mode);
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
      composite_effect_color(destination, x, y, overlay.color, source_alpha * overlay.opacity * layer.opacity(),
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
  const auto gradient_bounds = fill.gradient.align_with_layer
                                   ? layer_visible_alpha_bounds(layer, source, bounds).value_or(bounds)
                                   : bounds;
  for (std::int32_t y = draw_rect.y; y < draw_rect.y + draw_rect.height; ++y) {
    for (std::int32_t x = draw_rect.x; x < draw_rect.x + draw_rect.width; ++x) {
      const auto source_alpha =
          source_mask[static_cast<std::size_t>((y - draw_rect.y) * source_mask_width + (x - draw_rect.x))];
      if (source_alpha <= 0.0F) {
        continue;
      }
      const auto position = gradient_position(fill.gradient, gradient_bounds, x, y);
      const auto alpha = source_alpha * fill.opacity * layer.opacity() * gradient_stop_opacity(fill.gradient, position);
      composite_effect_color(destination, x, y, gradient_color_dithered(fill.gradient, position, x, y), alpha,
                             fill.blend_mode);
    }
  }
}

// PatternSampleRgba and PatternTileSampler moved (verbatim) to
// core/pattern_sampler.hpp so the vector shape rasterizer shares the
// PS-calibrated sampling rules; patchy:: is found through the enclosing
// namespace.

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
      composite_effect_color(destination, x, y, sample.color, alpha, overlay.blend_mode);
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
          const auto mask_shapes_source = !layer.layer_style().layer_mask_hides_effects;
          const auto clip_to_mask = layer_mask_clips_effect_output(layer);
          const auto has_aligned_gradient =
              std::any_of(strokes->begin(), strokes->end(), [](const LayerStroke& stroke) {
                return stroke.enabled && stroke.opacity > 0.0F && stroke.size > 0.0F && stroke.uses_gradient &&
                       stroke.gradient.align_with_layer;
              });
          const auto aligned_gradient_bounds = has_aligned_gradient
                                                   ? layer_visible_alpha_bounds(layer, source, bounds).value_or(bounds)
                                                   : bounds;
          for (const auto& stroke : *strokes) {
            if (!stroke.enabled || stroke.opacity <= 0.0F || stroke.size <= 0.0F) {
              continue;
            }
            const auto stroke_mask = stroke_alpha_mask(source, layer, bounds, domain, stroke.size, stroke.position,
                                                       layer_mask_bounds, mask_shapes_source);
            const auto stroke_radius = std::max(1, static_cast<int>(std::ceil(stroke.size)));
            const auto stroke_effect_bounds = stroke.position == LayerStrokePosition::Inside
                                                  ? bounds
                                                  : outset_rect(bounds, stroke_radius + 1);
            const auto stroke_gradient_bounds = stroke.uses_gradient && stroke.gradient.align_with_layer
                                                    ? aligned_gradient_bounds
                                                    : stroke_effect_bounds;
            for (std::int32_t local_y = 0; local_y < domain.height; ++local_y) {
              for (std::int32_t local_x = 0; local_x < domain.width; ++local_x) {
                const auto index = static_cast<std::size_t>(local_y) * domain.width + local_x;
                auto alpha = stroke_mask[index] * clamp_unit(stroke.opacity);
                if (alpha > 0.0F && stroke.uses_gradient) {
                  const auto position = gradient_position(stroke.gradient, stroke_gradient_bounds,
                                                          domain.x + local_x, domain.y + local_y);
                  alpha *= gradient_stop_opacity(stroke.gradient, position);
                }
                if (alpha > 0.0F && clip_to_mask) {
                  // "Layer Mask Hides Effects": hide the embossed stroke where it
                  // lands instead of reshaping its contour.
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
        composite_effect_color(destination, x, y, bevel.highlight_color,
                               clamp_unit(lighting) * effect_alpha * bevel.highlight_opacity * layer.opacity(),
                               bevel.highlight_blend_mode);
      } else if (lighting < 0.0F) {
        composite_effect_color(destination, x, y, bevel.shadow_color,
                               clamp_unit(-lighting) * effect_alpha * bevel.shadow_opacity * layer.opacity(),
                               bevel.shadow_blend_mode);
      }
    }
  }
}

inline std::vector<float> stroke_alpha_mask(const PixelBuffer& source, const Layer& layer, Rect bounds,
                                            Rect mask_bounds, float size, LayerStrokePosition position,
                                            std::optional<Rect> layer_mask_bounds, bool mask_shapes_source) {
  // Photoshop derives the stroke from the layer's pixel coverage, treating any painted
  // pixel as inside the shape: the stroke fills the (dilated) binary shape and the
  // layer's own pixels cover it according to their alpha, so semi-transparent fills let
  // it show through. By default the layer mask reshapes that coverage where it is fully
  // black — the contour follows {alpha > 0 AND mask > 0} and the band lands on
  // mask-hidden ground at full strength; with "Layer Mask Hides Effects" the caller
  // passes mask_shapes_source = false, keeps the raw-pixel contour, and hides the output
  // where it lands instead. Fractional (gray/feathered) mask values deliberately keep
  // the raw pixel alpha: Photoshop folds them into the matte with content-knockout
  // compositing (a gray mask washes the whole interior), the same knockout model this
  // renderer already skips for semi-transparent fills (docs/ps-compat.md). Calibrated
  // against Photoshop 2026 COM renders, July 2026: binary-mask band runs match PS
  // run-for-run at every position, including bands over fully-masked ground.
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
        auto alpha = static_cast<float>(pixel[3]) / 255.0F;
        if (mask_shapes_source && alpha > 0.0F &&
            layer_mask_alpha_for_render(layer, x, y, layer_mask_bounds) <= 0.0F) {
          alpha = 0.0F;
        }
        *output++ = alpha;
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
        *output++ = mask_shapes_source && layer_mask_alpha_for_render(layer, x, y, layer_mask_bounds) <= 0.0F
                        ? 0.0F
                        : 1.0F;
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
  const auto mask_shapes_source = !layer.layer_style().layer_mask_hides_effects;
  const auto [entry, mask_bounds] = style_mask_for_render(
      masks, layer, StyleMaskKind::Stroke, effect_index, full_mask_bounds, full_mask_bounds, legacy_mask_bounds,
      bounds, layer_mask_bounds, [&](Rect domain) {
        StyleMaskEntry computed;
        computed.primary = stroke_alpha_mask(source, layer, bounds, domain, stroke.size, stroke.position,
                                             layer_mask_bounds, mask_shapes_source);
        return computed;
      });
  const auto& mask = entry->primary;
  const auto mask_width = mask_bounds.width;
  const auto clip_to_mask = layer_mask_clips_effect_output(layer);
  const auto gradient_bounds = stroke.uses_gradient && stroke.gradient.align_with_layer
                                   ? layer_visible_alpha_bounds(layer, source, bounds).value_or(bounds)
                                   : effect_bounds;
  for (std::int32_t y = draw_rect.y; y < draw_rect.y + draw_rect.height; ++y) {
    for (std::int32_t x = draw_rect.x; x < draw_rect.x + draw_rect.width; ++x) {
      auto mask_alpha = mask[static_cast<std::size_t>((y - mask_bounds.y) * mask_width + (x - mask_bounds.x))];
      if (clip_to_mask && mask_alpha > 0.0F) {
        // "Layer Mask Hides Effects": the mask hides the stroke where it lands
        // instead of reshaping its contour.
        mask_alpha *= layer_mask_alpha_for_render(layer, x, y, layer_mask_bounds);
      }
      if (mask_alpha <= 0.0F) {
        continue;
      }
      auto color = stroke.color;
      auto alpha = mask_alpha * stroke.opacity * layer.opacity();
      if (stroke.uses_gradient) {
        const auto position = gradient_position(stroke.gradient, gradient_bounds, x, y);
        color = gradient_color_dithered(stroke.gradient, position, x, y);
        alpha *= gradient_stop_opacity(stroke.gradient, position);
      }
      composite_effect_color(destination, x, y, color, alpha, stroke.blend_mode);
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
      auto amount = layer_mask_alpha_for_render(layer, x, y, layer_mask_bounds) * layer.opacity() *
                    layer.fill_opacity();
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
      const auto has_enabled_mask = (layer.mask().has_value() && !layer.mask()->disabled) ||
                                    layer_has_enabled_vector_mask(layer);
      bool composited_by_target = false;
      if (!has_blend_if && !has_enabled_mask && prepared_satins.empty() && layer.fill_opacity() == 1.0F &&
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
            const auto source_coverage =
                source_alpha * layer_mask_alpha_for_render(layer, x, y, layer_mask_bounds);
            const auto special_fill = layer.fill_opacity() != 1.0F &&
                                      blend_mode_has_special_fill(layer.blend_mode());
            auto alpha = source_coverage * layer.opacity();
            if (layer.fill_opacity() != 1.0F) {
              alpha *= layer.fill_opacity();
            }
            if (alpha <= 0.0F) {
              continue;
            }

            if constexpr (requires { destination.record_clip_coverage(x, y, alpha); }) {
              destination.record_clip_coverage(x, y, alpha);
            }

            const auto source_color = RgbColor{src[0], src[1], src[2]};
            auto blend_if_factor = 1.0F;
            if (has_blend_if) {
              blend_if_factor *= blend_if_source_alpha_factor(blend_if, source_color);
              if (has_underlying_blend_if) {
                blend_if_factor *=
                    blend_if_underlying_alpha_factor(blend_if, blend_if_backdrop->sample_color(x, y));
              }
              alpha *= blend_if_factor;
              if (alpha <= 0.0F) {
                continue;
              }
            }

            std::array<std::uint8_t, 3> styled_color{src[0], src[1], src[2]};
            if (!has_blend_if && layer.fill_opacity() == 1.0F) {
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
            if (special_fill) {
              destination.composite_special_fill_color(
                  x, y, RgbColor{styled_color[0], styled_color[1], styled_color[2]},
                  source_coverage * blend_if_factor, layer.fill_opacity(), layer.opacity(), layer.blend_mode());
            } else {
              destination.composite_color(x, y, RgbColor{styled_color[0], styled_color[1], styled_color[2]}, alpha,
                                          layer.blend_mode());
            }
          }
        }
      }
    });
  }

  // Satin is normally folded into the base color to preserve Patchy's
  // established identity-path bytes. Photoshop does not gate layer effects
  // with Blend If, however, so a Blend-If layer renders Satin as its own
  // interior effect using the original (ungated) layer matte.
  if ((has_blend_if || layer.fill_opacity() != 1.0F) && !draw_rect.empty() && !prepared_satins.empty()) {
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
              composite_effect_color(destination, x, y, prepared.effect->color, alpha, prepared.effect->blend_mode);
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
    // Interior overlays on a stroked shape layer apply to the FILL plane and
    // the vector stroke re-composites above them (PS 2026 probes
    // fx-sofi-center/outside, docs/vector-tools.md). A stroke-only shape's
    // overlay covers the stroke, which the legacy combined path already
    // renders. The split planes come from the shape bake; when absent or
    // mismatched (no stroke, non-Normal stroke blend, transform-preview
    // override, preserved import raster) the legacy behavior stands. Blend-If
    // layers keep the legacy path too - the re-stamp cannot reproduce the
    // per-pixel gate.
    const PixelBuffer* interior_source = &source;
    const PixelBuffer* stroke_restamp = nullptr;
    if (!has_blend_if) {
      if (const auto* shape = layer.vector_shape();
          shape != nullptr && !shape->stroke_cache.empty() && !shape->fill_cache.empty() &&
          &source == &layer.pixels() && shape->fill_cache.width() == source.width() &&
          shape->fill_cache.height() == source.height() &&
          shape->stroke_cache.width() == source.width() &&
          shape->stroke_cache.height() == source.height()) {
        // Gate on an overlay that will actually paint: a needless re-stamp
        // would double-composite the stroke's AA edges.
        const auto overlay_paints = [](const auto& overlays) {
          return std::any_of(overlays.begin(), overlays.end(), [](const auto& overlay) {
            return overlay.enabled && overlay.opacity > 0.0F;
          });
        };
        if (overlay_paints(style.pattern_overlays) || overlay_paints(style.gradient_fills) ||
            overlay_paints(style.color_overlays)) {
          interior_source = &shape->fill_cache;
          stroke_restamp = &shape->stroke_cache;
        }
      }
    }
    // Overlay stacking pinned against Photoshop 2026 (pairwise 100%-opacity
    // probes): pattern under gradient under color, i.e. Color Overlay paints
    // last. The historical color-then-gradient order was inverted vs PS.
    for (const auto& overlay : style.pattern_overlays) {
      profile_compositor_step(destination, layer, "pattern_overlay", clip, [&] {
        render_pattern_overlay(destination, layer, *interior_source, clip, bounds, overlay,
                               layer_mask_bounds, patterns);
      });
    }
    for (const auto& fill : style.gradient_fills) {
      profile_compositor_step(destination, layer, "gradient_fill", clip, [&] {
        render_gradient_fill(destination, layer, *interior_source, clip, bounds, fill, layer_mask_bounds);
      });
    }
    for (const auto& overlay : style.color_overlays) {
      profile_compositor_step(destination, layer, "color_overlay", clip, [&] {
        render_color_overlay(destination, layer, *interior_source, clip, bounds, overlay, layer_mask_bounds);
      });
    }
    if (stroke_restamp != nullptr && !draw_rect.empty()) {
      // The vector stroke re-composites above the interior overlays with the
      // base pass's factors (mask, opacity, fill opacity, layer blend); satin
      // folding and clip-coverage recording stay with the base pass.
      profile_compositor_step(destination, layer, "vector_stroke_over_overlays", draw_rect, [&] {
        const auto* stroke_bytes = stroke_restamp->data().data();
        const auto stroke_stride = stroke_restamp->stride_bytes();
        for (std::int32_t y = draw_rect.y; y < draw_rect.y + draw_rect.height; ++y) {
          const auto sy = y - bounds.y;
          const auto* stroke_row = stroke_bytes + static_cast<std::size_t>(sy) * stroke_stride;
          for (std::int32_t x = draw_rect.x; x < draw_rect.x + draw_rect.width; ++x) {
            const auto sx = x - bounds.x;
            const auto* px = stroke_row + static_cast<std::size_t>(sx) * 4;
            const auto source_alpha = static_cast<float>(px[3]) / 255.0F;
            if (source_alpha <= 0.0F) {
              continue;
            }
            const auto source_coverage =
                source_alpha * layer_mask_alpha_for_render(layer, x, y, layer_mask_bounds);
            const auto special_fill = layer.fill_opacity() != 1.0F &&
                                      blend_mode_has_special_fill(layer.blend_mode());
            auto alpha = source_coverage * layer.opacity();
            if (layer.fill_opacity() != 1.0F) {
              alpha *= layer.fill_opacity();
            }
            if (alpha <= 0.0F) {
              continue;
            }
            const auto color = RgbColor{px[0], px[1], px[2]};
            if (special_fill) {
              destination.composite_special_fill_color(x, y, color, source_coverage,
                                                       layer.fill_opacity(), layer.opacity(),
                                                       layer.blend_mode());
            } else {
              destination.composite_color(x, y, color, alpha, layer.blend_mode());
            }
          }
        }
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

  void composite_special_fill_color(std::int32_t x, std::int32_t y, RgbColor color,
                                    float source_coverage, float fill_opacity, float layer_opacity,
                                    BlendMode mode) {
    source_coverage = clamp_unit(source_coverage);
    const auto effective_alpha = source_coverage * clamp_unit(fill_opacity) * clamp_unit(layer_opacity);
    x -= rect_.x;
    y -= rect_.y;
    if (effective_alpha <= 0.0F || x < 0 || y < 0 || x >= rect_.width || y >= rect_.height) {
      return;
    }
    const auto index =
        static_cast<std::size_t>(y) * static_cast<std::size_t>(rect_.width) + static_cast<std::size_t>(x);
    auto& destination_alpha = alpha_[index];
    const auto clip_alpha = clip_alpha_[index];
    if (frozen_ && clip_alpha <= 0.0F) {
      return;
    }
    auto* dst = rgb_.data() + index * 3U;
    const auto result = composite_special_fill_rgb(
        {color.red, color.green, color.blue}, {dst[0], dst[1], dst[2]}, mode, source_coverage,
        fill_opacity, layer_opacity, frozen_ ? 1.0F : destination_alpha);
    dst[0] = result.color[0];
    dst[1] = result.color[1];
    dst[2] = result.color[2];
    if (frozen_) {
      const auto normalized_destination_alpha =
          clip_alpha > 0.0F ? std::min(destination_alpha, clip_alpha) / clip_alpha : 0.0F;
      destination_alpha = std::max(
          destination_alpha,
          clip_alpha * (effective_alpha + normalized_destination_alpha * (1.0F - effective_alpha)));
    } else {
      destination_alpha = result.alpha;
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
    IsolatedClipGroupTarget group(
        group_rect, layer_has_rendered_blend_if(layer) || layer.fill_opacity() != 1.0F);
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
