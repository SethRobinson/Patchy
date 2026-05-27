#include "ui/image_save_options_dialog.hpp"

#include "ui/dialog_utils.hpp"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>

namespace patchy::ui {

namespace {

constexpr int kDefaultJpegQuality = 95;

QString normalized_save_extension(QString extension) {
  extension = extension.toLower();
  if (extension.startsWith(QLatin1Char('.'))) {
    extension.remove(0, 1);
  }
  return extension;
}

bool is_jpeg_extension(const QString& extension) {
  const auto normalized = normalized_save_extension(extension);
  return normalized == QStringLiteral("jpg") || normalized == QStringLiteral("jpeg");
}

bool is_bmp_extension(const QString& extension) {
  return normalized_save_extension(extension) == QStringLiteral("bmp");
}

QVBoxLayout* create_options_dialog_chrome(QDialog& dialog, const QString& title) {
  auto* root = new QVBoxLayout(&dialog);
  auto* content = install_dark_dialog_chrome(dialog, root, title);
  content->setSpacing(10);
  dialog.resize(320, 150);
  return content;
}

QDialogButtonBox* add_dialog_buttons(QVBoxLayout* content, QDialog& dialog) {
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  content->addWidget(buttons);
  return buttons;
}

}  // namespace

bool image_save_options_apply_to_extension(const QString& extension) {
  return is_jpeg_extension(extension) || is_bmp_extension(extension);
}

ImageSaveOptions load_image_save_option_defaults() {
  QSettings settings(QStringLiteral("Patchy"), QStringLiteral("Patchy"));
  ImageSaveOptions options;
  options.jpeg_quality =
      std::clamp(settings.value(QStringLiteral("saveOptions/jpegQuality"), kDefaultJpegQuality).toInt(), 0, 100);
  options.bmp_preserve_alpha = settings.value(QStringLiteral("saveOptions/bmpPreserveAlpha"), true).toBool();
  return options;
}

void save_image_save_option_defaults(const ImageSaveOptions& options) {
  QSettings settings(QStringLiteral("Patchy"), QStringLiteral("Patchy"));
  settings.setValue(QStringLiteral("saveOptions/jpegQuality"), std::clamp(options.jpeg_quality, 0, 100));
  settings.setValue(QStringLiteral("saveOptions/bmpPreserveAlpha"), options.bmp_preserve_alpha);
}

std::optional<ImageSaveOptions> prompt_image_save_options(QWidget* parent, const QString& extension,
                                                          ImageSaveOptions options) {
  if (is_jpeg_extension(extension)) {
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("jpegSaveOptionsDialog"));
    auto* content = create_options_dialog_chrome(dialog, QObject::tr("JPEG Options"));

    auto* form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(10);
    form->setVerticalSpacing(8);
    auto* quality_row = new QWidget(&dialog);
    auto* quality_layout = new QHBoxLayout(quality_row);
    quality_layout->setContentsMargins(0, 0, 0, 0);
    quality_layout->setSpacing(8);
    auto* quality_slider = new QSlider(Qt::Horizontal, quality_row);
    quality_slider->setObjectName(QStringLiteral("jpegQualitySlider"));
    quality_slider->setRange(0, 100);
    quality_slider->setValue(std::clamp(options.jpeg_quality, 0, 100));
    quality_slider->setMinimumWidth(160);
    auto* quality = new QSpinBox(quality_row);
    quality->setObjectName(QStringLiteral("jpegQualitySpin"));
    quality->setRange(0, 100);
    quality->setSuffix(QStringLiteral("%"));
    quality->setValue(std::clamp(options.jpeg_quality, 0, 100));
    configure_dialog_spinbox(quality, 88);
    QObject::connect(quality_slider, &QSlider::valueChanged, quality, &QSpinBox::setValue);
    QObject::connect(quality, &QSpinBox::valueChanged, quality_slider, &QSlider::setValue);
    quality_layout->addWidget(quality_slider, 1);
    quality_layout->addWidget(quality);
    form->addRow(new QLabel(QObject::tr("Quality:"), &dialog), quality_row);
    content->addLayout(form);
    add_dialog_buttons(content, dialog);

    if (exec_dialog(dialog) != QDialog::Accepted) {
      return std::nullopt;
    }
    options.jpeg_quality = quality->value();
    return options;
  }

  if (is_bmp_extension(extension)) {
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("bmpSaveOptionsDialog"));
    auto* content = create_options_dialog_chrome(dialog, QObject::tr("BMP Options"));

    auto* preserve_alpha = new QCheckBox(QObject::tr("Save alpha channel"), &dialog);
    preserve_alpha->setObjectName(QStringLiteral("bmpSaveAlphaCheck"));
    preserve_alpha->setChecked(options.bmp_preserve_alpha);
    content->addWidget(preserve_alpha);
    add_dialog_buttons(content, dialog);

    if (exec_dialog(dialog) != QDialog::Accepted) {
      return std::nullopt;
    }
    options.bmp_preserve_alpha = preserve_alpha->isChecked();
    return options;
  }

  return options;
}

}  // namespace patchy::ui
