#pragma once

#include <array>
#include <cstdint>

namespace patchy {

// The classic healing membrane taught by Adobe's EXPIRED US 6587592 (Georgiev,
// priority 2001, expired 2021-11-19; also published as "Photoshop Healing
// Brush: a Tool for Seamless Cloning"): the destination-minus-source offsets
// along the region boundary are interpolated harmonically across the interior
// (a relaxation solve of Laplace's equation on the OFFSET field with Dirichlet
// boundary conditions), and the healed result is source texture plus the
// smooth offset membrane. This is exactly that expired teaching, published
// earlier work. It is NOT gradient-domain compositing: no source gradients are
// composited and no Poisson guidance field exists (US 9058699 claims a
// different, later technique). See docs/legal-constraints-filters.md and the dated
// record in docs/patent-research.md.
//
// `interior` is a row-major byte mask over the width x height grid: non-zero
// cells are solved, zero cells are fixed Dirichlet cells whose
// `offsets_rgb` values (3 interleaved channels per cell, destination minus
// source, range roughly [-255, 255]) are the boundary conditions. On return,
// interior cells hold the interpolated membrane; Dirichlet cells are
// unchanged. Deterministic across toolchains: the solver is integer 16.16
// fixed-point successive over-relaxation on a coarse-to-fine cascade with
// fixed sweep counts and lexicographic order (AGENTS.md determinism rule).
void solve_heal_membrane(const std::uint8_t* interior, std::int32_t width, std::int32_t height,
                         std::int16_t* offsets_rgb);

// Content-Aware Move's edge band: scales each solved interior offset by a weight that is
// 1 next to the boundary and falls linearly to 0 `band` cells inside it (an integer 3-4
// chamfer distance), so only a band along the outline takes the surroundings' tone and
// the middle keeps its own pixels. A plain distance feather on the membrane above; no
// gradients are involved. Integer math, deterministic.
void fade_heal_offsets_to_edge_band(const std::uint8_t* interior, std::int32_t width, std::int32_t height,
                                    std::int16_t* offsets_rgb, std::int32_t band);

// Alpha in the membrane (desk card local-039 follow-up). When a heal reads or writes pixels
// that are not opaque, its color offsets are taken between alpha-premultiplied values and
// alpha gets offsets of its own, solved by the same membrane as a separate field (alpha in the
// first channel, the other two zero). The heal then neither darkens toward the black that
// transparent pixels store nor turns transparent pixels opaque; with every pixel opaque it is
// the plain membrane. Straight RGBA on the 0..255 scale (16-bit / 257, 32-bit linear * 255).

// True when either pixel is not opaque.
[[nodiscard]] constexpr bool heal_needs_alpha(const std::array<float, 4>& destination,
                                              const std::array<float, 4>& source) noexcept {
  return destination[3] < 255.0F || source[3] < 255.0F;
}
// Destination minus source: premultiplied color in [0..2], alpha in [3]; each times `scale`,
// rounded and clamped to the solver's int16 range.
[[nodiscard]] std::array<std::int32_t, 4> heal_alpha_offsets(const std::array<float, 4>& destination,
                                                             const std::array<float, 4>& source,
                                                             float scale) noexcept;
// The healed straight RGBA: the source's premultiplied color plus `color_offsets` (3 values)
// and its alpha plus `alpha_offset`, all offsets times `scale`. Alpha clamps to 0..255;
// color clamps to 0..255, or only at 0 with `unbounded_color` (32-bit linear).
[[nodiscard]] std::array<float, 4> heal_with_alpha(const std::array<float, 4>& source,
                                                   const std::int16_t* color_offsets, std::int16_t alpha_offset,
                                                   float scale, bool unbounded_color) noexcept;

}  // namespace patchy
