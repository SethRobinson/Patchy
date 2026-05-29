#include "ui/image_save_options_dialog.hpp"

#include "ui/dialog_utils.hpp"

#include <QButtonGroup>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
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

}  // namespace

bool image_save_options_apply_to_extension(const QString& extension) {
  return is_jpeg_extension(extension) || is_bmp_extension(extension);
}

ImageSaveOptions load_image_save_option_defaults() {
  QSettings settings(QStringLiteral("Patchy"), QStringLiteral("Patchy"));
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
  return options;
}

void save_image_save_option_defaults(const ImageSaveOptions& options) {
  QSettings settings(QStringLiteral("Patchy"), QStringLiteral("Patchy"));
  settings.setValue(QStringLiteral("saveOptions/jpegQuality"), std::clamp(options.jpeg_quality, 0, 100));
  settings.setValue(QStringLiteral("saveOptions/bmpEncoding"), bmp_encoding_key(options.bmp_encoding));
  settings.setValue(QStringLiteral("saveOptions/bmpPaletteMode"), bmp_palette_mode_key(options.bmp_palette_mode));
  if (!options.bmp_palette_path.isEmpty()) {
    settings.setValue(QStringLiteral("saveOptions/bmpPalettePath"), options.bmp_palette_path);
  }
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
    dialog.resize(420, 360);

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
      const auto path = QFileDialog::getOpenFileName(&dialog, QObject::tr("Choose Palette"), palette_path->text(),
                                                     QObject::tr("Palette Files (*.bmp *.pal);;All Files (*.*)"));
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
    return options;
  }

  return options;
}

}  // namespace patchy::ui
