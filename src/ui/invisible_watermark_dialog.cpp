#include "ui/invisible_watermark_dialog.hpp"

#include "ui/dialog_utils.hpp"
#include "ui/watermark_model_download.hpp"

#include <QButtonGroup>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSpinBox>
#include <QVBoxLayout>

#include <string>

namespace patchy::ui {
namespace {

class InvisibleWatermarkStrings {
  Q_DECLARE_TR_FUNCTIONS(InvisibleWatermarkDialog)
};

namespace tm = patchy::trustmark;

QString schema_choice_text(tm::Schema schema) {
  QString name;
  switch (schema) {
    case tm::Schema::Bch5:
      name = InvisibleWatermarkStrings::tr("Standard");
      break;
    case tm::Schema::BchSuper:
      name = InvisibleWatermarkStrings::tr("Most robust");
      break;
    case tm::Schema::Bch4:
      name = InvisibleWatermarkStrings::tr("More capacity");
      break;
    case tm::Schema::Bch3:
      name = InvisibleWatermarkStrings::tr("Most capacity");
      break;
  }
  return InvisibleWatermarkStrings::tr("%1: %2 characters, survives %3 damaged bits")
      .arg(name)
      .arg(tm::schema_text_capacity(schema))
      .arg(tm::schema_correctable_bits(schema));
}

QLabel* wrapped_label(const QString& text, QWidget* parent) {
  auto* label = new QLabel(text, parent);
  label->setWordWrap(true);
  return label;
}

}  // namespace

std::vector<std::uint8_t> random_watermark_id_bits(int bits) {
  std::vector<std::uint8_t> out;
  out.reserve(static_cast<std::size_t>(bits));
  auto* generator = QRandomGenerator::system();
  while (static_cast<int>(out.size()) < bits) {
    auto word = generator->generate64();
    for (int i = 0; i < 64 && static_cast<int>(out.size()) < bits; ++i) {
      out.push_back(static_cast<std::uint8_t>(word & 1U));
      word >>= 1U;
    }
  }
  return out;
}

std::optional<InvisibleWatermarkRequest> request_invisible_watermark(QWidget* parent,
                                                                     const InvisibleWatermarkDialogSupport& support) {
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("invisibleWatermarkDialog"));
  dialog.setWindowTitle(InvisibleWatermarkStrings::tr("Add Invisible Watermark"));
  auto* layout = new QVBoxLayout(&dialog);
  layout->setSpacing(10);

  auto* intro = wrapped_label(
      InvisibleWatermarkStrings::tr("Hides a short message in the active layer's pixels with Adobe TrustMark. It survives most "
                  "resizing, JPEG compression and sharing, and the change is too small to see."),
      &dialog);
  intro->setObjectName(QStringLiteral("invisibleWatermarkIntro"));
  layout->addWidget(intro);
  auto* warning = wrapped_label(
      InvisibleWatermarkStrings::tr("TrustMark has no key: anyone with a free TrustMark reader, Patchy's File Info included, can read "
                  "the message. Use it to label an image or point to a record of it, not to hide anything or to "
                  "prove ownership on its own."),
      &dialog);
  warning->setObjectName(QStringLiteral("invisibleWatermarkOpenNote"));
  layout->addWidget(warning);

  auto* form = new QFormLayout();
  form->setHorizontalSpacing(10);
  form->setVerticalSpacing(8);

  auto* schema_combo = new QComboBox(&dialog);
  schema_combo->setObjectName(QStringLiteral("invisibleWatermarkSchemaCombo"));
  for (const auto schema : {tm::Schema::Bch5, tm::Schema::BchSuper, tm::Schema::Bch4, tm::Schema::Bch3}) {
    schema_combo->addItem(schema_choice_text(schema), static_cast<int>(schema));
  }
  schema_combo->setToolTip(InvisibleWatermarkStrings::tr("How much error correction protects the message. Standard is TrustMark's "
                                       "default."));
  form->addRow(InvisibleWatermarkStrings::tr("Error correction:"), schema_combo);

