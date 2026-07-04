#include "ui/selection_outline.hpp"

#include <QImage>
#include <QPainter>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace patchy::ui {

namespace {

// Wall-follow directions, clockwise in Qt's y-down coordinates so that +1 is a
// right turn: East, South, West, North.
constexpr int kEast = 0;
constexpr int kSouth = 1;
constexpr int kWest = 2;
constexpr int kNorth = 3;
constexpr std::array<int, 4> kDirDx = {1, 0, -1, 0};
constexpr std::array<int, 4> kDirDy = {0, 1, 0, -1};

// Device perimeter below which a closed loop cannot alternate under the
// standard 4-on/4-off dash pattern (it spends whole phases fully covered by
// the white dash); such loops go to the pinpoint path.
constexpr double kMinMarchingPerimeter = 8.0;

void canonicalize_loop_start(QPolygonF& points) {
  qsizetype best = 0;
  for (qsizetype index = 1; index < points.size(); ++index) {
    const auto& candidate = points[index];
    const auto& current = points[best];
    if (candidate.y() < current.y() || (candidate.y() == current.y() && candidate.x() < current.x())) {
      best = index;
    }
  }
  std::rotate(points.begin(), points.begin() + best, points.end());
}

// Boundary tracing over a byte mask with a one-byte zero border on every side
// (so the edge predicates can sample x/y == -1 or == width/height without
// branching). Emits closed loops in the mask's local pixel coordinates.
std::vector<OutlineLoop> trace_mask_outlines(const std::uint8_t* mask, int width, int height,
                                             std::size_t stride) {
  std::vector<OutlineLoop> loops;
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
    QPolygonF points;
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
        points.append(QPointF(cx, cy));
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
    OutlineLoop loop;
    loop.points = std::move(points);
    loop.bounds = QRect(min_x, min_y, max_x - min_x, max_y - min_y);
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

void translate_loops(std::vector<OutlineLoop>& loops, QPoint offset) {
  for (auto& loop : loops) {
    loop.points.translate(offset);
    loop.bounds.translate(offset);
  }
}

}  // namespace

std::vector<OutlineLoop> trace_selection_outlines(const QRegion& region) {
  if (region.isEmpty()) {
    return {};
  }

  const auto bounds = region.boundingRect();
  if (region.rectCount() == 1) {
    // Select All / plain marquee: emit the four corners directly instead of
    // rasterising a full-canvas mask. Matches the traced output exactly
    // (clockwise, starting at the topmost-leftmost corner).
    OutlineLoop loop;
    loop.points = QPolygonF{QPointF(bounds.left(), bounds.top()),
                            QPointF(bounds.right() + 1, bounds.top()),
                            QPointF(bounds.right() + 1, bounds.bottom() + 1),
                            QPointF(bounds.left(), bounds.bottom() + 1)};
    loop.bounds = bounds;
    return {std::move(loop)};
  }

  const int width = bounds.width();
  const int height = bounds.height();
  const auto stride = static_cast<std::size_t>(width) + 2;
  std::vector<std::uint8_t> mask(stride * (static_cast<std::size_t>(height) + 2), std::uint8_t{0});
  for (const auto& rect : region) {
    const auto local_left = static_cast<std::size_t>(rect.left() - bounds.left());
    const auto local_top = rect.top() - bounds.top();
    for (int row = 0; row < rect.height(); ++row) {
      auto* begin = mask.data() + static_cast<std::size_t>(local_top + row + 1) * stride + local_left + 1;
      std::fill_n(begin, rect.width(), std::uint8_t{1});
    }
  }

  auto loops = trace_mask_outlines(mask.data(), width, height, stride);
  translate_loops(loops, bounds.topLeft());
  return loops;
}

std::vector<OutlineLoop> trace_device_selection_outlines(const QRegion& region, double zoom, QPointF pan,
                                                         const QRectF& device_viewport) {
  if (region.isEmpty() || zoom <= 0.0) {
    return {};
  }

  const auto region_bounds = region.boundingRect();
  const QRectF device_bounds(pan.x() + region_bounds.left() * zoom, pan.y() + region_bounds.top() * zoom,
                             region_bounds.width() * zoom, region_bounds.height() * zoom);
  // Two pixels of padding keep the clip cut (and the boundary the tracer walks
  // along it) off the visible viewport.
  const auto target = device_bounds.intersected(device_viewport.adjusted(-2.0, -2.0, 2.0, 2.0)).toAlignedRect();
  if (target.isEmpty()) {
    return {};
  }

  // Resolve the selection the same way the scaled-down artwork is resolved: an
  // antialiased coverage rasterisation at device resolution, thresholded at
  // 50%. A single winding fill of the whole region avoids seams between the
  // region's abutting rects.
  QImage coverage(target.width(), target.height(), QImage::Format_Grayscale8);
  coverage.fill(0);
  {
    QPainter painter(&coverage);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.translate(-target.left(), -target.top());
    painter.translate(pan);
    painter.scale(zoom, zoom);
    QPainterPath region_path;
    region_path.addRegion(region);
    region_path.setFillRule(Qt::WindingFill);
    painter.fillPath(region_path, Qt::white);
  }

  const int width = target.width();
  const int height = target.height();
  const auto stride = static_cast<std::size_t>(width) + 2;
  std::vector<std::uint8_t> mask(stride * (static_cast<std::size_t>(height) + 2), std::uint8_t{0});
  for (int y = 0; y < height; ++y) {
    const auto* source = coverage.constScanLine(y);
    auto* destination = mask.data() + static_cast<std::size_t>(y + 1) * stride + 1;
    for (int x = 0; x < width; ++x) {
      destination[x] = source[x] >= 128U ? 1U : 0U;
    }
  }

  auto loops = trace_mask_outlines(mask.data(), width, height, stride);
  translate_loops(loops, target.topLeft());
  if (loops.empty()) {
    // Everything visible resolved below 50% coverage (a tiny selection at far
    // zoom-out). Emit at least a 1x1 device rect so the selection stays
    // discoverable; its short perimeter routes it to the pinpoint path.
    const auto visible = device_bounds.intersected(device_viewport);
    if (!visible.isEmpty() || device_viewport.contains(device_bounds.center())) {
      const auto anchor_x = std::floor(device_bounds.left());
      const auto anchor_y = std::floor(device_bounds.top());
      const auto extent_x = std::max(1, static_cast<int>(std::ceil(device_bounds.width())));
      const auto extent_y = std::max(1, static_cast<int>(std::ceil(device_bounds.height())));
      OutlineLoop fallback;
      fallback.points = QPolygonF{QPointF(anchor_x, anchor_y), QPointF(anchor_x + extent_x, anchor_y),
                                  QPointF(anchor_x + extent_x, anchor_y + extent_y),
                                  QPointF(anchor_x, anchor_y + extent_y)};
      fallback.bounds = QRect(static_cast<int>(anchor_x), static_cast<int>(anchor_y), extent_x, extent_y);
      loops.push_back(std::move(fallback));
    }
  }
  return loops;
}

SelectionOutlineScreenPaths build_selection_outline_screen_paths(const std::vector<OutlineLoop>& loops,
                                                                 double zoom, QPointF pan,
                                                                 const QRectF& device_viewport) {
  SelectionOutlineScreenPaths paths;
  if (loops.empty() || zoom <= 0.0) {
    return paths;
  }
  for (const auto& loop : loops) {
    if (loop.points.size() < 3) {
      continue;
    }
    const QRectF device_bounds(pan.x() + loop.bounds.left() * zoom, pan.y() + loop.bounds.top() * zoom,
                               loop.bounds.width() * zoom, loop.bounds.height() * zoom);
    if (!device_viewport.intersects(device_bounds)) {
      continue;
    }
    // A closed axis-aligned loop's length is twice its bounding extent only
    // for rectangles; sum the real segment lengths so concave short loops are
    // classified correctly too.
    double perimeter = 0.0;
    for (qsizetype index = 0; index < loop.points.size(); ++index) {
      const auto& from = loop.points[index];
      const auto& to = loop.points[(index + 1) % loop.points.size()];
      perimeter += (std::abs(to.x() - from.x()) + std::abs(to.y() - from.y())) * zoom;
    }
    auto& path = perimeter < kMinMarchingPerimeter ? paths.pinpoint : paths.marching;
    path.moveTo(pan.x() + loop.points.first().x() * zoom, pan.y() + loop.points.first().y() * zoom);
    for (qsizetype index = 1; index < loop.points.size(); ++index) {
      path.lineTo(pan.x() + loop.points[index].x() * zoom, pan.y() + loop.points[index].y() * zoom);
    }
    path.closeSubpath();
  }
  return paths;
}

}  // namespace patchy::ui
