#include "ui/recognize_text_dialog.hpp"

#include "ui/app_settings.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/ocr_controls.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

#include <algorithm>

namespace patchy::ui {

namespace {

const QString kResultKey = QStringLiteral("ocr/result");
const QString kSourceKey = QStringLiteral("ocr/source");
const QString kGroupingKey = QStringLiteral("ocr/grouping");
const QString kMatchStyleKey = QStringLiteral("ocr/matchStyle");
const QString kFontFamilyKey = QStringLiteral("ocr/fontFamily");
// What Import PDF's recognition makes: a layer mode token, never "none".
const QString kPdfImportResultKey = QStringLiteral("imports/pdfOcrResult");
// ocr/result holds a layer mode token or this one.
const QString kClipboardResult = QStringLiteral("clipboard");

// The nearest source that applies: a selection that is gone reads the layer, a missing
// layer reads the image.
OcrSource fitted_source(OcrSource wanted, const RecognizeTextDialogContext& context) {
  if (wanted == OcrSource::Selection && !context.has_selection) {
    wanted = OcrSource::Layer;
  }
  if (wanted == OcrSource::Layer && !context.has_layer) {
    wanted = OcrSource::Image;
  }
  return wanted;
}

// The layer options every recognition shares, whatever asked for it.
void read_shared_layer_options(const QSettings& settings, RecognizeTextChoice& choice) {
  choice.grouping = ocr_grouping_from_token(settings.value(kGroupingKey).toString()).value_or(OcrGrouping::Blocks);
  choice.match_style = settings.value(kMatchStyleKey, true).toBool();
  choice.font_family = settings.value(kFontFamilyKey).toString();
}

}  // namespace

QString ocr_source_token(OcrSource source) {
  switch (source) {
    case OcrSource::Selection:
      return QStringLiteral("selection");
    case OcrSource::Image:
      return QStringLiteral("image");
    case OcrSource::Layer:
      break;
  }
  return QStringLiteral("layer");
}

std::optional<OcrSource> ocr_source_from_token(const QString& token) {
  if (token == QLatin1String("layer")) {
    return OcrSource::Layer;
  }
  if (token == QLatin1String("selection")) {
    return OcrSource::Selection;
  }
  if (token == QLatin1String("image")) {
    return OcrSource::Image;
  }
  return std::nullopt;
}

RecognizeTextChoice stored_recognize_text_choice(const RecognizeTextDialogContext& context) {
  const auto settings = app_settings();
  RecognizeTextChoice choice;
  const auto result = settings.value(kResultKey, QStringLiteral("visible")).toString();
  if (result == kClipboardResult) {
    choice.mode = OcrLayerMode::None;
    choice.copy_text = true;
  } else {
    const auto mode = ocr_layer_mode_from_token(result);
    choice.mode = mode.has_value() && *mode != OcrLayerMode::None ? *mode : OcrLayerMode::Visible;
  }
  // A selection is what the user just pointed at: it is the default source while one exists.
  const auto stored_source = ocr_source_from_token(settings.value(kSourceKey).toString());
  choice.source =
      fitted_source(context.has_selection ? OcrSource::Selection : stored_source.value_or(OcrSource::Layer), context);
  read_shared_layer_options(settings, choice);
  return choice;
}

RecognizeTextChoice stored_pdf_import_ocr_choice() {
  const auto settings = app_settings();
  RecognizeTextChoice choice;
  const auto mode = ocr_layer_mode_from_token(settings.value(kPdfImportResultKey).toString());
  // Hidden unless the user chose otherwise: an import that changes how the pages look
  // would be a surprise, and a hidden group keeps a scan's original image data for export.
  choice.mode = mode.has_value() && *mode != OcrLayerMode::None ? *mode : OcrLayerMode::Hidden;
  choice.source = OcrSource::Image;
  read_shared_layer_options(settings, choice);
  return choice;
}

QString pdf_import_ocr_result_label(OcrLayerMode mode) {
  switch (mode) {
    case OcrLayerMode::Visible:
      return QObject::tr("Text layers over the scan");
    case OcrLayerMode::Replace:
      return QObject::tr("Text layers, scan hidden");
    case OcrLayerMode::Hidden:
    case OcrLayerMode::None:
      break;
  }
  return QObject::tr("Hidden text layers, page unchanged");
}

std::optional<RecognizeTextChoice> request_recognize_text(QWidget* parent,
                                                          const RecognizeTextDialogContext& context) {
  const bool for_import = context.purpose == RecognizeTextPurpose::PdfImport;
  const auto stored = for_import ? stored_pdf_import_ocr_choice() : stored_recognize_text_choice(context);
  QDialog dialog(parent);
  dialog.setObjectName(for_import ? QStringLiteral("pdfImportOcrOptionsDialog") : QStringLiteral("recognizeTextDialog"));
  dialog.setWindowTitle(for_import ? QObject::tr("Text Recognition Options") : QObject::tr("Recognize Text"));
  auto* layout = new QVBoxLayout(&dialog);

  auto* picker = new OcrEnginePicker(QStringLiteral("recognizeText"), &dialog);
  layout->addWidget(picker);

  auto* form = new QFormLayout();
  QComboBox* source = nullptr;
  if (!for_import) {
    source = new QComboBox(&dialog);
    source->setObjectName(QStringLiteral("recognizeTextSourceCombo"));
    if (context.has_layer) {
      source->addItem(QObject::tr("Active layer"), ocr_source_token(OcrSource::Layer));
    }
    if (context.has_selection) {
      source->addItem(QObject::tr("Selection"), ocr_source_token(OcrSource::Selection));
    }
    source->addItem(QObject::tr("Whole image"), ocr_source_token(OcrSource::Image));
    source->setCurrentIndex(std::max(0, source->findData(ocr_source_token(stored.source))));
    form->addRow(QObject::tr("Read:"), source);
  }

  auto* result = new QComboBox(&dialog);
  result->setObjectName(QStringLiteral("recognizeTextResultCombo"));
  if (for_import) {
    for (const auto mode : {OcrLayerMode::Hidden, OcrLayerMode::Visible, OcrLayerMode::Replace}) {
      result->addItem(pdf_import_ocr_result_label(mode), ocr_layer_mode_token(mode));
    }
  } else {
    result->addItem(QObject::tr("Text layers over the image"), ocr_layer_mode_token(OcrLayerMode::Visible));
    result->addItem(QObject::tr("Show text and hide original"), ocr_layer_mode_token(OcrLayerMode::Replace));
    result->addItem(QObject::tr("Hidden text layers"), ocr_layer_mode_token(OcrLayerMode::Hidden));
    result->addItem(QObject::tr("Copy the text to the clipboard"), kClipboardResult);
  }
  result->setCurrentIndex(
      std::max(0, result->findData(stored.copy_text ? kClipboardResult : ocr_layer_mode_token(stored.mode))));
  form->addRow(for_import ? QObject::tr("Recognized text:") : QObject::tr("Result:"), result);

  auto* grouping = new QComboBox(&dialog);
  grouping->setObjectName(QStringLiteral("recognizeTextGroupingCombo"));
  grouping->addItem(QObject::tr("One per block of text"), ocr_grouping_token(OcrGrouping::Blocks));
  grouping->addItem(QObject::tr("One per line"), ocr_grouping_token(OcrGrouping::Lines));
  grouping->setCurrentIndex(std::max(0, grouping->findData(ocr_grouping_token(stored.grouping))));
  form->addRow(QObject::tr("Text layers:"), grouping);

  auto* font = new QFontComboBox(&dialog);
  font->setObjectName(QStringLiteral("recognizeTextFontCombo"));
  const auto start_family = stored.font_family.isEmpty() ? context.type_tool_family : stored.font_family;
  if (!start_family.isEmpty()) {
    font->setCurrentFont(QFont(start_family));
  }
  // What the row shows untouched: leaving it there is "no choice of my own".
  const auto untouched_family = font->currentFont().family();
  form->addRow(QObject::tr("Font:"), font);

  auto* match_style = new QCheckBox(QObject::tr("Match size, color and slant from the image"), &dialog);
  match_style->setObjectName(QStringLiteral("recognizeTextMatchStyleCheck"));
  match_style->setChecked(stored.match_style);
  form->addRow(match_style);
  layout->addLayout(form);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  auto* ok = buttons->button(QDialogButtonBox::Ok);
  if (!for_import) {
    ok->setText(QObject::tr("Recognize"));
  }
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  // The layer options only matter when layers are made; nothing runs without an engine.
  const auto sync = [=] {
    const bool layers = result->currentData().toString() != kClipboardResult;
    grouping->setEnabled(layers);
    font->setEnabled(layers);
    match_style->setEnabled(layers);
    ok->setEnabled(picker->usable());
  };
  picker->on_changed(sync);
  QObject::connect(result, &QComboBox::currentIndexChanged, &dialog, sync);
  sync();

  if (exec_dialog(dialog) != QDialog::Accepted || !picker->usable()) {
    return std::nullopt;
  }
  RecognizeTextChoice choice;
  choice.engine = picker->engine_id();
  choice.language = picker->language_tag();
  choice.source = source != nullptr ? ocr_source_from_token(source->currentData().toString()).value_or(OcrSource::Image)
                                    : OcrSource::Image;
  const auto result_token = result->currentData().toString();
  choice.copy_text = result_token == kClipboardResult;
  choice.mode = choice.copy_text ? OcrLayerMode::None
                                 : ocr_layer_mode_from_token(result_token).value_or(OcrLayerMode::Visible);
  choice.grouping = ocr_grouping_from_token(grouping->currentData().toString()).value_or(OcrGrouping::Blocks);
  choice.match_style = match_style->isChecked();
  // A row left as it opened keeps what was stored: the Type tool's font is "no choice", so
  // the row then follows the tool the next time too.
  const auto family = font->currentFont().family();
  choice.font_family = family == untouched_family ? stored.font_family : family;
  if (choice.font_family == context.type_tool_family) {
    choice.font_family.clear();
  }

  picker->store();
  auto settings = app_settings();
  if (for_import) {
    settings.setValue(kPdfImportResultKey, result_token);
  } else {
    settings.setValue(kResultKey, result_token);
    settings.setValue(kSourceKey, ocr_source_token(choice.source));
  }
  settings.setValue(kGroupingKey, ocr_grouping_token(choice.grouping));
  settings.setValue(kMatchStyleKey, choice.match_style);
  if (choice.font_family.isEmpty()) {
    settings.remove(kFontFamilyKey);
  } else {
    settings.setValue(kFontFamilyKey, choice.font_family);
  }
  return choice;
}

}  // namespace patchy::ui
