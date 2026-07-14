// MainWindow's smart-object implementation, split out of main_window.cpp:
// export/open/commit/refresh/update/relink/embed/replace smart-object
// contents, convert-to-smart-object, new-smart-object-via-copy, and the
// place-embedded-file flows. Pure function moves from main_window.cpp;
// behavior must stay identical.

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

// PSD link-element filetype OSTypes, pinned from Photoshop 2026 captures (docs/smart-objects.md).
std::string psd_element_filetype_for_extension(const QString& extension) {
  if (extension == QStringLiteral("psb")) {
    return "8BPB";
  }
  if (extension == QStringLiteral("psd")) {
    return "8BPS";
  }
  if (extension == QStringLiteral("jpg") || extension == QStringLiteral("jpeg")) {
    return "JPEG";
  }
  if (extension == QStringLiteral("tif") || extension == QStringLiteral("tiff")) {
    return "TIFF";
  }
  if (extension == QStringLiteral("bmp")) {
    return "BMP ";
  }
  return "png ";
}

std::optional<SmartObjectWarp> rescaled_warp_for_replaced_contents(
    const std::optional<SmartObjectWarp>& original,
    const SmartObjectPlacement& placement, double new_width,
    double new_height) {
  if (!original.has_value()) {
    return std::nullopt;
  }
  auto warp = scaled_smart_object_warp(
      *original, new_width / std::max(1.0, placement.width),
      new_height / std::max(1.0, placement.height));
  if (warp.mesh_generated) {
    if (const auto regenerated = generate_style_warp_mesh(
            warp.style, warp.value, warp.rotate == "Vrtc",
            warp.bounds_right - warp.bounds_left,
            warp.bounds_bottom - warp.bounds_top)) {
      warp.u_order = regenerated->u_order;
      warp.v_order = regenerated->v_order;
      warp.mesh_xs = regenerated->xs;
      warp.mesh_ys = regenerated->ys;
    }
  }
  return warp;
}

}  // namespace

void MainWindow::export_smart_object_contents() {
  if (!has_active_document()) {
    return;
  }
  const auto active = document().active_layer_id();
  const auto* layer = active.has_value() ? document().find_layer(*active) : nullptr;
  if (layer == nullptr || !layer_is_smart_object(*layer)) {
    statusBar()->showMessage(tr("Select a smart object layer first"));
    return;
  }
  const auto* source = document().metadata().smart_objects.find(smart_object_source_uuid(*layer));
  if (source == nullptr || source->file_bytes == nullptr) {
    statusBar()->showMessage(tr("This smart object has no embedded contents to export"));
    return;
  }
  const auto suggested = QString::fromStdString(source->filename.empty() ? "contents" : source->filename);
  auto path = get_save_file_name(this, tr("Export Smart Object Contents"),
                                 file_dialog_initial_path(QString(), suggested), tr("All Files (*.*)"), nullptr,
                                 QStringLiteral("exportSmartObjectContentsFileDialog"));
  if (path.isEmpty()) {
    return;
  }
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly) ||
      file.write(reinterpret_cast<const char*>(source->file_bytes->data()),
                 static_cast<qint64>(source->file_bytes->size())) !=
          static_cast<qint64>(source->file_bytes->size())) {
    show_critical_message(this, tr("Export failed"), tr("Could not write %1").arg(path),
                          QStringLiteral("exportSmartObjectFailedMessageBox"));
    return;
  }
  remember_save_directory_for_path(path);
  statusBar()->showMessage(tr("Exported smart object contents to %1").arg(path));
}

void MainWindow::open_smart_object_contents() {
  if (!has_active_document()) {
    return;
  }
  const auto active = document().active_layer_id();
  const auto* layer = active.has_value() ? document().find_layer(*active) : nullptr;
  if (layer == nullptr || !layer_is_smart_object(*layer)) {
    statusBar()->showMessage(tr("Select a smart object layer first"));
    return;
  }
  const auto lock_reason = smart_object_lock_reason(*layer);
  if (!lock_reason.empty() && lock_reason != "external") {
    if (lock_reason == "filters") {
      statusBar()->showMessage(
          tr("This smart object has Smart Filters; Patchy keeps Photoshop's preview (rasterize to edit pixels)"));
    } else if (lock_reason == "warp" || lock_reason == "non_affine") {
      statusBar()->showMessage(
          tr("This smart object has a warp or perspective transform; Patchy keeps Photoshop's preview"));
    } else {
      statusBar()->showMessage(tr("This smart object can only be preserved, not edited"));
    }
    return;
  }
  const auto uuid = smart_object_source_uuid(*layer);
  const auto* source = document().metadata().smart_objects.find(uuid);
  auto& parent_session = session();
  const auto parent_session_id = parent_session.session_id;
  for (auto* child : open_smart_object_child_sessions(parent_session_id)) {
    if (child->smart_object_link->source_uuid == uuid) {
      activate_document_session(*child);
      return;
    }
  }
  if (lock_reason == "external") {
    // A linked file opens from disk as a normal document whose Save also refreshes
    // the parent (Photoshop's Edit Contents behavior for linked smart objects).
    if (source == nullptr || source->kind != SmartObjectSourceKind::ExternalFile) {
      statusBar()->showMessage(tr("This smart object's contents are not embedded in the document"));
      return;
    }
    const auto parent_dir =
        parent_session.path.isEmpty() ? QString() : QFileInfo(parent_session.path).absolutePath();
    const auto resolved = resolve_smart_object_external_path(*source, parent_dir);
    if (!resolved.has_value()) {
      statusBar()->showMessage(tr("Linked file %1 was not found. Use Relink to File... to point it at a new location")
                                   .arg(QString::fromStdString(source->filename)));
      return;
    }
    const auto parent_title = parent_session.title.isEmpty() ? tr("Untitled") : parent_session.title;
    open_document_path(*resolved);
    auto& child_session = session();
    if (QFileInfo(child_session.path) != QFileInfo(*resolved)) {
      return;  // the open failed or landed elsewhere; nothing to link
    }
    child_session.smart_object_link = DocumentSession::SmartObjectLink{parent_session_id, uuid, true};
    statusBar()->showMessage(
        tr("Editing linked file. Save (Ctrl+S) writes %1 and updates %2")
            .arg(QString::fromStdString(source->filename), parent_title));
    return;
  }
  if (source == nullptr || source->kind != SmartObjectSourceKind::Embedded || source->file_bytes == nullptr) {
    statusBar()->showMessage(tr("This smart object's contents are not embedded in the document"));
    return;
  }
  const auto contents_format = classify_smart_object_contents(*source);
  if (contents_format != SmartObjectContentsFormat::PsdDocument &&
      contents_format != SmartObjectContentsFormat::QtImage) {
    statusBar()->showMessage(
        tr("Patchy can't re-encode %1 contents; use Export Smart Object Contents or rasterize the layer")
            .arg(QString::fromStdString(source->filename)));
    return;
  }
  auto child_document = decode_smart_object_source_document(*source);
  if (!child_document.has_value()) {
    statusBar()->showMessage(tr("Could not decode the embedded smart object contents"));
    return;
  }
  const auto file_name =
      QString::fromStdString(source->filename.empty() ? std::string("contents") : source->filename);
  const auto parent_title = parent_session.title.isEmpty() ? tr("Untitled") : parent_session.title;
  add_document_session(std::move(*child_document), tr("%1 (embedded in %2)").arg(file_name, parent_title));
  auto& child_session = session();
  child_session.smart_object_link = DocumentSession::SmartObjectLink{parent_session_id, uuid};
  statusBar()->showMessage(tr("Editing smart object contents. Save (Ctrl+S) applies them back to %1").arg(parent_title));
}

