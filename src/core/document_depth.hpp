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

// Converts every pixel buffer in the document (caches included) to `depth` and makes
// it the document's depth. 8 <-> 16 is exact in both directions for 8-bit content;
// to 32 bits decodes sRGB into linear light; from 32 bits clamps to 0..1 and encodes
// (Photoshop's Exposure and Gamma method at exposure 0, gamma 1, which is what the
// PSD reader has always used). Layer revisions bump, so render caches refresh.
void convert_document_depth(Document& document, BitDepth depth);

// Buffers that break the invariant above, one plain description each ("layer
// 'Name' is 8-bit"); empty when the document is consistent. For tests and debug
// checks.
[[nodiscard]] std::vector<std::string> document_depth_problems(const Document& document);

}  // namespace patchy
