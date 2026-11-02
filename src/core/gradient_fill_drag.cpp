#include "core/gradient_fill_drag.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace patchy {

namespace {

constexpr float kPi = 3.14159265358979323846F;
// The Gradient Fill dialog's Scale range (1% to 1000%); gradient_position floors it at 1% too.
constexpr float kMinScale = 0.01F;
constexpr float kMaxScale = 10.0F;

// gradient_position's CenterChord span for `angle_degrees` over `bounds`, computed with the
// renderer's own float steps so the inverse below lands on its ramp exactly.
float center_chord_span(float angle_degrees, Rect bounds) {
  const auto radians = angle_degrees * kPi / 180.0F;
  const auto abs_cos = std::abs(std::cos(radians));
  const auto abs_sin = std::abs(std::sin(radians));
  return std::max(1.0F, std::min(abs_cos > 1e-6F ? static_cast<float>(bounds.width) / abs_cos
                                                 : std::numeric_limits<float>::infinity(),
                                 abs_sin > 1e-6F ? static_cast<float>(bounds.height) / abs_sin
                                                 : std::numeric_limits<float>::infinity()));
}

}  // namespace

LayerStyleGradient gradient_fill_from_drag(const std::vector<GradientStop>& stops, GradientMethod method,
                                           bool reverse, Rect canvas, std::int32_t x0, std::int32_t y0,
                                           std::int32_t x1, std::int32_t y1) {
  LayerStyleGradient gradient;
  gradient.name = "Custom";
  gradient.form = GradientDefinitionForm::Solid;
  // draw_gradient interpolates straight RGBA linearly between stops; Classic at smoothness 0
  // with identity midpoints is that same ramp.
  gradient.smoothness = 0;
  for (const auto& stop : normalized_gradient_stops(stops)) {
    gradient.color_stops.push_back(
        GradientColorStop{stop.location, RgbColor{stop.color.r, stop.color.g, stop.color.b}, 0.5F});
    gradient.alpha_stops.push_back(
        GradientAlphaStop{stop.location, static_cast<float>(stop.color.a) / 255.0F, 0.5F});
  }
  gradient.type = method == GradientMethod::Radial ? LayerStyleGradientType::Radial : LayerStyleGradientType::Linear;
  gradient.reverse = reverse;
  gradient.dither = false;
  gradient.interpolation = GradientInterpolationMethod::Classic;
  gradient.align_with_layer = false;

  const auto dx = static_cast<double>(x1) - static_cast<double>(x0);
  const auto dy = static_cast<double>(y1) - static_cast<double>(y0);
  const auto length = std::hypot(dx, dy);
  if (length <= 0.0 || canvas.width <= 0 || canvas.height <= 0) {
    return gradient;
  }
  // gradient_position's ramp runs along (cos a, -sin a): angles grow counterclockwise on screen.
  gradient.angle_degrees = static_cast<float>(std::atan2(-dy, dx) * 180.0 / 3.14159265358979323846);
  const auto span = static_cast<double>(center_chord_span(gradient.angle_degrees, canvas));
  // Both ramps sample each pixel at its top-left corner, so the drag points map straight on.
  // Linear: position = 0.5 + along / (span * scale) about the center, and the tool's
  // (p - from) . d / |d|^2 is 0.5 + along / |d| about the drag's midpoint. Radial: distance /
  // (span * scale / 2) about the center against distance / |d| about the drag start.
  double center_x = 0.0;
  double center_y = 0.0;
  double scale = 1.0;
  if (gradient.type == LayerStyleGradientType::Radial) {
    center_x = static_cast<double>(x0);
    center_y = static_cast<double>(y0);
    scale = 2.0 * length / span;
  } else {
    center_x = (static_cast<double>(x0) + static_cast<double>(x1)) * 0.5;
    center_y = (static_cast<double>(y0) + static_cast<double>(y1)) * 0.5;
    scale = length / span;
  }
  gradient.scale = static_cast<float>(std::clamp(scale, static_cast<double>(kMinScale), static_cast<double>(kMaxScale)));
  gradient.offset_x_percent = static_cast<float>(
      ((center_x - static_cast<double>(canvas.x)) / static_cast<double>(canvas.width) - 0.5) * 100.0);
  gradient.offset_y_percent = static_cast<float>(
      ((center_y - static_cast<double>(canvas.y)) / static_cast<double>(canvas.height) - 0.5) * 100.0);
  return gradient;
}

}  // namespace patchy
