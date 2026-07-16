// MainWindow's file open/save/import/export implementation, split out of
// main_window.cpp: the file-format table and dialog filters, document loading
// and open/save/export flows, scanner and sprite-sheet import, tile preview,
// printing, the update notice, and the recent files/folders menus. Pure
// function moves from main_window.cpp; behavior must stay identical.

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
#include "ui/tile_preview_window.hpp"
#include "ui/warp_text_dialog.hpp"
#include "ui/qt_geometry.hpp"
#include "ui/start_panel.hpp"
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

constexpr int kOpenProgressTitleReservedWidth = 140;
constexpr int kOpenProgressTitleMinimumFileNameWidth = 180;

constexpr int kMaxRecentFiles = 200;
constexpr int kMaxRecentFolders = 200;
constexpr int kRecentFilesMenuPageSize = 50;

QString elided_open_progress_title_file_name(const QWidget& widget, const QString& file_name) {
  const int available_width =
      std::max(kOpenProgressTitleMinimumFileNameWidth, widget.sizeHint().width() - kOpenProgressTitleReservedWidth);
  return widget.fontMetrics().elidedText(file_name, Qt::ElideMiddle, available_width);
}

void trim_recent_files(QStringList& recent_files) {
  while (recent_files.size() > kMaxRecentFiles) {
    recent_files.removeLast();
  }
}

bool is_photoshop_document_extension(const QString& extension) {
  return extension == QStringLiteral("psd") || extension == QStringLiteral("psb");
}

// Formats whose writers keep the document's layer structure: PSD/PSB and Aseprite save
// the layer tree, and ICO/CUR are exempt because multi-size icons deliberately live as
// one hidden "WxH" layer per size that their writers round-trip.
bool save_extension_preserves_layers(const QString& extension) {
  return is_photoshop_document_extension(extension) || extension == QStringLiteral("aseprite") ||
         extension == QStringLiteral("ase") || extension == QStringLiteral("ico") ||
         extension == QStringLiteral("cur");
}

// True when the session's file came from a format Patchy can only read (camera raw):
// Save can never write back to such a path, so it must become Save As.
bool is_read_only_source_extension(const QString& extension) {
  if (extension.isEmpty()) {
    return false;
  }
  const auto* handler = builtin_format_registry().find_by_extension(extension.toStdString());
  return handler != nullptr && !handler->can_write();
}

// Photoshop's "this format cannot store the document's features" test, reduced to what a
// flat save discards in Patchy: a second layer or group, a non-pixel layer (group,
// adjustment, text, smart object), a hand-authored mask, or layer styles. A single pixel
// layer whose only mask is the document-alpha marker round-trips through flat formats
// (the mask is written back as the file's alpha plane), so it does not count.
bool flat_save_discards_layers(const Document& document) {
  const auto& layers = document.layers();
  if (layers.size() != 1) {
    return !layers.empty();
  }
  const Layer& layer = layers.front();
  if (layer.kind() != LayerKind::Pixel || !layer.children().empty() ||
      layer_pixels_are_procedural(layer)) {
    return true;
  }
  if (layer.mask().has_value() && !layer_mask_is_document_alpha(layer)) {
    return true;
  }
  const auto blend_if_status = layer.blend_if_payload_status();
  const bool has_blend_if = blend_if_status == BlendIfPayloadStatus::Unsupported
                                ? !layer.raw_psd_blending_ranges().empty()
                                : !blend_if_is_identity(layer.blend_if());
  return !layer.layer_style().empty() || has_blend_if;
}

bool layers_have_nondefault_fill_opacity(const std::vector<Layer>& layers) {
  for (const auto& layer : layers) {
    if (layer.kind() != LayerKind::Group && std::abs(layer.fill_opacity() - 1.0F) > 0.0001F) {
      return true;
    }
    if (layer.kind() == LayerKind::Group && layers_have_nondefault_fill_opacity(layer.children())) {
      return true;
    }
  }
  return false;
}

// The single source of truth for the file dialog filters: every open/save/export filter
// string, the supported-extension checks, and the default-extension logic are generated from
// this table. Adding a file format to Patchy means adding one row here (plus wiring the
// registry/writer). Display names go through QCoreApplication::translate("QObject", ...) so
// keep them inside QT_TRANSLATE_NOOP for lupdate.
struct FileFormatEntry {
  const char* display_name;
  QStringList open_extensions;   // advertised in the Open dialog
  QStringList save_extensions;   // empty = read-only; the FIRST one is the default extension
  bool in_save_dialog;           // offered by File > Save As
  bool in_export_dialog;         // offered by File > Export Flat Image
};

const QList<FileFormatEntry>& file_format_entries() {
  static const QList<FileFormatEntry> entries = [] {
    QList<FileFormatEntry> list = {
      {QT_TRANSLATE_NOOP("QObject", "Photoshop Document"),
       {QStringLiteral("psd"), QStringLiteral("psb")},
       {QStringLiteral("psd"), QStringLiteral("psb")},
       true,
       false},
      {QT_TRANSLATE_NOOP("QObject", "PNG Image"),
       {QStringLiteral("png")},
       {QStringLiteral("png")},
       true,
       true},
      {QT_TRANSLATE_NOOP("QObject", "JPEG Image"),
       {QStringLiteral("jpg"), QStringLiteral("jpeg")},
       {QStringLiteral("jpg"), QStringLiteral("jpeg")},
       true,
       true},
      {QT_TRANSLATE_NOOP("QObject", "Bitmap Image"),
       {QStringLiteral("bmp")},
       {QStringLiteral("bmp")},
       true,
       true},
      {QT_TRANSLATE_NOOP("QObject", "TIFF Image"),
       {QStringLiteral("tif"), QStringLiteral("tiff")},
       {QStringLiteral("tif"), QStringLiteral("tiff")},
       true,
       true},
      {QT_TRANSLATE_NOOP("QObject", "WebP Image"),
       {QStringLiteral("webp")},
       {QStringLiteral("webp")},
       true,
       true},
      {QT_TRANSLATE_NOOP("QObject", "GIF Image"),
       {QStringLiteral("gif")},
       {QStringLiteral("gif")},
       true,
       true},
      {QT_TRANSLATE_NOOP("QObject", "Aseprite Image"),
       {QStringLiteral("aseprite"), QStringLiteral("ase")},
       {QStringLiteral("aseprite")},
       true,
       false},
      {QT_TRANSLATE_NOOP("QObject", "Targa Image"),
       {QStringLiteral("tga")},
       {QStringLiteral("tga")},
       true,
       true},
      {QT_TRANSLATE_NOOP("QObject", "Windows Icon"),
       {QStringLiteral("ico")},
       {QStringLiteral("ico")},
       true,
       true},
      {QT_TRANSLATE_NOOP("QObject", "Windows Cursor"),
       {QStringLiteral("cur")},
       {QStringLiteral("cur")},
       true,
       true},
      {QT_TRANSLATE_NOOP("QObject", "PCX Image"),
       {QStringLiteral("pcx")},
       {QStringLiteral("pcx")},
       true,
       true},
      {QT_TRANSLATE_NOOP("QObject", "Amiga IFF Image"),
       {QStringLiteral("lbm"), QStringLiteral("iff"), QStringLiteral("bbm")},
       {QStringLiteral("lbm"), QStringLiteral("iff")},
       true,
       true},
    };
    // Camera raws open through the develop pipeline and are never written: empty
    // save_extensions marks the entry read-only, so Save As/Export skip it. The extension
    // list lives with the raw reader so the registry and the dialogs cannot drift apart.
    QStringList camera_raw_extensions;
    for (const auto& extension : raw::camera_raw_extensions()) {
      camera_raw_extensions.push_back(QString::fromStdString(extension));
    }
    list.push_back({QT_TRANSLATE_NOOP("QObject", "Camera Raw Image"),
                    camera_raw_extensions,
                    {},
                    false,
                    false});
    // HEIF/HEIC is decode-only like camera raw (platform codecs never encode for us and
    // Patchy must not ship an HEVC encoder), so its entry is read-only too.
    QStringList heif_extensions;
    for (const auto& extension : heif::heif_extensions()) {
      heif_extensions.push_back(QString::fromStdString(extension));
    }
    list.push_back({QT_TRANSLATE_NOOP("QObject", "HEIF Image"),
                    heif_extensions,
                    {},
                    false,
                    false});
    return list;
  }();
  return entries;
}

QString extension_patterns(const QStringList& extensions) {
  QStringList patterns;
  patterns.reserve(extensions.size());
  for (const auto& extension : extensions) {
    patterns.push_back(QStringLiteral("*.") + extension);
  }
  return patterns.join(QLatin1Char(' '));
}

