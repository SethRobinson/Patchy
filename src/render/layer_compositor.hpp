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

// Photoshop's Advanced Blending "Channels" restriction, as rendered: zero when
// the imported payload is preserved but not modeled (non-RGB indices).
[[nodiscard]] inline std::uint8_t layer_rendered_channel_restriction(const Layer& layer) noexcept {
  return layer.channel_restriction_supported() ? layer.restricted_channels() : std::uint8_t{0};
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

// Photoshop's Opacity on a pass-through group is a post-composite fade: the
// children first meet the backdrop exactly as at 100% (child blend modes and
// interior adjustments included), then the whole result interpolates back
// toward the pre-group backdrop — the PDF non-isolated-group alpha formula.
// One fade applies to the composite, so overlapping children never
// double-fade. Per-pixel independence keeps this strip-parallel and
// dirty-patch safe. Untouched pixels are skipped so their bytes never
// round-trip through the premultiplied lerp.
template <typename Target>
void fade_toward_snapshot(Target& destination, const CompositeSnapshot& before, Rect rect, float opacity) {
  opacity = clamp_unit(opacity);
  for (std::int32_t y = rect.y; y < rect.y + rect.height; ++y) {
    for (std::int32_t x = rect.x; x < rect.x + rect.width; ++x) {
      const auto previous = before.sample_color(x, y);
      const auto current = destination.sample_color(x, y);
      if (previous.color == current.color && previous.alpha == current.alpha) {
        continue;
      }
      const auto output_alpha = previous.alpha + (current.alpha - previous.alpha) * opacity;
      if (output_alpha <= 0.0F) {
        continue;
      }
      const auto previous_weight = previous.alpha * (1.0F - opacity) / output_alpha;
      const auto current_weight = current.alpha * opacity / output_alpha;
      const auto color =
          RgbColor{clamp_byte(static_cast<float>(previous.color.red) * previous_weight +
                              static_cast<float>(current.color.red) * current_weight),
                   clamp_byte(static_cast<float>(previous.color.green) * previous_weight +
                              static_cast<float>(current.color.green) * current_weight),
                   clamp_byte(static_cast<float>(previous.color.blue) * previous_weight +
                              static_cast<float>(current.color.blue) * current_weight)};
      destination.store_color(x, y, color, output_alpha);
    }
  }
}

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

// Folder Fill is ignored, content and effects both (COM-calibrated July 2026,
// docs/ps-compat.md group-effects bullet): a styled GROUP routed through the
// effect pipeline keeps Fill at 1; pixel layers keep theirs.
[[nodiscard]] inline float layer_fill_opacity_for_render(const Layer& layer) noexcept {
  return layer.kind() == LayerKind::Group ? 1.0F : layer.fill_opacity();
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

// Coverage an exterior effect (drop shadow, outer glow) must be painted with so
// that the base pass compositing over it leaves Photoshop's contribution.
//
// Photoshop's probes (COM, July 2026, exterior-knockout series) show the layer
// and its exterior effects contributing ADDITIVELY against the layer's original
// backdrop: a 50%-alpha square over gray with a full outer glow renders the glow
// at its own coverage AND the square at 50%, never the glow attenuated a second
// time by the square on top of it. Painting the effect first and letting the
// base pass composite over it costs exactly one (1 - paint) factor, so the
// pre-multiplied knockout has to divide that factor back out.
//
// `shape` is the layer's transparency coverage (source alpha x layer mask) and
// `paint` is the coverage the base pass will use for the layer's own pixels
// (shape x Fill x Opacity). `conceals` marks the effects Photoshop knocks out
// with the layer's SHAPE regardless of whether those pixels paint - every outer
// glow, and drop shadows with "Layer Knocks Out Drop Shadow" on: a Fill-0 layer
// shows the pure backdrop inside its shape, never the effect. Without it the
// only knockout is the layer's own coverage, which the base pass already
// applies, so the factor is 1.
[[nodiscard]] inline float exterior_effect_knockout(float shape, float paint, bool conceals) {
  if (!conceals) {
    return 1.0F;
  }
  const auto remaining = 1.0F - clamp_unit(paint);
  if (remaining <= 0.0F) {
    return 0.0F;
  }
  return std::min(1.0F, (1.0F - clamp_unit(shape)) / remaining);
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
// the source color toward the mode's neutral, then blends at full coverage:
// toward white for the burn modes (COM-probed July 2026 over a gray backdrop: a
// 50%-opacity black linearBurn shadow subtracts an absolute 255 x alpha from
// every channel and colorBurn matches the folded curve exactly, both refuting
// the plain lerp(b, blend(b, color), alpha) model) and toward BLACK for
// colorDodge (the July 2026 inner-glow repro probe: a pale-blue 77% colorDodge
// glow matches blend(b, color x alpha) within 2/255 while the plain model
// overshoots by ~15). Normal, Multiply, and Darken keep the plain model, and
// the affine modes — Multiply, Screen, LinearDodge — are algebraically
// identical either way. Every effect draw goes through here; layer PIXEL
// compositing keeps the standard model.
// Dissolve takes its own branch first: it is a coverage decision, not a colour
// function, so the effect's alpha becomes the probability that the pixel is
// painted at all and the surviving pixels composite at full strength through
// Normal. Each effect passes its own DissolveField so a dissolved shadow and a
// dissolved glow on one layer do not dither onto the same pixels.
template <typename Target>
inline void composite_effect_color(Target& destination, std::int32_t x, std::int32_t y, RgbColor color,
                                   float alpha, BlendMode mode, DissolveField field) {
  if (mode == BlendMode::Dissolve) {
    if (dissolve_coverage(x, y, alpha, field) <= 0.0F) {
      return;
    }
    destination.composite_color(x, y, color, 1.0F, BlendMode::Normal);
    return;
  }
  if (mode == BlendMode::LinearBurn || mode == BlendMode::ColorBurn) {
    const auto fold = [alpha](std::uint8_t channel) {
      return static_cast<std::uint8_t>(
          std::clamp<long>(std::lround(255.0F - (255.0F - static_cast<float>(channel)) * alpha), 0L, 255L));
    };
    destination.composite_color(x, y, RgbColor{fold(color.red), fold(color.green), fold(color.blue)},
                                alpha > 0.0F ? 1.0F : 0.0F, mode);
    return;
  }
  if (mode == BlendMode::ColorDodge) {
    const auto fold = [alpha](std::uint8_t channel) {
      return static_cast<std::uint8_t>(
          std::clamp<long>(std::lround(static_cast<float>(channel) * alpha), 0L, 255L));
    };
    destination.composite_color(x, y, RgbColor{fold(color.red), fold(color.green), fold(color.blue)},
                                alpha > 0.0F ? 1.0F : 0.0F, mode);
    return;
  }
  destination.composite_color(x, y, color, alpha, mode);
}

// RGB-space twin of composite_effect_color, for interior effects that fold into
// the layer's own straight color instead of compositing onto the destination.
// The LinearBurn/ColorBurn/ColorDodge opacity pre-fold has to come along or
// those modes lose their Photoshop calibration.
[[nodiscard]] inline std::array<std::uint8_t, 3> fold_effect_color(std::array<std::uint8_t, 3> destination,
                                                                   RgbColor color, float alpha, BlendMode mode,
                                                                   std::int32_t x, std::int32_t y,
                                                                   DissolveField field) {
  if (mode == BlendMode::Dissolve) {
    return dissolve_coverage(x, y, alpha, field) > 0.0F
               ? std::array<std::uint8_t, 3>{color.red, color.green, color.blue}
               : destination;
  }
  if (mode == BlendMode::LinearBurn || mode == BlendMode::ColorBurn) {
    const auto fold = [alpha](std::uint8_t channel) {
      return static_cast<std::uint8_t>(
          std::clamp<long>(std::lround(255.0F - (255.0F - static_cast<float>(channel)) * alpha), 0L, 255L));
    };
    return composite_blended_rgb({fold(color.red), fold(color.green), fold(color.blue)}, destination, mode,
                                 alpha > 0.0F ? 1.0F : 0.0F, 1.0F);
  }
  if (mode == BlendMode::ColorDodge) {
    const auto fold = [alpha](std::uint8_t channel) {
      return static_cast<std::uint8_t>(
          std::clamp<long>(std::lround(static_cast<float>(channel) * alpha), 0L, 255L));
    };
    return composite_blended_rgb({fold(color.red), fold(color.green), fold(color.blue)}, destination, mode,
                                 alpha > 0.0F ? 1.0F : 0.0F, 1.0F);
  }
  return composite_blended_rgb({color.red, color.green, color.blue}, destination, mode, alpha, 1.0F);
}

// One resolved interior overlay (Pattern, Gradient or Color), ready to fold into
// a layer's straight RGB per pixel. Kept in Photoshop's interior order: pattern
// under gradient under color.
struct PreparedInteriorOverlay {
  enum class Kind : std::uint8_t { Pattern, Gradient, Color };

  Kind kind{Kind::Color};
  BlendMode blend_mode{BlendMode::Normal};
  float opacity{1.0F};
  RgbColor color{};                                 // Color overlay
  std::optional<PatternTileSampler> pattern{};      // Pattern overlay
  const LayerStyleGradient* gradient{nullptr};      // Gradient overlay
  Rect gradient_bounds{};
};

// Resolves the interior overlays a layer will fold into its own color. An
// unresolvable pattern is dropped here, which renders nothing - the same
// outcome render_pattern_overlay gives it.
[[nodiscard]] inline std::vector<PreparedInteriorOverlay> prepare_interior_overlays(
    const Layer& layer, const LayerStyle& style, const PixelBuffer& source, Rect bounds,
    const PatternStore* patterns) {
  std::vector<PreparedInteriorOverlay> prepared;
  prepared.reserve(style.pattern_overlays.size() + style.gradient_fills.size() + style.color_overlays.size());
  for (const auto& overlay : style.pattern_overlays) {
    if (!overlay.enabled || overlay.opacity <= 0.0F || patterns == nullptr) {
      continue;
    }
    const auto* resource = patterns->find(overlay.pattern_id);
    if (resource == nullptr || resource->tile.empty()) {
      continue;
    }
    PreparedInteriorOverlay entry;
    entry.kind = PreparedInteriorOverlay::Kind::Pattern;
    entry.blend_mode = overlay.blend_mode;
    entry.opacity = overlay.opacity;
    entry.pattern.emplace(resource->tile, layer, overlay.scale, overlay.angle_degrees, overlay.link_with_layer,
                          overlay.phase_x, overlay.phase_y);
    prepared.push_back(std::move(entry));
  }
  for (const auto& fill : style.gradient_fills) {
    if (!fill.enabled || fill.opacity <= 0.0F) {
      continue;
    }
    PreparedInteriorOverlay entry;
    entry.kind = PreparedInteriorOverlay::Kind::Gradient;
    entry.blend_mode = fill.blend_mode;
    entry.opacity = fill.opacity;
    entry.gradient = &fill.gradient;
    entry.gradient_bounds = fill.gradient.align_with_layer
                                ? layer_visible_alpha_bounds(layer, source, bounds).value_or(bounds)
                                : bounds;
    prepared.push_back(std::move(entry));
  }
  for (const auto& overlay : style.color_overlays) {
    if (!overlay.enabled || overlay.opacity <= 0.0F) {
      continue;
    }
    PreparedInteriorOverlay entry;
    entry.kind = PreparedInteriorOverlay::Kind::Color;
    entry.blend_mode = overlay.blend_mode;
    entry.opacity = overlay.opacity;
    entry.color = overlay.color;
    prepared.push_back(std::move(entry));
  }
  return prepared;
}

// Folds the prepared overlays into one styled color. Source alpha, the layer
// mask, Fill/layer opacity and the backdrop are deliberately absent: the base
// pass applies each of them once to the folded result, which is what makes a
// 100%/Normal overlay cover the layer's own pixels the way Photoshop does.
[[nodiscard]] inline std::array<std::uint8_t, 3> fold_interior_overlays(
    std::array<std::uint8_t, 3> styled, const std::vector<PreparedInteriorOverlay>& overlays, std::int32_t x,
    std::int32_t y) {
  for (const auto& overlay : overlays) {
    auto coverage = overlay.opacity;
    auto color = overlay.color;
    auto field = DissolveField::ColorOverlay;
    switch (overlay.kind) {
      case PreparedInteriorOverlay::Kind::Pattern: {
        const auto sample = overlay.pattern->sample(x, y);
        coverage *= sample.alpha;
        color = sample.color;
        field = DissolveField::PatternOverlay;
        break;
      }
      case PreparedInteriorOverlay::Kind::Gradient: {
        const auto position = gradient_position(*overlay.gradient, overlay.gradient_bounds, x, y);
        coverage *= gradient_stop_opacity(*overlay.gradient, position);
        color = gradient_color_dithered(*overlay.gradient, position, x, y);
        field = DissolveField::GradientOverlay;
        break;
      }
      case PreparedInteriorOverlay::Kind::Color:
        break;
    }
    if (coverage <= 0.0F) {
      continue;
    }
    styled = fold_effect_color(styled, color, coverage, overlay.blend_mode, x, y, field);
  }
  return styled;
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
  // shadow either way. See exterior_effect_knockout for why the shape factor is
  // divided by the coverage the base pass will paint with.
  std::vector<float> conceal_mask;
  if (shadow.layer_conceals) {
    conceal_mask = layer_alpha_mask(source, layer, bounds, draw_rect, 0, 0, layer_mask_bounds);
  }
  const auto paint_scale = layer_fill_opacity_for_render(layer) * layer.opacity();
  const auto clip_to_mask = layer_mask_clips_effect_output(layer);
  for (std::int32_t y = draw_rect.y; y < draw_rect.y + draw_rect.height; ++y) {
    for (std::int32_t x = draw_rect.x; x < draw_rect.x + draw_rect.width; ++x) {
      auto alpha = mask[static_cast<std::size_t>((y - mask_bounds.y) * width + (x - mask_bounds.x))] *
                   shadow.opacity * layer.opacity();
      if (clip_to_mask) {
        alpha *= layer_mask_alpha_for_render(layer, x, y, layer_mask_bounds);
      }
      if (!conceal_mask.empty()) {
        const auto shape = conceal_mask[static_cast<std::size_t>((y - draw_rect.y) * draw_rect.width +
                                                                 (x - draw_rect.x))];
        alpha *= exterior_effect_knockout(shape, shape * paint_scale, true);
      }
      composite_effect_color(destination, x, y, shadow.color, alpha, shadow.blend_mode,
                             DissolveField::DropShadow);
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

  // The glow always concedes the layer's shape (a Fill-0 layer shows the pure
  // backdrop inside it, never the glow), but the base pass compositing over the
  // glow supplies part of that knockout already — see exterior_effect_knockout.
  const auto paint_scale = layer_fill_opacity_for_render(layer) * layer.opacity();
  const auto clip_to_mask = layer_mask_clips_effect_output(layer);
  for (std::int32_t y = draw_rect.y; y < draw_rect.y + draw_rect.height; ++y) {
    for (std::int32_t x = draw_rect.x; x < draw_rect.x + draw_rect.width; ++x) {
      const auto source_alpha =
          source_mask[static_cast<std::size_t>((y - draw_rect.y) * source_mask_width + (x - draw_rect.x))];
      auto glow_alpha = mask[static_cast<std::size_t>((y - mask_bounds.y) * width + (x - mask_bounds.x))] *
                        exterior_effect_knockout(source_alpha, source_alpha * paint_scale, true) *
                        glow.opacity * layer.opacity();
      if (clip_to_mask) {
        glow_alpha *= layer_mask_alpha_for_render(layer, x, y, layer_mask_bounds);
      }
      composite_effect_color(destination, x, y, glow.color, glow_alpha, glow.blend_mode,
                             DissolveField::OuterGlow);
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
  // The COM-calibrated interior pipeline (July 2026, distance-0 probes at sizes
  // 5-40 and chokes 0-50 matched byte-for-byte): choke dilation of the inverse
  // matte, then the tent blur. Padding covers choke reach + tent reach + offset.
  const auto sample_padding = static_cast<int>(std::lround(std::max(0.0F, shadow.size))) +
                              std::max(std::abs(offset_x), std::abs(offset_y)) + 2;
  const auto full_domain = outset_rect(bounds, sample_padding);
  const auto legacy_mask_bounds = clipped_mask_bounds(full_domain, draw_rect, sample_padding);
  const auto [entry, mask_bounds] = style_mask_for_render(
      masks, layer, StyleMaskKind::InnerShadow, effect_index, full_domain, bounds, legacy_mask_bounds, bounds,
      layer_mask_bounds, [&](Rect domain) {
        StyleMaskEntry computed;
        computed.primary =
            layer_alpha_mask(source, layer, bounds, domain, -offset_x, -offset_y, layer_mask_bounds);
        prepare_photoshop_interior_soft_mask(computed.primary, domain.width, domain.height, shadow.size,
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
      composite_effect_color(destination, x, y, shadow.color, shadow_alpha, shadow.blend_mode,
                             DissolveField::InnerShadow);
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

  // Softer (Photoshop's default technique) is the COM-calibrated interior
  // pipeline: inverse matte -> integer choke dilation -> tent blur -> Range
  // gain, with Center as the complement (see prepare_inner_glow_softer_mask).
  // Precise keeps the historical box-blur falloff verbatim (uncalibrated, like
  // the outer glow's Precise; Range is not applied there).
  const auto softer = glow.technique == LayerGlowTechnique::Softer;
  const auto choke_unit = clamp_unit(glow.choke / 100.0F);
  const auto blur_radius = interior_style_blur_radius(glow.size * (1.0F - choke_unit));
  int sample_padding = 0;
  if (softer) {
    // Covers the choke dilation reach plus the tent reach for every choke split.
    sample_padding = static_cast<int>(std::lround(std::max(0.0F, glow.size))) + 2;
  } else {
    // The choke = 0 padding must stay exactly the historical one: a wider window
    // shifts the box blur's running-sum rounding, and choke 0 is pinned bit for bit.
    sample_padding = blur_radius * 3 + 1;
    if (choke_unit > 0.0F) {
      sample_padding += static_cast<int>(std::ceil(std::max(0.0F, glow.size) * choke_unit)) + 1;
    }
  }
  const auto full_domain = outset_rect(bounds, sample_padding);
  const auto legacy_mask_bounds = clipped_mask_bounds(full_domain, draw_rect, sample_padding);
  const auto [entry, mask_bounds] = style_mask_for_render(
      masks, layer, StyleMaskKind::InnerGlow, effect_index, full_domain, bounds, legacy_mask_bounds, bounds,
      layer_mask_bounds, [&](Rect domain) {
        StyleMaskEntry computed;
        computed.primary = layer_alpha_mask(source, layer, bounds, domain, 0, 0, layer_mask_bounds);
        if (softer) {
          prepare_inner_glow_softer_mask(computed.primary, domain.width, domain.height, glow.size,
                                         glow.choke, glow.range,
                                         glow.source == LayerInnerGlowSource::Center);
        } else if (glow.source == LayerInnerGlowSource::Center && choke_unit <= 0.0F) {
          // The historical Center-source path: the blurred matte itself is the glow field.
          blur_mask_in_place(computed.primary, domain.width, domain.height, blur_radius, 3);
        } else {
          prepare_layer_style_interior_falloff_mask(computed.primary, domain.width, domain.height, glow.size,
                                                    glow.choke);
          if (glow.source == LayerInnerGlowSource::Center) {
            // Center source with choke: the glow retreats to the choked core (choke 100
            // leaves a hard Euclidean erosion by the full size).
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
      composite_effect_color(destination, x, y, glow.color, glow_alpha, glow.blend_mode,
                             DissolveField::InnerGlow);
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
      composite_effect_color(destination, x, y, overlay.color, alpha, overlay.blend_mode,
                             DissolveField::ColorOverlay);
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
                             fill.blend_mode, DissolveField::GradientOverlay);
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
      composite_effect_color(destination, x, y, sample.color, alpha, overlay.blend_mode,
                             DissolveField::PatternOverlay);
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
            // slopes shade the whole cell, not just its edges. The gain was
            // recalibrated in the LINEAR regime July 2026 with triangle-wave
            // ramp probes byte-patched into tree_world_a.psd's texture (the
            // old checker probes only exercised saturated hard edges, where
            // clamped shading let a ~5x-too-hot gain still fit): the bump
            // plane feeds the height field with NO extra amplitude, and the
            // size/bevel-depth/texture-depth couplings through normal_scale
            // reproduce PS's measured ratios (size 10 and depth 200% both
            // scale PS's stripe modulation by the same 1.79x they scale
            // ours). Pinned by photoshop-bevel-texture-ramp.psd/bmp.
            constexpr float kTextureAmplitude = 1.0F;
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
      // Locally flat surface: the tent/EDT plateaus far from any contour are
      // exact float constants, so exact-zero differences identify them.
      const auto surface_is_flat = left == right && top == bottom;
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
          // Gloss Contour remaps the Lambert LIGHT VALUE, not the split
          // shading: L' = LUT(clamp(L, 0, 1)), and the highlight/shadow split
          // then runs on L' against the flat-face sin(altitude) (COM-calibrated
          // July 2026, photoshop-gloss-contour fixtures). Flat INTERIOR
          // plateaus genuinely carry the constant LUT(sin alt) wash (Ring
          // at altitude 30 brightens the whole fill by 6/255; the altitude-60
          // arm DARKENS it, pinning the input as L itself, refuting every
          // signed-lighting mapping), while flat EXTERIOR ground stays clean -
          // the shading weight of a locally flat pixel is the matte alpha.
          // Without that gate the flat remap painted constant fog across the
          // whole padded effect rect outside the shape (the
          // pinball_from_photoshop garbage). The pillow shadow keeps its
          // calibrated unnormalized deficit, fed by the remapped raw light.
          // Linear short-circuits so plain bevels stay bit-identical to the
          // historical render.
          if (surface_is_flat) {
            weight *= matte_alpha;
            if (weight <= 0.0F) {
              return;
            }
          }
          const auto remapped_surface = sample_style_contour_lut(
              gloss_lut, clamp_unit(surface_light), bevel.gloss_anti_aliased);
          lighting = remapped_surface >= light_z
                         ? (remapped_surface - light_z) / std::max(0.01F, 1.0F - light_z)
                         : -((light_z - remapped_surface) / std::max(0.01F, light_z));
          if (pillow_family && remapped_surface < light_z) {
            const auto remapped_raw = sample_style_contour_lut(
                gloss_lut, clamp_unit(raw_light), bevel.gloss_anti_aliased);
            if (remapped_raw < light_z) {
              lighting = -((light_z - remapped_raw) / std::max(0.01F, light_z));
            }
          }
        }
        if (lighting > 0.0F) {
          composite_effect_color(destination, x, y, bevel.highlight_color,
                                 clamp_unit(lighting) * weight * bevel.highlight_opacity * layer.opacity(),
                                 bevel.highlight_blend_mode, DissolveField::Bevel);
        } else if (lighting < 0.0F) {
          composite_effect_color(destination, x, y, bevel.shadow_color,
                                 clamp_unit(-lighting) * weight * bevel.shadow_opacity * layer.opacity(),
                                 bevel.shadow_blend_mode, DissolveField::Bevel);
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
  // with the half-covered-or-more pixels plus any painted pixel that is both
  // farther than 2 px from every such solid pixel CENTER and farther than
  // 3 px from every half-coverage crossing of the bilinear field (the same
  // subpixel convention the band anchor uses): an AA fringe always hugs a
  // half-coverage crossing whatever the lattice phase, while a flat sub-half
  // wash never contains one. Distance to solid centers alone misfired on
  // smooth-AA text — fringe pixels at sqrt(5) from every solid center became
  // isolated solid islands and the band bloomed a square size+1 "nub" around
  // each (the bad_stroke.psd regression, July 2026). The crossing reach is
  // 3 px, not the 2 px solid gate: transform re-rasterization stretches
  // glyph-corner ramps so their 1-6/255 tips sit sqrt(8) from every solid
  // center with the crossing up to 2.75 px away (bad_stroke_transformed.psd,
  // 51 corner nubs at a 2 px reach, zero at 3). Residual: a faint tail
  // extending 3+ px past the half-coverage crossing still promotes, as a
  // contiguous ring, not blobs. Binary mattes are identical under all these
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
    constexpr float kAaCrossingReach = 3.0F;
    // Bilinear values are convex combinations of their cell corners, so a
    // half-coverage sample can only exist within sqrt(2) px of a solid pixel
    // center: beyond kAaCrossingReach + sqrt(2) the denial scan cannot
    // succeed and the candidate promotes directly.
    constexpr float kDenialSkip = kAaCrossingReach + 1.4142137F;
    const auto solid_distance = stroke_distance_field(contour, width, height, true);
    for (std::int32_t y = 0; y < height; ++y) {
      for (std::int32_t x = 0; x < width; ++x) {
        const auto index = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                           static_cast<std::size_t>(x);
        if (base[index] <= 0.0F || contour[index] != 0.0F ||
            solid_distance[index] <= kAaFringeReach) {
          continue;
        }
        if (solid_distance[index] > kDenialSkip ||
            !stroke_matte_reaches_half_coverage_near(base, width, height, x, y, kAaCrossingReach)) {
          contour[index] = 1.0F;
        }
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
      composite_effect_color(destination, x, y, color, alpha, stroke.blend_mode, DissolveField::Stroke);
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

// Applies Photoshop's Advanced Blending "Channels" restriction to everything a
// layer (or group) composites through it: a restricted channel keeps the
// backdrop's PREMULTIPLIED value while the other channels and alpha composite
// normally. COM calibration (July 2026): over a half-alpha gray-60 backdrop the
// excluded channel reads 60 * 0.5 / out_alpha, and over an alpha-zero backdrop
// it reads 0, refuting a straight-value keep. Nested restrictions push onto the
// same adapter (the chain ORs), which caps template-instantiation depth exactly
// like GroupMaskedTarget. composite_source_row is deliberately not forwarded so
// restricted layers always take the per-pixel path.
template <typename Base>
class ChannelRestrictedTarget {
public:
  ChannelRestrictedTarget(Base& base, std::uint8_t restriction) : base_(base) {
    push_channel_restriction(restriction);
  }

  void push_channel_restriction(std::uint8_t mask) {
    masks_.push_back(mask);
    combined_ |= mask;
  }
  void pop_channel_restriction() {
    masks_.pop_back();
    combined_ = 0;
    for (const auto mask : masks_) {
      combined_ |= mask;
    }
  }

  void composite_color(std::int32_t x, std::int32_t y, RgbColor color, float alpha, BlendMode mode) {
    const auto pre = base_.sample_color(x, y);
    base_.composite_color(x, y, color, alpha, mode);
    restore_restricted(x, y, pre);
  }

  void composite_special_fill_color(std::int32_t x, std::int32_t y, RgbColor color, float source_coverage,
                                    float fill_opacity, float layer_opacity, BlendMode mode)
    requires requires(Base& base) {
      base.composite_special_fill_color(std::int32_t{}, std::int32_t{}, RgbColor{}, 0.0F, 0.0F, 0.0F,
                                        BlendMode::Normal);
    }
  {
    const auto pre = base_.sample_color(x, y);
    base_.composite_special_fill_color(x, y, color, source_coverage, fill_opacity, layer_opacity, mode);
    restore_restricted(x, y, pre);
  }

  void adjust_color(std::int32_t x, std::int32_t y, const AdjustmentSettings& settings, float amount)
    requires requires(Base& base) {
      base.adjust_color(std::int32_t{}, std::int32_t{}, std::declval<const AdjustmentSettings&>(), 0.0F);
    }
  {
    const auto pre = base_.sample_color(x, y);
    base_.adjust_color(x, y, settings, amount);
    restore_restricted(x, y, pre);
  }

  void adjust_color(std::int32_t x, std::int32_t y, const AdjustmentLut& lut, float amount)
    requires requires(Base& base) {
      base.adjust_color(std::int32_t{}, std::int32_t{}, std::declval<const AdjustmentLut&>(), 0.0F);
    }
  {
    const auto pre = base_.sample_color(x, y);
    base_.adjust_color(x, y, lut, amount);
    restore_restricted(x, y, pre);
  }

  [[nodiscard]] CompositeSample sample_color(std::int32_t x, std::int32_t y) const {
    return base_.sample_color(x, y);
  }

  // Deliberately unrestricted: fade_toward_snapshot lerps two destination
  // states that were both composited through this adapter already.
  void store_color(std::int32_t x, std::int32_t y, RgbColor color, float alpha) {
    base_.store_color(x, y, color, alpha);
  }

  void record_clip_coverage(std::int32_t x, std::int32_t y, float alpha)
    requires requires(Base& base) { base.record_clip_coverage(std::int32_t{}, std::int32_t{}, 0.0F); }
  {
    base_.record_clip_coverage(x, y, alpha);
  }

  void profile_compositor_step(const char* step, const Layer& layer, Rect rect, double elapsed_ms)
    requires requires(Base& base) {
      base.profile_compositor_step(std::declval<const char*>(), std::declval<const Layer&>(), Rect{}, 0.0);
    }
  {
    base_.profile_compositor_step(step, layer, rect, elapsed_ms);
  }

private:
  void restore_restricted(std::int32_t x, std::int32_t y, const CompositeSample& pre) {
    auto post = base_.sample_color(x, y);
    if (post.alpha <= 0.0F) {
      return;  // nothing visible; the premultiplied keep is vacuous at zero coverage
    }
    auto color = post.color;
    bool changed = false;
    const auto restore_channel = [&](std::uint8_t bit, std::uint8_t& post_value, std::uint8_t pre_value) {
      if ((combined_ & bit) == 0U) {
        return;
      }
      // Keeping the PREMULTIPLIED backdrop value: pre straight * pre alpha,
      // re-straightened against the advanced alpha. Equal alphas restore the
      // exact byte so alpha-neutral writes (adjustments) cannot drift.
      const auto kept =
          pre.alpha == post.alpha
              ? pre_value
              : static_cast<std::uint8_t>(std::clamp<long>(
                    std::lround(static_cast<float>(pre_value) * pre.alpha / post.alpha), 0L, 255L));
      if (post_value != kept) {
        post_value = kept;
        changed = true;
      }
    };
    restore_channel(kRestrictRed, color.red, pre.color.red);
    restore_channel(kRestrictGreen, color.green, pre.color.green);
    restore_channel(kRestrictBlue, color.blue, pre.color.blue);
    if (changed) {
      base_.store_color(x, y, color, post.alpha);
    }
  }

  Base& base_;
  std::vector<std::uint8_t> masks_;
  std::uint8_t combined_{0};
};

// Runs fn against a destination that applies the given channel restriction on
// top of whatever the destination already restricts. A zero mask runs fn on the
// destination unchanged; an adapter anywhere in the chain absorbs the push so
// wrapper types cannot nest unboundedly.
template <typename Target, typename Fn>
void with_channel_restriction(Target& destination, std::uint8_t restriction, Fn&& fn) {
  if (restriction == 0U) {
    fn(destination);
    return;
  }
  if constexpr (requires {
                  destination.push_channel_restriction(std::uint8_t{});
                  destination.pop_channel_restriction();
                }) {
    destination.push_channel_restriction(restriction);
    fn(destination);
    destination.pop_channel_restriction();
  } else {
    ChannelRestrictedTarget<Target> restricted(destination, restriction);
    fn(restricted);
  }
}

template <typename Target>
void composite_layer(Target& destination, const Layer& layer, Rect clip,
                     const std::vector<LayerBoundsOverride>* overrides = nullptr,
                     bool throw_on_unsupported_pixel_format = false, StyleMaskProvider* masks = nullptr,
                     const CompositeSnapshot* blend_if_backdrop = nullptr,
                     const PatternStore* patterns = nullptr, bool suppress_channel_restriction = false);

template <typename Target>
void composite_pass_through_group(Target& destination, const Layer& layer, Rect clip,
                                  const std::vector<LayerBoundsOverride>* overrides,
                                  bool throw_on_unsupported_pixel_format, StyleMaskProvider* masks,
                                  const PatternStore* patterns, bool styled);

template <typename Target>
void composite_adjustment_layer(Target& destination, const Layer& layer, Rect clip,
                                const std::vector<LayerBoundsOverride>* overrides,
                                bool suppress_channel_restriction = false) {
  if (!layer_visible_for_render(layer, overrides) || layer.opacity() <= 0.0F) {
    return;
  }
  const auto channel_restriction = layer_rendered_channel_restriction(layer);
  if (channel_restriction == kRestrictAllChannels) {
    return;  // Photoshop removes an all-channels-restricted layer entirely
  }
  if (channel_restriction != 0U && !suppress_channel_restriction) {
    with_channel_restriction(destination, channel_restriction, [&](auto& restricted) {
      composite_adjustment_layer(restricted, layer, clip, overrides, true);
    });
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
  // Adjustment layers otherwise ignore their blend mode entirely (a known
  // approximation). Dissolve is the one mode that has to be honored here,
  // because it is a coverage decision rather than a colour function: the
  // adjustment then lands whole on a dithered subset of the pixels.
  const auto dissolve = layer.blend_mode() == BlendMode::Dissolve;
  for (std::int32_t y = draw_rect.y; y < draw_rect.y + draw_rect.height; ++y) {
    for (std::int32_t x = draw_rect.x; x < draw_rect.x + draw_rect.width; ++x) {
      auto amount = layer_mask_alpha_for_render(layer, x, y, layer_mask_bounds) * layer.opacity() *
                    layer_fill_opacity_for_render(layer);
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
      if (dissolve) {
        amount = dissolve_coverage(x, y, amount, DissolveField::Layer);
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
                           const PatternStore* patterns = nullptr,
                           bool suppress_channel_restriction = false) {
  // Styled GROUPS route through this same pipeline (July 2026): the group's
  // flattened children arrive as an override pixel buffer and the group plays
  // the layer's role. Plain groups never reach here (composite_layer
  // dispatches them to the group branch first).
  if (!layer_visible_for_render(layer, overrides) || layer.opacity() <= 0.0F ||
      (layer.kind() != LayerKind::Pixel && layer.kind() != LayerKind::Group)) {
    return;
  }
  const auto channel_restriction = layer_rendered_channel_restriction(layer);
  if (channel_restriction == kRestrictAllChannels) {
    // All channels excluded: the layer, effects included, does not composite
    // at all (the P4b probe: even its drop shadow vanishes).
    return;
  }
  if (channel_restriction != 0U && !suppress_channel_restriction) {
    // Wrapping here makes the layer's content AND its effects inherit the
    // restriction uniformly (the P4 probe: a drop shadow and a color overlay
    // both keep the backdrop's excluded channel). The clip-run base pass
    // suppresses this and restricts at merge_into instead, because inside the
    // isolated buffer the base's backdrop is transparent black.
    with_channel_restriction(destination, channel_restriction, [&](auto& restricted) {
      composite_pixel_layer(restricted, layer, clip, overrides, throw_on_unsupported_pixel_format, masks,
                            blend_if_backdrop_override, patterns, true);
    });
    return;
  }
  // Folder Fill is ignored, content and effects both (COM probe arm C of
  // photoshop-group-fx-blend-fill; docs/ps-compat.md).
  const float fill_opacity = layer.kind() == LayerKind::Group ? 1.0F : layer_fill_opacity_for_render(layer);

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
  // A non-Normal layer blend mode blends against the backdrop the layer met, not
  // against its own exterior effects (Photoshop COM probes, July 2026: a
  // 50%-alpha Multiply square over a drop shadow keeps the backdrop's blue, which
  // no shadow-then-blend order produces). The base pass therefore reads the
  // pre-effect composite here, and only when an exterior effect can actually
  // reach a pixel the layer also paints.
  const auto has_exterior_effects =
      style.effects_visible &&
      (std::any_of(style.drop_shadows.begin(), style.drop_shadows.end(),
                   [](const LayerDropShadow& shadow) { return shadow.enabled && shadow.opacity > 0.0F; }) ||
       std::any_of(style.outer_glows.begin(), style.outer_glows.end(),
                   [](const LayerOuterGlow& glow) { return glow.enabled && glow.opacity > 0.0F; }));
  std::optional<CompositeSnapshot> pre_effect_backdrop;
  if (has_exterior_effects && layer.blend_mode() != BlendMode::Normal && !has_blend_if &&
      fill_opacity == 1.0F && !draw_rect.empty()) {
    pre_effect_backdrop.emplace(destination, draw_rect);
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

  // Interior overlays on a stroked shape layer apply to the FILL plane and the
  // vector stroke re-composites above them (PS 2026 probes fx-sofi-center /
  // outside, docs/vector-tools.md). A stroke-only shape's overlay covers the
  // stroke, which the legacy combined path already renders. The split planes
  // come from the shape bake; when absent or mismatched (no stroke, non-Normal
  // stroke blend, transform-preview override, preserved import raster) the
  // legacy behavior stands. Blend-If layers keep the legacy path too - the
  // re-stamp cannot reproduce the per-pixel gate.
  const PixelBuffer* interior_source = &source;
  const PixelBuffer* stroke_restamp = nullptr;
  if (style.effects_visible && !has_blend_if) {
    if (const auto* shape = layer.vector_shape();
        shape != nullptr && !shape->stroke_cache.empty() && !shape->fill_cache.empty() &&
        &source == &layer.pixels() && shape->fill_cache.width() == source.width() &&
        shape->fill_cache.height() == source.height() && shape->stroke_cache.width() == source.width() &&
        shape->stroke_cache.height() == source.height()) {
      // Gate on an overlay that will actually paint: a needless re-stamp would
      // double-composite the stroke's AA edges.
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

  // Photoshop resolves the interior overlays INTO one styled color and applies
  // Fill/layer Opacity and the backdrop once to that result, so a 100%/Normal
  // overlay hides the layer's own pixels outright. Folding them into the base
  // pass is the only way to reproduce that: compositing each overlay as its own
  // pass scaled by layer.opacity() leaves (1 - opacity) of the layer's own color
  // showing through an opaque overlay, and over-composites semi-transparent
  // interiors instead of knocking them out. Same treatment Satin already gets,
  // and the same escapes: Blend If (Photoshop does not gate effects with it),
  // Fill Opacity (which scales the layer's pixels but not its effects, so the
  // overlay cannot ride the base alpha) and the vector fill/stroke split
  // (folding would tint the stroke) keep the legacy passes.
  const bool fold_interior_overlays_into_base =
      style.effects_visible && !has_blend_if && fill_opacity == 1.0F && stroke_restamp == nullptr;
  std::vector<PreparedInteriorOverlay> folded_overlays;
  if (fold_interior_overlays_into_base && !draw_rect.empty()) {
    profile_compositor_step(destination, layer, "interior_overlays", draw_rect, [&] {
      folded_overlays = prepare_interior_overlays(layer, style, source, bounds, patterns);
    });
  }

  // WHAT the fold lands on is Photoshop's "Blend Interior Effects as Group"
  // blending option ('infx'), off by default. Off, the layer's blend mode
  // carries its own pixels only and the interior effects blend over that result
  // with their own modes; on, they fold into the layer's color first and the
  // layer's blend mode carries everything (COM probes July 2026: a Saturation
  // layer with a Linear Dodge Gradient Overlay renders the gradient at full
  // strength with infx off and desaturated through the layer's mode with it on).
  // Both readings agree whenever the layer's blend mode is Normal, which is why
  // that path keeps the cheaper source-color fold and its pinned bytes.
  const bool fold_after_layer_blend = !style.blend_interior_elements;
  const bool has_interior_folds = !folded_overlays.empty() || !prepared_satins.empty();
  const bool blend_against_backdrop =
      layer.blend_mode() != BlendMode::Normal && !has_blend_if && fill_opacity == 1.0F &&
      ((has_interior_folds && fold_after_layer_blend) || has_exterior_effects);

  if (!draw_rect.empty()) {
    profile_compositor_step(destination, layer, "base_pixels", draw_rect, [&] {
      const auto format = source.format();
      const auto channels = format.channels;
      const auto* source_bytes = source.data().data();
      const auto source_stride = source.stride_bytes();
      const auto has_enabled_mask = (layer.mask().has_value() && !layer.mask()->disabled) ||
                                    layer_has_enabled_vector_mask(layer);
      const auto has_folded_overlays = !folded_overlays.empty();
      bool composited_by_target = false;
      if (!has_blend_if && !has_enabled_mask && prepared_satins.empty() && folded_overlays.empty() &&
          knockout == nullptr && fill_opacity == 1.0F && layer.blend_mode() == BlendMode::Normal) {
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
            const auto special_fill = fill_opacity != 1.0F &&
                                      blend_mode_has_special_fill(layer.blend_mode());
            auto alpha = source_coverage * layer.opacity();
            if (fill_opacity != 1.0F) {
              alpha *= fill_opacity;
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
            // Overlays sit under Satin in Photoshop's interior stack, so they
            // fold first.
            const auto fold_interiors = [&](std::array<std::uint8_t, 3> color) {
              if (has_folded_overlays) {
                color = fold_interior_overlays(color, folded_overlays, x, y);
              }
              if (!has_blend_if && fill_opacity == 1.0F) {
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
                  const auto& effect_color = prepared.effect->color;
                  // Deliberately not routed through fold_effect_color: this
                  // fold keeps the plain model for the burn/dodge modes, and
                  // changing that would move pinned satin bytes. Dissolve is
                  // handled here for the same reason.
                  if (prepared.effect->blend_mode == BlendMode::Dissolve) {
                    if (dissolve_coverage(x, y, coverage, DissolveField::Satin) > 0.0F) {
                      color = {effect_color.red, effect_color.green, effect_color.blue};
                    }
                    continue;
                  }
                  color = composite_blended_rgb({effect_color.red, effect_color.green, effect_color.blue}, color,
                                                prepared.effect->blend_mode, coverage, 1.0F);
                }
              }
              return color;
            };
            if (!fold_after_layer_blend) {
              styled_color = fold_interiors(styled_color);
            }
            if (blend_against_backdrop) {
              const auto backdrop = pre_effect_backdrop.has_value() ? pre_effect_backdrop->sample_color(x, y)
                                                                    : destination.sample_color(x, y);
              styled_color = composite_blended_rgb(
                  styled_color, {backdrop.color.red, backdrop.color.green, backdrop.color.blue},
                  layer.blend_mode(), 1.0F, backdrop.alpha);
            }
            if (fold_after_layer_blend) {
              styled_color = fold_interiors(styled_color);
            }
            if (special_fill) {
              destination.composite_special_fill_color(
                  x, y, RgbColor{styled_color[0], styled_color[1], styled_color[2]},
                  source_coverage * blend_if_factor, fill_opacity, layer.opacity(), layer.blend_mode());
            } else {
              // The blend already happened against the pre-effect backdrop on
              // that path, so the composite is a plain source-over.
              auto composite_mode = blend_against_backdrop ? BlendMode::Normal : layer.blend_mode();
              // Keyed on the LAYER's mode, not composite_mode: the
              // blend-against-backdrop path above already rewrote the latter to
              // Normal, and for Dissolve that pre-blend is an identity pass
              // because Dissolve's colour function is the source.
              if (layer.blend_mode() == BlendMode::Dissolve) {
                // Coverage is the paint probability; the pixels that survive
                // land at full strength through Normal. This runs AFTER
                // record_clip_coverage above, because a clipping run is masked
                // by the base's transparency and not by what it painted.
                alpha = dissolve_coverage(x, y, alpha, DissolveField::Layer);
                if (alpha <= 0.0F) {
                  continue;
                }
                composite_mode = BlendMode::Normal;
              }
              destination.composite_color(x, y, RgbColor{styled_color[0], styled_color[1], styled_color[2]}, alpha,
                                          composite_mode);
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
  if ((has_blend_if || fill_opacity != 1.0F) && !draw_rect.empty() && !prepared_satins.empty()) {
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
              composite_effect_color(destination, x, y, prepared.effect->color, alpha, prepared.effect->blend_mode,
                                     DissolveField::Satin);
            }
          }
        }
      }
    });
  }

  if (style.effects_visible) {
    // Overlay stacking pinned against Photoshop 2026 (pairwise 100%-opacity
    // probes): pattern under gradient under color, i.e. Color Overlay paints
    // last. The historical color-then-gradient order was inverted vs PS. These
    // destination passes only run for the cases the base-pass fold above cannot
    // take (Blend If, Fill Opacity, the vector fill/stroke split).
    if (!fold_interior_overlays_into_base) {
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
            const auto special_fill = fill_opacity != 1.0F &&
                                      blend_mode_has_special_fill(layer.blend_mode());
            auto alpha = source_coverage * layer.opacity();
            if (fill_opacity != 1.0F) {
              alpha *= fill_opacity;
            }
            if (alpha <= 0.0F) {
              continue;
            }
            const auto color = RgbColor{px[0], px[1], px[2]};
            if (special_fill) {
              destination.composite_special_fill_color(x, y, color, source_coverage,
                                                       fill_opacity, layer.opacity(),
                                                       layer.blend_mode());
            } else if (layer.blend_mode() == BlendMode::Dissolve) {
              // The stroke shares the layer's blend mode, so it dithers on the
              // same field as the base pass and lands on the same pixels.
              if (dissolve_coverage(x, y, alpha, DissolveField::Layer) > 0.0F) {
                destination.composite_color(x, y, color, 1.0F, BlendMode::Normal);
              }
            } else {
              destination.composite_color(x, y, color, alpha, layer.blend_mode());
            }
          }
        }
      });
    }
    // Interior effects paint ABOVE the overlays, glow below shadow (Photoshop's
    // interior stack bottom-to-top: pattern/gradient/color overlay, satin,
    // inner glow, inner shadow - the Layer Style dialog's list order mirrored).
    // COM-pinned July 2026 on capsule_v_top.psd: its ColorDodge inner glow
    // brightens the gradient-overlay-lightened content (blend reads the
    // destination AFTER overlays), where the historical effects-then-overlays
    // order dodged the raw dark pixels and the 15% overlay then washed the rim
    // out entirely.
    for (std::uint32_t index = 0; index < style.inner_glows.size(); ++index) {
      const auto& glow = style.inner_glows[index];
      profile_compositor_step(destination, layer, "inner_glow", clip, [&] {
        render_inner_glow(destination, layer, source, clip, bounds, glow, layer_mask_bounds, masks, index,
                          knockout);
      });
    }
    for (std::uint32_t index = 0; index < style.inner_shadows.size(); ++index) {
      const auto& shadow = style.inner_shadows[index];
      profile_compositor_step(destination, layer, "inner_shadow", clip, [&] {
        render_inner_shadow(destination, layer, source, clip, bounds, shadow, layer_mask_bounds, masks, index,
                            knockout);
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
// in normally, then freeze_clip() locks the clipping shape to the base's own
// CONTENT coverage - source alpha x layer/vector mask x opacity - recorded
// through record_clip_coverage() as the base paints. Photoshop clips members
// to the base's transparency alone, so the base's layer STYLES must not widen
// it: they still composite into the buffer and merge normally, they just do
// not license a clipped member to paint where the base itself is absent.
// Clipped members blend against the base's COLOR at full strength
// (destination alpha 1 - Photoshop's default "Blend Clipped Layers as Group"
// semantics) without growing coverage, and a clipped adjustment layer's
// adjust_color touches only masked pixels. merge_into() then lays the ensemble
// into the real destination with the base's blend mode; the base's own opacity
// is already folded into the frozen alpha, so the group fades as a unit.
//
// records_clip_coverage is set only for instances that will freeze (real clip
// runs); isolated non-pass-through groups reuse this buffer without a clip
// shape and skip the per-pixel bookkeeping.
class IsolatedClipGroupTarget {
public:
  explicit IsolatedClipGroupTarget(Rect rect, bool records_clip_coverage = false)
      : rect_(rect),
        rgb_(static_cast<std::size_t>(std::max(0, rect.width)) * static_cast<std::size_t>(std::max(0, rect.height)) *
                 3U,
             0),
        alpha_(static_cast<std::size_t>(std::max(0, rect.width)) * static_cast<std::size_t>(std::max(0, rect.height)),
               0.0F),
        clip_alpha_(alpha_.size(), 0.0F),
        records_clip_coverage_(records_clip_coverage) {}

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

  // The flattened straight-RGBA content, for routing a styled group's merged
  // children through the layer-effect pipeline (July 2026; the colors are
  // stored straight, so this is a plain re-pack).
  [[nodiscard]] PixelBuffer to_pixel_buffer() const {
    PixelBuffer buffer(rect_.width, rect_.height, PixelFormat::rgba8());
    for (std::int32_t y = 0; y < rect_.height; ++y) {
      auto row = buffer.row(y);
      const auto* rgb =
          rgb_.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(rect_.width) * 3U;
      const auto* alpha = alpha_.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(rect_.width);
      for (std::int32_t x = 0; x < rect_.width; ++x) {
        auto* px = row.data() + static_cast<std::size_t>(x) * 4U;
        px[0] = rgb[static_cast<std::size_t>(x) * 3U];
        px[1] = rgb[static_cast<std::size_t>(x) * 3U + 1U];
        px[2] = rgb[static_cast<std::size_t>(x) * 3U + 2U];
        px[3] = static_cast<std::uint8_t>(std::lround(clamp_unit(alpha[static_cast<std::size_t>(x)]) * 255.0F));
      }
    }
    return buffer;
  }

  // Direct overwrite for fade_toward_snapshot (a faded pass-through group
  // nested inside an isolated group). Groups are never composited into a
  // frozen instance (layer_clipped_for_render excludes groups from clip runs),
  // and the lerped alpha lies between two states that both respected any clip
  // cap, so no clip_alpha interaction is needed.
  void store_color(std::int32_t x, std::int32_t y, RgbColor color, float alpha) {
    x -= rect_.x;
    y -= rect_.y;
    if (x < 0 || y < 0 || x >= rect_.width || y >= rect_.height) {
      return;
    }
    const auto index =
        static_cast<std::size_t>(y) * static_cast<std::size_t>(rect_.width) + static_cast<std::size_t>(x);
    auto* dst = rgb_.data() + index * 3U;
    dst[0] = color.red;
    dst[1] = color.green;
    dst[2] = color.blue;
    alpha_[index] = clamp_unit(alpha);
  }

  void record_clip_coverage(std::int32_t x, std::int32_t y, float alpha) noexcept {
    if (frozen_ || !records_clip_coverage_) {
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

  // Locks the clipping shape. clip_alpha_ already holds exactly the base's
  // content coverage; the accumulated alpha_ must NOT be substituted, because
  // by now it also carries the base's drop shadow, glow, and stroke output.
  void freeze_clip() noexcept { frozen_ = true; }

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
        const auto color = RgbColor{px[0], px[1], px[2]};
        if (mode == BlendMode::Dissolve) {
          if (dissolve_coverage(rect_.x + x, rect_.y + y, alpha, DissolveField::Layer) > 0.0F) {
            destination.composite_color(rect_.x + x, rect_.y + y, color, 1.0F, BlendMode::Normal);
          }
          continue;
        }
        destination.composite_color(rect_.x + x, rect_.y + y, color, alpha, mode);
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
        if (alpha <= 0.0F) {
          continue;
        }
        // A Dissolve group dithers its MERGED result, so the children keep
        // compositing against each other at full strength first.
        if (mode == BlendMode::Dissolve) {
          if (dissolve_coverage(rect_.x + x, rect_.y + y, alpha, DissolveField::Layer) > 0.0F) {
            destination.composite_color(rect_.x + x, rect_.y + y, color, 1.0F, BlendMode::Normal);
          }
          continue;
        }
        destination.composite_color(rect_.x + x, rect_.y + y, color, alpha, mode);
      }
    }
  }

private:
  Rect rect_{};
  std::vector<std::uint8_t> rgb_;
  std::vector<float> alpha_;
  std::vector<float> clip_alpha_;
  bool records_clip_coverage_{false};
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

  // Deliberately NOT attenuated by mask_alpha: fade_toward_snapshot lerps two
  // destination states that already include this chain's mask attenuation, so
  // masking the store would double-apply the masks.
  void store_color(std::int32_t x, std::int32_t y, RgbColor color, float alpha)
    requires requires(Base& base) { base.store_color(std::int32_t{}, std::int32_t{}, RgbColor{}, 0.0F); }
  {
    base_.store_color(x, y, color, alpha);
  }

  void record_clip_coverage(std::int32_t x, std::int32_t y, float alpha)
    requires requires(Base& base) { base.record_clip_coverage(std::int32_t{}, std::int32_t{}, 0.0F); }
  {
    base_.record_clip_coverage(x, y, alpha * mask_alpha(x, y));
  }

  // Channel restrictions push through the mask chain onto an inner
  // ChannelRestrictedTarget, keeping the wrapper set {plain, restricted,
  // masked-restricted} closed under nesting instead of growing new template
  // instantiations.
  void push_channel_restriction(std::uint8_t mask)
    requires requires(Base& base) { base.push_channel_restriction(std::uint8_t{}); }
  {
    base_.push_channel_restriction(mask);
  }

  void pop_channel_restriction()
    requires requires(Base& base) { base.pop_channel_restriction(); }
  {
    base_.pop_channel_restriction();
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
    IsolatedClipGroupTarget group(group_rect, /*records_clip_coverage=*/true);
    // A restricted clip BASE must not self-wrap inside the isolated buffer
    // (its backdrop there is transparent black, so the kept channel would
    // merge as black); the restriction instead rides the merge below, gating
    // the whole clipped ensemble (the P7 probe: a member over a G-restricted
    // base keeps the ORIGINAL backdrop's green). Restricted MEMBERS self-wrap
    // normally: their backdrop is the base content (the P7b probe).
    composite_layer(group, layer, group_rect, overrides, throw_on_unsupported_pixel_format, masks,
                    base_backdrop.has_value() ? &*base_backdrop : nullptr, patterns,
                    /*suppress_channel_restriction=*/true);
    group.freeze_clip();
    for (std::size_t member = index + 1; member < run_end; ++member) {
      composite_layer(group, siblings[member], group_rect, overrides, throw_on_unsupported_pixel_format, masks,
                      nullptr, patterns);
    }
    with_channel_restriction(destination, layer_rendered_channel_restriction(layer),
                             [&](auto& merge_destination) {
                               group.merge_into(merge_destination, layer.blend_mode());
                             });
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
                     const CompositeSnapshot* blend_if_backdrop, const PatternStore* patterns,
                     bool suppress_channel_restriction) {
  if (!layer_visible_for_render(layer, overrides) || layer.opacity() <= 0.0F) {
    return;
  }

  if (layer.kind() == LayerKind::Group) {
    if (layer_rendered_channel_restriction(layer) == kRestrictAllChannels) {
      // All channels excluded removes the group and its effects entirely,
      // mirroring the pixel-layer rule (the P4b probe).
      return;
    }
    // A group whose own style renders (July 2026; COM-calibrated rules in
    // docs/ps-compat.md): the flattened children become the pipeline's source
    // buffer and the group plays the layer's role (blend mode, opacity, mask,
    // blend-if; folder Fill stays ignored via layer_fill_opacity_for_render).
    const bool styled = group_style_renders(layer);
    // Blend-if groups and every non-pass-through group isolate: children
    // composite against transparency and the merged result meets the backdrop
    // with the group's blend mode, opacity, and mask (Photoshop's isolated
    // transparency group).
    if (layer_has_rendered_blend_if(layer) || layer.blend_mode() != BlendMode::PassThrough) {
      // Blend-if groups keep the calibrated full-clip buffer; the plain
      // isolated path bounds it by the children's render bounds instead
      // (layer_render_bounds ignores LayerBoundsOverride, so overrides also
      // fall back to the full clip). Coverage can only exist where pixel
      // children painted, so the bounded buffer merges identically.
      const auto isolated_rect = overrides == nullptr && !layer_has_rendered_blend_if(layer)
                                     ? intersect_rect(clip, layer_render_bounds(layer))
                                     : clip;
      if (isolated_rect.empty()) {
        return;
      }
      // Unsupported blend-if payloads are raw-preserved, never rendered: a
      // non-pass-through group without a RENDERED blend-if merges with
      // identity ranges.
      const auto blend_if = layer_has_rendered_blend_if(layer) ? layer.blend_if() : LayerBlendIf{};
      std::optional<CompositeSnapshot> backdrop;
      if (blend_if_has_underlying_ranges(blend_if) && !styled) {
        backdrop.emplace(destination, isolated_rect);
      }
      IsolatedClipGroupTarget isolated(isolated_rect);
      composite_layers(isolated, layer.children(), isolated_rect, overrides, throw_on_unsupported_pixel_format,
                       masks, patterns);
      if (styled) {
        // Route the merged content through the full styled pipeline. The
        // group mask has NOT been applied yet (merge_layer_into is skipped),
        // so the pipeline's own mask handling applies it exactly once, which
        // also makes every effect derive from the masked silhouette (the
        // photoshop-group-fx-mask-stroke probe).
        const PixelBuffer flattened = isolated.to_pixel_buffer();
        const auto* outer_override = layer_override_for_render(layer, overrides);
        std::vector<LayerBoundsOverride> styled_override{LayerBoundsOverride{
            layer.id(), isolated_rect, &flattened,
            outer_override != nullptr ? outer_override->mask_bounds : std::nullopt,
            std::optional<bool>{}}};
        composite_pixel_layer(destination, layer, clip, &styled_override,
                              throw_on_unsupported_pixel_format, masks, blend_if_backdrop, patterns);
        return;
      }
      // A restricted isolated group applies its restriction where the merged
      // result meets the backdrop (the P5 isolated arm: the group's excluded
      // channel keeps the backdrop under Normal-mode children).
      with_channel_restriction(destination, layer_rendered_channel_restriction(layer),
                               [&](auto& merge_destination) {
                                 isolated.merge_layer_into(merge_destination, layer, blend_if,
                                                           backdrop.has_value() ? &*backdrop : nullptr,
                                                           layer_mask_bounds_for_render(layer, overrides));
                               });
      return;
    }
    // The restriction survives pass-through (the P5 pass-through arm: a
    // Multiply child keeps blending with the outside backdrop while the
    // group's excluded channel holds it): no isolation, every write the
    // children and the group's effects make is gated in place.
    with_channel_restriction(destination, layer_rendered_channel_restriction(layer),
                             [&](auto& pass_destination) {
                               composite_pass_through_group(pass_destination, layer, clip, overrides,
                                                            throw_on_unsupported_pixel_format, masks,
                                                            patterns, styled);
                             });
    return;
  }

  if (layer.kind() == LayerKind::Adjustment) {
    composite_adjustment_layer(destination, layer, clip, overrides, suppress_channel_restriction);
    return;
  }

  composite_pixel_layer(destination, layer, clip, overrides, throw_on_unsupported_pixel_format, masks,
                        blend_if_backdrop, patterns, suppress_channel_restriction);
}

// The pass-through tail of composite_layer: children composite against the
// live backdrop (optionally through the group's mask), group styles derive
// from the flattened silhouette, and group Opacity fades the result back
// toward the pre-group snapshot.
template <typename Target>
void composite_pass_through_group(Target& destination, const Layer& layer, Rect clip,
                                  const std::vector<LayerBoundsOverride>* overrides,
                                  bool throw_on_unsupported_pixel_format, StyleMaskProvider* masks,
                                  const PatternStore* patterns, bool styled) {
  // Pass-through group Opacity is a post-composite fade (fade_toward_snapshot):
  // snapshot the backdrop, composite the children at full strength, then
  // interpolate. The fade covers the full clip because an interior adjustment
  // with unlimited bounds can touch backdrop pixels outside the children's
  // render bounds.
  std::optional<CompositeSnapshot> before;
  if (layer.opacity() < 1.0F) {
    before.emplace(destination, clip);
  }
  // Styled pass-through groups do NOT isolate (the
  // photoshop-group-fx-passthrough probe: a Multiply child keeps blending
  // with the outside backdrop under a group drop shadow). The effects
  // derive from the flattened silhouette - with child opacities, after the
  // group mask via the renderers' own mask folding - exterior effects paint
  // BEHIND the children and interior effects above them.
  std::optional<PixelBuffer> silhouette;
  Rect silhouette_rect{};
  if (styled) {
    silhouette_rect = intersect_rect(clip, layer_render_bounds(layer));
    if (!silhouette_rect.empty()) {
      IsolatedClipGroupTarget isolated(silhouette_rect);
      composite_layers(isolated, layer.children(), silhouette_rect, overrides,
                       throw_on_unsupported_pixel_format, masks, patterns);
      silhouette = isolated.to_pixel_buffer();
      const auto& style = layer.layer_style();
      const auto mask_bounds = layer_mask_bounds_for_render(layer, overrides);
      if (style.effects_visible) {
        for (std::uint32_t index = 0; index < style.drop_shadows.size(); ++index) {
          render_drop_shadow(destination, layer, *silhouette, clip, silhouette_rect,
                             style.drop_shadows[index], mask_bounds, masks, index);
        }
        for (std::uint32_t index = 0; index < style.outer_glows.size(); ++index) {
          render_outer_glow(destination, layer, *silhouette, clip, silhouette_rect,
                            style.outer_glows[index], mask_bounds, masks, index);
        }
      }
    }
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
  } else {
    composite_layers(destination, layer.children(), clip, overrides, throw_on_unsupported_pixel_format, masks,
                     patterns);
  }
  // Interior effects paint ABOVE the pass-through content, masked by the
  // silhouette, each with its OWN blend mode (the photoshop-group-fx-interior
  // probe: a Normal overlay covers a Multiply child's composite at full
  // strength). Stack order mirrors composite_pixel_layer's destination
  // passes: overlays under satin under inner glow/shadow, stroke, bevel.
  if (silhouette.has_value()) {
    const auto& style = layer.layer_style();
    const auto mask_bounds = layer_mask_bounds_for_render(layer, overrides);
    const auto draw_rect = intersect_rect(clip, silhouette_rect);
    if (style.effects_visible && !draw_rect.empty()) {
      for (const auto& overlay : style.pattern_overlays) {
        render_pattern_overlay(destination, layer, *silhouette, clip, silhouette_rect, overlay,
                               mask_bounds, patterns, nullptr);
      }
      for (const auto& fill : style.gradient_fills) {
        render_gradient_fill(destination, layer, *silhouette, clip, silhouette_rect, fill,
                             mask_bounds, nullptr);
      }
      for (const auto& overlay : style.color_overlays) {
        render_color_overlay(destination, layer, *silhouette, clip, silhouette_rect, overlay,
                             mask_bounds, nullptr);
      }
      for (std::uint32_t index = 0; index < style.satins.size(); ++index) {
        const auto& satin = style.satins[index];
        if (!satin.enabled || satin.opacity <= 0.0F) {
          continue;
        }
        const auto prepared = prepare_satin(layer, *silhouette, draw_rect, silhouette_rect, satin,
                                            mask_bounds, masks, index);
        const auto channels = silhouette->format().channels;
        const auto* source_bytes = silhouette->data().data();
        const auto source_stride = silhouette->stride_bytes();
        for (std::int32_t y = draw_rect.y; y < draw_rect.y + draw_rect.height; ++y) {
          const auto* source_row =
              source_bytes + static_cast<std::size_t>(y - silhouette_rect.y) * source_stride;
          for (std::int32_t x = draw_rect.x; x < draw_rect.x + draw_rect.width; ++x) {
            const auto* src = source_row + static_cast<std::size_t>(x - silhouette_rect.x) * channels;
            const auto source_alpha = (channels >= 4 ? static_cast<float>(src[3]) / 255.0F : 1.0F) *
                                      layer_mask_alpha_for_render(layer, x, y, mask_bounds) *
                                      layer.opacity();
            if (source_alpha <= 0.0F) {
              continue;
            }
            const auto mask_index = static_cast<std::size_t>(y - prepared.mask_bounds.y) *
                                        static_cast<std::size_t>(prepared.mask_bounds.width) +
                                    static_cast<std::size_t>(x - prepared.mask_bounds.x);
            const auto alpha =
                source_alpha * prepared.entry->primary[mask_index] * clamp_unit(prepared.effect->opacity);
            if (alpha > 0.0F) {
              composite_effect_color(destination, x, y, prepared.effect->color, alpha,
                                     prepared.effect->blend_mode, DissolveField::Satin);
            }
          }
        }
      }
      for (std::uint32_t index = 0; index < style.inner_glows.size(); ++index) {
        render_inner_glow(destination, layer, *silhouette, clip, silhouette_rect,
                          style.inner_glows[index], mask_bounds, masks, index, nullptr);
      }
      for (std::uint32_t index = 0; index < style.inner_shadows.size(); ++index) {
        render_inner_shadow(destination, layer, *silhouette, clip, silhouette_rect,
                            style.inner_shadows[index], mask_bounds, masks, index, nullptr);
      }
      for (std::uint32_t index = 0; index < style.strokes.size(); ++index) {
        render_stroke(destination, layer, *silhouette, clip, silhouette_rect, style.strokes[index],
                      mask_bounds, masks, index);
      }
      for (std::uint32_t index = 0; index < style.bevels.size(); ++index) {
        if (style.bevels[index].style == BevelEmbossStyleKind::StrokeEmboss) {
          continue;
        }
        render_bevel_emboss(destination, layer, *silhouette, clip, silhouette_rect,
                            style.bevels[index], mask_bounds, masks, index, patterns, &style.strokes);
      }
      for (std::uint32_t index = 0; index < style.bevels.size(); ++index) {
        if (style.bevels[index].style != BevelEmbossStyleKind::StrokeEmboss) {
          continue;
        }
        render_bevel_emboss(destination, layer, *silhouette, clip, silhouette_rect,
                            style.bevels[index], mask_bounds, masks, index, patterns, &style.strokes);
      }
    }
  }
  if (before.has_value()) {
    fade_toward_snapshot(destination, *before, clip, layer.opacity());
  }
}

}  // namespace patchy::render_detail
