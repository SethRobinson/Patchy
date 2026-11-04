#pragma once

#include "ui/layer_style_dialog.hpp"
#include "ui/shape_appearance_dialog.hpp"

namespace patchy::ui {

[[nodiscard]] ShapeAppearanceSettings shape_appearance_settings(const Layer& layer);
[[nodiscard]] LayerStyleSettings layer_style_settings(const Layer& layer);
[[nodiscard]] std::vector<AppearanceProperty<ShapeAppearanceSettings>> shape_appearance_properties();
[[nodiscard]] std::vector<AppearanceProperty<LayerStyleSettings>> layer_style_properties(const LayerStyleSettings& reference);
[[nodiscard]] bool shape_appearance_equal(const ShapeAppearanceSettings& a, const ShapeAppearanceSettings& b);
[[nodiscard]] bool layer_style_settings_equal(const LayerStyleSettings& a, const LayerStyleSettings& b);
[[nodiscard]] bool appearance_has_editable_radii(const ShapeAppearanceSettings& settings);
// A single live line (its weight and arrowheads are editable).
[[nodiscard]] bool appearance_has_editable_line(const ShapeAppearanceSettings& settings);
// Sets the arrowhead width and length from the weight when the line has a head.
void tie_line_arrowheads_to_weight(LiveShapeParams& params);
[[nodiscard]] bool shape_vector_appearance_equal(const VectorShapeContent& left, const VectorShapeContent& right);
[[nodiscard]] VectorShapeContent assemble_shape_appearance(const VectorShapeContent& original,
                                                          const ShapeAppearanceSettings& settings);
[[nodiscard]] ShapeAppearanceSettings apply_shape_appearance_edits(const ShapeAppearanceSettings& original,
                                                                  const ShapeAppearanceSettings& edited);
[[nodiscard]] LayerStyleSettings apply_layer_style_edits(const LayerStyleSettings& original,
                                                        const LayerStyleSettings& edited);

}  // namespace patchy::ui
