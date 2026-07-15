#include "filters/filter_engine.hpp"

#include "filters/rgba_filter_staging.hpp"
#include "filters/smart_filter_renderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace patchy {

namespace {

std::uint8_t filter_clamp_byte(int value) {
  return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
}

std::uint8_t filter_clamp_byte(double value) {
  return static_cast<std::uint8_t>(std::clamp(std::lround(value), 0L, 255L));
}

int filter_luminance(const std::uint8_t *px) {
  return (static_cast<int>(px[0]) * 30 + static_cast<int>(px[1]) * 59 +
          static_cast<int>(px[2]) * 11) /
         100;
}

constexpr double kFilterPi = 3.14159265358979323846;

int filter_value(const FilterInvocation &invocation, std::string_view key,
                 int fallback) {
  const auto found = invocation.parameters.find(key);
  if (found == invocation.parameters.end()) {
    return fallback;
  }
  const auto *integer = std::get_if<std::int64_t>(&found->second);
  if (integer == nullptr || *integer < std::numeric_limits<int>::min() ||
      *integer > std::numeric_limits<int>::max()) {
    return fallback;
  }
  return static_cast<int>(*integer);
}

double filter_number(const FilterInvocation &invocation, std::string_view key,
                     double fallback) {
  const auto found = invocation.parameters.find(key);
  if (found == invocation.parameters.end()) {
    return fallback;
  }
  if (const auto *real = std::get_if<double>(&found->second);
      real != nullptr && std::isfinite(*real)) {
    return *real;
  }
  if (const auto *integer = std::get_if<std::int64_t>(&found->second);
      integer != nullptr) {
    return static_cast<double>(*integer);
  }
  return fallback;
}

double filter_center_coordinate(std::int32_t extent, double percent) {
  return static_cast<double>(std::max<std::int32_t>(0, extent - 1)) *
         std::clamp(percent, 0.0, 100.0) / 100.0;
}

void report_filter_progress(
    const FilterProgress *progress, int completed, int total,
    FilterProgressStage stage = FilterProgressStage::Filtering) {
  if (progress == nullptr || !progress->update) {
    return;
  }
  if (!progress->update(std::clamp(completed, 0, std::max(1, total)),
                        std::max(1, total), stage)) {
    throw FilterCancelled();
  }
}

void report_filter_row_progress(const FilterProgress *progress, std::int32_t y,
                                std::int32_t height, int progress_offset = 0,
                                int progress_total = 0) {
  report_filter_progress(progress, progress_offset + y,
                         progress_total > 0 ? progress_total : height,
                         FilterProgressStage::Filtering);
}

void finish_filter_row_progress(const FilterProgress *progress,
                                std::int32_t height, int progress_offset = 0,
                                int progress_total = 0) {
  report_filter_progress(progress, progress_offset + height,
                         progress_total > 0 ? progress_total : height,
                         FilterProgressStage::Filtering);
}

void blend_filter_with_original(PixelBuffer &pixels,
                                const PixelBuffer &original, int amount_percent,
                                const FilterProgress *progress = nullptr,
                                int progress_offset = 0,
                                int progress_total = 0) {
  amount_percent = std::clamp(amount_percent, 0, 100);
  if (amount_percent >= 100) {
    finish_filter_row_progress(progress, pixels.height(), progress_offset,
                               progress_total);
    return;
  }
  if (amount_percent <= 0 || pixels.format() != original.format() ||
      pixels.width() != original.width() ||
      pixels.height() != original.height()) {
    pixels = original;
    finish_filter_row_progress(progress, pixels.height(), progress_offset,
                               progress_total);
    return;
  }

  const auto channels = std::min<std::uint16_t>(pixels.format().channels, 3);
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    report_filter_row_progress(progress, y, pixels.height(), progress_offset,
                               progress_total);
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto *dst = pixels.pixel(x, y);
      const auto *src = original.pixel(x, y);
      for (std::uint16_t channel = 0; channel < channels; ++channel) {
        dst[channel] = filter_clamp_byte(
            (static_cast<int>(src[channel]) * (100 - amount_percent) +
             static_cast<int>(dst[channel]) * amount_percent) /
            100);
      }
      if (pixels.format().channels >= 4) {
        dst[3] = src[3];
      }
    }
  }
  finish_filter_row_progress(progress, pixels.height(), progress_offset,
                             progress_total);
}

FilterProgress filter_progress_phase(const FilterProgress *progress,
                                     int phase_index, int phase_count) {
  if (progress == nullptr || !progress->update) {
    return {};
  }
  return FilterProgress{[progress, phase_index,
                         phase_count](int completed, int total,
                                      FilterProgressStage stage) {
    constexpr int kPhaseScale = 1000;
    const auto safe_phase_count = std::max(1, phase_count);
    const auto safe_total = std::max(1, total);
    const auto clamped_completed = std::clamp(completed, 0, safe_total);
    const auto phase_completed = (clamped_completed * kPhaseScale) / safe_total;
    return progress->update(std::clamp(phase_index, 0, safe_phase_count - 1) *
                                    kPhaseScale +
                                phase_completed,
                            safe_phase_count * kPhaseScale, stage);
  }};
}

std::uint8_t filter_blend_byte(std::uint8_t base, std::uint8_t overlay,
                               int amount_percent) {
  amount_percent = std::clamp(amount_percent, 0, 100);
  return filter_clamp_byte((static_cast<int>(base) * (100 - amount_percent) +
                            static_cast<int>(overlay) * amount_percent + 50) /
                           100);
}

void adjust_contrast_filter_pixels(PixelBuffer &pixels, double factor,
                                   int midpoint,
                                   const FilterProgress *progress) {
  const auto channels = std::min<std::uint16_t>(pixels.format().channels, 3);
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    report_filter_row_progress(progress, y, pixels.height());
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto *px = pixels.pixel(x, y);
      for (std::uint16_t channel = 0; channel < channels; ++channel) {
        px[channel] = filter_clamp_byte(static_cast<int>(
            (static_cast<double>(px[channel]) - static_cast<double>(midpoint)) *
                factor +
            static_cast<double>(midpoint)));
      }
    }
  }
  finish_filter_row_progress(progress, pixels.height());
}

void adjust_saturation_filter_pixels(PixelBuffer &pixels, double factor,
                                     const FilterProgress *progress) {
  if (pixels.format().channels < 3) {
    return;
  }
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    report_filter_row_progress(progress, y, pixels.height());
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto *px = pixels.pixel(x, y);
      const auto luminance = static_cast<double>(filter_luminance(px));
      for (std::uint16_t channel = 0; channel < 3; ++channel) {
        px[channel] = filter_clamp_byte(static_cast<int>(
            luminance +
            (static_cast<double>(px[channel]) - luminance) * factor));
      }
    }
  }
  finish_filter_row_progress(progress, pixels.height());
}

void tint_filter_pixels(PixelBuffer &pixels, int red, int green, int blue,
                        const FilterProgress *progress) {
  if (pixels.format().channels < 3) {
    return;
  }
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    report_filter_row_progress(progress, y, pixels.height());
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto *px = pixels.pixel(x, y);
      px[0] = filter_clamp_byte(static_cast<int>(px[0]) + red);
      px[1] = filter_clamp_byte(static_cast<int>(px[1]) + green);
      px[2] = filter_clamp_byte(static_cast<int>(px[2]) + blue);
    }
  }
  finish_filter_row_progress(progress, pixels.height());
}

void blend_overlay_filter_pixels(PixelBuffer &pixels,
                                 const PixelBuffer &overlay, int amount_percent,
                                 const FilterProgress *progress) {
  if (pixels.format() != overlay.format() ||
      pixels.width() != overlay.width() ||
      pixels.height() != overlay.height()) {
    return;
  }
  const auto channels = std::min<std::uint16_t>(pixels.format().channels, 3);
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    report_filter_row_progress(progress, y, pixels.height());
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto *px = pixels.pixel(x, y);
      const auto *over = overlay.pixel(x, y);
      for (std::uint16_t channel = 0; channel < channels; ++channel) {
        px[channel] =
            filter_blend_byte(px[channel], over[channel], amount_percent);
      }
    }
  }
  finish_filter_row_progress(progress, pixels.height());
}

struct FilterPixelAccum {
  std::array<double, 3> premultiplied_color{0.0, 0.0, 0.0};
  double alpha{0.0};
  double weight{0.0};
};

void filter_accumulate_pixel(FilterPixelAccum &accum,
                             const PixelBuffer &original,
                             const std::uint8_t *px, double weight) {
  if (weight <= 0.0) {
    return;
  }
  const auto alpha = original.format().channels >= 4
                         ? static_cast<double>(px[3]) / 255.0
                         : 1.0;
  accum.weight += weight;
  accum.alpha += alpha * weight;
  for (std::uint16_t channel = 0;
       channel < std::min<std::uint16_t>(original.format().channels, 3);
       ++channel) {
    accum.premultiplied_color[static_cast<std::size_t>(channel)] +=
        static_cast<double>(px[channel]) * alpha * weight;
  }
}

void filter_accumulate_sample(FilterPixelAccum &accum,
                              const PixelBuffer &original, double x, double y,
                              double weight = 1.0) {
  x = std::clamp(
      x, 0.0,
      static_cast<double>(std::max<std::int32_t>(0, original.width() - 1)));
  y = std::clamp(
      y, 0.0,
      static_cast<double>(std::max<std::int32_t>(0, original.height() - 1)));
  const auto x0 = static_cast<std::int32_t>(std::floor(x));
  const auto y0 = static_cast<std::int32_t>(std::floor(y));
  const auto x1 = std::min<std::int32_t>(original.width() - 1, x0 + 1);
  const auto y1 = std::min<std::int32_t>(original.height() - 1, y0 + 1);
  const auto tx = x - static_cast<double>(x0);
  const auto ty = y - static_cast<double>(y0);
  filter_accumulate_pixel(accum, original, original.pixel(x0, y0),
                          weight * (1.0 - tx) * (1.0 - ty));
  filter_accumulate_pixel(accum, original, original.pixel(x1, y0),
                          weight * tx * (1.0 - ty));
  filter_accumulate_pixel(accum, original, original.pixel(x0, y1),
                          weight * (1.0 - tx) * ty);
  filter_accumulate_pixel(accum, original, original.pixel(x1, y1),
                          weight * tx * ty);
}

void filter_write_accumulated_pixel(PixelBuffer &pixels, std::int32_t x,
                                    std::int32_t y,
                                    const FilterPixelAccum &accum) {
  auto *dst = pixels.pixel(x, y);
  const auto channels = pixels.format().channels;
  const auto normalized_alpha =
      channels >= 4 && accum.weight > 0.0 ? accum.alpha / accum.weight : 1.0;
  for (std::uint16_t channel = 0;
       channel < std::min<std::uint16_t>(channels, 3); ++channel) {
    const auto value =
        accum.alpha > 0.000001
            ? accum.premultiplied_color[static_cast<std::size_t>(channel)] /
                  accum.alpha
            : 0.0;
    dst[channel] = filter_clamp_byte(value);
  }
  if (channels >= 4) {
    dst[3] = filter_clamp_byte(normalized_alpha * 255.0);
  }
}

void filter_copy_sampled_pixel(PixelBuffer &pixels, const PixelBuffer &original,
                               std::int32_t x, std::int32_t y, double source_x,
                               double source_y) {
  FilterPixelAccum accum;
  filter_accumulate_sample(accum, original, source_x, source_y);
  filter_write_accumulated_pixel(pixels, x, y, accum);
}

void apply_separable_tent_blur(PixelBuffer &pixels, const PixelBuffer &original,
                               int radius, bool weighted,
                               const FilterProgress *progress) {
  radius = std::clamp(radius, 1, 32);
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
    const auto weight =
        weighted ? static_cast<double>(radius + 1 - std::abs(offset)) : 1.0;
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
        const auto alpha =
            channels >= 4 ? static_cast<double>(px[3]) / 255.0 : 1.0;
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
    return h_rows.data() +
           static_cast<std::size_t>(source_y % taps) * row_stride;
  };

  std::vector<double> v_accum(row_stride);
  for (std::int32_t y = 0; y < height; ++y) {
    report_filter_progress(progress, y, height, FilterProgressStage::Blurring);
    const auto needed_through = std::min<std::int32_t>(height - 1, y + radius);
    while (h_rows_built_through < needed_through) {
      ++h_rows_built_through;
      build_h_row(h_rows_built_through,
                  h_rows.data() +
                      static_cast<std::size_t>(h_rows_built_through % taps) *
                          row_stride);
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
        const auto value =
            alpha_sum > 0.000001 ? accum[channel] / alpha_sum : 0.0;
        dst[channel] = filter_clamp_byte(value);
      }
      if (channels >= 4) {
        dst[3] = filter_clamp_byte(alpha_sum / total_weight * 255.0);
      }
    }
  }
  report_filter_progress(progress, height, height,
                         FilterProgressStage::Blurring);
}

void apply_builtin_gaussian_blur_filter_pixels(PixelBuffer &pixels,
                                               const FilterProgress *progress) {
  if (pixels.format().channels < 3 || pixels.width() == 0 ||
      pixels.height() == 0) {
    return;
  }
  constexpr std::array<int, 5> kWeights = {1, 4, 6, 4, 1};
  const auto original = pixels;
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    report_filter_progress(progress, y, pixels.height(),
                           FilterProgressStage::Blurring);
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      FilterPixelAccum accum;
      for (int ky = -2; ky <= 2; ++ky) {
        const auto sy =
            std::clamp<std::int32_t>(y + ky, 0, pixels.height() - 1);
        for (int kx = -2; kx <= 2; ++kx) {
          const auto sx =
              std::clamp<std::int32_t>(x + kx, 0, pixels.width() - 1);
          filter_accumulate_pixel(
              accum, original, original.pixel(sx, sy),
              static_cast<double>(kWeights[static_cast<std::size_t>(kx + 2)] *
                                  kWeights[static_cast<std::size_t>(ky + 2)]));
        }
      }
      filter_write_accumulated_pixel(pixels, x, y, accum);
    }
  }
  report_filter_progress(progress, pixels.height(), pixels.height(),
                         FilterProgressStage::Blurring);
}

void apply_high_pass_filter_pixels(PixelBuffer &pixels, double radius,
                                   const FilterProgress *progress) {
  if (pixels.format().channels < 3 || pixels.empty()) {
    report_filter_progress(progress, 1, 1,
                           FilterProgressStage::Sharpening);
    return;
  }
  stage_rgba_and_render(pixels, [&](const PixelBuffer &rgba) {
    return render_photoshop_high_pass(
        rgba, Rect::from_size(rgba.width(), rgba.height()), radius, progress);
  });
}

void apply_median_filter_pixels(PixelBuffer &pixels, double radius,
                                const FilterProgress *progress) {
  if (pixels.format().channels < 3 || pixels.empty()) {
    report_filter_progress(progress, 1, 1,
                           FilterProgressStage::Filtering);
    return;
  }
  stage_rgba_and_render(pixels, [&](const PixelBuffer &rgba) {
    return render_photoshop_median(
        rgba, Rect::from_size(rgba.width(), rgba.height()), radius, progress);
  });
}

void apply_dust_and_scratches_filter_pixels(PixelBuffer &pixels,
                                            std::int32_t radius,
                                            std::int32_t threshold,
                                            const FilterProgress *progress) {
  if (pixels.format().channels < 3 || pixels.empty()) {
    report_filter_progress(progress, 1, 1,
                           FilterProgressStage::Filtering);
    return;
  }
  stage_rgba_and_render(pixels, [&](const PixelBuffer &rgba) {
    return render_photoshop_dust_and_scratches(
        rgba, Rect::from_size(rgba.width(), rgba.height()), radius, threshold,
        progress);
  });
}

void apply_surface_blur_filter_pixels(PixelBuffer &pixels, double radius,
                                      std::int32_t threshold,
                                      const FilterProgress *progress) {
  if (pixels.format().channels < 3 || pixels.empty()) {
    report_filter_progress(progress, 1, 1,
                           FilterProgressStage::Filtering);
    return;
  }
  stage_rgba_and_render(pixels, [&](const PixelBuffer &rgba) {
    return render_photoshop_surface_blur(
        rgba, Rect::from_size(rgba.width(), rgba.height()), radius, threshold,
        progress);
  });
}

void apply_plastic_wrap_filter_pixels(PixelBuffer &pixels,
                                      std::int32_t highlight_strength,
                                      std::int32_t detail,
                                      std::int32_t smoothness,
                                      const FilterProgress *progress) {
  if (pixels.format().channels < 3 || pixels.empty()) {
    report_filter_progress(progress, 1, 1,
                           FilterProgressStage::Filtering);
    return;
  }
  stage_rgba_and_render(pixels, [&](const PixelBuffer &rgba) {
    return render_plastic_wrap(
        rgba, Rect::from_size(rgba.width(), rgba.height()),
        highlight_strength, detail, smoothness, progress);
  });
}

constexpr std::uint64_t kTiltWeightScale = 65536U;
constexpr std::uint64_t kTiltMixScale = 65535U;
constexpr std::uint64_t kTiltIntermediateScale = 256U;
constexpr std::int64_t kTiltGeometryScale = 65536;
constexpr std::int64_t kTiltNormalScale = 16777216;

