#include "filters/smart_filter_distort_stylize.hpp"

#include "core/rect_utils.hpp"
#include "core/smart_filter_schema.hpp"
#include "filters/filter_kernels.hpp"
#include "support/translate_noop.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <utility>
#include <vector>

namespace patchy {
namespace {

void report(const FilterProgress* progress, int completed, int total, FilterProgressStage stage) {
  if (progress != nullptr && progress->update && !progress->update(completed, total, stage)) {
    throw FilterCancelled();
  }
}

// Photoshop's Solarize: values above 127 invert, the rest pass through.
// Alpha and bounds are unchanged.
FilterRenderResult render_solarize(const FilterRenderResult& input, const FilterProgress* progress) {
  FilterRenderResult result = input;
  auto& pixels = result.pixels;
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    if ((y & 63) == 0) {
      report(progress, y, pixels.height(), FilterProgressStage::Filtering);
    }
    auto* row = pixels.pixel(0, y);
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* px = row + static_cast<std::size_t>(x) * 4U;
      for (int channel = 0; channel < 3; ++channel) {
        if (px[channel] > 127) {
          px[channel] = static_cast<std::uint8_t>(255 - px[channel]);
        }
      }
    }
  }
  report(progress, pixels.height(), pixels.height(), FilterProgressStage::Filtering);
  return result;
}

// round(sqrt(1 + (i / 256)^2) * 16384) for i in 0..256, in integers so every
// toolchain builds the same table: round(sqrt(4096 * (65536 + i^2))).
const std::array<std::int64_t, 257>& find_edges_hypot_table() {
  static const auto table = [] {
    std::array<std::int64_t, 257> values{};
    for (std::int64_t index = 0; index <= 256; ++index) {
      const auto n = 4096 * (65536 + index * index);
      auto root = static_cast<std::int64_t>(std::sqrt(static_cast<double>(n)));
      while (root * root > n) {
        --root;
      }
      while ((root + 1) * (root + 1) <= n) {
        ++root;
      }
      values[static_cast<std::size_t>(index)] = n - root * root > root ? root + 1 : root;
    }
    return values;
  }();
  return table;
}

