#pragma once

#include <optional>

class QWidget;

// Select > Smooth and Select > Feather dialogs (GitHub issue 614, docs/selection-modify.md).
// Up/Down step the radius by one step, Shift+Up/Down by ten.
namespace patchy::ui {

// Settings keys of the last radius each command used (persisted identifiers).
inline constexpr const char* kSelectionSmoothRadiusKey = "tools/selectionSmoothRadius";
inline constexpr const char* kSelectionFeatherRadiusKey = "tools/selectionFeatherRadius";

// Sample radius in whole pixels, 1 to 500. Object name patchySmoothSelectionDialog, spin box
// smoothSelectionRadiusSpin.
[[nodiscard]] std::optional<int> request_selection_smooth_radius(QWidget* parent, int initial);
// Feather radius in pixels with one decimal, 0.1 to 1000. Object name
// patchyFeatherSelectionDialog, spin box featherSelectionRadiusSpin.
[[nodiscard]] std::optional<double> request_selection_feather_radius(QWidget* parent, double initial);

}  // namespace patchy::ui
