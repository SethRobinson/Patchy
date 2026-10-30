#pragma once

#include "core/document.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace patchy::svg {

// One thing DocumentIo::write would bake into an embedded image instead of
// keeping as vectors. Append-only: the UI maps each kind to translated text.
enum class BakedContentKind : std::uint8_t {
  TextLayer,        // rasterized on its own
  PixelLayer,       // rasterized on its own
  SmartObjectLayer, // rasterized on its own
  AdjustmentLayer,  // a barrier: merged with everything below it
  BlendMode,        // a blend mode CSS cannot express: a barrier too
  ShapeLayer,       // a shape layer with features SVG cannot express (styles,
                    // fill opacity, intersect/xor combines, angle gradients, ...)
  Group,            // a folder with a style, fill opacity, or a mask the writer
                    // cannot carry, rasterized as a whole
  ClippingGroup,    // a clipping base plus its clipped layers, rasterized as one
  RasterMask,       // a layer mask written as a luminance <mask> image
  MergedBelow       // an otherwise exportable layer merged into a barrier's chunk
};

struct BakedContent {
  BakedContentKind kind{BakedContentKind::PixelLayer};
  std::string layer_name;
};

class DocumentIo {
public:
  // Imports .svg/.svgz as editable shape layers (groups -> folders,
  // rect/circle/ellipse/line -> live shapes, gradients/simple patterns ->
  // vector fills, clip paths -> vector masks, basic text -> text layers with
  // a post-open render marker). Throws for files past the supported subset;
  // the UI's QImageReader fallback then rasterizes them.
  [[nodiscard]] static Document read(std::span<const std::uint8_t> bytes,
                                     std::vector<std::string>* notices = nullptr);
  // Exports vector shape layers as real SVG vectors; everything SVG cannot
  // composite per-element (adjustment spans, clipping runs, unmapped blend
  // modes, effects) flattens into embedded PNG <image> chunks, reported via
  // notices.
  [[nodiscard]] static std::vector<std::uint8_t> write(const Document& document,
                                                       std::vector<std::string>* notices = nullptr);
  static void write_file(const Document& document, const std::filesystem::path& path,
                         std::vector<std::string>* notices = nullptr);
  // Dry run of write(): the same walk and representability rules, but nothing
  // is composited or encoded. Returns what write() would bake into embedded
  // images, in emission order; empty means the file holds nothing but vectors
  // (shape layers, folders, clipPath vector masks, gradient and pattern paint
  // servers), so a save loses nothing and needs no flatten warning.
  [[nodiscard]] static std::vector<BakedContent> baked_content(const Document& document);
};

// The importer's path-data grammar on its own (M/L/H/V/C/S/Q/T/A/Z, relative forms, implicit
// repeats; quadratics and arcs become cubics): each subpath in its own shape_group, open unless
// closed by Z. The bundled custom shapes (GitHub #521) store their outlines this way.
[[nodiscard]] VectorPath parse_svg_path_data(std::string_view data);

// Includes the leading dot, matching the format-registry convention. SVGZ is
// read-only; the UI advertises only .svg when saving.
[[nodiscard]] const std::vector<std::string>& svg_extensions();
[[nodiscard]] bool sniff(std::span<const std::uint8_t> bytes) noexcept;

}  // namespace patchy::svg
