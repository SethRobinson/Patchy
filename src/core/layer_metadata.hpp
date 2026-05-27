#pragma once

#include "core/layer.hpp"

namespace patchy {

inline constexpr const char* kLayerMetadataLockTransparentPixels = "patchy.lock_transparent_pixels";
inline constexpr const char* kLayerMetadataMaskLinked = "patchy.mask_linked";
inline constexpr const char* kLayerMetadataGroupExpanded = "patchy.layer_group_expanded";
inline constexpr const char* kLayerMetadataText = "patchy.text";
inline constexpr const char* kLayerMetadataTextFont = "patchy.text.font";
inline constexpr const char* kLayerMetadataTextSize = "patchy.text.size";
inline constexpr const char* kLayerMetadataTextColor = "patchy.text.color";
inline constexpr const char* kLayerMetadataTextBold = "patchy.text.bold";
inline constexpr const char* kLayerMetadataTextItalic = "patchy.text.italic";
inline constexpr const char* kLayerMetadataTextSourceBlock = "patchy.text.source_block";
inline constexpr const char* kLayerMetadataTextRasterStatus = "patchy.text.raster_status";

[[nodiscard]] bool layer_locks_transparent_pixels(const Layer& layer);
void set_layer_locks_transparent_pixels(Layer& layer, bool locked);

[[nodiscard]] bool layer_mask_linked(const Layer& layer);
void set_layer_mask_linked(Layer& layer, bool linked);

[[nodiscard]] bool layer_group_expanded(const Layer& layer);
void set_layer_group_expanded(Layer& layer, bool expanded);

[[nodiscard]] bool layer_is_text(const Layer& layer);

}  // namespace patchy
