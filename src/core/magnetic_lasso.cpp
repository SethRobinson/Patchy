#include "core/magnetic_lasso.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace patchy {
namespace {

// 8-connected step table. Order is load-bearing for determinism: neighbors are relaxed in
// this order, and within a Dial bucket nodes settle in insertion order.
struct Step {
  std::int32_t dx;
  std::int32_t dy;
  std::uint32_t dist;  // fixed-point step length: 5 straight, 7 diagonal (~5 * sqrt 2)
};
constexpr Step kSteps[8] = {
    {0, -1, 5}, {-1, 0, 5}, {1, 0, 5}, {0, 1, 5},
    {-1, -1, 7}, {1, -1, 7}, {-1, 1, 7}, {1, 1, 7},
};

constexpr std::uint8_t kParentNone = 0xFF;
constexpr std::uint8_t kParentAnchor = 0xFE;

// Transparency preference: an anti-aliased edge against transparency carries its gradient in
// the alpha channel across a several-pixel ramp, and the luma zero-crossing term is blind
// there (black art on transparent ground has flat luma), so without a tiebreaker the wire
// settles on the translucent fringe. Photoshop hugs the opaque pixels instead. The weight must
// dominate the gradient spread across the ramp (up to ~4*100) to pull the wire to the
// high-alpha side; it is zero everywhere on fully opaque images.
constexpr std::uint32_t kAlphaPenaltyWeight = 1400U;

// base(q) = 1 + 4*(255 - G8'(q)) + (zero-crossing ? 0 : 255) + alpha penalty: strong on-edge
// pixels cost ~1, flat pixels cost the uniform maximum, so flat-region shortest paths are
// straight lines.
constexpr std::uint32_t kMaxNodeBase = 1U + 4U * 255U + 255U + kAlphaPenaltyWeight;
constexpr std::uint32_t kMaxLinkCost = kMaxNodeBase * 7U;
constexpr std::uint32_t kBucketCount = kMaxLinkCost + 1U;

constexpr std::uint64_t kUnreached = ~std::uint64_t{0};

[[nodiscard]] std::vector<PointI32> bresenham_line(PointI32 from, PointI32 to) {
  std::vector<PointI32> line;
  const auto dx = std::abs(to.x - from.x);
  const auto dy = std::abs(to.y - from.y);
  line.reserve(static_cast<std::size_t>(std::max(dx, dy)) + 1U);
  const std::int32_t sx = from.x < to.x ? 1 : -1;
  const std::int32_t sy = from.y < to.y ? 1 : -1;
  std::int32_t error = dx - dy;
  auto point = from;
  while (true) {
    line.push_back(point);
    if (point == to) {
      break;
    }
    const auto doubled = 2 * error;
    if (doubled > -dy) {
      error -= dy;
      point.x += sx;
    }
    if (doubled < dx) {
      error += dx;
      point.y += sy;
    }
  }
  return line;
}

}  // namespace

void LiveWireEngine::set_image(const std::uint8_t* rgba, std::int32_t width, std::int32_t height,
                               std::ptrdiff_t stride_bytes) {
  rgba_ = rgba;
  image_width_ = width;
  image_height_ = height;
  stride_ = stride_bytes;
  field_valid_ = false;
}

void LiveWireEngine::set_params(const MagneticLassoParams& params) {
  params_ = params;
  params_.width = std::clamp(params_.width, 1, 256);
  params_.edge_contrast = std::clamp(params_.edge_contrast, 1, 100);
  params_.node_budget = std::max(params_.node_budget, 1024);
  field_valid_ = false;
}

void LiveWireEngine::set_anchor(PointI32 anchor) {
  const auto clamped = clamp_to_image(anchor);
  if (field_valid_ && clamped == anchor_) {
    return;
  }
  anchor_ = clamped;
  field_valid_ = false;
}

PointI32 LiveWireEngine::clamp_to_image(PointI32 p) const noexcept {
  return {std::clamp(p.x, std::int32_t{0}, std::max(std::int32_t{0}, image_width_ - 1)),
          std::clamp(p.y, std::int32_t{0}, std::max(std::int32_t{0}, image_height_ - 1))};
}

