#pragma once

#include "core/vector_shape.hpp"

#include <cstddef>
#include <vector>

// Simplify Path: flattens every subpath of a vector path and refits it
// through the same per-run Schneider fitter Make Work Path and image tracing
// use (core/path_fit), so an over-anchored trace or a hand-drawn path comes
// back with fewer anchors within a pixel tolerance. Independent runs, no
// global smoothness solve (docs/legal-constraints.md, "Vector tracing").
namespace patchy {

struct PathSimplifyOptions {
  double tolerance{1.0};             // maximum deviation in document pixels
  double corner_angle_degrees{60.0};  // sharper bends than this stay corners
  bool snap_curves_to_lines{false};   // collapse near-straight cubics
};

struct PathSimplifyResult {
  VectorPath path;
  std::size_t anchors_before{0};
  std::size_t anchors_after{0};
  // shape_group values whose subpaths changed (live-shape annotations of
  // those groups must be dropped, the keyShapeInvalidated rule).
  std::vector<int> changed_groups;
};

[[nodiscard]] std::size_t vector_path_anchor_count(const VectorPath& path) noexcept;

// A subpath keeps its original anchors when the refit is empty or not
// smaller ("never worse"); combine ops, shape groups, the closed flag, and the
// path-level fill fields survive unchanged.
[[nodiscard]] PathSimplifyResult simplify_vector_path(const VectorPath& path, const PathSimplifyOptions& options);

}  // namespace patchy
