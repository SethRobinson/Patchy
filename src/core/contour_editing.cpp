#include "core/contour_editing.hpp"

#include "core/style_contour.hpp"

#include <algorithm>
#include <cmath>

namespace patchy {

namespace {

float whole_level(float value) noexcept {
  if (!std::isfinite(value)) {
    return 0.0F;
  }
  return std::clamp(std::round(value), 0.0F, 255.0F);
}

}  // namespace

StyleContour editable_style_contour(const StyleContour& contour) {
  StyleContour result;
  result.name = contour.name;
  if (style_contour_is_linear(contour)) {
    result.points = {StyleContourPoint{0.0F, 0.0F, false}, StyleContourPoint{255.0F, 255.0F, false}};
    return result;
  }
  result.points.reserve(contour.points.size());
  for (auto point : contour.points) {
    point.x = std::isfinite(point.x) ? std::clamp(point.x, 0.0F, 255.0F) : 0.0F;
    point.y = std::isfinite(point.y) ? std::clamp(point.y, 0.0F, 255.0F) : 0.0F;
    result.points.push_back(point);
  }
  std::stable_sort(result.points.begin(), result.points.end(),
                   [](const StyleContourPoint& lhs, const StyleContourPoint& rhs) { return lhs.x < rhs.x; });
  // Equal inputs: the last one given wins (build_style_contour_lut's rule).
  std::vector<StyleContourPoint> unique;
  unique.reserve(result.points.size());
  for (const auto& point : result.points) {
    if (!unique.empty() && std::abs(unique.back().x - point.x) < 1e-6F) {
      unique.back() = point;
      continue;
    }
    unique.push_back(point);
  }
  result.points = std::move(unique);
  if (result.points.size() < kMinContourPoints) {
    // One usable point is a flat line at its output: keep that reading.
    const auto level = result.points.empty() ? 0.0F : result.points.front().y;
    result.points = {StyleContourPoint{0.0F, level, false}, StyleContourPoint{255.0F, level, false}};
  }
  return result;
}

std::optional<std::size_t> add_contour_point(StyleContour& contour, float x, float y) {
  if (contour.points.size() >= kMaxContourPoints) {
    return std::nullopt;
  }
  StyleContourPoint point;
  point.x = whole_level(x);
  point.y = whole_level(y);
  point.corner = false;
  const auto after = std::find_if(contour.points.begin(), contour.points.end(),
                                  [&point](const StyleContourPoint& existing) { return existing.x >= point.x; });
  if (after != contour.points.end() && after->x - point.x < 1.0F) {
    return std::nullopt;
  }
  if (after != contour.points.begin() && point.x - std::prev(after)->x < 1.0F) {
    return std::nullopt;
  }
  const auto index = static_cast<std::size_t>(std::distance(contour.points.begin(), after));
  contour.points.insert(after, point);
  return index;
}

bool move_contour_point(StyleContour& contour, std::size_t index, float x, float y) {
  if (index >= contour.points.size()) {
    return false;
  }
  auto& point = contour.points[index];
  auto low = 0.0F;
  auto high = 255.0F;
  if (index > 0U) {
    low = std::ceil(contour.points[index - 1U].x + 1.0F - 1e-4F);
  }
  if (index + 1U < contour.points.size()) {
    high = std::floor(contour.points[index + 1U].x - 1.0F + 1e-4F);
  }
  auto new_x = point.x;
  if (low <= high) {
    new_x = std::clamp(whole_level(x), low, high);
  }
  const auto new_y = whole_level(y);
  if (new_x == point.x && new_y == point.y) {
    return false;
  }
  point.x = new_x;
  point.y = new_y;
  return true;
}

bool remove_contour_point(StyleContour& contour, std::size_t index) {
  if (index >= contour.points.size() || contour.points.size() <= kMinContourPoints) {
    return false;
  }
  contour.points.erase(contour.points.begin() + static_cast<std::ptrdiff_t>(index));
  return true;
}

bool set_contour_point_corner(StyleContour& contour, std::size_t index, bool corner) {
  if (index >= contour.points.size() || contour.points[index].corner == corner) {
    return false;
  }
  contour.points[index].corner = corner;
  return true;
}

bool style_contours_same_shape(const StyleContour& lhs, const StyleContour& rhs) noexcept {
  if (style_contour_is_linear(lhs) && style_contour_is_linear(rhs)) {
    return true;
  }
  if (lhs.points.size() != rhs.points.size()) {
    return false;
  }
  for (std::size_t i = 0; i < lhs.points.size(); ++i) {
    const auto& a = lhs.points[i];
    const auto& b = rhs.points[i];
    if (std::abs(a.x - b.x) > 1e-4F || std::abs(a.y - b.y) > 1e-4F || a.corner != b.corner) {
      return false;
    }
  }
  return true;
}

}  // namespace patchy