struct TiltPremultipliedPixel {
  // The horizontal pass retains eight fractional bits. RGB start as
  // straight-byte * alpha-byte; alpha starts as 0..255. The extra precision
  // prevents low-alpha color drift before the vertical pass.
  std::array<std::uint32_t, 4> component{};
};

[[nodiscard]] std::uint64_t tilt_divide_rounded(std::uint64_t numerator,
                                                std::uint64_t denominator) {
  return denominator == 0U ? 0U : (numerator + denominator / 2U) / denominator;
}

[[nodiscard]] std::uint64_t tilt_prefix_range(
    const std::vector<std::uint64_t> &prefix, int first, int last) {
  if (first > last) {
    return 0U;
  }
  return prefix[static_cast<std::size_t>(last) + 1U] -
         prefix[static_cast<std::size_t>(first)];
}

[[nodiscard]] std::uint64_t tilt_missing_weight(
    int first_distance, int radius, std::uint64_t base_weight) {
  if (first_distance > radius) {
    return 0U;
  }
  const auto count = static_cast<std::uint64_t>(radius - first_distance + 1);
  const auto distance_sum =
      (static_cast<std::uint64_t>(first_distance + radius) * count) / 2U;
  return count * base_weight - kTiltWeightScale * distance_sum;
}

[[nodiscard]] std::uint64_t tilt_weighted_axis_sum(
    const std::vector<std::uint64_t> &prefix,
    const std::vector<std::uint64_t> &moment_prefix, int index, int count,
    int radius, std::uint64_t base_weight, bool repeat_edge,
    std::uint32_t first_value, std::uint32_t last_value) {
  const auto left_first = std::max(0, index - radius);
  const auto left_sum = tilt_prefix_range(prefix, left_first, index);
  const auto left_moment =
      tilt_prefix_range(moment_prefix, left_first, index);
  const auto left_distance =
      static_cast<std::uint64_t>(index) * left_sum - left_moment;
  auto weighted = base_weight * left_sum -
                  kTiltWeightScale * left_distance;

  const auto right_first = index + 1;
  const auto right_last = std::min(count - 1, index + radius);
  if (right_first <= right_last) {
    const auto right_sum =
        tilt_prefix_range(prefix, right_first, right_last);
    const auto right_moment =
        tilt_prefix_range(moment_prefix, right_first, right_last);
    const auto right_distance =
        right_moment - static_cast<std::uint64_t>(index) * right_sum;
    weighted += base_weight * right_sum -
                kTiltWeightScale * right_distance;
  }

  if (repeat_edge) {
    weighted += tilt_missing_weight(index + 1, radius, base_weight) *
                static_cast<std::uint64_t>(first_value);
    weighted += tilt_missing_weight(count - index, radius, base_weight) *
                static_cast<std::uint64_t>(last_value);
  }
  return weighted;
}

[[nodiscard]] PixelBuffer tilt_tent_blur_level(
    const PixelBuffer &source, std::uint64_t radius_fixed,
    const FilterProgress *progress) {
  const auto width = source.width();
  const auto height = source.height();
  PixelBuffer output(width, height, source.format());
  output.clear(0);
  if (width <= 0 || height <= 0 || radius_fixed == 0U) {
    output = source;
    report_filter_progress(progress, 1, 1, FilterProgressStage::Blurring);
    return output;
  }

  const auto radius = static_cast<int>(
      (radius_fixed + kTiltWeightScale - 1U) / kTiltWeightScale);
  const auto base_weight = radius_fixed + kTiltWeightScale;
  const auto positive_weight_sum =
      static_cast<std::uint64_t>(radius) * base_weight -
      kTiltWeightScale * static_cast<std::uint64_t>(radius) *
          static_cast<std::uint64_t>(radius + 1) / 2U;
  const auto kernel_weight = base_weight + positive_weight_sum * 2U;
  if (kernel_weight == 0U) {
    throw std::overflow_error("Tilt-Shift Blur kernel overflow");
  }

  const auto pixel_count = static_cast<std::uint64_t>(width) *
                           static_cast<std::uint64_t>(height);
  if (pixel_count > std::numeric_limits<std::size_t>::max() /
                        sizeof(TiltPremultipliedPixel)) {
    throw std::overflow_error("Tilt-Shift Blur working buffer overflow");
  }
  std::vector<TiltPremultipliedPixel> horizontal(
      static_cast<std::size_t>(pixel_count));
  std::array<std::vector<std::uint64_t>, 4> prefixes;
  std::array<std::vector<std::uint64_t>, 4> moment_prefixes;
  const auto prefix_size =
      static_cast<std::size_t>(std::max(width, height)) + 1U;
  for (auto &prefix : prefixes) {
    prefix.resize(prefix_size);
  }
  for (auto &prefix : moment_prefixes) {
    prefix.resize(prefix_size);
  }
  const auto has_alpha = source.format().channels >= 4;
  const auto repeat_edge = !has_alpha;
  const auto total_lines = static_cast<std::uint64_t>(height) +
                           static_cast<std::uint64_t>(width);
  std::uint64_t completed_lines = 0U;

  const auto source_components = [&](const std::uint8_t *pixel) {
    const auto alpha = static_cast<std::uint32_t>(has_alpha ? pixel[3] : 255U);
    return std::array<std::uint32_t, 4>{
        static_cast<std::uint32_t>(pixel[0]) * alpha,
        static_cast<std::uint32_t>(pixel[1]) * alpha,
        static_cast<std::uint32_t>(pixel[2]) * alpha, alpha};
  };

  for (std::int32_t y = 0; y < height; ++y) {
    report_filter_progress(
        progress,
        static_cast<int>(std::min<std::uint64_t>(
            completed_lines++, std::numeric_limits<int>::max())),
        static_cast<int>(std::min<std::uint64_t>(
            total_lines, std::numeric_limits<int>::max())),
        FilterProgressStage::Blurring);
    for (std::size_t component = 0; component < 4U; ++component) {
      prefixes[component][0] = 0U;
      moment_prefixes[component][0] = 0U;
    }
    for (std::int32_t x = 0; x < width; ++x) {
      const auto values = source_components(source.pixel(x, y));
      for (std::size_t component = 0; component < 4U; ++component) {
        prefixes[component][static_cast<std::size_t>(x) + 1U] =
            prefixes[component][static_cast<std::size_t>(x)] +
            values[component];
        moment_prefixes[component][static_cast<std::size_t>(x) + 1U] =
            moment_prefixes[component][static_cast<std::size_t>(x)] +
            static_cast<std::uint64_t>(x) * values[component];
      }
    }
    const auto first = source_components(source.pixel(0, y));
    const auto last = source_components(source.pixel(width - 1, y));
    for (std::int32_t x = 0; x < width; ++x) {
      auto &destination =
          horizontal[static_cast<std::size_t>(y) *
                         static_cast<std::size_t>(width) +
                     static_cast<std::size_t>(x)];
      for (std::size_t component = 0; component < 4U; ++component) {
        const auto numerator = tilt_weighted_axis_sum(
            prefixes[component], moment_prefixes[component], x, width, radius,
            base_weight, repeat_edge, first[component], last[component]);
        destination.component[component] = static_cast<std::uint32_t>(
            tilt_divide_rounded(numerator * kTiltIntermediateScale,
                                kernel_weight));
      }
    }
  }

  for (std::int32_t x = 0; x < width; ++x) {
    report_filter_progress(
        progress,
        static_cast<int>(std::min<std::uint64_t>(
            completed_lines++, std::numeric_limits<int>::max())),
        static_cast<int>(std::min<std::uint64_t>(
            total_lines, std::numeric_limits<int>::max())),
        FilterProgressStage::Blurring);
    for (std::size_t component = 0; component < 4U; ++component) {
      prefixes[component][0] = 0U;
      moment_prefixes[component][0] = 0U;
    }
    for (std::int32_t y = 0; y < height; ++y) {
      const auto &values = horizontal[
                               static_cast<std::size_t>(y) *
                                   static_cast<std::size_t>(width) +
                               static_cast<std::size_t>(x)]
                               .component;
      for (std::size_t component = 0; component < 4U; ++component) {
        prefixes[component][static_cast<std::size_t>(y) + 1U] =
            prefixes[component][static_cast<std::size_t>(y)] +
            values[component];
        moment_prefixes[component][static_cast<std::size_t>(y) + 1U] =
            moment_prefixes[component][static_cast<std::size_t>(y)] +
            static_cast<std::uint64_t>(y) * values[component];
      }
    }
    const auto &first = horizontal[static_cast<std::size_t>(x)].component;
    const auto &last =
        horizontal[static_cast<std::size_t>(height - 1) *
                       static_cast<std::size_t>(width) +
                   static_cast<std::size_t>(x)]
            .component;
    for (std::int32_t y = 0; y < height; ++y) {
      std::array<std::uint32_t, 4> values{};
      for (std::size_t component = 0; component < 4U; ++component) {
        const auto numerator = tilt_weighted_axis_sum(
            prefixes[component], moment_prefixes[component], y, height, radius,
            base_weight, repeat_edge, first[component], last[component]);
        values[component] = static_cast<std::uint32_t>(
            tilt_divide_rounded(numerator, kernel_weight));
      }
      auto *pixel = output.pixel(x, y);
      const auto alpha = has_alpha
                             ? std::min<std::uint32_t>(
                                   255U, static_cast<std::uint32_t>(
                                             tilt_divide_rounded(
                                                 values[3],
                                                 kTiltIntermediateScale)))
                             : 255U;
      if (has_alpha) {
        pixel[3] = static_cast<std::uint8_t>(alpha);
      }
      for (std::size_t component = 0; component < 3U; ++component) {
        pixel[component] =
            alpha == 0U || values[3] == 0U
                ? 0U
                : static_cast<std::uint8_t>(std::min<std::uint32_t>(
                      255U, static_cast<std::uint32_t>(tilt_divide_rounded(
                                values[component], values[3]))));
      }
    }
  }
  report_filter_progress(
      progress,
      static_cast<int>(std::min<std::uint64_t>(
          total_lines, std::numeric_limits<int>::max())),
      static_cast<int>(std::min<std::uint64_t>(
          total_lines, std::numeric_limits<int>::max())),
      FilterProgressStage::Blurring);
  return output;
}

void tilt_blend_levels(PixelBuffer &output, const PixelBuffer &low,
                       const PixelBuffer &high,
                       const std::vector<std::uint16_t> &blur_mask,
                       std::uint64_t maximum_radius_fixed,
                       std::uint64_t low_radius_fixed,
                       std::uint64_t high_radius_fixed,
                       const FilterProgress *progress) {
  const auto radius_span = high_radius_fixed - low_radius_fixed;
  const auto has_alpha = output.format().channels >= 4;
  for (std::int32_t y = 0; y < output.height(); ++y) {
    report_filter_progress(progress, y, output.height(),
                           FilterProgressStage::Blurring);
    for (std::int32_t x = 0; x < output.width(); ++x) {
      const auto index = static_cast<std::size_t>(y) *
                             static_cast<std::size_t>(output.width()) +
                         static_cast<std::size_t>(x);
      const auto target_radius =
          (maximum_radius_fixed * blur_mask[index] + kTiltMixScale / 2U) /
          kTiltMixScale;
      // Level zero already initialized the result. Every positive target falls
      // in exactly one half-open radius interval (low, high].
      if (target_radius <= low_radius_fixed ||
          target_radius > high_radius_fixed || radius_span == 0U) {
        continue;
      }
      const auto mix = std::min<std::uint64_t>(
          kTiltMixScale,
          ((target_radius - low_radius_fixed) * kTiltMixScale +
           radius_span / 2U) /
              radius_span);
      const auto inverse_mix = kTiltMixScale - mix;
      const auto *low_pixel = low.pixel(x, y);
      const auto *high_pixel = high.pixel(x, y);
      auto *destination = output.pixel(x, y);
      const auto low_alpha =
          static_cast<std::uint64_t>(has_alpha ? low_pixel[3] : 255U);
      const auto high_alpha =
          static_cast<std::uint64_t>(has_alpha ? high_pixel[3] : 255U);
      const auto alpha_numerator =
          low_alpha * inverse_mix + high_alpha * mix;
      for (std::size_t component = 0; component < 3U; ++component) {
        if (alpha_numerator == 0U) {
          destination[component] = 0U;
          continue;
        }
        const auto premultiplied_numerator =
            static_cast<std::uint64_t>(low_pixel[component]) * low_alpha *
                inverse_mix +
            static_cast<std::uint64_t>(high_pixel[component]) * high_alpha *
                mix;
        destination[component] = static_cast<std::uint8_t>(
            std::min<std::uint64_t>(
                255U, (premultiplied_numerator + alpha_numerator / 2U) /
                          alpha_numerator));
      }
      if (has_alpha) {
        destination[3] = static_cast<std::uint8_t>(
            std::min<std::uint64_t>(
                255U, (alpha_numerator + kTiltMixScale / 2U) /
                          kTiltMixScale));
      }
    }
  }
  report_filter_progress(progress, output.height(), output.height(),
                         FilterProgressStage::Blurring);
}

constexpr std::int64_t kAperturePositionScale = 8;
constexpr std::int64_t kApertureNormalScale = 1LL << 20;
constexpr int kApertureCoverageGrid = 4;
constexpr double kMaximumFullResolutionApertureRadius = 6.0;

struct ApertureKernelSample {
  std::int32_t x{};
  std::int32_t y{};
  std::uint32_t weight{};
};

struct ApertureKernel {
  std::vector<ApertureKernelSample> samples;
  std::uint64_t weight_sum{};
};

[[nodiscard]] ApertureKernel build_aperture_kernel(
    double radius, int blade_count, int curvature_percent,
    int rotation_degrees) {
  radius = std::clamp(radius, 0.0, kMaximumFullResolutionApertureRadius);
  blade_count = std::clamp(blade_count, 3, 8);
  curvature_percent = std::clamp(curvature_percent, 0, 100);
  rotation_degrees = std::clamp(rotation_degrees, -180, 180);

  const auto radius_fixed = std::max<std::int64_t>(
      1, std::llround(radius * kAperturePositionScale));
  const auto radius_squared = static_cast<std::uint64_t>(radius_fixed) *
                              static_cast<std::uint64_t>(radius_fixed);
  const auto apothem = static_cast<std::int64_t>(std::llround(
      static_cast<double>(radius_fixed) *
      std::cos(kFilterPi / static_cast<double>(blade_count)) *
      static_cast<double>(kApertureNormalScale)));
  std::vector<std::array<std::int64_t, 2>> side_normals;
  side_normals.reserve(static_cast<std::size_t>(blade_count));
  const auto rotation =
      static_cast<double>(rotation_degrees) * kFilterPi / 180.0;
  for (int side = 0; side < blade_count; ++side) {
    const auto angle = rotation +
                       (2.0 * static_cast<double>(side) + 1.0) * kFilterPi /
                           static_cast<double>(blade_count);
    side_normals.push_back(
        {std::llround(std::cos(angle) * kApertureNormalScale),
         std::llround(std::sin(angle) * kApertureNormalScale)});
  }

  ApertureKernel kernel;
  const auto extent = std::max(
      1, static_cast<int>(std::ceil(radius + 0.5)));
  kernel.samples.reserve(static_cast<std::size_t>((extent * 2 + 1) *
                                                   (extent * 2 + 1)));
  for (int y = -extent; y <= extent; ++y) {
    for (int x = -extent; x <= extent; ++x) {
      int polygon_coverage = 0;
      int circle_coverage = 0;
      for (int sub_y = 0; sub_y < kApertureCoverageGrid; ++sub_y) {
        const auto sample_y =
            static_cast<std::int64_t>(y) * kAperturePositionScale +
            (sub_y * 2 + 1 - kApertureCoverageGrid);
        for (int sub_x = 0; sub_x < kApertureCoverageGrid; ++sub_x) {
          const auto sample_x =
              static_cast<std::int64_t>(x) * kAperturePositionScale +
              (sub_x * 2 + 1 - kApertureCoverageGrid);
          const auto squared =
              static_cast<std::uint64_t>(sample_x * sample_x) +
              static_cast<std::uint64_t>(sample_y * sample_y);
          if (squared <= radius_squared) {
            ++circle_coverage;
          }
          const auto inside_polygon =
              std::all_of(side_normals.begin(), side_normals.end(),
                          [&](const auto &normal) {
                            return sample_x * normal[0] +
                                       sample_y * normal[1] <=
                                   apothem;
                          });
          if (inside_polygon) {
            ++polygon_coverage;
          }
        }
      }
      const auto weight = static_cast<std::uint32_t>(
          polygon_coverage * (100 - curvature_percent) +
          circle_coverage * curvature_percent);
      if (weight == 0U) {
        continue;
      }
      kernel.samples.push_back(ApertureKernelSample{x, y, weight});
      kernel.weight_sum += weight;
    }
  }
  if (kernel.samples.empty() || kernel.weight_sum == 0U) {
    kernel.samples = {{0, 0, 1U}};
    kernel.weight_sum = 1U;
  }
  return kernel;
}

