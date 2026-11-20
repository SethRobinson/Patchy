#include "core/aseprite_blend.hpp"
#include "core/blend_math.hpp"

#include <algorithm>
#include <array>

namespace patchy {
namespace {

// Fixed-point multiplication fitted to the public two-cel ASE/PNG pairs,
// including negative differences on the second interpolation.
int multiply_byte(int delta, int coverage) noexcept {
  const int product = delta * coverage + 128;
  return (product + (product >> 8)) >> 8;
}

int hard_light(int source, int backdrop) noexcept {
  return source < 128 ? multiply_byte(backdrop, 2 * source)
                      : 255 - multiply_byte(255 - backdrop, 2 * (255 - source));
}

}  // namespace

bool uses_aseprite_cel_blend(const Layer& layer) {
  if (layer.metadata().empty() || !layer.metadata().contains(kAsepriteCelBlend) || layer.kind() != LayerKind::Pixel ||
      layer.pixels().format() != PixelFormat::rgba8() || layer.clipped() || !layer.layer_style().empty() ||
      layer.mask().has_value() || layer.vector_mask() != nullptr || layer.restricted_channels() != 0 ||
      !layer.raw_psd_blending_ranges().empty() || layer.fill_opacity() != 1.0F) {
    return false;
  }
  switch (layer.blend_mode()) {
    case BlendMode::Normal: case BlendMode::Multiply: case BlendMode::Screen: case BlendMode::Overlay:
    case BlendMode::Darken: case BlendMode::Lighten: case BlendMode::ColorDodge: case BlendMode::ColorBurn:
    case BlendMode::HardLight: case BlendMode::SoftLight: case BlendMode::Difference: case BlendMode::Exclusion:
    case BlendMode::LinearDodge: case BlendMode::Subtract: case BlendMode::Divide:
    case BlendMode::Hue: case BlendMode::Saturation: case BlendMode::Color: case BlendMode::Luminosity:
      return true;
    default:
      return false;
  }
}

AsepriteCompositePixel composite_aseprite_cel(RgbColor source, RgbColor backdrop, std::uint8_t sa,
                                              std::uint8_t ba, BlendMode mode) {
  if (sa == 0) return {backdrop, ba};
  if (ba == 0 || (sa == 255 && mode == BlendMode::Normal)) return {source, sa};
  const auto overlap = multiply_byte(sa, ba);
  const auto alpha = sa + ba - overlap;
  const std::array<std::uint8_t, 3> s{source.red, source.green, source.blue};
  const std::array<std::uint8_t, 3> b{backdrop.red, backdrop.green, backdrop.blue};
  auto blended = aseprite_component_mode(mode) ? aseprite_component_color(s, b, mode) : blend_rgb(s, b, mode);
  for (std::size_t i = 0; i < s.size(); ++i) {
    int value = blended[i];
    switch (mode) {
      case BlendMode::Multiply: value = multiply_byte(s[i], b[i]); break;
      case BlendMode::Screen: value = 255 - multiply_byte(255 - s[i], 255 - b[i]); break;
      case BlendMode::HardLight: value = hard_light(s[i], b[i]); break;
      case BlendMode::Overlay: value = hard_light(b[i], s[i]); break;
      default: break;
    }
    // Straight-alpha source-over truncates its signed delta before it is
    // added to the backdrop. The blend contribution is applied in two steps.
    const int normal = b[i] + (s[i] - b[i]) * sa / alpha;
    const int result = b[i] + (value - b[i]) * sa / alpha;
    const int mid = normal + multiply_byte(result - normal, ba);
    blended[i] = static_cast<std::uint8_t>(mid + multiply_byte(result - mid, overlap));
  }
  return {{blended[0], blended[1], blended[2]}, static_cast<std::uint8_t>(alpha)};
}

}  // namespace patchy
