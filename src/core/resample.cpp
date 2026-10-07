#include "core/resample.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <deque>
#include <utility>
#include <vector>

namespace patchy {

namespace {

constexpr std::array<ResampleMethod, 6> kAllResampleMethods{
    ResampleMethod::Automatic,       ResampleMethod::NearestNeighbor, ResampleMethod::Bilinear,
    ResampleMethod::Bicubic,         ResampleMethod::BicubicSmoother, ResampleMethod::BicubicSharper,
};

// One axis of the separable filter: for output index i, source taps
// [first[i], first[i] + count[i]) with weights at weights[i * max_taps ...].
struct AxisWeights {
  std::vector<std::int32_t> first;
  std::vector<std::int32_t> count;
  std::vector<double> weights;
  std::int32_t max_taps{0};
};

AxisWeights build_axis_weights(std::int32_t source_length, std::int32_t target_length, const ResampleKernel& kernel) {
  AxisWeights axis;
  axis.first.resize(static_cast<std::size_t>(target_length));
  axis.count.resize(static_cast<std::size_t>(target_length));
  const double scale = static_cast<double>(source_length) / static_cast<double>(target_length);

  if (kernel.kind == ResampleKernel::Kind::Nearest) {
    axis.max_taps = 1;
    axis.weights.assign(static_cast<std::size_t>(target_length), 1.0);
    for (std::int32_t i = 0; i < target_length; ++i) {
      const auto center = (static_cast<double>(i) + 0.5) * scale;
      axis.first[static_cast<std::size_t>(i)] =
          std::clamp(static_cast<std::int32_t>(std::floor(center)), 0, source_length - 1);
      axis.count[static_cast<std::size_t>(i)] = 1;
    }
    return axis;
  }

  // A reduction widens the kernel by the scale so the output averages its footprint.
  const double filter_scale = std::max(1.0, scale);
  const double support = kernel.support * filter_scale;
  axis.max_taps = static_cast<std::int32_t>(std::ceil(support * 2.0)) + 2;
  axis.weights.assign(static_cast<std::size_t>(target_length) * static_cast<std::size_t>(axis.max_taps), 0.0);

  for (std::int32_t i = 0; i < target_length; ++i) {
    const auto center = (static_cast<double>(i) + 0.5) * scale;
    auto begin = static_cast<std::int32_t>(std::floor(center - support));
    auto end = static_cast<std::int32_t>(std::ceil(center + support));  // exclusive
    begin = std::max(begin, 0);
    end = std::min(end, source_length);
    if (end <= begin) {
      begin = std::clamp(static_cast<std::int32_t>(std::floor(center)), 0, source_length - 1);
      end = begin + 1;
    }
    end = std::min(end, begin + axis.max_taps);

    auto* row = axis.weights.data() + static_cast<std::size_t>(i) * static_cast<std::size_t>(axis.max_taps);
    double total = 0.0;
    for (std::int32_t j = begin; j < end; ++j) {
      // Distance between the source pixel's center and the sample point, in kernel units.
      const auto distance = (static_cast<double>(j) + 0.5 - center) / filter_scale;
      const auto weight = kernel.weight(distance);
      row[j - begin] = weight;
      total += weight;
    }
    if (total <= 0.0) {
      // Degenerate window (cannot happen with the kernels above, kept for safety): take the
      // nearest source pixel.
      std::fill(row, row + (end - begin), 0.0);
      const auto nearest = std::clamp(static_cast<std::int32_t>(std::floor(center)), begin, end - 1);
      row[nearest - begin] = 1.0;
      total = 1.0;
    }
    for (std::int32_t j = begin; j < end; ++j) {
      row[j - begin] /= total;
    }
    axis.first[static_cast<std::size_t>(i)] = begin;
    axis.count[static_cast<std::size_t>(i)] = end - begin;
  }
  return axis;
}

std::uint16_t color_channel_count(ColorMode mode) noexcept {
  switch (mode) {
    case ColorMode::Grayscale:
      return 1;
    case ColorMode::RGB:
      return 3;
    case ColorMode::CMYK:
      return 4;
    case ColorMode::Lab:
      return 3;
  }
  return 3;
}

// Channel access for every bit depth. Values are read as stored (0..255, 0..65535, or
// the raw float) and written back rounded and clamped for the integer depths.
double read_channel(const std::uint8_t* pixel, std::uint16_t channel, BitDepth depth) noexcept {
  switch (depth) {
    case BitDepth::UInt8:
      return static_cast<double>(pixel[channel]);
    case BitDepth::UInt16: {
      std::uint16_t value = 0;
      std::memcpy(&value, pixel + static_cast<std::size_t>(channel) * 2U, sizeof(value));
      return static_cast<double>(value);
    }
    case BitDepth::Float32: {
      float value = 0.0F;
      std::memcpy(&value, pixel + static_cast<std::size_t>(channel) * 4U, sizeof(value));
      return static_cast<double>(value);
    }
  }
  return 0.0;
}

void write_channel(std::uint8_t* pixel, std::uint16_t channel, BitDepth depth, double value) noexcept {
  switch (depth) {
    case BitDepth::UInt8:
      pixel[channel] = static_cast<std::uint8_t>(std::clamp<long long>(std::llround(value), 0LL, 255LL));
      return;
    case BitDepth::UInt16: {
      const auto stored = static_cast<std::uint16_t>(std::clamp<long long>(std::llround(value), 0LL, 65535LL));
      std::memcpy(pixel + static_cast<std::size_t>(channel) * 2U, &stored, sizeof(stored));
      return;
    }
    case BitDepth::Float32: {
      const auto stored = static_cast<float>(value);
      std::memcpy(pixel + static_cast<std::size_t>(channel) * 4U, &stored, sizeof(stored));
      return;
    }
  }
}

double alpha_scale(BitDepth depth) noexcept {
  switch (depth) {
    case BitDepth::UInt8:
      return 255.0;
    case BitDepth::UInt16:
      return 65535.0;
    case BitDepth::Float32:
      return 1.0;
  }
  return 1.0;
}

PixelBuffer copy_nearest(const PixelBuffer& source, std::int32_t width, std::int32_t height) {
  PixelBuffer scaled(width, height, source.format());
  const auto pixel_bytes = bytes_per_pixel(source.format());
  const auto nearest = ResampleKernel{ResampleKernel::Kind::Nearest, 0.0, 0.0, 0.0};
  const auto columns = build_axis_weights(source.width(), width, nearest);
  const auto rows = build_axis_weights(source.height(), height, nearest);
  for (std::int32_t y = 0; y < height; ++y) {
    const auto source_row = source.row(rows.first[static_cast<std::size_t>(y)]);
    auto target_row = scaled.row(y);
    for (std::int32_t x = 0; x < width; ++x) {
      const auto* src = source_row.data() + static_cast<std::size_t>(columns.first[static_cast<std::size_t>(x)]) * pixel_bytes;
      std::copy(src, src + pixel_bytes, target_row.data() + static_cast<std::size_t>(x) * pixel_bytes);
    }
  }
  return scaled;
}

}  // namespace

std::string_view resample_method_id(ResampleMethod method) noexcept {
  switch (method) {
    case ResampleMethod::Automatic:
      return "automatic";
    case ResampleMethod::NearestNeighbor:
      return "nearest";
    case ResampleMethod::Bilinear:
      return "bilinear";
    case ResampleMethod::Bicubic:
      return "bicubic";
    case ResampleMethod::BicubicSmoother:
      return "bicubicSmoother";
    case ResampleMethod::BicubicSharper:
      return "bicubicSharper";
  }
  return "automatic";
}

std::optional<ResampleMethod> parse_resample_method(std::string_view id) noexcept {
  for (const auto method : kAllResampleMethods) {
    if (resample_method_id(method) == id) {
      return method;
    }
  }
  return std::nullopt;
}

std::span<const ResampleMethod> all_resample_methods() noexcept {
  return kAllResampleMethods;
}

ResampleMethod resolve_automatic_resample_method(ResampleMethod method, std::int32_t source_width,
                                                 std::int32_t source_height, std::int32_t target_width,
                                                 std::int32_t target_height) noexcept {
  if (method != ResampleMethod::Automatic) {
    return method;
  }
  const auto source_area = static_cast<std::int64_t>(std::max(0, source_width)) * std::max(0, source_height);
  const auto target_area = static_cast<std::int64_t>(std::max(0, target_width)) * std::max(0, target_height);
  if (target_area < source_area) {
    return ResampleMethod::BicubicSharper;
  }
  if (target_area > source_area) {
    return ResampleMethod::BicubicSmoother;
  }
  return ResampleMethod::Bicubic;
}

double cubic_bc_weight(double distance, double b, double c) noexcept {
  const auto x = std::abs(distance);
  if (x < 1.0) {
    return ((12.0 - 9.0 * b - 6.0 * c) * x * x * x + (-18.0 + 12.0 * b + 6.0 * c) * x * x + (6.0 - 2.0 * b)) / 6.0;
  }
  if (x < 2.0) {
    return ((-b - 6.0 * c) * x * x * x + (6.0 * b + 30.0 * c) * x * x + (-12.0 * b - 48.0 * c) * x +
            (8.0 * b + 24.0 * c)) /
           6.0;
  }
  return 0.0;
}

double cubic_weight(double distance) noexcept {
  const auto x = std::abs(distance);
  if (x < 1.0) {
    return (1.5 * x * x * x) - (2.5 * x * x) + 1.0;
  }
  if (x < 2.0) {
    return (-0.5 * x * x * x) + (2.5 * x * x) - (4.0 * x) + 2.0;
  }
  return 0.0;
}

double triangle_weight(double distance) noexcept {
  const auto x = std::abs(distance);
  return x < 1.0 ? 1.0 - x : 0.0;
}

double ResampleKernel::weight(double distance) const noexcept {
  switch (kind) {
    case Kind::Nearest:
      return std::abs(distance) <= 0.5 ? 1.0 : 0.0;
    case Kind::Triangle:
      return triangle_weight(distance);
    case Kind::Cubic:
      return cubic_bc_weight(distance, b, c);
  }
  return 0.0;
}

ResampleKernel resample_kernel(ResampleMethod method) noexcept {
  switch (method) {
    case ResampleMethod::NearestNeighbor:
      return ResampleKernel{ResampleKernel::Kind::Nearest, 0.0, 0.0, 0.0};
    case ResampleMethod::Bilinear:
      return ResampleKernel{ResampleKernel::Kind::Triangle, 1.0, 0.0, 0.0};
    case ResampleMethod::BicubicSmoother:
      return ResampleKernel{ResampleKernel::Kind::Cubic, 2.0, 1.0 / 3.0, 1.0 / 3.0};
    case ResampleMethod::BicubicSharper:
      return ResampleKernel{ResampleKernel::Kind::Cubic, 2.0, 0.0, 0.75};
    case ResampleMethod::Automatic:
    case ResampleMethod::Bicubic:
      break;
  }
  return ResampleKernel{ResampleKernel::Kind::Cubic, 2.0, 0.0, 0.5};
}

PixelBuffer resample_pixels(const PixelBuffer& source, std::int32_t width, std::int32_t height,
                            ResampleMethod method) {
  if (source.empty() || width <= 0 || height <= 0) {
    return PixelBuffer(std::max(0, width), std::max(0, height), source.format());
  }
  method = resolve_automatic_resample_method(method, source.width(), source.height(), width, height);
  if (method == ResampleMethod::NearestNeighbor) {
    return copy_nearest(source, width, height);
  }

  const auto format = source.format();
  const auto depth = format.bit_depth;
  const auto channels = format.channels;
  const auto pixel_bytes = bytes_per_pixel(format);
  const auto color_channels = color_channel_count(format.color_mode);
  const bool has_alpha = channels == color_channels + 1;
  const auto alpha_index = has_alpha ? color_channels : channels;  // == channels: none
  const double alpha_max = alpha_scale(depth);

  const auto kernel = resample_kernel(method);
  const auto columns = build_axis_weights(source.width(), width, kernel);
  const auto rows = build_axis_weights(source.height(), height, kernel);
  const auto row_values = static_cast<std::size_t>(width) * channels;

  // Horizontal pass for one source row into premultiplied doubles.
  const auto resample_source_row = [&](std::int32_t source_y, std::vector<double>& out) {
    out.assign(row_values, 0.0);
    const auto source_row = source.row(source_y);
    for (std::int32_t x = 0; x < width; ++x) {
      const auto first = columns.first[static_cast<std::size_t>(x)];
      const auto count = columns.count[static_cast<std::size_t>(x)];
      const auto* weights =
          columns.weights.data() + static_cast<std::size_t>(x) * static_cast<std::size_t>(columns.max_taps);
      auto* dst = out.data() + static_cast<std::size_t>(x) * channels;
      for (std::int32_t tap = 0; tap < count; ++tap) {
        const auto* src = source_row.data() + static_cast<std::size_t>(first + tap) * pixel_bytes;
        const auto weight = weights[tap];
        double alpha_factor = 1.0;
        if (has_alpha) {
          const auto alpha = read_channel(src, alpha_index, depth);
          alpha_factor = alpha / alpha_max;
          dst[alpha_index] += alpha * weight;
        }
        for (std::uint16_t channel = 0; channel < channels; ++channel) {
          if (channel == alpha_index) {
            continue;
          }
          dst[channel] += read_channel(src, channel, depth) * alpha_factor * weight;
        }
      }
    }
  };

  // Vertical pass, streaming: source rows are resampled horizontally on demand and kept
  // in a window that advances with the output row, so memory is max_taps rows, never
  // the whole image.
  PixelBuffer scaled(width, height, format);
  std::deque<std::pair<std::int32_t, std::vector<double>>> window;
  std::vector<double> accumulated(row_values, 0.0);
  for (std::int32_t y = 0; y < height; ++y) {
    const auto first = rows.first[static_cast<std::size_t>(y)];
    const auto count = rows.count[static_cast<std::size_t>(y)];
    const auto* weights = rows.weights.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(rows.max_taps);
    while (!window.empty() && window.front().first < first) {
      window.pop_front();
    }
    const auto next_needed = window.empty() ? first : window.back().first + 1;
    for (std::int32_t source_y = next_needed; source_y < first + count; ++source_y) {
      window.emplace_back(source_y, std::vector<double>());
      resample_source_row(source_y, window.back().second);
    }

    std::fill(accumulated.begin(), accumulated.end(), 0.0);
    for (std::int32_t tap = 0; tap < count; ++tap) {
      const auto& source_values = window[static_cast<std::size_t>(first + tap - window.front().first)].second;
      const auto weight = weights[tap];
      for (std::size_t i = 0; i < row_values; ++i) {
        accumulated[i] += source_values[i] * weight;
      }
    }

    auto target_row = scaled.row(y);
    for (std::int32_t x = 0; x < width; ++x) {
      auto* dst = target_row.data() + static_cast<std::size_t>(x) * pixel_bytes;
      const auto* values = accumulated.data() + static_cast<std::size_t>(x) * channels;
      if (has_alpha) {
        const auto alpha = values[alpha_index];
        write_channel(dst, alpha_index, depth, alpha);
        // Un-premultiply against the stored (rounded) alpha so color and coverage agree;
        // a fully transparent result is black, like Free Transform's commit.
        const auto stored_alpha = read_channel(dst, alpha_index, depth);
        for (std::uint16_t channel = 0; channel < channels; ++channel) {
          if (channel == alpha_index) {
            continue;
          }
          const auto color = stored_alpha > 0.0 ? values[channel] * alpha_max / stored_alpha : 0.0;
          write_channel(dst, channel, depth, color);
        }
      } else {
        for (std::uint16_t channel = 0; channel < channels; ++channel) {
          write_channel(dst, channel, depth, values[channel]);
        }
      }
    }
  }
  return scaled;
}

}  // namespace patchy
