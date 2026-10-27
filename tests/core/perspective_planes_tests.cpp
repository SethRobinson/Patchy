#include "core/perspective_planes.hpp"
#include "test_groups.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using patchy::Homography;
using patchy::PerspectiveCamera;
using patchy::PerspectiveCanvas;
using patchy::PerspectivePlane;
using patchy::PerspectiveSampler;
using patchy::PerspectiveStrokeKind;
using patchy::PerspectiveStrokeSetup;
using patchy::PixelBuffer;
using patchy::PixelFormat;
using patchy::PlaneGeometry;
using patchy::PlanePoint;
using patchy::PlaneQuad;
using patchy::PlaneQuadProblem;
using patchy::Rect;

bool near(double a, double b, double tolerance = 1.0e-6) { return std::abs(a - b) <= tolerance; }

PixelBuffer solid(std::int32_t width, std::int32_t height, std::array<std::uint8_t, 4> color) {
  PixelBuffer pixels(width, height, PixelFormat::rgba8());
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      auto* p = pixels.pixel(x, y);
      p[0] = color[0];
      p[1] = color[1];
      p[2] = color[2];
      p[3] = color[3];
    }
  }
  return pixels;
}

// Four flat quadrants: red top left, green top right, blue bottom right, white bottom left.
PixelBuffer quadrants(std::int32_t size) {
  PixelBuffer pixels(size, size, PixelFormat::rgba8());
  for (std::int32_t y = 0; y < size; ++y) {
    for (std::int32_t x = 0; x < size; ++x) {
      auto* p = pixels.pixel(x, y);
      const bool right = x >= size / 2;
      const bool lower = y >= size / 2;
      const std::array<std::uint8_t, 3> color = !lower ? (right ? std::array<std::uint8_t, 3>{0, 255, 0}
                                                                : std::array<std::uint8_t, 3>{255, 0, 0})
                                                       : (right ? std::array<std::uint8_t, 3>{0, 0, 255}
                                                                : std::array<std::uint8_t, 3>{255, 255, 255});
      p[0] = color[0];
      p[1] = color[1];
      p[2] = color[2];
      p[3] = 255;
    }
  }
  return pixels;
}

std::uint64_t digest(const PixelBuffer& pixels) {
  std::uint64_t hash = 1469598103934665603ULL;
  for (const auto byte : pixels.data()) {
    hash = (hash ^ byte) * 1099511628211ULL;
  }
  return hash;
}

// A floor-like trapezoid: wide at the bottom (near), narrow at the top (far).
const PlaneQuad kTrapezoid{{{70.0, 40.0}, {170.0, 40.0}, {220.0, 180.0}, {20.0, 180.0}}};

PlaneGeometry geometry_of(const PlaneQuad& quad, std::int32_t width = 240, std::int32_t height = 200) {
  const auto camera = patchy::default_perspective_camera(width, height);
  const auto geometry = patchy::plane_geometry(quad, camera);
  CHECK(geometry.has_value());
  return *geometry;
}

void perspective_planes_homography_maps_the_unit_square_and_inverts() {
  const auto h = patchy::homography_from_unit_square(kTrapezoid);
  CHECK(h.has_value());
  const std::array<PlanePoint, 4> unit{{{0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}}};
  for (std::size_t i = 0; i < 4; ++i) {
    const auto mapped = h->map(unit[i]);
    CHECK(mapped.has_value());
    CHECK(near(mapped->x, kTrapezoid[i].x) && near(mapped->y, kTrapezoid[i].y));
  }
  const auto inverse = h->inverse();
  CHECK(inverse.has_value());
  const auto back = inverse->map({120.0, 110.0});
  CHECK(back.has_value());
  const auto again = h->map(*back);
  CHECK(again.has_value());
  CHECK(near(again->x, 120.0) && near(again->y, 110.0));
  // A square maps to itself.
  const auto square = patchy::homography_from_unit_square({{{0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}}});
  CHECK(square.has_value());
  const auto same = square->map({0.25, 0.75});
  CHECK(same.has_value() && near(same->x, 0.25) && near(same->y, 0.75));
  // The derivative matches a finite difference.
  const auto j = h->jacobian({0.4, 0.6});
  const auto a = h->map({0.4, 0.6});
  const auto b = h->map({0.4 + 1.0e-6, 0.6});
  CHECK(a.has_value() && b.has_value());
  CHECK(near(j[0], (b->x - a->x) / 1.0e-6, 1.0e-3));
  CHECK(near(j[2], (b->y - a->y) / 1.0e-6, 1.0e-3));
}

