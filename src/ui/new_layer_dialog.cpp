#include "ui/new_layer_dialog.hpp"

#include "ui/blend_mode_ui.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/theme_palette.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPixmap>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace patchy::ui {
namespace {

class NewLayerDialogStrings {
  Q_DECLARE_TR_FUNCTIONS(NewLayerDialog)
};

QString neutral_fill_name(NeutralFillColor color) {
  switch (color) {
    case NeutralFillColor::White:
      return NewLayerDialogStrings::tr("White");
    case NeutralFillColor::Black:
      return NewLayerDialogStrings::tr("Black");
    case NeutralFillColor::Gray:
      return NewLayerDialogStrings::tr("50% gray");
  }
  return {};
}

// The neutral color itself (document content, not chrome), framed so white and black both
// read against either scheme. Wider than tall, so it reads as a color chip, not a checkbox.
QPixmap neutral_fill_swatch(NeutralFillColor color, qreal device_pixel_ratio) {
  constexpr int kWidth = 24;
  constexpr int kHeight = 14;
  const int value = color == NeutralFillColor::White ? 255 : color == NeutralFillColor::Black ? 0 : 128;
  QPixmap pixmap(QSize(kWidth, kHeight) * device_pixel_ratio);
  pixmap.setDevicePixelRatio(device_pixel_ratio);
  pixmap.fill(QColor(value, value, value));
  QPainter painter(&pixmap);
  painter.setPen(QPen(theme().text_secondary, 0));
  painter.drawRect(QRectF(0.5, 0.5, kWidth - 1.0, kHeight - 1.0));
  return pixmap;
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
    color_combo->addItem(colors[index].swatch, colors[index].name, static_cast<int>(index));
  }
  form->addRow(NewLayerDialogStrings::tr("Color:"), color_combo);

  auto* mode_combo = new QComboBox(&dialog);
  mode_combo->setObjectName(QStringLiteral("newLayerModeCombo"));
  add_blend_mode_items(mode_combo, BlendModeMenu::Layer);
  enable_blend_mode_items_for_depth(mode_combo, depth);
  mode_combo->setCurrentIndex(std::max(0, mode_combo->findData(static_cast<int>(BlendMode::Normal))));
  form->addRow(NewLayerDialogStrings::tr("Mode:"), mode_combo);

  // Fill: the mode's neutral color, shown as a swatch and its name (#410). A mode without one
  // says so in plain text rather than a dimmed, easily missed control.
  auto* fill_row = new QWidget(&dialog);
  fill_row->setObjectName(QStringLiteral("newLayerNeutralFillRow"));
  auto* fill_layout = new QHBoxLayout(fill_row);
  fill_layout->setContentsMargins(0, 0, 0, 0);
  fill_layout->setSpacing(6);
  auto* neutral_check = new QCheckBox(NewLayerDialogStrings::tr("Neutral color"), fill_row);
  neutral_check->setObjectName(QStringLiteral("newLayerNeutralFillCheck"));
  neutral_check->setToolTip(NewLayerDialogStrings::tr("Fill the new layer with the color that leaves the image unchanged in this mode"));
  auto* neutral_swatch = new QLabel(fill_row);
  neutral_swatch->setObjectName(QStringLiteral("newLayerNeutralFillSwatch"));
  auto* neutral_name = new QLabel(fill_row);
  neutral_name->setObjectName(QStringLiteral("newLayerNeutralFillName"));
  auto* neutral_note = new QLabel(NewLayerDialogStrings::tr("This mode has no neutral color"), fill_row);
  neutral_note->setObjectName(QStringLiteral("newLayerNeutralFillNote"));
  fill_layout->addWidget(neutral_check);
  fill_layout->addWidget(neutral_swatch);
  fill_layout->addWidget(neutral_name);
  fill_layout->addWidget(neutral_note);
  fill_layout->addStretch(1);
  fill_row->setMinimumHeight(neutral_check->sizeHint().height());
  const auto refresh_neutral_fill = [mode_combo, neutral_check, neutral_swatch, neutral_name, neutral_note] {
    const auto color = neutral_fill_for_blend_mode(static_cast<BlendMode>(mode_combo->currentData().toInt()));
    neutral_check->setEnabled(color.has_value());
    neutral_check->setVisible(color.has_value());
    neutral_swatch->setVisible(color.has_value());
    neutral_name->setVisible(color.has_value());
    neutral_note->setVisible(!color.has_value());
    if (color.has_value()) {
      neutral_swatch->setPixmap(neutral_fill_swatch(*color, neutral_swatch->devicePixelRatioF()));
      neutral_name->setText(neutral_fill_name(*color));
    }
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
  if (neutral_check->isEnabled() && neutral_check->isChecked()) {
    settings.neutral_fill = neutral_fill_for_blend_mode(settings.blend_mode);
  }
  return settings;
}

}  // namespace patchy::ui
