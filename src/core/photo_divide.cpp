#include "core/photo_divide.hpp"

#include "core/rect_utils.hpp"
#include "core/warp_mesh.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <type_traits>

namespace patchy {
namespace {

// Fixed pipeline constants (see the header's constraint comment: global
// parameters only, no content-driven adaptation).
constexpr std::int32_t kAnalysisMaxEdge = 1024;
constexpr double kMinPhotoEdgeInches = 0.5;
constexpr double kMinPhotoEdgeFraction = 0.015;  // of the longer source edge, when PPI unknown
constexpr double kMinPhotoEdgeFloorPx = 24.0;    // full-resolution floor
constexpr double kAngleSnapDegrees = 0.3;
constexpr double kAspectSnapTolerance = 0.03;
constexpr double kMergeIouThreshold = 0.30;
constexpr double kMergeContainmentThreshold = 0.80;
constexpr double kMinFillRatio = 0.25;         // component area over its min-rect area
constexpr double kWholeImageCoverage = 0.98;   // regions covering the whole source are dropped
constexpr std::int32_t kMaxOutputEdge = 30000;

struct Point64 {
  std::int64_t x{0};
  std::int64_t y{0};
};

struct PointD {
  double x{0.0};
  double y{0.0};
};

struct AnalysisImage {
  std::int32_t width{0};
  std::int32_t height{0};
  std::int32_t factor{1};  // full-resolution pixels per analysis pixel, both axes
  std::vector<std::uint8_t> rgb;  // 3 bytes per pixel, row-major
};

[[nodiscard]] bool poll_cancelled(const std::function<bool()>& cancelled) {
  return cancelled && cancelled();
}

// --- source conversion --------------------------------------------------------

[[nodiscard]] bool supported_analysis_format(const PixelFormat& format) {
  if (format.color_mode != ColorMode::RGB && format.color_mode != ColorMode::Grayscale) {
    return false;
  }
  if (format.channels != 1 && format.channels != 3 && format.channels != 4) {
    return false;
  }
  return format.bit_depth == BitDepth::UInt8 || format.bit_depth == BitDepth::UInt16 ||
         format.bit_depth == BitDepth::Float32;
}

[[nodiscard]] std::uint8_t channel_to_8bit(const std::uint8_t* pixel, std::size_t channel,
                                           BitDepth depth) {
  switch (depth) {
    case BitDepth::UInt8:
      return pixel[channel];
    case BitDepth::UInt16: {
      std::uint16_t value = 0;
      std::memcpy(&value, pixel + channel * 2, 2);
      return static_cast<std::uint8_t>((value + 128U) / 257U);
    }
    case BitDepth::Float32: {
      float value = 0.0F;
      std::memcpy(&value, pixel + channel * 4, 4);
      const double clamped = std::clamp(static_cast<double>(value), 0.0, 1.0);
      return static_cast<std::uint8_t>(std::floor(clamped * 255.0 + 0.5));
    }
  }
  return 0;
}

// Downsamples to at most kAnalysisMaxEdge per edge with integer box averages,
// converting to 8-bit RGB and compositing alpha over white.
[[nodiscard]] AnalysisImage build_analysis_image(const PixelBuffer& source) {
  AnalysisImage analysis;
  const std::int32_t source_width = source.width();
  const std::int32_t source_height = source.height();
  const std::int32_t longest = std::max(source_width, source_height);
  analysis.factor = std::max(1, (longest + kAnalysisMaxEdge - 1) / kAnalysisMaxEdge);
  analysis.width = (source_width + analysis.factor - 1) / analysis.factor;
  analysis.height = (source_height + analysis.factor - 1) / analysis.factor;
  analysis.rgb.assign(static_cast<std::size_t>(analysis.width) * analysis.height * 3, 0);

  const PixelFormat format = source.format();
  const bool grayscale = format.channels == 1;
  const bool has_alpha = format.channels == 4;
  for (std::int32_t ay = 0; ay < analysis.height; ++ay) {
    const std::int32_t y0 = ay * analysis.factor;
    const std::int32_t y1 = std::min(y0 + analysis.factor, source_height);
    for (std::int32_t ax = 0; ax < analysis.width; ++ax) {
      const std::int32_t x0 = ax * analysis.factor;
      const std::int32_t x1 = std::min(x0 + analysis.factor, source_width);
      std::uint64_t sums[3] = {0, 0, 0};
      std::uint64_t count = 0;
      for (std::int32_t y = y0; y < y1; ++y) {
        for (std::int32_t x = x0; x < x1; ++x) {
          const std::uint8_t* pixel = source.pixel(x, y);
          std::uint32_t red = channel_to_8bit(pixel, 0, format.bit_depth);
          std::uint32_t green = grayscale ? red : channel_to_8bit(pixel, 1, format.bit_depth);
          std::uint32_t blue = grayscale ? red : channel_to_8bit(pixel, 2, format.bit_depth);
          if (has_alpha) {
            const std::uint32_t alpha = channel_to_8bit(pixel, 3, format.bit_depth);
            const std::uint32_t inverse = 255U - alpha;
            red = (red * alpha + 255U * inverse + 127U) / 255U;
            green = (green * alpha + 255U * inverse + 127U) / 255U;
            blue = (blue * alpha + 255U * inverse + 127U) / 255U;
          }
          sums[0] += red;
          sums[1] += green;
          sums[2] += blue;
          ++count;
        }
      }
      auto* out = analysis.rgb.data() + (static_cast<std::size_t>(ay) * analysis.width + ax) * 3;
      for (int channel = 0; channel < 3; ++channel) {
        out[channel] = static_cast<std::uint8_t>((sums[channel] + count / 2) / count);
      }
    }
  }
  return analysis;
}

// --- background model ---------------------------------------------------------

struct BackgroundModel {
  std::array<std::int32_t, 3> median{};
  std::int32_t mad{0};  // max over channels
};

[[nodiscard]] std::int32_t histogram_median(const std::array<std::uint32_t, 256>& histogram,
                                            std::uint64_t total) {
  std::uint64_t half = (total + 1) / 2;
  std::uint64_t running = 0;
  for (std::int32_t value = 0; value < 256; ++value) {
    running += histogram[static_cast<std::size_t>(value)];
    if (running >= half) {
      return value;
    }
  }
  return 255;
}

[[nodiscard]] BackgroundModel background_from_border(const AnalysisImage& analysis) {
  const std::int32_t band =
      std::max<std::int32_t>(2, std::min(analysis.width, analysis.height) / 64);
  std::array<std::array<std::uint32_t, 256>, 3> histograms{};
  std::uint64_t total = 0;
  for (std::int32_t y = 0; y < analysis.height; ++y) {
    const bool edge_row = y < band || y >= analysis.height - band;
    for (std::int32_t x = 0; x < analysis.width; ++x) {
      if (!edge_row && x >= band && x < analysis.width - band) {
        x = analysis.width - band - 1;  // skip to the right band
        continue;
      }
      const auto* pixel =
          analysis.rgb.data() + (static_cast<std::size_t>(y) * analysis.width + x) * 3;
      for (int channel = 0; channel < 3; ++channel) {
        ++histograms[static_cast<std::size_t>(channel)][pixel[channel]];
      }
      ++total;
    }
  }
  BackgroundModel model;
  if (total == 0) {
    return model;
  }
  for (int channel = 0; channel < 3; ++channel) {
    model.median[static_cast<std::size_t>(channel)] =
        histogram_median(histograms[static_cast<std::size_t>(channel)], total);
  }
  std::int32_t mad = 0;
  for (int channel = 0; channel < 3; ++channel) {
    std::array<std::uint32_t, 256> deviation{};
    const std::int32_t median = model.median[static_cast<std::size_t>(channel)];
    for (std::int32_t value = 0; value < 256; ++value) {
      deviation[static_cast<std::size_t>(std::abs(value - median))] +=
          histograms[static_cast<std::size_t>(channel)][static_cast<std::size_t>(value)];
    }
    mad = std::max(mad, histogram_median(deviation, total));
  }
  model.mad = mad;
  return model;
}

// sensitivity 0..100 -> Chebyshev color tolerance. Higher sensitivity keeps a
// tighter background band, so photos close to the background color separate.
[[nodiscard]] std::int32_t background_tolerance(int sensitivity, std::int32_t mad) {
  const std::int32_t clamped = std::clamp(sensitivity, 0, 100);
  const std::int32_t base = 64 - (54 * clamped) / 100;  // 64 at 0, 10 at 100
  return std::clamp(base + 2 * mad, 6, 160);
}

[[nodiscard]] std::int32_t background_distance(const AnalysisImage& analysis, std::size_t index,
                                               const BackgroundModel& model) {
  const auto* pixel = analysis.rgb.data() + index * 3;
  std::int32_t distance = 0;
  for (int channel = 0; channel < 3; ++channel) {
    distance = std::max(distance, std::abs(static_cast<std::int32_t>(pixel[channel]) -
                                           model.median[static_cast<std::size_t>(channel)]));
  }
  return distance;
}

// Foreground mask: 1 everywhere except the border-connected background
// component within tolerance (4-connected scanline-free BFS; the visit set is
// order-independent, so traversal order cannot change the result).
[[nodiscard]] std::vector<std::uint8_t> foreground_mask(const AnalysisImage& analysis,
                                                        const BackgroundModel& model,
                                                        std::int32_t tolerance) {
  const std::int32_t width = analysis.width;
  const std::int32_t height = analysis.height;
  const std::size_t pixel_count = static_cast<std::size_t>(width) * height;
  std::vector<std::uint8_t> mask(pixel_count, 1);
  std::vector<std::int32_t> stack;
  stack.reserve(static_cast<std::size_t>(width) * 2 + static_cast<std::size_t>(height) * 2);
  auto try_seed = [&](std::int32_t x, std::int32_t y) {
    const std::size_t index = static_cast<std::size_t>(y) * width + x;
    if (mask[index] != 0 && background_distance(analysis, index, model) <= tolerance) {
      mask[index] = 0;
      stack.push_back(static_cast<std::int32_t>(index));
    }
  };
  for (std::int32_t x = 0; x < width; ++x) {
    try_seed(x, 0);
    try_seed(x, height - 1);
  }
  for (std::int32_t y = 0; y < height; ++y) {
    try_seed(0, y);
    try_seed(width - 1, y);
  }
  while (!stack.empty()) {
    const std::int32_t index = stack.back();
    stack.pop_back();
    const std::int32_t x = index % width;
    const std::int32_t y = index / width;
    const std::array<std::int32_t, 4> neighbors = {x > 0 ? index - 1 : -1,
                                                   x + 1 < width ? index + 1 : -1,
                                                   y > 0 ? index - width : -1,
                                                   y + 1 < height ? index + width : -1};
    for (const std::int32_t neighbor : neighbors) {
      if (neighbor < 0) {
        continue;
      }
      const auto neighbor_index = static_cast<std::size_t>(neighbor);
      if (mask[neighbor_index] != 0 &&
          background_distance(analysis, neighbor_index, model) <= tolerance) {
        mask[neighbor_index] = 0;
        stack.push_back(neighbor);
      }
    }
  }
  return mask;
}

// --- binary morphology --------------------------------------------------------

// Separable box dilate/erode via row and column prefix sums of set pixels.
void binary_box_pass(std::vector<std::uint8_t>& mask, std::int32_t width, std::int32_t height,
                     std::int32_t radius, bool dilate) {
  if (radius <= 0) {
    return;
  }
  std::vector<std::uint8_t> scratch(mask.size(), 0);
  // Horizontal.
  std::vector<std::uint32_t> prefix(static_cast<std::size_t>(std::max(width, height)) + 1, 0);
  for (std::int32_t y = 0; y < height; ++y) {
    const std::size_t row = static_cast<std::size_t>(y) * width;
    for (std::int32_t x = 0; x < width; ++x) {
      prefix[static_cast<std::size_t>(x) + 1] =
          prefix[static_cast<std::size_t>(x)] + mask[row + static_cast<std::size_t>(x)];
    }
    for (std::int32_t x = 0; x < width; ++x) {
      const std::int32_t low = std::max(0, x - radius);
      const std::int32_t high = std::min(width - 1, x + radius);
      const std::uint32_t ones = prefix[static_cast<std::size_t>(high) + 1] -
                                 prefix[static_cast<std::size_t>(low)];
      const auto window = static_cast<std::uint32_t>(high - low + 1);
      scratch[row + static_cast<std::size_t>(x)] =
          dilate ? static_cast<std::uint8_t>(ones > 0) : static_cast<std::uint8_t>(ones == window);
    }
  }
  // Vertical.
  for (std::int32_t x = 0; x < width; ++x) {
    for (std::int32_t y = 0; y < height; ++y) {
      prefix[static_cast<std::size_t>(y) + 1] =
          prefix[static_cast<std::size_t>(y)] +
          scratch[static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)];
    }
    for (std::int32_t y = 0; y < height; ++y) {
      const std::int32_t low = std::max(0, y - radius);
      const std::int32_t high = std::min(height - 1, y + radius);
      const std::uint32_t ones = prefix[static_cast<std::size_t>(high) + 1] -
                                 prefix[static_cast<std::size_t>(low)];
      const auto window = static_cast<std::uint32_t>(high - low + 1);
      mask[static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)] =
          dilate ? static_cast<std::uint8_t>(ones > 0) : static_cast<std::uint8_t>(ones == window);
    }
  }
}

