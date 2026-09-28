#include "filters/smart_filter_renderer.hpp"

#include "core/blend_math.hpp"
#include "core/rect_utils.hpp"
#include "core/worker_budget.hpp"
#include "filters/filter_support.hpp"
#include "support/translate_noop.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <future>
#include <limits>
#include <numeric>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace patchy {

namespace {

constexpr double kMinimumGaussianRadius = 0.1;
constexpr double kMaximumGaussianRadius = 1000.0;
constexpr double kMinimumMedianRadius = 1.0;
constexpr double kMaximumMedianRadius = 500.0;
constexpr std::int32_t kMinimumDustAndScratchesRadius = 1;
constexpr std::int32_t kMaximumDustAndScratchesRadius = 500;
constexpr std::int32_t kMinimumDustAndScratchesThreshold = 0;
constexpr std::int32_t kMaximumDustAndScratchesThreshold = 255;
constexpr double kMinimumSurfaceBlurRadius = 1.0;
constexpr double kMaximumSurfaceBlurRadius = 100.0;
constexpr std::int32_t kMinimumSurfaceBlurThreshold = 2;
constexpr std::int32_t kMaximumSurfaceBlurThreshold = 255;
constexpr double kMinimumUnsharpMaskAmount = 1.0;
constexpr double kMaximumUnsharpMaskAmount = 500.0;
constexpr std::int32_t kMinimumUnsharpMaskThreshold = 0;
constexpr std::int32_t kMaximumUnsharpMaskThreshold = 255;
constexpr std::int32_t kMinimumMotionBlurAngle = -360;
constexpr std::int32_t kMaximumMotionBlurAngle = 360;
constexpr std::int32_t kMinimumMotionBlurDistance = 1;
constexpr std::int32_t kMaximumMotionBlurDistance = 2000;
constexpr std::int32_t kMinimumPlasticWrapHighlightStrength = 0;
constexpr std::int32_t kMaximumPlasticWrapHighlightStrength = 20;
constexpr std::int32_t kMinimumPlasticWrapDetail = 1;
constexpr std::int32_t kMaximumPlasticWrapDetail = 15;
constexpr std::int32_t kMinimumPlasticWrapSmoothness = 1;
constexpr std::int32_t kMaximumPlasticWrapSmoothness = 15;
constexpr std::int32_t kMinimumMosaicCellSize = 2;
constexpr std::int32_t kMaximumMosaicCellSize = 200;
constexpr std::int32_t kMinimumEmbossAngle = -360;
constexpr std::int32_t kMaximumEmbossAngle = 360;
constexpr std::int32_t kMinimumEmbossHeight = 1;
constexpr std::int32_t kMaximumEmbossHeight = 100;
constexpr std::int32_t kMinimumEmbossAmount = 1;
constexpr std::int32_t kMaximumEmbossAmount = 500;
constexpr double kMinimumBoxBlurRadius = 1.0;
constexpr double kMaximumBoxBlurRadius = 2000.0;
constexpr std::int32_t kMinimumRadialBlurAmount = 1;
constexpr std::int32_t kMaximumRadialBlurAmount = 100;
constexpr double kMinimumAddNoiseAmount = 0.1;
constexpr double kMaximumAddNoiseAmount = 400.0;
constexpr std::int32_t kMinimumAddNoiseSeed = 0;
constexpr std::int32_t kMaximumAddNoiseSeed = 999999999;
constexpr std::int32_t kBoxBlurDirectMaximumRadius = 12;
constexpr double kGaussianMarginScale = 3.0;
constexpr double kDirectGaussianMaximumRadius = 8.0;
constexpr int kProgressScale = 1000000;
constexpr std::int32_t kSurfaceBlurDirectMaximumRadius = 8;

void report_progress(const FilterProgress *progress, int completed, int total,
                     FilterProgressStage stage) {
  if (progress == nullptr || !progress->update) {
    return;
  }
  const auto safe_total = std::max(1, total);
  if (!progress->update(std::clamp(completed, 0, safe_total), safe_total,
                        stage)) {
    throw FilterCancelled();
  }
}

void report_fraction(const FilterProgress *progress, std::uint64_t completed,
                     std::uint64_t total, FilterProgressStage stage) {
  if (progress == nullptr || !progress->update) {
    return;
  }
  const auto safe_total = std::max<std::uint64_t>(1U, total);
  const auto safe_completed = std::min(completed, safe_total);
  const auto scaled = static_cast<int>(
      (safe_completed * static_cast<std::uint64_t>(kProgressScale)) /
      safe_total);
  report_progress(progress, scaled, kProgressScale, stage);
}

[[nodiscard]] FilterProgress phase_progress(const FilterProgress *progress,
                                            int phase_index, int phase_count) {
  if (progress == nullptr || !progress->update) {
    return {};
  }
  return FilterProgress{[progress, phase_index,
                         phase_count](int completed, int total,
                                      FilterProgressStage stage) {
    const auto safe_phase_count = std::max(1, phase_count);
    const auto phase_scale =
        std::max(1, std::min(kProgressScale, std::numeric_limits<int>::max() /
                                                 safe_phase_count));
    const auto safe_total = std::max(1, total);
    const auto safe_completed = std::clamp(completed, 0, safe_total);
    const auto phase_completed =
        (static_cast<std::int64_t>(safe_completed) * phase_scale) / safe_total;
    const auto combined = static_cast<std::int64_t>(std::clamp(
                              phase_index, 0, safe_phase_count - 1)) *
                              phase_scale +
                          phase_completed;
    return progress->update(
        static_cast<int>(std::min<std::int64_t>(
            combined,
            static_cast<std::int64_t>(safe_phase_count) * phase_scale)),
        safe_phase_count * phase_scale, stage);
  }};
}

[[nodiscard]] const std::uint8_t *
sample_result(const FilterRenderResult &result, std::int32_t document_x,
              std::int32_t document_y) noexcept {
  const auto local_x = static_cast<std::int64_t>(document_x) - result.bounds.x;
  const auto local_y = static_cast<std::int64_t>(document_y) - result.bounds.y;
  if (local_x < 0 || local_y < 0 || local_x >= result.bounds.width ||
      local_y >= result.bounds.height) {
    return nullptr;
  }
  return result.pixels.pixel(static_cast<std::int32_t>(local_x),
                             static_cast<std::int32_t>(local_y));
}

[[nodiscard]] bool equal_rect(Rect left, Rect right) noexcept {
  return left.x == right.x && left.y == right.y &&
         left.width == right.width && left.height == right.height;
}

[[nodiscard]] FilterRenderResult
embed_in_filter_canvas(const PixelBuffer &placed_pixels, Rect placed_bounds,
                       Rect filter_canvas_bounds) {
  if (filter_canvas_bounds.width <= 0 || filter_canvas_bounds.height <= 0) {
    throw std::invalid_argument(PATCHY_TRANSLATE_NOOP("QObject", "Smart Filter canvas bounds are empty"));
  }
  if (equal_rect(placed_bounds, filter_canvas_bounds)) {
    return FilterRenderResult{placed_pixels, placed_bounds};
  }

  PixelBuffer canvas(filter_canvas_bounds.width, filter_canvas_bounds.height,
                     PixelFormat::rgba8());
  canvas.clear(0);
  const auto copied_bounds = intersect_rect(placed_bounds, filter_canvas_bounds);
  if (!copied_bounds.empty()) {
    const auto source_x = copied_bounds.x - placed_bounds.x;
    const auto source_y = copied_bounds.y - placed_bounds.y;
    const auto destination_x = copied_bounds.x - filter_canvas_bounds.x;
    const auto destination_y = copied_bounds.y - filter_canvas_bounds.y;
    const auto row_bytes = static_cast<std::size_t>(copied_bounds.width) * 4U;
    for (std::int32_t y = 0; y < copied_bounds.height; ++y) {
      const auto *source = placed_pixels.pixel(source_x, source_y + y);
      auto *destination = canvas.pixel(destination_x, destination_y + y);
      std::copy(source, source + row_bytes, destination);
    }
  }
  return FilterRenderResult{std::move(canvas), filter_canvas_bounds};
}

[[nodiscard]] std::uint8_t rounded_byte(double value) noexcept {
  if (!(value > 0.0)) {
    return 0;
  }
  if (value >= 255.0) {
    return 255;
  }
  return static_cast<std::uint8_t>(std::floor(value + 0.5));
}

struct GaussianLinePlan {
  bool direct{false};
  std::vector<double> kernel;
  double gain{1.0};
  double coefficient1{0.0};
  double coefficient2{0.0};
  double coefficient3{0.0};
};

[[nodiscard]] GaussianLinePlan make_gaussian_line_plan(double radius,
                                                       int margin) {
  GaussianLinePlan plan;
  if (radius <= kDirectGaussianMaximumRadius) {
    plan.direct = true;
    plan.kernel.resize(static_cast<std::size_t>(margin) * 2U + 1U);
    struct Calibration {
      double radius;
      std::vector<double> weights;
    };
    // Photoshop 27.8 COM captures of a one-pixel vertical line. Interpolating
    // the measured kernels keeps every captured radius exact after byte
    // rounding and avoids the pronounced small-radius error of a point-sampled
    // Gaussian (notably radius 0.5). See docs/ps-compat.md.
    static const std::vector<Calibration> kCalibrations{
        {0.1, {255}},
        {0.2, {9, 237, 9}},
        {0.25, {24, 207, 24}},
        {0.3, {36, 183, 36}},
        {0.35, {44, 167, 44}},
        {0.4, {49, 157, 49}},
        {0.45, {52, 151, 52}},
        {0.49, {54, 147, 54}},
        {0.5, {55, 145, 55}},
        {0.51, {1, 55, 143, 55, 1}},
        {0.6, {3, 58, 133, 58, 3}},
        {0.7, {7, 60, 122, 60, 7}},
        {0.8, {11, 60, 114, 60, 11}},
        {0.9, {1, 14, 60, 106, 60, 14, 1}},
        {1.0, {2, 18, 60, 96, 60, 18, 2}},
        {1.1, {3, 21, 59, 90, 59, 21, 3}},
        {1.5, {2, 10, 28, 52, 72, 52, 28, 10, 2}},
        {2.0, {2, 7, 17, 30, 43, 58, 43, 30, 17, 7, 2}},
        {2.5, {1, 5, 11, 20, 31, 39, 42, 39, 31, 20, 11, 5, 1}},
        {3.0, {1, 2, 5, 9, 14, 21, 27, 32, 34, 32, 27, 21, 14, 9, 5, 2, 1}},
        {4.0, {1, 2, 4, 6, 9, 12, 16, 19, 22, 24, 24, 24, 22, 19, 16, 12, 9, 6, 4, 2, 1}},
        // The sub-byte tails round to zero for the one-pixel line capture, but
        // their cumulative radius-4.5 step response reaches alpha 1 twelve
        // pixels outside a broad opaque region. Keep them as doubles so both
        // Photoshop captures retain their observed support.
        {4.5, {0.08, 0.16, 0.40, 1, 2, 3, 5, 7, 9, 12, 16, 19,
               21, 22, 22.3, 22, 21, 19, 16, 12, 9, 7, 5, 3, 2, 1,
               0.40, 0.16, 0.08}},
        {8.0, {1, 1, 1, 2, 2, 3, 4, 4, 5, 6, 7, 8, 9, 10, 11, 11, 12, 12, 13, 13, 13, 12, 12, 11, 11, 10, 9, 8, 7, 6, 5, 4, 4, 3, 2, 2, 1, 1, 1}},
    };
    const auto upper = std::lower_bound(
        kCalibrations.begin(), kCalibrations.end(), radius,
        [](const Calibration& calibration, double value) {
          return calibration.radius < value;
        });
    const auto& high = upper == kCalibrations.end()
                           ? kCalibrations.back()
                           : *upper;
    const auto& low = upper == kCalibrations.begin() ? *upper : *(upper - 1);
    const auto span = high.radius - low.radius;
    const auto mix = span <= 0.0 ? 0.0 : (radius - low.radius) / span;
    const auto calibrated_weight = [&](const Calibration& calibration,
                                       int offset) {
      const auto support =
          static_cast<int>(calibration.weights.size() / 2U);
      return offset < -support || offset > support
                 ? 0.0
                 : static_cast<double>(calibration.weights[
                       static_cast<std::size_t>(offset + support)]);
    };
    double sum = 0.0;
    for (int offset = -margin; offset <= margin; ++offset) {
      const auto weight =
          calibrated_weight(low, offset) * (1.0 - mix) +
          calibrated_weight(high, offset) * mix;
      plan.kernel[static_cast<std::size_t>(offset + margin)] = weight;
      sum += weight;
    }
    if (!std::isfinite(sum) || sum <= 0.0) {
      throw std::invalid_argument(PATCHY_TRANSLATE_NOOP("QObject", "Invalid Gaussian Smart Filter radius"));
    }
    for (auto &weight : plan.kernel) {
      weight /= sum;
    }
    return plan;
  }

  // Young and van Vliet's stable third-order recursive approximation keeps
  // large Photoshop radii linear in the number of output pixels. The input and
  // recursive boundary state repeat the filter-canvas edge, matching the COM
  // captures rather than injecting transparent black.
  const auto q = radius >= 2.5
                     ? 0.98711 * radius - 0.96330
                     : 3.97156 - 4.14554 * std::sqrt(1.0 - 0.26891 * radius);
  const auto q2 = q * q;
  const auto q3 = q2 * q;
  const auto b0 = 1.57825 + 2.44413 * q + 1.4281 * q2 + 0.422205 * q3;
  plan.coefficient1 = (2.44413 * q + 2.85619 * q2 + 1.26661 * q3) / b0;
  plan.coefficient2 = -(1.4281 * q2 + 1.26661 * q3) / b0;
  plan.coefficient3 = 0.422205 * q3 / b0;
  plan.gain = 1.0 - plan.coefficient1 - plan.coefficient2 - plan.coefficient3;
  if (!std::isfinite(plan.gain) || !std::isfinite(plan.coefficient1) ||
      !std::isfinite(plan.coefficient2) || !std::isfinite(plan.coefficient3) ||
      plan.gain <= 0.0) {
    throw std::invalid_argument(PATCHY_TRANSLATE_NOOP("QObject", "Invalid Gaussian Smart Filter radius"));
  }
  return plan;
}

[[nodiscard]] GaussianLinePlan make_high_pass_line_plan(double radius,
                                                        int margin) {
  if (radius < 8.0 || radius > 12.0) {
    return make_gaussian_line_plan(radius, margin);
  }
  struct Calibration {
    double radius;
    std::span<const double> weights;
  };
  static constexpr std::array<double, 39> kRadius8Weights{
      1, 1, 1, 2, 2, 3, 4, 4, 5, 6, 7, 8, 9, 10, 11, 11,
      12, 12, 13, 13, 13, 12, 12, 11, 11, 10, 9, 8, 7, 6, 5, 4,
      4, 3, 2, 2, 1, 1, 1};
  static constexpr std::array<double, 47> kRadius10Weights{
      1, 1, 1, 1, 2, 2, 2, 3, 3, 4, 5, 5, 6, 6, 7, 7,
      8, 8, 9, 9, 10, 10, 10, 10, 10, 10, 10, 9, 9, 8, 8, 7,
      7, 6, 6, 5, 5, 4, 3, 3, 2, 2, 2, 1, 1, 1, 1};
  static constexpr std::array<double, 51> kRadius11Weights{
      1, 1, 1, 1, 1, 2, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7,
      7, 7, 8, 8, 8, 9, 9, 9, 9, 9, 9, 9, 8, 8, 8, 7, 7,
      7, 6, 6, 5, 5, 4, 4, 3, 3, 2, 2, 2, 1, 1, 1, 1, 1};
  static constexpr std::array<double, 57> kRadius12Weights{
      1, 1, 1, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 5, 6,
      6, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 8, 8, 8, 7, 7, 7, 7, 6,
      6, 5, 5, 5, 4, 4, 4, 3, 3, 3, 2, 2, 2, 1, 1, 1, 1, 1, 1};
  static constexpr std::array<Calibration, 4> kCalibrations{{
      {8.0, kRadius8Weights},
      {10.0, kRadius10Weights},
      {11.0, kRadius11Weights},
      {12.0, kRadius12Weights},
  }};
  const auto upper = std::lower_bound(
      kCalibrations.begin(), kCalibrations.end(), radius,
      [](const Calibration &calibration, double value) {
        return calibration.radius < value;
      });
  const auto &high = upper == kCalibrations.end()
                         ? kCalibrations.back()
                         : *upper;
  const auto &low = upper == kCalibrations.begin() ? *upper : *(upper - 1);
  const auto low_sum =
      std::accumulate(low.weights.begin(), low.weights.end(), 0.0);
  const auto high_sum =
      std::accumulate(high.weights.begin(), high.weights.end(), 0.0);
  const auto normalized_weight = [](const Calibration &calibration,
                                    double sum, std::ptrdiff_t offset) {
    const auto support =
        static_cast<std::ptrdiff_t>(calibration.weights.size() / 2U);
    if (offset < -support || offset > support) {
      return 0.0;
    }
    return calibration.weights[static_cast<std::size_t>(offset + support)] /
           sum;
  };
  const auto span = high.radius - low.radius;
  const auto mix = span <= 0.0 ? 0.0 : (radius - low.radius) / span;
  GaussianLinePlan plan;
  plan.direct = true;
  const auto support = static_cast<std::ptrdiff_t>(
      std::max(low.weights.size(), high.weights.size()) / 2U);
  plan.kernel.assign(static_cast<std::size_t>(support) * 2U + 1U, 0.0);
  for (std::ptrdiff_t offset = -support; offset <= support; ++offset) {
    plan.kernel[static_cast<std::size_t>(offset + support)] =
        normalized_weight(low, low_sum, offset) * (1.0 - mix) +
        normalized_weight(high, high_sum, offset) * mix;
  }
  return plan;
}

[[nodiscard]] GaussianLinePlan make_unsharp_line_plan(double radius,
                                                      int margin) {
  if (radius != 2.5) {
    return make_gaussian_line_plan(radius, margin);
  }
  // Photoshop's Unsharp Mask uses a separately quantized radius-2.5 low-pass
  // kernel rather than the Gaussian Blur filter's radius-2.5 kernel. The
  // weights below are recovered from opaque one-pixel-line captures and sum
  // to 256, including the otherwise easy-to-miss two-count outer taps.
  static constexpr std::array<double, 13> kRadius2_5Weights{
      2, 4, 12, 20, 30, 38, 44, 38, 30, 20, 12, 4, 2};
  GaussianLinePlan plan;
  plan.direct = true;
  plan.kernel.assign(static_cast<std::size_t>(margin) * 2U + 1U, 0.0);
  constexpr auto kSum = 256.0;
  constexpr auto kSupport =
      static_cast<std::ptrdiff_t>(kRadius2_5Weights.size() / 2U);
  for (std::ptrdiff_t offset = -kSupport; offset <= kSupport; ++offset) {
    plan.kernel[static_cast<std::size_t>(offset + margin)] =
        kRadius2_5Weights[static_cast<std::size_t>(offset + kSupport)] / kSum;
  }
  return plan;
}

void filter_gaussian_line(std::vector<double> &values,
                          std::vector<double> &scratch,
                          const GaussianLinePlan &plan) {
  const auto count = values.size();
  scratch.resize(count);
  if (plan.direct) {
    const auto radius = static_cast<std::ptrdiff_t>(plan.kernel.size() / 2U);
    for (std::size_t index = 0; index < count; ++index) {
      double value = 0.0;
      for (std::ptrdiff_t offset = -radius; offset <= radius; ++offset) {
        const auto source = std::clamp<std::ptrdiff_t>(
            static_cast<std::ptrdiff_t>(index) + offset, 0,
            static_cast<std::ptrdiff_t>(count) - 1);
        value += values[static_cast<std::size_t>(source)] *
                 plan.kernel[static_cast<std::size_t>(offset + radius)];
      }
      scratch[index] = value;
    }
    values.swap(scratch);
    return;
  }

  for (std::size_t index = 0; index < count; ++index) {
    const auto previous1 = index >= 1U ? scratch[index - 1U] : values.front();
    const auto previous2 = index >= 2U ? scratch[index - 2U] : values.front();
    const auto previous3 = index >= 3U ? scratch[index - 3U] : values.front();
    scratch[index] = plan.gain * values[index] + plan.coefficient1 * previous1 +
                     plan.coefficient2 * previous2 +
                     plan.coefficient3 * previous3;
  }
  for (std::size_t reverse = count; reverse > 0U; --reverse) {
    const auto index = reverse - 1U;
    const auto following1 =
        index + 1U < count ? values[index + 1U] : scratch.back();
    const auto following2 =
        index + 2U < count ? values[index + 2U] : scratch.back();
    const auto following3 =
        index + 3U < count ? values[index + 3U] : scratch.back();
    values[index] =
        plan.gain * scratch[index] + plan.coefficient1 * following1 +
        plan.coefficient2 * following2 + plan.coefficient3 * following3;
  }
}

[[nodiscard]] FilterRenderResult
render_gaussian(const FilterRenderResult &input, double radius,
                const FilterProgress *progress) {
  const auto margin =
      static_cast<int>(std::ceil(kGaussianMarginScale * radius));
  // Photoshop filters within the document-space FEid cache canvas. Production
  // callers embed the placed raster into that transparent canvas first, so the
  // blur may grow beyond the placed bounds but never beyond the cache canvas.
  // Samples outside the cache canvas repeat its nearest edge pixel.
  const auto bounds = input.bounds;
  const auto pixel_count = static_cast<std::uint64_t>(bounds.width) *
                           static_cast<std::uint64_t>(bounds.height);
  if (pixel_count > std::numeric_limits<std::size_t>::max() / sizeof(float)) {
    throw std::overflow_error("Smart Filter working buffer overflow");
  }

  const auto plan = make_gaussian_line_plan(radius, margin);
  PixelBuffer output(bounds.width, bounds.height, PixelFormat::rgba8());
  output.clear(0);
  std::vector<float> horizontal(static_cast<std::size_t>(pixel_count));
  std::vector<double> values;
  std::vector<double> scratch;

  const std::array<int, 4> channels{3, 0, 1, 2};
  const auto source_data = input.pixels.data();
  auto output_data = output.data();
  const auto source_width = input.pixels.width();
  const auto width = bounds.width;
  const auto height = bounds.height;
  const auto total_lines =
      static_cast<std::uint64_t>(channels.size()) *
      (static_cast<std::uint64_t>(width) + static_cast<std::uint64_t>(height));
  std::uint64_t completed_lines = 0U;

  for (const auto channel : channels) {
    values.resize(static_cast<std::size_t>(width));
    scratch.resize(static_cast<std::size_t>(width));
    for (std::int32_t y = 0; y < height; ++y) {
      report_fraction(progress, completed_lines++, total_lines,
                      FilterProgressStage::Blurring);
      const auto source_y = y;
      for (std::int32_t x = 0; x < width; ++x) {
        const auto source_x = x;
        const auto source_offset =
            (static_cast<std::size_t>(source_y) *
                 static_cast<std::size_t>(source_width) +
             static_cast<std::size_t>(source_x)) *
            4U;
        const auto alpha = source_data[source_offset + 3U];
        values[static_cast<std::size_t>(x)] =
            channel == 3
                ? static_cast<double>(alpha)
                : static_cast<double>(
                      source_data[source_offset +
                                  static_cast<std::size_t>(channel)]) *
                      static_cast<double>(alpha) / 255.0;
      }
      filter_gaussian_line(values, scratch, plan);
      const auto row_offset =
          static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
      for (std::int32_t x = 0; x < width; ++x) {
        horizontal[row_offset + static_cast<std::size_t>(x)] =
            static_cast<float>(rounded_byte(
                values[static_cast<std::size_t>(x)]));
      }
    }

    values.resize(static_cast<std::size_t>(height));
    scratch.resize(static_cast<std::size_t>(height));
    for (std::int32_t x = 0; x < width; ++x) {
      report_fraction(progress, completed_lines++, total_lines,
                      FilterProgressStage::Blurring);
      for (std::int32_t y = 0; y < height; ++y) {
        values[static_cast<std::size_t>(y)] =
            horizontal[static_cast<std::size_t>(y) *
                           static_cast<std::size_t>(width) +
                       static_cast<std::size_t>(x)];
      }
      filter_gaussian_line(values, scratch, plan);
      for (std::int32_t y = 0; y < height; ++y) {
        const auto output_offset =
            (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
             static_cast<std::size_t>(x)) *
            4U;
        if (channel == 3) {
          output_data[output_offset + 3U] =
              rounded_byte(values[static_cast<std::size_t>(y)]);
          continue;
        }
        const auto alpha = output_data[output_offset + 3U];
        const auto premultiplied =
            rounded_byte(values[static_cast<std::size_t>(y)]);
        output_data[output_offset + static_cast<std::size_t>(channel)] =
            alpha == 0U ? 0U
                        : rounded_byte(static_cast<double>(premultiplied) *
                                       255.0 / static_cast<double>(alpha));
      }
    }
  }
  report_fraction(progress, total_lines, total_lines,
                  FilterProgressStage::Blurring);
  return FilterRenderResult{std::move(output), bounds};
}

[[nodiscard]] FilterRenderResult
render_straight_gaussian(const FilterRenderResult &input, double radius,
                         const FilterProgress *progress,
                         bool unsharp_kernel = false) {
  const auto margin =
      static_cast<int>(std::ceil(kGaussianMarginScale * radius));
  const auto plan = unsharp_kernel ? make_unsharp_line_plan(radius, margin)
                                   : make_high_pass_line_plan(radius, margin);
  auto output = input.pixels;
  const auto width = input.bounds.width;
  const auto height = input.bounds.height;
  const auto pixel_count = static_cast<std::uint64_t>(width) *
                           static_cast<std::uint64_t>(height);
  if (pixel_count > std::numeric_limits<std::size_t>::max() / sizeof(float)) {
    throw std::overflow_error("Smart Filter working buffer overflow");
  }
  std::vector<float> horizontal(static_cast<std::size_t>(pixel_count));
  std::vector<double> values;
  std::vector<double> scratch;
  const auto total_lines = 3U *
                           (static_cast<std::uint64_t>(width) +
                            static_cast<std::uint64_t>(height));
  std::uint64_t completed_lines = 0U;
  for (std::size_t channel = 0; channel < 3U; ++channel) {
    values.resize(static_cast<std::size_t>(width));
    scratch.resize(static_cast<std::size_t>(width));
    for (std::int32_t y = 0; y < height; ++y) {
      report_fraction(progress, completed_lines++, total_lines,
                      FilterProgressStage::Blurring);
      for (std::int32_t x = 0; x < width; ++x) {
        values[static_cast<std::size_t>(x)] =
            input.pixels.pixel(x, y)[channel];
      }
      filter_gaussian_line(values, scratch, plan);
      const auto row_offset =
          static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
      for (std::int32_t x = 0; x < width; ++x) {
        horizontal[row_offset + static_cast<std::size_t>(x)] =
            rounded_byte(values[static_cast<std::size_t>(x)]);
      }
    }

    values.resize(static_cast<std::size_t>(height));
    scratch.resize(static_cast<std::size_t>(height));
    for (std::int32_t x = 0; x < width; ++x) {
      report_fraction(progress, completed_lines++, total_lines,
                      FilterProgressStage::Blurring);
      for (std::int32_t y = 0; y < height; ++y) {
        values[static_cast<std::size_t>(y)] =
            horizontal[static_cast<std::size_t>(y) *
                           static_cast<std::size_t>(width) +
                       static_cast<std::size_t>(x)];
      }
      filter_gaussian_line(values, scratch, plan);
      for (std::int32_t y = 0; y < height; ++y) {
        output.pixel(x, y)[channel] =
            rounded_byte(values[static_cast<std::size_t>(y)]);
      }
    }
  }
  report_fraction(progress, total_lines, total_lines,
                  FilterProgressStage::Blurring);
  return FilterRenderResult{std::move(output), input.bounds};
}

[[nodiscard]] FilterRenderResult
render_high_pass(const FilterRenderResult &input, double radius,
                 const FilterProgress *progress) {
  auto blur_progress = phase_progress(progress, 0, 2);
  const auto blurred =
      render_straight_gaussian(input, radius, &blur_progress);
  PixelBuffer output(input.bounds.width, input.bounds.height,
                     PixelFormat::rgba8());
  auto detail_progress = phase_progress(progress, 1, 2);
  for (std::int32_t y = 0; y < input.bounds.height; ++y) {
    report_progress(&detail_progress, y, input.bounds.height,
                    FilterProgressStage::Sharpening);
    for (std::int32_t x = 0; x < input.bounds.width; ++x) {
      const auto *source = input.pixels.pixel(x, y);
      const auto *low_frequency = blurred.pixels.pixel(x, y);
      auto *destination = output.pixel(x, y);
      for (std::size_t channel = 0; channel < 3U; ++channel) {
        destination[channel] = static_cast<std::uint8_t>(std::clamp(
            static_cast<int>(source[channel]) -
                static_cast<int>(low_frequency[channel]) + 128,
            0, 255));
      }
      destination[3] = source[3];
    }
  }
  report_progress(&detail_progress, input.bounds.height, input.bounds.height,
                  FilterProgressStage::Sharpening);
  return FilterRenderResult{std::move(output), input.bounds};
}

[[nodiscard]] FilterRenderResult
render_unsharp_mask(const FilterRenderResult &input, double amount_percent,
                    double radius, std::int32_t threshold,
                    const FilterProgress *progress) {
  auto blur_progress = phase_progress(progress, 0, 2);
  const auto blurred =
      render_straight_gaussian(input, radius, &blur_progress, true);
  PixelBuffer output(input.bounds.width, input.bounds.height,
                     PixelFormat::rgba8());
  auto sharpen_progress = phase_progress(progress, 1, 2);
  for (std::int32_t y = 0; y < input.bounds.height; ++y) {
    report_progress(&sharpen_progress, y, input.bounds.height,
                    FilterProgressStage::Sharpening);
    for (std::int32_t x = 0; x < input.bounds.width; ++x) {
      const auto *source = input.pixels.pixel(x, y);
      const auto *low_frequency = blurred.pixels.pixel(x, y);
      auto *destination = output.pixel(x, y);
      for (std::size_t channel = 0; channel < 3U; ++channel) {
        const auto detail = static_cast<int>(source[channel]) -
                            static_cast<int>(low_frequency[channel]);
        // Photoshop scales the detail first, then removes Threshold from the
        // signed adjustment. This differs from the common pre-test shortcut
        // and is pinned by the Photoshop 27.8 one-pixel-line captures.
        const auto scaled_detail = static_cast<int>(
            static_cast<double>(detail) * amount_percent / 100.0);
        const auto magnitude = std::abs(scaled_detail);
        const auto adjustment =
            magnitude <= threshold
                ? 0
                : (scaled_detail < 0 ? -(magnitude - threshold)
                                     : magnitude - threshold);
        destination[channel] = static_cast<std::uint8_t>(
            std::clamp(static_cast<int>(source[channel]) + adjustment, 0, 255));
      }
      destination[3] = source[3];
    }
  }
  report_progress(&sharpen_progress, input.bounds.height, input.bounds.height,
                  FilterProgressStage::Sharpening);
  return FilterRenderResult{std::move(output), input.bounds};
}

[[nodiscard]] std::int64_t floor_divide(std::int64_t numerator,
                                        std::int64_t denominator) noexcept {
  if (denominator < 0) {
    numerator = -numerator;
    denominator = -denominator;
  }
  const auto quotient = numerator / denominator;
  return quotient - (numerator % denominator < 0 ? 1 : 0);
}

// Runs body(begin, end) over [0, count) in blocks. Large jobs fan out to
// worker threads sized by the hardware and the wasm blocking fan-out budget.
// Every block writes only its own output pixels, so the result never depends
// on scheduling. Only the calling thread reports progress: callbacks and
// FilterCancelled never cross threads, and a cancel stops the workers at their
// next block before the exception propagates.
template <typename Body>
void run_filter_blocks(std::int32_t count, std::uint64_t work_per_item,
                       const FilterProgress *progress,
                       FilterProgressStage stage, const Body &body) {
  if (count <= 0) {
    report_progress(progress, 1, 1, stage);
    return;
  }
  constexpr std::uint64_t kMinimumParallelWork = 1U << 22;
  const auto total_work = static_cast<std::uint64_t>(count) *
                          std::max<std::uint64_t>(1U, work_per_item);
  const auto workers =
      total_work < kMinimumParallelWork
          ? 1
          : max_blocking_fanout_workers(
                std::clamp(std::min(hardware_worker_threads(), count), 1, 16));
  const auto block =
      workers < 2 ? 1 : std::max<std::int32_t>(1, count / (workers * 8));
  std::atomic<std::int32_t> next{0};
  std::atomic<std::int32_t> finished{0};
  std::atomic<bool> stop{false};
  const auto run_block = [&]() -> bool {
    const auto begin = next.fetch_add(block);
    if (begin >= count) {
      return false;
    }
    const auto end = std::min(count, begin + block);
    body(begin, end);
    finished.fetch_add(end - begin);
    return true;
  };
  std::vector<std::future<void>> helpers;
  try {
    for (int worker = 1; worker < workers; ++worker) {
      helpers.push_back(std::async(std::launch::async, [&] {
        try {
          while (!stop.load() && run_block()) {
          }
        } catch (...) {
          stop.store(true);
          throw;
        }
      }));
    }
    report_progress(progress, 0, count, stage);
    while (!stop.load() && run_block()) {
      report_progress(progress, finished.load(), count, stage);
    }
    for (auto &helper : helpers) {
      while (helper.wait_for(std::chrono::milliseconds(20)) !=
             std::future_status::ready) {
        report_progress(progress, finished.load(), count, stage);
      }
    }
  } catch (...) {
    stop.store(true);
    for (auto &helper : helpers) {
      helper.wait();
    }
    throw;
  }
  // get() rethrows a worker's exception (bad_alloc under memory pressure).
  for (auto &helper : helpers) {
    helper.get();
  }
  report_progress(progress, count, count, stage);
}

constexpr std::int64_t kMotionBlurCoordinateScale = 65536;
constexpr std::uint64_t kMotionBlurSampleWeight =
    static_cast<std::uint64_t>(kMotionBlurCoordinateScale) *
    kMotionBlurCoordinateScale;
// Longest non-axis distance the exact tap kernel renders; longer blurs use
// the O(1)-per-pixel running-sum kernel. Axis angles are exact at every
// distance.
constexpr std::int32_t kMotionBlurTapKernelMaximumDistance = 64;
// Sheared rows per pixel of the running-sum kernel. Quarter-pixel rows keep
// its extra cross-line blend small: within 4 levels of the tap kernel at
// 65 px on a noisy image, except near the diagonals, where the taps' own
// bilinear smear is the softer one.
constexpr std::int64_t kMotionBlurSubrows = 4;

using MotionBlurSums = std::array<std::uint64_t, 4>;

struct MotionBlurLine {
  std::int64_t step_x{0};
  std::int64_t step_y{0};
  std::int32_t first_sample{0};
  std::int32_t last_sample{0};
};

[[nodiscard]] MotionBlurLine motion_blur_line(std::int32_t angle_degrees,
                                              std::int32_t distance_pixels) {
  constexpr double kPi = 3.14159265358979323846;
  const auto radians = static_cast<double>(angle_degrees) * kPi / 180.0;
  MotionBlurLine line;
  // Quantizing the direction before sampling keeps the bilinear envelope and
  // tie behavior fixed across toolchains.
  line.step_x = static_cast<std::int64_t>(
      std::llround(std::cos(radians) * kMotionBlurCoordinateScale));
  line.step_y = static_cast<std::int64_t>(
      std::llround(-std::sin(radians) * kMotionBlurCoordinateScale));
  line.first_sample = -distance_pixels / 2;
  line.last_sample = line.first_sample + distance_pixels;
  return line;
}

void store_motion_blur_pixel(std::uint8_t *destination,
                             const MotionBlurSums &sums,
                             std::uint64_t alpha_denominator) noexcept {
  const auto alpha_sum = sums[3];
  for (std::size_t channel = 0; channel < 3U; ++channel) {
    destination[channel] =
        alpha_sum == 0U
            ? 0U
            : static_cast<std::uint8_t>(std::min<std::uint64_t>(
                  255U, (sums[channel] + alpha_sum / 2U) / alpha_sum));
  }
  destination[3] = static_cast<std::uint8_t>(std::min<std::uint64_t>(
      255U, (alpha_sum + alpha_denominator / 2U) / alpha_denominator));
}

// The exact kernel: distance + 1 bilinear taps per pixel at fixed-point
// offsets, each clamped to the input. The tap fractions do not depend on the
// pixel, so interior pixels (no tap clamps) read one merged sparse kernel:
// the same integer sums in another order, hence byte-identical.
void motion_blur_taps(const PixelBuffer &input, const MotionBlurLine &line,
                      std::uint8_t *output, const FilterProgress *progress) {
  constexpr auto kScale = kMotionBlurCoordinateScale;
  const auto width = input.width();
  const auto height = input.height();
  const auto stride = static_cast<std::ptrdiff_t>(input.stride_bytes());
  const auto *source = input.data().data();
  const auto maximum_x = static_cast<std::int64_t>(width - 1) * kScale;
  const auto maximum_y = static_cast<std::int64_t>(height - 1) * kScale;
  const auto sample_count =
      static_cast<std::uint64_t>(line.last_sample - line.first_sample) + 1U;
  const auto alpha_denominator = sample_count * kMotionBlurSampleWeight;

  struct KernelCell {
    std::int64_t dy{0};
    std::int64_t dx{0};
    std::uint64_t weight{0};
  };
  std::vector<KernelCell> cells;
  cells.reserve(static_cast<std::size_t>(sample_count) * 4U);
  for (auto sample = line.first_sample; sample <= line.last_sample; ++sample) {
    const auto offset_x = static_cast<std::int64_t>(sample) * line.step_x;
    const auto offset_y = static_cast<std::int64_t>(sample) * line.step_y;
    const auto dx = floor_divide(offset_x, kScale);
    const auto dy = floor_divide(offset_y, kScale);
    const auto fraction_x = static_cast<std::uint64_t>(offset_x - dx * kScale);
    const auto fraction_y = static_cast<std::uint64_t>(offset_y - dy * kScale);
    const auto inverse_x = static_cast<std::uint64_t>(kScale) - fraction_x;
    const auto inverse_y = static_cast<std::uint64_t>(kScale) - fraction_y;
    const std::array<KernelCell, 4> corners{
        KernelCell{dy, dx, inverse_x * inverse_y},
        KernelCell{dy, dx + 1, fraction_x * inverse_y},
        KernelCell{dy + 1, dx, inverse_x * fraction_y},
        KernelCell{dy + 1, dx + 1, fraction_x * fraction_y}};
    for (const auto &corner : corners) {
      if (corner.weight != 0U) {
        cells.push_back(corner);
      }
    }
  }
  std::sort(cells.begin(), cells.end(),
            [](const KernelCell &left, const KernelCell &right) {
              return left.dy != right.dy ? left.dy < right.dy
                                         : left.dx < right.dx;
            });
  std::vector<std::ptrdiff_t> kernel_offsets;
  std::vector<std::uint64_t> kernel_weights;
  for (std::size_t index = 0; index < cells.size(); ++index) {
    if (index > 0U && cells[index].dy == cells[index - 1U].dy &&
        cells[index].dx == cells[index - 1U].dx) {
      kernel_weights.back() += cells[index].weight;
      continue;
    }
    kernel_offsets.push_back(
        static_cast<std::ptrdiff_t>(cells[index].dy) * stride +
        static_cast<std::ptrdiff_t>(cells[index].dx) * 4);
    kernel_weights.push_back(cells[index].weight);
  }

  const auto reach_low_x =
      std::min(static_cast<std::int64_t>(line.first_sample) * line.step_x,
               static_cast<std::int64_t>(line.last_sample) * line.step_x);
  const auto reach_high_x =
      std::max(static_cast<std::int64_t>(line.first_sample) * line.step_x,
               static_cast<std::int64_t>(line.last_sample) * line.step_x);
  const auto reach_low_y =
      std::min(static_cast<std::int64_t>(line.first_sample) * line.step_y,
               static_cast<std::int64_t>(line.last_sample) * line.step_y);
  const auto reach_high_y =
      std::max(static_cast<std::int64_t>(line.first_sample) * line.step_y,
               static_cast<std::int64_t>(line.last_sample) * line.step_y);
  const auto interior_first_x = -floor_divide(reach_low_x, kScale);
  const auto interior_last_x = floor_divide(maximum_x - reach_high_x, kScale);
  const auto interior_first_y = -floor_divide(reach_low_y, kScale);
  const auto interior_last_y = floor_divide(maximum_y - reach_high_y, kScale);

  const auto clamped_pixel = [&](std::int32_t x, std::int32_t y,
                                 std::uint8_t *destination) {
    MotionBlurSums sums{};
    for (auto sample = line.first_sample; sample <= line.last_sample;
         ++sample) {
      const auto sample_x = std::clamp<std::int64_t>(
          static_cast<std::int64_t>(x) * kScale +
              static_cast<std::int64_t>(sample) * line.step_x,
          0, maximum_x);
      const auto sample_y = std::clamp<std::int64_t>(
          static_cast<std::int64_t>(y) * kScale +
              static_cast<std::int64_t>(sample) * line.step_y,
          0, maximum_y);
      const auto x0 = static_cast<std::int32_t>(sample_x / kScale);
      const auto y0 = static_cast<std::int32_t>(sample_y / kScale);
      const auto x1 = std::min(width - 1, x0 + 1);
      const auto y1 = std::min(height - 1, y0 + 1);
      const auto fraction_x = static_cast<std::uint64_t>(sample_x % kScale);
      const auto fraction_y = static_cast<std::uint64_t>(sample_y % kScale);
      const auto inverse_x = static_cast<std::uint64_t>(kScale) - fraction_x;
      const auto inverse_y = static_cast<std::uint64_t>(kScale) - fraction_y;
      const std::array<std::uint64_t, 4> weights{
          inverse_x * inverse_y, fraction_x * inverse_y,
          inverse_x * fraction_y, fraction_x * fraction_y};
      const std::array<const std::uint8_t *, 4> pixels{
          input.pixel(x0, y0), input.pixel(x1, y0), input.pixel(x0, y1),
          input.pixel(x1, y1)};
      for (std::size_t corner = 0; corner < pixels.size(); ++corner) {
        const auto alpha_weight =
            static_cast<std::uint64_t>(pixels[corner][3]) * weights[corner];
        sums[3] += alpha_weight;
        for (std::size_t channel = 0; channel < 3U; ++channel) {
          sums[channel] +=
              static_cast<std::uint64_t>(pixels[corner][channel]) *
              alpha_weight;
        }
      }
    }
    store_motion_blur_pixel(destination, sums, alpha_denominator);
  };

  const auto work_per_row =
      static_cast<std::uint64_t>(width) * kernel_weights.size();
  run_filter_blocks(
      height, work_per_row, progress, FilterProgressStage::Blurring,
      [&](std::int32_t begin, std::int32_t end) {
        for (auto y = begin; y < end; ++y) {
          auto *row = output + static_cast<std::ptrdiff_t>(y) * stride;
          const auto interior_row = y >= interior_first_y && y <= interior_last_y;
          const auto x_begin = static_cast<std::int32_t>(
              interior_row ? std::clamp<std::int64_t>(interior_first_x, 0, width)
                           : width);
          const auto x_end = static_cast<std::int32_t>(
              interior_row
                  ? std::clamp<std::int64_t>(interior_last_x + 1, x_begin, width)
                  : width);
          for (std::int32_t x = 0; x < x_begin; ++x) {
            clamped_pixel(x, y, row + static_cast<std::ptrdiff_t>(x) * 4);
          }
          for (auto x = x_begin; x < x_end; ++x) {
            const auto *center = source +
                                 static_cast<std::ptrdiff_t>(y) * stride +
                                 static_cast<std::ptrdiff_t>(x) * 4;
            MotionBlurSums sums{};
            for (std::size_t cell = 0; cell < kernel_offsets.size(); ++cell) {
              const auto *pixel = center + kernel_offsets[cell];
              const auto alpha_weight =
                  static_cast<std::uint64_t>(pixel[3]) * kernel_weights[cell];
              sums[0] += static_cast<std::uint64_t>(pixel[0]) * alpha_weight;
              sums[1] += static_cast<std::uint64_t>(pixel[1]) * alpha_weight;
              sums[2] += static_cast<std::uint64_t>(pixel[2]) * alpha_weight;
              sums[3] += alpha_weight;
            }
            store_motion_blur_pixel(row + static_cast<std::ptrdiff_t>(x) * 4,
                                    sums, alpha_denominator);
          }
          for (auto x = x_end; x < width; ++x) {
            clamped_pixel(x, y, row + static_cast<std::ptrdiff_t>(x) * 4);
          }
        }
      });
}

// Axis angles (0, 90, 180, 270 and 360 degrees, either sign) quantize to
// whole-pixel steps with zero fractions, so the exact kernel is an
// edge-clamped box of distance + 1 pixels along the axis. A running sum
// reproduces its integer sums in O(1) per pixel.
void motion_blur_axis(const PixelBuffer &input, const MotionBlurLine &line,
                      std::uint8_t *output, const FilterProgress *progress) {
  const auto width = input.width();
  const auto height = input.height();
  const auto stride = static_cast<std::ptrdiff_t>(input.stride_bytes());
  const auto *source = input.data().data();
  const auto horizontal = line.step_y == 0;
  const auto direction =
      (horizontal ? line.step_x : line.step_y) > 0 ? std::int64_t{1}
                                                   : std::int64_t{-1};
  const auto reach_low = std::min(line.first_sample * direction,
                                  line.last_sample * direction);
  const auto reach_high = std::max(line.first_sample * direction,
                                   line.last_sample * direction);
  const auto sample_count =
      static_cast<std::uint64_t>(line.last_sample - line.first_sample) + 1U;
  const auto alpha_denominator = sample_count * kMotionBlurSampleWeight;
  const auto add = [](MotionBlurSums &sums, const std::uint8_t *pixel) {
    const auto alpha = static_cast<std::uint64_t>(pixel[3]);
    sums[0] += pixel[0] * alpha;
    sums[1] += pixel[1] * alpha;
    sums[2] += pixel[2] * alpha;
    sums[3] += alpha;
  };
  const auto subtract = [](MotionBlurSums &sums, const std::uint8_t *pixel) {
    const auto alpha = static_cast<std::uint64_t>(pixel[3]);
    sums[0] -= pixel[0] * alpha;
    sums[1] -= pixel[1] * alpha;
    sums[2] -= pixel[2] * alpha;
    sums[3] -= alpha;
  };
  const auto store = [&](std::uint8_t *destination, const MotionBlurSums &sums) {
    MotionBlurSums scaled{};
    for (std::size_t channel = 0; channel < scaled.size(); ++channel) {
      scaled[channel] = sums[channel] * kMotionBlurSampleWeight;
    }
    store_motion_blur_pixel(destination, scaled, alpha_denominator);
  };

  if (horizontal) {
    const auto at = [&](std::int32_t y, std::int64_t x) {
      return source + static_cast<std::ptrdiff_t>(y) * stride +
             static_cast<std::ptrdiff_t>(
                 std::clamp<std::int64_t>(x, 0, width - 1)) *
                 4;
    };
    run_filter_blocks(
        height, static_cast<std::uint64_t>(width) + sample_count, progress,
        FilterProgressStage::Blurring,
        [&](std::int32_t begin, std::int32_t end) {
          for (auto y = begin; y < end; ++y) {
            MotionBlurSums sums{};
            for (auto offset = reach_low; offset <= reach_high; ++offset) {
              add(sums, at(y, offset));
            }
            auto *row = output + static_cast<std::ptrdiff_t>(y) * stride;
            for (std::int32_t x = 0; x < width; ++x) {
              store(row + static_cast<std::ptrdiff_t>(x) * 4, sums);
              add(sums, at(y, x + 1 + reach_high));
              subtract(sums, at(y, x + reach_low));
            }
          }
        });
    return;
  }

  // Vertical: strips of columns keep per-column running sums while walking
  // the rows in memory order.
  constexpr std::int32_t kStripWidth = 64;
  const auto strip_count = (width + kStripWidth - 1) / kStripWidth;
  const auto at = [&](std::int64_t y, std::int32_t x) {
    return source +
           static_cast<std::ptrdiff_t>(
               std::clamp<std::int64_t>(y, 0, height - 1)) *
               stride +
           static_cast<std::ptrdiff_t>(x) * 4;
  };
  run_filter_blocks(
      strip_count,
      static_cast<std::uint64_t>(kStripWidth) *
          (static_cast<std::uint64_t>(height) + sample_count),
      progress, FilterProgressStage::Blurring,
      [&](std::int32_t begin, std::int32_t end) {
        std::vector<MotionBlurSums> sums(static_cast<std::size_t>(kStripWidth));
        for (auto strip = begin; strip < end; ++strip) {
          const auto x_begin = strip * kStripWidth;
          const auto x_end = std::min(width, x_begin + kStripWidth);
          std::fill(sums.begin(), sums.end(), MotionBlurSums{});
          for (auto offset = reach_low; offset <= reach_high; ++offset) {
            for (auto x = x_begin; x < x_end; ++x) {
              add(sums[static_cast<std::size_t>(x - x_begin)], at(offset, x));
            }
          }
          for (std::int32_t y = 0; y < height; ++y) {
            auto *row = output + static_cast<std::ptrdiff_t>(y) * stride;
            for (auto x = x_begin; x < x_end; ++x) {
              auto &column = sums[static_cast<std::size_t>(x - x_begin)];
              store(row + static_cast<std::ptrdiff_t>(x) * 4, column);
              add(column, at(y + 1 + reach_high, x));
              subtract(column, at(y + reach_low, x));
            }
          }
        }
      });
}

// Long non-axis blurs. The input is resampled along sheared rows spaced a
// quarter pixel apart: sheared row r samples the pixel at major-axis column c
// and minor position r / 4 + c * slope, blending the two nearest minor-axis
// pixels. Every blur line runs between two adjacent sheared rows, so an
// output pixel blends the two rows' integrals over its window, and prefix
// sums make each integral O(1). The window is the taps' parameter range
// [first - 1/2, last + 1/2] projected onto the major axis, with fractional
// end coverage. Positions are Q16 and blend weights Q8, all integer, so the
// result is toolchain independent. It is not byte-identical to the tap
// kernel, which stays the reference through the threshold.
void motion_blur_running_sum(const PixelBuffer &input,
                             const MotionBlurLine &line, std::uint8_t *output,
                             const FilterProgress *progress) {
  constexpr std::int64_t kPosition = 65536;
  constexpr std::int64_t kBlend = 256;
  const auto steep = std::abs(line.step_y) > std::abs(line.step_x);
  const auto major_step = steep ? line.step_y : line.step_x;
  const auto minor_step = steep ? line.step_x : line.step_y;
  const std::int64_t major_size = steep ? input.height() : input.width();
  const std::int64_t minor_size = steep ? input.width() : input.height();
  const auto stride = static_cast<std::ptrdiff_t>(input.stride_bytes());
  const std::ptrdiff_t major_stride = steep ? stride : 4;
  const std::ptrdiff_t minor_stride = steep ? 4 : stride;
  const auto *source = input.data().data();

  // Window ends in Q8 pixels; the steps are Q16, the doubled parameters make
  // the half-tap ends integral, and 2 * 65536 / 256 = 512.
  const auto window_a =
      (2 * static_cast<std::int64_t>(line.first_sample) - 1) * major_step;
  const auto window_b =
      (2 * static_cast<std::int64_t>(line.last_sample) + 1) * major_step;
  const auto window_low = floor_divide(std::min(window_a, window_b) + 256, 512);
  const auto window_high =
      floor_divide(std::max(window_a, window_b) + 256, 512);
  const auto window_length = window_high - window_low;
  const auto alpha_denominator =
      static_cast<std::uint64_t>(window_length) * kBlend * kBlend;

  // Sample c covers [c - 1/2, c + 1/2), so z = u + 1/2 indexes it directly.
  const auto first_column = floor_divide(window_low + kBlend / 2, kBlend);
  const auto last_column =
      floor_divide((major_size - 1) * kBlend + window_high + kBlend / 2,
                   kBlend) +
      1;
  const auto column_count =
      static_cast<std::size_t>(last_column - first_column + 1);
  const auto minor_offset = [&](std::int64_t column) {
    return floor_divide(column * minor_step * kPosition, major_step);
  };
  const auto subrow_spacing = kPosition / kMotionBlurSubrows;
  std::vector<std::int64_t> column_offsets(column_count);
  for (std::size_t index = 0; index < column_count; ++index) {
    column_offsets[index] =
        minor_offset(first_column + static_cast<std::int64_t>(index));
  }
  std::vector<std::int64_t> output_offsets(static_cast<std::size_t>(major_size));
  for (std::int64_t u = 0; u < major_size; ++u) {
    output_offsets[static_cast<std::size_t>(u)] = minor_offset(u);
  }
  // Output (u, v) lies on the line whose Q16 intercept is
  // v * 65536 - offset(u); sheared row r holds the intercepts in
  // [r * spacing, (r + 1) * spacing).
  const auto [lowest_offset, highest_offset] =
      std::minmax_element(output_offsets.begin(), output_offsets.end());
  const auto first_row = floor_divide(-*highest_offset, subrow_spacing);
  const auto last_row = floor_divide(
      (minor_size - 1) * kPosition - *lowest_offset, subrow_spacing);
  const auto row_count = static_cast<std::int32_t>(last_row - first_row + 1);

  const auto build = [&](std::int64_t row, std::vector<MotionBlurSums> &prefix) {
    prefix[0] = MotionBlurSums{};
    const auto row_position = row * subrow_spacing;
    for (std::size_t index = 0; index < column_count; ++index) {
      const auto column = std::clamp<std::int64_t>(
          first_column + static_cast<std::int64_t>(index), 0, major_size - 1);
      // Signed right shifts are arithmetic (floor) since C++20.
      const auto position = row_position + column_offsets[index];
      const auto minor = position >> 16;
      const auto *base = source + static_cast<std::ptrdiff_t>(column) * major_stride;
      const auto *near_pixel =
          base + static_cast<std::ptrdiff_t>(
                     std::clamp<std::int64_t>(minor, 0, minor_size - 1)) *
                     minor_stride;
      const auto *far_pixel =
          base + static_cast<std::ptrdiff_t>(
                     std::clamp<std::int64_t>(minor + 1, 0, minor_size - 1)) *
                     minor_stride;
      const auto far_weight = static_cast<std::uint64_t>((position & 0xFFFF) >> 8);
      const auto near_alpha = static_cast<std::uint64_t>(near_pixel[3]) *
                              (static_cast<std::uint64_t>(kBlend) - far_weight);
      const auto far_alpha =
          static_cast<std::uint64_t>(far_pixel[3]) * far_weight;
      const auto &previous = prefix[index];
      auto &sums = prefix[index + 1U];
      for (std::size_t channel = 0; channel < 3U; ++channel) {
        sums[channel] = previous[channel] +
                        near_pixel[channel] * near_alpha +
                        far_pixel[channel] * far_alpha;
      }
      sums[3] = previous[3] + near_alpha + far_alpha;
    }
  };
  // Integral of the piecewise-constant samples from 0 to z (Q8, relative to
  // first_column).
  const auto integral = [](const std::vector<MotionBlurSums> &prefix,
                           std::int64_t z, std::size_t channel) {
    const auto cell = static_cast<std::size_t>(z / kBlend);
    const auto part = static_cast<std::uint64_t>(z % kBlend);
    return prefix[cell][channel] * static_cast<std::uint64_t>(kBlend) +
           (prefix[cell + 1U][channel] - prefix[cell][channel]) * part;
  };

  run_filter_blocks(
      row_count,
      static_cast<std::uint64_t>(column_count) +
          static_cast<std::uint64_t>(major_size),
      progress, FilterProgressStage::Blurring,
      [&](std::int32_t begin, std::int32_t end) {
        std::vector<MotionBlurSums> current(column_count + 1U);
        std::vector<MotionBlurSums> next(column_count + 1U);
        build(first_row + begin, current);
        for (auto index = begin; index < end; ++index) {
          const auto row = first_row + index;
          build(row + 1, next);
          const auto row_position = row * subrow_spacing;
          for (std::int64_t u = 0; u < major_size; ++u) {
            const auto start =
                row_position + output_offsets[static_cast<std::size_t>(u)];
            const auto minor = -((-start) >> 16);
            const auto remainder = minor * kPosition - start;
            if (remainder >= subrow_spacing || minor < 0 ||
                minor >= minor_size) {
              continue;
            }
            const auto low =
                u * kBlend + window_low + kBlend / 2 - first_column * kBlend;
            const auto high = low + window_length;
            const auto far_weight = static_cast<std::uint64_t>(
                remainder * kBlend / subrow_spacing);
            const auto near_weight =
                static_cast<std::uint64_t>(kBlend) - far_weight;
            MotionBlurSums sums{};
            for (std::size_t channel = 0; channel < sums.size(); ++channel) {
              sums[channel] =
                  near_weight * (integral(current, high, channel) -
                                 integral(current, low, channel)) +
                  far_weight * (integral(next, high, channel) -
                                integral(next, low, channel));
            }
            store_motion_blur_pixel(
                output + static_cast<std::ptrdiff_t>(u) * major_stride +
                    static_cast<std::ptrdiff_t>(minor) * minor_stride,
                sums, alpha_denominator);
          }
          std::swap(current, next);
        }
      });
}

[[nodiscard]] FilterRenderResult
render_motion_blur(const FilterRenderResult &input, std::int32_t angle_degrees,
                   std::int32_t distance_pixels,
                   const FilterProgress *progress,
                   MotionBlurKernel kernel = MotionBlurKernel::Automatic) {
  const auto width = input.bounds.width;
  const auto height = input.bounds.height;
  PixelBuffer output(width, height, PixelFormat::rgba8());
  output.clear(0);
  if (width <= 0 || height <= 0) {
    report_progress(progress, 1, 1, FilterProgressStage::Blurring);
    return FilterRenderResult{std::move(output), input.bounds};
  }
  const auto line = motion_blur_line(angle_degrees, distance_pixels);
  // Detach the fresh buffer here: the kernels write it from worker threads.
  auto *destination = output.data().data();
  const auto axis =
      (line.step_y == 0 && std::abs(line.step_x) == kMotionBlurCoordinateScale) ||
      (line.step_x == 0 && std::abs(line.step_y) == kMotionBlurCoordinateScale);
  if (axis && kernel != MotionBlurKernel::Taps) {
    motion_blur_axis(input.pixels, line, destination, progress);
  } else if (axis || kernel == MotionBlurKernel::Taps ||
             (kernel == MotionBlurKernel::Automatic &&
              distance_pixels <= kMotionBlurTapKernelMaximumDistance)) {
    motion_blur_taps(input.pixels, line, destination, progress);
  } else {
    motion_blur_running_sum(input.pixels, line, destination, progress);
  }
  return FilterRenderResult{std::move(output), input.bounds};
}

struct TransparentColorExtension {
  bool all_transparent{false};
  // Empty for an opaque input. A mixed-alpha input stores the nearest visible
  // source pixel's linear index for every pixel. Keeping only one uint32 per
  // pixel avoids materializing another RGBA working buffer.
  std::vector<std::uint32_t> nearest_visible;
};

[[nodiscard]] std::uint8_t extended_straight_color_sample(
    const FilterRenderResult &input,
    const TransparentColorExtension &extension, std::int32_t x,
    std::int32_t y, std::size_t channel) noexcept {
  const auto *pixel = input.pixels.pixel(x, y);
  if (pixel[3] != 0U || extension.nearest_visible.empty()) {
    return pixel[channel];
  }
  const auto index = extension.nearest_visible[
      static_cast<std::size_t>(y) *
          static_cast<std::size_t>(input.bounds.width) +
      static_cast<std::size_t>(x)];
  return input.pixels.data()[static_cast<std::size_t>(index) * 4U + channel];
}

[[nodiscard]] std::int64_t ceil_divide(std::int64_t numerator,
                                       std::int64_t denominator) noexcept {
  const auto quotient = numerator / denominator;
  const auto remainder = numerator % denominator;
  return quotient + (remainder > 0 ? 1 : 0);
}

[[nodiscard]] TransparentColorExtension extend_transparent_colors(
    const FilterRenderResult &input, const FilterProgress *progress) {
  TransparentColorExtension extension;
  const auto width = input.bounds.width;
  const auto height = input.bounds.height;
  const auto pixel_count = static_cast<std::uint64_t>(width) *
                           static_cast<std::uint64_t>(height);
  if (pixel_count == 0U) {
    report_progress(progress, 1, 1, FilterProgressStage::Filtering);
    extension.all_transparent = true;
    return extension;
  }

  bool saw_visible = false;
  bool saw_transparent = false;
  const auto total_work = static_cast<std::uint64_t>(height) * 2U +
                          static_cast<std::uint64_t>(width);
  for (std::int32_t y = 0; y < height; ++y) {
    report_fraction(progress, static_cast<std::uint64_t>(y), total_work,
                    FilterProgressStage::Filtering);
    for (std::int32_t x = 0; x < width; ++x) {
      const auto alpha = input.pixels.pixel(x, y)[3];
      saw_visible = saw_visible || alpha != 0U;
      saw_transparent = saw_transparent || alpha == 0U;
    }
  }
  if (!saw_visible) {
    extension.all_transparent = true;
    report_fraction(progress, total_work, total_work,
                    FilterProgressStage::Filtering);
    return extension;
  }
  if (!saw_transparent) {
    report_fraction(progress, total_work, total_work,
                    FilterProgressStage::Filtering);
    return extension;
  }
  if (pixel_count > std::numeric_limits<std::uint32_t>::max() ||
      pixel_count > std::numeric_limits<std::size_t>::max() /
                        sizeof(std::uint32_t)) {
    throw std::overflow_error("Filter color-extension buffer overflow");
  }

  constexpr auto kNoSource = std::numeric_limits<std::uint32_t>::max();
  extension.nearest_visible.assign(static_cast<std::size_t>(pixel_count),
                                   kNoSource);

  // First choose the nearest visible source on each row. An equal-distance
  // choice favors the source on the right, matching the Photoshop probes.
  for (std::int32_t y = 0; y < height; ++y) {
    report_fraction(progress,
                    static_cast<std::uint64_t>(height) +
                        static_cast<std::uint64_t>(y),
                    total_work, FilterProgressStage::Filtering);
    std::int32_t left = -1;
    const auto row_offset = static_cast<std::size_t>(y) *
                            static_cast<std::size_t>(width);
    for (std::int32_t x = 0; x < width; ++x) {
      if (input.pixels.pixel(x, y)[3] != 0U) {
        left = x;
      }
      if (left >= 0) {
        extension.nearest_visible[row_offset + static_cast<std::size_t>(x)] =
            static_cast<std::uint32_t>(left);
      }
    }
    std::int32_t right = -1;
    for (std::int32_t x = width; x-- > 0;) {
      if (input.pixels.pixel(x, y)[3] != 0U) {
        right = x;
      }
      if (right < 0) {
        continue;
      }
      auto &source = extension.nearest_visible[
          row_offset + static_cast<std::size_t>(x)];
      if (source == kNoSource ||
          right - x <= x - static_cast<std::int32_t>(source)) {
        source = static_cast<std::uint32_t>(right);
      }
    }
  }

  // Complete the exact squared-Euclidean transform down each column. The
  // lower envelope uses integer intersection boundaries, avoiding floating
  // rounding differences between toolchains. A tie favors the later row, so
  // the combined two-pass rule is down, then right.
  std::vector<std::uint32_t> row_source_x(static_cast<std::size_t>(height));
  std::vector<std::int32_t> envelope_rows(static_cast<std::size_t>(height));
  std::vector<std::int64_t> envelope_starts(static_cast<std::size_t>(height));
  for (std::int32_t x = 0; x < width; ++x) {
    report_fraction(progress,
                    static_cast<std::uint64_t>(height) * 2U +
                        static_cast<std::uint64_t>(x),
                    total_work, FilterProgressStage::Filtering);
    for (std::int32_t y = 0; y < height; ++y) {
      row_source_x[static_cast<std::size_t>(y)] =
          extension.nearest_visible[static_cast<std::size_t>(y) *
                                        static_cast<std::size_t>(width) +
                                    static_cast<std::size_t>(x)];
    }

    std::int32_t envelope_size = 0;
    for (std::int32_t candidate = 0; candidate < height; ++candidate) {
      const auto candidate_x =
          row_source_x[static_cast<std::size_t>(candidate)];
      if (candidate_x == kNoSource) {
        continue;
      }
      std::int64_t start = std::numeric_limits<std::int64_t>::min();
      while (envelope_size > 0) {
        const auto previous = envelope_rows[
            static_cast<std::size_t>(envelope_size - 1)];
        const auto previous_x =
            row_source_x[static_cast<std::size_t>(previous)];
        const auto candidate_dx = static_cast<std::int64_t>(x) -
                                  static_cast<std::int64_t>(candidate_x);
        const auto previous_dx = static_cast<std::int64_t>(x) -
                                 static_cast<std::int64_t>(previous_x);
        const auto numerator =
            candidate_dx * candidate_dx +
            static_cast<std::int64_t>(candidate) * candidate -
            previous_dx * previous_dx -
            static_cast<std::int64_t>(previous) * previous;
        const auto denominator =
            2LL * (static_cast<std::int64_t>(candidate) - previous);
        start = ceil_divide(numerator, denominator);
        if (start > envelope_starts[
                        static_cast<std::size_t>(envelope_size - 1)]) {
          break;
        }
        --envelope_size;
      }
      if (envelope_size == 0) {
        start = std::numeric_limits<std::int64_t>::min();
      }
      envelope_rows[static_cast<std::size_t>(envelope_size)] = candidate;
      envelope_starts[static_cast<std::size_t>(envelope_size)] = start;
      ++envelope_size;
    }
    if (envelope_size == 0) {
      throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Filter color extension has no visible source"));
    }

    std::int32_t selected = 0;
    for (std::int32_t y = 0; y < height; ++y) {
      while (selected + 1 < envelope_size &&
             envelope_starts[static_cast<std::size_t>(selected + 1)] <= y) {
        ++selected;
      }
      const auto source_y =
          envelope_rows[static_cast<std::size_t>(selected)];
      const auto source_x =
          row_source_x[static_cast<std::size_t>(source_y)];
      const auto source_index =
          static_cast<std::uint64_t>(source_y) *
              static_cast<std::uint64_t>(width) +
          source_x;
      extension.nearest_visible[static_cast<std::size_t>(y) *
                                    static_cast<std::size_t>(width) +
                                static_cast<std::size_t>(x)] =
          static_cast<std::uint32_t>(source_index);
    }
  }
  report_fraction(progress, total_work, total_work,
                  FilterProgressStage::Filtering);
  return extension;
}

// Patent design constraint, do not regress (details in docs/smart-objects.md and
// docs/legal-constraints.md): these window
// filters must never build value histograms that are merged from per-column
// histograms or slid between windows, and Surface Blur must not use a value
// histogram AT ALL. Adobe US 7920741 (in force to 2030) claims sliding a
// window histogram along a scan line by merging column histograms; Adobe
// US 8594445 (in force to ~2032) claims box-window bilateral filtering
// computed by applying a range filter to any histogram of the window's
// pixel values. Median and Dust & Scratches therefore keep exactly ONE plain
// window histogram updated a single pixel value at a time (Huang 1979 prior
// art; no column histograms ever exist), and Surface Blur computes the same
// triangle-weighted averages with no histogram: direct accumulation at small
// radii, per-intensity-level box sums (the decomposition published by
// Durand & Dorsey 2002) at large radii.
struct WindowValueHistogram {
  std::array<std::uint32_t, 256> bins{};
  std::array<std::uint32_t, 16> coarse{};

