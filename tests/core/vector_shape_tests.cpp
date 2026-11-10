#include "core/document.hpp"
#include "core/document_path.hpp"
#include "core/layer.hpp"
#include "core/path_fit.hpp"
#include "core/path_point_edits.hpp"
#include "core/path_simplify.hpp"
#include "core/pixel_tools.hpp"
#include "core/vector_live_shapes.hpp"
#include "core/vector_raster.hpp"
#include "core/vector_shape.hpp"
#include "core/layer_metadata.hpp"
#include "core_test_support.hpp"
#include "psd/psd_document_io.hpp"
#include "test_harness.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <array>
#include <utility>
#include <vector>

namespace {

using patchy::DocumentPath;
using patchy::DocumentPathKind;
using patchy::kLiveShapeKappa;
using patchy::Layer;
using patchy::LiveShapeKind;
using patchy::LiveShapeParams;
using patchy::PathAnchor;
using patchy::PathCombineOp;
using patchy::PathSubpath;
using patchy::VectorPath;

bool nearly(double a, double b, double tolerance = 1e-12) {
  return std::fabs(a - b) <= tolerance;
}

VectorPath make_sample_path() {
  VectorPath path;
  path.fill_rule_value = 0;
  path.initial_fill_value = 1;

  PathSubpath closed;
  closed.closed = true;
  closed.op = PathCombineOp::Subtract;
  closed.shape_group = 0;
  PathAnchor smooth;
  smooth.anchor_x = 12.25;
  smooth.anchor_y = -3.5;
  smooth.in_x = 10.0;
  smooth.in_y = -8.125;
  smooth.out_x = 14.75;
  smooth.out_y = 1.0625;
  smooth.smooth = true;
  PathAnchor corner;
  corner.anchor_x = 47.001953125;
  corner.anchor_y = 33.0;
  corner.in_x = corner.anchor_x;
  corner.in_y = corner.anchor_y;
  corner.out_x = corner.anchor_x;
  corner.out_y = corner.anchor_y;
  corner.smooth = false;
  closed.anchors = {smooth, corner};

  PathSubpath open;
  open.closed = false;
  open.op = PathCombineOp::Xor;
  open.shape_group = 1;
  PathAnchor lone;
  lone.anchor_x = 0.1;
  lone.anchor_y = 1e-9;
  lone.in_x = -0.25;
  lone.in_y = 100000.5;
  lone.out_x = 0.3333333333333333;
  lone.out_y = -1e6;
  lone.smooth = true;
  open.anchors = {lone};

  path.subpaths = {closed, open};
  return path;
}

void vector_path_text_codec_round_trips() {
  const auto path = make_sample_path();
  const auto text = patchy::serialize_vector_path(path);
  const auto parsed = patchy::parse_vector_path(text);
  CHECK(parsed.has_value());
  CHECK(*parsed == path);

  CHECK(!patchy::parse_vector_path("").has_value());
  CHECK(!patchy::parse_vector_path("v2 0 0 0").has_value());
  CHECK(!patchy::parse_vector_path("v1 0 0").has_value());
  CHECK(!patchy::parse_vector_path("v1 0 0 1\nS 1 9 0 0").has_value());
  CHECK(!patchy::parse_vector_path(text + " trailing").has_value());
  // Cut mid-record: the final anchor tag survives but its numbers are gone.
  const auto truncated = text.substr(0, text.rfind('A') + 1);
  CHECK(!patchy::parse_vector_path(truncated).has_value());
}

void path_fixed_point_conversion_round_trips() {
  for (const std::int32_t extent : {64, 977, 1920, 30000}) {
    for (const std::int32_t fixed : {0, 1, -1, 0x00a00000, -0x00280000, 0x7fffffff}) {
      const double pixels = patchy::path_coordinate_from_fixed(fixed, extent);
      CHECK(patchy::path_coordinate_to_fixed(pixels, extent) == fixed);
    }
  }
  // The observed encoding: 40 px on a 64 px canvas = 0.625 * 2^24.
  CHECK(patchy::path_coordinate_to_fixed(40.0, 64) == 0x00a00000);
  CHECK(nearly(patchy::path_coordinate_from_fixed(0x00a00000, 64), 40.0));
  // Degenerate extents produce 0 rather than dividing by zero.
  CHECK(patchy::path_coordinate_to_fixed(10.0, 0) == 0);
}

void live_rounded_rect_matches_photoshop_construction() {
  // The probe-live-rect capture: box (8,12)-(52,44), radii TL 4, TR 8,
  // BL 12, BR 16 (docs/vector-psd-encodings.md).
  LiveShapeParams params;
  params.kind = LiveShapeKind::RoundedRectangle;
  params.left = 8.0;
  params.top = 12.0;
  params.right = 52.0;
  params.bottom = 44.0;
  params.corner_radii = {4.0, 8.0, 16.0, 12.0};  // TL, TR, BR, BL
  const auto subpaths = patchy::generate_live_shape_subpaths(params);
  CHECK(subpaths.size() == 1);
  const auto& anchors = subpaths[0].anchors;
  CHECK(subpaths[0].closed);
  CHECK(anchors.size() == 8);
  // Knot order and handles exactly as Photoshop wrote them.
  CHECK(nearly(anchors[0].anchor_x, 12.0) && nearly(anchors[0].anchor_y, 12.0));
  CHECK(nearly(anchors[0].in_x, 12.0 - 4.0 * kLiveShapeKappa));
  CHECK(anchors[0].smooth);
  CHECK(nearly(anchors[1].anchor_x, 44.0) && nearly(anchors[1].anchor_y, 12.0));
  CHECK(nearly(anchors[1].out_x, 44.0 + 8.0 * kLiveShapeKappa));
  CHECK(nearly(anchors[2].anchor_x, 52.0) && nearly(anchors[2].anchor_y, 20.0));
  CHECK(nearly(anchors[2].in_y, 20.0 - 8.0 * kLiveShapeKappa));
  CHECK(nearly(anchors[3].anchor_x, 52.0) && nearly(anchors[3].anchor_y, 28.0));
  CHECK(nearly(anchors[3].out_y, 28.0 + 16.0 * kLiveShapeKappa));
  CHECK(nearly(anchors[4].anchor_x, 36.0) && nearly(anchors[4].anchor_y, 44.0));
  CHECK(nearly(anchors[4].in_x, 36.0 + 16.0 * kLiveShapeKappa));
  CHECK(nearly(anchors[5].anchor_x, 20.0) && nearly(anchors[5].anchor_y, 44.0));
  CHECK(nearly(anchors[5].out_x, 20.0 - 12.0 * kLiveShapeKappa));
  CHECK(nearly(anchors[6].anchor_x, 8.0) && nearly(anchors[6].anchor_y, 32.0));
  CHECK(nearly(anchors[6].in_y, 32.0 + 12.0 * kLiveShapeKappa));
  CHECK(nearly(anchors[7].anchor_x, 8.0) && nearly(anchors[7].anchor_y, 16.0));
  CHECK(nearly(anchors[7].out_y, 16.0 - 4.0 * kLiveShapeKappa));
}

void live_ellipse_and_line_match_photoshop_construction() {
  LiveShapeParams ellipse;
  ellipse.kind = LiveShapeKind::Ellipse;
  ellipse.left = 8.0;
  ellipse.top = 12.0;
  ellipse.right = 52.0;
  ellipse.bottom = 44.0;
  const auto ellipse_subpaths = patchy::generate_live_shape_subpaths(ellipse);
  CHECK(ellipse_subpaths.size() == 1);
  const auto& knots = ellipse_subpaths[0].anchors;
  CHECK(knots.size() == 4);
  CHECK(nearly(knots[0].anchor_x, 30.0) && nearly(knots[0].anchor_y, 12.0));
  CHECK(nearly(knots[0].in_x, 30.0 - 22.0 * kLiveShapeKappa));
  CHECK(nearly(knots[0].out_x, 30.0 + 22.0 * kLiveShapeKappa));
  CHECK(nearly(knots[1].anchor_x, 52.0) && nearly(knots[1].anchor_y, 28.0));
  CHECK(nearly(knots[1].in_y, 28.0 - 16.0 * kLiveShapeKappa));
  CHECK(nearly(knots[2].anchor_x, 30.0) && nearly(knots[2].anchor_y, 44.0));
  CHECK(nearly(knots[3].anchor_x, 8.0) && nearly(knots[3].anchor_y, 28.0));

  // The probe-live-line capture: (8,50)->(56,14), width 5 => the quad
  // (9.5,52), (6.5,48), (54.5,12), (57.5,16).
  LiveShapeParams line;
  line.kind = LiveShapeKind::Line;
  line.line_start_x = 8.0;
  line.line_start_y = 50.0;
  line.line_end_x = 56.0;
  line.line_end_y = 14.0;
  line.line_weight = 5.0;
  const auto line_subpaths = patchy::generate_live_shape_subpaths(line);
  CHECK(line_subpaths.size() == 1);
  const auto& quad = line_subpaths[0].anchors;
  CHECK(quad.size() == 4);
  CHECK(nearly(quad[0].anchor_x, 9.5) && nearly(quad[0].anchor_y, 52.0));
  CHECK(nearly(quad[1].anchor_x, 6.5) && nearly(quad[1].anchor_y, 48.0));
  CHECK(nearly(quad[2].anchor_x, 54.5) && nearly(quad[2].anchor_y, 12.0));
  CHECK(nearly(quad[3].anchor_x, 57.5) && nearly(quad[3].anchor_y, 16.0));
  CHECK(!quad[0].smooth);
}

void live_line_arrowheads_generate_heads() {
  LiveShapeParams line;
  line.kind = LiveShapeKind::Line;
  line.index = 3;
  line.line_start_x = 0.0;
  line.line_start_y = 0.0;
  line.line_end_x = 100.0;
  line.line_end_y = 0.0;
  line.line_weight = 4.0;
  line.arrow_end = true;
  line.arrow_width = 12.0;
  line.arrow_length = 16.0;
  line.arrow_concavity = 25;
  // A head wider than the shaft makes one outline with it (GitHub #622 follow-up):
  // start+n, start-n, then the end head from its -n side: the shaft edge meets the back
  // edge (from the notch, pulled 25% of the length toward the tip, to the corner), the
  // corner, the tip, the other corner, the other meeting point.
  const auto subpaths = patchy::generate_live_shape_subpaths(line);
  CHECK(subpaths.size() == 1);
  CHECK(subpaths[0].shape_group == 3 && subpaths[0].closed);
  const auto& outline = subpaths[0].anchors;
  CHECK(outline.size() == 7);
  const std::array<std::pair<double, double>, 7> expected{{
      {0.0, 2.0}, {0.0, -2.0}, {88.0 - 4.0 / 3.0, -2.0}, {84.0, -6.0}, {100.0, 0.0}, {84.0, 6.0}, {88.0 - 4.0 / 3.0, 2.0}}};
  for (std::size_t index = 0; index < expected.size(); ++index) {
    CHECK(nearly(outline[index].anchor_x, expected[index].first) && nearly(outline[index].anchor_y, expected[index].second));
  }
  CHECK(!outline[0].smooth);

  // A convex head (negative concavity) at the start: the meeting points sit behind its base.
  LiveShapeParams convex = line;
  convex.arrow_end = false;
  convex.arrow_start = true;
  convex.arrow_concavity = -50;
  const auto convex_paths = patchy::generate_live_shape_subpaths(convex);
  CHECK(convex_paths.size() == 1 && convex_paths[0].anchors.size() == 7);
  CHECK(nearly(convex_paths[0].anchors[0].anchor_x, 24.0 - 8.0 / 3.0) && nearly(convex_paths[0].anchors[0].anchor_y, 2.0));
  CHECK(nearly(convex_paths[0].anchors[2].anchor_x, 0.0) && nearly(convex_paths[0].anchors[2].anchor_y, 0.0));
  CHECK(nearly(convex_paths[0].anchors[6].anchor_x, 100.0) && nearly(convex_paths[0].anchors[6].anchor_y, 2.0));

  // A head no wider than the shaft stays a separate polygon, the shaft stopping at its base.
  LiveShapeParams narrow = line;
  narrow.arrow_width = 3.0;
  const auto narrow_paths = patchy::generate_live_shape_subpaths(narrow);
  CHECK(narrow_paths.size() == 2);
  CHECK(nearly(narrow_paths[0].anchors[2].anchor_x, 84.0));
  CHECK(narrow_paths[1].anchors.size() == 4 && nearly(narrow_paths[1].anchors[0].anchor_x, 100.0));

  LiveShapeParams degenerate = line;
  degenerate.line_end_x = 0.0;
  CHECK(patchy::generate_live_shape_subpaths(degenerate).empty());
}

LiveShapeParams arrow_line(double length, double weight, double head_width, double head_length, int concavity,
                           bool start, bool end) {
  LiveShapeParams line;
  line.kind = LiveShapeKind::Line;
  line.index = 2;
  line.line_start_x = 0.0;
  line.line_start_y = 0.0;
  line.line_end_x = length;
  line.line_end_y = 0.0;
  line.line_weight = weight;
  line.arrow_start = start;
  line.arrow_end = end;
  line.arrow_width = head_width;
  line.arrow_length = head_length;
  line.arrow_concavity = concavity;
  return line;
}

bool anchors_are(const std::vector<PathAnchor>& anchors, const std::vector<std::pair<double, double>>& expected) {
  if (anchors.size() != expected.size()) {
    return false;
  }
  for (std::size_t index = 0; index < expected.size(); ++index) {
    if (!nearly(anchors[index].anchor_x, expected[index].first, 1e-9) ||
        !nearly(anchors[index].anchor_y, expected[index].second, 1e-9)) {
      return false;
    }
  }
  return true;
}

// local-051: a head longer than its line keeps its size, tip on the line's end, and the
// outline stays one valid shape: the shaft goes once the head's back passes the other end.
void live_line_head_longer_than_the_line_keeps_its_size() {
  // A flat-backed head, 60 wide and 300 long, on a 100 px line: the head alone, its back
  // 200 px behind the line's start (listed from the middle of the back).
  const auto flat = patchy::generate_live_shape_subpaths(arrow_line(100, 4, 60, 300, 0, false, true));
  CHECK(flat.size() == 1 && flat[0].closed && flat[0].shape_group == 2);
  CHECK(anchors_are(flat[0].anchors, {{-200.0, 0.0}, {-200.0, -30.0}, {100.0, 0.0}, {-200.0, 30.0}}));
  // The same head at the start points the other way.
  const auto at_start = patchy::generate_live_shape_subpaths(arrow_line(100, 4, 60, 300, 0, true, false));
  CHECK(at_start.size() == 1);
  CHECK(anchors_are(at_start[0].anchors, {{300.0, 30.0}, {0.0, 0.0}, {300.0, -30.0}, {300.0, 0.0}}));

  // A concave head (50%) 190 long: its notch reaches 95 px back from the tip, so the last
  // 5 px of the line still fill the notch, cut off square at the line's start.
  const auto concave = patchy::generate_live_shape_subpaths(arrow_line(100, 4, 60, 190, 50, false, true));
  CHECK(concave.size() == 1);
  const double notch_half = 30.0 * 5.0 / 95.0;
  CHECK(anchors_are(concave[0].anchors,
                    {{0.0, notch_half}, {0.0, -notch_half}, {-90.0, -30.0}, {100.0, 0.0}, {-90.0, 30.0}}));
  // Longer still, the notch passes the start and the head is alone.
  const auto past = patchy::generate_live_shape_subpaths(arrow_line(100, 4, 60, 400, 50, false, true));
  CHECK(past.size() == 1);
  CHECK(anchors_are(past[0].anchors, {{-100.0, 0.0}, {-300.0, -30.0}, {100.0, 0.0}, {-300.0, 30.0}}));

  // A convex head (-50%) whose point passes the line's start while its sides have not:
  // the start cap carries that point.
  const auto convex = patchy::generate_live_shape_subpaths(arrow_line(100, 4, 60, 67, -50, false, true));
  CHECK(convex.size() == 1 && convex[0].anchors.size() == 10);
  const double back = 67.0 * (1.0 + 0.5 * (1.0 - 2.0 / 30.0));
  const double share = 0.5 / (100.5 - back);
  CHECK(nearly(convex[0].anchors[0].anchor_x, 0.0) && nearly(convex[0].anchors[0].anchor_y, 2.0));
  CHECK(nearly(convex[0].anchors[1].anchor_x, 0.0) && nearly(convex[0].anchors[1].anchor_y, 2.0 * share, 1e-9));
  CHECK(nearly(convex[0].anchors[2].anchor_x, -0.5, 1e-9) && nearly(convex[0].anchors[2].anchor_y, 0.0));
  CHECK(nearly(convex[0].anchors[3].anchor_y, -2.0 * share, 1e-9));
  CHECK(nearly(convex[0].anchors[4].anchor_x, 0.0) && nearly(convex[0].anchors[4].anchor_y, -2.0));

  // A head narrower than the shaft and longer than the line: the head alone, no shaft
  // drawn backwards behind the start.
  const auto narrow = patchy::generate_live_shape_subpaths(arrow_line(100, 10, 6, 300, 0, false, true));
  CHECK(narrow.size() == 1);
  CHECK(anchors_are(narrow[0].anchors, {{100.0, 0.0}, {-200.0, -3.0}, {-200.0, 0.0}, {-200.0, 3.0}}));

  // Such a line still resolves as live, moved or not.
  LiveShapeParams params = arrow_line(100, 4, 60, 300, 0, false, true);
  VectorPath path;
  path.subpaths = patchy::generate_live_shape_subpaths(params);
  auto resolved = patchy::resolve_live_shape_group(path, {params}, 2);
  CHECK(resolved.has_value() && resolved->arrow_length == 300.0);
  patchy::transform_vector_path(path, {1.0, 0.0, 0.0, 1.0, 12.0, -7.0});
  resolved = patchy::resolve_live_shape_group(path, {params}, 2);
  CHECK(resolved.has_value() && nearly(resolved->line_end_x, 112.0, 1e-9) && nearly(resolved->line_end_y, -7.0, 1e-9));
}

// local-051: two heads share the line. When they would run into each other each is drawn
// at most as long as lets their backs meet in the middle, at its own width.
void live_line_two_heads_meet_in_the_middle() {
  // Flat backs: a diamond, the two backs on one line.
  const auto flat = patchy::generate_live_shape_subpaths(arrow_line(100, 4, 60, 80, 0, true, true));
  CHECK(flat.size() == 1);
  CHECK(anchors_are(flat[0].anchors, {{0.0, 0.0}, {50.0, -30.0}, {100.0, 0.0}, {50.0, 30.0}}));
  // Far longer heads draw the same shape: the length that fits is all that shows.
  const auto huge = patchy::generate_live_shape_subpaths(arrow_line(100, 4, 60, 30000, 0, true, true));
  CHECK(huge.size() == 1 && anchors_are(huge[0].anchors, {{0.0, 0.0}, {50.0, -30.0}, {100.0, 0.0}, {50.0, 30.0}}));
  // Concave heads: the barbs meet at the middle, the shaft still shows in both notches.
  const auto concave = patchy::generate_live_shape_subpaths(arrow_line(100, 4, 60, 80, 50, true, true));
  CHECK(concave.size() == 1 && concave[0].anchors.size() == 10);
  CHECK(nearly(concave[0].anchors[1].anchor_x, 50.0) && nearly(concave[0].anchors[1].anchor_y, 30.0));
  CHECK(nearly(concave[0].anchors[8].anchor_x, 50.0) && nearly(concave[0].anchors[8].anchor_y, 30.0));
  // Convex heads: the points where the backs meet the shaft's edges touch at the middle.
  const auto convex = patchy::generate_live_shape_subpaths(arrow_line(100, 4, 60, 80, -50, true, true));
  CHECK(convex.size() == 1 && convex[0].anchors.size() == 8);
  CHECK(nearly(convex[0].anchors[3].anchor_x, 50.0, 1e-9) && nearly(convex[0].anchors[3].anchor_y, -2.0));
  CHECK(nearly(convex[0].anchors[7].anchor_x, 50.0, 1e-9) && nearly(convex[0].anchors[7].anchor_y, 2.0));
  // Narrow heads: two head polygons, base to base, and no shaft.
  const auto narrow = patchy::generate_live_shape_subpaths(arrow_line(100, 10, 6, 80, 0, true, true));
  CHECK(narrow.size() == 2);
  CHECK(nearly(narrow[0].anchors[1].anchor_x, 50.0) && nearly(narrow[1].anchors[1].anchor_x, 50.0));
  // Heads that fit are untouched (the #622 outline).
  const auto fits = patchy::generate_live_shape_subpaths(arrow_line(100, 4, 12, 16, 0, true, true));
  CHECK(fits.size() == 1 && fits[0].anchors.size() == 10);
}

// Even-odd membership of (x, y) in the subpaths' straight-edged outlines.
bool inside_outline(const std::vector<PathSubpath>& subpaths, double x, double y) {
  bool inside = false;
  for (const auto& subpath : subpaths) {
    const auto& anchors = subpath.anchors;
    for (std::size_t i = 0, j = anchors.size() - 1; i < anchors.size(); j = i++) {
      const double xi = anchors[i].anchor_x, yi = anchors[i].anchor_y;
      const double xj = anchors[j].anchor_x, yj = anchors[j].anchor_y;
      if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) {
        inside = !inside;
      }
    }
  }
  return inside;
}

