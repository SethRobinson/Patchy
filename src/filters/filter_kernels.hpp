#pragma once

// The 8-bit filter kernels that the legacy built-in filters (builtin_filters.cpp),
// the named-parameter engine (filter_engine.cpp) and the native Smart Filter renderer
// (smart_filter_renderer.cpp) used to each carry a verbatim copy of. One body each
// now; every caller's output stays byte-identical to its historical pixels (the
// legacy pins, the engine canaries and the smart-filter parity tests all run against
// these). Internal to src/filters: do not include this header elsewhere.
//
// Conventions shared by every kernel: `original` is the untouched source and
// `pixels` the destination of the same size and format (a kernel that reads a
// whole block before writing it may be given the same buffer for both, as the
// legacy in-place Pixel Mosaic does); RGB input is treated as alpha 255; a null
// `progress` reports nothing; a progress callback returning false throws
// FilterCancelled. Every kernel keeps the exact arithmetic and summation order of the
// copies it replaced. Do not change a body to make two callers agree.

#include "core/pixel_buffer.hpp"
#include "filters/filter_registry.hpp"

#include <array>
#include <cstdint>

namespace patchy {

// Clamps `completed` into [0, max(1, total)] and reports it; throws
// FilterCancelled when the callback declines.
void report_kernel_progress(const FilterProgress *progress, int completed, int total, FilterProgressStage stage);

// The position hash behind Add Noise, Clouds and Film Grain (one per TU before).
[[nodiscard]] std::uint32_t position_noise_hash(std::int32_t x, std::int32_t y, std::uint32_t seed) noexcept;

// (30R + 59G + 11B) / 100 in integers, and its bilinear edge-clamped sample.
[[nodiscard]] int integer_luminance(const std::uint8_t *px) noexcept;
[[nodiscard]] double sampled_luminance(const PixelBuffer &pixels, double x, double y);

// Premultiplied, weight-tracking accumulator for resampling kernels (Radial Blur,
// Twirl, Wave, Pinch, the legacy blur taps): straight color comes back as
// sum(color * alpha * w) / sum(alpha * w), alpha as sum(alpha * w) / sum(w).
struct PremultipliedAccum {
  std::array<double, 3> premultiplied_color{0.0, 0.0, 0.0};
  double alpha{0.0};
  double weight{0.0};
};
void accumulate_premultiplied_pixel(PremultipliedAccum &accum, const PixelBuffer &original, const std::uint8_t *px,
                                    double weight);
// Bilinear sample at (x, y), edge-clamped, as four weighted pixel accumulations.
void accumulate_bilinear_sample(PremultipliedAccum &accum, const PixelBuffer &original, double x, double y,
                                double weight = 1.0);
void write_accumulated_pixel(PixelBuffer &pixels, std::int32_t x, std::int32_t y, const PremultipliedAccum &accum);

// Alpha-weighted separable box average (or, `weighted`, the tent used by Glowing
// Edges) over an edge-clamped window. Radii through 12 take the direct
// double-precision path, larger radii the exact int64 running-sum path; the radius
// is clamped to 1..12 (weighted) or 1..2000. Reports the Blurring stage per row.
void box_blur_kernel(PixelBuffer &pixels, const PixelBuffer &original, int radius, bool weighted,
                     const FilterProgress *progress);

// Rotational sweep of amount * 3.6 degrees about (center_x, center_y) in pixel
// coordinates, `samples` bilinear taps per pixel; amount clamps to 0..100, samples
// to 4..32. Reports the Blurring stage per row.
void radial_blur_kernel(PixelBuffer &pixels, const PixelBuffer &original, int amount, int samples, double center_x,
                        double center_y, const FilterProgress *progress);

// Bilinear luminance at +/- `distance` along `angle_degrees`, written as
// clamp(128 + (highlight - shadow) * amount / 100) into R, G and B; alpha untouched.
// Callers clamp their own ranges. Reports the Embossing stage per row.
void emboss_kernel(PixelBuffer &pixels, const PixelBuffer &original, int angle_degrees, double distance,
                   int amount_percent, const FilterProgress *progress);

// Alpha-weighted block means on a grid anchored at the origin, every pixel of a
// block written with the block's straight color and normalized alpha. Each block
// is read completely before it is written, so `pixels` may alias `original`.
// Reports the Pixelating stage per block row.
void mosaic_kernel(PixelBuffer &pixels, const PixelBuffer &original, std::int32_t cell_size, const FilterProgress *progress);

}  // namespace patchy
