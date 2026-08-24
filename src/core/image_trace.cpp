#include "core/image_trace.hpp"

#include "core/document.hpp"
#include "core/mask_outline.hpp"
#include "core/palette.hpp"
#include "core/path_fit.hpp"
#include "core/vector_raster.hpp"
#include "core/worker_budget.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <future>
#include <iterator>
#include <limits>
#include <thread>
#include <unordered_map>
#include <utility>

namespace patchy {

namespace {

constexpr std::int32_t kUntraced = -1;
constexpr std::uint8_t kAlphaThreshold = 128;
// A palette entry with every channel at or above this is "white" for Ignore
// White (median cut averages near-white backgrounds to slightly below 255).
constexpr std::uint8_t kWhiteFloor = 240;

[[nodiscard]] std::uint8_t luminance(std::uint8_t r, std::uint8_t g, std::uint8_t b) noexcept {
  return static_cast<std::uint8_t>((r * 299 + g * 587 + b * 114 + 500) / 1000);
}

[[nodiscard]] bool is_white(RgbColor color) noexcept {
  return color.red >= kWhiteFloor && color.green >= kWhiteFloor && color.blue >= kWhiteFloor;
}

struct SourcePixel {
  std::uint8_t r{0};
  std::uint8_t g{0};
  std::uint8_t b{0};
  std::uint8_t a{255};
};

// Reads one pixel of an 8-bit gray/RGB/RGBA buffer as RGBA.
[[nodiscard]] SourcePixel read_pixel(const PixelBuffer& pixels, std::int32_t x, std::int32_t y) noexcept {
  const auto* px = pixels.pixel(x, y);
  const auto channels = pixels.format().channels;
  if (channels == 1) {
    return {px[0], px[0], px[0], 255};
  }
  if (channels == 2) {
    return {px[0], px[0], px[0], px[1]};
  }
  return {px[0], px[1], px[2], channels >= 4 ? px[3] : std::uint8_t{255}};
}

[[nodiscard]] bool format_supported(const PixelBuffer& pixels) noexcept {
  return !pixels.empty() && pixels.format().bit_depth == BitDepth::UInt8 && pixels.format().channels >= 1 &&
         pixels.format().channels <= 4;
}

// 16-bit and float buffers convert to 8-bit before tracing: value/257 with
// rounding for UInt16 (the PSD importer's rule), clamp 0..1 then round for
// Float32 (in-memory floats carry no encoding; linear is the documented
// assumption). Channel meaning follows read_pixel.
[[nodiscard]] PixelBuffer convert_deep_to_rgba8(const PixelBuffer& pixels) {
  PixelBuffer out(pixels.width(), pixels.height(), PixelFormat::rgba8());
  const auto channels = pixels.format().channels;
  const auto depth = pixels.format().bit_depth;
  const auto channel_bytes = bytes_per_channel(depth);
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto* src = pixels.pixel(x, y);
      const auto read = [&](std::size_t channel) -> std::uint8_t {
        const auto* bytes = src + channel * channel_bytes;
        if (depth == BitDepth::UInt16) {
          std::uint16_t value = 0;
          std::memcpy(&value, bytes, sizeof(value));
          return static_cast<std::uint8_t>((static_cast<std::uint32_t>(value) + 128U) / 257U);
        }
        float value = 0.0F;
        std::memcpy(&value, bytes, sizeof(value));
        return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0F, 1.0F) * 255.0F));
      };
      auto* dst = out.pixel(x, y);
      if (channels <= 2) {
        const auto gray = read(0);
        dst[0] = gray;
        dst[1] = gray;
        dst[2] = gray;
        dst[3] = channels == 2 ? read(1) : std::uint8_t{255};
      } else {
        dst[0] = read(0);
        dst[1] = read(1);
        dst[2] = read(2);
        dst[3] = channels >= 4 ? read(3) : std::uint8_t{255};
      }
    }
  }
  return out;
}

struct LabelMap {
  std::int32_t width{0};
  std::int32_t height{0};
  std::vector<std::int32_t> labels;  // kUntraced or palette index
  std::vector<RgbColor> palette;

  [[nodiscard]] std::size_t index(std::int32_t x, std::int32_t y) const noexcept {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
  }
};

// Runs body(begin, end) over chunk ranges of [0, count), in parallel when the
// hardware and the wasm fan-out budget allow it, sequentially otherwise
// (single-threaded wasm: hardware_concurrency() == 1 runs inline, the
// psd_channel_data rule). Deterministic by construction: workers write only
// disjoint, position-indexed slots, so the joined result never depends on
// scheduling. `max_workers` <= 0 means auto; 1 forces the sequential path.
void parallel_chunks(std::size_t count, std::size_t min_per_worker, int max_workers,
                     const std::function<void(std::size_t, std::size_t)>& body) {
  if (count == 0) {
    return;
  }
  int wanted = static_cast<int>(std::min<std::size_t>(
      std::max<std::size_t>(1, count / std::max<std::size_t>(1, min_per_worker)),
      std::clamp<unsigned>(std::thread::hardware_concurrency(), 1U, 8U)));
  if (max_workers > 0) {
    wanted = std::min(wanted, max_workers);
  }
  const int workers = max_blocking_fanout_workers(wanted);
  if (workers < 2) {
    body(0, count);
    return;
  }
  std::vector<std::future<void>> futures;
  futures.reserve(static_cast<std::size_t>(workers));
  const auto chunk = (count + static_cast<std::size_t>(workers) - 1) / static_cast<std::size_t>(workers);
  for (int w = 0; w < workers; ++w) {
    const auto begin = static_cast<std::size_t>(w) * chunk;
    if (begin >= count) {
      break;
    }
    const auto end = std::min(count, begin + chunk);
    futures.push_back(std::async(std::launch::async, [&body, begin, end] { body(begin, end); }));
  }
  for (auto& future : futures) {
    future.wait();
  }
}

// --- pre-quantization smoothing ------------------------------------------------
//
// Smoothing blurs grain and compression noise away before colors are chosen,
// so region boundaries between similar colors settle into clean curves
// instead of following JPEG mosquito noise. Alpha-weighted box blur, run
// twice per axis (a triangle-ish kernel), all integer with rounded divisions
// (the cross-toolchain rule). The alpha channel itself is copied through
// unblurred: which pixels are traced must not change, and weighting by the
// original alpha keeps background color from bleeding across the transparency
// edge.
void box_blur_axis_alpha_weighted(PixelBuffer& buffer, int radius, bool horizontal) {
  const auto width = buffer.width();
  const auto height = buffer.height();
  const auto outer = horizontal ? height : width;
  const auto inner = horizontal ? width : height;
  if (inner <= 1) {
    return;
  }
  std::vector<std::array<std::uint8_t, 3>> line(static_cast<std::size_t>(inner));
  for (std::int32_t o = 0; o < outer; ++o) {
    const auto pixel_at = [&](std::int32_t i) {
      return horizontal ? buffer.pixel(i, o) : buffer.pixel(o, i);
    };
    // Sliding window of alpha-weighted channel sums over [i - radius, i + radius],
    // clamped to the line (the window shrinks at the edges).
    std::int64_t sum_a = 0;
    std::int64_t sum_ra = 0;
    std::int64_t sum_ga = 0;
    std::int64_t sum_ba = 0;
    const auto add = [&](std::int32_t i, std::int64_t sign) {
      const auto* px = pixel_at(i);
      const std::int64_t a = px[3];
      sum_a += sign * a;
      sum_ra += sign * a * px[0];
      sum_ga += sign * a * px[1];
      sum_ba += sign * a * px[2];
    };
    const auto initial = std::min<std::int32_t>(radius, inner - 1);
    for (std::int32_t i = 0; i <= initial; ++i) {
      add(i, 1);
    }
    for (std::int32_t i = 0; i < inner; ++i) {
      const auto* src = pixel_at(i);
      auto& out = line[static_cast<std::size_t>(i)];
      if (sum_a > 0) {
        out = {static_cast<std::uint8_t>((sum_ra + sum_a / 2) / sum_a),
               static_cast<std::uint8_t>((sum_ga + sum_a / 2) / sum_a),
               static_cast<std::uint8_t>((sum_ba + sum_a / 2) / sum_a)};
      } else {
        out = {src[0], src[1], src[2]};  // a fully transparent window keeps its own color
      }
      const auto leaving = i - radius;
      if (leaving >= 0) {
        add(leaving, -1);
      }
      const auto entering = i + radius + 1;
      if (entering < inner) {
        add(entering, 1);
      }
    }
    for (std::int32_t i = 0; i < inner; ++i) {
      auto* px = pixel_at(i);
      const auto& out = line[static_cast<std::size_t>(i)];
      px[0] = out[0];
      px[1] = out[1];
      px[2] = out[2];
    }
  }
}

