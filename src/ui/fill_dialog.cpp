// Edit > Fill... (GitHub issues 530, 575; docs/fill-command.md).
#include "ui/fill_dialog.hpp"

#include "ui/app_settings.hpp"
#include "ui/blend_mode_ui.hpp"
#include "ui/color_panel.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/script_api.hpp"
#include "ui/theme_qss.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <memory>

namespace patchy::ui {

namespace {

const QString kFillContentsKey = QStringLiteral("tools/editFillContents");
const QString kFillColorKey = QStringLiteral("tools/editFillColor");
const QString kFillModeKey = QStringLiteral("tools/editFillMode");
const QString kFillOpacityKey = QStringLiteral("tools/editFillOpacity");
const QString kFillPreserveTransparencyKey = QStringLiteral("tools/editFillPreserveTransparency");
const QString kFillContentAwareNewLayerKey = QStringLiteral("tools/editFillContentAwareNewLayer");
const char* const kFillSwatchColorProperty = "fillColor";

// The Contents list in order; a separator goes before Black and before Content-Aware (issue 575).
constexpr std::array<FillContents, 7> kFillContentsOrder{
    FillContents::Foreground, FillContents::Background, FillContents::Color,       FillContents::Black,
    FillContents::Gray50,     FillContents::White,      FillContents::ContentAware};

QString fill_contents_name(FillContents contents) {
  switch (contents) {
    case FillContents::Foreground:
      return QCoreApplication::translate("FillDialog", "Foreground Color");
    case FillContents::Background:
      return QCoreApplication::translate("FillDialog", "Background Color");
    case FillContents::Color:
      return QCoreApplication::translate("FillDialog", "Color...");
    case FillContents::Black:
      return QCoreApplication::translate("FillDialog", "Black");
    case FillContents::Gray50:
      return QCoreApplication::translate("FillDialog", "50% Gray");
    case FillContents::White:
      return QCoreApplication::translate("FillDialog", "White");
    case FillContents::ContentAware:
      return QCoreApplication::translate("FillDialog", "Content-Aware");
  }
  return {};
}

ThemedQss fill_swatch_style(QColor color) {
  return ThemedQss(QStringLiteral("QPushButton#editFillColorSwatch { background: rgb(%1, %2, %3); "
                                  "border: 1px solid @dlg_neutral_border; border-radius: 3px; padding: 0; "
                                  "min-width: 49px; max-width: 49px; min-height: 24px; max-height: 24px; } "
                                  "QPushButton#editFillColorSwatch:hover { "
                                  "border-color: @dlg_neutral_border_bright; }")
                       .arg(color.red())
                       .arg(color.green())
                       .arg(color.blue()));
}

}  // namespace

const char* fill_contents_token(FillContents contents) noexcept {
  switch (contents) {
    case FillContents::Foreground:
      return "foreground";
    case FillContents::Background:
      return "background";
    case FillContents::Color:
      return "color";
    case FillContents::Black:
      return "black";
    case FillContents::Gray50:
      return "gray50";
    case FillContents::White:
      return "white";
    case FillContents::ContentAware:
      return "contentAware";
  }
  return "foreground";
}

FillContents fill_contents_from_token(const QString& token, FillContents fallback) noexcept {
  for (const auto contents : kFillContentsOrder) {
    if (token == QLatin1String(fill_contents_token(contents))) {
      return contents;
    }
  }
  return fallback;
}

QColor fill_contents_color(const FillDialogSettings& settings, QColor foreground, QColor background) {
  switch (settings.contents) {
    case FillContents::Foreground:
      return foreground;
    case FillContents::Background:
      return background;
    case FillContents::Color:
      return settings.color.isValid() ? settings.color : foreground;
    case FillContents::Black:
      return QColor(0, 0, 0);
    case FillContents::Gray50:
      return QColor(128, 128, 128);
    case FillContents::White:
      return QColor(255, 255, 255);
    case FillContents::ContentAware:
      return foreground;
  }
  return foreground;
}

FillDialogSettings load_fill_dialog_settings() {
  FillDialogSettings loaded;
  auto settings = app_settings();
  loaded.contents = fill_contents_from_token(settings.value(kFillContentsKey).toString(), loaded.contents);
  const QColor color(settings.value(kFillColorKey).toString());
  if (color.isValid()) {
    loaded.color = color;
  }
  PaintMode mode;
  if (script_paint_mode_from_id(settings.value(kFillModeKey).toString(), &mode)) {
    loaded.mode = mode;
  }
  loaded.opacity = std::clamp(settings.value(kFillOpacityKey, loaded.opacity).toInt(), 1, 100);
  loaded.preserve_transparency = settings.value(kFillPreserveTransparencyKey, false).toBool();
  loaded.content_aware_new_layer = settings.value(kFillContentAwareNewLayerKey, false).toBool();
  return loaded;
}

void save_fill_dialog_settings(const FillDialogSettings& chosen) {
  auto settings = app_settings();
  settings.setValue(kFillContentsKey, QString::fromLatin1(fill_contents_token(chosen.contents)));
  settings.setValue(kFillColorKey, chosen.color.name(QColor::HexRgb));
  settings.setValue(kFillModeKey, script_paint_mode_id(chosen.mode));
  settings.setValue(kFillOpacityKey, chosen.opacity);
  settings.setValue(kFillPreserveTransparencyKey, chosen.preserve_transparency);
  settings.setValue(kFillContentAwareNewLayerKey, chosen.content_aware_new_layer);
}

std::optional<FillDialogSettings> request_fill_settings(QWidget* parent, QColor foreground, QColor background,
                                                        bool content_aware_available) {
  const auto remembered = load_fill_dialog_settings();

  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("patchyFillDialog"));
  dialog.setWindowTitle(QCoreApplication::translate("FillDialog", "Fill"));
  auto* layout = new QVBoxLayout(&dialog);