[[nodiscard]] PixelBuffer aperture_pad_source(const PixelBuffer &source,
                                              int margin) {
  const auto width = static_cast<std::int64_t>(source.width()) +
                     static_cast<std::int64_t>(margin) * 2LL;
  const auto height = static_cast<std::int64_t>(source.height()) +
                      static_cast<std::int64_t>(margin) * 2LL;
  if (width <= 0 || height <= 0 ||
      width > std::numeric_limits<std::int32_t>::max() ||
      height > std::numeric_limits<std::int32_t>::max()) {
    throw std::overflow_error("Lens Blur working buffer overflow");
  }
  PixelBuffer output(static_cast<std::int32_t>(width),
                     static_cast<std::int32_t>(height), source.format());
  output.clear(0);
  const auto has_alpha = source.format().channels >= 4;
  for (std::int32_t y = 0; y < output.height(); ++y) {
    const auto source_y = y - margin;
    for (std::int32_t x = 0; x < output.width(); ++x) {
      const auto source_x = x - margin;
      if (has_alpha &&
          (source_x < 0 || source_y < 0 || source_x >= source.width() ||
           source_y >= source.height())) {
        continue;
      }
      const auto clamped_x =
          std::clamp<std::int32_t>(source_x, 0, source.width() - 1);
      const auto clamped_y =
          std::clamp<std::int32_t>(source_y, 0, source.height() - 1);
      std::copy(source.pixel(clamped_x, clamped_y),
                source.pixel(clamped_x, clamped_y) + source.format().channels,
                output.pixel(x, y));
    }
  }
  return output;
}

[[nodiscard]] PixelBuffer aperture_crop_source(const PixelBuffer &source,
                                               int margin,
                                               std::int32_t width,
                                               std::int32_t height) {
  PixelBuffer output(width, height, source.format());
  const auto row_bytes = static_cast<std::size_t>(width) *
                         bytes_per_pixel(source.format());
  for (std::int32_t y = 0; y < height; ++y) {
    const auto *source_row = source.pixel(margin, margin + y);
    std::copy(source_row, source_row + row_bytes, output.pixel(0, y));
  }
  return output;
}

[[nodiscard]] PixelBuffer aperture_downsample(
    const PixelBuffer &source, int factor, const FilterProgress *progress) {
  const auto width =
      std::max<std::int32_t>(1, (source.width() + factor - 1) / factor);
  const auto height =
      std::max<std::int32_t>(1, (source.height() + factor - 1) / factor);
  PixelBuffer output(width, height, source.format());
  output.clear(0);
  const auto has_alpha = source.format().channels >= 4;
  for (std::int32_t y = 0; y < height; ++y) {
    report_filter_progress(progress, y, height,
                           FilterProgressStage::Blurring);
    const auto first_y = y * factor;
    const auto last_y = std::min(source.height(), first_y + factor);
    for (std::int32_t x = 0; x < width; ++x) {
      const auto first_x = x * factor;
      const auto last_x = std::min(source.width(), first_x + factor);
      const auto sample_count = static_cast<std::uint64_t>(
          std::max(1, (last_x - first_x) * (last_y - first_y)));
      std::array<std::uint64_t, 3> color{};
      std::uint64_t alpha_sum = 0U;
      for (auto sample_y = first_y; sample_y < last_y; ++sample_y) {
        for (auto sample_x = first_x; sample_x < last_x; ++sample_x) {
          const auto *pixel = source.pixel(sample_x, sample_y);
          const auto alpha =
              static_cast<std::uint64_t>(has_alpha ? pixel[3] : 255U);
          alpha_sum += alpha;
          for (std::size_t channel = 0; channel < 3U; ++channel) {
            color[channel] +=
                static_cast<std::uint64_t>(pixel[channel]) * alpha;
          }
        }
      }
      auto *destination = output.pixel(x, y);
      for (std::size_t channel = 0; channel < 3U; ++channel) {
        destination[channel] =
            alpha_sum == 0U
                ? 0U
                : static_cast<std::uint8_t>(std::min<std::uint64_t>(
                      255U, (color[channel] + alpha_sum / 2U) / alpha_sum));
      }
      if (has_alpha) {
        destination[3] = static_cast<std::uint8_t>(
            std::min<std::uint64_t>(
                255U, (alpha_sum + sample_count / 2U) / sample_count));
      }
    }
  }
  report_filter_progress(progress, height, height,
                         FilterProgressStage::Blurring);
  return output;
}

[[nodiscard]] PixelBuffer aperture_convolve(
    const PixelBuffer &source, const ApertureKernel &kernel,
    const FilterProgress *progress) {
  PixelBuffer output(source.width(), source.height(), source.format());
  output.clear(0);
  const auto has_alpha = source.format().channels >= 4;
  for (std::int32_t y = 0; y < source.height(); ++y) {
    report_filter_progress(progress, y, source.height(),
                           FilterProgressStage::Blurring);
    for (std::int32_t x = 0; x < source.width(); ++x) {
      std::array<std::uint64_t, 3> color{};
      std::uint64_t alpha_sum = 0U;
      for (const auto &sample : kernel.samples) {
        auto sample_x = x + sample.x;
        auto sample_y = y + sample.y;
        if (has_alpha &&
            (sample_x < 0 || sample_y < 0 ||
             sample_x >= source.width() || sample_y >= source.height())) {
          continue;
        }
        sample_x = std::clamp<std::int32_t>(sample_x, 0, source.width() - 1);
        sample_y = std::clamp<std::int32_t>(sample_y, 0, source.height() - 1);
        const auto *pixel = source.pixel(sample_x, sample_y);
        const auto alpha =
            static_cast<std::uint64_t>(has_alpha ? pixel[3] : 255U);
        const auto alpha_weight =
            alpha * static_cast<std::uint64_t>(sample.weight);
        alpha_sum += alpha_weight;
        for (std::size_t channel = 0; channel < 3U; ++channel) {
          color[channel] +=
              static_cast<std::uint64_t>(pixel[channel]) * alpha_weight;
        }
      }
      auto *destination = output.pixel(x, y);
      for (std::size_t channel = 0; channel < 3U; ++channel) {
        destination[channel] =
            alpha_sum == 0U
                ? 0U
                : static_cast<std::uint8_t>(std::min<std::uint64_t>(
                      255U, (color[channel] + alpha_sum / 2U) / alpha_sum));
      }
      if (has_alpha) {
        const auto denominator = kernel.weight_sum * 255U;
        destination[3] = static_cast<std::uint8_t>(
            std::min<std::uint64_t>(
                255U, (alpha_sum * 255U + denominator / 2U) /
                          denominator));
        if (destination[3] == 0U) {
          destination[0] = 0U;
          destination[1] = 0U;
          destination[2] = 0U;
        }
      }
    }
  }
  report_filter_progress(progress, source.height(), source.height(),
                         FilterProgressStage::Blurring);
  return output;
}

[[nodiscard]] PixelBuffer aperture_upsample(
    const PixelBuffer &source, std::int32_t width, std::int32_t height,
    int factor, const FilterProgress *progress) {
  constexpr std::int64_t kCoordinateScale = 65536;
  constexpr std::uint64_t kBilinearWeight =
      static_cast<std::uint64_t>(kCoordinateScale) * kCoordinateScale;
  PixelBuffer output(width, height, source.format());
  output.clear(0);
  const auto has_alpha = source.format().channels >= 4;
  const auto coordinate_for = [factor](std::int32_t value,
                                       std::int32_t source_extent) {
    const auto numerator =
        (static_cast<std::int64_t>(value) * 2LL + 1LL) * 65536LL;
    const auto coordinate = numerator / (factor * 2LL) - 32768LL;
    return std::clamp<std::int64_t>(
        coordinate, 0,
        static_cast<std::int64_t>(std::max(0, source_extent - 1)) * 65536LL);
  };
  for (std::int32_t y = 0; y < height; ++y) {
    report_filter_progress(progress, y, height,
                           FilterProgressStage::Blurring);
    const auto sample_y = coordinate_for(y, source.height());
    const auto y0 = static_cast<std::int32_t>(sample_y / kCoordinateScale);
    const auto y1 = std::min(source.height() - 1, y0 + 1);
    const auto fraction_y = sample_y % kCoordinateScale;
    for (std::int32_t x = 0; x < width; ++x) {
      const auto sample_x = coordinate_for(x, source.width());
      const auto x0 = static_cast<std::int32_t>(sample_x / kCoordinateScale);
      const auto x1 = std::min(source.width() - 1, x0 + 1);
      const auto fraction_x = sample_x % kCoordinateScale;
      const std::array<std::uint64_t, 4> weights{
          static_cast<std::uint64_t>(kCoordinateScale - fraction_x) *
              static_cast<std::uint64_t>(kCoordinateScale - fraction_y),
          static_cast<std::uint64_t>(fraction_x) *
              static_cast<std::uint64_t>(kCoordinateScale - fraction_y),
          static_cast<std::uint64_t>(kCoordinateScale - fraction_x) *
              static_cast<std::uint64_t>(fraction_y),
          static_cast<std::uint64_t>(fraction_x) *
              static_cast<std::uint64_t>(fraction_y)};
      const std::array<const std::uint8_t *, 4> pixels{
          source.pixel(x0, y0), source.pixel(x1, y0), source.pixel(x0, y1),
          source.pixel(x1, y1)};
      std::array<std::uint64_t, 3> color{};
      std::uint64_t alpha_sum = 0U;
      for (std::size_t corner = 0; corner < pixels.size(); ++corner) {
        const auto alpha = static_cast<std::uint64_t>(
            has_alpha ? pixels[corner][3] : 255U);
        const auto alpha_weight = alpha * weights[corner];
        alpha_sum += alpha_weight;
        for (std::size_t channel = 0; channel < 3U; ++channel) {
          color[channel] += static_cast<std::uint64_t>(
                                pixels[corner][channel]) *
                            alpha_weight;
        }
      }
      auto *destination = output.pixel(x, y);
      for (std::size_t channel = 0; channel < 3U; ++channel) {
        destination[channel] =
            alpha_sum == 0U
                ? 0U
                : static_cast<std::uint8_t>(std::min<std::uint64_t>(
                      255U, (color[channel] + alpha_sum / 2U) / alpha_sum));
      }
      if (has_alpha) {
        const auto denominator = kBilinearWeight * 255U;
        destination[3] = static_cast<std::uint8_t>(
            std::min<std::uint64_t>(
                255U, (alpha_sum * 255U + denominator / 2U) /
                          denominator));
        if (destination[3] == 0U) {
          destination[0] = 0U;
          destination[1] = 0U;
          destination[2] = 0U;
        }
      }
    }
  }
  report_filter_progress(progress, height, height,
                         FilterProgressStage::Blurring);
  return output;
}

[[nodiscard]] std::uint64_t integer_square_root(std::uint64_t value) {
  std::uint64_t result = 0U;
  std::uint64_t bit = 1ULL << 62U;
  while (bit > value) {
    bit >>= 2U;
  }
  while (bit != 0U) {
    if (value >= result + bit) {
      value -= result + bit;
      result = (result >> 1U) + bit;
    } else {
      result >>= 1U;
    }
    bit >>= 2U;
  }
  return result;
}

std::uint32_t filter_coordinate_hash(std::int32_t x, std::int32_t y,
                                     std::uint16_t channel) noexcept {
  auto value = static_cast<std::uint32_t>(x + 1) * 73856093U;
  value ^= static_cast<std::uint32_t>(y + 1) * 19349663U;
  value ^= static_cast<std::uint32_t>(channel + 1) * 83492791U;
  value ^= value >> 13U;
  value *= 1274126177U;
  value ^= value >> 16U;
  return value;
}

std::uint32_t filter_noise_hash(std::int32_t x, std::int32_t y,
                                std::uint32_t seed) noexcept {
  auto value = static_cast<std::uint32_t>(x + 16384) * 374761393U;
  value ^= static_cast<std::uint32_t>(y + 8192) * 668265263U;
  value ^= seed * 2246822519U;
  value ^= value >> 13U;
  value *= 1274126177U;
  value ^= value >> 16U;
  return value;
}

double filter_smooth_step(double value) {
  value = std::clamp(value, 0.0, 1.0);
  return value * value * (3.0 - 2.0 * value);
}

double filter_lattice_noise(double x, double y, std::uint32_t seed) {
  const auto x0 = static_cast<std::int32_t>(std::floor(x));
  const auto y0 = static_cast<std::int32_t>(std::floor(y));
  const auto tx = filter_smooth_step(x - static_cast<double>(x0));
  const auto ty = filter_smooth_step(y - static_cast<double>(y0));
  const auto sample = [seed](std::int32_t sx, std::int32_t sy) {
    return static_cast<double>(filter_noise_hash(sx, sy, seed) & 0xffffU) /
           65535.0;
  };
  const auto top = sample(x0, y0) * (1.0 - tx) + sample(x0 + 1, y0) * tx;
  const auto bottom =
      sample(x0, y0 + 1) * (1.0 - tx) + sample(x0 + 1, y0 + 1) * tx;
  return top * (1.0 - ty) + bottom * ty;
}

double filter_cloud_noise(std::int32_t x, std::int32_t y, int scale, int detail,
                          int contrast, int seed) {
  scale = std::clamp(scale, 12, 512);
  detail = std::clamp(detail, 1, 8);
  contrast = std::clamp(contrast, 0, 100);
  double value = 0.0;
  double amplitude = 1.0;
  double amplitude_sum = 0.0;
  double frequency = 1.0;
  for (int octave = 0; octave < detail; ++octave) {
    value +=
        filter_lattice_noise(
            (static_cast<double>(x) * frequency) / static_cast<double>(scale),
            (static_cast<double>(y) * frequency) / static_cast<double>(scale),
            static_cast<std::uint32_t>(seed + octave * 101)) *
        amplitude;
    amplitude_sum += amplitude;
    amplitude *= 0.5;
    frequency *= 2.0;
  }
  value /= std::max(0.0001, amplitude_sum);
  const auto contrast_factor = 1.0 + static_cast<double>(contrast) / 65.0;
  return std::clamp((value - 0.5) * contrast_factor + 0.5, 0.0, 1.0);
}

void apply_clouds_to_pixels(PixelBuffer &pixels, RgbColor foreground,
                            RgbColor background, int scale, int detail,
                            int contrast, int seed,
                            const FilterProgress *progress) {
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    report_filter_progress(progress, y, pixels.height(),
                           FilterProgressStage::GeneratingClouds);
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto amount =
          filter_cloud_noise(x, y, scale, detail, contrast, seed);
      auto *px = pixels.pixel(x, y);
      px[0] = filter_clamp_byte(static_cast<double>(background.red) *
                                    (1.0 - amount) +
                                static_cast<double>(foreground.red) * amount);
      px[1] = filter_clamp_byte(static_cast<double>(background.green) *
                                    (1.0 - amount) +
                                static_cast<double>(foreground.green) * amount);
      px[2] = filter_clamp_byte(static_cast<double>(background.blue) *
                                    (1.0 - amount) +
                                static_cast<double>(foreground.blue) * amount);
      if (pixels.format().channels >= 4) {
        px[3] = 255;
      }
    }
  }
  report_filter_progress(progress, pixels.height(), pixels.height(),
                         FilterProgressStage::GeneratingClouds);
}

void apply_twirl_to_pixels(PixelBuffer &pixels, const PixelBuffer &original,
                           int angle_degrees, int radius_percent,
                           double center_x_percent, double center_y_percent,
                           const FilterProgress *progress) {
  const auto channels = pixels.format().channels;
  const auto center_x =
      filter_center_coordinate(pixels.width(), center_x_percent);
  const auto center_y =
      filter_center_coordinate(pixels.height(), center_y_percent);
  const auto radius = std::max(
      1.0, static_cast<double>(std::min(pixels.width(), pixels.height())) *
               0.5 * static_cast<double>(std::clamp(radius_percent, 1, 100)) /
               100.0);
  const auto angle = static_cast<double>(std::clamp(angle_degrees, -720, 720)) *
                     3.14159265358979323846 / 180.0;
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    report_filter_progress(progress, y, pixels.height(),
                           FilterProgressStage::Twisting);
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto dx = static_cast<double>(x) - center_x;
      const auto dy = static_cast<double>(y) - center_y;
      const auto distance = std::sqrt(dx * dx + dy * dy);
      if (distance > radius) {
        continue;
      }
      const auto falloff = 1.0 - distance / radius;
      const auto source_angle = std::atan2(dy, dx) - angle * falloff * falloff;
      const auto source_x = std::clamp<std::int32_t>(
          static_cast<std::int32_t>(
              std::lround(center_x + std::cos(source_angle) * distance)),
          0, pixels.width() - 1);
      const auto source_y = std::clamp<std::int32_t>(
          static_cast<std::int32_t>(
              std::lround(center_y + std::sin(source_angle) * distance)),
          0, pixels.height() - 1);
      auto *dst = pixels.pixel(x, y);
      const auto *src = original.pixel(source_x, source_y);
      std::copy(src, src + channels, dst);
    }
  }
  report_filter_progress(progress, pixels.height(), pixels.height(),
                         FilterProgressStage::Twisting);
}