  void add(std::uint8_t value) noexcept {
    ++bins[value];
    ++coarse[static_cast<std::size_t>(value >> 4U)];
  }

  void remove(std::uint8_t value) noexcept {
    --bins[value];
    --coarse[static_cast<std::size_t>(value >> 4U)];
  }

  [[nodiscard]] std::uint8_t median(std::uint32_t rank) const noexcept {
    std::size_t group = 0;
    while (group + 1U < coarse.size() && rank > coarse[group]) {
      rank -= coarse[group];
      ++group;
    }
    const auto first = group * 16U;
    std::size_t within = 0;
    while (within + 1U < 16U && rank > bins[first + within]) {
      rank -= bins[first + within];
      ++within;
    }
    return static_cast<std::uint8_t>(first + within);
  }
};

[[nodiscard]] std::uint8_t rounded_weighted_average(std::int64_t weight_sum,
                                                    std::int64_t weighted_sum) {
  if (weight_sum <= 0 || weighted_sum < 0) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Surface Blur produced an empty range kernel"));
  }
  const auto unsigned_weight_sum = static_cast<std::uint64_t>(weight_sum);
  const auto unsigned_weighted_sum = static_cast<std::uint64_t>(weighted_sum);
  auto quotient = unsigned_weighted_sum / unsigned_weight_sum;
  const auto remainder = unsigned_weighted_sum % unsigned_weight_sum;
  const auto complement = unsigned_weight_sum - remainder;
  if (remainder > complement ||
      (remainder == complement && (quotient & 1U) != 0U)) {
    ++quotient;
  }
  return static_cast<std::uint8_t>(std::min<std::uint64_t>(255U, quotient));
}

