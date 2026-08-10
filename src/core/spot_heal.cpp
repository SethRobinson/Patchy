#include "core/spot_heal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

namespace patchy {

// The mapping is deliberately a fixed rigid operation derived from the
// footprint mask alone (no patch search, no synthesis, no content-driven
// selection) - see the header and docs/legal-constraints.md. Candidate
// directions are examined in a fixed order and scored ONLY by mask-geometry
// validity (how many covered cells would map back into the footprint or off
// the canvas), so results are identical across toolchains: integer sums, an
// IEEE sqrt/llround envelope, strict-less-than tie-breaks.

std::pair<std::int32_t, std::int32_t> SpotHealSourceMap::map(std::int32_t x, std::int32_t y) const {
  const auto px = static_cast<double>(x);
  const auto py = static_cast<double>(y);
  double sx = px;
  double sy = py;
  if (mirrored) {
    const auto distance = (anchor_x - px) * direction_x + (anchor_y - py) * direction_y;
    sx = px + 2.0 * distance * direction_x;
    sy = py + 2.0 * distance * direction_y;
  } else {
    sx = px + shift * direction_x;
    sy = py + shift * direction_y;
  }
  return {static_cast<std::int32_t>(std::llround(sx)), static_cast<std::int32_t>(std::llround(sy))};
}

SpotHealSourceMap spot_heal_source_map(const std::uint8_t* mask, Rect bounds, std::int32_t canvas_width,
                                       std::int32_t canvas_height, std::int32_t margin) {
  SpotHealSourceMap result;
  if (mask == nullptr || bounds.width <= 0 || bounds.height <= 0 || canvas_width <= 0 ||
      canvas_height <= 0) {
    return result;
  }
  const auto width = bounds.width;
  const auto height = bounds.height;

  // Integer centroid of the covered cells (document space, floor rounding).
  std::int64_t sum_x = 0;
  std::int64_t sum_y = 0;
  std::int64_t covered = 0;
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      if (mask[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
               static_cast<std::size_t>(x)] != 0U) {
        sum_x += bounds.x + x;
        sum_y += bounds.y + y;
        ++covered;
      }
    }
  }
  if (covered == 0) {
    return result;
  }
  const auto centroid_x = static_cast<double>(sum_x) / static_cast<double>(covered);
  const auto centroid_y = static_cast<double>(sum_y) / static_cast<double>(covered);

  const auto covered_at = [&](std::int32_t doc_x, std::int32_t doc_y) {
    if (!bounds.contains(doc_x, doc_y)) {
      return false;
    }
    return mask[static_cast<std::size_t>(doc_y - bounds.y) * static_cast<std::size_t>(width) +
                static_cast<std::size_t>(doc_x - bounds.x)] != 0U;
  };

  // March from the centroid along a direction until the footprint ends; the
  // crossing point anchors the reflection line for that direction.
  const auto boundary_distance = [&](double ux, double uy) {
    const auto limit = static_cast<double>(width + height);
    double distance = 0.0;
    while (distance <= limit) {
      const auto x = static_cast<std::int32_t>(std::llround(centroid_x + ux * distance));
      const auto y = static_cast<std::int32_t>(std::llround(centroid_y + uy * distance));
      if (!covered_at(x, y)) {
        return distance;
      }
      distance += 1.0;
    }
    return limit;
  };

  // Primary direction: from the centroid toward its nearest uncovered cell
  // (mask geometry only). Scan every uncovered cell in the padded bounds and
  // keep the closest; fixed scan order breaks ties.
  double best_dx = 1.0;
  double best_dy = 0.0;
  auto best_distance_squared = std::numeric_limits<double>::max();
  bool found_outside = false;
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      if (mask[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
               static_cast<std::size_t>(x)] != 0U) {
        continue;
      }
      const auto doc_x = static_cast<double>(bounds.x + x);
      const auto doc_y = static_cast<double>(bounds.y + y);
      const auto dx = doc_x - centroid_x;
      const auto dy = doc_y - centroid_y;
      const auto distance_squared = dx * dx + dy * dy;
      if (distance_squared < best_distance_squared && distance_squared > 0.0) {
        best_distance_squared = distance_squared;
        best_dx = dx;
        best_dy = dy;
        found_outside = true;
      }
    }
  }
  if (!found_outside) {
    return result;
  }
  {
    const auto length = std::sqrt(best_dx * best_dx + best_dy * best_dy);
    best_dx /= length;
    best_dy /= length;
  }

  // Fixed candidate order: nearest-boundary direction, its opposite, the two
  // perpendiculars. For each, a reflection first, then a translation. Score =
  // covered cells whose mapped source lands off-canvas or back inside the
  // footprint; first candidate with zero violations wins, else the fewest.
  const std::array<std::array<double, 2>, 4> directions{{
      {{best_dx, best_dy}},
      {{-best_dx, -best_dy}},
      {{-best_dy, best_dx}},
      {{best_dy, -best_dx}},
  }};

  const auto violations_for = [&](const SpotHealSourceMap& candidate) {
    std::int64_t violations = 0;
    for (std::int32_t y = 0; y < height; ++y) {
      for (std::int32_t x = 0; x < width; ++x) {
        if (mask[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                 static_cast<std::size_t>(x)] == 0U) {
          continue;
        }
        const auto [sx, sy] = candidate.map(bounds.x + x, bounds.y + y);
        if (sx < 0 || sy < 0 || sx >= canvas_width || sy >= canvas_height || covered_at(sx, sy)) {
          ++violations;
        }
      }
    }
    return violations;
  };

  SpotHealSourceMap best;
  auto best_violations = std::numeric_limits<std::int64_t>::max();
  for (const auto& direction : directions) {
    const auto ux = direction[0];
    const auto uy = direction[1];
    const auto rim = boundary_distance(ux, uy);

    SpotHealSourceMap mirrored;
    mirrored.valid = true;
    mirrored.mirrored = true;
    mirrored.direction_x = ux;
    mirrored.direction_y = uy;
    mirrored.anchor_x = centroid_x + ux * (rim + static_cast<double>(margin));
    mirrored.anchor_y = centroid_y + uy * (rim + static_cast<double>(margin));
    const auto mirrored_violations = violations_for(mirrored);
    if (mirrored_violations < best_violations) {
      best_violations = mirrored_violations;
      best = mirrored;
    }
    if (best_violations == 0) {
      return best;
    }

    // Translation fallback: push the whole footprint clear of its own extent
    // along the direction.
    double max_extent = 0.0;
    for (std::int32_t y = 0; y < height; ++y) {
      for (std::int32_t x = 0; x < width; ++x) {
        if (mask[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                 static_cast<std::size_t>(x)] == 0U) {
          continue;
        }
        const auto along = (static_cast<double>(bounds.x + x) - centroid_x) * ux +
                           (static_cast<double>(bounds.y + y) - centroid_y) * uy;
        max_extent = std::max(max_extent, std::abs(along));
      }
    }
    SpotHealSourceMap translated;
    translated.valid = true;
    translated.mirrored = false;
    translated.direction_x = ux;
    translated.direction_y = uy;
    translated.shift = 2.0 * max_extent + static_cast<double>(margin) + 1.0;
    const auto translated_violations = violations_for(translated);
    if (translated_violations < best_violations) {
      best_violations = translated_violations;
      best = translated;
    }
    if (best_violations == 0) {
      return best;
    }
  }
  return best;
}

}  // namespace patchy