void perspective_planes_quad_check_names_why_corners_fail() {
  CHECK(patchy::check_plane_quad(kTrapezoid) == PlaneQuadProblem::None);
  // Reversed order is still one surface.
  CHECK(patchy::check_plane_quad({{kTrapezoid[3], kTrapezoid[2], kTrapezoid[1], kTrapezoid[0]}}) ==
        PlaneQuadProblem::None);
  // Corners clicked across the diagonal: two sides cross.
  CHECK(patchy::check_plane_quad({{{0.0, 0.0}, {100.0, 100.0}, {100.0, 0.0}, {0.0, 100.0}}}) ==
        PlaneQuadProblem::Crossing);
  // A dart: one corner points inward.
  CHECK(patchy::check_plane_quad({{{0.0, 0.0}, {100.0, 0.0}, {30.0, 30.0}, {0.0, 100.0}}}) ==
        PlaneQuadProblem::Concave);
  // Three corners in a line.
  CHECK(patchy::check_plane_quad({{{0.0, 0.0}, {50.0, 0.0}, {100.0, 0.0}, {50.0, 80.0}}}) ==
        PlaneQuadProblem::Concave);
  CHECK(patchy::check_plane_quad({{{0.0, 0.0}, {2.0, 0.0}, {2.0, 2.0}, {0.0, 2.0}}}) == PlaneQuadProblem::TooSmall);
}

void perspective_planes_sin_cos_is_exact_on_quarter_turns() {
  const auto [s0, c0] = patchy::perspective_sin_cos_degrees(0.0);
  CHECK(s0 == 0.0 && c0 == 1.0);
  const auto [s90, c90] = patchy::perspective_sin_cos_degrees(90.0);
  CHECK(s90 == 1.0 && c90 == 0.0);
  const auto [s180, c180] = patchy::perspective_sin_cos_degrees(-180.0);
  CHECK(s180 == 0.0 && c180 == -1.0);
  const auto [s30, c30] = patchy::perspective_sin_cos_degrees(30.0);
  CHECK(near(s30, 0.5, 1.0e-14) && near(c30, std::sqrt(3.0) / 2.0, 1.0e-14));
  const auto [s350, c350] = patchy::perspective_sin_cos_degrees(350.0);
  CHECK(near(s350, -0.17364817766693033, 1.0e-14) && near(c350, 0.98480775301220802, 1.0e-14));
}

// Projects a 3D rectangle (width 3, height 2, tilted away from the viewer) through a known
// lens: the plane keeps the rectangle's 3:2 proportions and the lens comes back.
void perspective_planes_plane_geometry_recovers_proportions_and_lens() {
  const PerspectiveCamera camera{800.0, {400.0, 300.0}};
  const auto project = [&camera](double x, double y, double z) {
    return PlanePoint{camera.focal * x / z + camera.center.x, camera.focal * y / z + camera.center.y};
  };
  // Rotate the rectangle 50 degrees about the x axis and 20 about y, 6 units away.
  const auto [s1, c1] = patchy::perspective_sin_cos_degrees(50.0);
  const auto [s2, c2] = patchy::perspective_sin_cos_degrees(20.0);
  const auto point = [&](double u, double v) {
    const double x = u - 1.5;
    const double y = v - 1.0;
    const double y1 = y * c1;
    const double z1 = y * s1;
    const double x2 = x * c2 + z1 * s2;
    const double z2 = -x * s2 + z1 * c2;
    return project(x2, y1, z2 + 6.0);
  };
  const PlaneQuad quad{{point(0, 0), point(3, 0), point(3, 2), point(0, 2)}};
  const auto focal = patchy::plane_focal_length(quad, camera.center);
  CHECK(focal.has_value());
  CHECK(near(*focal, 800.0, 1.0e-6));
  const auto geometry = patchy::plane_geometry(quad, camera);
  CHECK(geometry.has_value());
  CHECK(near(geometry->width / geometry->height, 1.5, 1.0e-9));
  // The camera for a set of planes takes the lens from its first free plane.
  std::vector<PerspectivePlane> planes{{3, quad, 0.0, std::nullopt}};
  const auto shared = patchy::perspective_camera_for(planes, 800, 600);
  CHECK(near(shared.focal, 800.0, 1.0e-6));
  // A fronto-parallel 200 x 100 rectangle maps one plane unit to one pixel.
  const auto flat = patchy::plane_geometry({{{10.0, 20.0}, {210.0, 20.0}, {210.0, 120.0}, {10.0, 120.0}}}, camera);
  CHECK(flat.has_value());
  CHECK(near(flat->width, 200.0, 1.0e-9) && near(flat->height, 100.0, 1.0e-9));
  const auto corner = flat->to_image.map({200.0, 100.0});
  CHECK(corner.has_value() && near(corner->x, 210.0) && near(corner->y, 120.0));
  // A quad drawn counterclockwise is reordered so plane space is never mirrored.
  const auto reversed = patchy::plane_geometry({{{10.0, 20.0}, {10.0, 120.0}, {210.0, 120.0}, {210.0, 20.0}}}, camera);
  CHECK(reversed.has_value());
  const auto right = reversed->to_image.map({reversed->width, 0.0});
  CHECK(right.has_value() && right->x > 100.0);
}

