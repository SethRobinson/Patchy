// MainWindow's file open/save/import/export implementation, split out of
// main_window.cpp: the file-format table and dialog filters, document loading
// and open/save/export flows, scanner and sprite-sheet import, tile preview,
// printing, the update notice, and the recent files/folders menus. Pure
// function moves from main_window.cpp; behavior must stay identical.

#include <QClipboard>
#include "core/artboard.hpp"
#include "core/layer.hpp"
#include "core/layer_metadata.hpp"
#include "core/vector_shape.hpp"
#include "color/color_management.hpp"
#include "color/rgb_profiles.hpp"
#include "core/psd_source_colors.hpp"
#include "core/document_file_info.hpp"
#include "core/pixel_buffer.hpp"
#include "formats/format_registry.hpp"
#include "filters/filter_registry.hpp"
#include "ui/blend_mode_ui.hpp"
#include "ui/theme_sounds.hpp"
#include "ui/document_order_list.hpp"
#include "ui/main_window.hpp"
#include "ui/save_metadata_settings.hpp"
#include "core/pixel_tools.hpp"
#include "ui/brush_tip_library.hpp"
#include "ui/gradient_stops_editor.hpp"
#include <QMenuBar>
#include <QStatusBar>
#include "ui/canvas_widget.hpp"
#include "ui/document_save_coordinator.hpp"
#include "ui/document_file_rename.hpp"
#ifndef Q_OS_WASM
#include "ui/document_recovery_coordinator.hpp"
#endif
#include "ui/appimage_environment.hpp"
#include "ui/main_window_shared.hpp"
#include "ui/theme_qss.hpp"
#include "ui/tiff_jpeg_reader.hpp"
#include "ui/recent_file_list.hpp"
#include "ui/network_mounts.hpp"
#include "ui/model_download_dialog.hpp"
#include "ui/object_selection_model_download.hpp"
#include "ui/qt_paths.hpp"

#include "core/document_depth.hpp"
#include "core/pixel_depth.hpp"
#include "core/smart_object.hpp"
#include "core/layer_tree.hpp"
#include "formats/aseprite_document_io.hpp"
#include "formats/layered_blend_support.hpp"
#include "formats/kra_document_io.hpp"
#include "formats/ora_document_io.hpp"
#include "formats/exr_document_io.hpp"
#include "formats/hdr_document_io.hpp"
#include "formats/jp2_document_io.hpp"
#include "formats/jpeg_deep_reader.hpp"
#include "formats/avif_document_io.hpp"
#include "formats/image_open_diagnosis.hpp"
#include "formats/heif_document_io.hpp"
#include "formats/jxl_document_io.hpp"
#include "formats/jxr_document_io.hpp"
#include "formats/metadata/metadata_containers.hpp"
#include "formats/metadata/metadata_edit.hpp"
#include "formats/raw_document_io.hpp"
#include "formats/dds_document_io.hpp"
#include "formats/rttex_document_io.hpp"
#include "formats/svg_document_io.hpp"
#include "formats/xcf_document_io.hpp"
#include "psd/psd_document_io.hpp"
#include "psd/psd_tiff.hpp"
#include "ui/app_settings.hpp"
#include "ui/image_save_option_keys.hpp"
#include "ui/background_workers.hpp"
#include "ui/save_progress.hpp"
#include "ui/brush_dynamics_popup.hpp"
#include "ui/brush_tip_manager_dialog.hpp"
#include "ui/cli_exit.hpp"
#include "ui/compatibility_report.hpp"
#include "ui/image_document_io.hpp"
#include "ui/image_save_options_dialog.hpp"
#include "ui/web_export.hpp"
#include "ui/raw_develop_dialog.hpp"
#include "ui/raw_develop_settings.hpp"
#include "ui/script_engine.hpp"
#include "ui/filter_workflows.hpp"
#include "ui/dialog_utils.hpp"
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCollator>
#include <QColorSpace>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QIODevice>
#include <QImage>
#include <QImageIOHandler>
#include <QImageReader>
#include <QKeyEvent>
#include <QLabel>
#include <QLatin1Char>
#include <QLineEdit>
#include <QList>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLockFile>
#include <QMenu>
#include <QMessageBox>
#include <QMetaObject>
#include <QMimeData>
#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QScrollBar>
#include <QRect>
#include <QScopeGuard>
#include <QSet>
#include <QSettings>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QStringLiteral>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <QWidgetAction>
#include <Qt>
#include <QtGlobal>
#include <vector>
#include <span>
#include "ui/ui_profile.hpp"
#ifdef Q_OS_WASM
#include "ui/dialog_utils_wasm.hpp"
#endif
#include "ui/document_float_window.hpp"
#include "ui/color_panel.hpp"
#include "ui/layer_style_dialog.hpp"
#include "ui/layer_list_widget.hpp"
#include "ui/localization.hpp"
#include "ui/multipage_pdf_export_dialog.hpp"
#include "ui/export_documents_folder_dialog.hpp"
#include "ui/pdf_export.hpp"
#include "ui/pdf_import.hpp"
#include "ui/print_dialog.hpp"
#include "ui/smart_object_render.hpp"
#include "ui/scanner_import.hpp"
#include "core/photo_divide.hpp"
#include "ui/divide_photos_dialog.hpp"
#include "ui/image_sequence_dialog.hpp"
#include "formats/gif_document_io.hpp"
#include "ui/sprite_sheet_dialog.hpp"
#include "ui/animation_preview_window.hpp"
#include "formats/animation_timing.hpp"
#include "formats/apng_writer.hpp"
#include "formats/webp_animation_io.hpp"
#include "ui/apng_reader.hpp"
#include "ui/tile_preview_window.hpp"
#include "ui/user_fonts.hpp"
#include "ui/warp_text_dialog.hpp"
#include "ui/start_panel.hpp"
#include "ui/update_checker.hpp"
#include "ui/update_installer.hpp"


#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <functional>
#include <future>
#include <iterator>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#endif

#include "patchy_version.hpp"

// Icon resources live in the static patchy_ui library; force registration before first use.
int qInitResources_icons();