double distance_to_segment(double x, double y, double ax, double ay, double bx, double by) {
  const double dx = bx - ax, dy = by - ay;
  const double span = dx * dx + dy * dy;
  const double t = span > 0.0 ? std::clamp(((x - ax) * dx + (y - ay) * dy) / span, 0.0, 1.0) : 0.0;
  return std::hypot(x - (ax + dx * t), y - (ay + dy * t));
}

// local-051: across short and long lines, small and very large heads, every concavity and
// either or both ends, a line's outline fills exactly its shaft and heads: nothing punched
// out where they overlap, no shaft drawn backwards, no stray area.
void live_line_outline_is_the_union_of_shaft_and_heads() {
  int compared = 0;
  for (const double length : {10.0, 40.0, 100.0, 400.0}) {
    for (const double head_length : {5.0, 30.0, 80.0, 150.0, 500.0, 30000.0}) {
      for (const double head_width : {6.0, 30.0, 3000.0}) {
        for (const int concavity : {-50, -20, 0, 20, 50}) {
          for (const int heads : {1, 2, 3}) {
            const double weight = 4.0;
            const auto line = arrow_line(length, weight, head_width, head_length, concavity, (heads & 1) != 0,
                                         (heads & 2) != 0);
            const auto subpaths = patchy::generate_live_shape_subpaths(line);
            CHECK(subpaths.size() == 1);
            for (const auto& anchor : subpaths[0].anchors) {
              CHECK(std::isfinite(anchor.anchor_x) && std::isfinite(anchor.anchor_y));
            }
            CHECK(subpaths[0].anchors.size() >= 3);

            // The shape by its parts, in line coordinates (t along the line, s across it).
            const double half = weight / 2.0;
            const double half_width = head_width / 2.0;
            const double c = concavity / 100.0;
            const double back = 1.0 - c * (1.0 - half / half_width);
            double drawn_length = head_length;
            if (heads == 3 && 2.0 * head_length * std::max(1.0, back) >= length) {
              drawn_length = length / (2.0 * std::max(1.0, back));
            }
            const double notch = drawn_length * (1.0 - c);
            const double join = drawn_length * back;
            // A head, u along the line from its tip and s across it.
            std::vector<PathSubpath> head(1);
            for (const auto& [u, s] : {std::pair{0.0, 0.0}, std::pair{drawn_length, half_width},
                                       std::pair{notch, 0.0}, std::pair{drawn_length, -half_width}}) {
              PathAnchor anchor;
              anchor.anchor_x = u;
              anchor.anchor_y = s;
              head[0].anchors.push_back(anchor);
            }
            const auto in_head = [&](double u, double s) { return inside_outline(head, u, s); };
            // The shaft ends on a head's back: the line from its middle to where it meets
            // the shaft's edge.
            const auto behind_back = [&](double u, double s) {
              return u >= notch + std::abs(s) / half * (join - notch);
            };
            const auto expected = [&](double t, double s) {
              const bool start_head = (heads & 1) != 0, end_head = (heads & 2) != 0;
              if ((start_head && in_head(t, s)) || (end_head && in_head(length - t, s))) {
                return true;
              }
              return t >= 0.0 && t <= length && std::abs(s) <= half && (!start_head || behind_back(t, s)) &&
                     (!end_head || behind_back(length - t, s));
            };
            VectorPath outline;
            outline.subpaths = subpaths;
            const auto bounds = outline.bounds();
            CHECK(bounds.has_value());
            const double left = std::min(bounds->left, -1.0) - 3.0, right = std::max(bounds->right, length + 1.0) + 3.0;
            const double top = bounds->top - 3.0, bottom = bounds->bottom + 3.0;
            constexpr int kGrid = 37;
            for (int row = 0; row < kGrid; ++row) {
              for (int column = 0; column < kGrid; ++column) {
                const double x = left + (right - left) * (column + 0.37) / kGrid;
                const double y = top + (bottom - top) * (row + 0.61) / kGrid;
                // A sample on an edge has no side: leave it out.
                double edge_distance = std::numeric_limits<double>::max();
                const auto& anchors = subpaths[0].anchors;
                for (std::size_t i = 0, j = anchors.size() - 1; i < anchors.size(); j = i++) {
                  edge_distance = std::min(edge_distance,
                                           distance_to_segment(x, y, anchors[i].anchor_x, anchors[i].anchor_y,
                                                               anchors[j].anchor_x, anchors[j].anchor_y));
                }
                if (edge_distance < 1e-6 * std::max(1.0, right - left)) {
                  continue;
                }
                ++compared;
                if (inside_outline(subpaths, x, y) != expected(x, y)) {
                  patchy::test::report_failure(
                      "line outline differs from its parts: length " + std::to_string(length) + " head " +
                      std::to_string(head_width) + "x" + std::to_string(head_length) + " concavity " +
                      std::to_string(concavity) + " heads " + std::to_string(heads) + " at " + std::to_string(x) +
                      ", " + std::to_string(y));
                  return;
                }
              }
            }
          }
        }
      }
    }
  }
  CHECK(compared > 400000);
}

