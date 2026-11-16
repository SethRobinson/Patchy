#include "ocr/ocr_text_detect.hpp"

#include "ocr/ocr_image.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace patchy::ocr {

namespace {

// Blue, green, red: the statistics the detector's own inference.yml lists, in its order.
constexpr std::array<float, 3> kMean{0.485F, 0.456F, 0.406F};
constexpr std::array<float, 3> kDeviation{0.229F, 0.224F, 0.225F};

struct IntPoint {
  std::int32_t x;
  std::int32_t y;
};

std::int64_t cross(IntPoint origin, IntPoint a, IntPoint b) noexcept {
  return static_cast<std::int64_t>(a.x - origin.x) * (b.y - origin.y) -
         static_cast<std::int64_t>(a.y - origin.y) * (b.x - origin.x);
}

// Andrew's monotone chain. `points` are sorted by x then y and unique. Counterclockwise in
// a y-up reading; the direction does not matter to the rectangle search.
std::vector<IntPoint> convex_hull(const std::vector<IntPoint>& points) {
  if (points.size() < 3) {
    return points;
  }
  std::vector<IntPoint> hull(points.size() * 2);
  std::size_t count = 0;
  for (const auto& point : points) {
    while (count >= 2 && cross(hull[count - 2], hull[count - 1], point) <= 0) {
      --count;
    }
    hull[count++] = point;
  }
  const std::size_t lower = count + 1;
  for (std::size_t index = points.size() - 1; index-- > 0;) {
    const auto& point = points[index];
    while (count >= lower && cross(hull[count - 2], hull[count - 1], point) <= 0) {
      --count;
    }
    hull[count++] = point;
  }
  hull.resize(count - 1);
  return hull;
}

struct Rectangle {
  double centre_x{0.0};
  double centre_y{0.0};
  double axis_x{1.0};  // unit vector of the first side
  double axis_y{0.0};
  double length{0.0};  // along the axis
  double thickness{0.0};  // across it
};

// The smallest rectangle around the hull's points: one of its sides lies along a hull edge,
// so each edge direction is tried and the first of the smallest wins.
Rectangle smallest_rectangle(const std::vector<IntPoint>& hull) {
  Rectangle best;
  if (hull.empty()) {
    return best;
  }
  if (hull.size() == 1) {
    best.centre_x = hull[0].x;
    best.centre_y = hull[0].y;
    return best;
  }
  double best_area = std::numeric_limits<double>::infinity();
  for (std::size_t index = 0; index < hull.size(); ++index) {
    const auto& from = hull[index];
    const auto& to = hull[(index + 1) % hull.size()];
    const double dx = static_cast<double>(to.x - from.x);
    const double dy = static_cast<double>(to.y - from.y);
    const double norm = std::sqrt(dx * dx + dy * dy);
    if (norm <= 0.0) {
      continue;
    }
    const double ux = dx / norm;
    const double uy = dy / norm;
    double min_along = std::numeric_limits<double>::infinity();
    double max_along = -min_along;
    double min_across = min_along;
    double max_across = -min_along;
    for (const auto& point : hull) {
      const double along = point.x * ux + point.y * uy;
      const double across = -point.x * uy + point.y * ux;
      min_along = std::min(min_along, along);
      max_along = std::max(max_along, along);
      min_across = std::min(min_across, across);
      max_across = std::max(max_across, across);
    }
    const double area = (max_along - min_along) * (max_across - min_across);
    if (area < best_area) {
      best_area = area;
      const double mid_along = (min_along + max_along) / 2.0;
      const double mid_across = (min_across + max_across) / 2.0;
      best.centre_x = mid_along * ux - mid_across * uy;
      best.centre_y = mid_along * uy + mid_across * ux;
      best.axis_x = ux;
      best.axis_y = uy;
      best.length = max_along - min_along;
      best.thickness = max_across - min_across;
    }
  }
  return best;
}

}  // namespace

