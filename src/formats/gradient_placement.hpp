#pragma once

#include "core/layer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

// Maps a gradient's document-space geometry onto Patchy's calibrated GdFl model
// (angle + scale + center offsets against a reference box; span = the center chord
// of the aligned bounds, docs/vector-tools.md "GdFl gradient fill geometry").
// Shared by the SVG and PDF importers so both formats place gradients identically.

namespace patchy::formats {

struct GradientReferenceBox {
  double x{0.0};
  double y{0.0};
  double width{1.0};
  double height{1.0};
};

// A linear ramp running from (x1, y1) to (x2, y2), both in document space.
inline void place_linear_gradient(LayerStyleGradient& gradient, const GradientReferenceBox& box, double x1, double y1,
                                  double x2, double y2) {
  constexpr double kEpsilon = 1e-9;
  const double width = std::max(1.0, box.width);
  const double height = std::max(1.0, box.height);
  const double dx = x2 - x1;
  const double dy = y2 - y1;
  // Screen y grows downward; Photoshop angles are counter-clockwise.
  gradient.angle_degrees = static_cast<float>(std::atan2(-dy, dx) * 180.0 / std::numbers::pi);
  const double center_x = (x1 + x2) / 2.0;
  const double center_y = (y1 + y2) / 2.0;
  gradient.offset_x_percent = static_cast<float>((center_x - (box.x + width / 2.0)) / width * 100.0);
  gradient.offset_y_percent = static_cast<float>((center_y - (box.y + height / 2.0)) / height * 100.0);
  const double radians = gradient.angle_degrees * std::numbers::pi / 180.0;
  const double abs_cos = std::abs(std::cos(radians));
  const double abs_sin = std::abs(std::sin(radians));
  const double span = std::min(abs_cos > kEpsilon ? width / abs_cos : std::numeric_limits<double>::infinity(),
                               abs_sin > kEpsilon ? height / abs_sin : std::numeric_limits<double>::infinity());
  gradient.scale = static_cast<float>(std::clamp(std::hypot(dx, dy) / std::max(1.0, span), 0.01, 10.0));
}

// A radial ramp centered at (cx, cy) with radius r, in document space.
inline void place_radial_gradient(LayerStyleGradient& gradient, const GradientReferenceBox& box, double cx, double cy,
                                  double r) {
  const double width = std::max(1.0, box.width);
  const double height = std::max(1.0, box.height);
  gradient.offset_x_percent = static_cast<float>((cx - (box.x + width / 2.0)) / width * 100.0);
  gradient.offset_y_percent = static_cast<float>((cy - (box.y + height / 2.0)) / height * 100.0);
  gradient.scale = static_cast<float>(std::clamp(r / std::max(1.0, std::max(width, height) / 2.0), 0.01, 10.0));
}

}  // namespace patchy::formats