// local-051: a head far past the old 1000% and 5000% ceilings, one longer than its line and
// one at the 30000 px ceiling are pixels in the PSD and come back as the same live line.
// The files stay in test-artifacts for the Photoshop open check (docs/vector-tools.md).
void live_line_large_arrowhead_survives_a_psd_round_trip() {
  struct Case {
    double line_length;
    double head_width;
    double head_length;
  };
  std::filesystem::create_directories("test-artifacts");
  for (const auto& [line_length, head_width, head_length] :
       {Case{900.0, 320.0, 480.0}, Case{200.0, 320.0, 480.0}, Case{900.0, 5000.0, 5000.0},
        Case{900.0, patchy::kLiveLineArrowMaxSize, patchy::kLiveLineArrowMaxSize}}) {
    patchy::Document document(1200, 700, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("bg", patchy::test::solid_rgba(1200, 700, 255, 255, 255, 255));
    Layer shape(document.allocate_layer_id(), "Line 1", patchy::PixelBuffer());
    LiveShapeParams params = arrow_line(line_length, 4, head_width, head_length, 20, false, true);
    params.index = 0;
    params.line_start_x = 150; params.line_start_y = 350;
    params.line_end_x = 150 + line_length; params.line_end_y = 350;
    patchy::VectorShapeContent content;
    content.path.subpaths = patchy::generate_live_shape_subpaths(params);
    const auto hull = content.path.bounds();
    CHECK(hull.has_value());
    params.left = hull->left; params.top = hull->top; params.right = hull->right; params.bottom = hull->bottom;
    patchy::populate_live_shape_box_corners(params);
    content.origination = {params};
    content.fill.kind = patchy::VectorFillKind::Solid;
    content.fill.color = patchy::RgbColor{20, 40, 160};
    shape.set_vector_shape(content);
    shape.metadata()[patchy::kLayerMetadataVectorShape] = "1";
    patchy::update_vector_shape_raster(shape, patchy::Rect::from_size(1200, 700), nullptr);
    document.add_layer(std::move(shape));

    const auto written = patchy::psd::DocumentIo::write_layered_rgb8(document);
    patchy::psd::DocumentIo::write_layered_rgb8_file(
        document, std::filesystem::path("test-artifacts") /
                      ("local_051_line_" + std::to_string(static_cast<int>(line_length)) + "_head_" +
                       std::to_string(static_cast<int>(head_width)) + "x" +
                       std::to_string(static_cast<int>(head_length)) + ".psd"));
    const auto reread = patchy::psd::DocumentIo::read(written, {});
    const auto* line = reread.layers()[1].vector_shape();
    CHECK(line != nullptr && line->origination.size() == 1);
    const auto live = patchy::resolve_live_shape_group(line->path, line->origination, line->origination[0].index);
    CHECK(live.has_value() && live->kind == LiveShapeKind::Line && live->arrow_end && !live->arrow_start);
    CHECK(live.has_value() && nearly(live->arrow_width, head_width, 1e-9) &&
          nearly(live->arrow_length, head_length, 1e-9));
    CHECK(live.has_value() && live->arrow_concavity == 20 && nearly(live->line_weight, 4.0, 1e-9));
    CHECK(live.has_value() && live->arrow_width_unit_pixels && live->arrow_length_unit_pixels);
    // The head is as tall as its width on the canvas, up to the 700 px canvas.
    CHECK(std::abs(reread.layers()[1].bounds().height - static_cast<int>(std::min(head_width, 700.0))) <= 2);
  }
}

void rounded_rect_zero_and_clamped_radii() {
  LiveShapeParams sharp;
  sharp.kind = LiveShapeKind::RoundedRectangle;
  sharp.left = 1.0;
  sharp.top = 2.0;
  sharp.right = 11.0;
  sharp.bottom = 8.0;
  sharp.corner_radii = {0.0, 0.0, 0.0, 0.0};
  const auto sharp_subpaths = patchy::generate_live_shape_subpaths(sharp);
  CHECK(sharp_subpaths.size() == 1);
  CHECK(sharp_subpaths[0].anchors.size() == 4);
  CHECK(!sharp_subpaths[0].anchors[0].smooth);

  LiveShapeParams clamped = sharp;
  clamped.left = 0.0;
  clamped.top = 0.0;
  clamped.right = 40.0;
  clamped.bottom = 30.0;
  clamped.corner_radii = {50.0, 50.0, 50.0, 50.0};
  const auto clamped_subpaths = patchy::generate_live_shape_subpaths(clamped);
  CHECK(clamped_subpaths.size() == 1);
  // Scale = min(40/100, 30/100) = 0.3 => radii 15 everywhere.
  const auto& anchors = clamped_subpaths[0].anchors;
  CHECK(anchors.size() == 8);
  CHECK(nearly(anchors[0].anchor_x, 15.0) && nearly(anchors[0].anchor_y, 0.0));
  CHECK(nearly(anchors[1].anchor_x, 25.0) && nearly(anchors[1].anchor_y, 0.0));
}

void vector_path_bounds_groups_and_transforms() {
  auto path = make_sample_path();
  CHECK(!path.empty());
  CHECK(path.next_shape_group() == 2);
  const auto bounds = path.bounds();
  CHECK(bounds.has_value());
  // Control points extend the hull beyond anchors.
  CHECK(nearly(bounds->left, -0.25));
  CHECK(nearly(bounds->right, 47.001953125));
  CHECK(nearly(bounds->top, -1e6));
  CHECK(nearly(bounds->bottom, 100000.5));

  VectorPath empty;
  CHECK(empty.empty());
  CHECK(!empty.bounds().has_value());
  CHECK(empty.next_shape_group() == 0);

  VectorPath square;
  PathSubpath subpath;
  subpath.anchors = {PathAnchor{0.0, 0.0, 0.0, 0.0, 0.0, 0.0, false},
                     PathAnchor{10.0, 0.0, 10.0, 0.0, 10.0, 0.0, false},
                     PathAnchor{10.0, 10.0, 10.0, 10.0, 10.0, 10.0, false}};
  square.subpaths = {subpath};
  patchy::transform_vector_path(square, {2.0, 0.0, 0.0, 0.5, 3.0, -1.0});
  CHECK(nearly(square.subpaths[0].anchors[1].anchor_x, 23.0));
  CHECK(nearly(square.subpaths[0].anchors[1].anchor_y, -1.0));
  CHECK(nearly(square.subpaths[0].anchors[2].anchor_y, 4.0));
  patchy::translate_vector_path(square, -3.0, 1.0);
  CHECK(nearly(square.subpaths[0].anchors[0].anchor_x, 0.0));
  CHECK(nearly(square.subpaths[0].anchors[0].anchor_y, 0.0));
}

void vector_metadata_flags_round_trip() {
  Layer layer(1, "shape", patchy::PixelBuffer());
  CHECK(!patchy::layer_has_vector_shape_marker(layer));
  CHECK(!patchy::layer_vector_block_dirty(layer));
  CHECK(patchy::vector_lock_reason(layer).empty());

  layer.metadata()[patchy::kLayerMetadataVectorShape] = "1";
  CHECK(patchy::layer_has_vector_shape_marker(layer));
  patchy::mark_layer_vector_block_dirty(layer);
  CHECK(patchy::layer_vector_block_dirty(layer));
  layer.metadata()[patchy::kLayerMetadataVectorLock] = "unparsed";
  CHECK(patchy::vector_lock_reason(layer) == "unparsed");
}

void document_path_revision_and_dirty_semantics() {
  DocumentPath path(7, "Alpha Path", DocumentPathKind::Saved, make_sample_path());
  CHECK(path.id() == 7);
  CHECK(!path.dirty());
  const auto initial_revision = path.content_revision();

  auto payload = std::make_shared<const std::vector<std::uint8_t>>(std::vector<std::uint8_t>{1, 2, 3});
  path.set_resource_source(2000, payload);
  CHECK(!path.dirty());
  CHECK(path.resource_id().has_value() && *path.resource_id() == 2000);
  CHECK(path.content_revision() == initial_revision);

  path.set_name("Alpha Path");  // no-op
  CHECK(!path.dirty());
  path.set_name("Renamed");
  CHECK(path.dirty());
  CHECK(path.content_revision() > initial_revision);

  DocumentPath work(8, "Work Path", DocumentPathKind::Work, VectorPath{});
  const auto before = work.content_revision();
  work.set_clipping_path(true);
  CHECK(work.is_clipping_path());
  CHECK(work.dirty());
  CHECK(work.content_revision() > before);
}

// Builds a 100x80 document holding a live-rect shape layer (10,10)-(50,40)
// and a saved path with one square subpath.
patchy::Document geometry_test_document() {
  patchy::Document document(100, 80, patchy::PixelFormat::rgb8());
  patchy::Layer shape(document.allocate_layer_id(), "Shape", patchy::PixelBuffer());
  patchy::VectorShapeContent content;
  patchy::LiveShapeParams params;
  params.kind = patchy::LiveShapeKind::Rectangle;
  params.left = 10;
  params.top = 10;
  params.right = 50;
  params.bottom = 40;
  patchy::populate_live_shape_box_corners(params);
  content.path.subpaths = patchy::generate_live_shape_subpaths(params);
  content.origination = {params};
  content.fill.kind = patchy::VectorFillKind::Solid;
  content.fill.color = patchy::RgbColor{10, 20, 30};
  content.stroke.enabled = true;
  content.stroke.width = 4.0;
  shape.set_vector_shape(content);
  shape.metadata()[patchy::kLayerMetadataVectorShape] = "1";
  patchy::update_vector_shape_raster(shape, patchy::Rect::from_size(100, 80), nullptr);
  document.add_layer(std::move(shape));

  patchy::VectorPath saved;
  patchy::PathSubpath square;
  for (const auto& [x, y] : {std::pair{60.0, 50.0}, {90.0, 50.0}, {90.0, 70.0}, {60.0, 70.0}}) {
    patchy::PathAnchor anchor;
    anchor.anchor_x = anchor.in_x = anchor.out_x = x;
    anchor.anchor_y = anchor.in_y = anchor.out_y = y;
    square.anchors.push_back(anchor);
  }
  saved.subpaths.push_back(square);
  patchy::DocumentPath path(document.allocate_path_id(), "Path 1", patchy::DocumentPathKind::Saved,
                            std::move(saved));
  path.reset_dirty();
  document.add_path(std::move(path));
  return document;
}

void geometry_ops_transform_vector_data() {
  // Crop translates paths, keeps live params, and re-normalizes saved paths.
  {
    auto document = geometry_test_document();
    CHECK(patchy::crop_document(document, patchy::Rect{10, 10, 80, 60}));
    const auto* layer = &document.layers()[0];
    const auto* content = layer->vector_shape();
    CHECK(std::abs(content->path.subpaths[0].anchors[0].anchor_x - 0.0) < 1e-9);
    CHECK(content->origination.size() == 1);
    CHECK(std::abs(content->origination[0].left - 0.0) < 1e-9);
    CHECK(std::abs(content->origination[0].bottom - 30.0) < 1e-9);
    CHECK(patchy::layer_vector_block_dirty(*layer));
    // The bake keeps the centered 4 px stroke's overhang past the new edge
    // (update_vector_shape_raster is not canvas-clipped).
    CHECK(layer->bounds().x == -2);
    CHECK(document.paths()[0].dirty());
    CHECK(std::abs(document.paths()[0].path().subpaths[0].anchors[0].anchor_x - 50.0) < 1e-9);
  }
  // Image resize scales anchors, live params, and the stroke width.
  {
    auto document = geometry_test_document();
    patchy::resize_image_and_layers(document, 200, 160);
    const auto* content = document.layers()[0].vector_shape();
    CHECK(std::abs(content->path.subpaths[0].anchors[2].anchor_x - 100.0) < 1e-9);
    CHECK(std::abs(content->origination[0].right - 100.0) < 1e-9);
    CHECK(std::abs(content->stroke.width - 8.0) < 1e-9);
    CHECK(document.layers()[0].bounds().width >= 80);
    CHECK(std::abs(document.paths()[0].path().subpaths[0].anchors[1].anchor_x - 180.0) < 1e-9);
  }
  // Rotate clockwise maps edge coordinates (x, y) -> (H - y, x) and drops the
  // live annotation (the path is exact).
  {
    auto document = geometry_test_document();
    patchy::rotate_document_clockwise(document);
    const auto* content = document.layers()[0].vector_shape();
    CHECK(content->origination.empty());
    // Anchor (10, 10) -> (80 - 10, 10) = (70, 10).
    CHECK(std::abs(content->path.subpaths[0].anchors[0].anchor_x - 70.0) < 1e-9);
    CHECK(std::abs(content->path.subpaths[0].anchors[0].anchor_y - 10.0) < 1e-9);
    CHECK(document.width() == 80);
    // Path occupies 40..70 horizontally; the 4 px centered stroke reaches 2
    // px beyond it.
    CHECK(std::abs(document.layers()[0].bounds().x - 38) <= 1);
  }
  // Canvas resize translates by the anchor offset.
  {
    auto document = geometry_test_document();
    patchy::resize_canvas_and_layers(document, 120, 100, patchy::CanvasAnchor::Center);
    const auto* content = document.layers()[0].vector_shape();
    CHECK(std::abs(content->path.subpaths[0].anchors[0].anchor_x - 20.0) < 1e-9);
    CHECK(content->origination.size() == 1);
  }
  // A per-layer flip mirrors the path about the pixel-bounds center.
  {
    auto document = geometry_test_document();
    const auto layer_id = document.layers()[0].id();
    static_cast<void>(patchy::flip_layer_horizontal(document, layer_id));
    const auto* content = document.layers()[0].vector_shape();
    CHECK(content->origination.empty());
    // Bounds spanned 8..52 (stroke reach); mirroring about the center swaps
    // the rect's 10 and 50 edges.
    const auto& anchors = content->path.subpaths[0].anchors;
    const auto min_x = std::min({anchors[0].anchor_x, anchors[1].anchor_x, anchors[2].anchor_x,
                                 anchors[3].anchor_x});
    CHECK(std::abs(min_x - 10.0) < 1e-6);
  }
}

void path_fit_square_keeps_four_corner_anchors() {
  // A traced rectangle (collinear runs already collapsed) must fit to exactly
  // its four corners with collapsed handles, at any reasonable tolerance.
  const std::vector<patchy::FitPoint> square{{10.0, 10.0}, {50.0, 10.0}, {50.0, 40.0}, {10.0, 40.0}};
  const auto fitted = patchy::fit_closed_loop(square, 2.0);
  CHECK(fitted.closed);
  CHECK(fitted.anchors.size() == 4);
  for (const auto& anchor : fitted.anchors) {
    CHECK(!anchor.smooth);
    CHECK(nearly(anchor.in_x, anchor.anchor_x, 1e-9));
    CHECK(nearly(anchor.in_y, anchor.anchor_y, 1e-9));
    CHECK(nearly(anchor.out_x, anchor.anchor_x, 1e-9));
    CHECK(nearly(anchor.out_y, anchor.anchor_y, 1e-9));
  }
  bool found_first_corner = false;
  for (const auto& anchor : fitted.anchors) {
    if (nearly(anchor.anchor_x, 10.0, 1e-9) && nearly(anchor.anchor_y, 10.0, 1e-9)) {
      found_first_corner = true;
    }
  }
  CHECK(found_first_corner);
  // Winding: clockwise in y-down coordinates reads positive (an outer loop).
  CHECK(patchy::loop_signed_area(square) > 0.0);
  const auto reversed = std::vector<patchy::FitPoint>{square.rbegin(), square.rend()};
  CHECK(patchy::loop_signed_area(reversed) < 0.0);
}

void path_fit_circle_is_smooth_and_within_tolerance() {
  // A dense circle polygon fits into a small set of smooth anchors whose
  // curve stays within the tolerance of the true circle.
  constexpr double kRadius = 50.0;
  constexpr double kCenter = 60.0;
  std::vector<patchy::FitPoint> circle;
  for (int step = 0; step < 360; ++step) {
    const double angle = step * 3.14159265358979323846 / 180.0;
    circle.push_back({kCenter + kRadius * std::cos(angle), kCenter + kRadius * std::sin(angle)});
  }
  constexpr double kTolerance = 2.0;
  const auto fitted = patchy::fit_closed_loop(circle, kTolerance);
  CHECK(fitted.closed);
  // Two smooth seam anchors suffice when each half-circle fits one cubic
  // within tolerance (radius 50 leaves ~1px of error per semicircle).
  CHECK(fitted.anchors.size() >= 2);
  CHECK(fitted.anchors.size() <= 24);  // far fewer than the 360 input points
  for (const auto& anchor : fitted.anchors) {
    CHECK(anchor.smooth);
  }
  // Evaluate every segment densely: the fitted curve must hug the circle.
  const auto& anchors = fitted.anchors;
  for (std::size_t i = 0; i < anchors.size(); ++i) {
    const auto& a = anchors[i];
    const auto& b = anchors[(i + 1) % anchors.size()];
    for (int step = 0; step <= 16; ++step) {
      const double t = step / 16.0;
      const double u = 1.0 - t;
      const double x = u * u * u * a.anchor_x + 3.0 * t * u * u * a.out_x +
                       3.0 * t * t * u * b.in_x + t * t * t * b.anchor_x;
      const double y = u * u * u * a.anchor_y + 3.0 * t * u * u * a.out_y +
                       3.0 * t * t * u * b.in_y + t * t * t * b.anchor_y;
      const double radius = std::hypot(x - kCenter, y - kCenter);
      CHECK(std::abs(radius - kRadius) <= kTolerance + 0.6);
    }
  }
  // Deterministic: the same input fits to the identical anchor list.
  const auto again = patchy::fit_closed_loop(circle, kTolerance);
  CHECK(again.anchors == fitted.anchors);
}

void path_fit_staircase_smooths_diagonal() {
  // A pixel-trace staircase (unit steps) along a diagonal collapses into a
  // near-line fit instead of keeping every stair corner.
  std::vector<patchy::FitPoint> loop;
  for (int i = 0; i < 20; ++i) {  // stair edge: right 1, down 1, twenty times
    loop.push_back({static_cast<double>(i), static_cast<double>(i)});
    loop.push_back({static_cast<double>(i + 1), static_cast<double>(i)});
  }
  loop.push_back({20.0, 20.0});
  loop.push_back({0.0, 20.0});  // close the triangle-ish region
  const auto fitted = patchy::fit_closed_loop(loop, 2.0);
  CHECK(fitted.anchors.size() >= 3);
  CHECK(fitted.anchors.size() <= 8);  // the 40 stair vertices must not survive
}

// A 64-gon approximating a circle: closed, corner anchors with collapsed
// handles (the shape an over-anchored trace produces).
patchy::PathSubpath polygon_circle_subpath(double cx, double cy, double radius, int sides, int group,
                                           patchy::PathCombineOp op) {
  patchy::PathSubpath subpath;
  subpath.closed = true;
  subpath.op = op;
  subpath.shape_group = group;
  for (int i = 0; i < sides; ++i) {
    const double angle = 2.0 * 3.14159265358979323846 * static_cast<double>(i) / sides;
    patchy::PathAnchor anchor;
    anchor.anchor_x = cx + radius * std::cos(angle);
    anchor.anchor_y = cy + radius * std::sin(angle);
    anchor.in_x = anchor.anchor_x;
    anchor.in_y = anchor.anchor_y;
    anchor.out_x = anchor.anchor_x;
    anchor.out_y = anchor.anchor_y;
    subpath.anchors.push_back(anchor);
  }
  return subpath;
}

double coverage_at(const patchy::CoverageBuffer& buffer, std::int32_t x, std::int32_t y) {
  if (buffer.bounds.empty() || !buffer.bounds.contains(x, y)) {
    return 0.0;
  }
  return *buffer.pixels.pixel(x - buffer.bounds.x, y - buffer.bounds.y);
}

void path_fit_open_polyline_keeps_endpoints_within_tolerance() {
  // A quarter circle, radius 50 about the origin, sampled every 2.25 degrees.
  std::vector<patchy::FitPoint> points;
  for (int i = 0; i <= 40; ++i) {
    const double angle = 0.5 * 3.14159265358979323846 * static_cast<double>(i) / 40.0;
    points.push_back(patchy::FitPoint{50.0 * std::cos(angle), 50.0 * std::sin(angle)});
  }
  patchy::PathFitOptions options;
  options.tolerance = 0.5;
  const auto fitted = patchy::fit_open_polyline(points, options);
  CHECK(!fitted.closed);
  CHECK(fitted.anchors.size() >= 2);
  CHECK(fitted.anchors.size() <= 6);
  CHECK(std::abs(fitted.anchors.front().anchor_x - 50.0) < 1e-9);
  CHECK(std::abs(fitted.anchors.front().anchor_y - 0.0) < 1e-9);
  CHECK(std::abs(fitted.anchors.back().anchor_x - 0.0) < 1e-9);
  CHECK(std::abs(fitted.anchors.back().anchor_y - 50.0) < 1e-9);
  // Endpoint handles collapse on the side with no neighbor.
  CHECK(fitted.anchors.front().in_x == fitted.anchors.front().anchor_x);
  CHECK(fitted.anchors.back().out_y == fitted.anchors.back().anchor_y);
  // The curve stays on the circle within the tolerance (sampled per segment).
  for (std::size_t i = 0; i + 1 < fitted.anchors.size(); ++i) {
    const auto& a = fitted.anchors[i];
    const auto& b = fitted.anchors[i + 1];
    for (int step = 0; step <= 16; ++step) {
      const double t = step / 16.0;
      const double u = 1.0 - t;
      const double x = u * u * u * a.anchor_x + 3 * u * u * t * a.out_x + 3 * u * t * t * b.in_x +
                       t * t * t * b.anchor_x;
      const double y = u * u * u * a.anchor_y + 3 * u * u * t * a.out_y + 3 * u * t * t * b.in_y +
                       t * t * t * b.anchor_y;
      CHECK(std::abs(std::hypot(x, y) - 50.0) < 0.75);
    }
  }
  // Degenerate input.
  CHECK(patchy::fit_open_polyline({patchy::FitPoint{1.0, 1.0}}, options).anchors.empty());
}

void path_simplify_reduces_dense_circle_anchors() {
  patchy::VectorPath dense;
  dense.subpaths = {polygon_circle_subpath(50.0, 50.0, 40.0, 64, 0, patchy::PathCombineOp::Add)};
  patchy::PathSimplifyOptions options;
  options.tolerance = 1.0;
  const auto simplified = patchy::simplify_vector_path(dense, options);
  CHECK(simplified.anchors_before == 64);
  CHECK(simplified.anchors_after == simplified.path.subpaths[0].anchors.size());
  CHECK(simplified.anchors_after <= 8);
  CHECK(simplified.changed_groups == std::vector<int>{0});
  for (const auto& anchor : simplified.path.subpaths[0].anchors) {
    CHECK(anchor.smooth);
  }
  // The refit covers the same pixels: the deviation lives in a band up to the
  // tolerance wide along the 250 px perimeter, so the mean delta over the
  // 100x100 canvas stays small and the total coverage within two percent.
  patchy::VectorRasterOptions raster;
  raster.clip = patchy::Rect{0, 0, 100, 100};
  const auto before = patchy::rasterize_vector_path(dense, raster);
  const auto after = patchy::rasterize_vector_path(simplified.path, raster);
  double delta_sum = 0.0;
  double before_sum = 0.0;
  double after_sum = 0.0;
  for (std::int32_t y = 0; y < 100; ++y) {
    for (std::int32_t x = 0; x < 100; ++x) {
      const auto a = coverage_at(before, x, y);
      const auto b = coverage_at(after, x, y);
      delta_sum += std::abs(a - b);
      before_sum += a;
      after_sum += b;
    }
  }
  CHECK(delta_sum / (100.0 * 100.0) < 8.0);
  CHECK(std::abs(after_sum - before_sum) < 0.02 * before_sum);
}

void path_simplify_preserves_ops_groups_open_flag_and_never_grows() {
  patchy::VectorPath path;
  path.subpaths.push_back(polygon_circle_subpath(50.0, 50.0, 40.0, 64, 0, patchy::PathCombineOp::Add));
  path.subpaths.push_back(polygon_circle_subpath(50.0, 50.0, 20.0, 64, 1, patchy::PathCombineOp::Subtract));
  patchy::PathSubpath wave;
  wave.closed = false;
  wave.op = patchy::PathCombineOp::Add;
  wave.shape_group = 2;
  for (int i = 0; i <= 40; ++i) {
    patchy::PathAnchor anchor;
    anchor.anchor_x = 10.0 + 2.0 * i;
    anchor.anchor_y = 90.0 + 5.0 * std::sin(i * 0.3);
    anchor.in_x = anchor.anchor_x;
    anchor.in_y = anchor.anchor_y;
    anchor.out_x = anchor.anchor_x;
    anchor.out_y = anchor.anchor_y;
    wave.anchors.push_back(anchor);
  }
  path.subpaths.push_back(wave);
  path.fill_rule_value = 7;
  path.initial_fill_value = 3;
  patchy::PathSimplifyOptions options;
  options.tolerance = 1.0;
  const auto simplified = patchy::simplify_vector_path(path, options);
  CHECK(simplified.path.subpaths.size() == 3);
  CHECK(simplified.path.fill_rule_value == 7 && simplified.path.initial_fill_value == 3);
  for (std::size_t i = 0; i < 3; ++i) {
    CHECK(simplified.path.subpaths[i].op == path.subpaths[i].op);
    CHECK(simplified.path.subpaths[i].shape_group == path.subpaths[i].shape_group);
    CHECK(simplified.path.subpaths[i].closed == path.subpaths[i].closed);
    CHECK(simplified.path.subpaths[i].anchors.size() <= path.subpaths[i].anchors.size());
  }
  CHECK(simplified.path.subpaths[2].anchors.size() < 41);
  CHECK(simplified.changed_groups.size() == 3);
  CHECK(simplified.anchors_after < simplified.anchors_before);

  // A plain rectangle cannot get smaller: it comes back untouched.
  patchy::VectorPath rect;
  patchy::PathSubpath square;
  square.closed = true;
  for (const auto& [x, y] : {std::pair{10.0, 10.0}, std::pair{60.0, 10.0}, std::pair{60.0, 60.0},
                             std::pair{10.0, 60.0}}) {
    patchy::PathAnchor anchor;
    anchor.anchor_x = x;
    anchor.anchor_y = y;
    anchor.in_x = x;
    anchor.in_y = y;
    anchor.out_x = x;
    anchor.out_y = y;
    square.anchors.push_back(anchor);
  }
  rect.subpaths = {square};
  const auto untouched = patchy::simplify_vector_path(rect, options);
  CHECK(untouched.path == rect);
  CHECK(untouched.changed_groups.empty());
  CHECK(untouched.anchors_before == 4 && untouched.anchors_after == 4);
}

}  // namespace


