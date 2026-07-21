#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace patchy {

// Magnetic Lasso: interactive edge-snapped boundary tracing, the "live wire" / "intelligent
// scissors" technique (Mortensen & Barrett, "Intelligent Scissors for Image Composition",
// SIGGRAPH 1995; Barrett & Mortensen, "Interactive live-wire boundary extraction", Medical
// Image Analysis 1997). The technique is public 1995-1997 prior art and the one covering
// patent, US 5,995,115 (Avid), expired 2017-04-04. The live overlay this engine feeds is a
// snapped *path polyline* only; the selection REGION is built once when the user closes the
// path (see docs/legal-constraints.md - never region classify-and-display while input is
// still being received).
struct MagneticLassoParams {
  int width{10};            // edge search diameter around the cursor, document px (1-256)
  int edge_contrast{10};    // percent 1-100: minimum gradient magnitude that counts as an edge
  int node_budget{600000};  // max Dijkstra window nodes before the straight-line fallback
};

struct PointI32 {
  std::int32_t x{0};
  std::int32_t y{0};

  [[nodiscard]] bool operator==(const PointI32& other) const noexcept {
    return x == other.x && y == other.y;
  }
};

// Per-anchor shortest-path field over a bounded window. All math is integer with fixed
// tie-breaks and no RNG, so identical inputs give identical paths on every toolchain (the
// same determinism rule as brush_dynamics). Typical costs: one Dijkstra solve per dropped
// anchor (~65k-node window, single-digit ms); per mouse-move work is snap() plus an O(path)
// parent walk.
class LiveWireEngine {
 public:
  // Non-owning RGBA8888 rows, `stride_bytes` apart; the caller guarantees the buffer outlives
  // the engine's use (CanvasWidget keeps a QImage copy alive for the whole trace).
  void set_image(const std::uint8_t* rgba, std::int32_t width, std::int32_t height,
                 std::ptrdiff_t stride_bytes);
  void set_params(const MagneticLassoParams& params);
  void set_anchor(PointI32 anchor);

  // Strongest qualifying edge pixel within width/2 of `cursor`, ties toward the cursor (then
  // smaller y, then smaller x); the cursor itself when no gradient reaches the edge-contrast
  // threshold inside the disc.
  [[nodiscard]] PointI32 snap(PointI32 cursor) const;

  // 8-connected pixel path from the current anchor to `target`, both inclusive. Lazily
  // (re)builds the Dijkstra field: a square window around the anchor, regrown to cover a
  // target that escapes it. Falls back to a straight Bresenham line when the needed window
  // would exceed node_budget, and also when no point of the extracted path reaches the
  // edge-contrast threshold (featureless ground traces as the literal line, not a tie-broken
  // staircase).
  [[nodiscard]] std::vector<PointI32> path_to(PointI32 target);

 private:
  struct Window {
    std::int32_t x0{0};
    std::int32_t y0{0};
    std::int32_t width{0};
    std::int32_t height{0};

    [[nodiscard]] bool contains(PointI32 p) const noexcept {
      return p.x >= x0 && p.y >= y0 && p.x < x0 + width && p.y < y0 + height;
    }
  };

  [[nodiscard]] PointI32 clamp_to_image(PointI32 p) const noexcept;
  [[nodiscard]] std::uint8_t gradient_g8(std::int32_t x, std::int32_t y) const noexcept;
  [[nodiscard]] std::int32_t luma(std::int32_t x, std::int32_t y) const noexcept;
  [[nodiscard]] bool build_field(Window window);  // false = budget exceeded
  void solve_dijkstra();

  const std::uint8_t* rgba_{nullptr};
  std::int32_t image_width_{0};
  std::int32_t image_height_{0};
  std::ptrdiff_t stride_{0};
  MagneticLassoParams params_{};
  PointI32 anchor_{};
  bool field_valid_{false};
  Window window_{};
  std::vector<std::uint16_t> node_base_;  // per-node destination cost (gradient + zero-crossing)
  std::vector<std::uint8_t> parent_;      // direction index into the anchor, or kParentNone
  std::vector<std::uint64_t> distance_;
};

}  // namespace patchy
