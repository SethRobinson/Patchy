#pragma once

#include "core/smart_filter.hpp"
#include "filters/filter_registry.hpp"

namespace patchy {

// Photoshop-calibrated primitives shared by destructive filters and native
// Smart Filters. Inputs and outputs retain the supplied bounds and require
// RGBA8 pixels.
[[nodiscard]] FilterRenderResult render_photoshop_gaussian_blur(
    const PixelBuffer &pixels, Rect bounds, double radius_pixels,
    const FilterProgress *progress = nullptr);
[[nodiscard]] FilterRenderResult render_photoshop_high_pass(
    const PixelBuffer &pixels, Rect bounds, double radius_pixels,
    const FilterProgress *progress = nullptr);
[[nodiscard]] FilterRenderResult render_photoshop_median(
    const PixelBuffer &pixels, Rect bounds, double radius_pixels,
    const FilterProgress *progress = nullptr);
[[nodiscard]] FilterRenderResult render_photoshop_dust_and_scratches(
    const PixelBuffer &pixels, Rect bounds, std::int32_t radius_pixels,
    std::int32_t threshold, const FilterProgress *progress = nullptr);

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
