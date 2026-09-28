#include "ui/brush_presets.hpp"

#include "ui/brush_tip_library.hpp"

#include <QObject>

#include <array>

namespace patchy::ui {

std::span<const BrushPreset> builtin_brush_presets() {
  static const std::array<BrushPreset, 7> presets{
      BrushPreset{QStringLiteral("round"), QStringLiteral("Round"), 25, 100, 100, 0, false, QString()},
      BrushPreset{QStringLiteral("square"), QStringLiteral("Square"), 25, 100, 100, 0, false,
                  builtin_square_brush_tip_id()},
      BrushPreset{QStringLiteral("soft_round"), QStringLiteral("Soft Round"), 12, 100, 100, 75, false,
                  QString()},
      BrushPreset{QStringLiteral("hard_round"), QStringLiteral("Hard Round"), 18, 100, 100, 0, false,
                  QString()},
      BrushPreset{QStringLiteral("pencil"), QStringLiteral("Pencil"), 4, 100, 100, 0, false, QString()},
      BrushPreset{QStringLiteral("ink"), QStringLiteral("Ink"), 12, 92, 100, 20, false, QString()},
      BrushPreset{QStringLiteral("airbrush"), QStringLiteral("Airbrush"), 56, 100, 12, 100, true, QString()},
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
  if (preset.id == QStringLiteral("round")) {
    return QObject::tr("Round");
  }
  if (preset.id == QStringLiteral("square")) {
    return QObject::tr("Square");
  }
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
