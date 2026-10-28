#include "filters/stylize_render_filters.hpp"

#include "filters/filter_kernels.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace patchy {
namespace {

// splitmix64 with an explicit 53-bit uniform mapping to [0, 1) (AGENTS.md:
// deterministic across toolchains).
struct SplitMix64 {
  std::uint64_t state;
  std::uint64_t next() noexcept {
    auto z = (state += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27U)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31U);
  }
  double uniform() noexcept { return static_cast<double>(next() >> 11U) * 0x1.0p-53; }
};

[[nodiscard]] SplitMix64 seeded(std::uint64_t salt, std::int64_t a, std::int64_t b,
                                std::int64_t seed) noexcept {
  SplitMix64 rng{salt ^ (static_cast<std::uint64_t>(a) * 0x9e3779b97f4a7c15ULL) ^
                 (static_cast<std::uint64_t>(b) * 0xc2b2ae3d27d4eb4fULL) ^
                 (static_cast<std::uint64_t>(seed) * 0x165667b19e3779f9ULL)};
  rng.next();
  return rng;
}

void report_rows(const FilterProgress* progress, std::int32_t y, std::int32_t height,
                 FilterProgressStage stage) {
  if ((y & 15) == 0 || y == height) {
    report_kernel_progress(progress, y, height, stage);
  }
}

[[nodiscard]] std::uint8_t to_byte(double unit) noexcept {
  return static_cast<std::uint8_t>(std::clamp(std::floor(unit * 255.0 + 0.5), 0.0, 255.0));
}

[[nodiscard]] double smoothstep(double edge0, double edge1, double value) noexcept {
  const auto t = std::clamp((value - edge0) / (edge1 - edge0), 0.0, 1.0);
  return t * t * (3.0 - 2.0 * t);
}

// Smooth value noise on a splitmix64 lattice, in [0, 1].
[[nodiscard]] double lattice_noise(double x, double y, std::uint64_t salt) noexcept {
  const auto fx = std::floor(x);
  const auto fy = std::floor(y);
  const auto x0 = static_cast<std::int64_t>(fx);
  const auto y0 = static_cast<std::int64_t>(fy);
  const auto tx = smoothstep(0.0, 1.0, x - fx);
  const auto ty = smoothstep(0.0, 1.0, y - fy);
  const auto at = [salt](std::int64_t lx, std::int64_t ly) {
    return seeded(salt, lx, ly, 0).uniform();
  };
  const auto top = at(x0, y0) * (1.0 - tx) + at(x0 + 1, y0) * tx;
  const auto bottom = at(x0, y0 + 1) * (1.0 - tx) + at(x0 + 1, y0 + 1) * tx;
  return top * (1.0 - ty) + bottom * ty;
}

[[nodiscard]] double fractal_noise(double x, double y, int octaves, std::uint64_t salt) noexcept {
  double value = 0.0;
  double amplitude = 1.0;
  double total = 0.0;
  for (int octave = 0; octave < octaves; ++octave) {
    value += lattice_noise(x, y, salt + static_cast<std::uint64_t>(octave) * 0x51ed27U) * amplitude;
    total += amplitude;
    amplitude *= 0.5;
    x *= 2.0;
    y *= 2.0;
  }
  return value / total;
}

// ---------------------------------------------------------------- Wind

// One row's streak pass: every premultiplied channel and alpha keeps the
// larger of the pixel and the carried value, which falls by `fall` per pixel
// (units of 1/65025), so a bright or opaque feature fades out downwind.
void streak_row(std::uint8_t* row, std::int32_t width, bool from_left, std::int32_t fall) {
  std::array<std::int32_t, 4> carried{0, 0, 0, 0};
  for (std::int32_t step = 0; step < width; ++step) {
    const auto x = from_left ? step : width - 1 - step;
    auto* px = row + static_cast<std::size_t>(x) * 4U;
    const std::int32_t alpha = px[3];
    const std::array<std::int32_t, 4> value{px[0] * alpha, px[1] * alpha, px[2] * alpha, alpha * 255};
    for (std::size_t channel = 0; channel < 4U; ++channel) {
      carried[channel] = std::max(value[channel], carried[channel] - fall);
    }
    const auto out_alpha = carried[3];
    if (out_alpha <= 0) {
      px[0] = px[1] = px[2] = px[3] = 0;
      continue;
    }
    for (std::size_t channel = 0; channel < 3U; ++channel) {
      px[channel] = static_cast<std::uint8_t>(
          std::min<std::int32_t>(255, (carried[channel] * 255 + out_alpha / 2) / out_alpha));
    }
    px[3] = static_cast<std::uint8_t>((out_alpha + 127) / 255);
  }
}

}  // namespace

