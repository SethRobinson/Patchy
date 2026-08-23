#include "core/mask_outline.hpp"

#include <algorithm>
#include <array>

namespace patchy {

namespace {

// Wall-follow directions, clockwise in y-down coordinates so that +1 is a
// right turn: East, South, West, North.
constexpr int kEast = 0;
constexpr int kSouth = 1;
constexpr int kWest = 2;
constexpr std::array<int, 4> kDirDx = {1, 0, -1, 0};
constexpr std::array<int, 4> kDirDy = {0, 1, 0, -1};

void canonicalize_loop_start(std::vector<FitPoint>& points) {
  std::size_t best = 0;
  for (std::size_t index = 1; index < points.size(); ++index) {
    const auto& candidate = points[index];
    const auto& current = points[best];
    if (candidate.y < current.y || (candidate.y == current.y && candidate.x < current.x)) {
      best = index;
    }
  }
  std::rotate(points.begin(), points.begin() + static_cast<std::ptrdiff_t>(best), points.end());
}

}  // namespace

std::vector<MaskLoop> trace_mask_outlines(const std::uint8_t* mask, int width, int height,
                                          std::size_t stride) {
  std::vector<MaskLoop> loops;
  if (mask == nullptr || width <= 0 || height <= 0) {
    return loops;
  }
  const auto selected = [mask, stride](int x, int y) noexcept {
    return mask[static_cast<std::size_t>(y + 1) * stride + static_cast<std::size_t>(x + 1)] != 0U;
  };

  // A directed boundary edge keeps selected pixels on its right-hand side:
  // walking East along a top edge, South along a right edge, West along a
  // bottom edge, North along a left edge. (cx, cy) is the corner stood on.
  const auto can_walk = [&selected](int cx, int cy, int direction) noexcept {
    switch (direction) {
      case kEast:
        return selected(cx, cy) && !selected(cx, cy - 1);
      case kSouth:
        return selected(cx - 1, cy) && !selected(cx, cy);
      case kWest:
        return selected(cx - 1, cy - 1) && !selected(cx - 1, cy);
      default:
        return selected(cx, cy - 1) && !selected(cx - 1, cy - 1);
    }
  };

  // Every closed rectilinear loop contains horizontal edges, and a given
  // horizontal lattice edge can carry East or West traffic but never both, so
  // one visited bit per horizontal edge finds each loop exactly once.
  const auto horizontal_edges = static_cast<std::size_t>(width) * (static_cast<std::size_t>(height) + 1);
  std::vector<std::uint64_t> visited((horizontal_edges + 63U) / 64U, 0ULL);
  const auto edge_index = [width](int left_x, int y) {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(left_x);
  };
  const auto edge_visited = [&visited](std::size_t index) {
    return (visited[index >> 6U] & (1ULL << (index & 63U))) != 0ULL;
  };
  const auto mark_edge = [&visited](std::size_t index) { visited[index >> 6U] |= 1ULL << (index & 63U); };

  const auto trace_loop = [&](int start_x, int start_y, int start_direction) {
    std::vector<FitPoint> points;
    int min_x = start_x;
    int min_y = start_y;
    int max_x = start_x;
    int max_y = start_y;
    int cx = start_x;
    int cy = start_y;
    int direction = start_direction;
    for (;;) {
      if (direction == kEast) {
        mark_edge(edge_index(cx, cy));
      } else if (direction == kWest) {
        mark_edge(edge_index(cx - 1, cy));
      }
      cx += kDirDx[static_cast<std::size_t>(direction)];
      cy += kDirDy[static_cast<std::size_t>(direction)];
      // Right turn first, then straight, then left. Trying right first is the
      // saddle rule: where two diagonal pixels meet at a corner the walk hugs
      // the pixel it was already tracing, so the contours touch but never
      // cross or merge.
      int next_direction = direction;
      for (const int candidate : {(direction + 1) & 3, direction, (direction + 3) & 3}) {
        if (can_walk(cx, cy, candidate)) {
          next_direction = candidate;
          break;
        }
      }
      if (next_direction != direction) {
        points.push_back({static_cast<double>(cx), static_cast<double>(cy)});
        min_x = std::min(min_x, cx);
        min_y = std::min(min_y, cy);
        max_x = std::max(max_x, cx);
        max_y = std::max(max_y, cy);
      }
      if (cx == start_x && cy == start_y && next_direction == start_direction) {
        break;
      }
      direction = next_direction;
    }
    canonicalize_loop_start(points);
    MaskLoop loop;
    loop.points = std::move(points);
    loop.bounds = Rect{min_x, min_y, max_x - min_x, max_y - min_y};
    loops.push_back(std::move(loop));
  };

  for (int y = 0; y <= height; ++y) {
    for (int x = 0; x < width; ++x) {
      const bool below = selected(x, y);
      const bool above = selected(x, y - 1);
      if (below == above || edge_visited(edge_index(x, y))) {
        continue;
      }
      if (below) {
        trace_loop(x, y, kEast);  // top edge: outer contour, clockwise
      } else {
        trace_loop(x + 1, y, kWest);  // bottom edge: hole contour, counterclockwise
      }
    }
  }
  return loops;
}

}  // namespace patchy