bool MainWindow::commit_smart_object_child_session(DocumentSession& child_session) {
  if (!child_session.smart_object_link.has_value()) {
    return false;
  }
  const auto link = *child_session.smart_object_link;
  auto* parent = session_with_id(link.parent_session_id);
  if (parent == nullptr) {
    // The parent tab is gone, so the edit has nowhere to commit; detach and fall
    // back to saving a copy on disk.
    child_session.smart_object_link.reset();
    statusBar()->showMessage(tr("The original document is closed; saving a copy instead"));
    return save_document_as();
  }
  auto* source = parent->document.metadata().smart_objects.find(link.source_uuid);
  if (source == nullptr || source->kind != SmartObjectSourceKind::Embedded) {
    statusBar()->showMessage(tr("The smart object no longer exists in %1").arg(parent->title));
    return false;
  }

  // Serialize the child in the source's own format.
  const auto contents_format = classify_smart_object_contents(*source);
  std::vector<std::uint8_t> encoded;
  double content_dpi = 72.0;
  if (contents_format == SmartObjectContentsFormat::PsdDocument) {
    psd::WriteOptions write_options;
    write_options.large_document = source->filetype == "8BPB";
    try {
      encoded = psd::DocumentIo::write_layered_rgb8(child_session.document, write_options);
    } catch (const std::exception& error) {
      show_critical_message(this, tr("Save failed"), QString::fromUtf8(error.what()),
                            QStringLiteral("smartObjectCommitFailedMessageBox"));
      return false;
    }
    content_dpi = child_session.document.print_settings().horizontal_ppi;
  } else if (contents_format == SmartObjectContentsFormat::QtImage) {
    const auto flattened = qimage_from_document(child_session.document, true);
    const auto extension = QFileInfo(QString::fromStdString(source->filename)).suffix().toLower();
    QByteArray encoded_bytes;
    QBuffer buffer(&encoded_bytes);
    buffer.open(QIODevice::WriteOnly);
    const auto format_token = extension.isEmpty() ? QByteArray("png") : extension.toUtf8();
    const int quality = (extension == QStringLiteral("jpg") || extension == QStringLiteral("jpeg") ||
                         extension == QStringLiteral("webp"))
                            ? 95
                            : -1;
    if (!flattened.save(&buffer, format_token.constData(), quality)) {
      show_critical_message(
          this, tr("Save failed"),
          tr("Could not re-encode the contents as %1").arg(QString::fromStdString(source->filename)),
          QStringLiteral("smartObjectCommitFailedMessageBox"));
      return false;
    }
    encoded.assign(encoded_bytes.begin(), encoded_bytes.end());
    // Image sources keep their placed density: Photoshop does not re-derive it on edit.
    content_dpi = 0.0;
  } else {
    statusBar()->showMessage(tr("These contents can't be re-encoded"));
    return false;
  }

  // Decode the committed bytes once up front (every referencing layer shares the
  // image); failing here leaves the parent untouched.
  SmartObjectSource updated = *source;
  updated.file_bytes = std::make_shared<const std::vector<std::uint8_t>>(std::move(encoded));
  updated.original_element_bytes = nullptr;
  updated.dirty = true;
  const auto rendered_image = decode_smart_object_source_image(updated);
  if (!rendered_image.has_value()) {
    show_critical_message(this, tr("Save failed"), tr("Could not render the committed contents"),
                          QStringLiteral("smartObjectCommitFailedMessageBox"));
    return false;
  }

  const auto refreshed_source_uuid = generate_smart_object_uuid();
  updated.uuid = refreshed_source_uuid;
  auto updated_document = parent->document;
  bool source_replaced = false;
  for (auto& block : updated_document.metadata().smart_objects.blocks) {
    for (auto& candidate_source : block.sources) {
      if (candidate_source.uuid != link.source_uuid) {
        continue;
      }
      candidate_source = std::move(updated);
      block.original_payload.reset();
      source_replaced = true;
      break;
    }
    if (source_replaced) {
      break;
    }
  }
  if (!source_replaced) {
    return false;
  }
  if (!refresh_smart_object_layers_for_source(
          updated_document, link.source_uuid, *rendered_image, content_dpi,
          false, true, refreshed_source_uuid)) {
    show_critical_message(
        this, tr("Save failed"),
        tr("Could not rebuild the Smart Filter preview and cache"),
        QStringLiteral("smartObjectCommitFailedMessageBox"));
    return false;
  }

  // One parent undo step for the whole commit (same 40-entry cap as push_undo_snapshot,
  // which only operates on the active session).
  parent->undo_stack.push_back(DocumentSession::HistoryState{
      parent->document, parent->revision,
      parent->canvas != nullptr ? parent->canvas->capture_selection_snapshot()
                                : CanvasWidget::SelectionSnapshot{}});
  constexpr std::size_t kMaxUndo = 40;
  if (parent->undo_stack.size() > kMaxUndo) {
    parent->undo_stack.erase(parent->undo_stack.begin());
  }
  parent->redo_stack.clear();
  parent->selection_move_coalescing = false;
  parent->document = std::move(updated_document);
  if (child_session.smart_object_link.has_value()) {
    child_session.smart_object_link->source_uuid = refreshed_source_uuid;
  }

  if (parent->canvas != nullptr) {
    parent->canvas->document_changed();
  }
  mark_session_modified(*parent);
  update_history(tr("Edit Smart Object Contents"));

  child_session.saved_revision = child_session.revision;
  refresh_document_tab_titles();
  update_document_action_state();
  statusBar()->showMessage(tr("Applied smart object contents to %1").arg(parent->title));
  return true;
}

