#include "core/image_trace.hpp"

#include "core/document.hpp"
#include "core/mask_outline.hpp"
#include "core/palette.hpp"
#include "core/path_fit.hpp"
#include "core/vector_raster.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <cstdio>
#include <limits>
#include <utility>

namespace patchy {

namespace {

constexpr std::int32_t kUntraced = -1;
constexpr std::uint8_t kAlphaThreshold = 128;
// A palette entry with every channel at or above this is "white" for Ignore
// White (median cut averages near-white backgrounds to slightly below 255).
constexpr std::uint8_t kWhiteFloor = 240;

[[nodiscard]] std::uint8_t luminance(std::uint8_t r, std::uint8_t g, std::uint8_t b) noexcept {
  return static_cast<std::uint8_t>((r * 299 + g * 587 + b * 114 + 500) / 1000);
}

[[nodiscard]] bool is_white(RgbColor color) noexcept {
  return color.red >= kWhiteFloor && color.green >= kWhiteFloor && color.blue >= kWhiteFloor;
}

struct SourcePixel {
  std::uint8_t r{0};
  std::uint8_t g{0};
  std::uint8_t b{0};
  std::uint8_t a{255};
};

// Reads one pixel of an 8-bit gray/RGB/RGBA buffer as RGBA.
[[nodiscard]] SourcePixel read_pixel(const PixelBuffer& pixels, std::int32_t x, std::int32_t y) noexcept {
  const auto* px = pixels.pixel(x, y);
  const auto channels = pixels.format().channels;
  if (channels == 1) {
    return {px[0], px[0], px[0], 255};
  }
  if (channels == 2) {
    return {px[0], px[0], px[0], px[1]};
  }
  return {px[0], px[1], px[2], channels >= 4 ? px[3] : std::uint8_t{255}};
}

[[nodiscard]] bool format_supported(const PixelBuffer& pixels) noexcept {
  return !pixels.empty() && pixels.format().bit_depth == BitDepth::UInt8 && pixels.format().channels >= 1 &&
         pixels.format().channels <= 4;
}

struct LabelMap {
  std::int32_t width{0};
  std::int32_t height{0};
  std::vector<std::int32_t> labels;  // kUntraced or palette index
  std::vector<RgbColor> palette;

