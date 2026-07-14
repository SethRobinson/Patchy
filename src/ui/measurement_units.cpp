#include "ui/measurement_units.hpp"

#include <QObject>

#include <algorithm>
#include <cmath>

namespace patchy::ui {

double sanitized_document_ppi(double ppi) noexcept {
  return std::isfinite(ppi) && ppi > 0.0 ? ppi : 300.0;
}

QString measurement_unit_suffix(MeasurementUnit unit) {
  switch (unit) {
    case MeasurementUnit::Pixels:
      return QObject::tr("px");
    case MeasurementUnit::Inches:
      return QObject::tr("in");
    case MeasurementUnit::Centimeters:
      return QObject::tr("cm");
    case MeasurementUnit::Millimeters:
      return QObject::tr("mm");
    case MeasurementUnit::Points:
      return QObject::tr("pt");
    case MeasurementUnit::Percent:
      return QStringLiteral("%");
  }
  return QObject::tr("px");
}

QString measurement_unit_name(MeasurementUnit unit) {
  switch (unit) {
    case MeasurementUnit::Pixels:
      return QObject::tr("Pixels");
    case MeasurementUnit::Inches:
      return QObject::tr("Inches");
    case MeasurementUnit::Centimeters:
      return QObject::tr("Centimeters");
    case MeasurementUnit::Millimeters:
      return QObject::tr("Millimeters");
    case MeasurementUnit::Points:
      return QObject::tr("Points");
    case MeasurementUnit::Percent:
      return QObject::tr("Percent");
  }
  return QObject::tr("Pixels");
}

QString measurement_unit_settings_token(MeasurementUnit unit) {
  switch (unit) {
    case MeasurementUnit::Pixels:
      return QStringLiteral("px");
    case MeasurementUnit::Inches:
      return QStringLiteral("in");
    case MeasurementUnit::Centimeters:
      return QStringLiteral("cm");
    case MeasurementUnit::Millimeters:
      return QStringLiteral("mm");
    case MeasurementUnit::Points:
      return QStringLiteral("pt");
    case MeasurementUnit::Percent:
      return QStringLiteral("percent");
  }
  return QStringLiteral("px");
}

MeasurementUnit measurement_unit_from_settings_token(const QString& token, MeasurementUnit fallback) {
  const auto lowered = token.trimmed().toLower();
  if (lowered == QStringLiteral("px")) {
    return MeasurementUnit::Pixels;
  }
  if (lowered == QStringLiteral("in")) {
    return MeasurementUnit::Inches;
  }
  if (lowered == QStringLiteral("cm")) {
    return MeasurementUnit::Centimeters;
  }
  if (lowered == QStringLiteral("mm")) {
    return MeasurementUnit::Millimeters;
  }
  if (lowered == QStringLiteral("pt")) {
    return MeasurementUnit::Points;
  }
  if (lowered == QStringLiteral("percent")) {
    return MeasurementUnit::Percent;
  }
  return fallback;
}

bool measurement_unit_is_physical(MeasurementUnit unit) noexcept {
  switch (unit) {
    case MeasurementUnit::Inches:
    case MeasurementUnit::Centimeters:
    case MeasurementUnit::Millimeters:
    case MeasurementUnit::Points:
      return true;
    case MeasurementUnit::Pixels:
    case MeasurementUnit::Percent:
      return false;
  }
  return false;
}

double measurement_units_per_inch(MeasurementUnit unit) noexcept {
  switch (unit) {
    case MeasurementUnit::Inches:
      return 1.0;
    case MeasurementUnit::Centimeters:
      return 2.54;
    case MeasurementUnit::Millimeters:
      return 25.4;
    case MeasurementUnit::Points:
      return 72.0;
    case MeasurementUnit::Pixels:
    case MeasurementUnit::Percent:
      return 1.0;
  }
  return 1.0;
}

double pixels_to_measurement_unit(double pixels, MeasurementUnit unit, double ppi,
                                  double reference_pixels) {
  switch (unit) {
    case MeasurementUnit::Pixels:
      return pixels;
    case MeasurementUnit::Percent:
      return reference_pixels > 0.0 ? pixels / reference_pixels * 100.0 : 100.0;
    default:
      return pixels / sanitized_document_ppi(ppi) * measurement_units_per_inch(unit);
  }
}

double measurement_unit_to_pixels(double value, MeasurementUnit unit, double ppi,
                                  double reference_pixels) {
  switch (unit) {
    case MeasurementUnit::Pixels:
      return value;
    case MeasurementUnit::Percent:
      return reference_pixels > 0.0 ? value / 100.0 * reference_pixels : value;
    default:
      return value * sanitized_document_ppi(ppi) / measurement_units_per_inch(unit);
  }
}

int measurement_unit_decimals(MeasurementUnit unit) noexcept {
  switch (unit) {
    case MeasurementUnit::Pixels:
      return 0;
    case MeasurementUnit::Inches:
      return 3;
    case MeasurementUnit::Centimeters:
      return 2;
    case MeasurementUnit::Millimeters:
      return 1;
    case MeasurementUnit::Points:
      return 2;
    case MeasurementUnit::Percent:
      return 2;
  }
  return 2;
}

RulerTickSteps ruler_tick_steps(MeasurementUnit unit, double pixels_per_unit_on_screen,
                                double min_major_screen_px) noexcept {
  RulerTickSteps steps;
  if (!std::isfinite(pixels_per_unit_on_screen) || pixels_per_unit_on_screen <= 0.0) {
    return steps;
  }
  min_major_screen_px = std::max(1.0, min_major_screen_px);

  // Smallest 1-2-5 decade step whose on-screen spacing reaches the threshold. The
  // pixel ruler historically floored at 1 px; physical units may go below 1 (0.5 in,
  // 0.2 cm, ...) when zoomed far in, so start the scan a few decades down.
  const double needed_units = min_major_screen_px / pixels_per_unit_on_screen;
  double decade = unit == MeasurementUnit::Pixels ? 1.0 : 0.001;
  constexpr double kSteps[] = {1.0, 2.0, 5.0};
  for (int iteration = 0; iteration < 64; ++iteration) {
    for (const auto step : kSteps) {
      const auto candidate = step * decade;
      if (candidate >= needed_units) {
        steps.major = candidate;
        if (unit == MeasurementUnit::Pixels) {
          // Pixel rulers never subdivide below one pixel; this reproduces the
          // historical minor spacing max(1, major / 5) exactly.
          steps.subdivisions = static_cast<int>(std::clamp(candidate, 1.0, 5.0));
        } else if (unit == MeasurementUnit::Inches && candidate <= 10.0 && step == 1.0) {
          // Inches traditionally subdivide in quarters at readable sizes.
          steps.subdivisions = 4;
        } else {
          steps.subdivisions = 5;
        }
        return steps;
      }
    }
    decade *= 10.0;
  }
  steps.major = decade;
  steps.subdivisions = 5;
  return steps;
}

}  // namespace patchy::ui
