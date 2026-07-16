#pragma once

// Helpers shared by the filter pipeline TUs (filter_engine, filter_registry,
// smart_filter_renderer); each used to carry its own near-identical copy.
// Internal to src/filters - do not include this header elsewhere.

#include "filters/filter_registry.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace patchy {

// Whether a recipe/smart-filter blend step supports this blend mode.
inline bool recipe_blend_mode_supported(BlendMode mode) noexcept {
  const auto value = static_cast<int>(mode);
  return value >= static_cast<int>(BlendMode::Normal) &&
         value <= static_cast<int>(BlendMode::Divide);
}

// Progress adapter that maps phase phase_index of phase_count onto the outer
// progress range. (smart_filter_renderer keeps its own phase_progress variant
// with kProgressScale-based overflow-guarded math - a deliberate delta.)
inline FilterProgress filter_progress_phase(const FilterProgress *progress,
                                            int phase_index, int phase_count) {
  if (progress == nullptr || !progress->update) {
    return {};
  }
  return FilterProgress{[progress, phase_index,
                         phase_count](int completed, int total,
                                      FilterProgressStage stage) {
    constexpr int kPhaseScale = 1000;
    const auto safe_phase_count = std::max(1, phase_count);
    const auto safe_total = std::max(1, total);
    const auto clamped_completed = std::clamp(completed, 0, safe_total);
    const auto phase_completed = (clamped_completed * kPhaseScale) / safe_total;
    return progress->update(std::clamp(phase_index, 0, safe_phase_count - 1) *
                                    kPhaseScale +
                                phase_completed,
                            safe_phase_count * kPhaseScale, stage);
  }};
}

// Union of two result bounds with int32 overflow checks; overflow_message
// keeps each caller's historical error text ("Filter result bounds overflow" /
// "Smart Filter result bounds overflow").
[[nodiscard]] inline Rect checked_union_bounds(Rect left, Rect right,
                                               const char *overflow_message) {
  if (left.empty()) {
    return right;
  }
  if (right.empty()) {
    return left;
  }
  const auto left_x2 = static_cast<std::int64_t>(left.x) + left.width;
  const auto left_y2 = static_cast<std::int64_t>(left.y) + left.height;
  const auto right_x2 = static_cast<std::int64_t>(right.x) + right.width;
  const auto right_y2 = static_cast<std::int64_t>(right.y) + right.height;
  const auto x1 = std::min<std::int64_t>(left.x, right.x);
  const auto y1 = std::min<std::int64_t>(left.y, right.y);
  const auto x2 = std::max(left_x2, right_x2);
  const auto y2 = std::max(left_y2, right_y2);
  if (x1 < std::numeric_limits<std::int32_t>::min() ||
      y1 < std::numeric_limits<std::int32_t>::min() ||
      x2 > std::numeric_limits<std::int32_t>::max() ||
      y2 > std::numeric_limits<std::int32_t>::max() ||
      x2 - x1 > std::numeric_limits<std::int32_t>::max() ||
      y2 - y1 > std::numeric_limits<std::int32_t>::max()) {
    throw std::overflow_error(overflow_message);
  }
  return Rect{static_cast<std::int32_t>(x1), static_cast<std::int32_t>(y1),
              static_cast<std::int32_t>(x2 - x1),
              static_cast<std::int32_t>(y2 - y1)};
}

}  // namespace patchy