  [[nodiscard]] std::size_t index(std::int32_t x, std::int32_t y) const noexcept {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
  }
};

std::vector<PaletteColorCount> histogram_counts(const std::array<std::uint64_t, 256>& histogram) {
  std::vector<PaletteColorCount> counts;
  for (int value = 0; value < 256; ++value) {
    const auto population = histogram[static_cast<std::size_t>(value)];
    if (population != 0) {
      const auto gray = static_cast<std::uint8_t>(value);
      counts.push_back({RgbColor{gray, gray, gray}, population});
    }
  }
  return counts;
}

// --- quantization -----------------------------------------------------------

LabelMap build_label_map(const PixelBuffer& pixels, const ImageTraceOptions& options) {
  LabelMap map;
  map.width = pixels.width();
  map.height = pixels.height();
  const auto count = static_cast<std::size_t>(map.width) * static_cast<std::size_t>(map.height);
  map.labels.assign(count, kUntraced);

  const int color_count =
      std::clamp(options.colors, ImageTraceOptions::kMinColors, ImageTraceOptions::kMaxColors);
  switch (options.mode) {
    case ImageTraceOptions::Mode::BlackAndWhite: {
      map.palette = {RgbColor{0, 0, 0}, RgbColor{255, 255, 255}};
      const int threshold = std::clamp(options.threshold, 1, 255);
      for (std::int32_t y = 0; y < map.height; ++y) {
        for (std::int32_t x = 0; x < map.width; ++x) {
          const auto px = read_pixel(pixels, x, y);
          if (px.a < kAlphaThreshold) {
            continue;
          }
          map.labels[map.index(x, y)] = luminance(px.r, px.g, px.b) < threshold ? 0 : 1;
        }
      }
      break;
    }
    case ImageTraceOptions::Mode::Grayscale: {
      std::array<std::uint64_t, 256> histogram{};
      std::vector<std::uint8_t> grays(count, 0);
      for (std::int32_t y = 0; y < map.height; ++y) {
        for (std::int32_t x = 0; x < map.width; ++x) {
          const auto px = read_pixel(pixels, x, y);
          if (px.a < kAlphaThreshold) {
            continue;
          }
          const auto gray = luminance(px.r, px.g, px.b);
          grays[map.index(x, y)] = gray;
          map.labels[map.index(x, y)] = 0;  // marks "visible"; resolved below
          ++histogram[gray];
        }
      }
      map.palette = median_cut_palette(histogram_counts(histogram), static_cast<std::size_t>(color_count));
      if (map.palette.empty()) {
        break;
      }
      // Nearest gray per value; the lowest index wins ties.
      std::array<std::int32_t, 256> nearest{};
      for (int value = 0; value < 256; ++value) {
        std::int32_t best = 0;
        int best_distance = std::numeric_limits<int>::max();
        for (std::size_t i = 0; i < map.palette.size(); ++i) {
          const int distance = std::abs(value - static_cast<int>(map.palette[i].red));
          if (distance < best_distance) {
            best_distance = distance;
            best = static_cast<std::int32_t>(i);
          }
        }
        nearest[static_cast<std::size_t>(value)] = best;
      }
      for (std::size_t i = 0; i < count; ++i) {
        if (map.labels[i] != kUntraced) {
          map.labels[i] = nearest[grays[i]];
        }
      }
      break;
    }
    case ImageTraceOptions::Mode::Color: {
      // collect_color_counts counts alpha >= threshold when the buffer has
      // alpha; gray buffers are expanded through read_pixel instead.
      std::vector<PaletteColorCount> counts;
      if (pixels.format().channels >= 3) {
        counts = collect_color_counts(pixels, kAlphaThreshold);
      } else {
        std::array<std::uint64_t, 256> histogram{};
        for (std::int32_t y = 0; y < map.height; ++y) {
          for (std::int32_t x = 0; x < map.width; ++x) {
            const auto px = read_pixel(pixels, x, y);
            if (px.a >= kAlphaThreshold) {
              ++histogram[px.r];
            }
          }
        }
        counts = histogram_counts(histogram);
      }
      map.palette = median_cut_palette(counts, static_cast<std::size_t>(color_count));
      if (map.palette.empty()) {
        break;
      }
      PaletteLut lut;
      lut.build(map.palette);
      for (std::int32_t y = 0; y < map.height; ++y) {
        for (std::int32_t x = 0; x < map.width; ++x) {
          const auto px = read_pixel(pixels, x, y);
          if (px.a < kAlphaThreshold) {
            continue;
          }
          map.labels[map.index(x, y)] = static_cast<std::int32_t>(lut.index_for(px.r, px.g, px.b));
        }
      }
      break;
    }
  }

  if (options.ignore_white) {
    std::vector<bool> white(map.palette.size(), false);
    bool any = false;
    for (std::size_t i = 0; i < map.palette.size(); ++i) {
      white[i] = is_white(map.palette[i]);
      any = any || white[i];
    }
    if (any) {
      for (auto& label : map.labels) {
        if (label != kUntraced && white[static_cast<std::size_t>(label)]) {
          label = kUntraced;
        }
      }
    }
  }
  return map;
}

// --- connected components -----------------------------------------------------

struct Component {
  std::int32_t label{kUntraced};
  std::int64_t area{0};
};

struct ComponentMap {
  std::vector<std::int32_t> ids;  // per pixel, -1 for untraced
  std::vector<Component> components;
};

// 4-connected components of equal label, numbered in scan order of their
// first pixel.
ComponentMap label_components(const LabelMap& map) {
  ComponentMap result;
  const auto width = map.width;
  const auto height = map.height;
  result.ids.assign(map.labels.size(), -1);
  std::vector<std::size_t> stack;
  for (std::size_t seed = 0; seed < map.labels.size(); ++seed) {
    if (map.labels[seed] == kUntraced || result.ids[seed] != -1) {
      continue;
    }
    const auto id = static_cast<std::int32_t>(result.components.size());
    const auto label = map.labels[seed];
    Component component;
    component.label = label;
    result.ids[seed] = id;
    stack.clear();
    stack.push_back(seed);
    while (!stack.empty()) {
      const auto index = stack.back();
      stack.pop_back();
      ++component.area;
      const auto x = static_cast<std::int32_t>(index % static_cast<std::size_t>(width));
      const auto y = static_cast<std::int32_t>(index / static_cast<std::size_t>(width));
      const auto visit = [&](std::int32_t nx, std::int32_t ny) {
        if (nx < 0 || ny < 0 || nx >= width || ny >= height) {
          return;
        }
        const auto neighbor = map.index(nx, ny);
        if (result.ids[neighbor] == -1 && map.labels[neighbor] == label) {
          result.ids[neighbor] = id;
          stack.push_back(neighbor);
        }
      };
      visit(x + 1, y);
      visit(x - 1, y);
      visit(x, y + 1);
      visit(x, y - 1);
    }
    result.components.push_back(component);
  }
  return result;
}

// Merges every component smaller than `minimum_area` into the neighboring
// label it shares the longest border with (untraced counts as a neighbor, so
// dust inside transparency disappears). Repeats until stable or the pass cap.
void remove_speckles(LabelMap& map, std::int64_t minimum_area) {
  if (minimum_area <= 1) {
    return;
  }
  constexpr int kMaxPasses = 8;
  const auto width = map.width;
  const auto height = map.height;
  for (int pass = 0; pass < kMaxPasses; ++pass) {
    auto components = label_components(map);
    std::vector<std::int32_t> small_index(components.components.size(), -1);
    std::int32_t small_count = 0;
    for (std::size_t i = 0; i < components.components.size(); ++i) {
      if (components.components[i].area < minimum_area) {
        small_index[i] = small_count++;
      }
    }
    if (small_count == 0) {
      return;
    }
    // Border length per (small component, neighbor label), neighbors kept in
    // first-seen order so ties resolve deterministically.
    struct Neighbor {
      std::int32_t label{kUntraced};
      std::int64_t border{0};
    };
    std::vector<std::vector<Neighbor>> neighbors(static_cast<std::size_t>(small_count));
    const auto note = [&](std::int32_t small, std::int32_t neighbor_label) {
      auto& list = neighbors[static_cast<std::size_t>(small)];
      for (auto& entry : list) {
        if (entry.label == neighbor_label) {
          ++entry.border;
          return;
        }
      }
      list.push_back({neighbor_label, 1});
    };
    for (std::int32_t y = 0; y < height; ++y) {
      for (std::int32_t x = 0; x < width; ++x) {
        const auto index = map.index(x, y);
        const auto id = components.ids[index];
        if (id < 0 || small_index[static_cast<std::size_t>(id)] < 0) {
          continue;
        }
        const auto small = small_index[static_cast<std::size_t>(id)];
        const auto consider = [&](std::int32_t nx, std::int32_t ny) {
          if (nx < 0 || ny < 0 || nx >= width || ny >= height) {
            return;
          }
          const auto neighbor = map.index(nx, ny);
          if (components.ids[neighbor] != id) {
            note(small, map.labels[neighbor]);
          }
        };
        consider(x - 1, y);
        consider(x + 1, y);
        consider(x, y - 1);
        consider(x, y + 1);
      }
    }
    std::vector<std::int32_t> replacement(static_cast<std::size_t>(small_count), kUntraced);
    bool changed = false;
    for (std::size_t i = 0; i < components.components.size(); ++i) {
      const auto small = small_index[i];
      if (small < 0) {
        continue;
      }
      const auto& list = neighbors[static_cast<std::size_t>(small)];
      if (list.empty()) {  // the whole image is one small region
        replacement[static_cast<std::size_t>(small)] = components.components[i].label;
        continue;
      }
      const Neighbor* best = &list.front();
      for (const auto& entry : list) {
        if (entry.border > best->border) {
          best = &entry;
        }
      }
      replacement[static_cast<std::size_t>(small)] = best->label;
      changed = changed || best->label != components.components[i].label;
    }
    if (!changed) {
      return;
    }
    for (std::size_t index = 0; index < map.labels.size(); ++index) {
      const auto id = components.ids[index];
      if (id < 0) {
        continue;
      }
      const auto small = small_index[static_cast<std::size_t>(id)];
      if (small >= 0) {
        map.labels[index] = replacement[static_cast<std::size_t>(small)];
      }
    }
  }
}

// --- contours -----------------------------------------------------------------

struct TracedLoop {
  std::vector<FitPoint> points;  // image coordinates
  Rect bounds{};
  std::int32_t component{-1};  // owning component; -1 once dropped
  bool hole{false};
  // Hole loops: the first pixel inside the hole (image coordinates).
  std::int32_t seed_x{0};
  std::int32_t seed_y{0};
};

// Traces every component of `label` (all at once, within the label's bounds)
// and attributes each loop to its component through the canonical start
// vertex: an outer loop starts at the top-left corner of its topmost-leftmost
// pixel; a hole loop starts at the top-left corner of its topmost-leftmost
// INSIDE pixel, whose upper neighbor belongs to the owner.
struct LabelBounds {
  std::int32_t min_x{0};
  std::int32_t min_y{0};
  std::int32_t max_x{-1};
  std::int32_t max_y{-1};
};

std::vector<LabelBounds> label_bounds(const LabelMap& map) {
  std::vector<LabelBounds> bounds(map.palette.size());
  for (auto& entry : bounds) {
    entry.min_x = map.width;
    entry.min_y = map.height;
  }
  for (std::int32_t y = 0; y < map.height; ++y) {
    for (std::int32_t x = 0; x < map.width; ++x) {
      const auto label = map.labels[map.index(x, y)];
      if (label == kUntraced) {
        continue;
      }
      auto& entry = bounds[static_cast<std::size_t>(label)];
      entry.min_x = std::min(entry.min_x, x);
      entry.min_y = std::min(entry.min_y, y);
      entry.max_x = std::max(entry.max_x, x);
      entry.max_y = std::max(entry.max_y, y);
    }
  }
  return bounds;
}

std::vector<TracedLoop> trace_label(const LabelMap& map, const ComponentMap& components, std::int32_t label,
                                    const LabelBounds& bounds) {
  std::vector<TracedLoop> loops;
  if (bounds.max_x < 0) {
    return loops;
  }
  const auto min_x = bounds.min_x;
  const auto min_y = bounds.min_y;
  const auto local_width = bounds.max_x - min_x + 1;
  const auto local_height = bounds.max_y - min_y + 1;
  const auto stride = static_cast<std::size_t>(local_width) + 2;
  std::vector<std::uint8_t> mask(stride * (static_cast<std::size_t>(local_height) + 2), std::uint8_t{0});
  for (std::int32_t y = 0; y < local_height; ++y) {
    auto* row = mask.data() + static_cast<std::size_t>(y + 1) * stride + 1;
    for (std::int32_t x = 0; x < local_width; ++x) {
      row[x] = map.labels[map.index(x + min_x, y + min_y)] == label ? 1U : 0U;
    }
  }
  for (auto& traced : trace_mask_outlines(mask.data(), local_width, local_height, stride)) {
    TracedLoop loop;
    loop.points = std::move(traced.points);
    for (auto& point : loop.points) {
      point.x += min_x;
      point.y += min_y;
    }
    loop.bounds =
        Rect{traced.bounds.x + min_x, traced.bounds.y + min_y, traced.bounds.width, traced.bounds.height};
    loop.hole = loop_signed_area(loop.points) < 0.0;
    const auto start_x = static_cast<std::int32_t>(loop.points.front().x);
    const auto start_y = static_cast<std::int32_t>(loop.points.front().y);
    if (loop.hole) {
      loop.seed_x = start_x;
      loop.seed_y = start_y;
      loop.component = components.ids[map.index(start_x, start_y - 1)];
    } else {
      loop.component = components.ids[map.index(start_x, start_y)];
    }
    loops.push_back(std::move(loop));
  }
  return loops;
}

// Overlapping mode: which component encloses which. For every hole, an
// 8-connected flood over the non-owner pixels it encloses finds the
// components inside; each component's parent is the owner of the SMALLEST
// hole that contains it. Holes enclosing no component keep their hole status
// (a ring around transparency stays a ring).
struct Nesting {
  std::vector<std::int32_t> parent;          // per component, -1 for roots
  std::vector<std::int64_t> enclosing_area;  // per component, pixel area of its parent hole
  std::vector<int> depth;                    // per component
};

Nesting compute_nesting(const LabelMap& map, const ComponentMap& components, std::vector<TracedLoop>& loops) {
  Nesting nesting;
  const auto component_count = components.components.size();
  nesting.parent.assign(component_count, -1);
  nesting.enclosing_area.assign(component_count, std::numeric_limits<std::int64_t>::max());
  nesting.depth.assign(component_count, 0);
  const auto width = map.width;
  const auto height = map.height;
  std::vector<std::int32_t> visit_stamp(map.labels.size(), 0);
  std::vector<std::int32_t> seen_stamp(component_count, 0);
  std::vector<std::size_t> stack;
  std::vector<std::int32_t> found;
  std::int32_t stamp = 0;
  for (auto& loop : loops) {
    if (!loop.hole) {
      continue;
    }
    ++stamp;
    found.clear();
    const auto owner = loop.component;
    const auto left = loop.bounds.x;
    const auto top = loop.bounds.y;
    const auto right = loop.bounds.x + loop.bounds.width;    // exclusive
    const auto bottom = loop.bounds.y + loop.bounds.height;  // exclusive
    std::int64_t area = 0;
    const auto seed = map.index(loop.seed_x, loop.seed_y);
    visit_stamp[seed] = stamp;
    stack.clear();
    stack.push_back(seed);
    while (!stack.empty()) {
      const auto index = stack.back();
      stack.pop_back();
      ++area;
      const auto id = components.ids[index];
      if (id >= 0 && seen_stamp[static_cast<std::size_t>(id)] != stamp) {
        seen_stamp[static_cast<std::size_t>(id)] = stamp;
        found.push_back(id);
      }
      const auto x = static_cast<std::int32_t>(index % static_cast<std::size_t>(width));
      const auto y = static_cast<std::int32_t>(index / static_cast<std::size_t>(width));
      for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
          if (dx == 0 && dy == 0) {
            continue;
          }
          const auto nx = x + dx;
          const auto ny = y + dy;
          if (nx < left || ny < top || nx >= right || ny >= bottom || nx < 0 || ny < 0 || nx >= width ||
              ny >= height) {
            continue;
          }
          const auto neighbor = map.index(nx, ny);
          if (visit_stamp[neighbor] == stamp || components.ids[neighbor] == owner) {
            continue;
          }
          visit_stamp[neighbor] = stamp;
          stack.push_back(neighbor);
        }
      }
    }
    for (const auto id : found) {
      auto& best = nesting.enclosing_area[static_cast<std::size_t>(id)];
      if (area < best) {
        best = area;
        nesting.parent[static_cast<std::size_t>(id)] = owner;
      }
    }
    if (!found.empty()) {
      loop.component = -1;  // painted over; the children stack on top
    }
  }
  // A parent's own enclosing hole strictly contains the hole its child sits
  // in, so descending enclosing area resolves every parent before its
  // children.
  std::vector<std::int32_t> order(component_count);
  for (std::size_t i = 0; i < component_count; ++i) {
    order[i] = static_cast<std::int32_t>(i);
  }
  std::stable_sort(order.begin(), order.end(), [&](std::int32_t a, std::int32_t b) {
    return nesting.enclosing_area[static_cast<std::size_t>(a)] >
           nesting.enclosing_area[static_cast<std::size_t>(b)];
  });
  for (const auto id : order) {
    const auto parent = nesting.parent[static_cast<std::size_t>(id)];
    nesting.depth[static_cast<std::size_t>(id)] =
        parent < 0 ? 0 : nesting.depth[static_cast<std::size_t>(parent)] + 1;
  }
  return nesting;
}

