#include "ui/palette_convert_dialog.hpp"

#include "core/palette_presets.hpp"
#include "formats/palette_io.hpp"
#include "ui/dialog_utils.hpp"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QPixmap>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <filesystem>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

// Fixed source rows before the presets; presets follow, "From file..." is last.
enum SourceRow : int {
  kSourceOptimized = 0,
  kSourceExact = 1,
  kSourceCurrent = 2,
  kSourceFirstPreset = 3,
};

[[nodiscard]] QImage qimage_from_rgb8(const PixelBuffer& pixels) {
  QImage image(pixels.width(), pixels.height(), QImage::Format_RGB32);
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto* px = pixels.pixel(x, y);
      image.setPixel(x, y, qRgb(px[0], px[1], px[2]));
    }
  }
  return image;
}

}  // namespace

std::optional<PaletteConvertSettings> request_palette_convert_settings(
    QWidget* parent, const PixelBuffer& flattened_rgb8, const std::optional<Palette>& current_palette) {
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("paletteConvertDialog"));
  dialog.setWindowTitle(QObject::tr("Convert to Indexed (Palette)"));
  auto* layout = new QVBoxLayout(&dialog);

  auto* form = new QFormLayout();
  form->setHorizontalSpacing(10);
  form->setVerticalSpacing(6);
  auto* source_combo = new QComboBox(&dialog);
  source_combo->setObjectName(QStringLiteral("paletteConvertSourceCombo"));
  source_combo->addItem(QObject::tr("Optimized (median cut)"));
  source_combo->addItem(QObject::tr("Exact image colors"));
  source_combo->addItem(QObject::tr("Current palette"));
  const auto presets = builtin_palette_presets();
  for (const auto& preset : presets) {
    source_combo->addItem(QObject::tr(preset.english_name));
  }
  source_combo->addItem(QObject::tr("From file..."));
  const auto file_row = source_combo->count() - 1;
  if (!current_palette.has_value()) {
    // Keep row indices stable; just disable the row when nothing is attached.
    if (auto* model = qobject_cast<QStandardItemModel*>(source_combo->model()); model != nullptr) {
      model->item(kSourceCurrent)->setEnabled(false);
    }
  }
  form->addRow(QObject::tr("Palette:"), source_combo);

  auto* colors_spin = new QSpinBox(&dialog);
  colors_spin->setObjectName(QStringLiteral("paletteConvertColorsSpin"));
  colors_spin->setRange(2, 256);
  colors_spin->setValue(16);
  form->addRow(QObject::tr("Colors:"), colors_spin);

  auto* dither_combo = new QComboBox(&dialog);
  dither_combo->setObjectName(QStringLiteral("paletteConvertDitherCombo"));
  dither_combo->addItem(QObject::tr("None"), static_cast<int>(PaletteDither::None));
  dither_combo->addItem(QObject::tr("Floyd-Steinberg"), static_cast<int>(PaletteDither::FloydSteinberg));
  dither_combo->addItem(QObject::tr("Ordered 4x4"), static_cast<int>(PaletteDither::OrderedBayer4x4));
  dither_combo->addItem(QObject::tr("Ordered 8x8"), static_cast<int>(PaletteDither::OrderedBayer8x8));
  form->addRow(QObject::tr("Dither:"), dither_combo);

  auto* alpha_spin = new QSpinBox(&dialog);
  alpha_spin->setObjectName(QStringLiteral("paletteConvertAlphaSpin"));
  alpha_spin->setRange(1, 255);
  alpha_spin->setValue(128);
  alpha_spin->setToolTip(QObject::tr("Pixels with alpha below this become fully transparent; the rest become opaque"));
  form->addRow(QObject::tr("Alpha threshold:"), alpha_spin);
  layout->addLayout(form);

  auto* preview_label = new QLabel(&dialog);
  preview_label->setObjectName(QStringLiteral("paletteConvertPreview"));
  preview_label->setMinimumSize(320, 220);
  preview_label->setAlignment(Qt::AlignCenter);
  layout->addWidget(preview_label, 1);
  auto* preview_info = new QLabel(&dialog);
  preview_info->setObjectName(QStringLiteral("paletteConvertPreviewInfo"));
  layout->addWidget(preview_info);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget(buttons);

  std::optional<Palette> file_palette;
  QString file_name;

  // Resolves the palette for the current control state; empty palette = invalid.
  const auto resolve_palette = [&]() -> Palette {
    const auto row = source_combo->currentIndex();
    if (row == kSourceOptimized) {
      return quantize_to_palette(flattened_rgb8, static_cast<std::size_t>(colors_spin->value()), 0);
    }
    if (row == kSourceExact) {
      auto exact = exact_palette_from_pixels(flattened_rgb8, 256, 0);
      return exact.has_value() ? std::move(*exact) : Palette{};
    }
    if (row == kSourceCurrent) {
      return current_palette.value_or(Palette{});
    }
    if (row == file_row) {
      return file_palette.value_or(Palette{});
    }
    const auto preset_index = row - kSourceFirstPreset;
    if (preset_index >= 0 && preset_index < static_cast<int>(presets.size())) {
      const auto& preset = presets[static_cast<std::size_t>(preset_index)];
      return Palette{{preset.colors.begin(), preset.colors.end()}};
    }
    return {};
  };

  const auto downscaled_source = [&flattened_rgb8]() {
    // Preview on a bounded copy so 4k documents re-quantize instantly.
    auto image = qimage_from_rgb8(flattened_rgb8);
    if (image.width() > 480 || image.height() > 320) {
      image = image.scaled(480, 320, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return image;
  }();

  auto* preview_timer = new QTimer(&dialog);
  preview_timer->setSingleShot(true);
  preview_timer->setInterval(180);
  const auto refresh_preview = [&, preview_label, preview_info] {
    const auto palette = resolve_palette();
    if (palette.colors.empty()) {
      preview_label->setPixmap(QPixmap());
      preview_info->setText(source_combo->currentIndex() == kSourceExact
                                ? QObject::tr("The image has more than 256 colors; choose Optimized instead.")
                                : QObject::tr("Choose a palette."));
      return;
    }
    PaletteLut lut;
    lut.build(palette.colors);
    PixelBuffer preview_pixels(downscaled_source.width(), downscaled_source.height(), PixelFormat::rgb8());
    for (std::int32_t y = 0; y < preview_pixels.height(); ++y) {
      for (std::int32_t x = 0; x < preview_pixels.width(); ++x) {
        const auto rgb = downscaled_source.pixel(x, y);
        auto* px = preview_pixels.pixel(x, y);
        px[0] = static_cast<std::uint8_t>(qRed(rgb));
        px[1] = static_cast<std::uint8_t>(qGreen(rgb));
        px[2] = static_cast<std::uint8_t>(qBlue(rgb));
      }
    }
    const auto dither = static_cast<PaletteDither>(dither_combo->currentData().toInt());
    (void)apply_palette_to_pixels(preview_pixels, lut, dither,
                                  static_cast<std::uint8_t>(alpha_spin->value()));
    auto preview_image = qimage_from_rgb8(preview_pixels);
    preview_label->setPixmap(QPixmap::fromImage(preview_image.scaled(
        preview_label->size(), Qt::KeepAspectRatio,
        preview_image.width() * 2 <= preview_label->width() ? Qt::FastTransformation : Qt::SmoothTransformation)));
    preview_info->setText(QObject::tr("%n color(s)", nullptr, static_cast<int>(palette.colors.size())));
  };
  QObject::connect(preview_timer, &QTimer::timeout, &dialog, refresh_preview);
  const auto schedule_preview = [preview_timer] { preview_timer->start(); };

  QObject::connect(source_combo, &QComboBox::activated, &dialog, [&, file_row](int row) {
    colors_spin->setEnabled(row == kSourceOptimized);
    if (row == file_row) {
      const auto path = QFileDialog::getOpenFileName(
          &dialog, QObject::tr("Load Palette"), QString(),
          QObject::tr("Palette Files (*.pal *.gpl *.hex *.act *.aco *.ase *.bmp);;All Files (*)"));
      if (!path.isEmpty()) {
        try {
          auto data = palette_io::read_palette_bytes([&path] {
            std::vector<std::uint8_t> bytes;
            QFile file(path);
            if (file.open(QIODevice::ReadOnly)) {
              const auto blob = file.readAll();
              bytes.assign(blob.begin(), blob.end());
            }
            return bytes;
          }());
          file_palette = Palette{std::move(data.colors)};
          file_name = QFileInfo(path).fileName();
        } catch (const std::exception& error) {
          QMessageBox::warning(&dialog, QObject::tr("Load Palette"),
                               QObject::tr("Could not load the palette file.\n%1")
                                   .arg(QString::fromUtf8(error.what())));
          file_palette.reset();
        }
      }
      source_combo->setItemText(file_row, file_palette.has_value()
                                              ? QObject::tr("From file: %1").arg(file_name)
                                              : QObject::tr("From file..."));
    }
    schedule_preview();
  });
  QObject::connect(colors_spin, &QSpinBox::valueChanged, &dialog, [&](int) { schedule_preview(); });
  QObject::connect(dither_combo, &QComboBox::currentIndexChanged, &dialog, [&](int) { schedule_preview(); });
  QObject::connect(alpha_spin, &QSpinBox::valueChanged, &dialog, [&](int) { schedule_preview(); });

  colors_spin->setEnabled(true);
  // Keep readable - / + buttons on the spin boxes (sub-control gotcha: apply the
  // style after all children exist, with unprefixed selectors; see dialog_utils).
  dialog.setStyleSheet(dialog.styleSheet() + dialog_spinbox_button_style());
  refresh_preview();

  if (exec_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }
  auto palette = resolve_palette();
  if (palette.colors.empty()) {
    return std::nullopt;
  }
  PaletteConvertSettings settings;
  settings.palette = std::move(palette);
  settings.dither = static_cast<PaletteDither>(dither_combo->currentData().toInt());
  settings.alpha_threshold = alpha_spin->value();
  return settings;
}

}  // namespace patchy::ui