std::int32_t LiveWireEngine::luma(std::int32_t x, std::int32_t y) const noexcept {
  x = std::clamp(x, std::int32_t{0}, image_width_ - 1);
  y = std::clamp(y, std::int32_t{0}, image_height_ - 1);
  const auto* px = rgba_ + static_cast<std::ptrdiff_t>(y) * stride_ + static_cast<std::ptrdiff_t>(x) * 4;
  return (299 * px[0] + 587 * px[1] + 114 * px[2]) / 1000;
}

std::uint8_t LiveWireEngine::gradient_g8(std::int32_t x, std::int32_t y) const noexcept {
  // 3x3 Sobel per channel (R, G, B, A), L1 magnitude, channel max. Border rows/columns
  // replicate, which makes the canvas edge itself read as a strong edge - desirable, traces
  // can hug the document border. Fixed absolute scale: a full 0-255 step edge saturates.
  const auto sample = [this](std::int32_t sx, std::int32_t sy) {
    sx = std::clamp(sx, std::int32_t{0}, image_width_ - 1);
    sy = std::clamp(sy, std::int32_t{0}, image_height_ - 1);
    return rgba_ + static_cast<std::ptrdiff_t>(sy) * stride_ + static_cast<std::ptrdiff_t>(sx) * 4;
  };
  const std::uint8_t* rows[3][3];
  for (std::int32_t j = 0; j < 3; ++j) {
    for (std::int32_t i = 0; i < 3; ++i) {
      rows[j][i] = sample(x + i - 1, y + j - 1);
    }
  }
  std::int32_t best = 0;
  for (std::int32_t c = 0; c < 4; ++c) {
    const std::int32_t gx = (rows[0][2][c] + 2 * rows[1][2][c] + rows[2][2][c]) -
                            (rows[0][0][c] + 2 * rows[1][0][c] + rows[2][0][c]);
    const std::int32_t gy = (rows[2][0][c] + 2 * rows[2][1][c] + rows[2][2][c]) -
                            (rows[0][0][c] + 2 * rows[0][1][c] + rows[0][2][c]);
    best = std::max(best, std::abs(gx) + std::abs(gy));
  }
  return static_cast<std::uint8_t>(std::min(best / 4, 255));
}

PointI32 LiveWireEngine::snap(PointI32 cursor) const {
  if (rgba_ == nullptr || image_width_ <= 0 || image_height_ <= 0) {
    return cursor;
  }
  const auto center = clamp_to_image(cursor);
  const auto radius = std::max(1, params_.width / 2);
  const auto threshold = std::max(1, params_.edge_contrast * 255 / 100);
  auto best_point = center;
  std::int64_t best_score = -1;
  std::int64_t best_distance = 0;
  for (std::int32_t dy = -radius; dy <= radius; ++dy) {
    for (std::int32_t dx = -radius; dx <= radius; ++dx) {
      if (dx * dx + dy * dy > radius * radius) {
        continue;
      }
      const PointI32 p{center.x + dx, center.y + dy};
      if (p.x < 0 || p.y < 0 || p.x >= image_width_ || p.y >= image_height_) {
        continue;
      }
      const std::int32_t g = gradient_g8(p.x, p.y);
      if (g < threshold) {
        continue;
      }
      // Same preference the path cost uses: strong gradient, opaque side of an alpha ramp.
      // Lower is better; ties break toward the cursor, then scanline order (the dy/dx loops
      // run in increasing y then x, so "first seen" is deterministic).
      const auto alpha = rgba_[static_cast<std::ptrdiff_t>(p.y) * stride_ +
                               static_cast<std::ptrdiff_t>(p.x) * 4 + 3];
      const std::int64_t score = 4 * (255 - g) +
                                 static_cast<std::int64_t>((255 - alpha) * kAlphaPenaltyWeight) / 255;
      const std::int64_t distance = std::int64_t{dx} * dx + std::int64_t{dy} * dy;
      if (best_score < 0 || score < best_score ||
          (score == best_score && distance < best_distance)) {
        best_score = score;
        best_point = p;
        best_distance = distance;
      }
    }
  }
  return best_score >= 0 ? best_point : center;
}

