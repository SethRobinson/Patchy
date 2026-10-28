#pragma once

#include <QString>
#include <span>

namespace patchy::ui {

struct BrushPreset {
  QString id;
  QString name;
  int size{12};
  int opacity{100};
  int flow{100};
  int softness{75};
  bool build_up{false};
  // Built-in procedural tip id the preset selects (builtin_square_brush_tip_id()); empty =
  // the procedural Round tip. Library (bitmap) tips are never preset targets.
  QString tip_id;
};

[[nodiscard]] std::span<const BrushPreset> builtin_brush_presets();
// Maps retired preset ids to the preset that replaced them ("round" -> "hard_round").
[[nodiscard]] QString canonical_brush_preset_id(const QString& id);
// Resolves retired ids too; the returned preset's id is the canonical one.
[[nodiscard]] const BrushPreset* find_brush_preset(const QString& id);
[[nodiscard]] QString brush_preset_display_name(const BrushPreset& preset);

}  // namespace patchy::ui