double filter_sampled_luminance(const PixelBuffer &pixels, double x, double y) {
  x = std::clamp(
      x, 0.0,
      static_cast<double>(std::max<std::int32_t>(0, pixels.width() - 1)));
  y = std::clamp(
      y, 0.0,
      static_cast<double>(std::max<std::int32_t>(0, pixels.height() - 1)));
  const auto x0 = static_cast<std::int32_t>(std::floor(x));
  const auto y0 = static_cast<std::int32_t>(std::floor(y));
  const auto x1 = std::min<std::int32_t>(pixels.width() - 1, x0 + 1);
  const auto y1 = std::min<std::int32_t>(pixels.height() - 1, y0 + 1);
  const auto tx = x - static_cast<double>(x0);
  const auto ty = y - static_cast<double>(y0);
  const auto l00 = static_cast<double>(filter_luminance(pixels.pixel(x0, y0)));
  const auto l10 = static_cast<double>(filter_luminance(pixels.pixel(x1, y0)));
  const auto l01 = static_cast<double>(filter_luminance(pixels.pixel(x0, y1)));
  const auto l11 = static_cast<double>(filter_luminance(pixels.pixel(x1, y1)));
  const auto top = l00 * (1.0 - tx) + l10 * tx;
  const auto bottom = l01 * (1.0 - tx) + l11 * tx;
  return top * (1.0 - ty) + bottom * ty;
}

void apply_emboss_to_pixels(PixelBuffer &pixels, const PixelBuffer &original,
                            int angle_degrees, int height, int amount,
                            const FilterProgress *progress) {
  const auto angle = static_cast<double>(angle_degrees) * kFilterPi / 180.0;
  const auto distance = static_cast<double>(std::clamp(height, 1, 24));
  const auto offset_x = std::cos(angle) * distance;
  const auto offset_y = -std::sin(angle) * distance;
  amount = std::clamp(amount, 0, 300);
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    report_filter_progress(progress, y, pixels.height(),
                           FilterProgressStage::Embossing);
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto highlight =
          filter_sampled_luminance(original, static_cast<double>(x) - offset_x,
                                   static_cast<double>(y) - offset_y);
      const auto shadow =
          filter_sampled_luminance(original, static_cast<double>(x) + offset_x,
                                   static_cast<double>(y) + offset_y);
      const auto value = filter_clamp_byte(
          128.0 + (highlight - shadow) * static_cast<double>(amount) / 100.0);
      auto *px = pixels.pixel(x, y);
      px[0] = value;
      px[1] = value;
      px[2] = value;
    }
  }
  report_filter_progress(progress, pixels.height(), pixels.height(),
                         FilterProgressStage::Embossing);
}

void apply_unsharp_mask_to_pixels(PixelBuffer &pixels, const PixelBuffer &,
                                  double amount, double radius, int threshold,
                                  const FilterProgress *progress) {
  if (pixels.format().channels < 3 || pixels.empty()) {
    report_filter_progress(progress, 1, 1, FilterProgressStage::Sharpening);
    return;
  }
  stage_rgba_and_render(pixels, [&](const PixelBuffer &rgba) {
    return render_photoshop_unsharp_mask(
        rgba, Rect::from_size(rgba.width(), rgba.height()), amount, radius,
        threshold, progress);
  });
}

void apply_motion_blur_to_pixels(PixelBuffer &pixels, const PixelBuffer &,
                                 int angle_degrees, int distance,
                                 const FilterProgress *progress) {
  if (pixels.format().channels < 3 || pixels.empty()) {
    report_filter_progress(progress, 1, 1, FilterProgressStage::Blurring);
    return;
  }
  stage_rgba_and_render(pixels, [&](const PixelBuffer &rgba) {
    return render_photoshop_motion_blur(
        rgba, Rect::from_size(rgba.width(), rgba.height()), angle_degrees,
        distance, progress);
  });
}

void apply_radial_blur_to_pixels(PixelBuffer &pixels,
                                  const PixelBuffer &original, int amount,
                                  int samples, double center_x_percent,
                                  double center_y_percent,
                                  const FilterProgress *progress) {
  amount = std::clamp(amount, 0, 100);
  samples = std::clamp(samples, 4, 32);
  const auto center_x =
      filter_center_coordinate(pixels.width(), center_x_percent);
  const auto center_y =
      filter_center_coordinate(pixels.height(), center_y_percent);
  const auto sweep = static_cast<double>(amount) * 3.6 * kFilterPi / 180.0;
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    report_filter_progress(progress, y, pixels.height(),
                           FilterProgressStage::Blurring);
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto dx = static_cast<double>(x) - center_x;
      const auto dy = static_cast<double>(y) - center_y;
      FilterPixelAccum accum;
      for (int sample = 0; sample < samples; ++sample) {
        const auto t = samples <= 1 ? 0.0
                                    : static_cast<double>(sample) /
                                              static_cast<double>(samples - 1) -
                                          0.5;
        const auto angle = sweep * t;
        const auto source_x =
            center_x + dx * std::cos(angle) - dy * std::sin(angle);
        const auto source_y =
            center_y + dx * std::sin(angle) + dy * std::cos(angle);
        filter_accumulate_sample(accum, original, source_x, source_y);
      }
      filter_write_accumulated_pixel(pixels, x, y, accum);
    }
  }
  report_filter_progress(progress, pixels.height(), pixels.height(),
                         FilterProgressStage::Blurring);
}

void apply_wave_to_pixels(PixelBuffer &pixels, const PixelBuffer &original,
                          int amplitude, int wavelength, int phase,
                          const FilterProgress *progress) {
  amplitude = std::clamp(amplitude, 0, 64);
  wavelength = std::clamp(wavelength, 4, 256);
  const auto phase_radians =
      static_cast<double>(std::clamp(phase, 0, 360)) * kFilterPi / 180.0;
  const auto frequency = 2.0 * kFilterPi / static_cast<double>(wavelength);
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    report_filter_progress(progress, y, pixels.height(),
                           FilterProgressStage::Distorting);
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto source_x =
          static_cast<double>(x) +
          std::sin(static_cast<double>(y) * frequency + phase_radians) *
              static_cast<double>(amplitude);
      const auto source_y =
          static_cast<double>(y) + std::sin(static_cast<double>(x) * frequency +
                                            phase_radians + kFilterPi * 0.5) *
                                       static_cast<double>(amplitude) * 0.5;
      filter_copy_sampled_pixel(pixels, original, x, y, source_x, source_y);
    }
  }
  report_filter_progress(progress, pixels.height(), pixels.height(),
                         FilterProgressStage::Distorting);
}

void apply_pinch_bloat_to_pixels(PixelBuffer &pixels,
                                  const PixelBuffer &original, int amount,
                                  int radius_percent,
                                  double center_x_percent,
                                  double center_y_percent,
                                  const FilterProgress *progress) {
  amount = std::clamp(amount, -100, 100);
  radius_percent = std::clamp(radius_percent, 1, 100);
  const auto center_x =
      filter_center_coordinate(pixels.width(), center_x_percent);
  const auto center_y =
      filter_center_coordinate(pixels.height(), center_y_percent);
  const auto radius = std::max(
      1.0, static_cast<double>(std::min(pixels.width(), pixels.height())) *
               0.5 * static_cast<double>(radius_percent) / 100.0);
  const auto strength = static_cast<double>(amount) / 100.0;
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    report_filter_progress(progress, y, pixels.height(),
                           FilterProgressStage::Distorting);
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto dx = static_cast<double>(x) - center_x;
      const auto dy = static_cast<double>(y) - center_y;
      const auto distance = std::sqrt(dx * dx + dy * dy);
      if (distance <= 0.0001 || distance > radius) {
        continue;
      }
      const auto normalized = distance / radius;
      const auto falloff = (1.0 - normalized) * (1.0 - normalized);
      const auto source_distance =
          std::clamp(distance * (1.0 - strength * falloff * 0.75), 0.0, radius);
      const auto sample_scale = source_distance / distance;
      filter_copy_sampled_pixel(pixels, original, x, y,
                                center_x + dx * sample_scale,
                                center_y + dy * sample_scale);
    }
  }
  report_filter_progress(progress, pixels.height(), pixels.height(),
                         FilterProgressStage::Distorting);
}

void apply_glowing_edges_to_pixels(PixelBuffer &pixels,
                                   const PixelBuffer &original, int edge_width,
                                   int brightness, int smoothness,
                                   const FilterProgress *progress) {
  edge_width = std::clamp(edge_width, 1, 12);
  brightness = std::clamp(brightness, 0, 300);
  smoothness = std::clamp(smoothness, 0, 12);
  auto source = original;
  const auto color_channels =
      std::min<std::uint16_t>(pixels.format().channels, 3);
  const auto total = pixels.height() * (smoothness > 0 ? 2 : 1);
  int progress_offset = 0;
  if (smoothness > 0) {
    report_filter_progress(progress, 0, total, FilterProgressStage::Blurring);
    apply_separable_tent_blur(source, original, smoothness, true, nullptr);
    report_filter_progress(progress, pixels.height(), total,
                           FilterProgressStage::Blurring);
    progress_offset = pixels.height();
  }
  const auto luminance_at = [&source](std::int32_t x, std::int32_t y) {
    x = std::clamp<std::int32_t>(x, 0, source.width() - 1);
    y = std::clamp<std::int32_t>(y, 0, source.height() - 1);
    return filter_luminance(source.pixel(x, y));
  };
  constexpr std::array<int, 9> kSobelX = {-1, 0, 1, -2, 0, 2, -1, 0, 1};
  constexpr std::array<int, 9> kSobelY = {-1, -2, -1, 0, 0, 0, 1, 2, 1};
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    report_filter_progress(progress, progress_offset + y, total,
                           FilterProgressStage::DetectingEdges);
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      int gx = 0;
      int gy = 0;
      int index = 0;
      for (int ky = -1; ky <= 1; ++ky) {
        for (int kx = -1; kx <= 1; ++kx) {
          const auto luminance = luminance_at(x + kx, y + ky);
          gx += luminance * kSobelX[static_cast<std::size_t>(index)];
          gy += luminance * kSobelY[static_cast<std::size_t>(index)];
          ++index;
        }
      }
      const auto magnitude = std::sqrt(static_cast<double>(gx * gx + gy * gy));
      const auto glow = filter_clamp_byte(
          magnitude * static_cast<double>(brightness) / 100.0 *
          (0.75 + static_cast<double>(edge_width) * 0.25));
      auto *dst = pixels.pixel(x, y);
      const auto *src = original.pixel(x, y);
      for (std::uint16_t channel = 0; channel < color_channels; ++channel) {
        const auto colorize =
            0.35 + 0.65 * static_cast<double>(src[channel]) / 255.0;
        dst[channel] = filter_clamp_byte(static_cast<double>(glow) * colorize);
      }
      if (pixels.format().channels >= 4) {
        dst[3] = src[3];
      }
    }
  }
  report_filter_progress(progress, total, total,
                         FilterProgressStage::DetectingEdges);
}

double halftone_cell_offset(double value, double cell_size) {
  auto offset = std::fmod(value, cell_size);
  if (offset < 0.0) {
    offset += cell_size;
  }
  return offset - cell_size * 0.5;
}

void apply_color_halftone_to_pixels(PixelBuffer &pixels,
                                    const PixelBuffer &original, int cell_size,
                                    int intensity, int contrast,
                                    const FilterProgress *progress) {
  cell_size = std::clamp(cell_size, 4, 64);
  intensity = std::clamp(intensity, 0, 100);
  contrast = std::clamp(contrast, 0, 100);
  constexpr std::array<double, 3> kAngles = {15.0, 75.0, 0.0};
  const auto contrast_factor = 1.0 + static_cast<double>(contrast) / 50.0;
  const auto cell = static_cast<double>(cell_size);
  const auto color_channels =
      std::min<std::uint16_t>(pixels.format().channels, 3);
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    report_filter_progress(progress, y, pixels.height(),
                           FilterProgressStage::RenderingHalftone);
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto *dst = pixels.pixel(x, y);
      const auto *src = original.pixel(x, y);
      for (std::uint16_t channel = 0; channel < color_channels; ++channel) {
        const auto angle =
            kAngles[static_cast<std::size_t>(channel)] * kFilterPi / 180.0;
        const auto rotated_x = static_cast<double>(x) * std::cos(angle) +
                               static_cast<double>(y) * std::sin(angle);
        const auto rotated_y = -static_cast<double>(x) * std::sin(angle) +
                               static_cast<double>(y) * std::cos(angle);
        const auto local_x = halftone_cell_offset(rotated_x, cell);
        const auto local_y = halftone_cell_offset(rotated_y, cell);
        auto coverage = 1.0 - static_cast<double>(src[channel]) / 255.0;
        coverage =
            std::clamp((coverage - 0.5) * contrast_factor + 0.5, 0.0, 1.0);
        const auto radius = std::sqrt(coverage) * cell * 0.48;
        const auto distance = std::sqrt(local_x * local_x + local_y * local_y);
        const auto dot = std::clamp(radius - distance + 1.0, 0.0, 1.0);
        const auto screen = filter_clamp_byte(255.0 * (1.0 - dot));
        dst[channel] = filter_blend_byte(src[channel], screen, intensity);
      }
      if (pixels.format().channels >= 4) {
        dst[3] = src[3];
      }
    }
  }
  report_filter_progress(progress, pixels.height(), pixels.height(),
                         FilterProgressStage::RenderingHalftone);
}

void apply_child_filter(
    const FilterRegistry &registry, std::string_view identifier,
    PixelBuffer &pixels, const FilterInvocation &parent,
    std::initializer_list<std::pair<std::string_view, int>> parameters,
    const FilterProgress *progress) {
  auto child = registry.default_invocation(identifier, parent.foreground,
                                           parent.background);
  for (const auto &[key, value] : parameters) {
    child.parameters[std::string(key)] = static_cast<std::int64_t>(value);
  }
  registry.apply(child, pixels, progress);
}

