// Builtin custom shapes: hand-authored unit-box geometry (corner polygons and
// kappa-arc curves). Every id is persisted; append new shapes, never re-id.
#include "ui/default_custom_shapes.hpp"

#include "core/vector_live_shapes.hpp"

#include <cmath>
#include <numbers>

namespace patchy::ui {

namespace {

PathAnchor corner(double x, double y) {
  PathAnchor anchor;
  anchor.anchor_x = x;
  anchor.anchor_y = y;
  anchor.in_x = x;
  anchor.in_y = y;
  anchor.out_x = x;
  anchor.out_y = y;
  anchor.smooth = false;
  return anchor;
}

PathAnchor smooth(double x, double y, double in_dx, double in_dy, double out_dx, double out_dy) {
  PathAnchor anchor;
  anchor.anchor_x = x;
  anchor.anchor_y = y;
  anchor.in_x = x + in_dx;
  anchor.in_y = y + in_dy;
  anchor.out_x = x + out_dx;
  anchor.out_y = y + out_dy;
  anchor.smooth = true;
  return anchor;
}

// A corner anchor that still carries handles (the tangent breaks at the
// anchor, so Direct Select must not mirror them).
PathAnchor cusp(double x, double y, double in_dx, double in_dy, double out_dx, double out_dy) {
  auto anchor = smooth(x, y, in_dx, in_dy, out_dx, out_dy);
  anchor.smooth = false;
  return anchor;
}

VectorPath polygon_path(std::initializer_list<std::pair<double, double>> points) {
  VectorPath path;
  PathSubpath subpath;
  for (const auto& [x, y] : points) {
    subpath.anchors.push_back(corner(x, y));
  }
  path.subpaths.push_back(std::move(subpath));
  return path;
}

// Circle of radius r about (cx, cy) as four kappa arcs.
PathSubpath circle_subpath(double cx, double cy, double r) {
  const double k = kLiveShapeKappa * r;
  PathSubpath subpath;
  subpath.anchors = {
      smooth(cx, cy - r, -k, 0.0, k, 0.0),
      smooth(cx + r, cy, 0.0, -k, 0.0, k),
      smooth(cx, cy + r, k, 0.0, -k, 0.0),
      smooth(cx - r, cy, 0.0, k, 0.0, -k),
  };
  return subpath;
}

VectorPath star_path(int points, double outer, double inner, double cx, double cy) {
  VectorPath path;
  PathSubpath subpath;
  for (int i = 0; i < points * 2; ++i) {
    const double radius = (i % 2) == 0 ? outer : inner;
    const double angle = -std::numbers::pi / 2.0 + i * std::numbers::pi / points;
    subpath.anchors.push_back(corner(cx + radius * std::cos(angle), cy + radius * std::sin(angle)));
  }
  path.subpaths.push_back(std::move(subpath));
  return path;
}

std::vector<BuiltinCustomShape> build_shapes() {
  std::vector<BuiltinCustomShape> shapes;
  const auto add = [&shapes](const char* id, const char* name, const char* folder,
                             VectorPath path) {
    shapes.push_back(BuiltinCustomShape{id, name, folder, std::move(path)});
  };

  // --- Arrows (unit box, pointing as named) ---
  add("shape.builtin.arrow-right", "Arrow Right", "Arrows",
      polygon_path({{0.0, 0.3}, {0.6, 0.3}, {0.6, 0.1}, {1.0, 0.5}, {0.6, 0.9}, {0.6, 0.7}, {0.0, 0.7}}));
  add("shape.builtin.arrow-left", "Arrow Left", "Arrows",
      polygon_path({{1.0, 0.3}, {0.4, 0.3}, {0.4, 0.1}, {0.0, 0.5}, {0.4, 0.9}, {0.4, 0.7}, {1.0, 0.7}}));
  add("shape.builtin.arrow-up", "Arrow Up", "Arrows",
      polygon_path({{0.3, 1.0}, {0.3, 0.4}, {0.1, 0.4}, {0.5, 0.0}, {0.9, 0.4}, {0.7, 0.4}, {0.7, 1.0}}));
  add("shape.builtin.arrow-down", "Arrow Down", "Arrows",
      polygon_path({{0.3, 0.0}, {0.3, 0.6}, {0.1, 0.6}, {0.5, 1.0}, {0.9, 0.6}, {0.7, 0.6}, {0.7, 0.0}}));
  add("shape.builtin.arrow-double", "Arrow Double", "Arrows",
      polygon_path({{0.0, 0.5}, {0.3, 0.2}, {0.3, 0.38}, {0.7, 0.38}, {0.7, 0.2}, {1.0, 0.5},
                    {0.7, 0.8}, {0.7, 0.62}, {0.3, 0.62}, {0.3, 0.8}}));
  add("shape.builtin.chevron-right", "Chevron", "Arrows",
      polygon_path({{0.2, 0.0}, {0.55, 0.0}, {0.9, 0.5}, {0.55, 1.0}, {0.2, 1.0}, {0.55, 0.5}}));
  {
    // Curved arrow: a quarter-annulus band swept about a center below the
    // head, from a horizontal tail cut on the bottom edge up to a rightward
    // tangent where a barbed head (twice the band's thickness) takes over.
    // Authored in band coordinates with the sweep center at the origin, then
    // normalized onto the unit box; the affine map keeps the kappa quarter
    // arcs exact ellipse quarters with matching tangents.
    const double r_outer = 0.9;
    const double r_inner = 0.6;
    const double r_mid = (r_outer + r_inner) / 2.0;
    const double head_half_width = 0.28;
    const double head_length = 0.4;
    const double k_outer = kLiveShapeKappa * r_outer;
    const double k_inner = kLiveShapeKappa * r_inner;
    VectorPath path;
    PathSubpath subpath;
    subpath.anchors = {
        cusp(-r_outer, 0.0, 0.0, 0.0, 0.0, -k_outer),  // tail cut, outer corner
        cusp(0.0, -r_outer, -k_outer, 0.0, 0.0, 0.0),  // outer arc end (moving +x)
        corner(0.0, -r_mid - head_half_width),         // outer barb
        corner(head_length, -r_mid),                   // tip
        corner(0.0, -r_mid + head_half_width),         // inner barb
        cusp(0.0, -r_inner, 0.0, 0.0, -k_inner, 0.0),  // inner arc start
        cusp(-r_inner, 0.0, 0.0, -k_inner, 0.0, 0.0),  // tail cut, inner corner
    };
    const double scale_x = 1.0 / (r_outer + head_length);
    const double scale_y = 1.0 / (r_mid + head_half_width);
    for (auto& anchor : subpath.anchors) {
      for (double* x : {&anchor.anchor_x, &anchor.in_x, &anchor.out_x}) {
        *x = (*x + r_outer) * scale_x;
      }
      for (double* y : {&anchor.anchor_y, &anchor.in_y, &anchor.out_y}) {
        *y = (*y + r_mid + head_half_width) * scale_y;
      }
    }
    path.subpaths.push_back(std::move(subpath));
    add("shape.builtin.arrow-curved", "Arrow Curved", "Arrows", std::move(path));
  }

  // --- Symbols ---
  {
    // Heart: near-circular crowns with horizontal tangents at the top, a
    // cusp notch between them, and a cusp bottom point joined by gently
    // bowed sides (vertical tangents at the widest point).
    VectorPath path;
    PathSubpath subpath;
    subpath.anchors = {
        cusp(0.5, 0.18, 0.055, -0.09, -0.055, -0.09),
        smooth(0.26, 0.0, 0.105, 0.0, -0.13, 0.0),
        smooth(0.0, 0.3, 0.0, -0.155, 0.0, 0.16),
        cusp(0.5, 1.0, -0.27, -0.16, 0.27, -0.16),
        smooth(1.0, 0.3, 0.0, 0.16, 0.0, -0.155),
        smooth(0.74, 0.0, 0.13, 0.0, -0.105, 0.0),
    };
    path.subpaths.push_back(std::move(subpath));
    add("shape.builtin.heart", "Heart", "Symbols", std::move(path));
  }
  add("shape.builtin.star", "Star", "Symbols", star_path(5, 0.5, 0.19, 0.5, 0.5));
  add("shape.builtin.check", "Check Mark", "Symbols",
      polygon_path({{0.0, 0.55}, {0.15, 0.4}, {0.35, 0.6}, {0.85, 0.05}, {1.0, 0.2}, {0.35, 0.95}}));
  add("shape.builtin.cross", "Cross", "Symbols",
      polygon_path({{0.15, 0.0}, {0.5, 0.35}, {0.85, 0.0}, {1.0, 0.15}, {0.65, 0.5}, {1.0, 0.85},
                    {0.85, 1.0}, {0.5, 0.65}, {0.15, 1.0}, {0.0, 0.85}, {0.35, 0.5}, {0.0, 0.15}}));
  add("shape.builtin.plus", "Plus", "Symbols",
      polygon_path({{0.35, 0.0}, {0.65, 0.0}, {0.65, 0.35}, {1.0, 0.35}, {1.0, 0.65}, {0.65, 0.65},
                    {0.65, 1.0}, {0.35, 1.0}, {0.35, 0.65}, {0.0, 0.65}, {0.0, 0.35}, {0.35, 0.35}}));
  add("shape.builtin.diamond", "Diamond", "Symbols",
      polygon_path({{0.5, 0.0}, {1.0, 0.5}, {0.5, 1.0}, {0.0, 0.5}}));
  add("shape.builtin.triangle", "Triangle", "Symbols",
      polygon_path({{0.5, 0.0}, {1.0, 1.0}, {0.0, 1.0}}));
  {
    // Speech bubble: rounded rectangle (kappa corners) with a tail.
    const double r = 0.18;
    const double k = kLiveShapeKappa * r;
    VectorPath path;
    PathSubpath subpath;
    subpath.anchors = {
        smooth(r, 0.0, -k, 0.0, 0.0, 0.0),
        smooth(1.0 - r, 0.0, 0.0, 0.0, k, 0.0),
        smooth(1.0, r, 0.0, -k, 0.0, 0.0),
        smooth(1.0, 0.75 - r, 0.0, 0.0, 0.0, k),
        smooth(1.0 - r, 0.75, k, 0.0, 0.0, 0.0),
        corner(0.45, 0.75),
        corner(0.2, 1.0),
        corner(0.25, 0.75),
        smooth(r, 0.75, 0.0, 0.0, -k, 0.0),
        smooth(0.0, 0.75 - r, 0.0, k, 0.0, 0.0),
        smooth(0.0, r, 0.0, 0.0, 0.0, -k),
    };
    path.subpaths.push_back(std::move(subpath));
    add("shape.builtin.speech-bubble", "Speech Bubble", "Symbols", std::move(path));
  }
  add("shape.builtin.lightning", "Lightning Bolt", "Symbols",
      polygon_path({{0.55, 0.0}, {0.2, 0.55}, {0.42, 0.55}, {0.3, 1.0}, {0.8, 0.4}, {0.55, 0.4},
                    {0.75, 0.0}}));
  {
    // Ring: concentric circles in one shape group; even-odd leaves the hole.
    VectorPath path;
    path.subpaths.push_back(circle_subpath(0.5, 0.5, 0.5));
    path.subpaths.push_back(circle_subpath(0.5, 0.5, 0.3));
    add("shape.builtin.ring", "Ring", "Symbols", std::move(path));
  }
  return shapes;
}

}  // namespace

const std::vector<BuiltinCustomShape>& builtin_custom_shapes() {
  static const std::vector<BuiltinCustomShape> shapes = build_shapes();
  return shapes;
}

}  // namespace patchy::ui
