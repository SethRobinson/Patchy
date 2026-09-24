#include "core/exemplar_inpaint.hpp"

#include "core/heal_membrane.hpp"
#include "core/worker_budget.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <future>
#include <limits>
#include <utility>
#include <vector>

#if defined(_M_X64) || defined(__x86_64__) || defined(__SSE2__)
#define PATCHY_EXEMPLAR_SSE2 1
#include <emmintrin.h>
#elif defined(__aarch64__) || defined(_M_ARM64)
#define PATCHY_EXEMPLAR_NEON 1
#include <arm_neon.h>
#endif

namespace patchy {

// See the header for the legal boundary this file implements: an exhaustive,
// per-target, full-resolution scan with no offset field of any kind.

namespace {

struct Candidate {
  std::int64_t distance{std::numeric_limits<std::int64_t>::max()};
  std::int64_t index{std::numeric_limits<std::int64_t>::max()};  // row-major scan index; smaller wins ties
};

// Target rows are padded to whole 16-byte chunks; the mask is 0 in the padding
// and in unknown cells, so the source bytes there never count.
constexpr std::int32_t kChunk = 16;

// splitmix64 (Steele, Lea, Flood 2014): the variation pick's only source of
// randomness. The uniform mapping below is explicit multiply-high, never a
// standard distribution (implementation-defined output).
inline std::uint64_t splitmix64(std::uint64_t value) noexcept {
  value += 0x9E3779B97F4A7C15ULL;
  value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
  value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
  return value ^ (value >> 31U);
}

inline std::int64_t uniform_below(std::uint64_t draw, std::int64_t count) noexcept {
  // count fits 32 bits (a candidate window holds far fewer centres), so the
  // top 32 bits of the draw times count, shifted down, is uniform enough and
  // identical on every toolchain.
  return static_cast<std::int64_t>(((draw >> 32U) * static_cast<std::uint64_t>(count)) >> 32U);
}

// Sum over `chunks` 16-byte chunks of |(source & mask) - target|. The SIMD
// paths and the scalar path produce the same integer, so results are
// identical across toolchains (the determinism rule).
inline std::uint32_t masked_row_distance(const std::uint8_t* source, const std::uint8_t* target,
                                         const std::uint8_t* mask, std::int32_t chunks) {
#if defined(PATCHY_EXEMPLAR_SSE2)
  __m128i accumulator = _mm_setzero_si128();
  for (std::int32_t chunk = 0; chunk < chunks; ++chunk) {
    const auto s = _mm_loadu_si128(reinterpret_cast<const __m128i*>(source + chunk * kChunk));
    const auto t = _mm_loadu_si128(reinterpret_cast<const __m128i*>(target + chunk * kChunk));
    const auto m = _mm_loadu_si128(reinterpret_cast<const __m128i*>(mask + chunk * kChunk));
    accumulator = _mm_add_epi64(accumulator, _mm_sad_epu8(_mm_and_si128(s, m), t));
  }
  return static_cast<std::uint32_t>(_mm_cvtsi128_si32(accumulator) +
                                    _mm_cvtsi128_si32(_mm_srli_si128(accumulator, 8)));
#elif defined(PATCHY_EXEMPLAR_NEON)
  std::uint32_t sum = 0;
  for (std::int32_t chunk = 0; chunk < chunks; ++chunk) {
    const auto s = vld1q_u8(source + chunk * kChunk);
    const auto t = vld1q_u8(target + chunk * kChunk);
    const auto m = vld1q_u8(mask + chunk * kChunk);
    sum += vaddlvq_u8(vabdq_u8(vandq_u8(s, m), t));
  }
  return sum;
#else
  std::uint32_t sum = 0;
  for (std::int32_t k = 0; k < chunks * kChunk; ++k) {
    const auto d = static_cast<std::int32_t>(source[k] & mask[k]) - static_cast<std::int32_t>(target[k]);
    sum += static_cast<std::uint32_t>(d < 0 ? -d : d);
  }
  return sum;
#endif
}

}  // namespace

ExemplarInpaintResult exemplar_inpaint(std::uint8_t* image, std::int32_t width, std::int32_t height,
                                       const std::uint8_t* hole, Rect bounds, const ExemplarInpaintOptions& options,
                                       const std::function<void(std::int64_t, std::int64_t)>& progress) {
  ExemplarInpaintResult result;
  if (image == nullptr || hole == nullptr || width <= 0 || height <= 0 || bounds.width <= 0 || bounds.height <= 0) {
    return result;
  }
  const auto patch = std::max(std::int32_t{3}, options.patch_size | 1);
  const auto half = patch / 2;
  const auto radius = std::max(half + 1, options.search_radius);

  // Working window: the hole padded by the search reach, clipped to the image.
  // Everything below runs in window-local coordinates over a private copy.
  const auto margin = radius + half;
  const auto wx0 = std::max(0, bounds.x - margin);
  const auto wy0 = std::max(0, bounds.y - margin);
  const auto wx1 = std::min(width, bounds.x + bounds.width + margin);
  const auto wy1 = std::min(height, bounds.y + bounds.height + margin);
  const auto lw = wx1 - wx0;
  const auto lh = wy1 - wy0;
  if (lw <= 0 || lh <= 0) {
    return result;
  }
  const auto row_chunks = (patch * 4 + kChunk - 1) / kChunk;
  const auto row_bytes = row_chunks * kChunk;
  const auto cells = static_cast<std::size_t>(lw) * static_cast<std::size_t>(lh);
  // Slack after the last row so a padded chunk read at the window's far edge
  // stays inside the buffer (the mask zeroes whatever it covers).
  std::vector<std::uint8_t> px(cells * 4U + static_cast<std::size_t>(row_bytes), 0U);
  for (std::int32_t y = 0; y < lh; ++y) {
    std::memcpy(px.data() + static_cast<std::size_t>(y) * lw * 4U,
                image + (static_cast<std::size_t>(wy0 + y) * width + static_cast<std::size_t>(wx0)) * 4U,
                static_cast<std::size_t>(lw) * 4U);
  }
  // unknown: still to be filled (filled pixels never become sources: source
  // validity reads the ORIGINAL hole through the integral image below).
  // conf: Criminisi's confidence, 0..255.
  std::vector<std::uint8_t> unknown(cells, 0U);
  std::vector<std::uint8_t> conf(cells, 255U);
  std::int64_t remaining = 0;
  for (std::int32_t y = 0; y < bounds.height; ++y) {
    for (std::int32_t x = 0; x < bounds.width; ++x) {
      if (hole[static_cast<std::size_t>(y) * bounds.width + x] == 0U) {
        continue;
      }
      const auto lx = bounds.x + x - wx0;
      const auto ly = bounds.y + y - wy0;
      if (lx < 0 || ly < 0 || lx >= lw || ly >= lh) {
        continue;
      }
      const auto index = static_cast<std::size_t>(ly) * lw + lx;
      unknown[index] = 1U;
      conf[index] = 0U;
      ++remaining;
    }
  }
  const auto total = remaining;
  if (remaining == 0) {
    result.filled = true;
    return result;
  }
  // Integral image of the original hole so "is this source patch entirely
  // known?" is four lookups.
  const auto iw = static_cast<std::size_t>(lw) + 1U;
  std::vector<std::int32_t> hole_integral((static_cast<std::size_t>(lh) + 1U) * iw, 0);
  for (std::int32_t y = 0; y < lh; ++y) {
    std::int32_t row_sum = 0;
    for (std::int32_t x = 0; x < lw; ++x) {
      row_sum += unknown[static_cast<std::size_t>(y) * lw + x];
      hole_integral[(static_cast<std::size_t>(y) + 1U) * iw + x + 1U] =
          hole_integral[static_cast<std::size_t>(y) * iw + x + 1U] + row_sum;
    }
  }
  const auto hole_count = [&](std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1) {
    // Inclusive-exclusive box [x0, x1) x [y0, y1) in window coordinates.
    return hole_integral[static_cast<std::size_t>(y1) * iw + x1] - hole_integral[static_cast<std::size_t>(y0) * iw + x1] -
           hole_integral[static_cast<std::size_t>(y1) * iw + x0] + hole_integral[static_cast<std::size_t>(y0) * iw + x0];
  };
  const auto luminance = [&](std::size_t index) {
    const auto* p = px.data() + index * 4U;
    return (static_cast<std::int32_t>(p[0]) * 77 + static_cast<std::int32_t>(p[1]) * 150 +
            static_cast<std::int32_t>(p[2]) * 29) >> 8;
  };
  const auto known_at = [&](std::int32_t x, std::int32_t y) {
    return x >= 0 && y >= 0 && x < lw && y < lh && unknown[static_cast<std::size_t>(y) * lw + x] == 0U;
  };

  // Valid source centres (patch inside the window, no original hole cell)
  // and, per row, the next valid centre at or after x, so a candidate scan
  // steps over the hole and the edges in O(1) instead of testing each cell.
  std::vector<std::uint16_t> next_valid(cells, 0U);
  std::vector<std::uint8_t> valid_center(cells, 0U);
  for (std::int32_t y = 0; y < lh; ++y) {
    auto next = static_cast<std::uint16_t>(std::min<std::int32_t>(lw, 65535));
    for (std::int32_t x = lw - 1; x >= 0; --x) {
      const bool valid = x >= half && x <= lw - 1 - half && y >= half && y <= lh - 1 - half &&
                         hole_count(x - half, y - half, x + half + 1, y + half + 1) == 0;
      if (valid) {
        next = static_cast<std::uint16_t>(x);
        valid_center[static_cast<std::size_t>(y) * lw + x] = 1U;
      }
      next_valid[static_cast<std::size_t>(y) * lw + x] = next;
    }
  }
  // Integral of the valid centres: how many sources a window holds, in four
  // lookups, decides whether a target needs the axis-ray windows at all.
  std::vector<std::int32_t> valid_integral((static_cast<std::size_t>(lh) + 1U) * iw, 0);
  for (std::int32_t y = 0; y < lh; ++y) {
    std::int32_t row_sum = 0;
    for (std::int32_t x = 0; x < lw; ++x) {
      row_sum += valid_center[static_cast<std::size_t>(y) * lw + x];
      valid_integral[(static_cast<std::size_t>(y) + 1U) * iw + x + 1U] =
          valid_integral[static_cast<std::size_t>(y) * iw + x + 1U] + row_sum;
    }
  }
  const auto valid_count = [&](std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1) {
    // Inclusive box in window coordinates, clamped.
    x0 = std::max(0, x0);
    y0 = std::max(0, y0);
    x1 = std::min(lw - 1, x1);
    y1 = std::min(lh - 1, y1);
    if (x0 > x1 || y0 > y1) {
      return 0;
    }
    return valid_integral[static_cast<std::size_t>(y1 + 1) * iw + x1 + 1] -
           valid_integral[static_cast<std::size_t>(y0) * iw + x1 + 1] -
           valid_integral[static_cast<std::size_t>(y1 + 1) * iw + x0] + valid_integral[static_cast<std::size_t>(y0) * iw + x0];
  };

  // Along each axis ray from a hole cell, the first ORIGINAL known cell
  // (-1 when the ray leaves the window first). A target's candidate windows
  // hang off those four edge points, so sources to its left and right at the
  // same height are always in reach, however deep the target sits.
  std::vector<std::int32_t> known_left(cells, -1);
  std::vector<std::int32_t> known_right(cells, -1);
  std::vector<std::int32_t> known_up(cells, -1);
  std::vector<std::int32_t> known_down(cells, -1);
  for (std::int32_t y = 0; y < lh; ++y) {
    std::int32_t last = -1;
    for (std::int32_t x = 0; x < lw; ++x) {
      const auto cell = static_cast<std::size_t>(y) * lw + x;
      known_left[cell] = last;
      if (unknown[cell] == 0U) {
        last = x;
      }
    }
    last = -1;
    for (std::int32_t x = lw - 1; x >= 0; --x) {
      const auto cell = static_cast<std::size_t>(y) * lw + x;
      known_right[cell] = last;
      if (unknown[cell] == 0U) {
        last = x;
      }
    }
  }
  for (std::int32_t x = 0; x < lw; ++x) {
    std::int32_t last = -1;
    for (std::int32_t y = 0; y < lh; ++y) {
      const auto cell = static_cast<std::size_t>(y) * lw + x;
      known_up[cell] = last;
      if (unknown[cell] == 0U) {
        last = y;
      }
    }
    last = -1;
    for (std::int32_t y = lh - 1; y >= 0; --y) {
      const auto cell = static_cast<std::size_t>(y) * lw + x;
      known_down[cell] = last;
      if (unknown[cell] == 0U) {
        last = y;
      }
    }
  }

  // Chebyshev distance from every hole cell to the nearest original known
  // cell (multi-source BFS, 8-connected): the priority tie-break, so equal
  // priorities fill shallow cells first from every edge (onion peel) instead
  // of sweeping from the lowest index and meeting the far edge in a seam.
  std::vector<std::int32_t> reach(cells, 0);
  {
    std::vector<std::int32_t> queue;
    queue.reserve(cells);
    for (std::size_t cell = 0; cell < cells; ++cell) {
      reach[cell] = unknown[cell] != 0U ? -1 : 0;
      if (unknown[cell] == 0U) {
        queue.push_back(static_cast<std::int32_t>(cell));
      }
    }
    for (std::size_t head = 0; head < queue.size(); ++head) {
      const auto cell = queue[head];
      const auto cx = cell % lw;
      const auto cy = cell / lw;
      for (std::int32_t dy = -1; dy <= 1; ++dy) {
        for (std::int32_t dx = -1; dx <= 1; ++dx) {
          const auto x = cx + dx;
          const auto y = cy + dy;
          if (x < 0 || y < 0 || x >= lw || y >= lh) {
            continue;
          }
          const auto neighbour = y * lw + x;
          if (reach[static_cast<std::size_t>(neighbour)] < 0) {
            reach[static_cast<std::size_t>(neighbour)] = reach[static_cast<std::size_t>(cell)] + 1;
            queue.push_back(neighbour);
          }
        }
      }
    }
    for (auto& value : reach) {
      value = std::max(value, 0);
    }
  }

  const auto hardware = hardware_worker_threads();
  const auto workers = max_blocking_fanout_workers(std::clamp(hardware, 1, 16));
  const bool single_threaded = workers < 2 || options.single_threaded;

  // Unknown cells only ever lie inside the hole's bounds, so the front scan
  // stays within them (window-local).
  const auto hx0 = std::max(0, bounds.x - wx0);
  const auto hy0 = std::max(0, bounds.y - wy0);
  const auto hx1 = std::min(lw, bounds.x + bounds.width - wx0);
  const auto hy1 = std::min(lh, bounds.y + bounds.height - wy0);
  // Priorities are cached per front cell and refreshed only around each
  // filled patch (Criminisi's incremental update): confidence (sum over the
  // patch of conf, 0..255 each) times a data term from the strongest known
  // isophote in the patch projected on the front normal (both integer; the
  // normal is the unknown-mask gradient over the 3x3). -1 marks a cell that
  // is not on the front.
  const auto compute_priority = [&](std::int32_t index) -> std::int64_t {
    const auto cx = index % lw;
    const auto cy = index / lw;
    std::int64_t confidence_sum = 0;
    std::int64_t strongest = -1;
    std::int32_t gx_best = 0;
    std::int32_t gy_best = 0;
    for (std::int32_t dy = -half; dy <= half; ++dy) {
      for (std::int32_t dx = -half; dx <= half; ++dx) {
        const auto x = cx + dx;
        const auto y = cy + dy;
        if (!known_at(x, y)) {
          continue;
        }
        const auto cell = static_cast<std::size_t>(y) * lw + x;
        confidence_sum += conf[cell];
        if (known_at(x - 1, y) && known_at(x + 1, y) && known_at(x, y - 1) && known_at(x, y + 1)) {
          const auto gx = luminance(cell + 1U) - luminance(cell - 1U);
          const auto gy = luminance(cell + static_cast<std::size_t>(lw)) - luminance(cell - static_cast<std::size_t>(lw));
          const auto magnitude = static_cast<std::int64_t>(gx) * gx + static_cast<std::int64_t>(gy) * gy;
          if (magnitude > strongest) {
            strongest = magnitude;
            gx_best = gx;
            gy_best = gy;
          }
        }
      }
    }
    std::int32_t nx = 0;
    std::int32_t ny = 0;
    for (std::int32_t d = -1; d <= 1; ++d) {
      const auto unknown_at = [&](std::int32_t x, std::int32_t y) {
        return x >= 0 && y >= 0 && x < lw && y < lh ? static_cast<std::int32_t>(unknown[static_cast<std::size_t>(y) * lw + x]) : 0;
      };
      nx += unknown_at(cx + 1, cy + d) - unknown_at(cx - 1, cy + d);
      ny += unknown_at(cx + d, cy + 1) - unknown_at(cx + d, cy - 1);
    }
    // Isophote direction is the gradient rotated 90 degrees: (-gy, gx).
    const auto data_term = std::abs(static_cast<std::int64_t>(-gy_best) * nx + static_cast<std::int64_t>(gx_best) * ny);
    return confidence_sum * (64 + data_term / 16);
  };
  std::vector<std::int64_t> priority(cells, -1);
  std::vector<std::int32_t> front;
  front.reserve(static_cast<std::size_t>(lw) * 2U + static_cast<std::size_t>(lh) * 2U);
  std::size_t stale = 0;
  // Front membership: unknown cells with a known 4-neighbour inside the
  // window. Cells leaving the front keep a stale slot in the list until the
  // next compaction.
  const auto refresh_cell = [&](std::int32_t x, std::int32_t y) {
    if (x < hx0 || y < hy0 || x >= hx1 || y >= hy1) {
      return;
    }
    const auto index = y * lw + x;
    const auto cell = static_cast<std::size_t>(index);
    const bool on_front = unknown[cell] != 0U && (known_at(x - 1, y) || known_at(x + 1, y) ||
                                                   known_at(x, y - 1) || known_at(x, y + 1));
    if (!on_front) {
      if (priority[cell] >= 0) {
        priority[cell] = -1;
        ++stale;
      }
      return;
    }
    if (priority[cell] < 0) {
      front.push_back(index);
    }
    priority[cell] = compute_priority(index);
  };
  for (std::int32_t y = hy0; y < hy1; ++y) {
    for (std::int32_t x = hx0; x < hx1; ++x) {
      refresh_cell(x, y);
    }
  }
  // Per-iteration target rows: masked pixels and the mask, padded to chunks.
  std::vector<std::uint8_t> target_rows(static_cast<std::size_t>(patch) * row_bytes);
  std::vector<std::uint8_t> mask_rows(static_cast<std::size_t>(patch) * row_bytes);
  while (remaining > 0) {
    if (options.cancel != nullptr && options.cancel->load(std::memory_order_relaxed)) {
      result.cancelled = true;
      return result;
    }
    if (stale * 2U > front.size()) {
      front.erase(std::remove_if(front.begin(), front.end(),
                                 [&](std::int32_t index) { return priority[static_cast<std::size_t>(index)] < 0; }),
                  front.end());
      stale = 0;
    }
    // The pick: the highest cached priority on the live front; ties go to
    // the cell nearest the original hole edge, then the smaller row-major
    // index.
    std::int64_t best_priority = -1;
    std::int32_t best_index = -1;
    std::int32_t best_reach = 0;
    for (const auto index : front) {
      const auto candidate_priority = priority[static_cast<std::size_t>(index)];
      if (candidate_priority < 0) {
        continue;
      }
      const auto candidate_reach = reach[static_cast<std::size_t>(index)];
      if (candidate_priority > best_priority ||
          (candidate_priority == best_priority &&
           (candidate_reach < best_reach || (candidate_reach == best_reach && index < best_index)))) {
        best_priority = candidate_priority;
        best_index = index;
        best_reach = candidate_reach;
      }
    }
    if (best_index < 0) {
      // Unknown cells with no known neighbour anywhere: only possible when the
      // hole touches the image border everywhere; nothing more can be filled.
      return result;
    }
    const auto tx = best_index % lw;
    const auto ty = best_index / lw;
    // Target patch, clipped to the window.
    const auto tx0 = std::max(0, tx - half);
    const auto ty0 = std::max(0, ty - half);
    const auto tx1 = std::min(lw - 1, tx + half);
    const auto ty1 = std::min(lh - 1, ty + half);
    const auto patch_w = tx1 - tx0 + 1;
    const auto patch_h = ty1 - ty0 + 1;
    // The patch's confidence, for the cells it fills, and the masked target
    // rows for the distance (unknown cells and padding read as 0 on both sides).
    std::int64_t target_conf_sum = 0;
    std::int64_t known_pixels = 0;
    std::fill(target_rows.begin(), target_rows.end(), std::uint8_t{0});
    std::fill(mask_rows.begin(), mask_rows.end(), std::uint8_t{0});
    for (std::int32_t y = ty0; y <= ty1; ++y) {
      auto* target_row = target_rows.data() + static_cast<std::size_t>(y - ty0) * row_bytes;
      auto* mask_row = mask_rows.data() + static_cast<std::size_t>(y - ty0) * row_bytes;
      for (std::int32_t x = tx0; x <= tx1; ++x) {
        const auto cell = static_cast<std::size_t>(y) * lw + x;
        target_conf_sum += conf[cell];
        if (unknown[cell] != 0U) {
          continue;
        }
        std::memcpy(target_row + static_cast<std::size_t>(x - tx0) * 4U, px.data() + cell * 4U, 4U);
        std::memset(mask_row + static_cast<std::size_t>(x - tx0) * 4U, 0xFF, 4U);
        ++known_pixels;
      }
    }
    const auto new_conf = static_cast<std::uint8_t>(
        std::clamp<std::int64_t>(target_conf_sum / std::max(1, patch_w * patch_h), 0, 255));

    // Candidate windows: source patch centres whose full patch lies inside
    // the window and contains no original hole cell, taken from (a) the
    // target's own window, radius `radius`, and (b) when that window holds
    // few clean sources (the target is deep inside), for each axis ray whose
    // first original known cell lies beyond it, the half-window of the same
    // radius on the far side of that edge point. Every window is
    // scanned at full resolution in row-major order, windows in a fixed order,
    // and a candidate's scan index is unique across windows; a last resort
    // scans the whole working window on a stride of 4. The strips share the
    // best distance seen so far only to prune (strictly worse partial sums
    // stop early); every candidate that can tie the final minimum is fully
    // scored, and the reduction takes the smallest distance, then the smallest
    // scan index, so the result matches a serial scan.
    struct Window {
      std::int32_t x0, y0, x1, y1;
    };
    Window windows[5];
    std::int32_t window_count = 0;
    const auto add_window = [&](std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1) {
      x0 = std::max(half, x0);
      y0 = std::max(half, y0);
      x1 = std::min(lw - 1 - half, x1);
      y1 = std::min(lh - 1 - half, y1);
      if (x0 <= x1 && y0 <= y1) {
        windows[window_count++] = Window{x0, y0, x1, y1};
      }
    };
    add_window(tx - radius, ty - radius, tx + radius, ty + radius);
    // A target whose own window already holds this many clean sources is
    // near the hole edge; the rays matter for targets deep inside, where the
    // own window is mostly hole.
    constexpr std::int32_t kRaysBelowValidCount = 4096;
    if (valid_count(tx - radius, ty - radius, tx + radius, ty + radius) < kRaysBelowValidCount) {
      const auto cell = static_cast<std::size_t>(best_index);
      const auto left = known_left[cell];
      if (left >= 0 && tx - left > radius) {
        add_window(left - radius, ty - radius, left, ty + radius);
      }
      const auto right = known_right[cell];
      if (right >= 0 && right - tx > radius) {
        add_window(right, ty - radius, right + radius, ty + radius);
      }
      const auto up = known_up[cell];
      if (up >= 0 && ty - up > radius) {
        add_window(tx - radius, up - radius, tx + radius, up);
      }
      const auto down = known_down[cell];
      if (down >= 0 && down - ty > radius) {
        add_window(tx - radius, down, tx + radius, down + radius);
      }
    }
    std::atomic<std::int64_t> shared_best{std::numeric_limits<std::int64_t>::max()};
    Candidate best;
    const auto scan_window = [&](const Window& window, std::int32_t stride, std::int64_t index_base) {
      const auto sx0 = window.x0;
      const auto sy0 = window.y0;
      const auto sx1 = window.x1;
      const auto sy1 = window.y1;
      const auto scan_width = (sx1 - sx0) / stride + 1;
      const auto scan_rows = [&](std::int32_t row_begin, std::int32_t row_end) {
        Candidate local;
        for (std::int32_t row = row_begin; row < row_end; ++row) {
          const auto sy = sy0 + row * stride;
          const auto* row_next = next_valid.data() + static_cast<std::size_t>(sy) * lw;
          for (std::int32_t sx = sx0; sx <= sx1;) {
            const std::int32_t valid_x = row_next[sx];
            if (valid_x > sx1) {
              break;
            }
            if (valid_x != sx) {
              // Snap forward onto the stride grid, then re-check validity there.
              const auto off_grid = (valid_x - sx0) % stride;
              sx = off_grid == 0 ? valid_x : valid_x + (stride - off_grid);
              continue;
            }
            const auto prune_at = shared_best.load(std::memory_order_relaxed);
            // A fixed geometric term first: sources near the target (same
            // rows for horizontal grain, same columns for vertical) keep the
            // texture's phase locked to the surroundings across the whole
            // hole; far sources must be clearly better to win.
            std::int64_t distance =
                static_cast<std::int64_t>(std::abs(sx - tx) + std::abs(sy - ty)) * options.offset_penalty;
            bool pruned = false;
            for (std::int32_t r = 0; r < patch_h; ++r) {
              const auto source_y = sy + (ty0 - ty) + r;
              const auto* source = px.data() + (static_cast<std::size_t>(source_y) * lw + (sx + (tx0 - tx))) * 4U;
              distance += masked_row_distance(source, target_rows.data() + static_cast<std::size_t>(r) * row_bytes,
                                              mask_rows.data() + static_cast<std::size_t>(r) * row_bytes, row_chunks);
              if (distance > prune_at) {
                pruned = true;
                break;
              }
            }
            if (!pruned && distance < local.distance) {
              // Strictly better only: an equal distance keeps the earlier scan index.
              local.distance = distance;
              local.index = index_base + static_cast<std::int64_t>(row) * scan_width + (sx - sx0) / stride;
              auto seen = shared_best.load(std::memory_order_relaxed);
              while (distance < seen &&
                     !shared_best.compare_exchange_weak(seen, distance, std::memory_order_relaxed)) {
              }
            }
            sx += stride;
          }
        }
        return local;
      };
      const auto rows = (sy1 - sy0) / stride + 1;
      const auto candidate_count = static_cast<std::int64_t>(rows) * scan_width;
      const auto merge = [&](const Candidate& candidate) {
        if (candidate.distance < best.distance ||
            (candidate.distance == best.distance && candidate.index < best.index)) {
          best = candidate;
        }
      };
      if (!single_threaded && candidate_count >= 4096 && rows >= workers * 2) {
        std::vector<std::future<Candidate>> strips;
        const auto rows_per_strip = (rows + workers - 1) / workers;
        for (std::int32_t start = 0; start < rows; start += rows_per_strip) {
          const auto end = std::min(start + rows_per_strip, rows);
          strips.push_back(std::async(std::launch::async, scan_rows, start, end));
        }
        for (auto& strip : strips) {
          merge(strip.get());
        }
      } else {
        merge(scan_rows(0, rows));
      }
      return static_cast<std::int64_t>(rows) * scan_width;
    };
    // Decoding a winning index back to a centre needs the window it came from.
    std::int64_t index_bases[6];
    std::int32_t strides[6];
    std::int32_t scanned = 0;
    std::int64_t next_base = 0;
    for (std::int32_t w = 0; w < window_count; ++w) {
      index_bases[scanned] = next_base;
      strides[scanned] = 1;
      next_base += scan_window(windows[w], 1, next_base);
      ++scanned;
    }
    if (best.index == std::numeric_limits<std::int64_t>::max()) {
      // Nothing clean in any window (a hole hugging the image edge, say): one
      // last pass over the whole working window on a coarse grid.
      windows[window_count] = Window{half, half, lw - 1 - half, lh - 1 - half};
      index_bases[scanned] = next_base;
      strides[scanned] = 4;
      next_base += scan_window(windows[window_count], 4, next_base);
      ++window_count;
      ++scanned;
    }
    if (best.index == std::numeric_limits<std::int64_t>::max()) {
      // No fully known source patch anywhere in the window: give up on the
      // whole fill (the caller falls back); the image is untouched.
      return result;
    }
    if (options.attempt > 0) {
      // Variation N (Efros-Leung 1999 epsilon selection): every candidate
      // within a fixed slack of the best distance is admissible, and the copy
      // comes from one of them. The admissible set is recomputed here from a
      // second full scan of the SAME windows against that fixed threshold,
      // used once, and dropped: nothing about it persists to another target
      // or another run. The pick is the k-th admissible candidate in scan
      // order, so the row strips only count (sums in row order) and the
      // result matches a serial scan whatever the strip split.
      const auto slack = std::max<std::int64_t>(best.distance / 8, known_pixels * 8);
      const auto threshold = best.distance + slack;
      // One candidate against the fixed threshold: false once a partial sum
      // exceeds it (the same row order and integer sums as the best-match
      // scan, so an admissible candidate here scored the same there).
      const auto admissible = [&](std::int32_t sx, std::int32_t sy) {
        std::int64_t distance =
            static_cast<std::int64_t>(std::abs(sx - tx) + std::abs(sy - ty)) * options.offset_penalty;
        for (std::int32_t r = 0; r < patch_h; ++r) {
          const auto source_y = sy + (ty0 - ty) + r;
          const auto* source = px.data() + (static_cast<std::size_t>(source_y) * lw + (sx + (tx0 - tx))) * 4U;
          distance += masked_row_distance(source, target_rows.data() + static_cast<std::size_t>(r) * row_bytes,
                                          mask_rows.data() + static_cast<std::size_t>(r) * row_bytes, row_chunks);
          if (distance > threshold) {
            return false;
          }
        }
        return true;
      };
      // Walks one scan row's valid centres on the stride grid (the best-match
      // scan's stepping) and calls `visit(sx, sy)`; a false return stops the
      // row.
      const auto walk_row = [&](const Window& window, std::int32_t stride, std::int32_t row, auto&& visit) {
        const auto sy = window.y0 + row * stride;
        const auto* row_next = next_valid.data() + static_cast<std::size_t>(sy) * lw;
        for (std::int32_t sx = window.x0; sx <= window.x1;) {
          const std::int32_t valid_x = row_next[sx];
          if (valid_x > window.x1) {
            break;
          }
          if (valid_x != sx) {
            const auto off_grid = (valid_x - window.x0) % stride;
            sx = off_grid == 0 ? valid_x : valid_x + (stride - off_grid);
            continue;
          }
          if (!visit(sx, sy)) {
            return;
          }
          sx += stride;
        }
      };
      std::int64_t admissible_total = 0;
      std::int64_t window_counts[6] = {0, 0, 0, 0, 0, 0};
      for (std::int32_t w = 0; w < scanned; ++w) {
        const auto& window = windows[w];
        const auto stride = strides[w];
        const auto rows = (window.y1 - window.y0) / stride + 1;
        const auto count_rows = [&](std::int32_t row_begin, std::int32_t row_end) {
          std::int64_t count = 0;
          for (std::int32_t row = row_begin; row < row_end; ++row) {
            walk_row(window, stride, row, [&](std::int32_t sx, std::int32_t sy) {
              count += admissible(sx, sy) ? 1 : 0;
              return true;
            });
          }
          return count;
        };
        const auto scan_width = (window.x1 - window.x0) / stride + 1;
        std::int64_t count = 0;
        if (!single_threaded && static_cast<std::int64_t>(rows) * scan_width >= 4096 && rows >= workers * 2) {
          std::vector<std::future<std::int64_t>> strips;
          const auto rows_per_strip = (rows + workers - 1) / workers;
          for (std::int32_t start = 0; start < rows; start += rows_per_strip) {
            strips.push_back(std::async(std::launch::async, count_rows, start, std::min(start + rows_per_strip, rows)));
          }
          for (auto& strip : strips) {
            count += strip.get();
          }
        } else {
          count = count_rows(0, rows);
        }
        window_counts[w] = count;
        admissible_total += count;
      }
      // The best candidate itself is admissible, so admissible_total >= 1. The seed
      // mixes the variation number with the target's image coordinates, so a
      // given variation is reproducible and different targets draw apart.
      const auto target_cell = static_cast<std::uint64_t>(wy0 + ty) * static_cast<std::uint64_t>(width) +
                               static_cast<std::uint64_t>(wx0 + tx);
      const auto seed = (static_cast<std::uint64_t>(options.attempt) << 40U) ^ target_cell;
      auto k = uniform_below(splitmix64(seed), std::max<std::int64_t>(1, admissible_total));
      for (std::int32_t w = 0; w < scanned; ++w) {
        if (k >= window_counts[w]) {
          k -= window_counts[w];
          continue;
        }
        const auto& window = windows[w];
        const auto stride = strides[w];
        const auto rows = (window.y1 - window.y0) / stride + 1;
        const auto scan_width = (window.x1 - window.x0) / stride + 1;
        std::int64_t picked = -1;
        for (std::int32_t row = 0; row < rows && picked < 0; ++row) {
          walk_row(window, stride, row, [&](std::int32_t sx, std::int32_t sy) {
            if (!admissible(sx, sy)) {
              return true;
            }
            if (k == 0) {
              picked = index_bases[w] + static_cast<std::int64_t>(row) * scan_width + (sx - window.x0) / stride;
              return false;
            }
            --k;
            return true;
          });
        }
        if (picked >= 0) {
          best.index = picked;
        }
        break;
      }
    }
    std::int32_t chosen = scanned - 1;
    while (chosen > 0 && best.index < index_bases[chosen]) {
      --chosen;
    }
    const auto& chosen_window = windows[chosen];
    const auto chosen_stride = strides[chosen];
    const auto chosen_width = (chosen_window.x1 - chosen_window.x0) / chosen_stride + 1;
    const auto local_index = best.index - index_bases[chosen];
    const auto sx = chosen_window.x0 + static_cast<std::int32_t>(local_index % chosen_width) * chosen_stride;
    const auto sy = chosen_window.y0 + static_cast<std::int32_t>(local_index / chosen_width) * chosen_stride;
    for (std::int32_t y = ty0; y <= ty1; ++y) {
      for (std::int32_t x = tx0; x <= tx1; ++x) {
        const auto cell = static_cast<std::size_t>(y) * lw + x;
        if (unknown[cell] == 0U) {
          continue;
        }
        const auto source = static_cast<std::size_t>(sy + (y - ty)) * lw + static_cast<std::size_t>(sx + (x - tx));
        std::memcpy(px.data() + cell * 4U, px.data() + source * 4U, 4U);
        unknown[cell] = 0U;
        conf[cell] = new_conf;
        --remaining;
      }
    }
    // Only cells whose patch overlaps the copied one can change front status
    // or priority; refresh that neighbourhood (plus the isophote's 1-cell
    // reach) instead of rescanning the hole.
    for (std::int32_t y = ty - patch - 1; y <= ty + patch + 1; ++y) {
      for (std::int32_t x = tx - patch - 1; x <= tx + patch + 1; ++x) {
        refresh_cell(x, y);
      }
    }
    ++result.patches;
    if (progress) {
      progress(total - remaining, total);
    }
  }

  // Write the filled hole back; everything else is byte-identical already.
  for (std::int32_t y = 0; y < bounds.height; ++y) {
    for (std::int32_t x = 0; x < bounds.width; ++x) {
      if (hole[static_cast<std::size_t>(y) * bounds.width + x] == 0U) {
        continue;
      }
      const auto ix = bounds.x + x;
      const auto iy = bounds.y + y;
      if (ix < 0 || iy < 0 || ix >= width || iy >= height) {
        continue;
      }
      std::memcpy(image + (static_cast<std::size_t>(iy) * width + ix) * 4U,
                  px.data() + (static_cast<std::size_t>(iy - wy0) * lw + (ix - wx0)) * 4U, 4U);
    }
  }
  result.filled = true;
  return result;
}

void exemplar_match_tone(std::uint8_t* filled, std::int32_t width, std::int32_t height, const std::uint8_t* hole,
                         Rect bounds, std::int32_t blur_radius, std::int32_t strength_percent) {
  const auto strength = std::clamp(strength_percent, 0, 100);
  if (filled == nullptr || hole == nullptr || width <= 0 || height <= 0 || bounds.width <= 0 || bounds.height <= 0 ||
      strength == 0) {
    return;
  }
  const auto radius = std::max(1, blur_radius);
  // Membrane grid: the hole bounds plus a one-cell Dirichlet ring. Every
  // low-pass value below is a box average over HOLE cells of the filled image
  // only (normalized by the count in the box), for ring and interior cells
  // alike: the ring cell next to a hole edge reads the fill just inside it,
  // never the content beyond the edge. A box normalized over the original
  // KNOWN pixels instead let whatever lay a blur radius past the edge take
  // the weight of the excluded hole cells (issue #23: a hole that ran 3 px
  // short of a dark band's edges had white background above and below, so
  // the ring read 170 where the fill was 26 and the membrane lifted the whole
  // fill toward gray; the fixture in tests/remove_object_fixture.hpp pins it).
  const auto gx0 = std::max(0, bounds.x - 1);
  const auto gy0 = std::max(0, bounds.y - 1);
  const auto gx1 = std::min(width, bounds.x + bounds.width + 1);
  const auto gy1 = std::min(height, bounds.y + bounds.height + 1);
  const auto gw = gx1 - gx0;
  const auto gh = gy1 - gy0;
  if (gw <= 0 || gh <= 0) {
    return;
  }
  // The blur boxes reach `radius` past the grid, but only hole cells count,
  // and those lie inside `bounds`; the integral covers the bounds alone.
  const auto bx0 = bounds.x;
  const auto by0 = bounds.y;
  const auto bw = bounds.width;
  const auto bh = bounds.height;
  const auto iw = static_cast<std::size_t>(bw) + 1U;
  const auto hole_at = [&](std::int32_t x, std::int32_t y) {
    if (x < bounds.x || y < bounds.y || x >= bounds.x + bounds.width || y >= bounds.y + bounds.height) {
      return false;
    }
    return hole[static_cast<std::size_t>(y - bounds.y) * bounds.width + (x - bounds.x)] != 0U;
  };
  std::vector<std::int64_t> sum_hole((static_cast<std::size_t>(bh) + 1U) * iw * 3U, 0);
  std::vector<std::int32_t> count_hole((static_cast<std::size_t>(bh) + 1U) * iw, 0);
  for (std::int32_t y = 0; y < bh; ++y) {
    std::int64_t row_sum[3] = {0, 0, 0};
    std::int32_t row_count = 0;
    for (std::int32_t x = 0; x < bw; ++x) {
      const auto ix = bx0 + x;
      const auto iy = by0 + y;
      if (ix >= 0 && iy >= 0 && ix < width && iy < height && hole_at(ix, iy)) {
        const auto* f = filled + (static_cast<std::size_t>(iy) * width + ix) * 4U;
        for (int c = 0; c < 3; ++c) {
          row_sum[c] += f[c];
        }
        ++row_count;
      }
      const auto here = (static_cast<std::size_t>(y) + 1U) * iw + x + 1U;
      const auto above = static_cast<std::size_t>(y) * iw + x + 1U;
      for (int c = 0; c < 3; ++c) {
        sum_hole[here * 3U + c] = sum_hole[above * 3U + c] + row_sum[c];
      }
      count_hole[here] = count_hole[above] + row_count;
    }
  }
  const auto box = [&](std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1, int c) {
    return sum_hole[(static_cast<std::size_t>(y1) * iw + x1) * 3U + c] -
           sum_hole[(static_cast<std::size_t>(y0) * iw + x1) * 3U + c] -
           sum_hole[(static_cast<std::size_t>(y1) * iw + x0) * 3U + c] +
           sum_hole[(static_cast<std::size_t>(y0) * iw + x0) * 3U + c];
  };
  const auto box_count = [&](std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1) {
    return count_hole[static_cast<std::size_t>(y1) * iw + x1] - count_hole[static_cast<std::size_t>(y0) * iw + x1] -
           count_hole[static_cast<std::size_t>(y1) * iw + x0] + count_hole[static_cast<std::size_t>(y0) * iw + x0];
  };
  // Membrane: ring cells carry the hole-side low-pass; the interior receives
  // its harmonic interpolation. The interior's own low-pass is kept per cell
  // to subtract.
  const auto cells = static_cast<std::size_t>(gw) * static_cast<std::size_t>(gh);
  std::vector<std::uint8_t> interior(cells, 0U);
  std::vector<std::int16_t> membrane(cells * 3U, 0);
  std::vector<std::int16_t> fill_low(cells * 3U, 0);
  for (std::int32_t y = 0; y < gh; ++y) {
    for (std::int32_t x = 0; x < gw; ++x) {
      const auto ix = gx0 + x;
      const auto iy = gy0 + y;
      const auto cell = static_cast<std::size_t>(y) * gw + x;
      // Blur box in bounds coordinates (exclusive upper bounds), clipped.
      const auto x0 = std::clamp(ix - radius - bx0, 0, bw);
      const auto y0 = std::clamp(iy - radius - by0, 0, bh);
      const auto x1 = std::clamp(ix + radius + 1 - bx0, 0, bw);
      const auto y1 = std::clamp(iy + radius + 1 - by0, 0, bh);
      const auto count = x1 > x0 && y1 > y0 ? box_count(x0, y0, x1, y1) : 0;
      for (int c = 0; c < 3; ++c) {
        // A ring cell with no hole cell in reach never neighbours the
        // interior, so its value is inert.
        fill_low[cell * 3U + c] = count > 0 ? static_cast<std::int16_t>(box(x0, y0, x1, y1, c) / count) : 0;
      }
      if (hole_at(ix, iy)) {
        interior[cell] = 1U;
        continue;
      }
      for (int c = 0; c < 3; ++c) {
        membrane[cell * 3U + c] = fill_low[cell * 3U + c];
      }
    }
  }
  solve_heal_membrane(interior.data(), gw, gh, membrane.data());
  for (std::int32_t y = 0; y < gh; ++y) {
    for (std::int32_t x = 0; x < gw; ++x) {
      const auto cell = static_cast<std::size_t>(y) * gw + x;
      if (interior[cell] == 0U) {
        continue;
      }
      auto* f = filled + (static_cast<std::size_t>(gy0 + y) * width + (gx0 + x)) * 4U;
      for (int c = 0; c < 3; ++c) {
        // Integer scaling, rounded half away from zero, so 100 is exactly the
        // full replacement.
        const auto correction = static_cast<std::int32_t>(membrane[cell * 3U + c]) - fill_low[cell * 3U + c];
        const auto scaled =
            correction >= 0 ? (correction * strength + 50) / 100 : -((-correction * strength + 50) / 100);
        const auto value = static_cast<std::int32_t>(f[c]) + scaled;
        f[c] = static_cast<std::uint8_t>(std::clamp(value, 0, 255));
      }
    }
  }
}

}  // namespace patchy
