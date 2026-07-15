#include "core/liquify.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace patchy {

namespace {

constexpr std::int64_t kFieldScale = 256;
constexpr std::int64_t kGridScale = 65536;
constexpr double kPi = 3.14159265358979323846;

std::int32_t clamp_field_value(std::int64_t value) {
  constexpr auto kLimit = static_cast<std::int64_t>(
      std::numeric_limits<std::int32_t>::max() / 2);
  return static_cast<std::int32_t>(std::clamp(value, -kLimit, kLimit));
}

std::int64_t divide_rounded(std::int64_t numerator,
                            std::int64_t denominator) {
  if (denominator <= 0) {
    return 0;
  }
  if (numerator >= 0) {
    return (numerator + denominator / 2) / denominator;
  }
  return -((-numerator + denominator / 2) / denominator);
}

std::int32_t bilinear_field(const std::vector<std::int32_t>& values,
                            int columns, int rows, int output_x,
                            int output_y, int output_width,
                            int output_height) {
  if (values.empty() || columns < 2 || rows < 2) {
    return 0;
  }
  const auto gx = output_width <= 1
                      ? std::int64_t{0}
                      : static_cast<std::int64_t>(output_x) * (columns - 1) *
                            kGridScale / (output_width - 1);
  const auto gy = output_height <= 1
                      ? std::int64_t{0}
                      : static_cast<std::int64_t>(output_y) * (rows - 1) *
                            kGridScale / (output_height - 1);
  const int x0 = std::min(static_cast<int>(gx / kGridScale), columns - 2);
  const int y0 = std::min(static_cast<int>(gy / kGridScale), rows - 2);
  const auto fx = gx - static_cast<std::int64_t>(x0) * kGridScale;
  const auto fy = gy - static_cast<std::int64_t>(y0) * kGridScale;
  const auto index = [columns](int x, int y) {
    return static_cast<std::size_t>(y * columns + x);
  };
  const auto top = static_cast<std::int64_t>(values[index(x0, y0)]) *
                       (kGridScale - fx) +
                   static_cast<std::int64_t>(values[index(x0 + 1, y0)]) * fx;
  const auto bottom =
      static_cast<std::int64_t>(values[index(x0, y0 + 1)]) *
          (kGridScale - fx) +
      static_cast<std::int64_t>(values[index(x0 + 1, y0 + 1)]) * fx;
  const auto blended = divide_rounded(
      divide_rounded(top, kGridScale) * (kGridScale - fy) +
          divide_rounded(bottom, kGridScale) * fy,
      kGridScale);
  return clamp_field_value(blended);
}

std::uint8_t bilinear_mask(const std::vector<std::uint8_t>& values,
                           int columns, int rows, double x, double y,
                           int width, int height) {
  if (values.empty() || columns < 2 || rows < 2 || width <= 0 || height <= 0) {
    return 0;
  }
  const double gx = width <= 1 ? 0.0 :
      std::clamp(x, 0.0, static_cast<double>(width - 1)) * (columns - 1) /
          (width - 1);
  const double gy = height <= 1 ? 0.0 :
      std::clamp(y, 0.0, static_cast<double>(height - 1)) * (rows - 1) /
          (height - 1);
  const int x0 = std::min(static_cast<int>(gx), columns - 2);
  const int y0 = std::min(static_cast<int>(gy), rows - 2);
  const double fx = gx - x0;
  const double fy = gy - y0;
  const auto index = [columns](int xx, int yy) {
    return static_cast<std::size_t>(yy * columns + xx);
  };
  const double top = values[index(x0, y0)] * (1.0 - fx) +
                     values[index(x0 + 1, y0)] * fx;
  const double bottom = values[index(x0, y0 + 1)] * (1.0 - fx) +
                        values[index(x0 + 1, y0 + 1)] * fx;
  return static_cast<std::uint8_t>(
      std::clamp(std::lround(top * (1.0 - fy) + bottom * fy), 0L, 255L));
}

}  // namespace