QString format_filter_entry(const FileFormatEntry& entry, const QStringList& extensions) {
  return QStringLiteral("%1 (%2)").arg(QCoreApplication::translate("QObject", entry.display_name),
                                       extension_patterns(extensions));
}

QString open_file_filter() {
  QStringList all_extensions;
  QStringList image_extensions;
  for (const auto& entry : file_format_entries()) {
    for (const auto& extension : entry.open_extensions) {
      all_extensions.push_back(extension);
      if (!is_photoshop_document_extension(extension)) {
        image_extensions.push_back(extension);
      }
    }
  }
  return QStringLiteral("%1 (%2);;%3 (*.psd *.psb);;%4 (%5);;%6")
      .arg(QObject::tr("Supported Files"), extension_patterns(all_extensions), QObject::tr("Photoshop Documents"),
           QObject::tr("Images"), extension_patterns(image_extensions), QObject::tr("All Files (*.*)"));
}

QString save_file_filter() {
  QStringList filters;
  for (const auto& entry : file_format_entries()) {
    if (entry.in_save_dialog && !entry.save_extensions.isEmpty()) {
      filters.push_back(format_filter_entry(entry, entry.save_extensions));
    }
  }
  return filters.join(QStringLiteral(";;"));
}

QString export_image_filter() {
  QStringList filters;
  for (const auto& entry : file_format_entries()) {
    if (entry.in_export_dialog && !entry.save_extensions.isEmpty()) {
      filters.push_back(format_filter_entry(entry, entry.save_extensions));
    }
  }
  return filters.join(QStringLiteral(";;"));
}

QString last_open_directory() {
  auto settings = app_settings();
  const auto path = settings.value(QStringLiteral("lastOpenDirectory")).toString();
  if (!path.isEmpty()) {
    const QFileInfo info(path);
    if (info.isDir()) {
      return info.absoluteFilePath();
    }
  }
  return default_file_dialog_directory();
}

void remember_open_directory_for_path(const QString& path) {
  const QFileInfo info(path);
  const auto directory = info.absoluteDir();
  if (!directory.exists()) {
    return;
  }
  auto settings = app_settings();
  settings.setValue(QStringLiteral("lastOpenDirectory"), directory.absolutePath());
}

QString extension_for_path(const QString& path) {
  return QFileInfo(path).suffix().toLower();
}

QString save_file_filter_for_path(const QString& path) {
  const auto extension = extension_for_path(path);
  if (extension.isEmpty()) {
    return {};
  }
  for (const auto& entry : file_format_entries()) {
    if (!entry.in_save_dialog || entry.save_extensions.isEmpty()) {
      continue;
    }
    if (entry.save_extensions.contains(extension) || entry.open_extensions.contains(extension)) {
      return format_filter_entry(entry, entry.save_extensions);
    }
  }
  return {};
}

bool is_supported_image_extension(const QString& extension) {
  if (is_photoshop_document_extension(extension)) {
    return false;
  }
  for (const auto& entry : file_format_entries()) {
    if (entry.open_extensions.contains(extension)) {
      return true;
    }
  }
  return false;
}

bool is_supported_open_path(const QString& path) {
  const QFileInfo info(path);
  if (!info.isFile()) {
    return false;
  }

  const auto extension = info.suffix().toLower();
  if (is_photoshop_document_extension(extension) || is_supported_image_extension(extension)) {
    return true;
  }
  return !QImageReader::imageFormat(path).isEmpty();
}

struct UnrenderedLayerEffectCounts {
  std::size_t groups_with_layer_effects{0};
};

void count_unrendered_layer_effects(const std::vector<Layer>& layers, UnrenderedLayerEffectCounts& counts) {
  for (const auto& layer : layers) {
    const auto& style = layer.layer_style();
    if (layer.kind() == LayerKind::Group && !style.empty()) {
      ++counts.groups_with_layer_effects;
    }
    count_unrendered_layer_effects(layer.children(), counts);
  }
}

QString unrendered_layer_effect_import_notice(const Document& document) {
  UnrenderedLayerEffectCounts counts;
  count_unrendered_layer_effects(document.layers(), counts);
  if (counts.groups_with_layer_effects == 0U) {
    return {};
  }
  return QObject::tr("Patchy preserved group layer effects for PSD round-trip but does not render them yet "
                     "(groups: %1).")
      .arg(counts.groups_with_layer_effects);
}

struct UnsupportedBlendIfCounts {
  std::size_t layer_payloads{0};
  std::size_t group_boundaries{0};
};

void count_unsupported_blend_if(const std::vector<Layer>& layers, UnsupportedBlendIfCounts& counts) {
  for (const auto& layer : layers) {
    if (!layer.raw_psd_blending_ranges().empty() &&
        layer.blend_if_payload_status() == BlendIfPayloadStatus::Unsupported) {
      ++counts.layer_payloads;
    }
    if (blend_if_payload_has_non_identity_or_unsupported(layer.raw_psd_group_boundary_blending_ranges())) {
      ++counts.group_boundaries;
    }
    count_unsupported_blend_if(layer.children(), counts);
  }
}

QString unsupported_blend_if_import_notice(const Document& document) {
  UnsupportedBlendIfCounts counts;
  count_unsupported_blend_if(document.layers(), counts);
  if (counts.layer_payloads == 0U && counts.group_boundaries == 0U) {
    return {};
  }
  if (counts.group_boundaries == 0U) {
    return QObject::tr("Patchy preserved unsupported Photoshop Blend If payloads but does not render or edit them "
                       "(%1 layer(s)).")
        .arg(counts.layer_payloads);
  }
  if (counts.layer_payloads == 0U) {
    return QObject::tr("Patchy preserved Blend If data on Photoshop group-boundary records but does not render or "
                       "edit it (%1 group(s)).")
        .arg(counts.group_boundaries);
  }
  return QObject::tr("Patchy preserved unsupported Photoshop Blend If data without rendering it (%1 layer "
                     "payload(s), %2 group-boundary record(s)).")
      .arg(counts.layer_payloads)
      .arg(counts.group_boundaries);
}

QStringList supported_local_open_paths(const QMimeData* mime_data) {
  QStringList paths;
  if (mime_data == nullptr || !mime_data->hasUrls()) {
    return paths;
  }

  for (const auto& url : mime_data->urls()) {
    if (!url.isLocalFile()) {
      continue;
    }
    const auto path = QDir::toNativeSeparators(url.toLocalFile());
    if (is_supported_open_path(path) && !paths.contains(path)) {
      paths.push_back(path);
    }
  }
  return paths;
}

struct OpenDocumentResult {
  Document document;
  QString file_name;
  QString extension;
  // User-facing notes about features the reader dropped or approximated (for example
  // "imported the first frame only"); shown in the Import Notes dialog after the open.
  QStringList import_notices;
};

std::vector<std::uint8_t> read_all_file_bytes(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    throw std::runtime_error(QStringLiteral("Unable to open file: %1").arg(path).toStdString());
  }
  const auto data = file.readAll();
  const auto* bytes = reinterpret_cast<const std::uint8_t*>(data.constData());
  return std::vector<std::uint8_t>(bytes, bytes + data.size());
}

