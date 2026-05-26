#include "ui/brush_presets.hpp"

#include <array>

namespace photoslop::ui {

std::span<const BrushPreset> builtin_brush_presets() {
  static const std::array<BrushPreset, 5> presets{
      BrushPreset{QStringLiteral("soft_round"), QStringLiteral("Soft Round"), 12, 100, 75, false},
      BrushPreset{QStringLiteral("hard_round"), QStringLiteral("Hard Round"), 18, 100, 0, false},
      BrushPreset{QStringLiteral("pencil"), QStringLiteral("Pencil"), 4, 100, 0, false},
      BrushPreset{QStringLiteral("ink"), QStringLiteral("Ink"), 12, 92, 20, false},
      BrushPreset{QStringLiteral("airbrush"), QStringLiteral("Airbrush"), 56, 12, 100, true},
  };
  return presets;
}

const BrushPreset* find_brush_preset(const QString& id) {
  for (const auto& preset : builtin_brush_presets()) {
    if (preset.id == id) {
      return &preset;
    }
  }
  return nullptr;
}

}  // namespace photoslop::ui
