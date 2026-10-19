#pragma once

#include "core/document.hpp"

#include <cstdint>
#include <optional>

namespace patchy {
namespace render_detail { struct LayerBoundsOverride; }

// Native ink-channel composition for imported 8-bit CMYK layer stacks. Returns
// straight RGBA over clip, or nullopt for a stack requiring the RGB renderer.
[[nodiscard]] std::optional<PixelBuffer> render_native_cmyk8(
    const Document& document, Rect clip,
    const std::vector<render_detail::LayerBoundsOverride>* overrides = nullptr);

// Process-wide work counters of render_native_cmyk8, for the tests that keep a brush
// stroke's per-dab cost bounded (docs/cmyk.md): Little CMS transforms built, and RGB
// pixels separated into inks.
struct NativeCmykRenderStats {
  std::uint64_t transform_builds{0};
  std::uint64_t separated_pixels{0};
};
[[nodiscard]] NativeCmykRenderStats native_cmyk_render_stats() noexcept;

}  // namespace patchy