void close_then_open(std::vector<std::uint8_t>& mask, std::int32_t width, std::int32_t height) {
  const std::int32_t close_radius = std::max<std::int32_t>(1, std::max(width, height) / 256);
  const std::int32_t open_radius = std::max<std::int32_t>(1, close_radius / 2);
  binary_box_pass(mask, width, height, close_radius, true);
  binary_box_pass(mask, width, height, close_radius, false);
  binary_box_pass(mask, width, height, open_radius, false);
  binary_box_pass(mask, width, height, open_radius, true);
}

// --- connected components -----------------------------------------------------

struct DisjointSet {
  std::vector<std::int32_t> parent;

  std::int32_t make() {
    parent.push_back(static_cast<std::int32_t>(parent.size()));
    return parent.back();
  }
  std::int32_t find(std::int32_t node) {
    while (parent[static_cast<std::size_t>(node)] != node) {
      parent[static_cast<std::size_t>(node)] =
          parent[static_cast<std::size_t>(parent[static_cast<std::size_t>(node)])];
      node = parent[static_cast<std::size_t>(node)];
    }
    return node;
  }
  // The smaller root wins (fixed tie-break).
  void unite(std::int32_t a, std::int32_t b) {
    a = find(a);
    b = find(b);
    if (a == b) {
      return;
    }
    if (b < a) {
      std::swap(a, b);
    }
    parent[static_cast<std::size_t>(b)] = a;
  }
};