// Photoshop's Find Edges (calibration in docs/smart-filters-distort-stylize.md):
// each pixel is matted over white, a 3x3 Sobel pair per channel (edge-clamped
// at the filter canvas, white beyond the content) gives an edge strength
// max(|gx|,|gy|) * T[256 * min / max] >> 14 (the table above), and
// 255 - strength is un-matted from white again. Alpha and bounds are unchanged.
FilterRenderResult render_find_edges(const FilterRenderResult& input, Rect canvas,
                                     const FilterProgress* progress) {
  const auto bounds = input.bounds;
  FilterRenderResult result = input;
  if (bounds.width <= 0 || bounds.height <= 0) {
    return result;
  }
  const auto& table = find_edges_hypot_table();
  // Matted samples for the content grown by one pixel; positions outside the
  // canvas repeat its edge, positions outside the content are transparent.
  const auto padded_width = bounds.width + 2;
  const auto padded_height = bounds.height + 2;
  std::vector<std::int32_t> matte(static_cast<std::size_t>(padded_width) *
                                  static_cast<std::size_t>(padded_height) * 3U);
  const auto canvas_right = canvas.x + canvas.width - 1;
  const auto canvas_bottom = canvas.y + canvas.height - 1;
  for (std::int32_t py = 0; py < padded_height; ++py) {
    const auto doc_y = std::clamp(bounds.y + py - 1, canvas.y, std::max(canvas.y, canvas_bottom));
    for (std::int32_t px = 0; px < padded_width; ++px) {
      const auto doc_x =
          std::clamp(bounds.x + px - 1, canvas.x, std::max(canvas.x, canvas_right));
      auto* out = matte.data() +
                  (static_cast<std::size_t>(py) * static_cast<std::size_t>(padded_width) +
                   static_cast<std::size_t>(px)) *
                      3U;
      const auto local_x = doc_x - bounds.x;
      const auto local_y = doc_y - bounds.y;
      if (local_x < 0 || local_y < 0 || local_x >= bounds.width || local_y >= bounds.height) {
        out[0] = out[1] = out[2] = 255;
        continue;
      }
      const auto* src = input.pixels.pixel(local_x, local_y);
      const std::int32_t alpha = src[3];
      for (int channel = 0; channel < 3; ++channel) {
        out[channel] = (src[channel] * alpha + 255 * (255 - alpha) + 127) / 255;
      }
    }
  }
  const auto sample = [&](std::int32_t px, std::int32_t py, int channel) {
    return matte[(static_cast<std::size_t>(py) * static_cast<std::size_t>(padded_width) +
                  static_cast<std::size_t>(px)) *
                     3U +
                 static_cast<std::size_t>(channel)];
  };
  for (std::int32_t y = 0; y < bounds.height; ++y) {
    if ((y & 31) == 0) {
      report(progress, y, bounds.height, FilterProgressStage::DetectingEdges);
    }
    for (std::int32_t x = 0; x < bounds.width; ++x) {
      auto* px = result.pixels.pixel(x, y);
      const std::int64_t alpha = px[3];
      if (alpha == 0) {
        px[0] = px[1] = px[2] = 255;
        continue;
      }
      const auto cx = x + 1;
      const auto cy = y + 1;
      for (int channel = 0; channel < 3; ++channel) {
        const std::int64_t gx =
            (sample(cx + 1, cy - 1, channel) + 2 * sample(cx + 1, cy, channel) +
             sample(cx + 1, cy + 1, channel)) -
            (sample(cx - 1, cy - 1, channel) + 2 * sample(cx - 1, cy, channel) +
             sample(cx - 1, cy + 1, channel));
        const std::int64_t gy =
            (sample(cx - 1, cy + 1, channel) + 2 * sample(cx, cy + 1, channel) +
             sample(cx + 1, cy + 1, channel)) -
            (sample(cx - 1, cy - 1, channel) + 2 * sample(cx, cy - 1, channel) +
             sample(cx + 1, cy - 1, channel));
        const auto high = std::max(std::abs(gx), std::abs(gy));
        const auto low = std::min(std::abs(gx), std::abs(gy));
        const auto strength =
            high == 0 ? std::int64_t{0}
                      : (high * table[static_cast<std::size_t>(low * 256 / high)]) >> 14;
        const auto ink = std::min<std::int64_t>(255, strength);
        const auto straight_ink = std::min<std::int64_t>(255, (ink * 255 + alpha / 2) / alpha);
        px[channel] = static_cast<std::uint8_t>(255 - straight_ink);
      }
    }
  }
  report(progress, bounds.height, bounds.height, FilterProgressStage::DetectingEdges);
  return result;
}

// The stack result so far, placed in the transparent document-space filter
// canvas, so geometric filters can pull content from anywhere in it.
PixelBuffer embed_in_canvas(const FilterRenderResult& input, Rect canvas) {
  PixelBuffer result(canvas.width, canvas.height, PixelFormat::rgba8());
  result.clear(0);
  const auto copied = intersect_rect(input.bounds, canvas);
  for (std::int32_t y = 0; y < copied.height; ++y) {
    const auto* source = input.pixels.pixel(copied.x - input.bounds.x, copied.y - input.bounds.y + y);
    auto* destination = result.pixel(copied.x - canvas.x, copied.y - canvas.y + y);
    std::copy(source, source + static_cast<std::size_t>(copied.width) * 4U, destination);
  }
  return result;
}

// Crops a canvas-sized result to its visible pixels (an empty result keeps a
// zero-sized rect at the canvas origin).
FilterRenderResult trim_to_visible(PixelBuffer pixels, Rect canvas) {
  std::int32_t left = pixels.width();
  std::int32_t top = pixels.height();
  std::int32_t right = -1;
  std::int32_t bottom = -1;
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    const auto* row = pixels.pixel(0, y);
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      if (row[static_cast<std::size_t>(x) * 4U + 3U] != 0) {
        left = std::min(left, x);
        right = std::max(right, x);
        top = std::min(top, y);
        bottom = std::max(bottom, y);
      }
    }
  }
  if (right < left) {
    return FilterRenderResult{PixelBuffer(0, 0, PixelFormat::rgba8()), Rect{canvas.x, canvas.y, 0, 0}};
  }
  const Rect local{left, top, right - left + 1, bottom - top + 1};
  if (local.width == pixels.width() && local.height == pixels.height()) {
    return FilterRenderResult{std::move(pixels), canvas};
  }
  PixelBuffer cropped(local.width, local.height, PixelFormat::rgba8());
  for (std::int32_t y = 0; y < local.height; ++y) {
    const auto* source = pixels.pixel(local.x, local.y + y);
    std::copy(source, source + static_cast<std::size_t>(local.width) * 4U, cropped.pixel(0, y));
  }
  return FilterRenderResult{std::move(cropped),
                            Rect{canvas.x + local.x, canvas.y + local.y, local.width, local.height}};
}

