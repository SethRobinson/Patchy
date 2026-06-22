#pragma once

#include "core/document.hpp"
#include "core/layer.hpp"
#include "core/pixel_buffer.hpp"
#include "core/rect_utils.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace patchy {

// When the document is a single flat pixel layer carrying an enabled grayscale mask,
// returns an RGBA8 buffer whose colors are the layer's ORIGINAL (unmasked) pixels and
// whose alpha channel is the mask. Saving this to an alpha-capable format preserves the
// mask non-destructively (the colors beneath the mask are kept, matching how Photoshop
// shows an opaque Background plus a separate "Alpha 1" channel). Returns nullopt when the
// document is not a single masked pixel layer.
[[nodiscard]] std::optional<PixelBuffer> document_alpha_rgba8(const Document& document);

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
[[nodiscard]] float layer_mask_alpha_at(const Layer& layer, std::int32_t x, std::int32_t y, Rect mask_bounds);
[[nodiscard]] std::vector<float> layer_alpha_mask(const PixelBuffer& source, const Layer& layer, Rect bounds,
                                                  Rect mask_bounds, std::int32_t sample_offset_x = 0,
                                                  std::int32_t sample_offset_y = 0,
                                                  std::optional<Rect> layer_mask_bounds = std::nullopt);
[[nodiscard]] std::vector<float> layer_alpha_mask(const Layer& layer, Rect bounds, Rect mask_bounds,
                                                  std::int32_t sample_offset_x = 0, std::int32_t sample_offset_y = 0,
                                                  std::optional<Rect> layer_mask_bounds = std::nullopt);

}  // namespace patchy
