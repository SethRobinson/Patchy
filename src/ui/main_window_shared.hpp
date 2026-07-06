#pragma once

// Helpers shared by the main_window_*.cpp translation units. MainWindow's
// implementation is split across several files (see AGENTS.md "main_window.cpp
// is split into thematic translation units"); helpers used by more than one of
// those files are promoted out of the per-file anonymous namespaces into this
// header. Internal to the MainWindow implementation - do not include this from
// outside the main_window_*.cpp family.

#include "core/adjustment_layer.hpp"
#include "core/pixel_buffer.hpp"

#include <QRect>
#include <QString>

namespace patchy::ui {

class CanvasWidget;

// Localized display name for an adjustment layer kind ("Levels", "Curves", ...).
[[nodiscard]] QString localized_adjustment_display_name(AdjustmentKind kind);

// Rasterize the canvas selection into an 8-bit coverage mask covering
// selection_rect (document coordinates); 255 = fully selected.
[[nodiscard]] PixelBuffer selection_mask_pixels(const CanvasWidget& canvas, QRect selection_rect);

}  // namespace patchy::ui
