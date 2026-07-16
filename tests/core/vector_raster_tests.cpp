#include "core/layer.hpp"
#include "core/pattern_resource.hpp"
#include "core/vector_live_shapes.hpp"
#include "core/vector_raster.hpp"
#include "core/vector_shape.hpp"

#include "core_test_support.hpp"
#include "test_harness.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using patchy::CoverageBuffer;
using patchy::LiveShapeKind;
using patchy::LiveShapeParams;
using patchy::PathAnchor;
using patchy::PathCombineOp;
using patchy::PathSubpath;
using patchy::Rect;
using patchy::VectorPath;
using patchy::VectorRasterOptions;
using patchy::test::fnv1a_hash_bytes;

PathAnchor corner(double x, double y) {
  PathAnchor anchor;
  anchor.anchor_x = anchor.in_x = anchor.out_x = x;
  anchor.anchor_y = anchor.in_y = anchor.out_y = y;
  return anchor;
}

PathSubpath rect_subpath(double left, double top, double right, double bottom, PathCombineOp op,
                         std::int32_t group) {
  PathSubpath subpath;
  subpath.closed = true;
  subpath.op = op;
  subpath.shape_group = group;
  subpath.anchors = {corner(left, top), corner(right, top), corner(right, bottom), corner(left, bottom)};
  return subpath;
}

CoverageBuffer rasterize(const VectorPath& path, Rect clip) {
  VectorRasterOptions options;
  options.clip = clip;
  return patchy::rasterize_vector_path(path, options);
}

std::uint8_t coverage_pixel(const CoverageBuffer& buffer, std::int32_t x, std::int32_t y) {
  if (buffer.bounds.empty() || !buffer.bounds.contains(x, y)) {
    return 0;
  }
  return buffer.pixels.data()[static_cast<std::size_t>(y - buffer.bounds.y) * buffer.pixels.stride_bytes() +
                              static_cast<std::size_t>(x - buffer.bounds.x)];
}

void raster_axis_aligned_rect_coverage_is_exact() {
  VectorPath path;
  path.subpaths = {rect_subpath(2.0, 3.0, 3.0, 4.0, PathCombineOp::Add, 0)};
  const auto unit = rasterize(path, Rect{0, 0, 16, 16});
  CHECK(unit.bounds.x == 2 && unit.bounds.y == 3);
  CHECK(unit.bounds.width == 1 && unit.bounds.height == 1);
  CHECK(coverage_pixel(unit, 2, 3) == 255);

  // Half-pixel-wide rect: exactly half coverage.
  VectorPath half;
  half.subpaths = {rect_subpath(2.0, 3.0, 2.5, 4.0, PathCombineOp::Add, 0)};
  const auto half_coverage = rasterize(half, Rect{0, 0, 16, 16});
  CHECK(coverage_pixel(half_coverage, 2, 3) == 128);

  // Quarter coverage via a quarter-pixel area (0.5 x 0.5).
  VectorPath quarter;
  quarter.subpaths = {rect_subpath(2.25, 3.25, 2.75, 3.75, PathCombineOp::Add, 0)};
  const auto quarter_coverage = rasterize(quarter, Rect{0, 0, 16, 16});
  CHECK(coverage_pixel(quarter_coverage, 2, 3) == 64);
}

void raster_half_plane_diagonal_ramp() {
  // Right triangle (0,0)-(8,0)-(0,8): the hypotenuse x+y=8 halves every
  // diagonal pixel; pixels well inside are full, outside empty.
  VectorPath path;
  PathSubpath triangle;
  triangle.closed = true;
  triangle.op = PathCombineOp::Add;
  triangle.anchors = {corner(0.0, 0.0), corner(8.0, 0.0), corner(0.0, 8.0)};
  path.subpaths = {triangle};
  const auto coverage = rasterize(path, Rect{0, 0, 16, 16});
  CHECK(coverage_pixel(coverage, 0, 0) == 255);
  CHECK(coverage_pixel(coverage, 3, 3) == 255);
  for (std::int32_t i = 0; i < 8; ++i) {
    const auto diagonal = coverage_pixel(coverage, i, 7 - i);
    CHECK(diagonal >= 126 && diagonal <= 129);
  }
  CHECK(coverage_pixel(coverage, 7, 7) == 0);
  CHECK(coverage_pixel(coverage, 8, 1) == 0);
}

