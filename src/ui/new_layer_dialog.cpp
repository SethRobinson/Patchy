#include "ui/new_layer_dialog.hpp"

#include "ui/blend_mode_ui.hpp"
#include "ui/dialog_utils.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace patchy::ui {
namespace {

class NewLayerDialogStrings {
  Q_DECLARE_TR_FUNCTIONS(NewLayerDialog)
};

}  // namespace

std::optional<NewLayerSettings> request_new_layer_settings(QWidget* parent, const QString& default_name,
                                                           bool clip_available, BitDepth depth,
                                                           const std::vector<NewLayerColorChoice>& colors) {
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("newLayerDialog"));
  dialog.setWindowTitle(NewLayerDialogStrings::tr("New Layer"));
  auto* layout = new QVBoxLayout(&dialog);
  auto* form = new QFormLayout();
  form->setHorizontalSpacing(10);
  form->setVerticalSpacing(8);

  auto* name_edit = new QLineEdit(default_name, &dialog);
  name_edit->setObjectName(QStringLiteral("newLayerNameEdit"));
  name_edit->setPlaceholderText(default_name);
  form->addRow(NewLayerDialogStrings::tr("Name:"), name_edit);

  // The Layers panel row strip's colors, swatch and all.
  auto* color_combo = new QComboBox(&dialog);
  color_combo->setObjectName(QStringLiteral("newLayerColorCombo"));
  for (std::size_t index = 0; index < colors.size(); ++index) {
    color_combo->addItem(colors[index].swatch, colors[index].name, static_cast<int>(index));
  }
  form->addRow(NewLayerDialogStrings::tr("Color:"), color_combo);

  auto* mode_combo = new QComboBox(&dialog);
  mode_combo->setObjectName(QStringLiteral("newLayerModeCombo"));
  add_blend_mode_items(mode_combo, BlendModeMenu::Layer);
  enable_blend_mode_items_for_depth(mode_combo, depth);
  mode_combo->setCurrentIndex(std::max(0, mode_combo->findData(static_cast<int>(BlendMode::Normal))));
  form->addRow(NewLayerDialogStrings::tr("Mode:"), mode_combo);

  auto* opacity_spin = add_dialog_slider_spin_row(form, &dialog, NewLayerDialogStrings::tr("Opacity:"),
                                                  QStringLiteral("newLayerOpacitySlider"),
                                                  QStringLiteral("newLayerOpacitySpin"), 0, 100, 100,
                                                  QStringLiteral("%"));
  layout->addLayout(form);

  auto* clip_check = new QCheckBox(NewLayerDialogStrings::tr("Use Previous Layer to Create Clipping Mask"), &dialog);
  clip_check->setObjectName(QStringLiteral("newLayerClipCheck"));
  clip_check->setEnabled(clip_available);
  clip_check->setToolTip(NewLayerDialogStrings::tr("Clip the new layer to the layer below it"));
  layout->addWidget(clip_check);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  dialog.setMinimumWidth(380);
  // Typing replaces the default name, as in the other naming dialogs.
  select_all_on_focus(*name_edit);
  name_edit->selectAll();
  name_edit->setFocus();
  if (exec_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }

  NewLayerSettings settings;
  const auto typed = name_edit->text().trimmed();
  settings.name = typed.isEmpty() ? default_name : typed;
  settings.clip_to_previous = clip_available && clip_check->isChecked();
  settings.color_label = static_cast<LayerColorLabel>(std::max(0, color_combo->currentData().toInt()));
  settings.blend_mode = static_cast<BlendMode>(mode_combo->currentData().toInt());
  settings.opacity = static_cast<float>(opacity_spin->value()) / 100.0F;
  return settings;
}

}  // namespace patchy::ui
