#include "ui/brush_presets.hpp"

#include <QObject>

#include <array>

namespace patchy::ui {

std::span<const BrushPreset> builtin_brush_presets() {
  static const std::array<BrushPreset, 5> presets{
      BrushPreset{QStringLiteral("soft_round"), QStringLiteral("Soft Round"), 12, 100, 75, false},
      BrushPreset{QStringLiteral("hard_round"), QStringLiteral("Hard Round"), 18, 100, 0, false},
      BrushPreset{QStringLiteral("pencil"), QStringLiteral("Pencil"), 4, 100, 0, false},
      BrushPreset{QStringLiteral("ink"), QStringLiteral("Ink"), 12, 92, 20, false},
      BrushPreset{QStringLiteral("airbrush"), QStringLiteral("Airbrush"), 56, 12, 100, false},
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

QString brush_preset_display_name(const BrushPreset& preset) {
  if (preset.id == QStringLiteral("soft_round")) {
    return QObject::tr("Soft Round");
  }
  if (preset.id == QStringLiteral("hard_round")) {
    return QObject::tr("Hard Round");
  }
  if (preset.id == QStringLiteral("pencil")) {
    return QObject::tr("Pencil");
  }
  if (preset.id == QStringLiteral("ink")) {
    return QObject::tr("Ink");
  }
  if (preset.id == QStringLiteral("airbrush")) {
    return QObject::tr("Airbrush");
  }
  return preset.name;
}

}  // namespace patchy::ui
