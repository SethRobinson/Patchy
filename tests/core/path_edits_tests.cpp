// GitHub #555 follow-up: point and segment edits must render without artifacts (the spikes Break
// Path at Point left on several selected points), and Merge Shapes bakes the combine ops into one
// outline (core/path_point_edits, core/path_boolean).
#include "core/path_point_edits.hpp"
#include "core/splitmix64.hpp"
#include "core/vector_raster.hpp"
#include "core/vector_shape.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

using patchy::PathAnchor;
using patchy::PathCombineOp;
using patchy::PathSubpath;
using patchy::VectorPath;

PathSubpath polygon(std::vector<std::pair<double, double>> points, bool closed = true, int group = 0,
                    PathCombineOp op = PathCombineOp::Add) {
  PathSubpath subpath;
  subpath.closed = closed;
  subpath.shape_group = group;
  subpath.op = op;
  for (const auto& [x, y] : points) {
    PathAnchor anchor;
    anchor.anchor_x = anchor.in_x = anchor.out_x = x;
    anchor.anchor_y = anchor.in_y = anchor.out_y = y;
    subpath.anchors.push_back(anchor);
  }
  return subpath;
}

int coverage_at(const patchy::CoverageBuffer& coverage, int x, int y) {
  const auto& b = coverage.bounds;
  if (coverage.pixels.empty() || x < b.x || y < b.y || x >= b.x + b.width || y >= b.y + b.height) {
    return 0;
  }
  return coverage.pixels.pixel(x - b.x, y - b.y)[0];
}

// Covered pixels (coverage above 8) farther than `reach` from every point of the polygon's outline
// or outside it: the artifacts the spikes were.
int stray_pixels(const patchy::CoverageBuffer& coverage, const std::vector<std::pair<double, double>>& outline,
                 double reach) {
  const auto inside = [&outline](double x, double y) {
    bool result = false;
    for (std::size_t i = 0, j = outline.size() - 1; i < outline.size(); j = i++) {
      const auto [xi, yi] = outline[i];
      const auto [xj, yj] = outline[j];
      if ((yi > y) != (yj > y) && x < xi + (y - yi) * (xj - xi) / (yj - yi)) {
        result = !result;
      }
    }
    return result;
  };
  const auto distance = [&outline](double x, double y) {
    double best = 1e9;
    for (std::size_t i = 0, j = outline.size() - 1; i < outline.size(); j = i++) {
      const auto [ax, ay] = outline[j];
      const auto [bx, by] = outline[i];
      const double dx = bx - ax;
      const double dy = by - ay;
      const double t = std::clamp(((x - ax) * dx + (y - ay) * dy) / std::max(1e-12, dx * dx + dy * dy), 0.0, 1.0);
      best = std::min(best, std::hypot(x - ax - t * dx, y - ay - t * dy));
    }
    return best;
  };
  int stray = 0;
  const auto& b = coverage.bounds;
  for (int y = b.y; y < b.y + b.height; ++y) {
    for (int x = b.x; x < b.x + b.width; ++x) {
      const double cx = x + 0.5;
      const double cy = y + 0.5;
      if (coverage_at(coverage, x, y) > 8 && !inside(cx, cy) && distance(cx, cy) > reach) {
        ++stray;
      }
    }
  }
  return stray;
}

const std::vector<std::pair<double, double>> kQuad{{115, 333}, {278, 85}, {348, 60}, {348, 333}};

