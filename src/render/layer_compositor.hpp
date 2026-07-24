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

// Per-pixel content attenuation from Stroke effects whose Overprint option is
// off (the Photoshop default): the stroke band knocks the layer's own content
// — fill, interior effects, and clipped members — out and blends against the
// layers below (Photoshop 2026 COM probes, July 2026; docs/ps-compat.md).
// Values are the combined factor over the base draw rect; anything outside
// the rect reads 1 (no attenuation).
struct StrokeKnockoutPlane {
  Rect rect{};
  std::vector<float> factor;

  [[nodiscard]] float at(std::int32_t x, std::int32_t y) const {
    if (x < rect.x || y < rect.y || x >= rect.x + rect.width || y >= rect.y + rect.height) {
      return 1.0F;
    }
    return factor[static_cast<std::size_t>(y - rect.y) * static_cast<std::size_t>(rect.width) +
                  static_cast<std::size_t>(x - rect.x)];
  }
};

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
                                            std::optional<Rect> layer_mask_bounds, bool mask_shapes_source,
                                            std::vector<float>* shape_burst_positions = nullptr);

// A Shape Burst stroke gradient's per-pixel band position (from
// stroke_alpha_mask) with the gradient's Reverse applied. Photoshop ignores
// the angle, scale, offset, and alignment controls for this style (COM
// probes, July 2026, photoshop-stroke-shapeburst fixtures).
inline float shape_burst_gradient_position(const LayerStyleGradient& gradient, float band_position) {
  return clamp_unit(gradient.reverse ? 1.0F - band_position : band_position);
}

// The ramp's full span in pixels: size plus the same +1 px reach as the
// coverage band on each side that is a band limit rather than the contour.
inline float shape_burst_ramp_span(float size, LayerStrokePosition position) {
  return position == LayerStrokePosition::Center ? size + 2.0F * kStrokeContourOffset
                                                 : size + kStrokeContourOffset;
}

// Photoshop supersamples the Shape Burst ramp across each pixel's footprint:
// a 1-2-1 tent of the sharp per-center colors reproduces both the title.psd
// silver band's reduced amplitude and the probe fixtures' clamped tail bytes
// (a symmetric filter is invisible on the probes' interior linear ramp, which
// is why the sharp model also matched them mid-band).
inline RgbColor shape_burst_stroke_color(const LayerStyleGradient& gradient, float band_position,
                                         float ramp_span, std::int32_t x, std::int32_t y) {
  const auto step = 1.0F / std::max(1.0F, ramp_span);
  const auto sample = [&](float band) {
    return gradient_color(gradient, shape_burst_gradient_position(gradient, band), true);
  };
  const auto below = sample(band_position - step);
  const auto center = sample(band_position);
  const auto above = sample(band_position + step);
  const auto tent = [](std::uint8_t low, std::uint8_t mid, std::uint8_t high) {
    return static_cast<std::uint8_t>(
        (static_cast<unsigned>(low) + 2U * mid + high + 2U) / 4U);
  };
  const auto color = RgbColor{tent(below.red, center.red, above.red),
                              tent(below.green, center.green, above.green),
                              tent(below.blue, center.blue, above.blue)};
  return apply_gradient_dither(gradient, color, x, y);
}

inline float shape_burst_stroke_opacity(const LayerStyleGradient& gradient, float band_position,
                                        float ramp_span) {
  const auto step = 1.0F / std::max(1.0F, ramp_span);
  const auto sample = [&](float band) {
    return gradient_stop_opacity(gradient, shape_burst_gradient_position(gradient, band), true);
  };
  return 0.25F * (sample(band_position - step) + 2.0F * sample(band_position) +
                  sample(band_position + step));
}

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

  // "Layer Knocks Out Drop Shadow" (layerConceals, PS default on): the layer's
  // transparency shape punches a hole in its own shadow — independent of fill
  // opacity, master opacity, and any stroke knockout (COM probes July 2026:
  // a fill-0 or knocked-out interior shows the pure backdrop, never the
  // shadow). Invisible under fully opaque content, which simply covers the
  // shadow either way.
  std::vector<float> conceal_mask;
  if (shadow.layer_conceals) {
    conceal_mask = layer_alpha_mask(source, layer, bounds, draw_rect, 0, 0, layer_mask_bounds);
  }
  const auto clip_to_mask = layer_mask_clips_effect_output(layer);
  for (std::int32_t y = draw_rect.y; y < draw_rect.y + draw_rect.height; ++y) {
    for (std::int32_t x = draw_rect.x; x < draw_rect.x + draw_rect.width; ++x) {
      auto alpha = mask[static_cast<std::size_t>((y - mask_bounds.y) * width + (x - mask_bounds.x))] *
                   shadow.opacity * layer.opacity();
      if (clip_to_mask) {
        alpha *= layer_mask_alpha_for_render(layer, x, y, layer_mask_bounds);
      }
      if (!conceal_mask.empty()) {
        alpha *= 1.0F - conceal_mask[static_cast<std::size_t>((y - draw_rect.y) * draw_rect.width +
                                                              (x - draw_rect.x))];
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
                         StyleMaskProvider* masks = nullptr, std::uint32_t effect_index = 0,
                         const StrokeKnockoutPlane* knockout = nullptr) {
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
      auto shadow_alpha = source_alpha * falloff_alpha * shadow.opacity * layer.opacity();
      if (knockout != nullptr) {
        shadow_alpha *= knockout->at(x, y);
      }
      composite_effect_color(destination, x, y, shadow.color, shadow_alpha, shadow.blend_mode);
    }
  }
}

