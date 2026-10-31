#pragma once

#include "core/vector_shape.hpp"
#include "ui/unit_spin_box.hpp"
#include "ui/appearance_edits.hpp"

#include <QString>

#include <functional>
#include <optional>
#include <vector>

class QWidget;

namespace patchy {
struct PatternStore;
}

namespace patchy::ui {

class GradientLibrary;
class PatternLibrary;

// The editable appearance of a shape/fill layer. `geometry` carries the
// layer's single live-shape origination when the caller allows editing it
// (rect/rounded-rect bounds and radii, ellipse bounds, line endpoints and
// weight); the dialog edits the params in place and the caller regenerates
// the subpaths. Dialog-based geometry editing is the patent-cleared route -
// on-canvas live-shape gizmos stay excluded (docs/vector-tools.md).
struct ShapeAppearanceSettings {
  VectorFill fill;
  VectorStroke stroke;
  std::optional<LiveShapeParams> geometry;
  // Layer transparency (the Layers panel values) and the shape's vector-mask
  // Feather / Density (VectorShapeContent::feather/density), all PSD-native.
  float layer_opacity{1.0F};
  float fill_opacity{1.0F};
  double feather{0.0};
  std::uint8_t density{255};
  std::shared_ptr<const AppearanceEdits<ShapeAppearanceSettings>> edits{};
  bool preview_enabled{true};
};

// GRD presets may defer stops to the tool colors; shape fills store concrete
// colors, so resolve at pick time (shared with the options-bar paint pickers).
[[nodiscard]] GradientDefinition resolve_gradient_definition(GradientDefinition definition,
                                                             RgbColor foreground,
                                                             RgbColor background);

// The Dashes > Custom "Dash pattern" field's text: lengths in stroke widths, space
// separated in the UI locale. Parsing returns nullopt for text that is not a
// usable pattern; an odd count repeats once (SVG's reading).
[[nodiscard]] QString dash_pattern_text(const std::vector<double>& dashes);
[[nodiscard]] std::optional<std::vector<double>> parse_dash_pattern_text(const QString& text);

// Live-preview dialog for a shape layer's fill and stroke (the
// request_levels_settings convention): every control change fires
// preview_changed with the full settings; returns nullopt on cancel. Pattern
// choices may reference library-only pattern ids - the caller adopts them
// into the document PatternStore when applying. foreground/background resolve
// gradient presets that defer stops to the current tool colors.
// `reset_defaults` is what the Reset button restores (fill, stroke, opacity,
// edge; the geometry in `initial` is kept). `units` presents the geometry,
// line weight and stroke width fields in the ruler unit through the document
// PPI (docs/resolution-units.md); every value stays document pixels.
[[nodiscard]] std::optional<ShapeAppearanceSettings> request_shape_appearance_settings(
    QWidget* parent, std::function<void(const ShapeAppearanceSettings&)> preview_changed,
    ShapeAppearanceSettings initial, ShapeAppearanceSettings reset_defaults,
    GradientLibrary* gradient_library,
    PatternLibrary* pattern_library, const PatternStore* document_patterns, RgbColor foreground,
    RgbColor background, const DocumentFieldUnits& units = {},
    const AppearanceDialogContext<ShapeAppearanceSettings>* batch = nullptr);

}  // namespace patchy::ui
