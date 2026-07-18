#include "ui/sprite_sheet_dialog.hpp"

#include "ui/dialog_utils.hpp"
#include "ui/image_document_io.hpp"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

QVBoxLayout* create_sheet_dialog_chrome(QDialog& dialog, const QString& title) {
  auto* root = new QVBoxLayout(&dialog);
  auto* content = install_dark_dialog_chrome(dialog, root, title);
  content->setSpacing(10);
  dialog.resize(340, 200);
  return content;
}

QDialogButtonBox* add_sheet_dialog_buttons(QVBoxLayout* content, QDialog& dialog) {
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  content->addWidget(buttons);
  return buttons;
}

}  // namespace

std::optional<SpriteSheetExportOptions> prompt_sprite_sheet_export_options(QWidget* parent, int frame_count) {
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("spriteSheetExportDialog"));
  auto* content = create_sheet_dialog_chrome(dialog, QObject::tr("Export Sprite Sheet"));

  auto* form = new QFormLayout();
  form->setContentsMargins(0, 0, 0, 0);
  form->setHorizontalSpacing(10);
  form->setVerticalSpacing(8);

  auto* info = new QLabel(QObject::tr("%1 frames, one per visible top-level layer").arg(frame_count), &dialog);
  info->setWordWrap(true);
  form->addRow(info);

  auto* columns = new QSpinBox(&dialog);
  columns->setObjectName(QStringLiteral("spriteSheetColumnsSpin"));
  columns->setRange(1, 64);
  columns->setValue(std::clamp(static_cast<int>(std::ceil(std::sqrt(std::max(1, frame_count)))), 1, 64));
  configure_dialog_spinbox(columns, 88);
  form->addRow(new QLabel(QObject::tr("Columns:"), &dialog), columns);

  auto* padding = new QSpinBox(&dialog);
  padding->setObjectName(QStringLiteral("spriteSheetPaddingSpin"));
  padding->setRange(0, 64);
  padding->setSuffix(QStringLiteral(" px"));
  configure_dialog_spinbox(padding, 88);
  form->addRow(new QLabel(QObject::tr("Padding:"), &dialog), padding);

  auto* transparent = new QCheckBox(QObject::tr("Transparent background"), &dialog);
  transparent->setObjectName(QStringLiteral("spriteSheetTransparentCheck"));
  transparent->setChecked(true);
  form->addRow(transparent);

  content->addLayout(form);
  add_sheet_dialog_buttons(content, dialog);

  if (exec_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }
  SpriteSheetExportOptions options;
  options.columns = columns->value();
  options.padding = padding->value();
  options.transparent_background = transparent->isChecked();
  return options;
}

std::optional<SpriteSheetImportOptions> prompt_sprite_sheet_import_options(QWidget* parent, QSize image_size) {
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("spriteSheetImportDialog"));
  auto* content = create_sheet_dialog_chrome(dialog, QObject::tr("Sprite Sheet to Layers"));

  auto* form = new QFormLayout();
  form->setContentsMargins(0, 0, 0, 0);
  form->setHorizontalSpacing(10);
  form->setVerticalSpacing(8);

  const auto make_spin = [&dialog](const char* name, int minimum, int maximum, int value) {
    auto* spin = new QSpinBox(&dialog);
    spin->setObjectName(QLatin1String(name));
    spin->setRange(minimum, maximum);
    spin->setValue(value);
    configure_dialog_spinbox(spin, 88);
    return spin;
  };
  auto* cell_width = make_spin("spriteSheetCellWidthSpin", 1, std::max(1, image_size.width()),
                               std::min(32, std::max(1, image_size.width())));
  auto* cell_height = make_spin("spriteSheetCellHeightSpin", 1, std::max(1, image_size.height()),
                                std::min(32, std::max(1, image_size.height())));
  auto* margin = make_spin("spriteSheetMarginSpin", 0, 1024, 0);
  auto* spacing = make_spin("spriteSheetSpacingSpin", 0, 1024, 0);
  form->addRow(new QLabel(QObject::tr("Cell width:"), &dialog), cell_width);
  form->addRow(new QLabel(QObject::tr("Cell height:"), &dialog), cell_height);
  form->addRow(new QLabel(QObject::tr("Margin:"), &dialog), margin);
  form->addRow(new QLabel(QObject::tr("Spacing:"), &dialog), spacing);

  auto* count_label = new QLabel(&dialog);
  count_label->setObjectName(QStringLiteral("spriteSheetCountLabel"));
  form->addRow(count_label);
  content->addLayout(form);
  auto* buttons = add_sheet_dialog_buttons(content, dialog);
  auto* ok_button = buttons->button(QDialogButtonBox::Ok);

  const auto update_count = [=] {
    const auto usable_w = image_size.width() - 2 * margin->value() + spacing->value();
    const auto usable_h = image_size.height() - 2 * margin->value() + spacing->value();
    const auto step_w = cell_width->value() + spacing->value();
    const auto step_h = cell_height->value() + spacing->value();
    const auto cols = step_w > 0 ? std::max(0, usable_w / step_w) : 0;
    const auto rows = step_h > 0 ? std::max(0, usable_h / step_h) : 0;
    count_label->setText(QObject::tr("%1 x %2 = %3 cells").arg(cols).arg(rows).arg(cols * rows));
    if (ok_button != nullptr) {
      ok_button->setEnabled(cols > 0 && rows > 0);
    }
  };
  for (auto* spin : {cell_width, cell_height, margin, spacing}) {
    QObject::connect(spin, &QSpinBox::valueChanged, &dialog, [update_count](int) { update_count(); });
  }
  update_count();

  if (exec_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }
  SpriteSheetImportOptions options;
  options.cell_width = cell_width->value();
  options.cell_height = cell_height->value();
  options.margin = margin->value();
  options.spacing = spacing->value();
  return options;
}

