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
  // A Color, Gradient or Pattern fill layer without a vector path: the dialog shows
  // only its Fill section (GitHub #582). Not an appearance value: never compared,
  // edited or applied.
  bool fill_layer_only{false};
};

// The Dashes > Custom "Dash pattern" field's text: lengths in stroke widths, space
// separated in the UI locale. Parsing returns nullopt for text that is not a
// usable pattern; an odd count repeats once (SVG's reading).
[[nodiscard]] QString dash_pattern_text(const std::vector<double>& dashes);
[[nodiscard]] std::optional<std::vector<double>> parse_dash_pattern_text(const QString& text);
// A dash preset's name, as the Dashes combo and the options bar's dash combo show it.
[[nodiscard]] QString stroke_dash_preset_label(StrokeDashPreset preset);

// A stroke style the user saved in Shape Appearance (GitHub #557): the line
// settings without width or paint. Saved in the app settings under
// kStrokeStylePresetsKey as a JSON array (stroke_style_presets_json).
struct StrokeStylePreset {
  QString name;
  VectorStrokeAlignment alignment{VectorStrokeAlignment::Inside};
  VectorStrokeCap cap{VectorStrokeCap::Butt};
  VectorStrokeJoin join{VectorStrokeJoin::Miter};
  std::vector<double> dashes;  // stroke widths; empty is solid
  double dash_offset{0.0};     // stroke widths
};
inline constexpr const char* kStrokeStylePresetsKey = "shapeAppearance/strokeStylePresets";
[[nodiscard]] StrokeStylePreset stroke_style_preset_from(const VectorStroke& stroke, QString name);
void apply_stroke_style_preset(VectorStroke& stroke, const StrokeStylePreset& preset);
[[nodiscard]] bool stroke_matches_style_preset(const VectorStroke& stroke, const StrokeStylePreset& preset);
[[nodiscard]] QString stroke_style_presets_json(const std::vector<StrokeStylePreset>& presets);
// Skips entries it cannot read (no name, unusable dashes).
[[nodiscard]] std::vector<StrokeStylePreset> parse_stroke_style_presets_json(const QString& json);
[[nodiscard]] std::vector<StrokeStylePreset> load_stroke_style_presets();
void save_stroke_style_presets(const std::vector<StrokeStylePreset>& presets);

// One stroke list (GitHub #557 follow-up): Shape Appearance's Dashes combo and the
// options bar's dash combo list the built-in dash presets, then the saved presets with
// item data kSavedStrokePresetRow + index, then Custom. The Stroke Presets manager lists
// the built-ins first too, locked.
inline constexpr int kSavedStrokePresetRow = 1000;
// A saved preset may not take a built-in's name (compared case-insensitively, in the UI language).
[[nodiscard]] bool stroke_preset_name_is_builtin(const QString& name);
// A built-in dash preset as the manager shows it: its name, its caps and corners (Butt and
// Miter where it sets none), its pattern for those caps, Inside and no offset.
[[nodiscard]] StrokeStylePreset builtin_stroke_style_preset(StrokeDashPreset preset);
// The index of the first saved preset `stroke` matches on every field, or -1.
[[nodiscard]] int matching_stroke_style_preset(const VectorStroke& stroke,
                                               const std::vector<StrokeStylePreset>& presets);

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
