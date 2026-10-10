#pragma once

#include "core/document.hpp"

#include <optional>

namespace patchy {
namespace render_detail { struct LayerBoundsOverride; }

// Native ink-channel composition for imported 8-bit CMYK layer stacks. Returns
// straight RGBA over clip, or nullopt for a stack requiring the RGB renderer.
[[nodiscard]] std::optional<PixelBuffer> render_native_cmyk8(
    const Document& document, Rect clip,
    const std::vector<render_detail::LayerBoundsOverride>* overrides = nullptr);

}  // namespace patchy