void raster_area_sum_matches_analytic() {
  // 10.25 x 7.5 rect at fractional offsets: total coverage equals the area.
  VectorPath path;
  path.subpaths = {rect_subpath(1.375, 2.25, 11.625, 9.75, PathCombineOp::Add, 0)};
  const auto coverage = rasterize(path, Rect{0, 0, 20, 20});
  double total = 0.0;
  for (std::int32_t y = coverage.bounds.y; y < coverage.bounds.y + coverage.bounds.height; ++y) {
    for (std::int32_t x = coverage.bounds.x; x < coverage.bounds.x + coverage.bounds.width; ++x) {
      total += coverage_pixel(coverage, x, y);
    }
  }
  const double analytic = 10.25 * 7.5 * 255.0;
  CHECK(std::fabs(total - analytic) <= 40.0);  // sub-half-unit rounding per boundary pixel
}

void raster_combine_ops_match_photoshop_semantics() {
  const Rect clip{0, 0, 64, 64};
  // Two overlapping rects: A = (8,8)-(40,40), B = (24,24)-(56,56).
  const auto build = [&](PathCombineOp second_op) {
    VectorPath path;
    path.subpaths = {rect_subpath(8, 8, 40, 40, PathCombineOp::Add, 0),
                     rect_subpath(24, 24, 56, 56, second_op, 1)};
    return rasterize(path, clip);
  };

  const auto added = build(PathCombineOp::Add);
  CHECK(coverage_pixel(added, 16, 16) == 255);
  CHECK(coverage_pixel(added, 32, 32) == 255);
  CHECK(coverage_pixel(added, 48, 48) == 255);

  const auto subtracted = build(PathCombineOp::Subtract);
  CHECK(coverage_pixel(subtracted, 16, 16) == 255);
  CHECK(coverage_pixel(subtracted, 32, 32) == 0);
  CHECK(coverage_pixel(subtracted, 48, 48) == 0);

  const auto intersected = build(PathCombineOp::Intersect);
  CHECK(coverage_pixel(intersected, 16, 16) == 0);
  CHECK(coverage_pixel(intersected, 32, 32) == 255);
  CHECK(coverage_pixel(intersected, 48, 48) == 0);

  const auto xored = build(PathCombineOp::Xor);
  CHECK(coverage_pixel(xored, 16, 16) == 255);
  CHECK(coverage_pixel(xored, 32, 32) == 0);
  CHECK(coverage_pixel(xored, 48, 48) == 255);
}

void raster_first_op_initial_accumulator_semantics() {
  const Rect clip{0, 0, 64, 64};
  // Subtract-first: full canvas minus the rect (photoshop-shape-first-ops).
  VectorPath subtract_first;
  subtract_first.subpaths = {rect_subpath(4, 4, 20, 20, PathCombineOp::Subtract, 0)};
  const auto subtracted = rasterize(subtract_first, clip);
  CHECK(subtracted.bounds.width == 64 && subtracted.bounds.height == 64);
  CHECK(coverage_pixel(subtracted, 12, 12) == 0);
  CHECK(coverage_pixel(subtracted, 58, 6) == 255);
  CHECK(coverage_pixel(subtracted, 2, 2) == 255);

  // Intersect-first and xor-first: exactly the shape.
  for (const auto op : {PathCombineOp::Intersect, PathCombineOp::Xor}) {
    VectorPath path;
    path.subpaths = {rect_subpath(24, 24, 40, 40, op, 0)};
    const auto coverage = rasterize(path, clip);
    CHECK(coverage_pixel(coverage, 32, 32) == 255);
    CHECK(coverage_pixel(coverage, 6, 32) == 0);
    CHECK(coverage.bounds.width <= 18);
  }
}