void path_fit_long_staircase_does_not_depend_on_call_stack_depth() {
  std::vector<patchy::FitPoint> edge;
  for (int i=0; i<7500; ++i) {
    edge.push_back({2.0*i, static_cast<double>(i)});
    edge.push_back({2.0*i+2.0, static_cast<double>(i)});
  }
  patchy::PathFitOptions options;
  options.tolerance = 0.5;
  const auto fitted = patchy::fit_open_polyline(edge, options);
  CHECK(!fitted.closed);
  CHECK(fitted.anchors.size() >= 2);
  CHECK(fitted.anchors.front().anchor_x == edge.front().x);
  CHECK(fitted.anchors.back().anchor_x == edge.back().x);
  CHECK(fitted.anchors.back().anchor_y == edge.back().y);
}

// Distort/Perspective (GitHub issue 225): every anchor and handle goes through
// the homography with the perspective divide; an affine homography matches the
// affine overload exactly.
void vector_path_projective_transform_maps_control_points() {
  auto path = make_sample_path();
  auto affine_path = path;
  patchy::transform_vector_path(affine_path, {2.0, 0.5, -0.25, 1.5, 10.0, -4.0});
  auto homography_path = path;
  patchy::transform_vector_path_projective(homography_path, {2.0, -0.25, 10.0, 0.5, 1.5, -4.0, 0.0, 0.0, 1.0});
  CHECK(homography_path.subpaths.size() == affine_path.subpaths.size());
  for (std::size_t s = 0; s < affine_path.subpaths.size(); ++s) {
    const auto& expected = affine_path.subpaths[s].anchors;
    const auto& actual = homography_path.subpaths[s].anchors;
    CHECK(actual.size() == expected.size());
    for (std::size_t a = 0; a < expected.size(); ++a) {
      CHECK(nearly(actual[a].anchor_x, expected[a].anchor_x, 1e-9));
      CHECK(nearly(actual[a].anchor_y, expected[a].anchor_y, 1e-9));
      CHECK(nearly(actual[a].in_x, expected[a].in_x, 1e-9));
      CHECK(nearly(actual[a].out_y, expected[a].out_y, 1e-9));
    }
  }
  // x' = x / (1 + x/100): (50, 20) -> (33.33.., 13.33..).
  VectorPath single;
  PathSubpath subpath;
  PathAnchor anchor;
  anchor.anchor_x = 50.0;
  anchor.anchor_y = 20.0;
  anchor.in_x = 50.0;
  anchor.in_y = 20.0;
  anchor.out_x = 0.0;
  anchor.out_y = 0.0;
  subpath.anchors = {anchor};
  single.subpaths = {subpath};
  patchy::transform_vector_path_projective(single, {1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.01, 0.0, 1.0});
  const auto& mapped = single.subpaths[0].anchors[0];
  CHECK(nearly(mapped.anchor_x, 50.0 / 1.5, 1e-9));
  CHECK(nearly(mapped.anchor_y, 20.0 / 1.5, 1e-9));
  CHECK(nearly(mapped.out_x, 0.0, 1e-12));
}

