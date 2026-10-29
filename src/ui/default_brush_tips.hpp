#pragma once

#include "core/brush_tip.hpp"

#include <QString>

#include <vector>

namespace patchy::ui {

struct DefaultBrushTipSpec {
  QString name;
  // The English name, whatever the interface language: the seeded tip's stable id
  // (default_brush_tip_id).
  QString key;
  double spacing{0.25};
  patchy::BrushTip tip;
  // Curated Photoshop-style dynamics seeded with the tip (default = none). Applied to existing
  // installs once per defaultTipsVersion bump via BrushTipLibrary::apply_default_tip_dynamics,
  // which skips tips whose dynamics the user has customized.
  patchy::BrushDynamics dynamics{};
  // The defaultTipsVersion in which this tip first shipped. Version-gated seeding only adds
  // specs newer than the user's stored version, so a version bump introduces new tips without
  // resurrecting older defaults the user deliberately deleted.
  int since_version{1};
};

// The folder the built-in tips are seeded into (localized once at seed time).
[[nodiscard]] QString default_brush_tips_folder_name();

// The library id (file name) a seeded default is stored under: "default-" and its key, so two
// Patchy processes seeding a fresh profile at once write the same files instead of one copy
// each (GitHub issue 506), and the tip is found again in any interface language.
[[nodiscard]] QString default_brush_tip_id(const DefaultBrushTipSpec& spec);

// Deterministic, parametric generation of the built-in bitmap brush set: natural media (chalk,
// charcoal, spray, bristle, and friends), the v3 stamp set (dotted/dashed lines, brick road,
// leaves, sparkles, the RTsoft logo, ...), and v4 effect presets. Same output on every run; the
// tips are seeded into the user's brush library on first launch and are ordinary user tips
// afterwards (renameable, deletable — deleting them does not resurrect on the next run).
[[nodiscard]] std::vector<DefaultBrushTipSpec> generate_default_brush_tips();

}  // namespace patchy::ui