// Materializes an RGBA8 working copy (read_pixel semantics) and blurs its RGB.
[[nodiscard]] PixelBuffer smooth_pixels_for_trace(const PixelBuffer& pixels, int radius,
                                                  const std::function<bool()>& is_cancelled) {
  PixelBuffer smoothed(pixels.width(), pixels.height(), PixelFormat::rgba8());
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto px = read_pixel(pixels, x, y);
      auto* dst = smoothed.pixel(x, y);
      dst[0] = px.r;
      dst[1] = px.g;
      dst[2] = px.b;
      dst[3] = px.a;
    }
  }
  for (int pass = 0; pass < 2; ++pass) {
    if (is_cancelled()) {
      return smoothed;
    }
    box_blur_axis_alpha_weighted(smoothed, radius, true);
    if (is_cancelled()) {
      return smoothed;
    }
    box_blur_axis_alpha_weighted(smoothed, radius, false);
  }
  return smoothed;
}

std::vector<PaletteColorCount> histogram_counts(const std::array<std::uint64_t, 256>& histogram) {
  std::vector<PaletteColorCount> counts;
  for (int value = 0; value < 256; ++value) {
    const auto population = histogram[static_cast<std::size_t>(value)];
    if (population != 0) {
      const auto gray = static_cast<std::uint8_t>(value);
      counts.push_back({RgbColor{gray, gray, gray}, population});
    }
  }
  return counts;
}

// Exact optimal scalar quantization of a 256-bin histogram into `target`
// levels (dynamic programming over cluster boundaries, the Bruce 1965 /
// Lloyd-Max lineage): minimizes the population-weighted squared error, which
// median cut only approximates. All integer with fixed tie-breaks (the
// smallest split index wins), so results are toolchain-deterministic.
std::vector<std::uint8_t> optimal_gray_levels(const std::array<std::uint64_t, 256>& histogram, int target) {
  std::vector<int> values;
  values.reserve(256);
  for (int value = 0; value < 256; ++value) {
    if (histogram[static_cast<std::size_t>(value)] != 0) {
      values.push_back(value);
    }
  }
  std::vector<std::uint8_t> levels;
  if (values.empty() || target <= 0) {
    return levels;
  }
  const auto m = values.size();
  if (m <= static_cast<std::size_t>(target)) {
    for (const auto value : values) {
      levels.push_back(static_cast<std::uint8_t>(value));
    }
    return levels;
  }
  // Scale populations down until Sum(v * count)^2 fits in 64 bits; halving
  // keeps every present value at count >= 1 so no level disappears.
  std::vector<std::uint64_t> counts(m);
  std::uint64_t total = 0;
  for (std::size_t i = 0; i < m; ++i) {
    counts[i] = histogram[static_cast<std::size_t>(values[i])];
    total += counts[i];
  }
  while (total > (std::uint64_t{1} << 23U)) {
    total = 0;
    for (auto& count : counts) {
      count = std::max<std::uint64_t>(1, count >> 1U);
      total += count;
    }
  }
  // Prefix sums over the present values; cost(i..j) is the exact SSE of one
  // cluster, with a single fixed rounding in the mean term.
  std::vector<std::uint64_t> prefix_w(m + 1, 0);
  std::vector<std::uint64_t> prefix_s1(m + 1, 0);
  std::vector<std::uint64_t> prefix_s2(m + 1, 0);
  for (std::size_t i = 0; i < m; ++i) {
    const auto v = static_cast<std::uint64_t>(values[i]);
    prefix_w[i + 1] = prefix_w[i] + counts[i];
    prefix_s1[i + 1] = prefix_s1[i] + v * counts[i];
    prefix_s2[i + 1] = prefix_s2[i] + v * v * counts[i];
  }
  const auto cost = [&](std::size_t first, std::size_t last) -> std::uint64_t {  // inclusive range
    const auto w = prefix_w[last + 1] - prefix_w[first];
    const auto s1 = prefix_s1[last + 1] - prefix_s1[first];
    const auto s2 = prefix_s2[last + 1] - prefix_s2[first];
    return s2 - (s1 * s1 + w / 2) / w;
  };
  const auto k_max = static_cast<std::size_t>(target);
  constexpr std::uint64_t kUnset = std::numeric_limits<std::uint64_t>::max();
  std::vector<std::uint64_t> previous(m, 0);
  std::vector<std::uint64_t> current(m, 0);
  // choice[k][j]: first value index of the last cluster in the best k-way
  // split of values[0..j].
  std::vector<std::vector<std::uint16_t>> choice(k_max, std::vector<std::uint16_t>(m, 0));
  for (std::size_t j = 0; j < m; ++j) {
    previous[j] = cost(0, j);
  }
  for (std::size_t k = 1; k < k_max; ++k) {
    for (std::size_t j = 0; j < m; ++j) {
      if (j < k) {
        current[j] = 0;  // never read: j+1 values always fit k+1 clusters
        choice[k][j] = static_cast<std::uint16_t>(j);
        continue;
      }
      std::uint64_t best = kUnset;
      std::size_t best_split = k;
      for (std::size_t split = k; split <= j; ++split) {
        const auto candidate = previous[split - 1] + cost(split, j);
        if (candidate < best) {
          best = candidate;
          best_split = split;
        }
      }
      current[j] = best;
      choice[k][j] = static_cast<std::uint16_t>(best_split);
    }
    std::swap(previous, current);
  }
  // Backtrack the cluster boundaries and emit each cluster's rounded mean.
  std::vector<std::pair<std::size_t, std::size_t>> clusters;
  std::size_t end = m - 1;
  for (std::size_t k = k_max; k-- > 1;) {
    const auto first = static_cast<std::size_t>(choice[k][end]);
    clusters.emplace_back(first, end);
    end = first - 1;
  }
  clusters.emplace_back(0, end);
  std::reverse(clusters.begin(), clusters.end());
  for (const auto& [first, last] : clusters) {
    const auto w = prefix_w[last + 1] - prefix_w[first];
    const auto s1 = prefix_s1[last + 1] - prefix_s1[first];
    const auto level = static_cast<std::uint8_t>((s1 + w / 2) / w);
    if (levels.empty() || levels.back() != level) {
      levels.push_back(level);
    }
  }
  return levels;
}

// --- quantization -----------------------------------------------------------

// Exact nearest palette index per unique color under weighted_color_distance;
// the lowest palette index wins ties (strict <). Parallel over chunks of the
// color-key-sorted unique colors into disjoint pre-sized slots, so the result
// is deterministic regardless of scheduling.
std::vector<std::int32_t> assign_colors_exact(const std::vector<PaletteColorCount>& counts,
                                              const std::vector<RgbColor>& palette,
                                              const std::function<bool()>& is_cancelled, int max_workers) {
  std::vector<std::int32_t> nearest(counts.size(), 0);
  parallel_chunks(counts.size(), 4096, max_workers, [&](std::size_t begin, std::size_t end) {
    for (std::size_t i = begin; i < end; ++i) {
      if ((i - begin) % 4096 == 0 && is_cancelled()) {
        return;
      }
      std::uint32_t best_distance = std::numeric_limits<std::uint32_t>::max();
      std::int32_t best = 0;
      for (std::size_t c = 0; c < palette.size(); ++c) {
        const auto distance = weighted_color_distance(counts[i].color, palette[c]);
        if (distance < best_distance) {
          best_distance = distance;
          best = static_cast<std::int32_t>(c);
        }
      }
      nearest[i] = best;
    }
  });
  return nearest;
}