// Photoshop's Surface Blur range weight is the triangle 5*threshold - 2*|d|
// over the value delta d, kept only while positive. Indexed by |d|.
[[nodiscard]] std::array<std::int32_t, 256>
surface_blur_delta_weights(std::int32_t threshold) {
  const auto weight_base = 5 * threshold;
  const auto maximum_delta = (weight_base - 1) / 2;
  std::array<std::int32_t, 256> weights{};
  for (std::int32_t delta = 0; delta < 256; ++delta) {
    weights[static_cast<std::size_t>(delta)] =
        delta <= maximum_delta ? weight_base - 2 * delta : 0;
  }
  return weights;
}

template <typename Sample>
void filter_square_median_channel(const FilterRenderResult &input,
                                  PixelBuffer &output, std::size_t channel,
                                  std::int32_t radius, Sample &&sample,
                                  const FilterProgress *progress) {
  const auto width = input.bounds.width;
  const auto height = input.bounds.height;
  const auto diameter = radius * 2 + 1;
  const auto window_area = static_cast<std::uint32_t>(diameter) *
                           static_cast<std::uint32_t>(diameter);
  const auto median_rank = window_area / 2U + 1U;
  const auto clamp_x = [width](std::int32_t x) {
    return std::clamp(x, 0, width - 1);
  };
  const auto clamp_y = [height](std::int32_t y) {
    return std::clamp(y, 0, height - 1);
  };

  // One window histogram, updated one pixel value at a time along a
  // serpentine traversal. Every window sees the identical edge-clamped
  // sample multiset the old per-tile scheme produced.
  WindowValueHistogram window;
  for (std::int32_t dy = -radius; dy <= radius; ++dy) {
    const auto sy = clamp_y(dy);
    for (std::int32_t dx = -radius; dx <= radius; ++dx) {
      window.add(sample(clamp_x(dx), sy));
    }
  }

  std::int32_t x = 0;
  std::int32_t direction = 1;
  for (std::int32_t y = 0; y < height; ++y) {
    report_fraction(progress, static_cast<std::uint64_t>(y),
                    static_cast<std::uint64_t>(height),
                    FilterProgressStage::Filtering);
    while (true) {
      output.pixel(x, y)[channel] = window.median(median_rank);
      const auto next_x = x + direction;
      if (next_x < 0 || next_x >= width) {
        break;
      }
      const auto leaving_x =
          direction > 0 ? clamp_x(x - radius) : clamp_x(x + radius);
      const auto entering_x = direction > 0 ? clamp_x(next_x + radius)
                                            : clamp_x(next_x - radius);
      for (std::int32_t dy = -radius; dy <= radius; ++dy) {
        const auto sy = clamp_y(y + dy);
        window.remove(sample(leaving_x, sy));
        window.add(sample(entering_x, sy));
      }
      x = next_x;
    }
    if (y + 1 < height) {
      const auto leaving_y = clamp_y(y - radius);
      const auto entering_y = clamp_y(y + 1 + radius);
      for (std::int32_t dx = -radius; dx <= radius; ++dx) {
        const auto sx = clamp_x(x + dx);
        window.remove(sample(sx, leaving_y));
        window.add(sample(sx, entering_y));
      }
    }
    direction = -direction;
  }
  report_fraction(progress, static_cast<std::uint64_t>(height),
                  static_cast<std::uint64_t>(height),
                  FilterProgressStage::Filtering);
}

