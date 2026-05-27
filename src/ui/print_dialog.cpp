#include "ui/print_dialog.hpp"

#include "ui/dialog_utils.hpp"
#include "ui/image_document_io.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMarginsF>
#include <QMessageBox>
#include <QPainter>
#include <QPageSetupDialog>
#include <QPageSize>
#include <QPrinter>
#include <QPrinterInfo>
#include <QPushButton>
#include <QSizePolicy>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>

namespace photoslop::ui {

namespace {

constexpr double kPointsPerInch = 72.0;

double sanitized_ppi(double value) noexcept {
  return std::isfinite(value) && value > 0.0 ? value : 300.0;
}

double sanitized_scale_percent(double value) noexcept {
  return std::clamp(std::isfinite(value) ? value : 100.0, 1.0, 1000.0);
}

double settings_print_ppi(const Document& document, const PrintSettings& settings) noexcept {
  return sanitized_ppi(settings.print_resolution_ppi > 0.0 ? settings.print_resolution_ppi
                                                           : document.print_settings().horizontal_ppi);
}

QRect document_rect(const Document& document) {
  return QRect(0, 0, std::max<std::int32_t>(0, document.width()), std::max<std::int32_t>(0, document.height()));
}

QRect source_rect_for_settings(const Document& document, const PrintSettings& settings) {
  const auto full = document_rect(document);
  if (settings.area_mode != PrintAreaMode::Selection || settings.selection_bounds.isEmpty()) {
    return full;
  }
  const auto selected = settings.selection_bounds.normalized().intersected(full);
  return selected.isEmpty() ? full : selected;
}

QPageLayout valid_page_layout(QPageLayout page_layout) {
  if (page_layout.isValid()) {
    return page_layout;
  }
  return default_print_page_layout();
}

void configure_printer(QPrinter& printer, const QPageLayout& page_layout, const QString& document_name) {
  printer.setDocName(document_name.isEmpty() ? QObject::tr("Photoslop Document") : document_name);
  printer.setCreator(QStringLiteral("Photoslop"));
  printer.setColorMode(QPrinter::Color);
  printer.setPageLayout(valid_page_layout(page_layout));
}

QString selected_printer_name(const QComboBox* printer_combo) {
  if (printer_combo == nullptr || !printer_combo->isEnabled()) {
    return {};
  }
  return printer_combo->currentData().toString();
}

std::unique_ptr<QPrinter> create_selected_printer(const QString& printer_name) {
  if (!printer_name.isEmpty()) {
    const auto info = QPrinterInfo::printerInfo(printer_name);
    if (!info.isNull()) {
      return std::make_unique<QPrinter>(info, QPrinter::HighResolution);
    }
  }
  return std::make_unique<QPrinter>(QPrinter::HighResolution);
}

void configure_selected_printer(QPrinter& printer, const QString& printer_name, const QPageLayout& page_layout,
                                const QString& document_name) {
  if (!printer_name.isEmpty()) {
    printer.setPrinterName(printer_name);
  }
  configure_printer(printer, page_layout, document_name);
}

QString printer_display_name(const QPrinterInfo& info) {
  auto name = info.printerName();
  if (name.isEmpty()) {
    name = QObject::tr("Unnamed Printer");
  }
  if (info.isDefault()) {
    name += QObject::tr(" (Default)");
  }
  return name;
}

void populate_printer_combo(QComboBox* printer_combo) {
  if (printer_combo == nullptr) {
    return;
  }
  const auto printers = QPrinterInfo::availablePrinters();
  if (printers.isEmpty()) {
    printer_combo->addItem(QObject::tr("No printers installed"), QString());
    printer_combo->setEnabled(false);
    return;
  }

  int default_index = 0;
  for (const auto& info : printers) {
    const auto index = printer_combo->count();
    printer_combo->addItem(printer_display_name(info), info.printerName());
    if (info.isDefault()) {
      default_index = index;
    }
  }
  printer_combo->setCurrentIndex(default_index);
}

bool paint_printer_page(QPrinter& printer, const Document& document, const PrintSettings& settings) {
  QPainter painter(&printer);
  if (!painter.isActive()) {
    return false;
  }

  const auto layout = valid_page_layout(printer.pageLayout());
  const auto full_points = layout.fullRect(QPageLayout::Point).toAlignedRect();
  const auto full_pixels = layout.fullRectPixels(std::max(1, printer.resolution()));
  if (!full_points.isEmpty() && !full_pixels.isEmpty()) {
    painter.setWindow(full_points);
    painter.setViewport(full_pixels);
  }
  render_print_page(painter, document, settings, layout);
  return painter.end();
}

void draw_crop_marks(QPainter& painter, const QRectF& target) {
  constexpr double kLength = 18.0;
  constexpr double kGap = 5.0;
  const auto left = target.left();
  const auto right = target.right();
  const auto top = target.top();
  const auto bottom = target.bottom();

  painter.save();
  QPen pen(QColor(20, 20, 20), 0.0);
  pen.setCosmetic(true);
  painter.setPen(pen);
  painter.drawLine(QPointF(left - kGap - kLength, top), QPointF(left - kGap, top));
  painter.drawLine(QPointF(left, top - kGap - kLength), QPointF(left, top - kGap));
  painter.drawLine(QPointF(right + kGap, top), QPointF(right + kGap + kLength, top));
  painter.drawLine(QPointF(right, top - kGap - kLength), QPointF(right, top - kGap));
  painter.drawLine(QPointF(left - kGap - kLength, bottom), QPointF(left - kGap, bottom));
  painter.drawLine(QPointF(left, bottom + kGap), QPointF(left, bottom + kGap + kLength));
  painter.drawLine(QPointF(right + kGap, bottom), QPointF(right + kGap + kLength, bottom));
  painter.drawLine(QPointF(right, bottom + kGap), QPointF(right, bottom + kGap + kLength));
  painter.restore();
}

class PrintPreviewPane final : public QWidget {
public:
  explicit PrintPreviewPane(QWidget* parent = nullptr) : QWidget(parent) {
    setObjectName(QStringLiteral("printPreviewPane"));
    setMinimumSize(260, 320);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  }