LabelMap build_label_map(const PixelBuffer& pixels, const ImageTraceOptions& options,
                         const std::function<bool()>& is_cancelled, int max_workers) {
  LabelMap map;
  map.width = pixels.width();
  map.height = pixels.height();
  const auto count = static_cast<std::size_t>(map.width) * static_cast<std::size_t>(map.height);
  map.labels.assign(count, kUntraced);

  const int color_count =
      std::clamp(options.colors, ImageTraceOptions::kMinColors, ImageTraceOptions::kMaxColors);
  switch (options.mode) {
    case ImageTraceOptions::Mode::BlackAndWhite: {
      map.palette = {RgbColor{0, 0, 0}, RgbColor{255, 255, 255}};
      const int threshold = std::clamp(options.threshold, 1, 255);
      for (std::int32_t y = 0; y < map.height; ++y) {
        for (std::int32_t x = 0; x < map.width; ++x) {
          const auto px = read_pixel(pixels, x, y);
          if (px.a < kAlphaThreshold) {
            continue;
          }
          map.labels[map.index(x, y)] = luminance(px.r, px.g, px.b) < threshold ? 0 : 1;
        }
      }
      break;
    }
    case ImageTraceOptions::Mode::Grayscale: {
      std::array<std::uint64_t, 256> histogram{};
      std::vector<std::uint8_t> grays(count, 0);
      for (std::int32_t y = 0; y < map.height; ++y) {
        for (std::int32_t x = 0; x < map.width; ++x) {
          const auto px = read_pixel(pixels, x, y);
          if (px.a < kAlphaThreshold) {
            continue;
          }
          const auto gray = luminance(px.r, px.g, px.b);
          grays[map.index(x, y)] = gray;
          map.labels[map.index(x, y)] = 0;  // marks "visible"; resolved below
          ++histogram[gray];
        }
      }
      // Exact optimal 1D quantization (strictly better than median cut for
      // grayscale: the histogram is small enough to solve outright).
      map.palette.clear();
      for (const auto level : optimal_gray_levels(histogram, color_count)) {
        map.palette.push_back(RgbColor{level, level, level});
      }
      if (map.palette.empty()) {
        break;
      }
      // Nearest gray per value; the lowest index wins ties.
      std::array<std::int32_t, 256> nearest{};
      for (int value = 0; value < 256; ++value) {
        std::int32_t best = 0;
        int best_distance = std::numeric_limits<int>::max();
        for (std::size_t i = 0; i < map.palette.size(); ++i) {
          const int distance = std::abs(value - static_cast<int>(map.palette[i].red));
          if (distance < best_distance) {
            best_distance = distance;
            best = static_cast<std::int32_t>(i);
          }
        }
        nearest[static_cast<std::size_t>(value)] = best;
      }
      for (std::size_t i = 0; i < count; ++i) {
        if (map.labels[i] != kUntraced) {
          map.labels[i] = nearest[grays[i]];
        }
      }
      break;
    }
    case ImageTraceOptions::Mode::Color: {
      // collect_color_counts counts alpha >= threshold when the buffer has
      // alpha; gray buffers are expanded through read_pixel instead.
      std::vector<PaletteColorCount> counts;
      if (pixels.format().channels >= 3) {
        counts = collect_color_counts(pixels, kAlphaThreshold);
      } else {
        std::array<std::uint64_t, 256> histogram{};
        for (std::int32_t y = 0; y < map.height; ++y) {
          for (std::int32_t x = 0; x < map.width; ++x) {
            const auto px = read_pixel(pixels, x, y);
            if (px.a >= kAlphaThreshold) {
              ++histogram[px.r];
            }
          }
        }
        counts = histogram_counts(histogram);
      }
      // Median cut seeds the palette; fixed-iteration integer Lloyd refinement
      // in the weighted color space reallocates entries to where the image's
      // tones actually live (the difference between banded and smooth photo
      // traces). When every unique color already fits the palette, median cut
      // returns them exactly and refinement is skipped: its 6-bit cell fold
      // could otherwise nudge exact colors that share a cell. Assignment is
      // then EXACT per unique color: the 5-5-5 lookup table's +-4 per-channel
      // bucket error misplaces whole color slabs once palette entries sit
      // closer together than the buckets.
      map.palette = median_cut_palette(counts, static_cast<std::size_t>(color_count));
      if (counts.size() > static_cast<std::size_t>(color_count)) {
        map.palette = refine_palette_weighted(counts, std::move(map.palette), 8, is_cancelled);
      }
      if (map.palette.empty() || is_cancelled()) {
        break;
      }
      const auto nearest = assign_colors_exact(counts, map.palette, is_cancelled, max_workers);
      if (is_cancelled()) {
        break;
      }
      std::unordered_map<std::uint32_t, std::int32_t> index_by_key;
      index_by_key.reserve(counts.size());
      for (std::size_t i = 0; i < counts.size(); ++i) {
        index_by_key.emplace(palette_color_key(counts[i].color), nearest[i]);
      }
      for (std::int32_t y = 0; y < map.height; ++y) {
        for (std::int32_t x = 0; x < map.width; ++x) {
          const auto px = read_pixel(pixels, x, y);
          if (px.a < kAlphaThreshold) {
            continue;
          }
          const auto found = index_by_key.find(palette_color_key(RgbColor{px.r, px.g, px.b}));
          if (found != index_by_key.end()) {
            map.labels[map.index(x, y)] = found->second;
          }
        }
      }
      break;
    }
  }

  if (options.ignore_white) {
    std::vector<bool> white(map.palette.size(), false);
    bool any = false;
    for (std::size_t i = 0; i < map.palette.size(); ++i) {
      white[i] = is_white(map.palette[i]);
      any = any || white[i];
    }
    if (any) {
      for (auto& label : map.labels) {
        if (label != kUntraced && white[static_cast<std::size_t>(label)]) {
          label = kUntraced;
        }
      }
    }
  }
  return map;
}

// One 3x3 label majority pass (runs with smoothing, after quantization and
// Ignore White, before speckle removal): de-rags the ragged interfaces that
// survive the blur where two similar palette colors trade single pixels along
// a boundary. Double-buffered so the result is order-independent; untraced
// pixels neither vote nor change (coverage never bleeds across transparency).
// Ties keep the center's current label when it is among the best (prevents
// checkerboard churn), otherwise the lowest palette color key wins. Plain
// 9-sample counting, deliberately no sliding or merged histograms.
void smooth_label_boundaries(LabelMap& map, const std::function<bool()>& is_cancelled) {
  const auto width = map.width;
  const auto height = map.height;
  if (width <= 2 || height <= 2) {
    return;
  }
  std::vector<std::int32_t> smoothed(map.labels.size(), kUntraced);
  for (std::int32_t y = 0; y < height; ++y) {
    if (y % 64 == 0 && is_cancelled()) {
      return;
    }
    for (std::int32_t x = 0; x < width; ++x) {
      const auto center = map.labels[map.index(x, y)];
      if (center == kUntraced) {
        continue;
      }
      std::array<std::int32_t, 9> labels{};
      std::array<int, 9> votes{};
      int distinct = 0;
      for (std::int32_t ny = std::max(0, y - 1); ny <= std::min(height - 1, y + 1); ++ny) {
        for (std::int32_t nx = std::max(0, x - 1); nx <= std::min(width - 1, x + 1); ++nx) {
          const auto label = map.labels[map.index(nx, ny)];
          if (label == kUntraced) {
            continue;
          }
          int slot = 0;
          while (slot < distinct && labels[static_cast<std::size_t>(slot)] != label) {
            ++slot;
          }
          if (slot == distinct) {
            labels[static_cast<std::size_t>(distinct)] = label;
            votes[static_cast<std::size_t>(distinct)] = 0;
            ++distinct;
          }
          ++votes[static_cast<std::size_t>(slot)];
        }
      }
      int best_votes = 0;
      std::int32_t best = center;
      bool center_is_best = false;
      for (int slot = 0; slot < distinct; ++slot) {
        const auto label = labels[static_cast<std::size_t>(slot)];
        const auto count = votes[static_cast<std::size_t>(slot)];
        if (count > best_votes) {
          best_votes = count;
          best = label;
          center_is_best = label == center;
        } else if (count == best_votes) {
          if (label == center) {
            center_is_best = true;
          } else if (!center_is_best &&
                     palette_color_key(map.palette[static_cast<std::size_t>(label)]) <
                         palette_color_key(map.palette[static_cast<std::size_t>(best)])) {
            best = label;
          }
        }
      }
      smoothed[map.index(x, y)] = center_is_best ? center : best;
    }
  }
  map.labels = std::move(smoothed);
}

