#include "ui/ruler_tool.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace patchy::ui {

namespace {

constexpr double kDegreesPerRadian = 180.0 / std::numbers::pi;

// Counterclockwise on screen (y down), (-180, 180].
double leg_angle_degrees(QPointF from, QPointF to) {
  const auto dx = to.x() - from.x();
  const auto dy = to.y() - from.y();
  if (dx == 0.0 && dy == 0.0) {
    return 0.0;
  }
  auto degrees = std::atan2(-dy, dx) * kDegreesPerRadian;
  if (degrees <= -180.0) {
    degrees += 360.0;
  }
  return degrees;
}

double leg_length(QPointF from, QPointF to) {
  return std::hypot(to.x() - from.x(), to.y() - from.y());
}

}  // namespace

RulerMeasurement measure_ruler(const RulerLine& line) {
  RulerMeasurement measurement;
  measurement.x = line.start.x();
  measurement.y = line.start.y();
  measurement.width = line.end.x() - line.start.x();
  measurement.height = line.end.y() - line.start.y();
  measurement.length1 = leg_length(line.start, line.end);
  measurement.angle_degrees = leg_angle_degrees(line.start, line.end);
  if (line.second.has_value()) {
    measurement.length2 = leg_length(line.start, *line.second);
    measurement.width2 = line.second->x() - line.start.x();
    measurement.height2 = line.second->y() - line.start.y();
    auto between = std::abs(leg_angle_degrees(line.start, *line.second) - measurement.angle_degrees);
    if (between > 180.0) {
      between = 360.0 - between;
    }
    measurement.angle_degrees = between;
  }
  return measurement;
}

double ruler_straighten_degrees(const RulerLine& line) {
  if (line.start == line.end) {
    return 0.0;
  }
  const auto angle = leg_angle_degrees(line.start, line.end);
  const auto target = std::round(angle / 90.0) * 90.0;
  return target - angle;
}

QPointF constrain_ruler_point(QPointF anchor, QPointF point) {
  const auto dx = point.x() - anchor.x();
  const auto dy = point.y() - anchor.y();
  if (dx == 0.0 && dy == 0.0) {
    return point;
  }
  const auto step = std::round(std::atan2(dy, dx) / (std::numbers::pi / 4.0));
  const auto octant = (static_cast<int>(step) % 8 + 8) % 8;
  if (octant == 0 || octant == 4) {
    return {std::round(point.x()), anchor.y()};
  }
  if (octant == 2 || octant == 6) {
    return {anchor.x(), std::round(point.y())};
  }
  const auto reach = std::round((std::abs(dx) + std::abs(dy)) / 2.0);
  const auto sign_x = (octant == 1 || octant == 7) ? 1.0 : -1.0;
  const auto sign_y = (octant == 1 || octant == 3) ? 1.0 : -1.0;
  return {anchor.x() + sign_x * reach, anchor.y() + sign_y * reach};
}

RulerStraightenCrop ruler_straighten_crop(int width, int height, double degrees) {
  RulerStraightenCrop crop;
  if (width <= 0 || height <= 0) {
    return crop;
  }
  const auto radians = std::abs(degrees) / kDegreesPerRadian;
  const auto cosine = std::abs(std::cos(radians));
  const auto sine = std::abs(std::sin(radians));
  const double w = width;
  const double h = height;
  const auto scale = std::min({1.0, w / (w * cosine + h * sine), h / (w * sine + h * cosine)});
  crop.width = std::max(1, static_cast<int>(std::floor(w * scale + 1e-9)));
  crop.height = std::max(1, static_cast<int>(std::floor(h * scale + 1e-9)));
  crop.x = (width - crop.width) / 2;
  crop.y = (height - crop.height) / 2;
  return crop;
}

RulerReadout format_ruler_readout(const RulerMeasurement& measurement, MeasurementUnit unit,
                                  double horizontal_ppi, double vertical_ppi, double document_width,
                                  double document_height) {
  const auto h_ppi = sanitized_document_ppi(horizontal_ppi);
  const auto v_ppi = sanitized_document_ppi(vertical_ppi);
  const auto position = [&](double pixels, bool horizontal) {
    if (unit == MeasurementUnit::Pixels) {
      return format_pixels(pixels, 0);
    }
    const auto value = pixels_to_measurement_unit(pixels, unit, horizontal ? h_ppi : v_ppi,
                                                  horizontal ? document_width : document_height);
    return format_measurement(value, unit, measurement_unit_decimals(unit));
  };
  // A length has no Percent basis; physical units convert each axis at its own PPI.
  const auto length = [&](double pixels, double dx, double dy) {
    if (unit == MeasurementUnit::Pixels || unit == MeasurementUnit::Percent) {
      return format_pixels(pixels, 1);
    }
    const auto x = pixels_to_measurement_unit(dx, unit, h_ppi, document_width);
    const auto y = pixels_to_measurement_unit(dy, unit, v_ppi, document_height);
    const auto scale = pixels > 0.0 ? std::hypot(x, y) / std::hypot(dx, dy) : 0.0;
    return format_measurement(pixels * scale, unit, measurement_unit_decimals(unit));
  };
  RulerReadout readout;
  readout.x = position(measurement.x, true);
  readout.y = position(measurement.y, false);
  readout.width = position(measurement.width, true);
  readout.height = position(measurement.height, false);
  readout.angle = format_degrees(measurement.angle_degrees, 1);
  readout.length1 = length(measurement.length1, measurement.width, measurement.height);
  if (measurement.length2.has_value()) {
    readout.length2 = length(*measurement.length2, measurement.width2, measurement.height2);
  }
  return readout;
}

}  // namespace patchy::ui
