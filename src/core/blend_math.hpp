#pragma once

#include "core/layer.hpp"

#include <array>
#include <cstdint>

namespace photoslop {

[[nodiscard]] std::uint8_t clamp_byte(float value);
[[nodiscard]] float clamp_unit(float value);
[[nodiscard]] std::array<std::uint8_t, 3> blend_rgb(std::array<std::uint8_t, 3> source,
                                                    std::array<std::uint8_t, 3> destination, BlendMode mode);
[[nodiscard]] float gradient_stop_opacity(const LayerStyleGradient& gradient, float position);
[[nodiscard]] RgbColor gradient_color(const LayerStyleGradient& gradient, float position);
[[nodiscard]] float gradient_position(const LayerStyleGradient& gradient, Rect bounds, std::int32_t x, std::int32_t y);

}  // namespace photoslop
