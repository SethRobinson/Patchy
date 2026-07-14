#include "filters/smart_filter_renderer.hpp"

#include "core/blend_math.hpp"
#include "core/rect_utils.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
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
constexpr std::int32_t kMaximumDustAndScratchesRadius = 100;
constexpr std::int32_t kMinimumDustAndScratchesThreshold = 0;
constexpr std::int32_t kMaximumDustAndScratchesThreshold = 255;
constexpr double kMinimumSurfaceBlurRadius = 1.0;
constexpr double kMaximumSurfaceBlurRadius = 100.0;
constexpr std::int32_t kMinimumSurfaceBlurThreshold = 2;
constexpr std::int32_t kMaximumSurfaceBlurThreshold = 255;
constexpr double kGaussianMarginScale = 3.0;
constexpr double kDirectGaussianMaximumRadius = 8.0;
constexpr int kProgressScale = 1000000;
constexpr std::int32_t kSurfaceBlurDirectMaximumRadius = 8;

[[nodiscard]] bool supported_blend_mode(BlendMode mode) noexcept {
  const auto value = static_cast<int>(mode);
  return value >= static_cast<int>(BlendMode::Normal) &&
         value <= static_cast<int>(BlendMode::Divide);
}

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

[[nodiscard]] Rect checked_union_bounds(Rect left, Rect right) {
  if (left.empty()) {
    return right;
  }
  if (right.empty()) {
    return left;
  }
  const auto left_x2 = static_cast<std::int64_t>(left.x) + left.width;
  const auto left_y2 = static_cast<std::int64_t>(left.y) + left.height;
  const auto right_x2 = static_cast<std::int64_t>(right.x) + right.width;
  const auto right_y2 = static_cast<std::int64_t>(right.y) + right.height;
  const auto x1 = std::min<std::int64_t>(left.x, right.x);
  const auto y1 = std::min<std::int64_t>(left.y, right.y);
  const auto x2 = std::max(left_x2, right_x2);
  const auto y2 = std::max(left_y2, right_y2);
  if (x1 < std::numeric_limits<std::int32_t>::min() ||
      y1 < std::numeric_limits<std::int32_t>::min() ||
      x2 > std::numeric_limits<std::int32_t>::max() ||
      y2 > std::numeric_limits<std::int32_t>::max() ||
      x2 - x1 > std::numeric_limits<std::int32_t>::max() ||
      y2 - y1 > std::numeric_limits<std::int32_t>::max()) {
    throw std::overflow_error("Smart Filter result bounds overflow");
  }
  return Rect{static_cast<std::int32_t>(x1), static_cast<std::int32_t>(y1),
              static_cast<std::int32_t>(x2 - x1),
              static_cast<std::int32_t>(y2 - y1)};
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
    throw std::invalid_argument("Smart Filter canvas bounds are empty");
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
      throw std::invalid_argument("Invalid Gaussian Smart Filter radius");
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
    throw std::invalid_argument("Invalid Gaussian Smart Filter radius");
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
                         const FilterProgress *progress) {
  const auto margin =
      static_cast<int>(std::ceil(kGaussianMarginScale * radius));
  const auto plan = make_high_pass_line_plan(radius, margin);
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
      throw std::runtime_error("Filter color extension has no visible source");
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

// Patent design constraint, do not regress (details in docs/smart-objects.md
// "Patents and trademarks" and AGENTS.md "Legal constraints"): these window
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
    throw std::runtime_error("Surface Blur produced an empty range kernel");
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
  const auto bounds = checked_union_bounds(before.bounds, filtered.bounds);
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
  const auto bounds = checked_union_bounds(base.bounds, filtered.bounds);
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

void validate_stack(const PixelBuffer &pixels, Rect bounds,
                    const SmartFilterStack &stack) {
  if (pixels.format() != PixelFormat::rgba8() || bounds.width < 0 ||
      bounds.height < 0 || pixels.width() != bounds.width ||
      pixels.height() != bounds.height) {
    throw std::invalid_argument(
        "Smart Filters require a bounds-matched RGBA8 preview");
  }
  if (stack.support != SmartFilterStackSupport::Supported ||
      stack.entries.empty() || stack.mask.linked) {
    throw std::invalid_argument("Unsupported Smart Filter stack");
  }
  if (!stack.mask.pixels.empty() &&
      (stack.mask.pixels.format() != PixelFormat::gray8() ||
       stack.mask.bounds.width < 0 || stack.mask.bounds.height < 0 ||
       stack.mask.pixels.width() != stack.mask.bounds.width ||
       stack.mask.pixels.height() != stack.mask.bounds.height)) {
    throw std::invalid_argument("Unsupported Smart Filter mask");
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
        throw std::invalid_argument("Unsupported Smart Filter entry");
      }
      radius = gaussian->radius_pixels;
      parameters_valid = true;
    } else if (entry.kind == SmartFilterKind::HighPass) {
      const auto *high_pass =
          std::get_if<HighPassSmartFilter>(&entry.parameters);
      if (high_pass == nullptr) {
        throw std::invalid_argument("Unsupported Smart Filter entry");
      }
      radius = high_pass->radius_pixels;
      parameters_valid = true;
    } else if (entry.kind == SmartFilterKind::Median) {
      const auto *median =
          std::get_if<MedianSmartFilter>(&entry.parameters);
      if (median == nullptr) {
        throw std::invalid_argument("Unsupported Smart Filter entry");
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
      parameters_valid =
          surface != nullptr && std::isfinite(surface->radius_pixels) &&
          surface->radius_pixels >= kMinimumSurfaceBlurRadius &&
          surface->radius_pixels <= kMaximumSurfaceBlurRadius &&
          surface->threshold >= kMinimumSurfaceBlurThreshold &&
          surface->threshold <= kMaximumSurfaceBlurThreshold;
    } else {
      throw std::invalid_argument("Unsupported Smart Filter entry");
    }
    if (!parameters_valid ||
        (entry.kind != SmartFilterKind::DustAndScratches &&
         entry.kind != SmartFilterKind::SurfaceBlur &&
         (!std::isfinite(radius) || radius < minimum_radius ||
          radius > maximum_radius)) ||
        !std::isfinite(entry.opacity) ||
        entry.opacity < 0.0 || entry.opacity > 1.0 ||
        !supported_blend_mode(entry.blend_mode)) {
      throw std::invalid_argument("Unsupported Smart Filter entry");
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
    throw std::invalid_argument("Invalid Photoshop Gaussian Blur input");
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
    throw std::invalid_argument("Invalid Photoshop High Pass input");
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
    throw std::invalid_argument("Invalid Photoshop Median input");
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
    throw std::invalid_argument("Invalid Photoshop Dust & Scratches input");
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
    throw std::invalid_argument("Invalid Photoshop Surface Blur input");
  }
  return render_surface_blur(FilterRenderResult{pixels, bounds}, radius_pixels,
                             threshold, progress);
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
      filtered = render_dust_and_scratches(
          current, dust.radius_pixels, dust.threshold, &filter_progress);
    } else {
      const auto &surface =
          std::get<SurfaceBlurSmartFilter>(entry.parameters);
      auto surface_input = embed_in_filter_canvas(
          current.pixels, current.bounds, filter_canvas_bounds);
      filtered = trim_transparent_result(render_surface_blur(
          surface_input, surface.radius_pixels, surface.threshold,
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

} // namespace patchy
