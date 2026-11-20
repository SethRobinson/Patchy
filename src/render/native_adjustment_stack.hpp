#pragma once

#include "core/document.hpp"

#include <optional>

namespace patchy {
namespace render_detail {
struct LayerBoundsOverride;
// An opaque, unchanged, full-canvas raster with retained source-space plates.
// A detached editing buffer falls back without scanning pixels during repaint.
[[nodiscard]] const PsdNativeLayerColors* retained_adjustment_base(const Document& document,
                                                                  std::size_t components);
}

enum class LabRenderOutput { DisplayRgba, NativeLab };
// A retained Lab paint layer followed by supported native channel or Lightness
// adjustments, with masks and opacity. Other stacks use the ordinary RGB renderer.
[[nodiscard]] std::optional<PixelBuffer> render_native_lab_adjustments(
    const Document& document, Rect clip, BitDepth depth,
    const std::vector<render_detail::LayerBoundsOverride>* overrides = nullptr,
    LabRenderOutput output = LabRenderOutput::DisplayRgba);

}  // namespace patchy