void raster_even_odd_within_group_ops_between_groups() {
  const Rect clip{0, 0, 64, 64};
  // Same group: two overlapping Add rects cancel in the overlap (even-odd).
  VectorPath same_group;
  same_group.subpaths = {rect_subpath(8, 8, 40, 40, PathCombineOp::Add, 0),
                         rect_subpath(24, 24, 56, 56, PathCombineOp::Add, 0)};
  const auto folded = rasterize(same_group, clip);
  CHECK(coverage_pixel(folded, 16, 16) == 255);
  CHECK(coverage_pixel(folded, 32, 32) == 0);  // overlap folds out
  CHECK(coverage_pixel(folded, 48, 48) == 255);

  // Self-intersecting bowtie in one subpath: the wing regions fill, the
  // crossing region follows even-odd.
  VectorPath bowtie;
  PathSubpath cross;
  cross.closed = true;
  cross.op = PathCombineOp::Add;
  cross.anchors = {corner(8, 8), corner(56, 56), corner(56, 8), corner(8, 56)};
  bowtie.subpaths = {cross};
  const auto crossing = rasterize(bowtie, clip);
  CHECK(coverage_pixel(crossing, 20, 32) == 255);
  CHECK(coverage_pixel(crossing, 44, 32) == 255);
  CHECK(coverage_pixel(crossing, 32, 20) == 0);
  CHECK(coverage_pixel(crossing, 32, 44) == 0);
}

void raster_curves_donut_and_open_chord() {
  const Rect clip{0, 0, 64, 64};
  // Kappa-circle donut: outer r=24, inner r=10 subtracted.
  LiveShapeParams outer;
  outer.kind = LiveShapeKind::Ellipse;
  outer.left = 8;
  outer.top = 8;
  outer.right = 56;
  outer.bottom = 56;
  LiveShapeParams inner = outer;
  inner.left = 22;
  inner.top = 22;
  inner.right = 42;
  inner.bottom = 42;
  inner.index = 1;
  VectorPath path;
  path.subpaths = patchy::generate_live_shape_subpaths(outer);
  auto hole = patchy::generate_live_shape_subpaths(inner);
  hole[0].op = PathCombineOp::Subtract;
  path.subpaths.push_back(hole[0]);
  const auto donut = rasterize(path, clip);
  CHECK(coverage_pixel(donut, 32, 32) == 0);
  CHECK(coverage_pixel(donut, 32, 12) == 255);
  CHECK(coverage_pixel(donut, 32, 45) == 255);
  CHECK(coverage_pixel(donut, 2, 2) == 0);
  // A pixel straddling the circle boundary near 45 degrees: partial coverage.
  const auto edge = coverage_pixel(donut, 49, 15);
  CHECK(edge > 0 && edge < 255);

  // Open subpath: fills the implied closing chord.
  VectorPath open;
  PathSubpath arc;
  arc.closed = false;
  arc.op = PathCombineOp::Add;
  arc.anchors = {corner(10, 50), corner(30, 10), corner(50, 50)};
  open.subpaths = {arc};
  const auto filled = rasterize(open, clip);
  CHECK(coverage_pixel(filled, 30, 40) == 255);
  CHECK(coverage_pixel(filled, 30, 55) == 0);
}

void raster_empty_path_fills_clip() {
  VectorPath path;
  const auto coverage = rasterize(path, Rect{0, 0, 32, 24});
  CHECK(coverage.bounds.width == 32 && coverage.bounds.height == 24);
  CHECK(coverage_pixel(coverage, 0, 0) == 255);
  CHECK(coverage_pixel(coverage, 31, 23) == 255);
}

void raster_mask_inverted_coverage() {
  patchy::LayerVectorMask mask;
  mask.path.subpaths = {rect_subpath(4, 4, 12, 12, PathCombineOp::Add, 0)};
  const auto normal = patchy::rasterize_vector_mask_coverage(mask, Rect{0, 0, 32, 32});
  CHECK(coverage_pixel(normal, 8, 8) == 255);
  CHECK(coverage_pixel(normal, 20, 20) == 0);

  mask.inverted = true;
  const auto inverted = patchy::rasterize_vector_mask_coverage(mask, Rect{0, 0, 32, 32});
  CHECK(inverted.bounds.width == 32 && inverted.bounds.height == 32);
  CHECK(coverage_pixel(inverted, 8, 8) == 0);
  CHECK(coverage_pixel(inverted, 20, 20) == 255);
}

