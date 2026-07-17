// Paths panel glue: row building with outline thumbnails, panel targeting
// (which path the pen/path tools edit), and the footer commands - New Path,
// Fill Path (foreground), Stroke Path (brush-sized band), Make Selection,
// Delete Path. The panel widget itself is presentation-only (paths_panel.cpp).
#include "ui/main_window.hpp"

#include "core/blend_math.hpp"
#include "core/document_path.hpp"
#include "core/layer_metadata.hpp"
#include "core/layer_render_utils.hpp"
#include "core/palette.hpp"
#include "core/path_fit.hpp"
#include "core/pattern_resource.hpp"
#include "core/vector_raster.hpp"
#include "core/vector_shape.hpp"
#include "ui/app_settings.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/main_window_shared.hpp"
#include "ui/paths_panel.hpp"
#include "ui/pattern_library.hpp"
#include "ui/qt_geometry.hpp"
#include "ui/selection_outline.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QImage>
#include <QLabel>
#include <QLineF>
#include <QMouseEvent>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPointingDevice>
#include <QSpinBox>
#include <QStatusBar>
#include <QTabletEvent>
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

// Dense document-space polylines for stroking with the brush engine: one per
// subpath, sampled every ~2 px (the engine applies its own stamp spacing
// between input events). Closed subpaths traverse their closing segment; open
// subpaths do NOT gain the fill-only implied chord.
std::vector<std::vector<QPointF>> stroke_polylines_for_path(const VectorPath& path) {
  std::vector<std::vector<QPointF>> polylines;
  for (const auto& subpath : path.subpaths) {
    if (subpath.anchors.size() < 2) {
      continue;
    }
    std::vector<QPointF> points;
    const auto anchor_count = subpath.anchors.size();
    const auto segment_count = subpath.closed ? anchor_count : anchor_count - 1;
    points.emplace_back(subpath.anchors[0].anchor_x, subpath.anchors[0].anchor_y);
    for (std::size_t i = 0; i < segment_count; ++i) {
      const auto& a = subpath.anchors[i];
      const auto& b = subpath.anchors[(i + 1) % anchor_count];
      const QPointF p0(a.anchor_x, a.anchor_y);
      const QPointF p1(a.out_x, a.out_y);
      const QPointF p2(b.in_x, b.in_y);
      const QPointF p3(b.anchor_x, b.anchor_y);
      const auto hull =
          QLineF(p0, p1).length() + QLineF(p1, p2).length() + QLineF(p2, p3).length();
      const int steps = std::clamp(static_cast<int>(std::lround(hull / 2.0)), 4, 400);
      for (int step = 1; step <= steps; ++step) {
        const double t = static_cast<double>(step) / steps;
        const double u = 1.0 - t;
        points.emplace_back(u * u * u * p0.x() + 3.0 * t * u * u * p1.x() +
                                3.0 * t * t * u * p2.x() + t * t * t * p3.x(),
                            u * u * u * p0.y() + 3.0 * t * u * u * p1.y() +
                                3.0 * t * t * u * p2.y() + t * t * t * p3.y());
      }
    }
    polylines.push_back(std::move(points));
  }
  return polylines;
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
    layer_path_thumbnail_cache_ = {};
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
        auto& cached = layer_path_thumbnail_cache_;
        if (cached.thumbnail.isNull() || cached.layer != layer->id() ||
            cached.content_revision != layer->content_revision() ||
            cached.document_width != doc.width() || cached.document_height != doc.height()) {
          cached.layer = layer->id();
          cached.content_revision = layer->content_revision();
          cached.document_width = doc.width();
          cached.document_height = doc.height();
          cached.thumbnail = path_outline_thumbnail(*layer_path, doc.width(), doc.height());
        }
        rows.push_back(PathsPanel::Row{PathsPanel::RowKind::LayerPath, 0, label, cached.thumbnail});
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
                                   cached_path_thumbnail(path, doc.width(), doc.height()),
                                   path.is_clipping_path()});
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

