#pragma once

#include "core/layer.hpp"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace patchy {

inline constexpr const char* kLayerMetadataMaskLinked = "patchy.mask_linked";
// Marks a layer mask that originated as a document-level alpha channel (a flat image's
// per-pixel alpha, or a PSD "Alpha 1" saved channel) rather than a hand-authored layer
// mask. Such masks are written back as the file's alpha / a PSD "Alpha 1" channel on save.
inline constexpr const char* kLayerMetadataDocumentAlpha = "patchy.document_alpha";
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
// "photoshop" on text layers imported from a Photoshop-authored TySh block: the renderer lays
// lines out with Photoshop's leading model (per-line max leading, auto = paragraph fraction x
// size, box first baseline at the typographic ascender) instead of Qt's natural line spacing.
// Patchy-authored text (including Patchy-written PSDs reopened later) never carries this, so
// native layout stays byte-stable.
inline constexpr const char* kLayerMetadataTextLayoutMode = "patchy.text.layout";
inline constexpr const char* kTextLayoutModePhotoshop = "photoshop";
inline constexpr const char* kLayerMetadataTextTransform = "patchy.text.transform";
inline constexpr const char* kLayerMetadataPsdTextTransform = "patchy.psd.text.transform";
inline constexpr const char* kLayerMetadataPsdTextBounds = "patchy.psd.text.bounds";
inline constexpr const char* kLayerMetadataPsdTextBoundingBox = "patchy.psd.text.bounding_box";
inline constexpr const char* kLayerMetadataPsdTextBoxBounds = "patchy.psd.text.box_bounds";
inline constexpr const char* kLayerMetadataPsdTextTailBounds = "patchy.psd.text.tail_bounds";
inline constexpr const char* kLayerMetadataPsdTextIndex = "patchy.psd.text.index";

using LayerAffineTransform = std::array<double, 6>;

[[nodiscard]] bool layer_locks_transparent_pixels(const Layer& layer);
void set_layer_locks_transparent_pixels(Layer& layer, bool locked);

[[nodiscard]] bool layer_locks_image_pixels(const Layer& layer);
void set_layer_locks_image_pixels(Layer& layer, bool locked);

[[nodiscard]] bool layer_locks_position(const Layer& layer);
void set_layer_locks_position(Layer& layer, bool locked);

[[nodiscard]] bool layer_locks_all(const Layer& layer);
void set_layer_locks_all(Layer& layer, bool locked);

[[nodiscard]] LayerLockFlags layer_lock_flags(const Layer& layer);
void set_layer_lock_flags(Layer& layer, LayerLockFlags flags);
void set_layer_lock_flag(Layer& layer, LayerLockFlags flag, bool locked);
[[nodiscard]] LayerLockFlags layer_effective_lock_flags(const std::vector<Layer>& layers, LayerId layer_id);
[[nodiscard]] LayerLockFlags layer_ancestor_lock_flags(const std::vector<Layer>& layers, LayerId layer_id);
[[nodiscard]] bool layer_effectively_locks_transparent_pixels(const std::vector<Layer>& layers, LayerId layer_id);
[[nodiscard]] bool layer_effectively_locks_image_pixels(const std::vector<Layer>& layers, LayerId layer_id);
[[nodiscard]] bool layer_effectively_locks_position(const std::vector<Layer>& layers, LayerId layer_id);

[[nodiscard]] bool layer_is_locked(const Layer& layer);
void set_layer_locked(Layer& layer, bool locked);
[[nodiscard]] bool layer_is_effectively_locked(const std::vector<Layer>& layers, LayerId layer_id);
[[nodiscard]] bool layer_has_locked_ancestor(const std::vector<Layer>& layers, LayerId layer_id);

[[nodiscard]] bool layer_mask_linked(const Layer& layer);
void set_layer_mask_linked(Layer& layer, bool linked);

[[nodiscard]] bool layer_mask_is_document_alpha(const Layer& layer);
void set_layer_mask_is_document_alpha(Layer& layer, bool document_alpha);

[[nodiscard]] bool layer_group_expanded(const Layer& layer);
void set_layer_group_expanded(Layer& layer, bool expanded);

[[nodiscard]] bool layer_is_text(const Layer& layer);

[[nodiscard]] std::optional<LayerAffineTransform> parse_layer_affine_transform(std::string_view text);
[[nodiscard]] std::string serialize_layer_affine_transform(const LayerAffineTransform& transform);
[[nodiscard]] LayerAffineTransform compose_layer_affine_transform(const LayerAffineTransform& outer,
                                                                  const LayerAffineTransform& inner);
void translate_moved_layer_metadata(Layer& layer, std::int32_t dx, std::int32_t dy, std::int32_t document_width,
                                    std::int32_t document_height);

}  // namespace patchy
