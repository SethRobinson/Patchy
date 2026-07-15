// MainWindow's document create/resize dialog implementation, split out of
// main_window.cpp: the Image Size and Canvas Size dialogs (the
// request_*_settings helpers and the settings structs they return) plus the
// members that drive them (reset_document, create_clipboard_document,
// create_new_document, resize_image_dialog, resize_canvas_dialog). The New
// Document dialog itself lives in ui/new_document_dialog.cpp (it grew a preset
// card grid); create_new_document here just consumes its settings.

#include "ui/main_window.hpp"
#include "ui/main_window_shared.hpp"

#include "core/blend_math.hpp"
#include "core/layer_metadata.hpp"
#include "core/smart_object.hpp"
#include "core/text_warp.hpp"
#include "core/warp_mesh.hpp"
#include "core/layer_render_utils.hpp"
#include "core/layer_tree.hpp"
#include "core/palette_presets.hpp"
#include "core/pattern_presets.hpp"
#include "core/pixel_tools.hpp"
#include "formats/palette_io.hpp"
#include "filters/builtin_filters.hpp"
#include "formats/aseprite_document_io.hpp"
#include "formats/bmp_document_io.hpp"
#include "formats/heif_document_io.hpp"
#include "formats/raw_document_io.hpp"
#include "plugins/legacy_photoshop_adapter.hpp"
#include "psd/psd_document_io.hpp"
#include "psd/psd_filter_effects.hpp"
#include "psd/psd_smart_objects.hpp"
#include "ui/action_icons.hpp"
#include "ui/app_settings.hpp"
#include "render/compositor.hpp"
#include "ui/blend_mode_ui.hpp"
#include "ui/brush_dynamics_popup.hpp"
#include "ui/brush_presets.hpp"
#include "ui/brush_tip_library.hpp"
#include "ui/brush_tip_manager_dialog.hpp"
#include "ui/brush_tip_picker.hpp"
#include "ui/default_brush_tips.hpp"
#include "ui/compatibility_report.hpp"
#include "ui/image_document_io.hpp"
#include "ui/image_save_options_dialog.hpp"
#include "ui/raw_develop_dialog.hpp"
#include "ui/filter_workflows.hpp"
#include "ui/gradient_stops_editor.hpp"
#include "ui/gradient_library.hpp"
#include "ui/gradient_manager_dialog.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/document_float_window.hpp"
#include "ui/font_picker.hpp"
#include "ui/hotkey_editor.hpp"
#include "ui/edit_conversions.hpp"
#include "ui/color_panel.hpp"
#include "ui/layer_style_dialog.hpp"
#include "ui/layer_list_widget.hpp"
#include "ui/localization.hpp"
#include "ui/measurement_units.hpp"
#include "ui/new_document_dialog.hpp"
#include "ui/palette_convert_dialog.hpp"
#include "ui/palette_panel.hpp"
#include "ui/pattern_library.hpp"
#include "ui/photo_pattern_presets.hpp"
#include "ui/style_library.hpp"
#include "ui/print_dialog.hpp"
#include "ui/smart_object_render.hpp"
#include "ui/scanner_import.hpp"
#include "ui/sprite_sheet_dialog.hpp"
#include "ui/tile_preview_window.hpp"
#include "ui/warp_text_dialog.hpp"
#include "ui/qt_geometry.hpp"
#include "ui/splash_dialog.hpp"
#include "ui/update_checker.hpp"
#include "ui/zoom_status_bar.hpp"
#include "support/string_utils.hpp"

#include <QAbstractItemView>
#include <QAbstractItemModel>
#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QAbstractTextDocumentLayout>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QBrush>
#include <QBuffer>
#include <QButtonGroup>
#include <QByteArray>
#include <QDateTime>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QCursor>
#include <QColorSpace>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLayout>
#include <QResizeEvent>
#include <QIcon>
#include <QImageReader>
#include <QInputDialog>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLabel>
#include <QKeySequence>
#include <QListWidget>
#include <QLinearGradient>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QMessageBox>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPushButton>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPolygon>
#include <QPointer>
#include <QProcess>
#include <QProgressDialog>
#include <QRegion>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QScopeGuard>
#include <QSettings>
#include <QShowEvent>
#include <QStandardPaths>
#include <QStandardItem>
#include <QStyledItemDelegate>
#include <QMutex>
#include <QRawFont>
#include <QTextCharFormat>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextEdit>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextLayout>
#include <QTextOption>
#include <QSignalBlocker>
#include <QSize>
#include <QSizePolicy>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QStringList>
#include <QStackedWidget>
#include <QStyle>
#include <QStyleOption>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QToolTip>
#include <QTransform>
#include <QUrl>
#include <QVariant>
#include <QVBoxLayout>
#include <QWindow>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <functional>
#include <future>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <tchar.h>
#include <tpcshrd.h>
#endif

#ifndef PATCHY_VERSION
#define PATCHY_VERSION "0.0.0"
#endif

// Icon resources live in the static patchy_ui library; force registration before first use.
int qInitResources_icons();

