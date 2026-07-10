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

// True for the preset warp styles Patchy can bake to a mesh exactly like Photoshop:
// all fifteen Warp Text styles (E9 + e9c/e9d COM captures).
[[nodiscard]] bool can_generate_style_warp_mesh(std::string_view style);

// Photoshop's preset-style bake, pinned point-for-point against the E9/e9c/e9d COM
// captures (see docs/warp.md for the constructions): `value` is the UI bend percent in
// [-100, 100], `width/height` the content rect the warp bounds describe, and
// `rotate_vertical` mirrors warpRotate == Vrtc (the transposed construction; twist
// is orientation-invariant and ignores it, matching Photoshop's bakes).
// Returns nullopt for styles outside the generatable set or degenerate sizes;
// value 0 yields the identity mesh of the style's natural orders.
[[nodiscard]] std::optional<WarpMeshGrid> generate_style_warp_mesh(std::string_view style, double value,
                                                                   bool rotate_vertical, double width,
                                                                   double height);

// Photoshop's warp Horizontal/Vertical Distortion (warpPerspective /
// warpPerspectiveOther, the Warp Text dialog sliders): scales every mesh ROW about
// the midpoint of its edge points by 1 + (2v-1)*vertical%/100 FIRST, then every
// COLUMN by 1 + (2u-1)*horizontal%/100 (order and midpoint rule pinned by the
// so_persp_* captures at ~1e-7 px).
void apply_warp_distortion(WarpMeshGrid& mesh, double horizontal_percent, double vertical_percent);

// Row-major 3x3 homography mapping the axis-aligned content rect onto the placement
// quad corners (top-left, top-right, bottom-right, bottom-left in doc coords).
// Returns nullopt for degenerate quads.
[[nodiscard]] std::optional<std::array<double, 9>> homography_from_rect_to_quad(
    double left, double top, double right, double bottom, const std::array<double, 8>& quad);

[[nodiscard]] std::array<double, 2> apply_homography(const std::array<double, 9>& matrix, double x,
                                                     double y);

// 3x3 inverse via the adjugate; nullopt when singular. Used by the warp tool to map
// dragged document points back into mesh/content space.
[[nodiscard]] std::optional<std::array<double, 9>> invert_homography(const std::array<double, 9>& matrix);

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

// Warped-text lattice: the warp box (the TySh text 'bounds' rect) fixes the mesh's
// [0,1]^2 parameterization, but the rendered raster covers `window` (the text ink
// rect, which may poke past the box - Bernstein extrapolation handles it). The
// surface is evaluated over the window's parameter range and carried into document
// space by `affine` (row-major 2x3: x' = a[0]x + a[1]y + a[2], y' = a[3]x + a[4]y +
// a[5], the text layer transform). source_xs/ys map the window linearly onto a
// source raster of `source_width/height` pixels.
[[nodiscard]] std::optional<WarpSurfaceGrid> build_warp_surface_grid_over_window(
    const WarpMeshGrid& mesh, double box_left, double box_top, double box_right, double box_bottom,
    double window_left, double window_top, double window_right, double window_bottom,
    double source_width, double source_height, const std::array<double, 6>& affine,
    double max_cell_doc_pixels, int max_cells);

}  // namespace patchy