void MainWindow::toggle_selected_path_clipping() {
  if (paths_panel_ == nullptr || !has_active_document()) {
    return;
  }
  const auto row = paths_panel_->selected_row();
  if (!row.has_value() || row->kind != PathsPanel::RowKind::SavedPath) {
    show_status_error(tr("Select a saved path to use as the clipping path"));
    refresh_paths_panel();  // restore the action's checked state
    return;
  }
  auto& doc = document();
  auto* target = doc.find_path(row->id);
  if (target == nullptr) {
    return;
  }
  const bool make_clipping = !target->is_clipping_path();
  push_undo_snapshot(tr("Clipping path"));
  // At most one clipping path per document (resource 2999 names exactly one).
  for (auto& path : doc.paths()) {
    path.set_clipping_path(make_clipping && path.id() == row->id);
  }
  refresh_paths_panel();
  statusBar()->showMessage(make_clipping
                               ? tr("Set %1 as the clipping path.").arg(row->name)
                               : tr("Cleared the clipping path."));
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
  {
    const auto& doc = document();
    const auto active = doc.active_layer_id();
    const auto* layer = active.has_value() ? doc.find_layer(*active) : nullptr;
    if (layer == nullptr || layer->kind() != LayerKind::Pixel ||
        layer_pixels_are_procedural(*layer)) {
      show_status_error(tr("Select an editable pixel layer first"));
      return;
    }
    if (layer_id_locks_image_pixels(*active)) {
      show_status_error(tr("Layer pixels are locked."));
      return;
    }
  }

  // Options: contents (foreground/background/pattern) and opacity, persisted
  // like the other paths dialogs.
  const auto contents_key = QStringLiteral("paths/fillContents");
  const auto pattern_key = QStringLiteral("paths/fillPatternId");
  const auto opacity_key = QStringLiteral("paths/fillOpacity");
  auto settings = app_settings();
  QDialog dialog(this);
  dialog.setObjectName(QStringLiteral("fillPathDialog"));
  dialog.setWindowTitle(tr("Fill Path"));
  auto* layout = new QVBoxLayout(&dialog);
  auto* form = new QFormLayout();
  auto* contents = new QComboBox(&dialog);
  contents->setObjectName(QStringLiteral("fillPathContentsCombo"));
  contents->addItems({tr("Foreground color"), tr("Background color"), tr("Pattern")});
  form->addRow(tr("Contents:"), contents);
  auto* pattern_combo = new QComboBox(&dialog);
  pattern_combo->setObjectName(QStringLiteral("fillPathPatternCombo"));
  pattern_combo->setIconSize(QSize(24, 24));
  for (const auto& resource : document().metadata().patterns.patterns) {
    const auto name = resource.name.empty() ? tr("Embedded pattern")
                                            : QString::fromStdString(resource.name);
    pattern_combo->addItem(name, QString::fromStdString(resource.id));
  }
  for (const auto& entry : pattern_library().entries()) {
    if (pattern_combo->findData(entry.id) < 0) {
      pattern_combo->addItem(QIcon(entry.thumbnail), entry.name, entry.id);
    }
  }
  form->addRow(tr("Pattern:"), pattern_combo);
  auto* opacity = new QSpinBox(&dialog);
  opacity->setObjectName(QStringLiteral("fillPathOpacitySpin"));
  opacity->setRange(1, 100);
  opacity->setSuffix(QStringLiteral("%"));
  form->addRow(tr("Opacity:"), opacity);
  layout->addLayout(form);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget(buttons);
  contents->setCurrentIndex(std::clamp(settings.value(contents_key, 0).toInt(), 0, 2));
  if (const auto stored = settings.value(pattern_key).toString(); !stored.isEmpty()) {
    if (const auto index = pattern_combo->findData(stored); index >= 0) {
      pattern_combo->setCurrentIndex(index);
    }
  }
  opacity->setValue(std::clamp(settings.value(opacity_key, 100).toInt(), 1, 100));
  const auto sync_pattern_enabled = [contents, pattern_combo, form] {
    const bool pattern_active = contents->currentIndex() == 2;
    pattern_combo->setEnabled(pattern_active);
    if (auto* label = form->labelForField(pattern_combo); label != nullptr) {
      label->setEnabled(pattern_active);  // grey the row label with the combo
    }
  };
  sync_pattern_enabled();
  connect(contents, &QComboBox::currentIndexChanged, &dialog, sync_pattern_enabled);
  // The QSS sub-control gotcha: spin-button styling lands AFTER children exist.
  dialog.setStyleSheet(dialog.styleSheet() + dialog_spinbox_button_style());
  if (run_non_modal_dialog(dialog) != QDialog::Accepted) {
    return;
  }
  settings.setValue(contents_key, contents->currentIndex());
  settings.setValue(pattern_key, pattern_combo->currentData().toString());
  settings.setValue(opacity_key, opacity->value());
  // Non-modal dialog: re-resolve everything the event loop may have changed.
  if (canvas_ == nullptr || !has_active_document()) {
    return;
  }
  path = resolved_panel_path(&path_name);
  if (path == nullptr || path->empty()) {
    return;
  }
  auto& doc = document();
  const auto active = doc.active_layer_id();
  auto* layer = active.has_value() ? doc.find_layer(*active) : nullptr;
  if (layer == nullptr || layer->kind() != LayerKind::Pixel || layer_pixels_are_procedural(*layer) ||
      layer_id_locks_image_pixels(*active)) {
    show_status_error(tr("Select an editable pixel layer first"));
    return;
  }

  const bool use_pattern = contents->currentIndex() == 2;
  std::optional<PatternResource> pattern;
  if (use_pattern) {
    const auto id = pattern_combo->currentData().toString();
    if (!id.isEmpty()) {
      if (const auto* existing = doc.metadata().patterns.find(id.toStdString());
          existing != nullptr && !existing->tile.empty()) {
        pattern = *existing;
      } else if (auto resource = pattern_library().resource(id); resource.has_value()) {
        pattern = std::move(*resource);
      }
    }
    if (!pattern.has_value() || pattern->tile.empty()) {
      show_status_error(tr("Choose a pattern to fill with"));
      return;
    }
  }

  push_undo_snapshot(tr("Fill path"));
  const auto coverage = path_selection_coverage(*path, doc.width(), doc.height());
  const auto color =
      contents->currentIndex() == 1 ? canvas_->secondary_color() : canvas_->primary_color();
  const int opacity_percent = opacity->value();
  const auto* palette_snap = canvas_->palette_snap_for_edits();
  auto& pixels = layer->pixels();
  const auto bounds = layer->bounds();
  const bool lock_transparency = layer_locks_transparent_pixels(*layer);
  const auto tile_width = pattern.has_value() ? pattern->tile.width() : 0;
  const auto tile_height = pattern.has_value() ? pattern->tile.height() : 0;
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
      auto alpha = static_cast<int>(*coverage.pixel(document_x, document_y));
      if (alpha == 0) {
        continue;
      }
      alpha = alpha * opacity_percent / 100;
      std::uint8_t source_red = static_cast<std::uint8_t>(color.red());
      std::uint8_t source_green = static_cast<std::uint8_t>(color.green());
      std::uint8_t source_blue = static_cast<std::uint8_t>(color.blue());
      if (pattern.has_value()) {
        // Tiles align to the document origin (the Photoshop fill rule).
        const auto* tile_pixel = pattern->tile.pixel(document_x % tile_width,
                                                     document_y % tile_height);
        source_red = tile_pixel[0];
        source_green = tile_pixel[1];
        source_blue = tile_pixel[2];
        if (pattern->tile.format().channels >= 4) {
          alpha = alpha * tile_pixel[3] / 255;
        }
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
      if (palette_snap != nullptr) {
        // Palette mode writes hard pixels: coverage below the threshold writes
        // nothing, anything above writes the source snapped to the palette.
        if (static_cast<float>(alpha) / 255.0F < palette_snap->coverage_threshold) {
          continue;
        }
        pixel[0] = source_red;
        pixel[1] = source_green;
        if (channels >= 3) {
          pixel[2] = source_blue;
        }
        if (channels >= 4 && !lock_transparency) {
          pixel[3] = 255;
        }
        snap_pixel_to_palette(pixel, channels, *palette_snap);
        continue;
      }
      const auto blend = [alpha](std::uint8_t source, std::uint8_t destination) {
        return static_cast<std::uint8_t>((source * alpha + destination * (255 - alpha) + 127) / 255);
      };
      pixel[0] = blend(source_red, pixel[0]);
      pixel[1] = blend(source_green, pixel[1]);
      if (channels >= 3) {
        pixel[2] = blend(source_blue, pixel[2]);
      }
      if (channels >= 4 && !lock_transparency) {
        pixel[3] = static_cast<std::uint8_t>(existing_alpha + (255 - existing_alpha) * alpha / 255);
      }
    }
  }
  canvas_->document_changed();
  refresh_layer_thumbnails();
  statusBar()->showMessage(use_pattern ? tr("Filled the path with the pattern")
                                       : tr("Filled the path"));
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

  const auto simulate_key = QStringLiteral("paths/strokeSimulatePressure");
  auto settings = app_settings();
  QDialog dialog(this);
  dialog.setObjectName(QStringLiteral("strokePathDialog"));
  dialog.setWindowTitle(tr("Stroke Path"));
  auto* layout = new QVBoxLayout(&dialog);
  auto* note = new QLabel(tr("Strokes along the path with the current brush and the "
                             "foreground color."),
                          &dialog);
  note->setWordWrap(true);
  layout->addWidget(note);
  auto* simulate = new QCheckBox(tr("Simulate pressure"), &dialog);
  simulate->setObjectName(QStringLiteral("strokePathSimulatePressureCheck"));
  simulate->setToolTip(tr("Tapers the stroke from thin to full and back, as if drawn with a "
                          "pressure pen."));
  simulate->setChecked(settings.value(simulate_key, false).toBool());
  layout->addWidget(simulate);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget(buttons);
  if (run_non_modal_dialog(dialog) != QDialog::Accepted) {
    return;
  }
  settings.setValue(simulate_key, simulate->isChecked());
  // Non-modal dialog: re-resolve everything the event loop may have changed.
  if (canvas_ == nullptr || !has_active_document()) {
    return;
  }
  path = resolved_panel_path(&path_name);
  if (path == nullptr || path->empty()) {
    return;
  }

  const auto polylines = stroke_polylines_for_path(*path);
  if (polylines.empty()) {
    show_status_error(tr("Select a path to stroke"));
    return;
  }
  // One history entry for the whole command; the per-stroke undo pushes from
  // the synthetic input below are suppressed.
  push_undo_snapshot(tr("Stroke path"));
  scripted_stroke_undo_suppressed_ = true;
  const auto previous_tool = canvas_->tool();
  canvas_->set_tool(CanvasTool::Brush);
  const auto origin = canvas_->widget_position_for_document_point(QPoint(0, 0));
  const auto zoom = canvas_->zoom();
  const auto widget_point = [&](QPointF document_point) {
    return QPointF(origin.x() + document_point.x() * zoom,
                   origin.y() + document_point.y() * zoom);
  };
  // The stylus device mirrors the test harness's synthetic pen: pressure
  // rides the canvas's own pen-input settings, so Simulate Pressure behaves
  // exactly like drawing the path with a real pen.
  static QPointingDevice stroke_pen(
      QStringLiteral("Patchy stroke-path pen"), 1004, QInputDevice::DeviceType::Stylus,
      QPointingDevice::PointerType::Pen,
      QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 3);
  for (const auto& polyline : polylines) {
    if (polyline.empty()) {
      continue;
    }
    // Cumulative arc length drives the taper profile.
    std::vector<double> arc(polyline.size(), 0.0);
    for (std::size_t i = 1; i < polyline.size(); ++i) {
      arc[i] = arc[i - 1] + QLineF(polyline[i - 1], polyline[i]).length();
    }
    const auto total = std::max(arc.back(), 1e-6);
    const auto send_point = [&](QEvent::Type mouse_type, QEvent::Type tablet_type,
                                std::size_t index) {
      const auto position = widget_point(polyline[index]);
      const auto global = canvas_->mapToGlobal(position.toPoint());
      if (simulate->isChecked()) {
        // The classic taper: thin at the ends, full in the middle.
        const auto pressure = std::sin(arc[index] / total * 3.14159265358979323846);
        QTabletEvent event(tablet_type, &stroke_pen, position, QPointF(global),
                           std::clamp(pressure, 0.02, 1.0), 0.0F, 0.0F, 0.0F, 0.0, 0.0F,
                           Qt::NoModifier, Qt::LeftButton,
                           tablet_type == QEvent::TabletRelease ? Qt::NoButton : Qt::LeftButton);
        QApplication::sendEvent(canvas_, &event);
      } else {
        QMouseEvent event(mouse_type, position, QPointF(global),
                          mouse_type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                          mouse_type == QEvent::MouseButtonRelease ? Qt::NoButton
                                                                   : Qt::LeftButton,
                          Qt::NoModifier);
        QApplication::sendEvent(canvas_, &event);
      }
    };
    send_point(QEvent::MouseButtonPress, QEvent::TabletPress, 0);
    for (std::size_t i = 1; i < polyline.size(); ++i) {
      send_point(QEvent::MouseMove, QEvent::TabletMove, i);
    }
    send_point(QEvent::MouseButtonRelease, QEvent::TabletRelease, polyline.size() - 1);
  }
  canvas_->set_tool(previous_tool);
  scripted_stroke_undo_suppressed_ = false;
  QApplication::processEvents();
  refresh_layer_thumbnails();
  statusBar()->showMessage(tr("Stroked the path with the current brush"));
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