namespace patchy::ui {

namespace {

// New documents open fully visible: an oversized canvas fits to the view like
// file-open does, but smaller ones stay at 100% (a fresh blank canvas zoomed
// past 100% reads as broken). No-op while the window has no size (startup).
void fit_new_document_view(CanvasWidget* canvas) {
  if (canvas == nullptr) {
    return;
  }
  canvas->fit_to_view();
  if (canvas->zoom() > 1.0) {
    canvas->set_zoom(1.0);
    canvas->center_document_in_view();
  }
}

struct CanvasSizeSettings {
  std::int32_t width{0};
  std::int32_t height{0};
  CanvasAnchor anchor{CanvasAnchor::Center};
  QColor extension_color{Qt::white};
};

struct ImageSizeSettings {
  std::int32_t width{0};
  std::int32_t height{0};
  double resolution{300.0};
  bool resample{true};
};

QString format_image_size_bytes(std::int32_t width, std::int32_t height, PixelFormat format) {
  const auto bytes = static_cast<double>(std::max<std::int32_t>(0, width)) *
                     static_cast<double>(std::max<std::int32_t>(0, height)) *
                     static_cast<double>(bytes_per_pixel(format));
  if (bytes >= 1024.0 * 1024.0) {
    return QObject::tr("%1M").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
  }
  return QObject::tr("%1K").arg(bytes / 1024.0, 0, 'f', 1);
}

QPixmap image_size_preview_pixmap(const Document& document, QSize preview_size) {
  QPixmap pixmap(preview_size);
  pixmap.fill(QColor(22, 22, 22));

  QPainter painter(&pixmap);
  constexpr int kTileSize = 12;
  for (int y = 0; y < preview_size.height(); y += kTileSize) {
    for (int x = 0; x < preview_size.width(); x += kTileSize) {
      const bool light_tile = ((x / kTileSize) + (y / kTileSize)) % 2 == 0;
      painter.fillRect(QRect(x, y, kTileSize, kTileSize), light_tile ? QColor(228, 228, 228) : QColor(176, 176, 176));
    }
  }

  const auto image = qimage_from_document(document, true);
  if (!image.isNull()) {
    const auto scaled = image.scaled(preview_size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const QPoint position((preview_size.width() - scaled.width()) / 2, (preview_size.height() - scaled.height()) / 2);
    painter.drawImage(position, scaled);
  }
  painter.setPen(QPen(QColor(30, 30, 30), 1));
  painter.drawRect(pixmap.rect().adjusted(0, 0, -1, -1));
  painter.end();
  return pixmap;
}

QString canvas_color_swatch_style(QColor color) {
  return QStringLiteral("QPushButton#canvasSizeExtensionColorSwatch { background: rgb(%1, %2, %3); "
                        "border: 1px solid #9a9a9a; border-radius: 3px; padding: 0; min-width: 49px; "
                        "max-width: 49px; min-height: 24px; max-height: 24px; } "
                        "QPushButton#canvasSizeExtensionColorSwatch:hover { border-color: #c8c8c8; }")
      .arg(color.red())
      .arg(color.green())
      .arg(color.blue());
}

std::optional<ImageSizeSettings> request_image_size_settings(QWidget* parent, const Document& document) {
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("patchyImageSizeDialog"));
  dialog.setWindowTitle(QObject::tr("Image Size"));
  dialog.setStyleSheet(dialog.styleSheet() + QStringLiteral(R"(
    QDialog#patchyImageSizeDialog {
      background: #555555;
      color: #f2f2f2;
    }
    QLabel {
      background: transparent;
      color: #f2f2f2;
    }
    QLabel#imageSizePreview {
      background: #1e1e1e;
      border: 1px solid #242424;
    }
    QLabel#imageSizeUpscaleLabel {
      color: #d8d8d8;
    }
    QSpinBox, QComboBox {
      background: #4a4a4a;
      border: 1px solid #686868;
      color: #ffffff;
      min-height: 24px;
      padding: 1px 6px;
    }
    QSpinBox:focus {
      border: 1px solid #1473e6;
      background: #3f3f3f;
    }
    QToolButton#imageSizeLinkButton {
      background: #4a4a4a;
      border: 1px solid #686868;
      min-width: 24px;
      max-width: 24px;
      min-height: 46px;
      max-height: 46px;
      padding: 0;
    }
    QToolButton#imageSizeLinkButton:checked {
      border-color: #9abbe7;
      background: #424f5f;
    }
    QDialogButtonBox QPushButton {
      background: #555555;
      border: 1px solid #8b8b8b;
      border-radius: 13px;
      color: #ffffff;
      min-width: 130px;
      min-height: 24px;
      padding: 0 18px;
    }
    QDialogButtonBox QPushButton:hover {
      border-color: #b5b5b5;
      background: #606060;
    }
  )"));
  dialog.resize(632, 386);

  auto* root = new QVBoxLayout(&dialog);
  root->setContentsMargins(7, 10, 7, 10);
  root->setSpacing(10);

  auto* body = new QHBoxLayout();
  body->setSpacing(36);
  root->addLayout(body, 1);

  auto* preview = new QLabel(&dialog);
  preview->setObjectName(QStringLiteral("imageSizePreview"));
  preview->setFixedSize(276, 304);
  preview->setAlignment(Qt::AlignCenter);
  preview->setPixmap(image_size_preview_pixmap(document, preview->size()));
  body->addWidget(preview, 0, Qt::AlignTop);

  auto* controls = new QWidget(&dialog);
  auto* controls_layout = new QVBoxLayout(controls);
  controls_layout->setContentsMargins(0, 0, 0, 0);
  controls_layout->setSpacing(8);
  body->addWidget(controls, 1, Qt::AlignTop);

  auto* grid = new QGridLayout();
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setHorizontalSpacing(8);
  grid->setVerticalSpacing(7);
  grid->setColumnMinimumWidth(0, 72);
  grid->setColumnMinimumWidth(1, 28);
  grid->setColumnMinimumWidth(2, 72);
  grid->setColumnMinimumWidth(3, 128);
  controls_layout->addLayout(grid);

  auto* image_size_value = new QLabel(&dialog);
  image_size_value->setObjectName(QStringLiteral("imageSizeSizeLabel"));
  auto* dimensions_value = new QLabel(&dialog);
  dimensions_value->setObjectName(QStringLiteral("imageSizeDimensionsLabel"));

  grid->addWidget(new QLabel(QObject::tr("Image Size:"), &dialog), 0, 0, Qt::AlignRight | Qt::AlignVCenter);
  grid->addWidget(image_size_value, 0, 1, 1, 3);
  grid->addWidget(new QLabel(QObject::tr("Dimensions:"), &dialog), 1, 0, Qt::AlignRight | Qt::AlignVCenter);
  grid->addWidget(dimensions_value, 1, 1, 1, 3);

  auto* fit = new QComboBox(&dialog);
  fit->setObjectName(QStringLiteral("imageSizeFitCombo"));
  fit->addItem(QObject::tr("Original Size"), QSize(document.width(), document.height()));
  fit->addItem(QObject::tr("Fit 640 x 480"), QSize(640, 480));
  fit->addItem(QObject::tr("Fit 1024 x 768"), QSize(1024, 768));
  fit->addItem(QObject::tr("Fit 1920 x 1080"), QSize(1920, 1080));
  grid->addWidget(new QLabel(QObject::tr("Fit To:"), &dialog), 2, 0, Qt::AlignRight | Qt::AlignVCenter);
  grid->addWidget(fit, 2, 1, 1, 3);

  // Canonical state, Photoshop's model: integer pixel dimensions plus resolution.
  // The spins display conversions in their selected unit and every edit routes back
  // through this state; pixels stay the stored truth.
  const auto sanitized_initial_resolution =
      std::isfinite(document.print_settings().horizontal_ppi) && document.print_settings().horizontal_ppi > 0.0
          ? std::clamp(document.print_settings().horizontal_ppi, 1.0, 9999.0)
          : 300.0;
  struct ImageSizeState {
    int pixel_width;
    int pixel_height;
    double ppi;
  };
  ImageSizeState state{document.width(), document.height(), sanitized_initial_resolution};

  auto* width = new QDoubleSpinBox(&dialog);
  width->setObjectName(QStringLiteral("imageSizeWidthSpin"));
  configure_dialog_spinbox(width, 72);
  auto* height = new QDoubleSpinBox(&dialog);
  height->setObjectName(QStringLiteral("imageSizeHeightSpin"));
  configure_dialog_spinbox(height, 72);

  const auto populate_dimension_units = [](QComboBox* combo) {
    for (const auto unit : {MeasurementUnit::Percent, MeasurementUnit::Pixels, MeasurementUnit::Inches,
                            MeasurementUnit::Centimeters, MeasurementUnit::Millimeters, MeasurementUnit::Points}) {
      combo->addItem(measurement_unit_name(unit), static_cast<int>(unit));
    }
    combo->setCurrentIndex(combo->findData(static_cast<int>(MeasurementUnit::Pixels)));
  };
  auto* width_unit = new QComboBox(&dialog);
  width_unit->setObjectName(QStringLiteral("imageSizeWidthUnitCombo"));
  populate_dimension_units(width_unit);
  auto* height_unit = new QComboBox(&dialog);
  height_unit->setObjectName(QStringLiteral("imageSizeHeightUnitCombo"));
  populate_dimension_units(height_unit);

  auto* link = new QToolButton(&dialog);
  link->setObjectName(QStringLiteral("imageSizeLinkButton"));
  link->setIcon(simple_icon(QStringLiteral("link"), QColor(220, 226, 235)));
  link->setIconSize(QSize(18, 18));
  link->setCheckable(true);
  link->setChecked(true);
  link->setToolTip(QObject::tr("Constrain proportions"));

  grid->addWidget(new QLabel(QObject::tr("Width:"), &dialog), 3, 0, Qt::AlignRight | Qt::AlignVCenter);
  grid->addWidget(link, 3, 1, 2, 1, Qt::AlignCenter);
  grid->addWidget(width, 3, 2);
  grid->addWidget(width_unit, 3, 3);
  grid->addWidget(new QLabel(QObject::tr("Height:"), &dialog), 4, 0, Qt::AlignRight | Qt::AlignVCenter);
  grid->addWidget(height, 4, 2);
  grid->addWidget(height_unit, 4, 3);

  auto* resolution = new QDoubleSpinBox(&dialog);
  resolution->setObjectName(QStringLiteral("imageSizeResolutionSpin"));
  resolution->setDecimals(2);
  resolution->setRange(0.01, 9999.0);
  resolution->setValue(state.ppi);
  configure_dialog_spinbox(resolution, 72);
  // Item data is the display divisor: shown value = PPI / divisor (px/cm shows
  // PPI / 2.54). The stored resolution is always PPI, matching PSD resource 1005.
  auto* resolution_unit = new QComboBox(&dialog);
  resolution_unit->setObjectName(QStringLiteral("imageSizeResolutionUnitCombo"));
  resolution_unit->addItem(QObject::tr("Pixels/Inch"), 1.0);
  resolution_unit->addItem(QObject::tr("Pixels/Centimeter"), 2.54);
  grid->addWidget(new QLabel(QObject::tr("Resolution:"), &dialog), 5, 0, Qt::AlignRight | Qt::AlignVCenter);
  grid->addWidget(resolution, 5, 2);
  grid->addWidget(resolution_unit, 5, 3);

  auto* resample = new QCheckBox(QObject::tr("Resample:"), &dialog);
  resample->setObjectName(QStringLiteral("imageSizeResampleCheck"));
  resample->setChecked(true);
  auto* resample_method = new QComboBox(&dialog);
  resample_method->setObjectName(QStringLiteral("imageSizeResampleCombo"));
  resample_method->addItem(QObject::tr("Bicubic Sharper (reduction)"));
  resample_method->addItem(QObject::tr("Bicubic Smoother (enlargement)"));
  resample_method->addItem(QObject::tr("Nearest Neighbor"));
  grid->addWidget(resample, 6, 0, Qt::AlignRight | Qt::AlignVCenter);
  grid->addWidget(resample_method, 6, 1, 1, 3);

  auto* upscale_label = new QLabel(QObject::tr("Create a new, larger document with more detail\n"
                                               "Open in Generative Upscale..."),
                                   &dialog);
  upscale_label->setObjectName(QStringLiteral("imageSizeUpscaleLabel"));
  upscale_label->setWordWrap(true);
  upscale_label->setMinimumHeight(54);
  controls_layout->addSpacing(26);
  controls_layout->addWidget(upscale_label);
  controls_layout->addStretch(1);

  const auto update_summary = [image_size_value, dimensions_value, &state, &document] {
    image_size_value->setText(format_image_size_bytes(state.pixel_width, state.pixel_height, document.format()));
    dimensions_value->setText(QObject::tr("%1 px x %2 px").arg(state.pixel_width).arg(state.pixel_height));
  };

  const auto current_unit = [](QComboBox* combo) {
    return static_cast<MeasurementUnit>(combo->currentData().toInt());
  };
  const double original_width_px = std::max<std::int32_t>(1, document.width());
  const double original_height_px = std::max<std::int32_t>(1, document.height());
  const double aspect_ratio = original_width_px / original_height_px;

  const auto refresh_dimension_spin = [&state, current_unit](QDoubleSpinBox* spin, QComboBox* unit_combo,
                                                             double pixels, double reference_pixels) {
    const QSignalBlocker blocker(spin);
    const auto unit = current_unit(unit_combo);
    spin->setDecimals(measurement_unit_decimals(unit));
    spin->setRange(unit == MeasurementUnit::Pixels ? 1.0 : 0.001, 999999.0);
    spin->setValue(pixels_to_measurement_unit(pixels, unit, state.ppi, reference_pixels));
  };
  const auto refresh_resolution_spin = [&state, resolution, resolution_unit] {
    const QSignalBlocker blocker(resolution);
    resolution->setValue(state.ppi / resolution_unit->currentData().toDouble());
  };
  const auto refresh_all = [&](QDoubleSpinBox* except) {
    if (except != width) {
      refresh_dimension_spin(width, width_unit, state.pixel_width, original_width_px);
    }
    if (except != height) {
      refresh_dimension_spin(height, height_unit, state.pixel_height, original_height_px);
    }
    if (except != resolution) {
      refresh_resolution_spin();
    }
    update_summary();
  };
  refresh_all(nullptr);

  bool updating_dimensions = false;
  const auto handle_dimension_edit = [&](bool editing_width) {
    if (updating_dimensions) {
      return;
    }
    updating_dimensions = true;
    auto* spin = editing_width ? width : height;
    auto* unit_combo = editing_width ? width_unit : height_unit;
    const auto unit = current_unit(unit_combo);
    if (!resample->isChecked()) {
      // Resample off: the pixel dimensions are locked, so a physical-size edit
      // re-derives the resolution instead (Photoshop's W/H/Resolution tri-link).
      const auto pixels = editing_width ? state.pixel_width : state.pixel_height;
      const auto inches = std::max(spin->value(), 0.000001) / measurement_units_per_inch(unit);
      state.ppi = std::clamp(pixels / inches, 0.01, 9999.0);
      refresh_all(spin);
      updating_dimensions = false;
      return;
    }
    const auto reference = editing_width ? original_width_px : original_height_px;
    const auto pixels = std::clamp(
        static_cast<int>(std::lround(measurement_unit_to_pixels(spin->value(), unit, state.ppi, reference))), 1,
        30000);
    if (editing_width) {
      state.pixel_width = pixels;
      if (link->isChecked()) {
        state.pixel_height = std::clamp(static_cast<int>(std::lround(pixels / aspect_ratio)), 1, 30000);
      }
    } else {
      state.pixel_height = pixels;
      if (link->isChecked()) {
        state.pixel_width = std::clamp(static_cast<int>(std::lround(pixels * aspect_ratio)), 1, 30000);
      }
    }
    refresh_all(spin);
    updating_dimensions = false;
  };
  QObject::connect(width, &QDoubleSpinBox::valueChanged, &dialog, [&] { handle_dimension_edit(true); });
  QObject::connect(height, &QDoubleSpinBox::valueChanged, &dialog, [&] { handle_dimension_edit(false); });

  // Photoshop keeps the two dimension units in step; changing one changes both.
  const auto sync_unit_combos = [&](QComboBox* changed, QComboBox* other) {
    {
      const QSignalBlocker blocker(other);
      other->setCurrentIndex(changed->currentIndex());
    }
    refresh_all(nullptr);
  };
  QObject::connect(width_unit, &QComboBox::currentIndexChanged, &dialog,
                   [&](int) { sync_unit_combos(width_unit, height_unit); });
  QObject::connect(height_unit, &QComboBox::currentIndexChanged, &dialog,
                   [&](int) { sync_unit_combos(height_unit, width_unit); });

  QObject::connect(resolution, &QDoubleSpinBox::valueChanged, &dialog, [&](double value) {
    if (updating_dimensions) {
      return;
    }
    updating_dimensions = true;
    const auto new_ppi = std::clamp(value * resolution_unit->currentData().toDouble(), 0.01, 9999.0);
    if (resample->isChecked() && measurement_unit_is_physical(current_unit(width_unit))) {
      // Physical W/H hold their print size, so the pixel dimensions re-derive
      // (Photoshop: raising resolution at a fixed print size adds pixels).
      const double width_inches = state.pixel_width / state.ppi;
      const double height_inches = state.pixel_height / state.ppi;
      state.ppi = new_ppi;
      state.pixel_width = std::clamp(static_cast<int>(std::lround(width_inches * new_ppi)), 1, 30000);
      state.pixel_height = std::clamp(static_cast<int>(std::lround(height_inches * new_ppi)), 1, 30000);
    } else {
      // Pixel/percent display (or Resample off): the pixels hold and only the
      // resolution metadata moves.
      state.ppi = new_ppi;
    }
    refresh_all(resolution);
    updating_dimensions = false;
  });
  QObject::connect(resolution_unit, &QComboBox::currentIndexChanged, &dialog, [&](int) { refresh_resolution_spin(); });

  QObject::connect(fit, &QComboBox::currentIndexChanged, &dialog, [&](int index) {
    const auto size = fit->itemData(index).toSize();
    if (!size.isValid()) {
      return;
    }
    state.pixel_width = std::clamp(size.width(), 1, 30000);
    state.pixel_height = std::clamp(size.height(), 1, 30000);
    refresh_all(nullptr);
  });
  QObject::connect(resample, &QCheckBox::toggled, &dialog, [&](bool checked) {
    fit->setEnabled(checked);
    link->setEnabled(checked);
    resample_method->setEnabled(checked);
    if (!checked) {
      // Pixels lock to the document's real dimensions: any pending resample reverts
      // (Photoshop behavior), matching the metadata-only apply that follows.
      state.pixel_width = document.width();
      state.pixel_height = document.height();
    }
    // Pixel and Percent entry require resampling; Photoshop flips the unit to Inches
    // when Resample goes off and disables the non-physical choices.
    const auto set_units_enabled = [checked](QComboBox* combo) {
      auto* model = qobject_cast<QStandardItemModel*>(combo->model());
      if (model == nullptr) {
        return;
      }
      for (int index = 0; index < combo->count(); ++index) {
        const auto unit = static_cast<MeasurementUnit>(combo->itemData(index).toInt());
        if (auto* item = model->item(index); item != nullptr && !measurement_unit_is_physical(unit)) {
          item->setEnabled(checked);
        }
      }
    };
    set_units_enabled(width_unit);
    set_units_enabled(height_unit);
    if (!checked && !measurement_unit_is_physical(current_unit(width_unit))) {
      // The sync handler mirrors the change into the height combo and refreshes.
      width_unit->setCurrentIndex(width_unit->findData(static_cast<int>(MeasurementUnit::Inches)));
    } else {
      refresh_all(nullptr);
    }
  });

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  root->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  width->setFocus(Qt::OtherFocusReason);
  width->selectAll();
  if (exec_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }
  return ImageSizeSettings{state.pixel_width, state.pixel_height, state.ppi, resample->isChecked()};
}

