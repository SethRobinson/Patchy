// MainWindow's legacy Photoshop plug-in support, split out of main_window.cpp:
// scan_legacy_plugins, load_bundled_legacy_plugins, register_legacy_plugin_path,
// add_legacy_plugin_action, run_legacy_plugin, and the anonymous-namespace
// helper legacy_plugin_kind_name only they use.
// Pure function moves from main_window.cpp; behavior must stay identical.

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
#include "ui/palette_convert_dialog.hpp"
#include "ui/palette_panel.hpp"
#include "ui/pattern_library.hpp"
#include "ui/photo_pattern_presets.hpp"
#include "ui/style_library.hpp"
#include "ui/print_dialog.hpp"
#include "ui/smart_object_render.hpp"
#include "ui/scanner_import.hpp"
#include "ui/sprite_sheet_dialog.hpp"
#include "ui/start_panel.hpp"
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

std::string legacy_plugin_kind_name(LegacyPhotoshopPluginKind kind) {
  switch (kind) {
    case LegacyPhotoshopPluginKind::Filter8bf:
      return "filter";
    case LegacyPhotoshopPluginKind::Format8bi:
      return "file-format";
    case LegacyPhotoshopPluginKind::Automation8li:
      return "automation";
    case LegacyPhotoshopPluginKind::Unknown:
      return "unknown";
  }
  return "unknown";
}

}  // namespace

void MainWindow::scan_legacy_plugins() {
  const auto paths =
      get_open_file_names(this, tr("Scan Legacy Photoshop Plug-ins"), QString(),
                          tr("Photoshop Plug-ins (*.8bf *.8bi *.8li);;All Files (*.*)"), nullptr,
                          QStringLiteral("legacyPluginScanFileDialog"));
  if (paths.isEmpty()) {
    return;
  }

  QStringList report;
  int available = 0;
  for (const auto& path : paths) {
    if (register_legacy_plugin_path(path, &report)) {
      ++available;
    }
  }

  show_information_message(this, tr("Legacy Photoshop Plug-ins"),
                           tr("%1 plug-in action(s) available under Plug-ins > Legacy Photoshop Plug-ins.\n\n%2")
                               .arg(available)
                               .arg(report.join('\n')),
                           QStringLiteral("legacyPluginScanMessageBox"));
}

void MainWindow::load_bundled_legacy_plugins() {
  const QDir fixture_dir(QCoreApplication::applicationDirPath() + QStringLiteral("/test-fixtures/photoshop-plugins"));
  if (!fixture_dir.exists()) {
    return;
  }

  const auto files =
      fixture_dir.entryInfoList({QStringLiteral("*.8bf"), QStringLiteral("*.8bi"), QStringLiteral("*.8li")},
                                QDir::Files | QDir::Readable, QDir::Name);
  for (const auto& file : files) {
    register_legacy_plugin_path(file.absoluteFilePath());
  }
}

bool MainWindow::register_legacy_plugin_path(const QString& path, QStringList* report) {
  LegacyPhotoshopAdapter adapter;
  const auto probe = adapter.probe(path.toStdString());
  const auto file_name = QFileInfo(path).fileName();
  if (report != nullptr) {
    *report << tr("%1: %2 (%3, %4)")
                   .arg(file_name, QString::fromStdString(probe.reason),
                        QString::fromStdString(legacy_plugin_kind_name(probe.kind)),
                        QString::fromStdString(probe.architecture));
  }
  if (!probe.supported) {
    return false;
  }

  const auto identifier = "legacy.photoshop." + QFileInfo(path).completeBaseName().toStdString();
  PluginDescriptor descriptor;
  descriptor.kind = probe.kind == LegacyPhotoshopPluginKind::Format8bi ? PATCHY_PLUGIN_FILE_FORMAT
                                                                        : PATCHY_PLUGIN_FILTER;
  descriptor.identifier = identifier;
  descriptor.display_name = QFileInfo(path).completeBaseName().toStdString();
  descriptor.path = path.toStdString();
  try {
    if (plugin_host_.find(identifier) == nullptr) {
      plugin_host_.register_plugin(std::move(descriptor));
    }
    if (const auto* registered = plugin_host_.find(identifier); registered != nullptr) {
      add_legacy_plugin_action(*registered);
      return true;
    }
  } catch (const std::exception&) {
  }
  return false;
}

