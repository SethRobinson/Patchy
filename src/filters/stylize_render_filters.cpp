#include "filters/stylize_render_filters.hpp"

#include "core/rect_utils.hpp"
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

std::array<double, 2> degrees_unit_vector(int degrees) noexcept {
  // cos and sin of whole degrees from fixed Taylor series on [0, 45] degrees
  // and the octant symmetries: basic arithmetic only, so every toolchain
  // agrees (no std::cos or std::sin).
  const auto reduced = ((degrees % 360) + 360) % 360;
  const auto quadrant = reduced / 90;
  const auto within = reduced % 90;
  const auto first_octant = [](int angle) {
    const auto x = static_cast<double>(angle) * (3.14159265358979323846 / 180.0);
    const auto x2 = x * x;
    const auto sine = x * (1.0 - x2 / 6.0 * (1.0 - x2 / 20.0 * (1.0 - x2 / 42.0 * (1.0 - x2 / 72.0))));
    const auto cosine =
        1.0 - x2 / 2.0 * (1.0 - x2 / 12.0 * (1.0 - x2 / 30.0 * (1.0 - x2 / 56.0 * (1.0 - x2 / 90.0))));
    return std::array<double, 2>{cosine, sine};
  };
  std::array<double, 2> unit{};
  if (within <= 45) {
    unit = first_octant(within);
  } else {
    const auto mirrored = first_octant(90 - within);
    unit = {mirrored[1], mirrored[0]};
  }
  switch (quadrant) {
    case 1:
      return {-unit[1], unit[0]};
    case 2:
      return {-unit[0], -unit[1]};
    case 3:
      return {unit[1], -unit[0]};
    default:
      return unit;
  }
}