QImage compose_sprite_sheet(const Document& document, const SpriteSheetExportOptions& options) {
  std::vector<const Layer*> frames;
  for (const auto& layer : std::as_const(document).layers()) {
    if (layer.visible()) {
      frames.push_back(&layer);
    }
  }
  if (frames.empty()) {
    return {};
  }
  const auto cell_w = document.width();
  const auto cell_h = document.height();
  const auto columns = std::clamp<int>(options.columns, 1, static_cast<int>(frames.size()));
  const auto rows = (static_cast<int>(frames.size()) + columns - 1) / columns;
  const auto pad = std::max(0, options.padding);
  QImage sheet(columns * cell_w + (columns + 1) * pad, rows * cell_h + (rows + 1) * pad, QImage::Format_RGBA8888);
  sheet.fill(options.transparent_background ? Qt::transparent : Qt::white);
  QPainter painter(&sheet);
  for (std::size_t index = 0; index < frames.size(); ++index) {
    const auto frame_image = render_layer_isolated(document, *frames[index]);
    const auto column = static_cast<int>(index) % columns;
    const auto row = static_cast<int>(index) / columns;
    painter.drawImage(pad + column * (cell_w + pad), pad + row * (cell_h + pad), frame_image);
  }
  return sheet;
}

std::optional<Document> slice_sprite_sheet(const QImage& sheet, const SpriteSheetImportOptions& options,
                                           const QString& frame_name_format) {
  if (sheet.isNull() || options.cell_width <= 0 || options.cell_height <= 0) {
    return std::nullopt;
  }
  const auto converted = sheet.convertToFormat(QImage::Format_RGBA8888);
  Document sliced(options.cell_width, options.cell_height, PixelFormat::rgba8());
  int frame = 0;
  for (int y = options.margin; y + options.cell_height <= converted.height();
       y += options.cell_height + options.spacing) {
    for (int x = options.margin; x + options.cell_width <= converted.width();
         x += options.cell_width + options.spacing) {
      const auto cell = converted.copy(x, y, options.cell_width, options.cell_height);
      bool any_visible_pixel = false;
      for (int cy = 0; cy < cell.height() && !any_visible_pixel; ++cy) {
        const auto* scan = cell.constScanLine(cy);
        for (int cx = 0; cx < cell.width(); ++cx) {
          if (scan[cx * 4 + 3] != 0) {
            any_visible_pixel = true;
            break;
          }
        }
      }
      if (!any_visible_pixel) {
        continue;  // fully transparent cells are dead sheet space, not frames
      }
      ++frame;
      Layer layer(sliced.allocate_layer_id(), frame_name_format.arg(frame).toStdString(),
                  pixels_from_image_rgba(cell));
      layer.set_visible(frame == 1);
      sliced.add_layer(std::move(layer));
    }
  }
  if (frame == 0) {
    return std::nullopt;
  }
  return sliced;
}

}  // namespace patchy::ui
