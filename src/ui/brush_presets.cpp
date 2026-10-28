#include "ui/brush_presets.hpp"

#include "ui/brush_tip_library.hpp"

#include <QObject>

#include <array>

namespace patchy::ui {

std::span<const BrushPreset> builtin_brush_presets() {
  // GitHub issue 482: "soft_round" shows as Round and leads the list, "hard_round" follows
  // at the old Round's 25 px, and the old "round" id is an alias of "hard_round"
  // (canonical_brush_preset_id), so it stays out of the list.
  static const std::array<BrushPreset, 6> presets{
      BrushPreset{QStringLiteral("soft_round"), QStringLiteral("Round"), 12, 100, 100, 75, false, QString()},
      BrushPreset{QStringLiteral("hard_round"), QStringLiteral("Hard Round"), 25, 100, 100, 0, false,
                  QString()},
      BrushPreset{QStringLiteral("square"), QStringLiteral("Square"), 25, 100, 100, 0, false,
                  builtin_square_brush_tip_id()},
      BrushPreset{QStringLiteral("pencil"), QStringLiteral("Pencil"), 4, 100, 100, 0, false, QString()},
      BrushPreset{QStringLiteral("ink"), QStringLiteral("Ink"), 12, 92, 100, 20, false, QString()},
      BrushPreset{QStringLiteral("airbrush"), QStringLiteral("Airbrush"), 56, 100, 12, 100, true, QString()},
  };
  return presets;
}

QString canonical_brush_preset_id(const QString& id) {
  // The Round preset of releases before issue 482 (a 25 px hard round): saved settings,
  // scripts and tool presets that name it get Hard Round, which now has the same values.
  if (id == QStringLiteral("round")) {
    return QStringLiteral("hard_round");
  }
  return id;
}

const BrushPreset* find_brush_preset(const QString& id) {
  const auto canonical = canonical_brush_preset_id(id);
  for (const auto& preset : builtin_brush_presets()) {
    if (preset.id == canonical) {
      return &preset;
    }
  }
  return nullptr;
}

QString brush_preset_display_name(const BrushPreset& preset) {
  if (preset.id == QStringLiteral("soft_round")) {
    return QObject::tr("Round");
  }
  if (preset.id == QStringLiteral("square")) {
    return QObject::tr("Square");
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
