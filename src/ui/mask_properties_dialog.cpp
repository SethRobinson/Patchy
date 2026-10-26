// Mask Properties: Density and Feather for a layer mask or a vector mask
// (docs/vector-tools.md, "Vector mask UI"; docs/layer-panel.md).
#include "ui/mask_properties_dialog.hpp"

#include "ui/curved_slider.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/measurement_units.hpp"
#include "ui/theme_qss.hpp"
#include "ui/unit_spin_box.hpp"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>

namespace patchy::ui {

std::optional<MaskPropertiesValues> request_mask_properties(
    QWidget* parent, bool vector_mask, const QString& layer_name, MaskPropertiesValues initial,
    const std::function<void(const MaskPropertiesValues&)>& preview) {
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("maskPropertiesDialog"));
  dialog.setWindowTitle(vector_mask ? QObject::tr("Vector Mask Properties")
                                    : QObject::tr("Layer Mask Properties"));
  dialog.setMinimumWidth(340);
  auto* layout = new QVBoxLayout(&dialog);
  auto* heading = new QLabel(layer_name, &dialog);
  heading->setObjectName(QStringLiteral("maskPropertiesLayerName"));
  heading->setTextFormat(Qt::PlainText);
  layout->addWidget(heading);

  // One field width wide enough for "1000 px", so both sliders end at the same x.
  constexpr int kFieldWidth = 120;
  auto* form = new QFormLayout();
  form->setHorizontalSpacing(10);
  form->setVerticalSpacing(8);
  layout->addLayout(form);
  auto* density = add_dialog_slider_spin_row(
      form, &dialog, QObject::tr("Density:"), QStringLiteral("maskPropertiesDensitySlider"),
      QStringLiteral("maskPropertiesDensitySpin"), 0, 100, std::clamp(initial.density_percent, 0, 100),
      percent_suffix(), kFieldWidth);
  density->setToolTip(QObject::tr("How opaque the hidden areas of the mask are. Below 100% the "
                                  "layer shows through them."));
  auto* feather = add_dialog_slider_spin_row(
      form, &dialog, QObject::tr("Feather:"), QStringLiteral("maskPropertiesFeatherSlider"),
      QStringLiteral("maskPropertiesFeatherSpin"), 0, 1000, std::clamp(initial.feather, 0, 1000),
      SpinUnit::Pixels, {}, kFieldWidth, -1, false, SliderCurve::FineLowEnd);
  feather->setToolTip(QObject::tr("Softens the mask's edges by this many pixels"));

  auto values = initial;
  const auto changed = [&values, &preview, density, feather] {
    values.density_percent = density->value();
    values.feather = feather->value();
    if (preview) {
      preview(values);
    }
  };
  QObject::connect(density, &QSpinBox::valueChanged, &dialog, changed);
  QObject::connect(feather, &QSpinBox::valueChanged, &dialog, changed);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget(buttons);
  append_themed_style(dialog, dialog_spinbox_button_style());
  if (run_non_modal_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }
  values.density_percent = density->value();
  values.feather = feather->value();
  return values;
}

}  // namespace patchy::ui