void perspective_planes_paste_fills_a_known_quad() {
  const auto geometry = geometry_of(kTrapezoid);
  PerspectiveCanvas canvas(solid(240, 200, {0, 0, 0, 0}), Rect{0, 0, 240, 200}, 240, 200);
  const auto image = quadrants(64);
  const auto change = canvas.paste(geometry, image, patchy::fill_plane_from_image(geometry, 64, 64));
  CHECK(!change.empty());
  const auto& edits = canvas.edits();
  const auto color_at = [&edits](int x, int y) {
    const auto* p = edits.pixel(x, y);
    return std::array<int, 4>{p[0], p[1], p[2], p[3]};
  };
  // Just inside each corner of the trapezoid: that corner's quadrant color, opaque.
  CHECK((color_at(78, 46) == std::array<int, 4>{255, 0, 0, 255}));
  CHECK((color_at(162, 46) == std::array<int, 4>{0, 255, 0, 255}));
  CHECK((color_at(208, 174) == std::array<int, 4>{0, 0, 255, 255}));
  CHECK((color_at(32, 174) == std::array<int, 4>{255, 255, 255, 255}));
  // Outside the quad nothing is written.
  CHECK(color_at(20, 50)[3] == 0);
  CHECK(color_at(230, 190)[3] == 0);
  CHECK(color_at(120, 30)[3] == 0);
  // The quadrants meet where the plane's center maps: the far half is foreshortened, so
  // the red/white boundary on the left side sits above the quad's vertical middle.
  const auto middle = geometry.to_image.map({geometry.width * 0.5, geometry.height * 0.5});
  CHECK(middle.has_value());
  CHECK(middle->y < 110.0);
  CHECK((color_at(static_cast<int>(middle->x) - 6, static_cast<int>(middle->y) - 6) == std::array<int, 4>{255, 0, 0, 255}));
  CHECK((color_at(static_cast<int>(middle->x) + 6, static_cast<int>(middle->y) + 6) == std::array<int, 4>{0, 0, 255, 255}));
}

