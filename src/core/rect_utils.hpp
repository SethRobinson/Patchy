#pragma once

#include "core/layer.hpp"

namespace patchy {

// The same area with non-negative width and height (a drag rect's anchor may
// be its bottom-right corner).
[[nodiscard]] Rect normalized_rect(Rect rect) noexcept;
[[nodiscard]] Rect intersect_rect(Rect a, Rect b) noexcept;
[[nodiscard]] Rect unite_rect(Rect a, Rect b) noexcept;

}  // namespace patchy
