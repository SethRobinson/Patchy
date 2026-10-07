#pragma once

#include "core/pixel_buffer.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace patchy {

// Image Size resampling (docs/resampling.md). Append-only: the values back the permanent
// ids below, which the Image Size dialog remembers and `doc.resizeImage` accepts.
enum class ResampleMethod {
  Automatic,
  NearestNeighbor,
  Bilinear,
  Bicubic,
  BicubicSmoother,
  BicubicSharper
};

// Permanent ids: "automatic", "nearest", "bilinear", "bicubic", "bicubicSmoother",
// "bicubicSharper". Settings and scripts store these, never the enum integers.
[[nodiscard]] std::string_view resample_method_id(ResampleMethod method) noexcept;
[[nodiscard]] std::optional<ResampleMethod> parse_resample_method(std::string_view id) noexcept;
// Every method in enum order, for combos and error messages.
[[nodiscard]] std::span<const ResampleMethod> all_resample_methods() noexcept;

// Automatic follows Photoshop: Bicubic Sharper when the target has fewer pixels than the
// source, Bicubic Smoother when it has more, plain Bicubic for an equal area. Any other
// method returns unchanged. Resolve once per document resize so every layer, mask and
// channel of that resize uses the same kernel.
[[nodiscard]] ResampleMethod resolve_automatic_resample_method(ResampleMethod method, std::int32_t source_width,
                                                                std::int32_t source_height, std::int32_t target_width,
                                                                std::int32_t target_height) noexcept;

// The kernels, shared with Free Transform's inverse-mapping samplers
// (src/ui/canvas_widget_transform.cpp). `distance` is in source pixels.
//
// Mitchell-Netravali BC-spline family (support 2): Catmull-Rom is B 0, C 0.5; Mitchell is
// B 1/3, C 1/3; Keys a = -0.75 is B 0, C 0.75.
[[nodiscard]] double cubic_bc_weight(double distance, double b, double c) noexcept;
// Catmull-Rom spelled with its literal coefficients: Free Transform's bicubic is pinned on
// this exact expression, so it stays separate from the general form above.
[[nodiscard]] double cubic_weight(double distance) noexcept;
// Bilinear's tent (support 1).
[[nodiscard]] double triangle_weight(double distance) noexcept;

struct ResampleKernel {
  enum class Kind {
    Nearest,
    Triangle,
    Cubic
  };
  Kind kind{Kind::Cubic};
  double support{2.0};  // half-width in source pixels at a 1:1 scale
  double b{0.0};        // Cubic only
  double c{0.5};
  [[nodiscard]] double weight(double distance) const noexcept;
};
// Automatic maps to the Bicubic kernel; callers resolve it first with the function above.
[[nodiscard]] ResampleKernel resample_kernel(ResampleMethod method) noexcept;

// Axis-aligned resize of any PixelBuffer (8-bit, 16-bit or float; any channel count) to
// `width` x `height`. Separable two-pass filter with per-axis weight tables: output pixel
// i samples the source around (i + 0.5) * source / target; on a reduction the kernel
// widens by the scale factor so the result averages instead of aliasing; taps outside the
// buffer are dropped and the rest renormalized (a bilinear enlargement therefore matches
// clamp-to-edge sampling exactly). Buffers with an alpha channel (one channel more than
// the color mode's color channels) interpolate premultiplied. Nearest Neighbor copies
// pixels verbatim. Deterministic: fixed summation order, single-threaded.
[[nodiscard]] PixelBuffer resample_pixels(const PixelBuffer& source, std::int32_t width, std::int32_t height,
                                          ResampleMethod method);

}  // namespace patchy
