#pragma once

#include "core/document.hpp"

namespace patchy {

// Flattened RGBA8 composite of the whole document canvas, shared by the flat-image format
// writers (ICO, TGA, GIF, PCX, ILBM). A single masked layer whose mask is marked as the
// document alpha exports non-destructively: the original colors stay intact and the mask
// becomes the alpha channel, matching the BMP/PNG export semantics.
[[nodiscard]] PixelBuffer flatten_document_rgba8(const Document& document);

struct IndexedFlattenResult {
  std::int32_t width{0};
  std::int32_t height{0};
  std::vector<RgbColor> palette;  // document palette in file order (+ appended transparent slot)
  int transparent_index{-1};      // -1 = no transparency
  std::vector<std::uint8_t> indexes;  // row-major, one byte per pixel
};

// Flattens a palette-mode document to 8-bit indexes with the document palette in file
// order: exact-membership match first, then the 15-bit LUT, plus one appended transparent
// slot when the flatten has pixels below the editing alpha threshold and the table has
// room. Same semantics as the indexed PNG-8 export; shared by the TGA/GIF/PCX/ILBM
// writers. Throws when the document is not in palette mode.
[[nodiscard]] IndexedFlattenResult indexed_flatten_for_palette_mode(const Document& document);

// Non-palette-mode variant: quantizes the flatten (exact colors when they fit, else
// deterministic median cut) to at most max_colors, reserving one transparent slot when the
// flatten has pixels below alpha_threshold. Shared by the GIF and ILBM writers.
[[nodiscard]] IndexedFlattenResult indexed_flatten_quantized(const Document& document, std::size_t max_colors,
                                                             std::uint8_t alpha_threshold);

}  // namespace patchy
