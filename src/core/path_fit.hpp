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

// Fits one implicitly-closed polyline loop (the first point is NOT repeated
// at the end) into a closed subpath. `tolerance` is the maximum allowed
// deviation in pixels (Photoshop's Make Work Path tolerance): larger values
// yield fewer anchors. Loops with fewer than 3 points return an empty
// subpath. The caller assigns the subpath's combine op and shape group.
[[nodiscard]] PathSubpath fit_closed_loop(const std::vector<FitPoint>& points, double tolerance);

// Signed area of the implicitly-closed loop (shoelace). Positive means
// clockwise winding in y-down screen coordinates - the convention traced
// outer boundaries use; holes come back counterclockwise (negative).
[[nodiscard]] double loop_signed_area(const std::vector<FitPoint>& points);

}  // namespace patchy