// A perspective rectangle is no longer a live rectangle: the annotation drops,
// the path corners land on the quad, and the raster follows.
void vector_layer_projective_transform_drops_live_shape() {
  patchy::Document document(200, 160, patchy::PixelFormat::rgba8());
  Layer layer(document.allocate_layer_id(), "Rectangle", patchy::PixelBuffer());
  LiveShapeParams params;
  params.kind = LiveShapeKind::Rectangle;
  params.left = 20;
  params.top = 30;
  params.right = 120;
  params.bottom = 90;
  patchy::populate_live_shape_box_corners(params);
  patchy::VectorShapeContent content;
  content.path.subpaths = patchy::generate_live_shape_subpaths(params);
  content.origination.push_back(params);
  content.fill.kind = patchy::VectorFillKind::Solid;
  content.fill.color = patchy::RgbColor{40, 90, 200};
  layer.set_vector_shape(std::move(content));
  patchy::update_vector_shape_raster(layer, patchy::Rect::from_size(200, 160), nullptr);
  // A homography that keeps (20,30) and (20,90) and pulls the right side in:
  // x' = 20 + (x - 20) / (1 + (x - 20) / 200), y' unchanged for these corners.
  const std::array<double, 9> homography{1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.005, 0.0, 0.9};
  patchy::transform_layer_vector_data_projective(document, layer, homography, patchy::Rect::from_size(200, 160));
  const auto* shape = layer.vector_shape();
  CHECK(shape != nullptr);
  CHECK(shape->origination.empty());
  bool found_mapped_corner = false;
  for (const auto& subpath : shape->path.subpaths) {
    for (const auto& point : subpath.anchors) {
      const double w = 0.005 * 120.0 + 0.9;
      if (nearly(point.anchor_x, 120.0 / w, 1e-9) && nearly(point.anchor_y, 30.0 / w, 1e-9)) {
        found_mapped_corner = true;
      }
    }
  }
  CHECK(found_mapped_corner);
  CHECK(!layer.bounds().empty());
}