// Constraint (docs/legal-constraints.md, "Perspective editing"): an edit is clipped to the
// one plane it starts on and never continues onto a neighbouring plane.
void perspective_planes_edits_never_leave_their_plane() {
  // Two planes sharing the edge x = 120: a wall on the left, a wall turning away on the right.
  const PlaneQuad left{{{20.0, 30.0}, {120.0, 50.0}, {120.0, 170.0}, {20.0, 190.0}}};
  const PlaneQuad right{{{120.0, 50.0}, {200.0, 30.0}, {200.0, 190.0}, {120.0, 170.0}}};
  const auto left_plane = geometry_of(left);
  const auto right_plane = geometry_of(right);
  // How far a pixel center lies inside a plane, in pixels (negative outside).
  const auto inside_by = [](const PlaneGeometry& plane, int x, int y) {
    const PlanePoint center{x + 0.5, y + 0.5};
    const auto p = plane.to_plane.map(center);
    if (!p.has_value()) {
      return -1.0e9;
    }
    const auto j = plane.to_plane.jacobian(center);
    const double gu = std::sqrt(j[0] * j[0] + j[1] * j[1]);
    const double gv = std::sqrt(j[2] * j[2] + j[3] * j[3]);
    return std::min({p->x / gu, (plane.width - p->x) / gu, p->y / gv, (plane.height - p->y) / gv});
  };
  const auto outside_left = [&](const PixelBuffer& edits) {
    int written = 0;
    for (int y = 0; y < edits.height(); ++y) {
      for (int x = 0; x < edits.width(); ++x) {
        // Only the half-pixel anti-aliased rim may be written outside the plane edge.
        if (edits.pixel(x, y)[3] != 0 && inside_by(left_plane, x, y) < -0.5) {
          ++written;
        }
      }
    }
    return written;
  };
  {
    // A paste placed half across the shared edge, much larger than the plane.
    PerspectiveCanvas canvas(solid(240, 200, {0, 0, 0, 0}), Rect{0, 0, 240, 200}, 240, 200);
    patchy::PerspectivePlacement placement;
    placement.center_x = left_plane.width;
    placement.center_y = left_plane.height * 0.5;
    placement.scale = 4.0;
    canvas.paste(left_plane, solid(64, 64, {200, 10, 10, 255}), patchy::placement_to_plane(placement, 64, 64));
    CHECK(canvas.edits().pixel(60, 110)[3] == 255);
    CHECK(outside_left(canvas.edits()) == 0);
    CHECK(canvas.edits().pixel(160, 110)[3] == 0);
  }
  for (const auto kind : {PerspectiveStrokeKind::Paint, PerspectiveStrokeKind::Clone}) {
    // A stroke dragged from the left plane well into the right one.
    PerspectiveCanvas canvas(quadrants(240), Rect{0, 0, 240, 240}, 240, 200);
    PerspectiveStrokeSetup setup;
    setup.kind = kind;
    setup.plane = left_plane;
    setup.brush.size = 40.0;
    setup.brush.hardness = 100;
    setup.color = {10, 200, 10, 255};
    setup.source_plane = left_plane;
    setup.source_offset = {-30.0, 0.0};
    canvas.begin_stroke(setup);
    canvas.stroke_to({40.0, left_plane.height * 0.5});
    // Plane coordinates past the shared edge: the right plane's pixels.
    canvas.stroke_to({left_plane.width * 2.5, left_plane.height * 0.5});
    const auto change = canvas.end_stroke();
    CHECK(!change.empty());
    CHECK(outside_left(canvas.edits()) == 0);
    int right_written = 0;
    for (int y = 0; y < 200; ++y) {
      for (int x = 0; x < 240; ++x) {
        if (inside_by(right_plane, x, y) > 0.5 && canvas.edits().pixel(x, y)[3] != 0) {
          ++right_written;
        }
      }
    }
    CHECK(right_written == 0);
  }
}

void perspective_planes_clone_reads_the_source_through_its_own_plane() {
  // A fronto-parallel plane: plane units are pixels, so a clone is a plain offset copy.
  const PlaneQuad flat{{{10.0, 10.0}, {210.0, 10.0}, {210.0, 190.0}, {10.0, 190.0}}};
  const auto plane = geometry_of(flat);
  PixelBuffer layer(240, 200, PixelFormat::rgba8());
  for (int y = 0; y < 200; ++y) {
    for (int x = 0; x < 240; ++x) {
      auto* p = layer.pixel(x, y);
      p[0] = static_cast<std::uint8_t>(x);
      p[1] = static_cast<std::uint8_t>(y);
      p[2] = 77;
      p[3] = 255;
    }
  }
  PerspectiveCanvas canvas(layer, Rect{0, 0, 240, 200}, 240, 200);
  PerspectiveStrokeSetup setup;
  setup.kind = PerspectiveStrokeKind::Clone;
  setup.plane = plane;
  setup.brush.size = 30.0;
  setup.brush.hardness = 100;
  setup.source_plane = plane;
  setup.source_offset = {-60.0, 20.0};
  canvas.begin_stroke(setup);
  canvas.stroke_to({140.0, 80.0});
  canvas.end_stroke();
  // The dab center (document 150, 90) now shows the source at (90, 110).
  const auto* center = canvas.composite().pixel(150, 90);
  CHECK(std::abs(center[0] - 90) <= 1 && std::abs(center[1] - 110) <= 1 && center[2] == 77);
  // A source offset that lands outside the plane copies nothing.
  PerspectiveCanvas outside(layer, Rect{0, 0, 240, 200}, 240, 200);
  setup.source_offset = {-180.0, 0.0};
  outside.begin_stroke(setup);
  outside.stroke_to({140.0, 80.0});
  outside.end_stroke();
  CHECK(outside.edits().pixel(150, 90)[3] == 0);
  // Paint lays the color with the brush's opacity.
  PerspectiveCanvas painted(layer, Rect{0, 0, 240, 200}, 240, 200);
  setup.kind = PerspectiveStrokeKind::Paint;
  setup.color = {0, 0, 0, 255};
  setup.brush.opacity = 50;
  painted.begin_stroke(setup);
  painted.stroke_to({140.0, 80.0});
  painted.stroke_to({141.0, 80.0});
  painted.end_stroke();
  const auto* dab = painted.edits().pixel(150, 90);
  CHECK(dab[3] >= 126 && dab[3] <= 129 && dab[0] == 0);
}