void render_wind(PixelBuffer& rgba, WindMethod method, bool from_left, int length,
                 const FilterProgress* progress) {
  const auto width = rgba.width();
  const auto height = rgba.height();
  length = std::clamp(length, 1, 1000);
  std::vector<std::uint8_t> shifted(static_cast<std::size_t>(width) * 4U);
  // Stagger moves rows in bands of random height; the sequence runs top to
  // bottom from one generator, so it is repeatable.
  auto band_rng = seeded(0x53746167ULL, 0, 0, 0);
  std::int32_t band_rows = 0;
  std::int32_t band_shift = 0;
  for (std::int32_t y = 0; y < height; ++y) {
    report_rows(progress, y, height, FilterProgressStage::Distorting);
    auto rng = seeded(0x57696e64ULL, y, 0, 0);
    const auto chance = rng.uniform();
    const auto spread = rng.uniform();
    double streak = 0.0;
    if (method == WindMethod::Wind) {
      // Only some rows streak, by varied lengths.
      streak = chance < 0.35 ? 0.0 : static_cast<double>(length) * (0.25 + 0.75 * spread * spread);
    } else if (method == WindMethod::Blast) {
      streak = static_cast<double>(length) * (1.5 + 1.5 * spread);
    } else {
      streak = static_cast<double>(length) * 0.35 * spread;
    }
    auto* row = rgba.pixel(0, y);
    if (method == WindMethod::Stagger) {
      if (band_rows <= 0) {
        band_rows = 1 + static_cast<std::int32_t>(band_rng.next() % 6U);
        band_shift = static_cast<std::int32_t>(band_rng.next() %
                                               static_cast<std::uint64_t>(length + 1));
      }
      --band_rows;
      // Content moves downwind; the upwind edge repeats.
      for (std::int32_t x = 0; x < width; ++x) {
        const auto source = std::clamp(from_left ? x - band_shift : x + band_shift, 0, width - 1);
        std::copy(row + static_cast<std::size_t>(source) * 4U,
                  row + static_cast<std::size_t>(source) * 4U + 4U,
                  shifted.begin() + static_cast<std::ptrdiff_t>(x) * 4);
      }
      std::copy(shifted.begin(), shifted.end(), row);
    }
    if (streak >= 1.0) {
      const auto fall = std::max<std::int32_t>(1, static_cast<std::int32_t>(65025.0 / streak));
      streak_row(row, width, from_left, fall);
      if (method == WindMethod::Blast) {
        streak_row(row, width, from_left, fall);
      }
    }
  }
  report_kernel_progress(progress, height, height, FilterProgressStage::Distorting);
}

// ---------------------------------------------------------------- Tiles

