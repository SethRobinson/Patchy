#include "core/rect_utils.hpp"

#include <algorithm>

namespace patchy {

namespace {

[[nodiscard]] Rect normalized_rect(Rect rect) noexcept {
  if (rect.width < 0) {
    rect.x += rect.width;
    rect.width = -rect.width;
  }
  if (rect.height < 0) {
    rect.y += rect.height;
    rect.height = -rect.height;
  }
  return rect;
}

}  // namespace

Rect intersect_rect(Rect a, Rect b) noexcept {
  a = normalized_rect(a);
  b = normalized_rect(b);
  const auto left = std::max(a.x, b.x);
  const auto top = std::max(a.y, b.y);
  const auto right = std::min(a.x + a.width, b.x + b.width);
  const auto bottom = std::min(a.y + a.height, b.y + b.height);
  return Rect{left, top, std::max(0, right - left), std::max(0, bottom - top)};
}

Rect unite_rect(Rect a, Rect b) noexcept {
  a = normalized_rect(a);
  b = normalized_rect(b);
  if (a.empty()) {
    return b;
  }
  if (b.empty()) {
    return a;
  }

  const auto left = std::min(a.x, b.x);
  const auto top = std::min(a.y, b.y);
  const auto right = std::max(a.x + a.width, b.x + b.width);
  const auto bottom = std::max(a.y + a.height, b.y + b.height);
  return Rect{left, top, right - left, bottom - top};
}

}  // namespace patchy
