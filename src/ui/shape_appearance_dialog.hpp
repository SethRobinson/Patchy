#pragma once

#include "core/vector_shape.hpp"

#include <functional>
#include <optional>

class QWidget;

namespace patchy {
struct PatternStore;
}

namespace patchy::ui {

class GradientLibrary;
class PatternLibrary;

// The editable appearance of a shape/fill layer (the dialog leaves the path,
// origination, and flags untouched).
struct ShapeAppearanceSettings {
  VectorFill fill;
  VectorStroke stroke;
};

// Live-preview dialog for a shape layer's fill and stroke (the
// request_levels_settings convention): every control change fires
// preview_changed with the full settings; returns nullopt on cancel. Pattern
// choices may reference library-only pattern ids - the caller adopts them
// into the document PatternStore when applying. foreground/background resolve
// gradient presets that defer stops to the current tool colors.
[[nodiscard]] std::optional<ShapeAppearanceSettings> request_shape_appearance_settings(
    QWidget* parent, std::function<void(const ShapeAppearanceSettings&)> preview_changed,
    ShapeAppearanceSettings initial, GradientLibrary* gradient_library,
    PatternLibrary* pattern_library, const PatternStore* document_patterns, RgbColor foreground,
    RgbColor background);

}  // namespace patchy::ui
