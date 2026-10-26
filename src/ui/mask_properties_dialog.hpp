#pragma once

#include <functional>
#include <optional>

class QString;
class QWidget;

namespace patchy::ui {

// The two mask parameters Photoshop keeps on the Properties panel's Masks
// page: Density (0..100 %) and Feather (pixels). Both layer masks and vector
// masks carry them (LayerMask, LayerVectorMask); they apply at render time.
struct MaskPropertiesValues {
  int density_percent{100};
  int feather{0};
};

// Runs the non-modal Mask Properties dialog (object name maskPropertiesDialog)
// a double-click on a layer row's mask thumbnail opens. `preview` sees every
// change while the dialog is open; returns the accepted values, or nullopt on
// Cancel (the caller restores what the preview changed).
[[nodiscard]] std::optional<MaskPropertiesValues> request_mask_properties(
    QWidget* parent, bool vector_mask, const QString& layer_name, MaskPropertiesValues initial,
    const std::function<void(const MaskPropertiesValues&)>& preview);

}  // namespace patchy::ui
