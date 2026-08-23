#include "ui/selection_outline.hpp"

#include "core/mask_outline.hpp"

#include <QImage>
#include <QPainter>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace patchy::ui {

namespace {

// Device perimeter below which a closed loop cannot alternate under the
// standard 4-on/4-off dash pattern (it spends whole phases fully covered by
// the white dash); such loops go to the pinpoint path.
constexpr double kMinMarchingPerimeter = 8.0;

// The tracer itself lives in core/mask_outline (shared with image tracing);
// this converts its loops to Qt types without changing a single coordinate.
std::vector<OutlineLoop> trace_mask_outlines(const std::uint8_t* mask, int width, int height,
                                             std::size_t stride) {
  std::vector<OutlineLoop> loops;
  for (auto& traced : patchy::trace_mask_outlines(mask, width, height, stride)) {
    OutlineLoop loop;
    loop.points.reserve(static_cast<qsizetype>(traced.points.size()));
    for (const auto& point : traced.points) {
      loop.points.append(QPointF(point.x, point.y));
    }
    loop.bounds = QRect(traced.bounds.x, traced.bounds.y, traced.bounds.width, traced.bounds.height);
    loops.push_back(std::move(loop));
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
