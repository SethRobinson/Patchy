#pragma once

#include "filters/filter_registry.hpp"

#include <string_view>

namespace patchy {

// Returns the built-in catalog entry for identifier, or an empty metadata
// record when identifier is not a built-in filter.
[[nodiscard]] FilterCatalogMetadata
builtin_filter_catalog(std::string_view identifier);

// Patchy's deterministic variable-radius Tilt-Shift Blur. The geometry values
// are expressed against the supplied buffer: center and band widths are
// percentages, while blur is a compact-support pixel radius.
void apply_tilt_shift_blur_filter(PixelBuffer &pixels, double blur_pixels,
                                  double center_x_percent,
                                  double center_y_percent, int angle_degrees,
                                  double focus_half_width_percent,
                                  double transition_width_percent,
                                  const FilterProgress *progress = nullptr);

// A fixed, user-directed aperture convolution. It deliberately has no depth
// inference, highlight detection, or content-adaptive behavior.
void apply_lens_blur_filter(PixelBuffer &pixels, double radius_pixels,
                            int blade_count, int blade_curvature_percent,
                            int rotation_degrees,
                            const FilterProgress *progress = nullptr);

// A single explicit elliptical focus region. The renderer blends one
// precomputed aperture blur through a fixed geometric mask; it does not author
// or partially emulate Photoshop's multi-widget Blur Gallery descriptor.
void apply_iris_blur_filter(PixelBuffer &pixels, double blur_pixels,
                            double center_x_percent,
                            double center_y_percent, int angle_degrees,
                            double iris_width_percent,
                            double iris_height_percent,
                            double focus_percent,
                            const FilterProgress *progress = nullptr);

} // namespace patchy
