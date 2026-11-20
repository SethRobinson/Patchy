#pragma once

#include "core/layer.hpp"
#include <array>

namespace patchy {

// Import-only provenance. The ASE writer stores ordinary mode numbers. Photoshop
// has its own alpha arithmetic, so its writer removes this on its save snapshot.
inline constexpr char kAsepriteCelBlend[] = "patchy.aseprite.cel_blend";

[[nodiscard]] bool uses_aseprite_cel_blend(const Layer& layer);

[[nodiscard]] inline bool aseprite_component_mode(BlendMode mode) noexcept {
  return mode == BlendMode::Hue || mode == BlendMode::Saturation ||
         mode == BlendMode::Color || mode == BlendMode::Luminosity;
}

[[nodiscard]] std::array<std::uint8_t, 3> aseprite_component_color(std::array<std::uint8_t, 3> source,
    std::array<std::uint8_t, 3> backdrop, BlendMode mode);

struct AsepriteCompositePixel {
  RgbColor color;
  std::uint8_t alpha;
};

[[nodiscard]] AsepriteCompositePixel composite_aseprite_cel(RgbColor source, RgbColor backdrop,
    std::uint8_t source_alpha, std::uint8_t backdrop_alpha, BlendMode mode);

}  // namespace patchy