// Seth's report: a filled, stroked quad with several points selected for Break Path at Point
// grew long spikes along its edges. Fill and stroke (every alignment, miter joins at the default
// limit) must stay on the shape for one, several and all points, on the closed and the open path.
void path_break_at_points_renders_without_spikes() {
  const patchy::Rect canvas{0, 0, 500, 450};
  VectorPath closed;
  closed.subpaths = {polygon(kQuad)};
  VectorPath open_path;
  open_path.subpaths = {polygon(kQuad, false)};
  const std::vector<std::set<patchy::PathPointKey>> selections{
      {{0, 1}}, {{0, 1}, {0, 2}}, {{0, 0}, {0, 2}}, {{0, 0}, {0, 1}, {0, 2}, {0, 3}}};
  for (const auto* source : {&closed, &open_path}) {
    for (const auto& selection : selections) {
      const auto broken = patchy::break_path_at_points(*source, selection);
      if (!broken.has_value()) {
        continue;  // an open path's own ends do not break
      }
      const auto fill = patchy::rasterize_vector_path(broken->path, {canvas});
      CHECK(stray_pixels(fill, kQuad, 1.5) == 0);
      for (const auto alignment : {patchy::VectorStrokeAlignment::Inside, patchy::VectorStrokeAlignment::Center,
                                   patchy::VectorStrokeAlignment::Outside}) {
        patchy::VectorStroke stroke;
        stroke.enabled = true;
        stroke.width = 6.0;
        stroke.alignment = alignment;
        stroke.join = patchy::VectorStrokeJoin::Miter;
        const auto band = patchy::rasterize_vector_stroke(broken->path, stroke, {canvas});
        // A miter at the quad's sharpest corner reaches about 2.5 widths out; the spikes reached
        // 100 px and more.
        const int stray = stray_pixels(band, kQuad, 3.0 * stroke.width + 1.5);
        if (stray != 0) {
          std::cerr << "break spikes: selection " << selection.size() << " closed " << source->subpaths[0].closed
                    << " alignment " << static_cast<int>(alignment) << " stray " << stray << " bounds "
                    << band.bounds.x << "," << band.bounds.y << " " << band.bounds.width << "x"
                    << band.bounds.height << "\n";
        }
        CHECK(stray == 0);
      }
    }
  }
}


double area_of(const patchy::CoverageBuffer& coverage) {
  double total = 0.0;
  const auto& b = coverage.bounds;
  for (int y = b.y; y < b.y + b.height; ++y) {
    for (int x = b.x; x < b.x + b.width; ++x) {
      total += coverage_at(coverage, x, y) / 255.0;
    }
  }
  return total;
}

// Pixels whose coverage differs by more than `slack` (0..255) between the two renders.
int coverage_mismatch(const VectorPath& a, const VectorPath& b, patchy::Rect clip, int slack) {
  const auto first = patchy::rasterize_vector_path(a, {clip});
  const auto second = patchy::rasterize_vector_path(b, {clip});
  int mismatched = 0;
  for (int y = clip.y; y < clip.y + clip.height; ++y) {
    for (int x = clip.x; x < clip.x + clip.width; ++x) {
      if (std::abs(coverage_at(first, x, y) - coverage_at(second, x, y)) > slack) {
        ++mismatched;
      }
    }
  }
  return mismatched;
}

std::vector<int> groups_of(const VectorPath& path) {
  std::vector<int> groups;
  for (const auto& subpath : path.subpaths) {
    if (std::find(groups.begin(), groups.end(), subpath.shape_group) == groups.end()) {
      groups.push_back(subpath.shape_group);
    }
  }
  return groups;
}

PathSubpath circle(double cx, double cy, double r, int group, PathCombineOp op = PathCombineOp::Add) {
  constexpr double k = 0.5522847498;
  PathSubpath subpath;
  subpath.shape_group = group;
  subpath.op = op;
  const double points[4][2] = {{cx + r, cy}, {cx, cy + r}, {cx - r, cy}, {cx, cy - r}};
  const double tangents[4][2] = {{0, 1}, {-1, 0}, {0, -1}, {1, 0}};
  for (int i = 0; i < 4; ++i) {
    PathAnchor anchor;
    anchor.anchor_x = points[i][0];
    anchor.anchor_y = points[i][1];
    anchor.in_x = points[i][0] - tangents[i][0] * r * k;
    anchor.in_y = points[i][1] - tangents[i][1] * r * k;
    anchor.out_x = points[i][0] + tangents[i][0] * r * k;
    anchor.out_y = points[i][1] + tangents[i][1] * r * k;
    anchor.smooth = true;
    subpath.anchors.push_back(anchor);
  }
  return subpath;
}