// --- connected components -----------------------------------------------------

struct Component {
  std::int32_t label{kUntraced};
  std::int64_t area{0};
};

struct ComponentMap {
  std::vector<std::int32_t> ids;  // per pixel, -1 for untraced
  std::vector<Component> components;
};

// 4-connected components of equal label, numbered in scan order of their
// first pixel.
ComponentMap label_components(const LabelMap& map) {
  ComponentMap result;
  const auto width = map.width;
  const auto height = map.height;
  result.ids.assign(map.labels.size(), -1);
  std::vector<std::size_t> stack;
  for (std::size_t seed = 0; seed < map.labels.size(); ++seed) {
    if (map.labels[seed] == kUntraced || result.ids[seed] != -1) {
      continue;
    }
    const auto id = static_cast<std::int32_t>(result.components.size());
    const auto label = map.labels[seed];
    Component component;
    component.label = label;
    result.ids[seed] = id;
    stack.clear();
    stack.push_back(seed);
    while (!stack.empty()) {
      const auto index = stack.back();
      stack.pop_back();
      ++component.area;
      const auto x = static_cast<std::int32_t>(index % static_cast<std::size_t>(width));
      const auto y = static_cast<std::int32_t>(index / static_cast<std::size_t>(width));
      const auto visit = [&](std::int32_t nx, std::int32_t ny) {
        if (nx < 0 || ny < 0 || nx >= width || ny >= height) {
          return;
        }
        const auto neighbor = map.index(nx, ny);
        if (result.ids[neighbor] == -1 && map.labels[neighbor] == label) {
          result.ids[neighbor] = id;
          stack.push_back(neighbor);
        }
      };
      visit(x + 1, y);
      visit(x - 1, y);
      visit(x, y + 1);
      visit(x, y - 1);
    }
    result.components.push_back(component);
  }
  return result;
}

// Merges every component smaller than `minimum_area` into a neighbor.
// Mostly-transparent surroundings win first (untraced takes components whose
// border is at least half untraced, so dust inside transparency disappears);
// otherwise the component merges into the labeled neighbor with the CLOSEST
// palette color among those sharing a substantial border (at least a quarter
// of the longest labeled border, which prunes 1 px touches). Ties fall to the
// longer border, then the lower label index. Color-aware merging dissolves
// photo speckles into the region they visually belong to instead of whichever
// neighbor happens to wrap them furthest. Repeats until stable or the pass
// cap.
void remove_speckles(LabelMap& map, std::int64_t minimum_area, const std::function<bool()>& is_cancelled) {
  if (minimum_area <= 1) {
    return;
  }
  constexpr int kMaxPasses = 8;
  const auto width = map.width;
  const auto height = map.height;
  for (int pass = 0; pass < kMaxPasses; ++pass) {
    if (is_cancelled()) {
      return;
    }
    auto components = label_components(map);
    std::vector<std::int32_t> small_index(components.components.size(), -1);
    std::int32_t small_count = 0;
    for (std::size_t i = 0; i < components.components.size(); ++i) {
      if (components.components[i].area < minimum_area) {
        small_index[i] = small_count++;
      }
    }
    if (small_count == 0) {
      return;
    }
    // Border length per (small component, neighbor label), neighbors kept in
    // first-seen order so ties resolve deterministically.
    struct Neighbor {
      std::int32_t label{kUntraced};
      std::int64_t border{0};
    };
    std::vector<std::vector<Neighbor>> neighbors(static_cast<std::size_t>(small_count));
    const auto note = [&](std::int32_t small, std::int32_t neighbor_label) {
      auto& list = neighbors[static_cast<std::size_t>(small)];
      for (auto& entry : list) {
        if (entry.label == neighbor_label) {
          ++entry.border;
          return;
        }
      }
      list.push_back({neighbor_label, 1});
    };
    for (std::int32_t y = 0; y < height; ++y) {
      for (std::int32_t x = 0; x < width; ++x) {
        const auto index = map.index(x, y);
        const auto id = components.ids[index];
        if (id < 0 || small_index[static_cast<std::size_t>(id)] < 0) {
          continue;
        }
        const auto small = small_index[static_cast<std::size_t>(id)];
        const auto consider = [&](std::int32_t nx, std::int32_t ny) {
          if (nx < 0 || ny < 0 || nx >= width || ny >= height) {
            return;
          }
          const auto neighbor = map.index(nx, ny);
          if (components.ids[neighbor] != id) {
            note(small, map.labels[neighbor]);
          }
        };
        consider(x - 1, y);
        consider(x + 1, y);
        consider(x, y - 1);
        consider(x, y + 1);
      }
    }
    std::vector<std::int32_t> replacement(static_cast<std::size_t>(small_count), kUntraced);
    bool changed = false;
    for (std::size_t i = 0; i < components.components.size(); ++i) {
      const auto small = small_index[i];
      if (small < 0) {
        continue;
      }
      const auto& list = neighbors[static_cast<std::size_t>(small)];
      if (list.empty()) {  // the whole image is one small region
        replacement[static_cast<std::size_t>(small)] = components.components[i].label;
        continue;
      }
      std::int64_t total_border = 0;
      std::int64_t untraced_border = 0;
      std::int64_t max_labeled_border = 0;
      for (const auto& entry : list) {
        total_border += entry.border;
        if (entry.label == kUntraced) {
          untraced_border += entry.border;
        } else {
          max_labeled_border = std::max(max_labeled_border, entry.border);
        }
      }
      std::int32_t chosen = kUntraced;
      if (untraced_border * 2 < total_border && max_labeled_border > 0) {
        const auto own_color = map.palette[static_cast<std::size_t>(components.components[i].label)];
        const Neighbor* best = nullptr;
        std::uint32_t best_distance = 0;
        for (const auto& entry : list) {
          if (entry.label == kUntraced || entry.border * 4 < max_labeled_border) {
            continue;
          }
          const auto distance =
              weighted_color_distance(own_color, map.palette[static_cast<std::size_t>(entry.label)]);
          if (best == nullptr || distance < best_distance ||
              (distance == best_distance &&
               (entry.border > best->border ||
                (entry.border == best->border && entry.label < best->label)))) {
            best = &entry;
            best_distance = distance;
          }
        }
        chosen = best->label;
      }
      replacement[static_cast<std::size_t>(small)] = chosen;
      changed = changed || chosen != components.components[i].label;
    }
    if (!changed) {
      return;
    }
    for (std::size_t index = 0; index < map.labels.size(); ++index) {
      const auto id = components.ids[index];
      if (id < 0) {
        continue;
      }
      const auto small = small_index[static_cast<std::size_t>(id)];
      if (small >= 0) {
        map.labels[index] = replacement[static_cast<std::size_t>(small)];
      }
    }
  }
}

