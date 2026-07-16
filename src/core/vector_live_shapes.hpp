#pragma once

#include "core/vector_shape.hpp"

#include <vector>

// Live-shape parameter -> path regeneration. The generated subpaths reproduce
// Photoshop 27.8's own knot constructions (docs/vector-tools.md):
// - sharp rectangle: 4 corner knots TL/TR/BR/BL clockwise;
// - rounded rectangle: per rounded corner two smooth knots (arc start/end)
//   with kappa handles, starting at the top-left arc's top-edge end;
//   zero-radius corners collapse to one corner knot;
// - ellipse: 4 smooth knots top/right/bottom/left with kappa handles;
// - line: the width-w quad [start+n, start-n, end-n, end+n] with
//   n = (w/2) * (-dy, dx)/|d|; arrowheads (Patchy-authored; Photoshop renders
//   the path regardless) append head polygons in the same shape group.
// All arithmetic is +,*,sqrt on doubles (no trig), deterministic across
// toolchains.
namespace patchy {

inline constexpr double kLiveShapeKappa = 0.5522847498307936;

// Regenerates the subpaths for one live-shape group. Every returned subpath is
// closed, op Add, shape_group = params.index. Returns an empty vector for
// kinds without a generator (None/Custom).
[[nodiscard]] std::vector<PathSubpath> generate_live_shape_subpaths(const LiveShapeParams& params);

// Fills box_corners (A..D clockwise from top-left) from the bbox.
void populate_live_shape_box_corners(LiveShapeParams& params) noexcept;

}  // namespace patchy
