#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>
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

// True for the preset warp styles Patchy can bake to a mesh exactly like Photoshop
// (E9 captures): warpArc, warpArch, warpBulge, warpFlag, warpWave, warpRise.
[[nodiscard]] bool can_generate_style_warp_mesh(std::string_view style);

// Photoshop's preset-style bake, pinned point-for-point against the E9 COM captures
// (see AGENTS.md for the constructions): `value` is the UI bend percent in
// [-100, 100], `width/height` the content rect the warp bounds describe, and
// `rotate_vertical` mirrors warpRotate == Vrtc (the transposed construction).
// Returns nullopt for styles outside the generatable set or degenerate sizes;
// value 0 yields the identity mesh of the style's natural orders.
[[nodiscard]] std::optional<WarpMeshGrid> generate_style_warp_mesh(std::string_view style, double value,
                                                                   bool rotate_vertical, double width,
                                                                   double height);

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
// [8, max_cells] per axis). The homography maps the mesh CONTROL-POINT HULL onto the
// quad: Photoshop stores a warped placement's Trnf/nonAffineTransform as the warp
// cage's doc-space bounding box, not the pre-warp rect (e6 capture). u/v map source
// pixels linearly via `source_width/height`.
[[nodiscard]] std::optional<WarpSurfaceGrid> build_warp_surface_grid(
    const WarpMeshGrid& mesh, const std::array<double, 8>& quad, double source_width,
    double source_height, double max_cell_doc_pixels, int max_cells);

// Inverse bilinear inside one quad cell: returns (s,t) in [0,1]^2 when `x,y` lies in
// the cell spanned by corners (p00,p10,p11,p01), else nullopt.
[[nodiscard]] std::optional<std::array<double, 2>> invert_bilinear_cell(
    double x, double y, double x00, double y00, double x10, double y10, double x11, double y11,
    double x01, double y01);

}  // namespace patchy
