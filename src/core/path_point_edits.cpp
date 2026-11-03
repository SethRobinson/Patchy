#include "core/path_point_edits.hpp"

#include "core/path_fit.hpp"
#include "core/vector_raster.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <numeric>

namespace patchy {

namespace {

// Two end points this close are one place: joining them merges them instead of adding a
// zero-length segment.
constexpr double kSamePlace = 0.01;

void collapse_in(PathAnchor& anchor) {
  anchor.in_x = anchor.anchor_x;
  anchor.in_y = anchor.anchor_y;
  anchor.smooth = false;
}

void collapse_out(PathAnchor& anchor) {
  anchor.out_x = anchor.anchor_x;
  anchor.out_y = anchor.anchor_y;
  anchor.smooth = false;
}

void reverse_subpath(PathSubpath& subpath) {
  std::reverse(subpath.anchors.begin(), subpath.anchors.end());
  for (auto& anchor : subpath.anchors) {
    std::swap(anchor.in_x, anchor.out_x);
    std::swap(anchor.in_y, anchor.out_y);
  }
}

void add_group(std::vector<int>& groups, int group) {
  if (std::find(groups.begin(), groups.end(), group) == groups.end()) {
    groups.push_back(group);
  }
}

bool valid_subpath(const VectorPath& path, int subpath) {
  return subpath >= 0 && subpath < static_cast<int>(path.subpaths.size());
}

bool valid_point(const VectorPath& path, PathPointKey key) {
  return valid_subpath(path, key.first) && key.second >= 0 &&
         key.second < static_cast<int>(path.subpaths[static_cast<std::size_t>(key.first)].anchors.size());
}

// The open pieces of `source` (an open anchor run) cut at the listed interior anchor indices (each
// becomes the end of one piece and the start of the next) and at the listed segment indices (the
// segment goes). Pieces with fewer than two anchors are dropped. A cut end loses the handle that
// drove the removed side; the run's own two ends keep theirs unless `cut_outer` (an opened loop).
std::vector<PathSubpath> cut_open_run(const PathSubpath& source, const std::set<int>& cut_anchors,
                                      const std::set<int>& cut_segments, bool cut_outer) {
  std::vector<PathSubpath> pieces;
  PathSubpath piece = source;
  piece.closed = false;
  piece.anchors.clear();
  const auto count = static_cast<int>(source.anchors.size());
  bool starts_at_cut = cut_outer;
  const auto finish = [&pieces, &piece, &starts_at_cut](bool ends_at_cut) {
    if (piece.anchors.size() >= 2) {
      if (starts_at_cut) {
        collapse_in(piece.anchors.front());
      }
      if (ends_at_cut) {
        collapse_out(piece.anchors.back());
      }
      pieces.push_back(piece);
    }
    piece.anchors.clear();
    starts_at_cut = true;
  };
  for (int i = 0; i < count; ++i) {
    piece.anchors.push_back(source.anchors[static_cast<std::size_t>(i)]);
    if (i == count - 1) {
      break;
    }
    if (cut_anchors.contains(i) && i > 0) {
      finish(true);
      piece.anchors.push_back(source.anchors[static_cast<std::size_t>(i)]);
    }
    if (cut_segments.contains(i)) {
      finish(true);
    }
  }
  finish(cut_outer);
  return pieces;
}

// The anchors of a closed subpath as an open run starting at `start` (the last anchor is the one
// before it), with index maps from old to new anchor positions.
PathSubpath rotated_open(const PathSubpath& source, int start) {
  PathSubpath result = source;
  result.closed = false;
  result.anchors.clear();
  const auto count = static_cast<int>(source.anchors.size());
  for (int i = 0; i < count; ++i) {
    result.anchors.push_back(source.anchors[static_cast<std::size_t>((start + i) % count)]);
  }
  return result;
}

// Replaces subpath `index` with `pieces`, shifting the selection keys of later subpaths.
void replace_subpath(PathPointEditResult& result, int index, std::vector<PathSubpath> pieces) {
  const auto added = static_cast<int>(pieces.size()) - 1;
  std::set<PathPointKey> shifted;
  for (const auto& key : result.selection) {
    if (key.first > index) {
      shifted.insert({key.first + added, key.second});
    } else if (key.first < index) {
      shifted.insert(key);
    }
  }
  result.selection = std::move(shifted);
  auto& subpaths = result.path.subpaths;
  subpaths.erase(subpaths.begin() + index);
  subpaths.insert(subpaths.begin() + index, std::make_move_iterator(pieces.begin()),
                  std::make_move_iterator(pieces.end()));
}

bool is_end_point(const PathSubpath& subpath, int anchor) {
  return !subpath.closed && subpath.anchors.size() >= 2 &&
         (anchor == 0 || anchor == static_cast<int>(subpath.anchors.size()) - 1);
}

PathAnchor merged_anchor(const PathAnchor& incoming, const PathAnchor& outgoing) {
  // `incoming` keeps its in-handle and `outgoing` its out-handle, both moved with the point.
  PathAnchor merged;
  merged.anchor_x = (incoming.anchor_x + outgoing.anchor_x) / 2.0;
  merged.anchor_y = (incoming.anchor_y + outgoing.anchor_y) / 2.0;
  merged.in_x = incoming.in_x + merged.anchor_x - incoming.anchor_x;
  merged.in_y = incoming.in_y + merged.anchor_y - incoming.anchor_y;
  merged.out_x = outgoing.out_x + merged.anchor_x - outgoing.anchor_x;
  merged.out_y = outgoing.out_y + merged.anchor_y - outgoing.anchor_y;
  merged.smooth = false;
  return merged;
}

bool same_place(const PathAnchor& a, const PathAnchor& b) {
  return std::hypot(a.anchor_x - b.anchor_x, a.anchor_y - b.anchor_y) < kSamePlace;
}

// The listed points of one subpath form one run of neighbours: its first index along the
// subpath (cyclic on a closed one) and its length, or nullopt.
std::optional<std::pair<int, int>> neighbour_run(const PathSubpath& subpath, const std::set<int>& indices) {
  const auto count = static_cast<int>(subpath.anchors.size());
  const auto length = static_cast<int>(indices.size());
  if (length < 2 || length > count) {
    return std::nullopt;
  }
  for (const int start : indices) {
    const int previous = start - 1;
    const bool has_previous =
        subpath.closed ? indices.contains((previous + count) % count) : previous >= 0 && indices.contains(previous);
    if (has_previous && length < count) {
      continue;  // not the run's first point
    }
    int run = 0;
    while (run < length) {
      const int index = start + run;
      if (!subpath.closed && index >= count) {
        break;
      }
      if (!indices.contains(index % count)) {
        break;
      }
      ++run;
    }
    if (run == length) {
      return std::pair{start, length};
    }
    return std::nullopt;
  }
  return std::nullopt;
}

int group_op_index(const VectorPath& path, int group) {
  for (std::size_t i = 0; i < path.subpaths.size(); ++i) {
    if (path.subpaths[i].shape_group == group) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

}  // namespace

std::optional<PathPointEditResult> break_path_at_points(const VectorPath& path,
                                                        const std::set<PathPointKey>& points) {
  std::map<int, std::set<int>> by_subpath;
  for (const auto& key : points) {
    if (!valid_point(path, key)) {
      continue;
    }
    const auto& subpath = path.subpaths[static_cast<std::size_t>(key.first)];
    if (subpath.anchors.size() < 2 || is_end_point(subpath, key.second)) {
      continue;
    }
    by_subpath[key.first].insert(key.second);
  }
  if (by_subpath.empty()) {
    return std::nullopt;
  }
  PathPointEditResult result{path, {}, {}};
  // Last subpath first, so the indices of earlier ones stay valid.
  for (auto it = by_subpath.rbegin(); it != by_subpath.rend(); ++it) {
    const auto& source = path.subpaths[static_cast<std::size_t>(it->first)];
    std::vector<PathSubpath> pieces;
    if (source.closed) {
      // Opening at the first listed anchor makes it both ends; the rest cut the open run.
      const int start = *it->second.begin();
      const auto count = static_cast<int>(source.anchors.size());
      auto run = rotated_open(source, start);
      run.anchors.push_back(source.anchors[static_cast<std::size_t>(start)]);
      std::set<int> cuts;
      for (const int index : it->second) {
        if (index != start) {
          cuts.insert((index - start + count) % count);
        }
      }
      pieces = cut_open_run(run, cuts, {}, true);
    } else {
      pieces = cut_open_run(source, it->second, {}, false);
    }
    add_group(result.touched_groups, source.shape_group);
    // Every end point a break made stays selected (an open subpath's own two ends do not).
    const auto piece_count = static_cast<int>(pieces.size());
    std::vector<PathPointKey> ends;
    for (int p = 0; p < piece_count; ++p) {
      if (source.closed || p > 0) {
        ends.push_back({it->first + p, 0});
      }
      if (source.closed || p + 1 < piece_count) {
        ends.push_back({it->first + p, static_cast<int>(pieces[static_cast<std::size_t>(p)].anchors.size()) - 1});
      }
    }
    replace_subpath(result, it->first, std::move(pieces));
    result.selection.insert(ends.begin(), ends.end());
  }
  return result;
}

std::optional<PathPointEditResult> join_path_end_points(const VectorPath& path, PathPointKey a, PathPointKey b,
                                                        bool merge) {
  if (a == b || !valid_point(path, a) || !valid_point(path, b)) {
    return std::nullopt;
  }
  if (b < a) {
    std::swap(a, b);
  }
  const auto& first = path.subpaths[static_cast<std::size_t>(a.first)];
  const auto& second = path.subpaths[static_cast<std::size_t>(b.first)];
  if (!is_end_point(first, a.second) || !is_end_point(second, b.second)) {
    return std::nullopt;
  }
  PathPointEditResult result{path, {}, {}};
  if (a.first == b.first) {
    // The two ends of one subpath: close it.
    auto subpath = first;
    auto& start = subpath.anchors.front();
    auto& end = subpath.anchors.back();
    if (merge || same_place(start, end)) {
      if (subpath.anchors.size() < 3) {
        return std::nullopt;  // one point cannot make a closed shape
      }
      subpath.anchors.front() = merged_anchor(end, start);
      subpath.anchors.pop_back();
      result.selection = {{a.first, 0}};
    } else {
      collapse_in(start);
      collapse_out(end);
      result.selection = {{a.first, 0}, {a.first, static_cast<int>(subpath.anchors.size()) - 1}};
    }
    subpath.closed = true;
    result.path.subpaths[static_cast<std::size_t>(a.first)] = std::move(subpath);
    result.touched_groups = {first.shape_group};
    return result;
  }
  // Ends of two subpaths: the first runs into its joined end, the second runs out of its own.
  auto joined = first;
  if (a.second == 0) {
    reverse_subpath(joined);
  }
  auto tail = second;
  if (b.second != 0) {
    reverse_subpath(tail);
  }
  const auto& end = joined.anchors.back();
  const auto& start = tail.anchors.front();
  if (merge || same_place(end, start)) {
    joined.anchors.back() = merged_anchor(end, start);
    joined.anchors.insert(joined.anchors.end(), tail.anchors.begin() + 1, tail.anchors.end());
    result.selection = {{a.first, static_cast<int>(first.anchors.size()) - 1}};
  } else {
    collapse_out(joined.anchors.back());
    collapse_in(tail.anchors.front());
    joined.anchors.insert(joined.anchors.end(), tail.anchors.begin(), tail.anchors.end());
    const auto seam = static_cast<int>(first.anchors.size());
    result.selection = {{a.first, seam - 1}, {a.first, seam}};
  }
  result.touched_groups = {first.shape_group};
  add_group(result.touched_groups, second.shape_group);
  result.path.subpaths[static_cast<std::size_t>(a.first)] = std::move(joined);
  result.path.subpaths.erase(result.path.subpaths.begin() + b.first);
  return result;
}

std::optional<PathPointEditResult> close_open_subpaths(const VectorPath& path,
                                                      const std::set<PathPointKey>& points) {
  std::set<int> subpaths;
  for (const auto& key : points) {
    if (valid_point(path, key)) {
      const auto& subpath = path.subpaths[static_cast<std::size_t>(key.first)];
      if (!subpath.closed && subpath.anchors.size() >= 3) {
        subpaths.insert(key.first);
      }
    }
  }
  if (subpaths.empty()) {
    return std::nullopt;
  }
  PathPointEditResult result{path, {}, {}};
  for (const int index : subpaths) {
    auto& subpath = result.path.subpaths[static_cast<std::size_t>(index)];
    auto& start = subpath.anchors.front();
    auto& end = subpath.anchors.back();
    if (same_place(start, end) && subpath.anchors.size() >= 4) {
      start = merged_anchor(end, start);
      subpath.anchors.pop_back();
    } else {
      collapse_in(start);
      collapse_out(end);
    }
    subpath.closed = true;
    add_group(result.touched_groups, subpath.shape_group);
    for (int a = 0; a < static_cast<int>(subpath.anchors.size()); ++a) {
      if (a == 0 || a == static_cast<int>(subpath.anchors.size()) - 1) {
        result.selection.insert({index, a});
      }
    }
  }
  return result;
}

std::optional<PathPointEditResult> merge_path_points(const VectorPath& path, const std::set<PathPointKey>& points) {
  if (points.size() == 2) {
    if (auto joined = join_path_end_points(path, *points.begin(), *std::next(points.begin()), true);
        joined.has_value()) {
      return joined;
    }
  }
  if (points.size() < 2 || !valid_point(path, *points.begin())) {
    return std::nullopt;
  }
  const int index = points.begin()->first;
  std::set<int> indices;
  for (const auto& key : points) {
    if (key.first != index || !valid_point(path, key)) {
      return std::nullopt;
    }
    indices.insert(key.second);
  }
  const auto& source = path.subpaths[static_cast<std::size_t>(index)];
  const auto count = static_cast<int>(source.anchors.size());
  const auto run = neighbour_run(source, indices);
  if (!run.has_value() || count - run->second + 1 < 2 || run->second >= count) {
    return std::nullopt;
  }
  const auto [start, length] = *run;
  const auto& first = source.anchors[static_cast<std::size_t>(start)];
  const auto& last = source.anchors[static_cast<std::size_t>((start + length - 1) % count)];
  PathAnchor merged;
  for (int i = 0; i < length; ++i) {
    const auto& anchor = source.anchors[static_cast<std::size_t>((start + i) % count)];
    merged.anchor_x += anchor.anchor_x / length;
    merged.anchor_y += anchor.anchor_y / length;
  }
  merged.in_x = first.in_x + merged.anchor_x - first.anchor_x;
  merged.in_y = first.in_y + merged.anchor_y - first.anchor_y;
  merged.out_x = last.out_x + merged.anchor_x - last.anchor_x;
  merged.out_y = last.out_y + merged.anchor_y - last.anchor_y;
  auto subpath = source;
  subpath.anchors.clear();
  int merged_index = 0;
  if (source.closed) {
    // The merged point first, then the anchors after the run around to the one before it.
    subpath.anchors.push_back(merged);
    for (int i = length; i < count; ++i) {
      subpath.anchors.push_back(source.anchors[static_cast<std::size_t>((start + i) % count)]);
    }
  } else {
    for (int i = 0; i < start; ++i) {
      subpath.anchors.push_back(source.anchors[static_cast<std::size_t>(i)]);
    }
    merged_index = start;
    subpath.anchors.push_back(merged);
    for (int i = start + length; i < count; ++i) {
      subpath.anchors.push_back(source.anchors[static_cast<std::size_t>(i)]);
    }
  }
  PathPointEditResult result{path, {source.shape_group}, {{index, merged_index}}};
  result.path.subpaths[static_cast<std::size_t>(index)] = std::move(subpath);
  return result;
}

std::set<PathPointKey> path_segments_between_points(const VectorPath& path, const std::set<PathPointKey>& points) {
  std::set<PathPointKey> segments;
  for (const auto& key : points) {
    if (!valid_point(path, key)) {
      continue;
    }
    const auto& subpath = path.subpaths[static_cast<std::size_t>(key.first)];
    const auto count = static_cast<int>(subpath.anchors.size());
    if (count < 2) {
      continue;
    }
    if (!subpath.closed && key.second == count - 1) {
      continue;  // an open subpath's last point starts no segment
    }
    if (points.contains({key.first, (key.second + 1) % count})) {
      segments.insert(key);
    }
  }
  return segments;
}

std::optional<PathPointEditResult> delete_path_segments(const VectorPath& path,
                                                        const std::set<PathPointKey>& segments) {
  std::map<int, std::set<int>> by_subpath;
  for (const auto& key : segments) {
    if (!valid_subpath(path, key.first)) {
      continue;
    }
    const auto& subpath = path.subpaths[static_cast<std::size_t>(key.first)];
    const auto count = static_cast<int>(subpath.anchors.size());
    const auto segment_count = subpath.closed ? count : count - 1;
    if (count >= 2 && key.second >= 0 && key.second < segment_count) {
      by_subpath[key.first].insert(key.second);
    }
  }
  if (by_subpath.empty()) {
    return std::nullopt;
  }
  PathPointEditResult result{path, {}, {}};
  for (auto it = by_subpath.rbegin(); it != by_subpath.rend(); ++it) {
    const auto& source = path.subpaths[static_cast<std::size_t>(it->first)];
    std::vector<PathSubpath> pieces;
    if (source.closed) {
      // Open the loop at the first deleted segment: the run starts after it and ends before it.
      const auto count = static_cast<int>(source.anchors.size());
      const int first = *it->second.begin();
      const int start = (first + 1) % count;
      const auto run = rotated_open(source, start);
      std::set<int> cuts;
      for (const int segment : it->second) {
        if (segment != first) {
          cuts.insert((segment - start + count) % count);
        }
      }
      pieces = cut_open_run(run, {}, cuts, true);
    } else {
      pieces = cut_open_run(source, {}, it->second, false);
    }
    add_group(result.touched_groups, source.shape_group);
    replace_subpath(result, it->first, std::move(pieces));
  }
  return result;
}

VectorPath path_components(const VectorPath& path, const std::vector<int>& groups) {
  VectorPath result;
  result.fill_rule_value = path.fill_rule_value;
  result.initial_fill_value = path.initial_fill_value;
  for (const auto& subpath : path.subpaths) {
    if (std::find(groups.begin(), groups.end(), subpath.shape_group) != groups.end()) {
      result.subpaths.push_back(subpath);
    }
  }
  return result;
}

VectorPath path_without_components(const VectorPath& path, const std::vector<int>& groups) {
  auto result = path;
  std::erase_if(result.subpaths, [&groups](const PathSubpath& subpath) {
    return std::find(groups.begin(), groups.end(), subpath.shape_group) != groups.end();
  });
  return result;
}

std::map<int, int> append_path_components(VectorPath& path, const VectorPath& components) {
  std::map<int, int> renumbered;
  auto next = path.next_shape_group();
  for (const auto& subpath : components.subpaths) {
    auto [found, inserted] = renumbered.try_emplace(subpath.shape_group, next);
    if (inserted) {
      ++next;
    }
    auto copy = subpath;
    copy.shape_group = found->second;
    path.subpaths.push_back(std::move(copy));
  }
  return renumbered;
}

namespace {

// A component's own fill outline (its subpaths filled as one Add component, the fill rule kept)
// on the 1/256 lattice as integers, with its bounds. Merge Shapes groups components by it.
struct ComponentOutline {
  using Point = std::array<std::int64_t, 2>;
  std::vector<std::vector<Point>> loops;
  Point min{0, 0};
  Point max{-1, -1};
  [[nodiscard]] bool empty() const { return loops.empty(); }
};

ComponentOutline component_outline(const VectorPath& path, int group) {
  VectorPath alone;
  alone.fill_rule_value = path.fill_rule_value;
  for (const auto& subpath : path.subpaths) {
    if (subpath.shape_group == group) {
      auto copy = subpath;
      copy.op = PathCombineOp::Add;
      alone.subpaths.push_back(std::move(copy));
    }
  }
  ComponentOutline outline;
  bool first = true;
  for (const auto& contour : vector_path_fill_contours(alone)) {
    if (contour.size() < 3) {
      continue;
    }
    std::vector<ComponentOutline::Point> loop;
    loop.reserve(contour.size());
    for (const auto& point : contour) {
      const ComponentOutline::Point lattice{std::llround(point[0] * 256.0), std::llround(point[1] * 256.0)};
      if (first) {
        outline.min = lattice;
        outline.max = lattice;
        first = false;
      }
      for (std::size_t axis = 0; axis < 2; ++axis) {
        outline.min[axis] = std::min(outline.min[axis], lattice[axis]);
        outline.max[axis] = std::max(outline.max[axis], lattice[axis]);
      }
      loop.push_back(lattice);
    }
    outline.loops.push_back(std::move(loop));
  }
  return outline;
}

int orientation(const ComponentOutline::Point& a, const ComponentOutline::Point& b, const ComponentOutline::Point& c) {
  const auto cross = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
  return cross > 0 ? 1 : (cross < 0 ? -1 : 0);
}

bool within_box(const ComponentOutline::Point& a, const ComponentOutline::Point& b, const ComponentOutline::Point& p) {
  return std::min(a[0], b[0]) <= p[0] && p[0] <= std::max(a[0], b[0]) && std::min(a[1], b[1]) <= p[1] &&
         p[1] <= std::max(a[1], b[1]);
}

// Segments that cross or touch (end points and collinear overlaps included).
bool segments_meet(const ComponentOutline::Point& a, const ComponentOutline::Point& b, const ComponentOutline::Point& c,
                   const ComponentOutline::Point& d) {
  const int o1 = orientation(a, b, c);
  const int o2 = orientation(a, b, d);
  const int o3 = orientation(c, d, a);
  const int o4 = orientation(c, d, b);
  if (o1 * o2 < 0 && o3 * o4 < 0) {
    return true;
  }
  return (o1 == 0 && within_box(a, b, c)) || (o2 == 0 && within_box(a, b, d)) || (o3 == 0 && within_box(c, d, a)) ||
         (o4 == 0 && within_box(c, d, b));
}

// Even-odd inside test against an outline's loops, for a point on none of them: the loops are
// one fill's boundary, so their parity is that fill.
bool inside_outline(const ComponentOutline& outline, const ComponentOutline::Point& point) {
  bool inside = false;
  for (const auto& loop : outline.loops) {
    for (std::size_t i = 0, j = loop.size() - 1; i < loop.size(); j = i++) {
      const auto& p = loop[j];
      const auto& q = loop[i];
      if ((p[1] > point[1]) != (q[1] > point[1])) {
        const auto cross = (q[0] - p[0]) * (point[1] - p[1]) - (point[0] - p[0]) * (q[1] - p[1]);
        if ((cross > 0) == (q[1] > p[1])) {
          inside = !inside;
        }
      }
    }
  }
  return inside;
}

// Whether two components' fills overlap or touch: their bounds meet, then an outline edge of one
// meets an edge of the other, or one lies inside the other (nested shapes count).
bool outlines_meet(const ComponentOutline& a, const ComponentOutline& b) {
  if (a.empty() || b.empty() || a.max[0] < b.min[0] || b.max[0] < a.min[0] || a.max[1] < b.min[1] ||
      b.max[1] < a.min[1]) {
    return false;
  }
  for (const auto& loop_a : a.loops) {
    for (std::size_t i = 0, j = loop_a.size() - 1; i < loop_a.size(); j = i++) {
      const auto& p = loop_a[j];
      const auto& q = loop_a[i];
      if (std::max(p[0], q[0]) < b.min[0] || std::min(p[0], q[0]) > b.max[0] || std::max(p[1], q[1]) < b.min[1] ||
          std::min(p[1], q[1]) > b.max[1]) {
        continue;
      }
      for (const auto& loop_b : b.loops) {
        for (std::size_t k = 0, l = loop_b.size() - 1; k < loop_b.size(); l = k++) {
          if (segments_meet(p, q, loop_b[l], loop_b[k])) {
            return true;
          }
        }
      }
    }
  }
  const auto any_loop_inside = [](const ComponentOutline& inner, const ComponentOutline& outer) {
    return std::any_of(inner.loops.begin(), inner.loops.end(),
                       [&outer](const auto& loop) { return inside_outline(outer, loop.front()); });
  };
  return any_loop_inside(a, b) || any_loop_inside(b, a);
}

// The listed groups (path order) split into clusters whose components overlap or touch, each in
// path order, clusters ordered by their first component. One cluster of them all when fewer than
// two are listed or any intersects (Intersect clears everything outside it, shapes it does not
// touch included).
std::vector<std::vector<int>> overlapping_component_clusters(const VectorPath& path, const std::vector<int>& ordered) {
  const auto count = ordered.size();
  const bool intersects =
      std::any_of(path.subpaths.begin(), path.subpaths.end(), [&ordered](const PathSubpath& subpath) {
        return subpath.op == PathCombineOp::Intersect &&
               std::find(ordered.begin(), ordered.end(), subpath.shape_group) != ordered.end();
      });
  if (count < 2 || intersects) {
    return {ordered};
  }
  std::vector<ComponentOutline> outlines;
  outlines.reserve(count);
  for (const int group : ordered) {
    outlines.push_back(component_outline(path, group));
  }
  std::vector<std::size_t> parent(count);
  std::iota(parent.begin(), parent.end(), std::size_t{0});
  const auto root = [&parent](std::size_t index) {
    while (parent[index] != index) {
      index = parent[index] = parent[parent[index]];
    }
    return index;
  };
  for (std::size_t i = 0; i < count; ++i) {
    for (std::size_t j = i + 1; j < count; ++j) {
      const auto ri = root(i);
      const auto rj = root(j);
      if (ri != rj && outlines_meet(outlines[i], outlines[j])) {
        parent[std::max(ri, rj)] = std::min(ri, rj);  // the root stays the cluster's first component
      }
    }
  }
  std::vector<std::vector<int>> clusters;
  std::vector<std::size_t> cluster_of(count, count);
  for (std::size_t i = 0; i < count; ++i) {
    const auto r = root(i);
    if (cluster_of[r] == count) {
      cluster_of[r] = clusters.size();
      clusters.emplace_back();
    }
    clusters[cluster_of[r]].push_back(ordered[i]);
  }
  return clusters;
}

PathCombineOp component_op(const VectorPath& path, int group) {
  for (const auto& subpath : path.subpaths) {
    if (subpath.shape_group == group) {
      return subpath.op;
    }
  }
  return PathCombineOp::Add;
}

// The combined fill of the listed groups (non-empty, path order), traced and refit into closed
// subpaths (group and op still to set): the components alone, the first as the base, or all
// painting when `all_cutters`.
std::vector<PathSubpath> traced_components(const VectorPath& path, const std::vector<int>& ordered, bool all_cutters,
                                           double tolerance) {
  // The listed components alone, consecutive and in order, the first as the base.
  VectorPath subset;
  for (const int group : ordered) {
    for (const auto& subpath : path.subpaths) {
      if (subpath.shape_group != group) {
        continue;
      }
      auto copy = subpath;
      if (all_cutters || group == ordered.front()) {
        copy.op = PathCombineOp::Add;
      }
      subset.subpaths.push_back(std::move(copy));
    }
  }
  PathFitOptions fit;
  fit.tolerance = tolerance;
  fit.corner_angle_degrees = 45.0;
  fit.snap_curves_to_lines = true;
  fit.refine_iterations = 8;
  std::vector<PathSubpath> merged;
  for (const auto& contour : vector_path_fill_contours(subset)) {
    std::vector<FitPoint> points;
    points.reserve(contour.size());
    for (const auto& point : contour) {
      points.push_back({point[0], point[1]});
    }
    auto subpath = fit_closed_loop(points, fit);
    if (subpath.anchors.size() < 2) {
      continue;
    }
    subpath.closed = true;
    merged.push_back(std::move(subpath));
  }
  return merged;
}

// `merged` (group and op set to the first listed one's) becomes the listed groups' one component,
// in the first one's place; the other listed groups go.
MergeComponentsResult place_merged_component(const VectorPath& path, const std::vector<int>& ordered,
                                             std::vector<PathSubpath> merged) {
  const auto first_op = component_op(path, ordered.front());
  for (auto& subpath : merged) {
    subpath.shape_group = ordered.front();
    subpath.op = first_op;
  }
  MergeComponentsResult result;
  result.merged_group = ordered.front();
  result.merged_subpaths = merged.size();
  if (!merged.empty()) {
    result.merged_groups.push_back(ordered.front());
  }
  result.removed_groups.assign(ordered.begin() + 1, ordered.end());
  result.path.fill_rule_value = path.fill_rule_value;
  result.path.initial_fill_value = path.initial_fill_value;
  bool placed = false;
  for (const auto& subpath : path.subpaths) {
    if (std::find(ordered.begin(), ordered.end(), subpath.shape_group) == ordered.end()) {
      result.path.subpaths.push_back(subpath);
      continue;
    }
    if (!placed) {
      placed = true;
      for (auto& piece : merged) {
        result.path.subpaths.push_back(std::move(piece));
      }
    }
  }
  return result;
}

// Merges the listed groups (non-empty, path order) into one traced component in the first one's place.
MergeComponentsResult merge_ordered_components(const VectorPath& path, const std::vector<int>& ordered,
                                               double tolerance) {
  const bool all_cutters = std::all_of(ordered.begin(), ordered.end(), [&path](int group) {
    return component_op(path, group) == PathCombineOp::Subtract;
  });
  return place_merged_component(path, ordered, traced_components(path, ordered, all_cutters, tolerance));
}

}  // namespace

std::optional<MergeComponentsResult> merge_path_components(const VectorPath& path, const std::vector<int>& groups,
                                                           double tolerance) {
  // The listed groups in path order.
  std::vector<int> ordered;
  for (const auto& subpath : path.subpaths) {
    if (std::find(groups.begin(), groups.end(), subpath.shape_group) != groups.end() &&
        std::find(ordered.begin(), ordered.end(), subpath.shape_group) == ordered.end()) {
      ordered.push_back(subpath.shape_group);
    }
  }
  if (ordered.empty()) {
    return std::nullopt;
  }
  const auto clusters = overlapping_component_clusters(path, ordered);
  if (clusters.size() == 1) {
    return merge_ordered_components(path, ordered, tolerance);
  }
  // Shapes apart from the others go into the merged shape untouched (#587): only clusters that
  // overlap are traced. The clusters' fills are apart, so the combined fill is theirs together,
  // each combined from nothing: a cluster's leading cutters cut nothing and go (the first listed
  // component is the base and paints, unless every one cuts).
  const bool all_cutters = std::all_of(ordered.begin(), ordered.end(), [&path](int group) {
    return component_op(path, group) == PathCombineOp::Subtract;
  });
  std::vector<PathSubpath> pieces;
  std::vector<int> kept;
  for (const auto& cluster : clusters) {
    std::vector<int> members = cluster;
    if (!all_cutters) {
      const auto first_painting = std::find_if(members.begin(), members.end(), [&](int group) {
        return group == ordered.front() || component_op(path, group) != PathCombineOp::Subtract;
      });
      members.erase(members.begin(), first_painting);
    }
    if (members.empty()) {
      continue;
    }
    if (members.size() == 1) {
      kept.push_back(members.front());
      for (const auto& subpath : path.subpaths) {
        if (subpath.shape_group == members.front()) {
          pieces.push_back(subpath);
        }
      }
      continue;
    }
    auto traced = traced_components(path, members, all_cutters, tolerance);
    std::move(traced.begin(), traced.end(), std::back_inserter(pieces));
  }
  auto result = place_merged_component(path, ordered, std::move(pieces));
  result.kept_groups = std::move(kept);
  return result;
}

namespace {

// One subpath's flattened outline (open ones closed by their chord) on the 1/256 lattice.
ComponentOutline subpath_outline(const PathSubpath& subpath) {
  ComponentOutline outline;
  const auto polyline = flatten_subpath_polyline(subpath);
  if (polyline.empty()) {
    return outline;
  }
  std::vector<ComponentOutline::Point> loop;
  loop.reserve(polyline.size());
  for (const auto& point : polyline) {
    const ComponentOutline::Point lattice{std::llround(point[0] * 256.0), std::llround(point[1] * 256.0)};
    if (loop.empty()) {
      outline.min = lattice;
      outline.max = lattice;
    }
    for (std::size_t axis = 0; axis < 2; ++axis) {
      outline.min[axis] = std::min(outline.min[axis], lattice[axis]);
      outline.max[axis] = std::max(outline.max[axis], lattice[axis]);
    }
    loop.push_back(lattice);
  }
  outline.loops.push_back(std::move(loop));
  return outline;
}

bool outline_edges_meet(const ComponentOutline& a, const ComponentOutline& b) {
  if (a.empty() || b.empty() || a.max[0] < b.min[0] || b.max[0] < a.min[0] || a.max[1] < b.min[1] ||
      b.max[1] < a.min[1]) {
    return false;
  }
  const auto& p = a.loops.front();
  const auto& q = b.loops.front();
  for (std::size_t i = 0, j = p.size() - 1; i < p.size(); j = i++) {
    const auto& p0 = p[j];
    const auto& p1 = p[i];
    if (std::max(p0[0], p1[0]) < b.min[0] || std::min(p0[0], p1[0]) > b.max[0] ||
        std::max(p0[1], p1[1]) < b.min[1] || std::min(p0[1], p1[1]) > b.max[1]) {
      continue;
    }
    for (std::size_t k = 0, l = q.size() - 1; k < q.size(); l = k++) {
      if (segments_meet(p0, p1, q[l], q[k])) {
        return true;
      }
    }
  }
  return false;
}

// The pieces of one component's subpaths (indices into `subpaths`), each piece in subpath order,
// pieces ordered by their first subpath.
std::vector<std::vector<std::size_t>> component_pieces(const std::vector<const PathSubpath*>& subpaths) {
  const auto count = subpaths.size();
  std::vector<ComponentOutline> outlines;
  outlines.reserve(count);
  for (const auto* subpath : subpaths) {
    outlines.push_back(subpath_outline(*subpath));
  }
  std::vector<std::size_t> parent(count);
  std::iota(parent.begin(), parent.end(), std::size_t{0});
  const auto root = [&parent](std::size_t index) {
    while (parent[index] != index) {
      index = parent[index] = parent[parent[index]];
    }
    return index;
  };
  const auto join = [&](std::size_t a, std::size_t b) {
    const auto ra = root(a);
    const auto rb = root(b);
    if (ra != rb) {
      parent[std::max(ra, rb)] = std::min(ra, rb);
    }
  };
  // contains[i][j]: subpath j lies inside subpath i (their outlines do not meet).
  std::vector<std::vector<bool>> contains(count, std::vector<bool>(count, false));
  for (std::size_t i = 0; i < count; ++i) {
    for (std::size_t j = i + 1; j < count; ++j) {
      if (outline_edges_meet(outlines[i], outlines[j])) {
        join(i, j);
        continue;
      }
      if (outlines[i].empty() || outlines[j].empty()) {
        continue;
      }
      if (outlines[i].loops.front().size() >= 3 && inside_outline(outlines[i], outlines[j].loops.front().front())) {
        contains[i][j] = true;
      } else if (outlines[j].loops.front().size() >= 3 &&
                 inside_outline(outlines[j], outlines[i].loops.front().front())) {
        contains[j][i] = true;
      }
    }
  }
  std::vector<std::size_t> depth(count, 0);
  for (std::size_t j = 0; j < count; ++j) {
    for (std::size_t i = 0; i < count; ++i) {
      depth[j] += contains[i][j] ? 1 : 0;
    }
  }
  // A hole (odd depth) goes with the outline right around it: its deepest container.
  for (std::size_t j = 0; j < count; ++j) {
    if (depth[j] % 2 == 0) {
      continue;
    }
    std::size_t around = count;
    for (std::size_t i = 0; i < count; ++i) {
      if (contains[i][j] && (around == count || depth[i] > depth[around])) {
        around = i;
      }
    }
    if (around != count) {
      join(j, around);
    }
  }
  std::vector<std::vector<std::size_t>> pieces;
  std::vector<std::size_t> piece_of(count, count);
  for (std::size_t i = 0; i < count; ++i) {
    const auto r = root(i);
    if (piece_of[r] == count) {
      piece_of[r] = pieces.size();
      pieces.emplace_back();
    }
    pieces[piece_of[r]].push_back(i);
  }
  return pieces;
}

}  // namespace

std::optional<SplitComponentsResult> split_path_components(const VectorPath& path, const std::vector<int>& groups) {
  SplitComponentsResult result;
  result.path.fill_rule_value = path.fill_rule_value;
  result.path.initial_fill_value = path.initial_fill_value;
  auto next_group = path.next_shape_group();
  std::set<int> done;
  for (const auto& subpath : path.subpaths) {
    const int group = subpath.shape_group;
    const bool listed = std::find(groups.begin(), groups.end(), group) != groups.end();
    if (!listed || component_op(path, group) == PathCombineOp::Intersect) {
      result.path.subpaths.push_back(subpath);
      continue;
    }
    if (!done.insert(group).second) {
      continue;  // placed with the component's first subpath
    }
    std::vector<const PathSubpath*> members;
    for (const auto& candidate : path.subpaths) {
      if (candidate.shape_group == group) {
        members.push_back(&candidate);
      }
    }
    const auto pieces = component_pieces(members);
    if (pieces.size() >= 2) {
      result.split_groups.push_back(group);
    }
    for (std::size_t piece = 0; piece < pieces.size(); ++piece) {
      int piece_group = group;
      if (piece > 0) {
        piece_group = static_cast<int>(next_group++);
        result.added_groups.emplace_back(group, piece_group);
      }
      if (pieces.size() >= 2) {
        result.pieces.push_back(piece_group);
      }
      for (const auto index : pieces[piece]) {
        auto copy = *members[index];
        copy.shape_group = piece_group;
        if (pieces.size() >= 2) {
          copy.op = subpath.op;  // the component's op: the rasterizer reads a run's first subpath
        }
        result.path.subpaths.push_back(std::move(copy));
      }
    }
  }
  if (result.split_groups.empty()) {
    return std::nullopt;
  }
  return result;
}

void make_first_component_paint(VectorPath& path) {
  if (path.subpaths.empty() || path.subpaths.front().op != PathCombineOp::Subtract) {
    return;
  }
  const auto group = path.subpaths.front().shape_group;
  for (auto& subpath : path.subpaths) {
    if (subpath.shape_group == group) {
      subpath.op = PathCombineOp::Add;
    }
  }
}

VectorShapeContent vector_shape_content_for_groups(const VectorShapeContent& content, const std::vector<int>& groups) {
  VectorShapeContent result = content;
  result.path = path_components(content.path, groups);
  std::erase_if(result.origination, [&groups](const LiveShapeParams& entry) {
    return std::find(groups.begin(), groups.end(), entry.index) == groups.end();
  });
  for (auto& part : result.parts) {
    std::erase_if(part.groups, [&groups](std::int32_t group) {
      return std::find(groups.begin(), groups.end(), group) == groups.end();
    });
  }
  std::erase_if(result.parts, [](const VectorShapePart& part) { return part.groups.empty() && !part.whole_canvas; });
  result.fill_cache = PixelBuffer{};
  result.stroke_cache = PixelBuffer{};
  result.effect_matte_cache = PixelBuffer{};
  return result;
}

namespace {

// The painting components of a part (its first, unless it subtracts, and every Add after it),
// with the part's groups in path order; empty `painting` with `blocked` when the part combines
// with Intersect or Exclude or starts with a Subtract.
struct PartGroups {
  std::vector<int> ordered;
  std::vector<int> painting;
  bool blocked{false};
};

PartGroups part_groups(const VectorPath& path, const std::vector<std::int32_t>& groups) {
  PartGroups result;
  for (const auto& subpath : path.subpaths) {
    if (std::find(groups.begin(), groups.end(), subpath.shape_group) != groups.end() &&
        std::find(result.ordered.begin(), result.ordered.end(), subpath.shape_group) == result.ordered.end()) {
      result.ordered.push_back(subpath.shape_group);
    }
  }
  for (std::size_t i = 0; i < result.ordered.size(); ++i) {
    const auto op = path.subpaths[static_cast<std::size_t>(group_op_index(path, result.ordered[i]))].op;
    if (i == 0) {
      if (op == PathCombineOp::Subtract) {
        result.blocked = true;
      } else {
        result.painting.push_back(result.ordered[i]);
      }
    } else if (op == PathCombineOp::Add) {
      result.painting.push_back(result.ordered[i]);
    } else if (op != PathCombineOp::Subtract) {
      result.blocked = true;
    }
  }
  return result;
}

bool listed(const std::vector<int>& groups, int group) {
  return std::find(groups.begin(), groups.end(), group) != groups.end();
}

std::vector<VectorShapePart> parts_or_base(const VectorShapeContent& content, std::array<double, 2> anchor) {
  if (!content.parts.empty()) {
    return content.parts;
  }
  VectorShapePart base;
  for (const auto& subpath : content.path.subpaths) {
    if (std::find(base.groups.begin(), base.groups.end(), subpath.shape_group) == base.groups.end()) {
      base.groups.push_back(subpath.shape_group);
    }
  }
  base.fill = content.fill;
  base.stroke = content.stroke;
  base.pattern_anchor = anchor;
  return {std::move(base)};
}

}  // namespace

ComponentColorRefusal vector_shape_components_color_refusal(const VectorShapeContent& content,
                                                            const std::vector<int>& groups) {
  auto copy = content;  // the pixel caches share their bytes until written
  return recolor_vector_shape_components(copy, groups, RgbColor{0, 0, 0});
}

ComponentColorRefusal recolor_vector_shape_components(VectorShapeContent& content, const std::vector<int>& groups,
                                                      RgbColor color, std::array<double, 2> pattern_anchor) {
  if (std::none_of(content.path.subpaths.begin(), content.path.subpaths.end(),
                   [&groups](const PathSubpath& subpath) { return listed(groups, subpath.shape_group); })) {
    return ComponentColorRefusal::NoComponents;
  }
  if (content.path_disabled || content.path_inverted) {
    return ComponentColorRefusal::InvertedShape;
  }
  auto parts = parts_or_base(content, pattern_anchor);
  std::vector<VectorShapePart> result;
  bool painted = false;
  for (auto& part : parts) {
    if (part.whole_canvas || part.path_disabled || part.path_inverted) {
      result.push_back(part);
      continue;
    }
    const auto ordered = part_groups(content.path, part.groups);
    std::vector<int> chosen;
    for (const int group : ordered.painting) {
      if (listed(groups, group)) {
        chosen.push_back(group);
      }
    }
    const bool touched = std::any_of(ordered.ordered.begin(), ordered.ordered.end(),
                                     [&groups](int group) { return listed(groups, group); });
    if (touched && ordered.blocked) {
      return ComponentColorRefusal::SharedOverlap;
    }
    if (chosen.empty()) {
      result.push_back(part);  // untouched, or only its cutters are listed
      continue;
    }
    painted = true;
    VectorFill fill;
    fill.kind = VectorFillKind::Solid;
    fill.color = color;
    if (chosen.size() == ordered.painting.size()) {
      part.fill = fill;
      result.push_back(part);
      continue;
    }
    // Split: the chosen components and the cutters after the first of them get their own part;
    // the rest keep the part, without cutters ahead of its first remaining painting component.
    const auto first_chosen = std::find(ordered.ordered.begin(), ordered.ordered.end(), chosen.front());
    VectorShapePart own = part;
    own.fill = fill;
    own.groups.clear();
    for (auto it = first_chosen; it != ordered.ordered.end(); ++it) {
      if (listed(chosen, *it) || !listed(ordered.painting, *it)) {
        own.groups.push_back(*it);
      }
    }
    VectorShapePart rest = part;
    rest.groups.clear();
    bool started = false;
    for (const int group : ordered.ordered) {
      if (listed(chosen, group)) {
        continue;
      }
      const bool paints = listed(ordered.painting, group);
      started = started || paints;
      if (started) {
        rest.groups.push_back(group);
      }
    }
    result.push_back(std::move(rest));
    result.push_back(std::move(own));
  }
  if (!painted) {
    return ComponentColorRefusal::OnlyCutters;
  }
  if (content.parts.empty() && result.size() == 1) {
    content.fill = result.front().fill;  // every painting component listed: a plain fill change
    return ComponentColorRefusal::None;
  }
  content.parts = std::move(result);
  return ComponentColorRefusal::None;
}

}  // namespace patchy