// --- contours -----------------------------------------------------------------

struct TracedLoop {
  std::vector<FitPoint> points;  // image coordinates
  Rect bounds{};
  std::int32_t component{-1};  // owning component; -1 once dropped
  bool hole{false};
  // Hole loops: the first pixel inside the hole (image coordinates).
  std::int32_t seed_x{0};
  std::int32_t seed_y{0};
};

// Traces every component of `label` (all at once, within the label's bounds)
// and attributes each loop to its component through the canonical start
// vertex: an outer loop starts at the top-left corner of its topmost-leftmost
// pixel; a hole loop starts at the top-left corner of its topmost-leftmost
// INSIDE pixel, whose upper neighbor belongs to the owner.
struct LabelBounds {
  std::int32_t min_x{0};
  std::int32_t min_y{0};
  std::int32_t max_x{-1};
  std::int32_t max_y{-1};
};

std::vector<LabelBounds> label_bounds(const LabelMap& map) {
  std::vector<LabelBounds> bounds(map.palette.size());
  for (auto& entry : bounds) {
    entry.min_x = map.width;
    entry.min_y = map.height;
  }
  for (std::int32_t y = 0; y < map.height; ++y) {
    for (std::int32_t x = 0; x < map.width; ++x) {
      const auto label = map.labels[map.index(x, y)];
      if (label == kUntraced) {
        continue;
      }
      auto& entry = bounds[static_cast<std::size_t>(label)];
      entry.min_x = std::min(entry.min_x, x);
      entry.min_y = std::min(entry.min_y, y);
      entry.max_x = std::max(entry.max_x, x);
      entry.max_y = std::max(entry.max_y, y);
    }
  }
  return bounds;
}

std::vector<TracedLoop> trace_label(const LabelMap& map, const ComponentMap& components, std::int32_t label,
                                    const LabelBounds& bounds) {
  std::vector<TracedLoop> loops;
  if (bounds.max_x < 0) {
    return loops;
  }
  const auto min_x = bounds.min_x;
  const auto min_y = bounds.min_y;
  const auto local_width = bounds.max_x - min_x + 1;
  const auto local_height = bounds.max_y - min_y + 1;
  const auto stride = static_cast<std::size_t>(local_width) + 2;
  std::vector<std::uint8_t> mask(stride * (static_cast<std::size_t>(local_height) + 2), std::uint8_t{0});
  for (std::int32_t y = 0; y < local_height; ++y) {
    auto* row = mask.data() + static_cast<std::size_t>(y + 1) * stride + 1;
    for (std::int32_t x = 0; x < local_width; ++x) {
      row[x] = map.labels[map.index(x + min_x, y + min_y)] == label ? 1U : 0U;
    }
  }
  for (auto& traced : trace_mask_outlines(mask.data(), local_width, local_height, stride)) {
    TracedLoop loop;
    loop.points = std::move(traced.points);
    for (auto& point : loop.points) {
      point.x += min_x;
      point.y += min_y;
    }
    loop.bounds =
        Rect{traced.bounds.x + min_x, traced.bounds.y + min_y, traced.bounds.width, traced.bounds.height};
    loop.hole = loop_signed_area(loop.points) < 0.0;
    const auto start_x = static_cast<std::int32_t>(loop.points.front().x);
    const auto start_y = static_cast<std::int32_t>(loop.points.front().y);
    if (loop.hole) {
      loop.seed_x = start_x;
      loop.seed_y = start_y;
      loop.component = components.ids[map.index(start_x, start_y - 1)];
    } else {
      loop.component = components.ids[map.index(start_x, start_y)];
    }
    loops.push_back(std::move(loop));
  }
  return loops;
}

// Overlapping mode: which component encloses which. For every hole, an
// 8-connected flood over the non-owner pixels it encloses finds the
// components inside; each component's parent is the owner of the SMALLEST
// hole that contains it. Holes enclosing no component keep their hole status
// (a ring around transparency stays a ring).
struct Nesting {
  std::vector<std::int32_t> parent;          // per component, -1 for roots
  std::vector<std::int64_t> enclosing_area;  // per component, pixel area of its parent hole
  std::vector<int> depth;                    // per component
};

Nesting compute_nesting(const LabelMap& map, const ComponentMap& components, std::vector<TracedLoop>& loops) {
  Nesting nesting;
  const auto component_count = components.components.size();
  nesting.parent.assign(component_count, -1);
  nesting.enclosing_area.assign(component_count, std::numeric_limits<std::int64_t>::max());
  nesting.depth.assign(component_count, 0);
  const auto width = map.width;
  const auto height = map.height;
  std::vector<std::int32_t> visit_stamp(map.labels.size(), 0);
  std::vector<std::int32_t> seen_stamp(component_count, 0);
  std::vector<std::size_t> stack;
  std::vector<std::int32_t> found;
  std::int32_t stamp = 0;
  for (auto& loop : loops) {
    if (!loop.hole) {
      continue;
    }
    ++stamp;
    found.clear();
    const auto owner = loop.component;
    const auto left = loop.bounds.x;
    const auto top = loop.bounds.y;
    const auto right = loop.bounds.x + loop.bounds.width;    // exclusive
    const auto bottom = loop.bounds.y + loop.bounds.height;  // exclusive
    std::int64_t area = 0;
    const auto seed = map.index(loop.seed_x, loop.seed_y);
    visit_stamp[seed] = stamp;
    stack.clear();
    stack.push_back(seed);
    while (!stack.empty()) {
      const auto index = stack.back();
      stack.pop_back();
      ++area;
      const auto id = components.ids[index];
      if (id >= 0 && seen_stamp[static_cast<std::size_t>(id)] != stamp) {
        seen_stamp[static_cast<std::size_t>(id)] = stamp;
        found.push_back(id);
      }
      const auto x = static_cast<std::int32_t>(index % static_cast<std::size_t>(width));
      const auto y = static_cast<std::int32_t>(index / static_cast<std::size_t>(width));
      for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
          if (dx == 0 && dy == 0) {
            continue;
          }
          const auto nx = x + dx;
          const auto ny = y + dy;
          if (nx < left || ny < top || nx >= right || ny >= bottom || nx < 0 || ny < 0 || nx >= width ||
              ny >= height) {
            continue;
          }
          const auto neighbor = map.index(nx, ny);
          if (visit_stamp[neighbor] == stamp || components.ids[neighbor] == owner) {
            continue;
          }
          visit_stamp[neighbor] = stamp;
          stack.push_back(neighbor);
        }
      }
    }
    for (const auto id : found) {
      auto& best = nesting.enclosing_area[static_cast<std::size_t>(id)];
      if (area < best) {
        best = area;
        nesting.parent[static_cast<std::size_t>(id)] = owner;
      }
    }
    if (!found.empty()) {
      loop.component = -1;  // painted over; the children stack on top
    }
  }
  // A parent's own enclosing hole strictly contains the hole its child sits
  // in, so descending enclosing area resolves every parent before its
  // children.
  std::vector<std::int32_t> order(component_count);
  for (std::size_t i = 0; i < component_count; ++i) {
    order[i] = static_cast<std::int32_t>(i);
  }
  std::stable_sort(order.begin(), order.end(), [&](std::int32_t a, std::int32_t b) {
    return nesting.enclosing_area[static_cast<std::size_t>(a)] >
           nesting.enclosing_area[static_cast<std::size_t>(b)];
  });
  for (const auto id : order) {
    const auto parent = nesting.parent[static_cast<std::size_t>(id)];
    nesting.depth[static_cast<std::size_t>(id)] =
        parent < 0 ? 0 : nesting.depth[static_cast<std::size_t>(parent)] + 1;
  }
  return nesting;
}