// Inverse-mapped geometric filters: every canvas pixel samples the canvas at
// the position `source_of(x, y)` returns (canvas pixel coordinates),
// bilinearly on premultiplied RGBA with the canvas edge repeated.
template <typename SourceOf>
FilterRenderResult render_inverse_mapped(const FilterRenderResult& input, Rect canvas,
                                         const FilterProgress* progress, SourceOf source_of) {
  const auto original = embed_in_canvas(input, canvas);
  PixelBuffer result(canvas.width, canvas.height, PixelFormat::rgba8());
  for (std::int32_t y = 0; y < canvas.height; ++y) {
    if ((y & 15) == 0) {
      report(progress, y, canvas.height, FilterProgressStage::Distorting);
    }
    for (std::int32_t x = 0; x < canvas.width; ++x) {
      const auto [source_x, source_y] = source_of(static_cast<double>(x), static_cast<double>(y));
      PremultipliedAccum accum;
      accumulate_bilinear_sample(accum, original, source_x, source_y);
      write_accumulated_pixel(result, x, y, accum);
    }
  }
  report(progress, canvas.height, canvas.height, FilterProgressStage::Distorting);
  return trim_to_visible(std::move(result), canvas);
}

constexpr double kPi = 3.14159265358979323846;

// Photoshop's ZigZag over the whole filter canvas (calibration in
// docs/smart-filters-distort-stylize.md): normalized ellipse coordinates
// u = (x - W/2) / (W/2), v = (y - H/2) / (H/2), r = |(u, v)|, and inside the
// ellipse the profile s = (1 - r)(1 - cos(2 pi ridges r)). Around Center turns
// the source by -amount * s degrees; Pond Ripples and Out From Center (which
// Photoshop renders identically) shift it by amount * s pixels along (+1, +1),
// each axis scaled by its half extent over the larger half extent.
FilterRenderResult render_zigzag(const FilterRenderResult& input, Rect canvas, std::int32_t amount,
                                 std::int32_t ridges, std::int32_t style,
                                 const FilterProgress* progress) {
  const auto half_width = static_cast<double>(canvas.width) * 0.5;
  const auto half_height = static_cast<double>(canvas.height) * 0.5;
  const auto larger = std::max(half_width, half_height);
  const auto around_center = style == 0;
  return render_inverse_mapped(input, canvas, progress, [&](double x, double y) {
    const auto u = (x - half_width) / half_width;
    const auto v = (y - half_height) / half_height;
    const auto r = std::sqrt(u * u + v * v);
    if (r >= 1.0) {
      return std::pair{x, y};
    }
    const auto profile = (1.0 - r) * (1.0 - std::cos(2.0 * kPi * ridges * r));
    if (around_center) {
      const auto angle = std::atan2(v, u) - static_cast<double>(amount) * profile * kPi / 180.0;
      return std::pair{half_width + half_width * r * std::cos(angle),
                       half_height + half_height * r * std::sin(angle)};
    }
    const auto shift = static_cast<double>(amount) * profile;
    return std::pair{x + shift * half_width / larger, y + shift * half_height / larger};
  });
}

// splitmix64 (AGENTS.md: deterministic across toolchains) with an explicit
// 53-bit uniform mapping to [0, 1).
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