bool MainWindow::refresh_smart_object_layers_for_source(
    Document& target_document, const std::string& source_uuid,
    const QImage& rendered_image, double content_dpi,
    bool include_external_locked, bool rekey_placed_instances,
    std::string_view replacement_source_uuid) {
  const double new_width = rendered_image.width();
  const double new_height = rendered_image.height();
  // Const walk on purpose: the non-const children() accessor bumps every
  // visited layer's revisions on access (AGENTS.md "Reads must not bump layer
  // revisions"), so the old mutable traversal invalidated every thumbnail and
  // style-mask cache in the document even when no smart object matched.
  // Matched layers are cast back to mutable below; their bumps are real edits.
  std::function<bool(const std::vector<Layer>&)> refresh_layers =
      [&](const std::vector<Layer>& layers) {
    for (const auto& const_layer : layers) {
      if (!const_layer.children().empty() && !refresh_layers(const_layer.children())) {
        return false;
      }
      if (!layer_is_smart_object(const_layer) || smart_object_source_uuid(const_layer) != source_uuid) {
        continue;
      }
      auto& layer = const_cast<Layer&>(const_layer);
      const auto lock = smart_object_lock_reason(layer);
      if (!lock.empty() && !(include_external_locked && lock == "external")) {
        if (rekey_placed_instances || !replacement_source_uuid.empty()) {
          return false;
        }
        continue;
      }
      const auto placement = smart_object_placement_from_layer(layer);
      if (!placement.has_value()) {
        if (rekey_placed_instances || !replacement_source_uuid.empty()) {
          return false;
        }
        continue;
      }
      auto updated_placement = *placement;
      const auto old_placed_uuid = smart_object_placed_uuid(layer);
      auto warp = smart_object_warp_from_layer(layer);
      const bool size_changed =
          std::abs(placement->width - new_width) > 0.5 || std::abs(placement->height - new_height) > 0.5;
      if (size_changed) {
        const double dpi = content_dpi > 0.0 ? content_dpi : placement->resolution;
        updated_placement = rescaled_smart_object_placement(*placement, new_width, new_height, dpi);
        store_smart_object_placement(layer, updated_placement);
        mark_layer_smart_object_block_dirty(layer);
        warp = rescaled_warp_for_replaced_contents(
            warp, *placement, new_width, new_height);
        if (warp.has_value()) {
          layer.metadata()[kLayerMetadataSmartObjectWarp] = serialize_smart_object_warp(*warp);
        }
      }
      if (!replacement_source_uuid.empty()) {
        updated_placement.uuid = std::string(replacement_source_uuid);
        layer.metadata()[kLayerMetadataSmartObject] =
            std::string(replacement_source_uuid);
        mark_layer_smart_object_block_dirty(layer);
      }
      if (rekey_placed_instances) {
        layer.metadata()[kLayerMetadataSmartObjectPlaced] =
            generate_smart_object_uuid();
        mark_layer_smart_object_block_dirty(layer);
      }
      if (auto rendered = render_smart_object_image_preview(
              rendered_image, updated_placement, warp,
              CanvasWidget::TransformInterpolation::Bicubic,
              std::as_const(layer).smart_filter_stack(),
              Rect::from_size(target_document.width(),
                              target_document.height()))) {
        if (!install_smart_object_layer_preview(
                target_document, layer, std::move(*rendered), true)) {
          return false;
        }
        if (rekey_placed_instances && !old_placed_uuid.empty() &&
            old_placed_uuid != smart_object_placed_uuid(layer) &&
            target_document.metadata().smart_filter_effects.find_unique(
                old_placed_uuid) != nullptr &&
            !target_document.metadata().smart_filter_effects.remove(
                old_placed_uuid)) {
          return false;
        }
      } else {
        return false;
      }
    }
    return true;
  };
  return refresh_layers(std::as_const(target_document).layers());
}

void MainWindow::refresh_external_smart_object_after_save(DocumentSession& child_session) {
  if (!child_session.smart_object_link.has_value() || child_session.path.isEmpty()) {
    return;
  }
  const auto link = *child_session.smart_object_link;
  auto* parent = session_with_id(link.parent_session_id);
  if (parent == nullptr) {
    // The parent tab is gone; the file itself is already saved, so just detach.
    child_session.smart_object_link.reset();
    return;
  }
  auto* source = parent->document.metadata().smart_objects.find(link.source_uuid);
  if (source == nullptr || source->kind != SmartObjectSourceKind::ExternalFile) {
    return;
  }

  QFile file(child_session.path);
  if (!file.open(QIODevice::ReadOnly)) {
    return;
  }
  const auto raw = file.readAll();
  file.close();
  // Decode via a probe that carries the fresh bytes (external sources keep none).
  SmartObjectSource probe = *source;
  probe.kind = SmartObjectSourceKind::Embedded;
  probe.file_bytes = std::make_shared<const std::vector<std::uint8_t>>(raw.begin(), raw.end());
  const auto rendered_image = decode_smart_object_source_image(probe);
  if (!rendered_image.has_value()) {
    return;
  }
  const auto content_dpi =
      psd::DocumentIo::can_read({probe.file_bytes->data(), probe.file_bytes->size()})
          ? smart_object_source_dpi(probe)
          : 0.0;

  auto updated_document = parent->document;
  auto* updated_source =
      updated_document.metadata().smart_objects.find(link.source_uuid);
  if (updated_source == nullptr) {
    return;
  }
  const QFileInfo saved_info(child_session.path);
  const auto modified = saved_info.lastModified();
  updated_source->external_mod_year = modified.date().year();
  updated_source->external_mod_month =
      static_cast<std::uint8_t>(modified.date().month());
  updated_source->external_mod_day =
      static_cast<std::uint8_t>(modified.date().day());
  updated_source->external_mod_hour =
      static_cast<std::uint8_t>(modified.time().hour());
  updated_source->external_mod_minute =
      static_cast<std::uint8_t>(modified.time().minute());
  updated_source->external_mod_seconds =
      modified.time().second() + modified.time().msec() / 1000.0;
  updated_source->external_file_size =
      static_cast<std::uint64_t>(saved_info.size());
  updated_source->dirty = true;
  if (!refresh_smart_object_layers_for_source(
          updated_document, link.source_uuid, *rendered_image, content_dpi,
          true)) {
    statusBar()->showMessage(
        tr("Could not rebuild the Smart Filter preview and cache"));
    return;
  }

  // One parent undo step for the refresh (same cap as push_undo_snapshot, which only
  // operates on the active session).
  parent->undo_stack.push_back(DocumentSession::HistoryState{
      parent->document, parent->revision,
      parent->canvas != nullptr ? parent->canvas->capture_selection_snapshot()
                                : CanvasWidget::SelectionSnapshot{}});
  constexpr std::size_t kMaxUndo = 40;
  if (parent->undo_stack.size() > kMaxUndo) {
    parent->undo_stack.erase(parent->undo_stack.begin());
  }
  parent->redo_stack.clear();
  parent->selection_move_coalescing = false;
  parent->document = std::move(updated_document);
  if (parent->canvas != nullptr) {
    parent->canvas->document_changed();
  }
  mark_session_modified(*parent);
  update_history(tr("Update Smart Object Content"));
  statusBar()->showMessage(tr("Saved %1 and updated %2")
                               .arg(QFileInfo(child_session.path).fileName(),
                                    parent->title.isEmpty() ? tr("Untitled") : parent->title));
}