  void set_state(const Document* document, const PrintSettings* settings, const QPageLayout* page_layout) noexcept {
    document_ = document;
    settings_ = settings;
    page_layout_ = page_layout;
    update();
  }

protected:
  void paintEvent(QPaintEvent* /*event*/) override {
    QPainter painter(this);
    painter.fillRect(rect(), QColor(36, 36, 36));
    if (document_ == nullptr || settings_ == nullptr || page_layout_ == nullptr) {
      return;
    }

    const auto layout = valid_page_layout(*page_layout_);
    const auto page_points = layout.fullRect(QPageLayout::Point);
    if (page_points.isEmpty()) {
      return;
    }

    const auto available = rect().adjusted(16, 16, -16, -16);
    const auto scale = std::min(static_cast<double>(available.width()) / page_points.width(),
                                static_cast<double>(available.height()) / page_points.height());
    const QSizeF page_size(page_points.width() * scale, page_points.height() * scale);
    const QPointF origin(available.x() + (available.width() - page_size.width()) / 2.0,
                         available.y() + (available.height() - page_size.height()) / 2.0);

    painter.save();
    painter.translate(origin);
    painter.scale(scale, scale);
    render_print_page(painter, *document_, *settings_, layout);
    painter.restore();

    painter.setPen(QPen(QColor(18, 18, 18), 1));
    painter.drawRect(QRectF(origin, page_size).adjusted(0.5, 0.5, -0.5, -0.5));
  }

private:
  const Document* document_{nullptr};
  const PrintSettings* settings_{nullptr};
  const QPageLayout* page_layout_{nullptr};
};

QString print_size_text(const Document& document, const PrintSettings& settings, const QPageLayout& page_layout) {
  const auto placement = calculate_print_placement(document, settings, page_layout);
  return QObject::tr("%1 in x %2 in at %3%")
      .arg(placement.print_size_inches.width(), 0, 'f', 2)
      .arg(placement.print_size_inches.height(), 0, 'f', 2)
      .arg(placement.scale_percent, 0, 'f', 1);
}

double unit_factor_from_inches(const QComboBox* units) {
  if (units == nullptr) {
    return 1.0;
  }
  const auto unit = units->currentData().toString();
  if (unit == QStringLiteral("cm")) {
    return 2.54;
  }
  if (unit == QStringLiteral("mm")) {
    return 25.4;
  }
  return 1.0;
}

QString unit_suffix(const QComboBox* units) {
  return QStringLiteral(" ") + (units == nullptr ? QStringLiteral("in") : units->currentData().toString());
}

QString formatted_size(QSizeF inches, const QComboBox* units) {
  const auto factor = unit_factor_from_inches(units);
  const auto suffix = unit_suffix(units).trimmed();
  return QObject::tr("%1 x %2 %3")
      .arg(inches.width() * factor, 0, 'f', 2)
      .arg(inches.height() * factor, 0, 'f', 2)
      .arg(suffix);
}

}  // namespace

QPageLayout default_print_page_layout() {
  return QPageLayout(QPageSize(QPageSize::Letter), QPageLayout::Portrait, QMarginsF(0.5, 0.5, 0.5, 0.5),
                     QPageLayout::Inch);
}

PrintSettings default_print_settings(const Document& document, std::optional<QRect> selection_bounds) {
  PrintSettings settings;
  settings.print_resolution_ppi = sanitized_ppi(document.print_settings().horizontal_ppi);
  if (selection_bounds.has_value()) {
    settings.selection_bounds = selection_bounds->normalized().intersected(document_rect(document));
  }
  return settings;
}

PrintPlacement calculate_print_placement(const Document& document, const PrintSettings& settings,
                                         const QPageLayout& page_layout) {
  const auto layout = valid_page_layout(page_layout);
  const auto source = source_rect_for_settings(document, settings);
  const auto print_ppi = settings_print_ppi(document, settings);
  const QSizeF actual_points(static_cast<double>(source.width()) / print_ppi * kPointsPerInch,
                             static_cast<double>(source.height()) / print_ppi * kPointsPerInch);

  QSizeF target_size = actual_points;
  double effective_scale = 100.0;
  const auto printable = layout.paintRect(QPageLayout::Point);
  if (settings.scale_mode == PrintScaleMode::FitToPage && actual_points.width() > 0.0 &&
      actual_points.height() > 0.0 && printable.width() > 0.0 && printable.height() > 0.0) {
    const auto fit = std::min(printable.width() / actual_points.width(), printable.height() / actual_points.height());
    target_size = QSizeF(actual_points.width() * fit, actual_points.height() * fit);
    effective_scale = fit * 100.0;
  } else if (settings.scale_mode == PrintScaleMode::CustomScale) {
    effective_scale = sanitized_scale_percent(settings.scale_percent);
    target_size = QSizeF(actual_points.width() * (effective_scale / 100.0),
                         actual_points.height() * (effective_scale / 100.0));
  }

  QPointF origin;
  if (settings.center) {
    origin = QPointF(printable.left() + (printable.width() - target_size.width()) / 2.0,
                     printable.top() + (printable.height() - target_size.height()) / 2.0);
  } else {
    origin = QPointF(printable.left() + settings.offset_x_inches * kPointsPerInch,
                     printable.top() + settings.offset_y_inches * kPointsPerInch);
  }

  return PrintPlacement{source, QRectF(origin, target_size), effective_scale,
                        QSizeF(target_size.width() / kPointsPerInch, target_size.height() / kPointsPerInch)};
}

void render_print_page(QPainter& painter, const Document& document, const PrintSettings& settings,
                       const QPageLayout& page_layout) {
  const auto layout = valid_page_layout(page_layout);
  const auto page = layout.fullRect(QPageLayout::Point);
  const auto printable = layout.paintRect(QPageLayout::Point);
  painter.save();
  painter.fillRect(page, Qt::white);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

  const auto placement = calculate_print_placement(document, settings, layout);
  const auto image = qimage_from_document_rect(document, placement.source_rect, false);
  if (!image.isNull()) {
    painter.drawImage(placement.target_rect_points, image, QRectF(0, 0, image.width(), image.height()));
  }
  if (settings.crop_marks) {
    draw_crop_marks(painter, placement.target_rect_points);
  }

  QPen printable_pen(QColor(205, 205, 205), 0.0, Qt::DashLine);
  printable_pen.setCosmetic(true);
  painter.setPen(printable_pen);
  painter.setBrush(Qt::NoBrush);
  painter.drawRect(printable);
  painter.restore();
}

bool write_print_pdf(const QString& path, const Document& document, const PrintSettings& settings,
                     const QPageLayout& page_layout) {
  if (path.isEmpty()) {
    return false;
  }
  QPrinter printer(QPrinter::HighResolution);
  printer.setOutputFormat(QPrinter::PdfFormat);
  printer.setOutputFileName(path);
  configure_printer(printer, page_layout, QObject::tr("Photoslop Print"));
  return paint_printer_page(printer, document, settings);
}

void run_page_setup_dialog(QWidget* parent, QPageLayout* page_layout) {
  QPrinter printer(QPrinter::HighResolution);
  configure_printer(printer, page_layout != nullptr ? *page_layout : default_print_page_layout(),
                    QObject::tr("Photoslop Print"));
  QPageSetupDialog dialog(&printer, parent);
  if (exec_dialog(dialog) == QDialog::Accepted && page_layout != nullptr) {
    *page_layout = printer.pageLayout();
  }
}

bool run_print_dialog(QWidget* parent, const Document& document, std::optional<QRect> selection_bounds,
                      QPageLayout* page_layout) {
  auto settings = default_print_settings(document, selection_bounds);
  settings.scale_mode = PrintScaleMode::FitToPage;
  auto current_layout = valid_page_layout(page_layout != nullptr ? *page_layout : QPageLayout{});

  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("photoslopPrintDialog"));
  dialog.setWindowTitle(QObject::tr("Print"));
  dialog.resize(760, 520);