std::optional<CanvasSizeSettings> request_canvas_size_settings(QWidget* parent, const Document& document) {
  const auto current_width = document.width();
  const auto current_height = document.height();
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("patchyCanvasSizeDialog"));
  dialog.setWindowTitle(QObject::tr("Canvas Size"));
  dialog.setStyleSheet(dialog.styleSheet() + QStringLiteral(R"(
    QDialog#patchyCanvasSizeDialog {
      background: #555555;
      color: #ffffff;
    }
    QDialog#patchyCanvasSizeDialog QWidget {
      background: #555555;
      color: #ffffff;
    }
    QDialog#patchyCanvasSizeDialog QLabel {
      background: transparent;
      color: #ffffff;
      font-size: 11px;
    }
    QDialog#patchyCanvasSizeDialog QLabel[sectionLabel="true"] {
      font-weight: 700;
    }
    QDialog#patchyCanvasSizeDialog QFrame#canvasSizeSeparator {
      background: #727272;
      color: #727272;
      min-height: 1px;
      max-height: 1px;
      border: 0;
    }
    QDialog#patchyCanvasSizeDialog QSpinBox,
    QDialog#patchyCanvasSizeDialog QComboBox {
      background: #4f4f4f;
      border: 1px solid #767676;
      border-radius: 3px;
      color: #ffffff;
      min-height: 22px;
      padding: 0 8px;
    }
    QDialog#patchyCanvasSizeDialog QSpinBox:focus,
    QDialog#patchyCanvasSizeDialog QComboBox:focus {
      border-color: #9abbe7;
      background: #4a4a4a;
    }
    QDialog#patchyCanvasSizeDialog QComboBox::drop-down {
      border: 0;
      width: 22px;
    }
    QDialog#patchyCanvasSizeDialog QCheckBox {
      background: transparent;
      color: #ffffff;
      font-size: 11px;
      spacing: 7px;
    }
    QDialog#patchyCanvasSizeDialog QCheckBox::indicator {
      width: 11px;
      height: 11px;
      background: #5a5a5a;
      border: 1px solid #8a8a8a;
    }
    QDialog#patchyCanvasSizeDialog QCheckBox::indicator:checked {
      background: #2f75bd;
      border-color: #9abbe7;
      image: url(:/patchy/icons/checkmark.svg);
    }
    QDialog#patchyCanvasSizeDialog QToolButton#canvasSizeAnchorButton {
      background: #5c5c5c;
      border: 1px solid #777777;
      border-radius: 0;
      min-width: 22px;
      max-width: 22px;
      min-height: 22px;
      max-height: 22px;
      padding: 0;
    }
    QDialog#patchyCanvasSizeDialog QToolButton#canvasSizeAnchorButton:hover {
      background: #656565;
      border-color: #9a9a9a;
    }
    QDialog#patchyCanvasSizeDialog QToolButton#canvasSizeAnchorButton:checked {
      background: #4a4a4a;
      border-color: #c8c8c8;
    }
    QDialog#patchyCanvasSizeDialog QPushButton {
      background: #555555;
      border: 1px solid #8c8c8c;
      border-radius: 13px;
      color: #ffffff;
      min-width: 70px;
      min-height: 24px;
      padding: 0 14px;
      font-size: 11px;
      font-weight: 700;
    }
    QDialog#patchyCanvasSizeDialog QPushButton:hover {
      background: #606060;
      border-color: #b7b7b7;
    }
    QDialog#patchyCanvasSizeDialog QPushButton#canvasSizeOkButton {
      border-color: #2d8cff;
      border-width: 2px;
    }
  )"));
  dialog.resize(453, 377);

  auto* root = new QHBoxLayout(&dialog);
  root->setContentsMargins(15, 17, 14, 14);
  root->setSpacing(12);

  auto* content = new QWidget(&dialog);
  content->setObjectName(QStringLiteral("canvasSizeContent"));
  auto* content_layout = new QVBoxLayout(content);
  content_layout->setContentsMargins(0, 0, 0, 0);
  content_layout->setSpacing(7);
  root->addWidget(content, 1, Qt::AlignTop);

  auto* current_size_label =
      new QLabel(QObject::tr("Current Size: %1").arg(format_image_size_bytes(current_width, current_height, document.format())),
                 &dialog);
  current_size_label->setObjectName(QStringLiteral("canvasSizeCurrentSizeLabel"));
  current_size_label->setProperty("sectionLabel", true);
  content_layout->addWidget(current_size_label);

  auto* current_grid = new QGridLayout();
  current_grid->setContentsMargins(0, 0, 0, 0);
  current_grid->setHorizontalSpacing(8);
  current_grid->setVerticalSpacing(5);
  current_grid->setColumnMinimumWidth(0, 52);
  content_layout->addLayout(current_grid);
  current_grid->addWidget(new QLabel(QObject::tr("Width"), &dialog), 0, 0);
  current_grid->addWidget(new QLabel(QObject::tr("%1 px").arg(current_width), &dialog), 0, 1);
  current_grid->addWidget(new QLabel(QObject::tr("Height"), &dialog), 1, 0);
  current_grid->addWidget(new QLabel(QObject::tr("%1 px").arg(current_height), &dialog), 1, 1);

  auto* separator = new QFrame(&dialog);
  separator->setObjectName(QStringLiteral("canvasSizeSeparator"));
  separator->setFrameShape(QFrame::HLine);
  content_layout->addWidget(separator);

  auto* new_size_label = new QLabel(&dialog);
  new_size_label->setObjectName(QStringLiteral("canvasSizeNewSizeLabel"));
  new_size_label->setProperty("sectionLabel", true);
  content_layout->addWidget(new_size_label);

  auto* size_grid = new QGridLayout();
  size_grid->setContentsMargins(0, 0, 42, 0);
  size_grid->setHorizontalSpacing(8);
  size_grid->setVerticalSpacing(7);
  size_grid->setColumnMinimumWidth(0, 38);
  content_layout->addLayout(size_grid);

  auto* width = new QSpinBox(&dialog);
  width->setObjectName(QStringLiteral("canvasSizeWidthSpin"));
  width->setRange(1, 30000);
  width->setValue(current_width);
  configure_dialog_spinbox(width, 84);
  auto* height = new QSpinBox(&dialog);
  height->setObjectName(QStringLiteral("canvasSizeHeightSpin"));
  height->setRange(1, 30000);
  height->setValue(current_height);
  configure_dialog_spinbox(height, 84);

  auto* width_unit = new QComboBox(&dialog);
  width_unit->setObjectName(QStringLiteral("canvasSizeWidthUnitCombo"));
  width_unit->addItem(QObject::tr("Pixels"));
  width_unit->setMinimumWidth(160);
  auto* height_unit = new QComboBox(&dialog);
  height_unit->setObjectName(QStringLiteral("canvasSizeHeightUnitCombo"));
  height_unit->addItem(QObject::tr("Pixels"));
  height_unit->setMinimumWidth(160);

  size_grid->addWidget(new QLabel(QObject::tr("Width"), &dialog), 0, 0, Qt::AlignVCenter);
  size_grid->addWidget(width, 0, 1);
  size_grid->addWidget(width_unit, 0, 2);
  size_grid->addWidget(new QLabel(QObject::tr("Height"), &dialog), 1, 0, Qt::AlignVCenter);
  size_grid->addWidget(height, 1, 1);
  size_grid->addWidget(height_unit, 1, 2);

  auto* relative = new QCheckBox(QObject::tr("Relative to current dimension"), &dialog);
  relative->setObjectName(QStringLiteral("canvasSizeRelativeCheck"));
  auto* relative_row = new QHBoxLayout();
  relative_row->setContentsMargins(45, 7, 0, 0);
  relative_row->setSpacing(0);
  relative_row->addWidget(relative);
  relative_row->addStretch(1);
  content_layout->addLayout(relative_row);

  auto* anchor_row = new QHBoxLayout();
  anchor_row->setContentsMargins(0, 4, 0, 0);
  anchor_row->setSpacing(10);
  content_layout->addLayout(anchor_row);
  anchor_row->addWidget(new QLabel(QObject::tr("Anchor"), &dialog), 0, Qt::AlignTop);

  auto* anchor_grid_widget = new QWidget(&dialog);
  anchor_grid_widget->setObjectName(QStringLiteral("canvasSizeAnchorGrid"));
  auto* anchor_grid = new QGridLayout(anchor_grid_widget);
  anchor_grid->setContentsMargins(0, 0, 0, 0);
  anchor_grid->setSpacing(0);
  anchor_row->addWidget(anchor_grid_widget, 0, Qt::AlignLeft | Qt::AlignTop);
  anchor_row->addStretch(1);

  auto* anchor_group = new QButtonGroup(&dialog);
  anchor_group->setExclusive(true);
  const std::array<std::pair<CanvasAnchor, QString>, 9> anchors{{
      {CanvasAnchor::TopLeft, QObject::tr("Anchor top left")},
      {CanvasAnchor::Top, QObject::tr("Anchor top")},
      {CanvasAnchor::TopRight, QObject::tr("Anchor top right")},
      {CanvasAnchor::Left, QObject::tr("Anchor left")},
      {CanvasAnchor::Center, QObject::tr("Anchor center")},
      {CanvasAnchor::Right, QObject::tr("Anchor right")},
      {CanvasAnchor::BottomLeft, QObject::tr("Anchor bottom left")},
      {CanvasAnchor::Bottom, QObject::tr("Anchor bottom")},
      {CanvasAnchor::BottomRight, QObject::tr("Anchor bottom right")},
  }};
  for (int index = 0; index < static_cast<int>(anchors.size()); ++index) {
    auto* button = new QToolButton(anchor_grid_widget);
    button->setObjectName(QStringLiteral("canvasSizeAnchorButton"));
    button->setIcon(canvas_anchor_icon(anchors[static_cast<std::size_t>(index)].first));
    button->setIconSize(QSize(18, 18));
    button->setToolTip(anchors[static_cast<std::size_t>(index)].second);
    button->setCheckable(true);
    anchor_group->addButton(button, static_cast<int>(anchors[static_cast<std::size_t>(index)].first));
    anchor_grid->addWidget(button, index / 3, index % 3);
    if (anchors[static_cast<std::size_t>(index)].first == CanvasAnchor::Center) {
      button->setChecked(true);
    }
  }

  auto* extension_row = new QHBoxLayout();
  extension_row->setContentsMargins(0, 6, 0, 0);
  extension_row->setSpacing(8);
  content_layout->addLayout(extension_row);
  auto* extension_label = new QLabel(QObject::tr("Canvas extension color"), &dialog);
  extension_label->setFixedWidth(116);
  extension_row->addWidget(extension_label, 0, Qt::AlignVCenter);

  auto* extension_color = new QComboBox(&dialog);
  extension_color->setObjectName(QStringLiteral("canvasSizeExtensionColorCombo"));
  extension_color->addItem(QObject::tr("Other..."), QColor(Qt::white));
  extension_color->addItem(QObject::tr("White"), QColor(Qt::white));
  extension_color->addItem(QObject::tr("Black"), QColor(Qt::black));
  extension_color->addItem(QObject::tr("Gray"), QColor(128, 128, 128));
  extension_color->setFixedWidth(154);
  extension_row->addWidget(extension_color, 0);

  auto* color_swatch = new QPushButton(&dialog);
  color_swatch->setObjectName(QStringLiteral("canvasSizeExtensionColorSwatch"));
  color_swatch->setAccessibleName(QObject::tr("Canvas extension color"));
  color_swatch->setToolTip(QObject::tr("Choose canvas extension color"));
  color_swatch->setCursor(Qt::PointingHandCursor);
  color_swatch->setFocusPolicy(Qt::StrongFocus);
  color_swatch->setFixedSize(49, 24);
  extension_row->addWidget(color_swatch);
  content_layout->addStretch(1);

  QColor extension_color_value(Qt::white);
  const auto update_swatch = [&extension_color_value, color_swatch] {
    color_swatch->setStyleSheet(canvas_color_swatch_style(extension_color_value));
  };
  update_swatch();

  const auto choose_extension_color = [extension_color, &dialog, &extension_color_value, update_swatch] {
    const auto selected = QColorDialog::getColor(extension_color_value, &dialog, QObject::tr("Canvas Extension Color"));
    if (!selected.isValid()) {
      update_swatch();
      return;
    }
    extension_color_value = selected;
    extension_color->setItemData(0, selected);
    extension_color->setCurrentIndex(0);
    update_swatch();
  };

  QObject::connect(extension_color, &QComboBox::activated, &dialog, [extension_color, &dialog, &extension_color_value,
                                                                      update_swatch, choose_extension_color](int index) {
    if (index == 0) {
      choose_extension_color();
      return;
    }
    extension_color_value = extension_color->itemData(index).value<QColor>();
    update_swatch();
  });
  QObject::connect(color_swatch, &QPushButton::clicked, &dialog, choose_extension_color);

  const auto target_width = [current_width, width, relative] {
    return relative->isChecked() ? current_width + width->value() : width->value();
  };
  const auto target_height = [current_height, height, relative] {
    return relative->isChecked() ? current_height + height->value() : height->value();
  };
  const auto update_summary = [new_size_label, target_width, target_height, &document] {
    new_size_label->setText(
        QObject::tr("New Size: %1").arg(format_image_size_bytes(target_width(), target_height(), document.format())));
  };
  update_summary();

  QObject::connect(width, &QSpinBox::valueChanged, &dialog, [update_summary](int) { update_summary(); });
  QObject::connect(height, &QSpinBox::valueChanged, &dialog, [update_summary](int) { update_summary(); });
  QObject::connect(relative, &QCheckBox::toggled, &dialog, [current_width, current_height, width, height,
                                                            update_summary](bool checked) {
    const auto absolute_width = checked ? width->value() : current_width + width->value();
    const auto absolute_height = checked ? height->value() : current_height + height->value();
    const QSignalBlocker block_width(width);
    const QSignalBlocker block_height(height);
    if (checked) {
      width->setRange(1 - current_width, 30000 - current_width);
      height->setRange(1 - current_height, 30000 - current_height);
      width->setValue(absolute_width - current_width);
      height->setValue(absolute_height - current_height);
    } else {
      width->setRange(1, 30000);
      height->setRange(1, 30000);
      width->setValue(std::clamp(absolute_width, 1, 30000));
      height->setValue(std::clamp(absolute_height, 1, 30000));
    }
    update_summary();
  });

  auto* button_column = new QWidget(&dialog);
  button_column->setObjectName(QStringLiteral("canvasSizeButtonColumn"));
  auto* button_layout = new QVBoxLayout(button_column);
  button_layout->setContentsMargins(0, 1, 0, 0);
  button_layout->setSpacing(9);
  root->addWidget(button_column, 0, Qt::AlignTop);

  auto* ok = new QPushButton(QObject::tr("OK"), &dialog);
  ok->setObjectName(QStringLiteral("canvasSizeOkButton"));
  ok->setDefault(true);
  auto* cancel = new QPushButton(QObject::tr("Cancel"), &dialog);
  cancel->setObjectName(QStringLiteral("canvasSizeCancelButton"));
  ok->setFixedSize(73, 28);
  cancel->setFixedSize(73, 28);
  button_layout->addWidget(ok);
  button_layout->addWidget(cancel);
  button_layout->addStretch(1);
  QObject::connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);
  QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);

  width->setFocus(Qt::OtherFocusReason);
  width->selectAll();

  if (exec_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }
  const auto checked_anchor =
      anchor_group->checkedId() < 0 ? CanvasAnchor::Center : static_cast<CanvasAnchor>(anchor_group->checkedId());
  return CanvasSizeSettings{target_width(), target_height(), checked_anchor, extension_color_value};
}

}  // namespace