void execute_builtin_filter(const FilterRegistry &registry,
                            const FilterInvocation &invocation,
                            PixelBuffer &pixels,
                            const FilterProgress *progress) {
  if (pixels.format().bit_depth != BitDepth::UInt8) {
    throw std::invalid_argument("Filter previews support UInt8 buffers only");
  }
  if (pixels.format().channels < 3 || pixels.empty()) {
    return;
  }

  const auto identifier = std::string_view(invocation.filter_id);
  const auto original = pixels;
  const auto channels = std::min<std::uint16_t>(pixels.format().channels, 3);

  if (identifier == "patchy.filters.invert") {
    const auto amount = filter_value(invocation, "amount", 100);
    const auto total = pixels.height() * 2;
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      report_filter_row_progress(progress, y, pixels.height(), 0, total);
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        auto *px = pixels.pixel(x, y);
        for (std::uint16_t channel = 0; channel < channels; ++channel) {
          px[channel] = static_cast<std::uint8_t>(255 - px[channel]);
        }
      }
    }
    blend_filter_with_original(pixels, original, amount, progress,
                               pixels.height(), total);
    return;
  }

  if (identifier == "patchy.filters.brightness_contrast") {
    const auto brightness =
        std::clamp(filter_value(invocation, "brightness", 0), -100, 100);
    const auto contrast =
        std::clamp(filter_value(invocation, "contrast", 0), -100, 100);
    const auto factor = 1.0 + static_cast<double>(contrast) / 100.0;
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      report_filter_row_progress(progress, y, pixels.height());
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        auto *px = pixels.pixel(x, y);
        for (std::uint16_t channel = 0; channel < channels; ++channel) {
          px[channel] = filter_clamp_byte(
              (static_cast<double>(px[channel]) - 128.0) * factor + 128.0 +
              brightness);
        }
      }
    }
    finish_filter_row_progress(progress, pixels.height());
    return;
  }

  if (identifier == "patchy.filters.grayscale" ||
      identifier == "patchy.filters.desaturate") {
    const auto amount = filter_value(invocation, "amount", 100);
    const auto total = pixels.height() * 2;
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      report_filter_row_progress(progress, y, pixels.height(), 0, total);
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        auto *px = pixels.pixel(x, y);
        const auto luminance = filter_clamp_byte(filter_luminance(px));
        px[0] = luminance;
        px[1] = luminance;
        px[2] = luminance;
      }
    }
    blend_filter_with_original(pixels, original, amount, progress,
                               pixels.height(), total);
    return;
  }

  if (identifier == "patchy.filters.auto_contrast") {
    const auto amount = filter_value(invocation, "amount", 100);
    std::array<int, 3> min_channel = {255, 255, 255};
    std::array<int, 3> max_channel = {0, 0, 0};
    const auto total = pixels.height() * 3;
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      report_filter_progress(progress, y, total,
                             FilterProgressStage::Filtering);
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        const auto *px = pixels.pixel(x, y);
        for (std::uint16_t channel = 0; channel < channels; ++channel) {
          min_channel[static_cast<std::size_t>(channel)] =
              std::min(min_channel[static_cast<std::size_t>(channel)],
                       static_cast<int>(px[channel]));
          max_channel[static_cast<std::size_t>(channel)] =
              std::max(max_channel[static_cast<std::size_t>(channel)],
                       static_cast<int>(px[channel]));
        }
      }
    }
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      report_filter_progress(progress, pixels.height() + y, total,
                             FilterProgressStage::Filtering);
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        auto *px = pixels.pixel(x, y);
        for (std::uint16_t channel = 0; channel < channels; ++channel) {
          const auto index = static_cast<std::size_t>(channel);
          const auto range = max_channel[index] - min_channel[index];
          if (range > 0) {
            px[channel] = filter_clamp_byte(
                ((static_cast<int>(px[channel]) - min_channel[index]) * 255) /
                range);
          }
        }
      }
    }
    blend_filter_with_original(pixels, original, amount, progress,
                               pixels.height() * 2, total);
    return;
  }

  if (identifier == "patchy.filters.soft_glow") {
    constexpr int kPhaseCount = 6;
    const auto amount = filter_value(invocation, "amount", 100);
    auto glow = pixels;
    auto blur_progress = filter_progress_phase(progress, 0, kPhaseCount);
    apply_builtin_gaussian_blur_filter_pixels(glow, &blur_progress);
    auto glow_tint_progress = filter_progress_phase(progress, 1, kPhaseCount);
    tint_filter_pixels(glow, 26, 14, -4, &glow_tint_progress);
    auto glow_contrast_progress =
        filter_progress_phase(progress, 2, kPhaseCount);
    adjust_contrast_filter_pixels(glow, 0.9, 128, &glow_contrast_progress);
    auto blend_progress = filter_progress_phase(progress, 3, kPhaseCount);
    blend_overlay_filter_pixels(pixels, glow, 38, &blend_progress);
    auto tint_progress = filter_progress_phase(progress, 4, kPhaseCount);
    tint_filter_pixels(pixels, 8, 4, 0, &tint_progress);
    auto amount_progress = filter_progress_phase(progress, 5, kPhaseCount);
    blend_filter_with_original(pixels, original, amount, &amount_progress);
    return;
  }

  if (identifier == "patchy.filters.punchy_color") {
    constexpr int kPhaseCount = 4;
    const auto amount = filter_value(invocation, "amount", 100);
    auto contrast_progress = filter_progress_phase(progress, 0, kPhaseCount);
    adjust_contrast_filter_pixels(pixels, 1.28, 128, &contrast_progress);
    auto saturation_progress = filter_progress_phase(progress, 1, kPhaseCount);
    adjust_saturation_filter_pixels(pixels, 1.26, &saturation_progress);
    auto sharpen_progress = filter_progress_phase(progress, 2, kPhaseCount);
    apply_child_filter(registry, "patchy.filters.sharpen", pixels, invocation,
                       {{"amount", 100}}, &sharpen_progress);
    auto amount_progress = filter_progress_phase(progress, 3, kPhaseCount);
    blend_filter_with_original(pixels, original, amount, &amount_progress);
    return;
  }

  if (identifier == "patchy.filters.noir") {
    constexpr int kPhaseCount = 5;
    const auto amount = filter_value(invocation, "amount", 100);
    auto grain_progress = filter_progress_phase(progress, 0, kPhaseCount);
    apply_child_filter(registry, "patchy.filters.film_grain", pixels,
                       invocation, {{"amount", 50}}, &grain_progress);
    auto grayscale_progress = filter_progress_phase(progress, 1, kPhaseCount);
    apply_child_filter(registry, "patchy.filters.grayscale", pixels, invocation,
                       {{"amount", 100}}, &grayscale_progress);
    auto contrast_progress = filter_progress_phase(progress, 2, kPhaseCount);
    adjust_contrast_filter_pixels(pixels, 1.55, 128, &contrast_progress);
    auto vignette_progress = filter_progress_phase(progress, 3, kPhaseCount);
    apply_child_filter(registry, "patchy.filters.vignette", pixels, invocation,
                       {{"strength", 55}}, &vignette_progress);
    auto amount_progress = filter_progress_phase(progress, 4, kPhaseCount);
    blend_filter_with_original(pixels, original, amount, &amount_progress);
    return;
  }

  if (identifier == "patchy.filters.cinematic_matte") {
    constexpr int kPhaseCount = 5;
    const auto amount = filter_value(invocation, "amount", 100);
    auto saturation_progress = filter_progress_phase(progress, 0, kPhaseCount);
    adjust_saturation_filter_pixels(pixels, 0.82, &saturation_progress);
    auto contrast_progress = filter_progress_phase(progress, 1, kPhaseCount);
    adjust_contrast_filter_pixels(pixels, 0.92, 128, &contrast_progress);
    auto matte_progress = filter_progress_phase(progress, 2, kPhaseCount);
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      report_filter_row_progress(&matte_progress, y, pixels.height());
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        auto *px = pixels.pixel(x, y);
        const auto luminance = filter_luminance(px);
        const auto shadow = std::clamp(160 - luminance, 0, 160);
        const auto highlight = std::clamp(luminance - 96, 0, 159);
        px[0] = filter_clamp_byte(static_cast<int>(px[0]) + 18 - shadow / 14 +
                                  highlight / 18);
        px[1] = filter_clamp_byte(static_cast<int>(px[1]) + 12 + shadow / 18 +
                                  highlight / 30);
        px[2] = filter_clamp_byte(static_cast<int>(px[2]) + 18 + shadow / 11 -
                                  highlight / 24);
      }
    }
    finish_filter_row_progress(&matte_progress, pixels.height());
    auto vignette_progress = filter_progress_phase(progress, 3, kPhaseCount);
    apply_child_filter(registry, "patchy.filters.vignette", pixels, invocation,
                       {{"strength", 55}}, &vignette_progress);
    auto amount_progress = filter_progress_phase(progress, 4, kPhaseCount);
    blend_filter_with_original(pixels, original, amount, &amount_progress);
    return;
  }

  if (identifier == "patchy.filters.vintage_fade") {
    constexpr int kPhaseCount = 7;
    const auto amount = filter_value(invocation, "amount", 100);
    const auto effect_original = pixels;
    auto tinted = pixels;
    auto sepia_progress = filter_progress_phase(progress, 0, kPhaseCount);
    apply_child_filter(registry, "patchy.filters.sepia", tinted, invocation,
                       {{"amount", 100}}, &sepia_progress);
    auto tint_blend_progress = filter_progress_phase(progress, 1, kPhaseCount);
    blend_overlay_filter_pixels(pixels, tinted, 45, &tint_blend_progress);
    auto saturation_progress = filter_progress_phase(progress, 2, kPhaseCount);
    adjust_saturation_filter_pixels(pixels, 0.72, &saturation_progress);
    auto contrast_progress = filter_progress_phase(progress, 3, kPhaseCount);
    adjust_contrast_filter_pixels(pixels, 0.86, 112, &contrast_progress);
    auto fade_progress = filter_progress_phase(progress, 4, kPhaseCount);
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      report_filter_row_progress(&fade_progress, y, pixels.height());
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        auto *px = pixels.pixel(x, y);
        const auto *src = effect_original.pixel(x, y);
        for (std::uint16_t channel = 0; channel < 3; ++channel) {
          px[channel] = filter_clamp_byte(static_cast<int>(px[channel]) + 16);
          px[channel] = filter_blend_byte(px[channel], src[channel], 12);
        }
      }
    }
    finish_filter_row_progress(&fade_progress, pixels.height());
    auto grain_progress = filter_progress_phase(progress, 5, kPhaseCount);
    apply_child_filter(registry, "patchy.filters.film_grain", pixels,
                       invocation, {{"amount", 50}}, &grain_progress);
    auto amount_progress = filter_progress_phase(progress, 6, kPhaseCount);
    blend_filter_with_original(pixels, original, amount, &amount_progress);
    return;
  }

  if (identifier == "patchy.filters.sepia") {
    const auto amount = filter_value(invocation, "amount", 100);
    const auto total = pixels.height() * 2;
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      report_filter_row_progress(progress, y, pixels.height(), 0, total);
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        auto *px = pixels.pixel(x, y);
        const auto r = static_cast<int>(px[0]);
        const auto g = static_cast<int>(px[1]);
        const auto b = static_cast<int>(px[2]);
        px[0] = filter_clamp_byte((r * 393 + g * 769 + b * 189) / 1000);
        px[1] = filter_clamp_byte((r * 349 + g * 686 + b * 168) / 1000);
        px[2] = filter_clamp_byte((r * 272 + g * 534 + b * 131) / 1000);
      }
    }
    blend_filter_with_original(pixels, original, amount, progress,
                               pixels.height(), total);
    return;
  }

  if (identifier == "patchy.filters.threshold") {
    const auto threshold =
        std::clamp(filter_value(invocation, "threshold", 128), 0, 255);
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      report_filter_row_progress(progress, y, pixels.height());
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        auto *px = pixels.pixel(x, y);
        const auto value = filter_luminance(px) >= threshold ? 255 : 0;
        px[0] = static_cast<std::uint8_t>(value);
        px[1] = static_cast<std::uint8_t>(value);
        px[2] = static_cast<std::uint8_t>(value);
      }
    }
    finish_filter_row_progress(progress, pixels.height());
    return;
  }

  if (identifier == "patchy.filters.posterize") {
    const auto levels =
        std::clamp(filter_value(invocation, "levels", 4), 2, 16);
    const auto denominator = std::max(1, levels - 1);
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      report_filter_row_progress(progress, y, pixels.height());
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        auto *px = pixels.pixel(x, y);
        for (std::uint16_t channel = 0; channel < channels; ++channel) {
          const auto bucket = static_cast<int>(std::round(
              static_cast<double>(px[channel]) * denominator / 255.0));
          px[channel] = filter_clamp_byte(
              std::round(static_cast<double>(bucket) * 255.0 / denominator));
        }
      }
    }
    finish_filter_row_progress(progress, pixels.height());
    return;
  }

  if (identifier == "patchy.filters.box_blur" ||
      identifier == "patchy.filters.gaussian_blur") {
    const auto radius = std::clamp(
        filter_value(invocation, "radius",
                     identifier == "patchy.filters.gaussian_blur" ? 2 : 1),
        1, 12);
    apply_separable_tent_blur(pixels, original, radius,
                              identifier == "patchy.filters.gaussian_blur",
                              progress);
    return;
  }

  if (identifier == "patchy.filters.high_pass") {
    const auto radius = std::clamp(
        filter_number(invocation, "radius", 10.0), 0.1, 1000.0);
    apply_high_pass_filter_pixels(pixels, radius, progress);
    return;
  }

  if (identifier == "patchy.filters.median") {
    const auto radius = std::clamp(
        filter_number(invocation, "radius", 1.0), 1.0, 500.0);
    apply_median_filter_pixels(pixels, radius, progress);
    return;
  }

  if (identifier == "patchy.filters.dust_and_scratches") {
    const auto radius = std::clamp(
        filter_value(invocation, "radius", 1), 1, 100);
    const auto threshold = std::clamp(
        filter_value(invocation, "threshold", 0), 0, 255);
    apply_dust_and_scratches_filter_pixels(pixels, radius, threshold,
                                           progress);
    return;
  }

  if (identifier == "patchy.filters.surface_blur") {
    const auto radius = std::clamp(
        filter_number(invocation, "radius", 5.0), 1.0, 100.0);
    const auto threshold = std::clamp(
        filter_value(invocation, "threshold", 15), 2, 255);
    apply_surface_blur_filter_pixels(pixels, radius, threshold, progress);
    return;
  }

  if (identifier == "patchy.filters.plastic_wrap") {
    apply_plastic_wrap_filter_pixels(
        pixels,
        std::clamp(filter_value(invocation, "highlight_strength", 9), 0,
                   20),
        std::clamp(filter_value(invocation, "detail", 7), 1, 15),
        std::clamp(filter_value(invocation, "smoothness", 5), 1, 15),
        progress);
    return;
  }

  if (identifier == "patchy.filters.lens_blur") {
    apply_lens_blur_filter(
        pixels,
        std::clamp(filter_number(invocation, "radius", 15.0), 0.0, 100.0),
        std::clamp(filter_value(invocation, "blades", 6), 3, 8),
        std::clamp(filter_value(invocation, "blade_curvature", 50), 0, 100),
        std::clamp(filter_value(invocation, "rotation", 0), -180, 180),
        progress);
    return;
  }

  if (identifier == "patchy.filters.iris_blur") {
    apply_iris_blur_filter(
        pixels,
        std::clamp(filter_number(invocation, "blur", 15.0), 0.0, 100.0),
        std::clamp(filter_number(invocation, "center_x", 50.0), 0.0,
                   100.0),
        std::clamp(filter_number(invocation, "center_y", 50.0), 0.0,
                   100.0),
        std::clamp(filter_value(invocation, "angle", 0), -180, 180),
        std::clamp(filter_number(invocation, "iris_width", 50.0), 1.0,
                   200.0),
        std::clamp(filter_number(invocation, "iris_height", 40.0), 1.0,
                   200.0),
        std::clamp(filter_number(invocation, "focus", 50.0), 0.0, 100.0),
        progress);
    return;
  }

  if (identifier == "patchy.filters.tilt_shift_blur") {
    const auto blur = std::clamp(
        filter_number(invocation, "blur", 15.0), 0.0, 500.0);
    const auto center_x = std::clamp(
        filter_number(invocation, "center_x", 50.0), 0.0, 100.0);
    const auto center_y = std::clamp(
        filter_number(invocation, "center_y", 50.0), 0.0, 100.0);
    const auto angle = std::clamp(
        filter_value(invocation, "angle", 0), -180, 180);
    const auto focus_half_width = std::clamp(
        filter_number(invocation, "focus_half_width", 10.0), 0.0, 100.0);
    const auto transition_width = std::clamp(
        filter_number(invocation, "transition_width", 20.0), 0.0, 100.0);
    apply_tilt_shift_blur_filter(
        pixels, blur, center_x, center_y, angle, focus_half_width,
        transition_width, progress);
    return;
  }

  if (identifier == "patchy.filters.sharpen") {
    const auto amount =
        std::clamp(filter_value(invocation, "amount", 100), 0, 300);
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      report_filter_progress(progress, y, pixels.height(),
                             FilterProgressStage::Sharpening);
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        auto *dst = pixels.pixel(x, y);
        for (std::uint16_t channel = 0; channel < channels; ++channel) {
          const auto center =
              static_cast<int>(original.pixel(x, y)[channel]) * 5;
          const auto left =
              x > 0 ? static_cast<int>(original.pixel(x - 1, y)[channel])
                    : static_cast<int>(original.pixel(x, y)[channel]);
          const auto right =
              x + 1 < pixels.width()
                  ? static_cast<int>(original.pixel(x + 1, y)[channel])
                  : static_cast<int>(original.pixel(x, y)[channel]);
          const auto up =
              y > 0 ? static_cast<int>(original.pixel(x, y - 1)[channel])
                    : static_cast<int>(original.pixel(x, y)[channel]);
          const auto down =
              y + 1 < pixels.height()
                  ? static_cast<int>(original.pixel(x, y + 1)[channel])
                  : static_cast<int>(original.pixel(x, y)[channel]);
          const auto sharpened = center - left - right - up - down;
          dst[channel] = filter_clamp_byte(
              static_cast<int>(original.pixel(x, y)[channel]) +
              ((sharpened - static_cast<int>(original.pixel(x, y)[channel])) *
               amount) /
                  100);
        }
      }
    }
    report_filter_progress(progress, pixels.height(), pixels.height(),
                           FilterProgressStage::Sharpening);
    return;
  }

  if (identifier == "patchy.filters.unsharp_mask") {
    apply_unsharp_mask_to_pixels(
        pixels, original,
        std::clamp(filter_number(invocation, "amount", 150.0), 1.0, 500.0),
        std::clamp(filter_number(invocation, "radius", 2.0), 0.1, 1000.0),
        std::clamp(filter_value(invocation, "threshold", 8), 0, 255), progress);
    return;
  }

  if (identifier == "patchy.filters.motion_blur") {
    apply_motion_blur_to_pixels(
        pixels, original,
        std::clamp(filter_value(invocation, "angle", 0), -360, 360),
        std::clamp(filter_value(invocation, "distance", 12), 1, 999), progress);
    return;
  }

  if (identifier == "patchy.filters.radial_blur") {
    apply_radial_blur_to_pixels(
        pixels, original,
        std::clamp(filter_value(invocation, "amount", 35), 0, 100),
        std::clamp(filter_value(invocation, "samples", 16), 4, 32),
        std::clamp(filter_number(invocation, "center_x", 50.0), 0.0, 100.0),
        std::clamp(filter_number(invocation, "center_y", 50.0), 0.0, 100.0),
        progress);
    return;
  }

  if (identifier == "patchy.filters.edge_detect") {
    constexpr std::array<int, 9> sobel_x = {-1, 0, 1, -2, 0, 2, -1, 0, 1};
    constexpr std::array<int, 9> sobel_y = {-1, -2, -1, 0, 0, 0, 1, 2, 1};
    const auto strength =
        std::clamp(filter_value(invocation, "strength", 100), 0, 300);
    const auto luminance_at = [&original](std::int32_t x, std::int32_t y) {
      x = std::clamp<std::int32_t>(x, 0, original.width() - 1);
      y = std::clamp<std::int32_t>(y, 0, original.height() - 1);
      return filter_luminance(original.pixel(x, y));
    };
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      report_filter_progress(progress, y, pixels.height(),
                             FilterProgressStage::DetectingEdges);
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        int gx = 0;
        int gy = 0;
        int index = 0;
        for (int ky = -1; ky <= 1; ++ky) {
          for (int kx = -1; kx <= 1; ++kx) {
            const auto luminance = luminance_at(x + kx, y + ky);
            gx += luminance * sobel_x[static_cast<std::size_t>(index)];
            gy += luminance * sobel_y[static_cast<std::size_t>(index)];
            ++index;
          }
        }
        const auto magnitude =
            filter_clamp_byte(std::sqrt(gx * gx + gy * gy) *
                              static_cast<double>(strength) / 100.0);
        auto *px = pixels.pixel(x, y);
        px[0] = magnitude;
        px[1] = magnitude;
        px[2] = magnitude;
      }
    }
    report_filter_progress(progress, pixels.height(), pixels.height(),
                           FilterProgressStage::DetectingEdges);
    return;
  }

  if (identifier == "patchy.filters.glowing_edges") {
    apply_glowing_edges_to_pixels(
        pixels, original,
        std::clamp(filter_value(invocation, "edge_width", 2), 1, 12),
        std::clamp(filter_value(invocation, "brightness", 140), 0, 300),
        std::clamp(filter_value(invocation, "smoothness", 2), 0, 12), progress);
    return;
  }

  if (identifier == "patchy.filters.emboss") {
    apply_emboss_to_pixels(
        pixels, original,
        std::clamp(filter_value(invocation, "angle", 135), -180, 180),
        std::clamp(filter_value(invocation, "height", 2), 1, 24),
        std::clamp(filter_value(invocation, "amount", 100), 0, 300), progress);
    return;
  }

  if (identifier == "patchy.filters.twirl") {
    apply_twirl_to_pixels(
        pixels, original,
        std::clamp(filter_value(invocation, "angle", 180), -720, 720),
        std::clamp(filter_value(invocation, "radius", 100), 1, 100),
        std::clamp(filter_number(invocation, "center_x", 50.0), 0.0, 100.0),
        std::clamp(filter_number(invocation, "center_y", 50.0), 0.0, 100.0),
        progress);
    return;
  }

  if (identifier == "patchy.filters.wave") {
    apply_wave_to_pixels(
        pixels, original,
        std::clamp(filter_value(invocation, "amplitude", 12), 0, 64),
        std::clamp(filter_value(invocation, "wavelength", 48), 4, 256),
        std::clamp(filter_value(invocation, "phase", 0), 0, 360), progress);
    return;
  }

  if (identifier == "patchy.filters.pinch_bloat") {
    apply_pinch_bloat_to_pixels(
        pixels, original,
        std::clamp(filter_value(invocation, "amount", 35), -100, 100),
        std::clamp(filter_value(invocation, "radius", 100), 1, 100),
        std::clamp(filter_number(invocation, "center_x", 50.0), 0.0, 100.0),
        std::clamp(filter_number(invocation, "center_y", 50.0), 0.0, 100.0),
        progress);
    return;
  }

  if (identifier == "patchy.filters.clouds") {
    apply_clouds_to_pixels(
        pixels, invocation.foreground, invocation.background,
        std::clamp(filter_value(invocation, "scale", 96), 12, 512),
        std::clamp(filter_value(invocation, "detail", 6), 1, 8),
        std::clamp(filter_value(invocation, "contrast", 40), 0, 100),
        std::clamp(filter_value(invocation, "seed", 1), 1, 9999), progress);
    return;
  }

  if (identifier == "patchy.filters.pixelate") {
    const auto block_size =
        std::clamp(filter_value(invocation, "block_size", 4), 2, 32);
    for (std::int32_t block_y = 0; block_y < pixels.height();
         block_y += block_size) {
      report_filter_progress(progress, block_y, pixels.height(),
                             FilterProgressStage::Pixelating);
      for (std::int32_t block_x = 0; block_x < pixels.width();
           block_x += block_size) {
        const auto block_width = std::min(block_size, pixels.width() - block_x);
        const auto block_height =
            std::min(block_size, pixels.height() - block_y);
        FilterPixelAccum accum;
        for (std::int32_t y = block_y; y < block_y + block_height; ++y) {
          for (std::int32_t x = block_x; x < block_x + block_width; ++x) {
            filter_accumulate_pixel(accum, original, original.pixel(x, y), 1.0);
          }
        }
        for (std::int32_t y = block_y; y < block_y + block_height; ++y) {
          for (std::int32_t x = block_x; x < block_x + block_width; ++x) {
            filter_write_accumulated_pixel(pixels, x, y, accum);
          }
        }
      }
    }
    report_filter_progress(progress, pixels.height(), pixels.height(),
                           FilterProgressStage::Pixelating);
    return;
  }

  if (identifier == "patchy.filters.color_halftone") {
    apply_color_halftone_to_pixels(
        pixels, original,
        std::clamp(filter_value(invocation, "cell_size", 10), 4, 64),
        std::clamp(filter_value(invocation, "intensity", 75), 0, 100),
        std::clamp(filter_value(invocation, "contrast", 60), 0, 100), progress);
    return;
  }

  if (identifier == "patchy.filters.film_grain") {
    const auto amount =
        std::clamp(filter_value(invocation, "amount", 50), 0, 100);
    const auto amplitude =
        static_cast<int>(std::round(static_cast<double>(amount) * 0.3));
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      report_filter_progress(progress, y, pixels.height(),
                             FilterProgressStage::AddingGrain);
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        auto *px = pixels.pixel(x, y);
        for (std::uint16_t channel = 0; channel < channels; ++channel) {
          const auto span = amplitude * 2 + 1;
          const auto grain =
              span <= 1
                  ? 0
                  : static_cast<int>(filter_coordinate_hash(x, y, channel) %
                                     static_cast<std::uint32_t>(span)) -
                        amplitude;
          px[channel] =
              filter_clamp_byte(static_cast<int>(px[channel]) + grain);
        }
      }
    }
    report_filter_progress(progress, pixels.height(), pixels.height(),
                           FilterProgressStage::AddingGrain);
    return;
  }

  if (identifier == "patchy.filters.vignette") {
    const auto strength =
        std::clamp(filter_value(invocation, "strength", 55), 0, 100);
    const auto center_x = filter_center_coordinate(
        pixels.width(),
        std::clamp(filter_number(invocation, "center_x", 50.0), 0.0,
                   100.0));
    const auto center_y = filter_center_coordinate(
        pixels.height(),
        std::clamp(filter_number(invocation, "center_y", 50.0), 0.0,
                   100.0));
    const auto far_x =
        std::max(center_x, static_cast<double>(pixels.width() - 1) - center_x);
    const auto far_y = std::max(
        center_y, static_cast<double>(pixels.height() - 1) - center_y);
    const auto max_distance = std::sqrt(far_x * far_x + far_y * far_y);
    if (max_distance <= 0.0) {
      return;
    }
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      report_filter_progress(progress, y, pixels.height(),
                             FilterProgressStage::ApplyingVignette);
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        const auto dx = static_cast<double>(x) - center_x;
        const auto dy = static_cast<double>(y) - center_y;
        const auto distance = std::sqrt(dx * dx + dy * dy) / max_distance;
        const auto darken = 1.0 - (static_cast<double>(strength) / 100.0) *
                                      std::clamp(distance * distance, 0.0, 1.0);
        auto *px = pixels.pixel(x, y);
        for (std::uint16_t channel = 0; channel < channels; ++channel) {
          px[channel] =
              filter_clamp_byte(static_cast<double>(px[channel]) * darken);
        }
      }
    }
    report_filter_progress(progress, pixels.height(), pixels.height(),
                           FilterProgressStage::ApplyingVignette);
    return;
  }

  throw std::invalid_argument("Unknown catalogued filter identifier");
}