// Settles a pixel-corner staircase before fitting. A traced contour walks
// pixel edges, so a diagonal or a curve is a run of short risers and treads
// whose corner vertices sit up to half a step off the edge they depict;
// Douglas-Peucker then picks those corners as chord endpoints and sees double
// the deviation, which keeps stair steps as anchors at tolerances near the
// step size. Every vertex between two stair edges moves to the average of
// their midpoints (the line a smooth edge would follow, exactly so for
// uniform steps); a vertex with one stair edge moves to that edge's midpoint.
// A stair edge is an edge no longer than `max_edge` whose two ends turn in
// opposite directions (a step). Vertices between longer edges (real corners)
// stay exactly in place, and a short edge with the same turn at both ends is
// the cap of a thin feature (a 1 px line, a single pixel) and is left alone
// so the feature survives.
std::vector<FitPoint> settle_staircase(const std::vector<FitPoint>& points, double max_edge) {
  const auto count = points.size();
  if (count < 4) {
    return points;
  }
  const auto at = [&](std::size_t i) -> const FitPoint& { return points[i % count]; };
  // Turn sign at each vertex: cross(incoming, outgoing).
  std::vector<double> turn(count, 0.0);
  for (std::size_t i = 0; i < count; ++i) {
    const auto& previous = at(i + count - 1);
    const auto& current = at(i);
    const auto& next = at(i + 1);
    turn[i] = (current.x - previous.x) * (next.y - current.y) - (current.y - previous.y) * (next.x - current.x);
  }
  // stair[i] marks the edge from vertex i to vertex i + 1.
  std::vector<bool> stair(count, false);
  for (std::size_t i = 0; i < count; ++i) {
    const auto& a = at(i);
    const auto& b = at(i + 1);
    const auto dx = b.x - a.x;
    const auto dy = b.y - a.y;
    const auto edge = std::sqrt(dx * dx + dy * dy);
    stair[i] = edge > 1e-9 && edge <= max_edge && turn[i] * turn[(i + 1) % count] < 0.0;
  }
  std::vector<FitPoint> settled;
  settled.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    const auto& current = at(i);
    double move_x = 0.0;
    double move_y = 0.0;
    int stairs = 0;
    const auto lean_toward = [&](const FitPoint& neighbor) {
      move_x += (neighbor.x - current.x) * 0.5;
      move_y += (neighbor.y - current.y) * 0.5;
      ++stairs;
    };
    if (stair[(i + count - 1) % count]) {
      lean_toward(at(i + count - 1));
    }
    if (stair[i]) {
      lean_toward(at(i + 1));
    }
    FitPoint moved = current;
    if (stairs > 0) {
      moved.x += move_x / stairs;
      moved.y += move_y / stairs;
    }
    if (!settled.empty() && settled.back() == moved) {
      continue;  // two stair vertices that met at one edge midpoint
    }
    settled.push_back(moved);
  }
  if (settled.size() > 1 && settled.front() == settled.back()) {
    settled.pop_back();
  }
  return settled;
}

