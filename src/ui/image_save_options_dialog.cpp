#include "ui/image_save_options_dialog.hpp"

#include "formats/jxr_document_io.hpp"
#include "formats/rttex_document_io.hpp"
#include "ui/app_settings.hpp"

#include "ui/dialog_utils.hpp"
#include "ui/theme_qss.hpp"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

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

bool is_ico_extension(const QString& extension) {
  return normalized_save_extension(extension) == QStringLiteral("ico");
}

bool is_cur_extension(const QString& extension) {
  return normalized_save_extension(extension) == QStringLiteral("cur");
}

bool is_pdf_extension(const QString& extension) {
  return normalized_save_extension(extension) == QStringLiteral("pdf");
}

bool is_jxr_extension(const QString& extension) {
  return jxr::is_jxr_extension(normalized_save_extension(extension).toStdString());
}

bool is_rttex_extension(const QString& extension) {
  return rttex::is_rttex_extension(normalized_save_extension(extension).toStdString());
}

// The Proton texture tokens live with the codec so the settings keys, the dialog, and the
// reader's session metadata can never disagree.
QString rttex_encoding_key(rttex::Encoding encoding) {
  const auto token = rttex::encoding_token(encoding);
  return QString::fromLatin1(token.data(), static_cast<qsizetype>(token.size()));
}

rttex::Encoding rttex_encoding_from_key(const QString& key, rttex::Encoding fallback) {
  return rttex::encoding_from_token(key.toStdString()).value_or(fallback);
}

QString rttex_power_of_two_key(rttex::PowerOfTwo mode) {
  const auto token = rttex::power_of_two_token(mode);
  return QString::fromLatin1(token.data(), static_cast<qsizetype>(token.size()));
}

rttex::PowerOfTwo rttex_power_of_two_from_key(const QString& key, rttex::PowerOfTwo fallback) {
  return rttex::power_of_two_from_token(key.toStdString()).value_or(fallback);
}

constexpr std::array<int, 7> kIcoSizeChoices = {16, 24, 32, 48, 64, 128, 256};

QString ico_resample_key(IcoResample resample) {
  switch (resample) {
    case IcoResample::Auto:
      return QStringLiteral("auto");
    case IcoResample::Nearest:
      return QStringLiteral("nearest");
    case IcoResample::Smooth:
      return QStringLiteral("smooth");
  }
  return QStringLiteral("auto");
}

IcoResample ico_resample_from_key(const QString& key, IcoResample fallback) {
  if (key == QStringLiteral("auto")) {
    return IcoResample::Auto;
  }
  if (key == QStringLiteral("nearest")) {
    return IcoResample::Nearest;
  }
  if (key == QStringLiteral("smooth")) {
    return IcoResample::Smooth;
  }
  return fallback;
}

QString bmp_encoding_key(bmp::BmpEncoding encoding) {
  switch (encoding) {
    case bmp::BmpEncoding::Rgba32:
      return QStringLiteral("rgba32");
    case bmp::BmpEncoding::Rgb24:
      return QStringLiteral("rgb24");
    case bmp::BmpEncoding::Indexed8:
      return QStringLiteral("indexed8");
    case bmp::BmpEncoding::Indexed4:
      return QStringLiteral("indexed4");
    case bmp::BmpEncoding::Indexed2:
      return QStringLiteral("indexed2");
  }
  return QStringLiteral("rgba32");
}

bmp::BmpEncoding bmp_encoding_from_key(const QString& key, bmp::BmpEncoding fallback) {
  if (key == QStringLiteral("rgba32")) {
    return bmp::BmpEncoding::Rgba32;
  }
  if (key == QStringLiteral("rgb24")) {
    return bmp::BmpEncoding::Rgb24;
  }
  if (key == QStringLiteral("indexed8")) {
    return bmp::BmpEncoding::Indexed8;
  }
  if (key == QStringLiteral("indexed4")) {
    return bmp::BmpEncoding::Indexed4;
  }
  if (key == QStringLiteral("indexed2")) {
    return bmp::BmpEncoding::Indexed2;
  }
  return fallback;
}

QString bmp_palette_mode_key(bmp::BmpPaletteMode mode) {
  switch (mode) {
    case bmp::BmpPaletteMode::Exact:
      return QStringLiteral("exact");
    case bmp::BmpPaletteMode::Quantize:
      return QStringLiteral("quantize");
    case bmp::BmpPaletteMode::PaletteFile:
      return QStringLiteral("paletteFile");
  }
  return QStringLiteral("exact");
}

bmp::BmpPaletteMode bmp_palette_mode_from_key(const QString& key, bmp::BmpPaletteMode fallback) {
  if (key == QStringLiteral("exact")) {
    return bmp::BmpPaletteMode::Exact;
  }
  if (key == QStringLiteral("quantize")) {
    return bmp::BmpPaletteMode::Quantize;
  }
  if (key == QStringLiteral("paletteFile")) {
    return bmp::BmpPaletteMode::PaletteFile;
  }
  return fallback;
}

bool bmp_encoding_is_indexed(bmp::BmpEncoding encoding) {
  return encoding == bmp::BmpEncoding::Indexed8 || encoding == bmp::BmpEncoding::Indexed4 ||
         encoding == bmp::BmpEncoding::Indexed2;
}

