#pragma once

#include "core/document.hpp"

#include <cstdint>
#include <span>

namespace patchy {

// Flattened RGBA8 composite of the whole document canvas, shared by the flat-image format
// writers (ICO, TGA, GIF, PCX, ILBM). A single masked layer whose mask is marked as the
// document alpha exports non-destructively: the original colors stay intact and the mask
// becomes the alpha channel, matching the BMP/PNG export semantics.
[[nodiscard]] PixelBuffer flatten_document_rgba8(const Document& document);
// The same composite limited to `rect` (clipped to the canvas): a rect-sized buffer whose
// (0, 0) is rect's top-left; empty when nothing is left. The compositor's clip keeps effects
// that reach into the rect, so it matches the full flatten's pixels there. Skips the
// native-CMYK and single-masked-layer shortcuts unless rect is the whole canvas.
[[nodiscard]] PixelBuffer flatten_document_rgba8_rect(const Document& document, Rect rect);

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

// PixelBuffer-level bodies of the two functions above, for callers that already hold an
// RGBA8 image (the animated GIF writer indexes each frame separately). Identical mapping
// and quantization semantics; the Document-level functions delegate here.
[[nodiscard]] IndexedFlattenResult indexed_rgba8_with_palette(const PixelBuffer& rgba,
                                                              std::span<const RgbColor> colors,
                                                              std::uint8_t alpha_threshold);
[[nodiscard]] IndexedFlattenResult indexed_rgba8_quantized(const PixelBuffer& rgba, std::size_t max_colors,
                                                           std::uint8_t alpha_threshold);

// Quantization choices for the GIF and palette PNG writers (File > Export for Web,
// docs/web-export.md): the color count (2..256, one slot of it reserved for transparency
// when needed) and Floyd-Steinberg error diffusion scaled by `diffusion_amount` (0..100,
// apply_palette_to_pixels). The defaults are exactly indexed_rgba8_quantized(rgba, 256, t).
struct QuantizeOptions {
  std::size_t max_colors{256};
  bool diffuse{false};
  int diffusion_amount{100};
};
[[nodiscard]] IndexedFlattenResult indexed_rgba8_quantized(const PixelBuffer& rgba, std::uint8_t alpha_threshold,
                                                           const QuantizeOptions& options);
[[nodiscard]] IndexedFlattenResult indexed_flatten_quantized(const Document& document, std::uint8_t alpha_threshold,
                                                             const QuantizeOptions& options);

}  // namespace patchy
