#pragma once

#include <cstdint>

namespace patchy {

// The classic healing membrane taught by Adobe's EXPIRED US 6587592 (Georgiev,
// priority 2001, expired 2021-11-19; also published as "Photoshop Healing
// Brush: a Tool for Seamless Cloning"): the destination-minus-source offsets
// along the region boundary are interpolated harmonically across the interior
// (a relaxation solve of Laplace's equation on the OFFSET field with Dirichlet
// boundary conditions), and the healed result is source texture plus the
// smooth offset membrane. Practicing this exact expired teaching is cleared
// prior art. It is NOT gradient-domain compositing: no source gradients are
// composited and no Poisson guidance field exists (US 9058699 claims a
// different, later technique). See docs/legal-constraints.md and the dated
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

}  // namespace patchy
