#pragma once

#include "core/layer.hpp"
#include "core/rect_utils.hpp"

#include <cstdint>
#include <vector>

namespace photoslop {

[[nodiscard]] Rect outset_rect(Rect rect, int amount) noexcept;
[[nodiscard]] Rect clipped_mask_bounds(Rect full_bounds, Rect draw_rect, int sample_padding) noexcept;
[[nodiscard]] Rect layer_pixel_bounds(const Layer& layer);
[[nodiscard]] float layer_mask_alpha_at(const Layer& layer, std::int32_t x, std::int32_t y);
[[nodiscard]] std::vector<float> layer_alpha_mask(const Layer& layer, Rect bounds, Rect mask_bounds,
                                                  std::int32_t sample_offset_x = 0,
                                                  std::int32_t sample_offset_y = 0);

}  // namespace photoslop