// Settles a pixel-corner staircase before fitting. A traced contour walks
// pixel edges, so a diagonal or a curve is a run of short risers and treads
// whose corner vertices sit up to half a step off the edge they depict;
// Douglas-Peucker then picks those corners as chord endpoints and sees double
// the deviation, which keeps stair steps as anchors at tolerances near the
// step size. Every vertex between two stair edges moves to the average of
// their midpoints (the line a smooth edge would follow, exactly so for
// uniform steps); a vertex with one stair edge moves to that edge's midpoint.
// A stair edge is an edge no longer than `max_edge` whose two ends turn in
// opposite directions (a step). Vertices between longer edges (real corners)
// stay exactly in place, and a short edge with the same turn at both ends is
// the cap of a thin feature (a 1 px line, a single pixel) and is left alone
// so the feature survives.
std::vector<FitPoint> settle_staircase(const std::vector<FitPoint>& points, double max_edge) {
  const auto count = points.size();
  if (count < 4) {
    return points;
  }
  const auto at = [&](std::size_t i) -> const FitPoint& { return points[i % count]; };
  // Turn sign at each vertex: cross(incoming, outgoing).
  std::vector<double> turn(count, 0.0);
  for (std::size_t i = 0; i < count; ++i) {
    const auto& previous = at(i + count - 1);
    const auto& current = at(i);
    const auto& next = at(i + 1);
    turn[i] = (current.x - previous.x) * (next.y - current.y) - (current.y - previous.y) * (next.x - current.x);
  }
  // stair[i] marks the edge from vertex i to vertex i + 1.
  std::vector<bool> stair(count, false);
  for (std::size_t i = 0; i < count; ++i) {
    const auto& a = at(i);
    const auto& b = at(i + 1);
    const auto dx = b.x - a.x;
    const auto dy = b.y - a.y;
    const auto edge = std::sqrt(dx * dx + dy * dy);
    stair[i] = edge > 1e-9 && edge <= max_edge && turn[i] * turn[(i + 1) % count] < 0.0;
  }
  std::vector<FitPoint> settled;
  settled.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    const auto& current = at(i);
    double move_x = 0.0;
    double move_y = 0.0;
    int stairs = 0;
    const auto lean_toward = [&](const FitPoint& neighbor) {
      move_x += (neighbor.x - current.x) * 0.5;
      move_y += (neighbor.y - current.y) * 0.5;
      ++stairs;
    };
    if (stair[(i + count - 1) % count]) {
      lean_toward(at(i + count - 1));
    }
    if (stair[i]) {
      lean_toward(at(i + 1));
    }
    FitPoint moved = current;
    if (stairs > 0) {
      moved.x += move_x / stairs;
      moved.y += move_y / stairs;
    }
    if (!settled.empty() && settled.back() == moved) {
      continue;  // two stair vertices that met at one edge midpoint
    }
    settled.push_back(moved);
  }
  if (settled.size() > 1 && settled.front() == settled.back()) {
    settled.pop_back();
  }
  return settled;
}

[[nodiscard]] bool subpath_is_visible(const PathSubpath& subpath) noexcept {
  if (subpath.anchors.size() >= 3) {
    return true;
  }
  if (subpath.anchors.size() < 2) {
    return false;
  }
  for (const auto& anchor : subpath.anchors) {
    if (anchor.in_x != anchor.anchor_x || anchor.in_y != anchor.anchor_y || anchor.out_x != anchor.anchor_x ||
        anchor.out_y != anchor.anchor_y) {
      return true;
    }
  }
  return false;
}

}  // namespace

double image_trace_fit_tolerance(int paths) noexcept {
  const double t = std::clamp(paths, 0, 100) / 100.0;
  return 4.0 * std::pow(0.0625, t);
}

double image_trace_corner_angle(int corners) noexcept {
  return 120.0 - 0.9 * std::clamp(corners, 0, 100);
}