OpenDocumentResult load_document_from_path(QString path) {
  const auto info = QFileInfo(path);
  const auto extension = info.suffix().toLower();
  Document opened;
  QStringList import_notices;
  const auto load_via_qt = [&] {
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const auto image = reader.read();
    if (image.isNull()) {
      throw std::runtime_error(reader.errorString().toStdString());
    }
    if (reader.supportsAnimation()) {
      const auto frames = reader.imageCount();
      if (frames > 1) {
        import_notices.push_back(
            QObject::tr("Animated image: imported the first frame only (%1 frames in the file)").arg(frames));
      }
    }
    opened = document_from_qimage(image, info.completeBaseName().toStdString());
    apply_imported_image_density(opened, read_all_file_bytes(path), image);
  };
  if (is_photoshop_document_extension(extension)) {
    std::vector<std::string> psd_notices;
    psd::ReadOptions psd_options{true, false, true};
    psd_options.notices = &psd_notices;
    opened = psd::DocumentIo::read_file(path.toStdString(), psd_options);
    for (const auto& notice : psd_notices) {
      import_notices.push_back(QString::fromStdString(notice));
    }
    if (const auto notice = unrendered_layer_effect_import_notice(opened); !notice.isEmpty()) {
      import_notices.push_back(notice);
    }
    if (const auto notice = unsupported_blend_if_import_notice(opened); !notice.isEmpty()) {
      import_notices.push_back(notice);
    }
    // Linked-file staleness: compare each external source's stored date/size with
    // the file on disk (Photoshop's own check). These are actionable, so they go
    // FIRST in the notice list (the status bar shows only the leading note).
    QStringList link_notices;
    const auto document_dir = info.absolutePath();
    for (const auto& block : std::as_const(opened.metadata().smart_objects.blocks)) {
      for (const auto& source : block.sources) {
        if (source.kind != SmartObjectSourceKind::ExternalFile) {
          continue;
        }
        const auto file_name = QString::fromStdString(source.filename);
        const auto resolved = resolve_smart_object_external_path(source, document_dir);
        if (!resolved.has_value()) {
          link_notices.push_back(QObject::tr("Linked file %1 was not found").arg(file_name));
          continue;
        }
        const QFileInfo linked_info(*resolved);
        const auto modified = linked_info.lastModified();
        const bool size_changed = source.external_file_size != 0U &&
                                  static_cast<std::uint64_t>(linked_info.size()) != source.external_file_size;
        const bool date_changed =
            source.external_mod_year != 0 &&
            (modified.date().year() != source.external_mod_year ||
             modified.date().month() != source.external_mod_month ||
             modified.date().day() != source.external_mod_day ||
             modified.time().hour() != source.external_mod_hour ||
             modified.time().minute() != source.external_mod_minute);
        if (size_changed || date_changed) {
          link_notices.push_back(
              QObject::tr("Linked file %1 has changed on disk; use Update Smart Object Content")
                  .arg(file_name));
        }
      }
    }
    for (int i = 0; i < link_notices.size(); ++i) {
      import_notices.insert(i, link_notices.at(i));
    }
  } else if (const auto* handler = builtin_format_registry().find_by_extension(extension.toStdString());
             handler != nullptr) {
    try {
      auto result = handler->read(read_all_file_bytes(path));
      opened = std::move(result.document);
      for (const auto& notice : result.notices) {
        import_notices.push_back(QString::fromStdString(notice));
      }
      // Containers with no density concept open at Photoshop's untagged 72 PPI
      // (their readers construct Documents with the 300 PPI new-document default).
      // BMP/HEIF/camera-raw record real densities and keep what their reader set.
      static const std::set<std::string> kDensitylessFormats = {
          "patchy.formats.ico", "patchy.formats.tga", "patchy.formats.aseprite",
          "patchy.formats.pcx", "patchy.formats.ilbm"};
      if (kDensitylessFormats.contains(handler->identifier)) {
        opened.print_settings().horizontal_ppi = kUntaggedImportPpi;
        opened.print_settings().vertical_ppi = kUntaggedImportPpi;
      }
    } catch (const std::exception& registry_error) {
      // Nonstandard files that a Qt plugin still understands (e.g. OS/2 BMPs) keep opening
      // through the Qt fallback; when Qt cannot read them either, report the registry
      // reader's error, which names the real problem. HEIC/HEIF relies on this on
      // macOS/Linux by design: the registry read always throws there and Qt's platform
      // plugin (qmacheif / the KDE runtime's kimg_heif) does the decoding.
      QImageReader reader(path);
      reader.setAutoTransform(true);
      auto image = reader.read();
      if (image.isNull()) {
        throw std::runtime_error(registry_error.what());
      }
      const bool heif_family = heif::is_heif_extension(extension.toStdString());
      if (heif_family && image.colorSpace().isValid() && image.colorSpace() != QColorSpace::SRgb) {
        // kimg_heif attaches the file's color space (iPhone = Display P3) without
        // converting; bake to sRGB so pixels match the Windows/macOS decode paths. Scoped
        // to HEIF so existing PNG/JPEG opens keep their bytes.
        image.convertToColorSpace(QColorSpace::SRgb);
      }
      // HEIF opens name their layer "Background" on every platform (the Windows WIC
      // reader's convention); other fallback formats keep the historical file-name label.
      opened = document_from_qimage(image, heif_family ? std::string("Background")
                                                       : info.completeBaseName().toStdString());
      apply_imported_image_density(opened, read_all_file_bytes(path), image);
    }
  } else {
    load_via_qt();
  }
  // Flat images (BMP/PNG/TIFF) carry their alpha as a per-pixel channel. Move a
  // meaningful alpha into an editable layer mask so it is visible and paintable,
  // matching Photoshop's "Alpha 1". PSD/PSB sources are excluded: flat ones are
  // promoted inside the reader (saved alpha channels), while a layered file's single
  // layer OWNS its transparency; promoting it grew a phantom mask on single-text-layer
  // files like the table tent's Content.psb and would push authored transparency into
  // a document alpha channel on resave.
  if (!opened.metadata().values.contains("psd.version")) {
    promote_flat_alpha_to_layer_mask(opened);
  }
  if (const auto default_layer_id = default_non_group_layer_id(opened.layers()); default_layer_id.has_value()) {
    opened.set_active_layer(*default_layer_id);
  } else {
    opened.clear_active_layer();
  }
  return OpenDocumentResult{std::move(opened), info.fileName(), extension, std::move(import_notices)};
}

// Shows the open-failure box. Windows HEIC errors carry a marker naming the missing
// Microsoft Store codec package (heif_document_io.hpp); those get an extra button that
// deep-links to that package's Store page, Photos-app style.
void show_open_failed_message_box(QWidget* parent, const QString& error_text) {
  QString display_text = error_text;
  QString store_product_id;
  const auto try_marker = [&](std::string_view marker, std::string_view product_id) {
    const auto prefix = QString::fromUtf8(marker.data(), static_cast<qsizetype>(marker.size()));
    if (!error_text.startsWith(prefix)) {
      return false;
    }
    display_text = error_text.mid(prefix.size()).trimmed();
    store_product_id = QString::fromUtf8(product_id.data(), static_cast<qsizetype>(product_id.size()));
    return true;
  };
  if (!try_marker(heif::kHeifPackageMissingMarker, heif::kHeifStoreProductId)) {
    try_marker(heif::kHevcPackageMissingMarker, heif::kHevcStoreProductId);
  }
  if (store_product_id.isEmpty()) {
    show_critical_message(parent, QObject::tr("Open failed"), display_text, QStringLiteral("openFailedMessageBox"));
    return;
  }
  QMessageBox dialog(QMessageBox::Critical, QObject::tr("Open failed"), display_text, QMessageBox::Ok, parent);
  dialog.setObjectName(QStringLiteral("openFailedMessageBox"));
  auto* store_button = dialog.addButton(QObject::tr("Open Microsoft Store"), QMessageBox::ActionRole);
  dialog.setDefaultButton(store_button);
  exec_dialog(dialog);
  if (dialog.clickedButton() == store_button) {
    QDesktopServices::openUrl(QUrl(QStringLiteral("ms-windows-store://pdp/?ProductId=%1").arg(store_product_id)));
  }
}

QString path_with_default_extension(QString path, const QString& selected_filter) {
  if (!QFileInfo(path).suffix().isEmpty()) {
    return path;
  }

  for (const auto& entry : file_format_entries()) {
    if (entry.save_extensions.isEmpty()) {
      continue;
    }
    for (const auto& extension : entry.save_extensions) {
      if (selected_filter.contains(QStringLiteral("*.") + extension)) {
        return path + QLatin1Char('.') + entry.save_extensions.front();
      }
    }
  }
  return path + QStringLiteral(".psd");
}

// Interactive-open machinery shared by open_document_path and the document-tab
// Reopen command. Camera raws get the interactive develop step (white balance,
// exposure, ...) and nullopt means the user cancelled it there; with the
// preference off, raws fall through to the normal path, where the format
// registry develops camera defaults. Everything else loads on a worker thread
// behind a modal progress dialog so big documents keep the UI responsive.
// Load failures propagate as exceptions.
std::optional<OpenDocumentResult> load_document_interactive(QWidget* parent, const QString& path) {
  const QFileInfo info(path);
  const auto extension = info.suffix().toLower();
  if (raw::is_camera_raw_extension(extension.toStdString()) &&
      app_settings().value(QStringLiteral("imports/showRawDevelopDialog"), true).toBool()) {
    auto outcome = run_raw_develop_dialog(parent, path);
    if (!outcome.has_value()) {
      return std::nullopt;
    }
    OpenDocumentResult loaded{std::move(outcome->document), info.fileName(), extension, {}};
    if (const auto default_layer_id = default_non_group_layer_id(loaded.document.layers());
        default_layer_id.has_value()) {
      loaded.document.set_active_layer(*default_layer_id);
    }
    return loaded;
  }

  QProgressDialog progress(MainWindow::tr("Opening %1...").arg(info.fileName()), QString(), 0, 0, parent);
  progress.setObjectName(QStringLiteral("openProgressDialog"));
  progress.setWindowTitle(
      MainWindow::tr("Opening %1").arg(elided_open_progress_title_file_name(progress, info.fileName())));
  progress.setWindowModality(Qt::WindowModal);
  progress.setMinimumDuration(0);
  progress.setCancelButton(nullptr);
  progress.setAutoClose(false);
  progress.setAutoReset(false);
  remember_dialog_position(progress);
  progress.show();
  progress.raise();
  progress.activateWindow();
  QApplication::processEvents();
  const auto close_progress = qScopeGuard([&progress] {
    progress.close();
    QApplication::processEvents();
  });
  Q_UNUSED(close_progress);

  auto open_future = std::async(std::launch::async, [path] { return load_document_from_path(path); });
  while (open_future.wait_for(std::chrono::milliseconds(15)) != std::future_status::ready) {
    QApplication::processEvents(QEventLoop::AllEvents, 15);
  }

  return open_future.get();
}

}  // namespace

