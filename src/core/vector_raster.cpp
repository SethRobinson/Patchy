#include "core/vector_raster.hpp"
#include "core/vector_compound.hpp"

#include "core/blend_math.hpp"
#include "core/layer_render_utils.hpp"
#include "core/pattern_sampler.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

namespace patchy {

namespace {

// 24.8 fixed point; one pixel = 256 subpixels.
constexpr std::int32_t kSub = 256;

struct FixedPoint {
  std::int32_t x{0};
  std::int32_t y{0};
};

struct Edge {
  FixedPoint from{};
  FixedPoint to{};
};

std::int32_t to_fixed(double v) noexcept {
  return static_cast<std::int32_t>(std::llround(v * 256.0));
}

// Rounded division, symmetric about zero (deterministic tie-break away from
// zero, matching llround).
std::int64_t divide_rounded(std::int64_t numerator, std::int64_t denominator) noexcept {
  if (denominator == 0) {
    return 0;
  }
  if ((numerator >= 0) == (denominator >= 0)) {
    const auto n = numerator >= 0 ? numerator : -numerator;
    const auto d = denominator >= 0 ? denominator : -denominator;
    return (n + d / 2) / d;
  }
  const auto n = numerator >= 0 ? numerator : -numerator;
  const auto d = denominator >= 0 ? denominator : -denominator;
  return -((n + d / 2) / d);
}

// Appends the flattened segments of one cubic (from -> to with control points
// c1/c2, all fixed). Subdivision is integer-only de Casteljau halving (no
// floating point anywhere), so output is bit-identical across toolchains
// regardless of FMA contraction differences.
void flatten_cubic_recursive(FixedPoint p0, FixedPoint p1, FixedPoint p2, FixedPoint p3,
                             std::int32_t depth, std::vector<Edge>& edges) {
  if (depth <= 0) {
    if (p0.y != p3.y || p0.x != p3.x) {
      edges.push_back(Edge{p0, p3});
    }
    return;
  }
  const auto mid = [](std::int32_t a, std::int32_t b) {
    // Floor average: exact when the sum is even; the half-subpixel bias is
    // far below the coverage quantum and fully deterministic.
    return static_cast<std::int32_t>((static_cast<std::int64_t>(a) + b) >> 1);
  };
  const FixedPoint m01{mid(p0.x, p1.x), mid(p0.y, p1.y)};
  const FixedPoint m12{mid(p1.x, p2.x), mid(p1.y, p2.y)};
  const FixedPoint m23{mid(p2.x, p3.x), mid(p2.y, p3.y)};
  const FixedPoint m012{mid(m01.x, m12.x), mid(m01.y, m12.y)};
  const FixedPoint m123{mid(m12.x, m23.x), mid(m12.y, m23.y)};
  const FixedPoint m{mid(m012.x, m123.x), mid(m012.y, m123.y)};
  flatten_cubic_recursive(p0, m01, m012, m, depth - 1, edges);
  flatten_cubic_recursive(m, m123, m23, p3, depth - 1, edges);
}

void flatten_cubic(FixedPoint from, FixedPoint c1, FixedPoint c2, FixedPoint to,
                   std::vector<Edge>& edges) {
  const auto deviation = std::max(
      std::max(std::abs(static_cast<std::int64_t>(from.x) - 2 * c1.x + c2.x),
               std::abs(static_cast<std::int64_t>(from.y) - 2 * c1.y + c2.y)),
      std::max(std::abs(static_cast<std::int64_t>(c1.x) - 2 * c2.x + to.x),
               std::abs(static_cast<std::int64_t>(c1.y) - 2 * c2.y + to.y)));
  if (deviation <= 8) {  // within 1/32 px of straight
    if (from.y != to.y || from.x != to.x) {
      edges.push_back(Edge{from, to});
    }
    return;
  }
  // Chord error after n segments <= 3 * deviation / (4 * n^2); each halving
  // quarters the error, so depth = ceil(log4(deviation * 3 / 32)) reaches the
  // 8-subpixel (1/32 px) target. Integer search keeps it float-free.
  std::int32_t depth = 1;
  std::int64_t error = deviation * 3 / 4;
  while (error > 8 && depth < 8) {
    error /= 4;
    ++depth;
  }
  flatten_cubic_recursive(from, c1, c2, to, depth, edges);
}

// Flattens every subpath of one shape group into edges relative to
// `origin` (document pixels), closing open subpaths with their chord.
void flatten_group(const VectorPath& path, std::size_t first_subpath, std::size_t subpath_end,
                   std::int32_t origin_x, std::int32_t origin_y, std::vector<Edge>& edges) {
  const std::int32_t shift_x = origin_x * kSub;
  const std::int32_t shift_y = origin_y * kSub;
  for (std::size_t s = first_subpath; s < subpath_end; ++s) {
    const auto& subpath = path.subpaths[s];
    const auto count = subpath.anchors.size();
    if (count < 2) {
      continue;
    }
    for (std::size_t i = 0; i < count; ++i) {
      const auto& a = subpath.anchors[i];
      const auto& b = subpath.anchors[(i + 1) % count];
      if (i + 1 == count && !subpath.closed) {
        // Open subpath: the implied closing chord (straight), matching the
        // Photoshop render of open shape subpaths.
        FixedPoint from{to_fixed(a.anchor_x) - shift_x, to_fixed(a.anchor_y) - shift_y};
        FixedPoint to{to_fixed(b.anchor_x) - shift_x, to_fixed(b.anchor_y) - shift_y};
        if (from.y != to.y) {
          edges.push_back(Edge{from, to});
        }
        continue;
      }
      FixedPoint from{to_fixed(a.anchor_x) - shift_x, to_fixed(a.anchor_y) - shift_y};
      FixedPoint c1{to_fixed(a.out_x) - shift_x, to_fixed(a.out_y) - shift_y};
      FixedPoint c2{to_fixed(b.in_x) - shift_x, to_fixed(b.in_y) - shift_y};
      FixedPoint to{to_fixed(b.anchor_x) - shift_x, to_fixed(b.anchor_y) - shift_y};
      if (c1.x == from.x && c1.y == from.y && c2.x == to.x && c2.y == to.y) {
        if (from.y != to.y) {
          edges.push_back(Edge{from, to});
        }
      } else {
        flatten_cubic(from, c1, c2, to, edges);
      }
    }
  }
}

struct Cell {
  std::int32_t cover{0};
  std::int64_t area{0};
};

enum class WindingRule { EvenOdd, NonZero };

// Exact-area cell rasterizer over one buffer-relative edge list. Emits gray8
// coverage into `out` (width x height, buffer-relative).
void rasterize_edges(const std::vector<Edge>& edges, std::int32_t width, std::int32_t height,
                     WindingRule rule, PixelBuffer& out) {
  // Bucket edges by their first touched row.
  std::vector<std::int32_t> bucket_heads(static_cast<std::size_t>(height) + 1, -1);
  std::vector<std::int32_t> bucket_next(edges.size(), -1);
  std::vector<std::int32_t> edge_min_row(edges.size(), 0);
  std::vector<std::int32_t> edge_max_row(edges.size(), 0);
  for (std::size_t i = 0; i < edges.size(); ++i) {
    const auto& edge = edges[i];
    const auto y_min = std::min(edge.from.y, edge.to.y);
    const auto y_max = std::max(edge.from.y, edge.to.y);
    if (y_min == y_max) {
      edge_min_row[i] = 1;
      edge_max_row[i] = 0;  // never active
      continue;
    }
    auto first_row = static_cast<std::int32_t>(std::floor(static_cast<double>(y_min) / kSub));
    auto last_row = static_cast<std::int32_t>(std::floor(static_cast<double>(y_max - 1) / kSub));
    first_row = std::clamp(first_row, 0, height - 1);
    last_row = std::clamp(last_row, 0, height - 1);
    edge_min_row[i] = first_row;
    edge_max_row[i] = last_row;
    bucket_next[i] = bucket_heads[static_cast<std::size_t>(first_row)];
    bucket_heads[static_cast<std::size_t>(first_row)] = static_cast<std::int32_t>(i);
  }

  std::vector<Cell> cells(static_cast<std::size_t>(width) + 1);
  std::vector<std::int32_t> active;
  auto* bytes = out.data().data();
  const auto stride = out.stride_bytes();

  // Emits one within-row, within-cell piece.
  const auto emit_piece = [&cells, width](std::int32_t cell_x, std::int64_t fx0, std::int64_t fx1,
                                          std::int64_t dy) {
    if (dy == 0) {
      return;
    }
    if (cell_x < 0) {
      cell_x = 0;
      fx0 = 0;
      fx1 = 0;
    } else if (cell_x >= width) {
      // Right of the buffer: contributes full-width cover to the row's tail,
      // which is clipped away; drop it.
      return;
    }
    auto& cell = cells[static_cast<std::size_t>(cell_x)];
    cell.cover += static_cast<std::int32_t>(dy);
    cell.area += dy * (2 * kSub - fx0 - fx1);
  };

  // Walks the sub-segment (sx, sy) -> (ex, ey) (fixed, already clipped to one
  // row) across cell boundaries.
  const auto emit_span = [&emit_piece](std::int64_t sx, std::int64_t sy, std::int64_t ex,
                                       std::int64_t ey) {
    if (sy == ey) {
      return;
    }
    auto cell_of = [](std::int64_t x) {
      // floor division for negatives
      return static_cast<std::int32_t>(x >= 0 ? x / kSub : -((-x + kSub - 1) / kSub));
    };
    std::int32_t cx = cell_of(sx);
    const std::int32_t cx_end = cell_of(ex);
    if (cx == cx_end) {
      emit_piece(cx, sx - static_cast<std::int64_t>(cx) * kSub, ex - static_cast<std::int64_t>(cx) * kSub,
                 ey - sy);
      return;
    }
    const bool rightward = ex > sx;
    std::int64_t previous_x = sx;
    std::int64_t previous_y = sy;
    while (cx != cx_end) {
      const std::int64_t boundary =
          rightward ? (static_cast<std::int64_t>(cx) + 1) * kSub : static_cast<std::int64_t>(cx) * kSub;
      const std::int64_t boundary_y =
          sy + divide_rounded((ey - sy) * (boundary - sx), ex - sx);
      emit_piece(cx, previous_x - static_cast<std::int64_t>(cx) * kSub,
                 boundary - static_cast<std::int64_t>(cx) * kSub, boundary_y - previous_y);
      previous_x = boundary;
      previous_y = boundary_y;
      cx += rightward ? 1 : -1;
    }
    emit_piece(cx, previous_x - static_cast<std::int64_t>(cx) * kSub,
               ex - static_cast<std::int64_t>(cx) * kSub, ey - previous_y);
  };

  for (std::int32_t row = 0; row < height; ++row) {
    // Admit edges starting on this row; retire finished ones.
    for (auto i = bucket_heads[static_cast<std::size_t>(row)]; i != -1;
         i = bucket_next[static_cast<std::size_t>(i)]) {
      active.push_back(i);
    }
    std::fill(cells.begin(), cells.end(), Cell{});
    const std::int64_t row_top = static_cast<std::int64_t>(row) * kSub;
    const std::int64_t row_bottom = row_top + kSub;
    std::size_t keep = 0;
    for (std::size_t a = 0; a < active.size(); ++a) {
      const auto index = active[a];
      const auto& edge = edges[static_cast<std::size_t>(index)];
      // Order endpoints by y; remember the true direction sign.
      const bool downward = edge.to.y > edge.from.y;
      const FixedPoint& top = downward ? edge.from : edge.to;
      const FixedPoint& bottom = downward ? edge.to : edge.from;
      const std::int64_t ys = std::max<std::int64_t>(top.y, row_top);
      const std::int64_t ye = std::min<std::int64_t>(bottom.y, row_bottom);
      if (ys < ye) {
        const std::int64_t dy_total = static_cast<std::int64_t>(bottom.y) - top.y;
        const std::int64_t xs =
            top.x + divide_rounded((static_cast<std::int64_t>(bottom.x) - top.x) * (ys - top.y), dy_total);
        const std::int64_t xe =
            top.x + divide_rounded((static_cast<std::int64_t>(bottom.x) - top.x) * (ye - top.y), dy_total);
        if (downward) {
          emit_span(xs, ys, xe, ye);
        } else {
          emit_span(xe, ye, xs, ys);
        }
      }
      if (edge_max_row[static_cast<std::size_t>(index)] > row) {
        active[keep++] = index;
      }
    }
    active.resize(keep);

    // Sweep the row: signed winding coverage folded by the winding rule.
    auto* row_bytes = bytes + static_cast<std::size_t>(row) * stride;
    std::int64_t running_cover = 0;
    for (std::int32_t x = 0; x < width; ++x) {
      const auto& cell = cells[static_cast<std::size_t>(x)];
      const std::int64_t twice_area = running_cover * (2 * kSub) + cell.area;
      running_cover += cell.cover;
      std::int64_t c = divide_rounded(twice_area, 2 * kSub);
      if (rule == WindingRule::EvenOdd) {
        c = ((c % (2 * kSub)) + 2 * kSub) % (2 * kSub);
        if (c > kSub) {
          c = 2 * kSub - c;
        }
      } else {
        c = std::min<std::int64_t>(c >= 0 ? c : -c, kSub);
      }
      row_bytes[x] = static_cast<std::uint8_t>((c * 255 + kSub / 2) / kSub);
    }
  }
}

Rect intersect_rects(Rect a, Rect b) noexcept {
  const auto x0 = std::max(a.x, b.x);
  const auto y0 = std::max(a.y, b.y);
  const auto x1 = std::min(a.x + a.width, b.x + b.width);
  const auto y1 = std::min(a.y + a.height, b.y + b.height);
  if (x1 <= x0 || y1 <= y0) {
    return Rect{};
  }
  return Rect{x0, y0, x1 - x0, y1 - y0};
}

Rect group_pixel_bounds(const VectorPath& path, std::size_t first_subpath, std::size_t subpath_end) {
  bool any = false;
  double min_x = 0.0;
  double min_y = 0.0;
  double max_x = 0.0;
  double max_y = 0.0;
  const auto extend = [&](double x, double y) {
    if (!any) {
      min_x = max_x = x;
      min_y = max_y = y;
      any = true;
      return;
    }
    min_x = std::min(min_x, x);
    max_x = std::max(max_x, x);
    min_y = std::min(min_y, y);
    max_y = std::max(max_y, y);
  };
  for (std::size_t s = first_subpath; s < subpath_end; ++s) {
    for (const auto& anchor : path.subpaths[s].anchors) {
      extend(anchor.anchor_x, anchor.anchor_y);
      extend(anchor.in_x, anchor.in_y);
      extend(anchor.out_x, anchor.out_y);
    }
  }
  if (!any) {
    return Rect{};
  }
  const auto x0 = static_cast<std::int32_t>(std::floor(min_x));
  const auto y0 = static_cast<std::int32_t>(std::floor(min_y));
  const auto x1 = static_cast<std::int32_t>(std::ceil(max_x)) + 1;
  const auto y1 = static_cast<std::int32_t>(std::ceil(max_y)) + 1;
  return Rect{x0, y0, x1 - x0, y1 - y0};
}

std::uint8_t coverage_at(const CoverageBuffer& buffer, std::int32_t x, std::int32_t y) noexcept {
  if (buffer.bounds.empty() || !buffer.bounds.contains(x, y)) {
    return 0;
  }
  return buffer.pixels
      .data()[static_cast<std::size_t>(y - buffer.bounds.y) * buffer.pixels.stride_bytes() +
              static_cast<std::size_t>(x - buffer.bounds.x)];
}

std::uint8_t combine_coverage(PathCombineOp op, std::uint8_t accumulated, std::uint8_t group) noexcept {
  const auto a = static_cast<std::int32_t>(accumulated);
  const auto b = static_cast<std::int32_t>(group);
  const auto mul = [](std::int32_t p, std::int32_t q) { return (p * q + 127) / 255; };
  switch (op) {
    case PathCombineOp::Add:
      return static_cast<std::uint8_t>(a + b - mul(a, b));
    case PathCombineOp::Subtract:
      return static_cast<std::uint8_t>(a - mul(a, b));
    case PathCombineOp::Intersect:
      return static_cast<std::uint8_t>(mul(a, b));
    case PathCombineOp::Xor:
      return static_cast<std::uint8_t>(a + b - 2 * mul(a, b));
  }
  return accumulated;
}

CoverageBuffer full_coverage(Rect bounds) {
  CoverageBuffer buffer;
  if (bounds.empty()) {
    return buffer;
  }
  buffer.bounds = bounds;
  buffer.pixels = PixelBuffer(bounds.width, bounds.height, PixelFormat::gray8());
  buffer.pixels.clear(255);
  return buffer;
}

// Trims a coverage buffer to its non-zero extent (empty result when blank).
void trim_coverage(CoverageBuffer& buffer) {
  if (buffer.bounds.empty()) {
    return;
  }
  const auto width = buffer.bounds.width;
  const auto height = buffer.bounds.height;
  const auto* bytes = buffer.pixels.data().data();
  const auto stride = buffer.pixels.stride_bytes();
  std::int32_t min_x = width;
  std::int32_t min_y = height;
  std::int32_t max_x = -1;
  std::int32_t max_y = -1;
  for (std::int32_t y = 0; y < height; ++y) {
    const auto* row = bytes + static_cast<std::size_t>(y) * stride;
    for (std::int32_t x = 0; x < width; ++x) {
      if (row[x] != 0) {
        min_x = std::min(min_x, x);
        max_x = std::max(max_x, x);
        min_y = std::min(min_y, y);
        max_y = std::max(max_y, y);
      }
    }
  }
  if (max_x < 0) {
    buffer = CoverageBuffer{};
    return;
  }
  if (min_x == 0 && min_y == 0 && max_x == width - 1 && max_y == height - 1) {
    return;
  }
  const Rect trimmed{buffer.bounds.x + min_x, buffer.bounds.y + min_y, max_x - min_x + 1,
                     max_y - min_y + 1};
  PixelBuffer pixels(trimmed.width, trimmed.height, PixelFormat::gray8());
  auto* out = pixels.data().data();
  for (std::int32_t y = 0; y < trimmed.height; ++y) {
    std::memcpy(out + static_cast<std::size_t>(y) * pixels.stride_bytes(),
                bytes + static_cast<std::size_t>(y + min_y) * stride + min_x,
                static_cast<std::size_t>(trimmed.width));
  }
  buffer.bounds = trimmed;
  buffer.pixels = std::move(pixels);
}

// ---------------------------------------------------------------------------
// Stroker: flattens subpaths to polylines, applies dashes, and emits closed
// outline loops (segment quads + join/cap fans) rasterized with NONZERO
// winding so overlapping pieces union. Outline math uses doubles (normals
// need sqrt); every emitted point is quantized to 24.8 fixed before
// rasterization, and expressions stay simple sums/products, keeping output
// deterministic in practice (verified by the pinned goldens across
// toolchains).
// ---------------------------------------------------------------------------

struct DPoint {
  double x{0.0};
  double y{0.0};
};

// Flattens one subpath into a document-space polyline. Curves go through the
// same integer flattener as fills (converted back to doubles exactly), so
// stroke and fill geometry always agree.
std::vector<DPoint> subpath_polyline(const PathSubpath& subpath) {
  std::vector<DPoint> points;
  const auto count = subpath.anchors.size();
  if (count == 0) {
    return points;
  }
  // Every vertex snaps to the 1/256 flattener lattice. Curve interiors arrive
  // quantized from the integer flattener; anchor endpoints must land on the
  // same lattice or the sub-quantum residual survives as a micro-segment whose
  // direction is rounding noise - the miter join then amplifies it into a
  // spike up to miter_limit x half-width long (the translation-dependent
  // spikes/bars on imported stroked curves, July 2026). On-lattice values
  // round-trip exactly, so the exact-equality dedupe needs no epsilon.
  const auto push = [&points](double x, double y) {
    x = static_cast<double>(to_fixed(x)) / kSub;
    y = static_cast<double>(to_fixed(y)) / kSub;
    if (!points.empty() && points.back().x == x && points.back().y == y) {
      return;
    }
    points.push_back(DPoint{x, y});
  };
  push(subpath.anchors[0].anchor_x, subpath.anchors[0].anchor_y);
  const auto segment_count = subpath.closed ? count : count - 1;
  for (std::size_t i = 0; i < segment_count; ++i) {
    const auto& a = subpath.anchors[i];
    const auto& b = subpath.anchors[(i + 1) % count];
    const FixedPoint from{to_fixed(a.anchor_x), to_fixed(a.anchor_y)};
    const FixedPoint c1{to_fixed(a.out_x), to_fixed(a.out_y)};
    const FixedPoint c2{to_fixed(b.in_x), to_fixed(b.in_y)};
    const FixedPoint to{to_fixed(b.anchor_x), to_fixed(b.anchor_y)};
    if ((c1.x == from.x && c1.y == from.y && c2.x == to.x && c2.y == to.y)) {
      push(b.anchor_x, b.anchor_y);
      continue;
    }
    std::vector<Edge> segment_edges;
    flatten_cubic(from, c1, c2, to, segment_edges);
    for (const auto& edge : segment_edges) {
      push(static_cast<double>(edge.to.x) / kSub, static_cast<double>(edge.to.y) / kSub);
    }
    // Ensure the exact endpoint lands (flatten skips zero-length tails).
    push(b.anchor_x, b.anchor_y);
  }
  // Closed subpaths wrap implicitly: drop the duplicated closing point so the
  // wrap-around segment/join indexing sees clean vertices.
  if (subpath.closed && points.size() > 1 && points.front().x == points.back().x &&
      points.front().y == points.back().y) {
    points.pop_back();
  }
  return points;
}

struct StrokeRun {
  std::vector<DPoint> points;
  bool closed{false};
};

double distance(const DPoint& a, const DPoint& b) noexcept {
  const double dx = b.x - a.x;
  const double dy = b.y - a.y;
  return std::sqrt(dx * dx + dy * dy);
}

// Splits a polyline into dash runs. Dash entries are stroke-width multiples
// (the vstk descriptor's unitless values); offset likewise.
std::vector<StrokeRun> apply_dashes(const std::vector<DPoint>& points, bool closed,
                                    const std::vector<double>& input_dashes, double offset_px) {
  std::vector<StrokeRun> runs;
  if (points.size() < 2) {
    return runs;
  }
  auto dashes_px = input_dashes;
  for (auto& dash : dashes_px) {
    if (!std::isfinite(dash)) {
      return {StrokeRun{points, closed}};
    }
    if (dash > 0.0) {
      dash = std::max(dash, 1.0 / kSub);
    }
  }
  if (!std::isfinite(offset_px)) {
    offset_px = 0.0;
  }
  std::size_t boundaries = 0;
  constexpr std::size_t kMaxDashBoundaries = 262144;
  double pattern_total = 0.0;
  for (const auto dash : dashes_px) {
    pattern_total += std::max(dash, 0.0);
  }
  if (dashes_px.empty() || !std::isfinite(pattern_total) || pattern_total <= 0.0) {
    runs.push_back(StrokeRun{points, closed});
    return runs;
  }

  // Walk the (possibly closed) polyline, toggling on/off at dash boundaries.
  std::vector<DPoint> walk = points;
  if (closed) {
    walk.push_back(points.front());
  }
  double phase = std::fmod(offset_px, pattern_total);
  if (phase < 0.0) {
    phase += pattern_total;
  }
  std::size_t dash_index = 0;
  while (phase >= std::max(dashes_px[dash_index], 0.0)) {
    phase -= std::max(dashes_px[dash_index], 0.0);
    dash_index = (dash_index + 1) % dashes_px.size();
    if (phase <= 0.0) {
      break;
    }
  }
  bool on = dash_index % 2 == 0;
  double remaining = std::max(dashes_px[dash_index], 0.0) - phase;

  StrokeRun current;
  const auto begin_run = [&current](const DPoint& at) {
    current.points.clear();
    current.points.push_back(at);
  };
  const auto finish_run = [&runs, &current]() {
    if (current.points.size() >= 2) {
      runs.push_back(StrokeRun{current.points, false});
    }
    current.points.clear();
  };
  if (on) {
    begin_run(walk.front());
  }
  for (std::size_t i = 0; i + 1 < walk.size(); ++i) {
    DPoint a = walk[i];
    const DPoint b = walk[i + 1];
    double segment_left = distance(a, b);
    while (segment_left > remaining && remaining >= 0.0) {
      if (++boundaries > kMaxDashBoundaries) {
        return {StrokeRun{points, closed}};
      }
      const double t = remaining / segment_left;
      const DPoint cut{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
      if (on) {
        current.points.push_back(cut);
        finish_run();
      } else {
        begin_run(cut);
      }
      on = !on;
      segment_left -= remaining;
      a = cut;
      dash_index = (dash_index + 1) % dashes_px.size();
      remaining = std::max(dashes_px[dash_index], 0.0);
      if (remaining <= 0.0) {
        remaining = 1e-9;  // zero-length entries advance without emitting
      }
    }
    remaining -= segment_left;
    if (on) {
      current.points.push_back(b);
    }
  }
  finish_run();
  return runs;
}

// Emits one closed outline loop into the edge list (buffer-relative fixed).
// The stroke band unions its loops under nonzero winding, so every loop must
// carry the SAME orientation: a join wedge emitted with the opposite winding
// cancels the segment quads it overlaps instead of accumulating - on a
// densely flattened arc the long wedges sweep across many short quads and
// bite a hatched notch out of the band (the vectors_overlay_stroke top-right
// corner glitch, July 2026). Normalize by signed area before emitting.
void append_outline_loop(const std::vector<DPoint>& loop, std::int32_t origin_x, std::int32_t origin_y,
                         std::vector<Edge>& edges) {
  if (loop.size() < 3) {
    return;
  }
  double doubled_area = 0.0;
  for (std::size_t i = 0; i < loop.size(); ++i) {
    const auto& a = loop[i];
    const auto& b = loop[(i + 1) % loop.size()];
    doubled_area += a.x * b.y - b.x * a.y;
  }
  const bool reverse = doubled_area > 0.0;
  const std::int32_t shift_x = origin_x * kSub;
  const std::int32_t shift_y = origin_y * kSub;
  const auto point_at = [&](std::size_t index) {
    const auto& p = reverse ? loop[loop.size() - 1 - index] : loop[index];
    return FixedPoint{to_fixed(p.x) - shift_x, to_fixed(p.y) - shift_y};
  };
  FixedPoint previous = point_at(0);
  const FixedPoint first = previous;
  for (std::size_t i = 1; i < loop.size(); ++i) {
    const FixedPoint point = point_at(i);
    if (point.x != previous.x || point.y != previous.y) {
      edges.push_back(Edge{previous, point});
    }
    previous = point;
  }
  if (previous.x != first.x || previous.y != first.y) {
    edges.push_back(Edge{previous, first});
  }
}

// Subdivided arc fan between two unit vectors around `center` (radius h),
// using normalized-midpoint halving (sqrt only; no trig).
void append_arc_fan(const DPoint& center, DPoint from_unit, DPoint to_unit, double radius,
                    std::int32_t origin_x, std::int32_t origin_y, std::vector<Edge>& edges, int depth = 0) {
  const double chord_x = to_unit.x - from_unit.x;
  const double chord_y = to_unit.y - from_unit.y;
  const double chord = std::sqrt(chord_x * chord_x + chord_y * chord_y);
  if (depth >= 6 || chord * radius <= 0.25) {
    append_outline_loop({center,
                         DPoint{center.x + from_unit.x * radius, center.y + from_unit.y * radius},
                         DPoint{center.x + to_unit.x * radius, center.y + to_unit.y * radius}},
                        origin_x, origin_y, edges);
    return;
  }
  double mid_x = from_unit.x + to_unit.x;
  double mid_y = from_unit.y + to_unit.y;
  const double mid_length = std::sqrt(mid_x * mid_x + mid_y * mid_y);
  if (mid_length <= 1e-12) {
    // Opposite vectors (half circle): split via the perpendicular.
    mid_x = -from_unit.y;
    mid_y = from_unit.x;
  } else {
    mid_x /= mid_length;
    mid_y /= mid_length;
  }
  const DPoint mid{mid_x, mid_y};
  append_arc_fan(center, from_unit, mid, radius, origin_x, origin_y, edges, depth + 1);
  append_arc_fan(center, mid, to_unit, radius, origin_x, origin_y, edges, depth + 1);
}

// Builds the stroke outline loops for one run at half-width h.
void append_run_outline(const StrokeRun& run, double h, VectorStrokeCap cap, VectorStrokeJoin join,
                        double miter_limit, std::int32_t origin_x, std::int32_t origin_y,
                        std::vector<Edge>& edges) {
  const auto& pts = run.points;
  if (pts.size() < 2 || h <= 0.0) {
    return;
  }
  const std::size_t segment_count = run.closed ? pts.size() : pts.size() - 1;

  // Segment quads.
  std::vector<DPoint> directions(segment_count);
  for (std::size_t i = 0; i < segment_count; ++i) {
    const auto& a = pts[i];
    const auto& b = pts[(i + 1) % pts.size()];
    const double length = distance(a, b);
    if (length <= 1e-12) {
      directions[i] = DPoint{0.0, 0.0};
      continue;
    }
    directions[i] = DPoint{(b.x - a.x) / length, (b.y - a.y) / length};
    const DPoint n{-directions[i].y * h, directions[i].x * h};
    append_outline_loop({DPoint{a.x + n.x, a.y + n.y}, DPoint{b.x + n.x, b.y + n.y},
                         DPoint{b.x - n.x, b.y - n.y}, DPoint{a.x - n.x, a.y - n.y}},
                        origin_x, origin_y, edges);
  }

  // Joins at interior vertices (every vertex for closed runs).
  const std::size_t first_join = run.closed ? 0 : 1;
  const std::size_t join_count = run.closed ? pts.size() : (pts.size() >= 2 ? pts.size() - 2 : 0);
  for (std::size_t j = 0; j < join_count; ++j) {
    const std::size_t vertex = (first_join + j) % pts.size();
    const std::size_t incoming = (vertex + segment_count - 1) % segment_count;
    const std::size_t outgoing = vertex % segment_count;
    const DPoint du = directions[incoming];
    const DPoint dv = directions[outgoing];
    if ((du.x == 0.0 && du.y == 0.0) || (dv.x == 0.0 && dv.y == 0.0)) {
      continue;
    }
    const double cross = du.x * dv.y - du.y * dv.x;
    if (std::abs(cross) <= 1e-12) {
      continue;  // straight or reversal; quads already overlap
    }
    const double side = cross > 0.0 ? -1.0 : 1.0;  // outer side of the turn
    const DPoint n_in{-du.y * side, du.x * side};
    const DPoint n_out{-dv.y * side, dv.x * side};
    const DPoint& v = pts[vertex];
    if (join == VectorStrokeJoin::Bevel) {
      append_outline_loop({v, DPoint{v.x + n_in.x * h, v.y + n_in.y * h},
                           DPoint{v.x + n_out.x * h, v.y + n_out.y * h}},
                          origin_x, origin_y, edges);
    } else if (join == VectorStrokeJoin::Round) {
      append_arc_fan(v, n_in, n_out, h, origin_x, origin_y, edges);
    } else {
      // Miter: outer point along the normal bisector at h / cos(alpha/2);
      // ratio 1/cos(alpha/2) checked against the limit (bevel fallback).
      double bis_x = n_in.x + n_out.x;
      double bis_y = n_in.y + n_out.y;
      const double bis_length = std::sqrt(bis_x * bis_x + bis_y * bis_y);
      if (bis_length <= 1e-12) {
        append_outline_loop({v, DPoint{v.x + n_in.x * h, v.y + n_in.y * h},
                             DPoint{v.x + n_out.x * h, v.y + n_out.y * h}},
                            origin_x, origin_y, edges);
        continue;
      }
      bis_x /= bis_length;
      bis_y /= bis_length;
      const double cos_half = n_in.x * bis_x + n_in.y * bis_y;  // unit dot
      const double ratio = cos_half > 1e-9 ? 1.0 / cos_half : 1e9;
      if (ratio > std::max(miter_limit, 1.0)) {
        append_outline_loop({v, DPoint{v.x + n_in.x * h, v.y + n_in.y * h},
                             DPoint{v.x + n_out.x * h, v.y + n_out.y * h}},
                            origin_x, origin_y, edges);
      } else {
        const DPoint m{v.x + bis_x * h * ratio, v.y + bis_y * h * ratio};
        append_outline_loop({v, DPoint{v.x + n_in.x * h, v.y + n_in.y * h}, m,
                             DPoint{v.x + n_out.x * h, v.y + n_out.y * h}},
                            origin_x, origin_y, edges);
      }
    }
  }

  // Caps on open runs.
  if (!run.closed && cap != VectorStrokeCap::Butt) {
    const auto add_cap = [&](const DPoint& end, DPoint direction) {
      if (direction.x == 0.0 && direction.y == 0.0) {
        return;
      }
      const DPoint n{-direction.y, direction.x};
      if (cap == VectorStrokeCap::Square) {
        append_outline_loop(
            {DPoint{end.x + n.x * h, end.y + n.y * h},
             DPoint{end.x + n.x * h + direction.x * h, end.y + n.y * h + direction.y * h},
             DPoint{end.x - n.x * h + direction.x * h, end.y - n.y * h + direction.y * h},
             DPoint{end.x - n.x * h, end.y - n.y * h}},
            origin_x, origin_y, edges);
      } else {
        append_arc_fan(end, n, direction, h, origin_x, origin_y, edges);
        append_arc_fan(end, direction, DPoint{-n.x, -n.y}, h, origin_x, origin_y, edges);
      }
    };
    // Find the first/last non-degenerate directions.
    DPoint first_dir{0.0, 0.0};
    for (const auto& d : directions) {
      if (d.x != 0.0 || d.y != 0.0) {
        first_dir = d;
        break;
      }
    }
    DPoint last_dir{0.0, 0.0};
    for (auto it = directions.rbegin(); it != directions.rend(); ++it) {
      if (it->x != 0.0 || it->y != 0.0) {
        last_dir = *it;
        break;
      }
    }
    add_cap(pts.front(), DPoint{-first_dir.x, -first_dir.y});
    add_cap(pts.back(), last_dir);
  }
}

}  // namespace

std::vector<std::array<double, 2>> flatten_subpath_polyline(const PathSubpath& subpath) {
  std::vector<std::array<double, 2>> points;
  for (const auto& point : subpath_polyline(subpath)) {
    points.push_back({point.x, point.y});
  }
  return points;
}

CoverageBuffer rasterize_vector_path(const VectorPath& path, const VectorRasterOptions& options) {
  if (options.clip.empty()) {
    return CoverageBuffer{};
  }
  if (path.empty()) {
    // Empty path = cover everything (fill layers without a mask path).
    return full_coverage(options.clip);
  }

  // Split subpaths into shape groups (consecutive runs of equal shape_group).
  struct Group {
    std::size_t first{0};
    std::size_t end{0};
    PathCombineOp op{PathCombineOp::Add};
  };
  std::vector<Group> groups;
  for (std::size_t i = 0; i < path.subpaths.size();) {
    std::size_t j = i + 1;
    while (j < path.subpaths.size() && path.subpaths[j].shape_group == path.subpaths[i].shape_group) {
      ++j;
    }
    groups.push_back(Group{i, j, path.subpaths[i].op});
    i = j;
  }

  // Accumulator bounds: subtract-first shapes cover the whole clip;
  // otherwise the union of group bounds suffices.
  Rect accumulator_bounds{};
  if (!groups.empty() && groups.front().op == PathCombineOp::Subtract) {
    accumulator_bounds = options.clip;
  } else {
    for (const auto& group : groups) {
      const auto bounds = intersect_rects(group_pixel_bounds(path, group.first, group.end), options.clip);
      if (bounds.empty()) {
        continue;
      }
      if (accumulator_bounds.empty()) {
        accumulator_bounds = bounds;
      } else {
        const auto x0 = std::min(accumulator_bounds.x, bounds.x);
        const auto y0 = std::min(accumulator_bounds.y, bounds.y);
        const auto x1 = std::max(accumulator_bounds.x + accumulator_bounds.width, bounds.x + bounds.width);
        const auto y1 =
            std::max(accumulator_bounds.y + accumulator_bounds.height, bounds.y + bounds.height);
        accumulator_bounds = Rect{x0, y0, x1 - x0, y1 - y0};
      }
    }
  }
  if (accumulator_bounds.empty()) {
    return CoverageBuffer{};
  }

  CoverageBuffer accumulator;
  accumulator.bounds = accumulator_bounds;
  accumulator.pixels = PixelBuffer(accumulator_bounds.width, accumulator_bounds.height, PixelFormat::gray8());
  bool first_group = true;

  for (const auto& group : groups) {
    const auto group_bounds =
        intersect_rects(group_pixel_bounds(path, group.first, group.end), options.clip);
    CoverageBuffer group_coverage;
    if (!group_bounds.empty()) {
      group_coverage.bounds = group_bounds;
      group_coverage.pixels = PixelBuffer(group_bounds.width, group_bounds.height, PixelFormat::gray8());
      std::vector<Edge> edges;
      flatten_group(path, group.first, group.end, group_bounds.x, group_bounds.y, edges);
      rasterize_edges(edges, group_bounds.width, group_bounds.height, WindingRule::EvenOdd,
                      group_coverage.pixels);
    }

    if (first_group) {
      first_group = false;
      auto* out = accumulator.pixels.data().data();
      const auto stride = accumulator.pixels.stride_bytes();
      if (group.op == PathCombineOp::Subtract) {
        // Subtract-first: full canvas minus the shape.
        for (std::int32_t y = 0; y < accumulator_bounds.height; ++y) {
          auto* row = out + static_cast<std::size_t>(y) * stride;
          for (std::int32_t x = 0; x < accumulator_bounds.width; ++x) {
            const auto coverage =
                coverage_at(group_coverage, accumulator_bounds.x + x, accumulator_bounds.y + y);
            row[x] = static_cast<std::uint8_t>(255 - coverage);
          }
        }
      } else {
        // Add/Intersect/Xor first: exactly the group's coverage.
        for (std::int32_t y = 0; y < accumulator_bounds.height; ++y) {
          auto* row = out + static_cast<std::size_t>(y) * stride;
          for (std::int32_t x = 0; x < accumulator_bounds.width; ++x) {
            row[x] = coverage_at(group_coverage, accumulator_bounds.x + x, accumulator_bounds.y + y);
          }
        }
      }
      continue;
    }

    auto* out = accumulator.pixels.data().data();
    const auto stride = accumulator.pixels.stride_bytes();
    // Only the affected region needs touching for Add/Xor/Subtract; Intersect
    // clears everything outside the group too.
    if (group.op == PathCombineOp::Intersect) {
      for (std::int32_t y = 0; y < accumulator_bounds.height; ++y) {
        auto* row = out + static_cast<std::size_t>(y) * stride;
        for (std::int32_t x = 0; x < accumulator_bounds.width; ++x) {
          const auto coverage =
              coverage_at(group_coverage, accumulator_bounds.x + x, accumulator_bounds.y + y);
          row[x] = combine_coverage(PathCombineOp::Intersect, row[x], coverage);
        }
      }
    } else if (!group_coverage.bounds.empty()) {
      const auto region = intersect_rects(group_coverage.bounds, accumulator_bounds);
      for (std::int32_t y = 0; y < region.height; ++y) {
        auto* row = out + static_cast<std::size_t>(region.y - accumulator_bounds.y + y) * stride;
        for (std::int32_t x = 0; x < region.width; ++x) {
          const auto document_x = region.x + x;
          const auto document_y = region.y + y;
          const auto coverage = coverage_at(group_coverage, document_x, document_y);
          auto& pixel = row[region.x - accumulator_bounds.x + x];
          pixel = combine_coverage(group.op, pixel, coverage);
        }
      }
    }
  }

  trim_coverage(accumulator);
  return accumulator;
}

CoverageBuffer rasterize_vector_stroke(const VectorPath& path, const VectorStroke& stroke,
                                       const VectorRasterOptions& options) {
  if (options.clip.empty() || path.empty() || !(stroke.width > 0.0)) {
    return CoverageBuffer{};
  }
  // Inside/outside strokes rasterize the centered band at DOUBLE width, then
  // clip by the fill region (or its complement): the clipped half is exactly
  // `width` deep and its path-edge side keeps the crisp fill-coverage AA.
  const bool centered = stroke.alignment == VectorStrokeAlignment::Center;
  const double geometry_width = centered ? stroke.width : stroke.width * 2.0;
  const double half = geometry_width / 2.0;

  // Resolve dash entries (stroke-width multiples) to pixels.
  std::vector<double> dashes_px;
  dashes_px.reserve(stroke.dashes.size());
  for (const auto dash : stroke.dashes) {
    dashes_px.push_back(dash * stroke.width);
  }
  const double offset_px = stroke.dash_offset * stroke.width;

  // Build the outline first at origin (0,0), then size the band from the
  // emitted edges' true hull: miter tips reach up to miter_limit x half-width
  // past the path hull, so any up-front padding guess either wastes memory or
  // crops spikes into artifacts at the buffer edge (the pre-July-2026
  // half*2+2 guess did the latter). Every loop's interior lies inside its own
  // vertex hull, so the edge hull is exact.
  std::vector<Edge> edges;
  for (const auto& subpath : path.subpaths) {
    const auto polyline = subpath_polyline(subpath);
    if (polyline.size() < 2) {
      continue;
    }
    const auto runs = apply_dashes(polyline, subpath.closed, dashes_px, offset_px);
    for (const auto& run : runs) {
      append_run_outline(run, half, stroke.cap, stroke.join, stroke.miter_limit, 0, 0, edges);
    }
  }
  if (edges.empty()) {
    return CoverageBuffer{};
  }
  std::int32_t min_fx = edges.front().from.x;
  std::int32_t max_fx = min_fx;
  std::int32_t min_fy = edges.front().from.y;
  std::int32_t max_fy = min_fy;
  for (const auto& edge : edges) {
    min_fx = std::min({min_fx, edge.from.x, edge.to.x});
    max_fx = std::max({max_fx, edge.from.x, edge.to.x});
    min_fy = std::min({min_fy, edge.from.y, edge.to.y});
    max_fy = std::max({max_fy, edge.from.y, edge.to.y});
  }
  const auto fixed_floor_div = [](std::int32_t v) {
    return v >= 0 ? v / kSub : -((-v + kSub - 1) / kSub);
  };
  Rect band_bounds{fixed_floor_div(min_fx), fixed_floor_div(min_fy), 0, 0};
  band_bounds.width = fixed_floor_div(max_fx) + 1 - band_bounds.x;
  band_bounds.height = fixed_floor_div(max_fy) + 1 - band_bounds.y;
  band_bounds = intersect_rects(band_bounds, options.clip);
  if (band_bounds.empty()) {
    return CoverageBuffer{};
  }
  const std::int32_t shift_x = band_bounds.x * kSub;
  const std::int32_t shift_y = band_bounds.y * kSub;
  for (auto& edge : edges) {
    edge.from.x -= shift_x;
    edge.from.y -= shift_y;
    edge.to.x -= shift_x;
    edge.to.y -= shift_y;
  }
  CoverageBuffer band;
  band.bounds = band_bounds;
  band.pixels = PixelBuffer(band_bounds.width, band_bounds.height, PixelFormat::gray8());
  rasterize_edges(edges, band_bounds.width, band_bounds.height, WindingRule::NonZero, band.pixels);

  if (!centered) {
    VectorRasterOptions region_options;
    region_options.clip = options.clip;
    const auto region = rasterize_vector_path(path, region_options);
    auto* bytes = band.pixels.data().data();
    const auto stride = band.pixels.stride_bytes();
    const bool inside = stroke.alignment == VectorStrokeAlignment::Inside;
    for (std::int32_t y = 0; y < band_bounds.height; ++y) {
      auto* row = bytes + static_cast<std::size_t>(y) * stride;
      for (std::int32_t x = 0; x < band_bounds.width; ++x) {
        const auto region_coverage = coverage_at(region, band_bounds.x + x, band_bounds.y + y);
        const auto clip_value = inside ? region_coverage : 255 - region_coverage;
        row[x] = static_cast<std::uint8_t>((row[x] * clip_value + 127) / 255);
      }
    }
  }
  trim_coverage(band);
  return band;
}

namespace {

// The document bake covers the whole shape wherever it sits, the way a pixel
// layer keeps its pixels past the canvas edge. Clipped to the canvas, a shape
// transformed fully onto the pasteboard baked to nothing (the Move tool then
// had nothing to grab, and the shape was gone), and a half-off shape moved
// back in showed only its clipped half until something re-baked it. Coverage
// buffers are sized to the shape, not the domain, so a far-off shape costs
// its own size. Shapes whose coverage fills the whole clip (a disabled or
// inverted path, a subtract-first combine) and oversized paths keep the
// canvas.
Rect shape_bake_domain(const VectorShapeContent& content, Rect canvas) {
  if (content.path_disabled || content.path_inverted || content.path.empty() ||
      content.path.subpaths.front().op == PathCombineOp::Subtract) {
    return canvas;
  }
  const auto hull = content.path.bounds();
  if (!hull.has_value()) {
    return canvas;
  }
  // Stroke reach (outside alignment, square caps, modest miters) plus the
  // antialiasing pixel; anything past it only loses off-canvas pixels.
  const double pad = (content.stroke.enabled ? content.stroke.width * 4.0 : 0.0) + 2.0;
  const auto left = static_cast<std::int64_t>(std::floor(hull->left - pad));
  const auto top = static_cast<std::int64_t>(std::floor(hull->top - pad));
  const auto right = static_cast<std::int64_t>(std::ceil(hull->right + pad));
  const auto bottom = static_cast<std::int64_t>(std::ceil(hull->bottom + pad));
  const auto area = (right - left) * (bottom - top);
  const auto canvas_area = static_cast<std::int64_t>(canvas.width) * canvas.height;
  constexpr std::int64_t kMinBudget = 16LL * 1024 * 1024;
  constexpr std::int64_t kCoordinateLimit = 1LL << 29;
  if (area <= 0 || area > std::max(kMinBudget, 4 * canvas_area) || left < -kCoordinateLimit ||
      top < -kCoordinateLimit || right > kCoordinateLimit || bottom > kCoordinateLimit) {
    return canvas;
  }
  const auto x0 = std::min<std::int64_t>(canvas.x, left);
  const auto y0 = std::min<std::int64_t>(canvas.y, top);
  const auto x1 = std::max<std::int64_t>(static_cast<std::int64_t>(canvas.x) + canvas.width, right);
  const auto y1 = std::max<std::int64_t>(static_cast<std::int64_t>(canvas.y) + canvas.height, bottom);
  return Rect{static_cast<std::int32_t>(x0), static_cast<std::int32_t>(y0), static_cast<std::int32_t>(x1 - x0),
              static_cast<std::int32_t>(y1 - y0)};
}

}  // namespace

void update_vector_shape_raster(Layer& layer, Rect canvas, const PatternStore* patterns) {
  const auto* shape = layer.vector_shape();
  if (shape == nullptr) {
    return;
  }
  const auto domain = shape_bake_domain(*shape, canvas);
  // Paint geometry (unaligned gradients, pattern phase) stays on the canvas.
  const VectorPaintBounds paint_bounds{canvas, std::nullopt, std::nullopt};
  const bool extended = domain.x != canvas.x || domain.y != canvas.y || domain.width != canvas.width ||
                        domain.height != canvas.height;
  auto raster = rasterize_vector_shape(*shape, domain, patterns, &layer, extended ? &paint_bounds : nullptr);
  layer.set_pixels(std::move(raster.pixels));
  layer.set_bounds(raster.bounds);
  // The split planes ride the content so the compositor can apply interior
  // overlays under the vector stroke; they stay in lockstep with pixels().
  // Content is shared immutably (undo snapshots hold the same object), so the
  // caches land through set_vector_shape on a copy, never in place.
  auto updated = *shape;
  updated.fill_cache = std::move(raster.fill_pixels);
  updated.stroke_cache = std::move(raster.stroke_pixels);
  layer.set_vector_shape(std::move(updated));
  layer.metadata()[kLayerMetadataVectorRasterStatus] = kVectorRasterStatusPatchy;
}

namespace {

// Photoshop's vector-mask feather: a gaussian of sigma = feather pixels over
// the path coverage (PS 27.9 calibration, photoshop-vector-mask-feather.psd),
// approximated by the deterministic three-box blur the raster mask feather
// uses. `coverage` must already span `reach` pixels beyond the area of
// interest; the blur grows it by `reach` within `domain`, which is where the
// passes edge-clamp.
void feather_coverage(CoverageBuffer& coverage, const std::array<std::int32_t, 3>& radii, Rect domain) {
  const std::int32_t reach = radii[0] + radii[1] + radii[2];
  if (reach <= 0 || coverage.bounds.empty()) {
    return;
  }
  Rect expanded{coverage.bounds.x - reach, coverage.bounds.y - reach,
                coverage.bounds.width + 2 * reach, coverage.bounds.height + 2 * reach};
  expanded = intersect_rects(expanded, domain);
  if (expanded.empty()) {
    return;
  }
  const auto width = static_cast<std::size_t>(expanded.width);
  std::vector<std::uint16_t> plane(width * static_cast<std::size_t>(expanded.height));
  for (std::int32_t y = 0; y < expanded.height; ++y) {
    auto* row = plane.data() + static_cast<std::size_t>(y) * width;
    for (std::int32_t x = 0; x < expanded.width; ++x) {
      row[x] = static_cast<std::uint16_t>(coverage_at(coverage, expanded.x + x, expanded.y + y) * 257U);
    }
  }
  mask_feather_blur(plane, expanded.width, expanded.height, radii);
  PixelBuffer blurred(expanded.width, expanded.height, PixelFormat::gray8());
  for (std::int32_t y = 0; y < expanded.height; ++y) {
    auto* out = blurred.row(y).data();
    const auto* in = plane.data() + static_cast<std::size_t>(y) * width;
    for (std::int32_t x = 0; x < expanded.width; ++x) {
      out[x] = static_cast<std::uint8_t>((in[x] + 128U) / 257U);
    }
  }
  coverage.bounds = expanded;
  coverage.pixels = std::move(blurred);
}

CoverageBuffer crop_coverage(const CoverageBuffer& coverage, Rect clip) {
  CoverageBuffer cropped;
  cropped.bounds = intersect_rects(coverage.bounds, clip);
  if (cropped.bounds.empty()) {
    cropped.bounds = {};
    return cropped;
  }
  cropped.pixels = PixelBuffer(cropped.bounds.width, cropped.bounds.height, PixelFormat::gray8());
  for (std::int32_t y = 0; y < cropped.bounds.height; ++y) {
    const auto source = coverage.pixels.row(cropped.bounds.y - coverage.bounds.y + y)
                            .subspan(static_cast<std::size_t>(cropped.bounds.x - coverage.bounds.x),
                                     static_cast<std::size_t>(cropped.bounds.width));
    std::copy(source.begin(), source.end(), cropped.pixels.row(y).begin());
  }
  return cropped;
}

// Shape-layer density (photoshop-shape-feather.psd, the density-60 layer
// tinted the whole canvas): the fill shows everywhere at (255 - density)/255,
// the vector-mask rule applied to the shape's own coverage over `domain`.
CoverageBuffer apply_shape_density(const CoverageBuffer& coverage, std::uint8_t density, Rect domain) {
  CoverageBuffer result;
  if (domain.empty()) {
    return result;
  }
  result.bounds = domain;
  result.pixels = PixelBuffer(domain.width, domain.height, PixelFormat::gray8());
  const auto floor_value = static_cast<unsigned>(255U - density);
  for (std::int32_t y = 0; y < domain.height; ++y) {
    auto* row = result.pixels.row(y).data();
    for (std::int32_t x = 0; x < domain.width; ++x) {
      const auto value = coverage_at(coverage, domain.x + x, domain.y + y);
      row[x] = static_cast<std::uint8_t>((value * density + floor_value * 255U + 127U) / 255U);
    }
  }
  return result;
}

// Shape-layer feather: Photoshop blurs the whole rendered shape, fill AND
// stroke (the probe's dark inside stroke became a soft smear), with the
// vector-mask gaussian, unclamped at the canvas. Straight-alpha RGBA planes
// blur premultiplied, then the result is cropped back to the canvas.
PixelBuffer feather_rgba_plane(const PixelBuffer& pixels, Rect bounds, Rect expanded,
                               const std::array<std::int32_t, 3>& radii) {
  const auto width = static_cast<std::size_t>(expanded.width);
  const auto count = width * static_cast<std::size_t>(expanded.height);
  std::array<std::vector<std::uint16_t>, 4> planes;
  for (auto& plane : planes) {
    plane.assign(count, 0);
  }
  for (std::int32_t y = 0; y < bounds.height; ++y) {
    const auto* row = pixels.data().data() + static_cast<std::size_t>(y) * pixels.stride_bytes();
    const auto target_y = bounds.y - expanded.y + y;
    if (target_y < 0 || target_y >= expanded.height) {
      continue;
    }
    for (std::int32_t x = 0; x < bounds.width; ++x) {
      const auto target_x = bounds.x - expanded.x + x;
      if (target_x < 0 || target_x >= expanded.width) {
        continue;
      }
      const auto index = static_cast<std::size_t>(target_y) * width + static_cast<std::size_t>(target_x);
      const unsigned alpha = row[x * 4 + 3];
      planes[3][index] = static_cast<std::uint16_t>(alpha * 257U);
      for (int channel = 0; channel < 3; ++channel) {
        planes[static_cast<std::size_t>(channel)][index] =
            static_cast<std::uint16_t>((row[x * 4 + channel] * alpha * 257U + 127U) / 255U);
      }
    }
  }
  for (auto& plane : planes) {
    mask_feather_blur(plane, expanded.width, expanded.height, radii);
  }
  PixelBuffer blurred(expanded.width, expanded.height, PixelFormat::rgba8());
  for (std::int32_t y = 0; y < expanded.height; ++y) {
    auto* out = blurred.data().data() + static_cast<std::size_t>(y) * blurred.stride_bytes();
    for (std::int32_t x = 0; x < expanded.width; ++x) {
      const auto index = static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x);
      const unsigned alpha16 = planes[3][index];
      const auto alpha = static_cast<std::uint8_t>((alpha16 + 128U) / 257U);
      for (int channel = 0; channel < 3; ++channel) {
        const unsigned value16 = planes[static_cast<std::size_t>(channel)][index];
        out[x * 4 + channel] = alpha16 > 0 ? static_cast<std::uint8_t>(std::min<unsigned>(255U, (value16 * 255U + alpha16 / 2U) / alpha16)) : 0;
      }
      out[x * 4 + 3] = alpha;
    }
  }
  return blurred;
}

PixelBuffer crop_rgba(const PixelBuffer& pixels, Rect bounds, Rect clip) {
  PixelBuffer cropped(clip.width, clip.height, PixelFormat::rgba8());
  for (std::int32_t y = 0; y < clip.height; ++y) {
    const auto* source = pixels.data().data() +
                         static_cast<std::size_t>(clip.y - bounds.y + y) * pixels.stride_bytes() +
                         static_cast<std::size_t>(clip.x - bounds.x) * 4;
    std::memcpy(cropped.data().data() + static_cast<std::size_t>(y) * cropped.stride_bytes(), source,
                static_cast<std::size_t>(clip.width) * 4);
  }
  return cropped;
}

void feather_shape_raster(ShapeRasterResult& result, const std::array<std::int32_t, 3>& radii, Rect domain,
                          Rect canvas) {
  const std::int32_t reach = radii[0] + radii[1] + radii[2];
  if (reach <= 0 || result.bounds.empty()) {
    return;
  }
  Rect expanded{result.bounds.x - reach, result.bounds.y - reach, result.bounds.width + 2 * reach,
                result.bounds.height + 2 * reach};
  expanded = intersect_rects(expanded, domain);
  const auto clip = intersect_rects(expanded, canvas);
  if (expanded.empty() || clip.empty()) {
    result.bounds = {};
    result.pixels = PixelBuffer();
    result.fill_pixels = PixelBuffer();
    result.stroke_pixels = PixelBuffer();
    return;
  }
  const auto process = [&](PixelBuffer& pixels) {
    if (pixels.empty()) {
      return;
    }
    pixels = crop_rgba(feather_rgba_plane(pixels, result.bounds, expanded, radii), expanded, clip);
  };
  process(result.pixels);
  process(result.fill_pixels);
  process(result.stroke_pixels);
  result.bounds = clip;
}

}  // namespace

void update_vector_mask_raster(Layer& layer, Rect canvas) {
  const auto* mask = layer.vector_mask();
  if (mask == nullptr) {
    return;
  }
  auto updated = *mask;
  const auto radii = updated.feather > 0.05 ? mask_feather_box_radii(updated.feather)
                                            : std::array<std::int32_t, 3>{};
  const auto reach = radii[0] + radii[1] + radii[2];
  CoverageBuffer coverage;
  if (reach > 0 && !canvas.empty()) {
    // Unlike the raster mask's, this feather does NOT clamp at the canvas:
    // coverage beyond the edge is whatever the path says (a path ending on the
    // canvas edge fades there). Rasterize `reach` pixels past the canvas so
    // every in-canvas pixel blurs real coverage, then crop back.
    const Rect domain{canvas.x - reach, canvas.y - reach, canvas.width + 2 * reach, canvas.height + 2 * reach};
    coverage = rasterize_vector_mask_coverage(updated, domain);
    feather_coverage(coverage, radii, domain);
    coverage = crop_coverage(coverage, canvas);
  } else {
    coverage = rasterize_vector_mask_coverage(updated, canvas);
  }
  updated.cache_bounds = coverage.bounds;
  updated.cache = std::move(coverage.pixels);
  layer.set_vector_mask(std::move(updated));
}

CoverageBuffer rasterize_vector_mask_coverage(const LayerVectorMask& mask, Rect clip) {
  VectorRasterOptions options;
  options.clip = clip;
  auto coverage = rasterize_vector_path(mask.path, options);
  if (!mask.inverted) {
    return coverage;
  }
  auto inverted = full_coverage(clip);
  if (inverted.bounds.empty()) {
    return inverted;
  }
  auto* out = inverted.pixels.data().data();
  const auto stride = inverted.pixels.stride_bytes();
  for (std::int32_t y = 0; y < clip.height; ++y) {
    auto* row = out + static_cast<std::size_t>(y) * stride;
    for (std::int32_t x = 0; x < clip.width; ++x) {
      row[x] = static_cast<std::uint8_t>(255 - coverage_at(coverage, clip.x + x, clip.y + y));
    }
  }
  return inverted;
}

namespace {

// Paints a coverage buffer with a VectorFill into straight-alpha RGBA8.
PixelBuffer paint_coverage(const CoverageBuffer& coverage, const VectorFill& fill, Rect canvas,
                           const PatternStore* patterns, const Layer* layer_for_pattern_anchor,
                           std::optional<Rect> aligned_bounds) {
  PixelBuffer pixels(coverage.bounds.width, coverage.bounds.height, PixelFormat::rgba8());
  auto* out = pixels.data().data();
  const auto out_stride = pixels.stride_bytes();
  const auto* cov = coverage.pixels.data().data();
  const auto cov_stride = coverage.pixels.stride_bytes();

  if (fill.kind == VectorFillKind::Solid) {
    for (std::int32_t y = 0; y < coverage.bounds.height; ++y) {
      auto* row = out + static_cast<std::size_t>(y) * out_stride;
      const auto* cov_row = cov + static_cast<std::size_t>(y) * cov_stride;
      for (std::int32_t x = 0; x < coverage.bounds.width; ++x) {
        row[x * 4 + 0] = fill.color.red;
        row[x * 4 + 1] = fill.color.green;
        row[x * 4 + 2] = fill.color.blue;
        row[x * 4 + 3] = cov_row[x];
      }
    }
    return pixels;
  }

  if (fill.kind == VectorFillKind::Gradient) {
    // align_with_layer follows the painted region's bounds (the layer's
    // transparency bounds once baked); otherwise the canvas.
    // Fill layers render Photoshop's GdFl geometry: the linear span is the
    // center chord of the aligned bounds and a Classic 2-stop ramp eases by
    // the clamped catmull-rom scaled by smoothness (probe5c/5d, PS 27.8).
    const Rect gradient_bounds = fill.gradient.align_with_layer ? aligned_bounds.value_or(coverage.bounds) : canvas;
    for (std::int32_t y = 0; y < coverage.bounds.height; ++y) {
      auto* row = out + static_cast<std::size_t>(y) * out_stride;
      const auto* cov_row = cov + static_cast<std::size_t>(y) * cov_stride;
      const auto document_y = coverage.bounds.y + y;
      for (std::int32_t x = 0; x < coverage.bounds.width; ++x) {
        const auto document_x = coverage.bounds.x + x;
        const auto position = gradient_position(fill.gradient, gradient_bounds, document_x,
                                                document_y, GradientSpanBasis::CenterChord);
        const auto color =
            gradient_color_dithered(fill.gradient, position, document_x, document_y, true);
        const auto opacity = gradient_stop_opacity(fill.gradient, position, true);
        row[x * 4 + 0] = color.red;
        row[x * 4 + 1] = color.green;
        row[x * 4 + 2] = color.blue;
        row[x * 4 + 3] = static_cast<std::uint8_t>(
            std::clamp<long>(std::lround(static_cast<double>(cov_row[x]) * opacity), 0L, 255L));
      }
    }
    return pixels;
  }

  if (fill.kind == VectorFillKind::Pattern) {
    const PatternResource* resource = patterns != nullptr ? patterns->find(fill.pattern_id) : nullptr;
    if (resource == nullptr || resource->tile.width() <= 0 || resource->tile.height() <= 0) {
      return pixels;  // transparent (missing tiles)
    }
    const Layer anchor_fallback;
    const Layer& anchor_layer =
        layer_for_pattern_anchor != nullptr ? *layer_for_pattern_anchor : anchor_fallback;
    const PatternTileSampler sampler(resource->tile, anchor_layer, static_cast<float>(fill.pattern_scale),
                                     static_cast<float>(fill.pattern_angle_degrees), fill.pattern_linked,
                                     static_cast<float>(fill.pattern_phase_x),
                                     static_cast<float>(fill.pattern_phase_y));
    for (std::int32_t y = 0; y < coverage.bounds.height; ++y) {
      auto* row = out + static_cast<std::size_t>(y) * out_stride;
      const auto* cov_row = cov + static_cast<std::size_t>(y) * cov_stride;
      const auto document_y = coverage.bounds.y + y;
      for (std::int32_t x = 0; x < coverage.bounds.width; ++x) {
        const auto sample = sampler.sample(coverage.bounds.x + x, document_y);
        row[x * 4 + 0] = sample.color.red;
        row[x * 4 + 1] = sample.color.green;
        row[x * 4 + 2] = sample.color.blue;
        row[x * 4 + 3] = static_cast<std::uint8_t>(
            std::clamp<long>(std::lround(static_cast<double>(cov_row[x]) * sample.alpha), 0L, 255L));
      }
    }
  }
  return pixels;
}

Rect union_rects(Rect a, Rect b) noexcept {
  if (a.empty()) {
    return b;
  }
  if (b.empty()) {
    return a;
  }
  const auto x0 = std::min(a.x, b.x);
  const auto y0 = std::min(a.y, b.y);
  const auto x1 = std::max(a.x + a.width, b.x + b.width);
  const auto y1 = std::max(a.y + a.height, b.y + b.height);
  return Rect{x0, y0, x1 - x0, y1 - y0};
}

}  // namespace

ShapeRasterResult rasterize_vector_shape(const VectorShapeContent& content, Rect canvas,
                                         const PatternStore* patterns,
                                         const Layer* layer_for_pattern_anchor,
                                         const VectorPaintBounds* paint_bounds) {
  ShapeRasterResult result;
  if (!content.parts.empty()) {
    struct PaintedPart { ShapeRasterResult raster; double opacity; };
    std::vector<PaintedPart> painted;
    painted.reserve(content.parts.size());
    // Each part aligns to its own coverage, but keeps the caller's canvas.
    std::optional<VectorPaintBounds> part_paint_bounds;
    if (paint_bounds != nullptr) {
      part_paint_bounds = VectorPaintBounds{paint_bounds->canvas, std::nullopt, std::nullopt};
    }
    for (const auto& part : content.parts) {
      if (part.opacity <= 0.0F || part.fill_opacity <= 0.0F) { continue; }
      Layer anchor(0, {}, LayerKind::Pixel);
      set_layer_effects_reference_point(anchor, part.pattern_anchor[0], part.pattern_anchor[1]);
      auto raster = rasterize_vector_shape(vector_shape_part_content(content, part), canvas, patterns, &anchor,
                                           part_paint_bounds.has_value() ? &*part_paint_bounds : nullptr);
      result.bounds = union_rects(result.bounds, raster.bounds);
      painted.push_back({std::move(raster), static_cast<double>(part.opacity) * part.fill_opacity});
    }
    if (result.bounds.empty()) { return result; }
    result.pixels = PixelBuffer(result.bounds.width, result.bounds.height, PixelFormat::rgba8());
    for (const auto& part : painted) {
      const auto& raster = part.raster;
      for (int y = 0; y < raster.bounds.height; ++y) {
        const auto* source = raster.pixels.data().data() + static_cast<std::size_t>(y) * raster.pixels.stride_bytes();
        auto* target = result.pixels.pixel(raster.bounds.x - result.bounds.x, raster.bounds.y - result.bounds.y + y);
        for (int x = 0; x < raster.bounds.width; ++x, source += 4, target += 4) {
          const double alpha = source[3] / 255.0 * part.opacity;
          if (alpha <= 0.0) { continue; }
          const double retained = target[3] / 255.0 * (1.0 - alpha);
          const double combined = alpha + retained;
          for (int c = 0; c < 3; ++c) {
            target[c] = static_cast<std::uint8_t>(std::clamp(std::lround((source[c] * alpha + target[c] * retained) / combined), 0L, 255L));
          }
          target[3] = static_cast<std::uint8_t>(std::clamp(std::lround(combined * 255.0), 0L, 255L));
        }
      }
    }
    return result;
  }
  VectorRasterOptions options;
  // Feather rasterizes `reach` pixels past the canvas so every in-canvas pixel
  // blurs real coverage (unclamped, the vector-mask recipe), then crops back.
  const auto feather_radii = content.feather > 0.05 ? mask_feather_box_radii(content.feather)
                                                    : std::array<std::int32_t, 3>{};
  const std::int32_t feather_reach = feather_radii[0] + feather_radii[1] + feather_radii[2];
  const Rect raster_domain =
      feather_reach > 0 ? Rect{canvas.x - feather_reach, canvas.y - feather_reach,
                               canvas.width + 2 * feather_reach, canvas.height + 2 * feather_reach}
                        : canvas;
  options.clip = raster_domain;

  const bool fill_on = content.stroke.fill_enabled && content.fill.kind != VectorFillKind::None;
  CoverageBuffer fill_coverage;
  if (fill_on) {
    if (content.path_disabled) {
      // Disabled shape path: the fill covers the whole canvas (mask off).
      VectorPath everything;
      fill_coverage = rasterize_vector_path(everything, options);
    } else if (content.path_inverted) {
      LayerVectorMask inverter;
      inverter.path = content.path;
      inverter.inverted = true;
      fill_coverage = rasterize_vector_mask_coverage(inverter, raster_domain);
    } else {
      fill_coverage = rasterize_vector_path(content.path, options);
    }
    if (content.density != 255) {
      fill_coverage = apply_shape_density(fill_coverage, content.density, raster_domain);
    }
  }
  const bool stroke_on =
      content.stroke.enabled && content.stroke.width > 0.0 && content.stroke.opacity > 0.0 &&
      content.stroke.content.kind != VectorFillKind::None && !content.path.empty() &&
      !content.path_disabled;
  CoverageBuffer stroke_coverage;
  if (stroke_on) {
    stroke_coverage = rasterize_vector_stroke(content.path, content.stroke, options);
  }

  const Rect bounds = union_rects(fill_coverage.bounds, stroke_coverage.bounds);
  if (bounds.empty()) {
    return result;
  }
  result.bounds = bounds;
  result.pixels = PixelBuffer(bounds.width, bounds.height, PixelFormat::rgba8());
  auto* out = result.pixels.data().data();
  const auto out_stride = result.pixels.stride_bytes();
  // Interior overlay effects need the fill and stroke as separate planes
  // (stroke composites above the overlays); a non-Normal stroke blend folds
  // against the fill during the bake and cannot be re-stamped standalone.
  const bool split_planes = !fill_coverage.bounds.empty() && !stroke_coverage.bounds.empty() &&
                            content.stroke.blend_mode == BlendMode::Normal;

  if (!fill_coverage.bounds.empty()) {
    const auto fill_pixels =
        paint_coverage(fill_coverage, content.fill, paint_bounds ? paint_bounds->canvas : canvas, patterns,
                       layer_for_pattern_anchor, paint_bounds ? paint_bounds->fill : std::nullopt);
    const auto* src = fill_pixels.data().data();
    const auto src_stride = fill_pixels.stride_bytes();
    if (split_planes) {
      result.fill_pixels = PixelBuffer(bounds.width, bounds.height, PixelFormat::rgba8());
    }
    for (std::int32_t y = 0; y < fill_coverage.bounds.height; ++y) {
      const auto row_offset =
          static_cast<std::size_t>(fill_coverage.bounds.y - bounds.y + y) * out_stride +
          static_cast<std::size_t>(fill_coverage.bounds.x - bounds.x) * 4;
      std::memcpy(out + row_offset, src + static_cast<std::size_t>(y) * src_stride,
                  static_cast<std::size_t>(fill_coverage.bounds.width) * 4);
      if (split_planes) {
        std::memcpy(result.fill_pixels.data().data() + row_offset,
                    src + static_cast<std::size_t>(y) * src_stride,
                    static_cast<std::size_t>(fill_coverage.bounds.width) * 4);
      }
    }
  }

  if (!stroke_coverage.bounds.empty()) {
    // The stroke paints over the fill within the same raster. Blend mode and
    // opacity apply against the fill where it exists; Photoshop composites
    // live shape strokes against the full backdrop, so non-Normal stroke
    // modes against content BELOW the layer are approximated by this baked
    // result.
    const auto stroke_pixels =
        paint_coverage(stroke_coverage, content.stroke.content, paint_bounds ? paint_bounds->canvas : canvas, patterns,
                       layer_for_pattern_anchor, paint_bounds ? paint_bounds->stroke : std::nullopt);
    const auto* src = stroke_pixels.data().data();
    const auto src_stride = stroke_pixels.stride_bytes();
    if (split_planes) {
      // Standalone stroke plane: paint color with coverage x stroke opacity
      // (quantized separately so the combined bake below stays byte-stable).
      result.stroke_pixels = PixelBuffer(bounds.width, bounds.height, PixelFormat::rgba8());
      auto* plane = result.stroke_pixels.data().data();
      const auto plane_stride = result.stroke_pixels.stride_bytes();
      for (std::int32_t y = 0; y < stroke_coverage.bounds.height; ++y) {
        auto* plane_row = plane +
                          static_cast<std::size_t>(stroke_coverage.bounds.y - bounds.y + y) * plane_stride +
                          static_cast<std::size_t>(stroke_coverage.bounds.x - bounds.x) * 4;
        const auto* stroke_row = src + static_cast<std::size_t>(y) * src_stride;
        for (std::int32_t x = 0; x < stroke_coverage.bounds.width; ++x) {
          const auto alpha =
              (static_cast<double>(stroke_row[x * 4 + 3]) / 255.0) * content.stroke.opacity;
          plane_row[x * 4 + 0] = stroke_row[x * 4 + 0];
          plane_row[x * 4 + 1] = stroke_row[x * 4 + 1];
          plane_row[x * 4 + 2] = stroke_row[x * 4 + 2];
          plane_row[x * 4 + 3] =
              static_cast<std::uint8_t>(std::clamp<long>(std::lround(alpha * 255.0), 0L, 255L));
        }
      }
    }
    for (std::int32_t y = 0; y < stroke_coverage.bounds.height; ++y) {
      auto* row = out + static_cast<std::size_t>(stroke_coverage.bounds.y - bounds.y + y) * out_stride +
                  static_cast<std::size_t>(stroke_coverage.bounds.x - bounds.x) * 4;
      const auto* stroke_row = src + static_cast<std::size_t>(y) * src_stride;
      for (std::int32_t x = 0; x < stroke_coverage.bounds.width; ++x) {
        const double source_alpha =
            (static_cast<double>(stroke_row[x * 4 + 3]) / 255.0) * content.stroke.opacity;
        if (source_alpha <= 0.0) {
          continue;
        }
        auto* dest = row + static_cast<std::size_t>(x) * 4;
        const double dest_alpha = static_cast<double>(dest[3]) / 255.0;
        std::array<std::uint8_t, 3> source_rgb{stroke_row[x * 4 + 0], stroke_row[x * 4 + 1],
                                               stroke_row[x * 4 + 2]};
        if (content.stroke.blend_mode != BlendMode::Normal && dest_alpha > 0.0) {
          source_rgb = composite_blended_rgb(source_rgb, {dest[0], dest[1], dest[2]},
                                             content.stroke.blend_mode, 1.0F,
                                             static_cast<float>(dest_alpha));
        }
        const double out_alpha = source_alpha + dest_alpha * (1.0 - source_alpha);
        if (out_alpha <= 0.0) {
          dest[0] = dest[1] = dest[2] = dest[3] = 0;
          continue;
        }
        for (int channel = 0; channel < 3; ++channel) {
          const double blended = (source_rgb[static_cast<std::size_t>(channel)] * source_alpha +
                                  dest[channel] * dest_alpha * (1.0 - source_alpha)) /
                                 out_alpha;
          dest[channel] = static_cast<std::uint8_t>(std::clamp<long>(std::lround(blended), 0L, 255L));
        }
        dest[3] = static_cast<std::uint8_t>(std::clamp<long>(std::lround(out_alpha * 255.0), 0L, 255L));
      }
    }
  }
  if (feather_reach > 0) {
    feather_shape_raster(result, feather_radii, raster_domain, canvas);
  }
  return result;
}

}  // namespace patchy