  auto* root = new QHBoxLayout(&dialog);
  root->setContentsMargins(12, 12, 12, 12);
  root->setSpacing(14);

  auto* preview = new PrintPreviewPane(&dialog);
  preview->set_state(&document, &settings, &current_layout);
  root->addWidget(preview, 1);

  auto* side = new QWidget(&dialog);
  auto* side_layout = new QVBoxLayout(side);
  side_layout->setContentsMargins(0, 0, 0, 0);
  side_layout->setSpacing(10);
  root->addWidget(side, 0);

  auto* output_group = new QGroupBox(QObject::tr("Output"), side);
  auto* output_layout = new QVBoxLayout(output_group);
  auto* printer_form = new QFormLayout();
  auto* printer_combo = new QComboBox(output_group);
  printer_combo->setObjectName(QStringLiteral("printPrinterCombo"));
  populate_printer_combo(printer_combo);
  printer_form->addRow(QObject::tr("Printer"), printer_combo);
  output_layout->addLayout(printer_form);
  auto* page_setup = new QPushButton(QObject::tr("Page Setup..."), output_group);
  page_setup->setObjectName(QStringLiteral("printPageSetupButton"));
  output_layout->addWidget(page_setup);
  side_layout->addWidget(output_group);

  auto* settings_group = new QGroupBox(QObject::tr("Position and Size"), side);
  auto* form = new QFormLayout(settings_group);
  auto* area = new QComboBox(settings_group);
  area->setObjectName(QStringLiteral("printAreaCombo"));
  area->addItem(QObject::tr("Document"), static_cast<int>(PrintAreaMode::Document));
  if (selection_bounds.has_value() && !selection_bounds->isEmpty()) {
    area->addItem(QObject::tr("Selection"), static_cast<int>(PrintAreaMode::Selection));
  }
  form->addRow(QObject::tr("Print"), area);

