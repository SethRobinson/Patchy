#pragma once

#include <QString>
#include <span>

namespace photoslop::ui {

struct BrushPreset {
  QString id;
  QString name;
  int size{12};
  int opacity{100};
  int softness{75};
};

[[nodiscard]] std::span<const BrushPreset> builtin_brush_presets();
[[nodiscard]] const BrushPreset* find_brush_preset(const QString& id);

}  // namespace photoslop::ui
