#include "filters/smart_filter_table_render.hpp"

#include "support/translate_noop.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

// Photoshop calibration for every kernel here: docs/smart-filters-pixelate-render.md.
namespace patchy {

namespace {

class RowProgress {
public:
  RowProgress(const FilterProgress *progress, std::int64_t total)
      : progress_(progress), total_(std::max<std::int64_t>(1, total)) {}

  void step(std::int64_t done) const {
    if (progress_ == nullptr || !progress_->update) {
      return;
    }
    constexpr std::int64_t kScale = 1000;
    const auto scaled = static_cast<int>(
        std::clamp<std::int64_t>(done, 0, total_) * kScale / total_);
    if (!progress_->update(scaled, static_cast<int>(kScale),
                           FilterProgressStage::Filtering)) {
      throw FilterCancelled();
    }
  }

private:
  const FilterProgress *progress_;
  std::int64_t total_;
};

std::int32_t integer_at(const TableSmartFilter &values, std::size_t index) {
  return std::get<std::int32_t>(values.values.at(index));
}

double number_at(const TableSmartFilter &values, std::size_t index) {
  return std::get<double>(values.values.at(index));
}

const std::string &token_at(const TableSmartFilter &values,
                            std::size_t index) {
  return std::get<std::string>(values.values.at(index));
}

const std::vector<std::int32_t> &list_at(const TableSmartFilter &values,
                                         std::size_t index) {
  return std::get<std::vector<std::int32_t>>(values.values.at(index));
}

std::uint8_t clamp_byte(std::int64_t value) noexcept {
  return static_cast<std::uint8_t>(std::clamp<std::int64_t>(value, 0, 255));
}

// Floor division for a possibly negative numerator.
std::int64_t floor_divide(std::int64_t numerator,
                          std::int64_t denominator) noexcept {
  auto quotient = numerator / denominator;
  if ((numerator % denominator != 0) &&
      ((numerator < 0) != (denominator < 0))) {
    --quotient;
  }
  return quotient;
}

// Straight RGB for every pixel, alpha-0 pixels taking the color of their
// nearest visible pixel by MANHATTAN distance; ties prefer the most vertical
// source (smallest |dx|), then the one below, then the one to the right. This
// is the transparent-color rule Photoshop's Minimum and Custom read (October
// 2026 probes; Median's rule differs, see smart-filters-native.md). An
// all-transparent canvas keeps its stored RGB.
PixelBuffer extend_transparent_manhattan(const PixelBuffer &pixels,
                                         const RowProgress &progress) {
  const auto width = pixels.width();
  const auto height = pixels.height();
  PixelBuffer result = pixels;
  bool saw_visible = false;
  bool saw_transparent = false;
  for (std::int32_t y = 0; y < height && !(saw_visible && saw_transparent);
       ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      if (pixels.pixel(x, y)[3] != 0U) {
        saw_visible = true;
      } else {
        saw_transparent = true;
      }
    }
  }
  if (!saw_visible || !saw_transparent) {
    return result;
  }
  // Vertical pass: each column's nearest visible row, ties going down.
  constexpr std::int32_t kNone = -1;
  const auto count = static_cast<std::size_t>(width) *
                     static_cast<std::size_t>(height);
  std::vector<std::int32_t> source_row(count, kNone);
  std::vector<std::int32_t> above(static_cast<std::size_t>(height));
  for (std::int32_t x = 0; x < width; ++x) {
    std::int32_t last = kNone;
    for (std::int32_t y = 0; y < height; ++y) {
      if (pixels.pixel(x, y)[3] != 0U) {
        last = y;
      }
      above[static_cast<std::size_t>(y)] = last;
    }
    std::int32_t next = kNone;
    for (std::int32_t y = height; y-- > 0;) {
      if (pixels.pixel(x, y)[3] != 0U) {
        next = y;
      }
      const auto up = above[static_cast<std::size_t>(y)];
      std::int32_t chosen = next;
      if (up != kNone && (next == kNone || y - up < next - y)) {
        chosen = up;
      }
      source_row[static_cast<std::size_t>(y) *
                     static_cast<std::size_t>(width) +
                 static_cast<std::size_t>(x)] = chosen;
    }
  }
  progress.step(1);
  // Horizontal sweeps: the best column candidate under the key
  // (|dx| + |dy|, |dx|, -dy, -dx). Shifting every candidate one column keeps
  // their order, so a left and a right running best are exact.
  struct Candidate {
    std::int32_t x{kNone};
    std::int32_t y{kNone};
  };
  const auto better = [](const Candidate &a, const Candidate &b,
                         std::int32_t x, std::int32_t y) {
    if (a.x == kNone) {
      return false;
    }
    if (b.x == kNone) {
      return true;
    }
    const auto adx = std::abs(a.x - x);
    const auto bdx = std::abs(b.x - x);
    const auto ad = adx + std::abs(a.y - y);
    const auto bd = bdx + std::abs(b.y - y);
    if (ad != bd) {
      return ad < bd;
    }
    if (adx != bdx) {
      return adx < bdx;
    }
    if (a.y != b.y) {
      return a.y > b.y;
    }
    return a.x > b.x;
  };
  std::vector<Candidate> left(static_cast<std::size_t>(width));
  for (std::int32_t y = 0; y < height; ++y) {
    progress.step(1 + y);
    const auto row = static_cast<std::size_t>(y) *
                     static_cast<std::size_t>(width);
    Candidate running;
    for (std::int32_t x = 0; x < width; ++x) {
      const Candidate own{source_row[row + static_cast<std::size_t>(x)] ==
                                  kNone
                              ? kNone
                              : x,
                          source_row[row + static_cast<std::size_t>(x)]};
      if (own.x != kNone && (running.x == kNone || better(own, running, x, y))) {
        running = own;
      }
      left[static_cast<std::size_t>(x)] = running;
    }
    running = Candidate{};
    for (std::int32_t x = width; x-- > 0;) {
      const Candidate own{source_row[row + static_cast<std::size_t>(x)] ==
                                  kNone
                              ? kNone
                              : x,
                          source_row[row + static_cast<std::size_t>(x)]};
      if (own.x != kNone && (running.x == kNone || better(own, running, x, y))) {
        running = own;
      }
      auto best = left[static_cast<std::size_t>(x)];
      if (better(running, best, x, y)) {
        best = running;
      }
      auto *destination = result.pixel(x, y);
      if (destination[3] != 0U || best.x == kNone) {
        continue;
      }
      const auto *source = pixels.pixel(best.x, best.y);
      destination[0] = source[0];
      destination[1] = source[1];
      destination[2] = source[2];
    }
  }
  return result;
}

FilterRenderResult render_offset(const FilterRenderResult &canvas,
                                 const TableSmartFilter &values,
                                 const RowProgress &progress) {
  const auto width = canvas.bounds.width;
  const auto height = canvas.bounds.height;
  const auto horizontal = static_cast<std::int64_t>(integer_at(values, 0));
  const auto vertical = static_cast<std::int64_t>(integer_at(values, 1));
  const auto &mode = token_at(values, 2);
  PixelBuffer output(width, height, PixelFormat::rgba8());
  output.clear(0);
  for (std::int32_t y = 0; y < height; ++y) {
    progress.step(y);
    auto source_y = static_cast<std::int64_t>(y) - vertical;
    if (mode == "wrap") {
      source_y = ((source_y % height) + height) % height;
    } else if (mode == "repeat") {
      source_y = std::clamp<std::int64_t>(source_y, 0, height - 1);
    } else if (source_y < 0 || source_y >= height) {
      continue;
    }
    for (std::int32_t x = 0; x < width; ++x) {
      auto source_x = static_cast<std::int64_t>(x) - horizontal;
      if (mode == "wrap") {
        source_x = ((source_x % width) + width) % width;
      } else if (mode == "repeat") {
        source_x = std::clamp<std::int64_t>(source_x, 0, width - 1);
      } else if (source_x < 0 || source_x >= width) {
        continue;
      }
      const auto *source =
          canvas.pixels.pixel(static_cast<std::int32_t>(source_x),
                              static_cast<std::int32_t>(source_y));
      std::copy(source, source + 4, output.pixel(x, y));
    }
  }
  return FilterRenderResult{std::move(output), canvas.bounds};
}

// One line of a running min or max over [i - radius, i + radius] clipped to
// the line (van Herk / Gil-Werman blocks, O(1) per sample).
template <bool kMaximum>
void running_extreme(const std::uint8_t *input, std::size_t stride,
                     std::int32_t length, std::int32_t radius,
                     std::uint8_t *output, std::vector<std::uint8_t> &prefix,
                     std::vector<std::uint8_t> &suffix) {
  constexpr std::uint8_t kIdentity = kMaximum ? 0U : 255U;
  const auto pick = [](std::uint8_t a, std::uint8_t b) {
    return kMaximum ? std::max(a, b) : std::min(a, b);
  };
  const auto window = static_cast<std::int64_t>(radius) * 2 + 1;
  const auto padded = static_cast<std::int64_t>(length) + 2 * radius;
  const auto blocks = (padded + window - 1) / window;
  const auto size = static_cast<std::size_t>(blocks * window);
  prefix.assign(size, kIdentity);
  suffix.assign(size, kIdentity);
  const auto sample = [&](std::int64_t index) {
    const auto source = index - radius;
    return source < 0 || source >= length
               ? kIdentity
               : input[static_cast<std::size_t>(source) * stride];
  };
  for (std::int64_t block = 0; block < blocks; ++block) {
    const auto start = block * window;
    auto running = kIdentity;
    for (std::int64_t offset = 0; offset < window; ++offset) {
      running = pick(running, sample(start + offset));
      prefix[static_cast<std::size_t>(start + offset)] = running;
    }
    running = kIdentity;
    for (std::int64_t offset = window; offset-- > 0;) {
      running = pick(running, sample(start + offset));
      suffix[static_cast<std::size_t>(start + offset)] = running;
    }
  }
  for (std::int32_t i = 0; i < length; ++i) {
    // Padded window [i, i + 2 * radius].
    output[static_cast<std::size_t>(i) * stride] =
        pick(suffix[static_cast<std::size_t>(i)],
             prefix[static_cast<std::size_t>(i) +
                    static_cast<std::size_t>(2 * radius)]);
  }
}

// Maximum / Minimum with Photoshop's Squareness: a (2n+1)^2 window, n the
// stored radius rounded half up with a minimum of 1, clipped at the canvas.
// RGB takes the extreme of the Manhattan-extended straight colors; alpha takes
// the OPPOSITE extreme (Maximum shrinks the visible area, Minimum grows it).
FilterRenderResult render_morphology(const FilterRenderResult &canvas,
                                     const TableSmartFilter &values,
                                     bool maximum,
                                     const RowProgress &progress) {
  const auto width = canvas.bounds.width;
  const auto height = canvas.bounds.height;
  const auto radius = static_cast<std::int32_t>(std::clamp(
      std::floor(number_at(values, 0) + 0.5), 1.0,
      static_cast<double>(std::max(width, height))));
  auto work = extend_transparent_manhattan(canvas.pixels, progress);
  PixelBuffer pass(width, height, PixelFormat::rgba8());
  std::vector<std::uint8_t> prefix;
  std::vector<std::uint8_t> suffix;
  const auto run = [&](const std::uint8_t *input, std::size_t stride,
                       std::int32_t length, std::uint8_t *output,
                       bool use_maximum) {
    if (use_maximum) {
      running_extreme<true>(input, stride, length, radius, output, prefix,
                            suffix);
    } else {
      running_extreme<false>(input, stride, length, radius, output, prefix,
                             suffix);
    }
  };
  for (std::int32_t y = 0; y < height; ++y) {
    progress.step(height + y);
    for (int channel = 0; channel < 4; ++channel) {
      const auto use_maximum = channel == 3 ? !maximum : maximum;
      run(work.pixel(0, y) + channel, 4U, width, pass.pixel(0, y) + channel,
          use_maximum);
    }
  }
  const auto row_stride = static_cast<std::size_t>(width) * 4U;
  for (std::int32_t x = 0; x < width; ++x) {
    progress.step(static_cast<std::int64_t>(height) * 2 +
                  static_cast<std::int64_t>(x) * height / std::max(1, width));
    for (int channel = 0; channel < 4; ++channel) {
      const auto use_maximum = channel == 3 ? !maximum : maximum;
      run(pass.pixel(x, 0) + channel, row_stride, height,
          work.pixel(x, 0) + channel, use_maximum);
    }
  }
  return FilterRenderResult{std::move(work), canvas.bounds};
}

// Custom: a 5x5 integer kernel over the Manhattan-extended straight RGB,
// edge-clamped; Photoshop divides in 15-bit fixed point:
// ((sum + floor(scale / 2)) * floor(32768 / scale)) >> 15, plus Offset,
// clamped. Alpha is unchanged.
FilterRenderResult render_custom(const FilterRenderResult &canvas,
                                 const TableSmartFilter &values,
                                 const RowProgress &progress) {
  const auto width = canvas.bounds.width;
  const auto height = canvas.bounds.height;
  const auto &matrix = list_at(values, 0);
  const auto scale = static_cast<std::int64_t>(integer_at(values, 1));
  const auto offset = static_cast<std::int64_t>(integer_at(values, 2));
  const auto reciprocal = static_cast<std::int64_t>(32768) / scale;
  const auto extended = extend_transparent_manhattan(canvas.pixels, progress);
  PixelBuffer output = canvas.pixels;
  for (std::int32_t y = 0; y < height; ++y) {
    progress.step(height + y);
    for (std::int32_t x = 0; x < width; ++x) {
      std::int64_t sums[3]{0, 0, 0};
      for (std::int32_t ky = 0; ky < 5; ++ky) {
        const auto sy = std::clamp(y + ky - 2, 0, height - 1);
        for (std::int32_t kx = 0; kx < 5; ++kx) {
          const auto weight = static_cast<std::int64_t>(
              matrix[static_cast<std::size_t>(ky * 5 + kx)]);
          if (weight == 0) {
            continue;
          }
          const auto sx = std::clamp(x + kx - 2, 0, width - 1);
          const auto *source = extended.pixel(sx, sy);
          sums[0] += weight * source[0];
          sums[1] += weight * source[1];
          sums[2] += weight * source[2];
        }
      }
      auto *destination = output.pixel(x, y);
      for (int channel = 0; channel < 3; ++channel) {
        const auto quotient = floor_divide(
            (sums[channel] + scale / 2) * reciprocal, 32768);
        destination[channel] = clamp_byte(quotient + offset);
      }
    }
  }
  return FilterRenderResult{std::move(output), canvas.bounds};
}

std::int64_t round_half_up_divide(std::int64_t numerator,
                                  std::int64_t denominator) noexcept {
  return (2 * numerator + denominator) / (2 * denominator);
}

// Fragment: four copies offset by (+/-4, +/-4), edge-clamped, averaged in
// byte-rounded premultiplied space: premultiply round(c * a / 255), average
// the four with round-half-up, unpremultiply round(p * 255 / a).
FilterRenderResult render_fragment(const FilterRenderResult &canvas,
                                   const RowProgress &progress) {
  const auto width = canvas.bounds.width;
  const auto height = canvas.bounds.height;
  PixelBuffer premultiplied(width, height, PixelFormat::rgba8());
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      const auto *source = canvas.pixels.pixel(x, y);
      auto *destination = premultiplied.pixel(x, y);
      for (int channel = 0; channel < 3; ++channel) {
        destination[channel] = static_cast<std::uint8_t>(round_half_up_divide(
            static_cast<std::int64_t>(source[channel]) * source[3], 255));
      }
      destination[3] = source[3];
    }
  }
  PixelBuffer output(width, height, PixelFormat::rgba8());
  constexpr std::int32_t kShift = 4;
  constexpr std::int32_t kOffsets[4][2]{
      {kShift, kShift}, {kShift, -kShift}, {-kShift, kShift}, {-kShift, -kShift}};
  for (std::int32_t y = 0; y < height; ++y) {
    progress.step(y);
    for (std::int32_t x = 0; x < width; ++x) {
      std::int64_t sums[4]{0, 0, 0, 0};
      for (const auto &offset : kOffsets) {
        const auto *source = premultiplied.pixel(
            std::clamp(x - offset[1], 0, width - 1),
            std::clamp(y - offset[0], 0, height - 1));
        for (int channel = 0; channel < 4; ++channel) {
          sums[channel] += source[channel];
        }
      }
      auto *destination = output.pixel(x, y);
      const auto alpha = round_half_up_divide(sums[3], 4);
      destination[3] = static_cast<std::uint8_t>(alpha);
      for (int channel = 0; channel < 3; ++channel) {
        const auto average = round_half_up_divide(sums[channel], 4);
        destination[channel] =
            alpha == 0 ? std::uint8_t{0}
                       : clamp_byte(round_half_up_divide(average * 255, alpha));
      }
    }
  }
  return FilterRenderResult{std::move(output), canvas.bounds};
}

}  // namespace

