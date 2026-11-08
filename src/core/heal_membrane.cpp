#include "core/heal_membrane.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace patchy {

namespace {

// 16.16 fixed point throughout: offsets span [-255, 255] (25 bits shifted),
// and the SOR update's transient overshoot stays far inside int32.
constexpr std::int32_t kFixedOne = 1 << 16;
// Over-relaxation factor 1.7 (stable below 2.0); fixed point.
constexpr std::int64_t kOmega = 111411;
constexpr int kCoarsestSweeps = 400;
constexpr int kLevelSweeps = 32;
constexpr std::int32_t kCoarsestCells = 64;

struct Level {
  std::int32_t width{0};
  std::int32_t height{0};
  std::vector<std::uint8_t> interior;      // non-zero = solve; zero = Dirichlet
  std::vector<std::int32_t> value;         // 3 channels interleaved, 16.16
};

// One lexicographic SOR sweep: each interior cell relaxes toward the average
// of its existing 4-neighbors. Fixed order, integer math, no early exit -
// byte-deterministic on every toolchain.
void sweep(Level& level) {
  const auto width = level.width;
  const auto height = level.height;
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      const auto index = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                         static_cast<std::size_t>(x);
      if (level.interior[index] == 0U) {
        continue;
      }
      for (int channel = 0; channel < 3; ++channel) {
        std::int64_t sum = 0;
        int neighbors = 0;
        const auto base = index * 3U + static_cast<std::size_t>(channel);
        if (x > 0) {
          sum += level.value[base - 3U];
          ++neighbors;
        }
        if (x + 1 < width) {
          sum += level.value[base + 3U];
          ++neighbors;
        }
        if (y > 0) {
          sum += level.value[base - static_cast<std::size_t>(width) * 3U];
          ++neighbors;
        }
        if (y + 1 < height) {
          sum += level.value[base + static_cast<std::size_t>(width) * 3U];
          ++neighbors;
        }
        if (neighbors == 0) {
          continue;
        }
        const auto average = sum / neighbors;
        const auto current = static_cast<std::int64_t>(level.value[base]);
        level.value[base] = static_cast<std::int32_t>(current + ((kOmega * (average - current)) >> 16));
      }
    }
  }
}

}  // namespace