namespace patchy::ui {

namespace {

constexpr int kOpenProgressTitleReservedWidth = 140;
constexpr int kOpenProgressTitleMinimumFileNameWidth = 180;

constexpr int kMaxRecentFiles = 200;
constexpr int kMaxRecentFolders = 200;
constexpr int kRecentFilesMenuPageSize = 50;
// Rows the Open Recent File/Folder lists show before they scroll (widget-action menus only).
constexpr int kRecentFilesMenuVisibleRows = 12;
// Minimum spacing of the background recent-history existence checks that
// File menu opens and the start panel's refresh timer start.
constexpr qint64 kRecentHistoryCheckIntervalMs = 30000;

// Network entries are never stat'ed: an asleep or unreachable host blocks a
// stat for the SMB timeout, and main() waits for the check worker at quit.
// They stay listed; clicking one that is gone reports it missing and drops it,
// like any other missing entry. `mounts` is the live mount table (macOS and
// Linux mount network shares under ordinary directories, so only the table
// tells them apart); Windows answers from the drive type instead.
bool is_network_recent_path(const QString& path, const std::vector<MountEntry>& mounts) {
  if (path.startsWith(QStringLiteral("\\\\")) || path.startsWith(QStringLiteral("//"))) {
    return true;
  }
#ifdef Q_OS_WIN
  if (path.size() >= 2 && path[1] == QLatin1Char(':') && path[0].isLetter()) {
    const wchar_t root[] = {static_cast<wchar_t>(path[0].unicode()), L':', L'\\', L'\0'};
    return GetDriveTypeW(root) == DRIVE_REMOTE;
  }
#endif
  return path_is_on_network_mount(path, mounts);
}

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

// QSettings merges different keys, but a recent list is one value. Serialize the
// complete read/modify/write so simultaneous GUI, headless and MCP saves cannot
// replace each other's entries. Use a separate lock from QSettings' own .lock.
QStringList update_recent_history(const QString& key, int limit,
                                  const std::function<void(QStringList&)>& update) {
  auto settings = recent_history_settings();
  std::unique_ptr<QLockFile> lock;
  if (settings.format() == QSettings::IniFormat) {
    QDir().mkpath(QFileInfo(settings.fileName()).absolutePath());
    lock = std::make_unique<QLockFile>(settings.fileName() + QStringLiteral(".recent-lock"));
    if (!lock->tryLock(1000)) {
      qWarning("Could not lock recent history for update");
      settings.sync();
      return settings.value(key).toStringList();
    }
  }
  settings.sync();
  auto paths = settings.value(key).toStringList();
  while (paths.size() > limit) paths.removeLast();
  update(paths);
  paths.removeDuplicates();
  while (paths.size() > limit) paths.removeLast();
  settings.setValue(key, paths);
  settings.sync();
  return paths;
}



bool is_affinity_document_extension(const QString& extension) {
  return extension == QStringLiteral("af") || extension == QStringLiteral("afphoto") ||
         extension == QStringLiteral("afdesign") || extension == QStringLiteral("afpub");
}

// Affinity "Image" layers (placed image files) import wrapped as embedded
// smart objects; count them for the import-choice dialog.
std::size_t count_smart_object_layers(const std::vector<Layer>& layers) {
  std::size_t count = 0;
  for (const auto& layer : layers) {
    if (layer.kind() == LayerKind::Group) {
      count += count_smart_object_layers(layer.children());
    } else if (layer_is_smart_object(layer)) {
      ++count;
    }
  }
  return count;
}

void strip_smart_object_layers(Document& document, std::vector<Layer>& layers,
                               std::vector<std::string>& source_uuids) {
  for (auto& layer : layers) {
    if (std::as_const(layer).kind() == LayerKind::Group) {
      strip_smart_object_layers(document, layer.children(), source_uuids);
      continue;
    }
    if (!layer_is_smart_object(std::as_const(layer))) {
      continue;
    }
    source_uuids.push_back(smart_object_source_uuid(std::as_const(layer)));
    strip_layer_smart_object_data(document, layer);
  }
}









// The SVG flatten warning names what bakes: one translated phrase per item, the
// first few joined, the rest counted.
QString svg_baked_content_summary(const std::vector<svg::BakedContent>& baked) {
  constexpr std::size_t kNamedItems = 6;
  QStringList phrases;
  for (const auto& item : baked) {
    if (phrases.size() >= static_cast<qsizetype>(kNamedItems)) {
      break;
    }
    const auto name = QString::fromStdString(item.layer_name);
    switch (item.kind) {
      case svg::BakedContentKind::TextLayer:
        phrases << MainWindow::tr("text layer \"%1\"").arg(name);
        break;
      case svg::BakedContentKind::PixelLayer:
        phrases << MainWindow::tr("pixel layer \"%1\"").arg(name);
        break;
      case svg::BakedContentKind::SmartObjectLayer:
        phrases << MainWindow::tr("smart object \"%1\"").arg(name);
        break;
      case svg::BakedContentKind::AdjustmentLayer:
        phrases << MainWindow::tr("adjustment layer \"%1\" and the layers below it").arg(name);
        break;
      case svg::BakedContentKind::BlendMode:
        phrases << MainWindow::tr("the blend mode of \"%1\" and the layers below it").arg(name);
        break;
      case svg::BakedContentKind::ShapeLayer:
        phrases << MainWindow::tr("shape layer \"%1\" (its styles or fill options)").arg(name);
        break;
      case svg::BakedContentKind::Group:
        phrases << MainWindow::tr("group \"%1\" (its styles or masks)").arg(name);
        break;
      case svg::BakedContentKind::ClippingGroup:
        phrases << MainWindow::tr("clipping mask group \"%1\"").arg(name);
        break;
      case svg::BakedContentKind::RasterMask:
        phrases << MainWindow::tr("the layer mask on \"%1\"").arg(name);
        break;
      case svg::BakedContentKind::MergedBelow:
        phrases << MainWindow::tr("\"%1\" (merged under an adjustment layer or blend mode)").arg(name);
        break;
    }
  }
  auto summary = phrases.join(QStringLiteral(", "));
  if (baked.size() > kNamedItems) {
    summary = MainWindow::tr("%1 and %n more", nullptr, static_cast<int>(baked.size() - kNamedItems)).arg(summary);
  }
  return summary;
}

// The layered formats whose layer records lack some blend modes: PSD and PSB, a TIFF saved
// with layers (the same records), and Aseprite. OpenRaster settles its own losses.
std::optional<LayeredSaveFormat> layered_save_format_for(const QString& extension, bool tiff_layers) {
  if (is_photoshop_document_extension(extension) || (tiff_layers && is_tiff_extension(extension))) {
    return LayeredSaveFormat::Photoshop;
  }
  if (extension == QStringLiteral("aseprite") || extension == QStringLiteral("ase")) {
    return LayeredSaveFormat::Aseprite;
  }
  return std::nullopt;
}

// Asks how to save layers in blend modes the format lacks: true keeps the look (bake),
// false switches them to Normal, nullopt cancels the save (docs/blend-modes.md,
// "Layered saves").
std::optional<bool> ask_unsupported_blend_modes(QWidget* parent, const QString& extension,
                                                const UnsupportedBlendModes& unsupported) {
  constexpr qsizetype kNamedLayers = 8;
  QStringList modes;
  for (const auto mode : unsupported.modes) {
    modes << blend_mode_name(mode);
  }
  QStringList layers;
  for (const auto& name : unsupported.layer_names) {
    layers << QStringLiteral("\"%1\"").arg(QString::fromStdString(name));
  }
  const auto count = static_cast<int>(layers.size());
  QString named = layers.mid(0, kNamedLayers).join(QStringLiteral(", "));
  if (layers.size() > kNamedLayers) {
    named = MainWindow::tr("%1 and %n more", nullptr, static_cast<int>(layers.size() - kNamedLayers)).arg(named);
  }
  QMessageBox box(parent);
  box.setObjectName(QStringLiteral("unsupportedBlendModesMessageBox"));
  box.setIcon(QMessageBox::Warning);
  box.setWindowTitle(MainWindow::tr("Blend Modes Not Supported"));
  box.setText(MainWindow::tr("%1 files can't store these blend modes: %2 (%n layer(s)).", nullptr, count)
                  .arg(extension.toUpper(), modes.join(QStringLiteral(", "))));
  box.setInformativeText(
      MainWindow::tr("Layers: %1.\n\n"
                     "Keep the look: each of these layers is saved as a Normal layer that shows what its blend "
                     "mode showed, so the saved image looks the same. Its pixels then include what was below it, "
                     "and a folder or clipping group that holds one is saved as one layer.\n\n"
                     "Save as Normal: the layers keep their own pixels but switch to Normal, so the saved image "
                     "looks different.\n\n"
                     "The open document keeps its blend modes either way.")
          .arg(named));
  if (layers.size() > kNamedLayers) {
    box.setDetailedText(layers.join(QLatin1Char('\n')));
  }
  auto* keep_button = box.addButton(MainWindow::tr("Keep the Look"), QMessageBox::AcceptRole);
  keep_button->setObjectName(QStringLiteral("unsupportedBlendModesKeepButton"));
  auto* normal_button = box.addButton(MainWindow::tr("Save as Normal"), QMessageBox::DestructiveRole);
  normal_button->setObjectName(QStringLiteral("unsupportedBlendModesNormalButton"));
  box.addButton(QMessageBox::Cancel);
  box.setDefaultButton(keep_button);
  box.setEscapeButton(QMessageBox::Cancel);
  exec_dialog(box);
  const auto* clicked = box.clickedButton();
  if (clicked == keep_button) {
    return true;
  }
  if (clicked == normal_button) {
    return false;
  }
  return std::nullopt;
}

// The OpenRaster save warning names what bakes or drops, like the SVG one: one translated
// phrase per item, the first few joined, the rest counted.
QString ora_lost_content_summary(const std::vector<ora::Loss>& losses) {
  constexpr std::size_t kNamedItems = 6;
  QStringList phrases;
  for (const auto& loss : losses) {
    if (phrases.size() >= static_cast<qsizetype>(kNamedItems)) {
      break;
    }
    const auto name = QString::fromStdString(loss.layer_name);
    switch (loss.kind) {
      case ora::LossKind::TextLayer:
        phrases << MainWindow::tr("text layer \"%1\"").arg(name);
        break;
      case ora::LossKind::ShapeLayer:
        phrases << MainWindow::tr("shape layer \"%1\"").arg(name);
        break;
      case ora::LossKind::SmartObjectLayer:
        phrases << MainWindow::tr("smart object \"%1\"").arg(name);
        break;
      case ora::LossKind::AdjustmentLayer:
        phrases << MainWindow::tr("adjustment layer \"%1\" and the layers below it").arg(name);
        break;
      case ora::LossKind::BlendModeMerged:
        phrases << MainWindow::tr("the blend mode of \"%1\" and the layers below it").arg(name);
        break;
      case ora::LossKind::BlendModeKrita:
        phrases << MainWindow::tr("the blend mode of \"%1\" (only Krita reads it)").arg(name);
        break;
      case ora::LossKind::LayerStyle:
        phrases << MainWindow::tr("the layer style of \"%1\"").arg(name);
        break;
      case ora::LossKind::LayerMask:
        phrases << MainWindow::tr("the layer mask on \"%1\"").arg(name);
        break;
      case ora::LossKind::ClippingGroup:
        phrases << MainWindow::tr("clipping mask group \"%1\"").arg(name);
        break;
      case ora::LossKind::BlendingOptions:
        phrases << MainWindow::tr("the blending options of \"%1\"").arg(name);
        break;
      case ora::LossKind::Group:
        phrases << MainWindow::tr("group \"%1\" (its styles or masks)").arg(name);
        break;
      case ora::LossKind::Depth32:
        phrases << MainWindow::tr("32-bit channels (saved at 16 bits)");
        break;
      case ora::LossKind::ColorMode:
        phrases << MainWindow::tr("the color mode (saved as RGB)");
        break;
      case ora::LossKind::PaletteMode:
        phrases << MainWindow::tr("the palette (saved as RGB colors)");
        break;
      case ora::LossKind::Guides:
        phrases << MainWindow::tr("guides");
        break;
      case ora::LossKind::Paths:
        phrases << MainWindow::tr("paths");
        break;
      case ora::LossKind::Artboards:
        phrases << MainWindow::tr("artboards (saved as groups)");
        break;
    }
  }
  auto summary = phrases.join(QStringLiteral(", "));
  if (losses.size() > kNamedItems) {
    summary = MainWindow::tr("%1 and %n more", nullptr, static_cast<int>(losses.size() - kNamedItems)).arg(summary);
  }
  return summary;
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

// Whether this build can decode HEIF at all: Windows always registers a WIC codec
// (a stub when the Store packages are missing, which the open path explains), the
// browser build decodes through WebCodecs, native Linux builds and the AppImage load the
// distribution's libheif at run time, and macOS and the Flatpak depend on a Qt image
// plug-in (qmacheif, kimg_heif in the Flatpak runtime).
bool heif_decoder_available() {
#if defined(Q_OS_WIN) || defined(Q_OS_WASM)
  return true;
#else
#if defined(Q_OS_LINUX)
  if (heif::host_libheif_available()) {
    return true;
  }
#endif
  const auto formats = QImageReader::supportedImageFormats();
  return formats.contains(QByteArrayLiteral("heic")) || formats.contains(QByteArrayLiteral("heif"));
#endif
}

const QList<FileFormatEntry>& file_format_entries() {
  static const QList<FileFormatEntry> entries = [] {
    QList<FileFormatEntry> list = {
      {QT_TRANSLATE_NOOP("QObject", "Photoshop Document"),
       {QStringLiteral("psd"), QStringLiteral("psb")},
       {QStringLiteral("psd"), QStringLiteral("psb")},
       true,
       false},
      {QT_TRANSLATE_NOOP("QObject", "PNG Image"),
       {QStringLiteral("png"), QStringLiteral("apng")},
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
      // OpenRaster keeps the layer tree (the layered writer in DocumentSaveCoordinator, not
      // write_flat_image_file), so it is a Save As format only (docs/ora.md).
      {QT_TRANSLATE_NOOP("QObject", "OpenRaster Image"),
       {QStringLiteral("ora")},
       {QStringLiteral("ora")},
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
      // Netpbm's portable anymaps (PBM bitmap, PGM graymap, PPM pixmap, and .pnm for any
      // of them) read through Qt's built-in PPM handler, which also sniffs the P1-P6
      // magic when the suffix is .pnm. Read-only: nothing writes them (GitHub issue 270).
      {QT_TRANSLATE_NOOP("QObject", "Portable Anymap Image"),
       {QStringLiteral("pbm"), QStringLiteral("pgm"), QStringLiteral("ppm"), QStringLiteral("pnm")},
       {},
       false,
       false},
      // SVG opens as editable shape layers and saves/exports with vectors
      // preserved (the layered svg writer, not write_flat_image_file). It
      // stays OUT of save_extension_preserves_layers on purpose: masks,
      // styles, and text bake on save, so layered saves keep Photoshop's
      // warn + save-a-copy semantics. .svgz is read-only.
      {QT_TRANSLATE_NOOP("QObject", "SVG Image"),
       {QStringLiteral("svg"), QStringLiteral("svgz")},
       {QStringLiteral("svg")},
       true,
       true},
    };
    // PDF writes everywhere (QPdfWriter is QtGui) but only reads where the optional Qt PDF
    // add-on is present, so the open half of the row is conditional. The writer lives on the
    // Qt side in write_flat_image_file, which is why the format registry carries no PDF row
    // at all; see is_read_only_source_extension for what that means for Save.
    list.push_back({QT_TRANSLATE_NOOP("QObject", "PDF Document"),
                    pdf_import_is_available() ? QStringList{QStringLiteral("pdf")} : QStringList{},
                    {QStringLiteral("pdf")},
                    true,
                    true});
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
    // Affinity documents open read-only, so the entry has no save extensions like
    // camera raw and HEIF. The Affinity 2.x generations (.afphoto/.afdesign/.afpub)
    // share the .af container and grammar, so the one row claims them all.
    list.push_back({QT_TRANSLATE_NOOP("QObject", "Affinity Document"),
                    {QStringLiteral("af"), QStringLiteral("afphoto"), QStringLiteral("afdesign"),
                     QStringLiteral("afpub")},
                    {},
                    false,
                    false});
    // Paint.NET documents open read-only too (docs/pdn-format.md).
    list.push_back({QT_TRANSLATE_NOOP("QObject", "Paint.NET Image"), {QStringLiteral("pdn")}, {}, false, false});
    // GIMP documents open read-only (docs/xcf.md), plain or in GIMP's compressed forms.
    list.push_back({QT_TRANSLATE_NOOP("QObject", "GIMP Image"),
                    {QStringLiteral("xcf"), QStringLiteral("xcf.gz"), QStringLiteral("xcf.bz2"),
                     QStringLiteral("xcf.xz"), QStringLiteral("xcfgz"), QStringLiteral("xcfbz2"),
                     QStringLiteral("xcfxz")},
                    {},
                    false,
                    false});
    // Krita documents open read-only (docs/kra.md); .krz is the same container.
    list.push_back({QT_TRANSLATE_NOOP("QObject", "Krita Document"), {QStringLiteral("kra"), QStringLiteral("krz")},
                    {}, false, false});
    // HEIF/HEIC is decode-only like camera raw (platform codecs never encode for us and
    // Patchy must not ship an HEVC encoder), so its entry is read-only too. Where no
    // decoder exists at all (an AppImage on a machine without libheif) the row is left out
    // rather than advertising an open that can only fail (GitHub issue 113); a dropped
    // or typed .heic path still reaches the sniffing open path and its explanation.
    QStringList heif_extensions;
    if (heif_decoder_available()) {
      for (const auto& extension : heif::heif_extensions()) {
        heif_extensions.push_back(QString::fromStdString(extension));
      }
    }
    list.push_back({QT_TRANSLATE_NOOP("QObject", "HEIF Image"),
                    heif_extensions,
                    {},
                    false,
                    false});
    // AVIF reads and writes everywhere through the vendored libavif (dav1d decodes, libaom
    // encodes; docs/avif.md); saves use the primary extension. The browser build has neither
    // codec and hides the row.
    QStringList avif_extensions;
    if (avif::is_available()) {
      for (const auto& extension : avif::avif_extensions()) {
        avif_extensions.push_back(QString::fromStdString(extension));
      }
    }
    list.push_back({QT_TRANSLATE_NOOP("QObject", "AVIF Image"), avif_extensions,
                    avif::is_available() ? QStringList{QStringLiteral("avif")} : QStringList{}, avif::is_available(),
                    avif::is_available()});
    // JPEG XR reads and writes, but only where the in-box Windows codec exists: Qt has no
    // JPEG XR plugin, so on macOS/Linux/wasm the row would advertise an open and a save that
    // can only fail. Gate the whole row like the PDF row's conditional open half.
    QStringList jxr_extensions;
    for (const auto& extension : jxr::jxr_extensions()) {
      jxr_extensions.push_back(QString::fromStdString(extension));
    }
    list.push_back({QT_TRANSLATE_NOOP("QObject", "JPEG XR Image"),
                    jxr::is_available() ? jxr_extensions : QStringList{},
                    jxr::is_available() ? jxr_extensions : QStringList{},
                    jxr::is_available(),
                    jxr::is_available()});
    // JPEG XL reads and writes everywhere through the vendored libjxl (docs/jxl.md).
    QStringList jxl_extensions;
    for (const auto& extension : jxl::jxl_extensions()) {
      jxl_extensions.push_back(QString::fromStdString(extension));
    }
    list.push_back({QT_TRANSLATE_NOOP("QObject", "JPEG XL Image"), jxl_extensions, jxl_extensions, true, true});
    // Proton SDK textures read and write on every platform: the codec is Patchy's own (raw
    // pixels or an embedded JPEG inside a zlib container), so nothing gates the row.
    QStringList rttex_extensions;
    for (const auto& extension : rttex::rttex_extensions()) {
      rttex_extensions.push_back(QString::fromStdString(extension));
    }
    list.push_back({QT_TRANSLATE_NOOP("QObject", "Proton Texture"), rttex_extensions, rttex_extensions, true, true});
    // DDS textures read and write everywhere too: the block decoder and encoder are vendored
    // into the formats library, so no platform codec is involved.
    QStringList dds_extensions;
    for (const auto& extension : dds::dds_extensions()) {
      dds_extensions.push_back(QString::fromStdString(extension));
    }
    list.push_back({QT_TRANSLATE_NOOP("QObject", "DirectDraw Surface"), dds_extensions, dds_extensions, true, true});
    // Radiance HDR, OpenEXR and JPEG 2000 read and write everywhere through Patchy's own
    // code and vendored codecs (docs/hdr-formats.md); saves use the primary extension.
    const auto string_list = [](const std::vector<std::string>& extensions) {
      QStringList list;
      for (const auto& extension : extensions) {
        list.push_back(QString::fromStdString(extension));
      }
      return list;
    };
    list.push_back({QT_TRANSLATE_NOOP("QObject", "Radiance HDR Image"), string_list(hdr::hdr_extensions()),
                    {QStringLiteral("hdr")}, true, true});
    list.push_back({QT_TRANSLATE_NOOP("QObject", "OpenEXR Image"), string_list(exr::exr_extensions()),
                    {QStringLiteral("exr")}, true, true});
    list.push_back({QT_TRANSLATE_NOOP("QObject", "JPEG 2000 Image"), string_list(jp2::jp2_extensions()),
                    {QStringLiteral("jp2")}, true, true});
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
  return QStringLiteral("%1 (%2)").arg(translate_data_text(entry.display_name),
                                       extension_patterns(extensions));
}

// At most this many patterns appear in an Open-dialog row's visible name; a longer list
// ends in "and more" like the all-formats row. The Camera Raw row names 27 extensions,
// which made GNOME's type dropdown as wide as the screen with its scrollbar at the far
// screen edge, so the rows below the tenth looked absent (GitHub issue 270). The
// machine-read pattern spec after the visible name still carries every extension.
constexpr qsizetype kMaxVisibleFilterPatterns = 6;

QString visible_pattern_hint(const QStringList& extensions) {
  if (extensions.size() <= kMaxVisibleFilterPatterns) {
    return extension_patterns(extensions);
  }
  //: Open-dialog file type row: %1 is a short list of *.ext patterns
  return QObject::tr("%1 and more").arg(extension_patterns(extensions.mid(0, kMaxVisibleFilterPatterns)));
}

QString open_file_filter() {
  QStringList all_extensions;
  for (const auto& entry : file_format_entries()) {
    for (const auto& extension : entry.open_extensions) {
      all_extensions.push_back(extension);
    }
  }
  // One dropdown row per format so every supported filetype is readable in the dialog.
  // The open dialogs run with HideNameFilterDetails: Qt displays only the text left of
  // the LAST "(" and filters with the parenthesized list after it, so each row is
  // written as "Name (hint) (patterns)". The duplication is deliberate: the first
  // copy is the visible name, the second is the machine-read pattern spec. A "*."
  // token must stay in the visible name because the Windows 11 dialog re-appends the
  // whole semicolon-joined spec to any filter name without one (verified empirically,
  // July 2026); that is also why the all-formats row shows a short hint instead of its
  // ~50 patterns, which would run off screen. Per-format rows cap their hint the same
  // way (visible_pattern_hint) so no row is wider than the dialog's dropdown.
  const auto supported_name =
      QObject::tr("Supported Files (%1 and more)").arg(QStringLiteral("*.psd *.png *.jpg"));
  // The format rows run A to Z by their translated names (GitHub issue 270), between the
  // all-formats row and All Files. Only this list is sorted: Save As and Export keep the
  // registry order, Photoshop Document first. lastOpenFilter stores the row text, so the
  // remembered row is found wherever it lands.
  QStringList format_rows;
  for (const auto& entry : file_format_entries()) {
    if (entry.open_extensions.isEmpty()) {
      continue;
    }
    format_rows.push_back(QStringLiteral("%1 (%2) (%3)").arg(translate_data_text(entry.display_name),
                                                            visible_pattern_hint(entry.open_extensions),
                                                            extension_patterns(entry.open_extensions)));
  }
  QCollator collator;
  collator.setCaseSensitivity(Qt::CaseInsensitive);
  std::stable_sort(format_rows.begin(), format_rows.end(), [&collator](const QString& left, const QString& right) {
    return collator.compare(left, right) < 0;
  });
  QStringList filters;
  filters.push_back(QStringLiteral("%1 (%2)").arg(supported_name, extension_patterns(all_extensions)));
  filters.append(format_rows);
  filters.push_back(QStringLiteral("%1 (*.*)").arg(QObject::tr("All Files (*.*)")));
  return filters.join(QStringLiteral(";;"));
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

// The Open dialog reopens on the format row picked last time instead of the
// all-formats row (GitHub issue 114). A cancelled dialog keeps the old pick; a
// row that no longer exists (language switch) falls back to the default.
QString remembered_open_filter() {
  return app_settings().value(QStringLiteral("lastOpenFilter")).toString();
}

void remember_open_filter(const QString& filter) {
  if (filter.isEmpty()) {
    return;
  }
  auto settings = app_settings();
  settings.setValue(QStringLiteral("lastOpenFilter"), filter);
}

void remember_open_directory(const QString& directory) {
  const QFileInfo info(directory);
  if (directory.isEmpty() || !info.isDir()) {
    return;
  }
  auto settings = app_settings();
  settings.setValue(QStringLiteral("lastOpenDirectory"), info.absoluteFilePath());
}

void remember_open_directory_for_path(const QString& path) {
  remember_open_directory(QFileInfo(path).absolutePath());
}

QString extension_for_path(const QString& path) {
  // GIMP's "name.xcf.gz" (docs/xcf.md): the format is the XCF inside.
  if (xcf::is_compressed_xcf_file_name(QFileInfo(path).fileName().toStdString())) {
    return QStringLiteral("xcf");
  }
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

// The first bytes of a file, enough for sniff_openable_image_format; empty when it cannot
// be read.
std::vector<std::uint8_t> read_file_head_bytes(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  const auto data = file.read(1024);
  const auto* bytes = reinterpret_cast<const std::uint8_t*>(data.constData());
  return std::vector<std::uint8_t>(bytes, bytes + data.size());
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
  if (is_photoshop_document_extension(extension) || is_supported_image_extension(extension) ||
      xcf::is_compressed_xcf_file_name(info.fileName().toStdString())) {
    return true;
  }
  // A .pdf is recognized even where the import module is missing (wasm, a desktop Qt
  // without the add-on): the open path then explains that this build cannot read PDFs,
  // and on wasm where to get one that can, instead of a generic unsupported-drop status.
  if (is_pdf_extension(extension)) {
    return true;
  }
  // A file whose name says nothing Patchy knows ("src.bin", no extension) still opens
  // when its bytes are a format Patchy reads (GitHub issue 362).
  if (formats::sniff_openable_image_format(read_file_head_bytes(path)).has_value()) {
    return true;
  }
  return !QImageReader::imageFormat(path).isEmpty();
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

// Dropped local directories, in drop order. Their contents are read only when
// the drop lands (open_folder_path), never per drag-move over the window.
QStringList dropped_directory_paths(const QMimeData* mime_data) {
  QStringList directories;
  if (mime_data == nullptr || !mime_data->hasUrls()) {
    return directories;
  }
  for (const auto& url : mime_data->urls()) {
    if (!url.isLocalFile()) {
      continue;
    }
    const auto path = QDir::toNativeSeparators(url.toLocalFile());
    if (QFileInfo(path).isDir() && !directories.contains(path)) {
      directories.push_back(path);
    }
  }
  return directories;
}

// The images directly inside a folder that Open Folder (and a dropped or
// command-line directory) opens: every file is_supported_open_path accepts except
// PDFs (each would raise the Import PDF page picker, and a folder scan is for
// images), in natural name order (page-2 before page-10). Never recursive.
QStringList supported_image_paths_in_folder(const QString& directory) {
  QStringList paths;
  const QFileInfo info(directory);
  if (!info.isDir()) {
    return paths;
  }
  const auto entries = QDir(info.absoluteFilePath()).entryInfoList(QDir::Files | QDir::Readable, QDir::NoSort);
  for (const auto& entry : entries) {
    if (is_pdf_extension(entry.suffix().toLower())) {
      continue;
    }
    const auto path = QDir::toNativeSeparators(entry.absoluteFilePath());
    if (is_supported_open_path(path)) {
      paths.push_back(path);
    }
  }
  return sorted_sequence_paths(std::move(paths));
}

// Dropped fonts (or a zip of fonts) register into the session instead of
// opening as documents; same isLocalFile filtering as the open paths.
QStringList font_or_zip_local_paths(const QMimeData* mime_data) {
  QStringList paths;
  if (mime_data == nullptr || !mime_data->hasUrls()) {
    return paths;
  }
  for (const auto& url : mime_data->urls()) {
    if (!url.isLocalFile()) {
      continue;
    }
    const auto path = QDir::toNativeSeparators(url.toLocalFile());
    if (!QFileInfo(path).isFile() || paths.contains(path)) {
      continue;
    }
    if (user_fonts::is_user_font_path(path) || user_fonts::is_zip_path(path)) {
      paths.push_back(path);
    }
  }
  return paths;
}

QString translated_file_message(const std::string& message) {
  const auto text = QString::fromStdString(message);
  const auto layer_note = [&](const char* suffix, const QString& translated) -> QString {
    const auto ending = QString::fromLatin1(suffix);
    if (text.startsWith(QStringLiteral("Layer '")) && text.endsWith(ending)) {
      return translated.arg(text.mid(7, text.size() - 7 - ending.size()));
    }
    return {};
  };
  if (auto note = layer_note("': invalid Gaussian blur radius; effect skipped",
                            QObject::tr("Layer '%1': invalid Gaussian blur radius; effect skipped"));
      !note.isEmpty()) {
    return note;
  }
  if (auto note = layer_note("': Gaussian blur could not be applied; original pixels kept",
                            QObject::tr("Layer '%1': Gaussian blur could not be applied; original pixels kept"));
      !note.isEmpty()) {
    return note;
  }
  // Core readers are Qt-free; localize their fixed diagnostic text at the UI boundary.
  return translate_data_text(message);
}

// The color mode a layered PSD or PSB save of this document in CMYK Proof writes
// (docs/cmyk.md): CMYK when its layers allow it, else RGB with the reason, or an unchanged
// imported file's own mode (docs/psd-native-color.md).
psd::LayeredSaveColorMode cmyk_proof_psd_save_color_mode(const Document& document) {
  psd::WriteOptions options;
  options.cmyk_proof = working_cmyk_space();
  return psd::DocumentIo::layered_save_color_mode(document, options);
}

// A CMYK JPEG or TIFF (Qt hands it over as Format_CMYK8888, 255 = full ink) converted to
// sRGB through its embedded CMYK profile, or the working profile when it has none,
// relative colorimetric with black point compensation as Photoshop converts back
// (docs/cmyk.md). Null when neither profile is usable; Qt's plain conversion applies then.
QImage srgb_image_from_cmyk(const QImage& cmyk, QString& profile_name) {
  std::optional<CmykToRgbTransform> transform;
  if (const auto embedded = cmyk.colorSpace().iccProfile(); !embedded.isEmpty()) {
    transform = CmykToRgbTransform::from_icc_profile(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(embedded.constData()), static_cast<std::size_t>(embedded.size())));
  }
  if (!transform.has_value()) {
    if (const auto space = working_cmyk_space(); space->has_profile()) {
      transform = CmykToRgbTransform::from_icc_profile(space->profile_bytes());
    }
  }
  if (!transform.has_value()) {
    return {};
  }
  profile_name = QString::fromStdString(transform->profile_description());
  const auto width = static_cast<std::size_t>(cmyk.width());
  QImage rgb(cmyk.size(), QImage::Format_RGB888);
  std::vector<std::uint8_t> inverted(width * 4U);
  for (int y = 0; y < cmyk.height(); ++y) {
    const auto* inks = cmyk.constScanLine(y);
    for (std::size_t index = 0; index < inverted.size(); ++index) {
      inverted[index] = static_cast<std::uint8_t>(255U - inks[index]);
    }
    transform->convert(inverted.data(), rgb.scanLine(y), width);
  }
  rgb.setDotsPerMeterX(cmyk.dotsPerMeterX());
  rgb.setDotsPerMeterY(cmyk.dotsPerMeterY());
  return rgb;
}

struct OpenDocumentResult {
  Document document;
  QString file_name;
  QString extension;
  // User-facing notes about features the reader dropped or approximated (for example
  // "imported the first frame only"); shown in the Import Notes dialog after the open.
  QStringList import_notices;
  // The notes describe permanent data loss (a 16/32-bit source converted to 8-bit, which
  // every save then writes), so the Import Notes popup shows even when the popup
  // preference is off. Only the status bar would otherwise carry them (GitHub issue 52).
  bool force_import_notices_popup{false};
  // A multi-page PDF opened as separate documents: the first page's label ("Page 1")
  // and every further page as its own document. Empty for every other open.
  QString document_title;
  std::vector<PdfImportedDocument> extra_documents;
  // The file held CMYK pixels (a CMYK JPEG, TIFF, PSD or PSB): the session opens in
  // Image > Mode > CMYK Proof so a save writes CMYK again (docs/cmyk.md).
  bool cmyk_source{false};
  // The file opened by its content because its name says another format or none (GitHub
  // issue 362): the extension of what it really is ("psd"); empty when the name fit.
  QString content_extension{};
  // A PDF import's Mode and Bit Depth, applied once the pages are finished documents.
  // Unset for every other open: nothing else is converted on the way in.
  PdfImportColorMode pdf_color_mode{PdfImportColorMode::Rgb};
  std::optional<BitDepth> pdf_bit_depth{};
  // A PDF import whose pages merge into one document (folders or artboards).
  PdfPagesMerge pdf_merge{PdfPagesMerge::None};
  bool pdf_white_background{true};
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

// A 12-bit or lossless JPEG (jpeg_deep_reader.hpp) as a gray or RGB QImage at 8 or 16 bits,
// which the ordinary import path then opens as a document of that depth (#438).
QImage qimage_from_deep_jpeg(const jpeg_deep::Image& decoded) {
  const bool deep = decoded.output_bits == 16;
  const bool gray = decoded.channels == 1;
  QImage image(decoded.width, decoded.height,
               gray ? (deep ? QImage::Format_Grayscale16 : QImage::Format_Grayscale8)
                    : (deep ? QImage::Format_RGBX64 : QImage::Format_RGB888));
  if (image.isNull()) {
    return image;
  }
  const auto row_samples = static_cast<std::size_t>(decoded.width) * static_cast<std::size_t>(decoded.channels);
  for (int y = 0; y < decoded.height; ++y) {
    const auto* source = decoded.samples.data() + static_cast<std::size_t>(y) * row_samples;
    if (gray && deep) {
      std::memcpy(image.scanLine(y), source, row_samples * sizeof(std::uint16_t));
    } else if (deep) {
      auto* row = reinterpret_cast<std::uint16_t*>(image.scanLine(y));
      for (int x = 0; x < decoded.width; ++x) {
        row[x * 4] = source[x * 3];
        row[x * 4 + 1] = source[x * 3 + 1];
        row[x * 4 + 2] = source[x * 3 + 2];
        row[x * 4 + 3] = 0xffff;
      }
    } else {
      auto* row = image.scanLine(y);
      for (std::size_t i = 0; i < row_samples; ++i) {
        row[i] = static_cast<uchar>(source[i]);
      }
    }
  }
  return image;
}

// Qt reports every decode failure as "Unable to read image data" or "Unsupported image
// format". When the bytes show the real reason, say that instead (GitHub issue 117).
QString open_failure_reason(const QString& decoder_error, const QString& path, const QString& extension) {
  std::vector<std::uint8_t> bytes;
  try {
    bytes = read_all_file_bytes(path);
  } catch (const std::exception&) {
    return decoder_error;
  }
  const auto diagnosis = formats::diagnose_undecodable_image(bytes, extension.toStdString());
  const auto detail = QString::fromStdString(diagnosis.detail);
  const auto claimed = extension.toUpper();
  using Reason = formats::UndecodableImageReason;
  switch (diagnosis.reason) {
    case Reason::EmptyFile:
      return QObject::tr("The file is empty (0 bytes).");
    case Reason::TextContent:
      return QObject::tr("The file holds text, not image data: a web page or an error message saved under an "
                         "image name.");
    case Reason::DifferentFormat:
      if (!diagnosis.suggested_extension.empty()) {
        return QObject::tr("This is a %1 file, not %2. Give it the .%3 extension and open it again.")
            .arg(detail, claimed, QString::fromStdString(diagnosis.suggested_extension));
      }
      return QObject::tr("This is a %1 file, not %2, and Patchy cannot open %1 files.").arg(detail, claimed);
    case Reason::UnsupportedFormat:
      return QObject::tr("Patchy cannot open %1 files.").arg(detail);
    case Reason::JpegTruncated:
      return QObject::tr("The JPEG data ends early: the file was cut short while it was downloaded or copied.");
    case Reason::JpegNoFrame:
      return QObject::tr("The JPEG header is damaged: no frame description precedes the image data.");
    case Reason::JpegLossless:
      return QObject::tr("This JPEG uses lossless coding, which the JPEG decoder does not support.");
    case Reason::JpegHierarchical:
      return QObject::tr("This JPEG uses hierarchical coding, which the JPEG decoder does not support.");
    case Reason::Jpeg12Bit:
      return QObject::tr("This JPEG stores 12-bit samples; only 8-bit JPEG can be decoded.");
    case Reason::TiffJpegCompression:
      return QObject::tr("This TIFF is JPEG-compressed, which the TIFF decoder does not support. Save it again "
                         "with LZW, ZIP or no compression.");
    case Reason::TiffUnsupportedCompression:
      return QObject::tr("This TIFF uses %1 compression, which the TIFF decoder does not support. Save it again "
                         "with LZW, ZIP or no compression.")
          .arg(detail);
    case Reason::TiffUnsupportedBitDepth:
      return QObject::tr("This TIFF stores %1 bits per sample. The TIFF decoder reads 8 and 16-bit integer, 32-bit "
                         "floating-point, and 1, 2 and 4-bit grayscale samples.")
          .arg(detail);
    case Reason::TiffIntegerSamples:
      return QObject::tr("This TIFF stores 32-bit integer samples; the TIFF decoder reads 32-bit samples only as "
                         "floating point.");
    case Reason::TiffPackedColor:
      return QObject::tr("This TIFF packs %1-bit color samples; the TIFF decoder reads fewer than 8 bits per sample "
                         "only for grayscale images.")
          .arg(detail);
    case Reason::TiffUnreadable:
      return QObject::tr("The TIFF header or its first directory is damaged.");
    case Reason::Unknown:
      break;
  }
  return decoder_error;
}

bool is_svg_extension(const QString& extension) {
  return extension == QStringLiteral("svg") || extension == QStringLiteral("svgz");
}

bool is_kra_extension(const QString& extension) {
  return extension == QStringLiteral("kra") || extension == QStringLiteral("krz");
}

// GIMP's .xcf and its single-suffix compressed names (docs/xcf.md).
bool is_xcf_extension(const QString& extension) {
  return extension == QStringLiteral("xcf") || extension == QStringLiteral("xcfgz") ||
         extension == QStringLiteral("xcfbz2") || extension == QStringLiteral("xcfxz");
}

// Whether an animated GIF export would have at least one frame (visible top-level layers
// become the frames).
bool has_visible_top_level_layer(const Document& document) {
  const auto& layers = document.layers();
  return std::any_of(layers.begin(), layers.end(), [](const Layer& layer) { return layer.visible(); });
}

// File Info (docs/file-info.md): PSD/PSB metadata was lifted by the PSD reader; every
// other format is scanned here from the file bytes. The source RGB profile becomes the
// document's embedded profile only when the pixels are still in the file's color space.
void capture_file_info(Document& opened, const QString& path, bool pixels_in_source_space,
                       bool orientation_applied) {
  const QFileInfo file(path);
  auto& info = opened.file_info();
  if (info.source_format == "PSD" || info.source_format == "PSB") {
    info.source_file_size = static_cast<std::uint64_t>(std::max<qint64>(0, file.size()));
    return;
  }
  info = metadata::extract_file_info_from_path(to_filesystem_path(path));
  if (info.source_format.empty()) {
    info.source_format = file.suffix().toUpper().toStdString();
  }
  if (orientation_applied) {
    if (const auto original = metadata::bake_orientation(info); !original.empty()) {
      FileInfoObserved note;
      note.container = "EXIF";
      note.label = "Orientation applied when the file was opened";
      note.summary = original;
      info.observed.push_back(std::move(note));
    }
  }
  if (!info.source_icc) {
    return;
  }
  const auto& profile = *info.source_icc;
  if (!metadata::icc_profile_is_rgb(profile)) {
    info.source_icc_note = QT_TRANSLATE_NOOP(
        "QObject", "This profile does not describe RGB data, so Patchy shows it for reference only and does not "
                   "write it to saves.");
  } else if (!pixels_in_source_space) {
    info.source_icc_note = QT_TRANSLATE_NOOP(
        "QObject", "Patchy converted the pixels to sRGB when it opened the file, so this profile is shown for "
                   "reference only and is not written to saves.");
  } else if (opened.color_state().embedded_icc_profile.empty()) {
    opened.color_state().embedded_icc_profile = profile;
  }
}

// A file whose RGB pixels are in another RGB profile (Display P3, Adobe RGB, ProPhoto)
// converts to sRGB, the space Patchy edits and shows in, the same way for every format
// (docs/rgb-profiles.md). Relative colorimetric with black point compensation, Photoshop's
// conversion defaults. The imports/convertRgbProfilesToSrgb preference (on by default)
// turns it off, which keeps the pixels and the tag as before.
void convert_opened_rgb_profile(Document& opened, QStringList& import_notices, bool cmyk_source) {
  const auto profile = std::as_const(opened).color_state().embedded_icc_profile;
  if (cmyk_source || profile.empty() || std::as_const(opened).palette_editing().has_value() ||
      !app_settings().value(QStringLiteral("imports/convertRgbProfilesToSrgb"), true).toBool() ||
      !rgb_profile_needs_conversion(profile)) {
    return;
  }
  if (const auto mode = opened.metadata().values.find("psd.color_mode");
      mode != opened.metadata().values.end() && mode->second != "RGB") {
    return;
  }
  const auto name = QString::fromStdString(document_rgb_profile_name(profile));
  const auto conversion = convert_document_rgb_profile(opened, {}, RenderingIntent::RelativeColorimetric, true);
  if (!conversion.has_value()) {
    import_notices.push_back(
        QObject::tr("The color profile %1 could not be converted to sRGB, so the file keeps its colors and profile")
            .arg(name));
    return;
  }
  auto& info = opened.file_info();
  if (!info.source_icc) {
    info.source_icc = std::make_shared<const std::vector<std::uint8_t>>(profile);
  }
  info.source_icc_note = QT_TRANSLATE_NOOP(
      "QObject", "Patchy converted the pixels to sRGB when it opened the file, so this profile is shown for "
                 "reference only and is not written to saves.");
  // PNG color chunks that describe the source space would mislabel the sRGB pixels.
  std::erase_if(info.png_color_chunks, [](const FileInfoChunk& chunk) {
    const std::string type(chunk.type.begin(), chunk.type.end());
    return type == "sRGB" || type == "gAMA" || type == "cHRM" || type == "cICP";
  });
  import_notices.push_back(QObject::tr("Converted from the color profile %1 to sRGB").arg(name));
  if (conversion->kept_stored_colors) {
    import_notices.push_back(
        QObject::tr("Colors stored in text, shape, fill and adjustment layers and in layer styles keep their "
                    "values from the file; only pixels were converted to sRGB"));
  }
}

// After a PSD reader import (a PSD, PSB or layered TIFF): force the Import Notes popup for
// conversions a save makes permanent, open CMYK sources in CMYK Proof (`cmyk_source`), note
// Blend If it cannot render, and put stale or missing linked smart-object files first.
void finish_photoshop_layers_import(Document& opened, const QFileInfo& info, QStringList& import_notices,
                                    bool& force_import_notices_popup, bool& cmyk_source) {
  // Only when the depth was converted: a document that kept it lost nothing.
  if (const auto depth = opened.metadata().values.find("psd.depth");
      depth != opened.metadata().values.end() && depth->second != "8" &&
      opened.color_state().bit_depth == BitDepth::UInt8) {
    force_import_notices_popup = true;
  }
  // The other conversions a save makes permanent: a color mode with no RGB, CMYK or
  // Grayscale equivalent, and adjustment layers that acted on CMYK inks.
  if (const auto mode = opened.metadata().values.find("psd.color_mode");
      mode != opened.metadata().values.end() && mode->second != "RGB" && mode->second != "CMYK" &&
      mode->second != "Grayscale") {
    force_import_notices_popup = true;
  }
  if (opened.metadata().values.contains("psd.ink_adjustments")) {
    force_import_notices_popup = true;
  }
  // A CMYK PSD opens in CMYK Proof like a CMYK JPEG or TIFF (GitHub issue 220). The reader
  // already converted through the file's profile and said so; an unchanged file saves its
  // own inks and profile again (docs/psd-native-color.md), everything else the working one.
  if (const auto mode = opened.metadata().values.find("psd.color_mode");
      mode != opened.metadata().values.end() && mode->second == "CMYK") {
    cmyk_source = true;
    const auto working = working_cmyk_space();
    const auto profile = psd::DocumentIo::source_icc_profile(opened);
    if (const auto transform = CmykToRgbTransform::from_icc_profile(profile);
        transform.has_value() && working->has_profile()) {
      const auto profile_name = QString::fromStdString(transform->profile_description());
      if (const auto working_name = QString::fromStdString(working->profile_description());
          working_name != profile_name) {
        import_notices.push_back(
            QObject::tr("The file's CMYK profile %1 differs from the working CMYK profile %2, which CMYK Proof "
                        "and CMYK JPEG, TIFF and PDF saves use")
                .arg(profile_name, working_name));
      }
    }
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
      if (smart_object_link_changed_on_disk(source, QFileInfo(*resolved))) {
        link_notices.push_back(
            QObject::tr("Linked file %1 has changed on disk; use Update Smart Object Content")
                .arg(file_name));
      }
    }
  }
  for (int i = 0; i < link_notices.size(); ++i) {
    import_notices.insert(i, link_notices.at(i));
  }
}

// A TIFF carrying Photoshop layers in tag 37724 opens with them (docs/tiff.md). False when
// the file has none or they cannot be read: the caller then opens the composite as it
// always has, with a note saying why the layers are missing.
bool open_layered_tiff(const QString& path, const QFileInfo& info, Document& opened, QStringList& import_notices,
                       bool& force_import_notices_popup, bool& cmyk_source) {
  const auto bytes = read_all_file_bytes(path);
  if (!psd::tiff_has_photoshop_layers(bytes)) {
    return false;
  }
  std::vector<std::string> notices;
  psd::ReadOptions options;
  options.notices = &notices;
  try {
    auto document = psd::read_layered_tiff(bytes, options);
    // File Info comes from the TIFF itself (capture_file_info), not the PSD resources.
    document.file_info() = DocumentFileInfo{};
    opened = std::move(document);
  } catch (const std::exception& error) {
    import_notices.push_back(
        QObject::tr("The Photoshop layers in this TIFF could not be read, so it opened as a flat image: %1")
            .arg(translated_file_message(error.what())));
    return false;
  }
  for (const auto& notice : notices) {
    import_notices.push_back(translated_file_message(notice));
  }
  finish_photoshop_layers_import(opened, info, import_notices, force_import_notices_popup, cmyk_source);
  return true;
}

// Opens `path` with the reader for `extension`, which is the file's own extension except
// when load_document_from_path opens it by its content (GitHub issue 362).
OpenDocumentResult load_document_from_path_as(const QString& path, const QString& extension) {
  const auto info = QFileInfo(path);
  // Qt's reader picks its plugin by the file name; one opened by content is told the format.
  const auto set_reader_format = [&](QImageReader& reader) {
    if (extension != info.suffix().toLower()) {
      reader.setFormat(extension.toLatin1());
    }
  };
  Document opened;
  QStringList import_notices;
  bool force_import_notices_popup = false;
  // True when the decoder handed over the file's pixel values untouched (Qt's
  // PNG/JPEG/TIFF/WebP/GIF readers, Patchy's BMP reader): the file's RGB profile then
  // still describes them and is kept for saves (docs/file-info.md). Decoders that
  // convert to sRGB (WIC HEIF/JPEG XR, LibRaw, the HEIF Qt fallback) leave it false.
  bool pixels_in_source_space = false;
  // True when the decoder rotated or mirrored the pixels by the file's orientation tag
  // (Qt's autoTransform, the WIC HEIF reader, LibRaw); the tag is then written back as
  // "normal" so saves do not apply it twice.
  bool orientation_applied = false;
  bool cmyk_source = false;
  const auto load_via_qt = [&] {
    pixels_in_source_space = true;
    if (extension == QStringLiteral("webp")) {
      const auto bytes = read_all_file_bytes(path);
      int index = 0;
      if (webp::decode_animation(bytes, static_cast<std::uint64_t>(std::max(0, QImageReader::allocationLimit())) * 1024 * 1024,
          [&](const webp::AnimationInfo& info) {
            opened = Document(info.width, info.height, PixelFormat::rgba8());
            opened.metadata().values[webp::kLoopCountMetadata] = std::to_string(info.loop_count);
          }, [&](PixelBuffer pixels, std::uint32_t delay) {
            const auto name = QObject::tr("Frame %1").arg(++index) + QLatin1Char(' ') +
                              QString::fromStdString(animation::format_delay_seconds_token(delay));
            opened.add_pixel_layer(name.toStdString(), std::move(pixels));
          })) {
        std::reverse(opened.layers().begin(), opened.layers().end());
        if (!opened.layers().empty()) opened.set_active_layer(opened.layers().back().id());
        opened.print_settings().horizontal_ppi = kUntaggedImportPpi;
        opened.print_settings().vertical_ppi = kUntaggedImportPpi;
        import_notices.push_back(QObject::tr("Animated WebP: imported %1 frames as layers").arg(index));
        return;
      }
    }
    // Animated PNG (GitHub issue 119): Qt's reader returns the default image
    // only, so the frames come through apng_reader and become layers the way
    // GIF and WebP frames do (frame 1 on top, all visible, millisecond delay
    // tokens). A damaged animation keeps the default image with a notice.
    if (extension == QStringLiteral("png") || extension == QStringLiteral("apng")) {
      const auto bytes = read_all_file_bytes(path);
      if (apng::is_animated_png(bytes)) {
        try {
          auto animation = apng::read_animated_png(bytes);
          std::vector<QImage> frames;
          QStringList layer_names;
          const QImage first_frame = animation.frames.front().image;
          for (auto& frame : animation.frames) {
            layer_names.push_back(QObject::tr("Frame %1").arg(frames.size() + 1) + QLatin1Char(' ') +
                                  QString::fromStdString(animation::format_delay_seconds_token(frame.delay_ms)));
            frames.push_back(std::move(frame.image));
          }
          const auto frame_count = static_cast<int>(frames.size());
          if (auto imported = document_from_frames(std::move(frames), layer_names,
                                                   {.first_frame_on_top = true, .all_layers_visible = true});
              imported.has_value()) {
            opened = std::move(*imported);
            apply_imported_image_density(opened, bytes, first_frame);
            // Layers as Animated PNG offers the file's own play count again.
            opened.metadata().values[apng::kLoopCountMetadata] = std::to_string(animation.play_count);
            import_notices.push_back(QObject::tr("Animated PNG: imported %1 frames as layers").arg(frame_count));
            return;
          }
        } catch (const std::exception& error) {
          import_notices.push_back(QObject::tr("Animated PNG: imported the default image only (%1)")
                                       .arg(QString::fromUtf8(error.what())));
        }
      }
    }
    QImageReader reader(path);
    set_reader_format(reader);
    reader.setAutoTransform(true);
    auto image = reader.read();
    if (image.isNull() && is_tiff_extension(extension)) {
      // Qt's libtiff has no JPEG codec; JPEG-compressed TIFFs decode strip by strip
      // through Qt's JPEG reader instead (GitHub issue 117).
      const auto bytes = read_all_file_bytes(path);
      if (tiff_jpeg::is_jpeg_compressed_tiff(bytes)) {
        image = tiff_jpeg::read_jpeg_compressed_tiff(bytes).image;
      }
    }
    if (image.isNull()) {
      // Qt's JPEG plug-in reads 8-bit samples only; 12-bit and lossless JPEGs decode through
      // the vendored libjpeg-turbo (#438). Its errors name the real reason.
      if (const auto bytes = read_all_file_bytes(path); jpeg_deep::needs_deep_decoder(bytes)) {
        image = qimage_from_deep_jpeg(jpeg_deep::read_deep_jpeg(bytes));
      }
    }
    if (image.isNull()) {
      throw std::runtime_error(open_failure_reason(reader.errorString(), path, extension).toStdString());
    }
    orientation_applied = reader.transformation() != QImageIOHandler::TransformationNone;
    // A multi-page TIFF opens its first page, as in Photoshop; pages are separate images
    // (scans, faxes, GIMP's layers-as-pages export), not a layer stack (docs/tiff.md).
    if (is_tiff_extension(extension)) {
      if (const auto pages = psd::tiff_page_count(read_all_file_bytes(path)); pages > 1U) {
        import_notices.push_back(
            QObject::tr("This TIFF holds %1 pages; Patchy opened the first one.").arg(static_cast<qulonglong>(pages)));
      }
    }
    // Animated GIFs import every frame as a layer, frame 1 on TOP and all layers visible,
    // each frame's delay stamped as a trailing seconds token ("Frame 3 0.4s") that the
    // animated GIF export parses back, so a straight open-then-save round-trips. qgif
    // returns fully composited full-canvas frames; nextImageDelay() is the just-read
    // frame's delay in milliseconds (natively centiseconds * 10).
    if (extension == QStringLiteral("gif") && reader.supportsAnimation() && reader.imageCount() > 1) {
      std::vector<QImage> frames;
      QStringList layer_names;
      frames.push_back(image);
      layer_names.push_back(
          QObject::tr("Frame %1").arg(1) + QLatin1Char(' ') +
          QString::fromStdString(gif::format_delay_seconds_token(static_cast<std::uint16_t>(
              std::clamp<long long>(std::llround(reader.nextImageDelay() / 10.0), 0, 0xffff)))));
      for (int i = 1; i < reader.imageCount(); ++i) {
        auto frame = reader.read();
        if (frame.isNull()) {
          break;  // a damaged tail keeps the frames that decoded
        }
        const auto delay_cs = static_cast<std::uint16_t>(
            std::clamp<long long>(std::llround(reader.nextImageDelay() / 10.0), 0, 0xffff));
        layer_names.push_back(QObject::tr("Frame %1").arg(i + 1) + QLatin1Char(' ') +
                              QString::fromStdString(gif::format_delay_seconds_token(delay_cs)));
        frames.push_back(std::move(frame));
      }
      if (frames.size() > 1) {
        const auto frame_count = static_cast<int>(frames.size());
        auto imported = document_from_frames(std::move(frames), layer_names,
                                             {.first_frame_on_top = true, .all_layers_visible = true});
        if (imported.has_value()) {
          opened = std::move(*imported);
          apply_imported_image_density(opened, read_all_file_bytes(path), image);
          // Always 2+ frames here, so the plain plural reads right.
          import_notices.push_back(QObject::tr("Animated GIF: imported %1 frames as layers").arg(frame_count));
          return;
        }
      }
    }
    if (reader.supportsAnimation()) {
      const auto frames = reader.imageCount();
      if (frames > 1) {
        import_notices.push_back(
            QObject::tr("Animated image: imported the first frame only (%1 frames in the file)").arg(frames));
      }
    }
    if (image.format() == QImage::Format_CMYK8888) {
      cmyk_source = true;
      pixels_in_source_space = false;
      QString profile_name;
      if (const auto rgb = srgb_image_from_cmyk(image, profile_name); !rgb.isNull()) {
        opened = document_from_qimage(rgb, info.completeBaseName().toStdString());
        apply_imported_image_density(opened, read_all_file_bytes(path), image);
        import_notices.push_back(QObject::tr("CMYK image: converted to RGB through the profile %1").arg(profile_name));
        const auto working = working_cmyk_space();
        if (const auto working_name = QString::fromStdString(working->profile_description());
            working->has_profile() && working_name != profile_name) {
          import_notices.push_back(
              QObject::tr("The file's CMYK profile %1 differs from the working CMYK profile %2, which CMYK saves use")
                  .arg(profile_name, working_name));
        }
        return;
      }
    }
    opened = document_from_qimage(image, info.completeBaseName().toStdString());
    apply_imported_image_density(opened, read_all_file_bytes(path), image);
  };
  // PDF before the registry, like PSD: the reader is Qt PDF, which cannot live in the
  // Qt-free format registry. This is the non-interactive path (CLI opens, tests, scripts,
  // Files as Layers), so it renders page 1 at the remembered resolution and says so; Open
  // and Reopen go through the page picker in load_document_interactive first.
  if (is_pdf_extension(extension)) {
    PdfImportOptions pdf_options;
    pdf_options.resolution_ppi =
        app_settings().value(QStringLiteral("imports/pdfResolution"), pdf_options.resolution_ppi).toInt();
    pdf_options.white_background =
        app_settings().value(QStringLiteral("imports/pdfWhiteBackground"), pdf_options.white_background).toBool();
    QString pdf_error;
    auto pdf_result = load_pdf_document(path, pdf_options, QString(), &pdf_error);
    if (!pdf_result.has_value()) {
      throw std::runtime_error(pdf_error.toStdString());
    }
    for (const auto& notice : pdf_result->notices) {
      import_notices.push_back(translated_file_message(notice));
    }
    opened = std::move(pdf_result->document);
  } else if (is_photoshop_document_extension(extension)) {
    std::vector<std::string> psd_notices;
    psd::ReadOptions psd_options{true, false, true};
    psd_options.notices = &psd_notices;
    opened = psd::DocumentIo::read_file(to_filesystem_path(path), psd_options);
    for (const auto& notice : psd_notices) {
      import_notices.push_back(translated_file_message(notice));
    }
    finish_photoshop_layers_import(opened, info, import_notices, force_import_notices_popup, cmyk_source);
  } else if (is_tiff_extension(extension) &&
             open_layered_tiff(path, info, opened, import_notices, force_import_notices_popup, cmyk_source)) {
    // The layers' pixels are the file's own values, as in a PSD.
    pixels_in_source_space = true;
  } else if (jxl::is_jxl_extension(extension.toStdString())) {
    // Before the registry row so animation frames get translated layer names and the
    // decoder's color and orientation facts reach File Info.
    jxl::ReadOptions read_options;
    read_options.frame_name = [](int index, std::uint32_t delay) {
      return (QObject::tr("Frame %1").arg(index) + QLatin1Char(' ') +
              QString::fromStdString(animation::format_delay_seconds_token(delay)))
          .toStdString();
    };
    auto result = jxl::read_jxl_detailed(read_all_file_bytes(path), read_options);
    opened = std::move(result.read.document);
    for (const auto& notice : result.read.notices) {
      import_notices.push_back(translated_file_message(notice));
    }
    if (result.frame_count > 1) {
      import_notices.push_back(QObject::tr("Animated JPEG XL: imported %1 frames as layers").arg(result.frame_count));
    }
    pixels_in_source_space = result.pixels_in_source_space;
    orientation_applied = result.orientation_applied;
  } else if (avif::is_avif_extension(extension.toStdString()) && avif::is_available()) {
    // Before the registry row so image-sequence frames get translated layer names
    // (docs/avif.md). The reader applies the container's rotation and mirror itself.
    avif::ReadOptions read_options;
    read_options.frame_name = [](int index, std::uint32_t delay) {
      return (QObject::tr("Frame %1").arg(index) + QLatin1Char(' ') +
              QString::fromStdString(animation::format_delay_seconds_token(delay)))
          .toStdString();
    };
    auto result = avif::read_avif_detailed(read_all_file_bytes(path), read_options);
    opened = std::move(result.read.document);
    for (const auto& notice : result.read.notices) {
      import_notices.push_back(translated_file_message(notice));
    }
    if (result.frame_count > 1) {
      import_notices.push_back(QObject::tr("Animated AVIF: imported %1 frames as layers").arg(result.frame_count));
    }
    pixels_in_source_space = result.pixels_in_source_space;
    orientation_applied = result.orientation_applied;
  } else if (raw::is_camera_raw_extension(extension.toStdString())) {
    const auto settings = load_raw_develop_settings(path);
    auto result = raw::read_camera_raw(read_all_file_bytes(path), settings.params);
    opened = std::move(result.document);
    orientation_applied = true;
    if (!settings.notice.isEmpty()) import_notices.push_back(settings.notice);
  } else if (is_kra_extension(extension)) {
    // Krita documents read here rather than through the registry row: the reader needs the
    // file's folder for file layers (docs/kra.md).
    std::vector<std::string> notices;
    opened = kra::DocumentIo::read(read_all_file_bytes(path), &notices,
                                   to_filesystem_path(QFileInfo(path).absolutePath()));
    for (const auto& notice : notices) {
      import_notices.push_back(translated_file_message(notice));
    }
    decode_pending_svg_images(opened.layers(), import_notices);
  } else if (const auto* handler = builtin_format_registry().find_by_extension(extension.toStdString());
             handler != nullptr) {
    try {
      auto result = handler->read(read_all_file_bytes(path));
      opened = std::move(result.document);
      for (const auto& notice : result.notices) {
        import_notices.push_back(translated_file_message(notice));
      }
      // Containers with no density concept open at Photoshop's untagged 72 PPI
      // (their readers construct Documents with the 300 PPI new-document default).
      // BMP/HEIF/camera-raw record real densities and keep what their reader set.
      static const std::set<std::string> kDensitylessFormats = {
          "patchy.formats.ico", "patchy.formats.tga", "patchy.formats.aseprite",
          "patchy.formats.pcx", "patchy.formats.ilbm", "patchy.formats.rttex",
          "patchy.formats.dds", "patchy.formats.hdr", "patchy.formats.exr", "patchy.formats.jp2",
          "patchy.formats.avif"};
      if (kDensitylessFormats.contains(handler->identifier)) {
        opened.print_settings().horizontal_ppi = kUntaggedImportPpi;
        opened.print_settings().vertical_ppi = kUntaggedImportPpi;
      }
      // The SVG reader sets its own density (96 PPI for physical units, 72
      // otherwise), so it is deliberately not in the densityless set.
      if (is_svg_extension(extension)) {
        decode_pending_svg_images(opened.layers(), import_notices);
      }
      pixels_in_source_space = handler->identifier == "patchy.formats.bmp";
      orientation_applied = heif::is_heif_extension(extension.toStdString());
    } catch (const std::exception& registry_error) {
      // Nonstandard files that a Qt plugin still understands (e.g. OS/2 BMPs) keep opening
      // through the Qt fallback; when Qt cannot read them either, report the registry
      // reader's error, which names the real problem. macOS and Flatpak HEIC/HEIF rely
      // on this by design: the registry read always throws there and Qt's platform
      // plugin (qmacheif / the KDE runtime's kimg_heif) does the decoding.
      QImageReader reader(path);
      set_reader_format(reader);
      reader.setAutoTransform(true);
      auto image = reader.read();
      if (image.isNull()) {
        throw std::runtime_error(registry_error.what());
      }
      orientation_applied = reader.transformation() != QImageIOHandler::TransformationNone;
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
      if (is_svg_extension(extension)) {
        // The editable importer refused (too complex / past the supported
        // subset) and the qsvg plugin rasterized instead - say so, with why.
        import_notices.push_back(QObject::tr("SVG was imported as flattened raster: %1")
                                     .arg(QString::fromUtf8(registry_error.what())));
      }
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
  // a document alpha channel on resave. SVG is excluded for the same structural
  // reason: its layers own their transparency, and the promotion's set_pixels would
  // clobber a lone placed <image> layer's offset bounds.
  if (opened.color_state().bit_depth == BitDepth::Float32 && !depth_supported_on_platform(BitDepth::Float32)) {
    convert_document_depth(opened, BitDepth::UInt16);
    import_notices.push_back(
        QObject::tr("The 32-bit image was converted to 16 bits per channel: the web version edits up to 16."));
  }
  if (!opened.metadata().values.contains("psd.version") && !is_svg_extension(extension) &&
      extension != QStringLiteral("ora") && !is_kra_extension(extension) && !is_xcf_extension(extension)) {
    promote_flat_alpha_to_layer_mask(opened);
  }
  capture_file_info(opened, path, pixels_in_source_space, orientation_applied);
  convert_opened_rgb_profile(opened, import_notices, cmyk_source);
  // OpenRaster, Krita and GIMP record the selected layer; it stays selected unless it is a group.
  const auto* file_selected = (extension == QStringLiteral("ora") || is_kra_extension(extension) ||
                               is_xcf_extension(extension)) &&
                                      opened.active_layer_id().has_value()
                                  ? std::as_const(opened).find_layer(*opened.active_layer_id())
                                  : nullptr;
  if (file_selected != nullptr && file_selected->kind() != LayerKind::Group) {
    opened.set_active_layer(file_selected->id());
  } else if (const auto default_layer_id = default_non_group_layer_id(opened.layers()); default_layer_id.has_value()) {
    opened.set_active_layer(*default_layer_id);
  } else {
    opened.clear_active_layer();
  }
  OpenDocumentResult result{std::move(opened),         info.fileName(), extension, std::move(import_notices),
                            force_import_notices_popup, {},              {}};
  result.cmyk_source = cmyk_source;
  return result;
}

// Extensions whose formats all decode through the same plain readers (Qt's, Patchy's BMP):
// a file named as one of them that holds another is simply misnamed, never a container
// that legitimately wraps the other format the way a camera raw wraps TIFF.
bool is_plain_raster_extension(const QString& extension) {
  static const QStringList kPlain = {QStringLiteral("png"),  QStringLiteral("apng"), QStringLiteral("jpg"),
                                     QStringLiteral("jpeg"), QStringLiteral("jpe"),  QStringLiteral("jfif"),
                                     QStringLiteral("gif"),  QStringLiteral("webp"), QStringLiteral("bmp")};
  return kPlain.contains(extension);
}

// Opens a file by its content when its name says another format or none (GitHub issue
// 362): an extension Patchy does not open ("src.bin") goes straight to the reader for
// what the bytes are; a PNG named .jpg (or another plain-image swap) reads as what it is;
// any other known extension opens by name first and by content only when that fails. The
// result's content_extension says it happened, so Save asks for a name that fits.
OpenDocumentResult load_document_from_path(QString path) {
  // "name.xcf.gz" and its kin: the last extension alone says nothing (docs/xcf.md).
  const auto extension = xcf::is_compressed_xcf_file_name(QFileInfo(path).fileName().toStdString())
                             ? QStringLiteral("xcf")
                             : QFileInfo(path).suffix().toLower();
  const auto sniffed = formats::sniff_openable_image_format(read_file_head_bytes(path));
  if (!sniffed.has_value() || formats::extensions_share_format(sniffed->extension, extension.toStdString())) {
    return load_document_from_path_as(path, extension);
  }
  const auto content_extension = QString::fromStdString(sniffed->extension);
  const auto open_by_content = [&] {
    auto result = load_document_from_path_as(path, content_extension);
    result.content_extension = content_extension;
    const auto format_name = QString::fromStdString(sniffed->name);
    result.import_notices.push_front(
        extension.isEmpty()
            ? QObject::tr("This is a %1 file with no extension; Save suggests the .%2 extension")
                  .arg(format_name, content_extension)
            : QObject::tr("This is a %1 file named .%2; Save suggests the .%3 extension")
                  .arg(format_name, extension, content_extension));
    return result;
  };
  const bool known_extension =
      is_photoshop_document_extension(extension) || is_supported_image_extension(extension) ||
      is_pdf_extension(extension);
  if (!known_extension || (is_plain_raster_extension(extension) && is_plain_raster_extension(content_extension))) {
    try {
      return open_by_content();
    } catch (const std::exception&) {
      if (known_extension) {
        throw;
      }
      // Bytes that only look like the format: Qt may still recognize the file. If not,
      // the content reader's error names the real problem.
      const auto content_error = std::current_exception();
      try {
        return load_document_from_path_as(path, extension);
      } catch (const std::exception&) {
        std::rethrow_exception(content_error);
      }
    }
  }
  try {
    return load_document_from_path_as(path, extension);
  } catch (const std::exception&) {
    return open_by_content();
  }
}

// The consolidated Import Notes box, one bullet per note. Import notes ride the status
// bar by default; this popup is opt-in through the preference that also gates the PSD
// compatibility report (Seth: do not annoy people with info popups). A data-loss note
// (`forced`: a 16/32-bit source converted to 8-bit, GitHub issue 52) shows regardless,
// as a warning. The object name is shared so tests find either form.
void show_import_notices_popup(QWidget* parent, const QString& file_name, const QStringList& notices,
                               bool forced) {
  const bool popup_preference =
      app_settings().value(QStringLiteral("imports/showPsdWarningsAndInfo"), false).toBool();
  if (notices.isEmpty() || (!popup_preference && !forced)) {
    return;
  }
  QStringList bullets;
  bullets.reserve(notices.size());
  for (const auto& notice : notices) {
    bullets.push_back(QStringLiteral("• ") + notice);
  }
  const auto text = QObject::tr("%1 opened with notes:\n\n%2").arg(file_name, bullets.join(QLatin1Char('\n')));
  const auto object_name = QStringLiteral("importNoticesMessageBox");
  if (forced) {
    (void)show_warning_message(parent, QObject::tr("Import Notes"), text, QMessageBox::Ok, QMessageBox::Ok,
                               object_name);
  } else {
    show_information_message(parent, QObject::tr("Import Notes"), text, object_name);
  }
}

// Shows the open-failure box. Browser HEIC errors get a capability-focused message.
// Windows HEIC errors carry a marker naming the missing Microsoft Store codec package
// (heif_document_io.hpp); those get an extra button that deep-links to that package's
// Store page, Photos-app style.
// file_name, when given, heads the message: a multi-file import that fails on one
// file must say which (GitHub issue 112).
void show_open_failed_message_box(QWidget* parent, const QString& error_text, const QString& file_name = QString()) {
  QString display_text = error_text;
  QString store_product_id;
  const auto with_file_name = [&file_name](const QString& text) {
    return file_name.isEmpty() ? text : QObject::tr("%1\n\n%2").arg(file_name, text);
  };
  // The wasm PDF stub's error: strip the marker and add a download button, Store-button
  // style, because the fix (the desktop build) is a link away.
  const auto pdf_desktop_only_marker = QString::fromUtf8(
      kPdfDesktopOnlyMarker.data(), static_cast<qsizetype>(kPdfDesktopOnlyMarker.size()));
  if (error_text.startsWith(pdf_desktop_only_marker)) {
    QMessageBox dialog(QMessageBox::Critical, QObject::tr("Open failed"),
                       error_text.mid(pdf_desktop_only_marker.size()).trimmed(), QMessageBox::Ok, parent);
    dialog.setObjectName(QStringLiteral("openFailedMessageBox"));
    auto* download_button = dialog.addButton(QObject::tr("Get the Desktop Version"), QMessageBox::ActionRole);
    dialog.setDefaultButton(download_button);
    exec_dialog(dialog);
    if (dialog.clickedButton() == download_button) {
      QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/SethRobinson/Patchy#download")));
    }
    return;
  }
  const auto browser_hevc_marker =
      QString::fromUtf8(heif::kBrowserHevcUnavailableMarker.data(),
                        static_cast<qsizetype>(heif::kBrowserHevcUnavailableMarker.size()));
  if (error_text.startsWith(browser_hevc_marker)) {
    show_critical_message(
        parent, QObject::tr("Open failed"),
        QObject::tr("This browser or device cannot decode this HEIC image because an HEVC "
                    "decoder is unavailable. Try another current browser or device with HEVC "
                    "support."),
        QStringLiteral("openFailedMessageBox"));
    return;
  }
  const auto browser_decode_marker =
      QString::fromUtf8(heif::kBrowserHeifDecodeFailedMarker.data(),
                        static_cast<qsizetype>(heif::kBrowserHeifDecodeFailedMarker.size()));
  if (error_text.startsWith(browser_decode_marker)) {
    show_critical_message(
        parent, QObject::tr("Open failed"),
        QObject::tr("Patchy could not decode this HEIC image. The file may be damaged or use "
                    "a profile this browser does not support."),
        QStringLiteral("openFailedMessageBox"));
    return;
  }
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
    show_critical_message(parent, QObject::tr("Open failed"), with_file_name(display_text),
                          QStringLiteral("openFailedMessageBox"));
    return;
  }
  QMessageBox dialog(QMessageBox::Critical, QObject::tr("Open failed"), with_file_name(display_text), QMessageBox::Ok,
                     parent);
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
// preference off or from a script, the shared file-opening path reads the photo's
// sidecar. Everything else loads on a worker thread
// behind a modal progress dialog so big documents keep the UI responsive.
// Load failures propagate as exceptions.
std::optional<OpenDocumentResult> load_document_interactive(QWidget* parent, const QString& path,
                                                          bool interactive, bool allow_raw_dialog) {
  const QFileInfo info(path);
  const auto extension = info.suffix().toLower();
  if (interactive && allow_raw_dialog && raw::is_camera_raw_extension(extension.toStdString()) &&
      app_settings().value(QStringLiteral("imports/showRawDevelopDialog"), true).toBool()) {
    auto outcome = run_raw_develop_dialog(parent, path);
    if (!outcome.has_value()) {
      return std::nullopt;
    }
    OpenDocumentResult loaded{std::move(outcome->document), info.fileName(), extension, {}, {}, {}, {}};
    if (const auto default_layer_id = default_non_group_layer_id(loaded.document.layers());
        default_layer_id.has_value()) {
      loaded.document.set_active_layer(*default_layer_id);
    }
    return loaded;
  }
  // PDFs get their page picker before the worker load, the same way raws get the develop
  // dialog: the pages and the rasterization resolution have to be chosen before anything
  // is rendered. There is no preference to skip it, because there is no sane default page
  // set for a multi-page file.
  if (interactive && is_pdf_extension(extension) && pdf_import_is_available()) {
    auto outcome = run_pdf_import_dialog(parent, path);
    if (!outcome.has_value()) {
      return std::nullopt;
    }
    QStringList notices;
    notices.reserve(static_cast<qsizetype>(outcome->notices.size()));
    for (const auto& notice : outcome->notices) {
      notices.push_back(QString::fromStdString(notice));
    }
    OpenDocumentResult loaded{std::move(outcome->document),
                              info.fileName(),
                              extension,
                              std::move(notices),
                              false,
                              std::move(outcome->document_title),
                              std::move(outcome->extra_documents)};
    loaded.pdf_color_mode = outcome->color_mode;
    loaded.pdf_bit_depth = outcome->bit_depth;
    loaded.pdf_merge = outcome->merge;
    loaded.pdf_white_background = outcome->white_background;
    // CMYK is the session's CMYK Proof, the same state a CMYK JPEG opens in.
    loaded.cmyk_source = outcome->color_mode == PdfImportColorMode::Cmyk;
    if (const auto default_layer_id = default_non_group_layer_id(loaded.document.layers());
        default_layer_id.has_value()) {
      loaded.document.set_active_layer(*default_layer_id);
    }
    for (auto& page : loaded.extra_documents) {
      if (const auto default_layer_id = default_non_group_layer_id(page.document.layers());
          default_layer_id.has_value()) {
        page.document.set_active_layer(*default_layer_id);
      }
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

#if defined(Q_OS_WASM) && defined(__EMSCRIPTEN_PTHREADS__)
  // future.wait_for on the browser main thread busy-spins (the main thread
  // cannot Atomics.wait), so the pump below would starve the browser event
  // loop for the whole load: no dialog frames, and the tab gets flagged
  // unresponsive. Wait in an event loop instead; idle exec suspends through
  // Asyncify, the dialog paints, and the worker's queued completion ends the
  // wait. The references stay valid because this function only returns after
  // the loop quits, which the worker posts after it is done touching them.
  std::optional<OpenDocumentResult> loaded;
  const auto error = std::make_shared<std::exception_ptr>();
  QEventLoop wait_loop;
  auto* app = QCoreApplication::instance();
  run_tracked_background_worker([&loaded, error, app, &wait_loop, path] {
    try {
      loaded = load_document_from_path(path);
    } catch (...) {
      *error = std::current_exception();
    }
    if (app == nullptr) {
      return;
    }
    QMetaObject::invokeMethod(app, [&wait_loop] { wait_loop.quit(); }, Qt::QueuedConnection);
  });
  wait_loop.exec();
  if (*error) {
    std::rethrow_exception(*error);
  }
  return loaded;
#else
  auto open_future = launch_async([path] { return load_document_from_path(path); });
  while (open_future.wait_for(std::chrono::milliseconds(15)) != std::future_status::ready) {
    QApplication::processEvents(QEventLoop::AllEvents, 15);
  }

  return open_future.get();
#endif
}

// The status-message tail for a layer-keeping writer's notices: the first note verbatim,
// the rest counted.
QString export_notes_suffix_for(const std::vector<std::string>& notices) {
  if (notices.empty()) {
    return QString();
  }
  QString suffix = QStringLiteral(" ") + translate_data_text(notices.front());
  if (notices.size() > 1) {
    suffix += QObject::tr(" (+%n more export note(s))", nullptr, static_cast<int>(notices.size()) - 1);
  }
  return suffix;
}

// Divide-photos save-format combo rows: the flat-image export formats, minus
// SVG (the layered writer, not write_flat_image_file) and the icon/cursor
// formats (their sizes make no sense for a photo batch).
std::vector<DividePhotosFormatChoice> divide_photos_format_choices() {
  std::vector<DividePhotosFormatChoice> choices;
  for (const auto& entry : file_format_entries()) {
    if (!entry.in_export_dialog || entry.save_extensions.isEmpty()) {
      continue;
    }
    const QString& extension = entry.save_extensions.front();
    if (extension == QStringLiteral("svg") || extension == QStringLiteral("ico") ||
        extension == QStringLiteral("cur")) {
      continue;
    }
    choices.push_back({translate_data_text(entry.display_name), extension});
  }
  return choices;
}

// Export Documents to Folder format rows: the divide-photos flat formats plus the
// layer-keeping writers (PSD, Aseprite), which the folder export routes through the
// same layered writers as Save As instead of write_flat_image_file.
std::vector<ExportDocumentsFormatChoice> export_documents_format_choices() {
  std::vector<ExportDocumentsFormatChoice> choices;
  for (const auto& entry : file_format_entries()) {
    if (entry.save_extensions.isEmpty()) {
      continue;
    }
    const QString& extension = entry.save_extensions.front();
    const bool layered = is_photoshop_document_extension(extension) || extension == QStringLiteral("aseprite");
    if (!layered && !entry.in_export_dialog) {
      continue;
    }
    if (extension == QStringLiteral("svg") || extension == QStringLiteral("ico") ||
        extension == QStringLiteral("cur")) {
      continue;
    }
    choices.push_back({translate_data_text(entry.display_name), extension});
  }
  return choices;
}

// prefix + zero-padded index + "." + extension; pads to 3 digits and grows
// naturally past 999 (the image_sequence_file_names formatting).
QString divide_photo_file_name(const QString& prefix, int index, const QString& extension) {
  return prefix + QStringLiteral("%1").arg(index, 3, 10, QLatin1Char('0')) + QLatin1Char('.') +
         extension;
}

}  // namespace

void MainWindow::open_document() {
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return;
  }
  // Multi-select: every picked file becomes its own document, in dialog order,
  // exactly as a multi-file drop does; the last one opened ends up active.
  auto selected_filter = remembered_open_filter();
  QString browsed_directory;
  const auto paths = get_open_file_names(this, tr("Open"), last_open_directory(), open_file_filter(),
                                         &selected_filter, QStringLiteral("openFileDialog"),
                                         FilterNameDetails::Hidden, &browsed_directory);
  remember_open_filter(paths.isEmpty() ? QString() : selected_filter);
  // A cancelled Open still remembers the folder it was showing (GitHub issue 183); a pick
  // remembers its file's folder in open_document_path.
  if (paths.isEmpty()) {
    remember_open_directory(browsed_directory);
  }
  for (const auto& path : paths) {
    open_document_path(path);
  }
}

void MainWindow::open_folder() {
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return;
  }
  const auto directory =
      QFileDialog::getExistingDirectory(this, tr("Open Folder"), last_open_directory(), QFileDialog::ShowDirsOnly);
  if (directory.isEmpty()) {
    return;
  }
  open_folder_path(directory);
}

// The first image opens through open_document_path (activation, fit, recent file,
// import notes, exactly as a single open); the rest mirror the pages of a PDF opened
// as separate documents (open_extra_imported_page_sessions): background sessions
// under a progress dialog with an event pump per file, so the window keeps painting
// and each tab shows up as it is made. Unlike PDF pages the files are independent,
// so the dialog has a Cancel button and one unreadable file never stops the rest.
int MainWindow::open_folder_path(const QString& directory) {
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return 0;
  }
  const auto native_directory = QDir::toNativeSeparators(QFileInfo(directory).absoluteFilePath());
  const auto paths = supported_image_paths_in_folder(directory);
  if (paths.isEmpty()) {
    show_status_error(tr("No supported images in %1").arg(native_directory));
    return 0;
  }
  const int total = static_cast<int>(paths.size());
  auto folder_name = QFileInfo(QDir(directory).absolutePath()).fileName();
  if (folder_name.isEmpty()) {
    folder_name = native_directory;  // a drive root has no file name
  }
  QProgressDialog progress(tr("Opening image %1 of %2...").arg(1).arg(total), tr("Cancel"), 0, total, this);
  progress.setObjectName(QStringLiteral("openFolderProgressDialog"));
  progress.setWindowTitle(tr("Opening %1").arg(folder_name));
  progress.setWindowModality(Qt::WindowModal);
  progress.setMinimumDuration(0);
  progress.setAutoClose(false);
  progress.setAutoReset(false);
  remember_dialog_position(progress);
  progress.setValue(0);

  std::int64_t first_session_id = 0;
  int opened = 0;
  bool cmyk_proof_opened = false;
  bool cancelled = false;
  QStringList failed;
  for (int index = 0; index < total; ++index) {
    const auto& path = paths[index];
    progress.setLabelText(tr("Opening image %1 of %2...").arg(index + 1).arg(total));
    progress.setValue(index);
    QApplication::processEvents(QEventLoop::AllEvents);
    if (progress.wasCanceled()) {
      cancelled = true;
      break;
    }
    if (first_session_id == 0) {
      // Until one file has opened, each goes through the full single-open path so
      // the first success becomes the active document.
      const auto* before = active_session();
      const auto before_id = before != nullptr ? before->session_id : 0;
      open_document_path(path);
      const auto* after = active_session();
      if (after != nullptr && after->session_id != before_id) {
        first_session_id = after->session_id;
        ++opened;
      } else {
        failed.push_back(QFileInfo(path).fileName());
      }
      continue;
    }
    try {
      const bool allow_raw_dialog = !script_engine_host_ || !script_engine_host_->run_active() ||
                                    script_engine_host_->manual_edit_pause();
      auto loaded = load_document_interactive(this, path, !unattended_automation(), allow_raw_dialog);
      if (!loaded.has_value()) {
        continue;  // cancelled in a per-file dialog (a raw develop, say)
      }
      render_pending_svg_text_layers(loaded->document);
      render_pending_af_text_layers(loaded->document);
      render_pending_pdf_text_layers(loaded->document);
      render_pending_pdf_image_layers(loaded->document);
      lay_out_raster_less_imported_text(loaded->document);
      record_text_layout_metrics_for_reopened_text(loaded->document);
      add_document_session(std::move(loaded->document), loaded->file_name, path, tr("Open"),
                           SessionActivation::Background);
      (**sessions_.rbegin()).content_extension = loaded->content_extension;
      (**sessions_.rbegin()).import_notes = loaded->import_notices;
      start_open_watermark_check((*sessions_.rbegin())->session_id);
      if (loaded->cmyk_source && open_cmyk_source_in_proof_mode(**sessions_.rbegin())) {
        cmyk_proof_opened = true;
      }
      ++opened;
      // Background files drop their ordinary notes (no status bar of their own), but a
      // data-loss note must not go unseen just because the file was not first.
      if (loaded->force_import_notices_popup && !unattended_automation()) {
        show_import_notices_popup(this, loaded->file_name, loaded->import_notices, true);
      }
    } catch (const std::exception& error) {
      failed.push_back(QFileInfo(path).fileName());
      if (unattended_automation()) {
        fprintf(stderr, "Open failed: %s (%s)\n", error.what(), path.toUtf8().constData());
      }
    }
    QApplication::processEvents(QEventLoop::AllEvents);
  }
  progress.setValue(total);
  progress.close();
  // Nothing above moved the active document after the first open, but an event
  // pumped along the way could have: land on the first image either way.
  if (auto* first = session_with_id(first_session_id); first != nullptr && first != active_session()) {
    activate_document_session(*first);
  }
  if (opened > 0) {
    // The folder itself is the thing to come back to; one recent-file entry per
    // image would flush the list.
    add_recent_folder(directory);
    if (!unattended_automation()) {
      remember_open_directory_for_path(paths.front());
    }
  }
  auto status = cancelled ? tr("Opened %1 of %2 images from %3").arg(opened).arg(total).arg(native_directory)
                          : tr("Opened %n image(s) from %1", nullptr, opened).arg(native_directory);
  if (!failed.isEmpty()) {
    status += tr(" (%n could not be opened)", nullptr, static_cast<int>(failed.size()));
    if (!unattended_automation()) {
      show_information_message(this, tr("Open Folder"),
                               tr("These files could not be opened:\n\n%1").arg(failed.join(QLatin1Char('\n'))),
                               QStringLiteral("openFolderFailedMessageBox"));
    }
  }
  statusBar()->showMessage(status);
  update_undo_redo_actions();
  if (cmyk_proof_opened) {
    show_cmyk_proof_open_notice();
  }
  return opened;
}

QStringList MainWindow::openable_file_extensions() {
  QStringList extensions;
  for (const auto& entry : file_format_entries()) {
    extensions.append(entry.open_extensions);
  }
  return extensions;
}

QStringList MainWindow::supported_layer_drop_paths(const QMimeData* mime_data) const {
  if (preview_dialog_edit_locked()) {
    return {};
  }
  return supported_local_open_paths(mime_data);
}

MainWindow::AddFilesAsLayersResult MainWindow::add_files_as_layers(
    DocumentSession& target, const QStringList& paths, std::optional<LayerInsertionTarget> drop_target,
    FailedFilesPolicy policy, const std::function<bool(int, int)>& progress,
    const std::function<bool(DocumentSession&)>& before_mutation, QString* error) {
  AddFilesAsLayersResult result;
  const auto target_session_id = target.session_id;
  const auto fail = [&](const QString& message) {
    if (error != nullptr) {
      *error = message;
    }
    result.added_root_ids_top_to_bottom.clear();
    return result;
  };
  const auto note_failure = [&](const QString& path, const QString& reason) {
    result.failed_paths.push_back(path);
    result.failure_messages.push_back(QStringLiteral("%1: %2").arg(QFileInfo(path).fileName(), reason));
    if (unattended_automation()) {
      fprintf(stderr, "Add as layer failed: %s (%s)\n", reason.toUtf8().constData(), path.toUtf8().constData());
    }
  };

  // Phase 1: decode every file before the document is touched.
  QStringList unique_paths;
  for (const auto& raw_path : paths) {
    const auto path = QDir::toNativeSeparators(raw_path);
    if (!path.isEmpty() && !unique_paths.contains(path)) {
      unique_paths.push_back(path);
    }
  }
  std::vector<Document> loaded_documents;
  loaded_documents.reserve(static_cast<std::size_t>(unique_paths.size()));
  const int total = static_cast<int>(unique_paths.size());
  for (int index = 0; index < total; ++index) {
    const auto& path = unique_paths[index];
    if (progress && !progress(index + 1, total)) {
      result.cancelled = true;
      return result;
    }
    try {
      auto loaded = load_document_from_path(path);
      auto& document = loaded.document;
      render_pending_svg_text_layers(document);
      render_pending_af_text_layers(document);
      render_pending_pdf_text_layers(document);
      render_pending_pdf_image_layers(document);
      lay_out_raster_less_imported_text(document);
      record_text_layout_metrics_for_reopened_text(document);
      if (document.layers().empty()) {
        note_failure(path, tr("the file holds no layers"));
        continue;
      }
      const auto layer_name = QFileInfo(path).completeBaseName().toStdString();
      if (document.layers().size() == 1U) {
        if (!layer_name.empty()) {
          document.layers().front().set_name(layer_name);
        }
      } else {
        // A multi-layer file (a PSD, GIF frames, a layered SVG) stays one unit: a
        // pass-through folder named after the file with its layers inside, in
        // their own order.
        Layer folder(document.allocate_layer_id(), layer_name.empty() ? tr("Folder").toStdString() : layer_name,
                     LayerKind::Group);
        folder.set_blend_mode(BlendMode::PassThrough);
        for (auto& layer : document.layers()) {
          folder.add_child(std::move(layer));
        }
        document.layers().clear();
        const auto folder_id = folder.id();
        document.layers().push_back(std::move(folder));
        document.set_active_layer(folder_id);
      }
      loaded_documents.push_back(std::move(document));
    } catch (const std::exception& load_error) {
      note_failure(path, translated_file_message(load_error.what()));
    }
  }
  if (loaded_documents.empty()) {
    return fail(result.failure_messages.isEmpty() ? tr("None of the files could be added as layers")
                                                  : result.failure_messages.join(QLatin1Char('\n')));
  }
  if (policy == FailedFilesPolicy::AbortOnAnyFailure && !result.failed_paths.isEmpty()) {
    return fail(result.failure_messages.join(QLatin1Char('\n')));
  }
  // The progress pump may have closed the document (or the window) meanwhile.
  auto* live_target = shutting_down_ ? nullptr : session_with_id(target_session_id);
  if (live_target == nullptr) {
    return fail(tr("The document is no longer open."));
  }

  // Phase 2: build the new stack in a staged copy, so a refusal part-way leaves
  // the live document untouched (there is no partial undo to fall back on).
  // Each copy lands above the active layer and becomes active, so the files
  // stack upward in path order: the last file ends on top.
  Document staged = live_target->document;
  std::vector<LayerId> added_top_to_bottom;
  CrossDocumentLayerPlacement placement;
  placement.keep_source_position = true;
  for (const auto& loaded : loaded_documents) {
    const std::vector<LayerId> root_ids{loaded.layers().front().id()};
    QString copy_error;
    const auto ids = copy_layers_between_documents(loaded, root_ids, staged, placement,
                                                   [] { return true; }, &copy_error);
    if (ids.empty()) {
      return fail(copy_error);
    }
    added_top_to_bottom.insert(added_top_to_bottom.begin(), ids.begin(), ids.end());
  }
  if (drop_target.has_value()) {
    LayerDropRequest request;
    request.layer_ids_top_to_bottom = added_top_to_bottom;
    request.target_layer_id = drop_target->layer_id;
    request.position = drop_target->position;
    if (!move_layers_for_drop(staged.layers(), request)) {
      return fail(tr("The drop target is no longer in the document"));
    }
  }
  if (!before_mutation(*live_target)) {
    result.added_root_ids_top_to_bottom.clear();
    return result;
  }
  live_target->document = std::move(staged);
  result.added_root_ids_top_to_bottom = std::move(added_top_to_bottom);
  return result;
}

bool MainWindow::add_files_as_layers_interactive(const QStringList& paths,
                                                 std::optional<LayerInsertionTarget> drop_target,
                                                 const QString& failure_title) {
  if (!has_active_document()) {
    return false;
  }
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return false;
  }
  canvas_->finish_free_transform();
  finish_active_text_editor();
  if (!drop_target.has_value()) {
    // Paste's rule: directly above the topmost selected row.
    const auto selected = selected_layer_ids();
    if (!selected.empty()) {
      drop_target = LayerInsertionTarget{selected.front(), LayerDropPosition::AboveItem};
    }
  }
  const auto target_session_id = session().session_id;
  // Decoding many files takes a while: the same cancellable progress dialog and
  // per-file event pump as Open Folder, so the window keeps painting and nobody
  // takes the pause for a hang. The dialog is window-modal, so the document
  // cannot change under the loop, and the core re-resolves the session anyway.
  QProgressDialog progress(tr("Adding file %1 of %2...").arg(1).arg(paths.size()), tr("Cancel"), 0,
                           static_cast<int>(paths.size()), this);
  progress.setObjectName(QStringLiteral("filesAsLayersProgressDialog"));
  progress.setWindowTitle(failure_title);
  progress.setWindowModality(Qt::WindowModal);
  progress.setMinimumDuration(0);
  progress.setAutoClose(false);
  progress.setAutoReset(false);
  remember_dialog_position(progress);
  progress.setValue(0);
  progress.show();  // at once, not after QProgressDialog's force timer
  QString error;
  auto result = add_files_as_layers(
      session(), paths, drop_target, FailedFilesPolicy::SkipFailed,
      [&progress](int index, int total) {
        progress.setMaximum(total);
        progress.setLabelText(tr("Adding file %1 of %2...").arg(index).arg(total));
        progress.setValue(index - 1);
        QApplication::processEvents(QEventLoop::AllEvents);
        return !progress.wasCanceled();
      },
      [this](DocumentSession& live_target) {
        push_undo_snapshot(live_target, tr("Add files as layers"));
        return true;
      },
      &error);
  progress.setValue(progress.maximum());
  progress.close();
  if (shutting_down_ || !isVisible()) {
    return false;
  }
  auto* live_target = session_with_id(target_session_id);
  if (live_target == nullptr) {
    return false;
  }
  if (live_target != active_session()) {
    activate_document_session(*live_target);
  }
  auto& target = *live_target;
  if (result.cancelled) {
    statusBar()->showMessage(tr("Cancelled adding files as layers"));
    return false;
  }
  const auto failure_text = result.failure_messages.join(QLatin1Char('\n'));
  const auto& added = result.added_root_ids_top_to_bottom;
  if (added.empty()) {
    show_status_error(error.section(QLatin1Char('\n'), 0, 0));
    if (!result.failed_paths.isEmpty() && !unattended_automation()) {
      show_information_message(this, failure_title,
                               tr("These files could not be added as layers:\n\n%1").arg(failure_text),
                               QStringLiteral("filesAsLayersFailedMessageBox"));
    }
    return false;
  }
  if (drop_target.has_value() && drop_target->position == LayerDropPosition::OnItem &&
      drop_target->layer_id.has_value()) {
    // A drop into a folder shows what it added (handle_layer_drop's rule).
    if (const auto* folder = std::as_const(target.document).find_layer(*drop_target->layer_id);
        folder != nullptr && folder->kind() == LayerKind::Group) {
      target.collapsed_layer_groups.erase(*drop_target->layer_id);
    }
  }
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
  // After the refreshes: a rebuild collapses the selection to the active row.
  select_layers_in_layer_list(added, added.front());
  auto status = tr("Added %n layer(s)", nullptr, static_cast<int>(added.size()));
  if (!result.failed_paths.isEmpty()) {
    status += tr(" (%n could not be opened)", nullptr, static_cast<int>(result.failed_paths.size()));
    if (!unattended_automation()) {
      show_information_message(this, failure_title,
                               tr("These files could not be added as layers:\n\n%1").arg(failure_text),
                               QStringLiteral("filesAsLayersFailedMessageBox"));
    }
  }
  statusBar()->showMessage(status);
  update_undo_redo_actions();
  return true;
}

void MainWindow::import_files_as_layers() {
  if (!has_active_document()) {
    return;
  }
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return;
  }
  QString browsed_directory;
  const auto paths = get_open_file_names(this, tr("Files as Layers"), last_open_directory(), open_file_filter(),
                                         nullptr, QStringLiteral("importFilesAsLayersFileDialog"),
                                         FilterNameDetails::Hidden, &browsed_directory);
  if (paths.isEmpty()) {
    if (!unattended_automation()) {
      remember_open_directory(browsed_directory);
    }
    return;
  }
  if (!unattended_automation()) {
    remember_open_directory_for_path(paths.front());
  }
  import_files_as_layers_with_paths(paths);
}

void MainWindow::import_files_as_layers_with_paths(const QStringList& paths) {
  add_files_as_layers_interactive(paths, std::nullopt, tr("Files as Layers"));
}

bool MainWindow::accept_open_file_drag(QDropEvent* event) {
  if (preview_dialog_edit_locked()) {
    if (event != nullptr) {
      event->ignore();
    }
    show_preview_dialog_edit_lock_message();
    return false;
  }
  if (event == nullptr || (supported_local_open_paths(event->mimeData()).isEmpty() &&
                           font_or_zip_local_paths(event->mimeData()).isEmpty() &&
                           dropped_directory_paths(event->mimeData()).isEmpty())) {
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
  const auto font_paths = font_or_zip_local_paths(event->mimeData());
  const auto directories = dropped_directory_paths(event->mimeData());
  if (paths.isEmpty() && font_paths.isEmpty() && directories.isEmpty()) {
    event->ignore();
    show_status_error(tr("Drop a supported image, Photoshop document, or font"));
    return false;
  }

  if ((event->possibleActions() & Qt::CopyAction) != 0) {
    event->setDropAction(Qt::CopyAction);
    event->accept();
  } else {
    event->acceptProposedAction();
  }

  // Return from the native drop before opening dialogs or processing files.
  // Windows Explorer waits for its OLE drop call to return, including any nested
  // RAW/PDF/import dialog loop entered here. Own the paths, never the event or
  // its source-owned mime data, and cancel delivery if this window is destroyed.
  QTimer::singleShot(0, this, [this, paths, font_paths, directories] {
    if (shutting_down_ || !isVisible()) {
      return;
    }
    if (preview_dialog_edit_locked()) {
      show_preview_dialog_edit_lock_message();
      return;
    }
    if (!font_paths.isEmpty()) {
      show_user_font_drop_result(user_fonts::add_user_fonts(font_paths));
    }
    for (const auto& path : paths) {
      open_document_path(path);
    }
    // A dropped folder opens like File > Open Folder: its images as tabs.
    for (const auto& directory : directories) {
      open_folder_path(directory);
    }
  });
  return true;
}

void MainWindow::handle_web_file_drop(const QString& path) {
  // One MEMFS path per dropped file, delivered by the wasm page-side glue
  // (install_web_drop_target); mirrors open_dropped_files' guards.
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return;
  }
  if (user_fonts::is_user_font_path(path) || user_fonts::is_zip_path(path)) {
    show_user_font_drop_result(user_fonts::add_user_fonts({path}));
    return;
  }
  if (!is_supported_open_path(path)) {
    show_status_error(tr("Drop a supported image, Photoshop document, or font"));
    return;
  }
  open_document_path(path);
}

void MainWindow::show_user_font_drop_result(const user_fonts::AddFontsResult& result) {
  // One status line: failures are the actionable outcome, so they win over a
  // partial success (the new families still show up in the font picker).
  if (!result.invalid_names.isEmpty()) {
    show_status_error(tr("Not a valid font file: %1").arg(result.invalid_names.join(QStringLiteral(", "))));
    return;
  }
  if (!result.zips_without_fonts.isEmpty()) {
    show_status_error(tr("No fonts found in %1").arg(result.zips_without_fonts.join(QStringLiteral(", "))));
    return;
  }
  if (!result.added_families.isEmpty()) {
    statusBar()->showMessage(tr("Added fonts: %1").arg(result.added_families.join(QStringLiteral(", "))));
    return;
  }
  if (result.duplicate_count > 0) {
    statusBar()->showMessage(tr("Fonts already added"));
  }
}

void MainWindow::open_command_line_files(const QStringList& paths) {
  for (const auto& path : paths) {
    if (path.isEmpty()) {
      continue;
    }
    const QFileInfo info(path);
    // A directory argument opens its images as tabs (File > Open Folder).
    if (info.isDir()) {
      open_folder_path(info.absoluteFilePath());
      continue;
    }
    open_document_path(info.absoluteFilePath());
  }
}

void MainWindow::run_cli_export(const QString& output_path, const QString& append_text) {
  // Deferred like start_cli_stress_test: the export runs once the event loop is up, so the
  // opened document's canvas/session state has fully settled before any edit session starts.
  QTimer::singleShot(0, this, [this, output_path, append_text] {
    if (!has_active_document()) {
      fprintf(stderr, "Export failed: no document opened\n");
      exit_cli_application(2);
      return;
    }
    if (!append_text.isEmpty()) {
      const int mutated = cli_append_text_to_text_layers(append_text);
      fprintf(stderr, "Appended text to %d text layer(s)\n", mutated);
    }
    // --export writes flat images; a layered TIFF needs a script's saveAs (docs/tiff.md).
    std::optional<ImageSaveOptions> export_options;
    if (is_tiff_extension(extension_for_path(output_path))) {
      export_options = image_save_defaults_for_document();
      export_options->tiff_layers = false;
    }
    const bool saved = save_document_to_path(output_path, export_options, SaveToPathPolicy{/*flatten_confirmed=*/true, /*scripted=*/false, /*export_copy=*/false});
    if (!saved) {
      fprintf(stderr, "Export failed: could not save %s\n", output_path.toUtf8().constData());
    }
    // No document may prompt during shutdown.
    for (auto& export_session : sessions_) {
      if (export_session != nullptr) {
        set_session_saved(*export_session);
      }
    }
    exit_cli_application(saved ? 0 : 3);
  });
}

void MainWindow::activate_for_second_instance(const QStringList& paths) {
  // Pull the existing window in front so the user sees the file they just double-clicked open in
  // this instance rather than a new process.
  bring_to_front_for_second_instance();
  open_command_line_files(paths);
}

void MainWindow::bring_to_front_for_second_instance() {
  if (isMinimized()) {
    setWindowState(windowState() & ~Qt::WindowMinimized);
  }
  if (!isVisible()) {
    show();
  }
  raise();
  // A modal dialog owns the input (it disables this window), so it alone is activated: a second
  // activation request for this window can land after the dialog's and leave it behind.
  auto* modal = QApplication::activeModalWidget();
  if (modal == nullptr || modal == this) {
    activateWindow();
    return;
  }
  modal->raise();
  modal->activateWindow();
}

bool MainWindow::save_debug_screenshot(const QString& file_path, const QString& widget_name,
                                       const QRect& region) {
  // Window captures include the optional display renderer; exports deliberately
  // do not. Wait for its newest generation, with no user-input reentrancy.
  QPointer<CanvasWidget> preview_canvas(canvas_);
  if (preview_canvas && preview_canvas->vector_preview_enabled()) {
    preview_canvas->update();
    QElapsedTimer deadline;
    deadline.start();
    while (preview_canvas && !preview_canvas->render_settled()) {
      if (deadline.elapsed() >= 60000) {
        return false;
      }
      QEventLoop pause;
      QTimer::singleShot(8, &pause, &QEventLoop::quit);
      pause.exec(QEventLoop::ExcludeUserInputEvents);
    }
  }
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

// Affinity "Image" layers (placed image files, Affinity's smart-object analog)
// import as embedded smart objects carrying their full-resolution originals.
// Offer converting them to plain pixel layers instead: the remembered
// preference decides ("smart"/"pixel"), "ask" (the default) raises a
// one-question dialog. Unattended runs never get here (callers gate on
// unattended_automation() and keep the smart-object default.
void MainWindow::maybe_convert_af_image_layers(Document& target) {
  const auto placed = count_smart_object_layers(std::as_const(target).layers());
  if (placed == 0) {
    return;
  }
  auto settings = app_settings();
  const auto policy =
      settings.value(QStringLiteral("imports/afImageLayers"), QStringLiteral("ask")).toString();
  if (policy == QStringLiteral("smart")) {
    return;
  }
  bool convert = policy == QStringLiteral("pixel");
  if (!convert) {
    QMessageBox box(this);
    box.setObjectName(QStringLiteral("afImageLayersMessageBox"));
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(tr("Affinity Image Layers"));
    box.setText(tr("This document places %n image file(s) as Affinity \"Image\" layers.", nullptr,
                   static_cast<int>(placed)));
    box.setInformativeText(
        tr("Keep them as embedded smart objects? Each keeps its full-resolution original file for "
           "re-editing and PSD export. Converting to regular pixel layers keeps only the pixels at "
           "their placed size, which uses less memory but discards the originals."));
    auto* keep_button = box.addButton(tr("Keep as Smart Objects"), QMessageBox::AcceptRole);
    auto* convert_button = box.addButton(tr("Convert to Pixel Layers"), QMessageBox::DestructiveRole);
    box.setDefaultButton(keep_button);
    auto* remember = new QCheckBox(tr("Remember this choice"), &box);
    box.setCheckBox(remember);
    exec_dialog(box);
    convert = box.clickedButton() == convert_button;
    if (remember->isChecked()) {
      settings.setValue(QStringLiteral("imports/afImageLayers"),
                        convert ? QStringLiteral("pixel") : QStringLiteral("smart"));
    }
    if (!convert) {
      return;
    }
  }
  std::vector<std::string> source_uuids;
  strip_smart_object_layers(target, target.layers(), source_uuids);
  // The import authored one embedded source per placed image; with every
  // wrapper gone the file bytes have no owner, so drop them (unlike Rasterize,
  // which keeps orphans for Photoshop parity, nothing was ever saved here).
  for (const auto& uuid : source_uuids) {
    if (!uuid.empty()) {
      (void)target.metadata().smart_objects.remove(uuid);
    }
  }
  // remove() keeps the emptied link block; drop authored blocks with nothing
  // left so the document reports no smart objects at all (preserved opaque or
  // original-payload blocks from other sources stay).
  auto& blocks = target.metadata().smart_objects.blocks;
  blocks.erase(std::remove_if(blocks.begin(), blocks.end(),
                              [](const SmartObjectLinkBlock& block) {
                                return block.sources.empty() && !block.opaque &&
                                       block.original_payload == nullptr;
                              }),
               blocks.end());
}

void MainWindow::open_document_path(QString path) {
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return;
  }
#ifdef Q_OS_WASM
  const bool browser_transfer = wasm_files::is_temporary_transfer_path(path);
  const auto release_browser_transfer = qScopeGuard([&path, browser_transfer] {
    if (browser_transfer) {
      wasm_files::discard_temporary_transfer(path);
    }
  });
  wasm_files::publish_open_probe(QStringLiteral("opening"), path);
#else
  constexpr bool browser_transfer = false;
#endif
  try {
    const bool allow_raw_dialog = !script_engine_host_ || !script_engine_host_->run_active() ||
                                  script_engine_host_->manual_edit_pause();
    auto loaded = load_document_interactive(this, path, !unattended_automation(), allow_raw_dialog);
    if (!loaded.has_value()) {
#ifdef Q_OS_WASM
      wasm_files::publish_open_probe(QStringLiteral("cancelled"), path);
#endif
      return;
    }
    render_pending_svg_text_layers(loaded->document);
    render_pending_af_text_layers(loaded->document);
    render_pending_pdf_text_layers(loaded->document);
    render_pending_pdf_image_layers(loaded->document);
    if (loaded->pdf_merge != PdfPagesMerge::None) {
      merge_imported_pdf_pages(loaded->document, loaded->document_title, loaded->extra_documents, loaded->pdf_merge,
                               loaded->pdf_white_background);
    }
    if (loaded->pdf_bit_depth.has_value()) {
      finish_imported_pdf_document(loaded->document, loaded->pdf_color_mode, *loaded->pdf_bit_depth,
                                   loaded->import_notices);
    }
    lay_out_raster_less_imported_text(loaded->document);
    record_text_layout_metrics_for_reopened_text(loaded->document);
    if (!unattended_automation() && is_affinity_document_extension(loaded->extension)) {
      maybe_convert_af_image_layers(loaded->document);
    }

    // A browser pick/drop is an import, not a writable host path. The loaded
    // document owns all decoded data, so release its MEMFS source after this
    // function and keep the session pathless. Save will correctly use Save As
    // and download a new browser file; Reopen/Reveal will not point at a dead
    // sandbox path.
    const auto session_path = browser_transfer ? QString() : path;
    const auto loaded_file_name = loaded->file_name;
    // A PDF opened as separate documents titles every tab with its page, page 1
    // included, so the tabs read "file.pdf - Page 1", "file.pdf - Page 2", ...
    const auto first_title = loaded->extra_documents.empty() || loaded->document_title.isEmpty()
                                 ? loaded_file_name
                                 : tr("%1 - %2").arg(loaded_file_name, loaded->document_title);
    {
      const UiProfileScope profile_scope("open.add_session");
      add_document_session(std::move(loaded->document), first_title, session_path, tr("Open"));
    }
    session().content_extension = loaded->content_extension;
    session().import_notes = loaded->import_notices;
    start_open_watermark_check(session().session_id);
    bool cmyk_proof_opened = false;
    if (loaded->pdf_color_mode == PdfImportColorMode::Cmyk) {
      // The Import PDF dialog's CMYK Color: say so when there is no CMYK profile to proof with.
      if (const auto refusal = set_session_cmyk_proof(session(), true); !refusal.isEmpty()) {
        loaded->import_notices.push_back(refusal);
      }
    } else if (loaded->cmyk_source && open_cmyk_source_in_proof_mode(session())) {
      // A PSD that a save cannot write in CMYK (adjustment layers, say) gets the JPEG/TIFF wording.
      loaded->import_notices.push_back(
          cmyk_proof_psd_save_color_mode(std::as_const(document())).mode == 4 /* PSD CMYK */
              ? tr("Opened in Image > Mode > CMYK Proof, so saving as PSD, JPEG, TIFF or PDF writes CMYK again")
              : tr("Opened in Image > Mode > CMYK Proof, so saving as JPEG, TIFF or PDF writes CMYK again"));
      loaded->import_notices.push_back(
          tr("CMYK Proof is an emulation: Patchy edits in RGB and converts to CMYK through the color profile, so "
             "colors and ink numbers can differ slightly from a true CMYK editor"));
      cmyk_proof_opened = true;
    }
    if (!unattended_automation() && is_photoshop_document_extension(loaded->extension) &&
        app_settings().value(QStringLiteral("imports/showPsdWarningsAndInfo"), false).toBool()) {
      show_compatibility_report(this, document(), loaded_file_name);
    }
    {
      const UiProfileScope profile_scope("open.fit_to_view");
      canvas_->fit_to_view_without_enlarging();
    }
    open_extra_imported_page_sessions(loaded_file_name, std::move(loaded->extra_documents), loaded->pdf_color_mode,
                                      loaded->pdf_bit_depth.value_or(BitDepth::UInt8), loaded->import_notices);
    if (!unattended_automation()) {
      // Unattended runs must not block on the adoption offer.
      maybe_offer_indexed_palette_adoption();
    }
    update_undo_redo_actions();
    if (!browser_transfer) {
      add_recent_file(path);
      if (!unattended_automation()) remember_open_directory_for_path(path);
    }
    if (loaded->import_notices.isEmpty()) {
      statusBar()->showMessage(tr("Opened %1").arg(browser_transfer ? loaded_file_name : path));
    } else {
      // Import notes ride the status bar by default; the consolidated popup is opt-in
      // (see show_import_notices_popup) unless the notes describe data loss.
      auto status_notes = loaded->import_notices.front();
      if (loaded->import_notices.size() > 1) {
        status_notes +=
            tr(" (+%n more import note(s))", nullptr, static_cast<int>(loaded->import_notices.size()) - 1);
      }
      statusBar()->showMessage(tr("Opened %1. %2").arg(loaded_file_name, status_notes));
      if (!unattended_automation()) {
        show_import_notices_popup(this, loaded_file_name, loaded->import_notices,
                                  loaded->force_import_notices_popup);
      }
    }
    if (cmyk_proof_opened) {
      show_cmyk_proof_open_notice();
    }
    // Linked smart objects whose files are missing (GitHub issue 257; off in Preferences).
    show_missing_linked_files_dialog(true);
    // Photoshop's Missing Fonts dialog on open (GitHub issue 169; off in Preferences). Last, so
    // its own status message is not overwritten by the "Opened" one.
    show_resolve_missing_fonts_dialog(true);
#ifdef Q_OS_WASM
    wasm_files::publish_open_probe(QStringLiteral("opened"), path);
#endif
  } catch (const std::exception& error) {
#ifdef Q_OS_WASM
    wasm_files::publish_open_probe(QStringLiteral("failed"), path, translated_file_message(error.what()));
#endif
    if (unattended_automation()) {
      fprintf(stderr, "Open failed: %s (%s)\n", error.what(), path.toUtf8().constData());
    } else {
      show_open_failed_message_box(this, translated_file_message(error.what()), QDir::toNativeSeparators(path));
    }
  }
}

// The pages after the first of a PDF opened as separate documents. Each becomes a
// pathless session (the PDF is a read-only source, so page 1 saves through Save As
// too), added in the background so page 1 stays the active document throughout.
void MainWindow::finish_imported_pdf_document(Document& document, PdfImportColorMode mode, BitDepth depth,
                                              QStringList& notices) {
  const auto note = [&notices](const QString& text) {
    if (!notices.contains(text)) {
      notices.push_back(text);
    }
  };
  if (mode == PdfImportColorMode::Palette) {
    // Indexed editing is 8-bit, so the depth choice does not apply.
    if (!convert_imported_document_to_palette(document)) {
      note(tr("The pages stayed RGB: Indexed color needs flattened 8-bit pages."));
    }
    return;
  }
  if (depth != std::as_const(document).color_state().bit_depth && depth_supported_on_platform(depth)) {
    convert_document_depth(document, depth);
  }
}

void MainWindow::merge_imported_pdf_pages(Document& first, const QString& first_title,
                                          std::vector<PdfImportedDocument>& extras, PdfPagesMerge merge,
                                          bool white_background) {
  struct Page {
    Document* document;
    QString title;
  };
  std::vector<Page> pages{{&first, first_title.isEmpty() ? tr("Page %1").arg(1) : first_title}};
  for (auto& extra : extras) {
    render_pending_pdf_text_layers(extra.document);
    render_pending_pdf_image_layers(extra.document);
    pages.push_back({&extra.document, extra.title});
  }
  // Layers: one canvas the size of the largest page, every page at its top-left. Artboards:
  // a grid about as wide as tall (a long PDF stays a reasonable canvas), page order left
  // to right, top to bottom, kArtboardSpacing apart, each cell the size of the largest page.
  std::int32_t cell_width = 1;
  std::int32_t cell_height = 1;
  for (const auto& page : pages) {
    cell_width = std::max(cell_width, page.document->width());
    cell_height = std::max(cell_height, page.document->height());
  }
  const auto count = static_cast<int>(pages.size());
  const int columns = merge == PdfPagesMerge::Artboards
                          ? std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(count)))))
                          : 1;
  const int rows = merge == PdfPagesMerge::Artboards ? (count + columns - 1) / columns : 1;
  const auto origin_of = [&](int index) {
    if (merge != PdfPagesMerge::Artboards) {
      return QPoint(0, 0);
    }
    return QPoint((index % columns) * (cell_width + kArtboardSpacing), (index / columns) * (cell_height + kArtboardSpacing));
  };
  const auto canvas_width = merge == PdfPagesMerge::Artboards
                                ? columns * cell_width + (columns - 1) * kArtboardSpacing
                                : cell_width;
  const auto canvas_height =
      merge == PdfPagesMerge::Artboards ? rows * cell_height + (rows - 1) * kArtboardSpacing : cell_height;
  Document merged(canvas_width, canvas_height, std::as_const(first).format());
  merged.print_settings() = std::as_const(first).print_settings();
  merged.color_state() = std::as_const(first).color_state();
  merged.file_info() = std::as_const(first).file_info();
  if (merge == PdfPagesMerge::Artboards) {
    merged.metadata().artboard_settings.emplace();
  }

  std::vector<std::pair<LayerId, std::shared_ptr<const PdfSourcePage>>> page_sources;
  // Copied bottom-up so page 1 ends on top: the topmost folder (only it visible) or the
  // topmost, top-left artboard, which is also the order every per-page export writes.
  for (int step = 0; step < count; ++step) {
    const int index = count - 1 - step;
    auto& page = *pages[static_cast<std::size_t>(index)].document;
    const auto& title = pages[static_cast<std::size_t>(index)].title;
    auto wrapper = merge == PdfPagesMerge::Artboards
                       ? make_artboard_group(page.allocate_layer_id(), title.toStdString(),
                                             Rect::from_size(page.width(), page.height()),
                                             white_background ? ArtboardBackground::White
                                                              : ArtboardBackground::Transparent)
                       : Layer(page.allocate_layer_id(), title.toStdString(), LayerKind::Group);
    if (merge == PdfPagesMerge::Layers) {
      // Every page is a folder, a scan included, and remembers its own size: exporting by
      // groups gives each page back at that size (docs/pdf.md).
      wrapper.set_blend_mode(BlendMode::PassThrough);
      wrapper.set_visible(index == 0);
      const auto origin = origin_of(index);
      wrapper.metadata()[kLayerMetadataPdfPageRect] = QStringLiteral("%1,%2,%3,%4")
                                                          .arg(origin.x())
                                                          .arg(origin.y())
                                                          .arg(page.width())
                                                          .arg(page.height())
                                                          .toStdString();
    }
    for (auto& layer : page.layers()) {
      wrapper.add_child(std::move(layer));
    }
    page.layers().clear();
    const auto root_id = wrapper.id();
    page.layers().push_back(std::move(wrapper));
    CrossDocumentLayerPlacement placement;
    placement.exact_offset = origin_of(index);
    placement.keep_names = true;
    QString error;
    const auto copied =
        copy_layers_between_documents(std::as_const(page), {root_id}, merged, placement, [] { return true; }, &error);
    if (!copied.empty()) {
      if (const auto source = std::as_const(page).metadata().pdf_source_page; source != nullptr) {
        page_sources.emplace_back(copied.front(), source);
      }
    }
  }
  first = std::move(merged);
  extras.clear();
  // Each untouched page re-exports its original image (docs/pdf.md "Pass-through"):
  // fingerprint the page exactly as the per-page export derives it.
  for (const auto& [page_layer_id, source] : page_sources) {
    attach_pdf_page_source(first, page_layer_id, source);
  }
  if (const auto default_layer = default_non_group_layer_id(std::as_const(first).layers()); default_layer.has_value()) {
    first.set_active_layer(*default_layer);
  }
}

void MainWindow::open_extra_imported_page_sessions(const QString& file_name,
                                                  std::vector<PdfImportedDocument> pages, PdfImportColorMode mode,
                                                  BitDepth depth, QStringList& notices) {
  if (pages.empty()) {
    return;
  }
  auto* first_session = active_session();
  const auto first_session_id = first_session != nullptr ? first_session->session_id : 0;
  // Text and image passes and the session setup all run here on the UI thread; a
  // progress dialog plus an event pump per page keeps the window painting and lets
  // each new tab show up in the tab bar as it is made instead of all at once at the end.
  const int page_count = static_cast<int>(pages.size()) + 1;
  QProgressDialog progress(tr("Opening page %1 of %2...").arg(2).arg(page_count), QString(), 0, page_count, this);
  progress.setObjectName(QStringLiteral("pdfPagesProgressDialog"));
  progress.setWindowTitle(tr("Opening %1").arg(file_name));
  progress.setWindowModality(Qt::WindowModal);
  progress.setMinimumDuration(0);
  progress.setCancelButton(nullptr);
  progress.setAutoClose(false);
  progress.setAutoReset(false);
  remember_dialog_position(progress);
  progress.setValue(1);
  int position = 1;
  for (auto& page : pages) {
    ++position;
    progress.setLabelText(tr("Opening page %1 of %2...").arg(position).arg(page_count));
    progress.setValue(position - 1);
    QApplication::processEvents(QEventLoop::AllEvents);
    const std::string profile_detail = "page=" + std::to_string(position);
    {
      const UiProfileScope profile_scope("pdf_pages.pending_layers", profile_detail);
      render_pending_pdf_text_layers(page.document);
      render_pending_pdf_image_layers(page.document);
      finish_imported_pdf_document(page.document, mode, depth, notices);
    }
    {
      // In the background: page 1 stays the active document, and each of these fits
      // its view when its tab is first selected. Activating every page (a layer-panel
      // rebuild, a full composite, and a paint each) only to come back to page 1 was
      // about a tenth of a second per page.
      const UiProfileScope profile_scope("pdf_pages.add_session", profile_detail);
      add_document_session(std::move(page.document), tr("%1 - %2").arg(file_name, page.title), QString(),
                           tr("Open"), SessionActivation::Background);
    }
    // CMYK is the session's CMYK Proof; a refusal (no CMYK profile) is said once.
    if (mode == PdfImportColorMode::Cmyk && !sessions_.empty()) {
      if (const auto refusal = set_session_cmyk_proof(**sessions_.rbegin(), true);
          !refusal.isEmpty() && !notices.contains(refusal)) {
        notices.push_back(refusal);
      }
    }
    const UiProfileScope profile_scope("pdf_pages.pump", profile_detail);
    QApplication::processEvents(QEventLoop::AllEvents);
  }
  progress.setValue(page_count);
  // Nothing above moved the active document, but an event pumped along the way could
  // have: land on page 1 either way.
  if (auto* first = session_with_id(first_session_id); first != nullptr && first != active_session()) {
    activate_document_session(*first);
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
    const bool allow_raw_dialog = !script_engine_host_ || !script_engine_host_->run_active() ||
                                  script_engine_host_->manual_edit_pause();
    auto loaded = load_document_interactive(this, path, !unattended_automation(), allow_raw_dialog);
    if (!loaded.has_value()) {
      return;
    }
    // Reopen replaces this one tab: a PDF re-imported as separate documents brings back
    // only its first page here, never a second set of page tabs.
    if (loaded->pdf_merge == PdfPagesMerge::None) {
      loaded->extra_documents.clear();
    }
    render_pending_svg_text_layers(loaded->document);
    render_pending_af_text_layers(loaded->document);
    render_pending_pdf_text_layers(loaded->document);
    render_pending_pdf_image_layers(loaded->document);
    if (loaded->pdf_merge != PdfPagesMerge::None) {
      merge_imported_pdf_pages(loaded->document, loaded->document_title, loaded->extra_documents, loaded->pdf_merge,
                               loaded->pdf_white_background);
    }
    if (loaded->pdf_bit_depth.has_value()) {
      finish_imported_pdf_document(loaded->document, loaded->pdf_color_mode, *loaded->pdf_bit_depth,
                                   loaded->import_notices);
    }
    lay_out_raster_less_imported_text(loaded->document);
    record_text_layout_metrics_for_reopened_text(loaded->document);
    if (!unattended_automation() &&
        is_affinity_document_extension(QFileInfo(path).suffix().toLower())) {
      maybe_convert_af_image_layers(loaded->document);
    }

    // Replace the document in place: tab position, float window, and session
    // identity survive (smart-object child tabs reference session ids). An open
    // inline text edit still targets the outgoing document, so settle it first.
    finish_active_text_editor();
    layer_thumbnail_cache_.clear();
    channel_thumbnail_cache_.clear();
    target_session.document = std::move(loaded->document);
    target_session.content_extension = loaded->content_extension;
    target_session.import_notes = loaded->import_notices;
    initialize_session_history(target_session, tr("Reopen"));
    target_session.collapsed_layer_groups.clear();
    collect_initially_collapsed_layer_groups(target_session.document.layers(),
                                             target_session.collapsed_layer_groups);
    target_session.expanded_layer_effects.clear();
    target_session.collapsed_smart_filters.clear();
    collect_initial_layer_fx_disclosure(target_session.document.layers(), target_session.expanded_layer_effects,
                                        target_session.collapsed_smart_filters);
    ++target_session.revision;
    target_session.saved_revision = target_session.revision;
    if (loaded->cmyk_source) {
      open_cmyk_source_in_proof_mode(target_session);
    }
    canvas_->set_document(&target_session.document);
    canvas_->fit_to_view_without_enlarging();
    refresh_layer_list();
    refresh_layer_controls();
    refresh_channel_panel();
    refresh_palette_panel();
    schedule_palette_compliance_check();
    maybe_offer_indexed_palette_adoption();
    update_undo_redo_actions();
    update_document_action_state();
    refresh_document_tab_titles();
    open_extra_imported_page_sessions(loaded->file_name, std::move(loaded->extra_documents), loaded->pdf_color_mode,
                                      loaded->pdf_bit_depth.value_or(BitDepth::UInt8), loaded->import_notices);
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
    show_open_failed_message_box(this, translated_file_message(error.what()), QDir::toNativeSeparators(path));
  }
}

namespace {

// Two spellings of one stored path. Windows and macOS volumes compare names without
// case, so a session or recent entry saved as "photo.psd" is the file a case-only
// rename moved.
bool same_stored_path(const QString& left, const QString& right) {
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
  constexpr auto sensitivity = Qt::CaseInsensitive;
#else
  constexpr auto sensitivity = Qt::CaseSensitive;
#endif
  return !left.isEmpty() && !right.isEmpty() &&
         QFileInfo(left).absoluteFilePath().compare(QFileInfo(right).absoluteFilePath(), sensitivity) == 0;
}

}  // namespace

void MainWindow::rename_document_file(std::int64_t session_id) {
  const auto* target_session = session_with_id(session_id);
  if (target_session == nullptr || target_session->path.isEmpty()) {
    return;
  }
  const auto old_path = QFileInfo(target_session->path).absoluteFilePath();
  if (!QFileInfo::exists(old_path)) {
    show_status_error(tr("File is missing"));
    return;
  }
  QWidget* dialog_parent = target_session->float_window != nullptr ? static_cast<QWidget*>(target_session->float_window)
                                                                    : static_cast<QWidget*>(this);
  (void)request_file_rename(dialog_parent, old_path, [this, &old_path](const QString& new_path) -> QString {
    const auto new_name = QFileInfo(new_path).fileName();
    const auto result = rename_file_without_replacing(old_path, new_path);
    switch (result.status) {
      case FileRenameStatus::Renamed:
        adopt_renamed_document_path(old_path, new_path);
        statusBar()->showMessage(tr("Renamed to %1").arg(new_name));
        return {};
      case FileRenameStatus::TargetExists:
        return tr("A file named %1 already exists in this folder.").arg(new_name);
      case FileRenameStatus::SourceMissing:
        return tr("%1 is no longer on disk.").arg(QFileInfo(old_path).fileName());
      case FileRenameStatus::Failed:
        break;
    }
    return tr("Could not rename the file: %1").arg(result.detail);
  });
}

void MainWindow::adopt_renamed_document_path(const QString& old_path, const QString& new_path) {
  const auto old_name = QFileInfo(old_path).fileName();
  const auto new_name = QFileInfo(new_path).fileName();
  for (const auto& target_session : sessions_) {
    if (target_session == nullptr || !same_stored_path(target_session->path, old_path)) {
      continue;
    }
    // Only where the file lives changes: no save mark, so unsaved edits stay unsaved
    // and the next Save writes them to the new name.
    target_session->path = new_path;
    // A file opened by its content (GitHub issue 362) renamed to fit it saves in place again.
    if (formats::extensions_share_format(target_session->content_extension.toStdString(),
                                         QFileInfo(new_path).suffix().toStdString())) {
      target_session->content_extension.clear();
    }
    if (target_session->title == old_name) {
      target_session->title = new_name;
    } else if (target_session->title.startsWith(old_name + QLatin1Char(' '))) {
      // "photo.psd (Recovered)" and the like keep their suffix.
      target_session->title = new_name + target_session->title.mid(old_name.size());
    }
    if (same_stored_path(target_session->image_save_options_path, old_path)) {
      target_session->image_save_options_path = new_path;
    }
#ifndef Q_OS_WASM
    if (recovery_coordinator_ != nullptr) {
      recovery_coordinator_->refresh_sidecar(target_session->session_id);
    }
#endif
  }
  replace_recent_file(old_path, new_path);
  refresh_document_tab_titles();
}

namespace {

// The device-reported scan DPI, when the backend supplied one, overrides the file's own
// density: drivers routinely write 72 or nothing into scanned JPEGs, which would print
// a 300 DPI scan at four times its real size. Absurd values (from either source) are
// clamped so physical sizes stay sane (Image Size for imports, the actual-size
// placement for photocopies).
void apply_scanned_document_ppi(Document& document, const ScannerAcquireResult& result) {
  auto& print_settings = document.print_settings();
  if (result.horizontal_dpi >= 10 && result.horizontal_dpi <= 4800) {
    print_settings.horizontal_ppi = result.horizontal_dpi;
  }
  if (result.vertical_dpi >= 10 && result.vertical_dpi <= 4800) {
    print_settings.vertical_ppi = result.vertical_dpi;
  }
  if (print_settings.horizontal_ppi < 10 || print_settings.horizontal_ppi > 4800) {
    print_settings.horizontal_ppi = 300;
  }
  if (print_settings.vertical_ppi < 10 || print_settings.vertical_ppi > 4800) {
    print_settings.vertical_ppi = 300;
  }
}

}  // namespace

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
    apply_scanned_document_ppi(loaded.document, result);
    // Untitled + modified: the scan exists nowhere else, so Save must prompt Save As and
    // closing must warn about unsaved changes.
    add_document_session(std::move(loaded.document), tr("Scanned Image"), QString(),
                         tr("Import from scanner"));
    canvas_->fit_to_view_without_enlarging();
    refresh_layer_list();
    refresh_layer_controls();
    update_undo_redo_actions();
    mark_session_modified(session());
    statusBar()->showMessage(tr("Imported image from scanner"));
  } catch (const std::exception& error) {
    show_critical_message(this, tr("Import failed"), translated_file_message(error.what()),
                          QStringLiteral("openFailedMessageBox"));
  }
}