template <typename Sample>
void filter_square_surface_channel(const FilterRenderResult &input,
                                   PixelBuffer &output, std::size_t channel,
                                   std::int32_t radius,
                                   std::int32_t threshold, Sample &&sample,
                                   const FilterProgress *progress) {
  const auto width = input.bounds.width;
  const auto height = input.bounds.height;
  const auto clamp_x = [width](std::int32_t x) {
    return std::clamp(x, 0, width - 1);
  };
  const auto clamp_y = [height](std::int32_t y) {
    return std::clamp(y, 0, height - 1);
  };
  const auto weights = surface_blur_delta_weights(threshold);

  const auto row_stride = static_cast<std::size_t>(width);
  std::vector<std::uint8_t> plane(row_stride *
                                  static_cast<std::size_t>(height));
  for (std::int32_t y = 0; y < height; ++y) {
    auto *row = plane.data() + static_cast<std::size_t>(y) * row_stride;
    for (std::int32_t x = 0; x < width; ++x) {
      row[static_cast<std::size_t>(x)] = sample(x, y);
    }
  }
  const auto plane_row = [&plane, row_stride](std::int32_t y) {
    return plane.data() + static_cast<std::size_t>(y) * row_stride;
  };

  if (radius <= kSurfaceBlurDirectMaximumRadius) {
    for (std::int32_t y = 0; y < height; ++y) {
      report_fraction(progress, static_cast<std::uint64_t>(y),
                      static_cast<std::uint64_t>(height),
                      FilterProgressStage::Filtering);
      const auto *center_row = plane_row(y);
      for (std::int32_t x = 0; x < width; ++x) {
        const auto center =
            static_cast<std::int32_t>(center_row[static_cast<std::size_t>(x)]);
        std::int64_t weight_sum = 0;
        std::int64_t weighted_sum = 0;
        for (std::int32_t dy = -radius; dy <= radius; ++dy) {
          const auto *row = plane_row(clamp_y(y + dy));
          for (std::int32_t dx = -radius; dx <= radius; ++dx) {
            const auto value = static_cast<std::int32_t>(
                row[static_cast<std::size_t>(clamp_x(x + dx))]);
            const auto weight =
                weights[static_cast<std::size_t>(std::abs(value - center))];
            weight_sum += weight;
            weighted_sum += static_cast<std::int64_t>(weight) * value;
          }
        }
        output.pixel(x, y)[channel] =
            rounded_weighted_average(weight_sum, weighted_sum);
      }
    }
    report_fraction(progress, static_cast<std::uint64_t>(height),
                    static_cast<std::uint64_t>(height),
                    FilterProgressStage::Filtering);
    return;
  }

  // Large radii: for each intensity level that occurs as a center value,
  // box-sum the level's weight-transformed plane with sliding column and
  // window sums, then resolve the pixels whose center equals that level.
  std::array<bool, 256> present{};
  for (const auto value : plane) {
    present[value] = true;
  }
  std::uint64_t level_count = 0U;
  for (const auto flag : present) {
    level_count += flag ? 1U : 0U;
  }
  const auto total_steps =
      level_count * static_cast<std::uint64_t>(std::max(1, height));
  std::uint64_t completed_levels = 0U;

  std::vector<std::uint32_t> column_weight(static_cast<std::size_t>(width));
  std::vector<std::uint32_t> column_weighted(static_cast<std::size_t>(width));
  for (std::int32_t center = 0; center < 256; ++center) {
    if (!present[static_cast<std::size_t>(center)]) {
      continue;
    }
    std::array<std::uint32_t, 256> weight_lut{};
    std::array<std::uint32_t, 256> weighted_lut{};
    for (std::int32_t value = 0; value < 256; ++value) {
      const auto weight = static_cast<std::uint32_t>(
          weights[static_cast<std::size_t>(std::abs(value - center))]);
      weight_lut[static_cast<std::size_t>(value)] = weight;
      weighted_lut[static_cast<std::size_t>(value)] =
          weight * static_cast<std::uint32_t>(value);
    }

    std::fill(column_weight.begin(), column_weight.end(), 0U);
    std::fill(column_weighted.begin(), column_weighted.end(), 0U);
    for (std::int32_t dy = -radius; dy <= radius; ++dy) {
      const auto *row = plane_row(clamp_y(dy));
      for (std::int32_t x = 0; x < width; ++x) {
        const auto value = row[static_cast<std::size_t>(x)];
        column_weight[static_cast<std::size_t>(x)] += weight_lut[value];
        column_weighted[static_cast<std::size_t>(x)] += weighted_lut[value];
      }
    }

    for (std::int32_t y = 0; y < height; ++y) {
      if ((y & 63) == 0) {
        report_fraction(progress,
                        completed_levels *
                                static_cast<std::uint64_t>(height) +
                            static_cast<std::uint64_t>(y),
                        total_steps, FilterProgressStage::Filtering);
      }
      if (y > 0) {
        const auto *leaving = plane_row(clamp_y(y - 1 - radius));
        const auto *entering = plane_row(clamp_y(y + radius));
        for (std::int32_t x = 0; x < width; ++x) {
          const auto leaving_value = leaving[static_cast<std::size_t>(x)];
          const auto entering_value = entering[static_cast<std::size_t>(x)];
          column_weight[static_cast<std::size_t>(x)] +=
              weight_lut[entering_value] - weight_lut[leaving_value];
          column_weighted[static_cast<std::size_t>(x)] +=
              weighted_lut[entering_value] - weighted_lut[leaving_value];
        }
      }

      std::int64_t window_weight = 0;
      std::int64_t window_weighted = 0;
      for (std::int32_t dx = -radius; dx <= radius; ++dx) {
        const auto sx = static_cast<std::size_t>(clamp_x(dx));
        window_weight += column_weight[sx];
        window_weighted += column_weighted[sx];
      }
      const auto *center_row = plane_row(y);
      for (std::int32_t x = 0; x < width; ++x) {
        if (x > 0) {
          const auto leaving_x = static_cast<std::size_t>(clamp_x(x - 1 - radius));
          const auto entering_x = static_cast<std::size_t>(clamp_x(x + radius));
          window_weight +=
              static_cast<std::int64_t>(column_weight[entering_x]) -
              static_cast<std::int64_t>(column_weight[leaving_x]);
          window_weighted +=
              static_cast<std::int64_t>(column_weighted[entering_x]) -
              static_cast<std::int64_t>(column_weighted[leaving_x]);
        }
        if (static_cast<std::int32_t>(
                center_row[static_cast<std::size_t>(x)]) == center) {
          output.pixel(x, y)[channel] =
              rounded_weighted_average(window_weight, window_weighted);
        }
      }
    }
    ++completed_levels;
  }
  report_fraction(progress, total_steps, total_steps,
                  FilterProgressStage::Filtering);
}