template <typename Target>
void render_inner_glow(Target& destination, const Layer& layer, const PixelBuffer& source, Rect clip, Rect bounds,
                       const LayerInnerGlow& glow, std::optional<Rect> layer_mask_bounds,
                       StyleMaskProvider* masks = nullptr, std::uint32_t effect_index = 0,
                       const StrokeKnockoutPlane* knockout = nullptr) {
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
      auto glow_alpha = source_alpha * source_factor * glow.opacity * layer.opacity();
      if (knockout != nullptr) {
        glow_alpha *= knockout->at(x, y);
      }
      composite_effect_color(destination, x, y, glow.color, glow_alpha, glow.blend_mode);
    }
  }
}

template <typename Target>
void render_color_overlay(Target& destination, const Layer& layer, const PixelBuffer& source, Rect clip, Rect bounds,
                          const LayerColorOverlay& overlay, std::optional<Rect> layer_mask_bounds,
                          const StrokeKnockoutPlane* knockout = nullptr) {
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
      auto alpha = source_alpha * overlay.opacity * layer.opacity();
      if (knockout != nullptr) {
        alpha *= knockout->at(x, y);
      }
      composite_effect_color(destination, x, y, overlay.color, alpha, overlay.blend_mode);
    }
  }
}

template <typename Target>
void render_gradient_fill(Target& destination, const Layer& layer, const PixelBuffer& source, Rect clip, Rect bounds,
                          const LayerGradientFill& fill, std::optional<Rect> layer_mask_bounds,
                          const StrokeKnockoutPlane* knockout = nullptr) {
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
      auto alpha = source_alpha * fill.opacity * layer.opacity() * gradient_stop_opacity(fill.gradient, position);
      if (knockout != nullptr) {
        alpha *= knockout->at(x, y);
      }
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
                            std::optional<Rect> layer_mask_bounds, const PatternStore* patterns,
                            const StrokeKnockoutPlane* knockout = nullptr) {
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
      auto alpha = source_alpha * sample.alpha * overlay.opacity * layer.opacity();
      if (knockout != nullptr) {
        alpha *= knockout->at(x, y);
      }
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
  // COM-calibrated smooth-bevel surface (July 2026, photoshop-bevel-smooth
  // fixtures): the normalized height field is `size` pixels deep, so the
  // per-axis central difference (a 2px span) scales by size/2 x Depth. A
  // LINEAR Contour sub-option at Range r multiplies the slope by 100/r — the
  // recovered slope profiles at Range 50 are exactly twice the Range-100 ones,
  // interior included, refuting any height-domain windowing for the linear
  // curve (non-linear curves keep the height remap below).
  auto slope_gain = 1.0F;
  if (bevel.contour.enabled && style_contour_is_linear(bevel.contour.contour)) {
    slope_gain = 1.0F / std::clamp(bevel.contour.range, 0.01F, 1.0F);
  }
  const auto pillow = bevel.style == BevelEmbossStyleKind::PillowEmboss;
  // Pillow and plain Emboss share one calibrated model (COM depth sweeps,
  // photoshop-pillow-emboss and photoshop-emboss-styles fixtures, July 2026):
  // the slope factor is 0.5 x Depth x the HALF-size tent peak, and Depth is
  // FLOORED at 25% — depths 1 through 25 all render identically in Photoshop
  // (title.psd stores 1% and still shades at quarter strength). The existing
  // 10x ceiling matched the depth-1000 probe as-is. Emboss is the same field
  // with one global sign (no interior flip: its square profiles are the
  // pillow interior's, mirrored across the contour on both sides).
  const auto pillow_family =
      pillow || bevel.style == BevelEmbossStyleKind::Emboss;
  const auto normal_scale =
      pillow_family ? 0.5F * std::clamp(bevel.depth, 0.25F, 10.0F) *
                          static_cast<float>(satin_tent_peak(bevel.size * 0.5F)) * slope_gain
                    : 0.5F * std::clamp(bevel.depth, 0.01F, 10.0F) * std::max(1.0F, bevel.size) * slope_gain;
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
            const auto stroke_shape_burst =
                stroke.uses_gradient && stroke.gradient.type == LayerStyleGradientType::ShapeBurst;
            std::vector<float> stroke_band_positions;
            const auto stroke_mask = stroke_alpha_mask(source, layer, bounds, domain, stroke.size, stroke.position,
                                                       layer_mask_bounds, mask_shapes_source,
                                                       stroke_shape_burst ? &stroke_band_positions : nullptr);
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
                  if (stroke_shape_burst) {
                    alpha *= shape_burst_stroke_opacity(
                        stroke.gradient, stroke_band_positions[index],
                        shape_burst_ramp_span(stroke.size, stroke.position));
                  } else {
                    alpha *= gradient_stop_opacity(
                        stroke.gradient, gradient_position(stroke.gradient, stroke_gradient_bounds,
                                                           domain.x + local_x, domain.y + local_y));
                  }
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
        // Pillow Emboss and plain Emboss light the smooth HALF-size ramp
        // directly (the interior lighting handling happens at composite
        // time). The old |2h-1| pillow fold creased the field at the contour,
        // cancelling the central difference exactly where Photoshop's shading
        // peaks, and spread the bands twice as wide as PS's
        // (photoshop-pillow-emboss probes: shading reach is size/2 per side,
        // profile peaks at the contour-adjacent pixels).
        auto height_bevel = bevel;
        if (bevel.style == BevelEmbossStyleKind::PillowEmboss ||
            bevel.style == BevelEmbossStyleKind::Emboss) {
          height_bevel.size = bevel.size * 0.5F;
        }
        computed.primary =
            bevel_technique_height_mask(computed.secondary, domain.width, domain.height, height_bevel);
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
            // slopes shade the whole cell, not just its edges. The amplitude
            // doubled when the July 2026 Lambert calibration halved
            // normal_scale, keeping the bump's height gradients unchanged.
            constexpr float kTextureAmplitude = 6.0F;
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
      const auto base_gradient_x = (left - right) * normal_scale;
      const auto base_gradient_y = (top - bottom) * normal_scale;
      if (layer_mask_clips_effect_output(layer)) {
        effect_alpha *= layer_mask_alpha_for_render(layer, x, y, layer_mask_bounds);
        if (effect_alpha <= 0.0F) {
          continue;
        }
      }
      // COM-calibrated Lambert shading (July 2026, photoshop-bevel-smooth
      // fixtures): the surface lighting L = N dot Light with a properly
      // normalized normal, then the HIGHLIGHT is the excess over the flat-face
      // value normalized to the headroom, (L - sin(alt)) / (1 - sin(alt)), and
      // the SHADOW is the deficit normalized to the floor,
      // (sin(alt) - L) / sin(alt). This reproduces both the full-strength
      // shadow edge under a low light and the non-monotone highlight band under
      // a high light (slopes steeper than 90 - altitude tip past the light),
      // within ~0.4/255 on the altitude 30 and 60 probes. Pillow/Emboss
      // shadows instead follow the UNNORMALIZED deficit — linear in slope,
      // saturating early (the probes pin the shadow gain to the highlight's
      // small-signal gain at every depth, and the shadow side clamps long
      // before the normalized Lambert would; the lit side's tip-past regime
      // stays on the normalized value, depth-1000 probe).
      const auto shade = [&](float sign, float weight) {
        if (weight <= 0.0F) {
          return;
        }
        const auto gradient_x = base_gradient_x * sign;
        const auto gradient_y = base_gradient_y * sign;
        const auto length = std::sqrt(gradient_x * gradient_x + gradient_y * gradient_y + 1.0F);
        const auto raw_light = gradient_x * light_x + gradient_y * light_y + light_z;
        const auto surface_light = raw_light / std::max(0.0001F, length);
        auto lighting = surface_light >= light_z
                            ? (surface_light - light_z) / std::max(0.01F, 1.0F - light_z)
                            : -((light_z - surface_light) / std::max(0.01F, light_z));
        if (pillow_family && raw_light < light_z) {
          lighting = -((light_z - raw_light) / std::max(0.01F, light_z));
        }
        if (!gloss_is_linear) {
          // Gloss Contour remaps the signed lighting scalar before the
          // highlight/shadow split; Linear short-circuits so plain bevels stay
          // bit-identical to the historical render.
          const auto remapped = sample_style_contour_lut(
              gloss_lut, clamp_unit((lighting + 1.0F) * 0.5F), bevel.gloss_anti_aliased);
          lighting = remapped * 2.0F - 1.0F;
        }
        if (lighting > 0.0F) {
          composite_effect_color(destination, x, y, bevel.highlight_color,
                                 clamp_unit(lighting) * weight * bevel.highlight_opacity * layer.opacity(),
                                 bevel.highlight_blend_mode);
        } else if (lighting < 0.0F) {
          composite_effect_color(destination, x, y, bevel.shadow_color,
                                 clamp_unit(-lighting) * weight * bevel.shadow_opacity * layer.opacity(),
                                 bevel.shadow_blend_mode);
        }
      };
      if (pillow) {
        // The valley mirrors the rim, and Photoshop composites BOTH sides at
        // an anti-aliased edge: the exterior-signed shading over the pixel's
        // backdrop fraction and the flipped interior shading over its content
        // fraction (pillow-ellipse fringe pixels carry a highlight AND a
        // shadow simultaneously — photoshop-emboss-styles probes). A hard
        // 0.5 flip instead speckles curved AA contours with wrong-side
        // shading. Direction Down ("Out ", title.psd and the probes) is the
        // calibrated orientation; Direction Up mirrors it.
        const auto base_sign = bevel.direction_up ? -1.0F : 1.0F;
        shade(base_sign, effect_alpha * (1.0F - matte_alpha));
        shade(-base_sign, effect_alpha * matte_alpha);
      } else {
        shade(direction, effect_alpha);
      }
    }
  }
}

