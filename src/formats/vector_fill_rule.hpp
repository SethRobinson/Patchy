#pragma once

#include "core/vector_shape.hpp"

#include <array>
#include <vector>

// Nonzero-winding decomposition, shared by every vector importer.
//
// Patchy's rasterizer combines subpaths of one shape_group under the EVEN-ODD rule
// and applies the combine ops BETWEEN groups (see core/vector_raster.hpp). There is
// no nonzero mode, so a format that specifies nonzero (SVG's default fill-rule,
// PDF's `f` and `B`) has to be rewritten into that model: give each subpath its own
// group, and turn a subpath contained in an opposite-winding subpath into a Subtract
// group, i.e. a hole.
//
// The one approximated case is a SINGLE self-intersecting subpath, which keeps
// even-odd semantics because there is nothing to decompose it against.

namespace patchy::formats {

// Anchors plus two cubic samples per curved segment: enough to classify holes,
// while the exact geometry stays in the beziers.
[[nodiscard]] std::vector<std::array<double, 2>> subpath_polyline(const PathSubpath& subpath);
// Positive for one winding direction, negative for the other; the magnitude is the
// enclosed area.
[[nodiscard]] double polyline_signed_area(const std::vector<std::array<double, 2>>& points);
[[nodiscard]] bool polyline_contains(const std::vector<std::array<double, 2>>& points, double x, double y);

// Rewrites `path` in place for nonzero winding.
void decompose_nonzero(VectorPath& path);
// The even-odd counterpart: one group, everything unioned, which is exactly what
// core's within-group rule already does.
void apply_even_odd(VectorPath& path);

}  // namespace patchy::formats