void render_tiles(PixelBuffer& rgba, int count, int max_offset_percent, TilesFill fill,
                  RgbColor foreground, RgbColor background, int seed,
                  const FilterProgress* progress) {
  const auto width = rgba.width();
  const auto height = rgba.height();
  if (width <= 0 || height <= 0) {
    return;
  }
  const auto original = rgba;
  count = std::clamp(count, 1, 99);
  const auto tile = std::max<std::int32_t>(1, std::min(width, height) / count);
  const auto max_offset = tile * std::clamp(max_offset_percent, 1, 99) / 100;
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      auto* px = rgba.pixel(x, y);
      const auto* source = original.pixel(x, y);
      switch (fill) {
        case TilesFill::Background:
        case TilesFill::Foreground: {
          const auto color = fill == TilesFill::Background ? background : foreground;
          px[0] = color.red;
          px[1] = color.green;
          px[2] = color.blue;
          px[3] = 255;
          break;
        }
        case TilesFill::Inverse:
          px[0] = static_cast<std::uint8_t>(255 - source[0]);
          px[1] = static_cast<std::uint8_t>(255 - source[1]);
          px[2] = static_cast<std::uint8_t>(255 - source[2]);
          break;
        case TilesFill::Unaltered:
          break;
        case TilesFill::Transparent:
          px[0] = px[1] = px[2] = px[3] = 0;
          break;
      }
    }
  }
  const auto columns = (width + tile - 1) / tile;
  const auto rows = (height + tile - 1) / tile;
  for (std::int32_t row = 0; row < rows; ++row) {
    report_rows(progress, row, rows, FilterProgressStage::Distorting);
    for (std::int32_t column = 0; column < columns; ++column) {
      auto rng = seeded(0x54696c65ULL, column, row, seed);
      const auto span = static_cast<std::uint64_t>(max_offset * 2 + 1);
      const auto dx = static_cast<std::int32_t>(rng.next() % span) - max_offset;
      const auto dy = static_cast<std::int32_t>(rng.next() % span) - max_offset;
      const auto left = column * tile;
      const auto top = row * tile;
      const auto right = std::min(width, left + tile);
      const auto bottom = std::min(height, top + tile);
      for (std::int32_t y = top; y < bottom; ++y) {
        const auto target_y = y + dy;
        if (target_y < 0 || target_y >= height) {
          continue;
        }
        for (std::int32_t x = left; x < right; ++x) {
          const auto target_x = x + dx;
          if (target_x < 0 || target_x >= width) {
            continue;
          }
          const auto* source = original.pixel(x, y);
          std::copy(source, source + 4, rgba.pixel(target_x, target_y));
        }
      }
    }
  }
  report_kernel_progress(progress, rows, rows, FilterProgressStage::Distorting);
}

// ---------------------------------------------------------------- Extrude

namespace {

struct ExtrudeCell {
  std::int32_t left{0};
  std::int32_t top{0};
  std::int32_t width{0};
  std::int32_t height{0};
  std::array<std::uint8_t, 4> color{};
  double raise{0.0};
  std::size_t index{0};
};

void fill_square(PixelBuffer& rgba, std::int32_t left, std::int32_t top, std::int32_t width,
                 std::int32_t height, const std::array<std::uint8_t, 4>& color) {
  const auto x0 = std::max<std::int32_t>(0, left);
  const auto y0 = std::max<std::int32_t>(0, top);
  const auto x1 = std::min(rgba.width(), left + width);
  const auto y1 = std::min(rgba.height(), top + height);
  for (std::int32_t y = y0; y < y1; ++y) {
    for (std::int32_t x = x0; x < x1; ++x) {
      std::copy(color.begin(), color.end(), rgba.pixel(x, y));
    }
  }
}

[[nodiscard]] std::array<std::uint8_t, 4> shaded(const std::array<std::uint8_t, 4>& color,
                                                 double factor) noexcept {
  std::array<std::uint8_t, 4> result = color;
  for (std::size_t channel = 0; channel < 3U; ++channel) {
    result[channel] = static_cast<std::uint8_t>(
        std::clamp(std::floor(static_cast<double>(color[channel]) * factor + 0.5), 0.0, 255.0));
  }
  return result;
}

}  // namespace