void render_wind(PixelBuffer& rgba, WindMethod method, int angle_degrees, int length,
                 const FilterProgress* progress) {
  const auto width = rgba.width();
  const auto height = rgba.height();
  if (width <= 0 || height <= 0) {
    return;
  }
  length = std::clamp(length, 1, 1000);
  // The wind blows toward `angle_degrees` (0 to the right, 90 up). Pixels are
  // gathered into lines along it, one pixel per step of the major axis
  // (x for mostly horizontal wind), each line a "row" of the streak pass in
  // its downwind order. At 0 and 180 degrees the lines are the rows.
  const auto direction = degrees_unit_vector(angle_degrees);
  const auto dx = direction[0];
  const auto dy = -direction[1];  // image y grows downward
  const auto horizontal = std::abs(dx) >= std::abs(dy);
  const auto slope = horizontal ? dy / dx : dx / dy;  // minor-axis offset per major step
  const auto major = horizontal ? width : height;
  const auto minor = horizontal ? height : width;
  // Scanning the gathered line forward means travelling downwind.
  const auto forward = horizontal ? dx > 0.0 : dy > 0.0;
  const auto step_length = std::sqrt(1.0 + slope * slope);
  const auto offset_at = [slope](std::int32_t step) {
    return static_cast<std::int32_t>(std::floor(slope * static_cast<double>(step) + 0.5));
  };
  const auto first_offset = offset_at(0);
  const auto last_offset = offset_at(major - 1);
  const auto low = -std::max(first_offset, last_offset);
  const auto high = minor - 1 - std::min(first_offset, last_offset);
  std::vector<std::uint8_t> line(static_cast<std::size_t>(major) * 4U);
  std::vector<std::uint8_t> shifted(line.size());
  std::vector<std::int32_t> steps;
  steps.reserve(static_cast<std::size_t>(major));
  // Stagger moves lines in bands of random height; the sequence runs from the
  // first line to the last from one generator, so it is repeatable.
  auto band_rng = seeded(0x53746167ULL, 0, 0, 0);
  std::int32_t band_rows = 0;
  std::int32_t band_shift = 0;
  const auto total = high - low + 1;
  for (std::int32_t index = low; index <= high; ++index) {
    report_rows(progress, index - low, total, FilterProgressStage::Distorting);
    // The pixels of this line, in increasing major-axis order.
    steps.clear();
    for (std::int32_t step = 0; step < major; ++step) {
      const auto across = index + offset_at(step);
      if (across >= 0 && across < minor) {
        steps.push_back(step);
      }
    }
    if (steps.empty()) {
      continue;
    }
    const auto count = static_cast<std::int32_t>(steps.size());
    const auto pixel_at = [&](std::size_t position) {
      const auto step = steps[position];
      const auto across = index + offset_at(step);
      return horizontal ? rgba.pixel(step, across) : rgba.pixel(across, step);
    };
    for (std::size_t position = 0; position < steps.size(); ++position) {
      const auto* px = pixel_at(position);
      std::copy(px, px + 4, line.begin() + static_cast<std::ptrdiff_t>(position) * 4);
    }
    auto rng = seeded(0x57696e64ULL, index, 0, 0);
    const auto chance = rng.uniform();
    const auto spread = rng.uniform();
    double streak = 0.0;
    if (method == WindMethod::Wind) {
      // Only some lines streak, by varied lengths.
      streak = chance < 0.35 ? 0.0 : static_cast<double>(length) * (0.25 + 0.75 * spread * spread);
    } else if (method == WindMethod::Blast) {
      streak = static_cast<double>(length) * (1.5 + 1.5 * spread);
    } else {
      streak = static_cast<double>(length) * 0.35 * spread;
    }
    if (method == WindMethod::Stagger) {
      if (band_rows <= 0) {
        band_rows = 1 + static_cast<std::int32_t>(band_rng.next() % 6U);
        band_shift = static_cast<std::int32_t>(band_rng.next() %
                                               static_cast<std::uint64_t>(length + 1));
      }
      --band_rows;
      // Content moves downwind; the upwind end repeats.
      for (std::int32_t x = 0; x < count; ++x) {
        const auto source = std::clamp(forward ? x - band_shift : x + band_shift, 0, count - 1);
        std::copy(line.begin() + static_cast<std::ptrdiff_t>(source) * 4,
                  line.begin() + static_cast<std::ptrdiff_t>(source) * 4 + 4,
                  shifted.begin() + static_cast<std::ptrdiff_t>(x) * 4);
      }
      std::copy(shifted.begin(), shifted.begin() + static_cast<std::ptrdiff_t>(count) * 4, line.begin());
    }
    if (streak >= 1.0) {
      // The fall is per pixel of travel, so a diagonal streak is as long as a level one.
      const auto fall = std::max<std::int32_t>(1, static_cast<std::int32_t>(65025.0 / streak * step_length));
      streak_row(line.data(), count, forward, fall);
      if (method == WindMethod::Blast) {
        streak_row(line.data(), count, forward, fall);
      }
    }
    for (std::size_t position = 0; position < steps.size(); ++position) {
      auto* px = pixel_at(position);
      std::copy(line.begin() + static_cast<std::ptrdiff_t>(position) * 4,
                line.begin() + static_cast<std::ptrdiff_t>(position) * 4 + 4, px);
    }
  }
  report_kernel_progress(progress, total, total, FilterProgressStage::Distorting);
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

using Rgb = std::array<double, 3>;

// One preset's look and its defaults, scaled by the dialog's percentages.
struct FlameStyle {
  std::vector<PaletteStop> stops;  // heat 0 (edge) to 1 (core)
  Rgb core;                        // the hottest color
  Rgb root;                        // tint at the base of each tongue
  double root_strength;
  Rgb smoke_color;
  double tongue;     // tongue width over flame height
  double variation;  // spread of tongue heights, 0..1
  double density;
  double turbulence;
  double flicker;    // noise scale factor
  double glow;
  double smoke;
  double sparks;
  double softness;   // edge softness factor
  double height;     // flame height factor
  bool single;       // Candle: one tongue at the base center
};

[[nodiscard]] Rgb unit_color(RgbColor color) {
  return {static_cast<double>(color.red) / 255.0, static_cast<double>(color.green) / 255.0,
          static_cast<double>(color.blue) / 255.0};
}

[[nodiscard]] std::vector<PaletteStop> fire_stops() {
  return {{0.0, {0.5, 0.04, 0.0}},
          {0.3, {0.9, 0.2, 0.02}},
          {0.55, {1.0, 0.45, 0.04}},
          {0.8, {1.0, 0.7, 0.16}},
          {1.0, {1.0, 0.88, 0.45}}};
}

[[nodiscard]] FlameStyle flame_style(FlamePreset preset, RgbColor foreground, RgbColor background) {
  const Rgb white{1.0, 0.99, 0.94};
  const Rgb soot{0.13, 0.12, 0.11};
  const Rgb none{0.0, 0.0, 0.0};
  //                stops, core, root, root_strength, smoke color,
  //                tongue, variation, density, turbulence, flicker,
  //                glow, smoke, sparks, softness, height, single
  switch (preset) {
    case FlamePreset::Candle:
      return {{{0.0, {0.9, 0.28, 0.03}}, {0.35, {1.0, 0.56, 0.12}}, {0.65, {1.0, 0.82, 0.38}},
               {1.0, {1.0, 0.96, 0.8}}},
              {1.0, 1.0, 0.96}, {0.25, 0.45, 1.0}, 0.75, soot,
              0.34, 0.1, 1.0, 0.22, 0.7, 1.1, 0.0, 0.0, 1.25, 0.6, true};
    case FlamePreset::Torch:
      return {fire_stops(), white, none, 0.0, soot, 0.24, 0.45, 0.55, 1.25, 1.1, 0.8, 0.3, 0.9, 1.0, 1.0,
              false};
    case FlamePreset::Wildfire:
      return {{{0.0, {0.35, 0.0, 0.0}}, {0.25, {0.8, 0.08, 0.0}}, {0.5, {1.0, 0.32, 0.02}},
               {0.75, {1.0, 0.6, 0.1}}, {1.0, {1.0, 0.82, 0.35}}},
              {1.0, 0.96, 0.75}, none, 0.0, {0.1, 0.09, 0.085},
              0.26, 0.7, 1.7, 1.45, 1.2, 0.5, 1.0, 1.0, 1.0, 1.15, false};
    case FlamePreset::FireWall:
      return {fire_stops(), white, none, 0.0, soot, 0.32, 0.25, 2.2, 1.1, 1.0, 0.5, 0.4, 0.4, 1.0, 1.0, false};
    case FlamePreset::FireLine:
      return {fire_stops(), white, none, 0.0, soot, 0.55, 0.4, 2.5, 0.9, 0.8, 0.7, 0.1, 0.3, 1.0, 0.35, false};
    case FlamePreset::Embers:
      return {{{0.0, {0.3, 0.02, 0.0}}, {0.4, {0.72, 0.09, 0.0}}, {0.75, {1.0, 0.36, 0.05}},
               {1.0, {1.0, 0.72, 0.32}}},
              {1.0, 0.85, 0.5}, none, 0.0, soot,
              0.4, 0.6, 1.2, 0.8, 1.0, 0.9, 0.2, 3.0, 1.0, 0.3, false};
    case FlamePreset::SmokyFire:
      return {{{0.0, {0.3, 0.05, 0.01}}, {0.35, {0.7, 0.2, 0.04}}, {0.7, {0.95, 0.46, 0.1}},
               {1.0, {1.0, 0.76, 0.42}}},
              {1.0, 0.9, 0.65}, none, 0.0, {0.09, 0.085, 0.08},
              0.3, 0.6, 1.1, 1.2, 1.0, 0.3, 3.2, 0.3, 1.0, 1.0, false};
    case FlamePreset::SoftGlow:
      return {{{0.0, {0.9, 0.36, 0.05}}, {0.5, {1.0, 0.6, 0.2}}, {0.8, {1.0, 0.86, 0.56}},
               {1.0, {1.0, 0.97, 0.88}}},
              {1.0, 1.0, 0.96}, none, 0.0, soot,
              0.36, 0.3, 1.0, 0.45, 0.8, 1.9, 0.0, 0.0, 2.0, 1.0, false};
    case FlamePreset::BlueGas:
      return {{{0.0, {0.02, 0.08, 0.45}}, {0.35, {0.08, 0.3, 0.95}}, {0.7, {0.25, 0.55, 1.0}},
               {1.0, {0.6, 0.85, 1.0}}},
              {1.0, 1.0, 1.0}, none, 0.0, soot,
              0.26, 0.2, 1.3, 0.45, 0.6, 0.8, 0.0, 0.0, 1.0, 1.0, false};
    case FlamePreset::GreenMagic:
      return {{{0.0, {0.02, 0.3, 0.06}}, {0.35, {0.12, 0.7, 0.15}}, {0.7, {0.5, 0.92, 0.25}},
               {1.0, {0.78, 1.0, 0.5}}},
              {0.96, 1.0, 0.9}, none, 0.0, {0.05, 0.18, 0.07},
              0.3, 0.5, 1.0, 1.1, 1.0, 1.3, 0.15, 1.3, 1.0, 1.0, false};
    case FlamePreset::PurpleSpirit:
      return {{{0.0, {0.25, 0.02, 0.4}}, {0.35, {0.6, 0.1, 0.85}}, {0.7, {0.85, 0.4, 1.0}},
               {1.0, {0.97, 0.72, 1.0}}},
              {1.0, 0.96, 1.0}, none, 0.0, {0.15, 0.05, 0.22},
              0.3, 0.55, 1.0, 1.2, 1.0, 1.5, 0.4, 0.8, 1.0, 1.0, false};
    case FlamePreset::ColdFlame:
      return {{{0.0, {0.3, 0.55, 0.8}}, {0.4, {0.55, 0.8, 0.95}}, {0.75, {0.8, 0.95, 1.0}},
               {1.0, {0.97, 1.0, 1.0}}},
              {1.0, 1.0, 1.0}, none, 0.0, {0.7, 0.8, 0.88},
              0.28, 0.4, 1.0, 0.7, 0.9, 1.1, 0.25, 0.5, 1.0, 1.0, false};
    case FlamePreset::Colors: {
      const auto outer = unit_color(foreground);
      const auto inner = unit_color(background);
      Rgb middle{};
      for (std::size_t c = 0; c < 3U; ++c) {
        middle[c] = (outer[c] + inner[c]) * 0.5;
      }
      return {{{0.0, outer}, {0.55, middle}, {1.0, inner}}, inner, none, 0.0, soot,
              0.3, 0.5, 1.0, 1.0, 1.0, 0.6, 0.25, 0.4, 1.0, 1.0, false};
    }
    case FlamePreset::Campfire:
      break;
  }
  return {fire_stops(), white, none, 0.0, soot, 0.3, 0.55, 1.0, 1.0, 1.0, 0.6, 0.35, 0.5, 1.0, 1.0, false};
}

[[nodiscard]] Rgb palette_color(const std::vector<PaletteStop>& stops, double t) {
  t = std::clamp(t, 0.0, 1.0);
  for (std::size_t i = 1; i < stops.size(); ++i) {
    if (t <= stops[i].at) {
      const auto span = stops[i].at - stops[i - 1].at;
      const auto f = span > 0.0 ? (t - stops[i - 1].at) / span : 1.0;
      Rgb color{};
      for (std::size_t c = 0; c < 3U; ++c) {
        color[c] = stops[i - 1].color[c] * (1.0 - f) + stops[i].color[c] * f;
      }
      return color;
    }
  }
  return stops.back().color;
}

// One flame tongue: a root on the emitter, the unit direction it rises in,
// and its size in pixels.
struct Tongue {
  double x;
  double y;
  double dx;
  double dy;
  double height;
  double width;
};

struct Spark {
  double x;
  double y;
  double radius;
  double strength;
};

// The tongue width profile along its height v (0 root, 1 tip): a teardrop,
// widest about a quarter of the way up.
[[nodiscard]] double tongue_profile(double v) noexcept {
  if (v <= 0.0) {
    return 0.55;
  }
  if (v >= 1.0) {
    return 0.0;
  }
  return std::sqrt(1.0 - v) * (0.55 + 1.8 * v * (1.0 - v));
}

// A float map over `area` of buffer pixels, filled by `value(x, y)`.
struct NoiseMap {
  Rect area{};
  std::vector<float> values;
  template <typename Fn>
  void fill(Rect region, Fn&& value) {
    area = region;
    values.assign(static_cast<std::size_t>(std::max(0, region.width)) *
                      static_cast<std::size_t>(std::max(0, region.height)),
                  0.0F);
    for (std::int32_t y = 0; y < region.height; ++y) {
      for (std::int32_t x = 0; x < region.width; ++x) {
        values[static_cast<std::size_t>(y) * static_cast<std::size_t>(region.width) +
               static_cast<std::size_t>(x)] =
            static_cast<float>(value(static_cast<double>(region.x + x), static_cast<double>(region.y + y)));
      }
    }
  }
  [[nodiscard]] double at(std::int32_t x, std::int32_t y) const {
    return static_cast<double>(values[static_cast<std::size_t>(y - area.y) * static_cast<std::size_t>(area.width) +
                                      static_cast<std::size_t>(x - area.x)]);
  }
};

// Three box blurs in a row approximate a Gaussian; running sums in a fixed
// order keep it repeatable.
void box_blur_plane(std::vector<double>& plane, std::int32_t width, std::int32_t height, std::int32_t radius) {
  if (radius <= 0 || width <= 0 || height <= 0) {
    return;
  }
  std::vector<double> line(static_cast<std::size_t>(std::max(width, height)));
  const auto window = static_cast<double>(2 * radius + 1);
  for (int pass = 0; pass < 3; ++pass) {
    for (std::int32_t y = 0; y < height; ++y) {
      auto* row = plane.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
      double sum = 0.0;
      for (std::int32_t x = -radius; x <= radius; ++x) {
        sum += row[std::clamp(x, 0, width - 1)];
      }
      for (std::int32_t x = 0; x < width; ++x) {
        line[static_cast<std::size_t>(x)] = sum / window;
        sum += row[std::min(width - 1, x + radius + 1)] - row[std::max(0, x - radius)];
      }
      std::copy(line.begin(), line.begin() + width, row);
    }
    for (std::int32_t x = 0; x < width; ++x) {
      double sum = 0.0;
      const auto at = [&](std::int32_t y) -> double& {
        return plane[static_cast<std::size_t>(std::clamp(y, 0, height - 1)) * static_cast<std::size_t>(width) +
                     static_cast<std::size_t>(x)];
      };
      for (std::int32_t y = -radius; y <= radius; ++y) {
        sum += at(y);
      }
      for (std::int32_t y = 0; y < height; ++y) {
        line[static_cast<std::size_t>(y)] = sum / window;
        sum += at(y + radius + 1) - at(y - radius);
      }
      for (std::int32_t y = 0; y < height; ++y) {
        at(y) = line[static_cast<std::size_t>(y)];
      }
    }
  }
}

}  // namespace

