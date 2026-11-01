#pragma once

#include "ui/shape_appearance_dialog.hpp"

#include <optional>

class QPixmap;
class QWidget;

namespace patchy::ui {

// A sample of the preset's line (caps, corners, dashes and offset) drawn in the
// theme's text color on a transparent pixmap; the manager's preview and its row icons.
[[nodiscard]] QPixmap stroke_style_preset_sample(const StrokeStylePreset& preset, int width, int height,
                                                 double line_width);

// One line naming the preset's settings, for example
// "Align: Center, Caps: Round, Corners: Bevel, Dashes: 4 2, Offset: 1".
[[nodiscard]] QString stroke_style_preset_summary(const StrokeStylePreset& preset);

// What the Stroke Presets manager's Use Preset chose: a built-in dash preset or a saved
// preset; both empty when it was closed without one.
struct StrokePresetChoice {
  std::optional<StrokeDashPreset> builtin;
  std::optional<StrokeStylePreset> saved;
};

// Opens the modal Stroke Presets manager (GitHub #557 follow-up). It lists the built-in dash
// presets first, marked Built-in and locked (they can be used, never renamed, moved or
// deleted), then the saved stroke presets (load_stroke_style_presets) with rename, move
// up/down and delete; every edit is saved to the setting at once. `initial_name` preselects a
// row by name.
[[nodiscard]] StrokePresetChoice request_stroke_preset_manager(QWidget* parent, const QString& initial_name = {});

}  // namespace patchy::ui
