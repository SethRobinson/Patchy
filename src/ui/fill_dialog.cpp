// Edit > Fill... (GitHub issues 530, 575, 655; docs/fill-command.md) and the Contents list it
// shares with Layer > New > New Layer (GitHub issue 654).
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
    case FillContents::Transparent:
      return QCoreApplication::translate("FillDialog", "Transparent");
    case FillContents::Neutral:
      return QCoreApplication::translate("FillDialog", "Neutral Color for This Mode");
  }
  return {};
}

// Every Contents value, for reading a list entry's token back (fill_contents_from_token reads
// only Edit > Fill's).
constexpr std::array<FillContents, 9> kAllFillContents{
    FillContents::Foreground,   FillContents::Background,  FillContents::Color,
    FillContents::Black,        FillContents::Gray50,      FillContents::White,
    FillContents::ContentAware, FillContents::Transparent, FillContents::Neutral};

FillContents any_fill_contents_from_token(const QString& token, FillContents fallback) {
  for (const auto contents : kAllFillContents) {
    if (token == QLatin1String(fill_contents_token(contents))) {
      return contents;
    }
  }
  return fallback;
}

QString fill_contents_data(FillContents contents) { return QString::fromLatin1(fill_contents_token(contents)); }

// Keyed by the swatch's own object name: Edit > Fill's and New Layer's differ.
ThemedQss fill_swatch_style(const QString& object_name, QColor color) {
  return ThemedQss(QStringLiteral("QPushButton#%4 { background: rgb(%1, %2, %3); "
                                  "border: 1px solid @dlg_neutral_border; border-radius: 3px; padding: 0; "
                                  "min-width: 49px; max-width: 49px; min-height: 24px; max-height: 24px; } "
                                  "QPushButton#%4:hover { "
                                  "border-color: @dlg_neutral_border_bright; }")
                       .arg(color.red())
                       .arg(color.green())
                       .arg(color.blue())
                       .arg(object_name));
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
    case FillContents::Transparent:
      return "transparent";
    case FillContents::Neutral:
      return "neutral";
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
    case FillContents::Transparent:
    case FillContents::Neutral:
      return foreground;
  }
  return foreground;
}

FillContentsField::FillContentsField(QWidget* parent, QHBoxLayout* row, const FillContentsFieldOptions& options)
    : QObject(nullptr), foreground_(options.foreground), background_(options.background) {
  combo_ = new QComboBox(parent);
  combo_->setObjectName(options.combo_object_name);
  setParent(combo_);
  for (std::size_t at = 0; at < options.choices.size(); ++at) {
    const auto contents = options.choices[at];
    if (contents == FillContents::Neutral) {
      has_neutral_choice_ = true;
      continue;  // listed while set_neutral_color gives it a color
    }
    if (combo_->count() > 0 &&
        (options.choices[at - 1] == FillContents::Transparent || contents == FillContents::Black ||
         contents == FillContents::ContentAware)) {
      combo_->insertSeparator(combo_->count());
    }
    combo_->addItem(fill_contents_name(contents), fill_contents_data(contents));
  }
  combo_->setCurrentIndex(std::max(0, index_of(options.initial)));
  previous_index_ = combo_->currentIndex();
  // Wide enough for the longest entry in the current language, closed and open (issues 575, 655).
  fit_combo_to_entries(*combo_, has_neutral_choice_ ? QStringList{fill_contents_name(FillContents::Neutral)}
                                                    : QStringList{});
  row->addWidget(combo_, 1);

  swatch_ = new QPushButton(parent);
  swatch_->setObjectName(options.swatch_object_name);
  swatch_->setAccessibleName(QCoreApplication::translate("FillDialog", "Fill color"));
  swatch_->setToolTip(QCoreApplication::translate("FillDialog", "Choose a color to fill with"));
  swatch_->setCursor(Qt::PointingHandCursor);
  swatch_->setFocusPolicy(Qt::StrongFocus);
  swatch_->setFixedSize(49, 24);
  swatch_->setProperty(kFillSwatchColorProperty, options.color);
  // A choice without a color hides the swatch but keeps its room, so nothing shifts.
  auto swatch_policy = swatch_->sizePolicy();
  swatch_policy.setRetainSizeWhenHidden(true);
  swatch_->setSizePolicy(swatch_policy);
  row->addWidget(swatch_);
  refresh_swatch();

  QObject::connect(combo_, &QComboBox::activated, this, [this](int index) {
    if (index == index_of(FillContents::Color)) {
      pick_color();
    } else {
      previous_index_ = index;
    }
  });
  QObject::connect(combo_, &QComboBox::currentIndexChanged, this, [this](int) { refresh_swatch(); });
  QObject::connect(swatch_, &QPushButton::clicked, this, [this] { pick_color(); });
}

int FillContentsField::index_of(FillContents contents) const { return combo_->findData(fill_contents_data(contents)); }

FillContents FillContentsField::contents() const {
  return any_fill_contents_from_token(combo_->currentData().toString(), FillContents::Foreground);
}

void FillContentsField::set_contents(FillContents contents) {
  if (const int index = index_of(contents); index >= 0) {
    combo_->setCurrentIndex(index);
    previous_index_ = index;
  }
}

QColor FillContentsField::chosen_color() const { return swatch_->property(kFillSwatchColorProperty).value<QColor>(); }

QColor FillContentsField::fill_color() const {
  switch (contents()) {
    case FillContents::Transparent:
    case FillContents::ContentAware:
      return {};
    case FillContents::Neutral:
      return neutral_.value_or(QColor());
    default:
      break;
  }
  FillDialogSettings shown;
  shown.contents = contents();
  shown.color = chosen_color();
  return fill_contents_color(shown, foreground_, background_);
}

