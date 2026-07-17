// Paths panel glue: row building with outline thumbnails, panel targeting
// (which path the pen/path tools edit), and the footer commands - New Path,
// Fill Path (foreground), Stroke Path (brush-sized band), Make Selection,
// Delete Path. The panel widget itself is presentation-only (paths_panel.cpp).
#include "ui/main_window.hpp"

#include "core/blend_math.hpp"
#include "core/document_path.hpp"
#include "core/layer_metadata.hpp"
#include "core/layer_render_utils.hpp"
#include "core/path_fit.hpp"
#include "core/vector_raster.hpp"
#include "core/vector_shape.hpp"
#include "ui/app_settings.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/main_window_shared.hpp"
#include "ui/paths_panel.hpp"
#include "ui/qt_geometry.hpp"
#include "ui/selection_outline.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QStatusBar>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <optional>
#include <set>
#include <string>
#include <utility>

namespace patchy::ui {

namespace {

// 42x30 thumbnail of a path over the document extent: filled coverage (so
// boolean subtract/intersect/xor holes read exactly like the canvas) under a
// light outline stroke.
QPixmap path_outline_thumbnail(const VectorPath& path, int document_width, int document_height) {
  constexpr int kWidth = 42;
  constexpr int kHeight = 30;
  QPixmap pixmap(kWidth, kHeight);
  pixmap.fill(QColor(52, 56, 64));
  if (document_width > 0 && document_height > 0 && !path.empty()) {
    QPainter painter(&pixmap);
    const auto scale_x = static_cast<double>(kWidth) / document_width;
    const auto scale_y = static_cast<double>(kHeight) / document_height;
    // Rasterize a thumbnail-space copy for the fill (cheap at 42x30).
    auto scaled = path;
    transform_vector_path(scaled, {scale_x, 0.0, 0.0, scale_y, 0.0, 0.0});
    VectorRasterOptions thumbnail_options;
    thumbnail_options.clip = Rect::from_size(kWidth, kHeight);
    if (const auto coverage = rasterize_vector_path(scaled, thumbnail_options);
        !coverage.pixels.empty()) {
      QImage fill(coverage.pixels.width(), coverage.pixels.height(),
                  QImage::Format_ARGB32_Premultiplied);
      constexpr int kFillGray = 165;
      for (int y = 0; y < coverage.pixels.height(); ++y) {
        auto* line = reinterpret_cast<QRgb*>(fill.scanLine(y));
        for (int x = 0; x < coverage.pixels.width(); ++x) {
          const auto alpha = *coverage.pixels.pixel(x, y);
          const auto component = static_cast<int>(kFillGray) * alpha / 255;
          line[x] = qRgba(component, component, component, alpha);
        }
      }
      painter.drawImage(coverage.bounds.x, coverage.bounds.y, fill);
    }
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath outline;
    for (const auto& subpath : path.subpaths) {
      if (subpath.anchors.empty()) {
        continue;
      }
      const auto to_point = [scale_x, scale_y](double x, double y) {
        return QPointF(x * scale_x, y * scale_y);
      };
      outline.moveTo(to_point(subpath.anchors[0].anchor_x, subpath.anchors[0].anchor_y));
      const auto anchor_count = subpath.anchors.size();
      const auto segment_count = subpath.closed ? anchor_count : anchor_count - 1;
      for (std::size_t i = 0; i < segment_count; ++i) {
        const auto& a = subpath.anchors[i];
        const auto& b = subpath.anchors[(i + 1) % anchor_count];
        outline.cubicTo(to_point(a.out_x, a.out_y), to_point(b.in_x, b.in_y),
                        to_point(b.anchor_x, b.anchor_y));
      }
    }
    painter.setPen(QPen(QColor(220, 226, 235), 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(outline);
  }
  QPainter border(&pixmap);
  border.setPen(QPen(QColor(150, 158, 168), 1));
  border.drawRect(QRect(0, 0, kWidth - 1, kHeight - 1));
  return pixmap;
}

// Full-canvas grayscale coverage of a path (no feather).
PixelBuffer path_selection_coverage(const VectorPath& path, int width, int height) {
  VectorRasterOptions options;
  options.clip = Rect::from_size(width, height);
  auto coverage = rasterize_vector_path(path, options);
  PixelBuffer full(width, height, PixelFormat::gray8());
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto local_x = x - coverage.bounds.x;
      const auto local_y = y - coverage.bounds.y;
      std::uint8_t value = 0;
      if (!coverage.pixels.empty() && local_x >= 0 && local_y >= 0 &&
          local_x < coverage.pixels.width() && local_y < coverage.pixels.height()) {
        value = *coverage.pixels.pixel(local_x, local_y);
      }
      *full.pixel(x, y) = value;
    }
  }
  return full;
}

}  // namespace

const VectorPath* MainWindow::resolved_row_path(int kind, DocumentPathId id, QString* name) const {
  if (!has_active_document()) {
    return nullptr;
  }
  const auto& doc = document();
  if (static_cast<PathsPanel::RowKind>(kind) == PathsPanel::RowKind::LayerPath) {
    const auto active = doc.active_layer_id();
    const auto* layer = active.has_value() ? doc.find_layer(*active) : nullptr;
    if (layer == nullptr) {
      return nullptr;
    }
    if (name != nullptr) {
      *name = QString::fromStdString(layer->name());
    }
    if (canvas_ != nullptr &&
        canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::VectorMask &&
        layer->vector_mask() != nullptr) {
      return &layer->vector_mask()->path;
    }
    if (layer_is_vector_shape(*layer)) {
      return &layer->vector_shape()->path;
    }
    if (layer->vector_mask() != nullptr) {
      return &layer->vector_mask()->path;
    }
    return nullptr;
  }
  const auto* path = doc.find_path(id);
  if (path == nullptr) {
    return nullptr;
  }
  if (name != nullptr) {
    *name = QString::fromStdString(path->name());
  }
  return &path->path();
}

const VectorPath* MainWindow::resolved_panel_path(QString* name) const {
  if (paths_panel_ == nullptr) {
    return nullptr;
  }
  const auto row = paths_panel_->selected_row();
  if (!row.has_value()) {
    return nullptr;
  }
  return resolved_row_path(static_cast<int>(row->kind), row->id, name);
}

QPixmap MainWindow::cached_path_thumbnail(const DocumentPath& path, int document_width,
                                          int document_height) {
  auto& cached = path_thumbnail_cache_[path.id()];
  if (cached.thumbnail.isNull() || cached.content_revision != path.content_revision() ||
      cached.document_width != document_width || cached.document_height != document_height) {
    cached.content_revision = path.content_revision();
    cached.document_width = document_width;
    cached.document_height = document_height;
    cached.thumbnail = path_outline_thumbnail(path.path(), document_width, document_height);
  }
  return cached.thumbnail;
}

void MainWindow::refresh_paths_panel() {
  if (paths_panel_ == nullptr) {
    return;
  }
  if (!has_active_document()) {
    path_thumbnail_cache_.clear();
    paths_panel_->set_rows({});
    paths_panel_->set_document_available(false);
    return;
  }
  // Match update_document_action_state's availability rule so a refresh during
  // a preview-dialog edit lock cannot re-enable the path commands.
  paths_panel_->set_document_available(!preview_dialog_edit_locked());
  const auto& doc = document();
  std::vector<PathsPanel::Row> rows;

  // Transient row: the active layer's shape / vector-mask path.
  if (const auto active = doc.active_layer_id(); active.has_value()) {
    if (const auto* layer = doc.find_layer(*active); layer != nullptr) {
      const VectorPath* layer_path = nullptr;
      QString label;
      if (layer_is_vector_shape(*layer)) {
        layer_path = &layer->vector_shape()->path;
        label = tr("%1 Shape Path").arg(QString::fromStdString(layer->name()));
      } else if (layer->vector_mask() != nullptr) {
        layer_path = &layer->vector_mask()->path;
        label = tr("%1 Vector Mask").arg(QString::fromStdString(layer->name()));
      }
      if (layer_path != nullptr) {
        rows.push_back(PathsPanel::Row{PathsPanel::RowKind::LayerPath, 0, label,
                                       path_outline_thumbnail(*layer_path, doc.width(), doc.height())});
      }
    }
  }
  const DocumentPath* work = nullptr;
  for (const auto& path : doc.paths()) {
    if (path.kind() == DocumentPathKind::Work) {
      work = &path;
      continue;
    }
    rows.push_back(PathsPanel::Row{PathsPanel::RowKind::SavedPath, path.id(),
                                   QString::fromStdString(path.name()),
                                   cached_path_thumbnail(path, doc.width(), doc.height())});
  }
  if (work != nullptr) {
    rows.push_back(PathsPanel::Row{PathsPanel::RowKind::WorkPath, work->id(),
                                   QString::fromStdString(work->name()),
                                   cached_path_thumbnail(*work, doc.width(), doc.height())});
  }
  // Deleted paths leave stale cache entries; prune to the live id set.
  std::erase_if(path_thumbnail_cache_, [&doc](const auto& entry) {
    return std::as_const(doc).find_path(entry.first) == nullptr;
  });

  // A dismissed layer-path row stays hidden only while its layer is active.
  if (path_row_hidden_for_layer_.has_value() &&
      doc.active_layer_id() != path_row_hidden_for_layer_) {
    path_row_hidden_for_layer_.reset();
  }
  std::optional<PathsPanel::Row> selected;
  if (active_document_path_id_.has_value()) {
    for (const auto& row : rows) {
      if (row.kind != PathsPanel::RowKind::LayerPath && row.id == *active_document_path_id_) {
        selected = row;
        break;
      }
    }
    if (!selected.has_value()) {
      active_document_path_id_.reset();  // the path was deleted or undone away
      if (canvas_ != nullptr) {
        canvas_->set_active_document_path(std::nullopt);
      }
    }
  }
  // Photoshop parity: the active layer's shape / vector-mask path row is
  // auto-targeted (its outline shows with any tool) unless the user dismissed
  // it for this layer.
  if (!selected.has_value() && !rows.empty() &&
      rows.front().kind == PathsPanel::RowKind::LayerPath &&
      !path_row_hidden_for_layer_.has_value()) {
    selected = rows.front();
  }
  paths_panel_->set_rows(std::move(rows), selected);
  if (canvas_ != nullptr) {
    canvas_->set_panel_path_targeted(paths_panel_->selected_row().has_value());
  }
}

void MainWindow::target_document_path_row(DocumentPathId id) {
  active_document_path_id_ = id;
  path_row_hidden_for_layer_.reset();
  if (canvas_ != nullptr) {
    canvas_->set_active_document_path(id);
  }
  refresh_paths_panel();
}

void MainWindow::handle_paths_panel_target(int kind, DocumentPathId id) {
  if (canvas_ == nullptr) {
    return;
  }
  path_row_hidden_for_layer_.reset();  // an explicit row click re-shows the path
  if (static_cast<PathsPanel::RowKind>(kind) == PathsPanel::RowKind::LayerPath) {
    active_document_path_id_.reset();
    canvas_->set_active_document_path(std::nullopt);
  } else {
    active_document_path_id_ = id;
    canvas_->set_active_document_path(id);
  }
  canvas_->set_panel_path_targeted(true);
  canvas_->update();
}

void MainWindow::handle_paths_panel_deselect() {
  active_document_path_id_.reset();
  if (has_active_document()) {
    path_row_hidden_for_layer_ = document().active_layer_id();
  }
  if (canvas_ != nullptr) {
    canvas_->set_active_document_path(std::nullopt);
    canvas_->set_panel_path_targeted(false);
    canvas_->clear_path_edit_selection();
    canvas_->update();
  }
}

void MainWindow::load_path_as_selection(int kind, DocumentPathId id) {
  if (canvas_ == nullptr || !has_active_document()) {
    return;
  }
  QString path_name;
  const auto* path = resolved_row_path(kind, id, &path_name);
  if (path == nullptr || path->empty()) {
    show_status_error(tr("The path is empty"));
    return;
  }
  const auto& doc = document();
  const auto coverage = path_selection_coverage(*path, doc.width(), doc.height());
  canvas_->replace_selection_from_grayscale(coverage, tr("Load path as selection"));
  statusBar()->showMessage(tr("Loaded %1 as a selection.").arg(path_name));
}

void MainWindow::duplicate_selected_path() {
  if (paths_panel_ == nullptr || !has_active_document()) {
    return;
  }
  const auto row = paths_panel_->selected_row();
  if (!row.has_value() || row->kind == PathsPanel::RowKind::LayerPath) {
    show_status_error(tr("Select a saved path or the work path to duplicate"));
    return;
  }
  auto& doc = document();
  const auto* source = doc.find_path(row->id);
  if (source == nullptr) {
    return;
  }
  push_undo_snapshot(tr("Duplicate path"));
  // "<name> copy", uniquified the Photoshop way (the shared layer-duplication
  // helper strips an existing " copy"/" copy N" stem first).
  std::set<std::string> existing;
  for (const auto& path : doc.paths()) {
    existing.insert(path.name());
  }
  const auto name = next_duplicate_name(source->name(), existing);
  DocumentPath created(doc.allocate_path_id(), name, DocumentPathKind::Saved, source->path());
  created.mark_dirty();  // authored copy: no original resource bytes to re-emit
  target_document_path_row(doc.add_path(std::move(created)).id());
  statusBar()->showMessage(tr("Duplicated the path as %1.").arg(QString::fromStdString(name)));
}

void MainWindow::reorder_paths_from_panel(std::vector<DocumentPathId> order) {
  if (!has_active_document()) {
    return;
  }
  auto& doc = document();
  auto& paths = doc.paths();
  // `order` lists the saved paths in their new panel order; accept only a
  // permutation of the current saved set.
  std::vector<DocumentPathId> current;
  for (const auto& path : std::as_const(paths)) {
    if (path.kind() == DocumentPathKind::Saved) {
      current.push_back(path.id());
    }
  }
  // is_permutation over unique ids already implies order is duplicate-free.
  if (order.size() != current.size() ||
      !std::is_permutation(order.begin(), order.end(), current.begin())) {
    refresh_paths_panel();
    return;
  }
  if (order == current) {
    return;
  }
  push_undo_snapshot(tr("Reorder paths"));
  std::vector<DocumentPath> reordered;
  reordered.reserve(paths.size());
  for (const auto id : order) {
    auto it = std::find_if(paths.begin(), paths.end(),
                           [id](const DocumentPath& path) { return path.id() == id; });
    if (it != paths.end()) {
      reordered.push_back(std::move(*it));
      paths.erase(it);
    }
  }
  for (auto& path : paths) {  // the work path keeps its spot after the block
    reordered.push_back(std::move(path));
  }
  paths = std::move(reordered);
  refresh_paths_panel();
  statusBar()->showMessage(tr("Reordered paths"));
}

void MainWindow::rename_document_path(DocumentPathId id, const QString& name) {
  auto& doc = document();
  auto* path = doc.find_path(id);
  if (path == nullptr || name.trimmed().isEmpty() ||
      path->name() == name.trimmed().toStdString()) {
    refresh_paths_panel();
    return;
  }
  push_undo_snapshot(tr("Rename path"));
  path->set_name(name.trimmed().toStdString());
  refresh_paths_panel();
  statusBar()->showMessage(tr("Renamed the path to %1.").arg(name.trimmed()));
}

QString MainWindow::unique_saved_path_name() {
  std::set<std::string> existing;
  for (const auto& path : document().paths()) {
    existing.insert(path.name());
  }
  int suffix = 1;
  std::string name;
  do {
    name = tr("Path %1").arg(suffix++).toStdString();
  } while (existing.contains(name));
  return QString::fromStdString(name);
}

void MainWindow::save_work_path_as_named() {
  auto& doc = document();
  auto* work = doc.work_path();
  if (work == nullptr) {
    return;
  }
  push_undo_snapshot(tr("Save path"));
  const auto name = unique_saved_path_name();
  const auto saved_id = work->id();
  work->set_name(name.toStdString());
  work->set_kind(DocumentPathKind::Saved);
  // Photoshop appends the saved path below the existing ones; moving it to
  // the end also keeps the siblings' resource ids stable (the writer assigns
  // ids by document order).
  auto& paths = doc.paths();
  if (const auto it = std::find_if(paths.begin(), paths.end(),
                                   [saved_id](const DocumentPath& path) {
                                     return path.id() == saved_id;
                                   });
      it != paths.end()) {
    std::rotate(it, it + 1, paths.end());
  }
  target_document_path_row(saved_id);
  statusBar()->showMessage(tr("Saved the work path as %1.").arg(name));
  if (paths_panel_ != nullptr) {
    paths_panel_->begin_rename(*active_document_path_id_);  // let the user name it right away
  }
}

void MainWindow::new_saved_path() {
  if (!has_active_document()) {
    return;
  }
  auto& doc = document();
  push_undo_snapshot(tr("New path"));
  const auto name = unique_saved_path_name();
  DocumentPath created(doc.allocate_path_id(), name.toStdString(), DocumentPathKind::Saved,
                       VectorPath{});
  created.mark_dirty();
  target_document_path_row(doc.add_path(std::move(created)).id());
  statusBar()->showMessage(tr("Created %1. Draw into it with the Pen tool.").arg(name));
}

void MainWindow::delete_selected_path() {
  if (paths_panel_ == nullptr || !has_active_document()) {
    return;
  }
  const auto row = paths_panel_->selected_row();
  if (!row.has_value() || row->kind == PathsPanel::RowKind::LayerPath) {
    show_status_error(tr("Select a saved path or the work path to delete"));
    return;
  }
  push_undo_snapshot(tr("Delete path"));
  document().remove_path(row->id);
  active_document_path_id_.reset();
  if (canvas_ != nullptr) {
    canvas_->set_active_document_path(std::nullopt);
  }
  refresh_paths_panel();
  statusBar()->showMessage(tr("Deleted the path"));
}

void MainWindow::fill_active_path() {
  QString path_name;
  const auto* path = resolved_panel_path(&path_name);
  if (path == nullptr || path->empty()) {
    show_status_error(tr("Select a path to fill"));
    return;
  }
  auto& doc = document();
  const auto active = doc.active_layer_id();
  auto* layer = active.has_value() ? doc.find_layer(*active) : nullptr;
  if (layer == nullptr || layer->kind() != LayerKind::Pixel || layer_pixels_are_procedural(*layer)) {
    show_status_error(tr("Select an editable pixel layer first"));
    return;
  }
  if (layer_id_locks_image_pixels(*active)) {
    show_status_error(tr("Layer pixels are locked."));
    return;
  }
  push_undo_snapshot(tr("Fill path"));
  const auto coverage = path_selection_coverage(*path, doc.width(), doc.height());
  const auto color = canvas_->primary_color();
  auto& pixels = layer->pixels();
  const auto bounds = layer->bounds();
  const bool lock_transparency = layer_locks_transparent_pixels(*layer);
  for (int y = 0; y < pixels.height(); ++y) {
    const auto document_y = bounds.y + y;
    if (document_y < 0 || document_y >= doc.height()) {
      continue;
    }
    for (int x = 0; x < pixels.width(); ++x) {
      const auto document_x = bounds.x + x;
      if (document_x < 0 || document_x >= doc.width()) {
        continue;
      }
      const auto alpha = *coverage.pixel(document_x, document_y);
      if (alpha == 0) {
        continue;
      }
      auto* pixel = pixels.pixel(x, y);
      const auto channels = pixels.format().channels;
      const auto existing_alpha = channels >= 4 ? pixel[3] : std::uint8_t{255};
      if (lock_transparency && existing_alpha == 0) {
        continue;
      }
      const auto blend = [alpha](std::uint8_t source, std::uint8_t destination) {
        return static_cast<std::uint8_t>((source * alpha + destination * (255 - alpha) + 127) / 255);
      };
      pixel[0] = blend(static_cast<std::uint8_t>(color.red()), pixel[0]);
      pixel[1] = blend(static_cast<std::uint8_t>(color.green()), pixel[1]);
      if (channels >= 3) {
        pixel[2] = blend(static_cast<std::uint8_t>(color.blue()), pixel[2]);
      }
      if (channels >= 4 && !lock_transparency) {
        pixel[3] = static_cast<std::uint8_t>(existing_alpha + (255 - existing_alpha) * alpha / 255);
      }
    }
  }
  canvas_->document_changed();
  refresh_layer_thumbnails();
  statusBar()->showMessage(tr("Filled the path with the foreground color"));
}

void MainWindow::stroke_active_path() {
  QString path_name;
  const auto* path = resolved_panel_path(&path_name);
  if (path == nullptr || path->empty()) {
    show_status_error(tr("Select a path to stroke"));
    return;
  }
  auto& doc = document();
  const auto active = doc.active_layer_id();
  auto* layer = active.has_value() ? doc.find_layer(*active) : nullptr;
  if (layer == nullptr || layer->kind() != LayerKind::Pixel || layer_pixels_are_procedural(*layer)) {
    show_status_error(tr("Select an editable pixel layer first"));
    return;
  }
  if (layer_id_locks_image_pixels(*active)) {
    show_status_error(tr("Layer pixels are locked."));
    return;
  }
  push_undo_snapshot(tr("Stroke path"));
  // A centered round-capped band at the brush size, painted with the
  // foreground color (the brush-tool defaults; per-stamp dynamics are not
  // simulated).
  VectorStroke stroke;
  stroke.enabled = true;
  stroke.width = std::max(1, canvas_->brush_size());
  stroke.cap = VectorStrokeCap::Round;
  stroke.join = VectorStrokeJoin::Round;
  stroke.alignment = VectorStrokeAlignment::Center;
  VectorRasterOptions options;
  options.clip = Rect::from_size(doc.width(), doc.height());
  const auto band = rasterize_vector_stroke(*path, stroke, options);
  const auto color = canvas_->primary_color();
  auto& pixels = layer->pixels();
  const auto bounds = layer->bounds();
  const bool lock_transparency = layer_locks_transparent_pixels(*layer);
  for (int y = 0; y < pixels.height(); ++y) {
    const auto document_y = bounds.y + y;
    for (int x = 0; x < pixels.width(); ++x) {
      const auto document_x = bounds.x + x;
      const auto local_x = document_x - band.bounds.x;
      const auto local_y = document_y - band.bounds.y;
      std::uint8_t alpha = 0;
      if (!band.pixels.empty() && local_x >= 0 && local_y >= 0 && local_x < band.pixels.width() &&
          local_y < band.pixels.height()) {
        alpha = *band.pixels.pixel(local_x, local_y);
      }
      if (alpha == 0) {
        continue;
      }
      auto* pixel = pixels.pixel(x, y);
      const auto channels = pixels.format().channels;
      const auto existing_alpha = channels >= 4 ? pixel[3] : std::uint8_t{255};
      if (lock_transparency && existing_alpha == 0) {
        continue;
      }
      const auto blend = [alpha](std::uint8_t source, std::uint8_t destination) {
        return static_cast<std::uint8_t>((source * alpha + destination * (255 - alpha) + 127) / 255);
      };
      pixel[0] = blend(static_cast<std::uint8_t>(color.red()), pixel[0]);
      pixel[1] = blend(static_cast<std::uint8_t>(color.green()), pixel[1]);
      if (channels >= 3) {
        pixel[2] = blend(static_cast<std::uint8_t>(color.blue()), pixel[2]);
      }
      if (channels >= 4 && !lock_transparency) {
        pixel[3] = static_cast<std::uint8_t>(existing_alpha + (255 - existing_alpha) * alpha / 255);
      }
    }
  }
  canvas_->document_changed();
  refresh_layer_thumbnails();
  statusBar()->showMessage(tr("Stroked the path with the foreground color"));
}

void MainWindow::make_selection_from_path() {
  QString path_name;
  const auto* path = resolved_panel_path(&path_name);
  if (path == nullptr || path->empty()) {
    show_status_error(tr("Select a path to convert"));
    return;
  }
  QDialog dialog(this);
  dialog.setObjectName(QStringLiteral("makeSelectionDialog"));
  dialog.setWindowTitle(tr("Make Selection"));
  auto* layout = new QVBoxLayout(&dialog);
  auto* form = new QFormLayout();
  auto* feather = new QDoubleSpinBox(&dialog);
  feather->setObjectName(QStringLiteral("makeSelectionFeatherSpin"));
  feather->setRange(0.0, 250.0);
  feather->setDecimals(1);
  feather->setSuffix(QStringLiteral(" px"));
  form->addRow(tr("Feather:"), feather);
  auto* antialias = new QCheckBox(tr("Anti-alias"), &dialog);
  antialias->setObjectName(QStringLiteral("makeSelectionAntialiasCheck"));
  antialias->setChecked(true);
  form->addRow(QString(), antialias);
  auto* operation = new QComboBox(&dialog);
  operation->setObjectName(QStringLiteral("makeSelectionOperationCombo"));
  operation->addItems({tr("New Selection"), tr("Add to Selection"), tr("Subtract from Selection"),
                       tr("Intersect with Selection")});
  const bool has_selection = canvas_ != nullptr && canvas_->has_selection();
  operation->setEnabled(has_selection);
  form->addRow(tr("Operation:"), operation);
  layout->addLayout(form);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget(buttons);
  // The QSS sub-control gotcha: spin-button styling lands AFTER children exist.
  dialog.setStyleSheet(dialog.styleSheet() + dialog_spinbox_button_style());
  if (run_non_modal_dialog(dialog) != QDialog::Accepted) {
    return;
  }
  // Non-modal dialog: re-resolve everything the event loop may have changed.
  if (canvas_ == nullptr || !has_active_document()) {
    return;
  }
  path = resolved_panel_path(&path_name);
  if (path == nullptr || path->empty()) {
    return;
  }

  auto& doc = document();
  auto coverage = path_selection_coverage(*path, doc.width(), doc.height());
  if (feather->value() > 0.0) {
    // Triple box blur approximating a gaussian (the vector-mask feather rule:
    // box radius is about half the feather value).
    const auto radius = std::max(1, static_cast<int>(std::lround(feather->value() * 0.5)));
    const auto width = coverage.width();
    const auto height = coverage.height();
    PixelBuffer scratch(width, height, PixelFormat::gray8());
    for (int pass = 0; pass < 3; ++pass) {
      // Horizontal then vertical box.
      for (int y = 0; y < height; ++y) {
        int sum = 0;
        int count = 0;
        for (int x = -radius; x <= radius; ++x) {
          if (x >= 0 && x < width) {
            sum += *coverage.pixel(x, y);
            ++count;
          }
        }
        for (int x = 0; x < width; ++x) {
          *scratch.pixel(x, y) = static_cast<std::uint8_t>(sum / std::max(1, count));
          const auto add_x = x + radius + 1;
          const auto remove_x = x - radius;
          if (add_x < width) {
            sum += *coverage.pixel(add_x, y);
            ++count;
          }
          if (remove_x >= 0) {
            sum -= *coverage.pixel(remove_x, y);
            --count;
          }
        }
      }
      for (int x = 0; x < width; ++x) {
        int sum = 0;
        int count = 0;
        for (int y = -radius; y <= radius; ++y) {
          if (y >= 0 && y < height) {
            sum += *scratch.pixel(x, y);
            ++count;
          }
        }
        for (int y = 0; y < height; ++y) {
          *coverage.pixel(x, y) = static_cast<std::uint8_t>(sum / std::max(1, count));
          const auto add_y = y + radius + 1;
          const auto remove_y = y - radius;
          if (add_y < height) {
            sum += *scratch.pixel(x, add_y);
            ++count;
          }
          if (remove_y >= 0) {
            sum -= *scratch.pixel(x, remove_y);
            --count;
          }
        }
      }
    }
  }
  if (!antialias->isChecked()) {
    for (int y = 0; y < coverage.height(); ++y) {
      for (int x = 0; x < coverage.width(); ++x) {
        auto* value = coverage.pixel(x, y);
        *value = *value >= 128 ? 255 : 0;
      }
    }
  }
  const auto operation_index = has_selection ? operation->currentIndex() : 0;
  if (operation_index != 0) {
    const auto existing =
        selection_mask_pixels(*canvas_, QRect(0, 0, doc.width(), doc.height()));
    for (int y = 0; y < coverage.height(); ++y) {
      for (int x = 0; x < coverage.width(); ++x) {
        auto* value = coverage.pixel(x, y);
        const auto current = *existing.pixel(x, y);
        if (operation_index == 1) {
          *value = std::max(*value, current);
        } else if (operation_index == 2) {
          *value = static_cast<std::uint8_t>((current * (255 - *value)) / 255);
        } else {
          *value = static_cast<std::uint8_t>((current * *value) / 255);
        }
      }
    }
  }
  canvas_->replace_selection_from_grayscale(coverage, tr("Make selection from path"));
  statusBar()->showMessage(tr("Made a selection from the path"));
}

void MainWindow::make_work_path_from_selection() {
  if (canvas_ == nullptr || !has_active_document()) {
    return;
  }
  if (!canvas_->has_selection()) {
    show_status_error(tr("Make a selection first"));
    return;
  }
  const auto tolerance_key = QStringLiteral("paths/makeWorkPathTolerance");
  auto settings = app_settings();
  QDialog dialog(this);
  dialog.setObjectName(QStringLiteral("makeWorkPathDialog"));
  dialog.setWindowTitle(tr("Make Work Path"));
  auto* layout = new QVBoxLayout(&dialog);
  auto* form = new QFormLayout();
  auto* tolerance = new QDoubleSpinBox(&dialog);
  tolerance->setObjectName(QStringLiteral("makeWorkPathToleranceSpin"));
  tolerance->setRange(0.5, 10.0);
  tolerance->setDecimals(1);
  tolerance->setSingleStep(0.5);
  tolerance->setSuffix(QStringLiteral(" px"));
  tolerance->setValue(std::clamp(settings.value(tolerance_key, 2.0).toDouble(), 0.5, 10.0));
  form->addRow(tr("Tolerance:"), tolerance);
  layout->addLayout(form);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget(buttons);
  // The QSS sub-control gotcha: spin-button styling lands AFTER children exist.
  dialog.setStyleSheet(dialog.styleSheet() + dialog_spinbox_button_style());
  if (run_non_modal_dialog(dialog) != QDialog::Accepted) {
    return;
  }
  settings.setValue(tolerance_key, tolerance->value());
  // The dialog is non-modal: the document (and canvas) can be closed or
  // switched while it is open, so re-validate before touching either.
  if (canvas_ == nullptr || !has_active_document() || !canvas_->has_selection()) {
    return;
  }

  const auto loops = trace_selection_outlines(canvas_->selected_document_region());
  VectorPath fitted;
  for (const auto& loop : loops) {
    std::vector<FitPoint> points;
    points.reserve(static_cast<std::size_t>(loop.points.size()));
    for (const auto& point : loop.points) {
      points.push_back(FitPoint{point.x(), point.y()});
    }
    auto subpath = fit_closed_loop(points, tolerance->value());
    if (subpath.anchors.size() < 2) {
      continue;
    }
    // Outer boundaries (clockwise in y-down coordinates) add coverage, holes
    // (counterclockwise) subtract; the tracer orders outers before their
    // holes, so the sequential combine reproduces the selection.
    subpath.op = loop_signed_area(points) >= 0.0 ? PathCombineOp::Add : PathCombineOp::Subtract;
    subpath.shape_group = static_cast<std::int32_t>(fitted.subpaths.size());
    fitted.subpaths.push_back(std::move(subpath));
  }
  if (fitted.subpaths.empty()) {
    show_status_error(tr("The selection is too small to trace"));
    return;
  }
  push_undo_snapshot(tr("Make work path"));
  auto& doc = document();
  DocumentPathId work_id = 0;
  if (auto* work = doc.work_path(); work != nullptr) {
    work->set_path(std::move(fitted));  // Photoshop replaces the work path
    work_id = work->id();
    // Anchor-selection keys into the replaced geometry are stale.
    canvas_->clear_path_edit_selection();
  } else {
    DocumentPath created(doc.allocate_path_id(), tr("Work Path").toStdString(),
                         DocumentPathKind::Work, std::move(fitted));
    created.mark_dirty();  // authored: no original resource bytes to re-emit
    work_id = doc.add_path(std::move(created)).id();
  }
  target_document_path_row(work_id);
  statusBar()->showMessage(tr("Made a work path from the selection."));
}

}  // namespace patchy::ui