// Wave (docs/smart-filters-distort-stylize.md): every generator adds one wave
// of period 2 * wavelength pixels and amplitude 0.78125 * amplitude pixels
// (Photoshop's units, independent of canvas size); the horizontal shift of a
// pixel depends on its row and the vertical shift on its column, scaled by the
// two Scale percentages. Photoshop draws each generator's wavelength,
// amplitude and phases from its own random generator, which probes cannot
// reproduce, so Patchy draws them from the stored seed with splitmix64: the
// same settings give the same Patchy image, with Photoshop's character but not
// its exact pattern.
FilterRenderResult render_wave(const FilterRenderResult& input, Rect canvas,
                               const SchemaSmartFilter& settings,
                               const FilterProgress* progress) {
  const auto& v = settings.values;
  const auto type = v[0];
  const auto generators = v[1];
  const auto wavelength_low = std::min(v[2], v[3]);
  const auto wavelength_high = std::max(v[2], v[3]);
  const auto amplitude_low = std::min(v[4], v[5]);
  const auto amplitude_high = std::max(v[4], v[5]);
  const auto scale_x = static_cast<double>(v[6]) / 100.0;
  const auto scale_y = static_cast<double>(v[7]) / 100.0;
  const auto wrap = v[8] == 0;
  SplitMix64 random{static_cast<std::uint64_t>(static_cast<std::uint32_t>(v[9]))};
  struct Generator {
    double wavelength;
    double amplitude;
    double phase_x;
    double phase_y;
  };
  std::vector<Generator> waves;
  waves.reserve(static_cast<std::size_t>(generators));
  for (std::int32_t index = 0; index < generators; ++index) {
    Generator wave{};
    wave.wavelength = static_cast<double>(wavelength_low) +
                      std::floor(random.uniform() *
                                 static_cast<double>(wavelength_high - wavelength_low + 1));
    wave.amplitude = 0.78125 * (static_cast<double>(amplitude_low) +
                                std::floor(random.uniform() *
                                           static_cast<double>(amplitude_high - amplitude_low + 1)));
    wave.phase_x = 2.0 * kPi * random.uniform();
    wave.phase_y = 2.0 * kPi * random.uniform();
    waves.push_back(wave);
  }
  const auto shape = [type](double t) {
    const auto s = std::sin(t);
    if (type == 1) {
      return std::asin(s) * (2.0 / kPi);
    }
    if (type == 2) {
      return s >= 0.0 ? 1.0 : -1.0;
    }
    return s;
  };
  std::vector<double> shift_x(static_cast<std::size_t>(canvas.height), 0.0);
  std::vector<double> shift_y(static_cast<std::size_t>(canvas.width), 0.0);
  for (const auto& wave : waves) {
    for (std::int32_t y = 0; y < canvas.height; ++y) {
      shift_x[static_cast<std::size_t>(y)] +=
          scale_x * wave.amplitude * shape(kPi * static_cast<double>(y) / wave.wavelength + wave.phase_x);
    }
    for (std::int32_t x = 0; x < canvas.width; ++x) {
      shift_y[static_cast<std::size_t>(x)] +=
          scale_y * wave.amplitude * shape(kPi * static_cast<double>(x) / wave.wavelength + wave.phase_y);
    }
  }
  const auto width = static_cast<double>(canvas.width);
  const auto height = static_cast<double>(canvas.height);
  return render_inverse_mapped(input, canvas, progress, [&](double x, double y) {
    auto source_x = x + shift_x[static_cast<std::size_t>(y)];
    auto source_y = y + shift_y[static_cast<std::size_t>(x)];
    if (wrap) {
      source_x -= width * std::floor(source_x / width);
      source_y -= height * std::floor(source_y / height);
    }
    return std::pair{source_x, source_y};
  });
}

// One lattice value in [-1, 1] for value noise, hashed from its integer
// position and a salt with splitmix64 (deterministic on every toolchain).
double lattice_value(std::int64_t x, std::int64_t y, std::uint64_t salt) noexcept {
  SplitMix64 hash{static_cast<std::uint64_t>(x) * 0x9e3779b97f4a7c15ULL ^
                  static_cast<std::uint64_t>(y) * 0xc2b2ae3d27d4eb4fULL ^ salt};
  return hash.uniform() * 2.0 - 1.0;
}

