#pragma once

#include "core/layer.hpp"

#include <span>
#include <string_view>

namespace patchy {

// Built-in layer-effect contour shapes, conceptually matching Photoshop's stock
// roster with original point values (no Adobe assets). The contour's persisted
// PSD name is the english_name; ids are stable for UI selection state.
struct ContourPreset {
  const char* id;
  const char* english_name;
  StyleContour contour;
};

[[nodiscard]] std::span<const ContourPreset> builtin_contour_presets();
[[nodiscard]] const ContourPreset* find_builtin_contour_preset(std::string_view id);
// Matches by curve shape (point coordinates and corner flags; names ignored,
// and any 2-point identity matches Linear). nullptr = custom curve.
[[nodiscard]] const ContourPreset* find_builtin_contour_preset(const StyleContour& contour);

}  // namespace patchy
