#include "render/layer_compositor.hpp"

// The non-template half of the reference compositor: per-layer preparation and the
// per-pixel fold and stroke-knockout helpers the render templates call. Bodies moved
// verbatim out of layer_compositor.hpp (October 2026); their math is byte-pinned by the
// compositor canaries and the composite corpus digests, so keep it verbatim.

namespace patchy::render_detail {

Rect paint_bounds_for_render(const Layer& layer, const PixelBuffer& pixels, Rect bounds,
                             bool aligned, int effect_padding) {
  if (const auto* appearance = raster_view_appearance(layer.id())) {
    if (aligned) {
      const auto* shape = layer.vector_shape();
      if (shape && &pixels == &shape->fill_cache && !appearance->fill_visible_bounds.empty()) {
        return appearance->fill_visible_bounds;
      }
    }
    return aligned ? appearance->visible_bounds : outset_rect(appearance->bounds, effect_padding);
  }
  return aligned ? layer_visible_alpha_bounds(layer, pixels, bounds).value_or(bounds) : bounds;
}

// Overrides-aware twin of layer_render_bounds: unions in any
// LayerBoundsOverride rect (move/transform previews substitute bounds and
// pixels), so isolated groups can bound their buffers while a preview
// override is active instead of falling back to the full clip. The result is
// a SUPERSET of anywhere the subtree can paint - an overridden layer
// contributes both its historical rect and the override rect, each padded by
// its effects - which is all the isolation merge needs (coverage can only
// exist where pixel children painted).
Rect layer_render_bounds_for_render(const Layer& layer, const std::vector<LayerBoundsOverride>* overrides) {
  if (overrides == nullptr || overrides->empty() || (layer.kind() == LayerKind::Group && layer.artboard().has_value())) {
    // An artboard's extent is its rect whatever its children's previews do.
    return layer_render_bounds(layer);
  }
  if (layer.kind() == LayerKind::Group) {
    Rect bounds;
    for (const auto& child : layer.children()) {
      bounds = unite_rect(bounds, layer_render_bounds_for_render(child, overrides));
    }
    const auto own_padding = layer_style_effect_padding(layer.layer_style());
    return bounds.empty() || own_padding <= 0 ? bounds : outset_rect(bounds, own_padding);
  }
  const auto* override = layer_override_for_render(layer, overrides);
  if (override == nullptr) {
    return layer_render_bounds(layer);
  }
  return unite_rect(layer_render_bounds(layer), layer_bounds_with_effects(layer, override->bounds));
}

// A styled group's flattened children without the group's own effect
// padding. The group's effects read this buffer and paint straight into the
// destination, exactly like a pixel layer's unpadded source, so padding it
// only allocated transparency (60000 px of it around a 30000 px drop shadow).
// The children's own effects stay inside, since they are group content.
Rect group_content_bounds_for_render(const Layer& group, const std::vector<LayerBoundsOverride>* overrides) {
  Rect bounds;
  for (const auto& child : group.children()) {
    bounds = unite_rect(bounds, layer_render_bounds_for_render(child, overrides));
  }
  if (const auto& artboard = group.artboard(); artboard.has_value()) {
    // An artboard's content is its rect: children clip to it, and its paper fills it.
    bounds = intersect_rect(bounds, artboard->rect);
    if (artboard_background_rgba(*artboard).has_value()) {
      bounds = unite_rect(bounds, artboard->rect);
    }
  }
  return bounds;
}

// Photoshop's "Layer Mask Hides Effects" ('lmgm') and "Vector Mask Hides Effects"
// ('vmgm') blending options: a mask with its option on stops shaping the effects
// and hides their output where it lands instead (effect_output_mask_alpha_for_render
// multiplies only those masks in). Each mask switches on its own.
bool layer_mask_clips_effect_output(const Layer& layer) {
  return (layer.layer_style().layer_mask_hides_effects && layer.mask().has_value() &&
          !layer.mask()->disabled) ||
         layer_vector_mask_hides_effects(layer);
}

// Photoshop supersamples the Shape Burst ramp across each pixel's footprint:
// a 1-2-1 tent of the sharp per-center colors reproduces both the title.psd
// silver band's reduced amplitude and the probe fixtures' clamped tail bytes
// (a symmetric filter is invisible on the probes' interior linear ramp, which
// is why the sharp model also matched them mid-band).
RgbColor shape_burst_stroke_color(const LayerStyleGradient& gradient, float band_position,
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

float shape_burst_stroke_opacity(const LayerStyleGradient& gradient, float band_position,
                                 float ramp_span) {
  const auto step = 1.0F / std::max(1.0F, ramp_span);
  const auto sample = [&](float band) {
    return gradient_stop_opacity(gradient, shape_burst_gradient_position(gradient, band), true);
  };
  return 0.25F * (sample(band_position - step) + 2.0F * sample(band_position) +
                  sample(band_position + step));
}

// RGB-space twin of composite_effect_color, for interior effects that fold into
// the layer's own straight color instead of compositing onto the destination.
// The LinearBurn/ColorBurn/ColorDodge opacity pre-fold has to come along or
// those modes lose their Photoshop calibration.
[[nodiscard]] std::array<std::uint8_t, 3> fold_effect_color(std::array<std::uint8_t, 3> destination,
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

// Deep twin of fold_effect_color (docs/high-bit-depth.md): the effect color is an 8-bit
// parameter converted into the domain; the burn/dodge pre-folds keep their 8-bit form.
[[nodiscard]] DeepChannels fold_effect_color(DeepChannels destination, RgbColor color, float alpha,
                                             BlendMode mode, std::int32_t x, std::int32_t y,
                                             DissolveField field, DeepDomain domain) {
  const auto deep = [domain](RgbColor value) { return deep_channels(deep_from_byte(value, domain)); };
  if (mode == BlendMode::Dissolve) {
    return dissolve_coverage(x, y, alpha, field) > 0.0F ? deep(color) : destination;
  }
  if (mode == BlendMode::LinearBurn || mode == BlendMode::ColorBurn) {
    const auto fold = [alpha](std::uint8_t channel) {
      return static_cast<std::uint8_t>(
          std::clamp<long>(std::lround(255.0F - (255.0F - static_cast<float>(channel)) * alpha), 0L, 255L));
    };
    return composite_blended_rgb_deep(deep(RgbColor{fold(color.red), fold(color.green), fold(color.blue)}),
                                      destination, mode, alpha > 0.0F ? 1.0F : 0.0F, 1.0F, domain);
  }
  if (mode == BlendMode::ColorDodge) {
    const auto fold = [alpha](std::uint8_t channel) {
      return static_cast<std::uint8_t>(std::clamp<long>(std::lround(static_cast<float>(channel) * alpha), 0L, 255L));
    };
    return composite_blended_rgb_deep(deep(RgbColor{fold(color.red), fold(color.green), fold(color.blue)}),
                                      destination, mode, alpha > 0.0F ? 1.0F : 0.0F, 1.0F, domain);
  }
  return composite_blended_rgb_deep(deep(color), destination, mode, alpha, 1.0F, domain);
}

// Resolves the interior overlays a layer will fold into its own color. An
// unresolvable pattern is dropped here, which renders nothing - the same
// outcome render_pattern_overlay gives it.
[[nodiscard]] std::vector<PreparedInteriorOverlay> prepare_interior_overlays(
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
    entry.gradient_bounds = paint_bounds_for_render(layer, source, bounds, fill.gradient.align_with_layer);
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
[[nodiscard]] std::array<std::uint8_t, 3> fold_interior_overlays(
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

[[nodiscard]] DeepChannels fold_interior_overlays(DeepChannels styled,
                                                  const std::vector<PreparedInteriorOverlay>& overlays,
                                                  std::int32_t x, std::int32_t y, DeepDomain domain) {
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
        color = gradient_color(*overlay.gradient, position);
        field = DissolveField::GradientOverlay;
        break;
      }
      case PreparedInteriorOverlay::Kind::Color:
        break;
    }
    if (coverage <= 0.0F) {
      continue;
    }
    styled = fold_effect_color(styled, color, coverage, overlay.blend_mode, x, y, field, domain);
  }
  return styled;
}

// Folds the prepared satins into one styled color, after the overlays
// (Photoshop's interior stack puts Satin above them). Deliberately not routed
// through fold_effect_color: this fold keeps the plain model for the
// burn/dodge modes, and changing that would move pinned satin bytes. Dissolve
// is handled here for the same reason.
[[nodiscard]] std::array<std::uint8_t, 3> fold_prepared_satins(std::array<std::uint8_t, 3> color,
                                                               const std::vector<PreparedSatin>& satins,
                                                               std::int32_t x, std::int32_t y) {
  for (const auto& prepared : satins) {
    const auto mask_index = static_cast<std::size_t>(y - prepared.mask_bounds.y) *
                                static_cast<std::size_t>(prepared.mask_bounds.width) +
                            static_cast<std::size_t>(x - prepared.mask_bounds.x);
    const auto coverage = prepared.entry->primary[mask_index] * clamp_unit(prepared.effect->opacity);
    if (coverage <= 0.0F) {
      continue;
    }
    const auto& effect_color = prepared.effect->color;
    if (prepared.effect->blend_mode == BlendMode::Dissolve) {
      if (dissolve_coverage(x, y, coverage, DissolveField::Satin) > 0.0F) {
        color = {effect_color.red, effect_color.green, effect_color.blue};
      }
      continue;
    }
    color = composite_blended_rgb({effect_color.red, effect_color.green, effect_color.blue}, color,
                                  prepared.effect->blend_mode, coverage, 1.0F);
  }
  return color;
}

[[nodiscard]] DeepChannels fold_prepared_satins(DeepChannels color, const std::vector<PreparedSatin>& satins,
                                                std::int32_t x, std::int32_t y, DeepDomain domain) {
  for (const auto& prepared : satins) {
    const auto mask_index = static_cast<std::size_t>(y - prepared.mask_bounds.y) *
                                static_cast<std::size_t>(prepared.mask_bounds.width) +
                            static_cast<std::size_t>(x - prepared.mask_bounds.x);
    const auto coverage = prepared.entry->primary[mask_index] * clamp_unit(prepared.effect->opacity);
    if (coverage <= 0.0F) {
      continue;
    }
    const auto effect_color = deep_channels(deep_from_byte(prepared.effect->color, domain));
    if (prepared.effect->blend_mode == BlendMode::Dissolve) {
      if (dissolve_coverage(x, y, coverage, DissolveField::Satin) > 0.0F) {
        color = effect_color;
      }
      continue;
    }
    color = composite_blended_rgb_deep(effect_color, color, prepared.effect->blend_mode, coverage, 1.0F, domain);
  }
  return color;
}

std::optional<PreparedStroke> prepare_stroke_render(const Layer& layer, const PixelBuffer& source, Rect clip,
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
  // The contour leaves out any mask whose "Hides Effects" option is on
  // (effect_source_mask_alpha_for_render); those hide the output instead.
  const auto mask_shapes_source = true;
  const auto shape_burst =
      stroke.uses_gradient && stroke.gradient.type == LayerStyleGradientType::ShapeBurst;
  auto [entry, mask_bounds] = style_mask_for_render(
      masks, layer, StyleMaskKind::Stroke, effect_index, full_mask_bounds, full_mask_bounds, legacy_mask_bounds,
      bounds, layer_mask_bounds, [&](Rect domain) {
        StyleMaskEntry computed;
        StrokeMaskPlanes planes;
        planes.underlay = &computed.underlay;
        planes.inside = &computed.inside;
        planes.knockout = &computed.knockout;
        planes.overprint = stroke.overprint;
        computed.primary = stroke_alpha_mask(source, layer, bounds, domain, stroke.size, stroke.position,
                                             layer_mask_bounds, mask_shapes_source,
                                             shape_burst ? &computed.secondary : nullptr, &planes);
        computed.partial_content = planes.partial_content;
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
                                 ? paint_bounds_for_render(layer, source, bounds, true)
                                 : paint_bounds_for_render(layer, source, effect_bounds, false,
                                     stroke.position == LayerStrokePosition::Inside ? 0 : radius + 1);
  return prepared;
}

// The band coverage the draw loop actually paints with at (x, y): the cached
// mask value times the "Layer Mask Hides Effects" output clip.
float prepared_stroke_coverage(const PreparedStroke& prepared, const Layer& layer, std::int32_t x,
                               std::int32_t y, std::optional<Rect> layer_mask_bounds) {
  const auto& bounds = prepared.mask_bounds;
  if (x < bounds.x || y < bounds.y || x >= bounds.x + bounds.width || y >= bounds.y + bounds.height) {
    return 0.0F;
  }
  const auto index = static_cast<std::size_t>((y - bounds.y) * bounds.width + (x - bounds.x));
  auto coverage = prepared.entry->primary[index];
  if (prepared.draw_includes_underlay && !prepared.entry->underlay.empty()) {
    coverage = clamp_unit(coverage + prepared.entry->underlay[index] + prepared.entry->inside[index]);
  }
  if (prepared.clip_to_mask && coverage > 0.0F) {
    coverage *= effect_output_mask_alpha_for_render(layer, x, y, layer_mask_bounds);
  }
  return coverage;
}

// The folded part of a stroke at (x, y): the share beneath the layer's own pixels
// plus the share replacing them (zero outside the planes or for opaque content).
float prepared_stroke_underlay(const PreparedStroke& prepared, const Layer& layer, std::int32_t x,
                               std::int32_t y, std::optional<Rect> layer_mask_bounds) {
  if (prepared.entry->underlay.empty()) {
    return 0.0F;
  }
  const auto& bounds = prepared.mask_bounds;
  if (x < bounds.x || y < bounds.y || x >= bounds.x + bounds.width || y >= bounds.y + bounds.height) {
    return 0.0F;
  }
  const auto index = static_cast<std::size_t>((y - bounds.y) * bounds.width + (x - bounds.x));
  auto coverage = clamp_unit(prepared.entry->underlay[index] + prepared.entry->inside[index]);
  if (prepared.clip_to_mask && coverage > 0.0F) {
    coverage *= effect_output_mask_alpha_for_render(layer, x, y, layer_mask_bounds);
  }
  return coverage;
}

// The band membership that knocks the content out at (x, y): the over-draw coverage,
// except where the content is semi-transparent and the band replaces it outright.
float prepared_stroke_knockout_coverage(const PreparedStroke& prepared, const Layer& layer, std::int32_t x,
                                        std::int32_t y, std::optional<Rect> layer_mask_bounds,
                                        float coverage) {
  if (prepared.entry->knockout.empty()) {
    return coverage;
  }
  const auto& bounds = prepared.mask_bounds;
  if (x < bounds.x || y < bounds.y || x >= bounds.x + bounds.width || y >= bounds.y + bounds.height) {
    return 0.0F;
  }
  auto region = prepared.entry->knockout[static_cast<std::size_t>((y - bounds.y) * bounds.width + (x - bounds.x))];
  if (prepared.clip_to_mask && region > 0.0F) {
    region *= effect_output_mask_alpha_for_render(layer, x, y, layer_mask_bounds);
  }
  return region;
}

// Mirrors render_prepared_stroke's per-pixel alpha (same expressions in the
// same order) so the Normal-mode knockout divisor cancels the later stroke
// draw exactly.
float prepared_stroke_draw_alpha(const PreparedStroke& prepared, const Layer& layer, std::int32_t x,
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
float stroke_knockout_factor(const PreparedStroke& prepared, const Layer& layer, std::int32_t x,
                             std::int32_t y, std::optional<Rect> layer_mask_bounds) {
  const auto coverage = prepared_stroke_coverage(prepared, layer, x, y, layer_mask_bounds);
  const auto region = prepared_stroke_knockout_coverage(prepared, layer, x, y, layer_mask_bounds, coverage);
  if (region <= 0.0F) {
    return 1.0F;
  }
  if (prepared.stroke->blend_mode != BlendMode::Normal) {
    return clamp_unit(1.0F - region);
  }
  const auto draw_alpha = coverage <= 0.0F ? 0.0F : prepared_stroke_draw_alpha(prepared, layer, x, y, coverage);
  const auto remaining = 1.0F - draw_alpha;
  if (remaining <= 1e-6F) {
    // Only reachable as coverage -> 1 (draw_alpha <= coverage): full knockout.
    return 0.0F;
  }
  return clamp_unit((1.0F - region) / remaining);
}

// The factor a stroke stacked ABOVE a lower stroke's draw applies to it (GitHub
// issue 265): the upper band's own over-draw coverage knocks the lower stroke
// out exactly as it knocks out content, with the same Normal-mode compensation
// for the upper draw that follows. The content plane cannot serve here: on a
// layer with anti-aliased pixels it holds the inner band membership, which is
// zero across an Outside band.
float stroke_stack_knockout_factor(const PreparedStroke& upper, const Layer& layer, std::int32_t x,
                                   std::int32_t y, std::optional<Rect> layer_mask_bounds) {
  const auto coverage = prepared_stroke_coverage(upper, layer, x, y, layer_mask_bounds);
  if (coverage <= 0.0F) {
    return 1.0F;
  }
  if (upper.stroke->blend_mode != BlendMode::Normal) {
    return clamp_unit(1.0F - coverage);
  }
  const auto draw_alpha = prepared_stroke_draw_alpha(upper, layer, x, y, coverage);
  const auto remaining = 1.0F - draw_alpha;
  if (remaining <= 1e-6F) {
    return 0.0F;
  }
  return clamp_unit((1.0F - coverage) / remaining);
}

// Overprint-off knockout is invisible for an opaque Normal stroke at full
// layer opacity: the stroke covers exactly what it would have knocked out.
// Skipping it keeps the base pass's fast row path and the pinned opaque
// fixtures on their historical byte-exact path.
bool stroke_knockout_is_identity(const LayerStroke& stroke, const Layer& layer) {
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

}  // namespace patchy::render_detail
