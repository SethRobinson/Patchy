// Type > Convert to Shape and Type > Create Work Path (GitHub issue 598; docs/text-tool.md,
// "Convert to Shape"): text layers as vector outlines, from the glyph outlines the text
// renderer draws (text_layer_outlines), never a raster trace.
#include "core/document_path.hpp"
#include "core/layer_metadata.hpp"
#include "core/layer_render_utils.hpp"
#include "core/rect_utils.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/main_window.hpp"
#include "ui/qt_geometry.hpp"
#include "ui/text_layer_painter.hpp"
#include "ui/text_outline.hpp"

#include <QStatusBar>

#include <string>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

// The text layers a Type command acts on: the selected ones, folders counted through their
// contents (the rasterize rule).
std::vector<LayerId> text_layer_targets(const Document& document, const std::vector<LayerId>& ids) {
  std::vector<LayerId> targets;
  for (const auto id : ids) {
    const auto* layer = document.find_layer(id);
    if (layer != nullptr && layer_is_text(*layer)) {
      targets.push_back(id);
    }
  }
  return targets;
}

}  // namespace

void MainWindow::convert_text_layers_to_shapes() {
  if (canvas_ == nullptr || !has_active_document()) {
    return;
  }
  finish_active_text_editor();
  auto& doc = document();
  const auto targets = text_layer_targets(std::as_const(doc), rasterize_target_layer_ids(selected_or_active_layer_ids()));
  if (targets.empty()) {
    show_status_error(tr("Select a text layer first."));
    return;
  }
  struct PendingShape {
    LayerId id{};
    VectorShapeContent content;
  };
  std::vector<PendingShape> pending;
  QString refusal;
  bool substituted_font = false;
  bool dropped_images = false;
  for (const auto id : targets) {
    if (layer_id_locks_image_pixels(id)) {
      refusal = tr("Layer pixels are locked.");
      continue;
    }
    const auto* layer = std::as_const(doc).find_layer(id);
    std::string error;
    const auto outlines = text_layer_outlines(*layer, &error);
    auto content = outlines.has_value() ? shape_content_from_text_outlines(outlines->paints) : std::nullopt;
    if (!content.has_value()) {
      refusal = tr("Could not convert \"%1\" to a shape.").arg(QString::fromStdString(layer->name()));
      continue;
    }
    substituted_font = substituted_font || !outlines->note.empty();
    dropped_images = dropped_images || outlines->skipped_glyph_images > 0;
    pending.push_back(PendingShape{id, std::move(*content)});
  }
  if (pending.empty()) {
    show_status_error(refusal);
    return;
  }
  push_undo_snapshot(pending.size() == 1U ? tr("Convert to Shape") : tr("Convert to Shapes"));
  const auto canvas_rect = Rect::from_size(doc.width(), doc.height());
  Rect affected;
  for (auto& change : pending) {
    auto* layer = doc.find_layer(change.id);
    if (layer == nullptr) {
      continue;
    }
    affected = unite_rect(affected, layer_render_bounds(std::as_const(*layer)));
    convert_text_layer_to_shape(*layer, std::move(change.content), canvas_rect, &doc.metadata().patterns);
    affected = unite_rect(affected, layer_render_bounds(std::as_const(*layer)));
  }
  refresh_layer_list();
  refresh_layer_controls();
  refresh_paths_panel();
  canvas_->document_changed(affected.empty() ? QRect() : to_qrect(affected));
  auto message = pending.size() == 1U ? tr("Converted the text layer to a shape.")
                                      : tr("Converted %1 text layers to shapes.").arg(pending.size());
  if (substituted_font) {
    message += QLatin1Char(' ') + tr("A missing font was drawn in a substitute.");
  }
  if (dropped_images) {
    message += QLatin1Char(' ') + tr("Color emoji have no outlines and were left out.");
  }
  statusBar()->showMessage(message);
}

void MainWindow::create_work_path_from_text() {
  if (canvas_ == nullptr || !has_active_document()) {
    return;
  }
  finish_active_text_editor();
  auto& doc = document();
  const auto targets = text_layer_targets(std::as_const(doc), rasterize_target_layer_ids(selected_or_active_layer_ids()));
  if (targets.empty()) {
    show_status_error(tr("Select a text layer first."));
    return;
  }
  VectorPath path;
  QString refusal;
  for (const auto id : targets) {
    const auto* layer = std::as_const(doc).find_layer(id);
    std::string error;
    const auto outlines = text_layer_outlines(*layer, &error);
    if (!outlines.has_value()) {
      refusal = tr("Could not outline \"%1\".").arg(QString::fromStdString(layer->name()));
      continue;
    }
    for (const auto& paint : outlines->paints) {
      append_outline_subpaths(paint.path, path, nullptr);
    }
  }
  if (path.subpaths.empty()) {
    show_status_error(refusal.isEmpty() ? tr("The text has no outlines.") : refusal);
    return;
  }
  push_undo_snapshot(tr("Create work path"));
  DocumentPathId work_id = 0;
  if (auto* work = doc.work_path(); work != nullptr) {
    work->set_path(std::move(path));  // Photoshop replaces the work path
    work_id = work->id();
    // Anchor-selection keys into the replaced geometry are stale.
    canvas_->clear_path_edit_selection();
  } else {
    DocumentPath created(doc.allocate_path_id(), tr("Work Path").toStdString(), DocumentPathKind::Work,
                         std::move(path));
    created.mark_dirty();  // authored: no original resource bytes to re-emit
    work_id = doc.add_path(std::move(created)).id();
  }
  target_document_path_row(work_id);
  statusBar()->showMessage(tr("Made a work path from the text."));
}

}  // namespace patchy::ui
