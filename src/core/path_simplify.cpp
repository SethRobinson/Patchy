#include "core/path_simplify.hpp"

#include "core/path_fit.hpp"
#include "core/vector_raster.hpp"

#include <algorithm>
#include <utility>

namespace patchy {

std::size_t vector_path_anchor_count(const VectorPath& path) noexcept {
  std::size_t count = 0;
  for (const auto& subpath : path.subpaths) {
    count += subpath.anchors.size();
  }
  return count;
}

PathSimplifyResult simplify_vector_path(const VectorPath& path, const PathSimplifyOptions& options) {
  PathSimplifyResult result;
  result.path = path;
  result.anchors_before = vector_path_anchor_count(path);
  PathFitOptions fit;
  fit.tolerance = std::max(0.1, options.tolerance);
  fit.corner_angle_degrees = options.corner_angle_degrees;
  // Raw flattened neighbors give the true curve tangent at a corner; the
  // kept-vertex estimate exists for pixel-edge staircases, not for curves.
  fit.smooth_corner_tangents = false;
  fit.snap_curves_to_lines = options.snap_curves_to_lines;
  for (auto& subpath : result.path.subpaths) {
    const auto minimum = subpath.closed ? std::size_t{3} : std::size_t{2};
    if (subpath.anchors.size() < minimum) {
      continue;
    }
    const auto polyline = flatten_subpath_polyline(subpath);
    std::vector<FitPoint> points;
    points.reserve(polyline.size());
    for (const auto& point : polyline) {
      points.push_back(FitPoint{point[0], point[1]});
    }
    auto fitted = subpath.closed ? fit_closed_loop(points, fit) : fit_open_polyline(points, fit);
    if (fitted.anchors.empty() || fitted.anchors.size() >= subpath.anchors.size()) {
      continue;  // never worse: keep the hand-authored anchors
    }
    fitted.closed = subpath.closed;
    fitted.op = subpath.op;
    fitted.shape_group = subpath.shape_group;
    if (std::find(result.changed_groups.begin(), result.changed_groups.end(), subpath.shape_group) ==
        result.changed_groups.end()) {
      result.changed_groups.push_back(subpath.shape_group);
    }
    subpath = std::move(fitted);
  }
  result.anchors_after = vector_path_anchor_count(result.path);
  return result;
}

}  // namespace patchy