void MainWindow::reset_document(std::int32_t width, std::int32_t height, QColor background, QString history_label,
                                double resolution_ppi) {
  Document new_document(width, height, PixelFormat::rgb8());
  if (std::isfinite(resolution_ppi) && resolution_ppi > 0.0) {
    new_document.print_settings().horizontal_ppi = std::clamp(resolution_ppi, 1.0, 9999.0);
    new_document.print_settings().vertical_ppi = std::clamp(resolution_ppi, 1.0, 9999.0);
  }
  const auto background_format = background.alpha() == 0 ? PixelFormat::rgba8() : PixelFormat::rgb8();
  auto& background_layer = new_document.add_pixel_layer("Background",
                                                        make_solid_pixels(new_document.width(), new_document.height(),
                                                                          background, background_format));
  set_layer_locks_position(background_layer, true);
  new_document.add_pixel_layer("Paint Layer", make_solid_pixels(new_document.width(), new_document.height(), QColor(0, 0, 0, 0),
                                                             PixelFormat::rgba8()));
  add_document_session(std::move(new_document), tr("Untitled-%1").arg(sessions_.size() + 1));
  auto& active_session = session();
  active_session.undo_stack.clear();
  active_session.redo_stack.clear();
  if (history_list_ != nullptr) {
    history_list_->clear();
  }
  update_history(std::move(history_label));
  refresh_layer_list();
  refresh_layer_controls();
  update_undo_redo_actions();
  statusBar()->showMessage(tr("Created %1 x %2 document").arg(width).arg(height));
}

