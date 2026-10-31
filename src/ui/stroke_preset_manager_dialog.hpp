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

// Opens the modal Stroke Presets manager (GitHub #557 follow-up): the saved stroke
// presets of Shape Appearance's Preset row (load_stroke_style_presets), with rename,
// move up/down and delete. Every edit is saved to the setting at once. Returns the
// preset chosen with Use Preset or a double-click; nullopt when closed without one.
[[nodiscard]] std::optional<StrokeStylePreset> request_stroke_preset_manager(QWidget* parent,
                                                                             const QString& initial_name = {});

}  // namespace patchy::ui