void render_extrude(PixelBuffer& rgba, bool pyramids, int size, int depth, bool level_based,
                    bool solid_front, int seed, const FilterProgress* progress) {
  const auto width = rgba.width();
  const auto height = rgba.height();
  if (width <= 0 || height <= 0) {
    return;
  }
  const auto original = rgba;
  size = std::clamp(size, 2, 255);
  depth = std::clamp(depth, 1, 255);
  std::vector<ExtrudeCell> cells;
  for (std::int32_t top = 0; top < height; top += size) {
    for (std::int32_t left = 0; left < width; left += size) {
      ExtrudeCell cell;
      cell.left = left;
      cell.top = top;
      cell.width = std::min(size, width - left);
      cell.height = std::min(size, height - top);
      cell.index = cells.size();
      std::array<std::int64_t, 4> sum{0, 0, 0, 0};
      for (std::int32_t y = top; y < top + cell.height; ++y) {
        for (std::int32_t x = left; x < left + cell.width; ++x) {
          const auto* px = original.pixel(x, y);
          for (std::size_t channel = 0; channel < 3U; ++channel) {
            sum[channel] += static_cast<std::int64_t>(px[channel]) * px[3];
          }
          sum[3] += px[3];
        }
      }
      const auto area = static_cast<std::int64_t>(cell.width) * cell.height;
      if (sum[3] > 0) {
        for (std::size_t channel = 0; channel < 3U; ++channel) {
          cell.color[channel] = static_cast<std::uint8_t>((sum[channel] + sum[3] / 2) / sum[3]);
        }
      }
      cell.color[3] = static_cast<std::uint8_t>((sum[3] + area / 2) / area);
      if (level_based) {
        cell.raise = static_cast<double>(cell.color[0] * 30 + cell.color[1] * 59 + cell.color[2] * 11) /
                     25500.0 * static_cast<double>(cell.color[3]) / 255.0;
      } else {
        cell.raise = seeded(0x45787472ULL, left / size, top / size, seed).uniform();
      }
      cells.push_back(cell);
    }
  }
  // Lower blocks first, so higher ones cover them.
  std::stable_sort(cells.begin(), cells.end(),
                   [](const ExtrudeCell& a, const ExtrudeCell& b) { return a.raise < b.raise; });
  const auto center_x = static_cast<double>(width) / 2.0;
  const auto center_y = static_cast<double>(height) / 2.0;
  const auto half_diagonal = std::max(1.0, std::sqrt(center_x * center_x + center_y * center_y));
  const auto total = static_cast<std::int32_t>(cells.size());
  for (std::int32_t done = 0; done < total; ++done) {
    report_rows(progress, done, total, FilterProgressStage::Embossing);
    const auto& cell = cells[static_cast<std::size_t>(done)];
    if (cell.color[3] == 0) {
      continue;
    }
    const auto cx = static_cast<double>(cell.left) + static_cast<double>(cell.width) / 2.0;
    const auto cy = static_cast<double>(cell.top) + static_cast<double>(cell.height) / 2.0;
    const auto reach = cell.raise * static_cast<double>(depth) / half_diagonal;
    const auto shift_x = (cx - center_x) * reach;
    const auto shift_y = (cy - center_y) * reach;
    const auto steps = static_cast<std::int32_t>(
        std::ceil(std::sqrt(shift_x * shift_x + shift_y * shift_y)));
    // The sides: the cell swept from its base out to its front face, darker
    // near the base.
    for (std::int32_t step = 0; step < steps; ++step) {
      const auto t = static_cast<double>(step) / static_cast<double>(steps);
      const auto x = static_cast<std::int32_t>(std::floor(static_cast<double>(cell.left) + shift_x * t + 0.5));
      const auto y = static_cast<std::int32_t>(std::floor(static_cast<double>(cell.top) + shift_y * t + 0.5));
      fill_square(rgba, x, y, cell.width, cell.height, shaded(cell.color, 0.4 + 0.35 * t));
    }
    const auto front_x =
        static_cast<std::int32_t>(std::floor(static_cast<double>(cell.left) + shift_x + 0.5));
    const auto front_y =
        static_cast<std::int32_t>(std::floor(static_cast<double>(cell.top) + shift_y + 0.5));
    if (pyramids) {
      // Four triangles meeting at the cell center, lit from the upper left.
      for (std::int32_t y = 0; y < cell.height; ++y) {
        for (std::int32_t x = 0; x < cell.width; ++x) {
          const auto tx = front_x + x;
          const auto ty = front_y + y;
          if (tx < 0 || ty < 0 || tx >= width || ty >= height) {
            continue;
          }
          // Position relative to the center, scaled to the unit square.
          const auto u = (2.0 * (static_cast<double>(x) + 0.5) / static_cast<double>(cell.width)) - 1.0;
          const auto v = (2.0 * (static_cast<double>(y) + 0.5) / static_cast<double>(cell.height)) - 1.0;
          double factor = 1.0;
          if (std::abs(u) >= std::abs(v)) {
            factor = u < 0.0 ? 1.0 : 0.62;
          } else {
            factor = v < 0.0 ? 1.18 : 0.48;
          }
          const auto color = shaded(cell.color, factor);
          std::copy(color.begin(), color.end(), rgba.pixel(tx, ty));
        }
      }
    } else if (solid_front) {
      fill_square(rgba, front_x, front_y, cell.width, cell.height, cell.color);
    } else {
      for (std::int32_t y = 0; y < cell.height; ++y) {
        for (std::int32_t x = 0; x < cell.width; ++x) {
          const auto tx = front_x + x;
          const auto ty = front_y + y;
          if (tx < 0 || ty < 0 || tx >= width || ty >= height) {
            continue;
          }
          const auto* source = original.pixel(cell.left + x, cell.top + y);
          std::copy(source, source + 4, rgba.pixel(tx, ty));
        }
      }
    }
  }
  report_kernel_progress(progress, total, total, FilterProgressStage::Embossing);
}