void MainWindow::create_clipboard_document(const QImage& image, QString history_label) {
  if (image.isNull()) {
    statusBar()->showMessage(tr("Clipboard does not contain an image"));
    return;
  }

  auto pixels = pixels_from_image_rgba(image);
  Document new_document(pixels.width(), pixels.height(), PixelFormat::rgba8());
  // Photoshop's Clipboard preset convention: pasted pixels carry no reliable density.
  new_document.print_settings().horizontal_ppi = kUntaggedImportPpi;
  new_document.print_settings().vertical_ppi = kUntaggedImportPpi;
  new_document.add_pixel_layer(tr("Clipboard Image").toStdString(), std::move(pixels));
  add_document_session(std::move(new_document), tr("Untitled-%1").arg(sessions_.size() + 1));
  fit_new_document_view(canvas_);
  auto& active_session = session();
  active_session.undo_stack.clear();
  active_session.redo_stack.clear();
  if (history_list_ != nullptr) {
    history_list_->clear();
  }
  update_history(std::move(history_label));
  refresh_layer_list();
  refresh_layer_controls();
  update_undo_redo_actions();
  statusBar()->showMessage(tr("Created %1 x %2 document").arg(image.width()).arg(image.height()));
}

void MainWindow::create_new_document() {
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return;
  }
  const auto settings = request_new_document_settings(this);
  if (!settings.has_value()) {
    return;
  }
  if (settings->from_clipboard) {
    create_clipboard_document(settings->clipboard_image, tr("New document"));
    return;
  }
  reset_document(settings->width, settings->height, settings->background, tr("New document"),
                 settings->resolution_ppi);
  // Only the dialog path fits: reset_document is also the startup path, where the
  // canvas only has its pre-layout default size and a fit would stick a bogus zoom.
  fit_new_document_view(canvas_);
}