struct RowSpan {
  std::int32_t y{0};
  std::int32_t min_x{0};
  std::int32_t max_x{0};
};

struct Component {
  std::int64_t area{0};
  std::int32_t min_x{std::numeric_limits<std::int32_t>::max()};
  std::int32_t min_y{std::numeric_limits<std::int32_t>::max()};
  std::int32_t max_x{-1};
  std::int32_t max_y{-1};
  std::vector<RowSpan> rows;
};

[[nodiscard]] std::vector<Component> label_components(const std::vector<std::uint8_t>& mask,
                                                      std::int32_t width, std::int32_t height) {
  std::vector<std::int32_t> labels(mask.size(), -1);
  DisjointSet sets;
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      const std::size_t index = static_cast<std::size_t>(y) * width + x;
      if (mask[index] == 0) {
        continue;
      }
      const std::int32_t left = x > 0 ? labels[index - 1] : -1;
      const std::int32_t up = y > 0 ? labels[index - width] : -1;
      if (left < 0 && up < 0) {
        labels[index] = sets.make();
      } else if (left >= 0 && up >= 0) {
        labels[index] = std::min(left, up);
        sets.unite(left, up);
      } else {
        labels[index] = std::max(left, up);
      }
    }
  }
  // Dense ids in first-encounter order.
  std::vector<std::int32_t> dense(sets.parent.size(), -1);
  std::vector<Component> components;
  for (std::int32_t y = 0; y < height; ++y) {
    RowSpan open{};
    std::int32_t open_id = -1;
    auto flush = [&]() {
      if (open_id >= 0) {
        components[static_cast<std::size_t>(open_id)].rows.push_back(open);
        open_id = -1;
      }
    };
    for (std::int32_t x = 0; x < width; ++x) {
      const std::size_t index = static_cast<std::size_t>(y) * width + x;
      if (labels[index] < 0) {
        flush();
        continue;
      }
      const std::int32_t root = sets.find(labels[index]);
      if (dense[static_cast<std::size_t>(root)] < 0) {
        dense[static_cast<std::size_t>(root)] = static_cast<std::int32_t>(components.size());
        components.emplace_back();
      }
      const std::int32_t id = dense[static_cast<std::size_t>(root)];
      auto& component = components[static_cast<std::size_t>(id)];
      ++component.area;
      component.min_x = std::min(component.min_x, x);
      component.max_x = std::max(component.max_x, x);
      component.min_y = std::min(component.min_y, y);
      component.max_y = std::max(component.max_y, y);
      if (open_id == id && open.y == y && open.max_x + 1 == x) {
        open.max_x = x;
      } else {
        flush();
        open = RowSpan{y, x, x};
        open_id = id;
      }
    }
    flush();
  }
  return components;
}

[[nodiscard]] std::int64_t box_area(std::int32_t min_x, std::int32_t min_y, std::int32_t max_x,
                                    std::int32_t max_y) {
  if (max_x < min_x || max_y < min_y) {
    return 0;
  }
  return static_cast<std::int64_t>(max_x - min_x + 1) * (max_y - min_y + 1);
}

[[nodiscard]] bool boxes_should_merge(const Component& a, const Component& b) {
  const std::int32_t min_x = std::max(a.min_x, b.min_x);
  const std::int32_t min_y = std::max(a.min_y, b.min_y);
  const std::int32_t max_x = std::min(a.max_x, b.max_x);
  const std::int32_t max_y = std::min(a.max_y, b.max_y);
  const std::int64_t intersection = box_area(min_x, min_y, max_x, max_y);
  if (intersection <= 0) {
    return false;
  }
  const std::int64_t area_a = box_area(a.min_x, a.min_y, a.max_x, a.max_y);
  const std::int64_t area_b = box_area(b.min_x, b.min_y, b.max_x, b.max_y);
  const std::int64_t union_area = area_a + area_b - intersection;
  const double iou = union_area > 0 ? static_cast<double>(intersection) / static_cast<double>(union_area) : 0.0;
  const double containment =
      static_cast<double>(intersection) / static_cast<double>(std::min(area_a, area_b));
  return iou > kMergeIouThreshold || containment > kMergeContainmentThreshold;
}

// Merges components whose bounding boxes overlap heavily (a photo fragmented
// by a bright band). Ascending pair order, repeated to a fixed point.
void merge_overlapping_components(std::vector<Component>& components) {
  DisjointSet sets;
  for (std::size_t i = 0; i < components.size(); ++i) {
    sets.make();
  }
  std::vector<Component> merged = components;
  bool changed = true;
  while (changed) {
    changed = false;
    for (std::size_t i = 0; i < merged.size(); ++i) {
      const std::int32_t root_i = sets.find(static_cast<std::int32_t>(i));
      for (std::size_t j = i + 1; j < merged.size(); ++j) {
        const std::int32_t root_j = sets.find(static_cast<std::int32_t>(j));
        if (root_i == root_j) {
          continue;
        }
        if (!boxes_should_merge(merged[static_cast<std::size_t>(root_i)],
                                merged[static_cast<std::size_t>(root_j)])) {
          continue;
        }
        sets.unite(root_i, root_j);
        const std::int32_t root = sets.find(root_i);
        const std::int32_t other = root == root_i ? root_j : root_i;
        auto& target = merged[static_cast<std::size_t>(root)];
        auto& donor = merged[static_cast<std::size_t>(other)];
        target.area += donor.area;
        target.min_x = std::min(target.min_x, donor.min_x);
        target.min_y = std::min(target.min_y, donor.min_y);
        target.max_x = std::max(target.max_x, donor.max_x);
        target.max_y = std::max(target.max_y, donor.max_y);
        changed = true;
      }
    }
  }
  std::vector<Component> result;
  for (std::size_t i = 0; i < components.size(); ++i) {
    if (sets.find(static_cast<std::int32_t>(i)) != static_cast<std::int32_t>(i)) {
      continue;
    }
    Component combined = merged[i];
    combined.rows.clear();
    for (std::size_t j = 0; j < components.size(); ++j) {
      if (sets.find(static_cast<std::int32_t>(j)) == static_cast<std::int32_t>(i)) {
        combined.rows.insert(combined.rows.end(), components[j].rows.begin(),
                             components[j].rows.end());
      }
    }
    std::sort(combined.rows.begin(), combined.rows.end(), [](const RowSpan& a, const RowSpan& b) {
      return a.y != b.y ? a.y < b.y : a.min_x < b.min_x;
    });
    result.push_back(std::move(combined));
  }
  components.swap(result);
}

