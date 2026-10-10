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
#include "core/vector_shape.hpp"
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
#include "ui/modifier_names.hpp"
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
#include "ui/image_sequence_dialog.hpp"
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

// Icon resources live in the static patchy_ui library; force registration before first use.
int qInitResources_icons();

namespace patchy::ui {

namespace {

// The image one layer renders from: vector artwork rasterized at the placement's own
// scale when `vector_contents` carries it and no warp mesh needs the natural size,
// else the shared natural-size image.
QImage smart_object_image_for_placement(const QImage& natural_image, const SmartObjectSource* vector_contents,
                                        const SmartObjectPlacement& placement,
                                        const std::optional<SmartObjectWarp>& warp) {
  if (vector_contents != nullptr && (!warp.has_value() || warp->mesh_xs.empty())) {
    if (auto vector = render_smart_object_vector_contents(*vector_contents, placement); vector.has_value()) {
      return std::move(*vector);
    }
  }
  return natural_image;
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

// Convert to Layers under a scaled, rotated, or flipped placement: maps one
// unpacked layer tree from the contents' canvas into the document the way the
// multi-target Free Transform commit maps its targets. Pixels and raster masks
// resample, text re-renders through its composed transform (keeping the resampled
// raster when its font is missing), vector data transforms exactly, and nested
// placements map their quads and re-render from their own sources.
// A text layer's text-local -> document mapping: Patchy's stored transform, then
// the imported Photoshop one, else the implicit translate(bounds).
LayerAffineTransform unpacked_text_transform(const Layer& layer) {
  for (const auto* key : {kLayerMetadataTextTransform, kLayerMetadataPsdTextTransform}) {
    if (const auto found = layer.metadata().find(key); found != layer.metadata().end()) {
      if (const auto parsed = parse_layer_affine_transform(found->second); parsed.has_value()) {
        return *parsed;
      }
    }
  }
  const auto bounds = layer.bounds();
  return LayerAffineTransform{1.0, 0.0, 0.0, 1.0, static_cast<double>(bounds.x), static_cast<double>(bounds.y)};
}

void map_unpacked_layer_tree(Document& document, Layer& layer, const QTransform& mapping,
                             CanvasWidget::TransformInterpolation interpolation) {
  const std::array<double, 6> matrix{mapping.m11(), mapping.m12(), mapping.m21(),
                                     mapping.m22(), mapping.dx(),  mapping.dy()};
  if (const auto& mask = std::as_const(layer).mask(); mask.has_value() && !mask->pixels.empty()) {
    auto resampled = resample_transformed_gray8(
        mask->pixels, mask->default_color,
        QTransform::fromTranslate(mask->bounds.x, mask->bounds.y) * mapping, interpolation);
    auto updated = *mask;
    updated.pixels = std::move(resampled.pixels);
    updated.bounds = resampled.bounds;
    layer.set_mask(std::move(updated));
  }
  if (layer.kind() == LayerKind::Group) {
    for (auto& child : layer.children()) {
      map_unpacked_layer_tree(document, child, mapping, interpolation);
    }
    return;
  }
  const auto text_layer = layer_is_text(std::as_const(layer));
  const auto vector_shape = layer_is_vector_shape(std::as_const(layer));
  const auto old_bounds = std::as_const(layer).bounds();
  if (!vector_shape && !std::as_const(layer).pixels().empty()) {
    const auto original_text_transform =
        text_layer ? unpacked_text_transform(std::as_const(layer)) : LayerAffineTransform{};
    const auto resampled = resample_transformed_rgba8(
        qimage_from_pixel_buffer(std::as_const(layer).pixels()),
        QTransform::fromTranslate(old_bounds.x, old_bounds.y) * mapping, interpolation);
    layer.set_pixels(pixels_from_image_native(resampled.image));
    layer.set_bounds(resampled.bounds);
    if (text_layer) {
      const LayerAffineTransform outer{matrix[0], matrix[1], matrix[2], matrix[3], matrix[4], matrix[5]};
      layer.metadata()[kLayerMetadataTextTransform] =
          serialize_layer_affine_transform(compose_layer_affine_transform(outer, original_text_transform));
      if (rerender_text_layer_through_stored_transform(layer)) {
        layer.metadata()[kLayerMetadataTextRasterStatus] = "patchy_raster";
      }
    }
  }
  if (vector_shape || std::as_const(layer).vector_mask() != nullptr) {
    transform_layer_vector_data(document, layer, matrix, Rect::from_size(document.width(), document.height()));
  }
  if (layer_is_smart_object(std::as_const(layer))) {
    if (const auto placement = smart_object_placement_from_layer(std::as_const(layer)); placement.has_value()) {
      store_smart_object_placement(layer, transformed_smart_object_placement(*placement, matrix));
      mark_layer_smart_object_block_dirty(layer);
      layer.metadata()[kLayerMetadataSmartObjectRasterStatus] = kSmartObjectRasterStatusPatchy;
      if (smart_object_lock_reason(std::as_const(layer)).empty()) {
        // A failed re-render keeps the resampled preview above.
        static_cast<void>(refresh_smart_object_layer_preview(document, layer, interpolation, false));
      }
    }
  }
}

}  // namespace

bool MainWindow::refuse_document_geometry_change() {
  const auto& doc = std::as_const(document());
  if (document_contains_smart_filters(doc)) {
    show_status_error(
        tr("Rasterize Smart Filters before changing document geometry"));
    return true;
  }
  if (document_contains_unparsed_smart_objects(doc)) {
    show_status_error(
        tr("Rasterize Smart Objects before changing document geometry"));
    return true;
  }
  return false;
}

QString MainWindow::linked_smart_object_problem_message(const Document& document, const Layer& layer,
                                                        const QString& parent_document_dir) const {
  const auto problem = smart_object_link_problem(document, layer, parent_document_dir);
  if (!problem.has_value()) {
    return QString();
  }
  const auto* source = document.metadata().smart_objects.find(smart_object_source_uuid(layer));
  const auto file_name = QString::fromStdString(source != nullptr ? source->filename : layer.name());
  // The same wording the open-time notice uses (QObject context), so no new catalog entry.
  return *problem == SmartObjectLinkProblem::missing ? QObject::tr("Linked file %1 was not found").arg(file_name)
                                                     : tr("Could not decode %1").arg(file_name);
}

void MainWindow::rerender_smart_object_previews(DocumentSession& target) {
  auto& document = target.document;
  const auto interpolation = target.canvas != nullptr
                                 ? target.canvas->transform_interpolation()
                                 : CanvasWidget::TransformInterpolation::Bicubic;
  const auto parent_document_dir = target.path.isEmpty() ? QString() : QFileInfo(target.path).absolutePath();
  // One read and decode per source however many layers place it (six linked logos
  // from three SVGs read three files).
  SmartObjectSourceRenderCache cache;
  QString problem;  // the first linked file that kept its resampled preview
  // Const walk on purpose: the non-const children() accessor bumps every visited
  // layer's revisions (docs/performance.md), so a mutable traversal would invalidate
  // every thumbnail and style-mask cache in the document. Only re-rendered layers are
  // cast back, and their bumps are real edits. Warp-, filter- and legacy-locked
  // placements are skipped for the same reason Free Transform skips them: no
  // re-render exists. A LINKED placement re-renders from its file exactly like an
  // embedded one (Photoshop's Image Size re-renders linked smart objects too); a
  // file that is missing or cannot be decoded keeps the resampled preview the
  // geometry operation already produced and is reported once on the status bar.
  std::function<void(const std::vector<Layer>&)> refresh_layers =
      [&](const std::vector<Layer>& layers) {
        for (const auto& const_layer : layers) {
          if (!const_layer.children().empty()) {
            refresh_layers(const_layer.children());
          }
          if (!layer_is_smart_object(const_layer)) {
            continue;
          }
          const auto lock = smart_object_lock_reason(const_layer);
          if (!lock.empty() && lock != "external") {
            continue;
          }
          if (refresh_smart_object_layer_preview(document, const_cast<Layer&>(const_layer), interpolation, true,
                                                 parent_document_dir, &cache)) {
            continue;
          }
          if (problem.isEmpty() && lock == "external") {
            problem = linked_smart_object_problem_message(std::as_const(document), const_layer, parent_document_dir);
          }
        }
      };
  refresh_layers(std::as_const(document).layers());
  if (!problem.isEmpty()) {
    show_status_error(problem);
  }
}

void MainWindow::export_smart_object_contents() {
  if (!has_active_document()) {
    return;
  }
  select_only_layer_if_none_active();
  const auto active = document().active_layer_id();
  const auto* layer = active.has_value() ? document().find_layer(*active) : nullptr;
  if (layer == nullptr || !layer_is_smart_object(*layer)) {
    show_status_error(tr("Select a smart object layer first"));
    return;
  }
  const auto* source = document().metadata().smart_objects.find(smart_object_source_uuid(*layer));
  if (source == nullptr || source->file_bytes == nullptr) {
    show_status_error(tr("This smart object has no embedded contents to export"));
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
  offer_browser_download_for_saved_file(path);
  remember_save_directory_for_path(path);
  statusBar()->showMessage(tr("Exported smart object contents to %1").arg(path));
}

void MainWindow::open_smart_object_contents() {
  if (!has_active_document()) {
    return;
  }
  select_only_layer_if_none_active();
  const auto active = document().active_layer_id();
  const auto* layer = active.has_value() ? document().find_layer(*active) : nullptr;
  if (layer == nullptr || !layer_is_smart_object(*layer)) {
    show_status_error(tr("Select a smart object layer first"));
    return;
  }
  const auto lock_reason = smart_object_lock_reason(*layer);
  if (!lock_reason.empty() && lock_reason != "external") {
    if (lock_reason == "filters") {
      show_status_error(
          tr("This smart object has Smart Filters; Patchy keeps Photoshop's preview (rasterize to edit pixels)"));
    } else if (lock_reason == "warp" || lock_reason == "non_affine") {
      show_status_error(
          tr("This smart object has a warp or perspective transform; Patchy keeps Photoshop's preview"));
    } else {
      show_status_error(tr("This smart object can only be preserved, not edited"));
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
      show_status_error(tr("This smart object's contents are not embedded in the document"));
      return;
    }
    const auto parent_dir =
        parent_session.path.isEmpty() ? QString() : QFileInfo(parent_session.path).absolutePath();
    const auto resolved = resolve_smart_object_external_path(*source, parent_dir);
    if (!resolved.has_value()) {
      show_status_error(tr("Linked file %1 was not found. Use Relink to File... to point it at a new location")
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
        resolve_modifier_names(tr("Editing linked file. Save (%CTRL%+S) writes %1 and updates %2"))
            .arg(QString::fromStdString(source->filename), parent_title));
    return;
  }
  if (source == nullptr || source->kind != SmartObjectSourceKind::Embedded || source->file_bytes == nullptr) {
    show_status_error(tr("This smart object's contents are not embedded in the document"));
    return;
  }
  const auto contents_format = classify_smart_object_contents(*source);
  if (contents_format != SmartObjectContentsFormat::PsdDocument &&
      contents_format != SmartObjectContentsFormat::QtImage) {
    show_status_error(
        tr("Patchy can't re-encode %1 contents; use Export Smart Object Contents or rasterize the layer")
            .arg(QString::fromStdString(source->filename)));
    return;
  }
  auto child_document = decode_smart_object_source_document(*source);
  if (!child_document.has_value()) {
    show_status_error(tr("Could not decode the embedded smart object contents"));
    return;
  }
  const auto file_name =
      QString::fromStdString(source->filename.empty() ? std::string("contents") : source->filename);
  const auto parent_title = parent_session.title.isEmpty() ? tr("Untitled") : parent_session.title;
  add_document_session(std::move(*child_document), tr("%1 (embedded in %2)").arg(file_name, parent_title),
                       QString(), tr("Open"));
  auto& child_session = session();
  child_session.smart_object_link = DocumentSession::SmartObjectLink{parent_session_id, uuid};
  statusBar()->showMessage(
      resolve_modifier_names(tr("Editing smart object contents. Save (%CTRL%+S) applies them back to %1"))
          .arg(parent_title));
}

void MainWindow::prompt_paint_on_smart_object(CanvasWidget* canvas, LayerId layer_id) {
  auto* owner_session = session_for_canvas(canvas);
  if (owner_session == nullptr || owner_session != active_session()) {
    return;
  }
  auto& doc = document();
  const auto* layer = doc.find_layer(layer_id);
  if (layer == nullptr || !layer_is_smart_object(*layer)) {
    return;
  }
  if (cli_automation_mode_) {
    // A prompt would block unattended runs; keep the old refusal there.
    show_status_error(tr("Smart object contents can't be painted. Rasterize the layer to edit its pixels."));
    return;
  }

  // Offer Edit Contents only when open_smart_object_contents() could succeed
  // (same guards): editable embedded sources in a re-encodable format, or
  // linked external files. A button that only yields an error is a dead end.
  const auto lock_reason = smart_object_lock_reason(*layer);
  const auto* source = doc.metadata().smart_objects.find(smart_object_source_uuid(*layer));
  bool can_edit_contents = false;
  if (lock_reason.empty()) {
    if (source != nullptr && source->kind == SmartObjectSourceKind::Embedded && source->file_bytes != nullptr) {
      const auto contents_format = classify_smart_object_contents(*source);
      can_edit_contents = contents_format == SmartObjectContentsFormat::PsdDocument ||
                          contents_format == SmartObjectContentsFormat::QtImage;
    }
  } else if (lock_reason == "external") {
    can_edit_contents = source != nullptr && source->kind == SmartObjectSourceKind::ExternalFile;
  }

  QMessageBox box(this);
  box.setObjectName(QStringLiteral("paintSmartObjectMessageBox"));
  box.setIcon(QMessageBox::Question);
  box.setWindowTitle(tr("Paint on Smart Object?"));
  box.setText(
      tr("\"%1\" is a smart object, so its pixels can't be painted directly.")
          .arg(QString::fromStdString(layer->name())));
  box.setInformativeText(
      can_edit_contents
          ? tr("Rasterize the layer to paint on its pixels, or open the smart object's contents in "
               "their own tab and draw there.")
          : tr("Rasterize the layer to paint on its pixels. This smart object's contents can't be "
               "edited in Patchy."));
  QPushButton* edit_button =
      can_edit_contents ? box.addButton(tr("Edit Contents"), QMessageBox::AcceptRole) : nullptr;
  auto* rasterize_button = box.addButton(tr("Rasterize"), QMessageBox::DestructiveRole);
  auto* cancel_button = box.addButton(QMessageBox::Cancel);
  box.setDefaultButton(edit_button != nullptr ? edit_button : cancel_button);
  exec_dialog(box);
  if (box.clickedButton() == nullptr || box.clickedButton() == cancel_button) {
    return;
  }
  if (box.clickedButton() == rasterize_button) {
    rasterize_layer_ids({layer_id});
    const auto* rasterized = doc.find_layer(layer_id);
    if (rasterized != nullptr && !layer_is_smart_object(*rasterized)) {
      // The triggering press was consumed by the prompt, so nothing painted yet.
      statusBar()->showMessage(tr("Rasterized layer. Paint again to draw on it."));
    }
    return;
  }
  if (doc.active_layer_id() != layer_id) {
    doc.set_active_layer(layer_id);
    refresh_layer_list();
  }
  open_smart_object_contents();
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
    show_status_error(tr("The smart object no longer exists in %1").arg(parent->title));
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
      show_critical_message(this, tr("Save failed"), translate_data_text(error.what()),
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
    show_status_error(tr("These contents can't be re-encoded"));
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

  // One parent undo step for the whole commit (push_undo_snapshot itself only
  // operates on the active session).
  record_history_push(*parent,
                      DocumentSession::HistoryState{
                          parent->document, parent->revision,
                          parent->canvas != nullptr ? parent->canvas->capture_selection_snapshot()
                                                    : CanvasWidget::SelectionSnapshot{},
                          {}, 0},
                      tr("Edit Smart Object Contents"));
  parent->document = std::move(updated_document);
  if (child_session.smart_object_link.has_value()) {
    child_session.smart_object_link->source_uuid_history.push_back(link.source_uuid);
    child_session.smart_object_link->source_uuid_history.push_back(refreshed_source_uuid);
    child_session.smart_object_link->source_uuid = refreshed_source_uuid;
  }

  if (parent->canvas != nullptr) {
    parent->canvas->document_changed();
  }
  mark_session_modified(*parent);
  // The commit usually runs while the CHILD tab is active; the shared panel
  // mirrors the active session only, so refresh only if the parent is it.
  if (parent == active_session()) {
    refresh_history_panel();
  }

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
    std::string_view replacement_source_uuid, const SmartObjectSource* vector_contents) {
  const double new_width = rendered_image.width();
  const double new_height = rendered_image.height();
  // Const walk on purpose: the non-const children() accessor bumps every
  // visited layer's revisions on access (see docs/performance.md), so the old
  // mutable traversal invalidated every thumbnail and
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
              smart_object_image_for_placement(rendered_image, vector_contents, updated_placement, warp),
              updated_placement, warp,
              CanvasWidget::TransformInterpolation::Bicubic,
              std::as_const(layer).smart_filter_stack(),
              target_document)) {
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

  // Decode via a probe that carries the fresh bytes (external sources keep none).
  const auto probe = load_smart_object_file_probe(child_session.path);
  if (!probe.has_value()) {
    return;
  }
  const auto rendered_image = decode_smart_object_source_image(*probe);
  if (!rendered_image.has_value()) {
    return;
  }
  const auto content_dpi =
      psd::DocumentIo::can_read({probe->file_bytes->data(), probe->file_bytes->size()})
          ? smart_object_source_dpi(*probe)
          : 0.0;

  auto updated_document = parent->document;
  auto* updated_source =
      updated_document.metadata().smart_objects.find(link.source_uuid);
  if (updated_source == nullptr) {
    return;
  }
  set_smart_object_link_target(*updated_source, QFileInfo(child_session.path),
                               parent->path.isEmpty() ? QString() : QFileInfo(parent->path).absolutePath());
  if (!refresh_smart_object_layers_for_source(
          updated_document, link.source_uuid, *rendered_image, content_dpi,
          true, false, {}, &*probe)) {
    show_status_error(
        tr("Could not rebuild the Smart Filter preview and cache"));
    return;
  }

  // One parent undo step for the refresh (push_undo_snapshot itself only
  // operates on the active session).
  record_history_push(*parent,
                      DocumentSession::HistoryState{
                          parent->document, parent->revision,
                          parent->canvas != nullptr ? parent->canvas->capture_selection_snapshot()
                                                    : CanvasWidget::SelectionSnapshot{},
                          {}, 0},
                      tr("Update Smart Object Content"));
  parent->document = std::move(updated_document);
  if (parent->canvas != nullptr) {
    parent->canvas->document_changed();
  }
  mark_session_modified(*parent);
  if (parent == active_session()) {
    refresh_history_panel();
  }
  statusBar()->showMessage(tr("Saved %1 and updated %2")
                               .arg(QFileInfo(child_session.path).fileName(),
                                    parent->title.isEmpty() ? tr("Untitled") : parent->title));
}

void MainWindow::update_smart_object_content() {
  if (!has_active_document()) {
    return;
  }
  auto& doc = document();
  select_only_layer_if_none_active();
  const auto active = doc.active_layer_id();
  const auto* layer = active.has_value() ? doc.find_layer(*active) : nullptr;
  if (layer == nullptr || !layer_is_smart_object(*layer) || smart_object_lock_reason(*layer) != "external") {
    show_status_error(tr("Select a linked smart object layer first"));
    return;
  }
  const auto* source = doc.metadata().smart_objects.find(smart_object_source_uuid(*layer));
  const auto file_name = source != nullptr ? QString::fromStdString(source->filename) : QString();
  QString error;
  const auto updated = update_linked_smart_object(
      session(), *active,
      [this] {
        push_undo_snapshot(tr("Update Smart Object Content"));
        return true;
      },
      &error);
  if (updated == 0) {
    show_status_error(error);
    return;
  }
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
  statusBar()->showMessage(tr("Updated smart object content from %1").arg(file_name));
}

int MainWindow::update_linked_smart_object(DocumentSession& target, LayerId layer_id,
                                           const std::function<bool()>& before_mutation, QString* error) {
  const auto fail = [error](const QString& message) {
    if (error != nullptr) {
      *error = message;
    }
    return 0;
  };
  const auto& doc = std::as_const(target.document);
  const auto* layer = doc.find_layer(layer_id);
  if (layer == nullptr || !layer_is_smart_object(*layer) || smart_object_lock_reason(*layer) != "external") {
    return fail(tr("This layer is not a linked smart object"));
  }
  const auto uuid = smart_object_source_uuid(*layer);
  const auto* source = doc.metadata().smart_objects.find(uuid);
  if (source == nullptr || source->kind != SmartObjectSourceKind::ExternalFile) {
    return fail(tr("This layer is not a linked smart object"));
  }
  const auto parent_dir = target.path.isEmpty() ? QString() : QFileInfo(target.path).absolutePath();
  const auto resolved = resolve_smart_object_external_path(*source, parent_dir);
  if (!resolved.has_value()) {
    return fail(tr("Linked file %1 was not found. Use Relink to File... to point it at a new location")
                    .arg(QString::fromStdString(source->filename)));
  }
  const auto probe = load_smart_object_file_probe(*resolved);
  if (!probe.has_value()) {
    return fail(tr("Could not read %1").arg(*resolved));
  }
  const auto rendered_image = decode_smart_object_source_image(*probe);
  if (!rendered_image.has_value()) {
    return fail(tr("Could not decode %1").arg(*resolved));
  }
  const auto content_dpi =
      psd::DocumentIo::can_read({probe->file_bytes->data(), probe->file_bytes->size()})
          ? smart_object_source_dpi(*probe)
          : 0.0;

  auto updated_document = target.document;
  auto* updated_source = updated_document.metadata().smart_objects.find(uuid);
  if (updated_source == nullptr) {
    return fail(tr("This layer is not a linked smart object"));
  }
  stamp_smart_object_link(*updated_source, QFileInfo(*resolved));
  updated_source->dirty = true;
  if (!refresh_smart_object_layers_for_source(
          updated_document, uuid, *rendered_image, content_dpi, true, false, {}, &*probe)) {
    return fail(tr("Could not rebuild the Smart Filter preview and cache"));
  }
  int refreshed = 0;
  const std::function<void(const std::vector<Layer>&)> count_layers = [&](const std::vector<Layer>& layers) {
    for (const auto& candidate : layers) {
      if (layer_is_smart_object(candidate) && smart_object_source_uuid(candidate) == uuid &&
          smart_object_lock_reason(candidate) == "external") {
        ++refreshed;
      }
      count_layers(candidate.children());
    }
  };
  count_layers(std::as_const(updated_document).layers());
  if (before_mutation && !before_mutation()) {
    return fail(QString());
  }
  target.document = std::move(updated_document);
  return refreshed;
}

int MainWindow::rerender_embedded_smart_object(DocumentSession& target, LayerId layer_id,
                                               const std::function<bool()>& before_mutation, QString* error) {
  const auto fail = [error](const QString& message) {
    if (error != nullptr) {
      *error = message;
    }
    return 0;
  };
  const auto& doc = std::as_const(target.document);
  const auto* layer = doc.find_layer(layer_id);
  if (layer == nullptr || !layer_is_smart_object(*layer)) {
    return fail(tr("This layer is not an embedded smart object"));
  }
  const auto uuid = smart_object_source_uuid(*layer);
  const auto* source = doc.metadata().smart_objects.find(uuid);
  if (source == nullptr || source->kind != SmartObjectSourceKind::Embedded || source->file_bytes == nullptr ||
      source->file_bytes->empty()) {
    return fail(tr("This layer is not an embedded smart object"));
  }
  if (!smart_object_lock_reason(*layer).empty()) {
    return fail(tr("This smart object cannot be re-rendered"));
  }
  const auto rendered_image = decode_smart_object_source_image(*source);
  if (!rendered_image.has_value()) {
    return fail(tr("Could not decode %1").arg(QString::fromStdString(source->filename)));
  }
  const auto content_dpi =
      psd::DocumentIo::can_read({source->file_bytes->data(), source->file_bytes->size()})
          ? smart_object_source_dpi(*source)
          : 0.0;

  auto updated_document = target.document;
  const auto* updated_source = std::as_const(updated_document).metadata().smart_objects.find(uuid);
  if (updated_source == nullptr) {
    return fail(tr("This layer is not an embedded smart object"));
  }
  if (!refresh_smart_object_layers_for_source(updated_document, uuid, *rendered_image, content_dpi, false, false, {},
                                              updated_source)) {
    return fail(tr("Could not rebuild the Smart Filter preview and cache"));
  }
  int refreshed = 0;
  const std::function<void(const std::vector<Layer>&)> count_layers = [&](const std::vector<Layer>& layers) {
    for (const auto& candidate : layers) {
      if (layer_is_smart_object(candidate) && smart_object_source_uuid(candidate) == uuid &&
          smart_object_lock_reason(candidate).empty()) {
        ++refreshed;
      }
      count_layers(candidate.children());
    }
  };
  count_layers(std::as_const(updated_document).layers());
  if (before_mutation && !before_mutation()) {
    return fail(QString());
  }
  target.document = std::move(updated_document);
  return refreshed;
}

void MainWindow::relink_smart_object_contents() {
  if (!has_active_document()) {
    return;
  }
  const auto path = get_open_file_name(
      this, tr("Relink to File"), file_dialog_initial_path(QString(), QString()),
      tr("Embeddable Files (*.psd *.psb *.png *.jpg *.jpeg *.tif *.tiff *.bmp *.svg *.svgz);;All Files (*.*)"),
      nullptr, QStringLiteral("relinkSmartObjectFileDialog"));
  if (!path.isEmpty()) {
    relink_smart_object_contents_with_path(path);
  }
}

void MainWindow::relink_smart_object_contents_with_path(const QString& path) {
  if (!has_active_document()) {
    return;
  }
  auto& doc = document();
  select_only_layer_if_none_active();
  const auto active = doc.active_layer_id();
  const auto* layer = active.has_value() ? doc.find_layer(*active) : nullptr;
  if (layer == nullptr || !layer_is_smart_object(*layer) || smart_object_lock_reason(*layer) != "external") {
    show_status_error(tr("Select a linked smart object layer first"));
    return;
  }
  const auto uuid = smart_object_source_uuid(*layer);
  const auto loaded = load_smart_object_file_probe(path);
  if (!loaded.has_value()) {
    show_critical_message(this, tr("Relink failed"), tr("Could not read %1").arg(path),
                          QStringLiteral("relinkSmartObjectFailedMessageBox"));
    return;
  }
  const auto& probe = *loaded;
  const QFileInfo info(path);
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
  const bool vector_contents = smart_object_contents_are_vector(probe);

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
  relinked.creator = std::string(4, '\0');
  set_smart_object_link_target(relinked, info,
                               session().path.isEmpty() ? QString() : QFileInfo(session().path).absolutePath());
  auto updated_document = doc;
  auto& store = updated_document.metadata().smart_objects;
  store.add_external(relinked);

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
      updated_placement.placed_type = vector_contents ? 1 : 2;
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
              smart_object_image_for_placement(*rendered_image, &probe, updated_placement, warp),
              updated_placement, warp,
              CanvasWidget::TransformInterpolation::Bicubic,
              std::as_const(target).smart_filter_stack(),
              updated_document)) {
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
    show_status_error(
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
  select_only_layer_if_none_active();
  const auto active = doc.active_layer_id();
  const auto* layer = active.has_value() ? doc.find_layer(*active) : nullptr;
  if (layer == nullptr || !layer_is_smart_object(*layer) || smart_object_lock_reason(*layer) != "external") {
    show_status_error(tr("Select a linked smart object layer first"));
    return;
  }
  const auto uuid = smart_object_source_uuid(*layer);
  const auto* source = doc.metadata().smart_objects.find(uuid);
  if (source == nullptr || source->kind != SmartObjectSourceKind::ExternalFile) {
    show_status_error(tr("Select a linked smart object layer first"));
    return;
  }
  const auto parent_dir = session().path.isEmpty() ? QString() : QFileInfo(session().path).absolutePath();
  const auto resolved = resolve_smart_object_external_path(*source, parent_dir);
  if (!resolved.has_value()) {
    show_status_error(tr("Linked file %1 was not found. Use Relink to File... to point it at a new location")
                                 .arg(QString::fromStdString(source->filename)));
    return;
  }
  QFile file(*resolved);
  if (!file.open(QIODevice::ReadOnly)) {
    show_status_error(tr("Could not read %1").arg(*resolved));
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
  select_only_layer_if_none_active();
  const auto active = doc.active_layer_id();
  auto* layer = active.has_value() ? doc.find_layer(*active) : nullptr;
  if (layer == nullptr || !layer_is_smart_object(*layer)) {
    show_status_error(tr("Select a smart object layer first"));
    return;
  }
  if (!smart_object_lock_reason(*layer).empty()) {
    show_status_error(tr("This smart object can only be preserved, not edited"));
    return;
  }
  const auto old_uuid = smart_object_source_uuid(*layer);
  const auto* old_source = doc.metadata().smart_objects.find(old_uuid);
  if (old_source == nullptr || old_source->kind != SmartObjectSourceKind::Embedded ||
      old_source->file_bytes == nullptr) {
    show_status_error(tr("This smart object's contents are not embedded in the document"));
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
  select_only_layer_if_none_active();
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
  const auto filetype = smart_object_filetype_for_extension(info.suffix().toLower());

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
              updated_document)) {
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
    show_status_error(
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
  select_only_layer_if_none_active();
  convert_layers_to_smart_object(root_drop_layer_ids(doc.layers(), selected_or_active_layer_ids()));
}

bool MainWindow::convert_layers_to_smart_object(const std::vector<LayerId>& selected_ids) {
  auto& target_document = document();
  auto doc = target_document;
  if (selected_ids.empty()) {
    show_status_error(tr("Select layers to convert to a smart object"));
    return false;
  }
  if (std::any_of(selected_ids.begin(), selected_ids.end(),
                  [&doc](LayerId id) {
                    const auto* layer = std::as_const(doc).find_layer(id);
                    return layer != nullptr &&
                           layer_tree_contains_smart_filters(*layer);
                  })) {
    show_status_error(
        tr("Smart Objects with Smart Filters cannot be wrapped in another Smart Object yet"));
    return false;
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
    return false;
  }
  const auto top_id = ids.back();
  const auto top_name = std::as_const(doc).find_layer(top_id)->name();
  // The child canvas is the union of the selected layers' render bounds (Photoshop
  // uses the pixel bounds; render bounds additionally keep layer-style effects
  // inside the child canvas so the preview matches the old composite).
  if (content.empty()) {
    show_status_error(tr("The selected layers have no pixels to convert"));
    return false;
  }

  // Move copies of the selected trees into the child document, translated so the
  // union origin becomes the child origin.
  Document child(content.width, content.height, doc.format());
  child.print_settings() = doc.print_settings();
  const int dx = -content.x;
  const int dy = -content.y;
  for (const auto id : ids) {
    const auto* layer = doc.find_layer(id);
    if (layer == nullptr) {
      continue;
    }
    auto copy = *layer;
    // Masks shift exactly once: shifting a linked one by hand as well as through
    // the shared helper moved it twice (fixed October 2026).
    offset_copied_layer_tree(copy, dx, dy, child.width(), child.height());
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
    show_critical_message(this, tr("Convert failed"), translate_data_text(error.what()),
                          QStringLiteral("convertSmartObjectFailedMessageBox"));
    return false;
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
    return false;
  }
  Layer replacement(top_id, top_name, pixels_from_image_at_depth(preview, std::as_const(doc).color_state().bit_depth));
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
  push_undo_snapshot(tr("Convert to Smart Object"));
  target_document = std::move(doc);
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
  statusBar()->showMessage(tr("Converted to a smart object; click its badge to edit its contents"));
  return true;
}

void MainWindow::new_smart_object_via_copy() {
  if (!has_active_document()) {
    return;
  }
  select_only_layer_if_none_active();
  auto& target_document = document();
  auto doc = target_document;
  const auto active = doc.active_layer_id();
  const auto* layer = active.has_value() ? doc.find_layer(*active) : nullptr;
  if (layer == nullptr || !layer_is_smart_object(*layer)) {
    show_status_error(tr("Select a smart object layer first"));
    return;
  }
  const auto* source = doc.metadata().smart_objects.find(smart_object_source_uuid(*layer));
  if (source == nullptr || source->kind != SmartObjectSourceKind::Embedded || source->file_bytes == nullptr) {
    show_status_error(tr("This smart object's contents are not embedded in the document"));
    return;
  }
  const auto placement = smart_object_placement_from_layer(*layer);
  if (!placement.has_value()) {
    show_status_error(tr("This smart object can only be preserved, not edited"));
    return;
  }
  if (!smart_filter_records_available_for_clone(
          *layer, doc.metadata().smart_filter_effects)) {
    show_status_error(
        tr("Smart Filter cache data could not be duplicated safely"));
    return;
  }

  // Photoshop's via-copy semantics (E8): the element is CLONED under a fresh uuid, so
  // the copy edits independently (a plain duplicate would keep tracking the source).
  const auto fresh_uuid = generate_smart_object_uuid();
  // add_embedded grows the store, invalidating `source`; every field it feeds must be
  // copied out before the call (the by-value arguments are, `creator` was not).
  const auto source_creator = source->creator;
  auto& cloned = doc.metadata().smart_objects.add_embedded(fresh_uuid, source->filename, source->filetype,
                                                           source->file_bytes);
  cloned.creator = source_creator;

  const auto location = find_layer_location(doc.layers(), *active);
  if (!location.has_value()) {
    refresh_layer_list();
    return;
  }
  auto copy = clone_layer_tree_with_document_ids(doc, *layer);
  if (!copy.has_value()) {
    show_status_error(
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
  push_undo_snapshot(tr("New Smart Object via Copy"));
  target_document = std::move(doc);
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
  statusBar()->showMessage(tr("Created an independent smart object copy"));
}

void MainWindow::convert_smart_object_to_layers() {
  if (!has_active_document()) {
    return;
  }
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return;
  }
  finish_active_text_editor();
  if (canvas_ != nullptr) {
    canvas_->finish_free_transform();
  }
  const auto& current = std::as_const(document());
  select_only_layer_if_none_active();
  const auto active = current.active_layer_id();
  const auto* layer = active.has_value() ? current.find_layer(*active) : nullptr;
  if (layer == nullptr || !layer_is_smart_object(*layer)) {
    show_status_error(tr("Select a smart object layer first"));
    return;
  }
  const auto lock_reason = smart_object_lock_reason(*layer);
  if (lock_reason == "external") {
    show_status_error(tr("Embed the linked Smart Object before converting it to layers"));
    return;
  }
  if (lock_reason == "filters" || layer_tree_contains_smart_filters(*layer)) {
    show_status_error(tr("Delete the Smart Filters before converting this Smart Object to layers"));
    return;
  }
  if (lock_reason == "warp" || lock_reason == "non_affine") {
    show_status_error(tr("A warped or perspective Smart Object can't be converted to layers; rasterize it instead"));
    return;
  }
  const auto placement = smart_object_placement_from_layer(*layer);
  if (!lock_reason.empty() || !placement.has_value()) {
    show_status_error(tr("This smart object can only be preserved, not edited"));
    return;
  }
  const auto* source = current.metadata().smart_objects.find(placement->uuid);
  if (source == nullptr || source->kind != SmartObjectSourceKind::Embedded || source->file_bytes == nullptr) {
    show_status_error(tr("This smart object's contents are not embedded in the document"));
    return;
  }
  auto contents = decode_smart_object_source_document(*source);
  if (!contents.has_value()) {
    show_status_error(tr("Could not decode the embedded smart object contents"));
    return;
  }
  // copy_layers_between_documents takes ids top to bottom; layers() is bottom to top.
  std::vector<LayerId> root_ids;
  const auto& content_roots = std::as_const(*contents).layers();
  for (auto it = content_roots.rbegin(); it != content_roots.rend(); ++it) {
    root_ids.push_back(it->id());
  }
  if (root_ids.empty() || contents->width() <= 0 || contents->height() <= 0) {
    show_status_error(tr("The smart object's contents have no layers to convert"));
    return;
  }

  // The same mapping the preview renders through: the contents' canvas onto the
  // placement quad.
  const auto content_width = static_cast<qreal>(contents->width());
  const auto content_height = static_cast<qreal>(contents->height());
  const auto& quad = placement->transform;
  QTransform mapping;
  if (!QTransform::quadToQuad(QPolygonF({QPointF(0.0, 0.0), QPointF(content_width, 0.0),
                                         QPointF(content_width, content_height), QPointF(0.0, content_height)}),
                              QPolygonF({QPointF(quad[0], quad[1]), QPointF(quad[2], quad[3]),
                                         QPointF(quad[4], quad[5]), QPointF(quad[6], quad[7])}),
                              mapping) ||
      !mapping.isAffine()) {
    show_status_error(tr("A warped or perspective Smart Object can't be converted to layers; rasterize it instead"));
    return;
  }
  // An unscaled, unrotated placement moves the layers exactly, rounded onto the
  // pixel grid; anything else resamples through the mapping.
  constexpr double kLinearTolerance = 1e-6;
  const bool translation_only =
      std::abs(mapping.m11() - 1.0) < kLinearTolerance && std::abs(mapping.m22() - 1.0) < kLinearTolerance &&
      std::abs(mapping.m12()) < kLinearTolerance && std::abs(mapping.m21()) < kLinearTolerance;
  const QPoint offset = translation_only ? QPoint(static_cast<int>(std::lround(mapping.dx())),
                                                  static_cast<int>(std::lround(mapping.dy())))
                                         : QPoint();
  const bool contents_move = !translation_only || !offset.isNull();
  if (contents_move && std::any_of(std::as_const(*contents).layers().begin(), std::as_const(*contents).layers().end(),
                                   [](const Layer& root) { return layer_tree_contains_smart_filters(root); })) {
    show_status_error(tr("The contents contain Smart Filters, which can't be moved out of the Smart Object yet"));
    return;
  }

  const auto smart_object_id = *active;
  const auto smart_object = *layer;
  auto staged = current;
  staged.set_active_layer(smart_object_id);  // the copies land directly above it
  CrossDocumentLayerPlacement unpack;
  unpack.exact_offset = offset;
  unpack.keep_names = true;
  QString error;
  const auto copied_ids = copy_layers_between_documents(std::as_const(*contents), root_ids, staged, unpack,
                                                        [] { return true; }, &error);
  if (copied_ids.empty()) {
    show_status_error(error.isEmpty() ? tr("Could not convert the smart object to layers") : error);
    return;
  }

  // A folder named after the Smart Object takes its place and its compositing:
  // isolated like the Smart Object was (its own blend mode, never Pass Through),
  // with its opacity, visibility, clipping, locks, masks, and layer style.
  Layer folder(staged.allocate_layer_id(), smart_object.name(), LayerKind::Group);
  const auto folder_id = folder.id();
  folder.set_visible(smart_object.visible());
  folder.set_clipped(smart_object.clipped());
  folder.set_opacity(smart_object.opacity());
  folder.set_fill_opacity(smart_object.fill_opacity());
  folder.set_blend_mode(smart_object.blend_mode() == BlendMode::PassThrough ? BlendMode::Normal
                                                                            : smart_object.blend_mode());
  folder.set_lock_flags(smart_object.lock_flags());
  if (smart_object.mask().has_value()) {
    folder.set_mask(*smart_object.mask());
    if (const auto linked = smart_object.metadata().find(kLayerMetadataMaskLinked);
        linked != smart_object.metadata().end()) {
      folder.metadata()[linked->first] = linked->second;
    }
  }
  if (const auto* vector_mask = smart_object.vector_mask(); vector_mask != nullptr) {
    folder.set_vector_mask(*vector_mask);
    mark_layer_vector_block_dirty(folder);
  }
  folder.layer_style() = smart_object.layer_style();
  std::vector<Layer> unpacked_top_to_bottom;
  unpacked_top_to_bottom.reserve(copied_ids.size());
  for (const auto id : copied_ids) {
    if (auto taken = take_layer_from_tree(staged.layers(), id); taken.has_value()) {
      unpacked_top_to_bottom.push_back(std::move(*taken));
    }
  }
  for (auto it = unpacked_top_to_bottom.rbegin(); it != unpacked_top_to_bottom.rend(); ++it) {
    folder.add_child(std::move(*it));
  }
  const auto location = find_layer_location(staged.layers(), smart_object_id);
  if (!location.has_value()) {
    show_status_error(tr("Could not convert the smart object to layers"));
    return;
  }
  (*location->siblings)[location->index] = std::move(folder);

  if (!translation_only) {
    auto* installed = staged.find_layer(folder_id);
    const auto interpolation = canvas_ != nullptr ? canvas_->transform_interpolation()
                                                  : CanvasWidget::TransformInterpolation::Bicubic;
    for (auto& child : installed->children()) {
      map_unpacked_layer_tree(staged, child, mapping, interpolation);
    }
  }

  // The source element stays in the store like every orphan; the PSD writer
  // leaves unreferenced Patchy-written elements out of the file, which
  // Photoshop requires (docs/smart-objects.md).
  staged.set_active_layer(folder_id);
  push_undo_snapshot(tr("Convert to Layers"));
  document() = std::move(staged);
  refresh_layer_list();
  refresh_layer_controls();
  refresh_document_info();
  canvas_->document_changed();
  statusBar()->showMessage(tr("Converted the smart object to %n layer(s)", nullptr,
                              static_cast<int>(copied_ids.size())));
}

void MainWindow::place_embedded_file() {
  if (!has_active_document()) {
    return;
  }
  const auto path = get_open_file_name(
      this, tr("Place Embedded"), file_dialog_initial_path(QString(), QString()),
      tr("Embeddable Files (*.psd *.psb *.png *.jpg *.jpeg *.tif *.tiff *.bmp *.svg *.svgz);;All Files (*.*)"),
      nullptr, QStringLiteral("placeEmbeddedFileDialog"));
  if (path.isEmpty()) {
    return;
  }
  place_embedded_file_with_path(path);
}

void MainWindow::place_embedded_file_with_path(const QString& path) {
  if (!has_active_document()) {
    return;
  }
  QString error;
  const auto placed = place_file_as_smart_object(
      session(), path, SmartObjectPlaceOptions{},
      [this] {
        push_undo_snapshot(tr("Place Embedded"));
        return true;
      },
      &error);
  if (!placed.has_value()) {
    show_critical_message(this, tr("Place failed"), error, QStringLiteral("placeEmbeddedFailedMessageBox"));
    return;
  }
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
  statusBar()->showMessage(tr("Placed %1 as a smart object").arg(QFileInfo(path).fileName()));
}

void MainWindow::place_linked_file() {
  if (!has_active_document()) {
    return;
  }
  // The same formats as Place Embedded: a link only needs contents Patchy can render.
  const auto path = get_open_file_name(
      this, tr("Place Linked"), file_dialog_initial_path(QString(), QString()),
      tr("Embeddable Files (*.psd *.psb *.png *.jpg *.jpeg *.tif *.tiff *.bmp *.svg *.svgz);;All Files (*.*)"),
      nullptr, QStringLiteral("placeLinkedFileDialog"));
  if (path.isEmpty()) {
    return;
  }
  place_linked_file_with_path(path);
}

void MainWindow::place_linked_file_with_path(const QString& path) {
  if (!has_active_document()) {
    return;
  }
  SmartObjectPlaceOptions options;
  options.linked = true;
  QString error;
  const auto placed = place_file_as_smart_object(
      session(), path, options,
      [this] {
        push_undo_snapshot(tr("Place Linked"));
        return true;
      },
      &error);
  if (!placed.has_value()) {
    show_critical_message(this, tr("Place failed"), error, QStringLiteral("placeLinkedFailedMessageBox"));
    return;
  }
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
  statusBar()->showMessage(tr("Placed %1 as a linked smart object").arg(QFileInfo(path).fileName()));
}

std::optional<LayerId> MainWindow::place_file_as_smart_object(DocumentSession& target, const QString& path,
                                                              const SmartObjectPlaceOptions& options,
                                                              const std::function<bool()>& before_mutation,
                                                              QString* error) {
  const auto fail = [error](const QString& message) -> std::optional<LayerId> {
    if (error != nullptr) {
      *error = message;
    }
    return std::nullopt;
  };
  const QFileInfo info(path);
  const auto contents = load_smart_object_file_probe(path);
  if (!contents.has_value()) {
    return fail(tr("Could not read %1").arg(path));
  }
  if (classify_smart_object_contents(*contents) == SmartObjectContentsFormat::Undecodable) {
    return fail(tr("Could not decode %1").arg(info.fileName()));
  }
  const auto image = decode_smart_object_source_image(*contents);
  if (!image.has_value()) {
    return fail(tr("Could not decode %1").arg(info.fileName()));
  }

  auto doc = target.document;
  // E2 placement rule: physical pixels (content px scaled by doc_ppi/content_dpi) land
  // 1:1 centered when they fit, else scaled down to fit the canvas, centered.
  const double content_dpi = smart_object_source_dpi(*contents);
  const double doc_ppi = doc.print_settings().horizontal_ppi > 0.0 ? doc.print_settings().horizontal_ppi : 72.0;
  const double physical_width = image->width() * doc_ppi / content_dpi;
  const double physical_height = image->height() * doc_ppi / content_dpi;
  double fit = 1.0;
  if (physical_width > doc.width() || physical_height > doc.height()) {
    fit = std::min(doc.width() / physical_width, doc.height() / physical_height);
  }
  const double default_width = physical_width * fit;
  const double default_height = physical_height * fit;
  const double default_left = (doc.width() - default_width) / 2.0;
  const double default_top = (doc.height() - default_height) / 2.0;

  // An explicit size wins over a scale; one side alone keeps the aspect ratio.
  double placed_width = default_width;
  double placed_height = default_height;
  if (options.width.has_value() && options.height.has_value()) {
    placed_width = *options.width;
    placed_height = *options.height;
  } else if (options.width.has_value()) {
    placed_width = *options.width;
    placed_height = physical_height * (placed_width / physical_width);
  } else if (options.height.has_value()) {
    placed_height = *options.height;
    placed_width = physical_width * (placed_height / physical_height);
  } else if (options.scale.has_value()) {
    placed_width = physical_width * *options.scale;
    placed_height = physical_height * *options.scale;
  }
  const double left = options.x.value_or((doc.width() - placed_width) / 2.0);
  const double top = options.y.value_or((doc.height() - placed_height) / 2.0);
  constexpr double kMaxPlacedSide = 30000.0;  // the document size limit
  constexpr double kMaxPlacedOffset = 1000000.0;
  const auto side_ok = [](double side) { return std::isfinite(side) && side >= 1.0 && side <= kMaxPlacedSide; };
  const auto offset_ok = [](double offset) { return std::isfinite(offset) && std::abs(offset) <= kMaxPlacedOffset; };
  if (!side_ok(placed_width) || !side_ok(placed_height) || !offset_ok(left) || !offset_ok(top)) {
    return fail(tr("The placed position or size is out of range"));
  }

  const bool vector_contents = smart_object_contents_are_vector(*contents);
  SmartObjectPlacement placement;
  placement.transform = {left,                top,                 left + placed_width, top,
                         left + placed_width, top + placed_height, left,                top + placed_height};
  placement.width = image->width();
  placement.height = image->height();
  placement.resolution = content_dpi;
  placement.placed_type = vector_contents ? 1 : 2;

  auto& store = doc.metadata().smart_objects;
  if (options.linked) {
    const auto document_dir = target.path.isEmpty() ? QString() : QFileInfo(target.path).absolutePath();
    if (auto* shared = find_smart_object_link_for_file(store, info, document_dir); shared != nullptr) {
      // The document already links this file: the new layer joins that element, so
      // one Update Smart Object Content refreshes every layer placed from it.
      placement.uuid = shared->uuid;
      if (smart_object_link_changed_on_disk(*shared, info)) {
        // The file changed since the other layers rendered; they follow now, so all
        // layers of one element show the same contents.
        stamp_smart_object_link(*shared, info);
        shared->dirty = true;
        const auto refresh_dpi =
            psd::DocumentIo::can_read({contents->file_bytes->data(), contents->file_bytes->size()})
                ? content_dpi
                : 0.0;
        if (!refresh_smart_object_layers_for_source(doc, placement.uuid, *image, refresh_dpi, true, false, {},
                                                    &*contents)) {
          return fail(tr("Could not rebuild the Smart Filter preview and cache"));
        }
      }
    } else {
      SmartObjectSource link;
      link.kind = SmartObjectSourceKind::ExternalFile;
      link.uuid = generate_smart_object_uuid();
      link.creator = std::string(4, '\0');
      set_smart_object_link_target(link, info, document_dir);
      placement.uuid = link.uuid;
      store.add_external(std::move(link));
    }
  } else {
    placement.uuid = generate_smart_object_uuid();
    auto& embedded = store.add_embedded(placement.uuid, contents->filename, contents->filetype, contents->file_bytes);
    embedded.creator = "    ";  // Photoshop writes four spaces for placed files
  }

  std::optional<TransformedImage> rendered;
  try {
    rendered = render_smart_object_pixels(
        smart_object_image_for_document(
            smart_object_image_for_placement(*image, vector_contents ? &*contents : nullptr, placement, std::nullopt), doc),
        placement, CanvasWidget::TransformInterpolation::Bicubic);
  } catch (const std::exception&) {
    rendered.reset();
  }
  if (!rendered.has_value()) {
    return fail(tr("Could not decode %1").arg(info.fileName()));
  }
  // add_pixel_layer requires full-canvas buffers; placed layers carry tight bounds.
  const auto layer_name = options.name.isEmpty() ? info.completeBaseName() : options.name;
  Layer placed_layer(doc.allocate_layer_id(), layer_name.toStdString(),
                     pixels_from_image_at_depth(rendered->image, std::as_const(doc).color_state().bit_depth));
  placed_layer.set_bounds(rendered->bounds);
  const auto placed_instance = generate_smart_object_uuid();
  // A linked layer carries the same descriptor under the 'SoLE' key and stays
  // preview-locked as "external", exactly as a Photoshop-written one reads back.
  const char* block_key = options.linked ? "SoLE" : "SoLd";
  set_layer_smart_object_metadata(placed_layer, placement, placed_instance, block_key,
                                  options.linked ? "external" : "", kSmartObjectRasterStatusPatchy);
  // Vector contents record the unscaled placement rectangle (Photoshop's warp bounds).
  const std::array<double, 4> vector_bounds{default_left, default_top, default_left + default_width,
                                            default_top + default_height};
  placed_layer.unknown_psd_blocks().push_back(
      UnknownPsdBlock{block_key, psd::author_placed_layer_sold_payload(placement, placed_instance, nullptr,
                                                                       vector_contents ? &vector_bounds : nullptr)});
  const auto layer_id = placed_layer.id();
  doc.add_layer(std::move(placed_layer));
  doc.set_active_layer(layer_id);
  if (before_mutation && !before_mutation()) {
    return fail(QString());
  }
  target.document = std::move(doc);
  return layer_id;
}

}  // namespace patchy::ui
