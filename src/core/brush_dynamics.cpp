#include "core/brush_dynamics.hpp"

#include <algorithm>
#include <cmath>

namespace patchy {

namespace {

constexpr double kPi = 3.14159265358979323846;

// Blend factor toward each new segment's direction. The canvas stroke smoother emits short
// segments (fractions of the brush size), so this settles within about half a brush width
// without letting single noisy segments whip the tip around.
constexpr double kDirectionSmoothing = 0.35;

// Fade control value for the current spacing step: 1 at the stroke start, 0 after fade_steps.
[[nodiscard]] double fade_value(int fade_steps, std::uint32_t step_index) noexcept {
  const auto steps = std::max(1, fade_steps);
  return std::max(0.0, 1.0 - static_cast<double>(step_index) / static_cast<double>(steps));
}

[[nodiscard]] double degrees_from_vector(double x, double y) noexcept {
  return std::atan2(y, x) * 180.0 / kPi;
}

}  // namespace

bool BrushDynamics::active() const noexcept {
  return size_jitter > 0.0 || angle_jitter > 0.0 || angle_control != BrushDynamicControl::Off ||
         roundness_jitter > 0.0 || flip_x_jitter || flip_y_jitter || scatter > 0.0 || count > 1 ||
         opacity_jitter > 0.0;
}

void BrushDynamicsRng::seed(std::uint32_t value) noexcept {
  state = 0x9E3779B97F4A7C15ULL ^ (static_cast<std::uint64_t>(value) * 0xBF58476D1CE4E5B9ULL);
}

std::uint64_t BrushDynamicsRng::next_u64() noexcept {
  state += 0x9E3779B97F4A7C15ULL;
  auto z = state;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}

double BrushDynamicsRng::next_unit() noexcept {
  return static_cast<double>(next_u64() >> 11) * 0x1.0p-53;
}

double BrushDynamicsRng::next_signed_unit() noexcept { return next_unit() * 2.0 - 1.0; }

bool BrushDynamicsRng::next_bool() noexcept { return (next_u64() >> 63) != 0; }

void advance_stroke_direction(BrushDynamicsStrokeContext& context, double dx, double dy) {
  const auto length = std::sqrt(dx * dx + dy * dy);
  if (length <= 0.0) {
    return;
  }
  const auto nx = dx / length;
  const auto ny = dy / length;
  if (!context.direction_valid) {
    context.direction_valid = true;
    context.direction_x = nx;
    context.direction_y = ny;
    context.initial_direction_valid = true;
    context.initial_direction_x = nx;
    context.initial_direction_y = ny;
    return;
  }
  auto blended_x = context.direction_x + (nx - context.direction_x) * kDirectionSmoothing;
  auto blended_y = context.direction_y + (ny - context.direction_y) * kDirectionSmoothing;
  const auto blended_length = std::sqrt(blended_x * blended_x + blended_y * blended_y);
  if (blended_length <= 1e-9) {
    // A hard reversal cancels the blend; snap to the new direction.
    blended_x = nx;
    blended_y = ny;
  } else {
    blended_x /= blended_length;
    blended_y /= blended_length;
  }
  context.direction_x = blended_x;
  context.direction_y = blended_y;
}

int sample_dab_count(const BrushDynamics& dynamics, BrushDynamicsRng& rng) noexcept {
  const auto count = std::clamp(dynamics.count, 1, 16);
  if (count <= 1 || dynamics.count_jitter <= 0.0) {
    return count;
  }
  const auto jitter = std::clamp(dynamics.count_jitter, 0.0, 1.0);
  const auto sampled = std::lround(static_cast<double>(count) * (1.0 - jitter * rng.next_unit()));
  return static_cast<int>(std::clamp<long>(sampled, 1, count));
}

BrushDabVariation sample_dab_variation(const BrushDynamics& dynamics, BrushDynamicsRng& rng,
                                       const BrushDynamicsStrokeContext& context,
                                       int brush_size) noexcept {
  BrushDabVariation variation;

  if (dynamics.scatter > 0.0) {
    const auto range =
        std::clamp(dynamics.scatter, 0.0, 10.0) * static_cast<double>(std::max(1, brush_size));
    const auto perpendicular = rng.next_signed_unit() * range;
    variation.offset_x = -context.direction_y * perpendicular;
    variation.offset_y = context.direction_x * perpendicular;
    if (dynamics.scatter_both_axes) {
      const auto parallel = rng.next_signed_unit() * range;
      variation.offset_x += context.direction_x * parallel;
      variation.offset_y += context.direction_y * parallel;
    }
  }

  if (dynamics.size_jitter > 0.0) {
    const auto floor_scale = std::clamp(dynamics.minimum_diameter, 0.01, 1.0);
    const auto jitter = std::clamp(dynamics.size_jitter, 0.0, 1.0);
    variation.scale = std::clamp(1.0 - jitter * rng.next_unit(), floor_scale, 1.0);
  }

  auto angle = 0.0;
  switch (dynamics.angle_control) {
    case BrushDynamicControl::Off:
      break;
    case BrushDynamicControl::Fade:
      angle = fade_value(dynamics.angle_fade_steps, context.step_index) * 360.0;
      break;
    case BrushDynamicControl::PenPressure:
      angle = std::clamp(dynamics.pen_control_value, 0.0, 1.0) * 360.0;
      break;
    case BrushDynamicControl::PenTilt:
    case BrushDynamicControl::PenRotation:
      angle = dynamics.pen_angle_valid ? dynamics.pen_angle_degrees : 0.0;
      break;
    case BrushDynamicControl::InitialDirection:
      angle = context.initial_direction_valid
                  ? degrees_from_vector(context.initial_direction_x, context.initial_direction_y)
                  : 0.0;
      break;
    case BrushDynamicControl::Direction:
      angle = context.direction_valid
                  ? degrees_from_vector(context.direction_x, context.direction_y)
                  : 0.0;
      break;
  }
  if (dynamics.angle_jitter > 0.0) {
    angle += rng.next_signed_unit() * std::clamp(dynamics.angle_jitter, 0.0, 1.0) * 180.0;
  }
  variation.angle_offset_degrees = angle;

  if (dynamics.roundness_jitter > 0.0) {
    const auto floor_roundness = std::clamp(dynamics.minimum_roundness, 0.01, 1.0);
    const auto jitter = std::clamp(dynamics.roundness_jitter, 0.0, 1.0);
    variation.roundness_multiplier = std::clamp(1.0 - jitter * rng.next_unit(), floor_roundness, 1.0);
  }
  if (dynamics.flip_x_jitter) {
    variation.flip_x = rng.next_bool();
  }
  if (dynamics.flip_y_jitter) {
    variation.flip_y = rng.next_bool();
  }
  if (dynamics.opacity_jitter > 0.0) {
    const auto jitter = std::clamp(dynamics.opacity_jitter, 0.0, 1.0);
    variation.opacity_multiplier = std::clamp(1.0 - jitter * rng.next_unit(), 0.03, 1.0);
  }
  return variation;
}

}  // namespace patchy