void render_flame(PixelBuffer& rgba, const FlameSettings& settings, const FilterProgress* progress) {
  const auto width = rgba.width();
  const auto height = rgba.height();
  if (width <= 0 || height <= 0) {
    return;
  }
  const auto style = flame_style(settings.preset, settings.foreground, settings.background);
  const auto percent = [](int value, int low, int high) {
    return static_cast<double>(std::clamp(value, low, high)) / 100.0;
  };
  const auto turbulence = percent(settings.turbulence, 0, 100) * 2.0 * style.turbulence;
  const auto flicker = std::max(0.1, percent(settings.flicker_percent, 10, 400) * style.flicker);
  const auto density = percent(settings.density_percent, 10, 400) * style.density;
  const auto core = percent(settings.core_percent, 0, 200);
  const auto glow = percent(settings.glow_percent, 0, 200) * style.glow;
  const auto smoke = percent(settings.smoke_percent, 0, 200) * style.smoke;
  const auto sparks = percent(settings.sparks_percent, 0, 200) * style.sparks;
  const auto opacity = percent(settings.opacity_percent, 0, 100);
  const auto seed = static_cast<std::uint64_t>(std::clamp(settings.seed, 1, 9999));
  const auto flame_height = std::max(
      4.0, static_cast<double>(height) * percent(settings.length_percent, 5, 100) * style.height);
  const auto tongue_width = std::max(2.0, flame_height * style.tongue);
  const auto spacing = std::max(1.0, tongue_width * 0.45 / std::max(0.05, density));
  const auto lean = degrees_unit_vector(90 - std::clamp(settings.lean_degrees, -60, 60));
  const auto up_x = lean[0];
  const auto up_y = -lean[1];

  // Tongue roots along the emitters: the base line, or the guide polylines.
  std::vector<Tongue> tongues;
  std::uint64_t tongue_index = 0;
  const auto add_tongue = [&](double x, double y, double dx, double dy, double envelope) {
    auto rng = seeded(0x546f6e67ULL, static_cast<std::int64_t>(tongue_index++), 0,
                      static_cast<std::int64_t>(seed));
    const auto size = 1.0 - style.variation * rng.uniform() * rng.uniform() * 1.6;
    const auto wide = 0.8 + 0.4 * rng.uniform();
    tongues.push_back({x, y, dx, dy, flame_height * envelope * std::max(0.15, size),
                       tongue_width * wide * (0.6 + 0.4 * envelope)});
  };
  if (!settings.follow_path) {
    const auto base_x =
        static_cast<double>(std::max(0, width - 1)) * std::clamp(settings.base_x_percent, 0.0, 100.0) / 100.0;
    const auto base_y =
        static_cast<double>(std::max(0, height - 1)) * std::clamp(settings.base_y_percent, 0.0, 100.0) / 100.0;
    const auto half = std::max(1.0, static_cast<double>(width) * percent(settings.width_percent, 1, 100) / 2.0);
    if (style.single) {
      add_tongue(base_x, base_y, up_x, up_y, 1.0);
    } else {
      const auto count = std::max<std::int64_t>(1, static_cast<std::int64_t>(std::floor(2.0 * half / spacing)) + 1);
      for (std::int64_t i = 0; i < count; ++i) {
        auto rng = seeded(0x42617365ULL, i, 0, static_cast<std::int64_t>(seed));
        const auto t = count == 1 ? 0.5
                                  : std::clamp((static_cast<double>(i) + (rng.uniform() - 0.5) * 0.6) /
                                                   static_cast<double>(count - 1),
                                               0.0, 1.0);
        const auto edge = 2.0 * t - 1.0;
        // Lower flames toward the ends of the base.
        const auto envelope = 0.3 + 0.7 * std::sqrt(std::max(0.0, 1.0 - edge * edge));
        add_tongue(base_x - half + 2.0 * half * t, base_y, up_x, up_y, envelope);
      }
    }
  } else {
    std::int64_t step_index = 0;
    for (const auto& polyline : settings.guides) {
      double carried = 0.0;  // distance walked since the last tongue
      for (std::size_t i = 1; i < polyline.size(); ++i) {
        const auto ax = polyline[i - 1].x;
        const auto ay = polyline[i - 1].y;
        const auto sx = polyline[i].x - ax;
        const auto sy = polyline[i].y - ay;
        const auto length = std::sqrt(sx * sx + sy * sy);
        if (length <= 0.0) {
          continue;
        }
        const auto tx = sx / length;
        const auto ty = sy / length;
        auto along = spacing - carried;
        while (along <= length) {
          auto rng = seeded(0x50617468ULL, step_index++, 0, static_cast<std::int64_t>(seed));
          const auto jitter = (rng.uniform() - 0.5) * spacing * 0.5;
          const auto at = std::clamp(along + jitter, 0.0, length);
          auto dx = up_x;
          auto dy = up_y;
          if (settings.rise_from_path) {
            // The side of the path that faces up, nudged upward so the flames still rise.
            auto nx = ty;
            auto ny = -tx;
            if (ny > 0.0 || (ny == 0.0 && nx < 0.0)) {
              nx = -nx;
              ny = -ny;
            }
            dx = nx + up_x * 0.35;
            dy = ny + up_y * 0.35;
            const auto norm = std::sqrt(dx * dx + dy * dy);
            dx /= norm;
            dy /= norm;
          }
          add_tongue(ax + tx * at, ay + ty * at, dx, dy, 0.75 + 0.25 * rng.uniform());
          along += spacing;
        }
        carried = length - (along - spacing);
      }
    }
  }
  if (tongues.empty()) {
    report_kernel_progress(progress, 1, 1, FilterProgressStage::Filtering);
    return;
  }

  // The region every tongue, its smoke and its sparks can reach.
  const auto reach_of = [&](const Tongue& tongue) {
    const auto tall = tongue.height * (smoke > 0.0 || sparks > 0.0 ? 2.3 : 1.35);
    const auto wide = tongue.width * 2.6 + turbulence * tongue.width * 1.2;
    const auto x0 = std::min({tongue.x - wide, tongue.x + tongue.dx * tall - wide});
    const auto x1 = std::max({tongue.x + wide, tongue.x + tongue.dx * tall + wide});
    const auto y0 = std::min({tongue.y - wide, tongue.y + tongue.dy * tall - wide});
    const auto y1 = std::max({tongue.y + wide, tongue.y + tongue.dy * tall + wide});
    const auto left = std::max<std::int32_t>(0, static_cast<std::int32_t>(std::floor(x0)));
    const auto top = std::max<std::int32_t>(0, static_cast<std::int32_t>(std::floor(y0)));
    const auto right = std::min<std::int32_t>(width, static_cast<std::int32_t>(std::ceil(x1)) + 1);
    const auto bottom = std::min<std::int32_t>(height, static_cast<std::int32_t>(std::ceil(y1)) + 1);
    return Rect{left, top, std::max(0, right - left), std::max(0, bottom - top)};
  };
  Rect area{};
  for (const auto& tongue : tongues) {
    const auto reach = reach_of(tongue);
    area = area.empty() ? reach : unite_rect(area, reach);
  }
  if (area.empty()) {
    return;
  }

  // Shared noise fields, so neighbouring tongues sway and break up together:
  // sideways sway (broad), flicker (fine, stretched upward into streaks) and
  // smoke. Domain-warped value noise, splitmix64 lattice.
  const auto salt = seed * 0x464c414dULL;
  const auto sway_scale = tongue_width * 1.6 * flicker;
  const auto fine_scale = tongue_width * 0.42 * flicker;
  NoiseMap sway_map;
  NoiseMap flicker_map;
  NoiseMap smoke_map;
  sway_map.fill(area, [&](double x, double y) {
    return fractal_noise(x / sway_scale, y / (sway_scale * 1.4), 3, salt + 11U);
  });
  flicker_map.fill(area, [&](double x, double y) {
    const auto warp = (sway_map.at(static_cast<std::int32_t>(x), static_cast<std::int32_t>(y)) - 0.5) * 1.5;
    return fractal_noise(x / fine_scale + warp, y / (fine_scale * 2.4) + warp, 4, salt + 23U);
  });
  if (smoke > 0.0) {
    smoke_map.fill(area, [&](double x, double y) {
      return fractal_noise(x / (sway_scale * 0.7), y / (sway_scale * 0.9), 3, salt + 37U);
    });
  }
  report_kernel_progress(progress, 1, 4, FilterProgressStage::Filtering);

  const auto plane_size = static_cast<std::size_t>(area.width) * static_cast<std::size_t>(area.height);
  // Each pixel keeps its hottest tongue plus a share of the others, so
  // overlapping tongues brighten without washing the body out to white.
  std::vector<double> hottest(plane_size, 0.0);
  std::vector<double> summed(plane_size, 0.0);
  std::vector<double> rooted(plane_size, 0.0);  // base tint weight
  std::vector<double> smoky(plane_size, 0.0);
  std::vector<double> sparkle(plane_size, 0.0);
  const auto index_of = [&](std::int32_t x, std::int32_t y) {
    return static_cast<std::size_t>(y - area.y) * static_cast<std::size_t>(area.width) +
           static_cast<std::size_t>(x - area.x);
  };
  std::vector<Spark> spark_list;
  std::uint64_t spark_index = 0;
  for (const auto& tongue : tongues) {
    const auto reach = reach_of(tongue);
    const auto px = -tongue.dy;  // across the tongue
    const auto py = tongue.dx;
    for (std::int32_t y = reach.y; y < reach.y + reach.height; ++y) {
      for (std::int32_t x = reach.x; x < reach.x + reach.width; ++x) {
        const auto rx = static_cast<double>(x) - tongue.x;
        const auto ry = static_cast<double>(y) - tongue.y;
        const auto v = (rx * tongue.dx + ry * tongue.dy) / tongue.height;
        if (v < -0.12 || v > 2.2) {
          continue;
        }
        const auto rise = std::max(0.0, v);
        const auto sway = (sway_map.at(x, y) - 0.5) * turbulence * 1.1 * rise * std::sqrt(rise);
        const auto u = (rx * px + ry * py) / tongue.width + sway;
        const auto index = index_of(x, y);
        if (v <= 1.0) {
          const auto profile = tongue_profile(v);
          if (profile > 0.0) {
            const auto shape = 1.0 - (u / profile) * (u / profile);
            if (shape > 0.0) {
              auto heat = 0.86 * std::sqrt(shape) * (1.0 - 0.75 * rise);
              if (v < 0.0) {
                heat *= 1.0 + v / 0.12;
              }
              heat -= (flicker_map.at(x, y) - 0.42) * (0.25 + 0.55 * turbulence) * (0.3 + 1.1 * rise);
              heat = std::clamp(heat, 0.0, 1.0);
              hottest[index] = std::max(hottest[index], heat);
              summed[index] += heat;
              rooted[index] = std::max(rooted[index], heat * std::clamp(1.0 - v / 0.2, 0.0, 1.0));
            }
          }
        }
        if (smoke > 0.0 && v > 0.5) {
          const auto spread = 0.9 + 1.1 * (v - 0.5);
          const auto across = 1.0 - (u / spread) * (u / spread);
          if (across > 0.0) {
            const auto fade = smoothstep(0.5, 0.95, v) * (1.0 - smoothstep(1.3, 2.2, v));
            const auto puff = std::clamp((smoke_map.at(x, y) - 0.3) * 1.8, 0.0, 1.0);
            smoky[index] = std::max(smoky[index], smoke * 0.55 * across * fade * puff);
          }
        }
      }
    }
    if (sparks > 0.0) {
      auto rng = seeded(0x53706b73ULL, static_cast<std::int64_t>(spark_index++), 0,
                        static_cast<std::int64_t>(seed));
      const auto count = static_cast<int>(std::floor(sparks * 2.2 * rng.uniform() + rng.uniform()));
      for (int i = 0; i < count; ++i) {
        const auto v = 0.7 + 1.3 * rng.uniform();
        const auto u = (rng.uniform() - 0.5) * 3.0;
        spark_list.push_back({tongue.x + tongue.dx * v * tongue.height + px * u * tongue.width,
                              tongue.y + tongue.dy * v * tongue.height + py * u * tongue.width,
                              std::max(0.6, tongue_width * 0.025 * (0.5 + rng.uniform())),
                              0.6 + 0.4 * rng.uniform()});
      }
    }
  }
  report_kernel_progress(progress, 2, 4, FilterProgressStage::Filtering);

  // Sparks: soft dots with a short trail below them.
  for (const auto& spark : spark_list) {
    const auto trail = spark.radius * 4.0;
    const auto x0 = std::max(area.x, static_cast<std::int32_t>(std::floor(spark.x - spark.radius - 1.0 -
                                                                          std::abs(up_x) * trail)));
    const auto x1 = std::min(area.x + area.width - 1,
                             static_cast<std::int32_t>(std::ceil(spark.x + spark.radius + 1.0 + std::abs(up_x) * trail)));
    const auto y0 = std::max(area.y, static_cast<std::int32_t>(std::floor(spark.y - spark.radius - 1.0)));
    const auto y1 = std::min(area.y + area.height - 1,
                             static_cast<std::int32_t>(std::ceil(spark.y + spark.radius + 1.0 + trail)));
    for (std::int32_t y = y0; y <= y1; ++y) {
      for (std::int32_t x = x0; x <= x1; ++x) {
        const auto ex = static_cast<double>(x) - spark.x;
        const auto ey = static_cast<double>(y) - spark.y;
        // Distance to the trail segment from the spark back down its rise direction.
        const auto back = std::clamp(-(ex * up_x + ey * up_y), 0.0, trail);
        const auto cx = ex + up_x * back;
        const auto cy = ey + up_y * back;
        const auto d2 = (cx * cx + cy * cy) / (spark.radius * spark.radius);
        if (d2 >= 1.0) {
          continue;
        }
        const auto amount = spark.strength * (1.0 - d2) * (1.0 - 0.8 * back / trail);
        auto& cell = sparkle[index_of(x, y)];
        cell = std::max(cell, amount);
      }
    }
  }

  // Glow: the flame light, blurred on a grid of a quarter of the resolution.
  constexpr std::int32_t kGlowStep = 4;
  const auto glow_width = (area.width + kGlowStep - 1) / kGlowStep;
  const auto glow_height = (area.height + kGlowStep - 1) / kGlowStep;
  std::array<std::vector<double>, 3> glow_planes;
  const auto heat_at = [&](std::size_t index) {
    return std::min(1.0, hottest[index] + 0.15 * (summed[index] - hottest[index]));
  };
  if (glow > 0.0) {
    for (auto& plane : glow_planes) {
      plane.assign(static_cast<std::size_t>(glow_width) * static_cast<std::size_t>(glow_height), 0.0);
    }
    for (std::int32_t y = 0; y < area.height; ++y) {
      for (std::int32_t x = 0; x < area.width; ++x) {
        const auto heat = heat_at(static_cast<std::size_t>(y) * static_cast<std::size_t>(area.width) +
                                  static_cast<std::size_t>(x));
        if (heat <= 0.0) {
          continue;
        }
        const auto color = palette_color(style.stops, heat);
        const auto cell = static_cast<std::size_t>(y / kGlowStep) * static_cast<std::size_t>(glow_width) +
                          static_cast<std::size_t>(x / kGlowStep);
        for (std::size_t c = 0; c < 3U; ++c) {
          glow_planes[c][cell] += color[c] * heat / static_cast<double>(kGlowStep * kGlowStep);
        }
      }
    }
    const auto radius = std::max<std::int32_t>(1, static_cast<std::int32_t>(std::floor(tongue_width * 0.9 / kGlowStep)));
    for (auto& plane : glow_planes) {
      box_blur_plane(plane, glow_width, glow_height, radius);
    }
  }
  const auto glow_at = [&](std::int32_t x, std::int32_t y, std::size_t channel) {
    // Bilinear from the coarse grid (cell centers at step / 2).
    const auto gx = std::clamp((static_cast<double>(x - area.x) + 0.5) / kGlowStep - 0.5, 0.0,
                               static_cast<double>(glow_width - 1));
    const auto gy = std::clamp((static_cast<double>(y - area.y) + 0.5) / kGlowStep - 0.5, 0.0,
                               static_cast<double>(glow_height - 1));
    const auto x0 = static_cast<std::int32_t>(std::floor(gx));
    const auto y0 = static_cast<std::int32_t>(std::floor(gy));
    const auto x1 = std::min(glow_width - 1, x0 + 1);
    const auto y1 = std::min(glow_height - 1, y0 + 1);
    const auto fx = gx - static_cast<double>(x0);
    const auto fy = gy - static_cast<double>(y0);
    const auto& plane = glow_planes[channel];
    const auto at = [&](std::int32_t cx, std::int32_t cy) {
      return plane[static_cast<std::size_t>(cy) * static_cast<std::size_t>(glow_width) + static_cast<std::size_t>(cx)];
    };
    return (at(x0, y0) * (1.0 - fx) + at(x1, y0) * fx) * (1.0 - fy) +
           (at(x0, y1) * (1.0 - fx) + at(x1, y1) * fx) * fy;
  };
  report_kernel_progress(progress, 3, 4, FilterProgressStage::Filtering);

  const auto spark_color = palette_color(style.stops, 0.9);
  const auto edge = 0.28 * style.softness;
  for (std::int32_t y = area.y; y < area.y + area.height; ++y) {
    for (std::int32_t x = area.x; x < area.x + area.width; ++x) {
      const auto index = index_of(x, y);
      const auto heat = heat_at(index);
      std::array<double, 3> glow_light{0.0, 0.0, 0.0};
      double glow_max = 0.0;
      if (glow > 0.0) {
        for (std::size_t c = 0; c < 3U; ++c) {
          glow_light[c] = std::clamp(glow_at(x, y, c) * glow * 1.6 * opacity, 0.0, 1.0);
          glow_max = std::max(glow_max, glow_light[c]);
        }
      }
      if (heat <= 0.0 && smoky[index] <= 0.0 && sparkle[index] <= 0.0 && glow_max <= 0.0) {
        continue;
      }
      auto* px = rgba.pixel(x, y);
      auto alpha = static_cast<double>(px[3]) / 255.0;
      std::array<double, 3> color{};  // premultiplied
      for (std::size_t c = 0; c < 3U; ++c) {
        color[c] = static_cast<double>(px[c]) / 255.0 * alpha;
      }
      const auto over = [&](const Rgb& paint, double coverage) {
        for (std::size_t c = 0; c < 3U; ++c) {
          color[c] = paint[c] * coverage + color[c] * (1.0 - coverage);
        }
        alpha = coverage + alpha * (1.0 - coverage);
      };
      if (smoky[index] > 0.0) {
        over(style.smoke_color, std::min(0.85, smoky[index]) * opacity);
      }
      if (glow_max > 0.0) {
        // Screened over the layer as if over black; transparency takes the light as alpha.
        for (std::size_t c = 0; c < 3U; ++c) {
          color[c] = color[c] + (1.0 - color[c]) * glow_light[c];
        }
        alpha = alpha + (1.0 - alpha) * glow_max;
      }
      if (heat > 0.0) {
        auto paint = palette_color(style.stops, heat);
        const auto hot = std::clamp((heat - 0.84) / 0.16, 0.0, 1.0) * core * 0.75;
        const auto tint = rooted[index] * style.root_strength;
        for (std::size_t c = 0; c < 3U; ++c) {
          paint[c] = std::min(1.0, paint[c] + (style.core[c] - paint[c]) * std::min(1.0, hot));
          paint[c] = paint[c] + (style.root[c] - paint[c]) * tint;
        }
        over(paint, smoothstep(0.0, edge, heat) * opacity);
      }
      if (sparkle[index] > 0.0) {
        over(spark_color, std::min(1.0, sparkle[index]) * opacity);
      }
      if (alpha <= 0.0) {
        continue;
      }
      for (std::size_t c = 0; c < 3U; ++c) {
        px[c] = to_byte(std::min(1.0, color[c] / alpha));
      }
      px[3] = to_byte(alpha);
    }
  }
  report_kernel_progress(progress, 4, 4, FilterProgressStage::Filtering);
}

}  // namespace patchy
