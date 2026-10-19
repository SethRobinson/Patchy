#pragma once

// The Brush tool's Mode (docs/brushes.md, "Brush Mode"): how one paint dab combines with
// the pixels under it. Every layer blend mode except Pass Through is a paint mode, plus
// Behind and Clear, which exist only for painting and so are not BlendMode values.
// A dab is the stroke's accumulated coverage (alpha) of the paint color composited
// against the stroke-start pixel, so overlapping dabs never compound a mode twice.
//
// Normal stays on the brush engine's historical byte-exact path; these functions serve
// every other mode. The math is standard compositing through blend_math: a separable or
// non-separable mode blends where the destination is opaque and paints plainly over
// transparency (the source-over form with a blend term), Dissolve turns coverage into an
// all-or-nothing choice from the deterministic dissolve field, Behind is destination-over
// (paint goes under existing pixels), and Clear removes alpha like an eraser.
//
// Behind and Clear change only alpha, so on a target whose alpha cannot change (no alpha
// channel, or Lock Transparent Pixels) they leave the pixel unchanged.

#include "core/blend_math_deep.hpp"
#include "core/layer.hpp"

#include <array>
#include <cstdint>

namespace patchy {

enum class PaintModeKind : std::uint8_t { Blend, Behind, Clear };

struct PaintMode {
  PaintModeKind kind{PaintModeKind::Blend};
  BlendMode blend{BlendMode::Normal};  // read only when kind == Blend

  [[nodiscard]] constexpr bool is_normal() const noexcept {
    return kind == PaintModeKind::Blend && blend == BlendMode::Normal;
  }
  [[nodiscard]] static constexpr PaintMode behind() noexcept { return {PaintModeKind::Behind, BlendMode::Normal}; }
  [[nodiscard]] static constexpr PaintMode clear() noexcept { return {PaintModeKind::Clear, BlendMode::Normal}; }
  [[nodiscard]] static constexpr PaintMode of(BlendMode mode) noexcept { return {PaintModeKind::Blend, mode}; }
  friend constexpr bool operator==(const PaintMode&, const PaintMode&) = default;
};

// False only for Pass Through, which is a group's mode and means nothing for a dab.
[[nodiscard]] bool blend_mode_is_paint_mode(BlendMode mode) noexcept;

// One paint dab on a straight-alpha RGBA8 pixel. `alpha` is the dab's coverage in 0..1
// (already capped by Opacity); (x, y) is the document coordinate the dissolve field reads.
// `has_alpha` is false for a target with no alpha channel, whose alpha byte is ignored
// and returned unchanged; `lock_alpha` keeps the destination alpha (Lock Transparent
// Pixels). The caller skips a locked fully transparent pixel, as for Normal.
[[nodiscard]] std::array<std::uint8_t, 4> composite_paint_rgba8(std::array<std::uint8_t, 4> original,
                                                                std::array<std::uint8_t, 3> paint, float alpha,
                                                                PaintMode mode, std::int32_t x, std::int32_t y,
                                                                bool has_alpha, bool lock_alpha) noexcept;

// The same on 16 and 32-bit values (deep scale: 255 = full, alpha included; Linear-domain
// colors may exceed 255). `paint` is already in `domain`.
[[nodiscard]] std::array<float, 4> composite_paint_deep(std::array<float, 4> original, DeepChannels paint,
                                                        float alpha, PaintMode mode, std::int32_t x, std::int32_t y,
                                                        bool has_alpha, bool lock_alpha, DeepDomain domain) noexcept;

}  // namespace patchy