// --- convex hull and min-area rect --------------------------------------------

[[nodiscard]] std::vector<Point64> component_hull(const Component& component) {
  std::vector<Point64> points;
  points.reserve(component.rows.size() * 4);
  for (const RowSpan& span : component.rows) {
    points.push_back({span.min_x, span.y});
    points.push_back({span.min_x, span.y + 1});
    points.push_back({span.max_x + 1, span.y});
    points.push_back({span.max_x + 1, span.y + 1});
  }
  std::sort(points.begin(), points.end(), [](const Point64& a, const Point64& b) {
    return a.x != b.x ? a.x < b.x : a.y < b.y;
  });
  points.erase(std::unique(points.begin(), points.end(),
                           [](const Point64& a, const Point64& b) {
                             return a.x == b.x && a.y == b.y;
                           }),
               points.end());
  if (points.size() < 3) {
    return points;
  }
  auto cross = [](const Point64& origin, const Point64& a, const Point64& b) {
    return (a.x - origin.x) * (b.y - origin.y) - (a.y - origin.y) * (b.x - origin.x);
  };
  std::vector<Point64> hull(points.size() * 2);
  std::size_t size = 0;
  for (const Point64& point : points) {  // lower
    while (size >= 2 && cross(hull[size - 2], hull[size - 1], point) <= 0) {
      --size;
    }
    hull[size++] = point;
  }
  const std::size_t lower = size + 1;
  for (auto it = points.rbegin() + 1; it != points.rend(); ++it) {  // upper
    while (size >= lower && cross(hull[size - 2], hull[size - 1], *it) <= 0) {
      --size;
    }
    hull[size++] = *it;
  }
  hull.resize(size - 1);
  return hull;
}

struct MinAreaRect {
  PointD u{1.0, 0.0};  // unit direction of the width axis, angle in [-45, 45)
  PointD v{0.0, 1.0};  // u rotated +90 degrees (points down in image space)
  PointD center{};
  double width{0.0};
  double height{0.0};
  double angle_degrees{0.0};
};

[[nodiscard]] double hull_area(const std::vector<Point64>& hull) {
  std::int64_t twice = 0;
  for (std::size_t i = 0; i < hull.size(); ++i) {
    const Point64& a = hull[i];
    const Point64& b = hull[(i + 1) % hull.size()];
    twice += a.x * b.y - b.x * a.y;
  }
  return std::abs(static_cast<double>(twice)) / 2.0;
}

[[nodiscard]] MinAreaRect min_area_rect(const std::vector<Point64>& hull) {
  MinAreaRect best;
  double best_area = std::numeric_limits<double>::infinity();
  PointD best_dir{1.0, 0.0};
  for (std::size_t i = 0; i < hull.size(); ++i) {
    const Point64& a = hull[i];
    const Point64& b = hull[(i + 1) % hull.size()];
    const double dx = static_cast<double>(b.x - a.x);
    const double dy = static_cast<double>(b.y - a.y);
    const double length = std::sqrt(dx * dx + dy * dy);
    if (length <= 0.0) {
      continue;
    }
    const PointD u{dx / length, dy / length};
    const PointD v{-u.y, u.x};
    double min_u = std::numeric_limits<double>::infinity();
    double max_u = -min_u;
    double min_v = min_u;
    double max_v = -min_u;
    for (const Point64& point : hull) {
      const double pu = point.x * u.x + point.y * u.y;
      const double pv = point.x * v.x + point.y * v.y;
      min_u = std::min(min_u, pu);
      max_u = std::max(max_u, pu);
      min_v = std::min(min_v, pv);
      max_v = std::max(max_v, pv);
    }
    const double area = (max_u - min_u) * (max_v - min_v);
    if (area < best_area - 1e-9) {  // strict improvement: lowest edge index wins ties
      best_area = area;
      best_dir = u;
    }
  }
  // Normalize the direction to [-45, 45) with exact 90-degree rotations.
  PointD direction = best_dir;
  for (int step = 0; step < 4; ++step) {
    if (direction.x > 0.0 && direction.y >= -direction.x && direction.y < direction.x) {
      break;
    }
    direction = PointD{-direction.y, direction.x};
  }
  best.u = direction;
  best.v = PointD{-direction.y, direction.x};
  best.angle_degrees = std::atan2(direction.y, direction.x) * 180.0 / 3.14159265358979323846;
  if (std::abs(best.angle_degrees) < kAngleSnapDegrees) {
    best.u = PointD{1.0, 0.0};
    best.v = PointD{0.0, 1.0};
    best.angle_degrees = 0.0;
  }
  double min_u = std::numeric_limits<double>::infinity();
  double max_u = -min_u;
  double min_v = min_u;
  double max_v = -min_u;
  for (const Point64& point : hull) {
    const double pu = point.x * best.u.x + point.y * best.u.y;
    const double pv = point.x * best.v.x + point.y * best.v.y;
    min_u = std::min(min_u, pu);
    max_u = std::max(max_u, pu);
    min_v = std::min(min_v, pv);
    max_v = std::max(max_v, pv);
  }
  best.width = max_u - min_u;
  best.height = max_v - min_v;
  const double mid_u = (min_u + max_u) / 2.0;
  const double mid_v = (min_v + max_v) / 2.0;
  best.center = PointD{best.u.x * mid_u + best.v.x * mid_v, best.u.y * mid_u + best.v.y * mid_v};
  return best;
}

[[nodiscard]] std::array<double, 8> rect_quad(const MinAreaRect& rect) {
  const double half_width = rect.width / 2.0;
  const double half_height = rect.height / 2.0;
  const PointD corners[4] = {
      {rect.center.x - half_width * rect.u.x - half_height * rect.v.x,
       rect.center.y - half_width * rect.u.y - half_height * rect.v.y},
      {rect.center.x + half_width * rect.u.x - half_height * rect.v.x,
       rect.center.y + half_width * rect.u.y - half_height * rect.v.y},
      {rect.center.x + half_width * rect.u.x + half_height * rect.v.x,
       rect.center.y + half_width * rect.u.y + half_height * rect.v.y},
      {rect.center.x - half_width * rect.u.x + half_height * rect.v.x,
       rect.center.y - half_width * rect.u.y + half_height * rect.v.y},
  };
  return {corners[0].x, corners[0].y, corners[1].x, corners[1].y,
          corners[2].x, corners[2].y, corners[3].x, corners[3].y};
}