LiquifyMesh::LiquifyMesh(int width, int height, int maximum_nodes_per_axis)
    : width_(width), height_(height) {
  if (width <= 0 || height <= 0) {
    throw std::invalid_argument("Liquify mesh dimensions must be positive");
  }
  const int node_limit = std::clamp(maximum_nodes_per_axis, 2, 513);
  columns_ = std::clamp((width + 7) / 8 + 1, 2, node_limit);
  rows_ = std::clamp((height + 7) / 8 + 1, 2, node_limit);
  const auto count = static_cast<std::size_t>(columns_) * rows_;
  displacement_x_256_.assign(count, 0);
  displacement_y_256_.assign(count, 0);
  freeze_mask_.assign(count, 0);
}

int LiquifyMesh::width() const noexcept { return width_; }
int LiquifyMesh::height() const noexcept { return height_; }
int LiquifyMesh::columns() const noexcept { return columns_; }
int LiquifyMesh::rows() const noexcept { return rows_; }

bool LiquifyMesh::is_identity() const noexcept {
  return std::all_of(displacement_x_256_.begin(), displacement_x_256_.end(),
                     [](std::int32_t value) { return value == 0; }) &&
         std::all_of(displacement_y_256_.begin(), displacement_y_256_.end(),
                     [](std::int32_t value) { return value == 0; });
}

std::size_t LiquifyMesh::node_index(int column, int row) const noexcept {
  return static_cast<std::size_t>(row * columns_ + column);
}

void LiquifyMesh::apply_stroke(LiquifyTool tool, double from_x, double from_y,
                               double to_x, double to_y, double size,
                               double pressure, double density) {
  if (columns_ < 2 || rows_ < 2) {
    return;
  }
  const double radius = std::max(0.5, size * 0.5);
  pressure = std::clamp(pressure, 1.0, 100.0) / 100.0;
  density = std::clamp(density, 1.0, 100.0) / 100.0;
  const double dx = to_x - from_x;
  const double dy = to_y - from_y;
  const double distance = std::hypot(dx, dy);
  const int steps = std::max(1, static_cast<int>(std::ceil(
                                    distance / std::max(1.0, radius * 0.2))));
  for (int step = 1; step <= steps; ++step) {
    const double t = static_cast<double>(step) / steps;
    const double previous_t = static_cast<double>(step - 1) / steps;
    const double center_x = from_x + dx * t;
    const double center_y = from_y + dy * t;
    const double step_dx = dx * (t - previous_t);
    const double step_dy = dy * (t - previous_t);
    apply_dab(tool, center_x, center_y, step_dx, step_dy, radius,
              pressure, density);
  }
}