// Merge Shapes bakes the combine ops: the merged outline renders like the live combine (to the
// edge antialiasing) and is one component of plain subpaths.
void path_merge_components_matches_the_live_combine() {
  const patchy::Rect clip{0, 0, 260, 220};
  struct Case {
    const char* name;
    VectorPath path;
    std::size_t subpaths;  // of the merged component
  };
  std::vector<Case> cases;
  {
    VectorPath p;  // two overlapping rectangles: one outline with eight corners
    p.subpaths = {polygon({{20, 20}, {120, 20}, {120, 100}, {20, 100}}),
                  polygon({{80, 60}, {200, 60}, {200, 160}, {80, 160}}, true, 1)};
    cases.push_back({"overlap", p, 1});
  }
  {
    VectorPath p;  // a hole: outer loop and its hole
    p.subpaths = {polygon({{20, 20}, {220, 20}, {220, 180}, {20, 180}}),
                  polygon({{80, 60}, {160, 60}, {160, 140}, {80, 140}}, true, 1, PathCombineOp::Subtract)};
    cases.push_back({"hole", p, 2});
  }
  {
    VectorPath p;  // curves: two circles united
    p.subpaths = {circle(90, 110, 60, 0), circle(160, 110, 60, 1)};
    cases.push_back({"circles", p, 1});
  }
  {
    VectorPath p;  // nested: a smaller square inside a bigger one adds nothing
    p.subpaths = {polygon({{20, 20}, {200, 20}, {200, 200}, {20, 200}}),
                  polygon({{60, 60}, {120, 60}, {120, 120}, {60, 120}}, true, 1)};
    cases.push_back({"nested", p, 1});
  }
  {
    VectorPath p;  // touching edges: two squares side by side become one rectangle
    p.subpaths = {polygon({{20, 40}, {100, 40}, {100, 120}, {20, 120}}),
                  polygon({{100, 40}, {180, 40}, {180, 120}, {100, 120}}, true, 1)};
    cases.push_back({"touching", p, 1});
  }
  {
    VectorPath p;  // intersect
    p.subpaths = {circle(100, 110, 70, 0),
                  polygon({{100, 20}, {240, 20}, {240, 200}, {100, 200}}, true, 1, PathCombineOp::Intersect)};
    cases.push_back({"intersect", p, 1});
  }
  {
    VectorPath p;  // exclude: two pieces where they do not overlap
    p.subpaths = {polygon({{20, 20}, {140, 20}, {140, 140}, {20, 140}}),
                  polygon({{80, 80}, {200, 80}, {200, 200}, {80, 200}}, true, 1, PathCombineOp::Xor)};
    cases.push_back({"exclude", p, 2});
  }
  for (const auto& test : cases) {
    const auto merged = patchy::merge_path_components(test.path, groups_of(test.path));
    CHECK(merged.has_value());
    if (!merged.has_value()) {
      continue;
    }
    CHECK(groups_of(merged->path).size() == 1);
    if (merged->merged_subpaths != test.subpaths) {
      std::cerr << test.name << ": " << merged->merged_subpaths << " subpaths\n";
    }
    CHECK(merged->merged_subpaths == test.subpaths);
    const int mismatch = coverage_mismatch(test.path, merged->path, clip, 40);
    if (mismatch > 6) {
      std::cerr << test.name << ": " << mismatch << " pixels differ\n";
    }
    CHECK(mismatch <= 6);
    const double before = area_of(patchy::rasterize_vector_path(test.path, {clip}));
    const double after = area_of(patchy::rasterize_vector_path(merged->path, {clip}));
    CHECK(std::abs(before - after) <= 0.01 * before + 4.0);
  }
  // Straight outlines stay straight: the overlap is eight corners with no curve handles.
  const auto overlap = patchy::merge_path_components(cases[0].path, {0, 1});
  CHECK(overlap->path.subpaths[0].anchors.size() == 8);
  for (const auto& anchor : overlap->path.subpaths[0].anchors) {
    CHECK(anchor.in_x == anchor.anchor_x && anchor.in_y == anchor.anchor_y);
    CHECK(anchor.out_x == anchor.anchor_x && anchor.out_y == anchor.anchor_y);
  }
  // The touching squares lose their shared edge: four corners.
  CHECK(patchy::merge_path_components(cases[4].path, {0, 1})->path.subpaths[0].anchors.size() == 4);
  // Deterministic: the same input merges to the same values.
  CHECK(patchy::merge_path_components(cases[2].path, {0, 1})->path ==
        patchy::merge_path_components(cases[2].path, {0, 1})->path);
}