bool bmp_encoding_accepts_palette_file(bmp::BmpEncoding encoding) {
  return encoding == bmp::BmpEncoding::Indexed8 || encoding == bmp::BmpEncoding::Indexed4;
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

// The export Scale combo (1x/2x/4x/8x nearest neighbor). Its choice persists via its own
// settings key so Save/Save As option defaults can never pick up a stale scale.
QComboBox* add_export_scale_row(QVBoxLayout* content, QDialog& dialog) {
  auto* row = new QWidget(&dialog);
  auto* layout = new QHBoxLayout(row);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(10);
  layout->addWidget(new QLabel(QObject::tr("Scale:"), row));
  auto* combo = new QComboBox(row);
  combo->setObjectName(QStringLiteral("exportScaleCombo"));
  for (const auto scale : {1, 2, 4, 8}) {
    combo->addItem(QObject::tr("%1x (nearest neighbor)").arg(scale), scale);
  }
  const auto stored = app_settings().value(QStringLiteral("saveOptions/exportScale"), 1).toInt();
  combo->setCurrentIndex(std::max(0, combo->findData(stored)));
  layout->addWidget(combo, 1);
  content->addWidget(row);
  return combo;
}

void persist_export_scale(int scale) {
  app_settings().setValue(QStringLiteral("saveOptions/exportScale"), scale);
}

}  // namespace

bool image_save_options_apply_to_extension(const QString& extension) {
  return is_jpeg_extension(extension) || is_bmp_extension(extension) || is_ico_extension(extension) ||
         is_cur_extension(extension) || is_pdf_extension(extension) || is_jxr_extension(extension) ||
         is_rttex_extension(extension);
}

ImageSaveOptions load_image_save_option_defaults() {
  auto settings = app_settings();
  ImageSaveOptions options;
  options.jpeg_quality =
      std::clamp(settings.value(QStringLiteral("saveOptions/jpegQuality"), kDefaultJpegQuality).toInt(), 0, 100);
  if (settings.contains(QStringLiteral("saveOptions/bmpEncoding"))) {
    options.bmp_encoding =
        bmp_encoding_from_key(settings.value(QStringLiteral("saveOptions/bmpEncoding")).toString(), options.bmp_encoding);
  } else {
    const auto preserve_alpha = settings.value(QStringLiteral("saveOptions/bmpPreserveAlpha"), true).toBool();
    options.bmp_encoding = preserve_alpha ? bmp::BmpEncoding::Rgba32 : bmp::BmpEncoding::Rgb24;
  }
  options.bmp_palette_mode = bmp_palette_mode_from_key(
      settings.value(QStringLiteral("saveOptions/bmpPaletteMode"), bmp_palette_mode_key(options.bmp_palette_mode))
          .toString(),
      options.bmp_palette_mode);
  options.bmp_palette_path = settings.value(QStringLiteral("saveOptions/bmpPalettePath")).toString();
  if (settings.contains(QStringLiteral("saveOptions/icoSizes"))) {
    std::vector<int> sizes;
    const auto stored = settings.value(QStringLiteral("saveOptions/icoSizes")).toString();
    for (const auto& token : stored.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
      bool valid = false;
      const auto size = token.trimmed().toInt(&valid);
      if (valid && std::find(kIcoSizeChoices.begin(), kIcoSizeChoices.end(), size) != kIcoSizeChoices.end() &&
          std::find(sizes.begin(), sizes.end(), size) == sizes.end()) {
        sizes.push_back(size);
      }
    }
    if (!sizes.empty()) {
      std::sort(sizes.begin(), sizes.end());
      options.ico_sizes = std::move(sizes);
    }
  }
  options.ico_resample = ico_resample_from_key(
      settings.value(QStringLiteral("saveOptions/icoResample"), ico_resample_key(options.ico_resample)).toString(),
      options.ico_resample);
  options.pdf_lossless = settings.value(QStringLiteral("saveOptions/pdfLossless"), options.pdf_lossless).toBool();
  options.pdf_missing_fonts_as_images =
      settings.value(QStringLiteral("saveOptions/pdfMissingFontsAsImages"), options.pdf_missing_fonts_as_images)
          .toBool();
  options.gif_frame_delay_cs = std::clamp(
      settings.value(QStringLiteral("saveOptions/gifFrameDelayCs"), options.gif_frame_delay_cs).toInt(), 0, 0xffff);
  options.jxr_quality =
      std::clamp(settings.value(QStringLiteral("saveOptions/jxrQuality"), options.jxr_quality).toInt(), 1, 100);
  options.jxr_lossless = settings.value(QStringLiteral("saveOptions/jxrLossless"), options.jxr_lossless).toBool();
  options.rttex_encoding = rttex_encoding_from_key(
      settings.value(QStringLiteral("saveOptions/rttexEncoding"), rttex_encoding_key(options.rttex_encoding))
          .toString(),
      options.rttex_encoding);
  options.rttex_jpeg_quality = std::clamp(
      settings.value(QStringLiteral("saveOptions/rttexJpegQuality"), options.rttex_jpeg_quality).toInt(), 1, 100);
  options.rttex_power_of_two = rttex_power_of_two_from_key(
      settings.value(QStringLiteral("saveOptions/rttexPowerOfTwo"), rttex_power_of_two_key(options.rttex_power_of_two))
          .toString(),
      options.rttex_power_of_two);
  options.rttex_force_square =
      settings.value(QStringLiteral("saveOptions/rttexForceSquare"), options.rttex_force_square).toBool();
  options.rttex_force_alpha =
      settings.value(QStringLiteral("saveOptions/rttexForceAlpha"), options.rttex_force_alpha).toBool();
  options.rttex_compress = settings.value(QStringLiteral("saveOptions/rttexCompress"), options.rttex_compress).toBool();
  return options;
}

