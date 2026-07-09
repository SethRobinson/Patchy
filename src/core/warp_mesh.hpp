#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <vector>

// Photoshop warp-mesh math (Qt-free, deterministic doubles only): Bezier patch
// evaluation over the SoLd warp mesh, the homography that maps the content rect onto
// the placement quad, and the forward-evaluated surface grid the resampler inverts.
// Mesh points live in CONTENT/bounds space (E6 captures: moves never touch them);
// orders 2..4 occur in real files (style bakes write 4x2, manual warps 4x4).
namespace patchy {

struct WarpMeshGrid {
  int u_order{4};  // control points per row (2..4)
  int v_order{4};  // rows (2..4)
  std::vector<double> xs;  // row-major, v_order rows of u_order columns
  std::vector<double> ys;
};

// A uniform (identity) mesh spanning the given content bounds.
[[nodiscard]] WarpMeshGrid identity_warp_mesh(double left, double top, double right, double bottom,
                                              int u_order, int v_order);

// Evaluates the Bezier patch at (u,v) in [0,1]^2.
[[nodiscard]] std::array<double, 2> evaluate_warp_mesh(const WarpMeshGrid& mesh, double u, double v);

// Exact Bezier degree elevation to 4x4 (the editor's working mesh).
[[nodiscard]] WarpMeshGrid elevate_warp_mesh_to_cubic(const WarpMeshGrid& mesh);

// Row-major 3x3 homography mapping the axis-aligned content rect onto the placement
// quad corners (top-left, top-right, bottom-right, bottom-left in doc coords).
// Returns nullopt for degenerate quads.
[[nodiscard]] std::optional<std::array<double, 9>> homography_from_rect_to_quad(
    double left, double top, double right, double bottom, const std::array<double, 8>& quad);

[[nodiscard]] std::array<double, 2> apply_homography(const std::array<double, 9>& matrix, double x,
                                                     double y);

// A dense forward-evaluated lattice of the warp surface in DOCUMENT space, plus the
// matching source-pixel coordinates. Cells are inverted per output pixel with the
// closed-form bilinear inverse.
struct WarpSurfaceGrid {
  int columns{0};  // lattice points per row (cells + 1)
  int rows{0};
  std::vector<double> doc_xs;  // row-major lattice, rows*columns
  std::vector<double> doc_ys;
  std::vector<double> source_xs;  // matching source-image pixel coordinates
  std::vector<double> source_ys;
};

// Builds the lattice: mesh (content space) -> homography (doc space), sampled so no
// cell spans more than `max_cell_doc_pixels` on either axis (lattice clamped to
// [8, max_cells] per axis). `source_width/height` map the mesh bounds onto source
// pixels (bounds and pixel size can differ).
[[nodiscard]] std::optional<WarpSurfaceGrid> build_warp_surface_grid(
    const WarpMeshGrid& mesh, double bounds_left, double bounds_top, double bounds_right,
    double bounds_bottom, const std::array<double, 8>& quad, double source_width, double source_height,
    double max_cell_doc_pixels, int max_cells);

// Inverse bilinear inside one quad cell: returns (s,t) in [0,1]^2 when `x,y` lies in
// the cell spanned by corners (p00,p10,p11,p01), else nullopt.
[[nodiscard]] std::optional<std::array<double, 2>> invert_bilinear_cell(
    double x, double y, double x00, double y00, double x10, double y10, double x11, double y11,
    double x01, double y01);

}  // namespace patchy
