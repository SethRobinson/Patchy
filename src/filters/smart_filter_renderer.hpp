#pragma once

#include "core/smart_filter.hpp"
#include "filters/filter_registry.hpp"

namespace patchy {

// Renders a complete native Smart Filter stack from the immutable placed or
// warped Smart Object preview. Unsupported semantics throw
// std::invalid_argument; cancellation throws FilterCancelled.
[[nodiscard]] FilterRenderResult
render_smart_filter_stack(const PixelBuffer &placed_pixels, Rect placed_bounds,
                          Rect filter_canvas_bounds,
                          const SmartFilterStack &stack,
                          const FilterProgress *progress = nullptr);

// Compatibility/testing form: filters within the supplied placed raster.
// Production native Smart Filters pass the document-space FEid cache bounds
// through the overload above so blur output can grow inside that canvas.
[[nodiscard]] FilterRenderResult
render_smart_filter_stack(const PixelBuffer &placed_pixels, Rect placed_bounds,
                          const SmartFilterStack &stack,
                          const FilterProgress *progress = nullptr);

} // namespace patchy