[[nodiscard]] bool subpath_is_visible(const PathSubpath& subpath) noexcept {
  if (subpath.anchors.size() >= 3) {
    return true;
  }
  if (subpath.anchors.size() < 2) {
    return false;
  }
  for (const auto& anchor : subpath.anchors) {
    if (anchor.in_x != anchor.anchor_x || anchor.in_y != anchor.anchor_y || anchor.out_x != anchor.anchor_x ||
        anchor.out_y != anchor.anchor_y) {
      return true;
    }
  }
  return false;
}

}  // namespace

double image_trace_fit_tolerance(int paths) noexcept {
  const double t = std::clamp(paths, 0, 100) / 100.0;
  return 4.0 * std::pow(0.0625, t);
}

double image_trace_corner_angle(int corners) noexcept {
  return 120.0 - 0.9 * std::clamp(corners, 0, 100);
}

ImageTraceResult trace_image(const PixelBuffer& pixels, const ImageTraceOptions& options,
                             const std::function<bool()>& cancelled) {
  ImageTraceResult result;
  if (!format_supported(pixels)) {
    return result;
  }
  const auto is_cancelled = [&cancelled]() { return cancelled && cancelled(); };

  auto map = build_label_map(pixels, options);
  result.palette_size = map.palette.size();
  if (map.palette.empty() || is_cancelled()) {
    return result;
  }
  remove_speckles(map, std::clamp(options.noise, 1, 100));
  if (is_cancelled()) {
    return result;
  }
  const auto components = label_components(map);
  if (components.components.empty()) {
    return result;
  }

  const bool overlapping = options.method == ImageTraceOptions::Method::Overlapping;
  std::vector<TracedLoop> loops;
  const auto bounds = label_bounds(map);
  for (std::int32_t label = 0; label < static_cast<std::int32_t>(map.palette.size()); ++label) {
    if (is_cancelled()) {
      return {};
    }
    auto traced = trace_label(map, components, label, bounds[static_cast<std::size_t>(label)]);
    loops.insert(loops.end(), std::make_move_iterator(traced.begin()), std::make_move_iterator(traced.end()));
  }
  Nesting nesting;
  if (overlapping) {
    nesting = compute_nesting(map, components, loops);
  } else {
    nesting.depth.assign(components.components.size(), 0);
  }
  if (is_cancelled()) {
    return {};
  }

  // One layer per (depth, label); loops stay in trace order (row-major per
  // label), which puts every hole after its outer loop and every island after
  // the hole it sits in, the order the sequential combine needs.
  struct LayerKey {
    int depth;
    std::int32_t label;
  };
  std::vector<std::pair<LayerKey, std::size_t>> layer_index;
  const auto layer_for = [&](int depth, std::int32_t label) -> ImageTraceLayer& {
    for (const auto& [existing, index] : layer_index) {
      if (existing.depth == depth && existing.label == label) {
        return result.layers[index];
      }
    }
    ImageTraceLayer layer;
    layer.color = map.palette[static_cast<std::size_t>(label)];
    layer.depth = depth;
    result.layers.push_back(std::move(layer));
    layer_index.emplace_back(LayerKey{depth, label}, result.layers.size() - 1);
    return result.layers.back();
  };
  std::vector<bool> area_counted(components.components.size(), false);

  PathFitOptions fit;
  fit.tolerance = image_trace_fit_tolerance(options.paths);
  fit.corner_angle_degrees = image_trace_corner_angle(options.corners);
  fit.smooth_corner_tangents = true;
  fit.snap_curves_to_lines = options.snap_curves_to_lines;
  // Stair steps up to three tolerances long settle (1 px risers always do),
  // so a tight Paths setting stays pixel-faithful while the default smooths.
  const double stair_edge = std::clamp(3.0 * fit.tolerance, 1.5, 6.0);
  std::size_t since_cancel_check = 0;
  for (const auto& loop : loops) {
    if (loop.component < 0) {
      continue;  // a painted-over hole (Overlapping)
    }
    if (++since_cancel_check % 64 == 0 && is_cancelled()) {
      return {};
    }
    const auto& component = components.components[static_cast<std::size_t>(loop.component)];
    auto& layer = layer_for(nesting.depth[static_cast<std::size_t>(loop.component)], component.label);
    if (!area_counted[static_cast<std::size_t>(loop.component)]) {
      area_counted[static_cast<std::size_t>(loop.component)] = true;
      layer.area += component.area;
    }
    auto subpath = fit_closed_loop(settle_staircase(loop.points, stair_edge), fit);
    if (!subpath_is_visible(subpath)) {
      continue;
    }
    subpath.op = loop.hole ? PathCombineOp::Subtract : PathCombineOp::Add;
    subpath.shape_group = layer.path.next_shape_group();
    result.anchor_count += subpath.anchors.size();
    layer.path.subpaths.push_back(std::move(subpath));
  }

  // Drop layers whose every loop vanished; order back to front: shallow
  // nesting first, then the largest regions, then palette order.
  std::erase_if(result.layers, [](const ImageTraceLayer& layer) { return layer.path.subpaths.empty(); });
  std::vector<std::size_t> order(result.layers.size());
  for (std::size_t i = 0; i < order.size(); ++i) {
    order[i] = i;
  }
  std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
    const auto& la = result.layers[a];
    const auto& lb = result.layers[b];
    if (la.depth != lb.depth) {
      return la.depth < lb.depth;
    }
    if (la.area != lb.area) {
      return la.area > lb.area;
    }
    return palette_color_key(la.color) < palette_color_key(lb.color);
  });
  std::vector<ImageTraceLayer> ordered;
  ordered.reserve(order.size());
  for (const auto index : order) {
    ordered.push_back(std::move(result.layers[index]));
  }
  result.layers = std::move(ordered);
  return result;
}