// Smooth value noise: lattice values every cell_x by cell_y pixels,
// smoothstep-interpolated.
double value_noise(double x, double y, double cell_x, double cell_y, std::uint64_t salt) noexcept {
  const auto gx = x / cell_x;
  const auto gy = y / cell_y;
  const auto ix = static_cast<std::int64_t>(std::floor(gx));
  const auto iy = static_cast<std::int64_t>(std::floor(gy));
  const auto smooth = [](double t) { return t * t * (3.0 - 2.0 * t); };
  const auto tx = smooth(gx - static_cast<double>(ix));
  const auto ty = smooth(gy - static_cast<double>(iy));
  const auto top = lattice_value(ix, iy, salt) * (1.0 - tx) + lattice_value(ix + 1, iy, salt) * tx;
  const auto bottom =
      lattice_value(ix, iy + 1, salt) * (1.0 - tx) + lattice_value(ix + 1, iy + 1, salt) * tx;
  return top * (1.0 - ty) + bottom * ty;
}

// Ripple (docs/smart-filters-distort-stylize.md). Photoshop's displacement is
// one fixed noise field per size scaled linearly by Amount / 100; the field
// itself comes from Photoshop's own generator and cannot be reproduced. Patchy
// uses its own smooth value noise with the measured statistics: each axis's
// shift varies fast along that axis and slowly across it (cells below, in
// pixels) with the measured spread (standard deviation in pixels at Amount 100).
FilterRenderResult render_ripple(const FilterRenderResult& input, Rect canvas, std::int32_t amount,
                                 std::int32_t size, const FilterProgress* progress) {
  struct RippleSize {
    double along;
    double across;
    double spread;
  };
  // Small, Medium, Large.
  constexpr RippleSize kSizes[]{{3.5, 10.5, 0.57}, {9.0, 10.5, 0.83}, {20.0, 10.5, 2.46}};
  // Standard deviation of smoothstep value noise with uniform [-1, 1] lattice values.
  constexpr double kValueNoiseSpread = 0.43;
  const auto& ripple = kSizes[static_cast<std::size_t>(std::clamp(size, 0, 2))];
  const auto scale = static_cast<double>(amount) / 100.0 * ripple.spread / kValueNoiseSpread;
  return render_inverse_mapped(input, canvas, progress, [&](double x, double y) {
    const auto doc_x = x + static_cast<double>(canvas.x);
    const auto doc_y = y + static_cast<double>(canvas.y);
    const auto shift_x = scale * value_noise(doc_x, doc_y, ripple.along, ripple.across, 0x52706c65ULL);
    const auto shift_y = scale * value_noise(doc_x, doc_y, ripple.across, ripple.along, 0x58706c66ULL);
    return std::pair{x + shift_x, y + shift_y};
  });
}

