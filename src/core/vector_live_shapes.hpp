#pragma once

#include "core/vector_shape.hpp"

#include <optional>
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

// Shared center-out Polygon/Star tool geometry, angle in radians.
[[nodiscard]] PathSubpath generate_polygon_subpath(double cx, double cy, double radius,
                                                  double angle_radians, int sides, int star_inset);

// Fills box_corners (A..D clockwise from top-left) from the bbox.
void populate_live_shape_box_corners(LiveShapeParams& params) noexcept;

// Maps the parameters through a positive axis-aligned scale + translate (a, 0, 0, d, tx, ty),
// the one matrix family live shapes survive; radii and weights take the mean scale.
void scale_live_shape_params(LiveShapeParams& params, double sx, double sy, double tx, double ty) noexcept;

// Moves every position in the parameters by (dx, dy).
void translate_live_shape_params(LiveShapeParams& params, double dx, double dy) noexcept;

// The live parameters of `group` while its subpaths are still exactly what they generate,
// up to a translation (a Path Select move, which keeps no annotation of its own): the
// result is translated to match. nullopt once a point edit changed the shape, or when no
// entry covers the group (GitHub #521: work and saved paths keep their parameters this way).
[[nodiscard]] std::optional<LiveShapeParams> resolve_live_shape_group(
    const VectorPath& path, const std::vector<LiveShapeParams>& origination, std::int32_t group);

// Regenerates `params.index`'s subpaths from `params` in place: the first subpath's
// position and combine op are kept. False when the group is missing or nothing generates.
bool replace_live_shape_group(VectorPath& path, const LiveShapeParams& params);

}  // namespace patchy