ImageTraceResult trace_image(const PixelBuffer& pixels, const ImageTraceOptions& options,
                             const std::function<bool()>& cancelled, int max_workers) {
  ImageTraceResult result;
  if (!pixels.empty() && pixels.format().bit_depth != BitDepth::UInt8) {
    if (pixels.format().channels < 1 || pixels.format().channels > 4) {
      return result;
    }
    return trace_image(convert_deep_to_rgba8(pixels), options, cancelled, max_workers);
  }
  if (!format_supported(pixels)) {
    return result;
  }
  const auto is_cancelled = [&cancelled]() { return cancelled && cancelled(); };

  // Smoothing 0 takes the untouched pre-existing flow: no working copy, no
  // new code path, byte-for-byte the same result.
  const int smoothing = std::clamp(options.smoothing, 0, ImageTraceOptions::kMaxSmoothing);
  const PixelBuffer* source = &pixels;
  PixelBuffer smoothed;
  if (smoothing > 0) {
    smoothed = smooth_pixels_for_trace(pixels, smoothing, is_cancelled);
    if (is_cancelled()) {
      return result;
    }
    source = &smoothed;
  }

  auto map = build_label_map(*source, options, is_cancelled, max_workers);
  result.palette_size = map.palette.size();
  if (map.palette.empty() || is_cancelled()) {
    return result;
  }
  if (smoothing > 0) {
    smooth_label_boundaries(map, is_cancelled);
    if (is_cancelled()) {
      return result;
    }
  }
  remove_speckles(map, std::clamp(options.noise, 1, 100), is_cancelled);
  if (is_cancelled()) {
    return result;
  }
  const auto components = label_components(map);
  if (components.components.empty()) {
    return result;
  }

  const bool overlapping = options.method == ImageTraceOptions::Method::Overlapping;
  const auto bounds = label_bounds(map);
  // Per-label contour extraction is independent per label; parallel workers
  // fill disjoint slots that concatenate in ascending label order, so the
  // loop order is identical to the sequential walk.
  std::vector<std::vector<TracedLoop>> per_label(map.palette.size());
  parallel_chunks(map.palette.size(), 1, max_workers, [&](std::size_t begin, std::size_t end) {
    for (std::size_t label = begin; label < end; ++label) {
      if (is_cancelled()) {
        return;
      }
      per_label[label] =
          trace_label(map, components, static_cast<std::int32_t>(label), bounds[label]);
    }
  });
  if (is_cancelled()) {
    return {};
  }
  std::vector<TracedLoop> loops;
  for (auto& traced : per_label) {
    loops.insert(loops.end(), std::make_move_iterator(traced.begin()), std::make_move_iterator(traced.end()));
  }
  per_label.clear();
  Nesting nesting;
  if (overlapping) {
    nesting = compute_nesting(map, components, loops);
  } else {
    nesting.depth.assign(components.components.size(), 0);
  }
  if (is_cancelled()) {
    return {};
  }

  PathFitOptions fit;
  fit.tolerance = image_trace_fit_tolerance(options.paths);
  fit.corner_angle_degrees = image_trace_corner_angle(options.corners);
  fit.smooth_corner_tangents = true;
  fit.snap_curves_to_lines = options.snap_curves_to_lines;
  // Near-miss runs (circles) converge under a few more reparametrization
  // iterations instead of splitting; the default 4 stays with Make Work Path.
  fit.refine_iterations = 8;
  // Stair steps up to three tolerances long settle (1 px risers always do),
  // so a tight Paths setting stays pixel-faithful while the default smooths.
  const double stair_edge = std::clamp(3.0 * fit.tolerance, 1.5, 6.0);

  // Everything to fit, in deterministic trace order. `area_component` is the
  // component whose area the job's layer counts on first touch (-1 when the
  // layer's area is pre-summed).
  struct FitJob {
    std::vector<FitPoint> points;
    bool hole{false};
    std::size_t layer_slot{0};
    std::int32_t area_component{-1};
  };
  std::vector<FitJob> jobs;
  std::vector<bool> area_counted(components.components.size(), false);

  if (!overlapping) {
    // Abutting: one layer per label in first-touch order (all depth 0).
    std::unordered_map<std::int32_t, std::size_t> layer_by_label;
    for (auto& loop : loops) {
      if (loop.component < 0) {
        continue;
      }
      const auto& component = components.components[static_cast<std::size_t>(loop.component)];
      auto found = layer_by_label.find(component.label);
      if (found == layer_by_label.end()) {
        ImageTraceLayer layer;
        layer.color = map.palette[static_cast<std::size_t>(component.label)];
        layer.depth = 0;
        result.layers.push_back(std::move(layer));
        found = layer_by_label.emplace(component.label, result.layers.size() - 1).first;
      }
      FitJob job;
      job.points = std::move(loop.points);
      job.hole = loop.hole;
      job.layer_slot = found->second;
      job.area_component = loop.component;
      jobs.push_back(std::move(job));
    }
  } else {
    // Overlapping: one layer per (depth, label), painted back to front in the
    // final ordering (ascending depth, then descending area, then palette
    // key). Each layer's mask is its own pixels grown a few pixels into
    // STRICTLY LATER painted neighbors, so every shape tucks slightly under
    // the shapes above it: the hairline seams where two independently fitted
    // edges meet (and where antialiased coverage of abutting edges lets the
    // backdrop bleed through) land on solid color instead. The dilation never
    // extends into earlier-painted or untraced pixels, so visible geometry
    // and the silhouette against transparency are unchanged. Holes whose
    // interior is later-painted are dropped (the children stack on top, the
    // existing Overlapping convention); holes over earlier-painted or
    // untraced content stay Subtract.
    struct LayerDef {
      int depth{0};
      std::int32_t label{0};
      std::int64_t area{0};
    };
    std::vector<LayerDef> defs;
    std::unordered_map<std::uint64_t, std::size_t> def_index;
    const auto def_key = [](int depth, std::int32_t label) {
      return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(depth)) << 32U) |
             static_cast<std::uint64_t>(static_cast<std::uint32_t>(label));
    };
    for (std::size_t c = 0; c < components.components.size(); ++c) {
      const auto& component = components.components[c];
      const auto depth = nesting.depth[c];
      const auto key = def_key(depth, component.label);
      auto found = def_index.find(key);
      if (found == def_index.end()) {
        defs.push_back(LayerDef{depth, component.label, 0});
        found = def_index.emplace(key, defs.size() - 1).first;
      }
      defs[found->second].area += component.area;
    }
    std::sort(defs.begin(), defs.end(), [&](const LayerDef& a, const LayerDef& b) {
      if (a.depth != b.depth) {
        return a.depth < b.depth;
      }
      if (a.area != b.area) {
        return a.area > b.area;
      }
      return palette_color_key(map.palette[static_cast<std::size_t>(a.label)]) <
             palette_color_key(map.palette[static_cast<std::size_t>(b.label)]);
    });
    // def_index positions predate the sort; rebuild it against the sorted order.
    std::vector<std::int32_t> component_order(components.components.size(), 0);
    def_index.clear();
    for (std::size_t i = 0; i < defs.size(); ++i) {
      def_index.emplace(def_key(defs[i].depth, defs[i].label), i);
    }
    for (std::size_t c = 0; c < components.components.size(); ++c) {
      component_order[c] = static_cast<std::int32_t>(
          def_index.at(def_key(nesting.depth[c], components.components[c].label)));
    }
    // Per-pixel paint order (-1 untraced).
    std::vector<std::int32_t> order_map(map.labels.size(), -1);
    for (std::size_t p = 0; p < map.labels.size(); ++p) {
      const auto id = components.ids[p];
      if (id >= 0) {
        order_map[p] = component_order[static_cast<std::size_t>(id)];
      }
    }
    const int underlap =
        std::clamp(static_cast<int>(std::ceil(fit.tolerance)) + 1, 2, 4);
    result.layers.reserve(defs.size());
    for (const auto& def : defs) {
      ImageTraceLayer layer;
      layer.color = map.palette[static_cast<std::size_t>(def.label)];
      layer.depth = def.depth;
      layer.area = def.area;
      result.layers.push_back(std::move(layer));
    }
    std::vector<std::vector<FitJob>> per_layer(defs.size());
    parallel_chunks(defs.size(), 1, max_workers, [&](std::size_t begin, std::size_t end) {
      for (std::size_t layer_i = begin; layer_i < end; ++layer_i) {
        if (is_cancelled()) {
          return;
        }
        const auto order = static_cast<std::int32_t>(layer_i);
        const auto& source_bounds = bounds[static_cast<std::size_t>(defs[layer_i].label)];
        if (source_bounds.max_x < 0) {
          continue;
        }
        const auto min_x = std::max(0, source_bounds.min_x - underlap);
        const auto min_y = std::max(0, source_bounds.min_y - underlap);
        const auto max_x = std::min(map.width - 1, source_bounds.max_x + underlap);
        const auto max_y = std::min(map.height - 1, source_bounds.max_y + underlap);
        const auto local_width = max_x - min_x + 1;
        const auto local_height = max_y - min_y + 1;
        const auto stride = static_cast<std::size_t>(local_width) + 2;
        std::vector<std::uint8_t> mask(stride * (static_cast<std::size_t>(local_height) + 2), std::uint8_t{0});
        const auto mask_at = [&](std::int32_t x, std::int32_t y) -> std::uint8_t& {
          return mask[static_cast<std::size_t>(y + 1) * stride + static_cast<std::size_t>(x + 1)];
        };
        for (std::int32_t y = 0; y < local_height; ++y) {
          for (std::int32_t x = 0; x < local_width; ++x) {
            if (order_map[map.index(x + min_x, y + min_y)] == order) {
              mask_at(x, y) = 1;
            }
          }
        }
        // Grow into strictly later-painted pixels, one 8-connected ring per
        // pass, double-buffered so the result is scan-order independent.
        auto grown = mask;
        for (int pass = 0; pass < underlap; ++pass) {
          for (std::int32_t y = 0; y < local_height; ++y) {
            for (std::int32_t x = 0; x < local_width; ++x) {
              if (mask_at(x, y) != 0) {
                continue;
              }
              if (order_map[map.index(x + min_x, y + min_y)] <= order) {
                continue;  // untraced, earlier-painted, or own order elsewhere
              }
              bool touches = false;
              for (std::int32_t dy = -1; dy <= 1 && !touches; ++dy) {
                for (std::int32_t dx = -1; dx <= 1; ++dx) {
                  if (mask_at(x + dx, y + dy) != 0) {
                    touches = true;
                    break;
                  }
                }
              }
              if (touches) {
                grown[static_cast<std::size_t>(y + 1) * stride + static_cast<std::size_t>(x + 1)] = 1;
              }
            }
          }
          mask = grown;
        }
        auto& layer_jobs = per_layer[layer_i];
        for (auto& traced : trace_mask_outlines(mask.data(), local_width, local_height, stride)) {
          const bool hole = loop_signed_area(traced.points) < 0.0;
          if (hole) {
            const auto seed_x = static_cast<std::int32_t>(traced.points.front().x) + min_x;
            const auto seed_y = static_cast<std::int32_t>(traced.points.front().y) + min_y;
            if (order_map[map.index(seed_x, seed_y)] > order) {
              continue;  // later-painted content stacks on top of the solid shape
            }
          }
          FitJob job;
          job.points = std::move(traced.points);
          for (auto& point : job.points) {
            point.x += min_x;
            point.y += min_y;
          }
          job.hole = hole;
          job.layer_slot = layer_i;
          layer_jobs.push_back(std::move(job));
        }
      }
    });
    if (is_cancelled()) {
      return {};
    }
    for (auto& layer_jobs : per_layer) {
      jobs.insert(jobs.end(), std::make_move_iterator(layer_jobs.begin()),
                  std::make_move_iterator(layer_jobs.end()));
    }
  }
  loops.clear();

  // Fit stage: each job settles and fits independently into its own slot
  // (parallel-safe and order-identical to the sequential walk). With an
  // anchor budget the settled polyline is kept as the canonical refit source:
  // every escalation pass refits from the pixel contour, never from a
  // flattening of an earlier fit, so the geometry cannot drift.
  const int budget = std::max(0, options.max_anchors);
  struct LoopFit {
    std::vector<FitPoint> settled;
    PathSubpath best;
  };
  std::vector<LoopFit> fits(jobs.size());
  parallel_chunks(jobs.size(), 16, max_workers, [&](std::size_t begin, std::size_t end) {
    for (std::size_t i = begin; i < end; ++i) {
      if ((i - begin) % 64 == 0 && is_cancelled()) {
        return;
      }
      auto settled = settle_staircase(jobs[i].points, stair_edge);
      fits[i].best = fit_closed_loop(settled, fit);
      if (budget > 0) {
        fits[i].settled = std::move(settled);
      }
    }
  });
  if (is_cancelled()) {
    return {};
  }

  // Anchor budget: escalate ONE global tolerance (x1.5 per pass, capped) and
  // refit every loop from its settled polyline until the total fits, keeping
  // each loop's smaller fit only (Simplify Path's never-worse rule). The
  // parameters stay global for the whole layer (the legal boundary) and the
  // fitter stays the per-run Schneider fit.
  if (budget > 0) {
    const auto total_anchors = [&] {
      std::size_t total = 0;
      for (const auto& fitted : fits) {
        if (subpath_is_visible(fitted.best)) {
          total += fitted.best.anchors.size();
        }
      }
      return total;
    };
    auto total = total_anchors();
    double tolerance = fit.tolerance;
    for (int pass = 0; pass < 12 && total > static_cast<std::size_t>(budget); ++pass) {
      const auto next = std::min(tolerance * 1.5, 16.0);
      if (next == tolerance) {
        break;  // the cap was reached on the previous pass
      }
      tolerance = next;
      PathFitOptions coarse = fit;
      coarse.tolerance = tolerance;
      parallel_chunks(jobs.size(), 16, max_workers, [&](std::size_t begin, std::size_t end) {
        for (std::size_t i = begin; i < end; ++i) {
          if ((i - begin) % 64 == 0 && is_cancelled()) {
            return;
          }
          auto& fitted = fits[i];
          if (fitted.settled.empty() || fitted.best.anchors.size() <= 4) {
            continue;  // minimal loops cannot shrink further
          }
          auto candidate = fit_closed_loop(fitted.settled, coarse);
          if (subpath_is_visible(candidate) && candidate.anchors.size() < fitted.best.anchors.size()) {
            fitted.best = std::move(candidate);
          }
        }
      });
      if (is_cancelled()) {
        return {};
      }
      total = total_anchors();
    }
  }

  // Assemble stage (serial, in trace order): jobs stay in trace order
  // (row-major per label or per painted layer), which puts every hole after
  // its outer loop and every island after the hole it sits in, the order the
  // sequential combine needs.
  for (std::size_t i = 0; i < jobs.size(); ++i) {
    const auto& job = jobs[i];
    auto& layer = result.layers[job.layer_slot];
    if (job.area_component >= 0 && !area_counted[static_cast<std::size_t>(job.area_component)]) {
      area_counted[static_cast<std::size_t>(job.area_component)] = true;
      layer.area += components.components[static_cast<std::size_t>(job.area_component)].area;
    }
    auto& subpath = fits[i].best;
    if (!subpath_is_visible(subpath)) {
      continue;
    }
    subpath.op = job.hole ? PathCombineOp::Subtract : PathCombineOp::Add;
    subpath.shape_group = layer.path.next_shape_group();
    result.anchor_count += subpath.anchors.size();
    layer.path.subpaths.push_back(std::move(subpath));
  }

  // Drop layers whose every loop vanished; order back to front: shallow
  // nesting first, then the largest regions, then palette order.
  std::erase_if(result.layers, [](const ImageTraceLayer& layer) { return layer.path.subpaths.empty(); });
  std::vector<std::size_t> order(result.layers.size());
  for (std::size_t i = 0; i < order.size(); ++i) {
    order[i] = i;
  }
  std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
    const auto& la = result.layers[a];
    const auto& lb = result.layers[b];
    if (la.depth != lb.depth) {
      return la.depth < lb.depth;
    }
    if (la.area != lb.area) {
      return la.area > lb.area;
    }
    return palette_color_key(la.color) < palette_color_key(lb.color);
  });
  std::vector<ImageTraceLayer> ordered;
  ordered.reserve(order.size());
  for (const auto index : order) {
    ordered.push_back(std::move(result.layers[index]));
  }
  result.layers = std::move(ordered);
  return result;
}