bool LiveWireEngine::build_field(Window window) {
  const auto node_count = static_cast<std::size_t>(window.width) * static_cast<std::size_t>(window.height);
  if (node_count == 0 || node_count > static_cast<std::size_t>(params_.node_budget)) {
    return false;
  }
  window_ = window;
  node_base_.assign(node_count, 0);
  parent_.assign(node_count, kParentNone);
  distance_.assign(node_count, kUnreached);

  const auto threshold = std::max(1, params_.edge_contrast * 255 / 100);

  // Luma Laplacian over the window (one-pixel border sampled straight off the image) for the
  // zero-crossing term: it pins the path to the center line of an edge instead of one pixel
  // to either side.
  std::vector<std::int32_t> laplacian(node_count);
  for (std::int32_t y = 0; y < window.height; ++y) {
    for (std::int32_t x = 0; x < window.width; ++x) {
      const auto ix = window.x0 + x;
      const auto iy = window.y0 + y;
      laplacian[static_cast<std::size_t>(y) * window.width + x] =
          4 * luma(ix, iy) - luma(ix - 1, iy) - luma(ix + 1, iy) - luma(ix, iy - 1) - luma(ix, iy + 1);
    }
  }
  const auto laplacian_at = [&](std::int32_t x, std::int32_t y) {
    x = std::clamp(x, std::int32_t{0}, window.width - 1);
    y = std::clamp(y, std::int32_t{0}, window.height - 1);
    return laplacian[static_cast<std::size_t>(y) * window.width + x];
  };

  for (std::int32_t y = 0; y < window.height; ++y) {
    for (std::int32_t x = 0; x < window.width; ++x) {
      const auto index = static_cast<std::size_t>(y) * window.width + x;
      const std::int32_t g8 = gradient_g8(window.x0 + x, window.y0 + y);
      const std::int32_t gated = g8 >= threshold ? g8 : 0;
      const auto center = laplacian_at(x, y);
      bool zero_crossing = center == 0;
      if (!zero_crossing) {
        for (const auto [nx, ny] : {std::pair{x, y - 1}, std::pair{x - 1, y}, std::pair{x + 1, y},
                                    std::pair{x, y + 1}}) {
          const auto neighbor = laplacian_at(nx, ny);
          if ((neighbor < 0) != (center < 0) && std::abs(center) <= std::abs(neighbor)) {
            zero_crossing = true;
            break;
          }
        }
      }
      const auto alpha = rgba_[static_cast<std::ptrdiff_t>(window.y0 + y) * stride_ +
                               static_cast<std::ptrdiff_t>(window.x0 + x) * 4 + 3];
      const auto alpha_penalty =
          static_cast<std::uint32_t>((255 - alpha) * kAlphaPenaltyWeight) / 255U;
      node_base_[index] =
          static_cast<std::uint16_t>(1 + 4 * (255 - gated) + (zero_crossing ? 0 : 255) + alpha_penalty);
    }
  }

  solve_dijkstra();
  field_valid_ = true;
  return true;
}

void LiveWireEngine::solve_dijkstra() {
  // Dial's bucket queue: total path costs are bounded by node_count * kMaxLinkCost, far above
  // 32 bits on big windows, so distances are u64 and the bucket index is dist % kBucketCount
  // (valid because every link cost is <= kMaxLinkCost). Entries are lazily deleted: a popped
  // node whose recorded distance is smaller than the entry's is already settled.
  struct Entry {
    std::uint64_t distance;
    std::uint32_t node;
  };
  std::vector<std::vector<Entry>> buckets(kBucketCount);
  const auto anchor_index = static_cast<std::size_t>(anchor_.y - window_.y0) * window_.width +
                            (anchor_.x - window_.x0);
  distance_[anchor_index] = 0;
  parent_[anchor_index] = kParentAnchor;
  buckets[0].push_back({0, static_cast<std::uint32_t>(anchor_index)});
  std::size_t remaining = 1;
  std::uint64_t current = 0;
  while (remaining > 0) {
    auto& bucket = buckets[current % kBucketCount];
    // Settle in insertion order; relaxations appended to the current bucket (zero-cost links
    // do not exist since base >= 1) land in later buckets, so a plain index scan is safe.
    for (std::size_t i = 0; i < bucket.size(); ++i) {
      const auto entry = bucket[i];
      --remaining;
      if (entry.distance != distance_[entry.node]) {
        continue;  // stale
      }
      const auto node_x = static_cast<std::int32_t>(entry.node % static_cast<std::uint32_t>(window_.width));
      const auto node_y = static_cast<std::int32_t>(entry.node / static_cast<std::uint32_t>(window_.width));
      for (std::uint8_t direction = 0; direction < 8; ++direction) {
        const auto& step = kSteps[direction];
        const auto nx = node_x + step.dx;
        const auto ny = node_y + step.dy;
        if (nx < 0 || ny < 0 || nx >= window_.width || ny >= window_.height) {
          continue;
        }
        const auto neighbor = static_cast<std::size_t>(ny) * window_.width + nx;
        const auto link = static_cast<std::uint64_t>(node_base_[neighbor]) * step.dist;
        const auto candidate = entry.distance + link;
        if (candidate < distance_[neighbor]) {
          distance_[neighbor] = candidate;
          parent_[neighbor] = direction;
          buckets[candidate % kBucketCount].push_back({candidate, static_cast<std::uint32_t>(neighbor)});
          ++remaining;
        }
      }
    }
    bucket.clear();
    ++current;
  }
}