void save_image_save_option_defaults(const ImageSaveOptions& options) {
  auto settings = app_settings();
  settings.setValue(QStringLiteral("saveOptions/jpegQuality"), std::clamp(options.jpeg_quality, 0, 100));
  settings.setValue(QStringLiteral("saveOptions/bmpEncoding"), bmp_encoding_key(options.bmp_encoding));
  settings.setValue(QStringLiteral("saveOptions/bmpPaletteMode"), bmp_palette_mode_key(options.bmp_palette_mode));
  if (!options.bmp_palette_path.isEmpty()) {
    settings.setValue(QStringLiteral("saveOptions/bmpPalettePath"), options.bmp_palette_path);
  }
  if (!options.ico_sizes.empty()) {
    QStringList tokens;
    tokens.reserve(static_cast<qsizetype>(options.ico_sizes.size()));
    for (const auto size : options.ico_sizes) {
      tokens.push_back(QString::number(size));
    }
    settings.setValue(QStringLiteral("saveOptions/icoSizes"), tokens.join(QLatin1Char(',')));
  }
  settings.setValue(QStringLiteral("saveOptions/icoResample"), ico_resample_key(options.ico_resample));
  settings.setValue(QStringLiteral("saveOptions/pdfLossless"), options.pdf_lossless);
  settings.setValue(QStringLiteral("saveOptions/pdfMissingFontsAsImages"), options.pdf_missing_fonts_as_images);
  settings.setValue(QStringLiteral("saveOptions/gifFrameDelayCs"), std::clamp(options.gif_frame_delay_cs, 0, 0xffff));
  settings.setValue(QStringLiteral("saveOptions/jxrQuality"), std::clamp(options.jxr_quality, 1, 100));
  settings.setValue(QStringLiteral("saveOptions/jxrLossless"), options.jxr_lossless);
  settings.setValue(QStringLiteral("saveOptions/rttexEncoding"), rttex_encoding_key(options.rttex_encoding));
  settings.setValue(QStringLiteral("saveOptions/rttexJpegQuality"), std::clamp(options.rttex_jpeg_quality, 1, 100));
  settings.setValue(QStringLiteral("saveOptions/rttexPowerOfTwo"), rttex_power_of_two_key(options.rttex_power_of_two));
  settings.setValue(QStringLiteral("saveOptions/rttexForceSquare"), options.rttex_force_square);
  settings.setValue(QStringLiteral("saveOptions/rttexForceAlpha"), options.rttex_force_alpha);
  settings.setValue(QStringLiteral("saveOptions/rttexCompress"), options.rttex_compress);
}