inline std::vector<float> stroke_alpha_mask(const PixelBuffer& source, const Layer& layer, Rect bounds,
                                            Rect mask_bounds, float size, LayerStrokePosition position,
                                            std::optional<Rect> layer_mask_bounds, bool mask_shapes_source,
                                            std::vector<float>* shape_burst_positions) {
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
  // Photoshop vectorizes the matte's coverage boundary at subpixel precision
  // rather than dilating from every alpha>0 pixel: an anti-aliased fringe
  // stays OUTSIDE the contour (on AA text the alpha>0 shape is one fringe
  // fatter per side, landing the band ~1 px outward and closing narrow
  // inter-glyph gaps Photoshop leaves clean — title.psd probes, July 2026),
  // while flat low-alpha regions are stroked in full (the pinned
  // photoshop-stroke-partial-alpha fixture's 25% strip). Approximate that
  // with the half-covered-or-more pixels plus any painted pixel farther than
  // 2 px from every such solid pixel: an AA fringe always hugs its solid
  // core, a flat wash does not. Binary mattes are identical under all these
  // conventions, so the pinned COM band calibration is unaffected.
  std::vector<float> contour(base.size(), 0.0F);
  auto has_solid_pixel = false;
  auto has_faint_pixel = false;
  for (std::size_t index = 0; index < base.size(); ++index) {
    if (base[index] >= 0.5F) {
      contour[index] = 1.0F;
      has_solid_pixel = true;
    } else if (base[index] > 0.0F) {
      has_faint_pixel = true;
    }
  }
  if (!has_solid_pixel) {
    for (std::size_t index = 0; index < base.size(); ++index) {
      contour[index] = base[index] > 0.0F ? 1.0F : 0.0F;
    }
  } else if (has_faint_pixel) {
    constexpr float kAaFringeReach = 2.0F;
    const auto solid_distance = stroke_distance_field(contour, width, height, true);
    for (std::size_t index = 0; index < base.size(); ++index) {
      if (base[index] > 0.0F && contour[index] == 0.0F && solid_distance[index] > kAaFringeReach) {
        contour[index] = 1.0F;
      }
    }
  }
  // Anchor matte for the band distances: augmented flat-wash (and no-solid
  // fallback) pixels stroke as a fully solid shape, while true AA fringes
  // keep their raw alpha so the subpixel path can place the contour at the
  // matte's real half-coverage crossing instead of a whole-pixel staircase.
  // A fully binary matte skips the supersampled path entirely and stays
  // bit-identical to the pinned pixel-center calibration.
  auto has_subpixel_fringe = false;
  std::vector<float> anchor_matte(base.size(), 0.0F);
  for (std::size_t index = 0; index < base.size(); ++index) {
    const auto anchored = contour[index] > 0.0F
                              ? (base[index] >= 0.5F ? base[index] : 1.0F)
                              : base[index];
    anchor_matte[index] = anchored;
    if (anchored > 0.0F && anchored < 1.0F) {
      has_subpixel_fringe = true;
    }
  }
  std::vector<float> outside_distance;
  std::vector<float> inside_distance;
  if (has_subpixel_fringe) {
    stroke_subpixel_distance_fields(anchor_matte, width, height, band_out > 0.0F, band_in > 0.0F,
                                    outside_distance, inside_distance);
  } else {
    if (band_out > 0.0F) {
      outside_distance = stroke_distance_field(contour, width, height, true);
    }
    if (band_in > 0.0F) {
      inside_distance = stroke_distance_field(contour, width, height, false);
    }
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

  if (shape_burst_positions != nullptr) {
    // Photoshop's Shape Burst gradient ramp is LINEAR in the same anchored
    // distance field: position 0 at the band's outer limit, 1 at its inner
    // limit, one continuous span across both halves of a Center stroke. Each
    // side that is a band limit (not the contour itself) extends by the same
    // +1 px the coverage reaches (kStrokeContourOffset), so an Outside or
    // Inside stroke spans size+1 and a Center stroke spans size+2. Pinned by
    // the photoshop-stroke-shapeburst fixtures within +/-1/255 (COM probes,
    // July 2026).
    auto& positions = *shape_burst_positions;
    positions.assign(base.size(), 0.0F);
    const auto outer_reach = band_out > 0.0F ? band_out + kStrokeContourOffset : 0.0F;
    const auto inner_reach = band_in > 0.0F ? band_in + kStrokeContourOffset : 0.0F;
    const auto span = std::max(1.0F, outer_reach + inner_reach);
    for (std::size_t index = 0; index < base.size(); ++index) {
      float along;
      if (contour[index] > 0.0F) {
        along = inside_distance.empty() ? span : band_out + inside_distance[index];
      } else {
        along = outside_distance.empty() ? 0.0F : outer_reach - outside_distance[index];
      }
      positions[index] = clamp_unit(along / span);
    }
  }
  return mask;
}

// A stroke whose band mask has been resolved (cache hit or computed): enough
// to draw the stroke and to evaluate its Overprint knockout of the base
// content without recomputing the distance fields.
struct PreparedStroke {
  const LayerStroke* stroke{nullptr};
  std::uint32_t effect_index{0};
  std::shared_ptr<const StyleMaskEntry> entry;
  Rect mask_bounds{};
  Rect draw_rect{};
  Rect gradient_bounds{};
  bool clip_to_mask{false};
  bool shape_burst{false};
};

inline std::optional<PreparedStroke> prepare_stroke_render(const Layer& layer, const PixelBuffer& source, Rect clip,
                                                           Rect bounds, const LayerStroke& stroke,
                                                           std::optional<Rect> layer_mask_bounds,
                                                           StyleMaskProvider* masks, std::uint32_t effect_index) {
  // No opacity gate here: a 0%-opacity Overprint-off stroke paints nothing
  // but still knocks the content out of its band at full coverage (PS COM
  // probe + the fixture's ins0 arm, July 2026), so the knockout pass must be
  // able to prepare it. Draw-only callers skip zero opacity themselves.
  if (!stroke.enabled || stroke.size <= 0.0F) {
    return std::nullopt;
  }
  const auto radius = std::max(1, static_cast<int>(std::ceil(stroke.size)));
  const auto full_mask_bounds = outset_rect(bounds, radius + 1);
  const auto effect_bounds = stroke.position == LayerStrokePosition::Inside ? bounds : full_mask_bounds;
  const auto draw_rect = intersect_rect(clip, effect_bounds);
  if (draw_rect.empty()) {
    return std::nullopt;
  }
  const auto legacy_mask_bounds = clipped_mask_bounds(full_mask_bounds, draw_rect, radius + 1);
  const auto mask_shapes_source = !layer.layer_style().layer_mask_hides_effects;
  const auto shape_burst =
      stroke.uses_gradient && stroke.gradient.type == LayerStyleGradientType::ShapeBurst;
  auto [entry, mask_bounds] = style_mask_for_render(
      masks, layer, StyleMaskKind::Stroke, effect_index, full_mask_bounds, full_mask_bounds, legacy_mask_bounds,
      bounds, layer_mask_bounds, [&](Rect domain) {
        StyleMaskEntry computed;
        computed.primary = stroke_alpha_mask(source, layer, bounds, domain, stroke.size, stroke.position,
                                             layer_mask_bounds, mask_shapes_source,
                                             shape_burst ? &computed.secondary : nullptr);
        return computed;
      });
  PreparedStroke prepared;
  prepared.stroke = &stroke;
  prepared.effect_index = effect_index;
  prepared.entry = std::move(entry);
  prepared.mask_bounds = mask_bounds;
  prepared.draw_rect = draw_rect;
  prepared.clip_to_mask = layer_mask_clips_effect_output(layer);
  prepared.shape_burst = shape_burst;
  prepared.gradient_bounds = stroke.uses_gradient && stroke.gradient.align_with_layer
                                 ? layer_visible_alpha_bounds(layer, source, bounds).value_or(bounds)
                                 : effect_bounds;
  return prepared;
}

// The band coverage the draw loop actually paints with at (x, y): the cached
// mask value times the "Layer Mask Hides Effects" output clip.
inline float prepared_stroke_coverage(const PreparedStroke& prepared, const Layer& layer, std::int32_t x,
                                      std::int32_t y, std::optional<Rect> layer_mask_bounds) {
  const auto& bounds = prepared.mask_bounds;
  if (x < bounds.x || y < bounds.y || x >= bounds.x + bounds.width || y >= bounds.y + bounds.height) {
    return 0.0F;
  }
  auto coverage = prepared.entry->primary[static_cast<std::size_t>((y - bounds.y) * bounds.width + (x - bounds.x))];
  if (prepared.clip_to_mask && coverage > 0.0F) {
    coverage *= layer_mask_alpha_for_render(layer, x, y, layer_mask_bounds);
  }
  return coverage;
}

// Mirrors render_prepared_stroke's per-pixel alpha (same expressions in the
// same order) so the Normal-mode knockout divisor cancels the later stroke
// draw exactly.
inline float prepared_stroke_draw_alpha(const PreparedStroke& prepared, const Layer& layer, std::int32_t x,
                                        std::int32_t y, float coverage) {
  const auto& stroke = *prepared.stroke;
  auto alpha = coverage * stroke.opacity * layer.opacity();
  if (stroke.uses_gradient) {
    if (prepared.shape_burst) {
      const auto& bounds = prepared.mask_bounds;
      const auto band =
          prepared.entry->secondary[static_cast<std::size_t>((y - bounds.y) * bounds.width + (x - bounds.x))];
      const auto span = shape_burst_ramp_span(stroke.size, stroke.position);
      alpha *= shape_burst_stroke_opacity(stroke.gradient, band, span);
    } else {
      const auto position = gradient_position(stroke.gradient, prepared.gradient_bounds, x, y);
      alpha *= gradient_stop_opacity(stroke.gradient, position);
    }
  }
  return alpha;
}

// Overprint-off knockout factor for the base content under this stroke.
// Normal mode uses the compensated divisor (1 - C) / (1 - s): the later
// source-over stroke draw scales the destination by (1 - s), so the band's
// content term lands at exactly a x (1 - C) — Photoshop's full knockout —
// and the opaque solid case (s = C) is a structural no-op. Non-Normal modes
// use plain (1 - C): the divisor degenerates there (an opaque stroke keeps
// s = C, f = 1 for every C < 1), and P3 pins the band interior (C = 1,
// content fully gone, blend against the backdrop), which both forms satisfy;
// only the 1 px AA fringe stays approximate. COM probes July 2026.
inline float stroke_knockout_factor(const PreparedStroke& prepared, const Layer& layer, std::int32_t x,
                                    std::int32_t y, std::optional<Rect> layer_mask_bounds) {
  const auto coverage = prepared_stroke_coverage(prepared, layer, x, y, layer_mask_bounds);
  if (coverage <= 0.0F) {
    return 1.0F;
  }
  if (prepared.stroke->blend_mode != BlendMode::Normal) {
    return clamp_unit(1.0F - coverage);
  }
  const auto draw_alpha = prepared_stroke_draw_alpha(prepared, layer, x, y, coverage);
  const auto remaining = 1.0F - draw_alpha;
  if (remaining <= 1e-6F) {
    // Only reachable as coverage -> 1 (draw_alpha <= coverage): full knockout.
    return 0.0F;
  }
  return clamp_unit((1.0F - coverage) / remaining);
}

// Overprint-off knockout is invisible for an opaque Normal stroke at full
// layer opacity: the stroke covers exactly what it would have knocked out.
// Skipping it keeps the base pass's fast row path and the pinned opaque
// fixtures on their historical byte-exact path.
inline bool stroke_knockout_is_identity(const LayerStroke& stroke, const Layer& layer) {
  if (stroke.blend_mode != BlendMode::Normal) {
    return false;
  }
  if (stroke.opacity < 1.0F || layer.opacity() < 1.0F) {
    return false;
  }
  if (stroke.uses_gradient) {
    for (const auto& stop : stroke.gradient.alpha_stops) {
      if (stop.opacity < 1.0F) {
        return false;
      }
    }
  }
  return true;
}

template <typename Target>
void render_prepared_stroke(Target& destination, const Layer& layer, const PreparedStroke& prepared,
                            std::optional<Rect> layer_mask_bounds) {
  const auto& stroke = *prepared.stroke;
  if (stroke.opacity <= 0.0F) {
    return;  // the knockout already happened in the base pass; nothing to draw
  }
  const auto& mask = prepared.entry->primary;
  const auto& entry = prepared.entry;
  const auto mask_bounds = prepared.mask_bounds;
  const auto draw_rect = prepared.draw_rect;
  const auto mask_width = mask_bounds.width;
  const auto clip_to_mask = prepared.clip_to_mask;
  const auto shape_burst = prepared.shape_burst;
  const auto gradient_bounds = prepared.gradient_bounds;
  for (std::int32_t y = draw_rect.y; y < draw_rect.y + draw_rect.height; ++y) {
    for (std::int32_t x = draw_rect.x; x < draw_rect.x + draw_rect.width; ++x) {
      const auto mask_index = static_cast<std::size_t>((y - mask_bounds.y) * mask_width + (x - mask_bounds.x));
      auto mask_alpha = mask[mask_index];
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
        // Shape Burst samples with endpoint smoothing (the probes show the
        // Intr ease applied even to a two-stop ramp, unlike the pinned linear
        // 2-stop overlays) and tent-averages across the pixel footprint.
        if (shape_burst) {
          const auto band = entry->secondary[mask_index];
          const auto span = shape_burst_ramp_span(stroke.size, stroke.position);
          color = shape_burst_stroke_color(stroke.gradient, band, span, x, y);
          alpha *= shape_burst_stroke_opacity(stroke.gradient, band, span);
        } else {
          const auto position = gradient_position(stroke.gradient, gradient_bounds, x, y);
          color = gradient_color_dithered(stroke.gradient, position, x, y);
          alpha *= gradient_stop_opacity(stroke.gradient, position);
        }
      }
      composite_effect_color(destination, x, y, color, alpha, stroke.blend_mode);
    }
  }
}

