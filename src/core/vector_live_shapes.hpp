#pragma once

#include "core/vector_shape.hpp"

#include <optional>
#include <vector>

// Live-shape parameter -> path regeneration. The generated subpaths reproduce
// Photoshop 27.8's own knot constructions (docs/vector-psd-encodings.md):
// - sharp rectangle: 4 corner knots TL/TR/BR/BL clockwise;
// - rounded rectangle: per rounded corner two smooth knots (arc start/end)
//   with kappa handles, starting at the top-left arc's top-edge end;
//   zero-radius corners collapse to one corner knot;
// - ellipse: 4 smooth knots top/right/bottom/left with kappa handles;
// - line: the width-w quad [start+n, start-n, end-n, end+n] with
//   n = (w/2) * (-dy, dx)/|d|; arrowheads (Patchy-authored; Photoshop renders
//   the path regardless) wider than the line make one outline with it (start
//   cap, -n side, end cap, +n side), narrower ones separate head polygons in
//   the same shape group.
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

// The live polygon whose generate_live_shape_subpaths output `path` is (within a
// fraction of a pixel), or nullopt. Only a path of exactly one closed corner-only
// subpath qualifies; an axis-aligned rectangle never does (it may be a rectangle whose
// live data is gone). The PSD reader uses it to give a reopened polygon back its Sides,
// Star inset and box, since the writer leaves polygon origination out (GitHub #643).
[[nodiscard]] std::optional<LiveShapeParams> recognize_live_polygon(const VectorPath& path);

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

// After a direct edit of a shape layer's path: each live-shape annotation whose group is
// still the shape it generates, up to a translation (a Path Select or Ctrl+shape-tool drag,
// a nudge, an align), moves with it; a touched group that no longer matches loses its
// annotation (drop_live_shape_origination, Photoshop's keyShapeInvalidated). A moved polygon
// stays a live polygon this way (GitHub #643).
void keep_or_drop_live_shapes_after_path_edit(VectorShapeContent& content,
                                              const std::vector<int>& touched_groups);

// Regenerates `params.index`'s subpaths from `params` in place: the first subpath's
// position and combine op are kept. False when the group is missing or nothing generates.
bool replace_live_shape_group(VectorPath& path, const LiveShapeParams& params);

// The two points of the drag that drew a live Polygon or Line, which the shape tools show
// on the canvas to edit it (GitHub #690 follow-up). A polygon has them only while its box
// is square, what the center-out drag makes: a W / H edit that stretched it has no single
// radius to drag. docs/vector-tools-shape-handles.md, "Drag points on a live Polygon and Line".
struct LiveShapePoint {
  double x{0.0};
  double y{0.0};
};
[[nodiscard]] bool live_polygon_is_regular(const LiveShapeParams& params) noexcept;
[[nodiscard]] LiveShapePoint live_polygon_center(const LiveShapeParams& params) noexcept;
[[nodiscard]] LiveShapePoint live_polygon_first_vertex(const LiveShapeParams& params) noexcept;
// Puts the first vertex at (x, y) about the unchanged center: the radius and polygon_angle
// follow exactly as in the Polygon tool's center-out drag; Sides and Star inset stay. False
// (params untouched) when the point is under a pixel from the center.
bool set_live_polygon_first_vertex(LiveShapeParams& params, double x, double y) noexcept;
// Moves one end of a live line (`end`: the end point, else the start); the other end, the
// weight and the arrowheads stay, and the box is refitted to the line the parameters
// generate. False (params untouched) when the line would be under a pixel long.
bool set_live_line_point(LiveShapeParams& params, bool end, double x, double y);

}  // namespace patchy