// A subset merges in the first one's place, op and group; other components stay; cutters merge
// as one cutout; shapes that miss each other under Intersect leave nothing.
void path_merge_components_keeps_the_rest_and_handles_cutters() {
  VectorPath path;
  path.subpaths = {polygon({{0, 0}, {200, 0}, {200, 200}, {0, 200}}, true, 0),
                   polygon({{20, 20}, {60, 20}, {60, 60}, {20, 60}}, true, 1, PathCombineOp::Subtract),
                   polygon({{40, 40}, {90, 40}, {90, 90}, {40, 90}}, true, 2, PathCombineOp::Subtract),
                   polygon({{300, 0}, {340, 0}, {340, 40}, {300, 40}}, true, 3)};
  const auto cutters = patchy::merge_path_components(path, {1, 2});
  CHECK(cutters.has_value());
  CHECK((groups_of(cutters->path) == std::vector<int>{0, 1, 3}));
  CHECK(cutters->path.subpaths[1].op == PathCombineOp::Subtract && cutters->merged_subpaths == 1);
  CHECK((cutters->removed_groups == std::vector<int>{2}));
  CHECK(coverage_mismatch(path, cutters->path, {0, 0, 360, 220}, 40) <= 4);
  // Disjoint shapes intersected: nothing left of them.
  VectorPath apart;
  apart.subpaths = {polygon({{0, 0}, {20, 0}, {20, 20}, {0, 20}}),
                    polygon({{50, 0}, {70, 0}, {70, 20}, {50, 20}}, true, 1, PathCombineOp::Intersect)};
  const auto nothing = patchy::merge_path_components(apart, {0, 1});
  CHECK(nothing.has_value() && nothing->path.subpaths.empty() && nothing->merged_subpaths == 0);
  CHECK(!patchy::merge_path_components(apart, {7}).has_value());
  // A self-crossing bow tie merges into its two clean triangles.
  VectorPath bow;
  bow.subpaths = {polygon({{0, 0}, {100, 100}, {100, 0}, {0, 100}})};
  const auto clean = patchy::merge_path_components(bow, {0});
  CHECK(clean.has_value() && clean->merged_subpaths == 2);
}


// A seeded sweep (#555 follow-up): random convex and concave polygons, some with curved handles,
// random point selections, every point command; the results render inside the source's hull
// (grown by the stroke and its miters) for fill and every stroke alignment.
void path_point_commands_sweep_renders_inside_the_shape() {
  std::uint64_t state = 555;
  const auto next = [&state](int range) {
    state = patchy::splitmix64(state);
    return static_cast<int>((state >> 33U) % static_cast<std::uint64_t>(range));
  };
  const patchy::Rect canvas{0, 0, 400, 400};
  int failures = 0;
  for (int round = 0; round < 60; ++round) {
    const int count = 3 + next(5);
    std::vector<std::pair<double, double>> points;
    for (int i = 0; i < count; ++i) {
      const double angle = 6.283185307179586 * i / count;
      const double radius = 60.0 + next(100);
      points.push_back({200.0 + radius * std::cos(angle), 200.0 + radius * std::sin(angle)});
    }
    VectorPath path;
    path.subpaths = {polygon(points, next(4) != 0)};
    if (next(3) == 0) {
      // Smooth handles along the neighbour direction, as Convert Point makes them.
      auto& anchors = path.subpaths[0].anchors;
      for (int i = 0; i < count; ++i) {
        const auto& before = anchors[static_cast<std::size_t>((i + count - 1) % count)];
        const auto& after = anchors[static_cast<std::size_t>((i + 1) % count)];
        auto& anchor = anchors[static_cast<std::size_t>(i)];
        const double tx = (after.anchor_x - before.anchor_x) / 6.0;
        const double ty = (after.anchor_y - before.anchor_y) / 6.0;
        anchor.in_x = anchor.anchor_x - tx;
        anchor.in_y = anchor.anchor_y - ty;
        anchor.out_x = anchor.anchor_x + tx;
        anchor.out_y = anchor.anchor_y + ty;
        anchor.smooth = true;
      }
    }
    // Some layers hold a second, untouched shape (the multi-group stroke path).
    if (next(2) == 0) {
      path.subpaths.push_back(polygon({{20, 20}, {60, 20}, {60, 60}, {20, 60}}, true, 1));
    }
    std::set<patchy::PathPointKey> selection;
    const int picks = 1 + next(count);
    for (int i = 0; i < picks; ++i) {
      selection.insert({0, next(count)});
    }
    // The box of the source, controls included: nothing may render outside it (plus the stroke).
    const auto bounds = path.bounds();  // both shapes: the box spans them
    const std::vector<std::pair<double, double>> box{{bounds->left, bounds->top},
                                                     {bounds->right, bounds->top},
                                                     {bounds->right, bounds->bottom},
                                                     {bounds->left, bounds->bottom}};
    std::vector<std::optional<patchy::PathPointEditResult>> results{
        patchy::break_path_at_points(path, selection), patchy::merge_path_points(path, selection),
        patchy::delete_path_segments(path, patchy::path_segments_between_points(path, selection))};
    if (selection.size() == 2) {
      results.push_back(patchy::join_path_end_points(path, *selection.begin(), *std::next(selection.begin()), false));
    }
    for (std::size_t r = 0; r < results.size(); ++r) {
      if (!results[r].has_value()) {
        continue;
      }
      const auto& edited = results[r]->path;
      for (const auto& subpath : edited.subpaths) {
        CHECK(subpath.anchors.size() >= 2);
      }
      // Break, Delete Segment and Join keep the source's points and handles; Merge into One Point
      // moves handles with the merged point, so it is held to its own control box.
      auto own_box = box;
      if (r == 1) {
        const auto edited_bounds = edited.bounds();
        own_box = {{edited_bounds->left, edited_bounds->top},
                   {edited_bounds->right, edited_bounds->top},
                   {edited_bounds->right, edited_bounds->bottom},
                   {edited_bounds->left, edited_bounds->bottom}};
      }
      const auto fill = patchy::rasterize_vector_path(edited, {canvas});
      int stray = stray_pixels(fill, own_box, 1.5);
      for (const auto alignment : {patchy::VectorStrokeAlignment::Inside, patchy::VectorStrokeAlignment::Center,
                                   patchy::VectorStrokeAlignment::Outside}) {
        patchy::VectorStroke stroke;
        stroke.enabled = true;
        stroke.width = 4.0;
        stroke.alignment = alignment;
        stroke.join = patchy::VectorStrokeJoin::Bevel;  // miters may legitimately reach far
        // An open path's off-center stroke offsets its corners up to four half-widths out (the
        // Photoshop-measured rule in offset_open_polyline), so the reach is that plus the band.
        stray += stray_pixels(patchy::rasterize_vector_stroke(edited, stroke, {canvas}), own_box,
                              2.5 * stroke.width + 1.5);
      }
      if (stray != 0) {
        ++failures;
        std::cerr << "sweep round " << round << " command " << r << " points " << count << " picks " << picks
                  << " closed " << path.subpaths[0].closed << " stray " << stray << "\n";
      }
    }
  }
  CHECK(failures == 0);
}