void solve_heal_membrane(const std::uint8_t* interior, std::int32_t width, std::int32_t height,
                         std::int16_t* offsets_rgb) {
  if (interior == nullptr || offsets_rgb == nullptr || width <= 0 || height <= 0) {
    return;
  }
  const auto cells = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);

  std::vector<Level> levels;
  levels.emplace_back();
  auto& finest = levels.back();
  finest.width = width;
  finest.height = height;
  finest.interior.assign(interior, interior + cells);
  finest.value.resize(cells * 3U);
  bool any_interior = false;
  bool any_dirichlet = false;
  for (std::size_t index = 0; index < cells; ++index) {
    for (int channel = 0; channel < 3; ++channel) {
      finest.value[index * 3U + static_cast<std::size_t>(channel)] =
          interior[index] != 0U ? 0
                                : static_cast<std::int32_t>(offsets_rgb[index * 3U +
                                                                        static_cast<std::size_t>(channel)]) *
                                      kFixedOne;
    }
    any_interior = any_interior || interior[index] != 0U;
    any_dirichlet = any_dirichlet || interior[index] == 0U;
  }
  if (!any_interior || !any_dirichlet) {
    return;
  }

  // Coarsen 2x per level: a coarse cell is Dirichlet when ANY child is (the
  // boundary must survive coarsening), carrying the average of its Dirichlet
  // children; interior coarse cells average all children as the initial guess.
  while (static_cast<std::int64_t>(levels.back().width) * levels.back().height > kCoarsestCells &&
         levels.back().width > 2 && levels.back().height > 2) {
    const auto& fine = levels.back();
    Level coarse;
    coarse.width = (fine.width + 1) / 2;
    coarse.height = (fine.height + 1) / 2;
    const auto coarse_cells = static_cast<std::size_t>(coarse.width) * static_cast<std::size_t>(coarse.height);
    coarse.interior.resize(coarse_cells);
    coarse.value.resize(coarse_cells * 3U);
    for (std::int32_t y = 0; y < coarse.height; ++y) {
      for (std::int32_t x = 0; x < coarse.width; ++x) {
        std::int64_t dirichlet_sum[3] = {0, 0, 0};
        std::int64_t all_sum[3] = {0, 0, 0};
        int dirichlet_count = 0;
        int all_count = 0;
        for (int child_y = 0; child_y < 2; ++child_y) {
          for (int child_x = 0; child_x < 2; ++child_x) {
            const auto fine_x = x * 2 + child_x;
            const auto fine_y = y * 2 + child_y;
            if (fine_x >= fine.width || fine_y >= fine.height) {
              continue;
            }
            const auto fine_index = static_cast<std::size_t>(fine_y) * static_cast<std::size_t>(fine.width) +
                                    static_cast<std::size_t>(fine_x);
            ++all_count;
            for (int channel = 0; channel < 3; ++channel) {
              all_sum[channel] += fine.value[fine_index * 3U + static_cast<std::size_t>(channel)];
            }
            if (fine.interior[fine_index] == 0U) {
              ++dirichlet_count;
              for (int channel = 0; channel < 3; ++channel) {
                dirichlet_sum[channel] += fine.value[fine_index * 3U + static_cast<std::size_t>(channel)];
              }
            }
          }
        }
        const auto coarse_index = static_cast<std::size_t>(y) * static_cast<std::size_t>(coarse.width) +
                                  static_cast<std::size_t>(x);
        coarse.interior[coarse_index] = dirichlet_count > 0 ? 0U : 1U;
        for (int channel = 0; channel < 3; ++channel) {
          const auto sum = dirichlet_count > 0 ? dirichlet_sum[channel] : all_sum[channel];
          const auto count = dirichlet_count > 0 ? dirichlet_count : all_count;
          coarse.value[coarse_index * 3U + static_cast<std::size_t>(channel)] =
              static_cast<std::int32_t>(count > 0 ? sum / count : 0);
        }
      }
    }
    levels.push_back(std::move(coarse));
  }

  // Coarse-to-fine cascade: solve hard at the coarsest grid, prolong the
  // interior values (2x replicate), then a fixed number of sweeps per level.
  for (auto level = static_cast<std::int32_t>(levels.size()) - 1; level >= 0; --level) {
    auto& current = levels[static_cast<std::size_t>(level)];
    if (level + 1 < static_cast<std::int32_t>(levels.size())) {
      const auto& coarse = levels[static_cast<std::size_t>(level) + 1U];
      for (std::int32_t y = 0; y < current.height; ++y) {
        for (std::int32_t x = 0; x < current.width; ++x) {
          const auto index = static_cast<std::size_t>(y) * static_cast<std::size_t>(current.width) +
                             static_cast<std::size_t>(x);
          if (current.interior[index] == 0U) {
            continue;
          }
          const auto coarse_index =
              static_cast<std::size_t>(std::min(y / 2, coarse.height - 1)) *
                  static_cast<std::size_t>(coarse.width) +
              static_cast<std::size_t>(std::min(x / 2, coarse.width - 1));
          for (int channel = 0; channel < 3; ++channel) {
            current.value[index * 3U + static_cast<std::size_t>(channel)] =
                coarse.value[coarse_index * 3U + static_cast<std::size_t>(channel)];
          }
        }
      }
    }
    const auto sweeps = level + 1 == static_cast<std::int32_t>(levels.size()) ? kCoarsestSweeps : kLevelSweeps;
    for (int iteration = 0; iteration < sweeps; ++iteration) {
      sweep(current);
    }
  }

  const auto& solved = levels.front();
  for (std::size_t index = 0; index < cells; ++index) {
    if (interior[index] == 0U) {
      continue;
    }
    for (int channel = 0; channel < 3; ++channel) {
      const auto value = solved.value[index * 3U + static_cast<std::size_t>(channel)];
      const auto rounded = value >= 0 ? (value + kFixedOne / 2) >> 16
                                      : -((-value + kFixedOne / 2) >> 16);
      offsets_rgb[index * 3U + static_cast<std::size_t>(channel)] =
          static_cast<std::int16_t>(std::clamp<std::int32_t>(rounded, -32768, 32767));
    }
  }
}

