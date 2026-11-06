#include "ui/new_layer_dialog.hpp"

#include "ui/blend_mode_ui.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/fill_dialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
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

// The neutral color itself (document content): white, black or 50% gray.
QColor neutral_fill_qcolor(NeutralFillColor color) {
  const int value = color == NeutralFillColor::White ? 255 : color == NeutralFillColor::Black ? 0 : 128;
  return {value, value, value};
}

}  // namespace

std::optional<NeutralFillColor> neutral_fill_for_blend_mode(BlendMode mode) noexcept {
  switch (mode) {
    case BlendMode::Darken:
    case BlendMode::Multiply:
    case BlendMode::ColorBurn:
    case BlendMode::LinearBurn:
    case BlendMode::DarkerColor:
    case BlendMode::Divide:
      return NeutralFillColor::White;
    case BlendMode::Lighten:
    case BlendMode::Screen:
    case BlendMode::ColorDodge:
    case BlendMode::LinearDodge:
    case BlendMode::LighterColor:
    case BlendMode::Difference:
    case BlendMode::Exclusion:
    case BlendMode::Subtract:
      return NeutralFillColor::Black;
    case BlendMode::Overlay:
    case BlendMode::SoftLight:
    case BlendMode::HardLight:
    case BlendMode::VividLight:
    case BlendMode::LinearLight:
    case BlendMode::PinLight:
      return NeutralFillColor::Gray;
    default:
      return std::nullopt;
  }
}

std::optional<NewLayerSettings> request_new_layer_settings(QWidget* parent, const QString& default_name,
                                                           bool clip_available, BitDepth depth,
                                                           const std::vector<NewLayerColorChoice>& colors,
                                                           QColor foreground, QColor background) {
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

  // Under Name in the field column, lined up with the fields (#410).
  auto* clip_check = new QCheckBox(NewLayerDialogStrings::tr("Clip to the layer below"), &dialog);
  clip_check->setObjectName(QStringLiteral("newLayerClipCheck"));
  clip_check->setEnabled(clip_available);
  clip_check->setToolTip(NewLayerDialogStrings::tr("Clip the new layer to the layer below it"));
  form->addRow(static_cast<QWidget*>(nullptr), clip_check);

  // The Layers panel row strip's colors, swatch and all.
  auto* color_combo = new QComboBox(&dialog);
  color_combo->setObjectName(QStringLiteral("newLayerColorCombo"));
  for (std::size_t index = 0; index < colors.size(); ++index) {
    color_combo->addItem(colors[index].swatch, colors[index].name, static_cast<int>(colors[index].label));
  }
  form->addRow(NewLayerDialogStrings::tr("Color:"), color_combo);

  auto* mode_combo = new QComboBox(&dialog);
  mode_combo->setObjectName(QStringLiteral("newLayerModeCombo"));
  add_blend_mode_items(mode_combo, BlendModeMenu::Layer);
  enable_blend_mode_items_for_depth(mode_combo, depth);
  mode_combo->setCurrentIndex(std::max(0, mode_combo->findData(static_cast<int>(BlendMode::Normal))));
  form->addRow(NewLayerDialogStrings::tr("Mode:"), mode_combo);

  // Fill (#410, #654): Edit > Fill's Contents list and swatch, Transparent first, with the
  // mode's neutral color listed last while the mode has one.
  auto* fill_row = new QWidget(&dialog);
  fill_row->setObjectName(QStringLiteral("newLayerFillRow"));
  auto* fill_layout = new QHBoxLayout(fill_row);
  fill_layout->setContentsMargins(0, 0, 0, 0);
  FillContentsFieldOptions fill_options;
  fill_options.choices = {FillContents::Transparent, FillContents::Foreground, FillContents::Background,
                          FillContents::Color,       FillContents::Black,      FillContents::Gray50,
                          FillContents::White,       FillContents::Neutral};
  fill_options.initial = FillContents::Transparent;
  fill_options.color = foreground;
  fill_options.foreground = foreground;
  fill_options.background = background;
  fill_options.combo_object_name = QStringLiteral("newLayerFillCombo");
  fill_options.swatch_object_name = QStringLiteral("newLayerFillColorSwatch");
  auto* fill = new FillContentsField(fill_row, fill_layout, fill_options);
  const auto refresh_neutral_fill = [mode_combo, fill] {
    const auto color = neutral_fill_for_blend_mode(static_cast<BlendMode>(mode_combo->currentData().toInt()));
    fill->set_neutral_color(color.has_value() ? std::optional<QColor>(neutral_fill_qcolor(*color)) : std::nullopt);
  };
  refresh_neutral_fill();
  QObject::connect(mode_combo, &QComboBox::currentIndexChanged, fill_row, refresh_neutral_fill);
  form->addRow(NewLayerDialogStrings::tr("Fill:"), fill_row);

  auto* opacity_spin = add_dialog_slider_spin_row(form, &dialog, NewLayerDialogStrings::tr("Opacity:"),
                                                  QStringLiteral("newLayerOpacitySlider"),
                                                  QStringLiteral("newLayerOpacitySpin"), 0, 100, 100,
                                                  QStringLiteral("%"));
  layout->addLayout(form);

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
  if (fill->contents() == FillContents::Neutral) {
    settings.neutral_fill = neutral_fill_for_blend_mode(settings.blend_mode);
  } else if (const auto color = fill->fill_color(); color.isValid()) {
    settings.fill_color = color;
  }
  return settings;
}

}  // namespace patchy::ui