void MainWindow::resize_image_dialog() {
  auto& doc = document();
  const auto settings = request_image_size_settings(this, doc);
  if (!settings.has_value()) {
    return;
  }
  if (!settings->resample) {
    const auto resolution = static_cast<double>(settings->resolution);
    if (std::abs(doc.print_settings().horizontal_ppi - resolution) > 0.01 ||
        std::abs(doc.print_settings().vertical_ppi - resolution) > 0.01) {
      push_undo_snapshot(tr("Print resolution"));
      doc.print_settings().horizontal_ppi = resolution;
      doc.print_settings().vertical_ppi = resolution;
      refresh_document_info();
    }
    statusBar()->showMessage(tr("Image size unchanged; print resolution set to %1 ppi").arg(settings->resolution));
    return;
  }
  const auto resolution = static_cast<double>(settings->resolution);
  const bool dimensions_changed = settings->width != doc.width() || settings->height != doc.height();
  const bool resolution_changed = std::abs(doc.print_settings().horizontal_ppi - resolution) > 0.01 ||
                                  std::abs(doc.print_settings().vertical_ppi - resolution) > 0.01;
  if (!dimensions_changed && !resolution_changed) {
    return;
  }
  if (dimensions_changed &&
      document_contains_smart_objects(std::as_const(doc))) {
    statusBar()->showMessage(
        tr("Rasterize Smart Objects before changing document geometry"));
    return;
  }

  push_undo_snapshot(tr("Image size"));
  if (dimensions_changed) {
    resize_image_and_layers(doc, settings->width, settings->height);
    canvas_->clear_selection();
    const auto previous_channel_target = canvas_->layer_edit_target();
    const auto previous_channel_id = canvas_->active_document_channel_id();
    const auto previous_channel_display = canvas_->mask_display_mode();
    canvas_->set_document(&doc);
    restore_channel_target_after_document_reset(previous_channel_target, previous_channel_id,
                                                previous_channel_display);
    refresh_layer_list();
    refresh_layer_controls();
  }
  doc.print_settings().horizontal_ppi = resolution;
  doc.print_settings().vertical_ppi = resolution;
  refresh_document_info();
  statusBar()->showMessage(tr("Image %1 x %2 px (%3 x %4 in) at %5 ppi")
                               .arg(settings->width)
                               .arg(settings->height)
                               .arg(settings->width / resolution, 0, 'f', 2)
                               .arg(settings->height / resolution, 0, 'f', 2)
                               .arg(settings->resolution));
}

void MainWindow::resize_canvas_dialog() {
  auto& doc = document();
  const auto settings = request_canvas_size_settings(this, doc);
  if (!settings.has_value()) {
    return;
  }
  if (settings->width == doc.width() && settings->height == doc.height()) {
    return;
  }
  if (document_contains_smart_objects(std::as_const(doc))) {
    statusBar()->showMessage(
        tr("Rasterize Smart Objects before changing document geometry"));
    return;
  }

  push_undo_snapshot(tr("Canvas size"));
  resize_canvas_and_layers(doc, settings->width, settings->height, settings->anchor, edit_color(settings->extension_color));
  canvas_->clear_selection();
  const auto previous_channel_target = canvas_->layer_edit_target();
  const auto previous_channel_id = canvas_->active_document_channel_id();
  const auto previous_channel_display = canvas_->mask_display_mode();
  canvas_->set_document(&doc);
  restore_channel_target_after_document_reset(previous_channel_target, previous_channel_id,
                                              previous_channel_display);
  refresh_layer_list();
  refresh_layer_controls();
  refresh_document_info();
  statusBar()->showMessage(tr("Canvas %1 x %2").arg(settings->width).arg(settings->height));
}

}  // namespace patchy::ui