std::vector<PointI32> LiveWireEngine::path_to(PointI32 target) {
  if (rgba_ == nullptr || image_width_ <= 0 || image_height_ <= 0) {
    return {anchor_, target};
  }
  const auto clamped = clamp_to_image(target);
  if (clamped == anchor_) {
    return {anchor_};
  }
  if (!field_valid_ || !window_.contains(clamped)) {
    // Initial window: square around the anchor sized to the edge-search width; regrow to the
    // anchor/target bounding box when the cursor escapes it.
    auto radius = std::max(std::int32_t{128}, 4 * params_.width);
    while ((2 * radius + 1) * (2 * radius + 1) > params_.node_budget && radius > 16) {
      --radius;
    }
    Window window{};
    if (std::abs(clamped.x - anchor_.x) <= radius && std::abs(clamped.y - anchor_.y) <= radius) {
      window.x0 = std::max(std::int32_t{0}, anchor_.x - radius);
      window.y0 = std::max(std::int32_t{0}, anchor_.y - radius);
      window.width = std::min(image_width_, anchor_.x + radius + 1) - window.x0;
      window.height = std::min(image_height_, anchor_.y + radius + 1) - window.y0;
    } else {
      const auto pad = std::max(std::int32_t{32}, params_.width);
      window.x0 = std::max(std::int32_t{0}, std::min(anchor_.x, clamped.x) - pad);
      window.y0 = std::max(std::int32_t{0}, std::min(anchor_.y, clamped.y) - pad);
      window.width = std::min(image_width_, std::max(anchor_.x, clamped.x) + pad + 1) - window.x0;
      window.height = std::min(image_height_, std::max(anchor_.y, clamped.y) + pad + 1) - window.y0;
    }
    if (!window.contains(clamped) || !window.contains(anchor_) || !build_field(window)) {
      field_valid_ = false;
      window_ = {};
      return bresenham_line(anchor_, clamped);
    }
  }
  const auto index_of = [this](PointI32 p) {
    return static_cast<std::size_t>(p.y - window_.y0) * window_.width + (p.x - window_.x0);
  };
  if (distance_[index_of(clamped)] == kUnreached) {
    return bresenham_line(anchor_, clamped);
  }
  std::vector<PointI32> path;
  auto point = clamped;
  while (true) {
    path.push_back(point);
    const auto parent = parent_[index_of(point)];
    if (parent == kParentAnchor) {
      break;
    }
    const auto& step = kSteps[parent];
    point = {point.x - step.dx, point.y - step.dy};
  }
  std::reverse(path.begin(), path.end());

  // In a gated-flat region every monotone staircase costs the same under the 5/7 metric, so
  // the tie-broken Dijkstra tree can hand back an elbow. When no point of the path reaches
  // the edge-contrast threshold the user is tracing featureless ground - give them the line
  // they see the cursor draw.
  const auto threshold = std::max(1, params_.edge_contrast * 255 / 100);
  const auto on_edge = [this, threshold](const PointI32& p) {
    return gradient_g8(p.x, p.y) >= threshold;
  };
  if (std::none_of(path.begin(), path.end(), on_edge)) {
    return bresenham_line(anchor_, clamped);
  }
  return path;
}

}  // namespace patchy