// Shear (docs/smart-filters-distort-stylize.md): the curve's points are
// (horizontal, vertical) in a 128-unit grid; a natural cubic spline through
// them, as a function of the vertical coordinate, gives each row's shift. Row y
// reads the spline at y * 128 / (H - 1); the shift in pixels is the spline
// value * H / 128 (Photoshop scales both axes by the height), and content moves
// right by it. Outside the first and last point the curve holds their values.
FilterRenderResult render_shear(const FilterRenderResult& input, Rect canvas,
                                const SchemaSmartFilter& settings,
                                const FilterProgress* progress) {
  struct Knot {
    double vertical;
    double horizontal;
  };
  std::vector<Knot> knots;
  for (std::size_t index = 0; index + 1U < settings.points.size(); index += 2U) {
    knots.push_back({settings.points[index + 1U], settings.points[index]});
  }
  std::stable_sort(knots.begin(), knots.end(),
                   [](const Knot& left, const Knot& right) { return left.vertical < right.vertical; });
  knots.erase(std::unique(knots.begin(), knots.end(),
                          [](const Knot& left, const Knot& right) {
                            return left.vertical == right.vertical;
                          }),
              knots.end());
  const auto count = knots.size();
  // Natural cubic: zero second derivative at both ends (the Curves solver).
  std::vector<double> second(count, 0.0);
  std::vector<double> work(count, 0.0);
  for (std::size_t index = 1U; index + 1U < count; ++index) {
    const auto previous_span = knots[index].vertical - knots[index - 1U].vertical;
    const auto next_span = knots[index + 1U].vertical - knots[index].vertical;
    const auto sigma = previous_span / (previous_span + next_span);
    const auto pivot = sigma * second[index - 1U] + 2.0;
    second[index] = (sigma - 1.0) / pivot;
    const auto previous_slope = (knots[index].horizontal - knots[index - 1U].horizontal) / previous_span;
    const auto next_slope = (knots[index + 1U].horizontal - knots[index].horizontal) / next_span;
    work[index] = (6.0 * (next_slope - previous_slope) / (previous_span + next_span) -
                   sigma * work[index - 1U]) /
                  pivot;
  }
  for (std::size_t upper = count; upper-- > 1U;) {
    second[upper - 1U] = second[upper - 1U] * second[upper] + work[upper - 1U];
  }
  const auto curve = [&](double vertical) {
    if (count == 0U) {
      return 0.0;
    }
    if (count == 1U || vertical <= knots.front().vertical) {
      return knots.front().horizontal;
    }
    if (vertical >= knots.back().vertical) {
      return knots.back().horizontal;
    }
    std::size_t upper = 1U;
    while (upper + 1U < count && vertical > knots[upper].vertical) {
      ++upper;
    }
    const auto span = knots[upper].vertical - knots[upper - 1U].vertical;
    const auto left = (knots[upper].vertical - vertical) / span;
    const auto right = (vertical - knots[upper - 1U].vertical) / span;
    return left * knots[upper - 1U].horizontal + right * knots[upper].horizontal +
           ((left * left * left - left) * second[upper - 1U] +
            (right * right * right - right) * second[upper]) *
               span * span / 6.0;
  };
  const auto height = static_cast<double>(canvas.height);
  const auto width = static_cast<double>(canvas.width);
  std::vector<double> shifts(static_cast<std::size_t>(canvas.height));
  for (std::int32_t y = 0; y < canvas.height; ++y) {
    const auto vertical =
        canvas.height > 1 ? static_cast<double>(y) * 128.0 / (height - 1.0) : 0.0;
    shifts[static_cast<std::size_t>(y)] = curve(vertical) * height / 128.0;
  }
  const auto wrap = settings.values[1] == 0;
  return render_inverse_mapped(input, canvas, progress, [&](double x, double y) {
    auto source_x = x - shifts[static_cast<std::size_t>(y)];
    if (wrap) {
      source_x -= width * std::floor(source_x / width);
    }
    return std::pair{source_x, y};
  });
}

}  // namespace

FilterRenderResult render_schema_smart_filter(const FilterRenderResult& current,
                                              Rect filter_canvas_bounds, SmartFilterKind kind,
                                              const SchemaSmartFilter& settings,
                                              const FilterProgress* progress) {
  const auto* schema = find_smart_filter_schema(kind);
  if (schema == nullptr || !schema_settings_valid(*schema, settings) ||
      current.pixels.format() != PixelFormat::rgba8() ||
      current.pixels.width() != current.bounds.width ||
      current.pixels.height() != current.bounds.height) {
    throw std::invalid_argument(PATCHY_TRANSLATE_NOOP("QObject", "Unsupported Smart Filter entry"));
  }
  switch (kind) {
    case SmartFilterKind::Solarize:
      return render_solarize(current, progress);
    case SmartFilterKind::FindEdges:
      return render_find_edges(current, filter_canvas_bounds, progress);
    case SmartFilterKind::ZigZag:
      return render_zigzag(current, filter_canvas_bounds, settings.values[0], settings.values[1],
                           settings.values[2], progress);
    case SmartFilterKind::Wave:
      return render_wave(current, filter_canvas_bounds, settings, progress);
    case SmartFilterKind::Ripple:
      return render_ripple(current, filter_canvas_bounds, settings.values[0], settings.values[1],
                           progress);
    case SmartFilterKind::Shear:
      return render_shear(current, filter_canvas_bounds, settings, progress);
    default:
      break;
  }
  throw std::invalid_argument(PATCHY_TRANSLATE_NOOP("QObject", "Unsupported Smart Filter entry"));
}

}  // namespace patchy