void MainWindow::update_smart_object_content() {
  if (!has_active_document()) {
    return;
  }
  auto& doc = document();
  const auto active = doc.active_layer_id();
  const auto* layer = active.has_value() ? doc.find_layer(*active) : nullptr;
  if (layer == nullptr || !layer_is_smart_object(*layer) || smart_object_lock_reason(*layer) != "external") {
    statusBar()->showMessage(tr("Select a linked smart object layer first"));
    return;
  }
  const auto uuid = smart_object_source_uuid(*layer);
  auto* source = doc.metadata().smart_objects.find(uuid);
  if (source == nullptr || source->kind != SmartObjectSourceKind::ExternalFile) {
    statusBar()->showMessage(tr("Select a linked smart object layer first"));
    return;
  }
  const auto parent_dir = session().path.isEmpty() ? QString() : QFileInfo(session().path).absolutePath();
  const auto resolved = resolve_smart_object_external_path(*source, parent_dir);
  if (!resolved.has_value()) {
    statusBar()->showMessage(tr("Linked file %1 was not found. Use Relink to File... to point it at a new location")
                                 .arg(QString::fromStdString(source->filename)));
    return;
  }
  QFile file(*resolved);
  if (!file.open(QIODevice::ReadOnly)) {
    statusBar()->showMessage(tr("Could not read %1").arg(*resolved));
    return;
  }
  const auto raw = file.readAll();
  file.close();
  SmartObjectSource probe = *source;
  probe.kind = SmartObjectSourceKind::Embedded;
  probe.file_bytes = std::make_shared<const std::vector<std::uint8_t>>(raw.begin(), raw.end());
  const auto rendered_image = decode_smart_object_source_image(probe);
  if (!rendered_image.has_value()) {
    statusBar()->showMessage(tr("Could not decode %1").arg(*resolved));
    return;
  }
  const auto content_dpi =
      psd::DocumentIo::can_read({probe.file_bytes->data(), probe.file_bytes->size()})
          ? smart_object_source_dpi(probe)
          : 0.0;

  auto updated_document = doc;
  auto* updated_source = updated_document.metadata().smart_objects.find(uuid);
  if (updated_source == nullptr) {
    return;
  }
  const QFileInfo linked_info(*resolved);
  const auto modified = linked_info.lastModified();
  updated_source->external_mod_year = modified.date().year();
  updated_source->external_mod_month =
      static_cast<std::uint8_t>(modified.date().month());
  updated_source->external_mod_day =
      static_cast<std::uint8_t>(modified.date().day());
  updated_source->external_mod_hour =
      static_cast<std::uint8_t>(modified.time().hour());
  updated_source->external_mod_minute =
      static_cast<std::uint8_t>(modified.time().minute());
  updated_source->external_mod_seconds =
      modified.time().second() + modified.time().msec() / 1000.0;
  updated_source->external_file_size =
      static_cast<std::uint64_t>(linked_info.size());
  updated_source->dirty = true;
  if (!refresh_smart_object_layers_for_source(
          updated_document, uuid, *rendered_image, content_dpi, true)) {
    statusBar()->showMessage(
        tr("Could not rebuild the Smart Filter preview and cache"));
    return;
  }
  push_undo_snapshot(tr("Update Smart Object Content"));
  doc = std::move(updated_document);
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
  statusBar()->showMessage(tr("Updated smart object content from %1").arg(linked_info.fileName()));
}

void MainWindow::relink_smart_object_contents() {
  if (!has_active_document()) {
    return;
  }
  const auto path = get_open_file_name(
      this, tr("Relink to File"), file_dialog_initial_path(QString(), QString()),
      tr("Embeddable Files (*.psd *.psb *.png *.jpg *.jpeg *.tif *.tiff *.bmp);;All Files (*.*)"), nullptr,
      QStringLiteral("relinkSmartObjectFileDialog"));
  if (!path.isEmpty()) {
    relink_smart_object_contents_with_path(path);
  }
}

void MainWindow::relink_smart_object_contents_with_path(const QString& path) {
  if (!has_active_document()) {
    return;
  }
  auto& doc = document();
  const auto active = doc.active_layer_id();
  const auto* layer = active.has_value() ? doc.find_layer(*active) : nullptr;
  if (layer == nullptr || !layer_is_smart_object(*layer) || smart_object_lock_reason(*layer) != "external") {
    statusBar()->showMessage(tr("Select a linked smart object layer first"));
    return;
  }
  const auto uuid = smart_object_source_uuid(*layer);
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    show_critical_message(this, tr("Relink failed"), tr("Could not read %1").arg(path),
                          QStringLiteral("relinkSmartObjectFailedMessageBox"));
    return;
  }
  const auto raw = file.readAll();
  file.close();
  const QFileInfo info(path);
  SmartObjectSource probe;
  probe.kind = SmartObjectSourceKind::Embedded;
  probe.filename = info.fileName().toStdString();
  probe.file_bytes = std::make_shared<const std::vector<std::uint8_t>>(raw.begin(), raw.end());
  if (classify_smart_object_contents(probe) == SmartObjectContentsFormat::Undecodable) {
    show_critical_message(this, tr("Relink failed"), tr("Could not decode %1").arg(info.fileName()),
                          QStringLiteral("relinkSmartObjectFailedMessageBox"));
    return;
  }
  const auto rendered_image = decode_smart_object_source_image(probe);
  if (!rendered_image.has_value()) {
    show_critical_message(this, tr("Relink failed"), tr("Could not decode %1").arg(info.fileName()),
                          QStringLiteral("relinkSmartObjectFailedMessageBox"));
    return;
  }
  const auto content_dpi = smart_object_source_dpi(probe);

  const auto* old_source = doc.metadata().smart_objects.find(uuid);
  if (old_source == nullptr) {
    return;
  }
  const auto old_stem = QFileInfo(QString::fromStdString(old_source->filename)).completeBaseName();
  // Photoshop semantics (E14 capture): relink behaves like Replace Contents but stays
  // external: a FRESH element replaces the old one, every referencing layer repoints
  // and rebuilds about its own quad center, and layer names swap the source stem.
  SmartObjectSource relinked;
  relinked.kind = SmartObjectSourceKind::ExternalFile;
  relinked.uuid = generate_smart_object_uuid();
  relinked.filename = info.fileName().toStdString();
  relinked.filetype = psd_element_filetype_for_extension(info.suffix().toLower());
  relinked.creator = std::string(4, '\0');
  const auto absolute = info.absoluteFilePath();
  const auto parent_dir = session().path.isEmpty() ? QString() : QFileInfo(session().path).absolutePath();
  relinked.external_full_path = QUrl::fromLocalFile(absolute).toString().toStdString();
  relinked.external_original_path = QDir::toNativeSeparators(absolute).toStdString();
  relinked.external_rel_path = parent_dir.isEmpty()
                                   ? info.fileName().toStdString()
                                   : QDir(parent_dir).relativeFilePath(absolute).toStdString();
  const auto modified = info.lastModified();
  relinked.external_mod_year = modified.date().year();
  relinked.external_mod_month = static_cast<std::uint8_t>(modified.date().month());
  relinked.external_mod_day = static_cast<std::uint8_t>(modified.date().day());
  relinked.external_mod_hour = static_cast<std::uint8_t>(modified.time().hour());
  relinked.external_mod_minute = static_cast<std::uint8_t>(modified.time().minute());
  relinked.external_mod_seconds = modified.time().second() + modified.time().msec() / 1000.0;
  relinked.external_file_size = static_cast<std::uint64_t>(info.size());
  relinked.dirty = true;
  auto updated_document = doc;
  auto& store = updated_document.metadata().smart_objects;
  SmartObjectLinkBlock* external_block = nullptr;
  for (auto& block : store.blocks) {
    if (block.key == "lnkE" && !block.opaque) {
      external_block = &block;
      break;
    }
  }
  if (external_block == nullptr) {
    store.blocks.push_back(SmartObjectLinkBlock{});
    external_block = &store.blocks.back();
    external_block->key = "lnkE";
  }
  external_block->original_payload.reset();
  external_block->sources.push_back(relinked);

  const auto new_stem = info.completeBaseName();
  std::function<bool(std::vector<Layer>&)> repoint_layers =
      [&](std::vector<Layer>& layers) {
    for (auto& target : layers) {
      if (!target.children().empty() && !repoint_layers(target.children())) {
        return false;
      }
      if (!layer_is_smart_object(target) || smart_object_source_uuid(target) != uuid ||
          smart_object_lock_reason(target) != "external") {
        if (layer_is_smart_object(target) &&
            smart_object_source_uuid(target) == uuid) {
          return false;
        }
        continue;
      }
      const auto placement = smart_object_placement_from_layer(target);
      if (!placement.has_value()) {
        return false;
      }
      auto updated_placement =
          rescaled_smart_object_placement(*placement, rendered_image->width(), rendered_image->height(),
                                          content_dpi > 0.0 ? content_dpi : placement->resolution);
      auto warp = smart_object_warp_from_layer(std::as_const(target));
      const bool size_changed =
          std::abs(placement->width - rendered_image->width()) > 0.5 ||
          std::abs(placement->height - rendered_image->height()) > 0.5;
      if (size_changed) {
        warp = rescaled_warp_for_replaced_contents(
            warp, *placement, rendered_image->width(),
            rendered_image->height());
        if (warp.has_value()) {
          target.metadata()[kLayerMetadataSmartObjectWarp] =
              serialize_smart_object_warp(*warp);
        }
      }
      updated_placement.uuid = relinked.uuid;
      const auto old_placed_uuid = smart_object_placed_uuid(target);
      target.metadata()[kLayerMetadataSmartObjectPlaced] =
          generate_smart_object_uuid();
      store_smart_object_placement(target, updated_placement);
      mark_layer_smart_object_block_dirty(target);
      const auto name = QString::fromStdString(target.name());
      if (!old_stem.isEmpty() && name.startsWith(old_stem)) {
        target.set_name((new_stem + name.mid(old_stem.size())).toStdString());
      }
      if (auto rendered = render_smart_object_image_preview(
              *rendered_image, updated_placement, warp,
              CanvasWidget::TransformInterpolation::Bicubic,
              std::as_const(target).smart_filter_stack(),
              Rect::from_size(updated_document.width(),
                              updated_document.height()))) {
        if (!install_smart_object_layer_preview(
                updated_document, target, std::move(*rendered), true)) {
          return false;
        }
        if (!old_placed_uuid.empty() &&
            old_placed_uuid != smart_object_placed_uuid(target) &&
            updated_document.metadata().smart_filter_effects.find_unique(
                old_placed_uuid) != nullptr &&
            !updated_document.metadata().smart_filter_effects.remove(
                old_placed_uuid)) {
          return false;
        }
      } else {
        return false;
      }
    }
    return true;
  };
  if (!repoint_layers(updated_document.layers())) {
    statusBar()->showMessage(
        tr("Could not rebuild the Smart Filter preview and cache"));
    return;
  }
  store.remove(uuid);
  push_undo_snapshot(tr("Relink Smart Object"));
  doc = std::move(updated_document);
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
  statusBar()->showMessage(tr("Relinked smart object to %1").arg(info.fileName()));
}