void fade_heal_offsets_to_edge_band(const std::uint8_t* interior, std::int32_t width, std::int32_t height,
                                    std::int16_t* offsets_rgb, std::int32_t band) {
  if (interior == nullptr || offsets_rgb == nullptr || width <= 0 || height <= 0 || band <= 0) {
    return;
  }
  const auto cells = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  // 3-4 chamfer distance (3 per straight step) from the nearest Dirichlet cell; the grid
  // edge counts as one step away (no ring cell lies beyond it).
  constexpr std::int32_t kFar = 1 << 28;
  std::vector<std::int32_t> distance(cells, 0);
  for (std::size_t index = 0; index < cells; ++index) {
    distance[index] = interior[index] != 0U ? kFar : 0;
  }
  const auto at = [&](std::int32_t x, std::int32_t y) -> std::int32_t& {
    return distance[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
  };
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      auto& d = at(x, y);
      if (d == 0) {
        continue;
      }
      if (x == 0 || y == 0 || x == width - 1 || y == height - 1) {
        d = std::min(d, 3);
      }
      if (x > 0) {
        d = std::min(d, at(x - 1, y) + 3);
      }
      if (y > 0) {
        d = std::min(d, at(x, y - 1) + 3);
        if (x > 0) {
          d = std::min(d, at(x - 1, y - 1) + 4);
        }
        if (x + 1 < width) {
          d = std::min(d, at(x + 1, y - 1) + 4);
        }
      }
    }
  }
  for (std::int32_t y = height - 1; y >= 0; --y) {
    for (std::int32_t x = width - 1; x >= 0; --x) {
      auto& d = at(x, y);
      if (d == 0) {
        continue;
      }
      if (x + 1 < width) {
        d = std::min(d, at(x + 1, y) + 3);
      }
      if (y + 1 < height) {
        d = std::min(d, at(x, y + 1) + 3);
        if (x + 1 < width) {
          d = std::min(d, at(x + 1, y + 1) + 4);
        }
        if (x > 0) {
          d = std::min(d, at(x - 1, y + 1) + 4);
        }
      }
    }
  }
  const auto span = static_cast<std::int64_t>(band) * 3;
  for (std::size_t index = 0; index < cells; ++index) {
    if (interior[index] == 0U) {
      continue;
    }
    // Weight in 1/256: 256 at the first interior cell (distance 3), 0 at the band's end.
    const auto d = static_cast<std::int64_t>(distance[index]) - 3;
    const auto weight = d >= span ? 0 : static_cast<std::int32_t>((span - d) * 256 / span);
    for (std::size_t channel = 0; channel < 3; ++channel) {
      const auto value = static_cast<std::int32_t>(offsets_rgb[index * 3U + channel]) * weight;
      offsets_rgb[index * 3U + channel] =
          static_cast<std::int16_t>(value >= 0 ? (value + 128) / 256 : -((-value + 128) / 256));
    }
  }
}

std::array<std::int32_t, 4> heal_alpha_offsets(const std::array<float, 4>& destination,
                                               const std::array<float, 4>& source, float scale) noexcept {
  const auto clamp_offset = [](double value) {
    return static_cast<std::int32_t>(std::clamp<long>(std::lround(value), -32767L, 32767L));
  };
  std::array<std::int32_t, 4> offsets{};
  for (std::size_t channel = 0; channel < 3U; ++channel) {
    const auto premultiplied = (static_cast<double>(destination[channel]) * destination[3] -
                                static_cast<double>(source[channel]) * source[3]) /
                               255.0;
    offsets[channel] = clamp_offset(premultiplied * scale);
  }
  offsets[3] = clamp_offset((static_cast<double>(destination[3]) - source[3]) * scale);
  return offsets;
}

std::array<float, 4> heal_with_alpha(const std::array<float, 4>& source, const std::int16_t* color_offsets,
                                     std::int16_t alpha_offset, float scale, bool unbounded_color) noexcept {
  std::array<float, 4> healed{};
  const auto alpha = std::clamp(static_cast<double>(source[3]) + static_cast<double>(alpha_offset) / scale, 0.0, 255.0);
  healed[3] = static_cast<float>(alpha);
  for (std::size_t channel = 0; channel < 3U; ++channel) {
    if (alpha <= 0.0) {
      healed[channel] = source[channel];
      continue;
    }
    const auto premultiplied = static_cast<double>(source[channel]) * source[3] / 255.0 +
                               static_cast<double>(color_offsets[channel]) / scale;
    const auto value = std::max(0.0, premultiplied * 255.0 / alpha);
    healed[channel] = static_cast<float>(unbounded_color ? value : std::min(value, 255.0));
  }
  return healed;
}

}  // namespace patchy
