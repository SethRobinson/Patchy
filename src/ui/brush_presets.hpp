#pragma once

#include <QString>
#include <span>

namespace patchy::ui {

struct BrushPreset {
  QString id;
  QString name;
  int size{12};
  int opacity{100};
  int softness{75};
  bool build_up{false};
};

[[nodiscard]] std::span<const BrushPreset> builtin_brush_presets();
[[nodiscard]] const BrushPreset* find_brush_preset(const QString& id);
[[nodiscard]] QString brush_preset_display_name(const BrushPreset& preset);

}  // namespace patchy::ui