void perspective_planes_changes_undo_and_redo_exactly() {
  const auto geometry = geometry_of(kTrapezoid);
  PerspectiveCanvas canvas(solid(240, 200, {30, 60, 90, 255}), Rect{0, 0, 240, 200}, 240, 200);
  const auto before = digest(canvas.composite());
  const auto change = canvas.paste(geometry, quadrants(32), patchy::fill_plane_from_image(geometry, 32, 32));
  CHECK(!change.empty());
  const auto after_edits = digest(canvas.edits());
  const auto after = digest(canvas.composite());
  CHECK(after != before);
  canvas.revert(change);
  CHECK(digest(canvas.composite()) == before);
  CHECK(!canvas.cropped_edits().has_value());
  canvas.reapply(change);
  CHECK(digest(canvas.edits()) == after_edits);
  CHECK(digest(canvas.composite()) == after);
  // The edits alone, cropped, and the edits flattened onto the layer.
  const auto cropped = canvas.cropped_edits();
  CHECK(cropped.has_value());
  CHECK(cropped->second.x >= 18 && cropped->second.x + cropped->second.width <= 223);
  Rect bounds{};
  const auto merged = patchy::composite_edits_onto_layer(solid(240, 200, {30, 60, 90, 255}), Rect{0, 0, 240, 200},
                                                         cropped->first, cropped->second, bounds);
  CHECK(bounds.x == 0 && bounds.width == 240);
  CHECK(digest(merged) == after);
}

void perspective_planes_connected_plane_hinges_on_the_edge() {
  const PerspectiveCamera camera = patchy::default_perspective_camera(800, 600);
  const PlaneQuad floor{{{300.0, 300.0}, {500.0, 300.0}, {600.0, 500.0}, {200.0, 500.0}}};
  // Up from the floor's far edge.
  const auto wall = patchy::connected_plane_quad(floor, 0, 90.0, 1.0, camera);
  CHECK(wall.has_value());
  CHECK(near((*wall)[0].x, floor[0].x, 1.0e-6) && near((*wall)[0].y, floor[0].y, 1.0e-6));
  CHECK(near((*wall)[1].x, floor[1].x, 1.0e-6) && near((*wall)[1].y, floor[1].y, 1.0e-6));
  CHECK((*wall)[2].y < 300.0 && (*wall)[3].y < 300.0);
  // Flat continuation: the new far corners lie on the floor's side lines, beyond the edge.
  const auto flat = patchy::connected_plane_quad(floor, 0, 180.0, 0.5, camera);
  CHECK(flat.has_value());
  const auto on_line = [](PlanePoint a, PlanePoint b, PlanePoint p) {
    const double span = std::sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
    return std::abs((b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x)) / span < 1.0e-6;
  };
  CHECK(on_line(floor[2], floor[1], (*flat)[2]));
  CHECK(on_line(floor[3], floor[0], (*flat)[3]));
  CHECK((*flat)[2].y < 300.0);
  // Dragging the far edge finds the depth that put it there.
  const PlanePoint far_middle{((*wall)[2].x + (*wall)[3].x) * 0.5, ((*wall)[2].y + (*wall)[3].y) * 0.5};
  const auto depth = patchy::connected_plane_depth_at(floor, 0, 90.0, camera, far_middle);
  CHECK(depth.has_value());
  CHECK(near(*depth, 1.0, 1.0e-6));
  // Linked planes follow their parent.
  std::vector<PerspectivePlane> planes{{1, floor, 0.0, std::nullopt},
                                       {2, *wall, 0.0, patchy::PlaneLink{1, 0, 90.0, 1.0}}};
  planes[0].corners[0].x -= 20.0;
  patchy::update_connected_planes(planes, camera);
  CHECK(near(planes[1].corners[0].x, 280.0, 1.0e-6));
  // Continuing the near edge flat toward the viewer far enough to pass behind is refused.
  CHECK(!patchy::connected_plane_quad(floor, 2, 180.0, 50.0, camera).has_value());
}