template <typename Target>
void render_stroke(Target& destination, const Layer& layer, const PixelBuffer& source, Rect clip, Rect bounds,
                   const LayerStroke& stroke, std::optional<Rect> layer_mask_bounds,
                   StyleMaskProvider* masks = nullptr, std::uint32_t effect_index = 0) {
  if (stroke.opacity <= 0.0F) {
    return;  // nothing to draw; any Overprint-off knockout is the caller's pass
  }
  const auto prepared =
      prepare_stroke_render(layer, source, clip, bounds, stroke, layer_mask_bounds, masks, effect_index);
  if (!prepared.has_value()) {
    return;
  }
  render_prepared_stroke(destination, layer, *prepared, layer_mask_bounds);
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

  // Strokes without Overprint knock the layer's own content (fill, interior
  // effects, clipped members) out of their band and blend against the layers
  // below (COM probes July 2026, docs/ps-compat.md). Resolve those strokes'
  // band masks up front — the stroke draw below reuses the same prepared
  // entries — and fold the combined per-pixel factor into one plane the base
  // pass and every interior effect multiply in. Opaque solid Normal strokes
  // skip all of this: their knockout is a structural no-op.
  std::vector<PreparedStroke> knockout_strokes;
  StrokeKnockoutPlane knockout_plane;
  if (!draw_rect.empty() && style.effects_visible) {
    for (std::uint32_t index = 0; index < style.strokes.size(); ++index) {
      const auto& stroke = style.strokes[index];
      if (stroke.overprint || stroke_knockout_is_identity(stroke, layer)) {
        continue;
      }
      if (auto prepared =
              prepare_stroke_render(layer, source, clip, bounds, stroke, layer_mask_bounds, masks, index)) {
        knockout_strokes.push_back(std::move(*prepared));
      }
    }
    if (!knockout_strokes.empty()) {
      profile_compositor_step(destination, layer, "stroke_knockout", draw_rect, [&] {
        knockout_plane.rect = draw_rect;
        knockout_plane.factor.assign(
            static_cast<std::size_t>(draw_rect.width) * static_cast<std::size_t>(draw_rect.height), 1.0F);
        for (const auto& prepared : knockout_strokes) {
          auto* factor = knockout_plane.factor.data();
          for (std::int32_t y = draw_rect.y; y < draw_rect.y + draw_rect.height; ++y) {
            for (std::int32_t x = draw_rect.x; x < draw_rect.x + draw_rect.width; ++x) {
              *factor++ *= stroke_knockout_factor(prepared, layer, x, y, layer_mask_bounds);
            }
          }
        }
      });
    }
  }
  const auto* knockout = knockout_strokes.empty() ? nullptr : &knockout_plane;

  if (!draw_rect.empty()) {
    profile_compositor_step(destination, layer, "base_pixels", draw_rect, [&] {
      const auto format = source.format();
      const auto channels = format.channels;
      const auto* source_bytes = source.data().data();
      const auto source_stride = source.stride_bytes();
      const auto has_enabled_mask = (layer.mask().has_value() && !layer.mask()->disabled) ||
                                    layer_has_enabled_vector_mask(layer);
      bool composited_by_target = false;
      if (!has_blend_if && !has_enabled_mask && prepared_satins.empty() && knockout == nullptr &&
          layer.fill_opacity() == 1.0F && layer.blend_mode() == BlendMode::Normal) {
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
            auto source_coverage =
                source_alpha * layer_mask_alpha_for_render(layer, x, y, layer_mask_bounds);
            if (knockout != nullptr) {
              source_coverage *= knockout->at(x, y);
            }
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
          auto source_alpha =
              (channels >= 4 ? static_cast<float>(src[3]) / 255.0F : 1.0F) *
              layer_mask_alpha_for_render(layer, x, y, layer_mask_bounds) * layer.opacity();
          if (knockout != nullptr) {
            source_alpha *= knockout->at(x, y);
          }
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
        render_inner_shadow(destination, layer, source, clip, bounds, shadow, layer_mask_bounds, masks, index,
                            knockout);
      });
    }
    for (std::uint32_t index = 0; index < style.inner_glows.size(); ++index) {
      const auto& glow = style.inner_glows[index];
      profile_compositor_step(destination, layer, "inner_glow", clip, [&] {
        render_inner_glow(destination, layer, source, clip, bounds, glow, layer_mask_bounds, masks, index,
                          knockout);
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
                               layer_mask_bounds, patterns, knockout);
      });
    }
    for (const auto& fill : style.gradient_fills) {
      profile_compositor_step(destination, layer, "gradient_fill", clip, [&] {
        render_gradient_fill(destination, layer, *interior_source, clip, bounds, fill, layer_mask_bounds,
                             knockout);
      });
    }
    for (const auto& overlay : style.color_overlays) {
      profile_compositor_step(destination, layer, "color_overlay", clip, [&] {
        render_color_overlay(destination, layer, *interior_source, clip, bounds, overlay, layer_mask_bounds,
                             knockout);
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
            auto source_coverage =
                source_alpha * layer_mask_alpha_for_render(layer, x, y, layer_mask_bounds);
            if (knockout != nullptr) {
              source_coverage *= knockout->at(x, y);
            }
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
    for (std::uint32_t index = 0; index < style.strokes.size(); ++index) {
      const auto& stroke = style.strokes[index];
      profile_compositor_step(destination, layer, "stroke", clip, [&] {
        const auto prepared =
            std::find_if(knockout_strokes.begin(), knockout_strokes.end(),
                         [index](const PreparedStroke& candidate) { return candidate.effect_index == index; });
        if (prepared != knockout_strokes.end()) {
          // Reuse the band mask the knockout pass already resolved.
          render_prepared_stroke(destination, layer, *prepared, layer_mask_bounds);
        } else {
          render_stroke(destination, layer, source, clip, bounds, stroke, layer_mask_bounds, masks, index);
        }
      });
    }
    // Bevel shading derives from the layer matte but composites OVER the
    // Stroke effect's band: the pillow probes show identical shading alphas
    // painted on top of the band with and without a stroke (July 2026,
    // photoshop-pillow-emboss fixtures), so strokes render first.
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

// Applies a group's raster/vector mask to everything its children composite,
// WITHOUT isolating them: Photoshop's default pass-through group keeps child
// blend modes and interior adjustments interacting with the backdrop below the
// group, so the mask must attenuate each contribution in place rather than
// clip a merged buffer. A nested masked group pushes onto the same adapter
// (the chain multiplies), which also caps template-instantiation depth at one
// wrapper per underlying target type. composite_source_row is deliberately not
// forwarded so masked groups always take the per-pixel path.
template <typename Base>
class GroupMaskedTarget {
public:
  explicit GroupMaskedTarget(Base& base) : base_(base) {}

  void push_mask(const Layer& group, std::optional<Rect> mask_bounds) {
    masks_.push_back(MaskEntry{&group, mask_bounds});
  }
  void pop_mask() { masks_.pop_back(); }

  [[nodiscard]] float mask_alpha(std::int32_t x, std::int32_t y) const {
    auto alpha = 1.0F;
    for (const auto& entry : masks_) {
      alpha *= layer_mask_alpha_for_render(*entry.group, x, y, entry.mask_bounds);
      if (alpha <= 0.0F) {
        return 0.0F;
      }
    }
    return alpha;
  }

  void composite_color(std::int32_t x, std::int32_t y, RgbColor color, float alpha, BlendMode mode) {
    alpha *= mask_alpha(x, y);
    if (alpha > 0.0F) {
      base_.composite_color(x, y, color, alpha, mode);
    }
  }

  void composite_special_fill_color(std::int32_t x, std::int32_t y, RgbColor color, float source_coverage,
                                    float fill_opacity, float layer_opacity, BlendMode mode)
    requires requires(Base& base) {
      base.composite_special_fill_color(std::int32_t{}, std::int32_t{}, RgbColor{}, 0.0F, 0.0F, 0.0F,
                                        BlendMode::Normal);
    }
  {
    source_coverage *= mask_alpha(x, y);
    if (source_coverage > 0.0F) {
      base_.composite_special_fill_color(x, y, color, source_coverage, fill_opacity, layer_opacity, mode);
    }
  }

  void adjust_color(std::int32_t x, std::int32_t y, const AdjustmentSettings& settings, float amount)
    requires requires(Base& base) {
      base.adjust_color(std::int32_t{}, std::int32_t{}, std::declval<const AdjustmentSettings&>(), 0.0F);
    }
  {
    amount *= mask_alpha(x, y);
    if (amount > 0.0F) {
      base_.adjust_color(x, y, settings, amount);
    }
  }

  void adjust_color(std::int32_t x, std::int32_t y, const AdjustmentLut& lut, float amount)
    requires requires(Base& base) {
      base.adjust_color(std::int32_t{}, std::int32_t{}, std::declval<const AdjustmentLut&>(), 0.0F);
    }
  {
    amount *= mask_alpha(x, y);
    if (amount > 0.0F) {
      base_.adjust_color(x, y, lut, amount);
    }
  }

  [[nodiscard]] CompositeSample sample_color(std::int32_t x, std::int32_t y) const
    requires requires(const Base& base) { base.sample_color(std::int32_t{}, std::int32_t{}); }
  {
    return base_.sample_color(x, y);
  }

  void record_clip_coverage(std::int32_t x, std::int32_t y, float alpha)
    requires requires(Base& base) { base.record_clip_coverage(std::int32_t{}, std::int32_t{}, 0.0F); }
  {
    base_.record_clip_coverage(x, y, alpha * mask_alpha(x, y));
  }

  void profile_compositor_step(const char* step, const Layer& layer, Rect rect, double elapsed_ms)
    requires requires(Base& base) {
      base.profile_compositor_step(std::declval<const char*>(), std::declval<const Layer&>(), Rect{}, 0.0);
    }
  {
    base_.profile_compositor_step(step, layer, rect, elapsed_ms);
  }

private:
  struct MaskEntry {
    const Layer* group;
    std::optional<Rect> mask_bounds;
  };

  Base& base_;
  std::vector<MaskEntry> masks_;
};

template <typename T>
struct is_group_masked_target : std::false_type {};
template <typename T>
struct is_group_masked_target<GroupMaskedTarget<T>> : std::true_type {};

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
    // A group's raster/vector mask attenuates every child contribution in
    // place. No isolation: the default group is pass-through, so child blend
    // modes and interior adjustments must keep meeting the backdrop below the
    // group, exactly as without a mask.
    if ((layer.mask().has_value() && !layer.mask()->disabled) || layer_has_enabled_vector_mask(layer)) {
      const auto mask_bounds = layer_mask_bounds_for_render(layer, overrides);
      if constexpr (is_group_masked_target<Target>::value) {
        destination.push_mask(layer, mask_bounds);
        composite_layers(destination, layer.children(), clip, overrides, throw_on_unsupported_pixel_format,
                         masks, patterns);
        destination.pop_mask();
      } else {
        GroupMaskedTarget<Target> masked(destination);
        masked.push_mask(layer, mask_bounds);
        composite_layers(masked, layer.children(), clip, overrides, throw_on_unsupported_pixel_format, masks,
                         patterns);
      }
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
