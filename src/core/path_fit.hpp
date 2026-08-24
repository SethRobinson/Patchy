#pragma once

#include "core/vector_shape.hpp"

#include <vector>

// Fits traced outline polygons into bezier subpaths (Make Work Path from
// Selection). The pipeline is the classic published one: Douglas-Peucker
// significant-vertex reduction, corner classification by turn angle, then
// per-run least-squares cubic fitting with error-driven splitting after
// Schneider, "An Algorithm for Automatically Fitting Digitized Curves"
// (Graphics Gems, 1990). Deterministic double math with fixed tie-breaks
// (first index wins on equal error), no RNG - the cross-toolchain rule.
namespace patchy {

struct FitPoint {
  double x{0.0};
  double y{0.0};

  friend bool operator==(const FitPoint&, const FitPoint&) = default;
};

struct PathFitOptions {
  // Maximum allowed deviation in pixels (Photoshop's Make Work Path
  // tolerance): larger values yield fewer anchors.
  double tolerance{2.0};
  // A significant vertex whose direction change exceeds this angle becomes a
  // corner (a fit-run boundary with independent tangents).
  double corner_angle_degrees{60.0};
  // Estimate run end tangents from the neighboring SIGNIFICANT vertices
  // instead of the raw neighbors. Raw neighbors of a pixel-edge contour are
  // always axis-aligned, which leaves diagonal edges with flat-ended curves;
  // image tracing turns this on, Make Work Path keeps the historical output.
  bool smooth_corner_tangents{false};
  // After fitting, collapse any cubic whose control points stay within
  // `tolerance` of its chord into a straight segment.
  bool snap_curves_to_lines{false};
};

// Fits one implicitly-closed polyline loop (the first point is NOT repeated
// at the end) into a closed subpath. Loops with fewer than 3 points return an
// empty subpath. The caller assigns the subpath's combine op and shape group.
[[nodiscard]] PathSubpath fit_closed_loop(const std::vector<FitPoint>& points, const PathFitOptions& options);
// The Make Work Path form: `tolerance` with every other option at its default.
[[nodiscard]] PathSubpath fit_closed_loop(const std::vector<FitPoint>& points, double tolerance);

// Fits an OPEN polyline (both endpoints kept exactly) into an open subpath:
// the same Douglas-Peucker / corner / per-run Schneider stages without the
// wrap-around. Fewer than 2 points return an empty subpath. Simplify Path
// uses it for open subpaths.
[[nodiscard]] PathSubpath fit_open_polyline(const std::vector<FitPoint>& points, const PathFitOptions& options);

// Signed area of the implicitly-closed loop (shoelace). Positive means
// clockwise winding in y-down screen coordinates - the convention traced
// outer boundaries use; holes come back counterclockwise (negative).
[[nodiscard]] double loop_signed_area(const std::vector<FitPoint>& points);

}  // namespace patchy