[[nodiscard]] FilterRenderResult
render_median(const FilterRenderResult &input, double radius,
              const FilterProgress *progress) {
  if (input.pixels.empty()) {
    report_progress(progress, 1, 1, FilterProgressStage::Filtering);
    return input;
  }
  const auto effective_radius =
      std::max(1, static_cast<std::int32_t>(std::floor(radius)));
  constexpr int kPhaseCount = 5;
  auto extension_progress = phase_progress(progress, 0, kPhaseCount);
  const auto extension =
      extend_transparent_colors(input, &extension_progress);
  auto output = input.pixels;

  auto alpha_progress = phase_progress(progress, 1, kPhaseCount);
  const auto alpha_sample = [&input](std::int32_t x, std::int32_t y) {
    return input.pixels.pixel(x, y)[3];
  };
  filter_square_median_channel(input, output, 3U, effective_radius,
                               alpha_sample, &alpha_progress);

  // With no visible color source Photoshop's hidden-RGB result is not an
  // observable contract. Retain the source bytes instead of manufacturing
  // black under a fully transparent layer.
  if (extension.all_transparent) {
    report_progress(progress, 1, 1, FilterProgressStage::Filtering);
    return FilterRenderResult{std::move(output), input.bounds};
  }

  for (std::size_t channel = 0; channel < 3U; ++channel) {
    auto color_progress = phase_progress(
        progress, static_cast<int>(channel) + 2, kPhaseCount);
    const auto color_sample = [&input, &extension,
                               channel](std::int32_t x, std::int32_t y) {
      return extended_straight_color_sample(input, extension, x, y, channel);
    };
    filter_square_median_channel(input, output, channel, effective_radius,
                                 color_sample, &color_progress);
  }
  return FilterRenderResult{std::move(output), input.bounds};
}

[[nodiscard]] FilterRenderResult render_surface_blur(
    const FilterRenderResult &input, double radius, std::int32_t threshold,
    const FilterProgress *progress) {
  if (input.pixels.empty()) {
    report_progress(progress, 1, 1, FilterProgressStage::Filtering);
    return input;
  }
  const auto effective_radius = std::max(
      1, static_cast<std::int32_t>(std::floor(radius + 0.5)));
  constexpr int kPhaseCount = 5;
  auto extension_progress = phase_progress(progress, 0, kPhaseCount);
  const auto extension =
      extend_transparent_colors(input, &extension_progress);
  auto output = input.pixels;

  auto alpha_progress = phase_progress(progress, 1, kPhaseCount);
  const auto alpha_sample = [&input](std::int32_t x, std::int32_t y) {
    return input.pixels.pixel(x, y)[3];
  };
  filter_square_surface_channel(input, output, 3U, effective_radius,
                                threshold, alpha_sample, &alpha_progress);

  if (extension.all_transparent) {
    report_progress(progress, 1, 1, FilterProgressStage::Filtering);
    return FilterRenderResult{std::move(output), input.bounds};
  }

  for (std::size_t channel = 0; channel < 3U; ++channel) {
    auto color_progress = phase_progress(
        progress, static_cast<int>(channel) + 2, kPhaseCount);
    const auto color_sample = [&input, &extension,
                               channel](std::int32_t x, std::int32_t y) {
      return extended_straight_color_sample(input, extension, x, y, channel);
    };
    filter_square_surface_channel(input, output, channel, effective_radius,
                                  threshold, color_sample, &color_progress);
  }
  return FilterRenderResult{std::move(output), input.bounds};
}

[[nodiscard]] FilterRenderResult render_plastic_wrap_effect(
    const FilterRenderResult &input, std::int32_t highlight_strength,
    std::int32_t detail, std::int32_t smoothness,
    const FilterProgress *progress) {
  if (input.pixels.empty()) {
    report_progress(progress, 1, 1, FilterProgressStage::Filtering);
    return input;
  }

  constexpr int kPhaseCount = 5;
  auto extension_progress = phase_progress(progress, 0, kPhaseCount);
  const auto extension =
      extend_transparent_colors(input, &extension_progress);
  if (extension.all_transparent) {
    report_progress(progress, 1, 1, FilterProgressStage::Filtering);
    return input;
  }

  const auto width = input.bounds.width;
  const auto height = input.bounds.height;
  const auto pixel_count = static_cast<std::uint64_t>(width) *
                           static_cast<std::uint64_t>(height);
  if (pixel_count > std::numeric_limits<std::size_t>::max() /
                        sizeof(std::uint16_t)) {
    throw std::overflow_error("Plastic Wrap working buffer overflow");
  }
  const auto count = static_cast<std::size_t>(pixel_count);
  std::vector<std::uint16_t> luminance(count);
  std::vector<std::uint16_t> horizontal(count);
  std::vector<std::uint16_t> height_field(count);
  const auto index_of = [width](std::int32_t x, std::int32_t y) {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
           static_cast<std::size_t>(x);
  };

  auto luminance_progress = phase_progress(progress, 1, kPhaseCount);
  for (std::int32_t y = 0; y < height; ++y) {
    report_progress(&luminance_progress, y, height,
                    FilterProgressStage::Filtering);
    for (std::int32_t x = 0; x < width; ++x) {
      const auto red = static_cast<std::uint32_t>(
          extended_straight_color_sample(input, extension, x, y, 0U));
      const auto green = static_cast<std::uint32_t>(
          extended_straight_color_sample(input, extension, x, y, 1U));
      const auto blue = static_cast<std::uint32_t>(
          extended_straight_color_sample(input, extension, x, y, 2U));
      const auto alpha = static_cast<std::uint32_t>(
          input.pixels.pixel(x, y)[3]);
      const auto straight_luminance =
          (77U * red + 150U * green + 29U * blue + 128U) >> 8U;
      luminance[index_of(x, y)] = static_cast<std::uint16_t>(
          (straight_luminance * alpha + 127U) / 255U);
    }
  }
  report_progress(&luminance_progress, height, height,
                  FilterProgressStage::Filtering);

  const auto radius = 1 + (smoothness - 1) / 3;
  const auto diameter = radius * 2 + 1;
  auto horizontal_progress = phase_progress(progress, 2, kPhaseCount);
  for (std::int32_t y = 0; y < height; ++y) {
    report_progress(&horizontal_progress, y, height,
                    FilterProgressStage::Filtering);
    std::uint32_t sum = 0U;
    for (std::int32_t offset = -radius; offset <= radius; ++offset) {
      sum += luminance[index_of(std::clamp(offset, 0, width - 1), y)];
    }
    for (std::int32_t x = 0; x < width; ++x) {
      horizontal[index_of(x, y)] = static_cast<std::uint16_t>(
          (sum + static_cast<std::uint32_t>(diameter / 2)) /
          static_cast<std::uint32_t>(diameter));
      const auto leaving = std::clamp(x - radius, 0, width - 1);
      const auto entering = std::clamp(x + radius + 1, 0, width - 1);
      sum += luminance[index_of(entering, y)];
      sum -= luminance[index_of(leaving, y)];
    }
  }
  report_progress(&horizontal_progress, height, height,
                  FilterProgressStage::Filtering);

  auto vertical_progress = phase_progress(progress, 3, kPhaseCount);
  for (std::int32_t x = 0; x < width; ++x) {
    report_progress(&vertical_progress, x, width,
                    FilterProgressStage::Filtering);
    std::uint32_t sum = 0U;
    for (std::int32_t offset = -radius; offset <= radius; ++offset) {
      sum += horizontal[index_of(x, std::clamp(offset, 0, height - 1))];
    }
    for (std::int32_t y = 0; y < height; ++y) {
      const auto smoothed = static_cast<std::uint32_t>(
          (sum + static_cast<std::uint32_t>(diameter / 2)) /
          static_cast<std::uint32_t>(diameter));
      const auto source =
          static_cast<std::uint32_t>(luminance[index_of(x, y)]);
      height_field[index_of(x, y)] = static_cast<std::uint16_t>(
          (smoothed * static_cast<std::uint32_t>(15 - detail) +
           source * static_cast<std::uint32_t>(detail) + 7U) /
          15U);
      const auto leaving = std::clamp(y - radius, 0, height - 1);
      const auto entering = std::clamp(y + radius + 1, 0, height - 1);
      sum += horizontal[index_of(x, entering)];
      sum -= horizontal[index_of(x, leaving)];
    }
  }
  report_progress(&vertical_progress, width, width,
                  FilterProgressStage::Filtering);

  auto output = input.pixels;
  auto shading_progress = phase_progress(progress, 4, kPhaseCount);
  // Patent/design boundary: this is one fixed local height-field treatment.
  // It does not infer materials or lighting from image content, search or
  // synthesize patches, classify objects, or select an algorithm adaptively.
  // The three user settings only scale this same box-blur/gradient formula.
  for (std::int32_t y = 0; y < height; ++y) {
    report_progress(&shading_progress, y, height,
                    FilterProgressStage::Filtering);
    for (std::int32_t x = 0; x < width; ++x) {
      const auto left = height_field[index_of(std::max(0, x - 1), y)];
      const auto right =
          height_field[index_of(std::min(width - 1, x + 1), y)];
      const auto up = height_field[index_of(x, std::max(0, y - 1))];
      const auto down =
          height_field[index_of(x, std::min(height - 1, y + 1))];
      const auto gradient_x = static_cast<std::int32_t>(right) - left;
      const auto gradient_y = static_cast<std::int32_t>(down) - up;
      const auto facing = -3 * gradient_x - 4 * gradient_y;
      const auto edge = std::abs(gradient_x) + std::abs(gradient_y);
      // Keep medium-contrast contours visible. The original divisors reduced
      // ordinary photographic gradients to one or two byte values, making the
      // filter look like an identity unless the source contained hard edges.
      // This remains the same fixed local formula: the stronger relief and
      // ridge response are constants, not measurements selected from content.
      const auto relief = std::clamp(facing / 3, -112, 112);
      const auto ridge_signal = std::clamp(edge * 8, 0, 255);
      const auto specular_signal = std::clamp(
          std::max(0, facing) + ridge_signal, 0, 255);
      const auto curved_specular =
          (specular_signal * 2 +
           (specular_signal * specular_signal + 127) / 255 + 1) /
          3;
      const auto shine =
          (curved_specular * highlight_strength + 10) / 20;
      const auto *source = input.pixels.pixel(x, y);
      auto *destination = output.pixel(x, y);
      for (std::size_t channel = 0; channel < 3U; ++channel) {
        const auto shaded =
            std::clamp(static_cast<int>(source[channel]) + relief, 0, 255);
        destination[channel] = static_cast<std::uint8_t>(std::clamp(
            shaded + ((255 - shaded) * shine + 127) / 255, 0, 255));
      }
      destination[3] = source[3];
    }
  }
  report_progress(&shading_progress, height, height,
                  FilterProgressStage::Filtering);
  return FilterRenderResult{std::move(output), input.bounds};
}

[[nodiscard]] FilterRenderResult render_dust_and_scratches(
    const FilterRenderResult &input, std::int32_t radius,
    std::int32_t threshold, const FilterProgress *progress) {
  if (input.pixels.empty()) {
    report_progress(progress, 1, 1, FilterProgressStage::Filtering);
    return input;
  }

  constexpr int kPhaseCount = 5;
  auto extension_progress = phase_progress(progress, 0, kPhaseCount);
  const auto extension =
      extend_transparent_colors(input, &extension_progress);
  if (extension.all_transparent) {
    report_progress(progress, 1, 1, FilterProgressStage::Filtering);
    return input;
  }

  auto output = input.pixels;
  for (std::size_t channel = 0; channel < 3U; ++channel) {
    auto color_progress = phase_progress(
        progress, static_cast<int>(channel) + 1, kPhaseCount);
    const auto color_sample = [&input, &extension,
                               channel](std::int32_t x, std::int32_t y) {
      return extended_straight_color_sample(input, extension, x, y, channel);
    };
    filter_square_median_channel(input, output, channel, radius,
                                 color_sample, &color_progress);
  }

  auto comparison_progress = phase_progress(progress, 4, kPhaseCount);
  for (std::int32_t y = 0; y < input.bounds.height; ++y) {
    report_progress(&comparison_progress, y, input.bounds.height,
                    FilterProgressStage::Filtering);
    for (std::int32_t x = 0; x < input.bounds.width; ++x) {
      auto *destination = output.pixel(x, y);
      const std::array<std::uint8_t, 3> median{
          destination[0], destination[1], destination[2]};
      std::array<std::uint8_t, 3> source{};
      std::int32_t difference = 0;
      for (std::size_t channel = 0; channel < source.size(); ++channel) {
        source[channel] =
            extended_straight_color_sample(input, extension, x, y, channel);
        difference = std::max(
            difference,
            std::abs(static_cast<std::int32_t>(source[channel]) -
                     static_cast<std::int32_t>(median[channel])));
      }
      const auto replace = difference > threshold;
      for (std::size_t channel = 0; channel < source.size(); ++channel) {
        destination[channel] = replace ? median[channel] : source[channel];
      }
      // Dust & Scratches filters straight RGB only. Alpha remains byte-exact.
      destination[3] = input.pixels.pixel(x, y)[3];
    }
  }
  report_progress(&comparison_progress, input.bounds.height,
                  input.bounds.height, FilterProgressStage::Filtering);
  return FilterRenderResult{std::move(output), input.bounds};
}

