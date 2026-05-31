#pragma once

#include "core/document.hpp"
#include "core/layer.hpp"
#include "core/rect_utils.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace patchy {

[[nodiscard]] Rect outset_rect(Rect rect, int amount) noexcept;
[[nodiscard]] Rect clipped_mask_bounds(Rect full_bounds, Rect draw_rect, int sample_padding) noexcept;
[[nodiscard]] Rect layer_pixel_bounds(const Layer& layer);
[[nodiscard]] std::optional<Rect> visible_alpha_local_bounds(const PixelBuffer& pixels);
[[nodiscard]] std::optional<Rect> layer_visible_alpha_bounds(const PixelBuffer& pixels, Rect bounds);
[[nodiscard]] std::optional<Rect> layer_visible_alpha_bounds(const Layer& layer, Rect bounds);
[[nodiscard]] int layer_style_effect_padding(const LayerStyle& style) noexcept;
[[nodiscard]] int layer_effect_padding(const Layer& layer) noexcept;
[[nodiscard]] int document_effect_padding(const Document& document) noexcept;
[[nodiscard]] Rect layer_bounds_with_effects(const Layer& layer, Rect bounds) noexcept;
[[nodiscard]] Rect layer_render_bounds(const Layer& layer) noexcept;
[[nodiscard]] bool layer_style_preview_is_expensive(const Layer& layer, Rect document_bounds) noexcept;
[[nodiscard]] float layer_mask_alpha_at(const Layer& layer, std::int32_t x, std::int32_t y);
[[nodiscard]] std::vector<float> layer_alpha_mask(const PixelBuffer& source, const Layer& layer, Rect bounds,
                                                  Rect mask_bounds, std::int32_t sample_offset_x = 0,
                                                  std::int32_t sample_offset_y = 0);
[[nodiscard]] std::vector<float> layer_alpha_mask(const Layer& layer, Rect bounds, Rect mask_bounds,
                                                  std::int32_t sample_offset_x = 0,
                                                  std::int32_t sample_offset_y = 0);

}  // namespace patchy
