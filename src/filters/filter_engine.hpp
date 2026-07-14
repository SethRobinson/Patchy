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

} // namespace patchy
