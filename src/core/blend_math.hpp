#pragma once

#include "core/layer.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace patchy {

struct FillCompositeResult {
  std::array<std::uint8_t, 3> color{};
  float alpha{0.0F};
};

[[nodiscard]] std::uint8_t clamp_byte(float value);
[[nodiscard]] float clamp_unit(float value);
[[nodiscard]] float blend_if_threshold_factor(const BlendIfThresholds& thresholds,
                                               std::uint8_t value) noexcept;
[[nodiscard]] std::uint8_t blend_if_gray_value(RgbColor color) noexcept;
[[nodiscard]] float blend_if_source_factor(const LayerBlendIf& settings, RgbColor source) noexcept;
[[nodiscard]] float blend_if_underlying_factor(const LayerBlendIf& settings, RgbColor underlying) noexcept;
// Photoshop evaluates each active channel as an 8-bit coverage and multiplies
// those coverages with integer truncation. The compositor uses these byte
// variants; the float helpers above expose the underlying normalized math.
[[nodiscard]] std::uint8_t blend_if_source_alpha_byte(const LayerBlendIf& settings, RgbColor source) noexcept;
[[nodiscard]] std::uint8_t blend_if_underlying_alpha_byte(const LayerBlendIf& settings,
                                                          RgbColor underlying) noexcept;
[[nodiscard]] std::array<std::uint8_t, 3> blend_rgb(std::array<std::uint8_t, 3> source,
                                                    std::array<std::uint8_t, 3> destination, BlendMode mode);
[[nodiscard]] std::array<std::uint8_t, 3> composite_blended_rgb(std::array<std::uint8_t, 3> source,
                                                                std::array<std::uint8_t, 3> destination,
                                                                BlendMode mode, float source_alpha,
                                                                float destination_alpha);
// Which deterministic noise field a Dissolve draw samples. A layer and each of
// its dissolved effects use a different field so their dither patterns do not
// land on identical pixels. Values are salts, not indices; they may be
// reordered freely because nothing persists them.
enum class DissolveField : std::uint32_t {
  Layer = 0,
  DropShadow,
  InnerShadow,
  OuterGlow,
  InnerGlow,
  ColorOverlay,
  GradientOverlay,
  PatternOverlay,
  Satin,
  Stroke,
  Bevel,
  Paint  // a Dissolve-mode brush dab (core/paint_mode.hpp)
};

// Photoshop's Dissolve replaces partial coverage with a stochastic all-or-
// nothing paint decision, so it cannot be expressed as a blend_rgb colour
// function. Returns exactly 0 or 1; callers composite the result with
// BlendMode::Normal.
//
// The threshold is a pure function of the DOCUMENT coordinate, which is what
// keeps a dirty-rect repaint, a strip-parallel render and a full flatten
// producing the same pattern. Never make this stateful.
[[nodiscard]] float dissolve_coverage(std::int32_t x, std::int32_t y, float alpha,
                                      DissolveField field = DissolveField::Layer) noexcept;
[[nodiscard]] bool blend_mode_has_special_fill(BlendMode mode) noexcept;
// Paint.NET's Reflect, Glow, Negation and Xor (GitHub issue 379): modes no Photoshop or
// Aseprite file can carry, so those saves write Normal and say so.
[[nodiscard]] bool blend_mode_is_paint_dot_net_only(BlendMode mode) noexcept;
// True when any layer in the tree (groups included) uses one of those modes.
[[nodiscard]] bool layers_use_paint_dot_net_blend_modes(const std::vector<Layer>& layers) noexcept;
// GIMP's Grain Extract and Grain Merge (docs/xcf.md): no Photoshop file carries them either.
[[nodiscard]] bool blend_mode_is_gimp_only(BlendMode mode) noexcept;
[[nodiscard]] bool layers_use_gimp_blend_modes(const std::vector<Layer>& layers) noexcept;
// Either kind: a mode that exists for another editor's files and that no blend menu lists.
[[nodiscard]] bool blend_mode_is_non_photoshop(BlendMode mode) noexcept;
[[nodiscard]] FillCompositeResult composite_special_fill_rgb(
    std::array<std::uint8_t, 3> source, std::array<std::uint8_t, 3> destination, BlendMode mode,
    float source_coverage, float fill_opacity, float layer_opacity, float destination_alpha);
[[nodiscard]] float gradient_stop_opacity(const LayerStyleGradient& gradient, float position,
                                          bool endpoint_smoothing = false);
// endpoint_smoothing applies the Classic catmull-rom ease to a 2-stop
// gradient's single segment (duplicated virtual endpoints), matching the
// GdFl fill-layer renderer measured on Photoshop 27.8 (probe5c). Layer-style
// overlays keep the historical linear 2-stop ramp (their pinned calibration);
// Shape Burst strokes DO ease (photoshop-stroke-shapeburst probes).
// ease_every_method applies that smoothness ease to the segment position for the
// Perceptual and Linear methods too, as Photoshop's Gradient Map does (#250; probe: a
// black-to-white Linear map takes 64 to 125). Fills and layer effects leave it off.
[[nodiscard]] RgbColor gradient_color(const LayerStyleGradient& gradient, float position,
                                      bool endpoint_smoothing = false, bool ease_every_method = false);
// gradient_color without the final rounding, for 16 and 32-bit documents
// (docs/high-bit-depth.md): channels on the 0..255 scale. With linear_light the stops
// interpolate in linear light and the result is linear (Photoshop's 32-bit gradients);
// otherwise the result is display-encoded like gradient_color's. Noise gradients and
// the Perceptual method fall back to gradient_color's bytes (as linear when asked).
[[nodiscard]] std::array<double, 3> gradient_color_precise(const LayerStyleGradient& gradient, float position,
                                                           bool endpoint_smoothing, bool linear_light,
                                                           bool ease_every_method = false);
[[nodiscard]] RgbColor
gradient_color_dithered(const LayerStyleGradient &gradient, float position,
                        std::int32_t x, std::int32_t y, bool endpoint_smoothing = false);
// The dither adjustment alone, for callers that build the color from several
// gradient samples (the Shape Burst tent) before dithering.
[[nodiscard]] RgbColor apply_gradient_dither(const LayerStyleGradient& gradient, RgbColor color,
                                             std::int32_t x, std::int32_t y);
// How Linear/Reflected gradients map the bounds onto the ramp. Layer-style
// overlays span the corner-to-corner projection (w|cos| + h|sin|, calibrated
// separately); GdFl fill layers span the CENTER CHORD through the bounds
// (min(w/|cos|, h/|sin|)) per the probe5c/5d measurements.
enum class GradientSpanBasis {
  LayerProjection,
  CenterChord
};
[[nodiscard]] float gradient_position(const LayerStyleGradient& gradient, Rect bounds, std::int32_t x,
                                      std::int32_t y,
                                      GradientSpanBasis basis = GradientSpanBasis::LayerProjection);

}  // namespace patchy