  auto* center = new QCheckBox(QObject::tr("Center Image"), settings_group);
  center->setObjectName(QStringLiteral("printCenterCheck"));
  center->setChecked(true);
  form->addRow(QString(), center);

  auto* x = new QDoubleSpinBox(settings_group);
  x->setObjectName(QStringLiteral("printOffsetXSpin"));
  x->setRange(-100.0, 100.0);
  x->setDecimals(2);
  x->setSuffix(QStringLiteral(" in"));
  configure_dialog_spinbox(x);
  auto* y = new QDoubleSpinBox(settings_group);
  y->setObjectName(QStringLiteral("printOffsetYSpin"));
  y->setRange(-100.0, 100.0);
  y->setDecimals(2);
  y->setSuffix(QStringLiteral(" in"));
  configure_dialog_spinbox(y);
  form->addRow(QObject::tr("X"), x);
  form->addRow(QObject::tr("Y"), y);

  auto* scaled_title = new QLabel(QObject::tr("Scaled Print Size"), settings_group);
  scaled_title->setObjectName(QStringLiteral("printScaledSizeTitle"));
  form->addRow(scaled_title);

  auto* scale_to_fit = new QCheckBox(QObject::tr("Scale to fit media"), settings_group);
  scale_to_fit->setObjectName(QStringLiteral("printScaleToFitCheck"));
  scale_to_fit->setChecked(true);
  form->addRow(QString(), scale_to_fit);

