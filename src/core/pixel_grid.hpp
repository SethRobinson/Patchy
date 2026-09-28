#pragma once

#include <cmath>
#include <cstdint>

namespace patchy {

// Photoshop's whole-pixel rule, pinned by PS 27.9 COM captures (September 2026): a
// fractional document coordinate lands on floor(value + 0.5). Halves round UP (6.5 -> 7,
// -3.5 -> -3), never away from zero, so this is not std::lround. Photoshop applies it to
// the destination rect edges of an axis-aligned Free Transform, to a type layer's anchor
// (tx, ty) before rasterizing, to Move-tool and scripted translations, and to smart-object
// quad corners while "Snap Vector Tools and Transforms to Pixel Grid" is on. See
// docs/tools.md (Free Transform) and docs/text-render-calibration.md (Pixel grid).
[[nodiscard]] inline double snap_to_pixel_grid(double value) noexcept {
  return std::floor(value + 0.5);
}

[[nodiscard]] inline std::int32_t snapped_pixel_coordinate(double value) noexcept {
  return static_cast<std::int32_t>(snap_to_pixel_grid(value));
}

}  // namespace patchy