std::optional<ImageSaveOptions> prompt_image_save_options(QWidget* parent, const QString& extension,
                                                          ImageSaveOptions options, bool for_export) {
  options.export_scale = 1;
  if (is_jpeg_extension(extension)) {
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("jpegSaveOptionsDialog"));
    auto* content = create_options_dialog_chrome(dialog, QObject::tr("JPEG Options"));
    auto* scale_combo = for_export ? add_export_scale_row(content, dialog) : nullptr;

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
    if (scale_combo != nullptr) {
      options.export_scale = scale_combo->currentData().toInt();
      persist_export_scale(options.export_scale);
    }
    return options;
  }

  if (is_jxr_extension(extension)) {
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("jxrSaveOptionsDialog"));
    auto* content = create_options_dialog_chrome(dialog, QObject::tr("JPEG XR Options"));
    dialog.resize(380, 210);
    auto* scale_combo = for_export ? add_export_scale_row(content, dialog) : nullptr;

    auto* form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(10);
    form->setVerticalSpacing(8);
    auto* quality_row = new QWidget(&dialog);
    auto* quality_layout = new QHBoxLayout(quality_row);
    quality_layout->setContentsMargins(0, 0, 0, 0);
    quality_layout->setSpacing(8);
    auto* quality_slider = new QSlider(Qt::Horizontal, quality_row);
    quality_slider->setObjectName(QStringLiteral("jxrQualitySlider"));
    quality_slider->setRange(1, 100);
    quality_slider->setValue(std::clamp(options.jxr_quality, 1, 100));
    quality_slider->setMinimumWidth(160);
    auto* quality = new QSpinBox(quality_row);
    quality->setObjectName(QStringLiteral("jxrQualitySpin"));
    quality->setRange(1, 100);
    quality->setSuffix(QStringLiteral("%"));
    quality->setValue(std::clamp(options.jxr_quality, 1, 100));
    configure_dialog_spinbox(quality, 88);
    QObject::connect(quality_slider, &QSlider::valueChanged, quality, &QSpinBox::setValue);
    QObject::connect(quality, &QSpinBox::valueChanged, quality_slider, &QSlider::setValue);
    quality_layout->addWidget(quality_slider, 1);
    quality_layout->addWidget(quality);
    form->addRow(new QLabel(QObject::tr("Quality:"), &dialog), quality_row);
    content->addLayout(form);

    auto* lossless = new QCheckBox(QObject::tr("Lossless (larger file)"), &dialog);
    lossless->setObjectName(QStringLiteral("jxrLosslessCheck"));
    lossless->setChecked(options.jxr_lossless);
    content->addWidget(lossless);

    auto* note = new QLabel(
        QObject::tr("JPEG XR is written by the Windows codec. Patchy saves 8 bits per channel, so a file opened "
                    "from an HDR capture is written back as the tone mapped image."),
        &dialog);
    note->setObjectName(QStringLiteral("jxrSaveNote"));
    note->setWordWrap(true);
    content->addWidget(note);
    add_dialog_buttons(content, dialog);

    // Lossless overrides the quality value in the encoder, so the slider goes dead rather
    // than showing a number the file will not use.
    const auto sync_quality_enabled = [quality_row, lossless] { quality_row->setEnabled(!lossless->isChecked()); };
    QObject::connect(lossless, &QCheckBox::toggled, &dialog, [sync_quality_enabled](bool) { sync_quality_enabled(); });
    sync_quality_enabled();

    if (exec_dialog(dialog) != QDialog::Accepted) {
      return std::nullopt;
    }
    options.jxr_quality = quality->value();
    options.jxr_lossless = lossless->isChecked();
    if (scale_combo != nullptr) {
      options.export_scale = scale_combo->currentData().toInt();
      persist_export_scale(options.export_scale);
    }
    return options;
  }

  if (is_rttex_extension(extension)) {
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("rttexSaveOptionsDialog"));
    auto* content = create_options_dialog_chrome(dialog, QObject::tr("Proton Texture Options"));
    dialog.resize(440, 360);
    auto* scale_combo = for_export ? add_export_scale_row(content, dialog) : nullptr;

    auto* form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(10);
    form->setVerticalSpacing(8);

    auto* encoding = new QComboBox(&dialog);
    encoding->setObjectName(QStringLiteral("rttexEncodingCombo"));
    encoding->addItem(QObject::tr("Lossless (8 bits per channel)"), rttex_encoding_key(rttex::Encoding::Rgba8));
    encoding->addItem(QObject::tr("16-bit (RGBA 4444, or RGB 565 without alpha)"),
                      rttex_encoding_key(rttex::Encoding::Rgba4444));
    encoding->addItem(QObject::tr("JPEG (smaller, no alpha)"), rttex_encoding_key(rttex::Encoding::Jpeg));
    encoding->setCurrentIndex(std::max(0, encoding->findData(rttex_encoding_key(options.rttex_encoding))));
    form->addRow(new QLabel(QObject::tr("Encoding:"), &dialog), encoding);

    auto* quality_row = new QWidget(&dialog);
    auto* quality_layout = new QHBoxLayout(quality_row);
    quality_layout->setContentsMargins(0, 0, 0, 0);
    quality_layout->setSpacing(8);
    auto* quality_slider = new QSlider(Qt::Horizontal, quality_row);
    quality_slider->setObjectName(QStringLiteral("rttexJpegQualitySlider"));
    quality_slider->setRange(1, 100);
    quality_slider->setValue(std::clamp(options.rttex_jpeg_quality, 1, 100));
    quality_slider->setMinimumWidth(160);
    auto* quality = new QSpinBox(quality_row);
    quality->setObjectName(QStringLiteral("rttexJpegQualitySpin"));
    quality->setRange(1, 100);
    quality->setSuffix(QStringLiteral("%"));
    quality->setValue(std::clamp(options.rttex_jpeg_quality, 1, 100));
    configure_dialog_spinbox(quality, 88);
    QObject::connect(quality_slider, &QSlider::valueChanged, quality, &QSpinBox::setValue);
    QObject::connect(quality, &QSpinBox::valueChanged, quality_slider, &QSlider::setValue);
    quality_layout->addWidget(quality_slider, 1);
    quality_layout->addWidget(quality);
    form->addRow(new QLabel(QObject::tr("JPEG quality:"), &dialog), quality_row);

    auto* power_of_two = new QComboBox(&dialog);
    power_of_two->setObjectName(QStringLiteral("rttexPowerOfTwoCombo"));
    power_of_two->addItem(QObject::tr("Pad to a power of two"), rttex_power_of_two_key(rttex::PowerOfTwo::Pad));
    power_of_two->addItem(QObject::tr("Stretch to a power of two"),
                          rttex_power_of_two_key(rttex::PowerOfTwo::Stretch));
    power_of_two->addItem(QObject::tr("Keep the exact size"), rttex_power_of_two_key(rttex::PowerOfTwo::None));
    power_of_two->setCurrentIndex(
        std::max(0, power_of_two->findData(rttex_power_of_two_key(options.rttex_power_of_two))));
    form->addRow(new QLabel(QObject::tr("Texture size:"), &dialog), power_of_two);
    content->addLayout(form);

    auto* force_square = new QCheckBox(QObject::tr("Force a square texture"), &dialog);
    force_square->setObjectName(QStringLiteral("rttexForceSquareCheck"));
    force_square->setChecked(options.rttex_force_square);
    content->addWidget(force_square);
    auto* force_alpha = new QCheckBox(QObject::tr("Keep the alpha channel even when the image is opaque"), &dialog);
    force_alpha->setObjectName(QStringLiteral("rttexForceAlphaCheck"));
    force_alpha->setChecked(options.rttex_force_alpha);
    content->addWidget(force_alpha);
    auto* compress = new QCheckBox(QObject::tr("Compress (RTPACK zlib container)"), &dialog);
    compress->setObjectName(QStringLiteral("rttexCompressCheck"));
    compress->setChecked(options.rttex_compress);
    content->addWidget(compress);

    auto* note = new QLabel(
        QObject::tr("The texture is padded to a power of two and its true size is recorded in the header, so it "
                    "opens again at the true size. JPEG applies only to images without transparency (RTPack's "
                    "rule): a transparent image is written lossless instead."),
        &dialog);
    note->setObjectName(QStringLiteral("rttexSaveNote"));
    note->setWordWrap(true);
    content->addWidget(note);
    add_dialog_buttons(content, dialog);

    // The quality only feeds the JPEG encoder, so the row goes dead for the other encodings
    // rather than showing a number the file will not use.
    const auto sync_quality_enabled = [quality_row, encoding] {
      quality_row->setEnabled(encoding->currentData().toString() == rttex_encoding_key(rttex::Encoding::Jpeg));
    };
    QObject::connect(encoding, &QComboBox::currentIndexChanged, &dialog,
                     [sync_quality_enabled](int) { sync_quality_enabled(); });
    sync_quality_enabled();

    if (exec_dialog(dialog) != QDialog::Accepted) {
      return std::nullopt;
    }
    options.rttex_encoding = rttex_encoding_from_key(encoding->currentData().toString(), options.rttex_encoding);
    options.rttex_jpeg_quality = quality->value();
    options.rttex_power_of_two =
        rttex_power_of_two_from_key(power_of_two->currentData().toString(), options.rttex_power_of_two);
    options.rttex_force_square = force_square->isChecked();
    options.rttex_force_alpha = force_alpha->isChecked();
    options.rttex_compress = compress->isChecked();
    if (scale_combo != nullptr) {
      options.export_scale = scale_combo->currentData().toInt();
      persist_export_scale(options.export_scale);
    }
    return options;
  }

  if (is_ico_extension(extension) || is_cur_extension(extension)) {
    const bool cursor = is_cur_extension(extension);
    QDialog dialog(parent);
    dialog.setObjectName(cursor ? QStringLiteral("curSaveOptionsDialog") : QStringLiteral("icoSaveOptionsDialog"));
    auto* content =
        create_options_dialog_chrome(dialog, cursor ? QObject::tr("Cursor Options") : QObject::tr("Icon Options"));
    dialog.resize(360, cursor ? 330 : 280);
    // No scale combo here: the size checkboxes fully define an icon's output dimensions.

    auto* sizes_group = new QGroupBox(QObject::tr("Sizes"), &dialog);
    auto* sizes_layout = new QVBoxLayout(sizes_group);
    sizes_layout->setContentsMargins(10, 8, 10, 8);
    sizes_layout->setSpacing(4);
    std::vector<QCheckBox*> size_checks;
    size_checks.reserve(kIcoSizeChoices.size());
    for (const auto size : kIcoSizeChoices) {
      auto* check = new QCheckBox(QStringLiteral("%1 x %1").arg(size), sizes_group);
      check->setObjectName(QStringLiteral("icoSize%1Check").arg(size));
      check->setChecked(std::find(options.ico_sizes.begin(), options.ico_sizes.end(), size) !=
                        options.ico_sizes.end());
      sizes_layout->addWidget(check);
      size_checks.push_back(check);
    }
    content->addWidget(sizes_group);

    auto* form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(10);
    form->setVerticalSpacing(8);
    auto* resample = new QComboBox(&dialog);
    resample->setObjectName(QStringLiteral("icoResampleCombo"));
    resample->addItem(QObject::tr("Auto (recommended)"), static_cast<int>(IcoResample::Auto));
    resample->addItem(QObject::tr("Nearest neighbor"), static_cast<int>(IcoResample::Nearest));
    resample->addItem(QObject::tr("Smooth"), static_cast<int>(IcoResample::Smooth));
    resample->setCurrentIndex(std::max(0, resample->findData(static_cast<int>(options.ico_resample))));
    form->addRow(new QLabel(QObject::tr("Scaling:"), &dialog), resample);

    QSpinBox* hotspot_x = nullptr;
    QSpinBox* hotspot_y = nullptr;
    if (cursor) {
      hotspot_x = new QSpinBox(&dialog);
      hotspot_x->setObjectName(QStringLiteral("curHotspotXSpin"));
      hotspot_x->setRange(0, 255);
      hotspot_x->setValue(std::clamp(options.cur_hotspot_x, 0, 255));
      configure_dialog_spinbox(hotspot_x, 88);
      hotspot_y = new QSpinBox(&dialog);
      hotspot_y->setObjectName(QStringLiteral("curHotspotYSpin"));
      hotspot_y->setRange(0, 255);
      hotspot_y->setValue(std::clamp(options.cur_hotspot_y, 0, 255));
      configure_dialog_spinbox(hotspot_y, 88);
      form->addRow(new QLabel(QObject::tr("Hotspot X:"), &dialog), hotspot_x);
      form->addRow(new QLabel(QObject::tr("Hotspot Y:"), &dialog), hotspot_y);
      auto* hint = new QLabel(QObject::tr("Hotspot is in pixels of the largest size; smaller sizes scale it."),
                              &dialog);
      hint->setWordWrap(true);
      form->addRow(hint);
    }
    content->addLayout(form);

    auto* buttons = add_dialog_buttons(content, dialog);
    auto* ok_button = buttons->button(QDialogButtonBox::Ok);
    const auto update_ok = [size_checks, ok_button] {
      if (ok_button == nullptr) {
        return;
      }
      const bool any = std::any_of(size_checks.begin(), size_checks.end(),
                                   [](const QCheckBox* check) { return check->isChecked(); });
      ok_button->setEnabled(any);
    };
    for (auto* check : size_checks) {
      QObject::connect(check, &QCheckBox::toggled, &dialog, [update_ok](bool) { update_ok(); });
    }
    update_ok();

    if (exec_dialog(dialog) != QDialog::Accepted) {
      return std::nullopt;
    }
    options.ico_sizes.clear();
    for (std::size_t i = 0; i < kIcoSizeChoices.size(); ++i) {
      if (size_checks[i]->isChecked()) {
        options.ico_sizes.push_back(kIcoSizeChoices[i]);
      }
    }
    options.ico_resample = static_cast<IcoResample>(resample->currentData().toInt());
    if (cursor) {
      options.cur_hotspot_x = hotspot_x->value();
      options.cur_hotspot_y = hotspot_y->value();
    }
    return options;
  }

  if (is_bmp_extension(extension)) {
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("bmpSaveOptionsDialog"));
    auto* content = create_options_dialog_chrome(dialog, QObject::tr("BMP Options"));
    dialog.resize(420, 360);
    auto* scale_combo = for_export ? add_export_scale_row(content, dialog) : nullptr;

    auto* depth_group = new QGroupBox(QObject::tr("Color depth"), &dialog);
    auto* depth_layout = new QVBoxLayout(depth_group);
    depth_layout->setContentsMargins(10, 8, 10, 8);
    depth_layout->setSpacing(4);
    auto* depth_buttons = new QButtonGroup(depth_group);
    const std::vector<std::pair<bmp::BmpEncoding, QString>> depth_choices = {
        {bmp::BmpEncoding::Rgba32, QObject::tr("32-bit with alpha")},
        {bmp::BmpEncoding::Rgb24, QObject::tr("24-bit RGB")},
        {bmp::BmpEncoding::Indexed8, QObject::tr("8-bit indexed")},
        {bmp::BmpEncoding::Indexed4, QObject::tr("4-bit indexed")},
        {bmp::BmpEncoding::Indexed2, QObject::tr("2-bit indexed (compatibility)")},
    };
    for (const auto& [encoding, label] : depth_choices) {
      auto* button = new QRadioButton(label, depth_group);
      QString object_name;
      switch (encoding) {
        case bmp::BmpEncoding::Rgba32:
          object_name = QStringLiteral("bmpEncodingRgba32Radio");
          break;
        case bmp::BmpEncoding::Rgb24:
          object_name = QStringLiteral("bmpEncodingRgb24Radio");
          break;
        case bmp::BmpEncoding::Indexed8:
          object_name = QStringLiteral("bmpEncodingIndexed8Radio");
          break;
        case bmp::BmpEncoding::Indexed4:
          object_name = QStringLiteral("bmpEncodingIndexed4Radio");
          break;
        case bmp::BmpEncoding::Indexed2:
          object_name = QStringLiteral("bmpEncodingIndexed2Radio");
          break;
      }
      button->setObjectName(object_name);
      depth_buttons->addButton(button, static_cast<int>(encoding));
      depth_layout->addWidget(button);
      if (encoding == options.bmp_encoding) {
        button->setChecked(true);
      }
    }
    if (depth_buttons->checkedButton() == nullptr) {
      depth_buttons->button(static_cast<int>(bmp::BmpEncoding::Rgba32))->setChecked(true);
    }
    content->addWidget(depth_group);

    auto* palette_group = new QGroupBox(QObject::tr("Indexed colors"), &dialog);
    auto* palette_layout = new QVBoxLayout(palette_group);
    palette_layout->setContentsMargins(10, 8, 10, 8);
    palette_layout->setSpacing(6);
    auto* palette_buttons = new QButtonGroup(palette_group);
    auto* exact_palette = new QRadioButton(QObject::tr("Exact colors"), palette_group);
    exact_palette->setObjectName(QStringLiteral("bmpPaletteExactRadio"));
    auto* quantize_palette = new QRadioButton(QObject::tr("Reduce colors automatically"), palette_group);
    quantize_palette->setObjectName(QStringLiteral("bmpPaletteQuantizeRadio"));
    auto* file_palette = new QRadioButton(QObject::tr("Use palette file"), palette_group);
    file_palette->setObjectName(QStringLiteral("bmpPaletteFileRadio"));
    palette_buttons->addButton(exact_palette, static_cast<int>(bmp::BmpPaletteMode::Exact));
    palette_buttons->addButton(quantize_palette, static_cast<int>(bmp::BmpPaletteMode::Quantize));
    palette_buttons->addButton(file_palette, static_cast<int>(bmp::BmpPaletteMode::PaletteFile));
    palette_layout->addWidget(exact_palette);
    palette_layout->addWidget(quantize_palette);
    palette_layout->addWidget(file_palette);
    if (auto* selected_palette_mode = palette_buttons->button(static_cast<int>(options.bmp_palette_mode))) {
      selected_palette_mode->setChecked(true);
    }
    if (palette_buttons->checkedButton() == nullptr) {
      exact_palette->setChecked(true);
    }

    auto* palette_row = new QWidget(palette_group);
    palette_row->setObjectName(QStringLiteral("bmpPalettePathRow"));
    auto* palette_row_layout = new QHBoxLayout(palette_row);
    palette_row_layout->setContentsMargins(0, 0, 0, 0);
    palette_row_layout->setSpacing(8);
    auto* palette_path = new QLineEdit(options.bmp_palette_path, palette_row);
    palette_path->setObjectName(QStringLiteral("bmpPalettePathEdit"));
    auto* browse_palette = new QPushButton(QObject::tr("Browse..."), palette_row);
    browse_palette->setObjectName(QStringLiteral("bmpPaletteBrowseButton"));
    palette_row_layout->addWidget(palette_path, 1);
    palette_row_layout->addWidget(browse_palette);
    palette_layout->addWidget(palette_row);
    content->addWidget(palette_group);

    auto* buttons = add_dialog_buttons(content, dialog);
    auto* ok_button = buttons->button(QDialogButtonBox::Ok);

    const auto selected_encoding = [depth_buttons] {
      return static_cast<bmp::BmpEncoding>(depth_buttons->checkedId());
    };
    const auto selected_palette_mode = [palette_buttons] {
      return static_cast<bmp::BmpPaletteMode>(palette_buttons->checkedId());
    };
    const auto update_palette_state = [=] {
      const auto encoding = selected_encoding();
      const auto indexed = bmp_encoding_is_indexed(encoding);
      const auto accepts_file = bmp_encoding_accepts_palette_file(encoding);
      palette_group->setEnabled(true);
      exact_palette->setEnabled(indexed);
      quantize_palette->setEnabled(indexed);
      file_palette->setEnabled(indexed && accepts_file);
      if (!indexed) {
        exact_palette->setChecked(true);
      } else if (!accepts_file && file_palette->isChecked()) {
        exact_palette->setChecked(true);
      }
      const auto use_file = indexed && accepts_file && selected_palette_mode() == bmp::BmpPaletteMode::PaletteFile;
      palette_path->setEnabled(use_file);
      browse_palette->setEnabled(true);
      if (ok_button != nullptr) {
        ok_button->setEnabled(!use_file || !palette_path->text().trimmed().isEmpty());
      }
    };
    QObject::connect(depth_buttons, &QButtonGroup::idClicked, &dialog, [update_palette_state](int) {
      update_palette_state();
    });
    QObject::connect(palette_buttons, &QButtonGroup::idClicked, &dialog, [update_palette_state](int) {
      update_palette_state();
    });
    QObject::connect(palette_path, &QLineEdit::textChanged, &dialog,
                     [update_palette_state](const QString&) { update_palette_state(); });
    QObject::connect(browse_palette, &QPushButton::clicked, &dialog,
                     [&dialog, depth_buttons, file_palette, palette_path, update_palette_state] {
      if (auto* indexed8 = depth_buttons->button(static_cast<int>(bmp::BmpEncoding::Indexed8))) {
        indexed8->setChecked(true);
      }
      file_palette->setChecked(true);
      update_palette_state();
      const auto path = get_open_file_name(&dialog, QObject::tr("Choose Palette"), palette_path->text(),
                                           QObject::tr("Palette Files (*.bmp *.pal);;All Files (*.*)"), nullptr,
                                           QStringLiteral("bmpChoosePaletteFileDialog"));
      if (!path.isEmpty()) {
        palette_path->setText(path);
      }
    });
    update_palette_state();

    if (exec_dialog(dialog) != QDialog::Accepted) {
      return std::nullopt;
    }
    options.bmp_encoding = selected_encoding();
    options.bmp_palette_mode = selected_palette_mode();
    if (!bmp_encoding_is_indexed(options.bmp_encoding)) {
      options.bmp_palette_mode = bmp::BmpPaletteMode::Exact;
    } else if (!bmp_encoding_accepts_palette_file(options.bmp_encoding) &&
               options.bmp_palette_mode == bmp::BmpPaletteMode::PaletteFile) {
      options.bmp_palette_mode = bmp::BmpPaletteMode::Exact;
    }
    options.bmp_palette_path = palette_path->text().trimmed();
    if (scale_combo != nullptr) {
      options.export_scale = scale_combo->currentData().toInt();
      persist_export_scale(options.export_scale);
    }
    return options;
  }

  if (is_pdf_extension(extension)) {
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("pdfSaveOptionsDialog"));
    auto* content = create_options_dialog_chrome(dialog, QObject::tr("PDF Options"));
    auto* scale_combo = for_export ? add_export_scale_row(content, dialog) : nullptr;

    auto* lossless = new QCheckBox(QObject::tr("Lossless image data (larger file)"), &dialog);
    lossless->setObjectName(QStringLiteral("pdfLosslessCheck"));
    lossless->setChecked(options.pdf_lossless);
    // Qt's PDF engine offers no quality setting on its lossy path, so this really is one
    // choice: pixel-exact Flate, or Qt's fixed JPEG quality-94 encode.
    lossless->setToolTip(QObject::tr("Unchecked, the page is compressed as JPEG at Qt's fixed quality."));
    content->addWidget(lossless);

    // Editable layers (decided before this dialog by MainWindow::resolve_pdf_layer_choice:
    // the flatten-or-keep question or the remembered policy) trade fidelity for
    // structure, which the banner says out loud while the choice is on.
    auto* editable_warning = new QLabel(
        QObject::tr("Layers are kept as editable objects (paths, text, images). The PDF may not look exactly "
                    "like the canvas: blend modes, adjustment layers, group opacity, layer styles, and pixel "
                    "masks have no editable PDF form here and are flattened into images where needed."),
        &dialog);
    editable_warning->setObjectName(QStringLiteral("pdfEditableLayersWarning"));
    editable_warning->setWordWrap(true);
    editable_warning->setProperty("warningBanner", true);
    set_themed_style(*editable_warning,
                     QStringLiteral("QLabel#pdfEditableLayersWarning { background: @warning_banner_bg; border: "
                                    "1px solid @warning_banner_border; border-radius: 3px; "
                                    "color: @warning_banner_text; padding: 7px 9px; }"));
    content->addWidget(editable_warning);

    // Editable layers draw text whose font is missing in a substitute face so it stays
    // editable; this asks for the layer's pixels instead, which look right but are an image.
    auto* missing_fonts_as_images = new QCheckBox(
        QObject::tr("When a font is missing, export that text as an image instead of substituting a font"),
        &dialog);
    missing_fonts_as_images->setObjectName(QStringLiteral("pdfMissingFontsAsImagesCheck"));
    missing_fonts_as_images->setChecked(options.pdf_missing_fonts_as_images);
    missing_fonts_as_images->setToolTip(
        QObject::tr("Unchecked, text in a font that is not installed is written as editable text in a "
                    "substitute font, so it can look different from the canvas. Checked, that text is "
                    "written as an image of the layer's pixels instead."));
    content->addWidget(missing_fonts_as_images);

    auto* page_note = new QLabel(
        QObject::tr("The page is sized from the document's resolution, so it prints at the image's own size."),
        &dialog);
    page_note->setObjectName(QStringLiteral("pdfPageSizeNote"));
    page_note->setWordWrap(true);
    content->addWidget(page_note);
    add_dialog_buttons(content, dialog);

    const bool keep_layers = options.pdf_editable_layers;
    editable_warning->setVisible(keep_layers);
    missing_fonts_as_images->setVisible(keep_layers);
    if (scale_combo != nullptr) {
      // Vectors and text scale with the page; the nearest-neighbor pixel scale only
      // applies to the flattened image.
      scale_combo->setEnabled(!keep_layers);
    }

    if (exec_dialog(dialog) != QDialog::Accepted) {
      return std::nullopt;
    }
    options.pdf_lossless = lossless->isChecked();
    if (keep_layers) {
      options.pdf_missing_fonts_as_images = missing_fonts_as_images->isChecked();
    }
    if (scale_combo != nullptr) {
      options.export_scale = options.pdf_editable_layers ? 1 : scale_combo->currentData().toInt();
      if (!options.pdf_editable_layers) {
        persist_export_scale(options.export_scale);
      }
    }
    return options;
  }

  // Formats with no format-specific options still get the scale choice on export.
  if (for_export && !is_ico_extension(extension) && !is_cur_extension(extension)) {
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("exportScaleOptionsDialog"));
    auto* content = create_options_dialog_chrome(dialog, QObject::tr("Export Options"));
    auto* scale_combo = add_export_scale_row(content, dialog);
    add_dialog_buttons(content, dialog);
    if (exec_dialog(dialog) != QDialog::Accepted) {
      return std::nullopt;
    }
    options.export_scale = scale_combo->currentData().toInt();
    persist_export_scale(options.export_scale);
    return options;
  }

  return options;
}