void MainWindow::photocopy_from_scanner() {
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return;
  }
  // Same PATCHY_FAKE_SCANNER_FILE bypass as import_from_scanner, so offscreen tests can
  // drive the photocopy dialog without hardware.
  const auto fake_path = qEnvironmentVariable("PATCHY_FAKE_SCANNER_FILE");
  if (!fake_path.isEmpty()) {
    finish_photocopy_scan({ScannerAcquireStatus::Acquired, fake_path, {}}, false);
    return;
  }

#ifdef Q_OS_WIN
  if (scanner_import_active_) {
    return;
  }
  scanner_import_active_ = true;
  const auto reentry_guard = qScopeGuard([this] { scanner_import_active_ = false; });
  finish_photocopy_scan(acquire_image_from_scanner(this), true);
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
    window->finish_photocopy_scan(std::move(result), true);
  });
#endif
}

void MainWindow::finish_photocopy_scan(ScannerAcquireResult result, bool delete_after) {
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
          this, tr("Photocopy"),
          tr("No scanner or camera was found. Connect a WIA-compatible device and try again."),
          QStringLiteral("scannerNoDeviceMessageBox"));
#else
      show_information_message(
          this, tr("Photocopy"),
          tr("No scanner was found. Connect a scanner recognized by macOS and try again."),
          QStringLiteral("scannerNoDeviceMessageBox"));
