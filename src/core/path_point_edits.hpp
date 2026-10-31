#pragma once

#include "core/vector_shape.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <utility>
#include <vector>

// Point, segment and component edits on a vector path (GitHub #555; docs/vector-path-edits.md,
// "Point and segment commands" and "Component commands"). A point is (subpath, anchor) and a
// segment is (subpath, index): segment i runs from anchor i to anchor i + 1, and on a closed
// subpath the last segment returns to anchor 0. Every edit keeps each subpath's combine op and
// shape group, so a component that splits into pieces still renders as one shape group.
namespace patchy {

using PathPointKey = std::pair<int, int>;

struct PathPointEditResult {
  VectorPath path;
  // shape_group values whose geometry changed (their live-shape annotations drop).
  std::vector<int> touched_groups;
  // The points to keep selected after the edit, as keys into `path`.
  std::set<PathPointKey> selection;
};

// Break Path at Point: a closed subpath opens at each listed anchor (the anchor becomes two end
// points at the same place), an open one splits into two subpaths at each interior anchor. End
// points of open subpaths are skipped. nullopt when nothing listed can break.
[[nodiscard]] std::optional<PathPointEditResult> break_path_at_points(const VectorPath& path,
                                                                      const std::set<PathPointKey>& points);

// Join End Points: two end points of open subpaths. The two ends of one subpath close it; ends
// of two subpaths connect them into one (in the first one's place, group and op). `merge` (or
// two points at the same place) makes them one point at their midpoint; otherwise a straight
// segment joins them. nullopt when the points are not two such end points.
[[nodiscard]] std::optional<PathPointEditResult> join_path_end_points(const VectorPath& path, PathPointKey a,
                                                                      PathPointKey b, bool merge);

// Merge into One Point: two joinable end points merge (join_path_end_points with merge), or a row
// of two or more neighbouring points of one subpath collapses into one point at their average,
// keeping the outer handles. nullopt when the points are neither, or the subpath would keep fewer
// than two points.
[[nodiscard]] std::optional<PathPointEditResult> merge_path_points(const VectorPath& path,
                                                                   const std::set<PathPointKey>& points);

// The segments whose two end points are both listed (a Direct Select segment click selects both).
[[nodiscard]] std::set<PathPointKey> path_segments_between_points(const VectorPath& path,
                                                                  const std::set<PathPointKey>& points);

// Delete Segment: removes each listed segment and leaves the path open there. A closed subpath
// opens; an open one splits in two. A piece left with a single point goes too. nullopt when no
// listed segment exists.
[[nodiscard]] std::optional<PathPointEditResult> delete_path_segments(const VectorPath& path,
                                                                      const std::set<PathPointKey>& segments);

// --- Components (shape groups) ---

// The subpaths of `groups`, in path order, groups and ops kept.
[[nodiscard]] VectorPath path_components(const VectorPath& path, const std::vector<int>& groups);
// `path` without the subpaths of `groups`.
[[nodiscard]] VectorPath path_without_components(const VectorPath& path, const std::vector<int>& groups);
// Appends every subpath of `components` to `path` with fresh group numbers (from
// next_shape_group, one per source group, ops kept) and returns the source -> new group map.
std::map<int, int> append_path_components(VectorPath& path, const VectorPath& components);

// On a path that starts a new shape layer, a first component that subtracts would paint the whole
// canvas around itself: it paints its own area instead (Add). Later components keep their ops.
void make_first_component_paint(VectorPath& path);

// A shape layer's content restricted to `groups`: the path, live-shape origination and paint
// parts that paint them (parts left with no listed group go); appearance otherwise unchanged and
// the render caches empty. Used by Copy and Move to New Layer and the component clipboard.
[[nodiscard]] VectorShapeContent vector_shape_content_for_groups(const VectorShapeContent& content,
                                                                 const std::vector<int>& groups);

enum class ComponentColorRefusal : std::uint8_t {
  None,
  NoComponents,   // no listed group is in the path
  OnlyCutters,    // every listed component subtracts or intersects: it paints nothing itself
  SharedOverlap,  // its paint part combines components with Intersect or Exclude
  InvertedShape,  // the shape's path is inverted or disabled
};

// Color Selected Shapes: gives the listed components a solid `color` of their own. A shape
// without paint parts becomes a merged shape (docs/layer-merging.md) whose first part keeps the
// layer's paint for the rest; a part whose painting components are all listed just changes
// color. A listed component that only cuts (Subtract, Intersect) keeps its place in every part.
// Rendering is unchanged except for the color while each affected part combines its components
// with Add and Subtract only. `pattern_anchor` seeds a new base part's pattern reference point.
[[nodiscard]] ComponentColorRefusal recolor_vector_shape_components(VectorShapeContent& content,
                                                                    const std::vector<int>& groups,
                                                                    RgbColor color,
                                                                    std::array<double, 2> pattern_anchor = {});
// The refusal recolor_vector_shape_components would report, without changing anything.
[[nodiscard]] ComponentColorRefusal vector_shape_components_color_refusal(const VectorShapeContent& content,
                                                                          const std::vector<int>& groups);

}  // namespace patchy