  // Both boxes are forms with one label column width, so every field starts at one x and
  // the checkboxes sit in the field column (issue 575's follow-up).
  auto* contents_box = new QGroupBox(QCoreApplication::translate("FillDialog", "Fill"), &dialog);
  auto* contents_form = new QFormLayout(contents_box);
  contents_form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  auto* contents_row = new QHBoxLayout();
  auto* contents_combo = new QComboBox(contents_box);
  contents_combo->setObjectName(QStringLiteral("editFillContentsCombo"));
  for (const auto contents : kFillContentsOrder) {
    if (contents == FillContents::Black || contents == FillContents::ContentAware) {
      contents_combo->insertSeparator(contents_combo->count());
    }
    contents_combo->addItem(fill_contents_name(contents), QString::fromLatin1(fill_contents_token(contents)));
  }
  const auto content_aware_index =
      contents_combo->findData(QString::fromLatin1(fill_contents_token(FillContents::ContentAware)));
  if (!content_aware_available) {
    if (auto* model = qobject_cast<QStandardItemModel*>(contents_combo->model()); model != nullptr) {
      if (auto* item = model->item(content_aware_index); item != nullptr) {
        item->setFlags(item->flags() & ~(Qt::ItemIsEnabled | Qt::ItemIsSelectable));
      }
    }
    contents_combo->setItemData(
        content_aware_index,
        QCoreApplication::translate("FillDialog", "Make a selection to fill it from its surroundings"),
        Qt::ToolTipRole);
  }
  const auto initial_contents =
      remembered.contents == FillContents::ContentAware && !content_aware_available ? FillContents::Foreground
                                                                                    : remembered.contents;
  contents_combo->setCurrentIndex(
      std::max(0, contents_combo->findData(QString::fromLatin1(fill_contents_token(initial_contents)))));
  // Wide enough for the longest entry in the current language (issue 575 cropped
  // "Foreground Color" and "Background Color").
  contents_combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
  contents_combo->ensurePolished();  // the style sheet's padding counts toward the width
  contents_combo->setMinimumWidth(contents_combo->sizeHint().width());
  contents_row->addWidget(contents_combo, 1);

