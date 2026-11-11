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

// Drops a disabled preview request that follows another one: both show the image as it
// was, so with Preview unticked a handle or slider drag sends nothing and the canvas does
// no work and shows no busy notice for it (#687 follow-up). The first disabled request
// after an enabled one still goes out, since it takes the preview away.
class AdjustmentPreviewGate {
public:
  [[nodiscard]] bool should_send(bool enabled) noexcept {
    const bool repeat = !enabled && sent_disabled_;
    sent_disabled_ = !enabled;
    return !repeat;
  }

private:
  bool sent_disabled_{false};
};

// Defined in filter_workflows.cpp (apply_levels_to_pixels there uses it too).
LevelsSettings clamp_levels_settings(LevelsSettings settings);

}  // namespace patchy::ui