// GitHub #555: point and segment edits and component helpers (core/path_point_edits).
PathSubpath corner_subpath(std::vector<std::pair<double, double>> points, bool closed, int group = 0,
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

bool at(const PathAnchor& anchor, double x, double y) {
  return nearly(anchor.anchor_x, x, 1e-9) && nearly(anchor.anchor_y, y, 1e-9);
}

void path_break_at_point_opens_closed_and_splits_open_subpaths() {
  VectorPath square;
  square.subpaths = {corner_subpath({{0, 0}, {10, 0}, {10, 10}, {0, 10}}, true, 3)};
  // A curved segment 0 -> 1 keeps its handles through the break at anchor 1.
  square.subpaths[0].anchors[0].out_x = 4.0;
  square.subpaths[0].anchors[1].in_x = 6.0;
  square.subpaths[0].anchors[1].in_y = -3.0;
  square.subpaths[0].anchors[1].out_y = 3.0;
  const auto opened = patchy::break_path_at_points(square, {{0, 1}});
  CHECK(opened.has_value());
  CHECK(opened->path.subpaths.size() == 1);
  const auto& run = opened->path.subpaths[0];
  CHECK(!run.closed && run.shape_group == 3 && run.anchors.size() == 5);
  CHECK(at(run.anchors.front(), 10, 0) && at(run.anchors.back(), 10, 0));
  // The start keeps the handle toward anchor 2, the end the one from anchor 0.
  CHECK(nearly(run.anchors.front().out_y, 3.0) && nearly(run.anchors.front().in_y, 0.0));
  CHECK(nearly(run.anchors.back().in_x, 6.0) && nearly(run.anchors.back().in_y, -3.0));
  CHECK(nearly(run.anchors.back().out_y, 0.0));
  CHECK(opened->touched_groups == std::vector<int>{3});
  CHECK((opened->selection == std::set<patchy::PathPointKey>{{0, 0}, {0, 4}}));
  // Breaking that open run at an interior anchor splits it; its end points do not break.
  CHECK(!patchy::break_path_at_points(opened->path, {{0, 0}, {0, 4}}).has_value());
  const auto split = patchy::break_path_at_points(opened->path, {{0, 2}});
  CHECK(split.has_value() && split->path.subpaths.size() == 2);
  CHECK(split->path.subpaths[0].anchors.size() == 3 && split->path.subpaths[1].anchors.size() == 3);
  CHECK(at(split->path.subpaths[0].anchors.back(), 0, 10) && at(split->path.subpaths[1].anchors.front(), 0, 10));
  CHECK(split->path.subpaths[1].shape_group == 3);
  CHECK((split->selection == std::set<patchy::PathPointKey>{{0, 2}, {1, 0}}));
  // Two breaks on one loop make two open pieces.
  const auto halves = patchy::break_path_at_points(square, {{0, 0}, {0, 2}});
  CHECK(halves.has_value() && halves->path.subpaths.size() == 2);
  CHECK(halves->path.subpaths[0].anchors.size() == 3 && halves->path.subpaths[1].anchors.size() == 3);
}

void path_join_end_points_closes_and_connects_subpaths() {
  VectorPath path;
  path.subpaths = {corner_subpath({{0, 0}, {10, 0}, {10, 10}}, false, 0),
                   corner_subpath({{20, 0}, {30, 0}}, false, 1, PathCombineOp::Subtract)};
  // Same subpath: a straight closing segment, or one point at the midpoint.
  const auto closed = patchy::join_path_end_points(path, {0, 2}, {0, 0}, false);
  CHECK(closed.has_value() && closed->path.subpaths[0].closed && closed->path.subpaths[0].anchors.size() == 3);
  const auto merged = patchy::join_path_end_points(path, {0, 0}, {0, 2}, true);
  CHECK(merged.has_value() && merged->path.subpaths[0].closed && merged->path.subpaths[0].anchors.size() == 2);
  CHECK(at(merged->path.subpaths[0].anchors[0], 5, 5));
  // Two subpaths: the first's end at 0 runs into the second's end at 30 (reversed): one subpath
  // in the first's place, group and op.
  const auto connected = patchy::join_path_end_points(path, {0, 0}, {1, 1}, false);
  CHECK(connected.has_value() && connected->path.subpaths.size() == 1);
  const auto& one = connected->path.subpaths[0];
  CHECK(!one.closed && one.anchors.size() == 5 && one.shape_group == 0 && one.op == PathCombineOp::Add);
  CHECK(at(one.anchors[0], 10, 10) && at(one.anchors[2], 0, 0) && at(one.anchors[3], 30, 0) &&
        at(one.anchors[4], 20, 0));
  CHECK((connected->touched_groups == std::vector<int>{0, 1}));
  const auto fused = patchy::join_path_end_points(path, {0, 2}, {1, 0}, true);
  CHECK(fused.has_value() && fused->path.subpaths[0].anchors.size() == 4);
  CHECK(at(fused->path.subpaths[0].anchors[2], 15, 5));
  CHECK((fused->selection == std::set<patchy::PathPointKey>{{0, 2}}));
  // Coinciding end points merge even as a plain join; interior points never join.
  VectorPath touching;
  touching.subpaths = {corner_subpath({{0, 0}, {10, 0}}, false), corner_subpath({{10, 0}, {10, 10}}, false, 1)};
  const auto touched = patchy::join_path_end_points(touching, {0, 1}, {1, 0}, false);
  CHECK(touched.has_value() && touched->path.subpaths[0].anchors.size() == 3);
  CHECK(!patchy::join_path_end_points(path, {0, 1}, {1, 0}, false).has_value());
  VectorPath loop;
  loop.subpaths = {corner_subpath({{0, 0}, {10, 0}, {10, 10}}, true)};
  CHECK(!patchy::join_path_end_points(loop, {0, 0}, {0, 2}, false).has_value());
}

void path_delete_segment_leaves_closed_and_open_paths_open() {
  VectorPath square;
  square.subpaths = {corner_subpath({{0, 0}, {10, 0}, {10, 10}, {0, 10}}, true, 2)};
  // Closed: the top segment goes, the path opens running 1 -> 2 -> 3 -> 0.
  const auto opened = patchy::delete_path_segments(square, {{0, 0}});
  CHECK(opened.has_value() && opened->path.subpaths.size() == 1);
  const auto& run = opened->path.subpaths[0];
  CHECK(!run.closed && run.anchors.size() == 4 && run.shape_group == 2);
  CHECK(at(run.anchors.front(), 10, 0) && at(run.anchors.back(), 0, 0));
  // The closing segment (3 -> 0) is a segment too.
  const auto closing = patchy::delete_path_segments(square, {{0, 3}});
  CHECK(closing.has_value() && at(closing->path.subpaths[0].anchors.front(), 0, 0) &&
        at(closing->path.subpaths[0].anchors.back(), 0, 10));
  // Open: the middle segment goes and the path splits in two, same group.
  const auto split = patchy::delete_path_segments(opened->path, {{0, 1}});
  CHECK(split.has_value() && split->path.subpaths.size() == 2);
  CHECK(split->path.subpaths[0].anchors.size() == 2 && split->path.subpaths[1].anchors.size() == 2);
  CHECK(split->path.subpaths[1].shape_group == 2);
  // An end segment leaves a single point, which goes.
  const auto trimmed = patchy::delete_path_segments(opened->path, {{0, 0}});
  CHECK(trimmed.has_value() && trimmed->path.subpaths.size() == 1 && trimmed->path.subpaths[0].anchors.size() == 3);
  // Selected points name segments: both ends selected.
  CHECK((patchy::path_segments_between_points(square, {{0, 3}, {0, 0}}) == std::set<patchy::PathPointKey>{{0, 3}}));
  CHECK(patchy::path_segments_between_points(opened->path, {{0, 3}, {0, 0}}).empty());
  CHECK(!patchy::delete_path_segments(opened->path, {{0, 3}}).has_value());
}

void path_merge_points_collapses_a_segment_or_a_row() {
  VectorPath path;
  path.subpaths = {corner_subpath({{0, 0}, {10, 0}, {10, 10}, {0, 10}}, true)};
  path.subpaths[0].anchors[0].in_y = 4.0;    // the handle toward anchor 3 survives the merge
  path.subpaths[0].anchors[1].out_y = -2.0;  // and the one toward anchor 2
  const auto collapsed = patchy::merge_path_points(path, {{0, 0}, {0, 1}});
  CHECK(collapsed.has_value());
  const auto& triangle = collapsed->path.subpaths[0];
  CHECK(triangle.closed && triangle.anchors.size() == 3);
  CHECK(at(triangle.anchors[0], 5, 0));
  CHECK(nearly(triangle.anchors[0].in_x, 5.0) && nearly(triangle.anchors[0].in_y, 4.0));
  CHECK(nearly(triangle.anchors[0].out_x, 5.0) && nearly(triangle.anchors[0].out_y, -2.0));
  CHECK((collapsed->selection == std::set<patchy::PathPointKey>{{0, 0}}));
  // A row across the loop's start (3, 0, 1) merges at its average.
  const auto row = patchy::merge_path_points(path, {{0, 3}, {0, 0}, {0, 1}});
  CHECK(row.has_value() && row->path.subpaths[0].anchors.size() == 2);
  CHECK(at(row->path.subpaths[0].anchors[0], 10.0 / 3.0, 10.0 / 3.0));
  // Points that are not neighbours, or the whole loop, do not merge.
  CHECK(!patchy::merge_path_points(path, {{0, 0}, {0, 2}}).has_value());
  CHECK(!patchy::merge_path_points(path, {{0, 0}, {0, 1}, {0, 2}, {0, 3}}).has_value());
  // Two end points of different subpaths merge into one (join as one point).
  VectorPath lines;
  lines.subpaths = {corner_subpath({{0, 0}, {10, 0}}, false), corner_subpath({{12, 0}, {20, 0}}, false, 1)};
  const auto fused = patchy::merge_path_points(lines, {{0, 1}, {1, 0}});
  CHECK(fused.has_value() && fused->path.subpaths.size() == 1 && fused->path.subpaths[0].anchors.size() == 3);
  CHECK(at(fused->path.subpaths[0].anchors[1], 11, 0));
  // A two-point open line cannot collapse to one point.
  VectorPath line;
  line.subpaths = {corner_subpath({{0, 0}, {10, 0}}, false)};
  CHECK(!patchy::merge_path_points(line, {{0, 0}, {0, 1}}).has_value());
}

void path_components_copy_append_and_recolor() {
  patchy::VectorShapeContent content;
  content.fill.color = patchy::RgbColor{200, 0, 0};
  content.path.subpaths = {corner_subpath({{0, 0}, {40, 0}, {40, 40}, {0, 40}}, true, 0),
                           corner_subpath({{10, 10}, {30, 10}, {30, 30}, {10, 30}}, true, 1, PathCombineOp::Subtract),
                           corner_subpath({{50, 0}, {90, 0}, {90, 40}, {50, 40}}, true, 2)};
  LiveShapeParams live;
  live.kind = LiveShapeKind::Rectangle;
  live.index = 2;
  content.origination = {live};
  const auto copied = patchy::vector_shape_content_for_groups(content, {2});
  CHECK(copied.path.subpaths.size() == 1 && copied.path.subpaths[0].shape_group == 2);
  CHECK(copied.origination.size() == 1 && copied.fill.color == content.fill.color);
  CHECK(patchy::path_without_components(content.path, {0, 1}).subpaths.size() == 1);
  auto target = content.path;
  const auto renumbered = patchy::append_path_components(target, patchy::path_components(content.path, {0, 1}));
  CHECK(target.subpaths.size() == 5);
  CHECK((renumbered == std::map<int, int>{{0, 3}, {1, 4}}));
  CHECK(target.subpaths[4].op == PathCombineOp::Subtract && target.subpaths[4].shape_group == 4);

  // Recolor the right square: a base part keeps the red for the rest, the square paints blue,
  // and the hole stays in the base part.
  auto recolored = content;
  CHECK(patchy::recolor_vector_shape_components(recolored, {2}, patchy::RgbColor{0, 0, 255}) ==
        patchy::ComponentColorRefusal::None);
  CHECK(recolored.parts.size() == 2);
  CHECK((recolored.parts[0].groups == std::vector<std::int32_t>{0, 1}));
  CHECK(recolored.parts[0].fill.color == content.fill.color);
  CHECK((recolored.parts[1].groups == std::vector<std::int32_t>{2}));
  CHECK((recolored.parts[1].fill.color == patchy::RgbColor{0, 0, 255}));
  // Recolor the donut: the hole follows its square into the new part.
  auto donut = content;
  CHECK(patchy::recolor_vector_shape_components(donut, {0}, patchy::RgbColor{0, 255, 0}) ==
        patchy::ComponentColorRefusal::None);
  CHECK((donut.parts[0].groups == std::vector<std::int32_t>{2}));
  CHECK((donut.parts[1].groups == std::vector<std::int32_t>{0, 1}));
  // Every painting component listed: a plain fill change, no parts.
  auto whole = content;
  CHECK(patchy::recolor_vector_shape_components(whole, {0, 1, 2}, patchy::RgbColor{1, 2, 3}) ==
        patchy::ComponentColorRefusal::None);
  CHECK(whole.parts.empty() && (whole.fill.color == patchy::RgbColor{1, 2, 3}));
  // A hole alone paints nothing; an Intersect in the part refuses.
  CHECK(patchy::vector_shape_components_color_refusal(content, {1}) == patchy::ComponentColorRefusal::OnlyCutters);
  auto intersect = content;
  intersect.path.subpaths[2].op = PathCombineOp::Intersect;
  CHECK(patchy::vector_shape_components_color_refusal(intersect, {0}) ==
        patchy::ComponentColorRefusal::SharedOverlap);
  CHECK(patchy::vector_shape_components_color_refusal(content, {9}) == patchy::ComponentColorRefusal::NoComponents);
}

std::vector<patchy::test::TestCase> vector_shape_tests() {
  return {
      {"vector_path_text_codec_round_trips", vector_path_text_codec_round_trips},
      {"vector_path_projective_transform_maps_control_points", vector_path_projective_transform_maps_control_points},
      {"vector_layer_projective_transform_drops_live_shape", vector_layer_projective_transform_drops_live_shape},
      {"path_fixed_point_conversion_round_trips", path_fixed_point_conversion_round_trips},
      {"live_rounded_rect_matches_photoshop_construction", live_rounded_rect_matches_photoshop_construction},
      {"live_ellipse_and_line_match_photoshop_construction",
       live_ellipse_and_line_match_photoshop_construction},
      {"live_line_arrowheads_generate_heads", live_line_arrowheads_generate_heads},
      {"live_line_head_longer_than_the_line_keeps_its_size", live_line_head_longer_than_the_line_keeps_its_size},
      {"live_line_two_heads_meet_in_the_middle", live_line_two_heads_meet_in_the_middle},
      {"live_line_outline_is_the_union_of_shaft_and_heads", live_line_outline_is_the_union_of_shaft_and_heads},
      {"live_line_large_arrowhead_survives_a_psd_round_trip", live_line_large_arrowhead_survives_a_psd_round_trip},
      {"rounded_rect_zero_and_clamped_radii", rounded_rect_zero_and_clamped_radii},
      {"vector_path_bounds_groups_and_transforms", vector_path_bounds_groups_and_transforms},
      {"vector_metadata_flags_round_trip", vector_metadata_flags_round_trip},
      {"document_path_revision_and_dirty_semantics", document_path_revision_and_dirty_semantics},
      {"geometry_ops_transform_vector_data", geometry_ops_transform_vector_data},
      {"path_fit_square_keeps_four_corner_anchors", path_fit_square_keeps_four_corner_anchors},
      {"path_fit_circle_is_smooth_and_within_tolerance",
       path_fit_circle_is_smooth_and_within_tolerance},
      {"path_fit_staircase_smooths_diagonal", path_fit_staircase_smooths_diagonal},
      {"path_fit_open_polyline_keeps_endpoints_within_tolerance",
       path_fit_open_polyline_keeps_endpoints_within_tolerance},
      {"path_simplify_reduces_dense_circle_anchors", path_simplify_reduces_dense_circle_anchors},
      {"path_simplify_preserves_ops_groups_open_flag_and_never_grows",
       path_simplify_preserves_ops_groups_open_flag_and_never_grows},
      {"path_fit_long_staircase_does_not_depend_on_call_stack_depth", path_fit_long_staircase_does_not_depend_on_call_stack_depth},
      {"path_break_at_point_opens_closed_and_splits_open_subpaths",
       path_break_at_point_opens_closed_and_splits_open_subpaths},
      {"path_join_end_points_closes_and_connects_subpaths", path_join_end_points_closes_and_connects_subpaths},
      {"path_delete_segment_leaves_closed_and_open_paths_open", path_delete_segment_leaves_closed_and_open_paths_open},
      {"path_merge_points_collapses_a_segment_or_a_row", path_merge_points_collapses_a_segment_or_a_row},
      {"path_components_copy_append_and_recolor", path_components_copy_append_and_recolor},
  };
}