void MainWindow::embed_linked_smart_object() {
  if (!has_active_document()) {
    return;
  }
  auto& doc = document();
  const auto active = doc.active_layer_id();
  const auto* layer = active.has_value() ? doc.find_layer(*active) : nullptr;
  if (layer == nullptr || !layer_is_smart_object(*layer) || smart_object_lock_reason(*layer) != "external") {
    statusBar()->showMessage(tr("Select a linked smart object layer first"));
    return;
  }
  const auto uuid = smart_object_source_uuid(*layer);
  const auto* source = doc.metadata().smart_objects.find(uuid);
  if (source == nullptr || source->kind != SmartObjectSourceKind::ExternalFile) {
    statusBar()->showMessage(tr("Select a linked smart object layer first"));
    return;
  }
  const auto parent_dir = session().path.isEmpty() ? QString() : QFileInfo(session().path).absolutePath();
  const auto resolved = resolve_smart_object_external_path(*source, parent_dir);
  if (!resolved.has_value()) {
    statusBar()->showMessage(tr("Linked file %1 was not found. Use Relink to File... to point it at a new location")
                                 .arg(QString::fromStdString(source->filename)));
    return;
  }
  QFile file(*resolved);
  if (!file.open(QIODevice::ReadOnly)) {
    statusBar()->showMessage(tr("Could not read %1").arg(*resolved));
    return;
  }
  const auto raw = file.readAll();
  file.close();
  const auto filename = source->filename;
  const auto filetype = source->filetype;

  push_undo_snapshot(tr("Embed Linked Smart Object"));
  auto& store = doc.metadata().smart_objects;
  // Photoshop semantics (E13 capture): embedding assigns a FRESH element uuid in
  // lnk2 (liFD), leaves the emptied lnkE behind, clears the lock, and the per-layer
  // block key flips SoLE -> SoLd (the payload is the same 'soLD' descriptor).
  const auto fresh_uuid = generate_smart_object_uuid();
  store.remove(uuid);
  store.add_embedded(fresh_uuid, filename, filetype,
                     std::make_shared<const std::vector<std::uint8_t>>(raw.begin(), raw.end()));
  std::function<void(std::vector<Layer>&)> unlock_layers = [&](std::vector<Layer>& layers) {
    for (auto& target : layers) {
      if (!target.children().empty()) {
        unlock_layers(target.children());
      }
      if (!layer_is_smart_object(target) || smart_object_source_uuid(target) != uuid) {
        continue;
      }
      target.metadata()[kLayerMetadataSmartObject] = fresh_uuid;
      target.metadata().erase(kLayerMetadataSmartObjectLock);
      target.metadata()[kLayerMetadataSmartObjectSourceBlock] = "SoLd";
      mark_layer_smart_object_block_dirty(target);  // the Idnt repoints on save
      for (auto& block : target.unknown_psd_blocks()) {
        if (block.key == "SoLE") {
          block.key = "SoLd";
        }
      }
    }
  };
  unlock_layers(doc.layers());
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
  statusBar()->showMessage(tr("Embedded linked smart object %1").arg(QString::fromStdString(filename)));
}

