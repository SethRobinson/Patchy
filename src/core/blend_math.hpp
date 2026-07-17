#pragma once

#include "core/layer.hpp"

#include <array>
#include <cstdint>

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
[[nodiscard]] bool blend_mode_has_special_fill(BlendMode mode) noexcept;
[[nodiscard]] FillCompositeResult composite_special_fill_rgb(
    std::array<std::uint8_t, 3> source, std::array<std::uint8_t, 3> destination, BlendMode mode,
    float source_coverage, float fill_opacity, float layer_opacity, float destination_alpha);
[[nodiscard]] float gradient_stop_opacity(const LayerStyleGradient& gradient, float position,
                                          bool endpoint_smoothing = false);
// endpoint_smoothing applies the Classic catmull-rom ease to a 2-stop
// gradient's single segment (duplicated virtual endpoints), matching the
// GdFl fill-layer renderer measured on Photoshop 27.8 (probe5c). Layer-style
// overlays keep the historical linear 2-stop ramp (their pinned calibration).
[[nodiscard]] RgbColor gradient_color(const LayerStyleGradient& gradient, float position,
                                      bool endpoint_smoothing = false);
[[nodiscard]] RgbColor
gradient_color_dithered(const LayerStyleGradient &gradient, float position,
                        std::int32_t x, std::int32_t y, bool endpoint_smoothing = false);
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