void perspective_planes_sampler_filters_strong_foreshortening() {
  // A fine checker seen far away averages to gray instead of aliasing to one color.
  PixelBuffer checker(256, 256, PixelFormat::rgba8());
  for (int y = 0; y < 256; ++y) {
    for (int x = 0; x < 256; ++x) {
      auto* p = checker.pixel(x, y);
      const std::uint8_t v = ((x + y) % 2) == 0 ? 255 : 0;
      p[0] = p[1] = p[2] = v;
      p[3] = 255;
    }
  }
  const PerspectiveSampler sampler(checker);
  const auto sharp = sampler.sample(10.5, 10.5, 1.0);
  CHECK(sharp[0] == 255U * 256U);
  const auto filtered = sampler.sample(64.0, 64.0, 8.0);
  CHECK(filtered[0] > 120U * 256U && filtered[0] < 136U * 256U);
  CHECK(filtered[3] == 255U * 256U);
  // Past the edge the image fades to transparent.
  CHECK(sampler.sample(-3.0, 10.0, 1.0)[3] == 0U);
}

void perspective_planes_results_are_deterministic() {
  const auto run = [] {
    const auto geometry = geometry_of(kTrapezoid);
    PerspectiveCanvas canvas(quadrants(200), Rect{20, 0, 200, 200}, 240, 200);
    patchy::PerspectivePlacement placement = patchy::fit_placement(geometry, 64, 48);
    placement.rotation_degrees += 17.0;
    canvas.paste(geometry, quadrants(64), patchy::placement_to_plane(placement, 64, 48));
    PerspectiveStrokeSetup setup;
    setup.kind = PerspectiveStrokeKind::Clone;
    setup.plane = geometry;
    setup.brush = {24.0, 40, 80};
    setup.source_plane = geometry;
    setup.source_offset = {-25.0, 10.0};
    canvas.begin_stroke(setup);
    for (int i = 0; i <= 10; ++i) {
      canvas.stroke_to({30.0 + i * 9.0, 40.0 + i * 3.5});
    }
    canvas.end_stroke();
    return digest(canvas.composite());
  };
  const auto first = run();
  CHECK(first == run());
  // Pinned on MSVC x64; every toolchain must produce the same pixels (docs/platform.md,
  // "Floating-point determinism").
  if (first != 0x96682f044a270186ULL) {
    std::printf("perspective_planes_results_are_deterministic digest %016llx\n",
                static_cast<unsigned long long>(first));
  }
  CHECK(first == 0x96682f044a270186ULL);
}

}  // namespace

std::vector<patchy::test::TestCase> perspective_planes_tests() {
  return {
      {"perspective_planes_homography_maps_the_unit_square_and_inverts", perspective_planes_homography_maps_the_unit_square_and_inverts},
      {"perspective_planes_quad_check_names_why_corners_fail", perspective_planes_quad_check_names_why_corners_fail},
      {"perspective_planes_sin_cos_is_exact_on_quarter_turns", perspective_planes_sin_cos_is_exact_on_quarter_turns},
      {"perspective_planes_plane_geometry_recovers_proportions_and_lens",
       perspective_planes_plane_geometry_recovers_proportions_and_lens},
      {"perspective_planes_paste_fills_a_known_quad", perspective_planes_paste_fills_a_known_quad},
      {"perspective_planes_edits_never_leave_their_plane", perspective_planes_edits_never_leave_their_plane},
      {"perspective_planes_clone_reads_the_source_through_its_own_plane",
       perspective_planes_clone_reads_the_source_through_its_own_plane},
      {"perspective_planes_changes_undo_and_redo_exactly", perspective_planes_changes_undo_and_redo_exactly},
      {"perspective_planes_connected_plane_hinges_on_the_edge", perspective_planes_connected_plane_hinges_on_the_edge},
      {"perspective_planes_sampler_filters_strong_foreshortening", perspective_planes_sampler_filters_strong_foreshortening},
      {"perspective_planes_results_are_deterministic", perspective_planes_results_are_deterministic},
  };
}
