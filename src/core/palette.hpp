#pragma once

#include "core/layer.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <unordered_set>
#include <vector>

namespace patchy {

class Document;

// A palettized-editing color table: 1..256 opaque RGB entries. Pixels stay RGBA
// document-wide; palette mode only constrains what tools may WRITE, so palette
// indices never appear in pixel storage and reordering entries needs no pixel
// change. See docs/palette-mode.md.
struct Palette {
  std::vector<RgbColor> colors;
};

[[nodiscard]] std::uint32_t palette_color_key(RgbColor color) noexcept;
[[nodiscard]] RgbColor palette_color_from_key(std::uint32_t key) noexcept;

// Nearest entry by squared RGB distance; the lowest index wins ties so results are
// deterministic. Returns 0 for an empty palette.
[[nodiscard]] std::uint16_t nearest_palette_index(RgbColor color, std::span<const RgbColor> palette) noexcept;

// 15-bit RGB -> nearest-entry table (32768 entries) for hot pixel loops, plus an
// exact-membership set. snap() keeps exact palette colors unchanged (idempotent
// even when two entries share one 5-5-5 bucket, e.g. near-duplicate dark entries);
// any other color lands on the entry nearest its bucket center, i.e. the true
// nearest entry within a +-4 per-channel quantization bound.
class PaletteLut {
public:
  PaletteLut() = default;

  // Rebuilds the table; entries beyond 256 colors are ignored.
  void build(std::span<const RgbColor> colors);

  [[nodiscard]] bool empty() const noexcept { return colors_.empty(); }
  [[nodiscard]] const std::vector<RgbColor>& colors() const noexcept { return colors_; }
  [[nodiscard]] bool contains(RgbColor color) const noexcept;
  [[nodiscard]] std::uint16_t index_for(std::uint8_t r, std::uint8_t g, std::uint8_t b) const noexcept;
  [[nodiscard]] RgbColor snap(std::uint8_t r, std::uint8_t g, std::uint8_t b) const noexcept;

private:
  std::vector<RgbColor> colors_;
  std::vector<std::uint8_t> table_;
  std::unordered_set<std::uint32_t> exact_;
};

// Non-owning write constraint carried by EditOptions; null lut = unconstrained.
struct PaletteSnapContext {
  const PaletteLut* lut{nullptr};
  std::uint8_t alpha_threshold{128};  // alpha >= threshold -> 255, else 0
  float coverage_threshold{0.5F};     // tool coverage below this writes nothing
};

// Snaps one interleaved 8-bit pixel in place: RGB to the palette and, for buffers
// with an alpha channel, alpha to 0/255 (RGB is snapped regardless of the alpha
// outcome). Buffers with fewer than 3 channels are left untouched.
void snap_pixel_to_palette(std::uint8_t* px, std::uint16_t channels, const PaletteSnapContext& context) noexcept;

// Exact-match recolor (alpha preserved). Returns the touched bounds in buffer
// coordinates, empty when nothing matched or from == to.
[[nodiscard]] Rect remap_exact_color(PixelBuffer& pixels, RgbColor from, RgbColor to);

// Recolors every ordinary pixel layer, recursing into groups; masks,
// Smart Object preview caches, and non-pixel layers are untouched. Lossless for
// palette-clean documents because enforced pixels are exact palette colors.
void remap_document_exact_color(Document& document, RgbColor from, RgbColor to);

struct PaletteColorCount {
  RgbColor color{};
  std::uint64_t count{0};
};

// Unique RGB colors with populations, sorted by color key. Buffers with an alpha
// channel only count pixels whose alpha >= alpha_threshold; threshold 0 counts
// every pixel.
[[nodiscard]] std::vector<PaletteColorCount> collect_color_counts(const PixelBuffer& pixels,
                                                                  std::uint8_t alpha_threshold = 0);

// Deterministic median cut: widest-range box splits first, splits are population
// weighted, and every comparison tie-breaks on the color key. Result size is
// <= target_size, sorted by color key.
[[nodiscard]] std::vector<RgbColor> median_cut_palette(const std::vector<PaletteColorCount>& colors,
                                                       std::size_t target_size);

// The unique visible colors when they fit within cap, otherwise nullopt.
[[nodiscard]] std::optional<Palette> exact_palette_from_pixels(const PixelBuffer& pixels, std::size_t cap,
                                                               std::uint8_t alpha_threshold = 1);

// exact_palette_from_pixels when the colors fit, else median cut to max_colors.
[[nodiscard]] Palette quantize_to_palette(const PixelBuffer& pixels, std::size_t max_colors,
                                          std::uint8_t alpha_threshold = 1);

enum class PaletteDither {
  None,
  FloydSteinberg,
  OrderedBayer4x4,
  OrderedBayer8x8
};

// Rewrites pixels to palette colors (convert/snap-time only; live painting never
// dithers). Alpha is thresholded to 0/255 first; fully transparent pixels keep
// their RGB and neither receive nor diffuse dither error. Floyd-Steinberg scans
// serpentine rows with integer error math so output is toolchain-deterministic.
// Returns the touched bounds in buffer coordinates.
Rect apply_palette_to_pixels(PixelBuffer& pixels, const PaletteLut& lut, PaletteDither dither,
                             std::uint8_t alpha_threshold);

// True when every pixel is palette-clean: alpha (if present) is exactly 0 or 255
// and every visible pixel's RGB is an exact palette color.
[[nodiscard]] bool pixels_are_palette_clean(const PixelBuffer& pixels, const PaletteLut& lut) noexcept;

// Mirrors Document::palette_editing() into Document::indexed_palette() (colors +
// a 2/4/8 source bit depth by size) so the existing indexed BMP "exact colors"
// export path sees palette-mode documents unchanged. No-op when palette mode is
// off; import metadata is preserved in that case.
void sync_document_indexed_palette(Document& document);

}  // namespace patchy