PixelBuffer render_image_trace(const ImageTraceResult& result, std::int32_t width, std::int32_t height) {
  if (width <= 0 || height <= 0) {
    return {};
  }
  PixelBuffer output(width, height, PixelFormat::rgba8());
  output.clear(0);
  VectorRasterOptions raster_options;
  raster_options.clip = Rect::from_size(width, height);
  for (const auto& layer : result.layers) {
    const auto coverage = rasterize_vector_path(layer.path, raster_options);
    if (coverage.pixels.empty()) {
      continue;
    }
    for (std::int32_t y = 0; y < coverage.bounds.height; ++y) {
      const auto* source = coverage.pixels.row(y).data();
      const auto target_y = coverage.bounds.y + y;
      if (target_y < 0 || target_y >= height) {
        continue;
      }
      for (std::int32_t x = 0; x < coverage.bounds.width; ++x) {
        const auto target_x = coverage.bounds.x + x;
        if (target_x < 0 || target_x >= width) {
          continue;
        }
        const int cov = source[x];
        if (cov == 0) {
          continue;
        }
        auto* dst = output.pixel(target_x, target_y);
        const int dst_a = dst[3];
        // Straight-alpha "over" in integer math.
        const int inverse = 255 - cov;
        const int out_a = cov + (dst_a * inverse + 127) / 255;
        if (out_a == 0) {
          continue;
        }
        const auto blend = [&](int src_c, int dst_c) {
          const int numerator = src_c * cov * 255 + dst_c * dst_a * inverse;
          return static_cast<std::uint8_t>(std::clamp((numerator + out_a * 255 / 2) / (out_a * 255), 0, 255));
        };
        dst[0] = blend(layer.color.red, dst[0]);
        dst[1] = blend(layer.color.green, dst[1]);
        dst[2] = blend(layer.color.blue, dst[2]);
        dst[3] = static_cast<std::uint8_t>(out_a);
      }
    }
  }
  return output;
}