std::optional<ImageSaveOptions> prompt_gif_save_options(QWidget* parent, ImageSaveOptions options,
                                                        bool offer_flatten_choice, bool for_export,
                                                        bool has_visible_frames) {
  options.export_scale = 1;
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("gifSaveOptionsDialog"));
  auto* content = create_options_dialog_chrome(
      dialog, offer_flatten_choice ? QObject::tr("GIF Options") : QObject::tr("Animated GIF Options"));
  dialog.resize(380, offer_flatten_choice ? 280 : 230);
  auto* scale_combo = for_export ? add_export_scale_row(content, dialog) : nullptr;

  QRadioButton* animation_radio = nullptr;
  QRadioButton* flatten_radio = nullptr;
  if (offer_flatten_choice) {
    animation_radio = new QRadioButton(QObject::tr("Animation from visible layers"), &dialog);
    animation_radio->setObjectName(QStringLiteral("gifAnimationRadio"));
    flatten_radio = new QRadioButton(QObject::tr("Single flattened image"), &dialog);
    flatten_radio->setObjectName(QStringLiteral("gifFlattenRadio"));
    auto* mode_group = new QButtonGroup(&dialog);
    mode_group->addButton(animation_radio);
    mode_group->addButton(flatten_radio);
    // Animation is the remembered default: an opened animated GIF re-saves as an
    // animation without extra choices.
    const auto stored_mode =
        app_settings().value(QStringLiteral("saveOptions/gifSaveMode"), QStringLiteral("animation")).toString();
    const bool animate = has_visible_frames && stored_mode != QStringLiteral("flatten");
    animation_radio->setChecked(animate);
    flatten_radio->setChecked(!animate);
    animation_radio->setEnabled(has_visible_frames);
    content->addWidget(animation_radio);
    content->addWidget(flatten_radio);
  }

  auto* delay_row = new QWidget(&dialog);
  auto* delay_layout = new QHBoxLayout(delay_row);
  delay_layout->setContentsMargins(0, 0, 0, 0);
  delay_layout->setSpacing(10);
  auto* delay_label = new QLabel(QObject::tr("Frame delay:"), delay_row);
  auto* delay_spin = new QDoubleSpinBox(delay_row);
  delay_spin->setObjectName(QStringLiteral("gifFrameDelaySpin"));
  delay_spin->setSuffix(QObject::tr(" s"));
  delay_spin->setRange(0.0, 655.35);  // the u16 centisecond wire range
  delay_spin->setDecimals(2);
  delay_spin->setSingleStep(0.05);
  delay_spin->setValue(std::clamp(options.gif_frame_delay_cs, 0, 0xffff) / 100.0);
  configure_dialog_spinbox(delay_spin, 96);
  delay_layout->addWidget(delay_label);
  delay_layout->addWidget(delay_spin);
  delay_layout->addStretch(1);
  content->addWidget(delay_row);

  auto* explanation = new QLabel(
      QObject::tr("Each visible top-level layer becomes one frame, with the top layer first. Hidden layers "
                  "are skipped. A layer name ending in a time, like \"blink 0.25s\", overrides the default "
                  "delay for that frame. The animation loops forever."),
      &dialog);
  explanation->setObjectName(QStringLiteral("gifAnimationExplanationLabel"));
  explanation->setWordWrap(true);
  content->addWidget(explanation);
  add_dialog_buttons(content, dialog);

  const auto sync_animation_controls = [delay_row, explanation, animation_radio] {
    const bool animate = animation_radio == nullptr || animation_radio->isChecked();
    delay_row->setEnabled(animate);
    explanation->setEnabled(animate);
  };
  if (animation_radio != nullptr) {
    QObject::connect(animation_radio, &QRadioButton::toggled, &dialog, sync_animation_controls);
  }
  sync_animation_controls();

  if (exec_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }
  options.gif_animate = animation_radio == nullptr || animation_radio->isChecked();
  options.gif_frame_delay_cs =
      static_cast<int>(std::clamp<long long>(std::llround(delay_spin->value() * 100.0), 0, 0xffff));
  auto settings = app_settings();
  settings.setValue(QStringLiteral("saveOptions/gifFrameDelayCs"), options.gif_frame_delay_cs);
  if (offer_flatten_choice && has_visible_frames) {
    // Only the Save As / Export form remembers the mode, and only when the choice was
    // real: the Export Layers as Animated GIF action is always an animation, and a
    // forced flatten (nothing visible) is not the user's preference.
    settings.setValue(QStringLiteral("saveOptions/gifSaveMode"),
                      options.gif_animate ? QStringLiteral("animation") : QStringLiteral("flatten"));
  }
  if (scale_combo != nullptr) {
    options.export_scale = scale_combo->currentData().toInt();
    persist_export_scale(options.export_scale);
  }
  return options;
}

}  // namespace patchy::ui
