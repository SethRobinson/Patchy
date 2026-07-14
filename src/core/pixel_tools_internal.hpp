#pragma once

#include "core/pixel_tools.hpp"

#include <cstdint>

namespace patchy {

// Helpers shared between the painting half (pixel_tools.cpp, where they are defined) and the
// geometry half (document_geometry.cpp). Internal to src/core: never include this header
// anywhere else. The extension_color default arguments of canvas_resized_format_for_layer and
// fill_resized_layer_background live at the definitions in pixel_tools.cpp, so only that file
// may call them with defaults; other callers pass every argument explicitly.

[[nodiscard]] Rect canvas_rect(const Document& document) noexcept;
[[nodiscard]] Rect normalized_rect(Rect rect) noexcept;
[[nodiscard]] Layer* editable_layer(Document& document, LayerId layer_id) noexcept;
[[nodiscard]] const Layer* editable_layer(const Document& document, LayerId layer_id) noexcept;
[[nodiscard]] PixelFormat canvas_resized_format_for_layer(const Layer& layer, const PixelBuffer& source,
                                                          EditColor extension_color) noexcept;
void fill_resized_layer_background(PixelBuffer& pixels, const Layer& layer, EditColor extension_color);
void copy_resized_layer_pixel(const PixelBuffer& source, PixelBuffer& destination, std::int32_t sx, std::int32_t sy,
                              std::int32_t dx, std::int32_t dy);

}  // namespace patchy
