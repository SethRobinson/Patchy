#include "ui/image_save_options_dialog.hpp"

#include "ui/app_settings.hpp"

#include "ui/dialog_utils.hpp"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
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
#include <array>
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
         is_cur_extension(extension);
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
    if (scale_combo != nullptr) {
      options.export_scale = scale_combo->currentData().toInt();
      persist_export_scale(options.export_scale);
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

}  // namespace patchy::ui
