#pragma once

#include "core/document.hpp"
#include <optional>

namespace patchy {
namespace render_detail { struct LayerBoundsOverride; }

// Imported duotone stacks compose in gray before the ink lookup is applied.
// Unsupported or edited color content uses the ordinary RGB editing preview.
[[nodiscard]] std::optional<PixelBuffer> render_native_duotone8(
    const Document& document, Rect clip,
    const std::vector<render_detail::LayerBoundsOverride>* overrides = nullptr,
    bool display_colors = true);

}  // namespace patchy