#endif
      return;
    case ScannerAcquireStatus::Failed:
      show_critical_message(this, tr("Photocopy"), result.error,
                            QStringLiteral("scannerFailedMessageBox"));
      return;
    case ScannerAcquireStatus::Acquired:
      break;
  }

  // The scan is loaded only to print it, never added as a document session: a
  // photocopy leaves nothing behind, like the machine it is named after. The try
  // covers only the load; the dialog handles its own printing errors, and wrapping
  // it would swallow test-harness unwinds into a modal error box.
  std::optional<Document> scanned;
  try {
    auto loaded = load_document_from_path(result.file_path);
    apply_scanned_document_ppi(loaded.document, result);
    scanned.emplace(std::move(loaded.document));
  } catch (const std::exception& error) {
    show_critical_message(this, tr("Photocopy"), translated_file_message(error.what()),
                          QStringLiteral("openFailedMessageBox"));
    return;
  }
  if (run_photocopy_dialog(this, *scanned)) {
    statusBar()->showMessage(tr("Photocopy sent to printer"));
  }
}

void MainWindow::import_and_divide_from_scanner() {
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return;
  }
  // Same PATCHY_FAKE_SCANNER_FILE bypass as import_from_scanner, so offscreen tests can
  // drive the divide dialog without hardware.
  const auto fake_path = qEnvironmentVariable("PATCHY_FAKE_SCANNER_FILE");
  if (!fake_path.isEmpty()) {
    while (finish_divide_scanner_import({ScannerAcquireStatus::Acquired, fake_path, {}}, false)) {
    }
    return;
  }