// Join End Points without two end points closes each open path with a selected point: Break at
// two opposite corners, then Join, splits a rectangle into two closed triangles.
void path_close_open_subpaths_splits_a_shape_in_two() {
  VectorPath rect;
  rect.subpaths = {polygon({{0, 0}, {100, 0}, {100, 60}, {0, 60}})};
  const auto broken = patchy::break_path_at_points(rect, {{0, 0}, {0, 2}});
  CHECK(broken.has_value() && broken->path.subpaths.size() == 2);
  const auto closed = patchy::close_open_subpaths(broken->path, broken->selection);
  CHECK(closed.has_value() && closed->path.subpaths.size() == 2);
  for (const auto& subpath : closed->path.subpaths) {
    CHECK(subpath.closed && subpath.anchors.size() == 3);
  }
  // The two halves still cover the rectangle (the diagonal is shared).
  const patchy::Rect clip{0, 0, 120, 80};
  CHECK(std::abs(area_of(patchy::rasterize_vector_path(closed->path, {clip})) -
                 area_of(patchy::rasterize_vector_path(rect, {clip}))) < 2.0);
  // A single break: the open run (ends at one place) closes into the original loop.
  const auto opened = patchy::break_path_at_points(rect, {{0, 1}});
  const auto reclosed = patchy::close_open_subpaths(opened->path, {{0, 2}});
  CHECK(reclosed.has_value() && reclosed->path.subpaths[0].closed);
  CHECK(reclosed->path.subpaths[0].anchors.size() == 4);
  // Two-point pieces and closed paths do not close.
  VectorPath line;
  line.subpaths = {polygon({{0, 0}, {10, 0}}, false)};
  CHECK(!patchy::close_open_subpaths(line, {{0, 0}}).has_value());
  CHECK(!patchy::close_open_subpaths(rect, {{0, 0}}).has_value());
}

}  // namespace

std::vector<patchy::test::TestCase> path_edits_tests() {
  return {
      {"path_close_open_subpaths_splits_a_shape_in_two", path_close_open_subpaths_splits_a_shape_in_two},
      {"path_point_commands_sweep_renders_inside_the_shape", path_point_commands_sweep_renders_inside_the_shape},
      {"path_merge_components_matches_the_live_combine", path_merge_components_matches_the_live_combine},
      {"path_merge_components_keeps_the_rest_and_handles_cutters",
       path_merge_components_keeps_the_rest_and_handles_cutters},
      {"path_break_at_points_renders_without_spikes", path_break_at_points_renders_without_spikes},
  };
}
