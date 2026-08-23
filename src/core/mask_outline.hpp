#pragma once

#include "core/layer.hpp"
#include "core/path_fit.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

// Boundary tracing of a byte mask into closed loops (the selection-outline
// walker, promoted here so Make Work Path, the marching ants, and image
// tracing share one deterministic contour source).
namespace patchy {

// One closed boundary loop in pixel-corner coordinates of the mask (pixel
// (x, y) spans corners (x, y)..(x+1, y+1), so a loop around the single pixel
// (3, 4) is {(3,4), (4,4), (4,5), (3,5)}). The polygon is implicitly closed:
// the first point is not repeated at the end. Outer boundaries wind clockwise
// in y-down coordinates, holes counterclockwise. `bounds` is the loop's pixel
// bounding box (corner extent [x, x+width] x [y, y+height]).
struct MaskLoop {
  std::vector<FitPoint> points;
  Rect bounds{};
};

// Traces `mask` (non-zero = inside), which must carry a one-byte zero border
// on every side: `mask` points at the border's top-left corner, `stride` is
// width + 2 bytes, and the buffer holds (height + 2) rows. Diagonally-touching
// pixels stay 4-connected: they produce separate loops that share a corner
// without crossing. Output is deterministic: loops appear in row-major order
// of their topmost boundary edge, each rotated to start at its topmost-then-
// leftmost vertex, and collinear runs are collapsed.
[[nodiscard]] std::vector<MaskLoop> trace_mask_outlines(const std::uint8_t* mask, int width, int height,
                                                        std::size_t stride);

}  // namespace patchy