// ---------------------------------------------------------------- Lens Flare

namespace {

struct FlareGhost {
  double position;  // along the flare line: 0 at the flare, 1 at the buffer center, 2 mirrored
  double radius;    // fraction of the buffer diagonal
  double strength;
  std::array<double, 3> color;
};

struct FlareDesign {
  double core_radius;
  double glow;
  int rays;
  double ray_strength;
  double ring_radius;
  double ring_strength;
  double streak;
  std::array<double, 3> tint;
  std::vector<FlareGhost> ghosts;
};

[[nodiscard]] FlareDesign flare_design(LensFlareStyle style) {
  switch (style) {
    case LensFlareStyle::Star:
      return {0.016, 0.4, 16, 1.3, 0.0, 0.0, 0.0, {1.0, 0.97, 0.92},
              {{0.7, 0.012, 0.3, {0.7, 0.8, 1.0}}, {1.35, 0.02, 0.2, {1.0, 0.85, 0.6}}}};
    case LensFlareStyle::Ring:
      return {0.02, 0.55, 0, 0.0, 0.16, 0.85, 0.0, {1.0, 0.92, 0.82},
              {{0.55, 0.018, 0.26, {0.9, 0.75, 1.0}},
               {1.2, 0.035, 0.2, {0.6, 1.0, 0.75}},
               {1.7, 0.05, 0.16, {1.0, 0.7, 0.5}}}};
    case LensFlareStyle::Anamorphic:
      return {0.014, 0.35, 0, 0.0, 0.0, 0.0, 1.3, {0.75, 0.88, 1.0},
              {{0.45, 0.01, 0.32, {0.45, 0.65, 1.0}},
               {1.25, 0.022, 0.24, {0.4, 0.6, 1.0}},
               {1.8, 0.04, 0.16, {0.55, 0.7, 1.0}}}};
    case LensFlareStyle::Classic:
      break;
  }
  return {0.02, 0.6, 10, 0.7, 0.11, 0.35, 0.0, {1.0, 0.94, 0.84},
          {{0.35, 0.010, 0.4, {1.0, 0.8, 0.5}},
           {0.75, 0.030, 0.22, {0.55, 0.85, 1.0}},
           {1.15, 0.016, 0.35, {0.8, 1.0, 0.6}},
           {1.45, 0.045, 0.16, {1.0, 0.6, 0.85}},
           {1.85, 0.022, 0.28, {0.6, 0.7, 1.0}}}};
}

// cos(n * theta) and sin(n * theta) of the unit vector (c, s) by complex powers:
// no trigonometric calls, so every toolchain agrees.
[[nodiscard]] std::array<double, 2> angle_multiple(double c, double s, int n) noexcept {
  double real = 1.0;
  double imaginary = 0.0;
  for (int i = 0; i < n; ++i) {
    const auto next_real = real * c - imaginary * s;
    imaginary = real * s + imaginary * c;
    real = next_real;
  }
  return {real, imaginary};
}

}  // namespace

