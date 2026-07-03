#pragma once

#include "core/brush_tip.hpp"

#include <QString>

#include <vector>

namespace patchy::ui {

struct DefaultBrushTipSpec {
  QString name;
  double spacing{0.25};
  patchy::BrushTip tip;
};

// The folder the built-in tips are seeded into (localized once at seed time).
[[nodiscard]] QString default_brush_tips_folder_name();

// Deterministic, parametric generation of the built-in bitmap brush set (chalk, charcoal,
// spatter, spray, bristle, calligraphy, and friends). Same output on every run; the tips are
// seeded into the user's brush library on first launch and are ordinary user tips afterwards
// (renameable, deletable — deleting them does not resurrect on the next run).
[[nodiscard]] std::vector<DefaultBrushTipSpec> generate_default_brush_tips();

}  // namespace patchy::ui