void FillContentsField::set_choice_enabled(FillContents contents, bool enabled, const QString& disabled_tooltip) {
  const int index = index_of(contents);
  auto* model = qobject_cast<QStandardItemModel*>(combo_->model());
  auto* item = model != nullptr && index >= 0 ? model->item(index) : nullptr;
  if (item == nullptr) {
    return;
  }
  item->setFlags(enabled ? item->flags() | Qt::ItemIsEnabled | Qt::ItemIsSelectable
                         : item->flags() & ~(Qt::ItemIsEnabled | Qt::ItemIsSelectable));
  combo_->setItemData(index, enabled ? QVariant() : QVariant(disabled_tooltip), Qt::ToolTipRole);
}

void FillContentsField::set_neutral_color(std::optional<QColor> color) {
  if (!has_neutral_choice_) {
    return;
  }
  neutral_ = color;
  const int index = index_of(FillContents::Neutral);
  if (color.has_value() && index < 0) {
    combo_->insertSeparator(combo_->count());
    combo_->addItem(fill_contents_name(FillContents::Neutral), fill_contents_data(FillContents::Neutral));
    combo_->setItemData(combo_->count() - 1,
                        QCoreApplication::translate(
                            "FillDialog", "Fill with the color that leaves the image unchanged in this blend mode"),
                        Qt::ToolTipRole);
  } else if (!color.has_value() && index >= 0) {
    if (combo_->currentIndex() == index) {
      combo_->setCurrentIndex(0);
    }
    if (previous_index_ >= index - 1) {
      previous_index_ = 0;
    }
    combo_->removeItem(index);
    combo_->removeItem(index - 1);  // its separator
  }
  refresh_swatch();
}

void FillContentsField::refresh_swatch() {
  const auto color = fill_color();
  swatch_->setVisible(color.isValid());
  if (color.isValid()) {
    set_themed_style(*swatch_, fill_swatch_style(swatch_->objectName(), color));
  }
}

// From the color the swatch showed under the previous choice; Cancel goes back to that choice
// and color.
void FillContentsField::pick_color() {
  const int color_index = index_of(FillContents::Color);
  if (color_index < 0) {
    return;
  }
  const auto original = chosen_color();
  const auto before =
      any_fill_contents_from_token(combo_->itemData(previous_index_).toString(), FillContents::Foreground);
  QColor initial;
  if (before == FillContents::Neutral) {
    initial = neutral_.value_or(QColor());
  } else if (before != FillContents::Transparent && before != FillContents::ContentAware) {
    FillDialogSettings shown;
    shown.contents = before;
    shown.color = original;
    initial = fill_contents_color(shown, foreground_, background_);
  }
  if (!initial.isValid()) {
    initial = original.isValid() ? original : foreground_;
  }
  swatch_->setProperty(kFillSwatchColorProperty, initial);
  combo_->setCurrentIndex(color_index);
  refresh_swatch();
  const auto selected = request_patchy_color(combo_->window(), initial,
                                             QCoreApplication::translate("FillDialog", "Fill Color"),
                                             [this](QColor color) {
                                               swatch_->setProperty(kFillSwatchColorProperty, color);
                                               refresh_swatch();
                                             });
  if (selected.has_value()) {
    swatch_->setProperty(kFillSwatchColorProperty, *selected);
    previous_index_ = color_index;
  } else {
    swatch_->setProperty(kFillSwatchColorProperty, original);
    combo_->setCurrentIndex(previous_index_);
  }
  refresh_swatch();
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
  const auto initial_contents =
      remembered.contents == FillContents::ContentAware && !content_aware_available ? FillContents::Foreground
                                                                                    : remembered.contents;
  FillContentsFieldOptions field_options;
  field_options.choices.assign(kFillContentsOrder.begin(), kFillContentsOrder.end());
  field_options.initial = initial_contents;
  field_options.color = remembered.color;
  field_options.foreground = foreground;
  field_options.background = background;
  field_options.combo_object_name = QStringLiteral("editFillContentsCombo");
  field_options.swatch_object_name = QStringLiteral("editFillColorSwatch");
  auto* field = new FillContentsField(contents_box, contents_row, field_options);
  auto* contents_combo = field->combo();
  if (!content_aware_available) {
    field->set_choice_enabled(
        FillContents::ContentAware, false,
        QCoreApplication::translate("FillDialog", "Make a selection to fill it from its surroundings"));
  }
  contents_form->addRow(QCoreApplication::translate("FillDialog", "Contents:"), contents_row);
  auto* new_layer_check = new QCheckBox(QCoreApplication::translate("FillDialog", "Add to New Layer"), contents_box);
  new_layer_check->setObjectName(QStringLiteral("editFillContentAwareNewLayerCheck"));
  new_layer_check->setToolTip(QCoreApplication::translate(
      "FillDialog", "Put the content-aware fill on a new layer above this one, transparent outside the selection"));
  new_layer_check->setChecked(remembered.content_aware_new_layer);
  contents_form->addRow(QString(), new_layer_check);
  layout->addWidget(contents_box);

  const auto current_contents = [field] { return field->contents(); };

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
  const auto update_content_aware = [current_contents, new_layer_check, blending_box, mode_combo,
                                     opacity_row, preserve_check, set_enabled_with_label, behind_data,
                                     clear_data] {
    const bool content_aware = current_contents() == FillContents::ContentAware;
    const bool layer_modes_only = content_aware && new_layer_check->isChecked();
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
                   [update_content_aware](int) { update_content_aware(); });
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
  chosen.color = field->chosen_color();
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