void MainWindow::replace_smart_object_contents() {
  if (!has_active_document()) {
    return;
  }
  auto& doc = document();
  const auto active = doc.active_layer_id();
  auto* layer = active.has_value() ? doc.find_layer(*active) : nullptr;
  if (layer == nullptr || !layer_is_smart_object(*layer)) {
    statusBar()->showMessage(tr("Select a smart object layer first"));
    return;
  }
  if (!smart_object_lock_reason(*layer).empty()) {
    statusBar()->showMessage(tr("This smart object can only be preserved, not edited"));
    return;
  }
  const auto old_uuid = smart_object_source_uuid(*layer);
  const auto* old_source = doc.metadata().smart_objects.find(old_uuid);
  if (old_source == nullptr || old_source->kind != SmartObjectSourceKind::Embedded ||
      old_source->file_bytes == nullptr) {
    statusBar()->showMessage(tr("This smart object's contents are not embedded in the document"));
    return;
  }

  const auto path = get_open_file_name(
      this, tr("Replace Smart Object Contents"), file_dialog_initial_path(QString(), QString()),
      tr("Embeddable Files (*.psd *.psb *.png *.jpg *.jpeg *.tif *.tiff *.bmp);;All Files (*.*)"), nullptr,
      QStringLiteral("replaceSmartObjectContentsFileDialog"));
  if (path.isEmpty()) {
    return;
  }
  replace_smart_object_contents_with_path(path);
}

void MainWindow::replace_smart_object_contents_with_path(const QString& path) {
  if (!has_active_document()) {
    return;
  }
  auto& doc = document();
  const auto active = doc.active_layer_id();
  auto* layer = active.has_value() ? doc.find_layer(*active) : nullptr;
  if (layer == nullptr || !layer_is_smart_object(*layer) || !smart_object_lock_reason(*layer).empty()) {
    return;
  }
  const auto old_uuid = smart_object_source_uuid(*layer);
  const auto* old_source = doc.metadata().smart_objects.find(old_uuid);
  if (old_source == nullptr || old_source->kind != SmartObjectSourceKind::Embedded ||
      old_source->file_bytes == nullptr) {
    return;
  }
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    show_critical_message(this, tr("Replace failed"), tr("Could not read %1").arg(path),
                          QStringLiteral("replaceSmartObjectFailedMessageBox"));
    return;
  }
  const auto raw = file.readAll();
  if (raw.isEmpty()) {
    show_critical_message(this, tr("Replace failed"), tr("Could not read %1").arg(path),
                          QStringLiteral("replaceSmartObjectFailedMessageBox"));
    return;
  }

  const QFileInfo info(path);
  const auto filetype = psd_element_filetype_for_extension(info.suffix().toLower());

  SmartObjectSource replacement;
  replacement.kind = SmartObjectSourceKind::Embedded;
  replacement.uuid = generate_smart_object_uuid();
  replacement.filename = info.fileName().toStdString();
  replacement.filetype = filetype;
  replacement.creator = "    ";  // Photoshop writes four spaces for placed files
  replacement.file_bytes =
      std::make_shared<const std::vector<std::uint8_t>>(raw.begin(), raw.end());
  replacement.dirty = true;

  const auto contents_format = classify_smart_object_contents(replacement);
  if (contents_format != SmartObjectContentsFormat::PsdDocument &&
      contents_format != SmartObjectContentsFormat::QtImage) {
    show_critical_message(this, tr("Replace failed"),
                          tr("%1 is not a file type Patchy can embed and edit").arg(info.fileName()),
                          QStringLiteral("replaceSmartObjectFailedMessageBox"));
    return;
  }
  const auto rendered_image = decode_smart_object_source_image(replacement);
  if (!rendered_image.has_value()) {
    show_critical_message(this, tr("Replace failed"), tr("Could not decode %1").arg(info.fileName()),
                          QStringLiteral("replaceSmartObjectFailedMessageBox"));
    return;
  }
  const double content_dpi = smart_object_source_dpi(replacement);
  const double new_width = rendered_image->width();
  const double new_height = rendered_image->height();
  const auto old_stem = QFileInfo(QString::fromStdString(old_source->filename)).completeBaseName();
  const auto new_stem = info.completeBaseName();

  // Photoshop semantics (E5 captures): a fresh element replaces the old one and
  // EVERY layer referencing the old uuid repoints to it, each rebuilt about its own
  // quad center with the content-inch map preserved; the old element is removed and
  // layer names swap the old source stem for the new one.
  auto updated_document = doc;
  auto& store = updated_document.metadata().smart_objects;
  store.add_embedded(replacement.uuid, replacement.filename, replacement.filetype, replacement.file_bytes);
  if (auto* added = store.find(replacement.uuid); added != nullptr) {
    added->creator = replacement.creator;
  }

  std::function<bool(std::vector<Layer>&)> repoint_layers =
      [&](std::vector<Layer>& layers) {
    for (auto& target : layers) {
      if (!target.children().empty() && !repoint_layers(target.children())) {
        return false;
      }
      if (!layer_is_smart_object(target) || smart_object_source_uuid(target) != old_uuid ||
          !smart_object_lock_reason(target).empty()) {
        if (layer_is_smart_object(target) &&
            smart_object_source_uuid(target) == old_uuid) {
          return false;
        }
        continue;
      }
      const auto placement = smart_object_placement_from_layer(target);
      if (!placement.has_value()) {
        return false;
      }
      auto updated_placement = rescaled_smart_object_placement(*placement, new_width, new_height, content_dpi);
      auto warp = smart_object_warp_from_layer(std::as_const(target));
      const bool size_changed =
          std::abs(placement->width - new_width) > 0.5 ||
          std::abs(placement->height - new_height) > 0.5;
      if (size_changed) {
        warp = rescaled_warp_for_replaced_contents(
            warp, *placement, new_width, new_height);
        if (warp.has_value()) {
          target.metadata()[kLayerMetadataSmartObjectWarp] =
              serialize_smart_object_warp(*warp);
        }
      }
      updated_placement.uuid = replacement.uuid;
      const auto old_placed_uuid = smart_object_placed_uuid(target);
      target.metadata()[kLayerMetadataSmartObjectPlaced] =
          generate_smart_object_uuid();
      store_smart_object_placement(target, updated_placement);
      mark_layer_smart_object_block_dirty(target);
      const auto name = QString::fromStdString(target.name());
      if (!old_stem.isEmpty() && name.startsWith(old_stem)) {
        target.set_name((new_stem + name.mid(old_stem.size())).toStdString());
      }
      if (auto rendered = render_smart_object_image_preview(
              *rendered_image, updated_placement, warp,
              CanvasWidget::TransformInterpolation::Bicubic,
              std::as_const(target).smart_filter_stack(),
              Rect::from_size(updated_document.width(),
                              updated_document.height()))) {
        if (!install_smart_object_layer_preview(
                updated_document, target, std::move(*rendered), true)) {
          return false;
        }
        if (!old_placed_uuid.empty() &&
            old_placed_uuid != smart_object_placed_uuid(target) &&
            updated_document.metadata().smart_filter_effects.find_unique(
                old_placed_uuid) != nullptr &&
            !updated_document.metadata().smart_filter_effects.remove(
                old_placed_uuid)) {
          return false;
        }
      } else {
        return false;
      }
    }
    return true;
  };
  if (!repoint_layers(updated_document.layers())) {
    statusBar()->showMessage(
        tr("Could not rebuild the Smart Filter preview and cache"));
    return;
  }
  store.remove(old_uuid);
  push_undo_snapshot(tr("Replace Smart Object Contents"));
  doc = std::move(updated_document);

  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
  statusBar()->showMessage(tr("Replaced smart object contents with %1").arg(info.fileName()));
}