#ifdef Q_OS_WIN
  if (scanner_import_active_) {
    return;
  }
  scanner_import_active_ = true;
  const auto reentry_guard = qScopeGuard([this] { scanner_import_active_ = false; });
  // "Scan another batch?" loops straight back into acquisition, saving the
  // menu round-trip when a stack of photos is being fed through.
  while (finish_divide_scanner_import(acquire_image_from_scanner(this), true)) {
  }
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
    // "Scan another batch?" re-enters acquisition (the async flow cannot loop
    // in place; scanner_import_active_ is already cleared above).
    if (window->finish_divide_scanner_import(std::move(result), true)) {
      window->import_and_divide_from_scanner();
    }
  });
#endif
}

bool MainWindow::finish_divide_scanner_import(ScannerAcquireResult result, bool delete_after) {
  const auto remove_temporary_scan = qScopeGuard([&result, delete_after] {
    if (delete_after && !result.file_path.isEmpty()) {
      QFile::remove(result.file_path);
    }
  });
  switch (result.status) {
    case ScannerAcquireStatus::Cancelled:
      return false;
    case ScannerAcquireStatus::NoDevice:
#ifdef Q_OS_WIN
      show_information_message(
          this, tr("Divide Scanned Photos"),
          tr("No scanner or camera was found. Connect a WIA-compatible device and try again."),
          QStringLiteral("scannerNoDeviceMessageBox"));
#else
      show_information_message(
          this, tr("Divide Scanned Photos"),
          tr("No scanner was found. Connect a scanner recognized by macOS and try again."),
          QStringLiteral("scannerNoDeviceMessageBox"));
#endif
      return false;
    case ScannerAcquireStatus::Failed:
      show_critical_message(this, tr("Divide Scanned Photos"), result.error,
                            QStringLiteral("scannerFailedMessageBox"));
      return false;
    case ScannerAcquireStatus::Acquired:
      break;
  }

  // Like the photocopy, the scan itself never becomes a session; only the
  // divided photos do. The try covers only the load.
  std::optional<Document> scanned;
  try {
    auto loaded = load_document_from_path(result.file_path);
    apply_scanned_document_ppi(loaded.document, result);
    scanned.emplace(std::move(loaded.document));
  } catch (const std::exception& error) {
    show_critical_message(this, tr("Divide Scanned Photos"), translated_file_message(error.what()),
                          QStringLiteral("openFailedMessageBox"));
    return false;
  }
  const auto image = qimage_from_document_rect(
      *scanned, QRect(0, 0, scanned->width(), scanned->height()), /*preserve_alpha*/ true);
  auto pixels = std::make_shared<const PixelBuffer>(pixels_from_image_rgba(image));
  if (!run_divide_photos_flow(std::move(pixels), scanned->print_settings())) {
    return false;
  }
  // Only after a completed batch: someone dividing a whole shoebox gets the
  // next scan one keypress away instead of a menu round-trip.
  const auto answer = show_warning_message(
      this, tr("Divide Scanned Photos"), tr("Scan another batch of photos?"),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes,
      QStringLiteral("dividePhotosScanAgainMessageBox"));
  return answer == QMessageBox::Yes;
}

void MainWindow::divide_current_document_photos() {
  if (!has_active_document()) {
    show_status_error(tr("No document"));
    return;
  }
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return;
  }
  finish_active_text_editor();
  // The flattened composite, alpha preserved: detection composites alpha over
  // white itself, and extraction keeps transparency. The source document is
  // never modified (no undo entry).
  const auto image = qimage_from_document_rect(
      document(), QRect(0, 0, document().width(), document().height()), /*preserve_alpha*/ true);
  auto pixels = std::make_shared<const PixelBuffer>(pixels_from_image_rgba(image));
  run_divide_photos_flow(std::move(pixels), document().print_settings());
}

bool MainWindow::run_divide_photos_flow(std::shared_ptr<const PixelBuffer> source,
                                        DocumentPrintSettings print_settings) {
  if (source == nullptr || source->empty()) {
    return false;
  }
  DividePhotosSettings dialog_settings;
  {
    auto settings = app_settings();
    dialog_settings.sensitivity =
        std::clamp(settings.value(QStringLiteral("dividePhotos/sensitivity"), 50).toInt(), 0, 100);
    const bool straighten = settings.value(QStringLiteral("dividePhotos/straighten"), true).toBool();
    const bool perspective =
        settings.value(QStringLiteral("dividePhotos/fixPerspective"), false).toBool();
    dialog_settings.mode = perspective ? PhotoExtractMode::Perspective
                           : straighten ? PhotoExtractMode::Straighten
                                        : PhotoExtractMode::Cut;
    dialog_settings.up_direction = static_cast<PhotoUpDirection>(
        std::clamp(settings.value(QStringLiteral("dividePhotos/upDirection"), 0).toInt(), 0, 3));
    dialog_settings.output = static_cast<DividePhotosOutput>(
        std::clamp(settings.value(QStringLiteral("dividePhotos/output"), 0).toInt(), 0, 2));
    dialog_settings.folder = settings.value(QStringLiteral("dividePhotos/folder")).toString();
    dialog_settings.prefix =
        settings.value(QStringLiteral("dividePhotos/prefix"), QStringLiteral("photo_")).toString();
    dialog_settings.format =
        settings.value(QStringLiteral("dividePhotos/format"), QStringLiteral("png")).toString();
    dialog_settings.existing_files = static_cast<DividePhotosExistingFiles>(
        std::clamp(settings.value(QStringLiteral("dividePhotos/existingFiles"), 0).toInt(), 0, 1));
  }
  if (dialog_settings.folder.isEmpty()) {
    dialog_settings.folder = last_save_directory();
  }
  const auto result = request_divide_photos(this, source, print_settings.horizontal_ppi,
                                            dialog_settings, divide_photos_format_choices());
  if (!result.has_value()) {
    return false;
  }
  const DividePhotosSettings& chosen = result->settings;
  {
    auto settings = app_settings();
    settings.setValue(QStringLiteral("dividePhotos/sensitivity"), chosen.sensitivity);
    settings.setValue(QStringLiteral("dividePhotos/straighten"),
                      chosen.mode != PhotoExtractMode::Cut);
    settings.setValue(QStringLiteral("dividePhotos/fixPerspective"),
                      chosen.mode == PhotoExtractMode::Perspective);
    settings.setValue(QStringLiteral("dividePhotos/upDirection"),
                      static_cast<int>(chosen.up_direction));
    settings.setValue(QStringLiteral("dividePhotos/output"), static_cast<int>(chosen.output));
    settings.setValue(QStringLiteral("dividePhotos/folder"), chosen.folder);
    settings.setValue(QStringLiteral("dividePhotos/prefix"), chosen.prefix);
    settings.setValue(QStringLiteral("dividePhotos/format"), chosen.format);
    settings.setValue(QStringLiteral("dividePhotos/existingFiles"),
                      static_cast<int>(chosen.existing_files));
  }
  if (result->regions.empty()) {
    show_status_error(tr("No photos were found"));
    return false;
  }

  QProgressDialog progress(tr("Dividing photos..."), tr("Cancel"), 0, 100, this);
  progress.setObjectName(QStringLiteral("dividePhotosProgressDialog"));
  progress.setWindowModality(Qt::WindowModal);
  progress.setMinimumDuration(kFilterProgressMinimumDurationMs);
  remember_dialog_position(progress);
  progress.setValue(0);
  std::vector<PixelBuffer> photos;
  // Captured by value: the compute may run on a worker under wasm and must not
  // read UI state (main_window_shared.hpp's rule).
  const auto regions = result->regions;
  const auto mode = chosen.mode;
  const int rotate_turns = up_direction_cw_turns(chosen.up_direction);
  try {
    run_filter_compute_with_progress(
        progress, [](const QString& detail) { return tr("Dividing photos...\n%1").arg(detail); },
        {}, [&photos, regions, mode, rotate_turns, source](FilterProgress& filter_progress) {
          for (std::size_t i = 0; i < regions.size(); ++i) {
            if (filter_progress.update &&
                !filter_progress.update(static_cast<int>(i), static_cast<int>(regions.size()),
                                        FilterProgressStage::Filtering)) {
              throw FilterCancelled();
            }
            auto photo = extract_photo_region(*source, regions[i], mode);
            if (!photo.empty() && rotate_turns != 0) {
              photo = rotated_quarter_turns(photo, rotate_turns);
            }
            if (!photo.empty()) {
              photos.push_back(std::move(photo));
            }
          }
        });
    progress.setValue(100);
  } catch (const FilterCancelled&) {
    statusBar()->showMessage(tr("Cancelled Divide Scanned Photos"));
    return false;
  }
  if (photos.empty()) {
    show_status_error(tr("No photos were found"));
    return false;
  }

  if (chosen.output != DividePhotosOutput::OpenDocuments) {
    const auto saved = save_divided_photos_to_folder(photos, print_settings, chosen.folder,
                                                     chosen.prefix, chosen.format,
                                                     chosen.existing_files);
    if (!saved.has_value()) {
      return false;
    }
    const int count = static_cast<int>(saved->size());
    const QString native_folder = QDir::toNativeSeparators(chosen.folder);
    if (chosen.output == DividePhotosOutput::SaveAndOpen) {
      for (std::size_t i = 0; i < photos.size(); ++i) {
        Document photo_document(photos[i].width(), photos[i].height(), photos[i].format());
        photo_document.print_settings() = print_settings;
        photo_document.add_pixel_layer("Background", std::move(photos[i]));
        const QString path = saved->at(static_cast<qsizetype>(i));
        // Deliberately not marked modified: the session mirrors the file just
        // written, so closing never prompts to save and the tab carries the
        // real file name.
        add_document_session(std::move(photo_document), QFileInfo(path).fileName(), path,
                             tr("Divide scanned photos"));
        canvas_->fit_to_view_without_enlarging();
      }
      refresh_layer_list();
      refresh_layer_controls();
      update_undo_redo_actions();
      statusBar()->showMessage(
          tr("Saved %n photo(s) to %1 and opened them", nullptr, count).arg(native_folder));
    } else {
      // A status-bar line alone is easy to miss after a batch export; confirm
      // visibly (there is no toast surface).
      statusBar()->showMessage(tr("Saved %n photo(s) to %1", nullptr, count).arg(native_folder));
      show_information_message(this, tr("Divide Scanned Photos"),
                               tr("Saved %n photo(s) to %1", nullptr, count).arg(native_folder),
                               QStringLiteral("dividePhotosSavedMessageBox"));
    }
    return true;
  }

  int photo_number = 1;
  for (auto& photo : photos) {
    Document photo_document(photo.width(), photo.height(), photo.format());
    photo_document.print_settings() = print_settings;
    photo_document.add_pixel_layer("Background", std::move(photo));
    // Untitled + modified, like scanner import: each photo exists nowhere else yet.
    add_document_session(std::move(photo_document), tr("Photo %1").arg(photo_number), QString(),
                         tr("Divide scanned photos"));
    canvas_->fit_to_view_without_enlarging();
    mark_session_modified(session());
    ++photo_number;
  }
  refresh_layer_list();
  refresh_layer_controls();
  update_undo_redo_actions();
  statusBar()->showMessage(tr("Divided into %n photo(s)", nullptr, static_cast<int>(photos.size())));
  return true;
}

std::optional<QStringList> MainWindow::save_divided_photos_to_folder(
    const std::vector<PixelBuffer>& photos, const DocumentPrintSettings& print_settings,
    const QString& folder, const QString& prefix, const QString& extension,
    DividePhotosExistingFiles existing_files) {
  if (photos.empty() || folder.trimmed().isEmpty()) {
    return std::nullopt;
  }
  try {
    // The dialog already created the folder; direct callers (tests) may not have.
    QDir().mkpath(folder);
    const QDir directory(folder);
    QStringList targets;
    targets.reserve(static_cast<qsizetype>(photos.size()));
    if (existing_files == DividePhotosExistingFiles::AddNumbering) {
      // Never overwrite: each file takes the next free index, so a batch lands
      // after whatever the folder already holds (001-003 there -> 004, 005...).
      int index = 1;
      for (std::size_t i = 0; i < photos.size(); ++i) {
        while (
            QFileInfo::exists(directory.filePath(divide_photo_file_name(prefix, index, extension)))) {
          ++index;
        }
        targets.push_back(directory.filePath(divide_photo_file_name(prefix, index, extension)));
        ++index;
      }
    } else {
      QString first_conflict;
      for (std::size_t i = 0; i < photos.size(); ++i) {
        const auto name = divide_photo_file_name(prefix, static_cast<int>(i) + 1, extension);
        const auto target = directory.filePath(name);
        if (first_conflict.isEmpty() && QFileInfo::exists(target)) {
          first_conflict = name;
        }
        targets.push_back(target);
      }
      // One confirmation for the whole batch, before anything is written; the
      // first colliding name stands in for the rest.
      if (!first_conflict.isEmpty()) {
        const auto answer = show_warning_message(
            this, tr("Divide Scanned Photos"),
            tr("%1 already exists. Overwrite existing photos?").arg(first_conflict),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No,
            QStringLiteral("dividePhotosOverwriteMessageBox"));
        if (answer != QMessageBox::Yes) {
          return std::nullopt;
        }
      }
    }

    // Per-format remembered defaults, applied silently; the export scale is
    // pinned to 1x so the shared saveOptions/exportScale can never rescale a
    // photo batch.
    auto image_options = load_image_save_option_defaults();
    image_options.export_scale = 1;

    QProgressDialog progress(tr("Saving photos..."), tr("Cancel"), 0, 100, this);
    progress.setObjectName(QStringLiteral("dividePhotosSaveProgressDialog"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(kFilterProgressMinimumDurationMs);
    remember_dialog_position(progress);
    progress.setValue(0);
    // Captured by value where UI state would otherwise be read (the wasm
    // worker rule); the save modes are desktop-only but the rule holds.
    const std::string background_name = tr("Background").toStdString();
    run_filter_compute_with_progress(
        progress, [](const QString& detail) { return tr("Saving photos...\n%1").arg(detail); }, {},
        [&photos, &targets, &extension, &image_options, &print_settings,
         &background_name](FilterProgress& filter_progress) {
          for (std::size_t i = 0; i < photos.size(); ++i) {
            if (filter_progress.update &&
                !filter_progress.update(static_cast<int>(i), static_cast<int>(photos.size()),
                                        FilterProgressStage::Filtering)) {
              throw FilterCancelled();
            }
            Document photo_document(photos[i].width(), photos[i].height(), photos[i].format());
            photo_document.print_settings() = print_settings;
            photo_document.add_pixel_layer(background_name, photos[i]);
            write_flat_image_file(photo_document, targets.at(static_cast<qsizetype>(i)), extension,
                                  image_options);
          }
        });
    progress.setValue(100);
    remember_save_directory_for_path(targets.front());
    return targets;
  } catch (const FilterCancelled&) {
    statusBar()->showMessage(tr("Cancelled Divide Scanned Photos"));
    return std::nullopt;
  } catch (const std::exception& error) {
    show_critical_message(this, tr("Save failed"), translated_file_message(error.what()),
                          QStringLiteral("exportFailedMessageBox"));
    return std::nullopt;
  }
}

void MainWindow::import_sprite_sheet() {
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return;
  }
  const auto path = get_open_file_name(this, tr("Sprite Sheet to Layers"), last_open_directory(), open_file_filter(),
                                       nullptr, QStringLiteral("spriteSheetImportFileDialog"),
                                       FilterNameDetails::Hidden);
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
    add_document_session(std::move(*sliced), tr("Sprite Frames"), QString(),
                         tr("Import sprite sheet"));
    canvas_->fit_to_view_without_enlarging();
    refresh_layer_list();
    refresh_layer_controls();
    update_undo_redo_actions();
    mark_session_modified(session());
    statusBar()->showMessage(tr("Imported %1 frames from %2").arg(frame_count).arg(path));
  } catch (const std::exception& error) {
    show_critical_message(this, tr("Import failed"), translated_file_message(error.what()),
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
                                                   /*for_export*/ true, sheet.size());
    if (!image_options.has_value()) {
      return;
    }
    // The sheet routes through the normal export machinery (export transforms, indexed GIF/PCX
    // quantization, ...) as a flat document. It inherits the source document's print
    // resolution (the composed QImage would otherwise contribute Qt's screen default).
    auto sheet_document = document_from_qimage(sheet, tr("Sprite Sheet").toStdString());
    sheet_document.print_settings() = document().print_settings();
    write_flat_image_file(sheet_document, path, extension, *image_options);
    offer_browser_download_for_saved_file(path);
    remember_save_directory_for_path(path);
    statusBar()->showMessage(tr("Exported sprite sheet %1").arg(path));
    play_theme_sound(ThemeSoundEvent::ExportDone);
    if (image_options->export_reveal_in_file_explorer) {
      reveal_path_in_file_explorer(path, /*is_file*/ true);
    }
  } catch (const std::exception& error) {
    show_critical_message(this, tr("Export failed"), translated_file_message(error.what()),
                          QStringLiteral("exportFailedMessageBox"));
  }
}

void MainWindow::import_image_sequence() {
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return;
  }
  auto paths = get_open_file_names(this, tr("Image Sequence to Layers"), last_open_directory(), open_file_filter(),
                                   nullptr, QStringLiteral("imageSequenceImportFileDialog"),
                                   FilterNameDetails::Hidden);
  if (paths.isEmpty()) {
    return;
  }
  // A single numbered file stands for its whole sibling run; the confirmation dialog
  // below shows exactly what the expansion found before anything imports.
  paths = paths.size() == 1 ? expand_numbered_sequence(paths.front()) : sorted_sequence_paths(std::move(paths));

  // Header-only size probe so the confirmation dialog can state the canvas size
  // without decoding every frame.
  QSize canvas_size;
  for (const auto& path : paths) {
    const auto size = QImageReader(path).size();
    if (size.isValid()) {
      canvas_size = canvas_size.isValid() ? canvas_size.expandedTo(size) : size;
    }
  }
  if (!prompt_image_sequence_import_options(this, paths, canvas_size)) {
    return;
  }
  QString error;
  auto imported = document_from_image_sequence(paths, &error);
  if (!imported.has_value()) {
    show_critical_message(this, tr("Import failed"), error, QStringLiteral("openFailedMessageBox"));
    return;
  }
  const auto frame_count = static_cast<int>(imported->layers().size());
  if (const auto default_layer_id = default_non_group_layer_id(imported->layers()); default_layer_id.has_value()) {
    imported->set_active_layer(*default_layer_id);
  }
  add_document_session(std::move(*imported), tr("Image Sequence"), QString(),
                       tr("Import image sequence"));
  canvas_->fit_to_view_without_enlarging();
  refresh_layer_list();
  refresh_layer_controls();
  update_undo_redo_actions();
  mark_session_modified(session());
  statusBar()->showMessage(tr("Imported %1 images as layers").arg(frame_count));
}

void MainWindow::export_image_sequence() {
  if (!has_active_document()) {
    show_status_error(tr("No document"));
    return;
  }
  finish_active_text_editor();
  // One file per top-level layer, bottom to top (matching the sprite-sheet export's
  // frame semantics); the options dialog picks visible-only (default) or all layers.
  std::vector<QString> visible_layer_names;
  std::vector<QString> all_layer_names;
  for (const auto& layer : std::as_const(document()).layers()) {
    all_layer_names.push_back(QString::fromStdString(layer.name()));
    if (layer.visible()) {
      visible_layer_names.push_back(all_layer_names.back());
    }
  }
  if (all_layer_names.empty()) {
    show_information_message(this, tr("Export Image Sequence"), tr("There are no layers to export."),
                             QStringLiteral("imageSequenceNoLayersMessageBox"));
    return;
  }

  QString selected_filter;
  const auto base_name = QFileInfo(session().title.isEmpty() ? tr("Untitled") : session().title).completeBaseName();
  auto path = get_save_file_name(this, tr("Export Image Sequence"),
                                 file_dialog_initial_path(QString(), base_name + QStringLiteral("_001.png")),
                                 export_image_filter(), &selected_filter,
                                 QStringLiteral("imageSequenceExportFileDialog"));
  if (path.isEmpty()) {
    return;
  }
  path = path_with_default_extension(path, selected_filter);
  const QFileInfo chosen(path);
  const auto extension = extension_for_path(path);
  const auto options = prompt_image_sequence_export_options(this, visible_layer_names, all_layer_names,
                                                            naming_from_save_base_name(chosen.completeBaseName()),
                                                            extension);
  if (!options.has_value()) {
    return;
  }
  try {
    auto image_options = prompt_image_save_options(
        this, extension, image_save_defaults_for_document(), /*for_export*/ true,
        QSize(std::as_const(document()).width(), std::as_const(document()).height()));
    if (!image_options.has_value()) {
      return;
    }
    const auto& layer_names = options->visible_layers_only ? visible_layer_names : all_layer_names;
    const auto file_names = image_sequence_file_names(layer_names, options->naming, extension);
    const auto directory = chosen.dir();
    // The save dialog confirmed overwriting only the exact name typed there; the rest
    // of the set needs its own check.
    int existing = 0;
    for (const auto& name : file_names) {
      const auto target = directory.filePath(name);
      if (target != path && QFileInfo::exists(target)) {
        ++existing;
      }
    }
    if (existing > 0) {
      const auto answer = show_warning_message(
          this, tr("Export Image Sequence"),
          tr("%1 of %2 files already exist in this folder. Overwrite them?").arg(existing).arg(file_names.size()),
          QMessageBox::Yes | QMessageBox::No, QMessageBox::No, QStringLiteral("imageSequenceOverwriteMessageBox"));
      if (answer != QMessageBox::Yes) {
        return;
      }
    }
    int frame_index = 0;
    for (const auto& layer : std::as_const(document()).layers()) {
      if (options->visible_layers_only && !layer.visible()) {
        continue;
      }
      // Each frame routes through the normal export machinery as a flat document and
      // inherits the source document's print resolution (same as the sprite sheet).
      auto frame_document = document_from_qimage(render_layer_isolated(document(), layer), tr("Frame").toStdString());
      frame_document.print_settings() = document().print_settings();
      write_flat_image_file(frame_document, directory.filePath(file_names[frame_index]), extension, *image_options);
      ++frame_index;
    }
    remember_save_directory_for_path(path);
    statusBar()->showMessage(tr("Exported %1 images to %2").arg(file_names.size()).arg(directory.absolutePath()));
    play_theme_sound(ThemeSoundEvent::ExportDone);
    if (image_options->export_reveal_in_file_explorer) {
      reveal_path_in_file_explorer(directory.absolutePath(), /*is_file*/ false);
    }
  } catch (const std::exception& error) {
    show_critical_message(this, tr("Export failed"), translated_file_message(error.what()),
                          QStringLiteral("exportFailedMessageBox"));
  }
}

void MainWindow::export_animated_gif() {
  if (!has_active_document()) {
    show_status_error(tr("No document"));
    return;
  }
  finish_active_text_editor();
  if (animation_preview_window_ != nullptr) animation_preview_window_->stop_playback_for(&document());
  // One frame per visible top-level layer, top to bottom; the options dialog restates
  // the rules (hidden layers skipped, "0.25s" name tokens override the default delay).
  if (!has_visible_top_level_layer(std::as_const(document()))) {
    show_information_message(this, tr("Export Animated GIF"), tr("There are no visible layers to export."),
                             QStringLiteral("animatedGifNoLayersMessageBox"));
    return;
  }
  QString selected_filter;
  const auto base_name = QFileInfo(session().title.isEmpty() ? tr("Untitled") : session().title).completeBaseName();
  const auto initial_path = file_dialog_initial_path(QString(), base_name + QStringLiteral(".gif"));
  auto path = get_save_file_name(this, tr("Export Animated GIF"), initial_path,
                                 save_file_filter_for_path(initial_path), &selected_filter,
                                 QStringLiteral("animatedGifExportFileDialog"));
  if (path.isEmpty()) {
    return;
  }
  path = path_with_default_extension(path, selected_filter);
  auto options = prompt_gif_save_options(this, image_save_defaults_for_document(),
                                         /*offer_flatten_choice*/ false, /*for_export*/ true,
                                         /*has_visible_frames*/ true,
                                         QSize(std::as_const(document()).width(), std::as_const(document()).height()));
  if (!options.has_value()) {
    return;
  }
  try {
    std::vector<std::string> writer_notices;
    write_flat_image_file(document(), path, QStringLiteral("gif"), *options, &writer_notices);
    offer_browser_download_for_saved_file(path);
    remember_save_directory_for_path(path);
    statusBar()->showMessage(tr("Exported %1").arg(path) + export_notes_suffix_for(writer_notices));
    play_theme_sound(ThemeSoundEvent::ExportDone);
    if (options->export_reveal_in_file_explorer) {
      reveal_path_in_file_explorer(path, /*is_file*/ true);
    }
  } catch (const std::exception& error) {
    show_critical_message(this, tr("Export failed"), translated_file_message(error.what()),
                          QStringLiteral("exportFailedMessageBox"));
  }
}

void MainWindow::export_animated_webp() {
  if (!has_active_document()) {
    show_status_error(tr("No document"));
    return;
  }
  finish_active_text_editor();
  if (animation_preview_window_ != nullptr) animation_preview_window_->stop_playback_for(&document());
  // One frame per visible top-level layer, top to bottom; the options dialog restates
  // the rules (hidden layers skipped, "0.25s" name tokens override the default delay).
  if (!has_visible_top_level_layer(std::as_const(document()))) {
    show_information_message(this, tr("Export Animated WebP"), tr("There are no visible layers to export."),
                             QStringLiteral("animatedWebpNoLayersMessageBox"));
    return;
  }
  QString selected_filter;
  const auto base_name = QFileInfo(session().title.isEmpty() ? tr("Untitled") : session().title).completeBaseName();
  const auto initial_path = file_dialog_initial_path(QString(), base_name + QStringLiteral(".webp"));
  auto path = get_save_file_name(this, tr("Export Animated WebP"), initial_path,
                                 save_file_filter_for_path(initial_path), &selected_filter,
                                 QStringLiteral("animatedWebpExportFileDialog"));
  if (path.isEmpty()) {
    return;
  }
  path = path_with_default_extension(path, selected_filter);
  auto defaults = image_save_defaults_for_document();
  defaults.webp_animate = true;
  auto options = prompt_image_save_options(this, QStringLiteral("webp"), defaults, true,
      QSize(std::as_const(document()).width(), std::as_const(document()).height()));
  if (!options.has_value()) {
    return;
  }
  try {
    std::vector<std::string> writer_notices;
    write_flat_image_file(document(), path, QStringLiteral("webp"), *options, &writer_notices);
    persist_image_save_defaults(*options);
    offer_browser_download_for_saved_file(path);
    remember_save_directory_for_path(path);
    statusBar()->showMessage(tr("Exported %1").arg(path) + export_notes_suffix_for(writer_notices));
    play_theme_sound(ThemeSoundEvent::ExportDone);
    if (options->export_reveal_in_file_explorer) {
      reveal_path_in_file_explorer(path, /*is_file*/ true);
    }
  } catch (const std::exception& error) {
    show_critical_message(this, tr("Export failed"), translated_file_message(error.what()),
                          QStringLiteral("exportFailedMessageBox"));
  }
}