[[nodiscard]] FilterRenderResult
blend_entry_result(const FilterRenderResult &before,
                   FilterRenderResult filtered, double opacity,
                   BlendMode blend_mode, const FilterProgress *progress) {
  if (opacity >= 1.0 && blend_mode == BlendMode::Normal) {
    report_progress(progress, 1, 1, FilterProgressStage::Filtering);
    return filtered;
  }

  constexpr std::uint64_t kOpacityScale = 65535U;
  constexpr std::uint64_t kChannelScale = 255U;
  const auto effect_weight = static_cast<std::uint64_t>(
      std::floor(opacity * static_cast<double>(kOpacityScale) + 0.5));
  const auto bounds = checked_union_bounds(before.bounds, filtered.bounds,
                                           "Smart Filter result bounds overflow");
  PixelBuffer output(bounds.width, bounds.height, PixelFormat::rgba8());
  output.clear(0);

  for (std::int32_t y = 0; y < bounds.height; ++y) {
    report_progress(progress, y, bounds.height, FilterProgressStage::Filtering);
    const auto document_y = bounds.y + y;
    for (std::int32_t x = 0; x < bounds.width; ++x) {
      const auto document_x = bounds.x + x;
      const auto *destination = sample_result(before, document_x, document_y);
      const auto *source = sample_result(filtered, document_x, document_y);
      const auto destination_alpha = static_cast<std::uint64_t>(
          destination != nullptr ? destination[3] : 0U);
      const auto source_alpha =
          static_cast<std::uint64_t>(source != nullptr ? source[3] : 0U);
      std::array<std::uint8_t, 3> destination_rgb{};
      std::array<std::uint8_t, 3> source_rgb{};
      if (destination != nullptr) {
        std::copy_n(destination, 3, destination_rgb.begin());
      }
      if (source != nullptr) {
        std::copy_n(source, 3, source_rgb.begin());
      }
      const auto effect_rgb =
          blend_mode == BlendMode::Normal || source_alpha == 0U ||
                  destination_alpha == 0U
              ? source_rgb
              : blend_rgb(source_rgb, destination_rgb, blend_mode);
      // Photoshop's non-default Smart Filter blending options composite the
      // opacity-scaled filtered result source-over the previous stack result.
      // Keep the Normal/100% replacement fast path above: that is the native
      // filter operation itself and is what gives a blurred transparent impulse
      // its calibrated alpha. Once opacity or blend mode is changed, the entry
      // uses the same source-over blend equation as a layer.
      const auto effective_source_alpha = source_alpha * effect_weight;
      const auto destination_weight =
          destination_alpha *
          (kChannelScale * kOpacityScale - effective_source_alpha);
      const auto source_outside_destination_weight =
          (kChannelScale - destination_alpha) * effective_source_alpha;
      const auto blend_overlap_weight =
          destination_alpha * effective_source_alpha;
      const auto output_alpha_numerator =
          destination_weight + source_outside_destination_weight +
          blend_overlap_weight;
      auto *pixel = output.pixel(x, y);
      for (std::size_t channel = 0; channel < 3U; ++channel) {
        if (output_alpha_numerator == 0U) {
          pixel[channel] = 0;
          continue;
        }
        const auto premultiplied_numerator =
            static_cast<std::uint64_t>(destination_rgb[channel]) *
                destination_weight +
            static_cast<std::uint64_t>(source_rgb[channel]) *
                source_outside_destination_weight +
            static_cast<std::uint64_t>(effect_rgb[channel]) *
                blend_overlap_weight;
        pixel[channel] = static_cast<std::uint8_t>(std::min<std::uint64_t>(
            255U, (premultiplied_numerator + output_alpha_numerator / 2U) /
                      output_alpha_numerator));
      }
      pixel[3] = static_cast<std::uint8_t>(std::min<std::uint64_t>(
          255U,
          (output_alpha_numerator +
           (kChannelScale * kOpacityScale) / 2U) /
              (kChannelScale * kOpacityScale)));
    }
  }
  report_progress(progress, bounds.height, bounds.height,
                  FilterProgressStage::Filtering);
  return FilterRenderResult{std::move(output), bounds};
}

[[nodiscard]] std::uint8_t sample_filter_mask(const SmartFilterMask &mask,
                                              std::int32_t document_x,
                                              std::int32_t document_y) {
  if (!mask.enabled) {
    return 255;
  }
  const auto local_x = static_cast<std::int64_t>(document_x) - mask.bounds.x;
  const auto local_y = static_cast<std::int64_t>(document_y) - mask.bounds.y;
  if (!mask.pixels.empty() && local_x >= 0 && local_y >= 0 &&
      local_x < mask.bounds.width && local_y < mask.bounds.height) {
    return mask.pixels.pixel(static_cast<std::int32_t>(local_x),
                             static_cast<std::int32_t>(local_y))[0];
  }
  return mask.extend_with_white ? 255 : mask.default_color;
}

[[nodiscard]] PixelBuffer crop_buffer(const PixelBuffer &source,
                                      Rect source_bounds, Rect crop_bounds) {
  PixelBuffer cropped(crop_bounds.width, crop_bounds.height, source.format());
  const auto source_x = crop_bounds.x - source_bounds.x;
  const auto source_y = crop_bounds.y - source_bounds.y;
  const auto row_bytes = static_cast<std::size_t>(crop_bounds.width) * 4U;
  for (std::int32_t y = 0; y < crop_bounds.height; ++y) {
    const auto *source_row = source.pixel(source_x, source_y + y);
    auto *destination_row = cropped.pixel(0, y);
    std::copy(source_row, source_row + row_bytes, destination_row);
  }
  return cropped;
}

[[nodiscard]] FilterRenderResult
trim_transparent_result(FilterRenderResult result) {
  std::int32_t minimum_x = result.bounds.width;
  std::int32_t minimum_y = result.bounds.height;
  std::int32_t maximum_x = -1;
  std::int32_t maximum_y = -1;
  for (std::int32_t y = 0; y < result.bounds.height; ++y) {
    for (std::int32_t x = 0; x < result.bounds.width; ++x) {
      if (result.pixels.pixel(x, y)[3] == 0U) {
        continue;
      }
      minimum_x = std::min(minimum_x, x);
      minimum_y = std::min(minimum_y, y);
      maximum_x = std::max(maximum_x, x);
      maximum_y = std::max(maximum_y, y);
    }
  }
  if (maximum_x < minimum_x || maximum_y < minimum_y) {
    return result;
  }
  const Rect cropped_bounds{result.bounds.x + minimum_x,
                            result.bounds.y + minimum_y,
                            maximum_x - minimum_x + 1,
                            maximum_y - minimum_y + 1};
  if (cropped_bounds.x == result.bounds.x &&
      cropped_bounds.y == result.bounds.y &&
      cropped_bounds.width == result.bounds.width &&
      cropped_bounds.height == result.bounds.height) {
    return result;
  }
  return FilterRenderResult{
      crop_buffer(result.pixels, result.bounds, cropped_bounds),
      cropped_bounds};
}

[[nodiscard]] FilterRenderResult
apply_stack_mask(const FilterRenderResult &base,
                 const FilterRenderResult &filtered,
                 const SmartFilterMask &mask, const FilterProgress *progress) {
  constexpr std::uint64_t kMaskScale = 255U;
  const auto bounds = checked_union_bounds(base.bounds, filtered.bounds,
                                           "Smart Filter result bounds overflow");
  PixelBuffer output(bounds.width, bounds.height, PixelFormat::rgba8());
  output.clear(0);
  std::int32_t minimum_x = bounds.width;
  std::int32_t minimum_y = bounds.height;
  std::int32_t maximum_x = -1;
  std::int32_t maximum_y = -1;

  for (std::int32_t y = 0; y < bounds.height; ++y) {
    report_progress(progress, y, bounds.height, FilterProgressStage::Filtering);
    const auto document_y = bounds.y + y;
    for (std::int32_t x = 0; x < bounds.width; ++x) {
      const auto document_x = bounds.x + x;
      const auto *destination = sample_result(base, document_x, document_y);
      const auto *source = sample_result(filtered, document_x, document_y);
      const auto effect_weight = static_cast<std::uint64_t>(
          sample_filter_mask(mask, document_x, document_y));
      const auto before_weight = kMaskScale - effect_weight;
      const auto destination_alpha = static_cast<std::uint64_t>(
          destination != nullptr ? destination[3] : 0U);
      const auto source_alpha =
          static_cast<std::uint64_t>(source != nullptr ? source[3] : 0U);
      const auto output_alpha_numerator =
          destination_alpha * before_weight + source_alpha * effect_weight;
      auto *pixel = output.pixel(x, y);
      for (std::size_t channel = 0; channel < 3U; ++channel) {
        if (output_alpha_numerator == 0U) {
          pixel[channel] = 0;
          continue;
        }
        const auto destination_color = static_cast<std::uint64_t>(
            destination != nullptr ? destination[channel] : 0U);
        const auto source_color = static_cast<std::uint64_t>(
            source != nullptr ? source[channel] : 0U);
        const auto premultiplied_numerator =
            destination_color * destination_alpha * before_weight +
            source_color * source_alpha * effect_weight;
        pixel[channel] = static_cast<std::uint8_t>(std::min<std::uint64_t>(
            255U, (premultiplied_numerator + output_alpha_numerator / 2U) /
                      output_alpha_numerator));
      }
      pixel[3] = static_cast<std::uint8_t>(std::min<std::uint64_t>(
          255U, (output_alpha_numerator + kMaskScale / 2U) / kMaskScale));
      if (pixel[3] != 0U) {
        minimum_x = std::min(minimum_x, x);
        minimum_y = std::min(minimum_y, y);
        maximum_x = std::max(maximum_x, x);
        maximum_y = std::max(maximum_y, y);
      }
    }
  }
  report_progress(progress, bounds.height, bounds.height,
                  FilterProgressStage::Filtering);

  Rect cropped_bounds;
  if (maximum_x < minimum_x || maximum_y < minimum_y) {
    cropped_bounds = base.bounds;
  } else {
    cropped_bounds = Rect{bounds.x + minimum_x, bounds.y + minimum_y,
                          maximum_x - minimum_x + 1, maximum_y - minimum_y + 1};
  }
  if (cropped_bounds.x == bounds.x && cropped_bounds.y == bounds.y &&
      cropped_bounds.width == bounds.width &&
      cropped_bounds.height == bounds.height) {
    return FilterRenderResult{std::move(output), bounds};
  }
  return FilterRenderResult{crop_buffer(output, bounds, cropped_bounds),
                            cropped_bounds};
}

// The destructive Box Blur math, verbatim: an alpha-weighted separable box
// average over an edge-clamped window, accumulated in doubles, written as
// straight color with the normalized alpha. Used for radii through
// kBoxBlurDirectMaximumRadius so the smart filter matches the destructive
// path byte for byte.
[[nodiscard]] FilterRenderResult render_box_blur_direct(
    const FilterRenderResult &input, std::int32_t radius,
    const FilterProgress *progress) {
  const auto width = input.pixels.width();
  const auto height = input.pixels.height();
  auto result = input;
  const auto taps = 2 * radius + 1;
  const auto axis_weight_sum = static_cast<double>(taps);
  const auto total_weight = axis_weight_sum * axis_weight_sum;

  const auto row_stride = static_cast<std::size_t>(width) * 4U;
  std::vector<double> h_rows(row_stride * static_cast<std::size_t>(taps), 0.0);
  int h_rows_built_through = -1;
  const auto build_h_row = [&](std::int32_t source_y, double *out) {
    std::fill(out, out + row_stride, 0.0);
    for (int dx = -radius; dx <= radius; ++dx) {
      for (std::int32_t x = 0; x < width; ++x) {
        const auto sx = std::clamp<std::int32_t>(x + dx, 0, width - 1);
        const auto *px = input.pixels.pixel(sx, source_y);
        const auto alpha = static_cast<double>(px[3]) / 255.0;
        auto *accum = out + static_cast<std::size_t>(x) * 4U;
        for (int channel = 0; channel < 3; ++channel) {
          accum[channel] += static_cast<double>(px[channel]) * alpha;
        }
        accum[3] += alpha;
      }
    }
  };
  const auto h_row_for = [&](std::int32_t source_y) -> const double * {
    return h_rows.data() +
           static_cast<std::size_t>(source_y % taps) * row_stride;
  };

  std::vector<double> v_accum(row_stride);
  for (std::int32_t y = 0; y < height; ++y) {
    report_progress(progress, y, height, FilterProgressStage::Blurring);
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
      const auto *h_row = h_row_for(sy);
      for (std::size_t i = 0; i < row_stride; ++i) {
        v_accum[i] += h_row[i];
      }
    }
    for (std::int32_t x = 0; x < width; ++x) {
      const auto *accum = v_accum.data() + static_cast<std::size_t>(x) * 4U;
      auto *dst = result.pixels.pixel(x, y);
      const auto alpha_sum = accum[3];
      for (int channel = 0; channel < 3; ++channel) {
        const auto value =
            alpha_sum > 0.000001 ? accum[channel] / alpha_sum : 0.0;
        dst[channel] = static_cast<std::uint8_t>(
            std::clamp(std::lround(value), 0L, 255L));
      }
      dst[3] = static_cast<std::uint8_t>(std::clamp(
          std::lround(alpha_sum / total_weight * 255.0), 0L, 255L));
    }
  }
  report_progress(progress, height, height, FilterProgressStage::Blurring);
  return result;
}

// Large-radius Box Blur: an exact integer sliding-window box average with
// the same edge-clamped sampling. The alpha-weighted quotient
// sum(color * alpha) / sum(alpha) is computed on raw bytes (the /255
// normalization cancels), so the math is deterministic across toolchains.
[[nodiscard]] FilterRenderResult render_box_blur_sliding(
    const FilterRenderResult &input, std::int32_t radius,
    const FilterProgress *progress) {
  const auto width = input.pixels.width();
  const auto height = input.pixels.height();
  auto result = input;
  const auto taps = static_cast<std::int64_t>(2) * radius + 1;
  const auto total_weight = static_cast<double>(taps) * static_cast<double>(taps);

  const auto row_stride = static_cast<std::size_t>(width) * 4U;
  const auto term = [&](std::int32_t x, std::int32_t y, int channel) {
    const auto *px = input.pixels.pixel(x, y);
    return channel < 3 ? static_cast<std::int64_t>(px[channel]) *
                             static_cast<std::int64_t>(px[3])
                       : static_cast<std::int64_t>(px[3]);
  };
  // Horizontal sliding sums for one source row.
  std::vector<std::int64_t> h_row(row_stride);
  const auto build_h_row = [&](std::int32_t source_y) {
    std::array<std::int64_t, 4> window{};
    for (std::int32_t t = -radius; t <= radius; ++t) {
      const auto sx = std::clamp<std::int32_t>(t, 0, width - 1);
      for (int channel = 0; channel < 4; ++channel) {
        window[static_cast<std::size_t>(channel)] += term(sx, source_y, channel);
      }
    }
    for (std::int32_t x = 0; x < width; ++x) {
      auto *out = h_row.data() + static_cast<std::size_t>(x) * 4U;
      for (int channel = 0; channel < 4; ++channel) {
        out[channel] = window[static_cast<std::size_t>(channel)];
      }
      const auto leaving = std::clamp<std::int32_t>(x - radius, 0, width - 1);
      const auto entering =
          std::clamp<std::int32_t>(x + 1 + radius, 0, width - 1);
      for (int channel = 0; channel < 4; ++channel) {
        window[static_cast<std::size_t>(channel)] +=
            term(entering, source_y, channel) - term(leaving, source_y, channel);
      }
    }
  };
  // Vertical sliding sums of the horizontal sums.
  std::vector<std::int64_t> v_accum(row_stride, 0);
  const auto add_row = [&](std::int32_t source_y, std::int64_t sign) {
    build_h_row(source_y);
    for (std::size_t i = 0; i < row_stride; ++i) {
      v_accum[i] += sign * h_row[i];
    }
  };
  for (std::int32_t t = -radius; t <= radius; ++t) {
    add_row(std::clamp<std::int32_t>(t, 0, height - 1), 1);
  }
  for (std::int32_t y = 0; y < height; ++y) {
    report_progress(progress, y, height, FilterProgressStage::Blurring);
    for (std::int32_t x = 0; x < width; ++x) {
      const auto *accum = v_accum.data() + static_cast<std::size_t>(x) * 4U;
      auto *dst = result.pixels.pixel(x, y);
      const auto alpha_sum = accum[3];
      for (int channel = 0; channel < 3; ++channel) {
        const auto value = alpha_sum > 0
                               ? static_cast<double>(accum[channel]) /
                                     static_cast<double>(alpha_sum)
                               : 0.0;
        dst[channel] = static_cast<std::uint8_t>(
            std::clamp(std::lround(value), 0L, 255L));
      }
      dst[3] = static_cast<std::uint8_t>(std::clamp(
          std::lround(static_cast<double>(alpha_sum) / total_weight), 0L,
          255L));
    }
    if (y + 1 < height) {
      add_row(std::clamp<std::int32_t>(y - radius, 0, height - 1), -1);
      add_row(std::clamp<std::int32_t>(y + 1 + radius, 0, height - 1), 1);
    }
  }
  report_progress(progress, height, height, FilterProgressStage::Blurring);
  return result;
}

[[nodiscard]] FilterRenderResult render_box_blur_effect(
    const FilterRenderResult &input, std::int32_t radius,
    const FilterProgress *progress) {
  if (input.pixels.empty()) {
    report_progress(progress, 1, 1, FilterProgressStage::Blurring);
    return input;
  }
  radius = std::clamp(radius, 1,
                      static_cast<std::int32_t>(kMaximumBoxBlurRadius));
  if (radius <= kBoxBlurDirectMaximumRadius) {
    return render_box_blur_direct(input, radius, progress);
  }
  return render_box_blur_sliding(input, radius, progress);
}

// The destructive Emboss math: bilinear edge-clamped luminance samples at
// +/- the height offset along the angle, written as clamp(128 + (highlight -
// shadow) * amount / 100) into all three channels. Alpha and bounds are
// preserved. Luminance is the destructive path's (30R + 59G + 11B) / 100
// integer formula.
[[nodiscard]] FilterRenderResult render_emboss_effect(
    const FilterRenderResult &input, std::int32_t angle_degrees,
    std::int32_t height_pixels, std::int32_t amount_percent,
    const FilterProgress *progress) {
  if (input.pixels.empty()) {
    report_progress(progress, 1, 1, FilterProgressStage::Embossing);
    return input;
  }
  constexpr double kPi = 3.14159265358979323846;
  const auto luminance = [](const std::uint8_t *px) {
    return (static_cast<int>(px[0]) * 30 + static_cast<int>(px[1]) * 59 +
            static_cast<int>(px[2]) * 11) /
           100;
  };
  const auto sampled_luminance = [&](double x, double y) {
    x = std::clamp(
        x, 0.0,
        static_cast<double>(
            std::max<std::int32_t>(0, input.pixels.width() - 1)));
    y = std::clamp(
        y, 0.0,
        static_cast<double>(
            std::max<std::int32_t>(0, input.pixels.height() - 1)));
    const auto x0 = static_cast<std::int32_t>(std::floor(x));
    const auto y0 = static_cast<std::int32_t>(std::floor(y));
    const auto x1 = std::min<std::int32_t>(input.pixels.width() - 1, x0 + 1);
    const auto y1 = std::min<std::int32_t>(input.pixels.height() - 1, y0 + 1);
    const auto tx = x - static_cast<double>(x0);
    const auto ty = y - static_cast<double>(y0);
    const auto l00 = static_cast<double>(luminance(input.pixels.pixel(x0, y0)));
    const auto l10 = static_cast<double>(luminance(input.pixels.pixel(x1, y0)));
    const auto l01 = static_cast<double>(luminance(input.pixels.pixel(x0, y1)));
    const auto l11 = static_cast<double>(luminance(input.pixels.pixel(x1, y1)));
    const auto top = l00 * (1.0 - tx) + l10 * tx;
    const auto bottom = l01 * (1.0 - tx) + l11 * tx;
    return top * (1.0 - ty) + bottom * ty;
  };
  const auto angle = static_cast<double>(angle_degrees) * kPi / 180.0;
  const auto distance = static_cast<double>(height_pixels);
  const auto offset_x = std::cos(angle) * distance;
  const auto offset_y = -std::sin(angle) * distance;
  auto result = input;
  for (std::int32_t y = 0; y < result.pixels.height(); ++y) {
    report_progress(progress, y, result.pixels.height(),
                    FilterProgressStage::Embossing);
    for (std::int32_t x = 0; x < result.pixels.width(); ++x) {
      const auto highlight = sampled_luminance(
          static_cast<double>(x) - offset_x, static_cast<double>(y) - offset_y);
      const auto shadow = sampled_luminance(
          static_cast<double>(x) + offset_x, static_cast<double>(y) + offset_y);
      const auto value = static_cast<std::uint8_t>(std::clamp(
          std::lround(128.0 + (highlight - shadow) *
                                  static_cast<double>(amount_percent) / 100.0),
          0L, 255L));
      auto *px = result.pixels.pixel(x, y);
      px[0] = value;
      px[1] = value;
      px[2] = value;
    }
  }
  report_progress(progress, result.pixels.height(), result.pixels.height(),
                  FilterProgressStage::Embossing);
  return result;
}

