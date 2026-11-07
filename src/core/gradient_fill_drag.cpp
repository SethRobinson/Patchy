#include "core/gradient_fill_drag.hpp"

#include "core/blend_math.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace patchy {

namespace {

constexpr float kPi = 3.14159265358979323846F;
constexpr double kPiDouble = 3.14159265358979323846;
// The Gradient Fill dialog's Scale range (1% to 1000%); gradient_position floors it at 1% too.
constexpr float kMinScale = 0.01F;
constexpr float kMaxScale = 10.0F;
// The Edit Gradient Stops dialog's midpoint range (5% to 95%).
constexpr float kMinMidpoint = 0.05F;
constexpr float kMaxMidpoint = 0.95F;

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

LayerStyleGradientType fill_type_for(GradientMethod method) {
  switch (method) {
    case GradientMethod::Radial:
      return LayerStyleGradientType::Radial;
    case GradientMethod::Angle:
      return LayerStyleGradientType::Angle;
    case GradientMethod::Reflected:
      return LayerStyleGradientType::Reflected;
    case GradientMethod::Diamond:
      return LayerStyleGradientType::Diamond;
    case GradientMethod::Linear:
      break;
  }
  return LayerStyleGradientType::Linear;
}

// Angle and Diamond fills sample pixel centers; the others sample top-left corners like the
// Gradient tool, so only those two move their center half a pixel off the drag start.
bool center_sampled(LayerStyleGradientType type) {
  return type == LayerStyleGradientType::Angle || type == LayerStyleGradientType::Diamond;
}

// Sorts `stops` by location (stable) and returns where the stop at `tracked` ended up.
template <typename Stop>
std::size_t sort_stops_tracking(std::vector<Stop>& stops, std::size_t tracked) {
  std::vector<std::size_t> order(stops.size());
  for (std::size_t i = 0; i < order.size(); ++i) {
    order[i] = i;
  }
  std::stable_sort(order.begin(), order.end(),
                   [&stops](std::size_t a, std::size_t b) { return stops[a].location < stops[b].location; });
  std::vector<Stop> sorted;
  sorted.reserve(stops.size());
  std::size_t result = tracked;
  for (std::size_t i = 0; i < order.size(); ++i) {
    if (order[i] == tracked) {
      result = i;
    }
    sorted.push_back(stops[order[i]]);
  }
  stops = std::move(sorted);
  return result;
}

float unit_location(float location) {
  return std::isfinite(location) ? std::clamp(location, 0.0F, 1.0F) : 0.0F;
}

float clamped_midpoint(float midpoint) {
  return std::isfinite(midpoint) ? std::clamp(midpoint, kMinMidpoint, kMaxMidpoint) : 0.5F;
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
    // The tool's midpoint remaps color and alpha alike; Classic at smoothness 0 remaps the
    // same way per list.
    gradient.color_stops.push_back(
        GradientColorStop{stop.location, RgbColor{stop.color.r, stop.color.g, stop.color.b}, stop.midpoint});
    gradient.alpha_stops.push_back(
        GradientAlphaStop{stop.location, static_cast<float>(stop.color.a) / 255.0F, stop.midpoint});
  }
  gradient.type = fill_type_for(method);
  gradient.reverse = reverse;
  gradient.dither = false;
  gradient.interpolation = GradientInterpolationMethod::Classic;
  gradient.align_with_layer = false;
  place_gradient_fill_line(gradient, canvas,
                           GradientFillLine{static_cast<double>(x0), static_cast<double>(y0), static_cast<double>(x1),
                                            static_cast<double>(y1)});
  return gradient;
}