  // The swatch shows the color the fill will use. Its property holds the Color... choice and
  // is that choice's source of truth, so automation can set it without driving the picker.
  auto* color_swatch = new QPushButton(contents_box);
  color_swatch->setObjectName(QStringLiteral("editFillColorSwatch"));
  color_swatch->setAccessibleName(QCoreApplication::translate("FillDialog", "Fill color"));
  color_swatch->setToolTip(QCoreApplication::translate("FillDialog", "Choose a color to fill with"));
  color_swatch->setCursor(Qt::PointingHandCursor);
  color_swatch->setFocusPolicy(Qt::StrongFocus);
  color_swatch->setFixedSize(49, 24);
  color_swatch->setProperty(kFillSwatchColorProperty, remembered.color);
  // Content-Aware has no color: the swatch hides but keeps its room so nothing shifts.
  auto swatch_policy = color_swatch->sizePolicy();
  swatch_policy.setRetainSizeWhenHidden(true);
  color_swatch->setSizePolicy(swatch_policy);
  contents_row->addWidget(color_swatch);
  contents_form->addRow(QCoreApplication::translate("FillDialog", "Contents:"), contents_row);
  auto* new_layer_check = new QCheckBox(QCoreApplication::translate("FillDialog", "Add to New Layer"), contents_box);
  new_layer_check->setObjectName(QStringLiteral("editFillContentAwareNewLayerCheck"));
  new_layer_check->setToolTip(QCoreApplication::translate(
      "FillDialog", "Put the content-aware fill on a new layer above this one, transparent outside the selection"));
  new_layer_check->setChecked(remembered.content_aware_new_layer);
  contents_form->addRow(QString(), new_layer_check);
  layout->addWidget(contents_box);

  const auto current_contents = [contents_combo] {
    return fill_contents_from_token(contents_combo->currentData().toString(), FillContents::Foreground);
  };
  const auto update_swatch = [color_swatch, current_contents, foreground, background] {
    FillDialogSettings shown;
    shown.contents = current_contents();
    shown.color = color_swatch->property(kFillSwatchColorProperty).value<QColor>();
    set_themed_style(*color_swatch, fill_swatch_style(fill_contents_color(shown, foreground, background)));
  };
  update_swatch();
  // Color... (from the list or a swatch click) opens the picker at once, starting from the color
  // the swatch showed under the previous choice; Cancel goes back to that choice and color.
  auto previous_index = std::make_shared<int>(contents_combo->currentIndex());
  const auto color_index = contents_combo->findData(QString::fromLatin1(fill_contents_token(FillContents::Color)));
  const auto pick_color = [&dialog, contents_combo, color_swatch, update_swatch, previous_index, color_index,
                           foreground, background] {
    const auto original = color_swatch->property(kFillSwatchColorProperty).value<QColor>();
    FillDialogSettings before;
    before.contents = fill_contents_from_token(contents_combo->itemData(*previous_index).toString(),
                                               FillContents::Foreground);
    before.color = original;
    const auto initial = fill_contents_color(before, foreground, background);
    color_swatch->setProperty(kFillSwatchColorProperty, initial);
    contents_combo->setCurrentIndex(color_index);
    const auto selected = request_patchy_color(&dialog, initial, QCoreApplication::translate("FillDialog", "Fill Color"),
                                               [color_swatch, update_swatch](QColor color) {
                                                 color_swatch->setProperty(kFillSwatchColorProperty, color);
                                                 update_swatch();
                                               });
    if (selected.has_value()) {
      color_swatch->setProperty(kFillSwatchColorProperty, *selected);
      *previous_index = color_index;
    } else {
      color_swatch->setProperty(kFillSwatchColorProperty, original);
      contents_combo->setCurrentIndex(*previous_index);
    }
    update_swatch();
  };
  QObject::connect(contents_combo, &QComboBox::activated, &dialog,
                   [previous_index, color_index, pick_color](int index) {
                     if (index == color_index) {
                       pick_color();
                     } else {
                       *previous_index = index;
                     }
                   });
  QObject::connect(color_swatch, &QPushButton::clicked, &dialog, pick_color);