void MainWindow::convert_to_smart_object() {
  if (!has_active_document()) {
    return;
  }
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return;
  }
  finish_active_text_editor();
  auto& doc = document();
  const auto selected_ids = root_drop_layer_ids(doc.layers(), selected_or_active_layer_ids());
  if (selected_ids.empty()) {
    statusBar()->showMessage(tr("Select layers to convert to a smart object"));
    return;
  }
  if (std::any_of(selected_ids.begin(), selected_ids.end(),
                  [&doc](LayerId id) {
                    const auto* layer = std::as_const(doc).find_layer(id);
                    return layer != nullptr &&
                           layer_tree_contains_smart_filters(*layer);
                  })) {
    statusBar()->showMessage(
        tr("Smart Objects with Smart Filters cannot be wrapped in another Smart Object yet"));
    return;
  }
  // Re-order the selection by tree walk (the layers vector is bottom-to-top) so the
  // child stacks correctly and the TOPMOST selected layer keeps its slot and name.
  const std::set<LayerId> selected_set(selected_ids.begin(), selected_ids.end());
  std::vector<LayerId> ids;
  Rect content;
  std::function<void(const std::vector<Layer>&)> scan = [&](const std::vector<Layer>& layers) {
    for (const auto& layer : layers) {
      if (selected_set.contains(layer.id())) {
        ids.push_back(layer.id());
        content = unite_rect(content, layer_render_bounds(layer));
      }
      if (!layer.children().empty()) {
        scan(layer.children());
      }
    }
  };
  scan(std::as_const(doc).layers());
  if (ids.empty()) {
    return;
  }
  const auto top_id = ids.back();
  const auto top_name = std::as_const(doc).find_layer(top_id)->name();
  // The child canvas is the union of the selected layers' render bounds (Photoshop
  // uses the pixel bounds; render bounds additionally keep layer-style effects
  // inside the child canvas so the preview matches the old composite).
  if (content.empty()) {
    statusBar()->showMessage(tr("The selected layers have no pixels to convert"));
    return;
  }

  push_undo_snapshot(tr("Convert to Smart Object"));

  // Move copies of the selected trees into the child document, translated so the
  // union origin becomes the child origin.
  Document child(content.width, content.height, doc.format());
  child.print_settings() = doc.print_settings();
  const int dx = -content.x;
  const int dy = -content.y;
  std::function<void(Layer&)> translate_into_child = [&](Layer& layer) {
    auto bounds = layer.bounds();
    bounds.x += dx;
    bounds.y += dy;
    layer.set_bounds(bounds);
    if (layer.mask().has_value()) {
      layer.mask()->bounds.x += dx;
      layer.mask()->bounds.y += dy;
    }
    translate_moved_layer_metadata(layer, dx, dy, child.width(), child.height());
    for (auto& nested : layer.children()) {
      translate_into_child(nested);
    }
  };
  for (const auto id : ids) {
    const auto* layer = doc.find_layer(id);
    if (layer == nullptr) {
      continue;
    }
    auto copy = *layer;
    translate_into_child(copy);
    // Nested smart objects keep working: their sources travel into the child's store
    // (the parent keeps its copies; unreferenced elements are never pruned, PS parity).
    std::vector<SmartObjectSource> referenced;
    collect_referenced_smart_object_sources(copy, doc.metadata().smart_objects, referenced);
    for (const auto& nested_source : referenced) {
      child.metadata().smart_objects.adopt(nested_source);
    }
    std::vector<PatternResource> referenced_patterns;
    collect_referenced_pattern_resources(copy, doc.metadata().patterns, referenced_patterns);
    for (const auto& nested_pattern : referenced_patterns) {
      PatternResource adopted = nested_pattern;
      adopted.provenance = PatternProvenance::Authored;  // the child file has no raw block for it
      child.metadata().patterns.adopt(adopted);
    }
    child.add_layer(std::move(copy));
  }

  std::vector<std::uint8_t> child_bytes;
  try {
    psd::WriteOptions write_options;
    write_options.large_document = true;  // Photoshop embeds .psb for converted layers
    child_bytes = psd::DocumentIo::write_layered_rgb8(child, write_options);
  } catch (const std::exception& error) {
    show_critical_message(this, tr("Convert failed"), QString::fromUtf8(error.what()),
                          QStringLiteral("convertSmartObjectFailedMessageBox"));
    undo();
    return;
  }
  const auto preview = qimage_from_document(child, true).convertToFormat(QImage::Format_RGBA8888);

  const auto uuid = generate_smart_object_uuid();
  const auto filename = top_name.empty() ? std::string("Layer.psb") : top_name + ".psb";
  doc.metadata().smart_objects.add_embedded(
      uuid, filename, "8BPB", std::make_shared<const std::vector<std::uint8_t>>(std::move(child_bytes)));

  SmartObjectPlacement placement;
  placement.uuid = uuid;
  placement.transform = {static_cast<double>(content.x),
                         static_cast<double>(content.y),
                         static_cast<double>(content.x + content.width),
                         static_cast<double>(content.y),
                         static_cast<double>(content.x + content.width),
                         static_cast<double>(content.y + content.height),
                         static_cast<double>(content.x),
                         static_cast<double>(content.y + content.height)};
  placement.width = content.width;
  placement.height = content.height;
  placement.resolution = doc.print_settings().horizontal_ppi;

  const auto target_location = find_layer_location(doc.layers(), top_id);
  if (!target_location.has_value()) {
    refresh_layer_list();
    return;
  }
  Layer replacement(top_id, top_name, pixels_from_image_rgba(preview));
  replacement.set_bounds(content);
  const auto placed_instance = generate_smart_object_uuid();
  set_layer_smart_object_metadata(replacement, placement, placed_instance, "SoLd", "",
                                  kSmartObjectRasterStatusPatchy);
  // The authored SoLd rides the normal preserve-unless-edited machinery; later
  // moves/transforms patch it in place like a Photoshop-authored one.
  replacement.unknown_psd_blocks().push_back(
      UnknownPsdBlock{"SoLd", psd::author_placed_layer_sold_payload(placement, placed_instance)});
  (*target_location->siblings)[target_location->index] = std::move(replacement);
  for (const auto id : ids) {
    if (id != top_id) {
      doc.remove_layer(id);
    }
  }
  doc.set_active_layer(top_id);
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
  statusBar()->showMessage(tr("Converted to a smart object; click its badge to edit its contents"));
}