FilterParameterDefinition
integer_parameter(std::string key, std::string display_name,
                  std::string control_object_name, int minimum, int maximum,
                  int default_value,
                  FilterParameterUnit unit = FilterParameterUnit::None,
                  FilterSpatialScale spatial_scale = FilterSpatialScale::None,
                  FilterParameterPresentation presentation =
                      FilterParameterPresentation::Standard) {
  FilterParameterDefinition definition;
  definition.key = std::move(key);
  definition.display_name = std::move(display_name);
  definition.control_object_name = std::move(control_object_name);
  definition.kind = FilterParameterKind::Integer;
  definition.default_value = static_cast<std::int64_t>(default_value);
  definition.minimum = static_cast<double>(minimum);
  definition.maximum = static_cast<double>(maximum);
  definition.step = 1.0;
  definition.unit = unit;
  definition.spatial_scale = spatial_scale;
  definition.presentation = presentation;
  return definition;
}

FilterParameterDefinition
double_parameter(std::string key, std::string display_name,
                 std::string control_object_name, double minimum,
                 double maximum, double default_value, double step,
                 FilterParameterUnit unit = FilterParameterUnit::None,
                 FilterSpatialScale spatial_scale = FilterSpatialScale::None,
                 FilterParameterPresentation presentation =
                     FilterParameterPresentation::Standard) {
  FilterParameterDefinition definition;
  definition.key = std::move(key);
  definition.display_name = std::move(display_name);
  definition.control_object_name = std::move(control_object_name);
  definition.kind = FilterParameterKind::Double;
  definition.default_value = default_value;
  definition.minimum = minimum;
  definition.maximum = maximum;
  definition.step = step;
  definition.unit = unit;
  definition.spatial_scale = spatial_scale;
  definition.presentation = presentation;
  return definition;
}

FilterCatalogMetadata
catalog_metadata(FilterCategory category, bool adjustment_only,
                 std::vector<FilterParameterDefinition> parameters) {
  FilterCatalogMetadata metadata;
  metadata.category = category;
  metadata.adjustment_only = adjustment_only;
  metadata.schema_version = 1;
  metadata.parameters = std::move(parameters);
  metadata.execute = execute_builtin_filter;
  return metadata;
}

int catalog_integer(const FilterInvocation &invocation, std::string_view key,
                    int fallback) {
  return filter_value(invocation, key, fallback);
}

double catalog_number(const FilterInvocation &invocation, std::string_view key,
                      double fallback) {
  return filter_number(invocation, key, fallback);
}

int off_center_radial_blur_margin(const FilterInvocation &invocation,
                                  std::int32_t width,
                                  std::int32_t height) {
  const auto amount =
      std::clamp(catalog_integer(invocation, "amount", 35), 0, 100);
  if (amount <= 0 || width <= 0 || height <= 0) {
    return 0;
  }
  const auto samples =
      std::clamp(catalog_integer(invocation, "samples", 16), 4, 32);
  const auto center_x = filter_center_coordinate(
      width,
      std::clamp(catalog_number(invocation, "center_x", 50.0), 0.0, 100.0));
  const auto center_y = filter_center_coordinate(
      height,
      std::clamp(catalog_number(invocation, "center_y", 50.0), 0.0, 100.0));
  const auto sweep = static_cast<double>(amount) * 3.6 * kFilterPi / 180.0;
  const std::array<std::array<double, 2>, 4> corners{{
      {{0.0, 0.0}},
      {{static_cast<double>(width - 1), 0.0}},
      {{0.0, static_cast<double>(height - 1)}},
      {{static_cast<double>(width - 1), static_cast<double>(height - 1)}},
  }};
  double minimum_x = 0.0;
  double minimum_y = 0.0;
  double maximum_x = static_cast<double>(width - 1);
  double maximum_y = static_cast<double>(height - 1);
  for (int sample = 0; sample < samples; ++sample) {
    const auto t = static_cast<double>(sample) /
                       static_cast<double>(samples - 1) -
                   0.5;
    const auto angle = -sweep * t;
    const auto cosine = std::cos(angle);
    const auto sine = std::sin(angle);
    for (const auto &corner : corners) {
      const auto dx = corner[0] - center_x;
      const auto dy = corner[1] - center_y;
      const auto rotated_x = center_x + dx * cosine - dy * sine;
      const auto rotated_y = center_y + dx * sine + dy * cosine;
      minimum_x = std::min(minimum_x, rotated_x);
      minimum_y = std::min(minimum_y, rotated_y);
      maximum_x = std::max(maximum_x, rotated_x);
      maximum_y = std::max(maximum_y, rotated_y);
    }
  }
  const auto excess = std::max(
      {0.0, -minimum_x, -minimum_y,
       maximum_x - static_cast<double>(width - 1),
       maximum_y - static_cast<double>(height - 1)});
  if (excess <= 0.0) {
    return 0;
  }
  const auto required = std::ceil(excess) + 1.0;
  if (required >=
      static_cast<double>(std::numeric_limits<int>::max())) {
    // FilterRegistry::render() performs the final checked-dimension arithmetic
    // and reports an overflow instead of silently clipping a valid halo.
    return std::numeric_limits<int>::max();
  }
  return std::max(0, static_cast<int>(required));
}

} // namespace

void apply_lens_blur_filter(PixelBuffer &pixels, double radius_pixels,
                            int blade_count, int blade_curvature_percent,
                            int rotation_degrees,
                            const FilterProgress *progress) {
  if (pixels.format().bit_depth != BitDepth::UInt8) {
    throw std::invalid_argument("Lens Blur supports UInt8 buffers only");
  }
  if (pixels.format().channels < 3 || pixels.empty()) {
    report_filter_progress(progress, 1, 1, FilterProgressStage::Blurring);
    return;
  }
  if (!std::isfinite(radius_pixels)) {
    throw std::invalid_argument("Invalid Lens Blur settings");
  }
  radius_pixels = std::clamp(radius_pixels, 0.0, 100.0);
  blade_count = std::clamp(blade_count, 3, 8);
  blade_curvature_percent = std::clamp(blade_curvature_percent, 0, 100);
  rotation_degrees = std::clamp(rotation_degrees, -180, 180);
  if (radius_pixels <= 0.0) {
    report_filter_progress(progress, 1, 1, FilterProgressStage::Blurring);
    return;
  }

  // Patent boundary: this is one fixed aperture convolution selected entirely
  // by explicit numeric settings. It does not infer a depth map, detect or
  // boost highlights, classify content, or vary its kernel per pixel.
  const auto factor = std::max(
      1, static_cast<int>(std::ceil(
             radius_pixels / kMaximumFullResolutionApertureRadius)));
  const auto base_margin = static_cast<int>(std::ceil(radius_pixels)) +
                           factor + 1;
  const auto working_margin =
      ((base_margin + factor - 1) / factor) * factor;
  const auto original_width = pixels.width();
  const auto original_height = pixels.height();
  auto working = aperture_pad_source(pixels, working_margin);
  if (factor == 1) {
    const auto kernel = build_aperture_kernel(
        radius_pixels, blade_count, blade_curvature_percent,
        rotation_degrees);
    working = aperture_convolve(working, kernel, progress);
    pixels = aperture_crop_source(working, working_margin, original_width,
                                  original_height);
    return;
  }

  auto downsample_progress = filter_progress_phase(progress, 0, 3);
  auto proxy =
      aperture_downsample(working, factor, &downsample_progress);
  const auto kernel = build_aperture_kernel(
      radius_pixels / static_cast<double>(factor), blade_count,
      blade_curvature_percent, rotation_degrees);
  auto blur_progress = filter_progress_phase(progress, 1, 3);
  proxy = aperture_convolve(proxy, kernel, &blur_progress);
  auto upsample_progress = filter_progress_phase(progress, 2, 3);
  working = aperture_upsample(proxy, working.width(), working.height(), factor,
                              &upsample_progress);
  pixels = aperture_crop_source(working, working_margin, original_width,
                                original_height);
}

