#include "core/brush_dynamics.hpp"

#include "core/splitmix64.hpp"

#include <algorithm>
#include <cmath>

namespace patchy {

namespace {

constexpr double kPi = 3.14159265358979323846;

// Blend factor toward each new segment's direction. The canvas stroke smoother emits short
// segments (fractions of the brush size), so this settles within about half a brush width
// without letting single noisy segments whip the tip around.
constexpr double kDirectionSmoothing = 0.35;

// Fade control value for the current spacing step: 1 at the stroke start, 0 after fade_steps.
[[nodiscard]] double fade_value(int fade_steps, std::uint32_t step_index) noexcept {
  const auto steps = std::max(1, fade_steps);
  return std::max(0.0, 1.0 - static_cast<double>(step_index) / static_cast<double>(steps));
}

[[nodiscard]] double degrees_from_vector(double x, double y) noexcept {
  return std::atan2(y, x) * 180.0 / kPi;
}

// True for control sources that need the per-dab dynamics path. Off and GlobalDefault do not:
// they only decide whether the global pen preferences apply pre-dab.
[[nodiscard]] bool control_has_source(BrushDynamicControl control) noexcept {
  switch (control) {
    case BrushDynamicControl::Fade:
    case BrushDynamicControl::PenPressure:
    case BrushDynamicControl::PenTilt:
    case BrushDynamicControl::PenRotation:
    case BrushDynamicControl::StylusWheel:
      return true;
    default:
      return false;
  }
}

// Deterministic 0..1 control value for a non-angle dynamic; consumes no randomness. Missing pen
// input reads as 1.0 (a mouse paints at full value, matching Photoshop).
[[nodiscard]] double control_value(BrushDynamicControl control, int fade_steps,
                                   const BrushDynamics& dynamics,
                                   const BrushDynamicsStrokeContext& context) noexcept {
  switch (control) {
    case BrushDynamicControl::Fade:
      return fade_value(fade_steps, context.step_index);
    case BrushDynamicControl::PenPressure:
      return std::clamp(dynamics.pen_pressure, 0.0, 1.0);
    case BrushDynamicControl::PenTilt:
      return std::clamp(dynamics.pen_tilt, 0.0, 1.0);
    case BrushDynamicControl::StylusWheel:
      return std::clamp(dynamics.pen_wheel, 0.0, 1.0);
    case BrushDynamicControl::PenRotation: {
      if (!dynamics.pen_rotation_valid) {
        return 1.0;
      }
      const auto wrapped = std::fmod(std::fmod(dynamics.pen_rotation_degrees, 360.0) + 360.0, 360.0);
      return wrapped / 360.0;
    }
    default:
      return 1.0;  // Off, GlobalDefault, and the angle-only Direction controls
  }
}

}  // namespace

BrushDynamics effective_brush_dynamics(const BrushDynamics& dynamics) noexcept {
  auto effective = dynamics;
  if (!dynamics.shape_dynamics_enabled) {
    reset_brush_dynamics_section(effective, BrushDynamicsSection::ShapeDynamics);
  }
  if (!dynamics.scattering_enabled) {
    reset_brush_dynamics_section(effective, BrushDynamicsSection::Scattering);
  }
  if (!dynamics.transfer_enabled) {
    reset_brush_dynamics_section(effective, BrushDynamicsSection::Transfer);
  }
  effective.shape_dynamics_enabled = true;
  effective.scattering_enabled = true;
  effective.transfer_enabled = true;
  return effective;
}

bool brush_dynamics_sections_all_enabled(const BrushDynamics& dynamics) noexcept {
  return dynamics.shape_dynamics_enabled && dynamics.scattering_enabled &&
         dynamics.transfer_enabled;
}

void reset_brush_dynamics_section(BrushDynamics& dynamics, BrushDynamicsSection section) noexcept {
  const BrushDynamics defaults;
  switch (section) {
    case BrushDynamicsSection::ShapeDynamics:
      dynamics.size_jitter = defaults.size_jitter;
      dynamics.minimum_diameter = defaults.minimum_diameter;
      dynamics.size_control = defaults.size_control;
      dynamics.size_fade_steps = defaults.size_fade_steps;
      dynamics.angle_jitter = defaults.angle_jitter;
      dynamics.angle_control = defaults.angle_control;
      dynamics.angle_fade_steps = defaults.angle_fade_steps;
      dynamics.roundness_jitter = defaults.roundness_jitter;
      dynamics.minimum_roundness = defaults.minimum_roundness;
      dynamics.roundness_control = defaults.roundness_control;
      dynamics.roundness_fade_steps = defaults.roundness_fade_steps;
      dynamics.flip_x_jitter = defaults.flip_x_jitter;
      dynamics.flip_y_jitter = defaults.flip_y_jitter;
      break;
    case BrushDynamicsSection::Scattering:
      dynamics.scatter = defaults.scatter;
      dynamics.scatter_both_axes = defaults.scatter_both_axes;
      dynamics.scatter_control = defaults.scatter_control;
      dynamics.scatter_fade_steps = defaults.scatter_fade_steps;
      dynamics.count = defaults.count;
      dynamics.count_jitter = defaults.count_jitter;
      dynamics.count_control = defaults.count_control;
      dynamics.count_fade_steps = defaults.count_fade_steps;
      break;
    case BrushDynamicsSection::Transfer:
      dynamics.opacity_jitter = defaults.opacity_jitter;
      dynamics.minimum_opacity = defaults.minimum_opacity;
      dynamics.opacity_control = defaults.opacity_control;
      dynamics.opacity_fade_steps = defaults.opacity_fade_steps;
      dynamics.flow_jitter = defaults.flow_jitter;
      dynamics.minimum_flow = defaults.minimum_flow;
      dynamics.flow_control = defaults.flow_control;
      dynamics.flow_fade_steps = defaults.flow_fade_steps;
      break;
    case BrushDynamicsSection::Texture:
      // texture_seed is an imported pattern's identity, not a value the user edits.
      dynamics.texture_style = defaults.texture_style;
      dynamics.texture_scale = defaults.texture_scale;
      dynamics.texture_depth = defaults.texture_depth;
      dynamics.texture_invert = defaults.texture_invert;
      break;
    case BrushDynamicsSection::DualBrush:
      dynamics.dual_brush_size = defaults.dual_brush_size;
      dynamics.dual_brush_hardness = defaults.dual_brush_hardness;
      dynamics.dual_brush_spacing = defaults.dual_brush_spacing;
      break;
    case BrushDynamicsSection::ColorDynamics:
      dynamics.foreground_background_jitter = defaults.foreground_background_jitter;
      dynamics.color_control = defaults.color_control;
      dynamics.color_fade_steps = defaults.color_fade_steps;
      dynamics.hue_jitter = defaults.hue_jitter;
      dynamics.saturation_jitter = defaults.saturation_jitter;
      dynamics.brightness_jitter = defaults.brightness_jitter;
      dynamics.purity = defaults.purity;
      dynamics.color_per_tip = defaults.color_per_tip;
      break;
    case BrushDynamicsSection::BrushEffects:
      dynamics.wet_edges = defaults.wet_edges;
      break;
  }
}

bool brush_dynamics_section_is_default(const BrushDynamics& dynamics,
                                       BrushDynamicsSection section) noexcept {
  const BrushDynamics d;
  const auto& v = dynamics;
  switch (section) {
    case BrushDynamicsSection::ShapeDynamics:
      return v.size_jitter == d.size_jitter && v.minimum_diameter == d.minimum_diameter &&
             v.size_control == d.size_control && v.size_fade_steps == d.size_fade_steps &&
             v.angle_jitter == d.angle_jitter && v.angle_control == d.angle_control &&
             v.angle_fade_steps == d.angle_fade_steps && v.roundness_jitter == d.roundness_jitter &&
             v.minimum_roundness == d.minimum_roundness &&
             v.roundness_control == d.roundness_control &&
             v.roundness_fade_steps == d.roundness_fade_steps &&
             v.flip_x_jitter == d.flip_x_jitter && v.flip_y_jitter == d.flip_y_jitter;
    case BrushDynamicsSection::Scattering:
      return v.scatter == d.scatter && v.scatter_both_axes == d.scatter_both_axes &&
             v.scatter_control == d.scatter_control &&
             v.scatter_fade_steps == d.scatter_fade_steps && v.count == d.count &&
             v.count_jitter == d.count_jitter && v.count_control == d.count_control &&
             v.count_fade_steps == d.count_fade_steps;
    case BrushDynamicsSection::Transfer:
      return v.opacity_jitter == d.opacity_jitter && v.minimum_opacity == d.minimum_opacity &&
             v.opacity_control == d.opacity_control &&
             v.opacity_fade_steps == d.opacity_fade_steps && v.flow_jitter == d.flow_jitter &&
             v.minimum_flow == d.minimum_flow && v.flow_control == d.flow_control &&
             v.flow_fade_steps == d.flow_fade_steps;
    case BrushDynamicsSection::Texture:
      return v.texture_style == d.texture_style && v.texture_scale == d.texture_scale &&
             v.texture_depth == d.texture_depth && v.texture_invert == d.texture_invert;
    case BrushDynamicsSection::DualBrush:
      return v.dual_brush_size == d.dual_brush_size &&
             v.dual_brush_hardness == d.dual_brush_hardness &&
             v.dual_brush_spacing == d.dual_brush_spacing;
    case BrushDynamicsSection::ColorDynamics:
      return v.foreground_background_jitter == d.foreground_background_jitter &&
             v.color_control == d.color_control && v.color_fade_steps == d.color_fade_steps &&
             v.hue_jitter == d.hue_jitter && v.saturation_jitter == d.saturation_jitter &&
             v.brightness_jitter == d.brightness_jitter && v.purity == d.purity &&
             v.color_per_tip == d.color_per_tip;
    case BrushDynamicsSection::BrushEffects:
      return v.wet_edges == d.wet_edges;
  }
  return true;
}

bool BrushDynamics::active() const noexcept {
  if (!brush_dynamics_sections_all_enabled(*this)) {
    return effective_brush_dynamics(*this).active();
  }
  return size_jitter > 0.0 || angle_jitter > 0.0 ||
         (angle_control != BrushDynamicControl::Off &&
          angle_control != BrushDynamicControl::GlobalDefault) ||
         roundness_jitter > 0.0 || flip_x_jitter || flip_y_jitter || scatter > 0.0 || count > 1 ||
         opacity_jitter > 0.0 || control_has_source(size_control) ||
         flow_jitter > 0.0 || control_has_source(roundness_control) ||
         control_has_source(opacity_control) || control_has_source(flow_control) ||
         (texture_enabled && texture_depth > 0.0) || dual_brush_enabled || wet_edges ||
         (color_dynamics_enabled &&
          (foreground_background_jitter > 0.0 || control_has_source(color_control) ||
           hue_jitter > 0.0 || saturation_jitter > 0.0 || brightness_jitter > 0.0 ||
           purity != 0.0));

  // scatter_control/count_control need no term: they only modulate scatter > 0 / count > 1,
  // which already activate the dynamic path.
}

void BrushDynamicsRng::seed(std::uint32_t value) noexcept {
  state = 0x9E3779B97F4A7C15ULL ^ (static_cast<std::uint64_t>(value) * 0xBF58476D1CE4E5B9ULL);
}

std::uint64_t BrushDynamicsRng::next_u64() noexcept {
  // splitmix64 advances the state by the golden-ratio increment and mixes the
  // new state; the stream is pinned by the brush dynamics pixel tests.
  const auto seed = state;
  state += 0x9E3779B97F4A7C15ULL;
  return splitmix64(seed);
}

double BrushDynamicsRng::next_unit() noexcept {
  return static_cast<double>(next_u64() >> 11) * 0x1.0p-53;
}

double BrushDynamicsRng::next_signed_unit() noexcept { return next_unit() * 2.0 - 1.0; }

bool BrushDynamicsRng::next_bool() noexcept { return (next_u64() >> 63) != 0; }

void advance_stroke_direction(BrushDynamicsStrokeContext& context, double dx, double dy) {
  const auto length = std::sqrt(dx * dx + dy * dy);
  if (length <= 0.0) {
    return;
  }
  const auto nx = dx / length;
  const auto ny = dy / length;
  if (!context.direction_valid) {
    context.direction_valid = true;
    context.direction_x = nx;
    context.direction_y = ny;
    context.initial_direction_valid = true;
    context.initial_direction_x = nx;
    context.initial_direction_y = ny;
    return;
  }
  auto blended_x = context.direction_x + (nx - context.direction_x) * kDirectionSmoothing;
  auto blended_y = context.direction_y + (ny - context.direction_y) * kDirectionSmoothing;
  const auto blended_length = std::sqrt(blended_x * blended_x + blended_y * blended_y);
  if (blended_length <= 1e-9) {
    // A hard reversal cancels the blend; snap to the new direction.
    blended_x = nx;
    blended_y = ny;
  } else {
    blended_x /= blended_length;
    blended_y /= blended_length;
  }
  context.direction_x = blended_x;
  context.direction_y = blended_y;
}

bool angle_follows_direction(const BrushDynamics& dynamics) noexcept {
  return dynamics.shape_dynamics_enabled &&
         (dynamics.angle_control == BrushDynamicControl::Direction ||
          dynamics.angle_control == BrushDynamicControl::InitialDirection);
}

int sample_dab_count(const BrushDynamics& dynamics, BrushDynamicsRng& rng,
                     const BrushDynamicsStrokeContext& context) noexcept {
  if (!dynamics.scattering_enabled) {
    return 1;  // Count is a Scattering setting: an off section stamps one dab per step
  }
  const auto count = std::clamp(dynamics.count, 1, 16);
  auto base = static_cast<double>(count);
  if (count > 1 && control_has_source(dynamics.count_control)) {
    // Photoshop: the control fades the count from the configured value down to 1.
    base = 1.0 + control_value(dynamics.count_control, dynamics.count_fade_steps, dynamics,
                               context) *
                     static_cast<double>(count - 1);
  }
  if (count <= 1 || dynamics.count_jitter <= 0.0) {
    return static_cast<int>(std::clamp<long>(std::lround(base), 1, count));
  }
  const auto jitter = std::clamp(dynamics.count_jitter, 0.0, 1.0);
  const auto sampled = std::lround(base * (1.0 - jitter * rng.next_unit()));
  return static_cast<int>(std::clamp<long>(sampled, 1, count));
}

BrushDabVariation sample_dab_variation(const BrushDynamics& dynamics, BrushDynamicsRng& rng,
                                       BrushDynamicsStrokeContext& context,
                                       int brush_size) noexcept {
  if (!brush_dynamics_sections_all_enabled(dynamics)) {
    const auto effective = effective_brush_dynamics(dynamics);
    return sample_dab_variation(effective, rng, context, brush_size);
  }
  BrushDabVariation variation;

  if (dynamics.scatter > 0.0) {
    auto range =
        std::clamp(dynamics.scatter, 0.0, 10.0) * static_cast<double>(std::max(1, brush_size));
    if (control_has_source(dynamics.scatter_control)) {
      // Photoshop: the control fades scattering from the full range down to none.
      range *= control_value(dynamics.scatter_control, dynamics.scatter_fade_steps, dynamics,
                             context);
    }
    const auto perpendicular = rng.next_signed_unit() * range;
    variation.offset_x = -context.direction_y * perpendicular;
    variation.offset_y = context.direction_x * perpendicular;
    if (dynamics.scatter_both_axes) {
      const auto parallel = rng.next_signed_unit() * range;
      variation.offset_x += context.direction_x * parallel;
      variation.offset_y += context.direction_y * parallel;
    }
  }

  {
    const auto floor_scale = std::clamp(dynamics.minimum_diameter, 0.01, 1.0);
    auto size_base = 1.0;
    if (control_has_source(dynamics.size_control)) {
      // The control maps into [minimum diameter, 1]; jitter then shrinks from that base, so the
      // shrink-only invariant (and the scaled-tip cache it protects) holds.
      size_base = floor_scale + control_value(dynamics.size_control, dynamics.size_fade_steps,
                                              dynamics, context) *
                                    (1.0 - floor_scale);
      variation.scale = size_base;
    }
    if (dynamics.size_jitter > 0.0) {
      const auto jitter = std::clamp(dynamics.size_jitter, 0.0, 1.0);
      variation.scale = std::clamp(size_base * (1.0 - jitter * rng.next_unit()), floor_scale, 1.0);
    }
  }

  auto angle = 0.0;
  switch (dynamics.angle_control) {
    case BrushDynamicControl::Off:
    case BrushDynamicControl::GlobalDefault:
      break;
    case BrushDynamicControl::Fade:
      angle = fade_value(dynamics.angle_fade_steps, context.step_index) * 360.0;
      break;
    case BrushDynamicControl::PenPressure:
      angle = std::clamp(dynamics.pen_pressure, 0.0, 1.0) * 360.0;
      break;
    case BrushDynamicControl::StylusWheel:
      angle = std::clamp(dynamics.pen_wheel, 0.0, 1.0) * 360.0;
      break;
    case BrushDynamicControl::PenTilt:
      angle = dynamics.pen_tilt_azimuth_valid ? dynamics.pen_tilt_azimuth_degrees : 0.0;
      break;
    case BrushDynamicControl::PenRotation:
      // Prefer true barrel rotation; fall back to the tilt azimuth like the pre-control canvas
      // path did, so styli without rotation still steer the tip.
      angle = dynamics.pen_rotation_valid
                  ? dynamics.pen_rotation_degrees
                  : (dynamics.pen_tilt_azimuth_valid ? dynamics.pen_tilt_azimuth_degrees : 0.0);
      break;
    case BrushDynamicControl::InitialDirection:
      angle = context.initial_direction_valid
                  ? degrees_from_vector(context.initial_direction_x, context.initial_direction_y)
                  : 0.0;
      break;
    case BrushDynamicControl::Direction:
      angle = context.direction_valid
                  ? degrees_from_vector(context.direction_x, context.direction_y)
                  : 0.0;
      break;
  }
  if (dynamics.angle_jitter > 0.0) {
    angle += rng.next_signed_unit() * std::clamp(dynamics.angle_jitter, 0.0, 1.0) * 180.0;
  }
  variation.angle_offset_degrees = angle;

  {
    const auto floor_roundness = std::clamp(dynamics.minimum_roundness, 0.01, 1.0);
    auto roundness_base = 1.0;
    if (control_has_source(dynamics.roundness_control)) {
      roundness_base = floor_roundness + control_value(dynamics.roundness_control,
                                                       dynamics.roundness_fade_steps, dynamics,
                                                       context) *
                                             (1.0 - floor_roundness);
      variation.roundness_multiplier = roundness_base;
    }
    if (dynamics.roundness_jitter > 0.0) {
      const auto jitter = std::clamp(dynamics.roundness_jitter, 0.0, 1.0);
      variation.roundness_multiplier =
          std::clamp(roundness_base * (1.0 - jitter * rng.next_unit()), floor_roundness, 1.0);
    }
  }
  if (dynamics.flip_x_jitter) {
    variation.flip_x = rng.next_bool();
  }
  if (dynamics.flip_y_jitter) {
    variation.flip_y = rng.next_bool();
  }
  {
    auto opacity_base = 1.0;
    if (control_has_source(dynamics.opacity_control)) {
      const auto floor_opacity = std::clamp(dynamics.minimum_opacity, 0.0, 1.0);
      opacity_base = floor_opacity + control_value(dynamics.opacity_control,
                                                   dynamics.opacity_fade_steps, dynamics,
                                                   context) *
                                         (1.0 - floor_opacity);
      variation.opacity_multiplier = std::clamp(opacity_base, 0.03, 1.0);
    }
    if (dynamics.opacity_jitter > 0.0) {
      const auto jitter = std::clamp(dynamics.opacity_jitter, 0.0, 1.0);
      variation.opacity_multiplier =
          std::clamp(opacity_base * (1.0 - jitter * rng.next_unit()), 0.03, 1.0);
    }
  }
  {
    auto flow_base = 1.0;
    if (control_has_source(dynamics.flow_control)) {
      const auto floor_flow = std::clamp(dynamics.minimum_flow, 0.0, 1.0);
      flow_base = floor_flow + control_value(dynamics.flow_control, dynamics.flow_fade_steps,
                                             dynamics, context) *
                                   (1.0 - floor_flow);
      variation.flow_multiplier = std::clamp(flow_base, 0.0, 1.0);
    }
    if (dynamics.flow_jitter > 0.0) {
      const auto jitter = std::clamp(dynamics.flow_jitter, 0.0, 1.0);
      variation.flow_multiplier =
          std::clamp(flow_base * (1.0 - jitter * rng.next_unit()), 0.0, 1.0);
    }
  }

  if (dynamics.color_dynamics_enabled) {
    const auto sample_color = [&] {
      auto mix = 0.0;
      if (control_has_source(dynamics.color_control)) {
        // Full input selects the foreground; a fading/reduced input moves toward the
        // background. Jitter then adds a deterministic random excursion toward background.
        mix = 1.0 - control_value(dynamics.color_control, dynamics.color_fade_steps, dynamics,
                                  context);
      }
      if (dynamics.foreground_background_jitter > 0.0) {
        const auto jitter = std::clamp(dynamics.foreground_background_jitter, 0.0, 1.0);
        mix += (1.0 - mix) * jitter * rng.next_unit();
      }
      variation.foreground_background_mix = std::clamp(mix, 0.0, 1.0);
      if (dynamics.hue_jitter > 0.0) {
        variation.hue_shift =
            rng.next_signed_unit() * std::clamp(dynamics.hue_jitter, 0.0, 1.0) * 0.5;
      }
      if (dynamics.saturation_jitter > 0.0) {
        variation.saturation_shift =
            rng.next_signed_unit() * std::clamp(dynamics.saturation_jitter, 0.0, 1.0);
      }
      if (dynamics.brightness_jitter > 0.0) {
        variation.brightness_shift =
            rng.next_signed_unit() * std::clamp(dynamics.brightness_jitter, 0.0, 1.0);
      }
    };
    if (dynamics.color_per_tip || !context.stroke_color_valid) {
      sample_color();
      if (!dynamics.color_per_tip) {
        context.stroke_color_valid = true;
        context.stroke_foreground_background_mix = variation.foreground_background_mix;
        context.stroke_hue_shift = variation.hue_shift;
        context.stroke_saturation_shift = variation.saturation_shift;
        context.stroke_brightness_shift = variation.brightness_shift;
      }
    } else {
      variation.foreground_background_mix = context.stroke_foreground_background_mix;
      variation.hue_shift = context.stroke_hue_shift;
      variation.saturation_shift = context.stroke_saturation_shift;
      variation.brightness_shift = context.stroke_brightness_shift;
    }
  }
  return variation;
}

}  // namespace patchy
