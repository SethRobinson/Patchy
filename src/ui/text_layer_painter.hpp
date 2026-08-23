#pragma once

#include "core/layer.hpp"

class QPainter;

namespace patchy::ui {

// Draws a text layer's glyphs through `painter` in DOCUMENT coordinates as real text, the
// way the editable PDF export wants them: the same line plan the layer's raster was
// rendered from, drawn through the layer's canonical text transform, so the text lands
// exactly where its pixels are and the PDF engine embeds the font instead of an image.
// Returns false when the layer cannot be redrawn faithfully (not a text layer, warped, or
// carrying a raster Patchy's own renderer did not produce); the caller then embeds the
// layer's pixels instead. The painter's state is left as it was.
//
// Implemented in main_window.cpp next to the text render pipeline it shares.
[[nodiscard]] bool draw_text_layer_to_painter(const Layer& layer, QPainter& painter);

}  // namespace patchy::ui