void MainWindow::open_document() {
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return;
  }
  const auto path = get_open_file_name(this, tr("Open"), last_open_directory(), open_file_filter(), nullptr,
                                       QStringLiteral("openFileDialog"));
  if (path.isEmpty()) {
    return;
  }
  open_document_path(path);
}

bool MainWindow::accept_open_file_drag(QDropEvent* event) {
  if (preview_dialog_edit_locked()) {
    if (event != nullptr) {
      event->ignore();
    }
    show_preview_dialog_edit_lock_message();
    return false;
  }
  if (event == nullptr || supported_local_open_paths(event->mimeData()).isEmpty()) {
    if (event != nullptr) {
      event->ignore();
    }
    return false;
  }

  if ((event->possibleActions() & Qt::CopyAction) != 0) {
    event->setDropAction(Qt::CopyAction);
    event->accept();
  } else {
    event->acceptProposedAction();
  }
  return true;
}

bool MainWindow::open_dropped_files(QDropEvent* event) {
  if (preview_dialog_edit_locked()) {
    if (event != nullptr) {
      event->ignore();
    }
    show_preview_dialog_edit_lock_message();
    return false;
  }
  if (event == nullptr) {
    return false;
  }

  const auto paths = supported_local_open_paths(event->mimeData());
  if (paths.isEmpty()) {
    event->ignore();
    show_status_error(tr("Drop a supported image or Photoshop document"));
    return false;
  }

  if ((event->possibleActions() & Qt::CopyAction) != 0) {
    event->setDropAction(Qt::CopyAction);
    event->accept();
  } else {
    event->acceptProposedAction();
  }

  for (const auto& path : paths) {
    open_document_path(path);
  }
  return true;
}

void MainWindow::open_command_line_files(const QStringList& paths) {
  for (const auto& path : paths) {
    if (path.isEmpty()) {
      continue;
    }
    open_document_path(QFileInfo(path).absoluteFilePath());
  }
}

void MainWindow::activate_for_second_instance(const QStringList& paths) {
  // Restore from a minimized/hidden state and pull the existing window in front so the user sees the
  // file they just double-clicked open in this instance rather than a new process.
  if (isMinimized()) {
    setWindowState(windowState() & ~Qt::WindowMinimized);
  }
  if (!isVisible()) {
    show();
  }
  raise();
  activateWindow();
  open_command_line_files(paths);
}

bool MainWindow::save_debug_screenshot(const QString& file_path, const QString& widget_name,
                                       const QRect& region) {
  QWidget* target = this;
  if (!widget_name.isEmpty()) {
    target = findChild<QWidget*>(widget_name);
    if (target == nullptr) {
      return false;
    }
  }
  const auto grab_rect = region.isValid() ? region : QRect(QPoint(0, 0), QSize(-1, -1));
  return target->grab(grab_rect).save(file_path);
}

void MainWindow::open_document_path(QString path) {
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return;
  }
  try {
    auto loaded = load_document_interactive(this, path);
    if (!loaded.has_value()) {
      return;
    }

    add_document_session(std::move(loaded->document), loaded->file_name, path);
    if (is_photoshop_document_extension(loaded->extension) &&
        app_settings().value(QStringLiteral("imports/showPsdWarningsAndInfo"), false).toBool()) {
      show_compatibility_report(this, document(), loaded->file_name);
    }
    canvas_->fit_to_view();
    session().undo_stack.clear();
    session().redo_stack.clear();
    if (history_list_ != nullptr) {
      history_list_->clear();
    }
    update_history(tr("Open"));
    refresh_layer_list();
    refresh_layer_controls();
    maybe_offer_indexed_palette_adoption();
    update_undo_redo_actions();
    add_recent_file(path);
    remember_open_directory_for_path(path);
    add_recent_folder(QFileInfo(path).absolutePath());
    if (loaded->import_notices.isEmpty()) {
      statusBar()->showMessage(tr("Opened %1").arg(path));
    } else {
      // Import notes ride the status bar by default; the consolidated popup is
      // opt-in via the same preference that gates the PSD compatibility report
      // (Seth: do not annoy people with info popups).
      auto status_notes = loaded->import_notices.front();
      if (loaded->import_notices.size() > 1) {
        status_notes +=
            tr(" (+%n more import note(s))", nullptr, static_cast<int>(loaded->import_notices.size()) - 1);
      }
      statusBar()->showMessage(tr("Opened %1. %2").arg(loaded->file_name, status_notes));
      if (app_settings().value(QStringLiteral("imports/showPsdWarningsAndInfo"), false).toBool()) {
        QStringList bullets;
        bullets.reserve(loaded->import_notices.size());
        for (const auto& notice : loaded->import_notices) {
          bullets.push_back(QStringLiteral("• ") + notice);
        }
        show_information_message(this, tr("Import Notes"),
                                 tr("%1 opened with notes:\n\n%2")
                                     .arg(loaded->file_name, bullets.join(QLatin1Char('\n'))),
                                 QStringLiteral("importNoticesMessageBox"));
      }
    }
  } catch (const std::exception& error) {
    show_open_failed_message_box(this, QString::fromUtf8(error.what()));
  }
}

void MainWindow::reopen_document_session(DocumentSession& target_session) {
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return;
  }
  const auto path = target_session.path;
  if (path.isEmpty()) {
    return;
  }
  // The discard prompt and every post-load refresh act on the ACTIVE session,
  // so the target must be activated first (raising its float window when it
  // lives in one).
  activate_document_session(target_session);
  if (!QFileInfo::exists(path)) {
    show_status_error(tr("File is missing"));
    return;
  }
  if (session_is_modified(target_session)) {
    const auto title = target_session.title.isEmpty() ? tr("Untitled") : target_session.title;
    const auto answer = show_warning_message(
        this, tr("Reopen document?"),
        tr("%1 has unsaved changes. Reopen the file from disk and discard them?").arg(title),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel,
        QStringLiteral("reopenDocumentMessageBox"));
    if (answer != QMessageBox::Yes) {
      return;
    }
  }
  try {
    auto loaded = load_document_interactive(this, path);
    if (!loaded.has_value()) {
      return;
    }

    // Replace the document in place: tab position, float window, and session
    // identity survive (smart-object child tabs reference session ids). An open
    // inline text edit still targets the outgoing document, so settle it first.
    finish_active_text_editor();
    layer_thumbnail_cache_.clear();
    channel_thumbnail_cache_.clear();
    target_session.document = std::move(loaded->document);
    target_session.undo_stack.clear();
    target_session.redo_stack.clear();
    target_session.selection_move_coalescing = false;
    target_session.collapsed_layer_groups.clear();
    collect_initially_collapsed_layer_groups(target_session.document.layers(),
                                             target_session.collapsed_layer_groups);
    ++target_session.revision;
    target_session.saved_revision = target_session.revision;
    canvas_->set_document(&target_session.document);
    canvas_->fit_to_view();
    if (history_list_ != nullptr) {
      history_list_->clear();
    }
    update_history(tr("Reopen"));
    refresh_layer_list();
    refresh_layer_controls();
    refresh_channel_panel();
    refresh_palette_panel();
    schedule_palette_compliance_check();
    maybe_offer_indexed_palette_adoption();
    update_undo_redo_actions();
    update_document_action_state();
    refresh_document_tab_titles();
    if (loaded->import_notices.isEmpty()) {
      statusBar()->showMessage(tr("Reopened %1").arg(path));
    } else {
      auto status_notes = loaded->import_notices.front();
      if (loaded->import_notices.size() > 1) {
        status_notes +=
            tr(" (+%n more import note(s))", nullptr, static_cast<int>(loaded->import_notices.size()) - 1);
      }
      statusBar()->showMessage(tr("Reopened %1. %2").arg(loaded->file_name, status_notes));
    }
  } catch (const std::exception& error) {
    show_open_failed_message_box(this, QString::fromUtf8(error.what()));
  }
}