Layer build_image_trace_group(Document& document, const ImageTraceResult& result, std::int32_t origin_x,
                              std::int32_t origin_y, std::string group_name) {
  const auto canvas_rect = Rect::from_size(document.width(), document.height());
  const auto* patterns = &document.metadata().patterns;
  Layer group(document.allocate_layer_id(), std::move(group_name), LayerKind::Group);
  group.set_blend_mode(BlendMode::PassThrough);
  // Back to front: add_child appends on top.
  for (const auto& traced : result.layers) {
    char name[8];
    std::snprintf(name, sizeof(name), "#%02X%02X%02X", traced.color.red, traced.color.green, traced.color.blue);
    Layer shape(document.allocate_layer_id(), name, LayerKind::Pixel);
    shape.metadata()[kLayerMetadataVectorShape] = "1";
    shape.metadata()[kLayerMetadataVectorRasterStatus] = kVectorRasterStatusPatchy;
    mark_layer_vector_block_dirty(shape);
    VectorShapeContent content;
    content.path = traced.path;
    translate_vector_path(content.path, origin_x, origin_y);
    content.fill.kind = VectorFillKind::Solid;
    content.fill.color = traced.color;
    content.stroke.enabled = false;
    shape.set_vector_shape(std::move(content));
    update_vector_shape_raster(shape, canvas_rect, patterns);
    group.add_child(std::move(shape));
  }
  return group;
}

}  // namespace patchy
