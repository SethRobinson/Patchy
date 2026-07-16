#pragma once

#include "ui/filter_workflows.hpp"
#include "ui/zoomable_image_preview.hpp"

#include <functional>
#include <optional>

namespace patchy::ui {

// Geometry shared by the spatial-overlay hosts (the gallery and the direct
// filter dialogs): the active entry's traced input rectangle and the final
// displayed proxy bounds, both in the proxy's local pixel space. Only the
// coordinate/sync math lives here — the overlay DRAWING stays in
// zoomable_image_preview.cpp (the tilt-shift grip bars are a patent
// design-around; see docs/filters.md).
struct FilterOverlayGeometry {
  Rect input_bounds{};
  Rect result_bounds{};
};

struct FilterOverlayState {
  std::optional<NormalizedCenterRadiusOverlay> center_radius;
  std::optional<NormalizedTiltShiftOverlay> tilt_shift;
};

// Finds the first control carrying the presentation role, or null.
[[nodiscard]] const FilterControlSpec* overlay_find_control(
    const FilterDialogSpec& spec, FilterParameterPresentation presentation);

// Builds the overlay for the spec's presentation roles from the current
// parameter values (the historical refresh_spatial_overlay math). Both
// members are empty when the filter has no center roles.
[[nodiscard]] FilterOverlayState overlay_state_for(
    const FilterDialogSpec& spec,
    const std::function<double(const FilterControlSpec&)>& value_for,
    const FilterOverlayGeometry& geometry);

// Maps a displayed normalized coordinate back to the stored center percent
// (the inverse of the overlay's center mapping).
[[nodiscard]] double overlay_source_percent(double normalized,
                                            int source_origin,
                                            int source_extent,
                                            int result_origin,
                                            int result_extent);

// Converts a displayed normalized width fraction back to a percent of the
// source's shorter extent (tilt-shift widths and the radius circle).
[[nodiscard]] double overlay_width_percent_from_normalized(
    double normalized, const FilterOverlayGeometry& geometry);

}  // namespace patchy::ui
