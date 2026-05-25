#pragma once

#include "core/layer.hpp"

namespace photoslop {

[[nodiscard]] Rect intersect_rect(Rect a, Rect b) noexcept;
[[nodiscard]] Rect unite_rect(Rect a, Rect b) noexcept;

}  // namespace photoslop