  auto* scale_row = new QWidget(settings_group);
  auto* scale_layout = new QHBoxLayout(scale_row);
  scale_layout->setContentsMargins(0, 0, 0, 0);
  scale_layout->setSpacing(8);
  auto* scale = new QDoubleSpinBox(scale_row);
  scale->setObjectName(QStringLiteral("printScalePercentSpin"));
  scale->setRange(1.0, 1000.0);
  scale->setDecimals(1);
  scale->setSuffix(QStringLiteral("%"));
  scale->setValue(100.0);
  configure_dialog_spinbox(scale, 82);
  auto* scale_size = new QLabel(scale_row);
  scale_size->setObjectName(QStringLiteral("printScaleSizeLabel"));
  scale_size->setMinimumWidth(112);
  scale_layout->addWidget(scale);
  scale_layout->addWidget(scale_size, 1);
  form->addRow(QObject::tr("Scale"), scale_row);

  auto* image_size = new QLabel(settings_group);
  image_size->setObjectName(QStringLiteral("printImageSizeLabel"));
  form->addRow(QObject::tr("Image"), image_size);

  auto* divider = new QFrame(settings_group);
  divider->setObjectName(QStringLiteral("printSizeDivider"));
  divider->setFrameShape(QFrame::HLine);
  divider->setFrameShadow(QFrame::Plain);
  form->addRow(divider);

  auto* resolution_row = new QWidget(settings_group);
  auto* resolution_layout = new QHBoxLayout(resolution_row);
  resolution_layout->setContentsMargins(0, 0, 0, 0);
  resolution_layout->setSpacing(10);
  auto* resolution = new QSpinBox(resolution_row);
  resolution->setObjectName(QStringLiteral("printResolutionSpin"));
  resolution->setRange(1, 9999);
  resolution->setSuffix(QStringLiteral(" PPI"));
  resolution->setValue(std::clamp(static_cast<int>(std::lround(settings.print_resolution_ppi)), 1, 9999));
  configure_dialog_spinbox(resolution, 92);
  auto* units_label = new QLabel(QObject::tr("Units:"), resolution_row);
  auto* units = new QComboBox(resolution_row);
  units->setObjectName(QStringLiteral("printUnitsCombo"));
  units->addItem(QObject::tr("in"), QStringLiteral("in"));
  units->addItem(QObject::tr("cm"), QStringLiteral("cm"));
  units->addItem(QObject::tr("mm"), QStringLiteral("mm"));
  resolution_layout->addWidget(resolution);
  resolution_layout->addWidget(units_label);
  resolution_layout->addWidget(units);
  form->addRow(QObject::tr("Print Resolution"), resolution_row);

  auto* crop_marks = new QCheckBox(QObject::tr("Print crop marks"), settings_group);
  crop_marks->setObjectName(QStringLiteral("printCropMarksCheck"));
  form->addRow(QString(), crop_marks);
  side_layout->addWidget(settings_group);
  side_layout->addStretch(1);

  auto* buttons = new QDialogButtonBox(&dialog);
  auto* print_button = buttons->addButton(QObject::tr("Print"), QDialogButtonBox::AcceptRole);
  print_button->setObjectName(QStringLiteral("printDialogPrintButton"));
  print_button->setEnabled(printer_combo->isEnabled());
  auto* pdf_button = buttons->addButton(QObject::tr("Save PDF..."), QDialogButtonBox::ActionRole);
  pdf_button->setObjectName(QStringLiteral("printDialogPdfButton"));
  buttons->addButton(QDialogButtonBox::Cancel);
  side_layout->addWidget(buttons);

