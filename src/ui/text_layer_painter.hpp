#pragma once

#include "core/layer.hpp"
#include "ui/text_outline.hpp"

#include <optional>
#include <string>
#include <vector>

class QPainter;

namespace patchy::ui {

// Draws a text layer's glyphs through `painter` in DOCUMENT coordinates as real text, the
// way the editable PDF export wants them: the same line plan the layer's raster was
// rendered from, drawn through the layer's canonical text transform (or, for an imported
// Photoshop preview, pinned to where that preview's raster sits), so the text lands where
// its pixels are and the PDF engine embeds the font instead of an image.
//
// A font the layer names that cannot draw its text is substituted with Qt's fallback face,
// what an edit of the layer would do, unless `missing_fonts_as_images` asks to refuse
// instead so the caller embeds the layer's pixels.
//
// Returns false when the layer cannot be redrawn faithfully (not a text layer, warped, a
// placeholder raster, a missing font under `missing_fonts_as_images`, or an imported
// preview that could not be placed); the caller then embeds the layer's pixels instead.
// `note` (optional) receives the refusal reason, or, after a successful draw in a substitute
// face, a note naming the missing font; it is cleared otherwise. The painter's state is left
// as it was.
//
// MainWindow adapts layer metadata to the shared text_render.hpp pipeline.
[[nodiscard]] bool draw_text_layer_to_painter(const Layer& layer, QPainter& painter, bool missing_fonts_as_images,
                                              std::string* note);

// The glyph outlines of a text layer in DOCUMENT coordinates, for Type > Convert to Shape and
// Type > Create Work Path (GitHub issue 598): the same plan draw_text_layer_to_painter draws,
// recorded as paths, so the outlines sit where the layer's pixels are. Warped text is laid out
// unwarped and its outlines are carried through the Warp Text surface the raster is resampled
// through. Strong anti-aliasing's extra raster weight is left out (it is not part of the type).
struct TextLayerOutlines {
  std::vector<TextOutlinePaint> paints;
  // Set when the layer's font is missing and a substitute face drew the outlines.
  std::string note;
  // Glyphs with no outline (colour emoji), left out.
  int skipped_glyph_images{0};
};
// nullopt with `error` set (when not null) for a layer that is not text, a placeholder raster,
// or text that could not be laid out.
[[nodiscard]] std::optional<TextLayerOutlines> text_layer_outlines(const Layer& layer, std::string* error);

}  // namespace patchy::ui