void render_lens_flare(PixelBuffer& rgba, double center_x_percent, double center_y_percent,
                       int brightness_percent, LensFlareStyle style,
                       const FilterProgress* progress) {
  const auto width = rgba.width();
  const auto height = rgba.height();
  if (width <= 0 || height <= 0) {
    return;
  }
  const auto design = flare_design(style);
  const auto brightness = static_cast<double>(std::clamp(brightness_percent, 10, 300)) / 100.0;
  const auto flare_x = static_cast<double>(std::max(0, width - 1)) * std::clamp(center_x_percent, 0.0, 100.0) / 100.0;
  const auto flare_y = static_cast<double>(std::max(0, height - 1)) * std::clamp(center_y_percent, 0.0, 100.0) / 100.0;
  const auto middle_x = static_cast<double>(std::max(0, width - 1)) / 2.0;
  const auto middle_y = static_cast<double>(std::max(0, height - 1)) / 2.0;
  const auto diagonal = std::max(
      1.0, std::sqrt(static_cast<double>(width) * width + static_cast<double>(height) * height));
  const auto core = design.core_radius * diagonal;
  const auto glow_radius = 0.1 * diagonal;
  const auto ray_falloff = 0.18 * diagonal;
  const auto ring_width = 0.010 * diagonal;
  for (std::int32_t y = 0; y < height; ++y) {
    report_rows(progress, y, height, FilterProgressStage::Filtering);
    for (std::int32_t x = 0; x < width; ++x) {
      const auto dx = static_cast<double>(x) - flare_x;
      const auto dy = static_cast<double>(y) - flare_y;
      const auto d2 = dx * dx + dy * dy;
      const auto d = std::sqrt(d2);
      std::array<double, 3> light{0.0, 0.0, 0.0};
      const auto add = [&light](const std::array<double, 3>& color, double amount) {
        for (std::size_t c = 0; c < 3U; ++c) {
          light[c] += color[c] * amount;
        }
      };
      add(design.tint, 1.0 / (1.0 + d2 / (core * core)));
      add({1.0, 0.82, 0.6}, design.glow / (1.0 + d2 / (glow_radius * glow_radius)));
      if (design.rays > 0 && d > 0.0) {
        const auto c = dx / d;
        const auto s = dy / d;
        const auto spoke = angle_multiple(c, s, design.rays);
        auto spike = (1.0 + spoke[0]) * 0.5;
        for (int i = 0; i < 5; ++i) {
          spike *= spike;  // power 32: thin rays
        }
        // Uneven ray lengths from a fixed lower harmonic.
        const auto lower = angle_multiple(c, s, 3);
        const auto variation = 0.55 + 0.45 * (1.0 + 0.6 * lower[0] - 0.8 * lower[1]) * 0.5;
        add(design.tint, design.ray_strength * spike * variation / (1.0 + d / (ray_falloff * variation)));
      }
      if (design.ring_strength > 0.0) {
        for (std::size_t channel = 0; channel < 3U; ++channel) {
          // A slightly wider red ring and narrower blue one: a faint rainbow edge.
          const auto radius = design.ring_radius * diagonal * (1.0 + 0.03 * (1.0 - static_cast<double>(channel)));
          const auto offset = (d - radius) / ring_width;
          const auto ring = 1.0 / (1.0 + offset * offset);
          light[channel] += design.ring_strength * ring * ring;
        }
      }
      if (design.streak > 0.0) {
        const auto sy = dy / (0.004 * diagonal);
        const auto sx = dx / (0.4 * diagonal);
        add(design.tint, design.streak / ((1.0 + sy * sy) * (1.0 + sx * sx)));
      }
      for (const auto& ghost : design.ghosts) {
        const auto gx = flare_x + (middle_x - flare_x) * ghost.position;
        const auto gy = flare_y + (middle_y - flare_y) * ghost.position;
        const auto radius = ghost.radius * diagonal;
        const auto ex = static_cast<double>(x) - gx;
        const auto ey = static_cast<double>(y) - gy;
        const auto distance = std::sqrt(ex * ex + ey * ey);
        if (distance >= radius + 1.0) {
          continue;
        }
        const auto edge = std::clamp((radius + 1.0 - distance) / (0.2 * radius + 1.0), 0.0, 1.0);
        const auto rim = distance / std::max(1.0, radius);
        add(ghost.color, ghost.strength * edge * (0.55 + 0.45 * rim * rim));
      }
      auto* px = rgba.pixel(x, y);
      const auto alpha = static_cast<double>(px[3]) / 255.0;
      double max_light = 0.0;
      std::array<double, 3> over_black{};
      for (std::size_t channel = 0; channel < 3U; ++channel) {
        const auto amount = std::clamp(light[channel] * brightness, 0.0, 1.0);
        max_light = std::max(max_light, amount);
        const auto base = static_cast<double>(px[channel]) / 255.0 * alpha;
        over_black[channel] = base + (1.0 - base) * amount;  // Screen over the layer on black
      }
      const auto out_alpha = alpha + (1.0 - alpha) * max_light;
      if (out_alpha <= 0.0) {
        continue;
      }
      for (std::size_t channel = 0; channel < 3U; ++channel) {
        px[channel] = to_byte(std::min(1.0, over_black[channel] / out_alpha));
      }
      px[3] = to_byte(out_alpha);
    }
  }
  report_kernel_progress(progress, height, height, FilterProgressStage::Filtering);
}

