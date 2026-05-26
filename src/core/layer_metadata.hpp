#pragma once

#include "core/layer.hpp"

namespace photoslop {

inline constexpr const char* kLayerMetadataLockTransparentPixels = "photoslop.lock_transparent_pixels";
inline constexpr const char* kLayerMetadataMaskLinked = "photoslop.mask_linked";
inline constexpr const char* kLayerMetadataGroupExpanded = "photoslop.layer_group_expanded";
inline constexpr const char* kLayerMetadataText = "photoslop.text";
inline constexpr const char* kLayerMetadataTextFont = "photoslop.text.font";
inline constexpr const char* kLayerMetadataTextSize = "photoslop.text.size";
inline constexpr const char* kLayerMetadataTextColor = "photoslop.text.color";
inline constexpr const char* kLayerMetadataTextBold = "photoslop.text.bold";
inline constexpr const char* kLayerMetadataTextItalic = "photoslop.text.italic";

[[nodiscard]] bool layer_locks_transparent_pixels(const Layer& layer);
void set_layer_locks_transparent_pixels(Layer& layer, bool locked);

[[nodiscard]] bool layer_mask_linked(const Layer& layer);
void set_layer_mask_linked(Layer& layer, bool linked);

[[nodiscard]] bool layer_group_expanded(const Layer& layer);
void set_layer_group_expanded(Layer& layer, bool expanded);

[[nodiscard]] bool layer_is_text(const Layer& layer);

}  // namespace photoslop