void MainWindow::export_animated_png() {
  if (!has_active_document()) {
    show_status_error(tr("No document"));
    return;
  }
  finish_active_text_editor();
  if (animation_preview_window_ != nullptr) animation_preview_window_->stop_playback_for(&document());
  // One frame per visible top-level layer, top to bottom; the options dialog restates
  // the rules (hidden layers skipped, "0.25s" name tokens override the default delay).
  if (!has_visible_top_level_layer(std::as_const(document()))) {
    show_information_message(this, tr("Export Animated PNG"), tr("There are no visible layers to export."),
                             QStringLiteral("animatedPngNoLayersMessageBox"));
    return;
  }
  QString selected_filter;
  const auto base_name = QFileInfo(session().title.isEmpty() ? tr("Untitled") : session().title).completeBaseName();
  const auto initial_path = file_dialog_initial_path(QString(), base_name + QStringLiteral(".png"));
  auto path = get_save_file_name(this, tr("Export Animated PNG"), initial_path,
                                 save_file_filter_for_path(initial_path), &selected_filter,
                                 QStringLiteral("animatedPngExportFileDialog"));
  if (path.isEmpty()) {
    return;
  }
  path = path_with_default_extension(path, selected_filter);
  auto defaults = image_save_defaults_for_document();
  defaults.apng_animate = true;
  auto options = prompt_image_save_options(this, QStringLiteral("png"), defaults, true,
      QSize(std::as_const(document()).width(), std::as_const(document()).height()));
  if (!options.has_value()) {
    return;
  }
  try {
    std::vector<std::string> writer_notices;
    write_flat_image_file(document(), path, QStringLiteral("png"), *options, &writer_notices);
    persist_image_save_defaults(*options);
    offer_browser_download_for_saved_file(path);
    remember_save_directory_for_path(path);
    statusBar()->showMessage(tr("Exported %1").arg(path) + export_notes_suffix_for(writer_notices));
    play_theme_sound(ThemeSoundEvent::ExportDone);
    if (options->export_reveal_in_file_explorer) {
      reveal_path_in_file_explorer(path, /*is_file*/ true);
    }
  } catch (const std::exception& error) {
    show_critical_message(this, tr("Export failed"), translated_file_message(error.what()),
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
        [this]() -> const Document* { return has_active_document() ? &document() : nullptr; },
        [this] { toggle_tile_seam_offset(); }, this);
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

void MainWindow::toggle_animation_preview_window() {
  if (animation_preview_window_ != nullptr && animation_preview_window_->isVisible()) {
    // close() funnels through done(), which stops playback and restores visibility.
    animation_preview_window_->close();
    return;
  }
  if (animation_preview_window_ == nullptr) {
    auto* window = new AnimationPreviewWindow(
        [this]() -> Document* { return has_active_document() ? &document() : nullptr; },
        [this](bool final_refresh) {
          if (canvas_ != nullptr) {
            canvas_->document_changed();
          }
          if (final_refresh) {
            refresh_layer_list();
            refresh_layer_controls();
          } else {
            // Per-frame: a full row rebuild is too heavy at animation rates.
            sync_layer_row_visibility_indicators();
          }
        },
        [this](std::optional<std::uint32_t> delay_ms, bool all_layers) {
          set_selected_layers_frame_time(delay_ms, all_layers);
        },
        this);
    window->setAttribute(Qt::WA_DeleteOnClose);
    animation_preview_window_ = window;
  }
  animation_preview_window_->show();
  animation_preview_window_->raise();
  animation_preview_window_->activateWindow();
}

#ifndef Q_OS_WASM
bool MainWindow::open_recovered_document(const QString& psb_path, const QString& title, const QString& original_path,
                                         std::int64_t* session_id) {
  try {
    // The copy is Patchy's own PSB: no prompts, no raw dialog, and the same
    // post-open text fix-up the regular open path applies.
    auto loaded = load_document_interactive(this, psb_path, /*interactive=*/false, /*allow_raw_dialog=*/false);
    if (!loaded.has_value()) {
      return false;
    }
    lay_out_raster_less_imported_text(loaded->document);
    record_text_layout_metrics_for_reopened_text(loaded->document);
    add_document_session(std::move(loaded->document), title, original_path, tr("Recover"));
    auto& recovered = session();
    apply_opening_view(recovered);
    // Recovered content is by definition unsaved: Save goes to the original path.
    mark_session_modified(recovered);
    if (session_id != nullptr) {
      *session_id = recovered.session_id;
    }
    return true;
  } catch (const std::exception& error) {
    fprintf(stderr, "Recovery failed: %s (%s)\n", error.what(), psb_path.toUtf8().constData());
    return false;
  }
}
#endif

// UI effects exposed to the save coordinator. The coordinator retains only session ids.
class MainWindowSaveHost final : public SaveHost {
public:
  explicit MainWindowSaveHost(MainWindow& window) : window_(window) {}
  DocumentSession* find_session(std::int64_t id) override { return window_.session_with_id(id); }
  bool save_as() override { return window_.save_document_as(); }
  bool save_to_path(const QString& path) override { return window_.save_document_to_path(path); }
  bool commit_embedded(std::int64_t id) override {
    auto* target = find_session(id);
    return target != nullptr && window_.commit_smart_object_child_session(*target);
  }
  void offer_download(const QString& path) override { offer_browser_download_for_saved_file(path); }
  void remember_directory(const QString& path) override { remember_save_directory_for_path(path); }
  void persist_defaults(const ImageSaveOptions& options) override { window_.persist_image_save_defaults(options); }
  void add_recent(const QString& path) override { window_.add_recent_file(path); }
  void mark_saved(std::int64_t id) override {
    if (auto* target = find_session(id)) window_.set_session_saved(*target);
  }
  void run_write(const QString& path, bool on_worker, bool show_progress,
                 const std::function<void(SaveProgress&)>& write) override {
    window_.run_save_write(path, on_worker, show_progress, write);
  }
private:
  MainWindow& window_;
};

namespace {

// A save quicker than this never shows the Saving dialog (PATCHY_SAVE_PROGRESS_DELAY_MS
// overrides it for tests).
constexpr int kSaveProgressDelayMs = 400;

int save_progress_delay_ms() noexcept {
  bool ok = false;
  const auto value = qEnvironmentVariableIntValue("PATCHY_SAVE_PROGRESS_DELAY_MS", &ok);
  return ok ? std::max(0, value) : kSaveProgressDelayMs;
}

}  // namespace

void MainWindow::run_save_write(const QString& path, bool on_worker, bool show_progress,
                                const std::function<void(SaveProgress&)>& write) {
  SaveProgress progress;
  if (!show_progress || unattended_automation()) {
    write(progress);
    return;
  }
  const auto file_name = QFileInfo(path).fileName();
  std::unique_ptr<QProgressDialog> dialog;
  QElapsedTimer clock;
  clock.start();
  const auto refresh = [&] {
    if (dialog == nullptr) {
      if (clock.elapsed() < save_progress_delay_ms()) {
        return;
      }
      dialog = std::make_unique<QProgressDialog>(tr("Saving %1...").arg(file_name), QString(), 0, 0, this);
      dialog->setObjectName(QStringLiteral("saveProgressDialog"));
      dialog->setWindowTitle(tr("Saving %1").arg(file_name));
      dialog->setWindowModality(Qt::WindowModal);
      dialog->setMinimumDuration(0);
      dialog->setCancelButton(nullptr);
      dialog->setAutoClose(false);
      dialog->setAutoReset(false);
      remember_dialog_position(*dialog);
      dialog->show();
    }
    const auto state = progress.state();
    QString text;
    if (state.total <= 0) {
      text = tr("Saving %1...").arg(file_name);
    } else if (state.kind == SaveProgress::Kind::Pages) {
      text = tr("Writing page %1 of %2...").arg(state.done).arg(state.total);
    } else if (state.done <= 0) {
      text = tr("Compositing the image...");
    } else if (state.done >= state.total) {
      text = tr("Writing %1...").arg(file_name);
    } else if (!state.label.isEmpty()) {
      text = tr("Saving layer %1 of %2: %3").arg(state.done).arg(state.total - 1).arg(state.label);
    } else {
      text = tr("Saving layer %1 of %2...").arg(state.done).arg(state.total - 1);
    }
    if (state.total > 0) {
      dialog->setRange(0, state.total);
      dialog->setValue(std::clamp(state.done, 0, state.total));
    } else {
      dialog->setRange(0, 0);  // busy
    }
    dialog->setLabelText(text);
  };
  // Input waits until the save is done (the document must not change under the writer);
  // paint and timers keep running, so the dialog animates.
  const auto pump = [&] {
    refresh();
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 15);
  };
  const auto close_dialog = qScopeGuard([&dialog] {
    if (dialog != nullptr) {
      dialog->close();
    }
  });
  Q_UNUSED(close_dialog);
#if defined(Q_OS_WASM)
  // A browser main thread must not spin on a future (see the open path); write here.
  on_worker = false;
#endif
  if (!on_worker || kBackgroundWorkRunsInline) {
    progress.set_pump(pump);
    write(progress);
    return;
  }
  auto future = launch_async([&write, &progress] { write(progress); });
  while (future.wait_for(std::chrono::milliseconds(15)) != std::future_status::ready) {
    pump();
  }
  future.get();  // rethrows the writer's failure
}

bool MainWindow::save_document() {
  if (!has_active_document()) {
    show_status_error(tr("No document"));
    return false;
  }
  finish_active_text_editor();
  MainWindowSaveHost host(*this);
  return DocumentSaveCoordinator(host).save(session().session_id);
}

bool MainWindow::save_document_as() {
  if (!has_active_document()) {
    show_status_error(tr("No document"));
    return false;
  }
  finish_active_text_editor();
  const auto saving_session_id = session().session_id;
  const auto still_saving_same_session = [this, saving_session_id] {
    return has_active_document() && session().session_id == saving_session_id;
  };
  const auto fallback_name = session().title.isEmpty() ? tr("Untitled.psd") : session().title;
  auto initial_path = file_dialog_initial_path(session().path, fallback_name);
  if (const auto content_extension = session().content_extension; !content_extension.isEmpty()) {
    // A file opened by its content (GitHub issue 362) suggests the extension of what it
    // really is: src.bin holding a PSD becomes src.psd.
    const QFileInfo initial_info(initial_path);
    const auto base_name = initial_info.suffix().isEmpty() ? initial_info.fileName() : initial_info.completeBaseName();
    initial_path = initial_info.dir().filePath(base_name + QLatin1Char('.') + content_extension);
  }
  if (save_discards_layers(std::as_const(document()), extension_for_path(initial_path)) ||
      is_read_only_source_extension(extension_for_path(initial_path))) {
    // Photoshop behavior: Save As for a layered document defaults to PSD, not the flat
    // format the document was opened from. Read-only sources (camera raw) also default
    // to PSD: their own extension can never be written.
    const QFileInfo initial_info(initial_path);
    auto base_name = is_supported_image_extension(extension_for_path(initial_path))
                         ? initial_info.completeBaseName()
                         : initial_info.fileName();
    if (xcf::is_compressed_xcf_file_name(initial_info.fileName().toStdString())) {
      base_name = initial_info.fileName().left(initial_info.fileName().lastIndexOf(QStringLiteral(".xcf."),
                                                                               -1, Qt::CaseInsensitive));
    }
    initial_path = initial_info.dir().filePath(base_name + QStringLiteral(".psd"));
  }
  auto selected_filter = save_file_filter_for_path(initial_path);
  refresh_recent_history();
  auto path = get_save_file_name(this, tr("Save As"), initial_path, save_file_filter(), &selected_filter,
                                 QStringLiteral("saveAsFileDialog"), recent_files_);
  if (path.isEmpty() || !still_saving_same_session()) {
    return false;
  }
  path = path_with_default_extension(path, selected_filter);
  const auto extension = extension_for_path(path);
  bool discards_layers = save_discards_layers(std::as_const(document()), extension);
  const bool linked_external_child =
      session().smart_object_link.has_value() && session().smart_object_link->external;
  std::optional<ImageSaveOptions> image_options;
  if (is_tiff_extension(extension) && flat_save_discards_layers(std::as_const(document()))) {
    // TIFF Options: keep the layers (a layered TIFF, the default) or save a flattened copy;
    // the dialog says what each means, so no flatten warning follows (docs/tiff.md).
    auto tiff_options = prompt_tiff_save_options(this, image_save_defaults_for_document());
    if (!tiff_options.has_value() || !still_saving_same_session()) {
      return false;
    }
    discards_layers = !tiff_options->tiff_layers.value_or(true);
    image_options = std::move(tiff_options);
  } else if (is_pdf_extension(extension) && !linked_external_child) {
    // PDF Options answers everything a PDF save asks, the flatten-or-editable question
    // included (docs/pdf.md).
    const auto choice = prompt_pdf_options(path, /*export_pages*/ false);
    if (!choice.has_value() || !still_saving_same_session()) {
      return false;
    }
    auto pdf_options = image_save_defaults_for_document();
    apply_pdf_choice_to_save_options(*choice, pdf_options);
    image_options = std::move(pdf_options);
  } else if (discards_layers) {
    if ((extension == QStringLiteral("gif") || extension == QStringLiteral("webp") ||
                extension == QStringLiteral("jxl")) &&
               std::as_const(document()).layers().size() >= 2) {
      // GIF asks animation-or-flatten instead of the plain flatten warning; an explicit
      // animation choice needs no warning (the frames round-trip through reopening).
      auto defaults = image_save_defaults_for_document();
      defaults.offer_animation = true;
      defaults.has_visible_frames = has_visible_top_level_layer(std::as_const(document()));
      auto gif_options = extension != QStringLiteral("gif")
          ? prompt_image_save_options(this, extension, defaults)
          : prompt_gif_save_options(this, defaults, /*offer_flatten_choice*/ true,
                                    /*for_export*/ false, defaults.has_visible_frames);
      if (!gif_options.has_value()) {
        return false;
      }
      if (!gif_options->gif_animate && !gif_options->webp_animate && !gif_options->jxl_animate &&
          !confirm_flatten_layers_for_save(extension)) {
        return false;
      }
      image_options = std::move(gif_options);
    } else if (!confirm_flatten_layers_for_save(extension)) {
      return false;
    }
  }
  if (!image_options.has_value() && !is_photoshop_document_extension(extension) &&
      image_save_options_apply_to_extension(extension)) {
    auto defaults = image_save_defaults_for_document();
    image_options = prompt_image_save_options(this, extension, defaults, /*for_export*/ false, QSize(),
                                              &std::as_const(document()));
    if (!image_options.has_value()) {
      return false;
    }
  }
  if (!still_saving_same_session() ||
      !save_document_to_path(path, image_options, SaveToPathPolicy{/*flatten_confirmed=*/discards_layers, /*scripted=*/false,
                                                           /*export_copy=*/false})) {
    return false;
  }
  return true;
}

bool MainWindow::confirm_flatten_layers_for_save(const QString& extension) {
  const bool linked_external_child =
      session().smart_object_link.has_value() && session().smart_object_link->external;
  // A linked smart-object child writes the linked file itself (the file on disk is the
  // document), so its flat save is a real save; everything else saves a flattened copy
  // and keeps the layered document open with its unsaved changes (Photoshop's
  // save-a-copy semantics). SVG gets its own wording: shape layers stay real
  // vectors there, only what the writer's dry run reports bakes, and the message
  // names it (save_discards_layers never brings a shape-only document here). PDF
  // (not linked) never comes here: it asks flatten-or-editable through
  // resolve_pdf_layer_choice instead.
  QString message;
  if (extension == QStringLiteral("svg")) {
    const auto baked = svg_baked_content_summary(svg::DocumentIo::baked_content(std::as_const(document())));
    message = linked_external_child
                  ? tr("SVG keeps shape layers as vectors, but this document has content SVG cannot hold as "
                       "vectors. Continue saving and bake it into images in the linked file?\n\n"
                       "Baked into images: %1.")
                        .arg(baked)
                  : tr("SVG keeps shape layers as vectors, but this document has content SVG cannot hold as "
                       "vectors, so Patchy will save a copy with that content baked into images. The open "
                       "document will keep its layers and unsaved changes. To keep everything editable, save "
                       "as a Photoshop document (.psd) instead.\n\nBaked into images: %1.")
                        .arg(baked);
  } else if (linked_external_child) {
    message = tr("This file format cannot store layers. Continue saving and flatten the linked file?");
  } else {
    message = tr("This file format cannot store layers, so Patchy will save a flattened copy. The open "
                 "document will keep its layers and unsaved changes. To keep layers in the file, save as a "
                 "Photoshop document (.psd) instead.");
  }
  const auto answer =
      show_warning_message(this, tr("Layers Will Be Flattened"), message,
                           QMessageBox::Save | QMessageBox::Cancel, QMessageBox::Cancel,
                           QStringLiteral("flattenLayersMessageBox"));
  return answer == QMessageBox::Save;
}

bool MainWindow::confirm_cmyk_proof_save(const QString& extension, ImageSaveOptions& options) {
  auto settings = app_settings();
  if (!settings.value(QStringLiteral("saveOptions/cmykProofSaveNotice"), true).toBool()) {
    return true;
  }
  const auto space = working_cmyk_space();
  const auto format = is_jpeg_extension(extension) ? QStringLiteral("JPEG")
                      : is_pdf_extension(extension) ? QStringLiteral("PDF")
                                                    : QStringLiteral("TIFF");
  const auto intent = space->intent() == RenderingIntent::Perceptual ? tr("Perceptual")
                      : space->intent() == RenderingIntent::Saturation ? tr("Saturation")
                      : space->intent() == RenderingIntent::AbsoluteColorimetric ? tr("Absolute Colorimetric")
                                                                                   : tr("Relative Colorimetric");
  QDialog dialog(this);
  dialog.setObjectName(QStringLiteral("cmykProofSaveDialog"));
  dialog.setWindowTitle(tr("Save as CMYK"));
  auto* layout = new QVBoxLayout(&dialog);
  auto* summary = new QLabel(tr("This document is in CMYK Proof mode, so Patchy saves this %1 file in CMYK.").arg(format),
                             &dialog);
  summary->setWordWrap(true);
  layout->addWidget(summary);
  auto* details = new QLabel(
      tr("The image is placed on white paper and separated through the CMYK profile %1 with the %2 rendering "
         "intent, and that profile is embedded in the file. Patchy opens CMYK files as RGB again, so keep a "
         "PSD copy for further editing. The profile is chosen in Preferences.")
          .arg(QString::fromStdString(space->profile_description()), intent),
      &dialog);
  details->setWordWrap(true);
  layout->addWidget(details);
  auto* black_ink_only = new QCheckBox(tr("Black and grays in black ink only"), &dialog);
  black_ink_only->setObjectName(QStringLiteral("cmykProofSaveBlackInkOnlyCheck"));
  black_ink_only->setToolTip(tr("Prints black text and gray areas with black ink alone, so small type stays sharp "
                                "on press. Colored areas are separated through the profile as usual."));
  black_ink_only->setChecked(options.export_black_ink_only);
  layout->addWidget(black_ink_only);
  auto* dont_show = new QCheckBox(tr("Don't show this again"), &dialog);
  dont_show->setObjectName(QStringLiteral("cmykProofSaveDontShowCheck"));
  layout->addWidget(dont_show);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget(buttons);
  dialog.setMinimumWidth(460);
  if (exec_dialog(dialog) != QDialog::Accepted) {
    return false;
  }
  options.export_black_ink_only = black_ink_only->isChecked();
  settings.setValue(cmyk_black_ink_only_settings_key(extension), black_ink_only->isChecked());
  if (dont_show->isChecked()) {
    settings.setValue(QStringLiteral("saveOptions/cmykProofSaveNotice"), false);
  }
  return true;
}

bool MainWindow::open_cmyk_source_in_proof_mode(DocumentSession& session) {
  return set_session_cmyk_proof(session, true).isEmpty();
}

void MainWindow::show_cmyk_proof_open_notice() {
  // Persisted settings key (never rename).
  const auto key = QStringLiteral("imports/cmykProofOpenNotice");
  auto settings = app_settings();
  if (unattended_automation() || (script_engine_host_ != nullptr && script_engine_host_->run_active()) ||
      !settings.value(key, true).toBool()) {
    return;
  }
  QDialog dialog(this);
  dialog.setObjectName(QStringLiteral("cmykProofOpenNoticeDialog"));
  dialog.setWindowTitle(tr("CMYK Proof"));
  auto* layout = new QVBoxLayout(&dialog);
  auto* summary = new QLabel(tr("This CMYK file is open in Image > Mode > CMYK Proof."), &dialog);
  summary->setWordWrap(true);
  layout->addWidget(summary);
  auto* details = new QLabel(
      tr("CMYK Proof is an emulation. Patchy edits in RGB: it converts the file's inks to RGB when it opens it, "
         "shows the print through the CMYK color profile, and converts back to CMYK when it saves. Colors and ink "
         "numbers can therefore differ slightly from a true CMYK editor such as Photoshop. Layers of a CMYK PSD "
         "that you do not change keep their original inks."),
      &dialog);
  details->setWordWrap(true);
  layout->addWidget(details);
  auto* dont_show = new QCheckBox(tr("Don't show this again"), &dialog);
  dont_show->setObjectName(QStringLiteral("cmykProofOpenNoticeDontShowCheck"));
  layout->addWidget(dont_show);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dialog);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  layout->addWidget(buttons);
  dialog.setMinimumWidth(460);
  exec_dialog(dialog);
  if (dont_show->isChecked()) {
    settings.setValue(key, false);
  }
}

// A layered document going to PDF has two honest forms (one flattened image, or layers
// kept as paths/text/images that may not composite exactly like the canvas), so instead
// of the flatten confirmation the user picks one here. The remembered policy
// (saveOptions/pdfLayerPolicy: "ask" / "flatten" / "editable", also in Preferences)
// answers without a dialog; the dialog's "Remember this choice" sets it. Returns the
// choice (true = keep layers editable) or nullopt for Cancel.
std::optional<MultiPagePdfExportChoice> MainWindow::prompt_pdf_options(const QString& path, bool export_pages) {
  if (!has_active_document()) {
    return std::nullopt;
  }
  const auto& doc = std::as_const(document());
  PdfOptionsDialogContext context;
  context.active_session_id = session().session_id;
  for (const auto& layer : doc.layers()) {
    if (layer.kind() == LayerKind::Group && !layer_is_artboard(layer)) {
      ++context.top_level_group_count;
      context.hidden_group_count += layer.visible() ? 0 : 1;
    }
  }
  context.artboard_count = static_cast<int>(document_artboards(doc).size());
  const auto carries_source = [](const Document& candidate) {
    return candidate.metadata().pdf_source_page != nullptr || !candidate.metadata().pdf_page_source_pages.empty();
  };
  context.original_image_data_available = carries_source(doc);
  if (export_pages) {
    context.documents.reserve(sessions_.size());
    for (const auto& candidate : sessions_) {
      if (candidate == nullptr) {
        continue;
      }
      context.documents.push_back(
          {candidate->title.isEmpty() ? tr("Untitled") : candidate->title, candidate->session_id});
      context.original_image_data_available =
          context.original_image_data_available || carries_source(std::as_const(candidate->document));
    }
    context.title = tr("Export Multi-Page PDF");
    context.accept_text = tr("Export");
  } else {
    // A flat document has nothing to keep editable. The page source starts where Save As
    // always started: artboards, an imported PDF's page folders, else the canvas.
    context.layered = flat_save_discards_layers(doc);
    const auto defaults = image_save_defaults_for_document();
    context.default_source = defaults.pdf_pages == QStringLiteral("artboards") ? MultiPagePdfSource::Artboards
                             : defaults.pdf_pages == QStringLiteral("groups")  ? MultiPagePdfSource::TopLevelGroups
                                                                               : MultiPagePdfSource::Canvas;
    context.title = tr("PDF Options");
    context.accept_text = tr("Save");
  }
  // The flatten-or-editable preference (Preferences, "Saving layered documents as PDF")
  // sets the check; "ask" starts from the last answer.
  const auto policy =
      app_settings().value(QStringLiteral("saveOptions/pdfLayerPolicy"), QStringLiteral("ask")).toString();
  if (policy == QStringLiteral("editable")) {
    context.editable_default = true;
  } else if (policy == QStringLiteral("flatten")) {
    context.editable_default = false;
  }
  context.file_name = QFileInfo(path).fileName();
  return run_pdf_options_dialog(this, context);
}

std::optional<bool> MainWindow::resolve_pdf_layer_choice(bool for_export, bool allow_prompt) {
  auto settings = app_settings();
  const auto policy = settings.value(QStringLiteral("saveOptions/pdfLayerPolicy"), QStringLiteral("ask")).toString();
  if (policy == QStringLiteral("flatten")) {
    return false;
  }
  if (policy == QStringLiteral("editable")) {
    return true;
  }
  if (!allow_prompt) {
    // A scripted, CLI, or already-confirmed save cannot ask; "ask" means flat, which is
    // the mode that always reproduces the canvas.
    return false;
  }
  QMessageBox box(this);
  box.setObjectName(QStringLiteral("pdfLayersMessageBox"));
  box.setIcon(QMessageBox::Question);
  box.setWindowTitle(for_export ? tr("Export PDF Layers") : tr("Save PDF Layers"));
  box.setText(tr("How should this document's layers be written to the PDF?"));
  box.setInformativeText(
      tr("Keep layers editable: shape layers become paths, text stays real text, and pixel layers "
         "become images, so the PDF opens as separate pieces in Patchy and other editors. Blend modes, "
         "adjustment layers, group opacity, layer styles, and pixel masks are flattened into images "
         "where needed, so the page may not look exactly like the canvas.\n\n"
         "Flatten to one image: the page looks exactly like the canvas.\n\n"
         "Either way Patchy writes a copy; the open document keeps its layers and unsaved changes. "
         "Preferences > Saving layered documents as PDF sets a default that skips this question."));
  auto* editable_button = box.addButton(tr("Keep Layers Editable"), QMessageBox::AcceptRole);
  auto* flatten_button = box.addButton(tr("Flatten to One Image"), QMessageBox::AcceptRole);
  box.addButton(QMessageBox::Cancel);
  box.setDefaultButton(flatten_button);
  box.setEscapeButton(QMessageBox::Cancel);
  auto* remember = new QCheckBox(tr("Remember this choice"), &box);
  remember->setObjectName(QStringLiteral("pdfLayersRememberCheck"));
  box.setCheckBox(remember);
  exec_dialog(box);
  const auto* clicked = box.clickedButton();
  if (clicked != editable_button && clicked != flatten_button) {
    return std::nullopt;
  }
  const bool editable = clicked == editable_button;
  if (remember->isChecked()) {
    settings.setValue(QStringLiteral("saveOptions/pdfLayerPolicy"),
                      editable ? QStringLiteral("editable") : QStringLiteral("flatten"));
  }
  return editable;
}