void MainWindow::import_from_scanner() {
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return;
  }
  // PATCHY_FAKE_SCANNER_FILE bypasses native acquisition so offscreen tests can exercise
  // the import/session plumbing (WIA and AppKit scanner dialogs cannot run in CI).
  const auto fake_path = qEnvironmentVariable("PATCHY_FAKE_SCANNER_FILE");
  if (!fake_path.isEmpty()) {
    finish_scanner_import({ScannerAcquireStatus::Acquired, fake_path, {}}, false);
    return;
  }

#ifdef Q_OS_WIN
  if (scanner_import_active_) {
    return;
  }
  scanner_import_active_ = true;
  const auto reentry_guard = qScopeGuard([this] { scanner_import_active_ = false; });
  finish_scanner_import(acquire_image_from_scanner(this), true);
#elif defined(Q_OS_MACOS)
  if (scanner_import_active_) {
    return;
  }
  scanner_import_active_ = true;
  const QPointer<MainWindow> window(this);
  acquire_image_from_scanner_async(this, [window](ScannerAcquireResult result) {
    if (window == nullptr) {
      if (result.status == ScannerAcquireStatus::Acquired) {
        QFile::remove(result.file_path);
      }
      return;
    }
    window->scanner_import_active_ = false;
    window->finish_scanner_import(std::move(result), true);
  });
#endif
}

void MainWindow::finish_scanner_import(ScannerAcquireResult result, bool delete_after) {
  const auto remove_temporary_scan = qScopeGuard([&result, delete_after] {
    if (delete_after && !result.file_path.isEmpty()) {
      QFile::remove(result.file_path);
    }
  });
  switch (result.status) {
    case ScannerAcquireStatus::Cancelled:
      return;
    case ScannerAcquireStatus::NoDevice:
#ifdef Q_OS_WIN
      show_information_message(
          this, tr("Import from Scanner"),
          tr("No scanner or camera was found. Connect a WIA-compatible device and try again."),
          QStringLiteral("scannerNoDeviceMessageBox"));
#else
      show_information_message(
          this, tr("Import from Scanner"),
          tr("No scanner was found. Connect a scanner recognized by macOS and try again."),
          QStringLiteral("scannerNoDeviceMessageBox"));
#endif
      return;
    case ScannerAcquireStatus::Failed:
      show_critical_message(this, tr("Import from Scanner"), result.error,
                            QStringLiteral("scannerFailedMessageBox"));
      return;
    case ScannerAcquireStatus::Acquired:
      break;
  }

  const auto& acquired_path = result.file_path;
  try {
    // Load by content: some scanner drivers save JPEG bytes regardless of the requested
    // format, and QImageReader's fallback probes plugins by content when the extension
    // path fails.
    auto loaded = load_document_from_path(acquired_path);
    auto& print_settings = loaded.document.print_settings();
    // Some drivers report absurd resolutions; clamp so Image Size stays sane.
    if (print_settings.horizontal_ppi < 10 || print_settings.horizontal_ppi > 4800) {
      print_settings.horizontal_ppi = 300;
    }
    if (print_settings.vertical_ppi < 10 || print_settings.vertical_ppi > 4800) {
      print_settings.vertical_ppi = 300;
    }
    // Untitled + modified: the scan exists nowhere else, so Save must prompt Save As and
    // closing must warn about unsaved changes.
    add_document_session(std::move(loaded.document), tr("Scanned Image"), QString());
    canvas_->fit_to_view();
    session().undo_stack.clear();
    session().redo_stack.clear();
    if (history_list_ != nullptr) {
      history_list_->clear();
    }
    update_history(tr("Import from scanner"));
    refresh_layer_list();
    refresh_layer_controls();
    update_undo_redo_actions();
    mark_session_modified(session());
    statusBar()->showMessage(tr("Imported image from scanner"));
  } catch (const std::exception& error) {
    show_critical_message(this, tr("Import failed"), QString::fromUtf8(error.what()),
                          QStringLiteral("openFailedMessageBox"));
  }
}

void MainWindow::import_sprite_sheet() {
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return;
  }
  const auto path = get_open_file_name(this, tr("Sprite Sheet to Layers"), last_open_directory(), open_file_filter(),
                                       nullptr, QStringLiteral("spriteSheetImportFileDialog"));
  if (path.isEmpty()) {
    return;
  }
  try {
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const auto sheet = reader.read().convertToFormat(QImage::Format_RGBA8888);
    if (sheet.isNull()) {
      throw std::runtime_error(reader.errorString().toStdString());
    }
    const auto options = prompt_sprite_sheet_import_options(this, sheet.size());
    if (!options.has_value()) {
      return;
    }
    auto sliced = slice_sprite_sheet(sheet, *options, tr("Frame %1"));
    if (!sliced.has_value()) {
      show_information_message(this, tr("Sprite Sheet to Layers"),
                               tr("No non-empty cells were found with these settings."),
                               QStringLiteral("spriteSheetEmptyMessageBox"));
      return;
    }
    const auto frame_count = static_cast<int>(sliced->layers().size());
    if (const auto default_layer_id = default_non_group_layer_id(sliced->layers()); default_layer_id.has_value()) {
      sliced->set_active_layer(*default_layer_id);
    }
    add_document_session(std::move(*sliced), tr("Sprite Frames"), QString());
    canvas_->fit_to_view();
    session().undo_stack.clear();
    session().redo_stack.clear();
    if (history_list_ != nullptr) {
      history_list_->clear();
    }
    update_history(tr("Import sprite sheet"));
    refresh_layer_list();
    refresh_layer_controls();
    update_undo_redo_actions();
    mark_session_modified(session());
    statusBar()->showMessage(tr("Imported %1 frames from %2").arg(frame_count).arg(path));
  } catch (const std::exception& error) {
    show_critical_message(this, tr("Import failed"), QString::fromUtf8(error.what()),
                          QStringLiteral("openFailedMessageBox"));
  }
}

void MainWindow::export_sprite_sheet() {
  if (!has_active_document()) {
    show_status_error(tr("No document"));
    return;
  }
  finish_active_text_editor();
  // One frame per visible top-level layer, bottom to top (hidden layers contribute
  // nothing, matching merge semantics); groups render as their flattened subtree.
  int frame_count = 0;
  for (const auto& layer : std::as_const(document()).layers()) {
    if (layer.visible()) {
      ++frame_count;
    }
  }
  if (frame_count == 0) {
    show_information_message(this, tr("Export Sprite Sheet"), tr("There are no visible layers to export."),
                             QStringLiteral("spriteSheetNoLayersMessageBox"));
    return;
  }
  const auto options = prompt_sprite_sheet_export_options(this, frame_count);
  if (!options.has_value()) {
    return;
  }
  const auto sheet = compose_sprite_sheet(document(), *options);
  if (sheet.isNull()) {
    return;
  }

  QString selected_filter;
  const auto base_name = QFileInfo(session().title.isEmpty() ? tr("Untitled") : session().title).completeBaseName();
  auto path = get_save_file_name(this, tr("Export Sprite Sheet"),
                                 file_dialog_initial_path(QString(), base_name + QStringLiteral("-sheet.png")),
                                 export_image_filter(), &selected_filter,
                                 QStringLiteral("spriteSheetExportFileDialog"));
  if (path.isEmpty()) {
    return;
  }
  path = path_with_default_extension(path, selected_filter);
  try {
    const auto extension = extension_for_path(path);
    auto image_options = prompt_image_save_options(this, extension, image_save_defaults_for_document(),
                                                   /*for_export*/ true);
    if (!image_options.has_value()) {
      return;
    }
    // The sheet routes through the normal export machinery (scale option, indexed GIF/PCX
    // quantization, ...) as a flat document. It inherits the source document's print
    // resolution (the composed QImage would otherwise contribute Qt's screen default).
    auto sheet_document = document_from_qimage(sheet, "Sprite Sheet");
    sheet_document.print_settings() = document().print_settings();
    write_flat_image_file(sheet_document, path, extension, *image_options);
    remember_save_directory_for_path(path);
    statusBar()->showMessage(tr("Exported sprite sheet %1").arg(path));
  } catch (const std::exception& error) {
    show_critical_message(this, tr("Export failed"), QString::fromUtf8(error.what()),
                          QStringLiteral("exportFailedMessageBox"));
  }
}