namespace {

// How one stop list behaves over the merged segment (a, b).
struct SegmentRamp {
  enum class Kind { Constant, Straight, Midpoint, Split } kind{Kind::Constant};
  float midpoint{0.5F};
};

template <typename Stop, typename Same>
SegmentRamp segment_ramp(const std::vector<Stop>& stops, float a, float b, Same same) {
  constexpr float kEpsilon = 1e-5F;
  if (stops.size() < 2U || b <= stops.front().location + kEpsilon || a >= stops.back().location - kEpsilon) {
    return {};
  }
  for (std::size_t index = 1; index < stops.size(); ++index) {
    const auto& left = stops[index - 1U];
    const auto& right = stops[index];
    if (a >= left.location - kEpsilon && b <= right.location + kEpsilon) {
      if (same(left, right)) {
        return {};
      }
      if (right.midpoint == 0.5F) {
        return {SegmentRamp::Kind::Straight, 0.5F};
      }
      const bool whole = std::abs(a - left.location) <= kEpsilon && std::abs(b - right.location) <= kEpsilon;
      return whole ? SegmentRamp{SegmentRamp::Kind::Midpoint, right.midpoint} : SegmentRamp{SegmentRamp::Kind::Split};
    }
  }
  return {SegmentRamp::Kind::Split};
}

}  // namespace