// ---------------------------------------------------------------- Flame

namespace {

struct PaletteStop {
  double at;
  std::array<double, 3> color;
};

[[nodiscard]] std::vector<PaletteStop> flame_stops(FlamePalette palette, RgbColor foreground,
                                                   RgbColor background) {
  const auto unit = [](RgbColor color) {
    return std::array<double, 3>{static_cast<double>(color.red) / 255.0,
                                 static_cast<double>(color.green) / 255.0,
                                 static_cast<double>(color.blue) / 255.0};
  };
  switch (palette) {
    case FlamePalette::Blue:
      return {{0.0, {0.02, 0.05, 0.35}},
              {0.3, {0.05, 0.2, 0.85}},
              {0.6, {0.2, 0.6, 1.0}},
              {0.85, {0.6, 0.9, 1.0}},
              {1.0, {0.95, 0.98, 1.0}}};
    case FlamePalette::Green:
      return {{0.0, {0.02, 0.25, 0.04}},
              {0.3, {0.1, 0.6, 0.1}},
              {0.6, {0.45, 0.9, 0.2}},
              {0.85, {0.8, 1.0, 0.5}},
              {1.0, {0.97, 1.0, 0.88}}};
    case FlamePalette::Colors: {
      const auto outer = unit(foreground);
      const auto inner = unit(background);
      std::array<double, 3> middle{};
      for (std::size_t c = 0; c < 3U; ++c) {
        middle[c] = (outer[c] + inner[c]) * 0.5;
      }
      return {{0.0, outer}, {0.55, middle}, {1.0, inner}};
    }
    case FlamePalette::Classic:
      break;
  }
  return {{0.0, {0.35, 0.02, 0.0}},
          {0.25, {0.78, 0.1, 0.0}},
          {0.5, {1.0, 0.38, 0.02}},
          {0.75, {1.0, 0.72, 0.15}},
          {1.0, {1.0, 0.97, 0.78}}};
}

[[nodiscard]] std::array<double, 3> palette_color(const std::vector<PaletteStop>& stops, double t) {
  t = std::clamp(t, 0.0, 1.0);
  for (std::size_t i = 1; i < stops.size(); ++i) {
    if (t <= stops[i].at) {
      const auto span = stops[i].at - stops[i - 1].at;
      const auto f = span > 0.0 ? (t - stops[i - 1].at) / span : 1.0;
      std::array<double, 3> color{};
      for (std::size_t c = 0; c < 3U; ++c) {
        color[c] = stops[i - 1].color[c] * (1.0 - f) + stops[i].color[c] * f;
      }
      return color;
    }
  }
  return stops.back().color;
}

}  // namespace

