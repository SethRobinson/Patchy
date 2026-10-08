#pragma once

// The color types the compositor runs on (docs/high-bit-depth.md). An 8-bit target
// composites RgbColor exactly as it always has; a deep target (16 and 32-bit documents)
// declares `using color_type = DeepRgb;` and receives float colors on the deep scale
// (core/pixel_depth.hpp: 255 = full scale, Linear-domain values may exceed it). Code
// shared by both instantiations goes through the helpers here, whose RgbColor overloads
// are the historical 8-bit expressions verbatim, so 8-bit output bytes cannot move.

#include "core/blend_math.hpp"
#include "core/blend_math_deep.hpp"
#include "core/layer.hpp"
#include "core/pixel_depth.hpp"

#include <array>
#include <cstdint>
#include <type_traits>

namespace patchy::render_detail {

struct DeepRgb {
  float red{0.0F};
  float green{0.0F};
  float blue{0.0F};

  friend bool operator==(const DeepRgb&, const DeepRgb&) = default;
};

template <typename Color>
struct CompositeSampleT {
  Color color{};
  float alpha{0.0F};
};

using CompositeSample = CompositeSampleT<RgbColor>;

template <typename Target, typename = void>
struct target_color_impl {
  using type = RgbColor;
};
template <typename Target>
struct target_color_impl<Target, std::void_t<typename Target::color_type>> {
  using type = typename Target::color_type;
};
template <typename Target>
using target_color_t = typename target_color_impl<std::remove_cvref_t<Target>>::type;
template <typename Target>
inline constexpr bool is_deep_target_v = std::is_same_v<target_color_t<Target>, DeepRgb>;

// The per-channel storage a color type keeps in isolated buffers and snapshots.
template <typename Color>
using color_channel_t = std::conditional_t<std::is_same_v<Color, DeepRgb>, float, std::uint8_t>;
// The 3-channel array the blend helpers take.
template <typename Color>
using color_array_t = std::array<color_channel_t<Color>, 3>;

// The deep domain of a target: Encoded for 8-bit ones (unused there).
template <typename Target>
[[nodiscard]] DeepDomain target_domain(const Target& target) noexcept {
  if constexpr (requires { target.deep_domain(); }) {
    return target.deep_domain();
  } else {
    return DeepDomain::Encoded;
  }
}

// An 8-bit color (layer style colors, effect and gradient colors stay 8-bit parameters)
// as a deep color in `domain`.
[[nodiscard]] inline DeepRgb deep_from_byte(RgbColor color, DeepDomain domain) noexcept {
  if (domain == DeepDomain::Encoded) {
    return DeepRgb{static_cast<float>(color.red), static_cast<float>(color.green), static_cast<float>(color.blue)};
  }
  const auto decode = [](std::uint8_t value) {
    return static_cast<float>(srgb_decode(static_cast<double>(value) / 255.0) * 255.0);
  };
  return DeepRgb{decode(color.red), decode(color.green), decode(color.blue)};
}

[[nodiscard]] inline DeepChannels deep_channels(DeepRgb color) noexcept {
  return {color.red, color.green, color.blue};
}
[[nodiscard]] inline DeepRgb deep_rgb(const DeepChannels& channels) noexcept {
  return DeepRgb{channels[0], channels[1], channels[2]};
}

// Converts a color into the target's color type (identity for matching types).
template <typename Color>
[[nodiscard]] Color as_target_color(RgbColor color, DeepDomain domain) noexcept {
  if constexpr (std::is_same_v<Color, DeepRgb>) {
    return deep_from_byte(color, domain);
  } else {
    (void)domain;
    return color;
  }
}
template <typename Color>
[[nodiscard]] Color as_target_color(DeepRgb color, DeepDomain) noexcept {
  static_assert(std::is_same_v<Color, DeepRgb>, "an 8-bit target cannot take a deep color");
  return color;
}

// Builds a color from float channel values: the 8-bit rule is clamp_byte (lround and
// clamp), the deep one keeps the value (the target clamps on store where its domain does).
template <typename Color>
[[nodiscard]] Color color_from_floats(float red, float green, float blue) noexcept {
  if constexpr (std::is_same_v<Color, DeepRgb>) {
    return DeepRgb{red, green, blue};
  } else {
    return RgbColor{clamp_byte(red), clamp_byte(green), clamp_byte(blue)};
  }
}

template <typename Color>
[[nodiscard]] color_array_t<Color> color_array(const Color& color) noexcept {
  return {color.red, color.green, color.blue};
}
template <typename Color>
[[nodiscard]] Color color_from_array(const color_array_t<Color>& array) noexcept {
  return Color{array[0], array[1], array[2]};
}

// composite_blended_rgb for either array type.
[[nodiscard]] inline std::array<std::uint8_t, 3> blend_composite(std::array<std::uint8_t, 3> source,
                                                                 std::array<std::uint8_t, 3> destination,
                                                                 BlendMode mode, float source_alpha,
                                                                 float destination_alpha, DeepDomain) {
  return composite_blended_rgb(source, destination, mode, source_alpha, destination_alpha);
}
[[nodiscard]] inline DeepChannels blend_composite(DeepChannels source, DeepChannels destination, BlendMode mode,
                                                  float source_alpha, float destination_alpha, DeepDomain domain) {
  return composite_blended_rgb_deep(source, destination, mode, source_alpha, destination_alpha, domain);
}

// Blend If factors for either color type.
[[nodiscard]] inline float blend_if_source_factor_for(const LayerBlendIf& settings, RgbColor source, DeepDomain) {
  return static_cast<float>(blend_if_source_alpha_byte(settings, source)) / 255.0F;
}
[[nodiscard]] inline float blend_if_source_factor_for(const LayerBlendIf& settings, DeepRgb source,
                                                      DeepDomain domain) {
  return blend_if_source_factor_deep(settings, deep_channels(source), domain);
}
[[nodiscard]] inline float blend_if_underlying_factor_for(const LayerBlendIf& settings, RgbColor underlying,
                                                          DeepDomain) {
  return static_cast<float>(blend_if_underlying_alpha_byte(settings, underlying)) / 255.0F;
}
[[nodiscard]] inline float blend_if_underlying_factor_for(const LayerBlendIf& settings, DeepRgb underlying,
                                                          DeepDomain domain) {
  return blend_if_underlying_factor_deep(settings, deep_channels(underlying), domain);
}

}  // namespace patchy::render_detail