std::vector<GradientStop> gradient_tool_stops_from_gradient(const LayerStyleGradient& source) {
  auto gradient = source;
  // The tool interpolates straight between its stops.
  gradient.smoothness = 0;
  gradient.interpolation = GradientInterpolationMethod::Classic;
  // A color stop's 32-bit Intensity, interpolated by location (none on Noise).
  std::vector<std::pair<float, float>> intensity;
  for (const auto& stop : gradient.color_stops) {
    intensity.emplace_back(stop.location, stop.intensity_stops);
  }
  std::stable_sort(intensity.begin(), intensity.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
  const auto intensity_at = [&intensity](float location) {
    if (intensity.empty() || location <= intensity.front().first) {
      return intensity.empty() ? 0.0F : intensity.front().second;
    }
    for (std::size_t index = 1; index < intensity.size(); ++index) {
      if (location <= intensity[index].first) {
        const auto& left = intensity[index - 1U];
        const auto& right = intensity[index];
        const auto span = std::max(0.0001F, right.first - left.first);
        return left.second + (right.second - left.second) * std::clamp((location - left.first) / span, 0.0F, 1.0F);
      }
    }
    return intensity.back().second;
  };
  const auto stop_at = [&gradient, &intensity_at](float location, float midpoint) {
    const auto color = gradient_color(gradient, location);
    const auto alpha = gradient_stop_opacity(gradient, location);
    GradientStop stop;
    stop.location = location;
    stop.color = EditColor{color.red, color.green, color.blue,
                           static_cast<std::uint8_t>(std::clamp(std::lround(alpha * 255.0F), 0L, 255L))};
    stop.midpoint = midpoint;
    stop.intensity_stops = gradient.form == GradientDefinitionForm::Noise ? 0.0F : intensity_at(location);
    return stop;
  };
  std::vector<GradientStop> result;
  if (gradient.form == GradientDefinitionForm::Noise || gradient.color_stops.empty()) {
    constexpr int kSamples = 65;
    for (int index = 0; index < kSamples; ++index) {
      result.push_back(stop_at(static_cast<float>(index) / static_cast<float>(kSamples - 1), 0.5F));
    }
    return result;
  }
  const auto by_location = [](const auto& a, const auto& b) { return a.location < b.location; };
  std::stable_sort(gradient.color_stops.begin(), gradient.color_stops.end(), by_location);
  std::stable_sort(gradient.alpha_stops.begin(), gradient.alpha_stops.end(), by_location);
  std::vector<float> locations;
  for (const auto& stop : gradient.color_stops) {
    locations.push_back(std::clamp(stop.location, 0.0F, 1.0F));
  }
  for (const auto& stop : gradient.alpha_stops) {
    locations.push_back(std::clamp(stop.location, 0.0F, 1.0F));
  }
  std::sort(locations.begin(), locations.end());
  std::vector<float> unique;
  for (const auto location : locations) {
    if (unique.empty() || location - unique.back() > 1e-5F) {
      unique.push_back(location);
    }
  }
  const auto same_color = [](const GradientColorStop& a, const GradientColorStop& b) { return a.color == b.color; };
  const auto same_alpha = [](const GradientAlphaStop& a, const GradientAlphaStop& b) { return a.opacity == b.opacity; };
  for (std::size_t index = 0; index < unique.size(); ++index) {
    if (index == 0U) {
      result.push_back(stop_at(unique[0], 0.5F));
      continue;
    }
    const auto a = unique[index - 1U];
    const auto b = unique[index];
    const auto color = segment_ramp(gradient.color_stops, a, b, same_color);
    const auto alpha = segment_ramp(gradient.alpha_stops, a, b, same_alpha);
    using Kind = SegmentRamp::Kind;
    std::optional<float> midpoint;
    if (color.kind == Kind::Split || alpha.kind == Kind::Split) {
      midpoint.reset();
    } else if (color.kind == Kind::Constant) {
      midpoint = alpha.midpoint;
    } else if (alpha.kind == Kind::Constant) {
      midpoint = color.midpoint;
    } else if (color.midpoint == alpha.midpoint) {
      midpoint = color.midpoint;
    }
    if (midpoint.has_value()) {
      result.push_back(stop_at(b, *midpoint));
      continue;
    }
    constexpr int kPieces = 8;
    for (int piece = 1; piece <= kPieces; ++piece) {
      result.push_back(stop_at(a + (b - a) * static_cast<float>(piece) / static_cast<float>(kPieces), 0.5F));
    }
  }
  if (result.size() == 1U) {
    result.push_back(stop_at(1.0F, 0.5F));
    result.front().location = 0.0F;
  }
  return result;
}

LayerStyleGradient gradient_from_tool_stops(const std::vector<GradientStop>& stops) {
  LayerStyleGradient gradient;
  gradient.name = "Custom";
  gradient.form = GradientDefinitionForm::Solid;
  gradient.smoothness = 0;
  for (const auto& stop : normalized_gradient_stops(stops)) {
    GradientColorStop color{stop.location, RgbColor{stop.color.r, stop.color.g, stop.color.b}, stop.midpoint};
    color.intensity_stops = stop.intensity_stops;
    gradient.color_stops.push_back(color);
    gradient.alpha_stops.push_back(
        GradientAlphaStop{stop.location, static_cast<float>(stop.color.a) / 255.0F, stop.midpoint});
  }
  return gradient;
}

LayerStyleGradient resolved_tool_gradient(const GradientDefinition& definition, EditColor foreground,
                                          EditColor background, float foreground_intensity,
                                          float background_intensity) {
  LayerStyleGradient gradient;
  static_cast<GradientDefinition&>(gradient) = definition;
  for (auto& stop : gradient.color_stops) {
    if (stop.kind == GradientColorStop::Kind::Foreground) {
      stop.color = RgbColor{foreground.r, foreground.g, foreground.b};
      stop.intensity_stops = foreground_intensity;
    } else if (stop.kind == GradientColorStop::Kind::Background) {
      stop.color = RgbColor{background.r, background.g, background.b};
      stop.intensity_stops = background_intensity;
    }
    stop.kind = GradientColorStop::Kind::User;
  }
  std::stable_sort(gradient.color_stops.begin(), gradient.color_stops.end(),
                   [](const auto& a, const auto& b) { return a.location < b.location; });
  std::stable_sort(gradient.alpha_stops.begin(), gradient.alpha_stops.end(),
                   [](const auto& a, const auto& b) { return a.location < b.location; });
  return gradient;
}

LayerStyleGradient gradient_fill_from_drag(const LayerStyleGradient& resolved, GradientMethod method, bool reverse,
                                           Rect canvas, std::int32_t x0, std::int32_t y0, std::int32_t x1,
                                           std::int32_t y1) {
  LayerStyleGradient gradient;
  static_cast<GradientDefinition&>(gradient) = static_cast<const GradientDefinition&>(resolved);
  // Fill layers have no Intensity (definitions, PSD and GRD carry none).
  for (auto& stop : gradient.color_stops) {
    stop.intensity_stops = 0.0F;
    stop.kind = GradientColorStop::Kind::User;
  }
  gradient.type = fill_type_for(method);
  gradient.reverse = reverse;
  gradient.dither = false;
  gradient.interpolation = GradientInterpolationMethod::Classic;
  gradient.align_with_layer = false;
  place_gradient_fill_line(gradient, canvas,
                           GradientFillLine{static_cast<double>(x0), static_cast<double>(y0), static_cast<double>(x1),
                                            static_cast<double>(y1)});
  return gradient;
}

GradientFillLine gradient_fill_line(const LayerStyleGradient& gradient, Rect bounds) {
  const auto radians = static_cast<double>(gradient.angle_degrees) * kPiDouble / 180.0;
  const auto span = static_cast<double>(center_chord_span(gradient.angle_degrees, bounds));
  const auto scale = static_cast<double>(std::max(kMinScale, gradient.scale));
  auto center_x = static_cast<double>(bounds.x) +
                  static_cast<double>(bounds.width) * (0.5 + static_cast<double>(gradient.offset_x_percent) / 100.0);
  auto center_y = static_cast<double>(bounds.y) +
                  static_cast<double>(bounds.height) * (0.5 + static_cast<double>(gradient.offset_y_percent) / 100.0);
  // The ramp runs along (cos a, -sin a) on screen (gradient_position).
  const auto direction_x = std::cos(radians);
  const auto direction_y = -std::sin(radians);
  if (gradient.type == LayerStyleGradientType::Linear) {
    const auto half = span * scale * 0.5;
    return GradientFillLine{center_x - direction_x * half, center_y - direction_y * half,
                            center_x + direction_x * half, center_y + direction_y * half};
  }
  if (center_sampled(gradient.type)) {
    center_x -= 0.5;
    center_y -= 0.5;
  }
  const auto length = span * scale * 0.5;
  return GradientFillLine{center_x, center_y, center_x + direction_x * length, center_y + direction_y * length};
}

void place_gradient_fill_line(LayerStyleGradient& gradient, Rect bounds, const GradientFillLine& line) {
  const auto dx = line.end_x - line.start_x;
  const auto dy = line.end_y - line.start_y;
  const auto length = std::hypot(dx, dy);
  if (!(length > 0.0) || !std::isfinite(length) || bounds.width <= 0 || bounds.height <= 0) {
    return;
  }
  // gradient_position's ramp runs along (cos a, -sin a): angles grow counterclockwise on screen.
  gradient.angle_degrees = static_cast<float>(std::atan2(-dy, dx) * 180.0 / kPiDouble);
  const auto span = static_cast<double>(center_chord_span(gradient.angle_degrees, bounds));
  // Linear, Reflected and Radial fills sample each pixel at its top-left corner like the
  // tool, so the drag points map straight on; Angle and Diamond fills sample pixel centers,
  // so their center moves half a pixel to land on the tool's drag start.
  // Linear: position = 0.5 + along / (span * scale) about the center, and the tool's
  // (p - from) . d / |d|^2 is 0.5 + along / |d| about the drag's midpoint. Reflected, Radial and
  // Diamond: |along|, distance or L1 distance / (span * scale / 2) about the center against the
  // same over |d| about the drag start. Angle ignores Scale; it keeps the Radial value.
  double center_x = 0.0;
  double center_y = 0.0;
  double scale = 1.0;
  if (gradient.type != LayerStyleGradientType::Linear) {
    const auto half_pixel = center_sampled(gradient.type) ? 0.5 : 0.0;
    center_x = line.start_x + half_pixel;
    center_y = line.start_y + half_pixel;
    scale = 2.0 * length / span;
  } else {
    center_x = (line.start_x + line.end_x) * 0.5;
    center_y = (line.start_y + line.end_y) * 0.5;
    scale = length / span;
  }
  gradient.scale =
      static_cast<float>(std::clamp(scale, static_cast<double>(kMinScale), static_cast<double>(kMaxScale)));
  gradient.offset_x_percent = static_cast<float>(
      ((center_x - static_cast<double>(bounds.x)) / static_cast<double>(bounds.width) - 0.5) * 100.0);
  gradient.offset_y_percent = static_cast<float>(
      ((center_y - static_cast<double>(bounds.y)) / static_cast<double>(bounds.height) - 0.5) * 100.0);
}

double gradient_fill_line_location(const GradientFillLine& line, bool reverse, double x, double y) {
  const auto dx = line.end_x - line.start_x;
  const auto dy = line.end_y - line.start_y;
  const auto length_squared = dx * dx + dy * dy;
  if (!(length_squared > 0.0)) {
    return 0.0;
  }
  const auto along = ((x - line.start_x) * dx + (y - line.start_y) * dy) / length_squared;
  return reverse ? 1.0 - along : along;
}

std::pair<double, double> gradient_fill_line_point(const GradientFillLine& line, bool reverse, double location) {
  const auto along = reverse ? 1.0 - location : location;
  return {line.start_x + (line.end_x - line.start_x) * along, line.start_y + (line.end_y - line.start_y) * along};
}

std::size_t add_gradient_color_stop(LayerStyleGradient& gradient, float location) {
  location = unit_location(location);
  // Fill layers render with endpoint smoothing (the GdFl path in vector_raster.cpp), so the
  // new stop takes the color shown there.
  const auto color = gradient_color(gradient, location, true);
  gradient.color_stops.push_back(GradientColorStop{location, color, 0.5F});
  return sort_stops_tracking(gradient.color_stops, gradient.color_stops.size() - 1U);
}

std::size_t add_gradient_alpha_stop(LayerStyleGradient& gradient, float location) {
  location = unit_location(location);
  const auto opacity = gradient_stop_opacity(gradient, location, true);
  gradient.alpha_stops.push_back(GradientAlphaStop{location, opacity, 0.5F});
  return sort_stops_tracking(gradient.alpha_stops, gradient.alpha_stops.size() - 1U);
}

std::size_t move_gradient_color_stop(LayerStyleGradient& gradient, std::size_t index, float location) {
  if (index >= gradient.color_stops.size()) {
    return index;
  }
  gradient.color_stops[index].location = unit_location(location);
  return sort_stops_tracking(gradient.color_stops, index);
}

std::size_t move_gradient_alpha_stop(LayerStyleGradient& gradient, std::size_t index, float location) {
  if (index >= gradient.alpha_stops.size()) {
    return index;
  }
  gradient.alpha_stops[index].location = unit_location(location);
  return sort_stops_tracking(gradient.alpha_stops, index);
}

bool remove_gradient_color_stop(LayerStyleGradient& gradient, std::size_t index) {
  if (index >= gradient.color_stops.size() || gradient.color_stops.size() <= 2U) {
    return false;
  }
  gradient.color_stops.erase(gradient.color_stops.begin() + static_cast<std::ptrdiff_t>(index));
  return true;
}

bool remove_gradient_alpha_stop(LayerStyleGradient& gradient, std::size_t index) {
  if (index >= gradient.alpha_stops.size() || gradient.alpha_stops.size() <= 2U) {
    return false;
  }
  gradient.alpha_stops.erase(gradient.alpha_stops.begin() + static_cast<std::ptrdiff_t>(index));
  return true;
}

void set_gradient_color_midpoint(LayerStyleGradient& gradient, std::size_t index, float midpoint) {
  if (index == 0U || index >= gradient.color_stops.size()) {
    return;
  }
  gradient.color_stops[index].midpoint = clamped_midpoint(midpoint);
}

void set_gradient_alpha_midpoint(LayerStyleGradient& gradient, std::size_t index, float midpoint) {
  if (index == 0U || index >= gradient.alpha_stops.size()) {
    return;
  }
  gradient.alpha_stops[index].midpoint = clamped_midpoint(midpoint);
}

}  // namespace patchy