void raster_golden_digests_are_stable() {
  // Cross-toolchain byte-stability canary for the coverage engine (all-integer
  // core; safe to pin). The failure output prints the new hashes - re-pin only
  // for deliberate rasterizer changes.
  struct Golden {
    const char* name;
    VectorPath path;
    std::uint64_t expected;
  };
  std::vector<Golden> goldens;

  {
    LiveShapeParams circle;
    circle.kind = LiveShapeKind::Ellipse;
    circle.left = 5.3;
    circle.top = 7.9;
    circle.right = 51.6;
    circle.bottom = 44.2;
    VectorPath path;
    path.subpaths = patchy::generate_live_shape_subpaths(circle);
    goldens.push_back({"ellipse", path, 0x0ULL});
  }
  {
    VectorPath thin;
    PathSubpath triangle;
    triangle.closed = true;
    triangle.op = PathCombineOp::Add;
    triangle.anchors = {corner(3.21, 60.87), corner(33.4, 2.05), corner(35.9, 61.5)};
    thin.subpaths = {triangle};
    goldens.push_back({"triangle", thin, 0x0ULL});
  }
  {
    VectorPath curves;
    PathSubpath blob;
    blob.closed = true;
    blob.op = PathCombineOp::Add;
    PathAnchor a = corner(6.0, 10.0);
    a.smooth = true;
    a.out_x = 22.0;
    a.out_y = 14.0;
    PathAnchor b = corner(34.0, 6.0);
    b.smooth = true;
    b.in_x = 46.0;
    b.in_y = -2.0;
    b.out_x = 30.0;
    b.out_y = 30.0;
    PathAnchor c = corner(58.0, 28.0);
    c.smooth = true;
    c.in_x = 44.0;
    c.in_y = 44.0;
    blob.anchors = {a, b, c};
    curves.subpaths = {blob};
    goldens.push_back({"curved-blob", curves, 0x0ULL});
  }
  {
    VectorPath ops;
    ops.subpaths = {rect_subpath(6.5, 6.25, 40.75, 40.5, PathCombineOp::Add, 0),
                    rect_subpath(14.2, 14.4, 26.8, 26.6, PathCombineOp::Subtract, 1),
                    rect_subpath(6.0, 6.0, 58.3, 30.1, PathCombineOp::Intersect, 2),
                    rect_subpath(30.6, 18.9, 58.2, 50.7, PathCombineOp::Xor, 3)};
    goldens.push_back({"boolean-chain", ops, 0x0ULL});
  }

  const std::array<std::uint64_t, 4> expected = {
      0x4763d7d076abe813ULL,
      0x458591696e73d268ULL,
      0x77f42c3a9a574a5cULL,
      0xbcdf266e1745427eULL,
  };
  bool all_match = true;
  for (std::size_t i = 0; i < goldens.size(); ++i) {
    const auto coverage = rasterize(goldens[i].path, Rect{0, 0, 64, 64});
    CHECK(!coverage.bounds.empty());
    const auto hash = fnv1a_hash_bytes(coverage.pixels.data());
    if (hash != expected[i]) {
      std::fprintf(stderr, "golden[%zu] %s: 0x%016llxULL (bounds %d,%d %dx%d)\n", i, goldens[i].name,
                   static_cast<unsigned long long>(hash), coverage.bounds.x, coverage.bounds.y,
                   coverage.bounds.width, coverage.bounds.height);
      all_match = false;
    }
  }
  CHECK(all_match);
}

