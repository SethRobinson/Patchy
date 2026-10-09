#pragma once

// Document bit depth (docs/high-bit-depth.md). A document has one depth,
// color_state().bit_depth. Its authoritative pixels follow it: pixel layers, layer
// masks, smart filter masks and saved channels. Derived caches (text and shape
// rasters, smart object previews, vector mask coverage, effect mattes) may be any
// depth; readers convert them through core/pixel_depth.hpp, so an 8-bit-only
// generator never corrupts a deep document.

#include "core/document.hpp"

#include <string>
#include <vector>

namespace patchy {

[[nodiscard]] BitDepth document_bit_depth(const Document& document) noexcept;

// Photoshop's 32-bit documents take only some layer blend modes: Photoshop 2026 refuses
// to open a 32-bit file with a layer in Color Burn, Linear Burn, Screen, Color Dodge,
// Overlay, Soft, Hard, Vivid, Linear or Pin Light, Hard Mix or Exclusion, and its own
// conversion to 32 bits sets those layers to Normal. Every mode works at 8 and 16 bits.
[[nodiscard]] bool blend_mode_supported_at_depth(BlendMode mode, BitDepth depth) noexcept;

// Converts every pixel buffer in the document (caches included) to `depth` and makes
// it the document's depth; to 32 bits, layers in unsupported blend modes become Normal
// (blend_mode_supported_at_depth), as in Photoshop. 8 <-> 16 is exact in both directions for 8-bit content;
// to 32 bits decodes sRGB into linear light; from 32 bits clamps to 0..1 and encodes
// (Photoshop's Exposure and Gamma method at exposure 0, gamma 1, which is what the
// PSD reader has always used). Layer revisions bump, so render caches refresh.
void convert_document_depth(Document& document, BitDepth depth);

// HDR toning for a 32-bit document about to convert to 16 or 8 bits, Photoshop's
// Exposure and Gamma method: every color sample of every 32-bit layer tree becomes
// (v * 2^exposure)^(1 / gamma) in linear light (negatives clamp to 0). Masks, channels
// and alpha are untouched. Exposure 0 and gamma 1 change nothing; other documents are
// left alone.
void tone_map_linear_document(Document& document, double exposure_stops, double gamma);

// One layer tree's buffers (pixels, masks, smart filter masks) converted to `depth` by
// the same rules: for a layer entering a document of another depth.
void convert_layer_depth(Layer& layer, BitDepth depth);

// Buffers that break the invariant above, one plain description each ("layer
// 'Name' is 8-bit"); empty when the document is consistent. For tests and debug
// checks.
[[nodiscard]] std::vector<std::string> document_depth_problems(const Document& document);

}  // namespace patchy