// Verbatim replicas of filter_engine.cpp's bilinear premultiplied sampling
// helpers so render_radial_blur stays byte-identical to the destructive
// patchy.filters.radial_blur (pinned by
// smart_filter_radial_blur_matches_destructive); keep both in sync.
constexpr double kRadialBlurPi = 3.14159265358979323846;

struct RadialBlurAccum {
  std::array<double, 3> premultiplied_color{0.0, 0.0, 0.0};
  double alpha{0.0};
  double weight{0.0};
};

void radial_blur_accumulate_pixel(RadialBlurAccum &accum,
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

void radial_blur_accumulate_sample(RadialBlurAccum &accum,
                                   const PixelBuffer &original, double x,
                                   double y, double weight = 1.0) {
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
  radial_blur_accumulate_pixel(accum, original, original.pixel(x0, y0),
                               weight * (1.0 - tx) * (1.0 - ty));
  radial_blur_accumulate_pixel(accum, original, original.pixel(x1, y0),
                               weight * tx * (1.0 - ty));
  radial_blur_accumulate_pixel(accum, original, original.pixel(x0, y1),
                               weight * (1.0 - tx) * ty);
  radial_blur_accumulate_pixel(accum, original, original.pixel(x1, y1),
                               weight * tx * ty);
}

void radial_blur_write_pixel(PixelBuffer &pixels, std::int32_t x,
                             std::int32_t y, const RadialBlurAccum &accum) {
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
    dst[channel] = static_cast<std::uint8_t>(
        std::clamp(std::lround(value), 0L, 255L));
  }
  if (channels >= 4) {
    dst[3] = static_cast<std::uint8_t>(
        std::clamp(std::lround(normalized_alpha * 255.0), 0L, 255L));
  }
}

// The destructive Radial Blur math: a rotational sample sweep of
// amount * 3.6 degrees about the supplied center in buffer coordinates.
[[nodiscard]] FilterRenderResult render_radial_blur_effect(
    const FilterRenderResult &input, std::int32_t amount, std::int32_t samples,
    double center_x, double center_y, const FilterProgress *progress) {
  if (input.pixels.empty()) {
    report_progress(progress, 1, 1, FilterProgressStage::Blurring);
    return input;
  }
  FilterRenderResult result{
      PixelBuffer(input.bounds.width, input.bounds.height,
                  PixelFormat::rgba8()),
      input.bounds};
  const auto clamped_amount = std::clamp(amount, 0, 100);
  const auto clamped_samples = std::clamp(samples, 4, 32);
  const auto sweep =
      static_cast<double>(clamped_amount) * 3.6 * kRadialBlurPi / 180.0;
  for (std::int32_t y = 0; y < input.bounds.height; ++y) {
    report_progress(progress, y, input.bounds.height,
                    FilterProgressStage::Blurring);
    for (std::int32_t x = 0; x < input.bounds.width; ++x) {
      const auto dx = static_cast<double>(x) - center_x;
      const auto dy = static_cast<double>(y) - center_y;
      RadialBlurAccum accum;
      for (int sample = 0; sample < clamped_samples; ++sample) {
        const auto t =
            clamped_samples <= 1
                ? 0.0
                : static_cast<double>(sample) /
                          static_cast<double>(clamped_samples - 1) -
                      0.5;
        const auto angle = sweep * t;
        const auto source_x =
            center_x + dx * std::cos(angle) - dy * std::sin(angle);
        const auto source_y =
            center_y + dx * std::sin(angle) + dy * std::cos(angle);
        radial_blur_accumulate_sample(accum, input.pixels, source_x, source_y);
      }
      radial_blur_write_pixel(result.pixels, x, y, accum);
    }
  }
  report_progress(progress, input.bounds.height, input.bounds.height,
                  FilterProgressStage::Blurring);
  return result;
}

// The same position-hash mix as filter_engine.cpp's filter_noise_hash; keep
// both in sync (smart_filter_add_noise_matches_destructive pins parity).
[[nodiscard]] std::uint32_t add_noise_hash(std::int32_t x, std::int32_t y,
                                           std::uint32_t seed) noexcept {
  auto value = static_cast<std::uint32_t>(x + 16384) * 374761393U;
  value ^= static_cast<std::uint32_t>(y + 8192) * 668265263U;
  value ^= seed * 2246822519U;
  value ^= value >> 13U;
  value *= 1274126177U;
  value ^= value >> 16U;
  return value;
}

// Deterministic Add Noise: RGB gains position-hashed deltas, alpha and
// bounds stay byte-identical. All math is hash integers plus exactly-rounded
// IEEE multiplies, so outputs are cross-toolchain stable.
[[nodiscard]] FilterRenderResult render_add_noise_effect(
    const FilterRenderResult &input, double amount_percent, bool gaussian,
    bool monochromatic, std::int32_t seed, const FilterProgress *progress) {
  if (input.pixels.empty()) {
    report_progress(progress, 1, 1, FilterProgressStage::AddingGrain);
    return input;
  }
  auto result = input;
  const auto range =
      std::clamp(amount_percent, kMinimumAddNoiseAmount, kMaximumAddNoiseAmount) *
      2.55;
  const auto lane_base =
      static_cast<std::uint32_t>(
          std::clamp(seed, kMinimumAddNoiseSeed, kMaximumAddNoiseSeed)) *
      16U;
  const auto unit_from_hash = [](std::uint32_t hash) {
    return static_cast<double>(hash) * (2.0 / 4294967295.0) - 1.0;
  };
  const auto delta_for_lane = [&](std::int32_t x, std::int32_t y,
                                  std::uint32_t lane) {
    if (!gaussian) {
      return std::lround(
          unit_from_hash(add_noise_hash(x, y, lane_base + lane * 4U)) * range);
    }
    // Sum of four uniforms: a deterministic gaussian approximation with no
    // transcendental calls (those vary across toolchains).
    double sum = 0.0;
    for (std::uint32_t sample = 1; sample <= 4U; ++sample) {
      sum += unit_from_hash(add_noise_hash(x, y, lane_base + lane * 4U + sample));
    }
    return std::lround(sum * 0.5 * range);
  };
  const auto clamp_byte = [](long value) {
    return static_cast<std::uint8_t>(std::clamp(value, 0L, 255L));
  };
  for (std::int32_t y = 0; y < result.bounds.height; ++y) {
    report_progress(progress, y, result.bounds.height,
                    FilterProgressStage::AddingGrain);
    for (std::int32_t x = 0; x < result.bounds.width; ++x) {
      auto *px = result.pixels.pixel(x, y);
      if (monochromatic) {
        const auto delta = delta_for_lane(x, y, 3U);
        for (int channel = 0; channel < 3; ++channel) {
          px[channel] = clamp_byte(static_cast<long>(px[channel]) + delta);
        }
      } else {
        for (std::uint32_t channel = 0; channel < 3U; ++channel) {
          px[channel] = clamp_byte(static_cast<long>(px[channel]) +
                                   delta_for_lane(x, y, channel));
        }
      }
    }
  }
  report_progress(progress, result.bounds.height, result.bounds.height,
                  FilterProgressStage::AddingGrain);
  return result;
}

// The destructive Pixel Mosaic math: alpha-weighted premultiplied block
// means, straight-color writes with the normalized alpha, grid anchored at
// the input's local origin. Bounds are preserved.
[[nodiscard]] FilterRenderResult render_mosaic_effect(
    const FilterRenderResult &input, std::int32_t cell_size_pixels,
    const FilterProgress *progress) {
  if (input.pixels.empty()) {
    report_progress(progress, 1, 1, FilterProgressStage::Pixelating);
    return input;
  }
  FilterRenderResult result{
      PixelBuffer(input.bounds.width, input.bounds.height,
                  PixelFormat::rgba8()),
      input.bounds};
  const auto width = input.bounds.width;
  const auto height = input.bounds.height;
  const auto cell = std::max<std::int32_t>(kMinimumMosaicCellSize,
                                           cell_size_pixels);
  for (std::int32_t block_y = 0; block_y < height; block_y += cell) {
    report_progress(progress, block_y, height,
                    FilterProgressStage::Pixelating);
    const auto block_height = std::min(cell, height - block_y);
    for (std::int32_t block_x = 0; block_x < width; block_x += cell) {
      const auto block_width = std::min(cell, width - block_x);
      double weight = 0.0;
      double alpha_sum = 0.0;
      std::array<double, 3> premultiplied{};
      for (std::int32_t y = block_y; y < block_y + block_height; ++y) {
        for (std::int32_t x = block_x; x < block_x + block_width; ++x) {
          const auto *px = input.pixels.pixel(x, y);
          const auto alpha = static_cast<double>(px[3]) / 255.0;
          weight += 1.0;
          alpha_sum += alpha;
          for (int channel = 0; channel < 3; ++channel) {
            premultiplied[static_cast<std::size_t>(channel)] +=
                static_cast<double>(px[channel]) * alpha;
          }
        }
      }
      std::array<std::uint8_t, 4> value{};
      for (int channel = 0; channel < 3; ++channel) {
        const auto straight =
            alpha_sum > 0.000001
                ? premultiplied[static_cast<std::size_t>(channel)] / alpha_sum
                : 0.0;
        value[static_cast<std::size_t>(channel)] = static_cast<std::uint8_t>(
            std::clamp(std::lround(straight), 0L, 255L));
      }
      value[3] = static_cast<std::uint8_t>(std::clamp(
          std::lround(weight > 0.0 ? alpha_sum / weight * 255.0 : 0.0), 0L,
          255L));
      for (std::int32_t y = block_y; y < block_y + block_height; ++y) {
        for (std::int32_t x = block_x; x < block_x + block_width; ++x) {
          auto *dst = result.pixels.pixel(x, y);
          dst[0] = value[0];
          dst[1] = value[1];
          dst[2] = value[2];
          dst[3] = value[3];
        }
      }
    }
  }
  report_progress(progress, height, height, FilterProgressStage::Pixelating);
  return result;
}

void validate_stack(const PixelBuffer &pixels, Rect bounds,
                    const SmartFilterStack &stack) {
  if (pixels.format() != PixelFormat::rgba8() || bounds.width < 0 ||
      bounds.height < 0 || pixels.width() != bounds.width ||
      pixels.height() != bounds.height) {
    throw std::invalid_argument(
        PATCHY_TRANSLATE_NOOP("QObject", "Smart Filters require a bounds-matched RGBA8 preview"));
  }
  if (stack.support != SmartFilterStackSupport::Supported ||
      stack.entries.empty() || stack.mask.linked) {
    throw std::invalid_argument(PATCHY_TRANSLATE_NOOP("QObject", "Unsupported Smart Filter stack"));
  }
  if (!stack.mask.pixels.empty() &&
      (stack.mask.pixels.format() != PixelFormat::gray8() ||
       stack.mask.bounds.width < 0 || stack.mask.bounds.height < 0 ||
       stack.mask.pixels.width() != stack.mask.bounds.width ||
       stack.mask.pixels.height() != stack.mask.bounds.height)) {
    throw std::invalid_argument(PATCHY_TRANSLATE_NOOP("QObject", "Unsupported Smart Filter mask"));
  }
  for (const auto &entry : stack.entries) {
    bool parameters_valid = false;
    double radius = 0.0;
    double minimum_radius = kMinimumGaussianRadius;
    double maximum_radius = kMaximumGaussianRadius;
    if (entry.kind == SmartFilterKind::GaussianBlur) {
      const auto *gaussian =
          std::get_if<GaussianBlurSmartFilter>(&entry.parameters);
      if (gaussian == nullptr) {
        throw std::invalid_argument(PATCHY_TRANSLATE_NOOP("QObject", "Unsupported Smart Filter entry"));
      }
      radius = gaussian->radius_pixels;
      parameters_valid = true;
    } else if (entry.kind == SmartFilterKind::HighPass) {
      const auto *high_pass =
          std::get_if<HighPassSmartFilter>(&entry.parameters);
      if (high_pass == nullptr) {
        throw std::invalid_argument(PATCHY_TRANSLATE_NOOP("QObject", "Unsupported Smart Filter entry"));
      }
      radius = high_pass->radius_pixels;
      parameters_valid = true;
    } else if (entry.kind == SmartFilterKind::Median) {
      const auto *median =
          std::get_if<MedianSmartFilter>(&entry.parameters);
      if (median == nullptr) {
        throw std::invalid_argument(PATCHY_TRANSLATE_NOOP("QObject", "Unsupported Smart Filter entry"));
      }
      radius = median->radius_pixels;
      minimum_radius = kMinimumMedianRadius;
      maximum_radius = kMaximumMedianRadius;
      parameters_valid = true;
    } else if (entry.kind == SmartFilterKind::DustAndScratches) {
      const auto *dust =
          std::get_if<DustAndScratchesSmartFilter>(&entry.parameters);
      parameters_valid =
          dust != nullptr &&
          dust->radius_pixels >= kMinimumDustAndScratchesRadius &&
          dust->radius_pixels <= kMaximumDustAndScratchesRadius &&
          dust->threshold >= kMinimumDustAndScratchesThreshold &&
          dust->threshold <= kMaximumDustAndScratchesThreshold;
    } else if (entry.kind == SmartFilterKind::SurfaceBlur) {
      const auto *surface =
          std::get_if<SurfaceBlurSmartFilter>(&entry.parameters);
      parameters_valid = surface != nullptr &&
                         std::isfinite(surface->radius_pixels) &&
                         surface->radius_pixels >= kMinimumSurfaceBlurRadius &&
                         surface->radius_pixels <= kMaximumSurfaceBlurRadius &&
                         surface->threshold >= kMinimumSurfaceBlurThreshold &&
                         surface->threshold <= kMaximumSurfaceBlurThreshold;
    } else if (entry.kind == SmartFilterKind::UnsharpMask) {
      const auto *unsharp =
          std::get_if<UnsharpMaskSmartFilter>(&entry.parameters);
      parameters_valid = unsharp != nullptr &&
                         std::isfinite(unsharp->amount_percent) &&
                         unsharp->amount_percent >= kMinimumUnsharpMaskAmount &&
                         unsharp->amount_percent <= kMaximumUnsharpMaskAmount &&
                         std::isfinite(unsharp->radius_pixels) &&
                         unsharp->radius_pixels >= kMinimumGaussianRadius &&
                         unsharp->radius_pixels <= kMaximumGaussianRadius &&
                         unsharp->threshold >= kMinimumUnsharpMaskThreshold &&
                         unsharp->threshold <= kMaximumUnsharpMaskThreshold;
    } else if (entry.kind == SmartFilterKind::MotionBlur) {
      const auto *motion =
          std::get_if<MotionBlurSmartFilter>(&entry.parameters);
      parameters_valid =
          motion != nullptr &&
          motion->angle_degrees >= kMinimumMotionBlurAngle &&
          motion->angle_degrees <= kMaximumMotionBlurAngle &&
          motion->distance_pixels >= kMinimumMotionBlurDistance &&
          motion->distance_pixels <= kMaximumMotionBlurDistance;
    } else if (entry.kind == SmartFilterKind::PlasticWrap) {
      const auto *plastic =
          std::get_if<PlasticWrapSmartFilter>(&entry.parameters);
      parameters_valid =
          plastic != nullptr &&
          plastic->highlight_strength >=
              kMinimumPlasticWrapHighlightStrength &&
          plastic->highlight_strength <=
              kMaximumPlasticWrapHighlightStrength &&
          plastic->detail >= kMinimumPlasticWrapDetail &&
          plastic->detail <= kMaximumPlasticWrapDetail &&
          plastic->smoothness >= kMinimumPlasticWrapSmoothness &&
          plastic->smoothness <= kMaximumPlasticWrapSmoothness;
    } else if (entry.kind == SmartFilterKind::Mosaic) {
      const auto *mosaic = std::get_if<MosaicSmartFilter>(&entry.parameters);
      parameters_valid = mosaic != nullptr &&
                         mosaic->cell_size_pixels >= kMinimumMosaicCellSize &&
                         mosaic->cell_size_pixels <= kMaximumMosaicCellSize;
    } else if (entry.kind == SmartFilterKind::Emboss) {
      const auto *emboss = std::get_if<EmbossSmartFilter>(&entry.parameters);
      parameters_valid = emboss != nullptr &&
                         emboss->angle_degrees >= kMinimumEmbossAngle &&
                         emboss->angle_degrees <= kMaximumEmbossAngle &&
                         emboss->height_pixels >= kMinimumEmbossHeight &&
                         emboss->height_pixels <= kMaximumEmbossHeight &&
                         emboss->amount_percent >= kMinimumEmbossAmount &&
                         emboss->amount_percent <= kMaximumEmbossAmount;
    } else if (entry.kind == SmartFilterKind::BoxBlur) {
      const auto *box = std::get_if<BoxBlurSmartFilter>(&entry.parameters);
      parameters_valid = box != nullptr &&
                         std::isfinite(box->radius_pixels) &&
                         box->radius_pixels >= kMinimumBoxBlurRadius &&
                         box->radius_pixels <= kMaximumBoxBlurRadius;
    } else if (entry.kind == SmartFilterKind::RadialBlur) {
      const auto *radial =
          std::get_if<RadialBlurSmartFilter>(&entry.parameters);
      parameters_valid = radial != nullptr &&
                         radial->amount >= kMinimumRadialBlurAmount &&
                         radial->amount <= kMaximumRadialBlurAmount;
    } else if (entry.kind == SmartFilterKind::AddNoise) {
      const auto *noise = std::get_if<AddNoiseSmartFilter>(&entry.parameters);
      parameters_valid = noise != nullptr &&
                         std::isfinite(noise->amount_percent) &&
                         noise->amount_percent >= kMinimumAddNoiseAmount &&
                         noise->amount_percent <= kMaximumAddNoiseAmount &&
                         noise->seed >= kMinimumAddNoiseSeed &&
                         noise->seed <= kMaximumAddNoiseSeed;
    } else {
      throw std::invalid_argument(PATCHY_TRANSLATE_NOOP("QObject", "Unsupported Smart Filter entry"));
    }
    if (!parameters_valid ||
        (entry.kind != SmartFilterKind::DustAndScratches &&
         entry.kind != SmartFilterKind::SurfaceBlur &&
         entry.kind != SmartFilterKind::UnsharpMask &&
         entry.kind != SmartFilterKind::MotionBlur &&
         entry.kind != SmartFilterKind::PlasticWrap &&
         entry.kind != SmartFilterKind::Mosaic &&
         entry.kind != SmartFilterKind::Emboss &&
         entry.kind != SmartFilterKind::BoxBlur &&
         entry.kind != SmartFilterKind::RadialBlur &&
         entry.kind != SmartFilterKind::AddNoise &&
         (!std::isfinite(radius) || radius < minimum_radius ||
          radius > maximum_radius)) ||
        !std::isfinite(entry.opacity) ||
        entry.opacity < 0.0 || entry.opacity > 1.0 ||
        !recipe_blend_mode_supported(entry.blend_mode)) {
      throw std::invalid_argument(PATCHY_TRANSLATE_NOOP("QObject", "Unsupported Smart Filter entry"));
    }
  }
}

} // namespace