  const auto sync_settings = [&] {
    settings.area_mode = static_cast<PrintAreaMode>(area->currentData().toInt());
    settings.scale_mode = scale_to_fit->isChecked() ? PrintScaleMode::FitToPage : PrintScaleMode::CustomScale;
    settings.scale_percent = scale->value();
    settings.print_resolution_ppi = static_cast<double>(resolution->value());
    settings.center = center->isChecked();
    settings.offset_x_inches = x->value();
    settings.offset_y_inches = y->value();
    settings.crop_marks = crop_marks->isChecked();
    scale->setEnabled(!scale_to_fit->isChecked());
    x->setEnabled(!settings.center);
    y->setEnabled(!settings.center);
    const auto placement = calculate_print_placement(document, settings, current_layout);
    scale->setValue(placement.scale_percent);
    scale_size->setText(formatted_size(placement.print_size_inches, units));
    auto actual_settings = settings;
    actual_settings.scale_mode = PrintScaleMode::ActualSize;
    actual_settings.scale_percent = 100.0;
    image_size->setText(formatted_size(calculate_print_placement(document, actual_settings, current_layout).print_size_inches,
                                       units));
    preview->update();
  };
  sync_settings();

  QObject::connect(area, &QComboBox::currentIndexChanged, &dialog, sync_settings);
  QObject::connect(scale_to_fit, &QCheckBox::toggled, &dialog, [scale, sync_settings](bool checked) {
    if (!checked) {
      scale->setValue(100.0);
    }
    sync_settings();
  });
  QObject::connect(scale, &QDoubleSpinBox::valueChanged, &dialog, sync_settings);
  QObject::connect(resolution, &QSpinBox::valueChanged, &dialog, sync_settings);
  QObject::connect(units, &QComboBox::currentIndexChanged, &dialog, sync_settings);
  QObject::connect(center, &QCheckBox::toggled, &dialog, sync_settings);
  QObject::connect(x, &QDoubleSpinBox::valueChanged, &dialog, sync_settings);
  QObject::connect(y, &QDoubleSpinBox::valueChanged, &dialog, sync_settings);
  QObject::connect(crop_marks, &QCheckBox::toggled, &dialog, sync_settings);

  QObject::connect(page_setup, &QPushButton::clicked, &dialog, [&] {
    const auto printer_name = selected_printer_name(printer_combo);
    auto printer = create_selected_printer(printer_name);
    configure_selected_printer(*printer, printer_name, current_layout,
                               QObject::tr("Photoslop Print"));
    QPageSetupDialog setup_dialog(printer.get(), &dialog);
    if (exec_dialog(setup_dialog) == QDialog::Accepted) {
      current_layout = printer->pageLayout();
      sync_settings();
    }
  });
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  QObject::connect(pdf_button, &QPushButton::clicked, &dialog, [&] {
    sync_settings();
    auto path = QFileDialog::getSaveFileName(&dialog, QObject::tr("Save Print PDF"), QString(),
                                             QObject::tr("PDF Document (*.pdf)"));
    if (path.isEmpty()) {
      return;
    }
    if (!path.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive)) {
      path += QStringLiteral(".pdf");
    }
    try {
      if (!write_print_pdf(path, document, settings, current_layout)) {
        throw std::runtime_error("Could not write PDF");
      }
      if (page_layout != nullptr) {
        *page_layout = current_layout;
      }
      dialog.accept();
    } catch (const std::exception& error) {
      QMessageBox::critical(&dialog, QObject::tr("PDF failed"), QString::fromUtf8(error.what()));
    }
  });
  QObject::connect(print_button, &QPushButton::clicked, &dialog, [&] {
    sync_settings();
    const auto printer_name = selected_printer_name(printer_combo);
    auto printer = create_selected_printer(printer_name);
    configure_selected_printer(*printer, printer_name, current_layout,
                               QObject::tr("Photoslop Print"));
    try {
      if (!printer->isValid()) {
        throw std::runtime_error("Selected printer is not available");
      }
      if (!paint_printer_page(*printer, document, settings)) {
        throw std::runtime_error("Selected printer did not accept the page");
      }
      current_layout = printer->pageLayout();
      if (page_layout != nullptr) {
        *page_layout = current_layout;
      }
      dialog.accept();
    } catch (const std::exception& error) {
      QMessageBox::critical(&dialog, QObject::tr("Print failed"), QString::fromUtf8(error.what()));
    }
  });

  return exec_dialog(dialog) == QDialog::Accepted;
}

}  // namespace photoslop::ui