void raster_shape_paints_solid_gradient_pattern() {
  const Rect canvas{0, 0, 32, 32};
  patchy::VectorShapeContent content;
  content.path.subpaths = {rect_subpath(4, 4, 20, 20, PathCombineOp::Add, 0)};
  content.fill.kind = patchy::VectorFillKind::Solid;
  content.fill.color = patchy::RgbColor{214, 40, 40};

  const auto solid = patchy::rasterize_vector_shape(content, canvas, nullptr, nullptr);
  CHECK(solid.bounds.x == 4 && solid.bounds.y == 4);
  const auto* pixel = solid.pixels.pixel(8 - solid.bounds.x, 8 - solid.bounds.y);
  CHECK(pixel[0] == 214 && pixel[1] == 40 && pixel[2] == 40 && pixel[3] == 255);

  // Fill disabled via the stroke's fillEnabled: nothing renders.
  content.stroke.fill_enabled = false;
  CHECK(patchy::rasterize_vector_shape(content, canvas, nullptr, nullptr).bounds.empty());
  content.stroke.fill_enabled = true;

  // Gradient fill: wiring matches the shared blend_math shading directly.
  content.fill.kind = patchy::VectorFillKind::Gradient;
  auto& gradient = content.fill.gradient;
  gradient.color_stops = {{0.0F, patchy::RgbColor{255, 0, 0}, 0.5F},
                          {1.0F, patchy::RgbColor{0, 0, 255}, 0.5F}};
  gradient.alpha_stops = {{0.0F, 1.0F, 0.5F}, {1.0F, 1.0F, 0.5F}};
  gradient.angle_degrees = 0.0F;
  gradient.align_with_layer = true;
  const auto shaded = patchy::rasterize_vector_shape(content, canvas, nullptr, nullptr);
  CHECK(!shaded.bounds.empty());
  const auto position = patchy::gradient_position(gradient, shaded.bounds, 10, 10);
  const auto expected_color = patchy::gradient_color_dithered(gradient, position, 10, 10);
  const auto* shaded_pixel = shaded.pixels.pixel(10 - shaded.bounds.x, 10 - shaded.bounds.y);
  CHECK(shaded_pixel[0] == expected_color.red);
  CHECK(shaded_pixel[1] == expected_color.green);
  CHECK(shaded_pixel[2] == expected_color.blue);
  CHECK(shaded_pixel[3] == 255);

  // Pattern fill: 2x2 checker tile, unlinked (origin anchor).
  content.fill = patchy::VectorFill{};
  content.fill.kind = patchy::VectorFillKind::Pattern;
  content.fill.pattern_id = "test-checker";
  content.fill.pattern_linked = false;
  patchy::PatternStore store;
  patchy::PatternResource resource;
  resource.id = "test-checker";
  resource.name = "checker";
  resource.tile = patchy::PixelBuffer(2, 2, patchy::PixelFormat::rgba8());
  auto tile = resource.tile.data();
  const auto set_texel = [&](int x, int y, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    auto* t = resource.tile.pixel(x, y);
    t[0] = r;
    t[1] = g;
    t[2] = b;
    t[3] = 255;
  };
  (void)tile;
  set_texel(0, 0, 10, 20, 30);
  set_texel(1, 0, 200, 210, 220);
  set_texel(0, 1, 200, 210, 220);
  set_texel(1, 1, 10, 20, 30);
  store.patterns.push_back(resource);
  const auto patterned = patchy::rasterize_vector_shape(content, canvas, &store, nullptr);
  CHECK(!patterned.bounds.empty());
  const auto* texel_a = patterned.pixels.pixel(4 - patterned.bounds.x, 4 - patterned.bounds.y);
  const auto* texel_b = patterned.pixels.pixel(5 - patterned.bounds.x, 4 - patterned.bounds.y);
  CHECK(texel_a[0] == 10 && texel_a[1] == 20 && texel_a[2] == 30 && texel_a[3] == 255);
  CHECK(texel_b[0] == 200 && texel_b[3] == 255);

  // Unknown pattern id renders transparent but keeps coverage bounds.
  content.fill.pattern_id = "missing";
  const auto missing = patchy::rasterize_vector_shape(content, canvas, &store, nullptr);
  CHECK(!missing.bounds.empty());
  const auto* transparent = missing.pixels.pixel(8 - missing.bounds.x, 8 - missing.bounds.y);
  CHECK(transparent[3] == 0);

  // Fill kind None renders nothing (stroke-only shapes wait for the stroker).
  content.fill.kind = patchy::VectorFillKind::None;
  CHECK(patchy::rasterize_vector_shape(content, canvas, &store, nullptr).bounds.empty());
}

}  // namespace

std::vector<patchy::test::TestCase> vector_raster_tests() {
  return {
      {"raster_axis_aligned_rect_coverage_is_exact", raster_axis_aligned_rect_coverage_is_exact},
      {"raster_half_plane_diagonal_ramp", raster_half_plane_diagonal_ramp},
      {"raster_area_sum_matches_analytic", raster_area_sum_matches_analytic},
      {"raster_combine_ops_match_photoshop_semantics", raster_combine_ops_match_photoshop_semantics},
      {"raster_first_op_initial_accumulator_semantics", raster_first_op_initial_accumulator_semantics},
      {"raster_even_odd_within_group_ops_between_groups", raster_even_odd_within_group_ops_between_groups},
      {"raster_curves_donut_and_open_chord", raster_curves_donut_and_open_chord},
      {"raster_empty_path_fills_clip", raster_empty_path_fills_clip},
      {"raster_mask_inverted_coverage", raster_mask_inverted_coverage},
      {"raster_golden_digests_are_stable", raster_golden_digests_are_stable},
      {"raster_shape_paints_solid_gradient_pattern", raster_shape_paints_solid_gradient_pattern},
  };
}