void render_flame(PixelBuffer& rgba, double base_x_percent, double base_y_percent,
                  int length_percent, int width_percent, int turbulence, FlamePalette palette,
                  RgbColor foreground, RgbColor background, int seed,
                  const FilterProgress* progress) {
  const auto width = rgba.width();
  const auto height = rgba.height();
  if (width <= 0 || height <= 0) {
    return;
  }
  const auto stops = flame_stops(palette, foreground, background);
  const auto flame_height =
      std::max(1.0, static_cast<double>(height) * std::clamp(length_percent, 5, 100) / 100.0);
  const auto half_width =
      std::max(1.0, static_cast<double>(width) * std::clamp(width_percent, 1, 100) / 200.0);
  const auto base_x = static_cast<double>(std::max(0, width - 1)) * std::clamp(base_x_percent, 0.0, 100.0) / 100.0;
  const auto base_y = static_cast<double>(std::max(0, height - 1)) * std::clamp(base_y_percent, 0.0, 100.0) / 100.0;
  const auto turbulence_amount = static_cast<double>(std::clamp(turbulence, 0, 100)) / 100.0;
  // Tongue width follows the flame height, so a preview proxy shows the same flame.
  const auto tongue = std::max(2.0, flame_height * 0.07);
  const auto salt = static_cast<std::uint64_t>(std::clamp(seed, 1, 9999)) * 0x464c414dULL;
  const auto top_y = base_y - flame_height * 1.6;
  for (std::int32_t y = 0; y < height; ++y) {
    report_rows(progress, y, height, FilterProgressStage::Filtering);
    const auto fy = static_cast<double>(y);
    if (fy < top_y || fy > base_y + flame_height * 0.06) {
      continue;
    }
    const auto v = (base_y - fy) / flame_height;  // 0 at the base, 1 at the nominal tip
    const auto rise = std::max(0.0, v);
    for (std::int32_t x = 0; x < width; ++x) {
      const auto fx = static_cast<double>(x);
      // The licks sway sideways more the higher they reach.
      const auto sway = (fractal_noise(fx / (tongue * 3.0), fy / (tongue * 4.0), 3, salt + 1U) - 0.5) *
                        tongue * (2.0 + 6.0 * turbulence_amount) * rise;
      const auto sx = fx + sway;
      const auto across = std::abs(sx - base_x) / half_width;
      if (across >= 1.0) {
        continue;
      }
      // Lower flames toward the sides of the base, each column of licks reaching its
      // own height: narrow tall tongues with low gaps between them.
      const auto envelope = 1.0 - across * across;
      const auto column = fractal_noise(sx / tongue, 0.37, 3, salt + 2U);
      const auto reach = std::max(0.02, envelope * (0.12 + 1.25 * column * column));
      // Vertically stretched noise breaks the body into rising streaks.
      const auto streaks =
          fractal_noise(sx / (tongue * 0.7), (fy + rise * tongue * 2.0) / (tongue * 2.4), 3, salt + 3U);
      auto heat = 1.0 - rise / reach + (streaks - 0.5) * (0.6 + 1.0 * turbulence_amount) * std::min(1.0, rise * 3.0);
      // The sides of the base cool off; just below the base line the flame keeps its
      // color and only fades out.
      heat *= smoothstep(1.0, 0.8, across);
      const auto base_fade = v < 0.0 ? std::clamp(1.0 + v / 0.06, 0.0, 1.0) : 1.0;
      heat = std::clamp(heat, 0.0, 1.0);
      if (heat <= 0.0) {
        continue;
      }
      const auto color = palette_color(stops, heat);
      const auto coverage = smoothstep(0.0, 0.22, heat) * base_fade;
      auto* px = rgba.pixel(x, y);
      const auto alpha = static_cast<double>(px[3]) / 255.0;
      const auto out_alpha = coverage + alpha * (1.0 - coverage);
      if (out_alpha <= 0.0) {
        continue;
      }
      for (std::size_t channel = 0; channel < 3U; ++channel) {
        const auto under = static_cast<double>(px[channel]) / 255.0 * alpha * (1.0 - coverage);
        px[channel] = to_byte(std::min(1.0, (color[channel] * coverage + under) / out_alpha));
      }
      px[3] = to_byte(out_alpha);
    }
  }
  report_kernel_progress(progress, height, height, FilterProgressStage::Filtering);
}

}  // namespace patchy