FilterRenderResult render_table_smart_filter(const FilterRenderResult &canvas,
                                             const SmartFilterEntry &entry,
                                             const FilterProgress *progress) {
  const auto *spec = smart_filter_table_spec(entry.kind);
  const auto *values = std::get_if<TableSmartFilter>(&entry.parameters);
  if (spec == nullptr || values == nullptr ||
      !table_smart_filter_valid(*spec, *values) ||
      canvas.pixels.format() != PixelFormat::rgba8() ||
      canvas.pixels.width() != canvas.bounds.width ||
      canvas.pixels.height() != canvas.bounds.height) {
    throw std::invalid_argument(
        PATCHY_TRANSLATE_NOOP("QObject", "Unsupported Smart Filter entry"));
  }
  if (canvas.pixels.empty()) {
    return canvas;
  }
  const RowProgress rows(progress,
                         static_cast<std::int64_t>(canvas.bounds.height) * 3);
  FilterRenderResult result;
  switch (entry.kind) {
    case SmartFilterKind::Offset:
      result = render_offset(canvas, *values, rows);
      break;
    case SmartFilterKind::Maximum:
      result = render_morphology(canvas, *values, true, rows);
      break;
    case SmartFilterKind::Minimum:
      result = render_morphology(canvas, *values, false, rows);
      break;
    case SmartFilterKind::Custom:
      result = render_custom(canvas, *values, rows);
      break;
    case SmartFilterKind::Fragment:
      result = render_fragment(canvas, rows);
      break;
    default:
      throw std::invalid_argument(
          PATCHY_TRANSLATE_NOOP("QObject", "Unsupported Smart Filter entry"));
  }
  rows.step(static_cast<std::int64_t>(canvas.bounds.height) * 3);
  return result;
}

}  // namespace patchy