// --- perspective quad fit -----------------------------------------------------

struct FitLine {
  PointD point{};
  PointD direction{1.0, 0.0};
  bool valid{false};
};

struct SideAccumulator {
  double weight{0.0};
  double sum_x{0.0};
  double sum_y{0.0};
  double sum_xx{0.0};
  double sum_xy{0.0};
  double sum_yy{0.0};
  int edges{0};

  void add(const PointD& point, double point_weight) {
    weight += point_weight;
    sum_x += point_weight * point.x;
    sum_y += point_weight * point.y;
    sum_xx += point_weight * point.x * point.x;
    sum_xy += point_weight * point.x * point.y;
    sum_yy += point_weight * point.y * point.y;
  }

  [[nodiscard]] FitLine fit() const {
    FitLine line;
    if (edges < 1 || weight <= 1e-9) {
      return line;
    }
    const double mean_x = sum_x / weight;
    const double mean_y = sum_y / weight;
    const double cxx = sum_xx / weight - mean_x * mean_x;
    const double cxy = sum_xy / weight - mean_x * mean_y;
    const double cyy = sum_yy / weight - mean_y * mean_y;
    const double trace_half = (cxx + cyy) / 2.0;
    const double delta = std::sqrt(std::max(0.0, (cxx - cyy) * (cxx - cyy) / 4.0 + cxy * cxy));
    const double lambda = trace_half + delta;  // largest eigenvalue
    PointD direction{cxy, lambda - cxx};
    PointD alternate{lambda - cyy, cxy};
    if (alternate.x * alternate.x + alternate.y * alternate.y >
        direction.x * direction.x + direction.y * direction.y) {
      direction = alternate;
    }
    const double norm = std::sqrt(direction.x * direction.x + direction.y * direction.y);
    if (norm <= 1e-12) {
      return line;
    }
    line.point = PointD{mean_x, mean_y};
    line.direction = PointD{direction.x / norm, direction.y / norm};
    line.valid = true;
    return line;
  }
};

[[nodiscard]] std::optional<PointD> intersect_lines(const FitLine& a, const FitLine& b) {
  const double cross = a.direction.x * b.direction.y - a.direction.y * b.direction.x;
  if (std::abs(cross) < 1e-9) {
    return std::nullopt;
  }
  const double dx = b.point.x - a.point.x;
  const double dy = b.point.y - a.point.y;
  const double t = (dx * b.direction.y - dy * b.direction.x) / cross;
  return PointD{a.point.x + t * a.direction.x, a.point.y + t * a.direction.y};
}

[[nodiscard]] double quad_area(const std::array<double, 8>& quad) {
  double twice = 0.0;
  for (int i = 0; i < 4; ++i) {
    const int j = (i + 1) % 4;
    twice += quad[static_cast<std::size_t>(i * 2)] * quad[static_cast<std::size_t>(j * 2 + 1)] -
             quad[static_cast<std::size_t>(j * 2)] * quad[static_cast<std::size_t>(i * 2 + 1)];
  }
  return std::abs(twice) / 2.0;
}

[[nodiscard]] bool quad_is_convex(const std::array<double, 8>& quad) {
  double reference = 0.0;
  for (int i = 0; i < 4; ++i) {
    const int j = (i + 1) % 4;
    const int k = (i + 2) % 4;
    const double abx = quad[static_cast<std::size_t>(j * 2)] - quad[static_cast<std::size_t>(i * 2)];
    const double aby =
        quad[static_cast<std::size_t>(j * 2 + 1)] - quad[static_cast<std::size_t>(i * 2 + 1)];
    const double bcx = quad[static_cast<std::size_t>(k * 2)] - quad[static_cast<std::size_t>(j * 2)];
    const double bcy =
        quad[static_cast<std::size_t>(k * 2 + 1)] - quad[static_cast<std::size_t>(j * 2 + 1)];
    const double cross = abx * bcy - aby * bcx;
    if (std::abs(cross) < 1e-9) {
      return false;
    }
    if (reference == 0.0) {
      reference = cross;
    } else if ((cross > 0.0) != (reference > 0.0)) {
      return false;
    }
  }
  return true;
}

