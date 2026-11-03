#include "core/selection_modify.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <vector>

namespace patchy {
namespace {

// Sum of one coverage row over [x - radius, x + radius] (clipped to the row) for every x.
void horizontal_window_sums(std::span<const std::uint8_t> row, int radius, std::vector<std::uint32_t>& prefix,
                            std::vector<std::uint32_t>& sums) {
  const auto width = static_cast<int>(row.size());
  prefix[0] = 0;
  for (int x = 0; x < width; ++x) {
    prefix[static_cast<std::size_t>(x) + 1U] = prefix[static_cast<std::size_t>(x)] + row[static_cast<std::size_t>(x)];
  }
  for (int x = 0; x < width; ++x) {
    const auto right = std::min(width, x + radius + 1);
    const auto left = std::max(0, x - radius);
    sums[static_cast<std::size_t>(x)] = prefix[static_cast<std::size_t>(right)] - prefix[static_cast<std::size_t>(left)];
  }
}

// One fixed-point extended box pass: taps within `whole` count 256, the two taps just outside
// count `edge_weight` (0..255). Outside [0, size) is zero.
struct BoxWeights {
  int whole{0};
  std::int64_t edge_weight{0};
  std::int64_t denominator{1};
};

BoxWeights box_weights(double pass_radius) {
  BoxWeights weights;
  weights.whole = static_cast<int>(std::floor(pass_radius));
  weights.edge_weight = std::llround((pass_radius - weights.whole) * 256.0);
  if (weights.edge_weight >= 256) {
    weights.whole += 1;
    weights.edge_weight = 0;
  }
  weights.denominator = 256LL * (2LL * weights.whole + 1LL) + 2LL * weights.edge_weight;
  return weights;
}

std::uint8_t box_value(std::int64_t inner, std::int64_t edges, const BoxWeights& weights) {
  const auto total = 256LL * inner + weights.edge_weight * edges;
  return static_cast<std::uint8_t>(std::min<std::int64_t>(255, (total + weights.denominator / 2) / weights.denominator));
}

void horizontal_box_pass(const PixelBuffer& source, PixelBuffer& destination, const BoxWeights& weights) {
  const auto width = source.width();
  const auto k = weights.whole;
  for (std::int32_t y = 0; y < source.height(); ++y) {
    const auto src = source.row(y);
    auto dst = destination.row(y);
    const auto at = [&](std::int32_t x) -> std::int64_t {
      return x >= 0 && x < width ? src[static_cast<std::size_t>(x)] : 0;
    };
    std::int64_t inner = 0;
    for (std::int32_t x = 0; x <= std::min(width - 1, k); ++x) {
      inner += src[static_cast<std::size_t>(x)];
    }
    for (std::int32_t x = 0; x < width; ++x) {
      dst[static_cast<std::size_t>(x)] = box_value(inner, at(x - k - 1) + at(x + k + 1), weights);
      inner += at(x + k + 1) - at(x - k);
    }
  }
}

// Vertical pass walked row by row with one running sum per column (cache friendly).
void vertical_box_pass(const PixelBuffer& source, PixelBuffer& destination, const BoxWeights& weights) {
  const auto width = static_cast<std::size_t>(source.width());
  const auto height = source.height();
  const auto k = weights.whole;
  std::vector<std::int64_t> inner(width, 0);
  for (std::int32_t y = 0; y <= std::min(height - 1, k); ++y) {
    const auto row = source.row(y);
    for (std::size_t x = 0; x < width; ++x) {
      inner[x] += row[x];
    }
  }
  for (std::int32_t y = 0; y < height; ++y) {
    const auto above = y - k - 1 >= 0 ? source.row(y - k - 1) : std::span<const std::uint8_t>{};
    const auto below = y + k + 1 < height ? source.row(y + k + 1) : std::span<const std::uint8_t>{};
    const auto leaving = y - k >= 0 ? source.row(y - k) : std::span<const std::uint8_t>{};
    auto dst = destination.row(y);
    for (std::size_t x = 0; x < width; ++x) {
      const std::int64_t up = above.empty() ? 0 : above[x];
      const std::int64_t down = below.empty() ? 0 : below[x];
      dst[x] = box_value(inner[x], up + down, weights);
      inner[x] += down - (leaving.empty() ? 0 : leaving[x]);
    }
  }
}

}  // namespace

PixelBuffer smooth_selection_mask(const PixelBuffer& coverage, Rect area, std::int32_t canvas_width,
                                  std::int32_t canvas_height, int radius) {
  const auto width = coverage.width();
  const auto height = coverage.height();
  PixelBuffer result(width, height, PixelFormat::gray8());
  result.clear(0);
  if (width <= 0 || height <= 0 || coverage.format() != PixelFormat::gray8() || area.width != width ||
      area.height != height) {
    return result;
  }
  radius = std::clamp(radius, kMinSelectionSmoothRadius, kMaxSelectionSmoothRadius);
  // Window counts per column and per row, clipped to the canvas.
  std::vector<std::int64_t> column_counts(static_cast<std::size_t>(width));
  for (std::int32_t x = 0; x < width; ++x) {
    const auto document_x = area.x + x;
    column_counts[static_cast<std::size_t>(x)] =
        std::min(canvas_width - 1, document_x + radius) - std::max(0, document_x - radius) + 1;
  }
  std::vector<std::uint32_t> prefix(static_cast<std::size_t>(width) + 1U);
  std::vector<std::uint32_t> row_sums(static_cast<std::size_t>(width));
  // (2r + 1)^2 * 255 stays below 2^32 at r = 500.
  std::vector<std::uint32_t> window_sums(static_cast<std::size_t>(width), 0U);
  const auto add_row = [&](std::int32_t y, bool add) {
    horizontal_window_sums(coverage.row(y), radius, prefix, row_sums);
    for (std::size_t x = 0; x < row_sums.size(); ++x) {
      window_sums[x] = add ? window_sums[x] + row_sums[x] : window_sums[x] - row_sums[x];
    }
  };
  for (std::int32_t y = 0; y <= std::min(height - 1, radius); ++y) {
    add_row(y, true);
  }
  const std::int64_t gain = 2LL * radius + 1LL;
  for (std::int32_t y = 0; y < height; ++y) {
    const auto document_y = area.y + y;
    const std::int64_t row_count =
        std::min(canvas_height - 1, document_y + radius) - std::max(0, document_y - radius) + 1;
    auto out = result.row(y);
    for (std::int32_t x = 0; x < width; ++x) {
      const auto count = std::max<std::int64_t>(1, row_count * column_counts[static_cast<std::size_t>(x)]);
      const std::int64_t sum = window_sums[static_cast<std::size_t>(x)];
      // 127.5 + gain * (mean - 127.5), in units of 1 / (2 * count), rounded half up.
      const auto twice = 255LL * count + gain * (2LL * sum - 255LL * count);
      const auto value = twice <= 0 ? 0LL : (twice + count) / (2LL * count);
      out[static_cast<std::size_t>(x)] = static_cast<std::uint8_t>(std::min<std::int64_t>(255, value));
    }
    if (y + radius + 1 < height) {
      add_row(y + radius + 1, true);
    }
    if (y - radius >= 0) {
      add_row(y - radius, false);
    }
  }
  return result;
}

double selection_feather_pass_radius(double radius) {
  if (!(radius > 0.0)) {
    return 0.0;
  }
  radius = std::min(radius, kMaxSelectionFeatherCommandRadius);
  return radius < 1.0 ? radius : (radius + 1.0) * 0.5;
}

int selection_feather_padding(double radius) {
  const auto pass_radius = selection_feather_pass_radius(radius);
  if (pass_radius <= 0.0) {
    return 0;
  }
  return (static_cast<int>(std::floor(pass_radius)) + 1) * 3 + 1;
}

void feather_selection_mask(PixelBuffer& coverage, double radius) {
  const auto pass_radius = selection_feather_pass_radius(radius);
  if (pass_radius <= 0.0 || coverage.empty() || coverage.format() != PixelFormat::gray8()) {
    return;
  }
  const auto weights = box_weights(pass_radius);
  if (weights.whole == 0 && weights.edge_weight == 0) {
    return;
  }
  PixelBuffer scratch(coverage.width(), coverage.height(), PixelFormat::gray8());
  for (int pass = 0; pass < 3; ++pass) {
    horizontal_box_pass(coverage, scratch, weights);
    vertical_box_pass(scratch, coverage, weights);
  }
}

}  // namespace patchy
