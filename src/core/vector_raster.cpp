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

// Exact-area cell rasterizer over one buffer-relative edge list. Emits gray8
// even-odd coverage into `out` (width x height, buffer-relative).
void rasterize_edges_even_odd(const std::vector<Edge>& edges, std::int32_t width, std::int32_t height,
                              PixelBuffer& out) {
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

    // Sweep the row: signed winding coverage, folded even-odd.
    auto* row_bytes = bytes + static_cast<std::size_t>(row) * stride;
    std::int64_t running_cover = 0;
    for (std::int32_t x = 0; x < width; ++x) {
      const auto& cell = cells[static_cast<std::size_t>(x)];
      const std::int64_t twice_area = running_cover * (2 * kSub) + cell.area;
      running_cover += cell.cover;
      std::int64_t c = divide_rounded(twice_area, 2 * kSub);
      c = ((c % (2 * kSub)) + 2 * kSub) % (2 * kSub);
      if (c > kSub) {
        c = 2 * kSub - c;
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
      rasterize_edges_even_odd(edges, group_bounds.width, group_bounds.height, group_coverage.pixels);
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

ShapeRasterResult rasterize_vector_shape(const VectorShapeContent& content, Rect canvas,
                                         const PatternStore* patterns,
                                         const Layer* layer_for_pattern_anchor) {
  ShapeRasterResult result;
  const bool fill_on = content.stroke.fill_enabled && content.fill.kind != VectorFillKind::None;
  if (!fill_on) {
    return result;
  }
  VectorRasterOptions options;
  options.clip = canvas;
  const auto coverage = rasterize_vector_path(content.path, options);
  if (coverage.bounds.empty()) {
    return result;
  }

  result.bounds = coverage.bounds;
  result.pixels = PixelBuffer(coverage.bounds.width, coverage.bounds.height, PixelFormat::rgba8());
  auto* out = result.pixels.data().data();
  const auto out_stride = result.pixels.stride_bytes();
  const auto* cov = coverage.pixels.data().data();
  const auto cov_stride = coverage.pixels.stride_bytes();

  if (content.fill.kind == VectorFillKind::Solid) {
    const auto color = content.fill.color;
    for (std::int32_t y = 0; y < coverage.bounds.height; ++y) {
      auto* row = out + static_cast<std::size_t>(y) * out_stride;
      const auto* cov_row = cov + static_cast<std::size_t>(y) * cov_stride;
      for (std::int32_t x = 0; x < coverage.bounds.width; ++x) {
        row[x * 4 + 0] = color.red;
        row[x * 4 + 1] = color.green;
        row[x * 4 + 2] = color.blue;
        row[x * 4 + 3] = cov_row[x];
      }
    }
    return result;
  }

  if (content.fill.kind == VectorFillKind::Gradient) {
    // align_with_layer follows the shape's own coverage bounds (the layer's
    // transparency bounds once baked); otherwise the canvas.
    const Rect gradient_bounds = content.fill.gradient.align_with_layer ? coverage.bounds : canvas;
    for (std::int32_t y = 0; y < coverage.bounds.height; ++y) {
      auto* row = out + static_cast<std::size_t>(y) * out_stride;
      const auto* cov_row = cov + static_cast<std::size_t>(y) * cov_stride;
      const auto document_y = coverage.bounds.y + y;
      for (std::int32_t x = 0; x < coverage.bounds.width; ++x) {
        const auto document_x = coverage.bounds.x + x;
        const auto position =
            gradient_position(content.fill.gradient, gradient_bounds, document_x, document_y);
        const auto color = gradient_color_dithered(content.fill.gradient, position, document_x, document_y);
        const auto opacity = gradient_stop_opacity(content.fill.gradient, position);
        row[x * 4 + 0] = color.red;
        row[x * 4 + 1] = color.green;
        row[x * 4 + 2] = color.blue;
        row[x * 4 + 3] = static_cast<std::uint8_t>(
            std::clamp<long>(std::lround(static_cast<double>(cov_row[x]) * opacity), 0L, 255L));
      }
    }
    return result;
  }

  // Pattern fill.
  const PatternResource* resource =
      patterns != nullptr ? patterns->find(content.fill.pattern_id) : nullptr;
  if (resource == nullptr || resource->tile.width() <= 0 || resource->tile.height() <= 0) {
    // Missing tiles render transparent (the layer keeps its coverage bounds).
    for (std::int32_t y = 0; y < coverage.bounds.height; ++y) {
      auto* row = out + static_cast<std::size_t>(y) * out_stride;
      std::memset(row, 0, static_cast<std::size_t>(coverage.bounds.width) * 4);
    }
    return result;
  }
  const Layer anchor_fallback;
  const Layer& anchor_layer = layer_for_pattern_anchor != nullptr ? *layer_for_pattern_anchor : anchor_fallback;
  const PatternTileSampler sampler(resource->tile, anchor_layer,
                                   static_cast<float>(content.fill.pattern_scale),
                                   static_cast<float>(content.fill.pattern_angle_degrees),
                                   content.fill.pattern_linked,
                                   static_cast<float>(content.fill.pattern_phase_x),
                                   static_cast<float>(content.fill.pattern_phase_y));
  for (std::int32_t y = 0; y < coverage.bounds.height; ++y) {
    auto* row = out + static_cast<std::size_t>(y) * out_stride;
    const auto* cov_row = cov + static_cast<std::size_t>(y) * cov_stride;
    const auto document_y = coverage.bounds.y + y;
    for (std::int32_t x = 0; x < coverage.bounds.width; ++x) {
      const auto sample = sampler.sample(coverage.bounds.x + x, document_y);
      row[x * 4 + 0] = sample.color.red;
      row[x * 4 + 1] = sample.color.green;
      row[x * 4 + 2] = sample.color.blue;
      row[x * 4 + 3] = static_cast<std::uint8_t>(std::clamp<long>(
          std::lround(static_cast<double>(cov_row[x]) * sample.alpha), 0L, 255L));
    }
  }
  return result;
}

}  // namespace patchy