bool MainWindow::save_document_to_path(QString path, std::optional<ImageSaveOptions> image_options,
                                       SaveToPathPolicy policy) {
  const bool flatten_confirmed = policy.flatten_confirmed;
  // Scripted saves answer every data-loss question themselves; they also never touch the
  // user's persisted defaults, which unattended runs already leave alone.
  const bool quiet = policy.scripted || unattended_automation();
  finish_active_text_editor();
  if (!has_active_document()) {
    return false;
  }
  const auto saving_session_id = session().session_id;
  // Playback drives real layer visibility one frame at a time; a save mid-playback would
  // write that frame's visibility to disk (close_document_session stops it for the same
  // reason before its own prompt).
  if (animation_preview_window_ != nullptr && has_active_document()) {
    animation_preview_window_->stop_playback_for(&document());
  }
  const auto extension = extension_for_path(path);
  // Layered TIFF (docs/tiff.md): a TIFF keeps the document's layers unless the options turn
  // them off (TIFF Options, a script's `layers: false`, an explicit CMYK color form);
  // exports stay flat.
  std::optional<bool> tiff_layers;
  if (is_tiff_extension(extension)) {
    const bool explicit_cmyk = image_options.has_value() && image_options->export_color_explicit &&
                               image_options->export_cmyk;
    tiff_layers = image_options.has_value() && image_options->tiff_layers.has_value()
                      ? *image_options->tiff_layers
                      : !policy.export_copy && !explicit_cmyk && flat_save_discards_layers(std::as_const(document()));
  }
  const bool discards_layers = tiff_layers.has_value()
                                   ? !*tiff_layers && flat_save_discards_layers(std::as_const(document()))
                                   : save_discards_layers(std::as_const(document()), extension);
  // CLI automation saves are explicit about their target format, so flattening needs no
  // confirmation there (and an unattended run must never block on the prompt).
  const bool linked_external_child =
      session().smart_object_link.has_value() && session().smart_object_link->external;
  // A layered PDF asks its own flatten-or-editable question instead of the generic
  // flatten confirmation, and never both.
  const bool pdf_layer_choice_applies = discards_layers && is_pdf_extension(extension) && !linked_external_child;
  std::optional<bool> pdf_editable_layers;
  if (pdf_layer_choice_applies) {
    if (!image_options.has_value()) {
      // Nobody has resolved the choice yet (Save As and Export resolve it before calling
      // and hand the answer over in `image_options`): ask when this save may prompt,
      // otherwise take the preference. Scripted, CLI, and already-confirmed saves never
      // prompt.
      pdf_editable_layers = resolve_pdf_layer_choice(
          /*for_export*/ false, /*allow_prompt*/ !flatten_confirmed && !unattended_automation());
      if (!pdf_editable_layers.has_value()) {
        return false;
      }
    }
  } else if (discards_layers && !flatten_confirmed && !unattended_automation() &&
             !confirm_flatten_layers_for_save(extension)) {
    return false;
  }
  if (!quiet && !is_photoshop_document_extension(extension) &&
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
  if (!quiet && (extension == QStringLiteral("aseprite") || extension == QStringLiteral("ase")) &&
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
  if (!quiet && extension == QStringLiteral("ora")) {
    // OpenRaster keeps the layer tree, so Save stays on the .ora file; what it cannot hold
    // bakes into pixels or drops, and the user hears what before the write (docs/ora.md).
    const auto losses = ora::DocumentIo::lost_content(std::as_const(document()));
    if (!losses.empty()) {
      const auto answer = show_warning_message(
          this, tr("Some Content Will Be Baked"),
          tr("OpenRaster keeps layers, groups, opacity and blend modes, but some of this document's content "
             "cannot be stored as it is. Continue saving with it baked into layer pixels or left out? To keep "
             "everything editable, also save a Photoshop document (.psd).\n\nChanged: %1.")
              .arg(ora_lost_content_summary(losses)),
          QMessageBox::Save | QMessageBox::Cancel, QMessageBox::Cancel, QStringLiteral("oraLostContentMessageBox"));
      if (answer != QMessageBox::Save) {
        return false;
      }
    }
  }
  // Layered formats that lack some of the document's blend modes ask whether to keep the
  // look or save those layers as Normal; scripts choose with saveAs({unsupportedBlendModes}).
  std::optional<bool> bake_blend_modes;
  if (const auto layered = layered_save_format_for(extension, tiff_layers.value_or(false));
      !quiet && layered.has_value()) {
    const auto unsupported = find_unsupported_blend_modes(std::as_const(document()), *layered);
    if (!unsupported.empty()) {
      bake_blend_modes = ask_unsupported_blend_modes(this, extension, unsupported);
      if (!bake_blend_modes.has_value()) {
        return false;
      }
    }
  }
  if (!has_active_document() || session().session_id != saving_session_id) {
    return false;
  }
  auto effective_image_options = DocumentSaveCoordinator::effective_options(
      session(), path, extension, image_options, image_save_defaults_for_document());
  // Remembered save options must not carry an earlier save's answer into this one.
  effective_image_options.bake_unsupported_blend_modes = bake_blend_modes.value_or(
      image_options.has_value() && image_options->bake_unsupported_blend_modes);
  if (pdf_editable_layers.has_value()) {
    effective_image_options.pdf_editable_layers = *pdf_editable_layers;
  }
  if (tiff_layers.has_value()) {
    effective_image_options.tiff_layers = *tiff_layers;
  }
  bool cmyk_psd = false;
  // Remembered save options must not carry an earlier CMYK Proof save into this one.
  effective_image_options.psd_cmyk_proof = false;
  // CMYK Proof (docs/cmyk.md): unless the Export dialog or a script chose the color form,
  // the document's mode does. CMYK where the format has a CMYK form, RGB everywhere else.
  if (!effective_image_options.export_color_explicit) {
    const bool cmyk = session().cmyk_proof && working_cmyk_space()->has_profile() &&
                      cmyk_form_available(extension, effective_image_options.pdf_editable_layers ||
                                                         effective_image_options.tiff_layers.value_or(false));
    effective_image_options.export_cmyk = cmyk;
    effective_image_options.export_black_ink_only =
        cmyk && app_settings().value(cmyk_black_ink_only_settings_key(extension), false).toBool();
    if (cmyk && !quiet && !confirm_cmyk_proof_save(extension, effective_image_options)) {
      return false;
    }
    // A PSD or PSB is written in CMYK, layers kept, unless a layer needs RGB; an unchanged
    // imported CMYK, gray, Lab or duotone file keeps its own mode (docs/psd-native-color.md).
    psd::LayeredSaveColorMode psd_color;
    const bool photoshop_document = is_photoshop_document_extension(extension);
    if (session().cmyk_proof && photoshop_document) {
      effective_image_options.psd_cmyk_proof = true;
      psd_color = cmyk_proof_psd_save_color_mode(std::as_const(session()).document_for_write());
      cmyk_psd = psd_color.mode == 4 /* PSD CMYK */;
    }
    if (!cmyk && (!photoshop_document || psd_color.mode == 3 /* PSD RGB */) && session().cmyk_proof && !quiet &&
        !policy.export_copy && !session().cmyk_proof_rgb_save_accepted) {
      const auto message =
          is_pdf_extension(extension)
              ? tr("This document is in CMYK Proof mode, but a PDF with editable layers is saved in RGB. "
                   "Save it in RGB?")
          : is_tiff_extension(extension)
              ? tr("This document is in CMYK Proof mode, but a TIFF with layers is saved in RGB. Save it in RGB?")
          : photoshop_document
              ? tr("This document is in CMYK Proof mode, but this %1 file will be saved in RGB. %2 JPEG, TIFF "
                   "and flattened PDF files can always be saved in CMYK. Save it in RGB?")
                    .arg(extension.toUpper(), translate_data_text(psd_color.rgb_reason))
              : tr("This document is in CMYK Proof mode, but %1 files are saved in RGB. Only JPEG, TIFF and "
                   "flattened PDF files can be saved in CMYK. Save it in RGB?")
                    .arg(extension.toUpper());
      const auto answer = show_warning_message(this, tr("Save in RGB"), message, QMessageBox::Save | QMessageBox::Cancel,
                                               QMessageBox::Save, QStringLiteral("cmykProofRgbSaveMessageBox"));
      if (answer != QMessageBox::Save) {
        return false;
      }
      if (has_active_document() && session().session_id == saving_session_id) {
        session().cmyk_proof_rgb_save_accepted = true;
      }
    }
  }
  if (!has_active_document() || session().session_id != saving_session_id) {
    return false;
  }
  // Preferences > Metadata and Watermarks, "Saved metadata": the user's name and copyright
  // (asking first when that is on), never on exports, which follow the Export dialog's
  // Metadata choice. Then the watermark: it goes into the written pixels only (flat
  // formats), never into the open document; Ask mode asks here, before the write.
  const auto save_metadata_mode = stored_save_metadata_mode();
  if (save_metadata_mode == SaveMetadataMode::Personal && !policy.export_copy) {
    add_personal_metadata_for_save(extension, quiet);
  }
  if (!has_active_document() || session().session_id != saving_session_id) {
    return false;
  }
  const auto watermark = watermark_for_save(extension, effective_image_options, quiet);
  if (!has_active_document() || session().session_id != saving_session_id) {
    return false;
  }
  try {
    MainWindowSaveHost host(*this);
    const auto saved = DocumentSaveCoordinator(host).write_prepared(saving_session_id, path, extension,
                                                                    effective_image_options, quiet,
                                                                    policy.export_copy, discards_layers, watermark,
                                                                    save_metadata_mode != SaveMetadataMode::None);
    if (!saved.has_value()) {
      return false;
    }
    const auto export_notes_suffix = export_notes_suffix_for(saved->notices);
    const bool saved_flattened_copy = saved->flattened_copy;
    if (saved_flattened_copy || policy.export_copy) {
      statusBar()->showMessage((!saved_flattened_copy ? tr("Saved copy %1")
                                : extension == QStringLiteral("svg") ? tr("Saved SVG copy %1.")
                                : is_pdf_extension(extension) && effective_image_options.pdf_editable_layers
                                    ? tr("Saved PDF copy with editable layers %1.")
                                : extension == QStringLiteral("gif") && effective_image_options.gif_animate
                                    ? tr("Saved animated GIF copy %1")
                                : extension == QStringLiteral("webp") && effective_image_options.webp_animate
                                    ? tr("Saved animated WebP copy %1")
                                : extension == QStringLiteral("jxl") && effective_image_options.jxl_animate
                                    ? tr("Saved animated JPEG XL copy %1")
                                    : tr("Saved flattened copy %1"))
                                   .arg(path) +
                               export_notes_suffix);
      play_theme_sound(ThemeSoundEvent::SaveDone);
      return true;
    }
    statusBar()->showMessage(
        (effective_image_options.export_cmyk || cmyk_psd ? tr("Saved %1 in CMYK") : tr("Saved %1")).arg(path) +
        export_notes_suffix);
    play_theme_sound(ThemeSoundEvent::SaveDone);
    if (linked_external_child) {
      if (auto* saved_session = session_with_id(saving_session_id)) {
        refresh_external_smart_object_after_save(*saved_session);
      }
    }
    return true;
  } catch (const std::exception& error) {
    if (unattended_automation()) {
      fprintf(stderr, "Save failed: %s (%s)\n", error.what(), path.toUtf8().constData());
    } else {
      show_critical_message(this, tr("Save failed"), translated_file_message(error.what()),
                            QStringLiteral("saveFailedMessageBox"));
    }
  }
  return false;
}

void MainWindow::export_flat_image() {
  if (!has_active_document()) {
    show_status_error(tr("No document"));
    return;
  }
  finish_active_text_editor();
  if (animation_preview_window_ != nullptr) animation_preview_window_->stop_playback_for(&document());
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
    const bool svg_export = extension == QStringLiteral("svg");
    std::optional<ImageSaveOptions> image_options;
    if (is_pdf_extension(extension)) {
      // The one PDF Options dialog (docs/pdf.md), like Save As.
      const auto choice = prompt_pdf_options(path, /*export_pages*/ false);
      if (!choice.has_value()) {
        return;
      }
      auto pdf_options = image_save_defaults_for_document();
      apply_pdf_choice_to_save_options(*choice, pdf_options);
      image_options = std::move(pdf_options);
    } else if (!is_photoshop_document_extension(extension) && !svg_export) {
      auto defaults = image_save_defaults_for_document();
      defaults.offer_animation = std::as_const(document()).layers().size() >= 2;
      defaults.has_visible_frames = has_visible_top_level_layer(std::as_const(document()));
      // for_export adds the shared Export section (resize, pixel-art scale, transparency,
      // trim, reveal) to every raster format's options (a section-only dialog for formats
      // with no other options). SVG has no raster options: vectors scale client-side.
      const QSize document_size(std::as_const(document()).width(), std::as_const(document()).height());
      if (extension == QStringLiteral("gif") && std::as_const(document()).layers().size() >= 2) {
        // Animation-or-flatten plus the delay, replacing the section-only dialog.
        image_options = prompt_gif_save_options(this, defaults, /*offer_flatten_choice*/ true,
                                                /*for_export*/ true,
                                                has_visible_top_level_layer(std::as_const(document())),
                                                document_size);
      } else {
        image_options = prompt_image_save_options(this, extension, defaults, /*for_export*/ true, document_size,
                                                  &std::as_const(document()));
      }
      if (!image_options.has_value()) {
        return;
      }
    }
    const auto effective_image_options = image_options.value_or(image_save_defaults_for_document());
    QString export_notes_suffix;
    if (is_photoshop_document_extension(extension)) {
      psd::DocumentIo::write_flat_rgb8_file(document(), to_filesystem_path(path));
    } else if (svg_export) {
      // The same structure-preserving writer as Save As: shape layers export
      // as real vectors even from the "flat" export flow.
      std::vector<std::string> svg_notices;
      std::optional<Document> narrowed;
      svg::DocumentIo::write_file(document_for_8bit_writer(document(), narrowed), to_filesystem_path(path),
                                  &svg_notices);
      export_notes_suffix = export_notes_suffix_for(svg_notices);
    } else {
      std::vector<std::string> writer_notices;
      // The Saving dialog, like Save: the export writes a snapshot on a worker.
      const Document snapshot = std::as_const(document());
      run_save_write(path, /*on_worker*/ true, /*show_progress*/ true,
                     [&snapshot, &path, &extension, &effective_image_options, &writer_notices](SaveProgress& progress) {
                       const ScopedSaveProgress scope(progress);
                       write_flat_image_file(snapshot, path, extension, effective_image_options, &writer_notices);
                     });
      export_notes_suffix = export_notes_suffix_for(writer_notices);
    }
    offer_browser_download_for_saved_file(path);
    if (!is_photoshop_document_extension(extension) && image_save_options_apply_to_extension(extension)) {
      persist_image_save_defaults(effective_image_options);
    }
    remember_save_directory_for_path(path);
    statusBar()->showMessage(tr("Exported %1").arg(path) + export_notes_suffix);
    play_theme_sound(ThemeSoundEvent::ExportDone);
    if (effective_image_options.export_reveal_in_file_explorer) {
      reveal_path_in_file_explorer(path, /*is_file*/ true);
    }
  } catch (const std::exception& error) {
    show_critical_message(this, tr("Export failed"), translated_file_message(error.what()),
                          QStringLiteral("exportFailedMessageBox"));
  }
}

// File > Export for Web (docs/web-export.md): the dialog previews every web format on a
// snapshot and hands back the bytes it encoded at the chosen settings; this asks for the
// file name and writes them, encoding here only when the dialog had no current result.
void MainWindow::export_for_web() {
  if (!has_active_document()) {
    show_status_error(tr("No document"));
    return;
  }
  finish_active_text_editor();
  if (animation_preview_window_ != nullptr) animation_preview_window_->stop_playback_for(&document());
  auto snapshot = std::make_shared<const Document>(std::as_const(document()));
  const auto choice = request_web_export(this, snapshot);
  if (!choice.has_value() || !has_active_document()) {
    return;
  }
  const auto& job = choice->job;
  const auto extension = web_export_extension(job.format);
  const char* filter_name = job.format == WebExportFormat::Png    ? QT_TRANSLATE_NOOP("QObject", "PNG Image")
                            : job.format == WebExportFormat::Webp ? QT_TRANSLATE_NOOP("QObject", "WebP Image")
                            : job.format == WebExportFormat::Gif  ? QT_TRANSLATE_NOOP("QObject", "GIF Image")
                                                                  : QT_TRANSLATE_NOOP("QObject", "JPEG Image");
  const auto filter = job.format == WebExportFormat::Jpeg
                          ? QStringLiteral("%1 (*.jpg *.jpeg)").arg(QCoreApplication::translate("QObject", filter_name))
                          : QStringLiteral("%1 (*.%2)").arg(QCoreApplication::translate("QObject", filter_name), extension);
  const auto base_name = QFileInfo(session().title.isEmpty() ? tr("Untitled") : session().title).completeBaseName();
  auto path = get_save_file_name(this, tr("Export for Web"),
                                 file_dialog_initial_path(QString(), base_name + QLatin1Char('.') + extension), filter,
                                 nullptr, QStringLiteral("exportForWebFileDialog"));
  if (path.isEmpty()) {
    return;
  }
  // The extension follows the format the dialog encoded.
  const auto suffix = QFileInfo(path).suffix().toLower();
  const bool suffix_matches = suffix == extension || (job.format == WebExportFormat::Jpeg && suffix == QStringLiteral("jpeg"));
  if (!suffix_matches) {
    path += QLatin1Char('.') + extension;
  }
  try {
    std::vector<std::string> notices;
    if (choice->bytes != nullptr) {
      notices = choice->notices;
      write_encoded_image_file(path, *choice->bytes);
    } else {
      run_save_write(path, /*on_worker*/ true, /*show_progress*/ true,
                     [&snapshot, &job, &path, &notices](SaveProgress& progress) {
                       const ScopedSaveProgress scope(progress);
                       std::optional<Document> narrowed;
                       const auto& source = document_for_8bit_writer(*snapshot, narrowed);
                       const auto prepared =
                           job.animate ? nullptr
                                       : WebExportPreviewEngine::prepare(source, job.geometry, job.convert_to_srgb,
                                                                         &notices);
                       const auto result = WebExportPreviewEngine::encode(source, job, prepared);
                       write_encoded_image_file(path, *result.bytes);
                     });
    }
    offer_browser_download_for_saved_file(path);
    remember_save_directory_for_path(path);
    statusBar()->showMessage(tr("Exported %1").arg(path) + export_notes_suffix_for(notices));
    if (choice->settings.reveal_when_done) {
      reveal_path_in_file_explorer(path, /*is_file*/ true);
    }
  } catch (const std::exception& error) {
    show_critical_message(this, tr("Export failed"), translated_file_message(error.what()),
                          QStringLiteral("exportFailedMessageBox"));
  }
}

// File > Export Multi-Page PDF: the pages are whole open documents or the top-level
// groups of the active one; the dialog picks, this resolves sessions to documents and
// writes through write_multipage_pdf_file.
void MainWindow::export_multipage_pdf() {
  if (!has_active_document()) {
    show_status_error(tr("No document"));
    return;
  }
  finish_active_text_editor();
  // The file first, then PDF Options, the same order as Save As.
  const auto base_name = QFileInfo(session().title.isEmpty() ? tr("Untitled") : session().title).completeBaseName();
  auto path = get_save_file_name(this, tr("Export Multi-Page PDF"),
                                 file_dialog_initial_path(QString(), base_name + QStringLiteral(".pdf")),
                                 QStringLiteral("%1 (*.pdf)").arg(QCoreApplication::translate("QObject", "PDF Document")),
                                 nullptr, QStringLiteral("exportMultiPagePdfFileDialog"));
  if (path.isEmpty() || !has_active_document()) {
    return;
  }
  if (!path.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive)) {
    path += QStringLiteral(".pdf");
  }
  const auto choice = prompt_pdf_options(path, /*export_pages*/ true);
  if (!choice.has_value() || !has_active_document()) {
    return;
  }

  try {
    std::vector<Document> group_pages;
    std::vector<const Document*> pages;
    if (choice->source == MultiPagePdfSource::TopLevelGroups) {
      group_pages = documents_for_top_level_groups(std::as_const(document()), choice->include_ungrouped_layers,
                                                   choice->include_hidden_groups);
      pages.reserve(group_pages.size());
      for (const auto& page : group_pages) {
        pages.push_back(&page);
      }
    } else if (choice->source == MultiPagePdfSource::Artboards) {
      group_pages = documents_for_artboards(std::as_const(document()));
      pages.reserve(group_pages.size());
      for (const auto& page : group_pages) {
        pages.push_back(&page);
      }
    } else if (choice->source == MultiPagePdfSource::Canvas) {
      pages.push_back(&std::as_const(document()));
    } else {
      for (const auto session_id : choice->session_ids) {
        if (const auto* page_session = session_with_id(session_id); page_session != nullptr) {
          pages.push_back(&page_session->document);
        }
      }
    }
    // The sources above are already one document per page; only an open document may
    // still split into its artboards (the Canvas source is the whole canvas, unsplit).
    auto write_options = choice->options;
    write_options.pages =
        choice->source == MultiPagePdfSource::Canvas ? PdfPageSource::Canvas : PdfPageSource::Artboards;
    const int page_count = static_cast<int>(pages.size());
    QProgressDialog progress(tr("Writing page %1 of %2...").arg(1).arg(page_count), tr("Cancel"), 0, page_count,
                             this);
    progress.setObjectName(QStringLiteral("multiPagePdfProgressDialog"));
    progress.setWindowTitle(tr("Export Multi-Page PDF"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    remember_dialog_position(progress);
    progress.setValue(0);
    std::vector<std::string> writer_notices;
    const bool completed =
        write_multipage_pdf_file(pages, path, write_options, &writer_notices, [&progress](int page, int count) {
          progress.setLabelText(tr("Writing page %1 of %2...").arg(page).arg(count));
          progress.setValue(page - 1);
          QApplication::processEvents(QEventLoop::AllEvents);
          return !progress.wasCanceled();
        });
    progress.close();
    if (!completed) {
      statusBar()->showMessage(tr("Export cancelled"));
      return;
    }
    offer_browser_download_for_saved_file(path);
    remember_save_directory_for_path(path);
    statusBar()->showMessage(tr("Exported %n page(s) to %1", nullptr, page_count).arg(path) +
                             export_notes_suffix_for(writer_notices));
    play_theme_sound(ThemeSoundEvent::ExportDone);
  } catch (const std::exception& error) {
    show_critical_message(this, tr("Export failed"), translated_file_message(error.what()),
                          QStringLiteral("exportFailedMessageBox"));
  }
}

// File > Export Documents to Folder: the checked open documents, in the dialog's
// order, as numbered image files; the dialog picks, this resolves sessions to
// documents and writes them: PSD/PSB and Aseprite through the layered writers Save
// As uses, everything else flattened through write_flat_image_file.
void MainWindow::export_documents_to_folder() {
  if (!has_active_document()) {
    show_status_error(tr("No document"));
    return;
  }
  finish_active_text_editor();
  std::vector<DocumentOrderEntry> entries;
  entries.reserve(sessions_.size());
  for (const auto& candidate : sessions_) {
    if (candidate != nullptr) {
      entries.push_back({candidate->title.isEmpty() ? tr("Untitled") : candidate->title, candidate->session_id});
    }
  }
  const auto choice = run_export_documents_folder_dialog(this, entries, session().session_id,
                                                         export_documents_format_choices(), last_save_directory());
  if (!choice.has_value()) {
    return;
  }
  const auto written = export_document_sessions_to_folder(choice->session_ids, choice->folder, choice->extension,
                                                          choice->naming, choice->existing_files);
  if (!written.has_value()) {
    return;
  }
  statusBar()->showMessage(
      tr("Exported %1 images to %2").arg(written->size()).arg(QDir::toNativeSeparators(choice->folder)));
}

std::optional<QStringList> MainWindow::export_document_sessions_to_folder(
    const std::vector<std::int64_t>& session_ids, const QString& folder, const QString& extension,
    const ImageSequenceNaming& naming, ExportDocumentsExistingFiles existing_files) {
  std::vector<const Document*> pages;
  pages.reserve(session_ids.size());
  for (const auto session_id : session_ids) {
    if (auto* page_session = session_with_id(session_id); page_session != nullptr) {
      pages.push_back(&std::as_const(page_session->document));
    }
  }
  return export_page_documents_to_folder(pages, {}, folder, extension, naming, existing_files,
                                         tr("Export Documents to Folder"));
}

bool MainWindow::export_documents_format_supported(const QString& extension) {
  const auto choices = export_documents_format_choices();
  return std::any_of(choices.begin(), choices.end(),
                     [&extension](const ExportDocumentsFormatChoice& choice) { return choice.extension == extension; });
}

void MainWindow::export_artboards_to_files() {
  if (!has_active_document()) {
    show_status_error(tr("No document"));
    return;
  }
  finish_active_text_editor();
  const auto artboards = document_artboards(std::as_const(document()));
  if (artboards.empty()) {
    show_status_error(tr("This document has no artboards"));
    return;
  }
  std::vector<DocumentOrderEntry> entries;
  entries.reserve(artboards.size());
  for (const auto* artboard : artboards) {
    entries.push_back({QString::fromStdString(artboard->name()), static_cast<std::int64_t>(artboard->id())});
  }
  // "Poster - Artboard 1.png": the document's name, then the artboard's.
  auto base = QFileInfo(session().title).completeBaseName();
  if (base.isEmpty()) {
    base = tr("Untitled");
  }
  const auto active = canvas_ != nullptr ? canvas_->selected_root_artboard_id() : std::nullopt;
  const auto choice = run_export_documents_folder_dialog(
      this, entries, active.has_value() ? static_cast<std::int64_t>(*active) : entries.front().session_id,
      export_documents_format_choices(), last_save_directory(), ExportDocumentsFolderSource::Artboards,
      base + QStringLiteral(" - "));
  if (!choice.has_value()) {
    return;
  }
  std::vector<LayerId> ids;
  ids.reserve(choice->session_ids.size());
  for (const auto id : choice->session_ids) {
    ids.push_back(static_cast<LayerId>(id));
  }
  const auto written = export_artboards_to_folder(std::as_const(document()), ids, choice->folder, choice->extension,
                                                  choice->naming, choice->existing_files);
  if (!written.has_value()) {
    return;
  }
  statusBar()->showMessage(
      tr("Exported %1 images to %2").arg(written->size()).arg(QDir::toNativeSeparators(choice->folder)));
}

std::optional<QStringList> MainWindow::export_artboards_to_folder(const Document& source,
                                                                  const std::vector<LayerId>& artboard_ids,
                                                                  const QString& folder, const QString& extension,
                                                                  const ImageSequenceNaming& naming,
                                                                  ExportDocumentsExistingFiles existing_files,
                                                                  QString* script_error) {
  std::vector<Document> derived;
  std::vector<QString> names;
  derived.reserve(artboard_ids.size());
  for (const auto id : artboard_ids) {
    const auto* artboard = source.find_layer(id);
    if (artboard == nullptr || !layer_is_artboard(*artboard)) {
      continue;
    }
    derived.push_back(document_for_artboard(source, id));
    names.push_back(naming.prefix + QString::fromStdString(artboard->name()));
  }
  std::vector<const Document*> pages;
  pages.reserve(derived.size());
  for (const auto& page : derived) {
    pages.push_back(&page);
  }
  ImageSequenceNaming named = naming;
  named.use_layer_names = true;
  return export_page_documents_to_folder(pages, names, folder, extension, named, existing_files,
                                         tr("Export Artboards to Files"), script_error);
}

std::optional<QStringList> MainWindow::export_page_documents_to_folder(
    const std::vector<const Document*>& pages, const std::vector<QString>& base_names, const QString& folder,
    const QString& extension, const ImageSequenceNaming& naming, ExportDocumentsExistingFiles existing_files,
    const QString& title, QString* script_error) {
  if (pages.empty() || folder.trimmed().isEmpty()) {
    return std::nullopt;
  }
  // A script call: no dialogs at all, and a failure comes back as text to throw.
  const bool unattended = script_error != nullptr;
  const bool by_name = naming.use_layer_names && base_names.size() == pages.size();
  try {
    // The dialog already created the folder; direct callers (tests) may not have.
    QDir().mkpath(folder);
    const QDir directory(folder);
    // prefix + zero-padded number + "." + extension (the image_sequence_file_names
    // formatting, one name at a time so Add mode can skip past existing files).
    const auto file_name_for = [&naming, &extension](int number) {
      ImageSequenceNaming one = naming;
      one.use_layer_names = false;
      one.start = number;
      return image_sequence_file_names(std::vector<QString>(1), one, extension).front();
    };
    QStringList targets;
    targets.reserve(static_cast<qsizetype>(pages.size()));
    const auto named_files = by_name ? image_sequence_file_names(base_names, naming, extension) : QStringList();
    if (by_name && existing_files == ExportDocumentsExistingFiles::AddNumbering) {
      // Never overwrite: a name already in the folder gets " 2", " 3"... (the names
      // within the batch are already distinct).
      for (const auto& name : named_files) {
        const QFileInfo info(name);
        const auto stem = info.completeBaseName();
        const auto suffix = info.suffix().isEmpty() ? QString() : QStringLiteral(".") + info.suffix();
        auto candidate = name;
        for (int copy = 2; QFileInfo::exists(directory.filePath(candidate)) ||
                           targets.contains(directory.filePath(candidate), Qt::CaseInsensitive);
             ++copy) {
          candidate = stem + QStringLiteral(" %1").arg(copy) + suffix;
        }
        targets.push_back(directory.filePath(candidate));
      }
    } else if (existing_files == ExportDocumentsExistingFiles::AddNumbering) {
      // Never overwrite: each file takes the next free number, so a batch lands
      // after whatever the folder already holds.
      int number = naming.start;
      for (std::size_t i = 0; i < pages.size(); ++i) {
        while (QFileInfo::exists(directory.filePath(file_name_for(number)))) {
          ++number;
        }
        targets.push_back(directory.filePath(file_name_for(number)));
        ++number;
      }
    } else {
      QString first_conflict;
      for (std::size_t i = 0; i < pages.size(); ++i) {
        const auto name = by_name ? named_files.at(static_cast<qsizetype>(i))
                                  : file_name_for(naming.start + static_cast<int>(i));
        const auto target = directory.filePath(name);
        if (first_conflict.isEmpty() && QFileInfo::exists(target)) {
          first_conflict = name;
        }
        targets.push_back(target);
      }
      // One confirmation for the whole batch, before anything is written; the
      // first colliding name stands in for the rest.
      if (!first_conflict.isEmpty() && !unattended) {
        const auto answer = show_warning_message(
            this, title, tr("%1 already exists. Overwrite existing files?").arg(first_conflict),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No, QStringLiteral("exportDocumentsOverwriteMessageBox"));
        if (answer != QMessageBox::Yes) {
          return std::nullopt;
        }
      }
    }

    // Per-format remembered defaults, applied silently; the export scale is pinned
    // to 1x so the shared saveOptions/exportScale can never rescale a page batch.
    auto image_options = load_image_save_option_defaults();
    image_options.export_scale = 1;
    if (is_pdf_extension(extension) &&
        std::any_of(pages.begin(), pages.end(),
                    [](const Document* page) { return flat_save_discards_layers(*page); })) {
      // Flatten or keep layers editable, decided once for the whole batch the way Save As
      // and Export Flat Image decide it: the remembered policy, or the question. A page
      // that is already one plain pixel layer comes out the same either way.
      const auto pdf_editable_layers =
          resolve_pdf_layer_choice(/*for_export*/ true, /*allow_prompt*/ !unattended && !unattended_automation());
      if (!pdf_editable_layers.has_value()) {
        return std::nullopt;
      }
      image_options.pdf_editable_layers = *pdf_editable_layers;
    }

    // On the UI thread with an event pump per file: the pages are live session
    // documents, so no worker may read them while the window stays interactive.
    const int count = static_cast<int>(pages.size());
    std::optional<QProgressDialog> progress;
    if (!unattended) {
      progress.emplace(tr("Writing image %1 of %2...").arg(1).arg(count), tr("Cancel"), 0, count, this);
      progress->setObjectName(QStringLiteral("exportDocumentsProgressDialog"));
      progress->setWindowTitle(title);
      progress->setWindowModality(Qt::WindowModal);
      progress->setMinimumDuration(0);
      progress->setAutoClose(false);
      progress->setAutoReset(false);
      remember_dialog_position(*progress);
      progress->setValue(0);
    }
    QStringList written;
    for (int index = 0; index < count; ++index) {
      // Scripts get no event pump: their documents must not change mid-export.
      if (progress.has_value()) {
        progress->setLabelText(tr("Writing image %1 of %2...").arg(index + 1).arg(count));
        progress->setValue(index);
        QApplication::processEvents(QEventLoop::AllEvents);
      }
      if (progress.has_value() && progress->wasCanceled()) {
        // Files already written stay: a partial page set is still useful.
        progress->close();
        statusBar()->showMessage(tr("Export cancelled after %1 of %2 images").arg(index).arg(count));
        return std::nullopt;
      }
      const auto& page_document = *pages[static_cast<std::size_t>(index)];
      const auto& target = targets.at(index);
      if (is_photoshop_document_extension(extension)) {
        // Layered, like Save As: a page batch of PSDs keeps every document's layer tree.
        psd::DocumentIo::write_layered_rgb8_file(page_document, to_filesystem_path(target),
                                                 psd::WriteOptions{extension == QStringLiteral("psb")});
      } else if (extension == QStringLiteral("aseprite") || extension == QStringLiteral("ase")) {
        // Layered too; unattended, so the Save As Fill Opacity warning does not apply.
        std::optional<Document> narrowed;
        aseprite::DocumentIo::write_file(document_for_8bit_writer(page_document, narrowed), to_filesystem_path(target));
      } else {
        write_flat_image_file(page_document, target, extension, image_options);
      }
      written.push_back(target);
    }
    if (progress.has_value()) {
      progress->setValue(count);
      progress->close();
      remember_save_directory_for_path(written.front());
    }
    return written;
  } catch (const std::exception& error) {
    if (unattended) {
      *script_error = translated_file_message(error.what());
    } else {
      show_critical_message(this, tr("Export failed"), translated_file_message(error.what()),
                            QStringLiteral("exportFailedMessageBox"));
    }
    return std::nullopt;
  }
}

void MainWindow::page_setup() {
  run_page_setup_dialog(this, &print_page_layout_);
  store_print_page_layout(print_page_layout_);
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
  if (run_print_dialog(this, document(), session().title, selection_bounds, &print_page_layout_)) {
    store_print_page_layout(print_page_layout_);
    statusBar()->showMessage(tr("Print output created"));
  }
}

bool MainWindow::can_install_update(const UpdateInfo& update) const {
  // An AppImage replaces its own file (docs/auto-update.md, "AppImage"); everything else
  // needs the Windows installer's copy.
  if (const auto appimage = appimage_file_path(); !appimage.isEmpty()) {
    return update_has_appimage_install(update) &&
           self_update_override_for_tests_.value_or(appimage_can_be_replaced(appimage));
  }
  return update_has_installer(update) && self_update_override_for_tests_.value_or(installed_copy_can_self_update());
}

void MainWindow::download_and_install_update(const UpdateInfo& update) {
#if defined(Q_OS_WASM)
  Q_UNUSED(update);
#else
  // The file this update installs: the Windows installer, or for an AppImage the new
  // AppImage, downloaded as a hidden file beside the running one so the final rename stays
  // in one folder.
  const auto appimage = appimage_file_path();
  const bool appimage_update = !appimage.isEmpty();
  const auto file_name = appimage_update ? appimage_update_download_name(appimage, update.version)
                                         : update_installer_file_name();
  const auto file_url = appimage_update ? update.appimage_install_url : update.installer_url;
  const auto file_size = appimage_update ? update.appimage_install_size : update.installer_size;
  const auto file_sha256 = appimage_update ? update.appimage_install_sha256 : update.installer_sha256;
  QUrl browser_url = update.download_url;
  if (appimage_update) {
    browser_url = update.appimage_url.isEmpty() ? QUrl(QString::fromLatin1(kAppImageDownloadUrl)) : update.appimage_url;
  }
  const auto megabytes = [](qint64 bytes) { return static_cast<int>((bytes + 500000) / 1000000); };
  const auto total_mb = megabytes(file_size);
  const auto progress_text = [&](qint64 received) {
    return tr("Downloading Patchy %1... %2 of %3 MB").arg(update.version).arg(megabytes(received)).arg(total_mb);
  };
  QString folder = update_download_folder_for_tests_;
  if (folder.isEmpty()) {
    folder = appimage_update ? QFileInfo(appimage).absolutePath() : update_download_folder(update.version);
  }
  std::optional<ModelDownloadDialog> dialog;
  dialog.emplace(this);
  dialog->setObjectName(QStringLiteral("updateDownloadDialog"));
  dialog->setWindowTitle(tr("Update Available"));
  dialog->set_heading(progress_text(0));
  remember_dialog_position(*dialog);
  dialog->show();
  ModelDownloadResult result;
  bool done = false;
  QEventLoop wait_loop;
  // Pinned by size and SHA-256 and written through QSaveFile; a file already downloaded
  // for this version (an update cancelled at the save prompt) is reused.
  auto job = start_model_download(this, folder, {DownloadableModelFile{file_name, file_url, file_size, file_sha256}},
      [&](const ModelDownloadProgress& progress) {
        dialog->set_heading(progress_text(progress.received));
        dialog->set_progress(progress);
      },
      [&](ModelDownloadResult finished) {
        result = std::move(finished);
        done = true;
        wait_loop.quit();
      });
  connect(&*dialog, &QDialog::rejected, this, [&job] { job->cancel(); });
  if (!done) {
    wait_loop.exec();
  }
  dialog.reset();
  if (result.cancelled) {
    statusBar()->showMessage(tr("Download cancelled"));
    return;
  }
  if (!result.ok) {
    QMessageBox failure(QMessageBox::Warning, tr("Update Available"),
                        tr("The update could not be downloaded: %1").arg(result.error), QMessageBox::NoButton, this);
    failure.setObjectName(QStringLiteral("updateDownloadFailedMessageBox"));
    auto* browser_button = failure.addButton(tr("Download"), QMessageBox::AcceptRole);
    failure.addButton(tr("Not Now"), QMessageBox::RejectRole);
    failure.setDefaultButton(browser_button);
    exec_dialog(failure);
    if (failure.clickedButton() == browser_button && !QDesktopServices::openUrl(browser_url)) {
      show_status_error(tr("Could not open the download link"));
    }
    return;
  }
  // closeEvent asks about unsaved work as on any quit, then starts the installer, which
  // waits for this process before it replaces anything (an AppImage: puts the new file in
  // place and starts it once this process has exited).
  pending_update_install_ = UpdateInstallerLaunch{QDir(folder).filePath(file_name),
                                                  QCoreApplication::applicationDirPath(),
                                                  QCoreApplication::applicationPid(),
                                                  file_size,
                                                  file_sha256,
                                                  browser_url,
                                                  appimage};
  if (!close()) {
    if (pending_update_install_.has_value()) {
      pending_update_install_.reset();
      statusBar()->showMessage(tr("The update was not installed because Patchy stayed open"));
    }
    return;
  }
  // The installer is running (or the AppImage is replaced) and waits for this process. Do
  // not leave the quit to Qt's posted Quit event, which a window shown in the meantime
  // cancels. Before app.exec() (an update accepted while a command-line file was still
  // opening) this cannot end anything; main.cpp then skips exec() for the hidden window.
  request_application_exit();
#endif
}

void MainWindow::request_application_exit() {
  if (application_exit_hook_for_tests_) {
    application_exit_hook_for_tests_();
    return;
  }
  // Unconditional, unlike QCoreApplication::quit(): the Quit event handler refuses while any
  // top-level window is visible, and a stray tooltip or float must not keep the process alive.
  QCoreApplication::exit(0);
}

void MainWindow::show_update_launch_failure(const UpdateLaunchResult& result, const QUrl& browser_download_url) {
  QString reason;
  const bool appimage = !appimage_file_path().isEmpty();
  switch (result.failure) {
    case UpdateLaunchFailure::None:
      return;
    case UpdateLaunchFailure::InstallerMissing:
      reason = appimage ? tr("The downloaded AppImage is no longer on this computer.")
                        : tr("The downloaded installer is no longer on this computer.");
      break;
    case UpdateLaunchFailure::ChecksumMismatch:
      reason = appimage ? tr("The downloaded AppImage does not match the checksum published for this update.")
                        : tr("The downloaded installer does not match the checksum published for this update.");
      break;
    case UpdateLaunchFailure::ReplaceFailed:
      reason = tr("The new AppImage could not be put in place of this one.");
      break;
    case UpdateLaunchFailure::NotSigned:
      reason = tr("The downloaded installer is not signed by %1, the publisher of this copy of Patchy.")
                   .arg(result.expected_signer);
      break;
    case UpdateLaunchFailure::WrongSigner:
      reason = tr("The downloaded installer is signed by %1, not by %2, the publisher of this copy of Patchy.")
                   .arg(result.installer_signer, result.expected_signer);
      break;
    case UpdateLaunchFailure::StartFailed:
      reason = tr("Windows could not start the installer.");
      break;
  }
  QMessageBox failure(QMessageBox::Warning, tr("Update Available"),
                      tr("The update was not installed.\n\n%1\n\nYou can still download it in your browser.").arg(reason),
                      QMessageBox::NoButton, this);
  failure.setObjectName(QStringLiteral("updateInstallFailedMessageBox"));
  failure.setTextFormat(Qt::PlainText);
  auto* browser_button = failure.addButton(tr("Download"), QMessageBox::AcceptRole);
  failure.addButton(tr("Not Now"), QMessageBox::RejectRole);
  failure.setDefaultButton(browser_button);
  exec_dialog(failure);
  if (failure.clickedButton() == browser_button && !QDesktopServices::openUrl(browser_download_url)) {
    show_status_error(tr("Could not open the download link"));
  }
}

void MainWindow::show_update_available(const UpdateInfo& update) {
  // Windows only, on a copy the installer made (docs/auto-update.md): Patchy fetches and
  // runs the installer itself. Everything else gets the per-artifact advice below.
  const bool self_update = can_install_update(update);
  // The install advice is artifact-specific: Windows ships an installer exe, macOS a
  // drag-to-Applications DMG, Linux a Flatpak that updates from its repository.
#if defined(Q_OS_MACOS)
  const auto update_text = tr("Patchy %1 is available. You are using version %2.\n\n"
                              "Download the DMG, quit Patchy, and drag the new Patchy into Applications.")
                               .arg(update.version, QStringLiteral(PATCHY_VERSION));
#elif defined(Q_OS_LINUX)
  // Every install from 1.05 on has the Patchy repository as its origin: the flatpakref
  // names it, and the bundle carries it as --repo-url, which also rewrites the origin of
  // an older bundle install it is installed over (packaging/linux/README.md). So one
  // command updates them all. --user matches every documented install command (no
  // polkit/root prompt, GitHub issue 14); without it flatpak 1.14 also fails outright on
  // a machine that has no system-wide installation ("While opening repository").
  const auto install_command = QStringLiteral("flatpak update --user -y com.rtsoft.patchy");
  // An AppImage is one file (docs/appimage.md). With the manifest's pinned AppImage and a
  // writable folder it replaces itself (docs/auto-update.md, "AppImage"); otherwise its
  // prompt names the file and downloads the AppImage, not the Flatpak bundle the linux
  // entry's download_url points at.
  const auto appimage = appimage_file_path();
  QString update_text;
  if (appimage.isEmpty()) {
    update_text = tr("Patchy %1 is available. You are using version %2.\n\n"
                     "To update, paste this into a terminal:\n\n%3")
                      .arg(update.version, QStringLiteral(PATCHY_VERSION), install_command);
  } else if (self_update) {
    update_text = tr("Patchy %1 is available. You are using version %2.\n\n"
                     "Patchy can download the update, replace this AppImage, and start again. "
                     "You will be asked about unsaved work first.")
                      .arg(update.version, QStringLiteral(PATCHY_VERSION));
  } else {
    update_text = tr("Patchy %1 is available. You are using version %2.\n\n"
                     "Download the new AppImage, quit Patchy, and replace this file with it:\n\n%3")
                      .arg(update.version, QStringLiteral(PATCHY_VERSION), appimage);
  }
  QUrl download_url = update.download_url;
  if (!appimage.isEmpty()) {
    download_url = update.appimage_url.isEmpty() ? QUrl(QString::fromLatin1(kAppImageDownloadUrl))
                                                 : update.appimage_url;
  }
#else
  const auto update_text =
      (self_update ? tr("Patchy %1 is available. You are using version %2.\n\n"
                        "Patchy can download the update, close, install it, and start again. "
                        "You will be asked about unsaved work first.")
                   : tr("Patchy %1 is available. You are using version %2.\n\n"
                        "Save your work and close Patchy before running the installer."))
          .arg(update.version, QStringLiteral(PATCHY_VERSION));
#endif
  QMessageBox dialog(QMessageBox::Information, tr("Update Available"), update_text, QMessageBox::NoButton, this);
  dialog.setObjectName(QStringLiteral("updateAvailableMessageBox"));
  dialog.setTextInteractionFlags(Qt::TextSelectableByMouse);
  QPushButton* copy_button = nullptr;
#if defined(Q_OS_LINUX)
  if (appimage.isEmpty()) {
    copy_button = dialog.addButton(tr("Copy Command"), QMessageBox::AcceptRole);
    copy_button->setObjectName(QStringLiteral("updateCopyCommandButton"));
    dialog.setDefaultButton(copy_button);
  }
#endif
  QAbstractButton* install_button = nullptr;
  QAbstractButton* download_button = nullptr;
  if (self_update) {
    auto* button = dialog.addButton(tr("Update Now"), QMessageBox::AcceptRole);
    button->setObjectName(QStringLiteral("updateInstallNowButton"));
    dialog.setDefaultButton(button);
    install_button = button;
  } else {
    auto* button = dialog.addButton(tr("Download"), QMessageBox::AcceptRole);
    button->setObjectName(QStringLiteral("updateDownloadButton"));
    if (copy_button == nullptr) {
      dialog.setDefaultButton(button);
    }
    download_button = button;
  }
  dialog.addButton(tr("Not Now"), QMessageBox::RejectRole);
  // Silences the startup prompt for this version only (GitHub issue 92): a later release
  // asks again, and the start panel and About window keep reporting the update.
  auto* skip_check = new QCheckBox(tr("Don't ask again about version %1").arg(update.version), &dialog);
  skip_check->setObjectName(QStringLiteral("updateSkipVersionCheck"));
  dialog.setCheckBox(skip_check);

  exec_dialog(dialog);
  if (install_button != nullptr && dialog.clickedButton() == install_button) {
    download_and_install_update(update);
    return;
  }
  if (skip_check->isChecked()) {
    set_stored_update_skipped_version(update.version);
  }
#if defined(Q_OS_LINUX)
  if (copy_button != nullptr && dialog.clickedButton() == copy_button) {
    if (auto* clipboard = QApplication::clipboard(); clipboard != nullptr) {
      clipboard->setText(install_command);
    }
    statusBar()->showMessage(tr("Install command copied to the clipboard"));
    return;
  }
#else
  const auto& download_url = update.download_url;
#endif
  if (download_button != nullptr && dialog.clickedButton() == download_button &&
      !QDesktopServices::openUrl(download_url)) {
    show_status_error(tr("Could not open the download link"));
  }
}

void MainWindow::show_connector_restart_notice(const QStringList& apps) {
  if (apps.isEmpty()) {
    return;
  }
  QMessageBox dialog(QMessageBox::Information, tr("Patchy Updated"),
                     tr("Patchy was updated to version %1.\n\n"
                        "These apps are still connected to the old version: %2.\n\n"
                        "Restart them to finish updating.")
                         .arg(QStringLiteral(PATCHY_VERSION), apps.join(QStringLiteral(", "))),
                     QMessageBox::Ok, this);
  dialog.setObjectName(QStringLiteral("updateConnectorNoticeMessageBox"));
  dialog.setTextFormat(Qt::PlainText);
  exec_dialog(dialog);
}

void MainWindow::begin_startup_update_check() {
#ifdef Q_OS_WASM
  // The web build updates by redeploying the site; the GitHub manifest fetch
  // would only fail CORS and surface a network error on the start panel.
  return;
#endif
  // The installer's update mode starts this copy with the AI apps it found still running
  // the previous connector (packaging/windows/InstallPatchy.ps1).
  if (const auto clients = qEnvironmentVariable("PATCHY_UPDATE_CONNECTOR_CLIENTS"); !clients.isEmpty()) {
    qunsetenv("PATCHY_UPDATE_CONNECTOR_CLIENTS");  // one notice; nothing this copy starts inherits it
    QTimer::singleShot(0, this, [this, apps = clients.split(QLatin1Char('|'), Qt::SkipEmptyParts)] {
      show_connector_restart_notice(apps);
    });
  }
  if (!update_checks_available()) {
    return;
  }
  // A one-click update leaves the installer for this version in the temp folder. It may
  // still be exiting when the copy it installed starts, so the cleanup waits a little.
  QTimer::singleShot(30000, this, [] { remove_update_download(QStringLiteral(PATCHY_VERSION)); });
  // An AppImage update cancelled at the save prompt leaves its download beside the file;
  // once this version runs (updated some other way), it is no longer needed.
  if (const auto appimage = appimage_file_path(); !appimage.isEmpty()) {
    remove_appimage_update_download(appimage, QStringLiteral(PATCHY_VERSION));
  }
  {
    auto settings = app_settings();
    if (!settings.value(QStringLiteral("updates/checkOnStartup"), true).toBool()) {
      return;
    }
  }
  // The chosen interval (Preferences, GitHub issue 92): every launch by default.
  if (!startup_update_check_due(stored_update_check_interval_days(), stored_update_last_check_secs(),
                                QDateTime::currentSecsSinceEpoch())) {
    return;
  }
  if (start_panel_ != nullptr) {
    start_panel_->set_update_status(QObject::tr("Checking for updates..."));
  }
  // request_update_check drops the callback if this owner is destroyed first, so `this`
  // stays safe to capture. The status lands on the panel even while it is hidden (a file
  // was opened at startup): it shows if the panel reappears after the last document closes.
  request_update_check(this, QStringLiteral(PATCHY_VERSION), [this](UpdateCheckResult result) {
    // Only an answer starts the interval: a launch without a network tries again next time.
    if (result.status != UpdateCheckStatus::NetworkError) {
      set_stored_update_last_check_secs(QDateTime::currentSecsSinceEpoch());
    }
    if (start_panel_ != nullptr) {
      // "Up to date" says nothing the version line above does not; clear the
      // "Checking..." line instead of restating the version (Seth, October 2026).
      // Updates, errors and unsupported platforms still show.
      start_panel_->set_update_status(result.status == UpdateCheckStatus::NoUpdateAvailable
                                          ? QString()
                                          : update_check_status_text(result));
    }
    if (result.update.has_value() &&
        update_prompt_wanted(result.update->version, stored_update_skipped_version())) {
      show_update_available(*result.update);
    }
  });
}

void MainWindow::load_recent_files() {
  auto settings = recent_history_settings();
  settings.sync();
  set_recent_files_from_stored(settings.value(QStringLiteral("recentFiles")).toStringList());
}

void MainWindow::set_recent_files_from_stored(QStringList stored) {
  trim_recent_files(stored);
  recent_files_stored_ = stored;
  stored.erase(std::remove_if(stored.begin(), stored.end(),
                              [this](const QString& path) {
                                return path.trimmed().isEmpty() || recent_missing_files_.contains(path);
                              }),
               stored.end());
  recent_files_ = std::move(stored);
}

// Rereads the stored lists (no disk access beyond the settings file) and
// rebuilds whichever menu's displayed list changed. Returns whether a stored
// list changed, which is when a new existence check is worth running.
bool MainWindow::reload_recent_history() {
  const auto files = recent_files_;
  const auto folders = recent_folders_;
  const auto stored_files = recent_files_stored_;
  const auto stored_folders = recent_folders_stored_;
  load_recent_files();
  load_recent_folders();
  if (files != recent_files_) rebuild_recent_files_menu();
  if (folders != recent_folders_) rebuild_recent_folders_menu();
  return stored_files != recent_files_stored_ || stored_folders != recent_folders_stored_;
}

void MainWindow::refresh_recent_history() {
  if (recent_menus_stale_) {
    // An entry was removed from an open Open Recent list; the menus waited.
    recent_menus_stale_ = false;
    rebuild_recent_files_menu();
    rebuild_recent_folders_menu();
  }
  schedule_recent_history_check(reload_recent_history());
}

// Existence checks run on a worker: a stat of a cold, spun-down or absent
// volume can take seconds, and the File menu and start panel used to pay that
// on the UI thread for every entry. Entries show until a check finds them
// missing; the stored lists keep them, so an unplugged drive's entries return
// with the drive.
void MainWindow::schedule_recent_history_check(bool force) {
  if (recent_check_in_flight_) {
    recent_check_pending_ = recent_check_pending_ || force;
    return;
  }
  if (!force && recent_check_clock_.isValid() && recent_check_clock_.elapsed() < kRecentHistoryCheckIntervalMs) {
    return;
  }
  recent_check_clock_.start();
  recent_check_in_flight_ = true;
  recent_check_pending_ = false;
  recent_confirmed_paths_.clear();
  auto* app = QApplication::instance();
  QPointer<MainWindow> window(this);
  run_tracked_background_worker([app, window, files = recent_files_stored_, folders = recent_folders_stored_] {
    // Cached kernel data only; nothing here contacts a server.
    const auto mounts = read_system_mounts();
    QSet<QString> missing_files;
    QSet<QString> missing_folders;
    for (const auto& path : files) {
      if (!path.trimmed().isEmpty() && !is_network_recent_path(path, mounts) && !QFileInfo::exists(path)) {
        missing_files.insert(path);
      }
    }
    for (const auto& dir : folders) {
      if (!dir.trimmed().isEmpty() && !is_network_recent_path(dir, mounts) && !QFileInfo(dir).isDir()) {
        missing_folders.insert(dir);
      }
    }
    if (app == nullptr) {
      return;
    }
    QMetaObject::invokeMethod(
        app,
        [window, missing_files = std::move(missing_files), missing_folders = std::move(missing_folders)]() mutable {
          if (window == nullptr) {
            return;
          }
          // A path opened or saved while the check ran exists now, whatever
          // the worker saw.
          for (const auto& path : std::as_const(window->recent_confirmed_paths_)) {
            missing_files.remove(path);
            missing_folders.remove(path);
          }
          window->recent_missing_files_ = std::move(missing_files);
          window->recent_missing_folders_ = std::move(missing_folders);
          window->recent_check_in_flight_ = false;
          // Rebuilding under an open menu would delete its live filter row; the
          // next File menu open or start-panel tick applies the result instead.
          if (QApplication::activePopupWidget() == nullptr) {
            window->reload_recent_history();
          }
          if (window->recent_check_pending_) {
            window->schedule_recent_history_check(true);
          }
        },
        Qt::QueuedConnection);
  });
}

void MainWindow::add_recent_file(QString path) {
  path = QFileInfo(path).absoluteFilePath();
  if (path.isEmpty()) {
    return;
  }
  recent_missing_files_.remove(path);
  recent_confirmed_paths_.insert(path);
  set_recent_files_from_stored(update_recent_history(QStringLiteral("recentFiles"), kMaxRecentFiles,
      [&path](QStringList& paths) { paths.removeAll(path); paths.prepend(path); }));
  rebuild_recent_files_menu();
  add_recent_folder(QFileInfo(path).absolutePath());
}

void MainWindow::replace_recent_file(const QString& old_path, const QString& new_path) {
  const auto target = QFileInfo(new_path).absoluteFilePath();
  if (target.isEmpty()) {
    return;
  }
  recent_missing_files_.remove(old_path);
  recent_missing_files_.remove(target);
  recent_confirmed_paths_.remove(old_path);
  recent_confirmed_paths_.insert(target);
  set_recent_files_from_stored(update_recent_history(
      QStringLiteral("recentFiles"), kMaxRecentFiles, [&old_path, &target](QStringList& paths) {
        for (auto& entry : paths) {
          if (same_stored_path(entry, old_path)) {
            entry = target;
          }
        }
      }));
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
  recent_files_menu_->clear();  // also deletes the previous filter row, list and their widgets
  recent_files_menu_->setEnabled(!recent_files_.isEmpty());

  if (!build_recent_list_menu(recent_files_list_menu_, recent_files_menu_)) {
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
        page_menu->menuAction()->setMenuRole(QAction::NoRole);  // submenus never merge on macOS (docs/platform.md)
        configure_recent_files_context_menu(page_menu);
        for (int index = page_start; index < page_end; ++index) {
          add_recent_action(page_menu, recent_files_[index], index + 1);
        }
      }
    }
  }

  if (!recent_files_.isEmpty()) {
    recent_files_menu_->addSeparator();
    auto* clear_action = recent_files_menu_->addAction(tr("Clear Recent Files"));
    clear_action->setObjectName(QStringLiteral("fileClearRecentAction"));
    connect(clear_action, &QAction::triggered, this, [this] {
      set_recent_files_from_stored(update_recent_history(QStringLiteral("recentFiles"), kMaxRecentFiles,
          [](QStringList& paths) { paths.clear(); }));
      rebuild_recent_files_menu();
    });
  }
}

