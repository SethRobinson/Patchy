#include "filters/filter_kernels.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace patchy {
namespace {

constexpr double kKernelPi = 3.14159265358979323846;
// Box Blur radii through the historical 1..12 range keep the direct
// double-precision path byte for byte (pins and the Box Blur Smart Filter
// parity depend on it); larger radii switch to the exact integer running-sum
// path. The weighted tent (Glowing Edges smoothing) stays on the direct path;
// its radius never exceeds 12.
constexpr int kDirectTentBlurMaximumRadius = 12;
// Photoshop's Box Blur range (1..2000 px).
constexpr int kBoxBlurMaximumRadius = 2000;

std::uint8_t kernel_clamp_byte(double value) {
  return static_cast<std::uint8_t>(std::clamp(std::lround(value), 0L, 255L));
}

// Large-radius box average with the same edge-clamped, alpha-weighted
// sampling as the direct path, in O(1) work per pixel per axis whatever the
// radius. Terms are raw bytes (color * alpha, alpha), so every sum is an
// exact int64 (at most 4001^2 * 255 * 255) and the result is deterministic
// across toolchains.
void box_blur_running(PixelBuffer &pixels, const PixelBuffer &original, int radius, const FilterProgress *progress) {
  const auto width = original.width();
  const auto height = original.height();
  const auto channels = original.format().channels;
  const auto color_channels = std::min<std::uint16_t>(channels, 3);
  const auto has_alpha = channels >= 4;
  const auto pixel_bytes = bytes_per_pixel(original.format());
  const auto row_stride = static_cast<std::size_t>(width) * 4U;
  const auto r = static_cast<std::int64_t>(radius);
  const auto taps = static_cast<double>(2 * r + 1);
  const auto total_weight = taps * taps;

  const auto term = [&](const std::uint8_t *row, std::int64_t x, std::array<std::int64_t, 4> &out) {
    const auto clamped = std::clamp<std::int64_t>(x, 0, width - 1);
    const auto *px = row + static_cast<std::size_t>(clamped) * pixel_bytes;
    const auto alpha = has_alpha ? static_cast<std::int64_t>(px[3]) : 255;
    for (std::uint16_t channel = 0; channel < color_channels; ++channel) {
      out[channel] = static_cast<std::int64_t>(px[channel]) * alpha;
    }
    out[3] = alpha;
  };
  // Horizontal window sums for one source row, written as width * 4 terms.
  const auto build_h_row = [&](std::int32_t source_y, std::int64_t *out) {
    const auto *row = original.row(source_y).data();
    std::array<std::int64_t, 4> sample{};
    std::array<std::int64_t, 4> sum{};
    for (auto d = -r; d <= r; ++d) {
      term(row, d, sample);
      for (std::size_t c = 0; c < 4; ++c) {
        sum[c] += sample[c];
      }
    }
    std::array<std::int64_t, 4> entering{};
    std::array<std::int64_t, 4> leaving{};
    for (std::int32_t x = 0; x < width; ++x) {
      auto *dst = out + static_cast<std::size_t>(x) * 4U;
      for (std::size_t c = 0; c < 4; ++c) {
        dst[c] = sum[c];
      }
      if (x + 1 >= width) {
        break;
      }
      term(row, x + r + 1, entering);
      term(row, x - r, leaving);
      for (std::size_t c = 0; c < 4; ++c) {
        sum[c] += entering[c] - leaving[c];
      }
    }
  };

  // Rows at or beyond an edge repeat that edge row, so cache both edge rows;
  // every other row is rebuilt on demand into a scratch slot (O(width + r)).
  std::vector<std::int64_t> first_row(row_stride);
  std::vector<std::int64_t> last_row(row_stride);
  build_h_row(0, first_row.data());
  build_h_row(height - 1, last_row.data());
  std::array<std::vector<std::int64_t>, 2> scratch_rows{std::vector<std::int64_t>(row_stride),
                                                        std::vector<std::int64_t>(row_stride)};
  const auto h_row = [&](std::int64_t source_y, std::size_t slot) -> const std::int64_t * {
    if (source_y <= 0) {
      return first_row.data();
    }
    if (source_y >= height - 1) {
      return last_row.data();
    }
    build_h_row(static_cast<std::int32_t>(source_y), scratch_rows[slot].data());
    return scratch_rows[slot].data();
  };

  std::vector<std::int64_t> sum(row_stride, 0);
  // Rows above the top edge all repeat row 0: fold them into one add.
  for (std::size_t i = 0; i < row_stride; ++i) {
    sum[i] += first_row[i] * (r + 1);
  }
  for (auto k = std::int64_t{1}; k <= r; ++k) {
    const auto *row = h_row(k, 0);
    for (std::size_t i = 0; i < row_stride; ++i) {
      sum[i] += row[i];
    }
  }

  for (std::int32_t y = 0; y < height; ++y) {
    report_kernel_progress(progress, y, height, FilterProgressStage::Blurring);
    for (std::int32_t x = 0; x < width; ++x) {
      const auto *accum = sum.data() + static_cast<std::size_t>(x) * 4U;
      auto *dst = pixels.pixel(x, y);
      const auto alpha_sum = accum[3];
      for (std::uint16_t channel = 0; channel < color_channels; ++channel) {
        dst[channel] = kernel_clamp_byte(
            alpha_sum > 0 ? static_cast<double>(accum[channel]) / static_cast<double>(alpha_sum) : 0.0);
      }
      if (has_alpha) {
        dst[3] = kernel_clamp_byte(static_cast<double>(alpha_sum) / total_weight);
      }
    }
    if (y + 1 >= height) {
      break;
    }
    const auto *entering = h_row(y + r + 1, 0);
    const auto *leaving = h_row(y - r, 1);
    for (std::size_t i = 0; i < row_stride; ++i) {
      sum[i] += entering[i] - leaving[i];
    }
  }
  report_kernel_progress(progress, height, height, FilterProgressStage::Blurring);
}

}  // namespace

