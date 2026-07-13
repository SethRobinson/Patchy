#pragma once

// Helpers shared by the main_window_*.cpp translation units. MainWindow's
// implementation is split across several files (see AGENTS.md "MainWindow's
// implementation is split across main_window_*.cpp"); helpers used by more than one of
// those files are promoted out of the per-file anonymous namespaces into this
// header. Internal to the MainWindow implementation - do not include this from
// outside the main_window_*.cpp family.

#include "core/adjustment_layer.hpp"
#include "core/layer.hpp"
#include "core/pixel_buffer.hpp"
#include "core/rect_utils.hpp"

#include <QRect>
#include <QString>

namespace patchy {
class Document;
}

namespace patchy::ui {

class CanvasWidget;

// Recursive content checks shared by the split MainWindow translation units.
// These deliberately inspect groups as well as the selected root because a
// whole-document operation can otherwise rewrite a nested Smart Object's
// cached preview without updating its native source data.
[[nodiscard]] bool layer_tree_contains_smart_filters(const Layer& layer);
[[nodiscard]] bool layer_tree_contains_smart_object(const Layer& layer);
[[nodiscard]] bool document_contains_smart_objects(const Document& document);

// Localized display name for an adjustment layer kind ("Levels", "Curves", ...).
[[nodiscard]] QString localized_adjustment_display_name(AdjustmentKind kind);

// Rasterize the canvas selection into an 8-bit coverage mask covering
// selection_rect (document coordinates); 255 = fully selected.
[[nodiscard]] PixelBuffer selection_mask_pixels(const CanvasWidget& canvas, QRect selection_rect);

// Drop the verbatim PSD effect blocks ('lfx2'/'lrFX'/'plFX') a layer carried in
// from import. Must be called whenever code replaces layer_style(), or the next
// PSD save would resurrect the imported effects over the new ones.
void clear_layer_psd_style_source(Layer& layer);

// Text sizes are shown in points but rendered in document pixels through the
// document's print PPI (default 300 when unset).
[[nodiscard]] double text_size_ppi(const Document& document) noexcept;
[[nodiscard]] double text_pixels_to_points(int pixels, const Document& document) noexcept;
[[nodiscard]] int text_points_to_pixels(double points, const Document& document) noexcept;

// Replace a layer's pixels, keeping the old bounds origin.
void set_layer_pixels_preserving_origin(Layer& layer, PixelBuffer pixels, Rect original_bounds);

// Like set_layer_pixels_preserving_origin, but takes the new origin from
// new_bounds instead of preserving the old one. Used when a filter grows the
// layer (e.g. a blur bleeding into transparency) and the origin must shift.
void set_layer_pixels_with_bounds(Layer& layer, PixelBuffer pixels, Rect new_bounds);

}  // namespace patchy::ui
