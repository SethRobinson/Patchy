#include "core/warp_mesh.hpp"

#include <algorithm>
#include <cmath>

namespace patchy {

namespace {

// Bernstein basis of degree (order-1) at t, into weights[0..order-1].
void bernstein_weights(int order, double t, std::array<double, 4>& weights) {
  const double s = 1.0 - t;
  switch (order) {
    case 2:
      weights = {s, t, 0.0, 0.0};
      return;
    case 3:
      weights = {s * s, 2.0 * s * t, t * t, 0.0};
      return;
    default:  // 4 (cubic)
      weights = {s * s * s, 3.0 * s * s * t, 3.0 * s * t * t, t * t * t};
      return;
  }
}

}  // namespace

WarpMeshGrid identity_warp_mesh(double left, double top, double right, double bottom, int u_order,
                                int v_order) {
  WarpMeshGrid mesh;
  mesh.u_order = std::clamp(u_order, 2, 4);
  mesh.v_order = std::clamp(v_order, 2, 4);
  mesh.xs.reserve(static_cast<std::size_t>(mesh.u_order * mesh.v_order));
  mesh.ys.reserve(mesh.xs.capacity());
  for (int row = 0; row < mesh.v_order; ++row) {
    const double fv = mesh.v_order == 1 ? 0.0 : static_cast<double>(row) / (mesh.v_order - 1);
    for (int column = 0; column < mesh.u_order; ++column) {
      const double fu = mesh.u_order == 1 ? 0.0 : static_cast<double>(column) / (mesh.u_order - 1);
      mesh.xs.push_back(left + (right - left) * fu);
      mesh.ys.push_back(top + (bottom - top) * fv);
    }
  }
  return mesh;
}

std::array<double, 2> evaluate_warp_mesh(const WarpMeshGrid& mesh, double u, double v) {
  std::array<double, 4> wu{};
  std::array<double, 4> wv{};
  bernstein_weights(mesh.u_order, u, wu);
  bernstein_weights(mesh.v_order, v, wv);
  double x = 0.0;
  double y = 0.0;
  for (int row = 0; row < mesh.v_order; ++row) {
    for (int column = 0; column < mesh.u_order; ++column) {
      const auto index = static_cast<std::size_t>(row * mesh.u_order + column);
      const double weight = wu[static_cast<std::size_t>(column)] * wv[static_cast<std::size_t>(row)];
      x += weight * mesh.xs[index];
      y += weight * mesh.ys[index];
    }
  }
  return {x, y};
}

namespace {

// One exact Bezier degree elevation step along a row of control points.
void elevate_row(const std::vector<double>& source, int count, std::vector<double>& target) {
  target.push_back(source[0]);
  for (int i = 1; i < count; ++i) {
    const double alpha = static_cast<double>(i) / count;
    target.push_back(alpha * source[static_cast<std::size_t>(i - 1)] +
                     (1.0 - alpha) * source[static_cast<std::size_t>(i)]);
  }
  target.push_back(source[static_cast<std::size_t>(count - 1)]);
}

WarpMeshGrid elevate_u(const WarpMeshGrid& mesh) {
  WarpMeshGrid result;
  result.u_order = mesh.u_order + 1;
  result.v_order = mesh.v_order;
  for (int row = 0; row < mesh.v_order; ++row) {
    std::vector<double> row_xs(mesh.xs.begin() + row * mesh.u_order,
                               mesh.xs.begin() + (row + 1) * mesh.u_order);
    std::vector<double> row_ys(mesh.ys.begin() + row * mesh.u_order,
                               mesh.ys.begin() + (row + 1) * mesh.u_order);
    elevate_row(row_xs, mesh.u_order, result.xs);
    elevate_row(row_ys, mesh.u_order, result.ys);
  }
  return result;
}

WarpMeshGrid transpose_mesh(const WarpMeshGrid& mesh) {
  WarpMeshGrid result;
  result.u_order = mesh.v_order;
  result.v_order = mesh.u_order;
  result.xs.resize(mesh.xs.size());
  result.ys.resize(mesh.ys.size());
  for (int row = 0; row < mesh.v_order; ++row) {
    for (int column = 0; column < mesh.u_order; ++column) {
      const auto from = static_cast<std::size_t>(row * mesh.u_order + column);
      const auto to = static_cast<std::size_t>(column * mesh.v_order + row);
      result.xs[to] = mesh.xs[from];
      result.ys[to] = mesh.ys[from];
    }
  }
  return result;
}

}  // namespace

WarpMeshGrid elevate_warp_mesh_to_cubic(const WarpMeshGrid& mesh) {
  WarpMeshGrid current = mesh;
  while (current.u_order < 4) {
    current = elevate_u(current);
  }
  while (current.v_order < 4) {
    current = transpose_mesh(elevate_u(transpose_mesh(current)));
  }
  return current;
}

namespace {

// One row of the classic cubic circle-arc approximation: the arc with horizontal
// chord (ax,y)..(bx,y), central angle 2*theta, bulging toward -y when bulge_up
// (+y is down in content space). Handle length is (4/3)tan(theta/2)*radius with
// endpoint tangents rotated by theta off the chord (E9 captures match this exactly).
void append_arc_row(WarpMeshGrid& mesh, double ax, double bx, double y, double theta, bool bulge_up) {
  const double sin_theta = std::sin(theta);
  const double cos_theta = std::cos(theta);
  const double radius = (bx - ax) / (2.0 * sin_theta);
  const double handle = (4.0 / 3.0) * std::tan(theta / 2.0) * radius;
  const double dy = bulge_up ? -handle * sin_theta : handle * sin_theta;
  mesh.xs.insert(mesh.xs.end(), {ax, ax + handle * cos_theta, bx - handle * cos_theta, bx});
  mesh.ys.insert(mesh.ys.end(), {y, y + dy, y + dy, y});
}

void append_identity_row(WarpMeshGrid& mesh, double width, double y) {
  mesh.xs.insert(mesh.xs.end(), {0.0, width / 3.0, 2.0 * width / 3.0, width});
  mesh.ys.insert(mesh.ys.end(), {y, y, y, y});
}

// Mirrors a 4x2 mesh vertically (y -> height - y, rows swapped): Photoshop's
// negative-bend arc is exactly the positive arc flipped.
WarpMeshGrid mirror_mesh_vertically(const WarpMeshGrid& mesh, double height) {
  WarpMeshGrid mirrored;
  mirrored.u_order = mesh.u_order;
  mirrored.v_order = mesh.v_order;
  mirrored.xs.resize(mesh.xs.size());
  mirrored.ys.resize(mesh.ys.size());
  for (int row = 0; row < mesh.v_order; ++row) {
    const int source_row = mesh.v_order - 1 - row;
    for (int column = 0; column < mesh.u_order; ++column) {
      const auto to = static_cast<std::size_t>(row * mesh.u_order + column);
      const auto from = static_cast<std::size_t>(source_row * mesh.u_order + column);
      mirrored.xs[to] = mesh.xs[from];
      mirrored.ys[to] = height - mesh.ys[from];
    }
  }
  return mirrored;
}

// warpRotate == Vrtc: the horizontal construction over swapped dimensions with x/y
// swapped back and the layout transposed (pinned by the e9 vrtc capture).
WarpMeshGrid swap_mesh_axes(const WarpMeshGrid& mesh) {
  WarpMeshGrid swapped;
  swapped.u_order = mesh.v_order;
  swapped.v_order = mesh.u_order;
  swapped.xs.resize(mesh.xs.size());
  swapped.ys.resize(mesh.ys.size());
  for (int row = 0; row < mesh.v_order; ++row) {
    for (int column = 0; column < mesh.u_order; ++column) {
      const auto from = static_cast<std::size_t>(row * mesh.u_order + column);
      const auto to = static_cast<std::size_t>(column * mesh.v_order + row);
      swapped.xs[to] = mesh.ys[from];
      swapped.ys[to] = mesh.xs[from];
    }
  }
  return swapped;
}

WarpMeshGrid generate_horizontal_style_mesh(std::string_view style, double value, double width,
                                            double height) {
  const double bend = std::clamp(value, -100.0, 100.0);
  const double theta = std::abs(bend) * 3.14159265358979323846 / 200.0;
  const double displacement = 2.0 * height * bend / 100.0;  // flag/wave/rise scale (E9b: height-based)
  constexpr double kZeroBend = 1e-9;
  WarpMeshGrid mesh;
  mesh.u_order = 4;
  mesh.v_order = style == "warpWave" ? 3 : 2;
  if (style == "warpArc") {
    if (std::abs(bend) < kZeroBend) {
      return identity_warp_mesh(0.0, 0.0, width, height, 4, 2);
    }
    const double sin_theta = std::sin(theta);
    // Top corners swing outward about the bottom corners (radius = height); both
    // edges arc with the same central angle, so the band stays height thick.
    append_arc_row(mesh, -height * sin_theta, width + height * sin_theta,
                   height * (1.0 - std::cos(theta)), theta, true);
    append_arc_row(mesh, 0.0, width, height, theta, true);
    return bend < 0.0 ? mirror_mesh_vertically(mesh, height) : mesh;
  }
  if (style == "warpArch" || style == "warpBulge") {
    if (std::abs(bend) < kZeroBend) {
      return identity_warp_mesh(0.0, 0.0, width, height, 4, 2);
    }
    const bool top_up = bend > 0.0;
    append_arc_row(mesh, 0.0, width, 0.0, theta, top_up);
    // Arch: both edges bow the same way (columns translate rigidly); bulge: the
    // bottom edge bows the opposite way (the band inflates).
    append_arc_row(mesh, 0.0, width, height, theta, style == "warpBulge" ? !top_up : top_up);
    return mesh;
  }
  if (style == "warpFlag") {
    append_identity_row(mesh, width, 0.0);
    append_identity_row(mesh, width, height);
    mesh.ys[1] -= displacement;
    mesh.ys[2] += displacement;
    mesh.ys[5] -= displacement;
    mesh.ys[6] += displacement;
    return mesh;
  }
  if (style == "warpWave") {
    append_identity_row(mesh, width, 0.0);
    append_identity_row(mesh, width, height / 2.0);
    append_identity_row(mesh, width, height);
    // Edges stay pinned; only the middle row waves (and opposite to flag's S).
    mesh.ys[5] += displacement;
    mesh.ys[6] -= displacement;
    return mesh;
  }
  // warpRise: rigid column ramp.
  append_identity_row(mesh, width, 0.0);
  append_identity_row(mesh, width, height);
  mesh.ys[0] += displacement;
  mesh.ys[1] += displacement;
  mesh.ys[4] += displacement;
  mesh.ys[5] += displacement;
  return mesh;
}

}  // namespace

bool can_generate_style_warp_mesh(std::string_view style) {
  return style == "warpArc" || style == "warpArch" || style == "warpBulge" || style == "warpFlag" ||
         style == "warpWave" || style == "warpRise";
}

std::optional<WarpMeshGrid> generate_style_warp_mesh(std::string_view style, double value,
                                                     bool rotate_vertical, double width, double height) {
  if (!can_generate_style_warp_mesh(style) || width <= 0.0 || height <= 0.0) {
    return std::nullopt;
  }
  if (!rotate_vertical) {
    return generate_horizontal_style_mesh(style, value, width, height);
  }
  return swap_mesh_axes(generate_horizontal_style_mesh(style, value, height, width));
}

std::optional<std::array<double, 9>> homography_from_rect_to_quad(double left, double top, double right,
                                                                  double bottom,
                                                                  const std::array<double, 8>& quad) {
  const double width = right - left;
  const double height = bottom - top;
  if (std::abs(width) < 1e-12 || std::abs(height) < 1e-12) {
    return std::nullopt;
  }
  // Unit square -> quad (projective), then compose with rect -> unit square.
  const double x0 = quad[0], y0 = quad[1], x1 = quad[2], y1 = quad[3];
  const double x2 = quad[4], y2 = quad[5], x3 = quad[6], y3 = quad[7];
  const double dx1 = x1 - x2, dx2 = x3 - x2, dx3 = x0 - x1 + x2 - x3;
  const double dy1 = y1 - y2, dy2 = y3 - y2, dy3 = y0 - y1 + y2 - y3;
  double g = 0.0;
  double h = 0.0;
  const double denominator = dx1 * dy2 - dx2 * dy1;
  if (std::abs(dx3) > 1e-12 || std::abs(dy3) > 1e-12) {
    if (std::abs(denominator) < 1e-12) {
      return std::nullopt;
    }
    g = (dx3 * dy2 - dx2 * dy3) / denominator;
    h = (dx1 * dy3 - dx3 * dy1) / denominator;
  }
  const double a = x1 - x0 + g * x1;
  const double b = x3 - x0 + h * x3;
  const double c = x0;
  const double d = y1 - y0 + g * y1;
  const double e = y3 - y0 + h * y3;
  const double f = y0;
  // Compose with (x - left)/width, (y - top)/height.
  const std::array<double, 9> unit{a, b, c, d, e, f, g, h, 1.0};
  std::array<double, 9> composed{};
  const double inv_w = 1.0 / width;
  const double inv_h = 1.0 / height;
  for (int row = 0; row < 3; ++row) {
    const double m0 = unit[static_cast<std::size_t>(row * 3 + 0)];
    const double m1 = unit[static_cast<std::size_t>(row * 3 + 1)];
    const double m2 = unit[static_cast<std::size_t>(row * 3 + 2)];
    composed[static_cast<std::size_t>(row * 3 + 0)] = m0 * inv_w;
    composed[static_cast<std::size_t>(row * 3 + 1)] = m1 * inv_h;
    composed[static_cast<std::size_t>(row * 3 + 2)] = m2 - m0 * inv_w * left - m1 * inv_h * top;
  }
  return composed;
}

std::array<double, 2> apply_homography(const std::array<double, 9>& matrix, double x, double y) {
  const double w = matrix[6] * x + matrix[7] * y + matrix[8];
  const double safe_w = std::abs(w) < 1e-12 ? 1e-12 : w;
  return {(matrix[0] * x + matrix[1] * y + matrix[2]) / safe_w,
          (matrix[3] * x + matrix[4] * y + matrix[5]) / safe_w};
}

std::optional<WarpSurfaceGrid> build_warp_surface_grid(const WarpMeshGrid& mesh,
                                                       const std::array<double, 8>& quad,
                                                       double source_width, double source_height,
                                                       double max_cell_doc_pixels, int max_cells) {
  if (source_width <= 0.0 || source_height <= 0.0 || max_cell_doc_pixels <= 0.0 || mesh.xs.empty() ||
      mesh.ys.empty()) {
    return std::nullopt;
  }
  // Photoshop's placement quad for a warped smart object is the mesh CONTROL HULL's
  // doc-space box (e6: mesh hull [-13.62,53.62]x[-7.49,30] <-> Trnf (66,78,133,115)),
  // so the hull, not the warp bounds rect, is what maps onto the quad.
  const auto [hull_min_x, hull_max_x] = std::minmax_element(mesh.xs.begin(), mesh.xs.end());
  const auto [hull_min_y, hull_max_y] = std::minmax_element(mesh.ys.begin(), mesh.ys.end());
  const auto homography =
      homography_from_rect_to_quad(*hull_min_x, *hull_min_y, *hull_max_x, *hull_max_y, quad);
  if (!homography.has_value()) {
    return std::nullopt;
  }
  const double quad_width = std::max({quad[0], quad[2], quad[4], quad[6]}) -
                            std::min({quad[0], quad[2], quad[4], quad[6]});
  const double quad_height = std::max({quad[1], quad[3], quad[5], quad[7]}) -
                             std::min({quad[1], quad[3], quad[5], quad[7]});
  const int clamp_cells = std::max(8, max_cells);
  const int cells_x =
      std::clamp(static_cast<int>(std::ceil(quad_width / max_cell_doc_pixels)), 8, clamp_cells);
  const int cells_y =
      std::clamp(static_cast<int>(std::ceil(quad_height / max_cell_doc_pixels)), 8, clamp_cells);

  WarpSurfaceGrid grid;
  grid.columns = cells_x + 1;
  grid.rows = cells_y + 1;
  const auto total = static_cast<std::size_t>(grid.columns * grid.rows);
  grid.doc_xs.reserve(total);
  grid.doc_ys.reserve(total);
  grid.source_xs.reserve(total);
  grid.source_ys.reserve(total);
  for (int row = 0; row < grid.rows; ++row) {
    const double v = static_cast<double>(row) / cells_y;
    for (int column = 0; column < grid.columns; ++column) {
      const double u = static_cast<double>(column) / cells_x;
      const auto surface = evaluate_warp_mesh(mesh, u, v);
      const auto doc = apply_homography(*homography, surface[0], surface[1]);
      grid.doc_xs.push_back(doc[0]);
      grid.doc_ys.push_back(doc[1]);
      // u/v are the UNDISTORTED source parameters (the mesh only bends where they
      // land), so source pixels map linearly.
      grid.source_xs.push_back(u * source_width);
      grid.source_ys.push_back(v * source_height);
    }
  }
  return grid;
}

std::optional<std::array<double, 2>> invert_bilinear_cell(double x, double y, double x00, double y00,
                                                          double x10, double y10, double x11, double y11,
                                                          double x01, double y01) {
  // Solve p(s,t) = (1-s)(1-t)p00 + s(1-t)p10 + s t p11 + (1-s) t p01 = (x,y).
  const double ax = x10 - x00, ay = y10 - y00;
  const double bx = x01 - x00, by = y01 - y00;
  const double cx = x00 - x10 - x01 + x11, cy = y00 - y10 - y01 + y11;
  const double dx = x - x00, dy = y - y00;
  // Cross-product elimination of t: s^2 (a x c) + s ((a x b) - (d x c)) - (d x b) = 0.
  const double k2 = ax * cy - ay * cx;
  const double k1 = (ax * by - ay * bx) - (dx * cy - dy * cx);
  const double k0 = -(dx * by - dy * bx);
  double s = -1.0;
  constexpr double kEps = 1e-9;
  if (std::abs(k2) < kEps) {
    if (std::abs(k1) < kEps) {
      return std::nullopt;
    }
    s = -k0 / k1;
  } else {
    const double discriminant = k1 * k1 - 4.0 * k2 * k0;
    if (discriminant < 0.0) {
      return std::nullopt;
    }
    const double root = std::sqrt(discriminant);
    const double s0 = (-k1 + root) / (2.0 * k2);
    const double s1 = (-k1 - root) / (2.0 * k2);
    const bool s0_valid = s0 >= -kEps && s0 <= 1.0 + kEps;
    const bool s1_valid = s1 >= -kEps && s1 <= 1.0 + kEps;
    if (s0_valid && s1_valid) {
      s = std::min(std::max(s0, 0.0), 1.0);
    } else if (s0_valid) {
      s = s0;
    } else if (s1_valid) {
      s = s1;
    } else {
      return std::nullopt;
    }
  }
  const double tx = bx + cx * s;
  const double ty = by + cy * s;
  double t = 0.0;
  if (std::abs(tx) >= std::abs(ty)) {
    if (std::abs(tx) < kEps) {
      return std::nullopt;
    }
    t = (dx - ax * s) / tx;
  } else {
    t = (dy - ay * s) / ty;
  }
  if (s < -kEps || s > 1.0 + kEps || t < -kEps || t > 1.0 + kEps) {
    return std::nullopt;
  }
  return std::array<double, 2>{std::clamp(s, 0.0, 1.0), std::clamp(t, 0.0, 1.0)};
}

}  // namespace patchy