void MainWindow::set_tile_preview_visible(bool visible, QAction* toggle_action) {
  if (!visible) {
    if (tile_preview_window_ != nullptr) {
      tile_preview_window_->close();
    }
    return;
  }
  if (tile_preview_window_ == nullptr) {
    auto* window = new TilePreviewWindow(
        [this]() -> const Document* { return has_active_document() ? &document() : nullptr; }, this);
    window->setAttribute(Qt::WA_DeleteOnClose);
    connect(window, &TilePreviewWindow::preview_closed, this, [toggle_action] {
      if (toggle_action != nullptr) {
        toggle_action->setChecked(false);
      }
    });
    tile_preview_window_ = window;
  }
  tile_preview_window_->show();
  tile_preview_window_->raise();
  tile_preview_window_->activateWindow();
}

bool MainWindow::save_document() {
  if (!has_active_document()) {
    show_status_error(tr("No document"));
    return false;
  }
  finish_active_text_editor();
  if (session().smart_object_link.has_value()) {
    if (session().smart_object_link->external) {
      // A linked-file child: the file on disk is the source of truth, so write it
      // first and only then refresh the parent's previews and link metadata.
      if (session().path.isEmpty()) {
        return save_document_as();
      }
      if (!save_document_to_path(session().path)) {
        return false;
      }
      refresh_external_smart_object_after_save(session());
      return true;
    }
    // An embedded Edit Smart Object Contents tab: Save applies the contents back to
    // the parent document instead of writing a file (Photoshop semantics).
    return commit_smart_object_child_session(session());
  }
  if (session().path.isEmpty()) {
    return save_document_as();
  }
  if (is_read_only_source_extension(extension_for_path(session().path))) {
    // The document was developed from a read-only source (camera raw); writing the raw
    // back is impossible, so Save is really Save As (defaulting to <basename>.psd).
    return save_document_as();
  }
  if (!save_extension_preserves_layers(extension_for_path(session().path)) &&
      flat_save_discards_layers(std::as_const(document()))) {
    // Photoshop behavior: Save on a document whose file format cannot hold its layers
    // (a JPEG that grew layers) turns into Save As, defaulting to PSD, instead of
    // silently flattening back over the original file.
    return save_document_as();
  }
  return save_document_to_path(session().path);
}

bool MainWindow::save_document_as() {
  if (!has_active_document()) {
    show_status_error(tr("No document"));
    return false;
  }
  finish_active_text_editor();
  const auto fallback_name = session().title.isEmpty() ? tr("Untitled.psd") : session().title;
  auto initial_path = file_dialog_initial_path(session().path, fallback_name);
  const bool layered_document = flat_save_discards_layers(std::as_const(document()));
  if ((layered_document && !save_extension_preserves_layers(extension_for_path(initial_path))) ||
      is_read_only_source_extension(extension_for_path(initial_path))) {
    // Photoshop behavior: Save As for a layered document defaults to PSD, not the flat
    // format the document was opened from. Read-only sources (camera raw) also default
    // to PSD: their own extension can never be written.
    const QFileInfo initial_info(initial_path);
    const auto base_name = is_supported_image_extension(extension_for_path(initial_path))
                               ? initial_info.completeBaseName()
                               : initial_info.fileName();
    initial_path = initial_info.dir().filePath(base_name + QStringLiteral(".psd"));
  }
  auto selected_filter = save_file_filter_for_path(initial_path);
  auto path = get_save_file_name(this, tr("Save As"), initial_path, save_file_filter(), &selected_filter,
                                 QStringLiteral("saveAsFileDialog"), recent_files_);
  if (path.isEmpty()) {
    return false;
  }
  path = path_with_default_extension(path, selected_filter);
  const auto extension = extension_for_path(path);
  const bool discards_layers = layered_document && !save_extension_preserves_layers(extension);
  if (discards_layers && !confirm_flatten_layers_for_save()) {
    return false;
  }
  std::optional<ImageSaveOptions> image_options;
  if (!is_photoshop_document_extension(extension) && image_save_options_apply_to_extension(extension)) {
    image_options = prompt_image_save_options(this, extension, image_save_defaults_for_document());
    if (!image_options.has_value()) {
      return false;
    }
  }
  return save_document_to_path(path, image_options, /*flatten_confirmed*/ discards_layers);
}

bool MainWindow::confirm_flatten_layers_for_save() {
  const bool linked_external_child =
      session().smart_object_link.has_value() && session().smart_object_link->external;
  // A linked smart-object child writes the linked file itself (the file on disk is the
  // document), so its flat save is a real save; everything else saves a flattened copy
  // and keeps the layered document open with its unsaved changes (Photoshop's
  // save-a-copy semantics).
  const auto message =
      linked_external_child
          ? tr("This file format cannot store layers. Continue saving and flatten the linked file?")
          : tr("This file format cannot store layers, so Patchy will save a flattened copy. The open "
               "document will keep its layers and unsaved changes. To keep layers in the file, save as a "
               "Photoshop document (.psd) instead.");
  const auto answer =
      show_warning_message(this, tr("Layers Will Be Flattened"), message,
                           QMessageBox::Save | QMessageBox::Cancel, QMessageBox::Cancel,
                           QStringLiteral("flattenLayersMessageBox"));
  return answer == QMessageBox::Save;
}

bool MainWindow::save_document_to_path(QString path, std::optional<ImageSaveOptions> image_options,
                                       bool flatten_confirmed) {
  finish_active_text_editor();
  const auto extension = extension_for_path(path);
  const bool discards_layers = !save_extension_preserves_layers(extension) &&
                               flat_save_discards_layers(std::as_const(document()));
  if (discards_layers && !flatten_confirmed && !confirm_flatten_layers_for_save()) {
    return false;
  }
  if (!is_photoshop_document_extension(extension) &&
      !std::as_const(document()).channels().empty()) {
    const auto answer = show_warning_message(
        this, tr("Saved Channels Will Be Discarded"),
        tr("This file format cannot store saved channels. Continue saving and discard them?"),
        QMessageBox::Save | QMessageBox::Cancel, QMessageBox::Cancel,
        QStringLiteral("discardSavedChannelsMessageBox"));
    if (answer != QMessageBox::Save) {
      return false;
    }
  }
  if ((extension == QStringLiteral("aseprite") || extension == QStringLiteral("ase")) &&
      layers_have_nondefault_fill_opacity(std::as_const(document()).layers())) {
    const auto answer = show_warning_message(
        this, tr("Fill Opacity Will Be Discarded"),
        tr("Aseprite files cannot store Photoshop Fill Opacity. Continue saving without Fill Opacity?"),
        QMessageBox::Save | QMessageBox::Cancel, QMessageBox::Cancel,
        QStringLiteral("discardFillOpacityMessageBox"));
    if (answer != QMessageBox::Save) {
      return false;
    }
  }
  try {
    auto effective_image_options = image_options.value_or(image_save_defaults_for_document());
    if (!image_options.has_value() && image_save_options_apply_to_extension(extension)) {
      const auto& active_session = session();
      if (active_session.image_save_options.has_value() && active_session.image_save_options_path == path &&
          active_session.image_save_options_extension == extension) {
        effective_image_options = *active_session.image_save_options;
      }
    }

    if (is_photoshop_document_extension(extension)) {
      psd::DocumentIo::write_layered_rgb8_file(document(), path.toStdString(),
                                               psd::WriteOptions{extension == QStringLiteral("psb")});
    } else if (extension == QStringLiteral("aseprite") || extension == QStringLiteral("ase")) {
      // Layered save: the Aseprite writer keeps the layer tree instead of flattening.
      aseprite::DocumentIo::write_file(document(), path.toStdString());
    } else {
      write_flat_image_file(document(), path, extension, effective_image_options);
    }
    const bool saved_flattened_copy =
        discards_layers &&
        !(session().smart_object_link.has_value() && session().smart_object_link->external);
    if (saved_flattened_copy) {
      // Photoshop's save-a-copy semantics: only the flat copy lands on disk; the layered
      // document stays open, modified, and pointed at its original file, so a later Save
      // still offers PSD instead of quietly flattening again.
      remember_save_directory_for_path(path);
      if (image_save_options_apply_to_extension(extension)) {
        persist_image_save_defaults(effective_image_options);
      }
      update_history(tr("Save"));
      add_recent_file(path);
      statusBar()->showMessage(tr("Saved flattened copy %1").arg(path));
      return true;
    }
    auto& active_session = session();
    active_session.path = path;
    active_session.title = QFileInfo(path).fileName();
    remember_save_directory_for_path(path);
    if (!is_photoshop_document_extension(extension) && image_save_options_apply_to_extension(extension)) {
      active_session.image_save_options = effective_image_options;
      active_session.image_save_options_path = path;
      active_session.image_save_options_extension = extension;
      persist_image_save_defaults(effective_image_options);
    } else {
      active_session.image_save_options.reset();
      active_session.image_save_options_path.clear();
      active_session.image_save_options_extension.clear();
    }
    set_session_saved(active_session);
    update_history(tr("Save"));
    add_recent_file(path);
    statusBar()->showMessage(tr("Saved %1").arg(path));
    return true;
  } catch (const std::exception& error) {
    show_critical_message(this, tr("Save failed"), QString::fromUtf8(error.what()),
                          QStringLiteral("saveFailedMessageBox"));
  }
  return false;
}

