#pragma once

#include <cstdint>

namespace patchy {

// Which per-stroke input drives a dynamic parameter (Photoshop's "Control" dropdown). Every
// dynamic carries one; Direction/InitialDirection are meaningful for the angle only and degrade
// to Off elsewhere. Only append new values at the end: UI combo indices and persisted casts
// depend on the existing order.
enum class BrushDynamicControl : std::uint8_t {
  Off = 0,
  Fade,
  PenPressure,
  PenTilt,
  PenRotation,
  InitialDirection,
  Direction,
  StylusWheel,    // airbrush wheel (tablet tangential pressure)
  GlobalDefault,  // defer to the global input/pen/* preferences (size/roundness/opacity only)
};

// Static, code-generated grain families used by Brush Texture. The texture never changes in
// response to pen pressure, direction, velocity, or pose: that fixed boundary keeps this
// implementation outside the active dynamic-stamp-texture patent family. Append only because
// the numeric values are persisted in brush sidecars.
enum class BrushTextureStyle : std::uint8_t {
  FineGrain = 0,
  Canvas,
  Speckle,
};

// Photoshop-style per-dab brush tip dynamics (Shape Dynamics + Scattering + Transfer).
// Default-constructed = disabled: the stamp engine takes its historical path bit-for-bit and
// consumes no randomness. Fractions are 0..1 of the Photoshop percent unless noted.
struct BrushDynamics {
  double size_jitter{0.0};
  double minimum_diameter{0.0};  // floor for the per-dab size scale; size jitter only shrinks
  BrushDynamicControl size_control{BrushDynamicControl::GlobalDefault};
  int size_fade_steps{25};
  double angle_jitter{0.0};      // 1.0 = anywhere in the full turn
  BrushDynamicControl angle_control{BrushDynamicControl::Off};
  int angle_fade_steps{25};      // >= 1; spacing steps to fade over when angle_control == Fade
  double roundness_jitter{0.0};
  double minimum_roundness{0.25};
  BrushDynamicControl roundness_control{BrushDynamicControl::GlobalDefault};
  int roundness_fade_steps{25};
  bool flip_x_jitter{false};
  bool flip_y_jitter{false};
  double scatter{0.0};           // offset range as a fraction of brush size; 0..10 = PS 0..1000%
  bool scatter_both_axes{false};  // false = scatter perpendicular to the stroke only
  BrushDynamicControl scatter_control{BrushDynamicControl::Off};
  int scatter_fade_steps{25};
  int count{1};                  // dabs per spacing step, 1..16
  double count_jitter{0.0};
  BrushDynamicControl count_control{BrushDynamicControl::Off};
  int count_fade_steps{25};
  double opacity_jitter{0.0};
  double minimum_opacity{0.0};  // floor when opacity_control has a source (PS Transfer "Minimum")
  BrushDynamicControl opacity_control{BrushDynamicControl::GlobalDefault};
  int opacity_fade_steps{25};
  double flow_jitter{0.0};
  double minimum_flow{0.0};  // floor when flow_control has a source (PS Transfer "Minimum Flow")
  BrushDynamicControl flow_control{BrushDynamicControl::Off};
  int flow_fade_steps{25};

  // Photoshop-compatible Brush Texture subset. Grain is a static, world-anchored procedural
  // mask; no stroke input can alter its channels, weights, or pattern. texture_scale is the
  // Photoshop percentage divided by 100 (0.01..10). texture_seed preserves the identity of an
  // imported pattern without bundling Adobe artwork.
  bool texture_enabled{false};
  BrushTextureStyle texture_style{BrushTextureStyle::FineGrain};
  double texture_scale{1.0};
  double texture_depth{0.5};
  bool texture_invert{false};
  std::uint32_t texture_seed{0x5A17C9E3U};

  // One fixed secondary computed mask. It is deliberately not a component graph: a single
  // repeated round mask combines with the primary coverage using multiplication.
  bool dual_brush_enabled{false};
  double dual_brush_size{0.5};      // secondary diameter / primary diameter, 0.05..4
  double dual_brush_hardness{1.0};  // 0..1
  double dual_brush_spacing{1.0};   // center spacing / secondary diameter, 0.1..10

  // Photoshop Color Dynamics. This varies the selected foreground/background colors only; it
  // never samples the canvas and never performs pigment or wet-paint mixing.
  bool color_dynamics_enabled{false};
  double foreground_background_jitter{0.0};
  BrushDynamicControl color_control{BrushDynamicControl::Off};
  int color_fade_steps{25};
  double hue_jitter{0.0};
  double saturation_jitter{0.0};
  double brightness_jitter{0.0};
  double purity{0.0};  // -1..1 saturation bias
  bool color_per_tip{true};

  // Fixed coverage-edge treatment, not a fluid, pigment, drying, or canvas-pickup simulation.
  bool wet_edges{false};

  // Per-brush control precedence: GlobalDefault (the size/roundness/opacity default) leaves the
  // global pen preferences authoritative (they modulate pre-dab in effective_brush_input); any
  // other value makes this brush own the aspect and suppresses that global modulation. Off
  // therefore means "this brush ignores the pen for this aspect" and needs no per-dab work.