void apply_iris_blur_filter(PixelBuffer &pixels, double blur_pixels,
                            double center_x_percent,
                            double center_y_percent, int angle_degrees,
                            double iris_width_percent,
                            double iris_height_percent,
                            double focus_percent,
                            const FilterProgress *progress) {
  if (pixels.format().bit_depth != BitDepth::UInt8) {
    throw std::invalid_argument("Iris Blur supports UInt8 buffers only");
  }
  if (pixels.format().channels < 3 || pixels.empty()) {
    report_filter_progress(progress, 1, 1, FilterProgressStage::Blurring);
    return;
  }
  if (!std::isfinite(blur_pixels) || !std::isfinite(center_x_percent) ||
      !std::isfinite(center_y_percent) ||
      !std::isfinite(iris_width_percent) ||
      !std::isfinite(iris_height_percent) ||
      !std::isfinite(focus_percent)) {
    throw std::invalid_argument("Invalid Iris Blur settings");
  }
  blur_pixels = std::clamp(blur_pixels, 0.0, 100.0);
  center_x_percent = std::clamp(center_x_percent, 0.0, 100.0);
  center_y_percent = std::clamp(center_y_percent, 0.0, 100.0);
  angle_degrees = std::clamp(angle_degrees, -180, 180);
  iris_width_percent = std::clamp(iris_width_percent, 1.0, 200.0);
  iris_height_percent = std::clamp(iris_height_percent, 1.0, 200.0);
  focus_percent = std::clamp(focus_percent, 0.0, 100.0);
  if (blur_pixels <= 0.0) {
    report_filter_progress(progress, 1, 1, FilterProgressStage::Blurring);
    return;
  }

  // Patent boundary: one ellipse creates one scalar mix mask, and one fixed
  // blur is computed before that mask is applied. There are no multiple pins,
  // combined blur patterns, per-pixel variable-radius kernels, or bokeh boost.
  const auto source = pixels;
  auto blur_progress = filter_progress_phase(progress, 0, 2);
  apply_lens_blur_filter(pixels, blur_pixels, 8, 100, 0,
                         &blur_progress);

  constexpr std::int64_t kGeometryScale = 65536;
  constexpr std::int64_t kDirectionScale = 1LL << 20;
  constexpr std::uint64_t kMixScale = 65535U;
  const auto radians =
      static_cast<double>(angle_degrees) * kFilterPi / 180.0;
  const auto cosine = static_cast<std::int64_t>(
      std::llround(std::cos(radians) * kDirectionScale));
  const auto sine = static_cast<std::int64_t>(
      std::llround(std::sin(radians) * kDirectionScale));
  const auto center_x = static_cast<std::int64_t>(std::llround(
      filter_center_coordinate(pixels.width(), center_x_percent) *
      kGeometryScale));
  const auto center_y = static_cast<std::int64_t>(std::llround(
      filter_center_coordinate(pixels.height(), center_y_percent) *
      kGeometryScale));
  const auto radius_x = std::max<std::int64_t>(
      kGeometryScale,
      static_cast<std::int64_t>(std::llround(
          static_cast<double>(std::max(1, pixels.width())) *
          iris_width_percent * kGeometryScale / 200.0)));
  const auto radius_y = std::max<std::int64_t>(
      kGeometryScale,
      static_cast<std::int64_t>(std::llround(
          static_cast<double>(std::max(1, pixels.height())) *
          iris_height_percent * kGeometryScale / 200.0)));
  const auto focus = static_cast<std::uint64_t>(
      std::llround(focus_percent * static_cast<double>(kMixScale) / 100.0));
  const auto transition = kMixScale - focus;
  const auto smoothstep_denominator = kMixScale * kMixScale;
  const auto has_alpha = pixels.format().channels >= 4;
  auto blend_progress = filter_progress_phase(progress, 1, 2);
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    report_filter_progress(&blend_progress, y, pixels.height(),
                           FilterProgressStage::Blurring);
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto offset_x =
          static_cast<std::int64_t>(x) * kGeometryScale - center_x;
      const auto offset_y =
          static_cast<std::int64_t>(y) * kGeometryScale - center_y;
      const auto rotated_x =
          (offset_x * cosine + offset_y * sine) / kDirectionScale;
      const auto rotated_y =
          (-offset_x * sine + offset_y * cosine) / kDirectionScale;
      const auto normalized_x = static_cast<std::uint64_t>(
          (static_cast<std::uint64_t>(std::abs(rotated_x)) * kMixScale +
           static_cast<std::uint64_t>(radius_x) / 2U) /
          static_cast<std::uint64_t>(radius_x));
      const auto normalized_y = static_cast<std::uint64_t>(
          (static_cast<std::uint64_t>(std::abs(rotated_y)) * kMixScale +
           static_cast<std::uint64_t>(radius_y) / 2U) /
          static_cast<std::uint64_t>(radius_y));
      const auto distance = integer_square_root(
          normalized_x * normalized_x + normalized_y * normalized_y);
      std::uint64_t amount = 0U;
      if (distance > focus) {
        if (distance >= kMixScale || transition == 0U) {
          amount = kMixScale;
        } else {
          const auto position =
              ((distance - focus) * kMixScale + transition / 2U) /
              transition;
          amount =
              (position * position * (3U * kMixScale - 2U * position) +
               smoothstep_denominator / 2U) /
              smoothstep_denominator;
        }
      }
      if (amount == kMixScale) {
        continue;
      }
      auto *destination = pixels.pixel(x, y);
      const auto *original = source.pixel(x, y);
      if (amount == 0U) {
        std::copy(original, original + pixels.format().channels,
                  destination);
        continue;
      }
      const auto inverse = kMixScale - amount;
      const auto source_alpha = static_cast<std::uint64_t>(
          has_alpha ? original[3] : 255U);
      const auto blurred_alpha = static_cast<std::uint64_t>(
          has_alpha ? destination[3] : 255U);
      const auto alpha_numerator =
          source_alpha * inverse + blurred_alpha * amount;
      for (std::size_t channel = 0; channel < 3U; ++channel) {
        const auto premultiplied =
            static_cast<std::uint64_t>(original[channel]) * source_alpha *
                inverse +
            static_cast<std::uint64_t>(destination[channel]) *
                blurred_alpha * amount;
        destination[channel] =
            alpha_numerator == 0U
                ? 0U
                : static_cast<std::uint8_t>(std::min<std::uint64_t>(
                      255U,
                      (premultiplied + alpha_numerator / 2U) /
                          alpha_numerator));
      }
      if (has_alpha) {
        destination[3] = static_cast<std::uint8_t>(
            std::min<std::uint64_t>(
                255U, (alpha_numerator + kMixScale / 2U) / kMixScale));
      }
    }
  }
  report_filter_progress(&blend_progress, pixels.height(), pixels.height(),
                         FilterProgressStage::Blurring);
}

void apply_tilt_shift_blur_filter(PixelBuffer &pixels, double blur_pixels,
                                  double center_x_percent,
                                  double center_y_percent, int angle_degrees,
                                  double focus_half_width_percent,
                                  double transition_width_percent,
                                  const FilterProgress *progress) {
  if (pixels.format().bit_depth != BitDepth::UInt8) {
    throw std::invalid_argument(
        "Tilt-Shift Blur supports UInt8 buffers only");
  }
  if (pixels.format().channels < 3 || pixels.empty()) {
    report_filter_progress(progress, 1, 1, FilterProgressStage::Blurring);
    return;
  }
  if (!std::isfinite(blur_pixels) ||
      !std::isfinite(center_x_percent) ||
      !std::isfinite(center_y_percent) ||
      !std::isfinite(focus_half_width_percent) ||
      !std::isfinite(transition_width_percent)) {
    throw std::invalid_argument("Invalid Tilt-Shift Blur settings");
  }

  blur_pixels = std::clamp(blur_pixels, 0.0, 500.0);
  center_x_percent = std::clamp(center_x_percent, 0.0, 100.0);
  center_y_percent = std::clamp(center_y_percent, 0.0, 100.0);
  angle_degrees = std::clamp(angle_degrees, -180, 180);
  focus_half_width_percent =
      std::clamp(focus_half_width_percent, 0.0, 100.0);
  transition_width_percent =
      std::clamp(transition_width_percent, 0.0, 100.0);
  const auto maximum_radius_fixed = static_cast<std::uint64_t>(
      std::llround(blur_pixels * static_cast<double>(kTiltWeightScale)));
  if (maximum_radius_fixed == 0U) {
    report_filter_progress(progress, 1, 1, FilterProgressStage::Blurring);
    return;
  }

  std::vector<std::uint64_t> radii{0U};
  auto radius = std::min(kTiltWeightScale, maximum_radius_fixed);
  radii.push_back(radius);
  while (radius < maximum_radius_fixed) {
    radius = std::min(maximum_radius_fixed, radius * 2U);
    radii.push_back(radius);
  }
  const auto interval_count = static_cast<int>(radii.size() - 1U);
  const auto phase_count = 1 + interval_count * 2;
  auto mask_progress = filter_progress_phase(progress, 0, phase_count);

  const auto width = pixels.width();
  const auto height = pixels.height();
  constexpr auto kMaximumGeometryExtent =
      std::numeric_limits<std::int64_t>::max() /
      (2LL * kTiltGeometryScale * kTiltNormalScale);
  // The vertical moment prefix stores the Q8 horizontal result. One million
  // rows is already well above Patchy's document limit and keeps its largest
  // possible prefix below uint64_t's ceiling.
  constexpr std::int64_t kMaximumIntermediateExtent = 1'000'000;
  if (width > kMaximumGeometryExtent || height > kMaximumGeometryExtent ||
      height > kMaximumIntermediateExtent) {
    throw std::overflow_error("Tilt-Shift Blur geometry overflow");
  }
  const auto pixel_count = static_cast<std::uint64_t>(width) *
                           static_cast<std::uint64_t>(height);
  if (pixel_count > std::numeric_limits<std::size_t>::max() /
                        sizeof(std::uint16_t)) {
    throw std::overflow_error("Tilt-Shift Blur mask overflow");
  }
  std::vector<std::uint16_t> blur_mask(
      static_cast<std::size_t>(pixel_count));
  const auto angle = static_cast<double>(angle_degrees) * kFilterPi / 180.0;
  // Patchy's filter angle dial increases counterclockwise in image
  // coordinates, so the band tangent is (+cos, -sin) and its normal is
  // (+sin, +cos). Quantizing the normal once makes the geometry and smoothstep
  // mask byte-identical across the supported toolchains even if their libm
  // implementations differ in the final bits of sin/cos.
  const auto normal_x = static_cast<std::int64_t>(
      std::llround(std::sin(angle) * kTiltNormalScale));
  const auto normal_y = static_cast<std::int64_t>(
      std::llround(std::cos(angle) * kTiltNormalScale));
  const auto center_x = static_cast<std::int64_t>(std::llround(
      filter_center_coordinate(width, center_x_percent) *
      kTiltGeometryScale));
  const auto center_y = static_cast<std::int64_t>(std::llround(
      filter_center_coordinate(height, center_y_percent) *
      kTiltGeometryScale));
  const auto shorter_extent =
      static_cast<double>(std::max<std::int32_t>(1, std::min(width, height)));
  const auto focus_distance = static_cast<std::int64_t>(std::llround(
      shorter_extent * focus_half_width_percent * kTiltGeometryScale /
      100.0));
  const auto transition_distance = static_cast<std::int64_t>(std::llround(
      shorter_extent * transition_width_percent * kTiltGeometryScale /
      100.0));
  const auto smoothstep_denominator = kTiltMixScale * kTiltMixScale;
  std::uint16_t maximum_mask = 0U;
  for (std::int32_t y = 0; y < height; ++y) {
    report_filter_progress(&mask_progress, y, height,
                           FilterProgressStage::Blurring);
    for (std::int32_t x = 0; x < width; ++x) {
      const auto x_offset =
          static_cast<std::int64_t>(x) * kTiltGeometryScale - center_x;
      const auto y_offset =
          static_cast<std::int64_t>(y) * kTiltGeometryScale - center_y;
      const auto projection = x_offset * normal_x + y_offset * normal_y;
      const auto distance =
          (std::abs(projection) + kTiltNormalScale / 2) / kTiltNormalScale;
      std::uint64_t amount = 0U;
      if (distance > focus_distance) {
        if (transition_distance <= 0) {
          amount = kTiltMixScale;
        } else {
          const auto delta = std::min(distance - focus_distance,
                                      transition_distance);
          const auto position = std::min<std::uint64_t>(
              kTiltMixScale,
              (static_cast<std::uint64_t>(delta) * kTiltMixScale +
               static_cast<std::uint64_t>(transition_distance) / 2U) /
                  static_cast<std::uint64_t>(transition_distance));
          const auto smoothstep_numerator =
              position * position * (3U * kTiltMixScale - 2U * position);
          amount = (smoothstep_numerator + smoothstep_denominator / 2U) /
                   smoothstep_denominator;
        }
      }
      const auto quantized = static_cast<std::uint16_t>(amount);
      blur_mask[static_cast<std::size_t>(y) *
                    static_cast<std::size_t>(width) +
                static_cast<std::size_t>(x)] = quantized;
      maximum_mask = std::max(maximum_mask, quantized);
    }
  }
  report_filter_progress(&mask_progress, height, height,
                         FilterProgressStage::Blurring);
  if (maximum_mask == 0U) {
    report_filter_progress(progress, 1, 1, FilterProgressStage::Blurring);
    return;
  }

  const auto source = pixels;
  const PixelBuffer *low_level = &source;
  PixelBuffer owned_low_level;
  for (int interval = 0; interval < interval_count; ++interval) {
    auto level_progress =
        filter_progress_phase(progress, 1 + interval * 2, phase_count);
    auto high_level = tilt_tent_blur_level(
        source, radii[static_cast<std::size_t>(interval) + 1U],
        &level_progress);
    auto blend_progress =
        filter_progress_phase(progress, 2 + interval * 2, phase_count);
    tilt_blend_levels(
        pixels, *low_level, high_level, blur_mask, maximum_radius_fixed,
        radii[static_cast<std::size_t>(interval)],
        radii[static_cast<std::size_t>(interval) + 1U], &blend_progress);
    owned_low_level = std::move(high_level);
    low_level = &owned_low_level;
  }
  if (pixels.format().channels >= 4) {
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        auto *pixel = pixels.pixel(x, y);
        if (pixel[3] == 0U) {
          pixel[0] = 0U;
          pixel[1] = 0U;
          pixel[2] = 0U;
        }
      }
    }
  }
}