// Assigns hull edges to the min-area rect's four sides, fits one orthogonal
// regression line per side (edge endpoints weighted by edge length), and
// intersects adjacent lines. Returns nullopt when the fit degenerates.
[[nodiscard]] std::optional<std::array<double, 8>> fit_perspective_quad(
    const std::vector<Point64>& hull, const MinAreaRect& rect) {
  enum Side : int { kTop = 0, kRight = 1, kBottom = 2, kLeft = 3 };
  if (hull.size() < 4 || rect.width <= 1.0 || rect.height <= 1.0) {
    return std::nullopt;
  }
  std::array<SideAccumulator, 4> sides{};
  const double half_width = std::max(rect.width / 2.0, 1e-6);
  const double half_height = std::max(rect.height / 2.0, 1e-6);
  for (std::size_t i = 0; i < hull.size(); ++i) {
    const Point64& a = hull[i];
    const Point64& b = hull[(i + 1) % hull.size()];
    const double dx = static_cast<double>(b.x - a.x);
    const double dy = static_cast<double>(b.y - a.y);
    const double length = std::sqrt(dx * dx + dy * dy);
    if (length <= 0.0) {
      continue;
    }
    const PointD mid{(static_cast<double>(a.x) + static_cast<double>(b.x)) / 2.0,
                     (static_cast<double>(a.y) + static_cast<double>(b.y)) / 2.0};
    const double du = (mid.x - rect.center.x) * rect.u.x + (mid.y - rect.center.y) * rect.u.y;
    const double dv = (mid.x - rect.center.x) * rect.v.x + (mid.y - rect.center.y) * rect.v.y;
    const double nu = du / half_width;
    const double nv = dv / half_height;
    const int side = std::abs(nu) > std::abs(nv) ? (nu > 0.0 ? kRight : kLeft)
                                                 : (nv > 0.0 ? kBottom : kTop);
    const PointD tangent = (side == kTop || side == kBottom) ? rect.u : rect.v;
    const double alignment = std::abs((dx * tangent.x + dy * tangent.y) / length);
    if (alignment < 0.70710678) {  // more than 45 degrees off the side: a corner edge
      continue;
    }
    auto& accumulator = sides[static_cast<std::size_t>(side)];
    accumulator.add(PointD{static_cast<double>(a.x), static_cast<double>(a.y)}, length / 2.0);
    accumulator.add(PointD{static_cast<double>(b.x), static_cast<double>(b.y)}, length / 2.0);
    ++accumulator.edges;
  }
  const std::array<double, 8> rect_corners = rect_quad(rect);
  std::array<FitLine, 4> lines{};
  for (int side = 0; side < 4; ++side) {
    lines[static_cast<std::size_t>(side)] = sides[static_cast<std::size_t>(side)].fit();
    if (!lines[static_cast<std::size_t>(side)].valid) {
      // Fall back to the rect's own side.
      const int start = side;           // top edge runs TL->TR, right TR->BR, ...
      const int end = (side + 1) % 4;
      const PointD a{rect_corners[static_cast<std::size_t>(start * 2)],
                     rect_corners[static_cast<std::size_t>(start * 2 + 1)]};
      const PointD b{rect_corners[static_cast<std::size_t>(end * 2)],
                     rect_corners[static_cast<std::size_t>(end * 2 + 1)]};
      const double dx = b.x - a.x;
      const double dy = b.y - a.y;
      const double length = std::sqrt(dx * dx + dy * dy);
      if (length <= 1e-9) {
        return std::nullopt;
      }
      lines[static_cast<std::size_t>(side)] =
          FitLine{a, PointD{dx / length, dy / length}, true};
    }
  }
  const auto top_left = intersect_lines(lines[kTop], lines[kLeft]);
  const auto top_right = intersect_lines(lines[kTop], lines[kRight]);
  const auto bottom_right = intersect_lines(lines[kBottom], lines[kRight]);
  const auto bottom_left = intersect_lines(lines[kBottom], lines[kLeft]);
  if (!top_left || !top_right || !bottom_right || !bottom_left) {
    return std::nullopt;
  }
  const std::array<double, 8> quad = {top_left->x,     top_left->y,     top_right->x,
                                      top_right->y,    bottom_right->x, bottom_right->y,
                                      bottom_left->x,  bottom_left->y};
  if (!quad_is_convex(quad)) {
    return std::nullopt;
  }
  const double reach = 1.2 * std::max(rect.width, rect.height);
  for (int corner = 0; corner < 4; ++corner) {
    const double dx = quad[static_cast<std::size_t>(corner * 2)] - rect.center.x;
    const double dy = quad[static_cast<std::size_t>(corner * 2 + 1)] - rect.center.y;
    if (std::sqrt(dx * dx + dy * dy) > reach) {
      return std::nullopt;
    }
  }
  const double area = quad_area(quad);
  const double hull_pixels = hull_area(hull);
  if (hull_pixels <= 0.0 || area / hull_pixels < 0.8 || area / hull_pixels > 1.25) {
    return std::nullopt;
  }
  const double min_side = 0.3 * std::min(rect.width, rect.height);
  for (int i = 0; i < 4; ++i) {
    const int j = (i + 1) % 4;
    const double dx =
        quad[static_cast<std::size_t>(j * 2)] - quad[static_cast<std::size_t>(i * 2)];
    const double dy =
        quad[static_cast<std::size_t>(j * 2 + 1)] - quad[static_cast<std::size_t>(i * 2 + 1)];
    if (std::sqrt(dx * dx + dy * dy) < min_side) {
      return std::nullopt;
    }
  }
  return quad;
}

}  // namespace

void order_photo_regions_reading_order(std::vector<PhotoRegion>& regions) {
  if (regions.size() < 2) {
    return;
  }
  struct Entry {
    double center_x{0.0};
    double center_y{0.0};
    double height{0.0};
    std::size_t index{0};
  };
  std::vector<Entry> entries;
  entries.reserve(regions.size());
  for (std::size_t i = 0; i < regions.size(); ++i) {
    const auto& quad = regions[i].quad;
    Entry entry;
    entry.index = i;
    for (int corner = 0; corner < 4; ++corner) {
      entry.center_x += quad[static_cast<std::size_t>(corner * 2)] / 4.0;
      entry.center_y += quad[static_cast<std::size_t>(corner * 2 + 1)] / 4.0;
    }
    entry.height = static_cast<double>(regions[i].bounding_box.height);
    entries.push_back(entry);
  }
  std::vector<double> heights;
  heights.reserve(entries.size());
  for (const Entry& entry : entries) {
    heights.push_back(entry.height);
  }
  std::sort(heights.begin(), heights.end());
  const double median_height = heights[heights.size() / 2];
  const double band = std::max(1.0, median_height / 2.0);
  std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
    return a.center_y != b.center_y ? a.center_y < b.center_y : a.index < b.index;
  });
  // Group into rows: a region joins the current row while its center stays
  // within `band` of the row's first center.
  std::vector<std::vector<Entry>> bands;
  for (const Entry& entry : entries) {
    if (bands.empty() || entry.center_y - bands.back().front().center_y > band) {
      bands.emplace_back();
    }
    bands.back().push_back(entry);
  }
  std::vector<PhotoRegion> ordered;
  ordered.reserve(regions.size());
  for (auto& row : bands) {
    std::sort(row.begin(), row.end(), [](const Entry& a, const Entry& b) {
      return a.center_x != b.center_x ? a.center_x < b.center_x : a.index < b.index;
    });
    for (const Entry& entry : row) {
      ordered.push_back(regions[entry.index]);
    }
  }
  regions.swap(ordered);
}

