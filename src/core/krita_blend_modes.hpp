#pragma once

#include "core/layer.hpp"

#include <cstdint>

namespace patchy {

// Krita's own per-channel blend modes (docs/blend-modes.md, "Krita modes"; Krita .kra
// import, docs/kra.md): Soft Light (SVG), Additive-Subtractive, Arc Tangent, Geometric Mean,
// Allanon, Parallel, Hard Overlay, Interpolation and Easy Dodge. Each was measured on Krita
// 5.3's renders of every 8-bit layer and backdrop pair and matches them exactly or within
// one level (rounding).
[[nodiscard]] bool blend_mode_is_krita_only(BlendMode mode) noexcept;

// The 8-bit kernel: `src` is the layer, `dst` the backdrop.
[[nodiscard]] std::uint8_t krita_blend_channel8(std::uint8_t src, std::uint8_t dst, BlendMode mode) noexcept;

// The same functions on 0..1 floats (16 and 32-bit documents); inputs are clamped first.
[[nodiscard]] float krita_blend_unit(float s, float d, BlendMode mode) noexcept;

}  // namespace patchy