void report_kernel_progress(const FilterProgress *progress, int completed, int total, FilterProgressStage stage) {
  if (progress == nullptr || !progress->update) {
    return;
  }
  const auto safe_total = std::max(1, total);
  if (!progress->update(std::clamp(completed, 0, safe_total), safe_total, stage)) {
    throw FilterCancelled();
  }
}

std::uint32_t position_noise_hash(std::int32_t x, std::int32_t y, std::uint32_t seed) noexcept {
  auto value = static_cast<std::uint32_t>(x + 16384) * 374761393U;
  value ^= static_cast<std::uint32_t>(y + 8192) * 668265263U;
  value ^= seed * 2246822519U;
  value ^= value >> 13U;
  value *= 1274126177U;
  value ^= value >> 16U;
  return value;
}

int integer_luminance(const std::uint8_t *px) noexcept {
  return (static_cast<int>(px[0]) * 30 + static_cast<int>(px[1]) * 59 + static_cast<int>(px[2]) * 11) / 100;
}

double sampled_luminance(const PixelBuffer &pixels, double x, double y) {
  x = std::clamp(x, 0.0, static_cast<double>(std::max<std::int32_t>(0, pixels.width() - 1)));
  y = std::clamp(y, 0.0, static_cast<double>(std::max<std::int32_t>(0, pixels.height() - 1)));
  const auto x0 = static_cast<std::int32_t>(std::floor(x));
  const auto y0 = static_cast<std::int32_t>(std::floor(y));
  const auto x1 = std::min<std::int32_t>(pixels.width() - 1, x0 + 1);
  const auto y1 = std::min<std::int32_t>(pixels.height() - 1, y0 + 1);
  const auto tx = x - static_cast<double>(x0);
  const auto ty = y - static_cast<double>(y0);
  const auto l00 = static_cast<double>(integer_luminance(pixels.pixel(x0, y0)));
  const auto l10 = static_cast<double>(integer_luminance(pixels.pixel(x1, y0)));
  const auto l01 = static_cast<double>(integer_luminance(pixels.pixel(x0, y1)));
  const auto l11 = static_cast<double>(integer_luminance(pixels.pixel(x1, y1)));
  const auto top = l00 * (1.0 - tx) + l10 * tx;
  const auto bottom = l01 * (1.0 - tx) + l11 * tx;
  return top * (1.0 - ty) + bottom * ty;
}

void accumulate_premultiplied_pixel(PremultipliedAccum &accum, const PixelBuffer &original, const std::uint8_t *px,
                                    double weight) {
  if (weight <= 0.0) {
    return;
  }
  const auto alpha = original.format().channels >= 4 ? static_cast<double>(px[3]) / 255.0 : 1.0;
  accum.weight += weight;
  accum.alpha += alpha * weight;
  for (std::uint16_t channel = 0; channel < std::min<std::uint16_t>(original.format().channels, 3); ++channel) {
    accum.premultiplied_color[static_cast<std::size_t>(channel)] += static_cast<double>(px[channel]) * alpha * weight;
  }
}