FilterRenderResult render_photoshop_gaussian_blur(
    const PixelBuffer &pixels, Rect bounds, double radius_pixels,
    const FilterProgress *progress) {
  if (pixels.format() != PixelFormat::rgba8() || pixels.width() != bounds.width ||
      pixels.height() != bounds.height || !std::isfinite(radius_pixels) ||
      radius_pixels < kMinimumGaussianRadius ||
      radius_pixels > kMaximumGaussianRadius) {
    throw std::invalid_argument(PATCHY_TRANSLATE_NOOP("QObject", "Invalid Photoshop Gaussian Blur input"));
  }
  return render_gaussian(FilterRenderResult{pixels, bounds}, radius_pixels,
                         progress);
}

FilterRenderResult render_photoshop_high_pass(
    const PixelBuffer &pixels, Rect bounds, double radius_pixels,
    const FilterProgress *progress) {
  if (pixels.format() != PixelFormat::rgba8() || pixels.width() != bounds.width ||
      pixels.height() != bounds.height || !std::isfinite(radius_pixels) ||
      radius_pixels < kMinimumGaussianRadius ||
      radius_pixels > kMaximumGaussianRadius) {
    throw std::invalid_argument(PATCHY_TRANSLATE_NOOP("QObject", "Invalid Photoshop High Pass input"));
  }
  return render_high_pass(FilterRenderResult{pixels, bounds}, radius_pixels,
                          progress);
}

FilterRenderResult render_photoshop_median(
    const PixelBuffer &pixels, Rect bounds, double radius_pixels,
    const FilterProgress *progress) {
  if (pixels.format() != PixelFormat::rgba8() || pixels.width() != bounds.width ||
      pixels.height() != bounds.height || !std::isfinite(radius_pixels) ||
      radius_pixels < kMinimumMedianRadius ||
      radius_pixels > kMaximumMedianRadius) {
    throw std::invalid_argument(PATCHY_TRANSLATE_NOOP("QObject", "Invalid Photoshop Median input"));
  }
  return render_median(FilterRenderResult{pixels, bounds}, radius_pixels,
                       progress);
}

FilterRenderResult render_photoshop_dust_and_scratches(
    const PixelBuffer &pixels, Rect bounds, std::int32_t radius_pixels,
    std::int32_t threshold, const FilterProgress *progress) {
  if (pixels.format() != PixelFormat::rgba8() || pixels.width() != bounds.width ||
      pixels.height() != bounds.height ||
      radius_pixels < kMinimumDustAndScratchesRadius ||
      radius_pixels > kMaximumDustAndScratchesRadius ||
      threshold < kMinimumDustAndScratchesThreshold ||
      threshold > kMaximumDustAndScratchesThreshold) {
    throw std::invalid_argument(PATCHY_TRANSLATE_NOOP("QObject", "Invalid Photoshop Dust & Scratches input"));
  }
  return render_dust_and_scratches(FilterRenderResult{pixels, bounds},
                                   radius_pixels, threshold, progress);
}

FilterRenderResult render_photoshop_surface_blur(
    const PixelBuffer &pixels, Rect bounds, double radius_pixels,
    std::int32_t threshold, const FilterProgress *progress) {
  if (pixels.format() != PixelFormat::rgba8() || pixels.width() != bounds.width ||
      pixels.height() != bounds.height || !std::isfinite(radius_pixels) ||
      radius_pixels < kMinimumSurfaceBlurRadius ||
      radius_pixels > kMaximumSurfaceBlurRadius ||
      threshold < kMinimumSurfaceBlurThreshold ||
      threshold > kMaximumSurfaceBlurThreshold) {
    throw std::invalid_argument(PATCHY_TRANSLATE_NOOP("QObject", "Invalid Photoshop Surface Blur input"));
  }
  return render_surface_blur(FilterRenderResult{pixels, bounds}, radius_pixels,
                             threshold, progress);
}

FilterRenderResult
render_photoshop_unsharp_mask(const PixelBuffer &pixels, Rect bounds,
                              double amount_percent, double radius_pixels,
                              std::int32_t threshold,
                              const FilterProgress *progress) {
  if (pixels.format() != PixelFormat::rgba8() ||
      pixels.width() != bounds.width || pixels.height() != bounds.height ||
      !std::isfinite(amount_percent) ||
      amount_percent < kMinimumUnsharpMaskAmount ||
      amount_percent > kMaximumUnsharpMaskAmount ||
      !std::isfinite(radius_pixels) || radius_pixels < kMinimumGaussianRadius ||
      radius_pixels > kMaximumGaussianRadius ||
      threshold < kMinimumUnsharpMaskThreshold ||
      threshold > kMaximumUnsharpMaskThreshold) {
    throw std::invalid_argument(PATCHY_TRANSLATE_NOOP("QObject", "Invalid Photoshop Unsharp Mask input"));
  }
  return render_unsharp_mask(FilterRenderResult{pixels, bounds}, amount_percent,
                             radius_pixels, threshold, progress);
}

FilterRenderResult render_photoshop_motion_blur(
    const PixelBuffer &pixels, Rect bounds, std::int32_t angle_degrees,
    std::int32_t distance_pixels, const FilterProgress *progress,
    MotionBlurKernel kernel) {
  if (pixels.format() != PixelFormat::rgba8() ||
      pixels.width() != bounds.width || pixels.height() != bounds.height ||
      angle_degrees < kMinimumMotionBlurAngle ||
      angle_degrees > kMaximumMotionBlurAngle ||
      distance_pixels < kMinimumMotionBlurDistance ||
      distance_pixels > kMaximumMotionBlurDistance) {
    throw std::invalid_argument(PATCHY_TRANSLATE_NOOP("QObject", "Invalid Photoshop Motion Blur input"));
  }
  return render_motion_blur(FilterRenderResult{pixels, bounds}, angle_degrees,
                            distance_pixels, progress, kernel);
}

FilterRenderResult render_plastic_wrap(
    const PixelBuffer &pixels, Rect bounds, std::int32_t highlight_strength,
    std::int32_t detail, std::int32_t smoothness,
    const FilterProgress *progress) {
  if (pixels.format() != PixelFormat::rgba8() ||
      pixels.width() != bounds.width || pixels.height() != bounds.height ||
      highlight_strength < kMinimumPlasticWrapHighlightStrength ||
      highlight_strength > kMaximumPlasticWrapHighlightStrength ||
      detail < kMinimumPlasticWrapDetail ||
      detail > kMaximumPlasticWrapDetail ||
      smoothness < kMinimumPlasticWrapSmoothness ||
      smoothness > kMaximumPlasticWrapSmoothness) {
    throw std::invalid_argument(PATCHY_TRANSLATE_NOOP("QObject", "Invalid Plastic Wrap input"));
  }
  return render_plastic_wrap_effect(FilterRenderResult{pixels, bounds},
                                    highlight_strength, detail, smoothness,
                                    progress);
}

FilterRenderResult render_mosaic(const PixelBuffer &pixels, Rect bounds,
                                 std::int32_t cell_size_pixels,
                                 const FilterProgress *progress) {
  if (pixels.format() != PixelFormat::rgba8() ||
      pixels.width() != bounds.width || pixels.height() != bounds.height ||
      cell_size_pixels < kMinimumMosaicCellSize ||
      cell_size_pixels > kMaximumMosaicCellSize) {
    throw std::invalid_argument(PATCHY_TRANSLATE_NOOP("QObject", "Invalid Mosaic input"));
  }
  return render_mosaic_effect(FilterRenderResult{pixels, bounds},
                              cell_size_pixels, progress);
}

FilterRenderResult render_box_blur(const PixelBuffer &pixels, Rect bounds,
                                   double radius_pixels,
                                   const FilterProgress *progress) {
  if (pixels.format() != PixelFormat::rgba8() ||
      pixels.width() != bounds.width || pixels.height() != bounds.height ||
      !std::isfinite(radius_pixels) ||
      radius_pixels < kMinimumBoxBlurRadius ||
      radius_pixels > kMaximumBoxBlurRadius) {
    throw std::invalid_argument(PATCHY_TRANSLATE_NOOP("QObject", "Invalid Box Blur input"));
  }
  return render_box_blur_effect(
      FilterRenderResult{pixels, bounds},
      static_cast<std::int32_t>(std::floor(radius_pixels)), progress);
}

FilterRenderResult render_emboss(const PixelBuffer &pixels, Rect bounds,
                                 std::int32_t angle_degrees,
                                 std::int32_t height_pixels,
                                 std::int32_t amount_percent,
                                 const FilterProgress *progress) {
  if (pixels.format() != PixelFormat::rgba8() ||
      pixels.width() != bounds.width || pixels.height() != bounds.height ||
      angle_degrees < kMinimumEmbossAngle ||
      angle_degrees > kMaximumEmbossAngle ||
      height_pixels < kMinimumEmbossHeight ||
      height_pixels > kMaximumEmbossHeight ||
      amount_percent < kMinimumEmbossAmount ||
      amount_percent > kMaximumEmbossAmount) {
    throw std::invalid_argument(PATCHY_TRANSLATE_NOOP("QObject", "Invalid Emboss input"));
  }
  return render_emboss_effect(FilterRenderResult{pixels, bounds},
                              angle_degrees, height_pixels, amount_percent,
                              progress);
}

FilterRenderResult render_smart_filter_stack(const PixelBuffer &placed_pixels,
                                             Rect placed_bounds,
                                             Rect filter_canvas_bounds,
                                             const SmartFilterStack &stack,
                                             const FilterProgress *progress) {
  validate_stack(placed_pixels, placed_bounds, stack);
  FilterRenderResult placed{placed_pixels, placed_bounds};
  if (!stack.enabled || placed_pixels.empty()) {
    report_progress(progress, 1, 1, FilterProgressStage::Filtering);
    return trim_transparent_result(std::move(placed));
  }

  const auto active_count_size = static_cast<std::size_t>(
      std::count_if(stack.entries.begin(), stack.entries.end(),
                    [](const SmartFilterEntry &entry) {
                      return entry.enabled && entry.opacity > 0.0;
                    }));
  if (active_count_size == 0U) {
    report_progress(progress, 1, 1, FilterProgressStage::Filtering);
    return trim_transparent_result(std::move(placed));
  }
  if (active_count_size >
      static_cast<std::size_t>((std::numeric_limits<int>::max() - 1) / 2)) {
    throw std::overflow_error("Too many Smart Filter entries");
  }

  const auto active_count = static_cast<int>(active_count_size);
  const auto phase_count = active_count * 2 + 1;
  int phase = 0;
  auto base = embed_in_filter_canvas(placed_pixels, placed_bounds,
                                     filter_canvas_bounds);
  auto current = placed;
  for (const auto &entry : stack.entries) {
    if (!entry.enabled || entry.opacity <= 0.0) {
      continue;
    }
    auto filter_progress = phase_progress(progress, phase++, phase_count);
    FilterRenderResult filtered;
    if (entry.kind == SmartFilterKind::GaussianBlur) {
      const auto &gaussian =
          std::get<GaussianBlurSmartFilter>(entry.parameters);
      auto gaussian_input = embed_in_filter_canvas(
          current.pixels, current.bounds, filter_canvas_bounds);
      filtered = trim_transparent_result(render_gaussian(
          gaussian_input, gaussian.radius_pixels, &filter_progress));
    } else if (entry.kind == SmartFilterKind::HighPass) {
      const auto &high_pass =
          std::get<HighPassSmartFilter>(entry.parameters);
      filtered =
          render_high_pass(current, high_pass.radius_pixels, &filter_progress);
    } else if (entry.kind == SmartFilterKind::Median) {
      const auto &median = std::get<MedianSmartFilter>(entry.parameters);
      filtered =
          render_median(current, median.radius_pixels, &filter_progress);
    } else if (entry.kind == SmartFilterKind::DustAndScratches) {
      const auto &dust =
          std::get<DustAndScratchesSmartFilter>(entry.parameters);
      filtered = render_dust_and_scratches(current, dust.radius_pixels,
                                           dust.threshold, &filter_progress);
    } else if (entry.kind == SmartFilterKind::SurfaceBlur) {
      const auto &surface = std::get<SurfaceBlurSmartFilter>(entry.parameters);
      auto surface_input = embed_in_filter_canvas(
          current.pixels, current.bounds, filter_canvas_bounds);
      filtered = trim_transparent_result(
          render_surface_blur(surface_input, surface.radius_pixels,
                              surface.threshold, &filter_progress));
    } else if (entry.kind == SmartFilterKind::UnsharpMask) {
      const auto &unsharp = std::get<UnsharpMaskSmartFilter>(entry.parameters);
      filtered = render_unsharp_mask(current, unsharp.amount_percent,
                                     unsharp.radius_pixels, unsharp.threshold,
                                     &filter_progress);
    } else if (entry.kind == SmartFilterKind::MotionBlur) {
      const auto &motion = std::get<MotionBlurSmartFilter>(entry.parameters);
      auto motion_input = embed_in_filter_canvas(current.pixels, current.bounds,
                                                 filter_canvas_bounds);
      filtered = trim_transparent_result(
          render_motion_blur(motion_input, motion.angle_degrees,
                             motion.distance_pixels, &filter_progress));
    } else if (entry.kind == SmartFilterKind::PlasticWrap) {
      const auto &plastic =
          std::get<PlasticWrapSmartFilter>(entry.parameters);
      filtered = render_plastic_wrap_effect(
          current, plastic.highlight_strength, plastic.detail,
          plastic.smoothness, &filter_progress);
    } else if (entry.kind == SmartFilterKind::Mosaic) {
      const auto &mosaic = std::get<MosaicSmartFilter>(entry.parameters);
      filtered = render_mosaic_effect(current, mosaic.cell_size_pixels,
                                      &filter_progress);
    } else if (entry.kind == SmartFilterKind::Emboss) {
      const auto &emboss = std::get<EmbossSmartFilter>(entry.parameters);
      filtered = render_emboss_effect(current, emboss.angle_degrees,
                                      emboss.height_pixels,
                                      emboss.amount_percent, &filter_progress);
    } else if (entry.kind == SmartFilterKind::RadialBlur) {
      const auto &radial = std::get<RadialBlurSmartFilter>(entry.parameters);
      const auto content_bounds = current.bounds;
      auto radial_input = embed_in_filter_canvas(
          current.pixels, current.bounds, filter_canvas_bounds);
      // The sweep pivots on the CONTENT center expressed in canvas buffer
      // coordinates (the same image-space point the destructive path's
      // padded-percent remap produces), never the canvas center.
      const auto center_x =
          static_cast<double>(content_bounds.x - radial_input.bounds.x) +
          static_cast<double>(
              std::max<std::int32_t>(0, content_bounds.width - 1)) *
              0.5;
      const auto center_y =
          static_cast<double>(content_bounds.y - radial_input.bounds.y) +
          static_cast<double>(
              std::max<std::int32_t>(0, content_bounds.height - 1)) *
              0.5;
      const auto samples = radial.quality == RadialBlurQuality::Draft
                               ? 8
                               : (radial.quality == RadialBlurQuality::Good
                                      ? 16
                                      : 32);
      filtered = trim_transparent_result(
          render_radial_blur_effect(radial_input, radial.amount, samples,
                                    center_x, center_y, &filter_progress));
    } else if (entry.kind == SmartFilterKind::AddNoise) {
      const auto &noise = std::get<AddNoiseSmartFilter>(entry.parameters);
      filtered = render_add_noise_effect(current, noise.amount_percent,
                                         noise.gaussian, noise.monochromatic,
                                         noise.seed, &filter_progress);
    } else {
      const auto &box = std::get<BoxBlurSmartFilter>(entry.parameters);
      auto box_input = embed_in_filter_canvas(current.pixels, current.bounds,
                                              filter_canvas_bounds);
      filtered = trim_transparent_result(render_box_blur_effect(
          box_input,
          static_cast<std::int32_t>(std::floor(box.radius_pixels)),
          &filter_progress));
    }
    auto blend_progress = phase_progress(progress, phase++, phase_count);
    current = blend_entry_result(current, std::move(filtered), entry.opacity,
                                 entry.blend_mode, &blend_progress);
  }

  auto mask_progress = phase_progress(progress, phase, phase_count);
  return apply_stack_mask(base, current, stack.mask, &mask_progress);
}

FilterRenderResult render_smart_filter_stack(const PixelBuffer &placed_pixels,
                                             Rect placed_bounds,
                                             const SmartFilterStack &stack,
                                             const FilterProgress *progress) {
  return render_smart_filter_stack(placed_pixels, placed_bounds, placed_bounds,
                                   stack, progress);
}

FilterRenderResult render_radial_blur(const PixelBuffer &pixels, Rect bounds,
                                      std::int32_t amount,
                                      std::int32_t samples, double center_x,
                                      double center_y,
                                      const FilterProgress *progress) {
  if (pixels.format() != PixelFormat::rgba8() ||
      pixels.width() != bounds.width || pixels.height() != bounds.height ||
      amount < kMinimumRadialBlurAmount || amount > kMaximumRadialBlurAmount ||
      samples < 4 || samples > 32 || !std::isfinite(center_x) ||
      !std::isfinite(center_y)) {
    throw std::invalid_argument(PATCHY_TRANSLATE_NOOP("QObject", "Invalid Radial Blur input"));
  }
  return render_radial_blur_effect(FilterRenderResult{pixels, bounds}, amount,
                                   samples, center_x, center_y, progress);
}

FilterRenderResult render_add_noise(const PixelBuffer &pixels, Rect bounds,
                                    double amount_percent, bool gaussian,
                                    bool monochromatic, std::int32_t seed,
                                    const FilterProgress *progress) {
  if (pixels.format() != PixelFormat::rgba8() ||
      pixels.width() != bounds.width || pixels.height() != bounds.height ||
      !std::isfinite(amount_percent) ||
      amount_percent < kMinimumAddNoiseAmount ||
      amount_percent > kMaximumAddNoiseAmount ||
      seed < kMinimumAddNoiseSeed || seed > kMaximumAddNoiseSeed) {
    throw std::invalid_argument(PATCHY_TRANSLATE_NOOP("QObject", "Invalid Add Noise input"));
  }
  return render_add_noise_effect(FilterRenderResult{pixels, bounds},
                                 amount_percent, gaussian, monochromatic, seed,
                                 progress);
}

} // namespace patchy
