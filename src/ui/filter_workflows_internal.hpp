#pragma once

// Helpers shared between the two halves of the filter_workflows TU split
// (filter_workflows.cpp and adjustment_dialogs.cpp). Never include this
// header anywhere else.

#include "ui/filter_workflows.hpp"

namespace patchy::ui {

template <typename Settings>
struct AdjustmentPreviewRequest {
  bool enabled{true};
  Settings settings{};
};

// Defined in filter_workflows.cpp (apply_levels_to_pixels there uses it too).
LevelsSettings clamp_levels_settings(LevelsSettings settings);

}  // namespace patchy::ui