namespace {

// --- extraction sampling ------------------------------------------------------

template <typename T>
void sample_quad(const PixelBuffer& source, const std::array<double, 9>& homography,
                 PixelBuffer& out) {
  const std::int32_t source_width = source.width();
  const std::int32_t source_height = source.height();
  const int channels = source.format().channels;
  for (std::int32_t y = 0; y < out.height(); ++y) {
    auto* out_row = reinterpret_cast<T*>(out.row(y).data());
    for (std::int32_t x = 0; x < out.width(); ++x) {
      const auto mapped = apply_homography(homography, x + 0.5, y + 0.5);
      const double sample_x = mapped[0] - 0.5;
      const double sample_y = mapped[1] - 0.5;
      const double floor_x = std::floor(sample_x);
      const double floor_y = std::floor(sample_y);
      const double fraction_x = sample_x - floor_x;
      const double fraction_y = sample_y - floor_y;
      const std::int32_t x0 = std::clamp(static_cast<std::int32_t>(floor_x), 0, source_width - 1);
      const std::int32_t x1 =
          std::clamp(static_cast<std::int32_t>(floor_x) + 1, 0, source_width - 1);
      const std::int32_t y0 = std::clamp(static_cast<std::int32_t>(floor_y), 0, source_height - 1);
      const std::int32_t y1 =
          std::clamp(static_cast<std::int32_t>(floor_y) + 1, 0, source_height - 1);
      const auto* p00 = reinterpret_cast<const T*>(source.pixel(x0, y0));
      const auto* p10 = reinterpret_cast<const T*>(source.pixel(x1, y0));
      const auto* p01 = reinterpret_cast<const T*>(source.pixel(x0, y1));
      const auto* p11 = reinterpret_cast<const T*>(source.pixel(x1, y1));
      const double w00 = (1.0 - fraction_x) * (1.0 - fraction_y);
      const double w10 = fraction_x * (1.0 - fraction_y);
      const double w01 = (1.0 - fraction_x) * fraction_y;
      const double w11 = fraction_x * fraction_y;
      T* out_pixel = out_row + static_cast<std::size_t>(x) * channels;
      for (int channel = 0; channel < channels; ++channel) {
        const double value = w00 * static_cast<double>(p00[channel]) +
                             w10 * static_cast<double>(p10[channel]) +
                             w01 * static_cast<double>(p01[channel]) +
                             w11 * static_cast<double>(p11[channel]);
        if constexpr (std::is_floating_point_v<T>) {
          out_pixel[channel] = static_cast<T>(value);
        } else {
          const double limit = static_cast<double>(std::numeric_limits<T>::max());
          out_pixel[channel] =
              static_cast<T>(std::clamp(std::floor(value + 0.5), 0.0, limit));
        }
      }
    }
  }
}

// The quad is an axis-aligned integer rect fully inside the source: byte copy.
[[nodiscard]] bool try_exact_copy(const PixelBuffer& source, const std::array<double, 8>& quad,
                                  PixelBuffer& out) {
  for (const double value : quad) {
    if (std::abs(value - std::floor(value + 0.5)) > 0.005) {
      return false;
    }
  }
  const auto left = static_cast<std::int32_t>(std::floor(quad[0] + 0.5));
  const auto top = static_cast<std::int32_t>(std::floor(quad[1] + 0.5));
  const auto right = static_cast<std::int32_t>(std::floor(quad[4] + 0.5));
  const auto bottom = static_cast<std::int32_t>(std::floor(quad[5] + 0.5));
  const bool axis_aligned =
      std::floor(quad[2] + 0.5) == right && std::floor(quad[3] + 0.5) == top &&
      std::floor(quad[6] + 0.5) == left && std::floor(quad[7] + 0.5) == bottom;
  if (!axis_aligned || right - left != out.width() || bottom - top != out.height()) {
    return false;
  }
  if (left < 0 || top < 0 || right > source.width() || bottom > source.height()) {
    return false;
  }
  const std::size_t pixel_bytes = bytes_per_pixel(source.format());
  for (std::int32_t y = 0; y < out.height(); ++y) {
    std::memcpy(out.row(y).data(), source.pixel(left, top + y), pixel_bytes * out.width());
  }
  return true;
}

[[nodiscard]] double corner_distance(const std::array<double, 8>& quad, int a, int b) {
  const double dx = quad[static_cast<std::size_t>(b * 2)] - quad[static_cast<std::size_t>(a * 2)];
  const double dy =
      quad[static_cast<std::size_t>(b * 2 + 1)] - quad[static_cast<std::size_t>(a * 2 + 1)];
  return std::sqrt(dx * dx + dy * dy);
}

}  // namespace

PhotoDetectResult detect_photo_regions(const PixelBuffer& source, const PhotoDetectOptions& options,
                                       const std::function<bool()>& cancelled) {
  PhotoDetectResult result;
  if (source.empty() || source.width() < 4 || source.height() < 4 ||
      !supported_analysis_format(source.format())) {
    return result;
  }
  const AnalysisImage analysis = build_analysis_image(source);
  result.analysis_width = analysis.width;
  result.analysis_height = analysis.height;
  if (poll_cancelled(cancelled)) {
    return PhotoDetectResult{};
  }
  const BackgroundModel background = background_from_border(analysis);
  const std::int32_t tolerance = background_tolerance(options.sensitivity, background.mad);
  std::vector<std::uint8_t> mask = foreground_mask(analysis, background, tolerance);
  if (poll_cancelled(cancelled)) {
    return PhotoDetectResult{};
  }
  close_then_open(mask, analysis.width, analysis.height);
  if (poll_cancelled(cancelled)) {
    return PhotoDetectResult{};
  }
  std::vector<Component> components = label_components(mask, analysis.width, analysis.height);
  merge_overlapping_components(components);
  if (poll_cancelled(cancelled)) {
    return PhotoDetectResult{};
  }

  const double factor = analysis.factor;
  const double longest_source = std::max(source.width(), source.height());
  const double min_edge_full =
      options.source_ppi > 0.0
          ? std::max(kMinPhotoEdgeFloorPx, kMinPhotoEdgeInches * options.source_ppi)
          : std::max(kMinPhotoEdgeFloorPx, kMinPhotoEdgeFraction * longest_source);
  const double min_edge = min_edge_full / factor;

  for (const Component& component : components) {
    if (poll_cancelled(cancelled)) {
      return PhotoDetectResult{};
    }
    const double box_width = component.max_x - component.min_x + 1;
    const double box_height = component.max_y - component.min_y + 1;
    if (box_width < min_edge || box_height < min_edge) {
      continue;
    }
    if (box_width >= kWholeImageCoverage * analysis.width &&
        box_height >= kWholeImageCoverage * analysis.height) {
      continue;
    }
    const std::vector<Point64> hull = component_hull(component);
    if (hull.size() < 3) {
      continue;
    }
    const MinAreaRect rect = min_area_rect(hull);
    if (std::min(rect.width, rect.height) < min_edge) {
      continue;
    }
    if (static_cast<double>(component.area) < kMinFillRatio * rect.width * rect.height) {
      continue;
    }
    PhotoRegion region;
    region.angle_degrees = rect.angle_degrees;
    const std::array<double, 8> quad = rect_quad(rect);
    for (int i = 0; i < 8; ++i) {
      region.quad[static_cast<std::size_t>(i)] = quad[static_cast<std::size_t>(i)] * factor;
    }
    region.bounding_box =
        Rect{static_cast<std::int32_t>(component.min_x * analysis.factor),
             static_cast<std::int32_t>(component.min_y * analysis.factor),
             static_cast<std::int32_t>((component.max_x + 1 - component.min_x) * analysis.factor),
             static_cast<std::int32_t>((component.max_y + 1 - component.min_y) * analysis.factor)};
    if (const auto perspective = fit_perspective_quad(hull, rect)) {
      region.perspective_quad = true;
      for (int i = 0; i < 8; ++i) {
        region.perspective_corners[static_cast<std::size_t>(i)] =
            (*perspective)[static_cast<std::size_t>(i)] * factor;
      }
    } else {
      region.perspective_corners = region.quad;
    }
    result.regions.push_back(region);
  }
  order_photo_regions_reading_order(result.regions);
  return result;
}