void accumulate_bilinear_sample(PremultipliedAccum &accum, const PixelBuffer &original, double x, double y,
                                double weight) {
  x = std::clamp(x, 0.0, static_cast<double>(std::max<std::int32_t>(0, original.width() - 1)));
  y = std::clamp(y, 0.0, static_cast<double>(std::max<std::int32_t>(0, original.height() - 1)));
  const auto x0 = static_cast<std::int32_t>(std::floor(x));
  const auto y0 = static_cast<std::int32_t>(std::floor(y));
  const auto x1 = std::min<std::int32_t>(original.width() - 1, x0 + 1);
  const auto y1 = std::min<std::int32_t>(original.height() - 1, y0 + 1);
  const auto tx = x - static_cast<double>(x0);
  const auto ty = y - static_cast<double>(y0);
  accumulate_premultiplied_pixel(accum, original, original.pixel(x0, y0), weight * (1.0 - tx) * (1.0 - ty));
  accumulate_premultiplied_pixel(accum, original, original.pixel(x1, y0), weight * tx * (1.0 - ty));
  accumulate_premultiplied_pixel(accum, original, original.pixel(x0, y1), weight * (1.0 - tx) * ty);
  accumulate_premultiplied_pixel(accum, original, original.pixel(x1, y1), weight * tx * ty);
}

void write_accumulated_pixel(PixelBuffer &pixels, std::int32_t x, std::int32_t y, const PremultipliedAccum &accum) {
  auto *dst = pixels.pixel(x, y);
  const auto channels = pixels.format().channels;
  const auto normalized_alpha = channels >= 4 && accum.weight > 0.0 ? accum.alpha / accum.weight : 1.0;
  for (std::uint16_t channel = 0; channel < std::min<std::uint16_t>(channels, 3); ++channel) {
    const auto value =
        accum.alpha > 0.000001 ? accum.premultiplied_color[static_cast<std::size_t>(channel)] / accum.alpha : 0.0;
    dst[channel] = kernel_clamp_byte(value);
  }
  if (channels >= 4) {
    dst[3] = kernel_clamp_byte(normalized_alpha * 255.0);
  }
}

void box_blur_kernel(PixelBuffer &pixels, const PixelBuffer &original, int radius, bool weighted,
                     const FilterProgress *progress) {
  radius = std::clamp(radius, 1, weighted ? kDirectTentBlurMaximumRadius : kBoxBlurMaximumRadius);
  if (original.width() == 0 || original.height() == 0) {
    return;
  }
  if (radius > kDirectTentBlurMaximumRadius) {
    box_blur_running(pixels, original, radius, progress);
    return;
  }
  const auto width = original.width();
  const auto height = original.height();
  if (width == 0 || height == 0) {
    return;
  }
  const auto channels = original.format().channels;
  const auto color_channels = std::min<std::uint16_t>(channels, 3);
  const auto taps = 2 * radius + 1;
  double axis_weight_sum = 0.0;
  std::vector<double> axis_weights(static_cast<std::size_t>(taps));
  for (int offset = -radius; offset <= radius; ++offset) {
    const auto weight = weighted ? static_cast<double>(radius + 1 - std::abs(offset)) : 1.0;
    axis_weights[static_cast<std::size_t>(offset + radius)] = weight;
    axis_weight_sum += weight;
  }
  const auto total_weight = axis_weight_sum * axis_weight_sum;

  const auto row_stride = static_cast<std::size_t>(width) * 4U;
  std::vector<double> h_rows(row_stride * static_cast<std::size_t>(taps), 0.0);
  int h_rows_built_through = -1;
  const auto build_h_row = [&](std::int32_t source_y, double *out) {
    std::fill(out, out + row_stride, 0.0);
    for (int dx = -radius; dx <= radius; ++dx) {
      const auto weight = axis_weights[static_cast<std::size_t>(dx + radius)];
      for (std::int32_t x = 0; x < width; ++x) {
        const auto sx = std::clamp<std::int32_t>(x + dx, 0, width - 1);
        const auto *px = original.pixel(sx, source_y);
        const auto alpha = channels >= 4 ? static_cast<double>(px[3]) / 255.0 : 1.0;
        auto *accum = out + static_cast<std::size_t>(x) * 4U;
        const auto alpha_weight = alpha * weight;
        for (std::uint16_t channel = 0; channel < color_channels; ++channel) {
          accum[channel] += static_cast<double>(px[channel]) * alpha_weight;
        }
        accum[3] += alpha_weight;
      }
    }
  };
  const auto h_row_for = [&](std::int32_t source_y) -> const double * {
    return h_rows.data() + static_cast<std::size_t>(source_y % taps) * row_stride;
  };

  std::vector<double> v_accum(row_stride);
  for (std::int32_t y = 0; y < height; ++y) {
    report_kernel_progress(progress, y, height, FilterProgressStage::Blurring);
    const auto needed_through = std::min<std::int32_t>(height - 1, y + radius);
    while (h_rows_built_through < needed_through) {
      ++h_rows_built_through;
      build_h_row(h_rows_built_through,
                  h_rows.data() + static_cast<std::size_t>(h_rows_built_through % taps) * row_stride);
    }
    std::fill(v_accum.begin(), v_accum.end(), 0.0);
    for (int dy = -radius; dy <= radius; ++dy) {
      const auto sy = std::clamp<std::int32_t>(y + dy, 0, height - 1);
      const auto weight = axis_weights[static_cast<std::size_t>(dy + radius)];
      const auto *h_row = h_row_for(sy);
      for (std::size_t i = 0; i < row_stride; ++i) {
        v_accum[i] += h_row[i] * weight;
      }
    }
    for (std::int32_t x = 0; x < width; ++x) {
      const auto *accum = v_accum.data() + static_cast<std::size_t>(x) * 4U;
      auto *dst = pixels.pixel(x, y);
      const auto alpha_sum = accum[3];
      for (std::uint16_t channel = 0; channel < color_channels; ++channel) {
        const auto value = alpha_sum > 0.000001 ? accum[channel] / alpha_sum : 0.0;
        dst[channel] = kernel_clamp_byte(value);
      }
      if (channels >= 4) {
        dst[3] = kernel_clamp_byte(alpha_sum / total_weight * 255.0);
      }
    }
  }
  report_kernel_progress(progress, height, height, FilterProgressStage::Blurring);
}