  auto* text_radio = new QRadioButton(InvisibleWatermarkStrings::tr("Text:"), &dialog);
  text_radio->setObjectName(QStringLiteral("invisibleWatermarkTextRadio"));
  text_radio->setChecked(true);
  auto* text_row = new QHBoxLayout();
  auto* text_edit = new QLineEdit(&dialog);
  text_edit->setObjectName(QStringLiteral("invisibleWatermarkTextEdit"));
  // TrustMark text is 7-bit ASCII.
  text_edit->setValidator(
      new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[\\x20-\\x7E]*")), text_edit));
  auto* remaining = new QLabel(&dialog);
  remaining->setObjectName(QStringLiteral("invisibleWatermarkRemainingLabel"));
  text_row->addWidget(text_edit, 1);
  text_row->addWidget(remaining);
  form->addRow(text_radio, text_row);

  auto* id_radio = new QRadioButton(InvisibleWatermarkStrings::tr("Random ID:"), &dialog);
  id_radio->setObjectName(QStringLiteral("invisibleWatermarkIdRadio"));
  auto* id_row = new QHBoxLayout();
  auto* id_label = new QLabel(&dialog);
  id_label->setObjectName(QStringLiteral("invisibleWatermarkIdLabel"));
  id_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
  auto* new_id_button = new QPushButton(InvisibleWatermarkStrings::tr("New ID"), &dialog);
  new_id_button->setObjectName(QStringLiteral("invisibleWatermarkNewIdButton"));
  id_row->addWidget(id_label, 1);
  id_row->addWidget(new_id_button);
  form->addRow(id_radio, id_row);
  auto* kinds = new QButtonGroup(&dialog);
  kinds->addButton(text_radio);
  kinds->addButton(id_radio);

  auto* strength_spin = add_dialog_slider_spin_row(form, &dialog, InvisibleWatermarkStrings::tr("Strength:"),
                                                   QStringLiteral("invisibleWatermarkStrengthSlider"),
                                                   QStringLiteral("invisibleWatermarkStrengthSpin"), 50, 150, 100,
                                                   QStringLiteral("%"));
  strength_spin->setToolTip(InvisibleWatermarkStrings::tr("100% is TrustMark's default. Lower is less visible but easier to lose; "
                                        "Adobe uses 150% for prints."));
  layout->addLayout(form);

  // The models are one optional download; nothing is fetched until the user asks.
  auto* models_row = new QHBoxLayout();
  auto* models_note = wrapped_label(
      InvisibleWatermarkStrings::tr("Needs the watermark models (Adobe's, MIT License): a %1 MB download from %2.")
          .arg(watermark_model_download_megabytes())
          .arg(model_download_source_text(watermark_model_files())),
      &dialog);
  models_note->setObjectName(QStringLiteral("invisibleWatermarkModelsNote"));
  auto* download_button = new QPushButton(InvisibleWatermarkStrings::tr("Download Watermark Models"), &dialog);
  download_button->setObjectName(QStringLiteral("invisibleWatermarkDownloadButton"));
  models_row->addWidget(models_note, 1);
  models_row->addWidget(download_button);
  layout->addLayout(models_row);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  buttons->setObjectName(QStringLiteral("invisibleWatermarkButtonBox"));
  buttons->button(QDialogButtonBox::Ok)->setText(InvisibleWatermarkStrings::tr("Add Watermark"));
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  std::vector<std::uint8_t> id_bits;
  const auto current_schema = [schema_combo] { return static_cast<tm::Schema>(schema_combo->currentData().toInt()); };
  const auto new_id = [&] {
    id_bits = random_watermark_id_bits(tm::schema_data_bits(current_schema()));
    id_label->setText(QString::fromStdString(tm::bits_to_hex(id_bits)));
  };
  const auto refresh = [&] {
    const int capacity = tm::schema_text_capacity(current_schema());
    text_edit->setMaxLength(capacity);  // truncates a longer text
    remaining->setText(InvisibleWatermarkStrings::tr("%1 of %2 characters left").arg(capacity - text_edit->text().size()).arg(capacity));
    if (static_cast<int>(id_bits.size()) != tm::schema_data_bits(current_schema())) {
      new_id();
    }
    const bool text_mode = text_radio->isChecked();
    text_edit->setEnabled(text_mode);
    remaining->setEnabled(text_mode);
    id_label->setEnabled(!text_mode);
    new_id_button->setEnabled(!text_mode);
    const bool models = support.models_available && support.models_available();
    models_note->setVisible(!models);
    download_button->setVisible(!models && static_cast<bool>(support.download_models));
    buttons->button(QDialogButtonBox::Ok)
        ->setEnabled(models && (!text_mode || !text_edit->text().trimmed().isEmpty()));
  };
  QObject::connect(schema_combo, &QComboBox::currentIndexChanged, &dialog, [&] { refresh(); });
  QObject::connect(text_edit, &QLineEdit::textChanged, &dialog, [&] { refresh(); });
  QObject::connect(text_radio, &QRadioButton::toggled, &dialog, [&] { refresh(); });
  QObject::connect(new_id_button, &QPushButton::clicked, &dialog, [&] {
    new_id();
    refresh();
  });
  QObject::connect(download_button, &QPushButton::clicked, &dialog, [&] {
    if (support.download_models) {
      static_cast<void>(support.download_models(&dialog));
    }
    refresh();
  });
  new_id();
  refresh();
  dialog.setMinimumWidth(500);
  text_edit->setFocus();
  if (exec_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }

  InvisibleWatermarkRequest request;
  request.schema = current_schema();
  if (text_radio->isChecked()) {
    request.text = text_edit->text().trimmed();
    request.data_bits = tm::text_to_bits(request.text.toLatin1().toStdString());
  } else {
    request.data_bits = id_bits;
  }
  request.data_bits.resize(static_cast<std::size_t>(tm::schema_data_bits(request.schema)), 0);
  request.strength = static_cast<float>(strength_spin->value()) / 100.0F;
  return request;
}

}  // namespace patchy::ui