void LiquifyMesh::apply_dab(LiquifyTool tool, double center_x,
                            double center_y, double delta_x, double delta_y,
                            double radius, double pressure, double density) {
  const auto old_x = displacement_x_256_;
  const auto old_y = displacement_y_256_;
  for (int row = 0; row < rows_; ++row) {
    const double node_y = rows_ <= 1
                              ? 0.0
                              : static_cast<double>(row) * (height_ - 1) /
                                    (rows_ - 1);
    for (int column = 0; column < columns_; ++column) {
      const double node_x = columns_ <= 1
                                ? 0.0
                                : static_cast<double>(column) * (width_ - 1) /
                                      (columns_ - 1);
      const double relative_x = node_x - center_x;
      const double relative_y = node_y - center_y;
      const double distance = std::hypot(relative_x, relative_y);
      if (distance >= radius) {
        continue;
      }
      const auto index = node_index(column, row);
      const double linear = 1.0 - distance / radius;
      const double soft = linear * linear;
      const double falloff = soft + (linear - soft) * density;
      const double protection =
          tool == LiquifyTool::ThawMask
              ? 1.0
              : 1.0 - static_cast<double>(freeze_mask_[index]) / 255.0;
      const double weight = falloff * pressure * protection;
      if (weight <= 0.0) {
        continue;
      }

      if (tool == LiquifyTool::FreezeMask ||
          tool == LiquifyTool::ThawMask) {
        const int change = static_cast<int>(std::lround(falloff * pressure * 255.0));
        const int current = freeze_mask_[index];
        freeze_mask_[index] = static_cast<std::uint8_t>(std::clamp(
            tool == LiquifyTool::FreezeMask ? current + change
                                             : current - change,
            0, 255));
        continue;
      }

      auto add_offset = [&](double x, double y) {
        displacement_x_256_[index] = clamp_field_value(
            static_cast<std::int64_t>(displacement_x_256_[index]) +
            std::llround(x * kFieldScale));
        displacement_y_256_[index] = clamp_field_value(
            static_cast<std::int64_t>(displacement_y_256_[index]) +
            std::llround(y * kFieldScale));
      };

      switch (tool) {
        case LiquifyTool::ForwardWarp:
          // The field is inverse: sampling opposite the pointer movement makes
          // the visible pixels follow the brush.
          add_offset(-delta_x * weight, -delta_y * weight);
          break;
        case LiquifyTool::Reconstruct:
          displacement_x_256_[index] = clamp_field_value(std::llround(
              old_x[index] * std::max(0.0, 1.0 - weight)));
          displacement_y_256_[index] = clamp_field_value(std::llround(
              old_y[index] * std::max(0.0, 1.0 - weight)));
          break;
        case LiquifyTool::Smooth: {
          std::int64_t sum_x = old_x[index];
          std::int64_t sum_y = old_y[index];
          int count = 1;
          for (const auto [offset_x, offset_y] :
               {std::array<int, 2>{-1, 0}, {1, 0}, {0, -1}, {0, 1}}) {
            const int neighbor_x = column + offset_x;
            const int neighbor_y = row + offset_y;
            if (neighbor_x >= 0 && neighbor_x < columns_ && neighbor_y >= 0 &&
                neighbor_y < rows_) {
              const auto neighbor = node_index(neighbor_x, neighbor_y);
              sum_x += old_x[neighbor];
              sum_y += old_y[neighbor];
              ++count;
            }
          }
          const double average_x = static_cast<double>(sum_x) / count;
          const double average_y = static_cast<double>(sum_y) / count;
          displacement_x_256_[index] = clamp_field_value(std::llround(
              old_x[index] + (average_x - old_x[index]) * weight));
          displacement_y_256_[index] = clamp_field_value(std::llround(
              old_y[index] + (average_y - old_y[index]) * weight));
          break;
        }
        case LiquifyTool::TwirlClockwise:
        case LiquifyTool::TwirlCounterClockwise: {
          const double direction =
              tool == LiquifyTool::TwirlClockwise ? -1.0 : 1.0;
          const double angle = direction * 12.0 * kPi / 180.0 * weight;
          const double cosine = std::cos(angle);
          const double sine = std::sin(angle);
          const double source_x = relative_x * cosine - relative_y * sine;
          const double source_y = relative_x * sine + relative_y * cosine;
          add_offset(source_x - relative_x, source_y - relative_y);
          break;
        }
        case LiquifyTool::Pucker:
          add_offset(relative_x * 0.12 * weight,
                     relative_y * 0.12 * weight);
          break;
        case LiquifyTool::Bloat:
          add_offset(-relative_x * 0.12 * weight,
                     -relative_y * 0.12 * weight);
          break;
        case LiquifyTool::FreezeMask:
        case LiquifyTool::ThawMask:
          break;
      }
    }
  }
}

void LiquifyMesh::reset() {
  std::fill(displacement_x_256_.begin(), displacement_x_256_.end(), 0);
  std::fill(displacement_y_256_.begin(), displacement_y_256_.end(), 0);
  std::fill(freeze_mask_.begin(), freeze_mask_.end(), std::uint8_t{0});
}

std::array<double, 2> LiquifyMesh::displacement_at(double x, double y) const {
  if (width_ <= 0 || height_ <= 0) {
    return {0.0, 0.0};
  }
  const int sample_x = std::clamp(static_cast<int>(std::lround(x)), 0, width_ - 1);
  const int sample_y = std::clamp(static_cast<int>(std::lround(y)), 0, height_ - 1);
  return {static_cast<double>(bilinear_field(displacement_x_256_, columns_, rows_,
                                             sample_x, sample_y, width_, height_)) /
              kFieldScale,
          static_cast<double>(bilinear_field(displacement_y_256_, columns_, rows_,
                                             sample_x, sample_y, width_, height_)) /
              kFieldScale};
}

double LiquifyMesh::freeze_strength_at(double x, double y) const {
  return bilinear_mask(freeze_mask_, columns_, rows_, x, y, width_, height_) /
         255.0;
}

