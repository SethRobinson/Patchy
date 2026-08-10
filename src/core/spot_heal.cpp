#include "core/spot_heal.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace patchy {

// The mirror geometry is deliberately a fixed local operation derived from the
// footprint mask alone (no patch search, no synthesis, no gradient-domain
// solve, no content-driven selection) - see the header and
// docs/legal-constraints.md. The nearest-point transform is the standard
// two-pass eight-neighbor propagation with integer squared distances, a fixed
// scan order, and strict-less-than ties, so results are identical across
// toolchains (AGENTS.md determinism rule; the only floating point is an IEEE
// sqrt/llround of integer inputs).
SpotHealSourceField spot_heal_mirror_sources(const std::uint8_t* mask, Rect bounds,
                                             std::int32_t canvas_width, std::int32_t canvas_height,
                                             std::int32_t margin) {
  SpotHealSourceField field;
  field.bounds = bounds;
  if (mask == nullptr || bounds.width <= 0 || bounds.height <= 0 || canvas_width <= 0 ||
      canvas_height <= 0) {
    return field;
  }

  const auto width = bounds.width;
  const auto height = bounds.height;
  const auto count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  constexpr auto kInfinite = std::numeric_limits<std::int64_t>::max();

  // Nearest outside pixel per covered pixel, in bounds-local coordinates.
  std::vector<std::int32_t> nearest_x(count);
  std::vector<std::int32_t> nearest_y(count);
  std::vector<std::int64_t> distance(count);
  bool has_outside = false;
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      const auto index = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                         static_cast<std::size_t>(x);
      if (mask[index] == 0U) {
        nearest_x[index] = x;
        nearest_y[index] = y;
        distance[index] = 0;
        has_outside = true;
      } else {
        distance[index] = kInfinite;
      }
    }
  }
  if (!has_outside) {
    return field;
  }

  const auto relax = [&](std::size_t index, std::int32_t x, std::int32_t y, std::size_t from) {
    if (distance[from] == kInfinite) {
      return;
    }
    const auto dx = static_cast<std::int64_t>(x) - nearest_x[from];
    const auto dy = static_cast<std::int64_t>(y) - nearest_y[from];
    const auto candidate = dx * dx + dy * dy;
    if (candidate < distance[index]) {
      distance[index] = candidate;
      nearest_x[index] = nearest_x[from];
      nearest_y[index] = nearest_y[from];
    }
  };

  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      const auto index = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                         static_cast<std::size_t>(x);
      if (distance[index] == 0) {
        continue;
      }
      if (x > 0) {
        relax(index, x, y, index - 1U);
      }
      if (y > 0) {
        const auto up = index - static_cast<std::size_t>(width);
        if (x > 0) {
          relax(index, x, y, up - 1U);
        }
        relax(index, x, y, up);
        if (x + 1 < width) {
          relax(index, x, y, up + 1U);
        }
      }
    }
  }
  for (std::int32_t y = height - 1; y >= 0; --y) {
    for (std::int32_t x = width - 1; x >= 0; --x) {
      const auto index = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                         static_cast<std::size_t>(x);
      if (distance[index] == 0) {
        continue;
      }
      if (x + 1 < width) {
        relax(index, x, y, index + 1U);
      }
      if (y + 1 < height) {
        const auto down = index + static_cast<std::size_t>(width);
        if (x + 1 < width) {
          relax(index, x, y, down + 1U);
        }
        relax(index, x, y, down);
        if (x > 0) {
          relax(index, x, y, down - 1U);
        }
      }
    }
  }

  const auto clamp_x = [&](std::int64_t x) {
    return static_cast<std::int32_t>(std::clamp<std::int64_t>(x, 0, canvas_width - 1));
  };
  const auto clamp_y = [&](std::int64_t y) {
    return static_cast<std::int32_t>(std::clamp<std::int64_t>(y, 0, canvas_height - 1));
  };
  // Covered = inside the footprint. Outside `bounds` is uncovered by
  // construction: bounds wraps the stamped footprint (padded), so any
  // in-canvas point beyond it has zero coverage.
  const auto covered = [&](std::int32_t doc_x, std::int32_t doc_y) {
    if (!bounds.contains(doc_x, doc_y)) {
      return false;
    }
    const auto index =
        static_cast<std::size_t>(doc_y - bounds.y) * static_cast<std::size_t>(width) +
        static_cast<std::size_t>(doc_x - bounds.x);
    return mask[index] != 0U;
  };

  field.source_x.resize(count);
  field.source_y.resize(count);
  field.rim_x.resize(count);
  field.rim_y.resize(count);
  const auto diagonal = std::sqrt(static_cast<double>(width) * width +
                                  static_cast<double>(height) * height);
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      const auto index = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                         static_cast<std::size_t>(x);
      const auto doc_x = bounds.x + x;
      const auto doc_y = bounds.y + y;
      if (mask[index] == 0U) {
        field.source_x[index] = doc_x;
        field.source_y[index] = doc_y;
        field.rim_x[index] = doc_x;
        field.rim_y[index] = doc_y;
        continue;
      }

      const auto vx = static_cast<std::int64_t>(nearest_x[index]) - x;
      const auto vy = static_cast<std::int64_t>(nearest_y[index]) - y;
      const auto length = std::sqrt(static_cast<double>(vx * vx + vy * vy));
      const auto push_x = std::llround(static_cast<double>(margin) * static_cast<double>(vx) / length);
      const auto push_y = std::llround(static_cast<double>(margin) * static_cast<double>(vy) / length);

      // Known-good tone point just past the boundary nearest this pixel.
      const auto boundary_doc_x = static_cast<std::int64_t>(bounds.x) + nearest_x[index];
      const auto boundary_doc_y = static_cast<std::int64_t>(bounds.y) + nearest_y[index];
      auto rim_x = clamp_x(boundary_doc_x + push_x);
      auto rim_y = clamp_y(boundary_doc_y + push_y);
      if (covered(rim_x, rim_y)) {
        rim_x = static_cast<std::int32_t>(boundary_doc_x);
        rim_y = static_cast<std::int32_t>(boundary_doc_y);
      }

      // Mirror across the boundary: land |v| + margin past the rim along the
      // local outward normal. Concave footprints can re-enter the mask, so
      // step onward in whole-v increments (bounded by the bounds diagonal)
      // and fall back to the rim point when nothing uncovered is found.
      auto source_x = clamp_x(static_cast<std::int64_t>(doc_x) + 2 * vx + push_x);
      auto source_y = clamp_y(static_cast<std::int64_t>(doc_y) + 2 * vy + push_y);
      const auto max_steps = static_cast<int>(std::ceil(diagonal / length)) + 3;
      for (int step = 0; step < max_steps && covered(source_x, source_y); ++step) {
        const auto next_x = clamp_x(static_cast<std::int64_t>(source_x) + vx);
        const auto next_y = clamp_y(static_cast<std::int64_t>(source_y) + vy);
        if (next_x == source_x && next_y == source_y) {
          break;
        }
        source_x = next_x;
        source_y = next_y;
      }
      if (covered(source_x, source_y)) {
        source_x = rim_x;
        source_y = rim_y;
      }
      field.source_x[index] = source_x;
      field.source_y[index] = source_y;
      field.rim_x[index] = rim_x;
      field.rim_y[index] = rim_y;
    }
  }
  return field;
}

}  // namespace patchy
