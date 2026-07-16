#include "ui/filter_overlay_sync.hpp"

#include <algorithm>
#include <cmath>

namespace patchy::ui {
namespace {

// Maps a stored center percent of the traced input rectangle into the
// displayed result's normalized 0..1 space (the historical mapped_center).
[[nodiscard]] double mapped_center(double percent, int source_origin,
                                   int source_extent, int result_origin,
                                   int result_extent) {
  if (source_extent <= 1 || result_extent <= 1) {
    return 0.5;
  }
  const auto source_coordinate =
      static_cast<double>(source_origin) +
      static_cast<double>(source_extent - 1) * percent / 100.0;
  return std::clamp(
      (source_coordinate - static_cast<double>(result_origin)) /
          static_cast<double>(result_extent - 1),
      0.0, 1.0);
}

[[nodiscard]] int shorter_extent(const Rect& bounds) {
  return std::max(1, std::min(bounds.width, bounds.height));
}

}  // namespace

const FilterControlSpec* overlay_find_control(
    const FilterDialogSpec& spec, FilterParameterPresentation presentation) {
  const auto found = std::find_if(
      spec.controls.begin(), spec.controls.end(),
      [presentation](const FilterControlSpec& control) {
        return control.presentation == presentation;
      });
  return found == spec.controls.end() ? nullptr : &*found;
}

FilterOverlayState overlay_state_for(
    const FilterDialogSpec& spec,
    const std::function<double(const FilterControlSpec&)>& value_for,
    const FilterOverlayGeometry& geometry) {
  FilterOverlayState state;
  const auto* center_x =
      overlay_find_control(spec, FilterParameterPresentation::CenterXPercent);
  const auto* center_y =
      overlay_find_control(spec, FilterParameterPresentation::CenterYPercent);
  if (center_x == nullptr || center_y == nullptr) {
    return state;
  }
  const QPointF mapped_overlay_center(
      mapped_center(value_for(*center_x), geometry.input_bounds.x,
                    geometry.input_bounds.width, geometry.result_bounds.x,
                    geometry.result_bounds.width),
      mapped_center(value_for(*center_y), geometry.input_bounds.y,
                    geometry.input_bounds.height, geometry.result_bounds.y,
                    geometry.result_bounds.height));
  const auto source_shorter = shorter_extent(geometry.input_bounds);
  const auto result_shorter = shorter_extent(geometry.result_bounds);

  const auto* focus_half_width = overlay_find_control(
      spec, FilterParameterPresentation::TiltFocusHalfWidthPercent);
  const auto* transition_width = overlay_find_control(
      spec, FilterParameterPresentation::TiltTransitionWidthPercent);
  const auto* angle =
      overlay_find_control(spec, FilterParameterPresentation::Angle);
  if (focus_half_width != nullptr && transition_width != nullptr &&
      angle != nullptr) {
    NormalizedTiltShiftOverlay overlay;
    overlay.center = mapped_overlay_center;
    overlay.angle_degrees = value_for(*angle);
    overlay.focus_half_width = std::clamp(
        value_for(*focus_half_width) / 100.0 *
            static_cast<double>(source_shorter) / result_shorter,
        0.0, 1.0);
    overlay.transition_width = std::clamp(
        value_for(*transition_width) / 100.0 *
            static_cast<double>(source_shorter) / result_shorter,
        0.0, 1.0);
    state.tilt_shift = overlay;
    return state;
  }

  NormalizedCenterRadiusOverlay overlay;
  overlay.center = mapped_overlay_center;
  if (const auto* radius = overlay_find_control(
          spec, FilterParameterPresentation::EffectRadiusPercent);
      radius != nullptr) {
    overlay.radius = std::clamp(
        value_for(*radius) / 100.0 *
            static_cast<double>(source_shorter) / result_shorter,
        0.01, 1.0);
  }
  state.center_radius = overlay;
  return state;
}

double overlay_source_percent(double normalized, int source_origin,
                              int source_extent, int result_origin,
                              int result_extent) {
  if (source_extent <= 1 || result_extent <= 1) {
    return 50.0;
  }
  const auto source_coordinate =
      static_cast<double>(result_origin) +
      normalized * static_cast<double>(result_extent - 1);
  return std::clamp(
      (source_coordinate - static_cast<double>(source_origin)) /
          static_cast<double>(source_extent - 1) * 100.0,
      0.0, 100.0);
}

double overlay_width_percent_from_normalized(
    double normalized, const FilterOverlayGeometry& geometry) {
  return normalized *
         static_cast<double>(shorter_extent(geometry.result_bounds)) /
         shorter_extent(geometry.input_bounds) * 100.0;
}

}  // namespace patchy::ui