std::optional<PixelBuffer> LiquifyMesh::render(
    const PixelBuffer& source, ProgressCallback progress) const {
  if (source.empty() || source.format().bit_depth != BitDepth::UInt8 ||
      (source.format().channels != 3 && source.format().channels != 4) ||
      width_ <= 0 || height_ <= 0) {
    throw std::invalid_argument("Liquify requires RGB or RGBA UInt8 pixels");
  }
  PixelBuffer output(source.width(), source.height(), source.format());
  const int channels = source.format().channels;
  const std::int64_t target_x_denominator = std::max(1, width_ - 1);
  const std::int64_t target_y_denominator = std::max(1, height_ - 1);
  const auto max_x_256 = static_cast<std::int64_t>(source.width() - 1) * kFieldScale;
  const auto max_y_256 = static_cast<std::int64_t>(source.height() - 1) * kFieldScale;

  for (int y = 0; y < source.height(); ++y) {
    if (progress && !progress(y, source.height())) {
      return std::nullopt;
    }
    for (int x = 0; x < source.width(); ++x) {
      const auto mesh_dx = bilinear_field(displacement_x_256_, columns_, rows_,
                                          x, y, source.width(), source.height());
      const auto mesh_dy = bilinear_field(displacement_y_256_, columns_, rows_,
                                          x, y, source.width(), source.height());
      const auto dx = divide_rounded(
          static_cast<std::int64_t>(mesh_dx) * (source.width() - 1),
          target_x_denominator);
      const auto dy = divide_rounded(
          static_cast<std::int64_t>(mesh_dy) * (source.height() - 1),
          target_y_denominator);
      const auto source_x_256 = std::clamp(
          static_cast<std::int64_t>(x) * kFieldScale + dx,
          std::int64_t{0}, max_x_256);
      const auto source_y_256 = std::clamp(
          static_cast<std::int64_t>(y) * kFieldScale + dy,
          std::int64_t{0}, max_y_256);
      const int x0 = static_cast<int>(source_x_256 / kFieldScale);
      const int y0 = static_cast<int>(source_y_256 / kFieldScale);
      const int x1 = std::min(x0 + 1, source.width() - 1);
      const int y1 = std::min(y0 + 1, source.height() - 1);
      const auto fx = source_x_256 - static_cast<std::int64_t>(x0) * kFieldScale;
      const auto fy = source_y_256 - static_cast<std::int64_t>(y0) * kFieldScale;
      const std::array<std::int64_t, 4> weights{
          (kFieldScale - fx) * (kFieldScale - fy),
          fx * (kFieldScale - fy),
          (kFieldScale - fx) * fy,
          fx * fy};
      const std::array<const std::uint8_t*, 4> samples{
          source.pixel(x0, y0), source.pixel(x1, y0),
          source.pixel(x0, y1), source.pixel(x1, y1)};
      auto* destination = output.pixel(x, y);
      if (channels == 4) {
        std::int64_t alpha_sum = 0;
        for (std::size_t sample = 0; sample < samples.size(); ++sample) {
          alpha_sum += static_cast<std::int64_t>(samples[sample][3]) *
                       weights[sample];
        }
        destination[3] = static_cast<std::uint8_t>(std::clamp(
            divide_rounded(alpha_sum, kGridScale), std::int64_t{0},
            std::int64_t{255}));
        for (int channel = 0; channel < 3; ++channel) {
          std::int64_t premultiplied_sum = 0;
          for (std::size_t sample = 0; sample < samples.size(); ++sample) {
            premultiplied_sum +=
                static_cast<std::int64_t>(samples[sample][channel]) *
                samples[sample][3] * weights[sample];
          }
          destination[channel] = static_cast<std::uint8_t>(
              alpha_sum == 0
                  ? 0
                  : std::clamp(divide_rounded(premultiplied_sum, alpha_sum),
                               std::int64_t{0}, std::int64_t{255}));
        }
      } else {
        for (int channel = 0; channel < 3; ++channel) {
          std::int64_t sum = 0;
          for (std::size_t sample = 0; sample < samples.size(); ++sample) {
            sum += static_cast<std::int64_t>(samples[sample][channel]) *
                   weights[sample];
          }
          destination[channel] = static_cast<std::uint8_t>(std::clamp(
              divide_rounded(sum, kGridScale), std::int64_t{0},
              std::int64_t{255}));
        }
      }
    }
  }
  if (progress && !progress(source.height(), source.height())) {
    return std::nullopt;
  }
  return output;
}

}  // namespace patchy