DetectorInput detector_input(const ImageView& image, std::int32_t long_side_cap) {
  if (image.data == nullptr || image.width <= 0 || image.height <= 0 || long_side_cap < 32) {
    throw std::invalid_argument("text recognition was given an empty or badly sized image");
  }
  DetectorInput input;
  const std::int64_t long_side = std::max(image.width, image.height);
  if (long_side > long_side_cap) {
    input.scaled_width =
        static_cast<std::int32_t>(std::max<std::int64_t>(1, (static_cast<std::int64_t>(image.width) * long_side_cap + long_side / 2) / long_side));
    input.scaled_height =
        static_cast<std::int32_t>(std::max<std::int64_t>(1, (static_cast<std::int64_t>(image.height) * long_side_cap + long_side / 2) / long_side));
  } else {
    input.scaled_width = image.width;
    input.scaled_height = image.height;
  }
  input.tensor_width = (input.scaled_width + 31) / 32 * 32;
  input.tensor_height = (input.scaled_height + 31) / 32 * 32;
  const auto scaled = scaled_rows_bgra(image, input.scaled_width, input.scaled_height, 0, input.scaled_height);
  const auto plane = static_cast<std::size_t>(input.tensor_width) * static_cast<std::size_t>(input.tensor_height);
  input.values.resize(plane * 3U);
  for (std::size_t channel = 0; channel < 3; ++channel) {
    // The padding is white paper.
    const float white = (1.0F - kMean[channel]) / kDeviation[channel];
    std::fill_n(input.values.begin() + static_cast<std::ptrdiff_t>(channel * plane), plane, white);
    float* out = input.values.data() + channel * plane;
    for (std::int32_t y = 0; y < input.scaled_height; ++y) {
      const std::uint8_t* row =
          scaled.pixels.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(input.scaled_width) * 4U;
      float* target = out + static_cast<std::size_t>(y) * static_cast<std::size_t>(input.tensor_width);
      for (std::int32_t x = 0; x < input.scaled_width; ++x) {
        target[x] = (static_cast<float>(row[static_cast<std::size_t>(x) * 4U + channel]) / 255.0F - kMean[channel]) /
                    kDeviation[channel];
      }
    }
  }
  return input;
}