std::optional<double> rectified_aspect_ratio(const std::array<double, 8>& quad,
                                             double source_width, double source_height) {
  // Zhang & He, MSR-TR-2003-39: principal point at the image center, square
  // pixels. Coordinates are normalized by the longer source edge so the
  // epsilons below are scale-free.
  const double scale = std::max(source_width, source_height);
  if (scale <= 0.0) {
    return std::nullopt;
  }
  const double center_x = source_width / 2.0;
  const double center_y = source_height / 2.0;
  using Vec3 = std::array<double, 3>;
  auto corner = [&](int index) {
    return Vec3{(quad[static_cast<std::size_t>(index * 2)] - center_x) / scale,
                (quad[static_cast<std::size_t>(index * 2 + 1)] - center_y) / scale, 1.0};
  };
  const Vec3 m1 = corner(0);  // TL
  const Vec3 m2 = corner(1);  // TR
  const Vec3 m3 = corner(3);  // BL
  const Vec3 m4 = corner(2);  // BR
  auto cross3 = [](const Vec3& a, const Vec3& b) {
    return Vec3{a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
  };
  auto dot3 = [](const Vec3& a, const Vec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
  const Vec3 m14 = cross3(m1, m4);
  const double k2_denominator = dot3(cross3(m2, m4), m3);
  const double k3_denominator = dot3(cross3(m3, m4), m2);
  if (std::abs(k2_denominator) < 1e-12 || std::abs(k3_denominator) < 1e-12) {
    return std::nullopt;
  }
  const double k2 = dot3(m14, m3) / k2_denominator;
  const double k3 = dot3(m14, m2) / k3_denominator;
  const Vec3 n2 = {k2 * m2[0] - m1[0], k2 * m2[1] - m1[1], k2 * m2[2] - m1[2]};
  const Vec3 n3 = {k3 * m3[0] - m1[0], k3 * m3[1] - m1[1], k3 * m3[2] - m1[2]};
  constexpr double kAffineEpsilon = 1e-6;
  double ratio_squared = 0.0;
  if (std::abs(n2[2]) < kAffineEpsilon && std::abs(n3[2]) < kAffineEpsilon) {
    const double denominator = n3[0] * n3[0] + n3[1] * n3[1];
    if (denominator < 1e-12) {
      return std::nullopt;
    }
    ratio_squared = (n2[0] * n2[0] + n2[1] * n2[1]) / denominator;
  } else if (std::abs(n2[2]) < kAffineEpsilon || std::abs(n3[2]) < kAffineEpsilon) {
    return std::nullopt;
  } else {
    const double focal_squared = -(n2[0] * n3[0] + n2[1] * n3[1]) / (n2[2] * n3[2]);
    if (!(focal_squared > 0.0) || !std::isfinite(focal_squared)) {
      return std::nullopt;
    }
    const double denominator = n3[0] * n3[0] + n3[1] * n3[1] + n3[2] * n3[2] * focal_squared;
    if (denominator < 1e-12) {
      return std::nullopt;
    }
    ratio_squared =
        (n2[0] * n2[0] + n2[1] * n2[1] + n2[2] * n2[2] * focal_squared) / denominator;
  }
  if (!(ratio_squared > 0.0) || !std::isfinite(ratio_squared)) {
    return std::nullopt;
  }
  return std::clamp(std::sqrt(ratio_squared), 0.05, 20.0);
}

double snap_aspect_to_print_ratios(double aspect) {
  if (!(aspect > 0.0) || !std::isfinite(aspect)) {
    return aspect;
  }
  constexpr std::array<double, 6> kRatios = {1.0, 1.25, 4.0 / 3.0, 1.4, 1.5, 16.0 / 9.0};
  const bool landscape = aspect >= 1.0;
  const double oriented = landscape ? aspect : 1.0 / aspect;
  double best = oriented;
  double best_difference = std::numeric_limits<double>::infinity();
  for (const double ratio : kRatios) {
    const double difference = std::abs(oriented - ratio);
    if (difference / ratio <= kAspectSnapTolerance && difference < best_difference) {
      best = ratio;
      best_difference = difference;
    }
  }
  return landscape ? best : 1.0 / best;
}

PhotoOutputGeometry photo_output_geometry(const PhotoRegion& region, PhotoExtractMode mode,
                                          double source_width, double source_height) {
  PhotoOutputGeometry geometry;
  if (mode == PhotoExtractMode::Cut) {
    const Rect source_rect{0, 0, static_cast<std::int32_t>(source_width),
                           static_cast<std::int32_t>(source_height)};
    const Rect clipped = intersect_rect(region.bounding_box, source_rect);
    geometry.width = std::max(0, clipped.width);
    geometry.height = std::max(0, clipped.height);
    const auto left = static_cast<double>(clipped.x);
    const auto top = static_cast<double>(clipped.y);
    const auto right = static_cast<double>(clipped.x + clipped.width);
    const auto bottom = static_cast<double>(clipped.y + clipped.height);
    geometry.source_quad = {left, top, right, top, right, bottom, left, bottom};
    return geometry;
  }
  const std::array<double, 8>& quad =
      mode == PhotoExtractMode::Perspective && region.perspective_quad ? region.perspective_corners
                                                                       : region.quad;
  geometry.source_quad = quad;
  const double top_length = corner_distance(quad, 0, 1);
  const double bottom_length = corner_distance(quad, 3, 2);
  const double left_length = corner_distance(quad, 0, 3);
  const double right_length = corner_distance(quad, 1, 2);
  const double width_length = (top_length + bottom_length) / 2.0;
  const double height_length = (left_length + right_length) / 2.0;
  double out_width = width_length;
  double out_height = height_length;
  if (mode == PhotoExtractMode::Perspective && height_length > 0.0) {
    const auto recovered = rectified_aspect_ratio(quad, source_width, source_height);
    const double aspect =
        snap_aspect_to_print_ratios(recovered.value_or(width_length / height_length));
    if (aspect > 0.0 && std::isfinite(aspect)) {
      // Keep the larger measured extent so the output never drops resolution.
      out_height = std::max(height_length, width_length / aspect);
      out_width = aspect * out_height;
    }
  }
  geometry.width = std::clamp(static_cast<std::int32_t>(std::floor(out_width + 0.5)), 1,
                              kMaxOutputEdge);
  geometry.height = std::clamp(static_cast<std::int32_t>(std::floor(out_height + 0.5)), 1,
                               kMaxOutputEdge);
  return geometry;
}

PixelBuffer extract_photo_region(const PixelBuffer& source, const PhotoRegion& region,
                                 PhotoExtractMode mode) {
  if (source.empty()) {
    return {};
  }
  const PhotoOutputGeometry geometry =
      photo_output_geometry(region, mode, source.width(), source.height());
  if (geometry.width <= 0 || geometry.height <= 0) {
    return {};
  }
  PixelBuffer out(geometry.width, geometry.height, source.format());
  if (try_exact_copy(source, geometry.source_quad, out)) {
    return out;
  }
  const auto homography = homography_from_rect_to_quad(
      0.0, 0.0, static_cast<double>(geometry.width), static_cast<double>(geometry.height),
      geometry.source_quad);
  if (!homography) {
    return {};
  }
  switch (source.format().bit_depth) {
    case BitDepth::UInt8:
      sample_quad<std::uint8_t>(source, *homography, out);
      break;
    case BitDepth::UInt16:
      sample_quad<std::uint16_t>(source, *homography, out);
      break;
    case BitDepth::Float32:
      sample_quad<float>(source, *homography, out);
      break;
  }
  return out;
}

}  // namespace patchy