FilterCatalogMetadata builtin_filter_catalog(std::string_view identifier) {
  using Category = FilterCategory;
  using Presentation = FilterParameterPresentation;
  using Scale = FilterSpatialScale;
  using Unit = FilterParameterUnit;

  const auto amount = [](std::string label = "Amount",
                         int default_value = 100) {
    return integer_parameter("amount", std::move(label), "filterAmount", 0, 100,
                             default_value, Unit::Percent);
  };
  const auto center = [](std::string key, std::string label,
                         std::string object_name,
                         Presentation presentation) {
    return double_parameter(std::move(key), std::move(label),
                            std::move(object_name), 0.0, 100.0, 50.0, 0.1,
                            Unit::Percent, Scale::None, presentation);
  };

  FilterCatalogMetadata metadata;
  if (identifier == "patchy.filters.invert") {
    metadata = catalog_metadata(Category::Adjustment, true, {amount()});
  } else if (identifier == "patchy.filters.brightness_contrast") {
    metadata = catalog_metadata(
        Category::Adjustment, true,
        {integer_parameter("brightness", "Brightness", "filterBrightness", -100,
                           100, 0),
         integer_parameter("contrast", "Contrast", "filterContrast", -100, 100,
                           0, Unit::Percent)});
  } else if (identifier == "patchy.filters.grayscale" ||
             identifier == "patchy.filters.desaturate") {
    metadata = catalog_metadata(Category::Adjustment, true, {amount()});
  } else if (identifier == "patchy.filters.auto_contrast") {
    metadata = catalog_metadata(Category::Adjustment, true, {amount()});
  } else if (identifier == "patchy.filters.soft_glow") {
    metadata = catalog_metadata(Category::PhotoLooks, false, {amount("Glow")});
  } else if (identifier == "patchy.filters.punchy_color" ||
             identifier == "patchy.filters.cinematic_matte") {
    metadata =
        catalog_metadata(Category::PhotoLooks, false, {amount("Intensity")});
  } else if (identifier == "patchy.filters.noir") {
    metadata =
        catalog_metadata(Category::PhotoLooks, false, {amount("Contrast")});
  } else if (identifier == "patchy.filters.vintage_fade") {
    metadata = catalog_metadata(Category::PhotoLooks, false, {amount("Fade")});
  } else if (identifier == "patchy.filters.sepia") {
    metadata = catalog_metadata(Category::PhotoLooks, false, {amount()});
  } else if (identifier == "patchy.filters.threshold") {
    metadata =
        catalog_metadata(Category::Adjustment, true,
                         {integer_parameter("threshold", "Threshold",
                                            "filterThreshold", 0, 255, 128)});
  } else if (identifier == "patchy.filters.posterize") {
    metadata = catalog_metadata(
        Category::Adjustment, true,
        {integer_parameter("levels", "Levels", "filterLevels", 2, 16, 4)});
  } else if (identifier == "patchy.filters.box_blur") {
    metadata = catalog_metadata(
        Category::Blur, false,
        {integer_parameter("radius", "Radius", "filterRadius", 1, 12, 1,
                           Unit::Pixels, Scale::Pixels)});
  } else if (identifier == "patchy.filters.sharpen") {
    metadata =
        catalog_metadata(Category::Sharpen, false,
                         {integer_parameter("amount", "Amount", "filterAmount",
                                            0, 300, 100, Unit::Percent)});
  } else if (identifier == "patchy.filters.unsharp_mask") {
    auto radius =
        double_parameter("radius", "Radius", "filterRadius", 0.1, 1000.0, 2.0,
                         0.1, Unit::Pixels, Scale::Pixels);
    radius.practical_minimum = 0.1;
    radius.practical_maximum = 12.0;
    metadata =
        catalog_metadata(Category::Sharpen, false,
                         {integer_parameter("amount", "Amount", "filterAmount",
                                            1, 500, 150, Unit::Percent),
                          std::move(radius),
                          integer_parameter("threshold", "Threshold",
                                            "filterThreshold", 0, 255, 8)});
  } else if (identifier == "patchy.filters.high_pass") {
    auto radius = double_parameter("radius", "Radius", "filterRadius", 0.1,
                                   1000.0, 10.0, 0.1, Unit::Pixels,
                                   Scale::Pixels);
    radius.practical_minimum = 0.1;
    radius.practical_maximum = 12.0;
    metadata = catalog_metadata(
        Category::Sharpen, false, {std::move(radius)});
  } else if (identifier == "patchy.filters.median") {
    auto radius = double_parameter("radius", "Radius", "filterRadius", 1.0,
                                   500.0, 1.0, 0.01, Unit::Pixels,
                                   Scale::Pixels);
    radius.practical_minimum = 1.0;
    radius.practical_maximum = 25.0;
    metadata = catalog_metadata(Category::Noise, false,
                                {std::move(radius)});
  } else if (identifier == "patchy.filters.dust_and_scratches") {
    auto radius = integer_parameter("radius", "Radius", "filterRadius", 1,
                                    100, 1, Unit::Pixels, Scale::Pixels);
    radius.practical_minimum = 1.0;
    radius.practical_maximum = 25.0;
    metadata = catalog_metadata(
        Category::Noise, false,
        {std::move(radius),
         integer_parameter("threshold", "Threshold", "filterThreshold", 0,
                           255, 0)});
  } else if (identifier == "patchy.filters.surface_blur") {
    auto radius = double_parameter("radius", "Radius", "filterRadius", 1.0,
                                   100.0, 5.0, 0.01, Unit::Pixels,
                                   Scale::Pixels);
    radius.practical_minimum = 1.0;
    radius.practical_maximum = 25.0;
    metadata = catalog_metadata(
        Category::Blur, false,
        {std::move(radius),
         integer_parameter("threshold", "Threshold", "filterThreshold", 2,
                           255, 15)});
  } else if (identifier == "patchy.filters.plastic_wrap") {
    metadata = catalog_metadata(
        Category::Artistic, false,
        {integer_parameter("highlight_strength", "Highlight Strength",
                           "filterHighlightStrength", 0, 20, 9),
         integer_parameter("detail", "Detail", "filterDetail", 1, 15, 7),
         integer_parameter("smoothness", "Smoothness", "filterSmoothness",
                           1, 15, 5)});
  } else if (identifier == "patchy.filters.lens_blur") {
    auto radius = double_parameter("radius", "Radius", "filterRadius", 0.0,
                                   100.0, 15.0, 0.1, Unit::Pixels,
                                   Scale::Pixels);
    radius.practical_minimum = 0.0;
    radius.practical_maximum = 50.0;
    metadata = catalog_metadata(
        Category::Blur, false,
        {std::move(radius),
         integer_parameter("blades", "Blades", "filterBlades", 3, 8, 6),
         integer_parameter("blade_curvature", "Blade Curvature",
                           "filterBladeCurvature", 0, 100, 50,
                           Unit::Percent),
         integer_parameter("rotation", "Rotation", "filterRotation", -180,
                           180, 0, Unit::Degrees, Scale::None,
                           Presentation::Angle)});
  } else if (identifier == "patchy.filters.iris_blur") {
    auto blur = double_parameter("blur", "Blur", "filterBlur", 0.0, 100.0,
                                 15.0, 0.1, Unit::Pixels, Scale::Pixels);
    blur.practical_minimum = 0.0;
    blur.practical_maximum = 50.0;
    metadata = catalog_metadata(
        Category::Blur, false,
        {std::move(blur),
         center("center_x", "Center X", "filterCenterX",
                Presentation::CenterXPercent),
         center("center_y", "Center Y", "filterCenterY",
                Presentation::CenterYPercent),
         integer_parameter("angle", "Angle", "filterAngle", -180, 180, 0,
                           Unit::Degrees, Scale::None, Presentation::Angle),
         double_parameter("iris_width", "Iris Width", "filterIrisWidth",
                          1.0, 200.0, 50.0, 0.1, Unit::Percent, Scale::None,
                          Presentation::IrisWidthPercent),
         double_parameter("iris_height", "Iris Height", "filterIrisHeight",
                          1.0, 200.0, 40.0, 0.1, Unit::Percent, Scale::None,
                          Presentation::IrisHeightPercent),
         double_parameter("focus", "Focus", "filterFocus", 0.0, 100.0,
                          50.0, 0.1, Unit::Percent)});
  } else if (identifier == "patchy.filters.tilt_shift_blur") {
    auto blur = double_parameter("blur", "Blur", "filterBlur", 0.0, 500.0,
                                 15.0, 0.1, Unit::Pixels, Scale::Pixels);
    blur.practical_minimum = 0.0;
    blur.practical_maximum = 50.0;
    metadata = catalog_metadata(
        Category::Blur, false,
        {std::move(blur),
         center("center_x", "Center X", "filterCenterX",
                Presentation::CenterXPercent),
         center("center_y", "Center Y", "filterCenterY",
                Presentation::CenterYPercent),
         integer_parameter("angle", "Angle", "filterAngle", -180, 180, 0,
                           Unit::Degrees, Scale::None, Presentation::Angle),
         double_parameter(
             "focus_half_width", "Focus Half-Width", "filterFocusHalfWidth",
             0.0, 100.0, 10.0, 0.1, Unit::Percent, Scale::None,
             Presentation::TiltFocusHalfWidthPercent),
         double_parameter(
             "transition_width", "Transition Width", "filterTransitionWidth",
             0.0, 100.0, 20.0, 0.1, Unit::Percent, Scale::None,
             Presentation::TiltTransitionWidthPercent)});
  } else if (identifier == "patchy.filters.gaussian_blur") {
    metadata = catalog_metadata(
        Category::Blur, false,
        {integer_parameter("radius", "Radius", "filterRadius", 1, 12, 2,
                           Unit::Pixels, Scale::Pixels)});
  } else if (identifier == "patchy.filters.motion_blur") {
    auto angle =
        integer_parameter("angle", "Angle", "filterAngle", -360, 360, 0,
                          Unit::Degrees, Scale::None, Presentation::Angle);
    angle.practical_minimum = -180.0;
    angle.practical_maximum = 180.0;
    auto distance = integer_parameter("distance", "Distance", "filterDistance",
                                      1, 999, 12, Unit::Pixels, Scale::Pixels);
    distance.practical_minimum = 1.0;
    distance.practical_maximum = 64.0;
    metadata = catalog_metadata(Category::Blur, false,
                                {std::move(angle), std::move(distance)});
  } else if (identifier == "patchy.filters.radial_blur") {
    metadata = catalog_metadata(
        Category::Blur, false,
        {integer_parameter("amount", "Amount", "filterAmount", 0, 100, 35,
                           Unit::Percent),
         integer_parameter("samples", "Samples", "filterSamples", 4, 32, 16),
         center("center_x", "Center X", "filterCenterX",
                Presentation::CenterXPercent),
         center("center_y", "Center Y", "filterCenterY",
                Presentation::CenterYPercent)});
  } else if (identifier == "patchy.filters.edge_detect") {
    metadata = catalog_metadata(
        Category::Stylize, false,
        {integer_parameter("strength", "Strength", "filterStrength", 0, 300,
                           100, Unit::Percent)});
  } else if (identifier == "patchy.filters.emboss") {
    metadata = catalog_metadata(
        Category::Stylize, false,
        {integer_parameter("angle", "Angle", "filterAngle", -180, 180, 135,
                           Unit::Degrees, Scale::None, Presentation::Angle),
         integer_parameter("height", "Height", "filterHeight", 1, 24, 2,
                           Unit::Pixels, Scale::Pixels),
         integer_parameter("amount", "Amount", "filterDepth", 0, 300, 100,
                           Unit::Percent)});
  } else if (identifier == "patchy.filters.glowing_edges") {
    metadata = catalog_metadata(
        Category::Stylize, false,
        {integer_parameter("edge_width", "Edge Width", "filterEdgeWidth", 1, 12,
                           2, Unit::Pixels, Scale::Pixels),
         integer_parameter("brightness", "Brightness", "filterBrightness", 0,
                           300, 140, Unit::Percent),
         integer_parameter("smoothness", "Smoothness", "filterSmoothness", 0,
                           12, 2, Unit::Pixels, Scale::Pixels)});
  } else if (identifier == "patchy.filters.twirl") {
    metadata = catalog_metadata(
        Category::Distort, false,
        {integer_parameter("angle", "Angle", "filterAngle", -720, 720, 180,
                           Unit::Degrees, Scale::None, Presentation::Angle),
         integer_parameter("radius", "Radius", "filterRadius", 1, 100, 100,
                           Unit::Percent, Scale::None,
                           Presentation::EffectRadiusPercent),
         center("center_x", "Center X", "filterCenterX",
                Presentation::CenterXPercent),
         center("center_y", "Center Y", "filterCenterY",
                Presentation::CenterYPercent)});
  } else if (identifier == "patchy.filters.wave") {
    metadata = catalog_metadata(
        Category::Distort, false,
        {integer_parameter("amplitude", "Amplitude", "filterAmplitude", 0, 64,
                           12, Unit::Pixels, Scale::Pixels,
                           Presentation::WaveAmplitude),
         integer_parameter("wavelength", "Wavelength", "filterWavelength", 4,
                           256, 48, Unit::Pixels, Scale::Pixels,
                           Presentation::WaveWavelength),
         integer_parameter("phase", "Phase", "filterPhase", 0, 360, 0,
                           Unit::Degrees, Scale::None,
                           Presentation::WavePhase)});
  } else if (identifier == "patchy.filters.pinch_bloat") {
    metadata = catalog_metadata(
        Category::Distort, false,
        {integer_parameter("amount", "Amount", "filterAmount", -100, 100, 35,
                           Unit::Percent),
         integer_parameter("radius", "Radius", "filterRadius", 1, 100, 100,
                           Unit::Percent, Scale::None,
                           Presentation::EffectRadiusPercent),
         center("center_x", "Center X", "filterCenterX",
                Presentation::CenterXPercent),
         center("center_y", "Center Y", "filterCenterY",
                Presentation::CenterYPercent)});
  } else if (identifier == "patchy.filters.clouds") {
    metadata = catalog_metadata(
        Category::Render, false,
        {integer_parameter("scale", "Scale", "filterScale", 12, 512, 96,
                           Unit::Pixels, Scale::Pixels),
         integer_parameter("detail", "Detail", "filterDetail", 1, 8, 6),
         integer_parameter("contrast", "Contrast", "filterContrast", 0, 100, 40,
                           Unit::Percent),
         integer_parameter("seed", "Seed", "filterSeed", 1, 9999, 1)});
  } else if (identifier == "patchy.filters.pixelate") {
    metadata = catalog_metadata(
        Category::Pixelate, false,
        {integer_parameter("block_size", "Block Size", "filterBlockSize", 2, 32,
                           4, Unit::Pixels, Scale::Pixels)});
  } else if (identifier == "patchy.filters.color_halftone") {
    metadata = catalog_metadata(
        Category::Pixelate, false,
        {integer_parameter("cell_size", "Cell Size", "filterCellSize", 4, 64,
                           10, Unit::Pixels, Scale::Pixels),
         integer_parameter("intensity", "Intensity", "filterIntensity", 0, 100,
                           75, Unit::Percent),
         integer_parameter("contrast", "Contrast", "filterContrast", 0, 100, 60,
                           Unit::Percent)});
  } else if (identifier == "patchy.filters.film_grain") {
    metadata = catalog_metadata(Category::Noise, false, {amount("Amount", 50)});
  } else if (identifier == "patchy.filters.vignette") {
    metadata = catalog_metadata(
        Category::PhotoLooks, false,
        {integer_parameter("strength", "Strength", "filterStrength", 0, 100, 55,
                           Unit::Percent),
         center("center_x", "Center X", "filterCenterX",
                Presentation::CenterXPercent),
         center("center_y", "Center Y", "filterCenterY",
                Presentation::CenterYPercent)});
  } else {
    return {};
  }

  if (identifier == "patchy.filters.box_blur") {
    metadata.output_margin = [](const FilterInvocation &invocation,
                                std::int32_t, std::int32_t) {
      return std::clamp(catalog_integer(invocation, "radius", 1), 1, 12);
    };
    metadata.translation_support =
        [](const FilterInvocation &invocation) -> std::optional<int> {
      return std::clamp(catalog_integer(invocation, "radius", 1), 1, 12);
    };
  } else if (identifier == "patchy.filters.gaussian_blur") {
    metadata.output_margin = [](const FilterInvocation &invocation,
                                std::int32_t, std::int32_t) {
      return std::clamp(catalog_integer(invocation, "radius", 2), 1, 12);
    };
    metadata.translation_support =
        [](const FilterInvocation &invocation) -> std::optional<int> {
      return std::clamp(catalog_integer(invocation, "radius", 2), 1, 12);
    };
  } else if (identifier == "patchy.filters.unsharp_mask") {
    metadata.translation_support =
        [](const FilterInvocation &invocation) -> std::optional<int> {
      const auto radius =
          std::clamp(catalog_number(invocation, "radius", 2.0), 0.1, 1000.0);
      return std::max(1, static_cast<int>(std::ceil(radius * 3.0)));
    };
  } else if (identifier == "patchy.filters.high_pass") {
    metadata.translation_support =
        [](const FilterInvocation &invocation) -> std::optional<int> {
      const auto radius = std::clamp(
          catalog_number(invocation, "radius", 10.0), 0.1, 1000.0);
      return std::max(1, static_cast<int>(std::ceil(radius * 3.0)));
    };
  } else if (identifier == "patchy.filters.surface_blur") {
    metadata.output_margin = [](const FilterInvocation &invocation,
                                std::int32_t, std::int32_t) {
      const auto radius = std::clamp(
          catalog_number(invocation, "radius", 5.0), 1.0, 100.0);
      return std::max(1, static_cast<int>(std::floor(radius + 0.5)));
    };
  } else if (identifier == "patchy.filters.lens_blur" ||
             identifier == "patchy.filters.iris_blur") {
    const auto is_lens = identifier == "patchy.filters.lens_blur";
    metadata.output_margin = [is_lens](const FilterInvocation &invocation,
                                       std::int32_t, std::int32_t) {
      const auto radius = std::clamp(
          catalog_number(invocation, is_lens ? "radius" : "blur", 15.0),
          0.0, 100.0);
      if (radius <= 0.0) {
        return 0;
      }
      const auto factor = std::max(
          1, static_cast<int>(std::ceil(
                 radius / kMaximumFullResolutionApertureRadius)));
      const auto base_margin =
          static_cast<int>(std::ceil(radius)) + factor + 1;
      return ((base_margin + factor - 1) / factor) * factor;
    };
  } else if (identifier == "patchy.filters.tilt_shift_blur") {
    metadata.output_margin = [](const FilterInvocation &invocation,
                                std::int32_t, std::int32_t) {
      const auto blur = std::clamp(
          catalog_number(invocation, "blur", 15.0), 0.0, 500.0);
      return static_cast<int>(std::ceil(blur));
    };
  } else if (identifier == "patchy.filters.motion_blur") {
    metadata.output_margin = [](const FilterInvocation &invocation,
                                std::int32_t, std::int32_t) {
      return std::clamp(catalog_integer(invocation, "distance", 12), 1, 999);
    };
    metadata.translation_support =
        [](const FilterInvocation &invocation) -> std::optional<int> {
      return std::clamp(catalog_integer(invocation, "distance", 12), 1, 999) +
             1;
    };
  } else if (identifier == "patchy.filters.radial_blur") {
    metadata.output_margin = [](const FilterInvocation &invocation,
                                std::int32_t width, std::int32_t height) {
      const auto center_x =
          std::clamp(catalog_number(invocation, "center_x", 50.0), 0.0, 100.0);
      const auto center_y =
          std::clamp(catalog_number(invocation, "center_y", 50.0), 0.0, 100.0);
      if (center_x != 50.0 || center_y != 50.0) {
        return off_center_radial_blur_margin(invocation, width, height);
      }

      // Keep the historical centered calculation byte-for-byte: default
      // invocations and old files must retain their existing growth and pixels.
      const auto amount =
          std::clamp(catalog_integer(invocation, "amount", 35), 0, 100);
      if (amount <= 0) {
        return 0;
      }
      const auto diagonal =
          std::sqrt(static_cast<double>(width) * static_cast<double>(width) +
                    static_cast<double>(height) * static_cast<double>(height));
      const auto reach =
          (diagonal - static_cast<double>(std::min(width, height))) * 0.5 *
          static_cast<double>(amount) / 100.0;
      return std::clamp(static_cast<int>(std::ceil(reach)), 0, 256);
    };
  } else if (identifier == "patchy.filters.sharpen" ||
             identifier == "patchy.filters.edge_detect") {
    metadata.translation_support =
        [](const FilterInvocation &) -> std::optional<int> { return 1; };
  }

  return metadata;
}

} // namespace patchy
