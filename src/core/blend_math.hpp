#pragma once

#include "core/layer.hpp"

#include <array>
#include <cstdint>

namespace patchy {

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
[[nodiscard]] float gradient_stop_opacity(const LayerStyleGradient& gradient, float position);
[[nodiscard]] RgbColor gradient_color(const LayerStyleGradient& gradient, float position);
[[nodiscard]] RgbColor
gradient_color_dithered(const LayerStyleGradient &gradient, float position,
                        std::int32_t x, std::int32_t y);
[[nodiscard]] float gradient_position(const LayerStyleGradient& gradient, Rect bounds, std::int32_t x, std::int32_t y);

}  // namespace patchy