const QStringList& MainWindow::recent_list_paths(const RecentListMenu& ui) const {
  return ui.folders ? recent_folders_ : recent_files_;
}

// The Open Recent File and Open Recent Folder menus: one filter row over one scrolling
// list of every entry (the start panel's two-line rows), at a width that leaves room to
// open beside the File menu instead of covering it. The caller has just cleared `menu`,
// which deleted the previous row and list. Returns false, adding nothing, under a native
// menu bar (the macOS global bar cannot host QWidgetAction rows) or for an empty history;
// the caller then builds the plain paged menu.
bool MainWindow::build_recent_list_menu(RecentListMenu& ui, QMenu* menu) {
  ui.menu = menu;
  ui.filter_action = nullptr;
  ui.filter_edit = nullptr;
  ui.list = nullptr;
  const auto& paths = recent_list_paths(ui);
  const bool native_menu_bar = menuBar() != nullptr && menuBar()->isNativeMenuBar();
  if (menu == nullptr || paths.isEmpty() || native_menu_bar) {
    return false;
  }
  // Object names keep the files menu's original ones; the folders menu inserts "Folder".
  const auto prefix = ui.folders ? QStringLiteral("fileOpenRecentFolder") : QStringLiteral("fileOpenRecent");
  const auto list_width = std::clamp(QFontMetrics(menu->font()).averageCharWidth() * 56, 380, 560);

  auto* filter_row = new QWidget(menu);
  filter_row->setObjectName(prefix + QStringLiteral("FilterRow"));
  auto* filter_layout = new QHBoxLayout(filter_row);
  filter_layout->setContentsMargins(8, 6, 8, 6);
  auto* filter_edit = new QLineEdit(filter_row);
  filter_edit->setObjectName(prefix + QStringLiteral("FilterEdit"));
  filter_edit->setClearButtonEnabled(true);
  filter_edit->setFixedHeight(24);
  // The edit's own Cut/Copy/Paste popup must not nest inside the menu popup.
  filter_edit->setContextMenuPolicy(Qt::NoContextMenu);
  bind_widget_text(filter_edit, ui.folders
                                    ? QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Filter recent folders...")
                                    : QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Filter recent files..."));
  filter_layout->addWidget(filter_edit);
  // The global QWidget rule would otherwise paint @window_bg over the menu background.
  set_themed_style(*filter_row,
                   QStringLiteral("QWidget#%1FilterRow { background: transparent; }").arg(prefix));
  auto* filter_action = new QWidgetAction(menu);
  filter_action->setObjectName(prefix + QStringLiteral("FilterAction"));
  filter_action->setDefaultWidget(filter_row);
  menu->addAction(filter_action);

  auto* list_row = new QWidget(menu);
  list_row->setObjectName(prefix + QStringLiteral("ListRow"));
  auto* list_layout = new QHBoxLayout(list_row);
  list_layout->setContentsMargins(6, 0, 6, 4);
  auto* list = new RecentFileList(list_row, kRecentFilesMenuVisibleRows);
  list->setObjectName(prefix + QStringLiteral("List"));
  list->setSelectionMode(QAbstractItemView::SingleSelection);
  // The filter edit keeps focus; it forwards the navigation keys to the list.
  list->setFocusPolicy(Qt::NoFocus);
  list->setFixedWidth(list_width);
  // Filtering never resizes the open menu: the box keeps the full list's height.
  list->set_fixed_rows(static_cast<int>(paths.size()));
  list_layout->addWidget(list);
  set_themed_style(*list_row, QStringLiteral(R"(
    QWidget#%1ListRow { background: transparent; }
    QListWidget#%1List {
      background: @list_surface_bg;
      border: 1px solid @list_surface_border;
      border-radius: 5px;
      padding: 3px;
    }
  )").arg(prefix));
  auto* list_action = new QWidgetAction(menu);
  list_action->setObjectName(prefix + QStringLiteral("ListAction"));
  list_action->setDefaultWidget(list_row);
  menu->addAction(list_action);

  ui.filter_action = filter_action;
  ui.filter_edit = filter_edit;
  ui.list = list;
  // The menu, and so ui, outlives every connection below: rebuilding clears the menu,
  // which deletes the edit and the list first.
  auto* state = &ui;
  connect(filter_edit, &QLineEdit::textChanged, this,
          [this, state](const QString& text) { apply_recent_list_filter(*state, text); });
  connect(list, &QListWidget::itemEntered, list, [list](QListWidgetItem* item) {
    if (item != nullptr && !item->data(kRecentPathRole).toString().isEmpty()) {
      list->setCurrentItem(item);
    }
  });
  connect(list, &QListWidget::itemClicked, this, [this, state](QListWidgetItem* item) {
    if (item != nullptr) {
      open_recent_list_path(*state, item->data(kRecentPathRole).toString());
    }
  });
  // The hovered row's x removes that entry (GitHub issue 157) and keeps the menu
  // open: the list refills in place, keeping its scroll position with the highlight
  // on the entry that moved up into the removed row (as on the start panel), and the
  // menu rebuilds on the next File open.
  list->set_remove_handler(
      [this, state](const QString& path) {
        int removed_row = -1;
        if (state->list != nullptr) {
          for (int row = 0; row < state->list->count(); ++row) {
            if (state->list->item(row)->data(kRecentPathRole).toString() == path) {
              removed_row = row;
              break;
            }
          }
        }
        remove_recent_path(path, state->folders, /*rebuild_menus=*/false);
        apply_recent_list_filter(*state, state->filter_edit != nullptr ? state->filter_edit->text() : QString(),
                                 removed_row);
      },
      state->folders ? tr("Remove from Recent Folders") : tr("Remove from Recent Files"));
  connect(list, &QListWidget::customContextMenuRequested, this, [this, state, list](const QPoint& position) {
    auto* item = list->itemAt(position);
    if (item == nullptr || item->data(kRecentPathRole).toString().isEmpty()) {
      return;
    }
    show_recent_path_context_menu(list, state->menu, item->data(kRecentPathRole).toString(), state->folders,
                                  list->viewport()->mapToGlobal(position));
  });
  apply_recent_list_filter(ui, QString());
  return true;
}

// `keep_row` >= 0 refills after a removal: the scroll position stays, and the highlight
// lands on that row (the next entry), or the last one when the bottom entry went.
void MainWindow::apply_recent_list_filter(RecentListMenu& ui, const QString& filter_text, int keep_row) {
  if (ui.list == nullptr) {
    return;
  }
  const auto scroll = ui.list->verticalScrollBar()->value();
  const auto placeholder = ui.folders ? tr("No matching recent folders") : tr("No matching recent files");
  const auto rows = fill_recent_file_list(*ui.list, recent_list_paths(ui), filter_text, placeholder);
  if (keep_row >= 0) {
    // Lay the refilled rows out now: the scroll range is stale until then and would
    // clamp the restored position.
    ui.list->doItemsLayout();
    ui.list->setCurrentRow(rows > 0 ? std::min(keep_row, rows - 1) : -1);
    ui.list->verticalScrollBar()->setValue(scroll);
    return;
  }
  // The highlight starts on the best match, so Enter opens it.
  ui.list->setCurrentRow(rows > 0 ? 0 : -1);
  ui.list->scrollToTop();
}

void MainWindow::open_recent_list_path(const RecentListMenu& ui, const QString& path) {
  if (path.isEmpty()) {
    return;  // the "no matches" placeholder row
  }
  // Close the whole popup chain (the list lives in a submenu of File), then open
  // on the next event-loop turn: opening rebuilds this menu, which deletes the
  // list and edit whose event is still being delivered, and a folder opens a
  // modal file dialog that must not start under a popup.
  for (int guard = 0; guard < 8; ++guard) {
    auto* popup = qobject_cast<QMenu*>(QApplication::activePopupWidget());
    if (popup == nullptr) {
      break;
    }
    popup->close();
  }
  if (ui.menu != nullptr) {
    ui.menu->close();
  }
  const bool folder = ui.folders;
  QTimer::singleShot(0, this, [this, path, folder] {
    if (folder) {
      open_recent_folder(path);
    } else {
      open_recent_document(path);
    }
  });
}

bool MainWindow::handle_recent_list_filter_key(RecentListMenu& ui, QKeyEvent& event) {
  if (ui.list == nullptr) {
    return false;
  }
  switch (event.key()) {
    case Qt::Key_Up:
    case Qt::Key_Down:
    case Qt::Key_PageUp:
    case Qt::Key_PageDown: {
      // List navigation keeps working while the edit holds focus (the
      // SearchKeyForwarder pattern from the font picker popup).
      QKeyEvent forwarded(event.type(), event.key(), event.modifiers(), event.text());
      QApplication::sendEvent(ui.list, &forwarded);
      return true;
    }
    case Qt::Key_Return:
    case Qt::Key_Enter: {
      QString path;
      if (auto* current = ui.list->currentItem(); current != nullptr) {
        path = current->data(kRecentPathRole).toString();
      }
      if (path.isEmpty() && ui.list->count() > 0) {
        path = ui.list->item(0)->data(kRecentPathRole).toString();
      }
      open_recent_list_path(ui, path);
      return true;
    }
    default:
      return false;
  }
}

void MainWindow::load_recent_folders() {
  auto settings = recent_history_settings();
  settings.sync();
  set_recent_folders_from_stored(settings.value(QStringLiteral("recentFolders")).toStringList());
}

void MainWindow::set_recent_folders_from_stored(QStringList stored) {
  while (stored.size() > kMaxRecentFolders) {
    stored.removeLast();
  }
  recent_folders_stored_ = stored;
  stored.erase(std::remove_if(stored.begin(), stored.end(),
                              [this](const QString& dir) {
                                return dir.trimmed().isEmpty() || recent_missing_folders_.contains(dir);
                              }),
               stored.end());
  recent_folders_ = std::move(stored);
}

void MainWindow::add_recent_folder(QString dir) {
  dir = QFileInfo(dir).absoluteFilePath();
  if (dir.isEmpty()) {
    return;
  }
  recent_missing_folders_.remove(dir);
  recent_confirmed_paths_.insert(dir);
  set_recent_folders_from_stored(update_recent_history(QStringLiteral("recentFolders"), kMaxRecentFolders,
      [&dir](QStringList& paths) { paths.removeAll(dir); paths.prepend(dir); }));
  rebuild_recent_folders_menu();
}

void MainWindow::open_recent_folder(const QString& dir) {
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return;
  }
  const auto start_dir = QFileInfo(dir).isDir() ? dir : last_open_directory();
  auto selected_filter = remembered_open_filter();
  const auto paths = get_open_file_names(this, tr("Open"), start_dir, open_file_filter(), &selected_filter,
                                         QStringLiteral("openFileDialog"), FilterNameDetails::Hidden);
  remember_open_filter(paths.isEmpty() ? QString() : selected_filter);
  for (const auto& path : paths) {
    open_document_path(path);
  }
}

void MainWindow::rebuild_recent_folders_menu() {
  if (recent_folders_menu_ == nullptr) {
    return;
  }
  recent_folders_menu_->clear();  // also deletes the previous filter row, list and their widgets
  recent_folders_menu_->setEnabled(!recent_folders_.isEmpty());

  if (!build_recent_list_menu(recent_folders_list_menu_, recent_folders_menu_)) {
    const auto add_recent_folder_action = [this](QMenu* menu, const QString& dir, int index) {
      const auto label = tr("&%1 %2").arg(index).arg(QDir::toNativeSeparators(dir));
      auto* action = menu->addAction(label);
      action->setToolTip(dir);
      action->setData(dir);
      connect(action, &QAction::triggered, this, [this, dir] { open_recent_folder(dir); });
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
        auto* page_menu =
            recent_folders_menu_->addMenu(tr("Recent Folders %1-%2").arg(page_start + 1).arg(page_end));
        page_menu->setObjectName(QStringLiteral("fileOpenRecentFolderRangeMenu%1").arg(page_start + 1));
        page_menu->menuAction()->setMenuRole(QAction::NoRole);  // submenus never merge on macOS (docs/platform.md)
        configure_recent_files_context_menu(page_menu);
        page_menu->setProperty(kRecentFoldersMenuProperty, true);
        for (int index = page_start; index < page_end; ++index) {
          add_recent_folder_action(page_menu, recent_folders_[index], index + 1);
        }
      }
    }
  }

  if (!recent_folders_.isEmpty()) {
    recent_folders_menu_->addSeparator();
    auto* clear_action = recent_folders_menu_->addAction(tr("Clear Recent Folders"));
    clear_action->setObjectName(QStringLiteral("fileClearRecentFoldersAction"));
    connect(clear_action, &QAction::triggered, this, [this] {
      set_recent_folders_from_stored(update_recent_history(QStringLiteral("recentFolders"), kMaxRecentFolders,
          [](QStringList& paths) { paths.clear(); }));
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
  show_recent_path_context_menu(menu, menu, path, is_folder, menu->mapToGlobal(position));
}

void MainWindow::show_start_panel_recent_context_menu(const QString& path, const QPoint& global_position) {
  if (start_panel_ == nullptr || path.isEmpty()) {
    return;
  }
  show_recent_path_context_menu(start_panel_, nullptr, path, /*is_folder=*/false, global_position);
}

void MainWindow::show_recent_path_context_menu(QWidget* parent, QMenu* source_menu, const QString& path,
                                               bool is_folder, const QPoint& global_position) {
  // Only a menu source needs dismissing; a start-panel row leaves nothing open.
  const auto close_menus = [this, source_menu] {
    if (source_menu == nullptr) {
      return;
    }
    source_menu->close();
    if (recent_files_menu_ != nullptr) {
      recent_files_menu_->close();
    }
    if (recent_folders_menu_ != nullptr) {
      recent_folders_menu_->close();
    }
  };

  QMenu context_menu(parent);
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

  context_menu.addSeparator();
  auto* remove_action =
      context_menu.addAction(is_folder ? tr("Remove from Recent Folders") : tr("Remove from Recent Files"));
  remove_action->setObjectName(is_folder ? QStringLiteral("recentFolderRemoveAction")
                                         : QStringLiteral("recentFileRemoveAction"));
  connect(remove_action, &QAction::triggered, this, [this, close_menus, is_folder, path] {
    close_menus();
    // Queued: removing rebuilds the list this menu was opened from, which is
    // still delivering the right-click that opened it.
    QTimer::singleShot(0, this, [this, is_folder, path] { remove_recent_path(path, is_folder); });
  });

  context_menu.exec(global_position);
}

void MainWindow::remove_recent_path(const QString& path, bool is_folder, bool rebuild_menus) {
  if (path.isEmpty()) {
    return;
  }
  const auto target = QFileInfo(path).absoluteFilePath();
  const auto drop = [&target](QStringList& paths) {
    paths.removeIf([&target](const QString& entry) { return QFileInfo(entry).absoluteFilePath() == target; });
  };
  if (is_folder) {
    set_recent_folders_from_stored(update_recent_history(QStringLiteral("recentFolders"), kMaxRecentFolders, drop));
  } else {
    set_recent_files_from_stored(update_recent_history(QStringLiteral("recentFiles"), kMaxRecentFiles, drop));
  }
  if (!rebuild_menus) {
    recent_menus_stale_ = true;
    if (!is_folder && start_panel_ != nullptr && start_panel_->isVisible()) {
      start_panel_->set_recent_files(recent_files_);
    }
    return;
  }
  if (is_folder) {
    rebuild_recent_folders_menu();
  } else {
    rebuild_recent_files_menu();
  }
}

void MainWindow::reveal_path_in_file_explorer(const QString& path, bool is_file) {
  const QFileInfo info(path);
  if (is_file) {
    if (!info.exists()) {
      show_status_error(tr("File is missing"));
      return;
    }
#if defined(Q_OS_WIN)
    // Open the containing folder with the file pre-selected. "/select," and the
    // path must stay SEPARATE arguments: QProcess quotes any space-containing
    // argument whole, and Explorer treats a quoted "/select,path" blob as
    // unparseable, silently opening the default folder instead.
    QProcess::startDetached(QStringLiteral("explorer.exe"),
                            {QStringLiteral("/select,"), QDir::toNativeSeparators(info.absoluteFilePath())});
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
    set_recent_files_from_stored(update_recent_history(QStringLiteral("recentFiles"), kMaxRecentFiles,
        [&path](QStringList& paths) { paths.removeAll(path); }));
    rebuild_recent_files_menu();
    show_status_error(tr("Recent file is missing"));
    return;
  }
  open_document_path(path);
}

}  // namespace patchy::ui