void MainWindow::export_flat_image() {
  if (!has_active_document()) {
    show_status_error(tr("No document"));
    return;
  }
  finish_active_text_editor();
  QString selected_filter;
  const auto base_name = QFileInfo(session().title.isEmpty() ? tr("Untitled") : session().title).completeBaseName();
  auto path =
      get_save_file_name(this, tr("Export Flat Image"),
                         file_dialog_initial_path(QString(), base_name + QStringLiteral(".png")),
                         export_image_filter(), &selected_filter, QStringLiteral("exportFlatImageFileDialog"));
  if (path.isEmpty()) {
    return;
  }
  path = path_with_default_extension(path, selected_filter);

  try {
    const auto extension = extension_for_path(path);
    std::optional<ImageSaveOptions> image_options;
    if (!is_photoshop_document_extension(extension)) {
      // for_export adds the nearest-neighbor Scale combo to every raster format's options
      // (a scale-only dialog for formats with no other options).
      image_options = prompt_image_save_options(this, extension, image_save_defaults_for_document(),
                                                /*for_export*/ true);
      if (!image_options.has_value()) {
        return;
      }
    }
    const auto effective_image_options = image_options.value_or(image_save_defaults_for_document());
    if (is_photoshop_document_extension(extension)) {
      psd::DocumentIo::write_flat_rgb8_file(document(), path.toStdString());
    } else {
      write_flat_image_file(document(), path, extension, effective_image_options);
    }
    if (!is_photoshop_document_extension(extension) && image_save_options_apply_to_extension(extension)) {
      persist_image_save_defaults(effective_image_options);
    }
    remember_save_directory_for_path(path);
    update_history(tr("Export flat image"));
    statusBar()->showMessage(tr("Exported %1").arg(path));
  } catch (const std::exception& error) {
    show_critical_message(this, tr("Export failed"), QString::fromUtf8(error.what()),
                          QStringLiteral("exportFailedMessageBox"));
  }
}

void MainWindow::page_setup() {
  run_page_setup_dialog(this, &print_page_layout_);
}

void MainWindow::print_document() {
  if (!has_active_document()) {
    show_status_error(tr("No document"));
    return;
  }
  std::optional<QRect> selection_bounds;
  if (canvas_ != nullptr) {
    selection_bounds = canvas_->selected_document_rect();
  }
  if (run_print_dialog(this, document(), selection_bounds, &print_page_layout_)) {
    statusBar()->showMessage(tr("Print output created"));
  }
}

void MainWindow::show_update_available(const UpdateInfo& update) {
  // The install advice is artifact-specific: Windows ships an installer exe, macOS a
  // drag-to-Applications DMG, Linux a Flatpak bundle.
#if defined(Q_OS_MACOS)
  const auto update_text = tr("Patchy %1 is available. You are using version %2.\n\n"
                              "Download the DMG, quit Patchy, and drag the new Patchy into Applications.")
                               .arg(update.version, QStringLiteral(PATCHY_VERSION));
#elif defined(Q_OS_LINUX)
  // A flatpak bundle installs from a local path only (URLs work only for repo-backed
  // flatpakrefs), so the one-liner fetches the stable URL first. curl ships by default
  // on Ubuntu/Fedora/Arch/openSUSE.
  const auto bundle_name = QFileInfo(update.download_url.path()).fileName();
  const auto install_command = QStringLiteral("curl -L -o /tmp/%1 %2 && flatpak install -y /tmp/%1")
                                   .arg(bundle_name, update.download_url.toString());
  const auto update_text = tr("Patchy %1 is available. You are using version %2.\n\n"
                              "To update, paste this into a terminal:\n\n%3")
                               .arg(update.version, QStringLiteral(PATCHY_VERSION), install_command);
#else
  const auto update_text = tr("Patchy %1 is available. You are using version %2.\n\n"
                              "Save your work and close Patchy before running the installer.")
                               .arg(update.version, QStringLiteral(PATCHY_VERSION));
#endif
  QMessageBox dialog(QMessageBox::Information, tr("Update Available"), update_text, QMessageBox::NoButton, this);
  dialog.setObjectName(QStringLiteral("updateAvailableMessageBox"));
  dialog.setTextInteractionFlags(Qt::TextSelectableByMouse);
#if defined(Q_OS_LINUX)
  auto* copy_button = dialog.addButton(tr("Copy Command"), QMessageBox::AcceptRole);
  copy_button->setObjectName(QStringLiteral("updateCopyCommandButton"));
  dialog.setDefaultButton(copy_button);
#else
  QAbstractButton* copy_button = nullptr;
#endif
  auto* download_button = dialog.addButton(tr("Download"), QMessageBox::AcceptRole);
  dialog.addButton(tr("Not Now"), QMessageBox::RejectRole);
#if !defined(Q_OS_LINUX)
  dialog.setDefaultButton(download_button);
#endif

  exec_dialog(dialog);
#if defined(Q_OS_LINUX)
  if (dialog.clickedButton() == copy_button) {
    if (auto* clipboard = QApplication::clipboard(); clipboard != nullptr) {
      clipboard->setText(install_command);
    }
    statusBar()->showMessage(tr("Install command copied to the clipboard"));
    return;
  }
#else
  Q_UNUSED(copy_button);
#endif
  if (dialog.clickedButton() == download_button && !QDesktopServices::openUrl(update.download_url)) {
    show_status_error(tr("Could not open the download link"));
  }
}

void MainWindow::load_recent_files() {
  auto settings = app_settings();
  recent_files_ = settings.value(QStringLiteral("recentFiles")).toStringList();
  recent_files_.erase(std::remove_if(recent_files_.begin(), recent_files_.end(), [](const QString& path) {
                        return path.trimmed().isEmpty() || !QFileInfo::exists(path);
                      }),
                      recent_files_.end());
  trim_recent_files(recent_files_);
}

void MainWindow::save_recent_files() const {
  auto settings = app_settings();
  settings.setValue(QStringLiteral("recentFiles"), recent_files_);
}

void MainWindow::add_recent_file(QString path) {
  path = QFileInfo(path).absoluteFilePath();
  if (path.isEmpty()) {
    return;
  }
  recent_files_.removeAll(path);
  recent_files_.prepend(path);
  trim_recent_files(recent_files_);
  save_recent_files();
  rebuild_recent_files_menu();
}

void MainWindow::rebuild_recent_files_menu() {
  // The start panel mirrors the recent list while it is showing (e.g. a recent
  // entry was cleared from the menu with no document open).
  if (start_panel_ != nullptr && start_panel_->isVisible()) {
    start_panel_->set_recent_files(recent_files_);
  }
  if (recent_files_menu_ == nullptr) {
    return;
  }
  recent_files_menu_->clear();
  recent_files_menu_->setEnabled(!recent_files_.isEmpty());

  const auto add_recent_action = [this](QMenu* menu, const QString& path, int index) {
    const auto label = tr("&%1 %2").arg(index).arg(QDir::toNativeSeparators(path));
    auto* action = menu->addAction(label);
    action->setToolTip(path);
    action->setData(path);
    connect(action, &QAction::triggered, this, [this, path] { open_recent_document(path); });
  };

  const auto recent_count = static_cast<int>(recent_files_.size());
  const auto direct_count = std::min(recent_count, kRecentFilesMenuPageSize);
  for (int index = 0; index < direct_count; ++index) {
    add_recent_action(recent_files_menu_, recent_files_[index], index + 1);
  }

  if (recent_count > direct_count) {
    recent_files_menu_->addSeparator();
    for (int page_start = direct_count; page_start < recent_count; page_start += kRecentFilesMenuPageSize) {
      const auto page_end = std::min(page_start + kRecentFilesMenuPageSize, recent_count);
      auto* page_menu = recent_files_menu_->addMenu(tr("Recent Files %1-%2").arg(page_start + 1).arg(page_end));
      page_menu->setObjectName(QStringLiteral("fileOpenRecentRangeMenu%1").arg(page_start + 1));
      configure_recent_files_context_menu(page_menu);
      for (int index = page_start; index < page_end; ++index) {
        add_recent_action(page_menu, recent_files_[index], index + 1);
      }
    }
  }

  if (!recent_files_.isEmpty()) {
    recent_files_menu_->addSeparator();
    auto* clear_action = recent_files_menu_->addAction(tr("Clear Recent Files"));
    clear_action->setObjectName(QStringLiteral("fileClearRecentAction"));
    connect(clear_action, &QAction::triggered, this, [this] {
      recent_files_.clear();
      save_recent_files();
      rebuild_recent_files_menu();
    });
  }
}