void MainWindow::new_smart_object_via_copy() {
  if (!has_active_document()) {
    return;
  }
  auto& doc = document();
  const auto active = doc.active_layer_id();
  const auto* layer = active.has_value() ? doc.find_layer(*active) : nullptr;
  if (layer == nullptr || !layer_is_smart_object(*layer)) {
    statusBar()->showMessage(tr("Select a smart object layer first"));
    return;
  }
  const auto* source = doc.metadata().smart_objects.find(smart_object_source_uuid(*layer));
  if (source == nullptr || source->kind != SmartObjectSourceKind::Embedded || source->file_bytes == nullptr) {
    statusBar()->showMessage(tr("This smart object's contents are not embedded in the document"));
    return;
  }
  const auto placement = smart_object_placement_from_layer(*layer);
  if (!placement.has_value()) {
    statusBar()->showMessage(tr("This smart object can only be preserved, not edited"));
    return;
  }
  if (!smart_filter_records_available_for_clone(
          *layer, doc.metadata().smart_filter_effects)) {
    statusBar()->showMessage(
        tr("Smart Filter cache data could not be duplicated safely"));
    return;
  }

  push_undo_snapshot(tr("New Smart Object via Copy"));

  // Photoshop's via-copy semantics (E8): the element is CLONED under a fresh uuid, so
  // the copy edits independently (a plain duplicate would keep tracking the source).
  const auto fresh_uuid = generate_smart_object_uuid();
  auto& cloned = doc.metadata().smart_objects.add_embedded(fresh_uuid, source->filename, source->filetype,
                                                           source->file_bytes);
  cloned.creator = source->creator;

  const auto location = find_layer_location(doc.layers(), *active);
  if (!location.has_value()) {
    refresh_layer_list();
    return;
  }
  auto copy = clone_layer_tree_with_document_ids(doc, *layer);
  if (!copy.has_value()) {
    undo();
    statusBar()->showMessage(
        tr("Smart Filter cache data could not be duplicated safely"));
    return;
  }
  copy->set_name(layer->name() + " copy");
  auto copied_placement = *placement;
  copied_placement.uuid = fresh_uuid;
  store_smart_object_placement(*copy, copied_placement);
  mark_layer_smart_object_block_dirty(*copy);  // the preserved SoLd's Idnt repoints on save
  const auto copy_id = copy->id();
  location->siblings->insert(location->siblings->begin() + static_cast<std::ptrdiff_t>(location->index) + 1,
                             std::move(*copy));
  doc.set_active_layer(copy_id);
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
  statusBar()->showMessage(tr("Created an independent smart object copy"));
}

void MainWindow::place_embedded_file() {
  if (!has_active_document()) {
    return;
  }
  const auto path = get_open_file_name(
      this, tr("Place Embedded"), file_dialog_initial_path(QString(), QString()),
      tr("Embeddable Files (*.psd *.psb *.png *.jpg *.jpeg *.tif *.tiff *.bmp);;All Files (*.*)"), nullptr,
      QStringLiteral("placeEmbeddedFileDialog"));
  if (path.isEmpty()) {
    return;
  }
  place_embedded_file_with_path(path);
}

void MainWindow::place_embedded_file_with_path(const QString& path) {
  if (!has_active_document()) {
    return;
  }
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    show_critical_message(this, tr("Place failed"), tr("Could not read %1").arg(path),
                          QStringLiteral("placeEmbeddedFailedMessageBox"));
    return;
  }
  const auto raw = file.readAll();
  const QFileInfo info(path);
  const auto filetype = psd_element_filetype_for_extension(info.suffix().toLower());

  SmartObjectSource placed;
  placed.kind = SmartObjectSourceKind::Embedded;
  placed.uuid = generate_smart_object_uuid();
  placed.filename = info.fileName().toStdString();
  placed.filetype = filetype;
  placed.creator = "    ";  // Photoshop writes four spaces for placed files
  placed.file_bytes = std::make_shared<const std::vector<std::uint8_t>>(raw.begin(), raw.end());
  placed.dirty = true;

  const auto contents_format = classify_smart_object_contents(placed);
  if (contents_format == SmartObjectContentsFormat::Undecodable) {
    show_critical_message(this, tr("Place failed"), tr("Could not decode %1").arg(info.fileName()),
                          QStringLiteral("placeEmbeddedFailedMessageBox"));
    return;
  }
  const auto image = decode_smart_object_source_image(placed);
  if (!image.has_value()) {
    show_critical_message(this, tr("Place failed"), tr("Could not decode %1").arg(info.fileName()),
                          QStringLiteral("placeEmbeddedFailedMessageBox"));
    return;
  }

  auto& doc = document();
  // E2 placement rule: physical pixels (content px scaled by doc_ppi/content_dpi) land
  // 1:1 centered when they fit, else scaled down to fit the canvas, centered.
  const double content_dpi = smart_object_source_dpi(placed);
  const double doc_ppi = doc.print_settings().horizontal_ppi > 0.0 ? doc.print_settings().horizontal_ppi : 72.0;
  const double physical_width = image->width() * doc_ppi / content_dpi;
  const double physical_height = image->height() * doc_ppi / content_dpi;
  double scale = 1.0;
  if (physical_width > doc.width() || physical_height > doc.height()) {
    scale = std::min(doc.width() / physical_width, doc.height() / physical_height);
  }
  const double placed_width = physical_width * scale;
  const double placed_height = physical_height * scale;
  const double left = (doc.width() - placed_width) / 2.0;
  const double top = (doc.height() - placed_height) / 2.0;

  push_undo_snapshot(tr("Place Embedded"));

  doc.metadata().smart_objects.add_embedded(placed.uuid, placed.filename, placed.filetype, placed.file_bytes);
  if (auto* added = doc.metadata().smart_objects.find(placed.uuid); added != nullptr) {
    added->creator = placed.creator;
  }

  SmartObjectPlacement placement;
  placement.uuid = placed.uuid;
  placement.transform = {left,        top,          left + placed_width, top,
                         left + placed_width, top + placed_height, left, top + placed_height};
  placement.width = image->width();
  placement.height = image->height();
  placement.resolution = content_dpi;

  auto rendered = render_smart_object_pixels(*image, placement, CanvasWidget::TransformInterpolation::Bicubic);
  if (!rendered.has_value()) {
    undo();
    show_critical_message(this, tr("Place failed"), tr("Could not decode %1").arg(info.fileName()),
                          QStringLiteral("placeEmbeddedFailedMessageBox"));
    return;
  }
  // add_pixel_layer requires full-canvas buffers; placed layers carry tight bounds.
  Layer placed_layer(doc.allocate_layer_id(), info.completeBaseName().toStdString(),
                     pixels_from_image_rgba(rendered->image));
  placed_layer.set_bounds(rendered->bounds);
  const auto placed_instance = generate_smart_object_uuid();
  set_layer_smart_object_metadata(placed_layer, placement, placed_instance, "SoLd", "",
                                  kSmartObjectRasterStatusPatchy);
  placed_layer.unknown_psd_blocks().push_back(
      UnknownPsdBlock{"SoLd", psd::author_placed_layer_sold_payload(placement, placed_instance)});
  auto& layer = doc.add_layer(std::move(placed_layer));
  doc.set_active_layer(layer.id());
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
  statusBar()->showMessage(tr("Placed %1 as a smart object").arg(info.fileName()));
}

}  // namespace patchy::ui