void radial_blur_kernel(PixelBuffer &pixels, const PixelBuffer &original, int amount, int samples, double center_x,
                        double center_y, const FilterProgress *progress) {
  amount = std::clamp(amount, 0, 100);
  samples = std::clamp(samples, 4, 32);
  const auto sweep = static_cast<double>(amount) * 3.6 * kKernelPi / 180.0;
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    report_kernel_progress(progress, y, pixels.height(), FilterProgressStage::Blurring);
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto dx = static_cast<double>(x) - center_x;
      const auto dy = static_cast<double>(y) - center_y;
      PremultipliedAccum accum;
      for (int sample = 0; sample < samples; ++sample) {
        const auto t =
            samples <= 1 ? 0.0 : static_cast<double>(sample) / static_cast<double>(samples - 1) - 0.5;
        const auto angle = sweep * t;
        const auto source_x = center_x + dx * std::cos(angle) - dy * std::sin(angle);
        const auto source_y = center_y + dx * std::sin(angle) + dy * std::cos(angle);
        accumulate_bilinear_sample(accum, original, source_x, source_y);
      }
      write_accumulated_pixel(pixels, x, y, accum);
    }
  }
  report_kernel_progress(progress, pixels.height(), pixels.height(), FilterProgressStage::Blurring);
}

void emboss_kernel(PixelBuffer &pixels, const PixelBuffer &original, int angle_degrees, double distance,
                   int amount_percent, const FilterProgress *progress) {
  const auto angle = static_cast<double>(angle_degrees) * kKernelPi / 180.0;
  const auto offset_x = std::cos(angle) * distance;
  const auto offset_y = -std::sin(angle) * distance;
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    report_kernel_progress(progress, y, pixels.height(), FilterProgressStage::Embossing);
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto highlight =
          sampled_luminance(original, static_cast<double>(x) - offset_x, static_cast<double>(y) - offset_y);
      const auto shadow =
          sampled_luminance(original, static_cast<double>(x) + offset_x, static_cast<double>(y) + offset_y);
      const auto value =
          kernel_clamp_byte(128.0 + (highlight - shadow) * static_cast<double>(amount_percent) / 100.0);
      auto *px = pixels.pixel(x, y);
      px[0] = value;
      px[1] = value;
      px[2] = value;
    }
  }
  report_kernel_progress(progress, pixels.height(), pixels.height(), FilterProgressStage::Embossing);
}

void mosaic_kernel(PixelBuffer &pixels, const PixelBuffer &original, std::int32_t cell_size,
                   const FilterProgress *progress) {
  const auto width = original.width();
  const auto height = original.height();
  for (std::int32_t block_y = 0; block_y < height; block_y += cell_size) {
    report_kernel_progress(progress, block_y, height, FilterProgressStage::Pixelating);
    for (std::int32_t block_x = 0; block_x < width; block_x += cell_size) {
      const auto block_width = std::min(cell_size, width - block_x);
      const auto block_height = std::min(cell_size, height - block_y);
      PremultipliedAccum accum;
      for (std::int32_t y = block_y; y < block_y + block_height; ++y) {
        for (std::int32_t x = block_x; x < block_x + block_width; ++x) {
          accumulate_premultiplied_pixel(accum, original, original.pixel(x, y), 1.0);
        }
      }
      for (std::int32_t y = block_y; y < block_y + block_height; ++y) {
        for (std::int32_t x = block_x; x < block_x + block_width; ++x) {
          write_accumulated_pixel(pixels, x, y, accum);
        }
      }
    }
  }
  report_kernel_progress(progress, height, height, FilterProgressStage::Pixelating);
}

}  // namespace patchy