void MainWindow::add_legacy_plugin_action(const PluginDescriptor& descriptor) {
  if (legacy_plugins_menu_ == nullptr) {
    return;
  }
  const auto identifier = QString::fromStdString(descriptor.identifier);
  for (auto* action : legacy_plugins_menu_->actions()) {
    if (action->data().toString() == identifier) {
      return;
    }
  }

  auto* action = legacy_plugins_menu_->addAction(QString::fromStdString(descriptor.display_name));
  action->setData(identifier);
  action->setObjectName(QStringLiteral("legacyPluginAction"));
  action->setProperty("patchy.channelViewBlocked", true);
  action->setIcon(simple_icon(QStringLiteral("8BF"), QColor(105, 185, 255)));
  action->setIconVisibleInMenu(false);
  connect(action, &QAction::triggered, this, [this, identifier] { run_legacy_plugin(identifier); });
  register_document_action(action);
  update_document_action_state();
}

void MainWindow::run_legacy_plugin(QString identifier) {
  if (canvas_ != nullptr &&
      (canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::DocumentChannel ||
       canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::ComponentRed ||
       canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::ComponentGreen ||
       canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::ComponentBlue)) {
    show_status_error(tr("Filters are unavailable while viewing a document channel"));
    return;
  }
  const auto* descriptor = plugin_host_.find(identifier.toStdString());
  if (descriptor == nullptr) {
    return;
  }
  const auto active = document().active_layer_id();
  if (!active.has_value()) {
    show_status_error(tr("Select a pixel layer before running the plug-in"));
    return;
  }
  auto* layer = document().find_layer(*active);
  if (layer == nullptr || layer->kind() != LayerKind::Pixel) {
    show_status_error(tr("Select an editable 8-bit pixel layer before running the plug-in"));
    return;
  }
  const auto& source_pixels = std::as_const(*layer).pixels();
  if (source_pixels.format().bit_depth != BitDepth::UInt8 || source_pixels.format().channels < 3) {
    show_status_error(tr("Select an editable 8-bit pixel layer before running the plug-in"));
    return;
  }
  if (layer_is_text(*layer) || layer_is_smart_object(*layer)) {
    show_status_error(
        tr("Rasterize Text and Smart Object layers before editing their pixels"));
    return;
  }
  if (layer_id_locks_image_pixels(*active)) {
    show_status_error(tr("Layer pixels are locked."));
    return;
  }

  const auto name = QString::fromStdString(descriptor->display_name).toLower();
  if (!name.contains(QStringLiteral("greyscale")) && !name.contains(QStringLiteral("white to transparent"))) {
    show_information_message(
        this, tr("Legacy Photoshop Plug-in"),
        tr("%1 was scanned and is available, but this build only has compatibility shims for the "
           "bundled Greyscale and White to Transparent test filters. A full 8BF host still needs "
           "the out-of-process Photoshop SDK adapter.")
            .arg(QString::fromStdString(descriptor->display_name)),
        QStringLiteral("legacyPluginUnavailableMessageBox"));
    return;
  }

  push_undo_snapshot(tr("Legacy plug-in"));
  auto& pixels = layer->pixels();
  const auto channels = pixels.format().channels;
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* px = pixels.pixel(x, y);
      if (name.contains(QStringLiteral("greyscale"))) {
        const auto gray = static_cast<std::uint8_t>(
            std::clamp(std::lround(0.299 * px[0] + 0.587 * px[1] + 0.114 * px[2]), 0L, 255L));
        px[0] = gray;
        px[1] = gray;
        px[2] = gray;
      } else {
        if (channels < 4) {
          continue;
        }
        const auto whiteness = std::min({px[0], px[1], px[2]});
        px[3] = static_cast<std::uint8_t>(255 - whiteness);
      }
    }
  }
  canvas_->document_changed(to_qrect(layer->bounds()));
  statusBar()->showMessage(tr("Applied %1").arg(QString::fromStdString(descriptor->display_name)));
}

}  // namespace patchy::ui
