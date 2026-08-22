#include "formats/vector_fill_rule.hpp"

#include <cmath>
#include <limits>

namespace patchy::formats {
namespace {

constexpr double kEpsilon = 1e-9;

}  // namespace

std::vector<std::array<double, 2>> subpath_polyline(const PathSubpath& subpath) {
  std::vector<std::array<double, 2>> points;
  const auto count = subpath.anchors.size();
  if (count == 0) {
    return points;
  }
  points.reserve(count * 3);
  for (std::size_t i = 0; i < count; ++i) {
    const auto& from = subpath.anchors[i];
    points.push_back({from.anchor_x, from.anchor_y});
    const auto& to = subpath.anchors[(i + 1) % count];
    if (i + 1 == count && !subpath.closed) {
      break;
    }
    const bool straight = std::abs(from.out_x - from.anchor_x) < kEpsilon &&
                          std::abs(from.out_y - from.anchor_y) < kEpsilon &&
                          std::abs(to.in_x - to.anchor_x) < kEpsilon && std::abs(to.in_y - to.anchor_y) < kEpsilon;
    if (straight) {
      continue;
    }
    for (const double t : {1.0 / 3.0, 2.0 / 3.0}) {
      const double u = 1.0 - t;
      const double bx = u * u * u * from.anchor_x + 3.0 * u * u * t * from.out_x + 3.0 * u * t * t * to.in_x +
                        t * t * t * to.anchor_x;
      const double by = u * u * u * from.anchor_y + 3.0 * u * u * t * from.out_y + 3.0 * u * t * t * to.in_y +
                        t * t * t * to.anchor_y;
      points.push_back({bx, by});
    }
  }
  return points;
}

double polyline_signed_area(const std::vector<std::array<double, 2>>& points) {
  if (points.size() < 3) {
    return 0.0;
  }
  double area = 0.0;
  for (std::size_t i = 0; i < points.size(); ++i) {
    const auto& a = points[i];
    const auto& b = points[(i + 1) % points.size()];
    area += a[0] * b[1] - b[0] * a[1];
  }
  return area / 2.0;
}

bool polyline_contains(const std::vector<std::array<double, 2>>& points, double x, double y) {
  if (points.empty()) {
    return false;
  }
  bool inside = false;
  for (std::size_t i = 0, j = points.size() - 1; i < points.size(); j = i++) {
    const auto& a = points[i];
    const auto& b = points[j];
    if ((a[1] > y) != (b[1] > y) && x < (b[0] - a[0]) * (y - a[1]) / (b[1] - a[1]) + a[0]) {
      inside = !inside;
    }
  }
  return inside;
}

void apply_even_odd(VectorPath& path) {
  for (auto& subpath : path.subpaths) {
    subpath.shape_group = 0;
    subpath.op = PathCombineOp::Add;
  }
}

void decompose_nonzero(VectorPath& path) {
  if (path.subpaths.size() < 2) {
    if (!path.subpaths.empty()) {
      path.subpaths.front().shape_group = 0;
      path.subpaths.front().op = PathCombineOp::Add;
    }
    return;
  }
  std::vector<std::vector<std::array<double, 2>>> polylines;
  std::vector<double> areas;
  polylines.reserve(path.subpaths.size());
  for (const auto& subpath : path.subpaths) {
    polylines.push_back(subpath_polyline(subpath));
    areas.push_back(polyline_signed_area(polylines.back()));
  }
  for (std::size_t i = 0; i < path.subpaths.size(); ++i) {
    path.subpaths[i].shape_group = static_cast<std::int32_t>(i);
    path.subpaths[i].op = PathCombineOp::Add;
    if (polylines[i].empty()) {
      continue;
    }
    // Innermost strictly-larger container decides hole-ness.
    int parent = -1;
    double parent_area = std::numeric_limits<double>::infinity();
    const auto& probe = polylines[i].front();
    for (std::size_t j = 0; j < path.subpaths.size(); ++j) {
      if (i == j || std::abs(areas[j]) <= std::abs(areas[i])) {
        continue;
      }
      if (polyline_contains(polylines[j], probe[0], probe[1]) && std::abs(areas[j]) < parent_area) {
        parent = static_cast<int>(j);
        parent_area = std::abs(areas[j]);
      }
    }
    if (parent >= 0 && (areas[i] < 0.0) != (areas[static_cast<std::size_t>(parent)] < 0.0)) {
      path.subpaths[i].op = PathCombineOp::Subtract;
    }
  }
}

}  // namespace patchy::formats