  // Per-stroke inputs the caller fills in before painting; never persisted or compared.
  std::uint32_t seed{0};
  double pen_pressure{1.0};  // 0..1; 1.0 when unavailable so a mouse paints at full value
  double pen_tilt{1.0};      // tilt magnitude 0..1 (hypot(x,y)/90 clamped); 1.0 when unavailable
  double pen_wheel{1.0};     // airbrush/tangential wheel mapped to 0..1; 1.0 when unavailable
  double pen_tilt_azimuth_degrees{0.0};  // tilt direction (PenTilt angle control)
  bool pen_tilt_azimuth_valid{false};    // set only when tilt is available and non-zero
  double pen_rotation_degrees{0.0};      // barrel rotation (PenRotation)
  bool pen_rotation_valid{false};

  // True when any persisted setting can change a stamp; the engine gates the dynamic path on
  // this so plain strokes stay on the exact historical code path.
  [[nodiscard]] bool active() const noexcept;
};

// Deterministic splitmix64 generator with explicit uniform mapping. std::uniform_*_distribution
// output is implementation-defined, which would break cross-toolchain pixel-exact tests, so all
// draws go through these helpers.
struct BrushDynamicsRng {
  std::uint64_t state{0};

  void seed(std::uint32_t value) noexcept;
  [[nodiscard]] std::uint64_t next_u64() noexcept;
  [[nodiscard]] double next_unit() noexcept;         // [0, 1)
  [[nodiscard]] double next_signed_unit() noexcept;  // [-1, 1)
  [[nodiscard]] bool next_bool() noexcept;
};

// Fade / direction bookkeeping carried across the chained segments of one stroke.
struct BrushDynamicsStrokeContext {
  std::uint32_t step_index{0};  // spacing steps stamped so far; drives the Fade control
  bool direction_valid{false};
  double direction_x{1.0};  // smoothed unit travel direction (document space, y down)
  double direction_y{0.0};
  bool initial_direction_valid{false};
  double initial_direction_x{1.0};
  double initial_direction_y{0.0};
  bool stroke_color_valid{false};
  double stroke_foreground_background_mix{0.0};
  double stroke_hue_shift{0.0};
  double stroke_saturation_shift{0.0};
  double stroke_brightness_shift{0.0};
};

// Feed each segment's delta into the smoothed stroke direction. The first non-degenerate call
// also latches the initial direction.
void advance_stroke_direction(BrushDynamicsStrokeContext& context, double dx, double dy);

// One dab's sampled deviation from the base stamp.
struct BrushDabVariation {
  double scale{1.0};                // (0, 1]; size jitter only shrinks the stamp
  double angle_offset_degrees{0.0};  // added to EditOptions::brush_angle_degrees
  double roundness_multiplier{1.0};
  bool flip_x{false};
  bool flip_y{false};
  double offset_x{0.0};  // scatter offset, document pixels
  double offset_y{0.0};
  double opacity_multiplier{1.0};
  double flow_multiplier{1.0};
  double foreground_background_mix{0.0};
  double hue_shift{0.0};         // signed fraction of a full hue turn
  double saturation_shift{0.0};  // signed fraction
  double brightness_shift{0.0};  // signed fraction
};

// RNG draw-order contract (tests depend on it; keep brush_dynamics.cpp in sync):
// per spacing step, one draw for count jitter iff count > 1 && count_jitter > 0, then per dab,
// draws happen only for enabled features, in this fixed order:
//   1. scatter perpendicular   (signed) iff scatter > 0
//   2. scatter parallel        (signed) iff scatter > 0 && scatter_both_axes
//   3. size jitter             (unit)   iff size_jitter > 0
//   4. angle jitter            (signed) iff angle_jitter > 0
//   5. roundness jitter        (unit)   iff roundness_jitter > 0
//   6. flip X                  (bool)   iff flip_x_jitter
//   7. flip Y                  (bool)   iff flip_y_jitter
//   8. opacity jitter          (unit)   iff opacity_jitter > 0
//   9. flow jitter             (unit)   iff flow_jitter > 0
//  10. foreground/background  (unit)   iff Color Dynamics + foreground/background jitter > 0
//  11. hue jitter              (signed) iff Color Dynamics + hue jitter > 0
//  12. saturation jitter       (signed) iff Color Dynamics + saturation jitter > 0
//  13. brightness jitter       (signed) iff Color Dynamics + brightness jitter > 0
// When color_per_tip is false, draws 10-13 happen on the first dab only and the sampled values
// are reused for the rest of the stroke.
// The per-dynamic controls never draw: each control value is computed deterministically from the
// pen inputs / fade step and only scales the result, so the gates above stay keyed on static
// configuration and adding a control cannot shift any draw.
[[nodiscard]] int sample_dab_count(const BrushDynamics& dynamics, BrushDynamicsRng& rng,
                                   const BrushDynamicsStrokeContext& context) noexcept;
[[nodiscard]] BrushDabVariation sample_dab_variation(const BrushDynamics& dynamics, BrushDynamicsRng& rng,
                                                     BrushDynamicsStrokeContext& context,
                                                     int brush_size) noexcept;

}  // namespace patchy
