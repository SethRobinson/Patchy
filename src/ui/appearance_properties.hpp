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
// A single live polygon (its sides and star inset are editable; GitHub #643).
[[nodiscard]] bool appearance_has_editable_polygon(const ShapeAppearanceSettings& settings);
// Maps `path` from its own bounds onto `box` (each axis scaled on its own; an axis with no
// extent only moves). Geometry's box edit and the Shape picker's fit (GitHub #642, #644).
void fit_vector_path_to_box(VectorPath& path, const ShapePathBox& box);
// True when `outline` is the library shape `shape` fitted to the outline's own bounds (the
// Custom Shape tool's stamp, moved or resized), within a small tolerance (GitHub #644).
[[nodiscard]] bool vector_path_is_fitted_shape(const VectorPath& outline, const VectorPath& shape);
// Arrowhead width and length are stored in pixels and shown as a percent of the line
// weight (GitHub #622 follow-up). Photoshop's default head is 500% wide and 1000% long;
// its Line tool takes 10% to 1000% for width, 10% to 5000% for length, -50% to 50% concavity.
inline constexpr double kLineArrowDefaultWidthPercent = 500.0;
inline constexpr double kLineArrowDefaultLengthPercent = 1000.0;
// A line with a head switched on but no head size gets the default proportions.
void give_line_arrowheads_default_size(LiveShapeParams& params);
// After a weight change from `previous_weight`: the heads scale with the weight, so they
// keep the line's own proportions; a line with a head but no head size gets the defaults.
void tie_line_arrowheads_to_weight(LiveShapeParams& params, double previous_weight);
// The head width and length as a percent of the weight (the default when the line has none).
[[nodiscard]] double line_arrow_width_percent(const LiveShapeParams& params);
[[nodiscard]] double line_arrow_length_percent(const LiveShapeParams& params);
[[nodiscard]] bool shape_vector_appearance_equal(const VectorShapeContent& left, const VectorShapeContent& right);
[[nodiscard]] VectorShapeContent assemble_shape_appearance(const VectorShapeContent& original,
                                                          const ShapeAppearanceSettings& settings);
[[nodiscard]] ShapeAppearanceSettings apply_shape_appearance_edits(const ShapeAppearanceSettings& original,
                                                                  const ShapeAppearanceSettings& edited);
[[nodiscard]] LayerStyleSettings apply_layer_style_edits(const LayerStyleSettings& original,
                                                        const LayerStyleSettings& edited);

}  // namespace patchy::ui
