#pragma once

#include "core/layer.hpp"

#include <string>

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
// Implemented in main_window.cpp next to the text render pipeline it shares.
[[nodiscard]] bool draw_text_layer_to_painter(const Layer& layer, QPainter& painter, bool missing_fonts_as_images,
                                              std::string* note);

}  // namespace patchy::ui
