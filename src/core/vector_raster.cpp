#include "core/vector_raster.hpp"

#include "core/blend_math.hpp"
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
  const auto push = [&points](double x, double y) {
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
    flatten_cubic_recursive(from, c1, c2, to, 6, segment_edges);
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
                                    const std::vector<double>& dashes_px, double offset_px) {
  std::vector<StrokeRun> runs;
  if (points.size() < 2) {
    return runs;
  }
  double pattern_total = 0.0;
  for (const auto dash : dashes_px) {
    pattern_total += std::max(dash, 0.0);
  }
  if (dashes_px.empty() || pattern_total <= 0.0) {
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
void append_outline_loop(const std::vector<DPoint>& loop, std::int32_t origin_x, std::int32_t origin_y,
                         std::vector<Edge>& edges) {
  if (loop.size() < 3) {
    return;
  }
  const std::int32_t shift_x = origin_x * kSub;
  const std::int32_t shift_y = origin_y * kSub;
  FixedPoint previous{to_fixed(loop[0].x) - shift_x, to_fixed(loop[0].y) - shift_y};
  const FixedPoint first = previous;
  for (std::size_t i = 1; i < loop.size(); ++i) {
    const FixedPoint point{to_fixed(loop[i].x) - shift_x, to_fixed(loop[i].y) - shift_y};
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

  // Bounds: path hull expanded by the band's reach (half width; miter/square
  // caps reach at most half * max(miter_limit-capped ratio, sqrt(2)) - use a
  // conservative 2x half + 2px guard).
  const auto hull = path.bounds();
  if (!hull.has_value()) {
    return CoverageBuffer{};
  }
  const double reach = half * 2.0 + 2.0;
  Rect band_bounds{static_cast<std::int32_t>(std::floor(hull->left - reach)),
                   static_cast<std::int32_t>(std::floor(hull->top - reach)), 0, 0};
  band_bounds.width = static_cast<std::int32_t>(std::ceil(hull->right + reach)) - band_bounds.x + 1;
  band_bounds.height = static_cast<std::int32_t>(std::ceil(hull->bottom + reach)) - band_bounds.y + 1;
  band_bounds = intersect_rects(band_bounds, options.clip);
  if (band_bounds.empty()) {
    return CoverageBuffer{};
  }

  // Resolve dash entries (stroke-width multiples) to pixels.
  std::vector<double> dashes_px;
  dashes_px.reserve(stroke.dashes.size());
  for (const auto dash : stroke.dashes) {
    dashes_px.push_back(dash * stroke.width);
  }
  const double offset_px = stroke.dash_offset * stroke.width;

  std::vector<Edge> edges;
  for (const auto& subpath : path.subpaths) {
    const auto polyline = subpath_polyline(subpath);
    if (polyline.size() < 2) {
      continue;
    }
    const auto runs = apply_dashes(polyline, subpath.closed, dashes_px, offset_px);
    for (const auto& run : runs) {
      append_run_outline(run, half, stroke.cap, stroke.join, stroke.miter_limit, band_bounds.x,
                         band_bounds.y, edges);
    }
  }
  if (edges.empty()) {
    return CoverageBuffer{};
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

void update_vector_shape_raster(Layer& layer, Rect canvas, const PatternStore* patterns) {
  const auto* shape = layer.vector_shape();
  if (shape == nullptr) {
    return;
  }
  auto raster = rasterize_vector_shape(*shape, canvas, patterns, &layer);
  layer.set_pixels(std::move(raster.pixels));
  layer.set_bounds(raster.bounds);
  layer.metadata()[kLayerMetadataVectorRasterStatus] = kVectorRasterStatusPatchy;
}

void update_vector_mask_raster(Layer& layer, Rect canvas) {
  const auto* mask = layer.vector_mask();
  if (mask == nullptr) {
    return;
  }
  auto updated = *mask;
  auto coverage = rasterize_vector_mask_coverage(updated, canvas);
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
                           const PatternStore* patterns, const Layer* layer_for_pattern_anchor) {
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
    const Rect gradient_bounds = fill.gradient.align_with_layer ? coverage.bounds : canvas;
    for (std::int32_t y = 0; y < coverage.bounds.height; ++y) {
      auto* row = out + static_cast<std::size_t>(y) * out_stride;
      const auto* cov_row = cov + static_cast<std::size_t>(y) * cov_stride;
      const auto document_y = coverage.bounds.y + y;
      for (std::int32_t x = 0; x < coverage.bounds.width; ++x) {
        const auto document_x = coverage.bounds.x + x;
        const auto position = gradient_position(fill.gradient, gradient_bounds, document_x, document_y);
        const auto color = gradient_color_dithered(fill.gradient, position, document_x, document_y);
        const auto opacity = gradient_stop_opacity(fill.gradient, position);
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
                                         const Layer* layer_for_pattern_anchor) {
  ShapeRasterResult result;
  VectorRasterOptions options;
  options.clip = canvas;

  const bool fill_on = content.stroke.fill_enabled && content.fill.kind != VectorFillKind::None;
  CoverageBuffer fill_coverage;
  if (fill_on) {
    fill_coverage = rasterize_vector_path(content.path, options);
  }
  const bool stroke_on =
      content.stroke.enabled && content.stroke.width > 0.0 && content.stroke.opacity > 0.0 &&
      content.stroke.content.kind != VectorFillKind::None && !content.path.empty();
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

  if (!fill_coverage.bounds.empty()) {
    const auto fill_pixels =
        paint_coverage(fill_coverage, content.fill, canvas, patterns, layer_for_pattern_anchor);
    const auto* src = fill_pixels.data().data();
    const auto src_stride = fill_pixels.stride_bytes();
    for (std::int32_t y = 0; y < fill_coverage.bounds.height; ++y) {
      auto* row = out + static_cast<std::size_t>(fill_coverage.bounds.y - bounds.y + y) * out_stride +
                  static_cast<std::size_t>(fill_coverage.bounds.x - bounds.x) * 4;
      std::memcpy(row, src + static_cast<std::size_t>(y) * src_stride,
                  static_cast<std::size_t>(fill_coverage.bounds.width) * 4);
    }
  }

  if (!stroke_coverage.bounds.empty()) {
    // The stroke paints over the fill within the same raster. Blend mode and
    // opacity apply against the fill where it exists; Photoshop composites
    // live shape strokes against the full backdrop, so non-Normal stroke
    // modes against content BELOW the layer are approximated by this baked
    // result.
    const auto stroke_pixels =
        paint_coverage(stroke_coverage, content.stroke.content, canvas, patterns, layer_for_pattern_anchor);
    const auto* src = stroke_pixels.data().data();
    const auto src_stride = stroke_pixels.stride_bytes();
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
  return result;
}

}  // namespace patchy