  auto* blending_box = new QGroupBox(QCoreApplication::translate("FillDialog", "Blending"), &dialog);
  auto* form = new QFormLayout(blending_box);
  form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  auto* mode_combo = new QComboBox(blending_box);
  mode_combo->setObjectName(QStringLiteral("editFillModeCombo"));
  add_paint_mode_items(mode_combo);
  // Behind and Clear change only alpha: a separator sets them apart from Normal and Dissolve
  // here (the Brush bar's shared list keeps its own layout).
  const auto behind_data = paint_mode_item_data(PaintMode::behind());
  const auto clear_data = paint_mode_item_data(PaintMode::clear());
  const auto behind_index = mode_combo->findData(behind_data);
  if (behind_index > 0 && !is_blend_mode_separator(mode_combo, behind_index - 1)) {
    mode_combo->insertSeparator(behind_index);
  }
  mode_combo->setCurrentIndex(std::max(0, mode_combo->findData(paint_mode_item_data(remembered.mode))));
  form->addRow(QCoreApplication::translate("FillDialog", "Mode:"), mode_combo);
  auto* opacity_spin = add_dialog_slider_spin_row(
      form, blending_box, QCoreApplication::translate("FillDialog", "Opacity:"),
      QStringLiteral("editFillOpacitySlider"), QStringLiteral("editFillOpacitySpin"), 1, 100, remembered.opacity,
      QCoreApplication::translate("FillDialog", " %"));
  auto* opacity_row = opacity_spin->parentWidget();
  auto* preserve_check =
      new QCheckBox(QCoreApplication::translate("FillDialog", "Preserve Transparency"), blending_box);
  preserve_check->setObjectName(QStringLiteral("editFillPreserveTransparencyCheck"));
  preserve_check->setToolTip(
      QCoreApplication::translate("FillDialog", "Fill only where the layer already has pixels, keeping their opacity"));
  preserve_check->setChecked(remembered.preserve_transparency);
  form->addRow(QString(), preserve_check);
  layout->addWidget(blending_box);
  align_form_label_columns({contents_form, form});

  // Content-Aware fills from the surroundings, so Preserve Transparency never applies to it,
  // and Mode and Opacity apply only with Add to New Layer, as the new layer's blend mode and
  // opacity. Behind and Clear are not layer blend modes, so they are off then.
  const auto set_enabled_with_label = [form](QWidget* field, bool enabled) {
    field->setEnabled(enabled);
    if (auto* label = form->labelForField(field); label != nullptr) {
      label->setEnabled(enabled);
    }
  };
  const auto update_content_aware = [current_contents, color_swatch, new_layer_check, blending_box, mode_combo,
                                     opacity_row, preserve_check, set_enabled_with_label, behind_data,
                                     clear_data] {
    const bool content_aware = current_contents() == FillContents::ContentAware;
    const bool layer_modes_only = content_aware && new_layer_check->isChecked();
    color_swatch->setVisible(!content_aware);
    new_layer_check->setEnabled(content_aware);
    set_enabled_with_label(mode_combo, !content_aware || layer_modes_only);
    set_enabled_with_label(opacity_row, !content_aware || layer_modes_only);
    preserve_check->setEnabled(!content_aware);
    if (auto* model = qobject_cast<QStandardItemModel*>(mode_combo->model()); model != nullptr) {
      for (const auto data : {behind_data, clear_data}) {
        if (auto* item = model->item(mode_combo->findData(data)); item != nullptr) {
          item->setFlags(layer_modes_only ? item->flags() & ~(Qt::ItemIsEnabled | Qt::ItemIsSelectable)
                                          : item->flags() | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        }
      }
    }
    if (layer_modes_only && paint_mode_from_item_data(mode_combo->currentData().toInt()).kind != PaintModeKind::Blend) {
      mode_combo->setCurrentIndex(std::max(0, mode_combo->findData(paint_mode_item_data(PaintMode{}))));
    }
    blending_box->setToolTip(
        !content_aware     ? QString()
        : layer_modes_only ? QCoreApplication::translate("FillDialog",
                                                         "Mode and Opacity set the new layer's blend mode and opacity")
                           : QCoreApplication::translate("FillDialog",
                                                         "Content-Aware fills from the pixels around the selection"));
  };
  update_content_aware();
  QObject::connect(contents_combo, &QComboBox::currentIndexChanged, &dialog,
                   [update_swatch, update_content_aware](int) {
                     update_swatch();
                     update_content_aware();
                   });
  QObject::connect(new_layer_check, &QCheckBox::toggled, &dialog,
                   [update_content_aware](bool) { update_content_aware(); });

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  // OK as wide as Cancel (#575 follow-up 2), whatever the language makes either.
  equalize_dialog_button_widths(buttons);
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  contents_combo->setFocus();
  if (exec_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }

  FillDialogSettings chosen;
  chosen.contents = current_contents();
  chosen.color = color_swatch->property(kFillSwatchColorProperty).value<QColor>();
  if (!chosen.color.isValid()) {
    chosen.color = remembered.color;
  }
  chosen.mode = paint_mode_from_item_data(mode_combo->currentData().toInt());
  chosen.opacity = opacity_spin->value();
  chosen.preserve_transparency = preserve_check->isChecked();
  chosen.content_aware_new_layer = new_layer_check->isChecked();
  save_fill_dialog_settings(chosen);
  return chosen;
}

}  // namespace patchy::ui