void MainWindow::load_recent_folders() {
  auto settings = app_settings();
  recent_folders_ = settings.value(QStringLiteral("recentFolders")).toStringList();
  recent_folders_.erase(std::remove_if(recent_folders_.begin(), recent_folders_.end(),
                                       [](const QString& dir) {
                                         return dir.trimmed().isEmpty() || !QFileInfo(dir).isDir();
                                       }),
                        recent_folders_.end());
  while (recent_folders_.size() > kMaxRecentFolders) {
    recent_folders_.removeLast();
  }
}

void MainWindow::save_recent_folders() const {
  auto settings = app_settings();
  settings.setValue(QStringLiteral("recentFolders"), recent_folders_);
}

void MainWindow::add_recent_folder(QString dir) {
  dir = QFileInfo(dir).absoluteFilePath();
  if (dir.isEmpty()) {
    return;
  }
  recent_folders_.removeAll(dir);
  recent_folders_.prepend(dir);
  while (recent_folders_.size() > kMaxRecentFolders) {
    recent_folders_.removeLast();
  }
  save_recent_folders();
  rebuild_recent_folders_menu();
}

void MainWindow::rebuild_recent_folders_menu() {
  if (recent_folders_menu_ == nullptr) {
    return;
  }
  recent_folders_menu_->clear();
  recent_folders_menu_->setEnabled(!recent_folders_.isEmpty());

  const auto add_recent_folder_action = [this](QMenu* menu, const QString& dir, int index) {
    const auto label = tr("&%1 %2").arg(index).arg(QDir::toNativeSeparators(dir));
    auto* action = menu->addAction(label);
    action->setToolTip(dir);
    action->setData(dir);
    connect(action, &QAction::triggered, this, [this, dir] {
      if (preview_dialog_edit_locked()) {
        show_preview_dialog_edit_lock_message();
        return;
      }
      const auto start_dir = QFileInfo(dir).isDir() ? dir : last_open_directory();
      const auto path = get_open_file_name(this, tr("Open"), start_dir, open_file_filter(), nullptr,
                                           QStringLiteral("openFileDialog"));
      if (!path.isEmpty()) {
        open_document_path(path);
      }
    });
  };

  const auto recent_count = static_cast<int>(recent_folders_.size());
  const auto direct_count = std::min(recent_count, kRecentFilesMenuPageSize);
  for (int index = 0; index < direct_count; ++index) {
    add_recent_folder_action(recent_folders_menu_, recent_folders_[index], index + 1);
  }

  if (recent_count > direct_count) {
    recent_folders_menu_->addSeparator();
    for (int page_start = direct_count; page_start < recent_count; page_start += kRecentFilesMenuPageSize) {
      const auto page_end = std::min(page_start + kRecentFilesMenuPageSize, recent_count);
      auto* page_menu = recent_folders_menu_->addMenu(tr("Recent Folders %1-%2").arg(page_start + 1).arg(page_end));
      page_menu->setObjectName(QStringLiteral("fileOpenRecentFolderRangeMenu%1").arg(page_start + 1));
      configure_recent_files_context_menu(page_menu);
      page_menu->setProperty(kRecentFoldersMenuProperty, true);
      for (int index = page_start; index < page_end; ++index) {
        add_recent_folder_action(page_menu, recent_folders_[index], index + 1);
      }
    }
  }

  if (!recent_folders_.isEmpty()) {
    recent_folders_menu_->addSeparator();
    auto* clear_action = recent_folders_menu_->addAction(tr("Clear Recent Folders"));
    clear_action->setObjectName(QStringLiteral("fileClearRecentFoldersAction"));
    connect(clear_action, &QAction::triggered, this, [this] {
      recent_folders_.clear();
      save_recent_folders();
      rebuild_recent_folders_menu();
    });
  }
}

void MainWindow::configure_recent_files_context_menu(QMenu* menu) {
  if (menu == nullptr) {
    return;
  }
  menu->setProperty(kRecentFilesMenuProperty, true);
  menu->setContextMenuPolicy(Qt::CustomContextMenu);
  menu->installEventFilter(this);
  connect(menu, &QMenu::customContextMenuRequested, this,
          [this, menu](const QPoint& position) { show_recent_file_context_menu(menu, position); });
}

void MainWindow::show_recent_file_context_menu(const QPoint& position) {
  show_recent_file_context_menu(recent_files_menu_, position);
}

void MainWindow::show_recent_file_context_menu(QMenu* menu, const QPoint& position) {
  if (menu == nullptr) {
    return;
  }

  const auto* action = menu->actionAt(position);
  if (action == nullptr || action->isSeparator()) {
    return;
  }

  const auto path = action->data().toString();
  if (path.isEmpty()) {
    return;
  }

  const bool is_folder = menu->property(kRecentFoldersMenuProperty).toBool();
  const auto close_menus = [this, menu] {
    if (menu != nullptr) {
      menu->close();
    }
    if (recent_files_menu_ != nullptr) {
      recent_files_menu_->close();
    }
    if (recent_folders_menu_ != nullptr) {
      recent_folders_menu_->close();
    }
  };

  QMenu context_menu(menu);
  context_menu.setObjectName(QStringLiteral("recentFileContextMenu"));

  auto* copy_path_action = context_menu.addAction(is_folder ? tr("Copy Folder Path") : tr("Copy File Path"));
  copy_path_action->setObjectName(is_folder ? QStringLiteral("recentFolderCopyPathAction")
                                            : QStringLiteral("recentFileCopyPathAction"));
  connect(copy_path_action, &QAction::triggered, this, [this, close_menus, is_folder, path] {
    QApplication::clipboard()->setText(QDir::toNativeSeparators(path));
    statusBar()->showMessage(is_folder ? tr("Folder path copied") : tr("File path copied"));
    close_menus();
  });

  auto* open_in_explorer_action = context_menu.addAction(tr("Open in File Explorer"));
  open_in_explorer_action->setObjectName(is_folder ? QStringLiteral("recentFolderOpenInExplorerAction")
                                                   : QStringLiteral("recentFileOpenInExplorerAction"));
  connect(open_in_explorer_action, &QAction::triggered, this, [this, close_menus, is_folder, path] {
    reveal_path_in_file_explorer(path, !is_folder);
    close_menus();
  });

  context_menu.exec(menu->mapToGlobal(position));
}

void MainWindow::reveal_path_in_file_explorer(const QString& path, bool is_file) {
  const QFileInfo info(path);
  if (is_file) {
    if (!info.exists()) {
      show_status_error(tr("File is missing"));
      return;
    }
#if defined(Q_OS_WIN)
    // Open the containing folder with the file pre-selected.
    QProcess::startDetached(QStringLiteral("explorer.exe"),
                            {QStringLiteral("/select,") + QDir::toNativeSeparators(info.absoluteFilePath())});
#elif defined(Q_OS_MACOS)
    // open -R reveals the file selected in Finder (a plain folder open loses the selection).
    QProcess::startDetached(QStringLiteral("open"), {QStringLiteral("-R"), info.absoluteFilePath()});
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(info.absolutePath()));
#endif
    return;
  }
  if (!info.isDir()) {
    show_status_error(tr("Folder is missing"));
    return;
  }
  QDesktopServices::openUrl(QUrl::fromLocalFile(info.absoluteFilePath()));
}

void MainWindow::open_recent_document(QString path) {
  if (!QFileInfo::exists(path)) {
    recent_files_.removeAll(path);
    save_recent_files();
    rebuild_recent_files_menu();
    show_status_error(tr("Recent file is missing"));
    return;
  }
  open_document_path(path);
}

}  // namespace patchy::ui
