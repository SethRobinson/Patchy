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
[[nodiscard]] FilterRenderResult
render_photoshop_surface_blur(const PixelBuffer &pixels, Rect bounds,
                              double radius_pixels, std::int32_t threshold,
                              const FilterProgress *progress = nullptr);
[[nodiscard]] FilterRenderResult
render_photoshop_unsharp_mask(const PixelBuffer &pixels, Rect bounds,
                              double amount_percent, double radius_pixels,
                              std::int32_t threshold,
                              const FilterProgress *progress = nullptr);
[[nodiscard]] FilterRenderResult render_photoshop_motion_blur(
    const PixelBuffer &pixels, Rect bounds, std::int32_t angle_degrees,
    std::int32_t distance_pixels, const FilterProgress *progress = nullptr);
[[nodiscard]] FilterRenderResult render_plastic_wrap(
    const PixelBuffer &pixels, Rect bounds, std::int32_t highlight_strength,
    std::int32_t detail, std::int32_t smoothness,
    const FilterProgress *progress = nullptr);
// Mosaic keeps the input bounds and block-averages alpha-weighted straight
// RGB plus alpha over a cell grid anchored at the input's local origin (the
// destructive Pixel Mosaic math). Like Plastic Wrap, the native descriptor
// round-trips exactly while the pixels are Patchy's own compatible rendering.
[[nodiscard]] FilterRenderResult render_mosaic(
    const PixelBuffer &pixels, Rect bounds, std::int32_t cell_size_pixels,
    const FilterProgress *progress = nullptr);
// Emboss keeps the input bounds and alpha, replacing RGB with the
// destructive filter's gray relief (bilinear luminance samples at +/- the
// height offset along the angle). Like Plastic Wrap, the native descriptor
// round-trips exactly while the pixels are Patchy's own compatible rendering.
[[nodiscard]] FilterRenderResult render_emboss(
    const PixelBuffer &pixels, Rect bounds, std::int32_t angle_degrees,
    std::int32_t height_pixels, std::int32_t amount_percent,
    const FilterProgress *progress = nullptr);
// Box Blur keeps the supplied bounds (the stack embeds in the filter canvas
// and alpha-trims, like Gaussian Blur). Radii through 12 px replicate the
// destructive Box Blur's double math byte for byte; larger radii use an
// exact integer sliding-window path with identical edge-clamped sampling.
// The fractional stored radius is floored for rendering like Median.
[[nodiscard]] FilterRenderResult render_box_blur(
    const PixelBuffer &pixels, Rect bounds, double radius_pixels,
    const FilterProgress *progress = nullptr);
// Radial Blur (Photoshop's Spin method) rotationally sweeps bilinear
// premultiplied samples about the supplied center, given in BUFFER
// coordinates. It replicates the destructive patchy.filters.radial_blur math
// byte for byte; the stack embeds in the filter canvas first so output can
// grow inside it like Gaussian Blur. Quality maps Draft/Good/Best to
// 8/16/32 samples. Photoshop's Zoom method is deliberately unsupported and
// keeps its stack preview-locked.
[[nodiscard]] FilterRenderResult render_radial_blur(
    const PixelBuffer &pixels, Rect bounds, std::int32_t amount,
    std::int32_t samples, double center_x, double center_y,
    const FilterProgress *progress = nullptr);
// Add Noise keeps the input bounds and alpha, adding deterministic
// position-hashed noise to RGB: uniform, or a sum-of-four-uniforms gaussian
// approximation; monochromatic applies one delta to all three channels. The
// native FlRs seed feeds the hash so re-renders are reproducible; the pixels
// are Patchy's own compatible rendering, not Photoshop's noise, and match the
// destructive patchy.filters.add_noise byte for byte.
[[nodiscard]] FilterRenderResult render_add_noise(
    const PixelBuffer &pixels, Rect bounds, double amount_percent,
    bool gaussian, bool monochromatic, std::int32_t seed,
    const FilterProgress *progress = nullptr);

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
