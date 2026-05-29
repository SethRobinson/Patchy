#pragma once

#include "core/layer.hpp"

namespace patchy {

inline constexpr const char* kLayerMetadataLockTransparentPixels = "patchy.lock_transparent_pixels";
inline constexpr const char* kLayerMetadataMaskLinked = "patchy.mask_linked";
inline constexpr const char* kLayerMetadataGroupExpanded = "patchy.layer_group_expanded";
inline constexpr const char* kLayerMetadataText = "patchy.text";
inline constexpr const char* kLayerMetadataTextHtml = "patchy.text.html";
inline constexpr const char* kLayerMetadataTextRuns = "patchy.text.runs";
inline constexpr const char* kLayerMetadataTextParagraphRuns = "patchy.text.paragraph_runs";
inline constexpr const char* kLayerMetadataTextFlow = "patchy.text.flow";
inline constexpr const char* kLayerMetadataTextBoxWidth = "patchy.text.box_width";
inline constexpr const char* kLayerMetadataTextBoxHeight = "patchy.text.box_height";
inline constexpr const char* kLayerMetadataTextFont = "patchy.text.font";
inline constexpr const char* kLayerMetadataTextSize = "patchy.text.size";
inline constexpr const char* kLayerMetadataTextColor = "patchy.text.color";
inline constexpr const char* kLayerMetadataTextBold = "patchy.text.bold";
inline constexpr const char* kLayerMetadataTextItalic = "patchy.text.italic";
inline constexpr const char* kLayerMetadataTextAntiAlias = "patchy.text.anti_alias";
inline constexpr const char* kLayerMetadataTextSourceBlock = "patchy.text.source_block";
inline constexpr const char* kLayerMetadataTextRasterStatus = "patchy.text.raster_status";
inline constexpr const char* kLayerMetadataPsdTextTransform = "patchy.psd.text.transform";
inline constexpr const char* kLayerMetadataPsdTextBounds = "patchy.psd.text.bounds";
inline constexpr const char* kLayerMetadataPsdTextBoundingBox = "patchy.psd.text.bounding_box";
inline constexpr const char* kLayerMetadataPsdTextBoxBounds = "patchy.psd.text.box_bounds";
inline constexpr const char* kLayerMetadataPsdTextTailBounds = "patchy.psd.text.tail_bounds";
inline constexpr const char* kLayerMetadataPsdTextIndex = "patchy.psd.text.index";

[[nodiscard]] bool layer_locks_transparent_pixels(const Layer& layer);
void set_layer_locks_transparent_pixels(Layer& layer, bool locked);

[[nodiscard]] bool layer_mask_linked(const Layer& layer);
void set_layer_mask_linked(Layer& layer, bool linked);

[[nodiscard]] bool layer_group_expanded(const Layer& layer);
void set_layer_group_expanded(Layer& layer, bool expanded);

[[nodiscard]] bool layer_is_text(const Layer& layer);

}  // namespace patchy