PixelBuffer render_image_trace(const ImageTraceResult& result, std::int32_t width, std::int32_t height) {
  if (width <= 0 || height <= 0) {
    return {};
  }
  PixelBuffer output(width, height, PixelFormat::rgba8());
  output.clear(0);
  VectorRasterOptions raster_options;
  raster_options.clip = Rect::from_size(width, height);
  for (const auto& layer : result.layers) {
    const auto coverage = rasterize_vector_path(layer.path, raster_options);
    if (coverage.pixels.empty()) {
      continue;
    }
    for (std::int32_t y = 0; y < coverage.bounds.height; ++y) {
      const auto* source = coverage.pixels.row(y).data();
      const auto target_y = coverage.bounds.y + y;
      if (target_y < 0 || target_y >= height) {
        continue;
      }
      for (std::int32_t x = 0; x < coverage.bounds.width; ++x) {
        const auto target_x = coverage.bounds.x + x;
        if (target_x < 0 || target_x >= width) {
          continue;
        }
        const int cov = source[x];
        if (cov == 0) {
          continue;
        }
        auto* dst = output.pixel(target_x, target_y);
        const int dst_a = dst[3];
        // Straight-alpha "over" in integer math.
        const int inverse = 255 - cov;
        const int out_a = cov + (dst_a * inverse + 127) / 255;
        if (out_a == 0) {
          continue;
        }
        const auto blend = [&](int src_c, int dst_c) {
          const int numerator = src_c * cov * 255 + dst_c * dst_a * inverse;
          return static_cast<std::uint8_t>(std::clamp((numerator + out_a * 255 / 2) / (out_a * 255), 0, 255));
        };
        dst[0] = blend(layer.color.red, dst[0]);
        dst[1] = blend(layer.color.green, dst[1]);
        dst[2] = blend(layer.color.blue, dst[2]);
        dst[3] = static_cast<std::uint8_t>(out_a);
      }
    }
  }
  return output;
}

Layer build_image_trace_group(Document& document, const ImageTraceResult& result, std::int32_t origin_x,
                              std::int32_t origin_y, std::string group_name) {
  const auto canvas_rect = Rect::from_size(document.width(), document.height());
  const auto* patterns = &document.metadata().patterns;
  Layer group(document.allocate_layer_id(), std::move(group_name), LayerKind::Group);
  group.set_blend_mode(BlendMode::PassThrough);
  // Back to front: add_child appends on top.
  for (const auto& traced : result.layers) {
    char name[8];
    std::snprintf(name, sizeof(name), "#%02X%02X%02X", traced.color.red, traced.color.green, traced.color.blue);
    Layer shape(document.allocate_layer_id(), name, LayerKind::Pixel);
    shape.metadata()[kLayerMetadataVectorShape] = "1";
    shape.metadata()[kLayerMetadataVectorRasterStatus] = kVectorRasterStatusPatchy;
    mark_layer_vector_block_dirty(shape);
    VectorShapeContent content;
    content.path = traced.path;
    translate_vector_path(content.path, origin_x, origin_y);
    content.fill.kind = VectorFillKind::Solid;
    content.fill.color = traced.color;
    content.stroke.enabled = false;
    shape.set_vector_shape(std::move(content));
    update_vector_shape_raster(shape, canvas_rect, patterns);
    group.add_child(std::move(shape));
  }
  return group;
}

}  // namespace patchy
