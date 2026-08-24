#include "ui/print_dialog.hpp"

#include "ui/app_settings.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/image_document_io.hpp"
#include "ui/measurement_units.hpp"
#include "ui/print_internal.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
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
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QPainterPath>
#include <QPageSetupDialog>
#include <QPrintDialog>
#include <QPrinter>
#include <QPrinterInfo>
#include <QPushButton>
#include <QDir>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace patchy::ui {

namespace {

using print_detail::document_horizontal_ppi;
using print_detail::document_vertical_ppi;
using print_detail::valid_page_layout;

QString default_documents_path(QString filename) {
  auto directory = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
  if (directory.isEmpty()) {
    directory = QDir::homePath();
  }
  return QDir(directory).filePath(filename);
}

void configure_printer(QPrinter& printer, const QPageLayout& page_layout, const QString& document_name) {
  printer.setDocName(document_name.isEmpty() ? QObject::tr("Patchy Document") : document_name);
  printer.setCreator(QStringLiteral("Patchy"));
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

bool paint_printer_page(QPrinter& printer, const Document& document, const PrintSettings& settings,
                        int copies = 1) {
  const auto requested_copies = std::max(1, copies);
  // Drivers that can duplicate a job themselves take the count and get one painted page.
  // The rest need the page painted once per copy inside a single job.
  const bool driver_duplicates = printer.supportsMultipleCopies();
  if (driver_duplicates) {
    printer.setCopyCount(requested_copies);
  }
  // Full-page mode puts the painter's device origin at the physical page corner. Without
  // it the origin sits at the printable-area corner, and mapping the full-page window
  // onto fullRectPixels shifts the output down-right by the margins (off-center prints
  // with the bottom clipped). Margins are still honored: render_print_page places the
  // image inside paintRect.
  printer.setFullPage(true);
  QPainter painter(&printer);
  if (!painter.isActive()) {
    return false;
  }
  // Qt's PDF engine re-encodes the composite as JPEG quality 94 unless the painter asks
  // for lossless rendering. A file the user saved deserves the exact pixels; a real
  // printer job is rasterized by the driver anyway, so the hint is scoped to PDF output.
  if (printer.outputFormat() == QPrinter::PdfFormat) {
    painter.setRenderHint(QPainter::LosslessImageRendering, true);
  }

  const auto layout = valid_page_layout(printer.pageLayout());
  const auto full_points = layout.fullRect(QPageLayout::Point).toAlignedRect();
  const auto full_pixels = layout.fullRectPixels(std::max(1, printer.resolution()));
  const auto painted_pages = driver_duplicates ? 1 : requested_copies;
  for (int page = 0; page < painted_pages; ++page) {
    if (page > 0 && !printer.newPage()) {
      painter.end();
      return false;
    }
    // newPage() resets the painter state on some platforms, so re-establish the
    // full-page mapping for every page rather than once before the loop.
    if (!full_points.isEmpty() && !full_pixels.isEmpty()) {
      painter.setWindow(full_points);
      painter.setViewport(full_pixels);
    }
    render_print_page(painter, document, settings, layout);
  }
  return painter.end();
}

#ifdef Q_OS_WIN
// Qt 6 assumes every printer has a DEVMODE: QPageSetupDialog::exec and
// QWin32PrintEngine::begin dereference it with no null check. When CreateDC fails,
// QWin32PrintEnginePrivate::initialize calls release(), which sets devMode to null,
// and the next dialog crashes the app instead of reporting an error. A broken
// Microsoft Print to PDF does exactly this: its CreateDC fails with
// ERROR_PATH_NOT_FOUND while the spooler still hands out a DEVMODE, so testing the
// device context is the only reliable check. See docs/platform.md.
bool windows_printer_device_is_usable(const QString& printer_name) {
  const auto name = printer_name.toStdWString();
  // The same call the engine makes; a null DEVMODE just means driver defaults.
  HDC context = CreateDCW(nullptr, name.c_str(), nullptr, nullptr);
  if (context == nullptr) {
    return false;
  }
  DeleteDC(context);
  return true;
}
#endif  // Q_OS_WIN

// True when the printer can be handed to Qt safely. Only Windows has a failure mode
// here (see windows_printer_device_is_usable); elsewhere the drivers Qt talks to always
// describe themselves.
bool ensure_printer_driver_usable(const QString& printer_name, QWidget* parent) {
#ifdef Q_OS_WIN
  auto name = printer_name;
  if (name.isEmpty()) {
    name = QPrinterInfo::defaultPrinter().printerName();
  }
  if (name.isEmpty() || windows_printer_device_is_usable(name)) {
    return true;
  }
  show_critical_message(parent, QObject::tr("Printer unavailable"),
                        QObject::tr("Windows cannot read the settings for \"%1\". The printer driver "
                                    "may need to be repaired or reinstalled.")
                            .arg(name),
                        QStringLiteral("printerUnavailableMessageBox"));
  return false;
#else
  (void)printer_name;
  (void)parent;
  return true;
#endif
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

// --- Photocopy --------------------------------------------------------------
// File > Import > Photocopy prints a fresh scan at actual size, centered, never
// scaled. These helpers pick the paper orientation and preview what gets cut off.

constexpr double kPhotocopyPointsPerInch = 72.0;

QString photocopy_formatted_size(QSizeF inches) {
  // Sizes follow the ruler-unit preference the way the document info line does:
  // metric rulers read cm/mm, everything else reads inches.
  const auto unit = measurement_unit_from_settings_token(
      app_settings().value(QStringLiteral("view/rulerUnits"), QStringLiteral("px")).toString(),
      MeasurementUnit::Pixels);
  const auto display = (unit == MeasurementUnit::Centimeters || unit == MeasurementUnit::Millimeters)
                           ? unit
                           : MeasurementUnit::Inches;
  const auto factor = measurement_units_per_inch(display);
  return QObject::tr("%1 x %2 %3")
      .arg(inches.width() * factor, 0, 'f', 2)
      .arg(inches.height() * factor, 0, 'f', 2)
      .arg(measurement_unit_suffix(display));
}

// Photocopies anchor the scan's top-right corner to the paper's top-right corner: that
// mirrors the platen's document registration corner, so the part the user pushed the
// original against on the glass is the part that prints. Dragging the preview moves it
// from there.
PrintSettings anchored_photocopy_settings(const Document& document, PrintSettings settings,
                                          const QPageLayout& layout) {
  settings.center = false;
  const auto scan_inches = calculate_print_placement(document, settings, layout).print_size_inches;
  const auto printable = valid_page_layout(layout).paintRect(QPageLayout::Point);
  settings.offset_x_inches = printable.width() / kPhotocopyPointsPerInch - scan_inches.width();
  settings.offset_y_inches = 0.0;
  return settings;
}

double photocopy_clipped_area_points(const Document& document, const PrintSettings& settings,
                                     const QPageLayout& layout) {
  const auto target = calculate_print_placement(document, settings, layout).target_rect_points;
  const auto kept = target.intersected(layout.paintRect(QPageLayout::Point));
  return target.width() * target.height() - kept.width() * kept.height();
}

QPageLayout photocopy_page_layout(const Document& document, const PrintSettings& settings,
                                  QPageLayout layout) {
  layout = valid_page_layout(layout);
  // Hardware-minimum margins are the honest printable edge for a copy: driver default
  // margins waste paper, and FullPageMode would preview pixels the device cannot reach.
  // Borderless-capable drivers (photo printers such as the Epson SC-PX1V) report
  // NEGATIVE minimums for their overspray; clamp at zero so the printable rect never
  // leaves the paper and the anchor corner stays a real paper corner.
  const auto minimums = layout.minimumMargins();
  layout.setMargins(QMarginsF(std::max(0.0, static_cast<double>(minimums.left())),
                              std::max(0.0, static_cast<double>(minimums.top())),
                              std::max(0.0, static_cast<double>(minimums.right())),
                              std::max(0.0, static_cast<double>(minimums.bottom()))));
  auto portrait = layout;
  portrait.setOrientation(QPageLayout::Portrait);
  auto landscape = layout;
  landscape.setOrientation(QPageLayout::Landscape);
  // Auto-rotate like a copier: with the scan anchored the way it will actually print,
  // the orientation that loses the least of it wins, portrait on a tie (half a point
  // squared absorbs placement rounding).
  const auto portrait_loss =
      photocopy_clipped_area_points(document, anchored_photocopy_settings(document, settings, portrait), portrait);
  const auto landscape_loss = photocopy_clipped_area_points(
      document, anchored_photocopy_settings(document, settings, landscape), landscape);
  return landscape_loss + 0.5 < portrait_loss ? landscape : portrait;
}

bool photocopy_scan_is_clipped(const Document& document, const PrintSettings& settings,
                               const QPageLayout& layout) {
  // Containment, not size: a scan smaller than the paper still clips once it is dragged
  // partly off the printable area.
  const auto target = calculate_print_placement(document, settings, layout).target_rect_points;
  const auto printable = layout.paintRect(QPageLayout::Point);
  return !printable.adjusted(-0.5, -0.5, 0.5, 0.5).contains(target);
}

class PhotocopyPreviewPane final : public QWidget {
public:
  explicit PhotocopyPreviewPane(const Document& document, QWidget* parent = nullptr) : QWidget(parent) {
    setObjectName(QStringLiteral("photocopyPreviewPane"));
    setMinimumSize(280, 320);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setCursor(Qt::OpenHandCursor);
    setMouseTracking(true);
    // Rendered once up front: the scan never changes while the dialog is open, and
    // compositing it per repaint would touch every scan pixel on each paint.
    scan_ = qimage_from_document_rect(document, QRect(0, 0, document.width(), document.height()), false);
    constexpr int kMaxPreviewEdge = 2048;
    if (std::max(scan_.width(), scan_.height()) > kMaxPreviewEdge) {
      scan_ = scan_.scaled(kMaxPreviewEdge, kMaxPreviewEdge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
  }

  // The pane owns the crop and move interactions, so it mutates the dialog's settings
  // (selection_bounds is the crop, the offsets are the crop's position on the printable
  // area) and reports through the callback so the dialog refreshes labels and warning.
  void set_state(const Document* document, PrintSettings* settings, const QPageLayout* page_layout) noexcept {
    document_ = document;
    settings_ = settings;
    page_layout_ = page_layout;
    frozen_view_.reset();
    refresh_view_properties();
    update();
  }

  void set_placement_changed_callback(std::function<void()> callback) {
    placement_changed_ = std::move(callback);
  }

protected:
  struct ViewMetrics {
    double scale{0.0};  // device pixels per point; 0 = no usable view
    QPointF origin;     // device position of bounds.topLeft
    QRectF bounds;      // point-space rect fitted into the widget
  };

  [[nodiscard]] bool has_state() const noexcept {
    return document_ != nullptr && settings_ != nullptr && page_layout_ != nullptr;
  }

  // The crop in scan pixels; an empty selection means the whole scan.
  [[nodiscard]] QRect effective_crop() const {
    const QRect full(0, 0, document_->width(), document_->height());
    const auto crop = settings_->selection_bounds.normalized().intersected(full);
    return crop.isEmpty() ? full : crop;
  }

  // The crop's rect on the paper, in points (exactly what will print).
  [[nodiscard]] QRectF crop_rect_points() const {
    return calculate_print_placement(*document_, *settings_, valid_page_layout(*page_layout_)).target_rect_points;
  }

  // The whole scan's rect in points, positioned so the crop sits at its paper position.
  [[nodiscard]] QRectF scan_rect_points() const {
    const auto crop = effective_crop();
    const auto crop_points = crop_rect_points();
    const auto x_ppi = document_horizontal_ppi(*document_);
    const auto y_ppi = document_vertical_ppi(*document_);
    const QPointF origin(crop_points.left() - crop.left() / x_ppi * kPhotocopyPointsPerInch,
                         crop_points.top() - crop.top() / y_ppi * kPhotocopyPointsPerInch);
    return QRectF(origin, QSizeF(document_->width() / x_ppi * kPhotocopyPointsPerInch,
                                 document_->height() / y_ppi * kPhotocopyPointsPerInch));
  }

  // Computed from state rather than cached at paint time so interaction math works
  // even before the first paint. While a drag or resize is active the metrics captured
  // at the press are returned instead: the fit-to-content refit would otherwise move
  // the page underneath the pointer and make the drag feel reversed.
  [[nodiscard]] ViewMetrics view_metrics() const {
    if (frozen_view_.has_value()) {
      return *frozen_view_;
    }
    ViewMetrics metrics;
    if (!has_state()) {
      return metrics;
    }
    const auto layout = valid_page_layout(*page_layout_);
    // Fit the page AND the whole scan into the widget, so cut-off and cropped-away
    // areas stay visible instead of vanishing outside the preview.
    const auto bounds = QRectF(layout.fullRect(QPageLayout::Point)).united(scan_rect_points());
    const auto available = rect().adjusted(16, 16, -16, -16);
    if (bounds.isEmpty() || available.isEmpty()) {
      return metrics;
    }
    metrics.scale = std::min(static_cast<double>(available.width()) / bounds.width(),
                             static_cast<double>(available.height()) / bounds.height());
    metrics.origin = QPointF(available.x() + (available.width() - bounds.width() * metrics.scale) / 2.0,
                             available.y() + (available.height() - bounds.height() * metrics.scale) / 2.0);
    metrics.bounds = bounds;
    return metrics;
  }

  [[nodiscard]] QPointF to_device(QPointF points, const ViewMetrics& metrics) const {
    return metrics.origin + (points - metrics.bounds.topLeft()) * metrics.scale;
  }

  [[nodiscard]] QRectF crop_rect_device(const ViewMetrics& metrics) const {
    const auto crop_points = crop_rect_points();
    return QRectF(to_device(crop_points.topLeft(), metrics), to_device(crop_points.bottomRight(), metrics));
  }

  // Corner handles first (TL TR BL BR), then edge midpoints (top bottom left right);
  // resize_crop_to keys off this order.
  [[nodiscard]] std::array<QRectF, 8> handle_rects(const ViewMetrics& metrics) const {
    const auto crop = crop_rect_device(metrics);
    constexpr double kHalf = 4.0;
    const auto at = [](QPointF center) {
      return QRectF(center.x() - kHalf, center.y() - kHalf, 2 * kHalf, 2 * kHalf);
    };
    return {at(crop.topLeft()),
            at(crop.topRight()),
            at(crop.bottomLeft()),
            at(crop.bottomRight()),
            at(QPointF(crop.center().x(), crop.top())),
            at(QPointF(crop.center().x(), crop.bottom())),
            at(QPointF(crop.left(), crop.center().y())),
            at(QPointF(crop.right(), crop.center().y()))};
  }

  [[nodiscard]] int handle_at(QPointF device_pos) const {
    const auto metrics = view_metrics();
    if (metrics.scale <= 0.0) {
      return -1;
    }
    const auto handles = handle_rects(metrics);
    for (int i = 0; i < static_cast<int>(handles.size()); ++i) {
      if (handles[static_cast<std::size_t>(i)].adjusted(-2, -2, 2, 2).contains(device_pos)) {
        return i;
      }
    }
    return -1;
  }

  void notify_placement_changed() {
    refresh_view_properties();
    if (placement_changed_) {
      placement_changed_();
    }
    update();
  }

  // Exposed for the UI tests: the crop's on-screen rect.
  void refresh_view_properties() {
    if (!has_state()) {
      return;
    }
    const auto metrics = view_metrics();
    if (metrics.scale > 0.0) {
      setProperty("cropRectView", crop_rect_device(metrics).toRect());
    }
  }

  // Dragging a handle resizes the crop. The scan stays put on screen, so the offsets
  // (which position the crop's origin on the paper) follow the crop's top-left corner.
  void resize_crop_to(QPointF device_pos) {
    const auto metrics = view_metrics();
    if (metrics.scale <= 0.0) {
      return;
    }
    const auto x_ppi = document_horizontal_ppi(*document_);
    const auto y_ppi = document_vertical_ppi(*document_);
    const auto scan = scan_rect_points();
    const auto points = metrics.bounds.topLeft() + (device_pos - metrics.origin) / metrics.scale;
    const QPointF px((points.x() - scan.left()) / kPhotocopyPointsPerInch * x_ppi,
                     (points.y() - scan.top()) / kPhotocopyPointsPerInch * y_ppi);
    const auto old_crop = effective_crop();
    auto crop = old_crop;
    // A quarter inch minimum keeps the handles apart and grabbable.
    const auto min_w = std::max(1, qRound(x_ppi / 4.0));
    const auto min_h = std::max(1, qRound(y_ppi / 4.0));
    const bool left = active_handle_ == 0 || active_handle_ == 2 || active_handle_ == 6;
    const bool right = active_handle_ == 1 || active_handle_ == 3 || active_handle_ == 7;
    const bool top = active_handle_ == 0 || active_handle_ == 1 || active_handle_ == 4;
    const bool bottom = active_handle_ == 2 || active_handle_ == 3 || active_handle_ == 5;
    if (left) {
      crop.setLeft(std::clamp(qRound(px.x()), 0, crop.right() + 1 - min_w));
    }
    if (right) {
      crop.setRight(std::clamp(qRound(px.x()) - 1, crop.left() + min_w - 1, document_->width() - 1));
    }
    if (top) {
      crop.setTop(std::clamp(qRound(px.y()), 0, crop.bottom() + 1 - min_h));
    }
    if (bottom) {
      crop.setBottom(std::clamp(qRound(px.y()) - 1, crop.top() + min_h - 1, document_->height() - 1));
    }
    if (crop == old_crop) {
      return;
    }
    settings_->selection_bounds = crop;
    settings_->offset_x_inches += (crop.left() - old_crop.left()) / x_ppi;
    settings_->offset_y_inches += (crop.top() - old_crop.top()) / y_ppi;
    notify_placement_changed();
  }

  // Dragging anywhere else slides the crop (scan and all) across the paper, clamped so
  // at least half an inch of the crop always stays on the printable area.
  void move_crop_by_device_delta(QPointF delta) {
    const auto metrics = view_metrics();
    if (metrics.scale <= 0.0) {
      return;
    }
    const auto layout = valid_page_layout(*page_layout_);
    const auto crop_inches = calculate_print_placement(*document_, *settings_, layout).print_size_inches;
    const auto printable = layout.paintRect(QPageLayout::Point);
    const auto printable_w = printable.width() / kPhotocopyPointsPerInch;
    const auto printable_h = printable.height() / kPhotocopyPointsPerInch;
    const auto keep_w = std::min(0.5, crop_inches.width());
    const auto keep_h = std::min(0.5, crop_inches.height());
    const auto dx_inches = delta.x() / metrics.scale / kPhotocopyPointsPerInch;
    const auto dy_inches = delta.y() / metrics.scale / kPhotocopyPointsPerInch;
    settings_->offset_x_inches =
        std::clamp(settings_->offset_x_inches + dx_inches, keep_w - crop_inches.width(), printable_w - keep_w);
    settings_->offset_y_inches =
        std::clamp(settings_->offset_y_inches + dy_inches, keep_h - crop_inches.height(), printable_h - keep_h);
    notify_placement_changed();
  }

  [[nodiscard]] Qt::CursorShape cursor_for_handle(int handle) const {
    switch (handle) {
      case 0:
      case 3:
        return Qt::SizeFDiagCursor;
      case 1:
      case 2:
        return Qt::SizeBDiagCursor;
      case 4:
      case 5:
        return Qt::SizeVerCursor;
      case 6:
      case 7:
        return Qt::SizeHorCursor;
      default:
        return dragging_ ? Qt::ClosedHandCursor : Qt::OpenHandCursor;
    }
  }

  void mousePressEvent(QMouseEvent* event) override {
    if (event->button() != Qt::LeftButton || !has_state()) {
      return;
    }
    // Freeze the view for the whole interaction (see view_metrics).
    const auto metrics = view_metrics();
    if (metrics.scale > 0.0) {
      frozen_view_ = metrics;
    }
    active_handle_ = handle_at(event->position());
    dragging_ = active_handle_ < 0;
    last_drag_pos_ = event->position();
    setCursor(cursor_for_handle(active_handle_));
  }

  void mouseMoveEvent(QMouseEvent* event) override {
    if (!has_state()) {
      return;
    }
    if (active_handle_ >= 0) {
      resize_crop_to(event->position());
      return;
    }
    if (dragging_) {
      const auto delta = event->position() - last_drag_pos_;
      last_drag_pos_ = event->position();
      move_crop_by_device_delta(delta);
      return;
    }
    setCursor(cursor_for_handle(handle_at(event->position())));
  }

  void mouseReleaseEvent(QMouseEvent* event) override {
    if (event->button() == Qt::LeftButton) {
      dragging_ = false;
      active_handle_ = -1;
      // Unfreeze and refit once, now that the interaction is over.
      frozen_view_.reset();
      refresh_view_properties();
      update();
      setCursor(cursor_for_handle(handle_at(event->position())));
    }
  }

  void resizeEvent(QResizeEvent* event) override {
    QWidget::resizeEvent(event);
    refresh_view_properties();
  }

  void paintEvent(QPaintEvent* /*event*/) override {
    QPainter painter(this);
    painter.fillRect(rect(), QColor(36, 36, 36));
    if (!has_state()) {
      return;
    }
    const auto metrics = view_metrics();
    if (metrics.scale <= 0.0) {
      return;
    }
    const auto layout = valid_page_layout(*page_layout_);
    const auto page = QRectF(layout.fullRect(QPageLayout::Point));
    const auto printable = layout.paintRect(QPageLayout::Point);
    const auto crop_points = crop_rect_points();
    const auto scan = scan_rect_points();

    painter.save();
    painter.translate(metrics.origin);
    painter.scale(metrics.scale, metrics.scale);
    painter.translate(-metrics.bounds.topLeft());

    painter.fillRect(page, Qt::white);
    if (!scan_.isNull()) {
      painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
      painter.drawImage(scan, scan_, QRectF(scan_.rect()));
    }
    // Cropped-away scan areas are dimmed (they never print); crop areas outside the
    // printable area are shaded red (they would print but get cut off).
    QPainterPath crop_path;
    crop_path.addRect(crop_points);
    QPainterPath outside_crop;
    outside_crop.addRect(scan);
    outside_crop = outside_crop.subtracted(crop_path);
    if (!outside_crop.isEmpty()) {
      painter.fillPath(outside_crop, QColor(15, 15, 15, 150));
    }
    QPainterPath kept;
    kept.addRect(crop_points.intersected(printable));
    const auto lost = crop_path.subtracted(kept);
    if (!lost.isEmpty()) {
      // Fill only: stroking the subtracted path draws spurious edges along the shared
      // boundary with the kept region.
      painter.fillPath(lost, QColor(190, 40, 35, 120));
    }
    QPen printable_pen(QColor(205, 205, 205), 0.0, Qt::DashLine);
    printable_pen.setCosmetic(true);
    painter.setPen(printable_pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(printable);
    QPen page_pen(QColor(18, 18, 18), 0.0);
    page_pen.setCosmetic(true);
    painter.setPen(page_pen);
    painter.drawRect(page);
    painter.restore();

    // Crop chrome in device space so line weight and handles stay constant. A black
    // and white pair reads over arbitrary scan content, like other selection UI.
    const auto crop_device = crop_rect_device(metrics);
    painter.setPen(QPen(QColor(255, 255, 255), 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(crop_device);
    painter.setPen(QPen(QColor(0, 0, 0), 1.0, Qt::DashLine));
    painter.drawRect(crop_device);
    for (const auto& handle : handle_rects(metrics)) {
      painter.setPen(QPen(QColor(0, 0, 0), 1.0));
      painter.setBrush(QColor(255, 255, 255));
      painter.drawRect(handle);
    }
  }

private:
  QImage scan_;
  const Document* document_{nullptr};
  PrintSettings* settings_{nullptr};
  const QPageLayout* page_layout_{nullptr};
  std::function<void()> placement_changed_;
  std::optional<ViewMetrics> frozen_view_;
  bool dragging_{false};
  int active_handle_{-1};
  QPointF last_drag_pos_;
};

}  // namespace

bool write_print_pdf(const QString& path, const Document& document, const PrintSettings& settings,
                     const QPageLayout& page_layout, const QString& document_name) {
  if (path.isEmpty()) {
    return false;
  }
  QPrinter printer(QPrinter::HighResolution);
  printer.setOutputFormat(QPrinter::PdfFormat);
  printer.setOutputFileName(path);
  configure_printer(printer, page_layout, document_name);
  return paint_printer_page(printer, document, settings);
}

void run_page_setup_dialog(QWidget* parent, QPageLayout* page_layout) {
  QPrinter printer(QPrinter::HighResolution);
  configure_printer(printer, page_layout != nullptr ? *page_layout : default_print_page_layout(),
                    QObject::tr("Patchy Print"));
  if (!ensure_printer_driver_usable(printer.printerName(), parent)) {
    return;
  }
  QPageSetupDialog dialog(&printer, parent);
  dialog.setObjectName(QStringLiteral("pageSetupDialog"));
  if (exec_dialog(dialog) == QDialog::Accepted && page_layout != nullptr) {
    *page_layout = printer.pageLayout();
  }
}

bool run_print_dialog(QWidget* parent, const Document& document, const QString& document_title,
                      std::optional<QRect> selection_bounds, QPageLayout* page_layout) {
  const auto display_title = document_title.isEmpty() ? QObject::tr("Untitled") : document_title;
  auto settings = default_print_settings(document, selection_bounds);
  auto current_layout = valid_page_layout(page_layout != nullptr ? *page_layout : QPageLayout{});
  // Photoshop prints at actual size (100%) by default. Patchy keeps that whenever the
  // document fits the printable area, and only pre-checks "Scale to fit media" when
  // actual size would overflow the page (scaling beats silent clipping).
  const bool fits_at_actual_size = [&] {
    auto actual = settings;
    actual.scale_mode = PrintScaleMode::ActualSize;
    const auto placement = calculate_print_placement(document, actual, current_layout);
    const auto printable = current_layout.paintRect(QPageLayout::Point);
    return placement.target_rect_points.width() <= printable.width() + 0.5 &&
           placement.target_rect_points.height() <= printable.height() + 0.5;
  }();
  settings.scale_mode = fits_at_actual_size ? PrintScaleMode::CustomScale : PrintScaleMode::FitToPage;

  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("patchyPrintDialog"));
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
  auto* copies_spin = new QSpinBox(output_group);
  copies_spin->setObjectName(QStringLiteral("printCopiesSpin"));
  copies_spin->setRange(1, 999);
  copies_spin->setValue(1);
  configure_dialog_spinbox(copies_spin, 82);
  printer_form->addRow(QObject::tr("Copies"), copies_spin);
  output_layout->addLayout(printer_form);
  auto* page_setup = new QPushButton(QObject::tr("Page Setup..."), output_group);
  page_setup->setObjectName(QStringLiteral("printPageSetupButton"));
  output_layout->addWidget(page_setup);
  auto* system_dialog = new QPushButton(QObject::tr("Print Using System Dialog..."), output_group);
  system_dialog->setObjectName(QStringLiteral("printSystemDialogButton"));
  system_dialog->setEnabled(printer_combo->isEnabled());
  output_layout->addWidget(system_dialog);
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
  scale_to_fit->setChecked(settings.scale_mode == PrintScaleMode::FitToPage);
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
  // Read-only, Photoshop-style: the effective on-paper density is the document
  // resolution divided by the print scale. Changing the stored resolution is Image
  // Size's job.
  auto* resolution_value = new QLabel(resolution_row);
  resolution_value->setObjectName(QStringLiteral("printResolutionValueLabel"));
  auto* units_label = new QLabel(QObject::tr("Units:"), resolution_row);
  auto* units = new QComboBox(resolution_row);
  units->setObjectName(QStringLiteral("printUnitsCombo"));
  units->addItem(QObject::tr("in"), QStringLiteral("in"));
  units->addItem(QObject::tr("cm"), QStringLiteral("cm"));
  units->addItem(QObject::tr("mm"), QStringLiteral("mm"));
  resolution_layout->addWidget(resolution_value);
  resolution_layout->addStretch(1);
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
    const auto scale_factor = std::max(0.01, placement.scale_percent / 100.0);
    const auto derived_horizontal =
        static_cast<int>(std::lround(document_horizontal_ppi(document) / scale_factor));
    const auto derived_vertical =
        static_cast<int>(std::lround(document_vertical_ppi(document) / scale_factor));
    resolution_value->setText(derived_horizontal == derived_vertical
                                  ? QObject::tr("%1 PPI").arg(derived_horizontal)
                                  : QObject::tr("%1 x %2 PPI").arg(derived_horizontal).arg(derived_vertical));
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
  QObject::connect(units, &QComboBox::currentIndexChanged, &dialog, sync_settings);
  QObject::connect(center, &QCheckBox::toggled, &dialog, sync_settings);
  QObject::connect(x, &QDoubleSpinBox::valueChanged, &dialog, sync_settings);
  QObject::connect(y, &QDoubleSpinBox::valueChanged, &dialog, sync_settings);
  QObject::connect(crop_marks, &QCheckBox::toggled, &dialog, sync_settings);

  const auto send_print_job = [&](QPrinter& printer, int copies) {
    try {
      if (!printer.isValid()) {
        throw std::runtime_error("Selected printer is not available");
      }
      if (!paint_printer_page(printer, document, settings, copies)) {
        throw std::runtime_error("Selected printer did not accept the page");
      }
      current_layout = printer.pageLayout();
      if (page_layout != nullptr) {
        *page_layout = current_layout;
      }
      dialog.accept();
    } catch (const std::exception& error) {
      show_critical_message(&dialog, QObject::tr("Print failed"), QString::fromUtf8(error.what()),
                            QStringLiteral("printFailedMessageBox"));
    }
  };

  QObject::connect(page_setup, &QPushButton::clicked, &dialog, [&] {
    const auto printer_name = selected_printer_name(printer_combo);
    if (!ensure_printer_driver_usable(printer_name, &dialog)) {
      return;
    }
    auto printer = create_selected_printer(printer_name);
    configure_selected_printer(*printer, printer_name, current_layout, display_title);
    QPageSetupDialog setup_dialog(printer.get(), &dialog);
    setup_dialog.setObjectName(QStringLiteral("printPageSetupDialog"));
    if (exec_dialog(setup_dialog) == QDialog::Accepted) {
      current_layout = printer->pageLayout();
      sync_settings();
    }
  });
  QObject::connect(system_dialog, &QPushButton::clicked, &dialog, [&] {
    // Chrome-style hand-off: the OS dialog owns printer, paper, orientation, and copies
    // (and driver-only features such as duplex or trays); accepting it prints at once
    // with Patchy's position, scale, and crop-mark settings, cancelling returns here.
    // The dialog is always the platform's own (Qt 6 has no widget fallback for it), so
    // the UI tests only check the button and never click it.
    sync_settings();
    const auto printer_name = selected_printer_name(printer_combo);
    if (!ensure_printer_driver_usable(printer_name, &dialog)) {
      return;
    }
    auto printer = create_selected_printer(printer_name);
    configure_selected_printer(*printer, printer_name, current_layout, display_title);
    if (printer->supportsMultipleCopies()) {
      printer->setCopyCount(copies_spin->value());
    }
    QPrintDialog system_print(printer.get(), &dialog);
    system_print.setObjectName(QStringLiteral("printSystemPrintDialog"));
    // Patchy always emits a single page, so a page range makes no sense.
    system_print.setOption(QAbstractPrintDialog::PrintPageRange, false);
    if (exec_dialog(system_print) != QDialog::Accepted) {
      return;
    }
    current_layout = valid_page_layout(printer->pageLayout());
    copies_spin->setValue(std::max(1, printer->copyCount()));
    const auto combo_index = printer_combo->findData(printer->printerName());
    if (combo_index >= 0) {
      printer_combo->setCurrentIndex(combo_index);
    }
    sync_settings();
    // Copies are not double-counted: paint_printer_page either hands the count to a
    // driver that duplicates jobs itself or repaints the page N times in one job.
    send_print_job(*printer, copies_spin->value());
  });
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  QObject::connect(pdf_button, &QPushButton::clicked, &dialog, [&] {
    sync_settings();
    auto path = get_save_file_name(&dialog, QObject::tr("Save Print PDF"),
                                   default_documents_path(default_print_pdf_filename(document_title)),
                                   // Same display name the format table gives .pdf, so the
                                   // two save dialogs cannot drift apart in translation.
                                   QStringLiteral("%1 (*.pdf)").arg(
                                       QCoreApplication::translate("QObject", "PDF Document")),
                                   nullptr,
                                   QStringLiteral("savePrintPdfFileDialog"));
    if (path.isEmpty()) {
      return;
    }
    if (!path.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive)) {
      path += QStringLiteral(".pdf");
    }
    try {
      if (!write_print_pdf(path, document, settings, current_layout, display_title)) {
        throw std::runtime_error("Could not write PDF");
      }
      if (page_layout != nullptr) {
        *page_layout = current_layout;
      }
      dialog.accept();
    } catch (const std::exception& error) {
      show_critical_message(&dialog, QObject::tr("PDF failed"), QString::fromUtf8(error.what()),
                            QStringLiteral("pdfFailedMessageBox"));
    }
  });
  QObject::connect(print_button, &QPushButton::clicked, &dialog, [&] {
    sync_settings();
    const auto printer_name = selected_printer_name(printer_combo);
    if (!ensure_printer_driver_usable(printer_name, &dialog)) {
      return;
    }
    auto printer = create_selected_printer(printer_name);
    configure_selected_printer(*printer, printer_name, current_layout, display_title);
    send_print_job(*printer, copies_spin->value());
  });

  return exec_dialog(dialog) == QDialog::Accepted;
}

bool run_photocopy_dialog(QWidget* parent, const Document& document) {
  // The whole point of the photocopy flow: actual size, never scaled. The selection is
  // the crop rect the preview edits; it starts as the whole scan.
  PrintSettings settings;
  settings.scale_mode = PrintScaleMode::ActualSize;
  settings.area_mode = PrintAreaMode::Selection;
  settings.selection_bounds = QRect(0, 0, std::max(0, document.width()), std::max(0, document.height()));

  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("photocopyDialog"));
  dialog.setWindowTitle(QObject::tr("Photocopy"));
  dialog.resize(720, 480);

  auto* root = new QHBoxLayout(&dialog);
  root->setContentsMargins(12, 12, 12, 12);
  root->setSpacing(14);

  auto* preview = new PhotocopyPreviewPane(document, &dialog);
  root->addWidget(preview, 1);

  auto* side = new QWidget(&dialog);
  auto* side_layout = new QVBoxLayout(side);
  side_layout->setContentsMargins(0, 0, 0, 0);
  side_layout->setSpacing(10);
  root->addWidget(side, 0);

  auto* output_group = new QGroupBox(QObject::tr("Output"), side);
  auto* output_form = new QFormLayout(output_group);
  auto* printer_combo = new QComboBox(output_group);
  printer_combo->setObjectName(QStringLiteral("photocopyPrinterCombo"));
  populate_printer_combo(printer_combo);
  if (printer_combo->isEnabled()) {
    const auto remembered = app_settings().value(QStringLiteral("photocopy/printerName")).toString();
    if (const auto index = printer_combo->findData(remembered); index >= 0) {
      printer_combo->setCurrentIndex(index);
    }
  }
  output_form->addRow(QObject::tr("Printer"), printer_combo);
  auto* copies_spin = new QSpinBox(output_group);
  copies_spin->setObjectName(QStringLiteral("photocopyCopiesSpin"));
  copies_spin->setRange(1, 999);
  copies_spin->setValue(1);
  configure_dialog_spinbox(copies_spin, 82);
  output_form->addRow(QObject::tr("Copies"), copies_spin);
  side_layout->addWidget(output_group);

  auto* size_group = new QGroupBox(QObject::tr("Size"), side);
  auto* size_layout = new QVBoxLayout(size_group);
  auto* note = new QLabel(QObject::tr("The copy is printed at actual size and is never scaled."), size_group);
  note->setObjectName(QStringLiteral("photocopyActualSizeNote"));
  note->setWordWrap(true);
  size_layout->addWidget(note);
  auto* size_form = new QFormLayout();
  auto* scan_size = new QLabel(size_group);
  scan_size->setObjectName(QStringLiteral("photocopyScanSizeLabel"));
  size_form->addRow(QObject::tr("Scan"), scan_size);
  auto* print_size = new QLabel(size_group);
  print_size->setObjectName(QStringLiteral("photocopyPrintSizeLabel"));
  size_form->addRow(QObject::tr("Print"), print_size);
  auto* paper_size = new QLabel(size_group);
  paper_size->setObjectName(QStringLiteral("photocopyPaperSizeLabel"));
  size_form->addRow(QObject::tr("Paper"), paper_size);
  size_layout->addLayout(size_form);
  auto* clip_warning = new QLabel(QObject::tr("The shaded parts fall outside the printable area and will be "
                                              "cut off. Drag the preview to choose which part prints."),
                                  size_group);
  clip_warning->setObjectName(QStringLiteral("photocopyClipWarningLabel"));
  clip_warning->setWordWrap(true);
  size_layout->addWidget(clip_warning);
  side_layout->addWidget(size_group);
  side_layout->addStretch(1);

  auto* buttons = new QDialogButtonBox(&dialog);
  auto* print_button = buttons->addButton(QObject::tr("Print"), QDialogButtonBox::AcceptRole);
  print_button->setObjectName(QStringLiteral("photocopyPrintButton"));
  print_button->setEnabled(printer_combo->isEnabled());
  print_button->setDefault(true);
  buttons->addButton(QDialogButtonBox::Cancel);
  side_layout->addWidget(buttons);

  QPageLayout current_layout = default_print_page_layout();
  const auto refresh_placement = [&] {
    clip_warning->setVisible(photocopy_scan_is_clipped(document, settings, current_layout));
    print_size->setText(photocopy_formatted_size(
        calculate_print_placement(document, settings, current_layout).print_size_inches));
    // Exposed for the UI tests: the crop's paper offset and its bounds in scan pixels.
    preview->setProperty("scanOffsetInches", QPointF(settings.offset_x_inches, settings.offset_y_inches));
    preview->setProperty("cropRectPixels", settings.selection_bounds);
    preview->update();
  };
  const auto sync = [&] {
    // The paper comes from the selected printer's own defaults (orientation is then
    // auto-chosen); photocopy deliberately has no page setup to fiddle with. Switching
    // printers keeps the crop but re-anchors it to the top-right corner.
    const auto printer = create_selected_printer(selected_printer_name(printer_combo));
    current_layout = photocopy_page_layout(document, settings, printer->pageLayout());
    settings = anchored_photocopy_settings(document, settings, current_layout);
    auto full_scan = settings;
    full_scan.area_mode = PrintAreaMode::Document;
    scan_size->setText(photocopy_formatted_size(
        calculate_print_placement(document, full_scan, current_layout).print_size_inches));
    const auto page_points = current_layout.fullRect(QPageLayout::Point);
    paper_size->setText(photocopy_formatted_size(QSizeF(page_points.width() / kPhotocopyPointsPerInch,
                                                        page_points.height() / kPhotocopyPointsPerInch)));
    preview->set_state(&document, &settings, &current_layout);
    refresh_placement();
  };
  sync();
  QObject::connect(printer_combo, &QComboBox::currentIndexChanged, &dialog, sync);
  preview->set_placement_changed_callback(refresh_placement);

  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  QObject::connect(print_button, &QPushButton::clicked, &dialog, [&] {
    const auto printer_name = selected_printer_name(printer_combo);
    if (!ensure_printer_driver_usable(printer_name, &dialog)) {
      return;
    }
    auto printer = create_selected_printer(printer_name);
    configure_selected_printer(*printer, printer_name, current_layout, QObject::tr("Patchy Photocopy"));
    try {
      if (!printer->isValid()) {
        throw std::runtime_error("Selected printer is not available");
      }
      if (!paint_printer_page(*printer, document, settings, copies_spin->value())) {
        throw std::runtime_error("Selected printer did not accept the page");
      }
      if (!printer_name.isEmpty()) {
        auto stored = app_settings();
        stored.setValue(QStringLiteral("photocopy/printerName"), printer_name);
      }
      dialog.accept();
    } catch (const std::exception& error) {
      show_critical_message(&dialog, QObject::tr("Print failed"), QString::fromUtf8(error.what()),
                            QStringLiteral("printFailedMessageBox"));
    }
  });

  return exec_dialog(dialog) == QDialog::Accepted;
}

}  // namespace patchy::ui
