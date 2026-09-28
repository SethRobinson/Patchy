#pragma once

#include "core/layer.hpp"

#include <QRect>
#include <QRegion>

namespace patchy::ui {

[[nodiscard]] QRect to_qrect(Rect rect);
[[nodiscard]] Rect to_core_rect(QRect rect);

// Photoshop's ranges: selection Feather 0..1000 px, Select > Modify > Expand /
// Contract 1..500 px.
inline constexpr int kMaxSelectionFeatherRadius = 1000;
inline constexpr int kMaxSelectionModifyRadius = 500;

// Dilates the region by a square structuring element of the given radius (Expand Selection),
// up to kMaxSelectionModifyRadius.
[[nodiscard]] QRegion expanded_region(const QRegion& region, int pixels, QRect bounds);

// Where Stroke Selection lays its band relative to the selection edge (Photoshop's Location).
// The token values are the persisted settings/script identifiers and never change.
enum class SelectionStrokeLocation { Inside, Center, Outside };
[[nodiscard]] const char* selection_stroke_location_token(SelectionStrokeLocation location);
[[nodiscard]] SelectionStrokeLocation selection_stroke_location_from_token(const QString& token,
                                                                          SelectionStrokeLocation fallback);

// The pixels a Stroke Selection of `width` pixels paints. Inside stays within the selection,
// Outside stays outside it, and Center splits the width (the larger half inside when odd).
[[nodiscard]] QRegion selection_stroke_region(const QRegion& selection, int width,
                                              SelectionStrokeLocation location, QRect bounds);

}  // namespace patchy::ui
