#pragma once

#include "core/layer.hpp"

namespace patchy {

[[nodiscard]] Rect intersect_rect(Rect a, Rect b) noexcept;
[[nodiscard]] Rect unite_rect(Rect a, Rect b) noexcept;

}  // namespace patchy