std::vector<DetectedText> detected_text(std::span<const float> probability, std::int32_t width, std::int32_t height,
                                        double scale_x, double scale_y, const TextDetectSettings& settings) {
  if (width <= 0 || height <= 0 ||
      probability.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) {
    throw std::invalid_argument("text recognition: the detector's output has an unexpected size");
  }
  std::vector<DetectedText> found;
  std::vector<std::uint8_t> visited(probability.size(), 0);
  std::vector<std::int32_t> stack;
  std::vector<std::int32_t> row_first;
  std::vector<std::int32_t> row_last;
  std::vector<IntPoint> edge_points;
  const auto inside = [&](std::size_t index) { return probability[index] > settings.threshold; };

  for (std::int32_t start_y = 0; start_y < height && found.size() < settings.max_boxes; ++start_y) {
    for (std::int32_t start_x = 0; start_x < width && found.size() < settings.max_boxes; ++start_x) {
      const auto start = static_cast<std::size_t>(start_y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(start_x);
      if (visited[start] != 0 || !inside(start)) {
        continue;
      }
      // One region: every pixel above the threshold reachable through its eight neighbours.
      // Only its outline per row matters to the rectangle; the sum is for its score.
      std::int32_t top = start_y;
      std::int32_t bottom = start_y;
      row_first.assign(1, start_x);
      row_last.assign(1, start_x);
      double sum = 0.0;
      std::size_t count = 0;
      stack.assign(1, static_cast<std::int32_t>(start));
      visited[start] = 1;
      while (!stack.empty()) {
        const std::int32_t index = stack.back();
        stack.pop_back();
        const std::int32_t x = index % width;
        const std::int32_t y = index / width;
        sum += probability[static_cast<std::size_t>(index)];
        ++count;
        // Rows are met from start_y downward and, through diagonal steps, never above it:
        // the scan reaches a region at its topmost row first.
        while (y > bottom) {
          ++bottom;
          row_first.push_back(std::numeric_limits<std::int32_t>::max());
          row_last.push_back(std::numeric_limits<std::int32_t>::min());
        }
        const auto row = static_cast<std::size_t>(y - top);
        row_first[row] = std::min(row_first[row], x);
        row_last[row] = std::max(row_last[row], x);
        for (std::int32_t ny = std::max(start_y, y - 1); ny <= std::min(height - 1, y + 1); ++ny) {
          for (std::int32_t nx = std::max(0, x - 1); nx <= std::min(width - 1, x + 1); ++nx) {
            const auto neighbour = static_cast<std::size_t>(ny) * static_cast<std::size_t>(width) + static_cast<std::size_t>(nx);
            if (visited[neighbour] == 0 && inside(neighbour)) {
              visited[neighbour] = 1;
              stack.push_back(static_cast<std::int32_t>(neighbour));
            }
          }
        }
      }
      if (count < 9) {
        continue;
      }
      const double score = sum / static_cast<double>(count);
      if (score < settings.box_threshold) {
        continue;
      }
      // Each row's two end pixels are enough for the hull. Sorted by x then y for the chain.
      edge_points.clear();
      for (std::size_t row = 0; row < row_first.size(); ++row) {
        if (row_first[row] > row_last[row]) {
          continue;
        }
        const auto y = top + static_cast<std::int32_t>(row);
        edge_points.push_back({row_first[row], y});
        if (row_last[row] != row_first[row]) {
          edge_points.push_back({row_last[row], y});
        }
      }
      std::sort(edge_points.begin(), edge_points.end(), [](IntPoint a, IntPoint b) {
        return a.x != b.x ? a.x < b.x : a.y < b.y;
      });
      auto rectangle = smallest_rectangle(convex_hull(edge_points));
      // The points are pixel centres: half a pixel more on every side covers the pixels.
      rectangle.length += 1.0;
      rectangle.thickness += 1.0;
      rectangle.centre_x += 0.5;
      rectangle.centre_y += 0.5;
      if (std::min(rectangle.length, rectangle.thickness) < settings.min_side) {
        continue;
      }
      const double grow =
          rectangle.length * rectangle.thickness * settings.unclip_ratio / (2.0 * (rectangle.length + rectangle.thickness));
      rectangle.length += 2.0 * grow;
      rectangle.thickness += 2.0 * grow;

      // The frame closest to upright: its first axis points right, within 45 degrees of level.
      double ux = rectangle.axis_x;
      double uy = rectangle.axis_y;
      double along = rectangle.length;
      double across = rectangle.thickness;
      if (std::abs(uy) > std::abs(ux)) {
        std::swap(along, across);
        const double turned_x = -uy;
        uy = ux;
        ux = turned_x;
      }
      if (ux < 0.0 || (ux == 0.0 && uy < 0.0)) {
        ux = -ux;
        uy = -uy;
      }
      // Exactly 45 degrees belongs to the upper end of the range.
      const double vx = -uy;
      const double vy = ux;
      const double half_along = along / 2.0;
      const double half_across = across / 2.0;
      const auto corner = [&](double a, double b) {
        return Point{(rectangle.centre_x + a * ux + b * vx) * scale_x, (rectangle.centre_y + a * uy + b * vy) * scale_y};
      };
      DetectedText text;
      text.quad = {corner(-half_along, -half_across), corner(half_along, -half_across), corner(half_along, half_across),
                   corner(-half_along, half_across)};
      // The angle is read off the scaled quad: the two scales may differ a little.
      text.angle_degrees = std::atan2(text.quad[1].y - text.quad[0].y, text.quad[1].x - text.quad[0].x) * 180.0 /
                           3.14159265358979323846;
      text.score = score;
      found.push_back(text);
    }
  }
  return found;
}

}  // namespace patchy::ocr
