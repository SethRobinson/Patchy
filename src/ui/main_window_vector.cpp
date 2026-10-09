#include "core/vector_compound.hpp"
// Vector shape tool flows: a released Shape/Path-mode drag from the canvas
// becomes a new shape layer, an addition to the active shape layer, or work
// path subpaths. The options-bar swatches and per-mode widget visibility for
// the Line/Rectangle/Ellipse tools live here too (built in
// main_window_actions.cpp, refined by refresh_vector_tool_options_visibility).
#include "ui/main_window.hpp"
#include "ui/vector_operations.hpp"

#include "core/path_simplify.hpp"
#include "core/shape_combine.hpp"
#include "core/blend_math.hpp"
#include "core/document_path.hpp"
#include "core/layer_render_utils.hpp"
#include "core/layer_tree.hpp"
#include "core/pattern_resource.hpp"
#include "core/pixel_tools.hpp"
#include "core/vector_live_shapes.hpp"
#include "core/vector_raster.hpp"
#include "core/vector_shape.hpp"
#include "formats/svg_document_io.hpp"
#include "ui/app_settings.hpp"
#include "ui/image_trace_dialog.hpp"
#include "ui/background_workers.hpp"
#include "ui/color_panel.hpp"
#include "ui/custom_shape_library.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/edit_conversions.hpp"
#include "ui/gradient_library.hpp"
#include "ui/gradient_manager_dialog.hpp"
#include "ui/main_window_shared.hpp"
#include "ui/pattern_library.hpp"
#include "ui/pattern_manager_dialog.hpp"
#include "ui/photo_pattern_presets.hpp"
#include "ui/qt_geometry.hpp"
#include "ui/shape_appearance_dialog.hpp"
#include "ui/appearance_properties.hpp"
#include "ui/shape_create_dialog.hpp"
#include "ui/localization.hpp"
#include "ui/measurement_units.hpp"

#include <QBrush>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QIcon>
#include <QLineEdit>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QSignalBlocker>
#include <QScopeGuard>
#include <atomic>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QTimer>
#include <QToolButton>
#include <QTransform>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>

namespace patchy::ui {

namespace {

// Ordered to match the options-bar combine combo: index 0 is "New Layer"
// (Shape mode) / plain Add (Path mode); 1..4 are the PSD operation values.
PathCombineOp combine_op_for_index(int index) noexcept {
  switch (index) {
    case 2:
      return PathCombineOp::Subtract;
    case 3:
      return PathCombineOp::Intersect;
    case 4:
      return PathCombineOp::Xor;
    default:
      return PathCombineOp::Add;
  }
}

QString shape_layer_base_name(LiveShapeKind kind) {
  switch (kind) {
    case LiveShapeKind::Ellipse:
      return MainWindow::tr("Ellipse %1");
    case LiveShapeKind::Line:
      return MainWindow::tr("Line %1");
    default:
      return MainWindow::tr("Rectangle %1");
  }
}

void collect_shape_layer_names(const std::vector<Layer>& layers, std::set<std::string>& names) {
  for (const auto& layer : layers) {
    names.insert(layer.name());
    collect_shape_layer_names(layer.children(), names);
  }
}

// Copies pattern tiles referenced by the fill (and stroke paint) into the
// document store so the rasterizer and the PSD writer can resolve them
// (the ensure_patterns_for_style convention: library first, bundle repair).
void ensure_vector_fill_patterns(Document& doc, const VectorShapeContent& content,
                                 const PatternLibrary& library, const PatternStore* available = nullptr) {
  std::vector<const VectorFill*> paints{&content.fill, &content.stroke.content};
  for (const auto& part : content.parts) {
    paints.push_back(&part.fill);
    paints.push_back(&part.stroke.content);
  }
  for (const auto* fill : paints) {
    if (fill->kind != VectorFillKind::Pattern || fill->pattern_id.empty()) {
      continue;
    }
    // A stored entry only satisfies the reference when it can actually
    // render; an empty tile (a poisoned adopt) must be replaced, which the
    // healing adopt below performs.
    if (const auto* existing = doc.metadata().patterns.find(fill->pattern_id);
        existing != nullptr && !existing->tile.empty()) {
      continue;
    }
    if (const auto* resource = available ? available->find(fill->pattern_id) : nullptr;
        resource && !resource->tile.empty()) {
      doc.metadata().patterns.adopt(*resource);
    } else if (auto library_resource = library.resource(QString::fromStdString(fill->pattern_id));
        library_resource.has_value()) {
      library_resource->provenance = PatternProvenance::Authored;
      doc.metadata().patterns.adopt(*library_resource);
    } else if (auto bundled = bundled_pattern_resource(fill->pattern_id); bundled.has_value()) {
      doc.metadata().patterns.adopt(*bundled);
    }
  }
}

}  // namespace

void MainWindow::handle_vector_shape_drawn(LiveShapeKind kind, QRectF bounds, QPointF line_start,
                                           QPointF line_end) {
  if (!has_active_document() || canvas_ == nullptr) {
    return;
  }
  if (kind == LiveShapeKind::Line) {
    const auto length = std::hypot(line_end.x() - line_start.x(), line_end.y() - line_start.y());
    if (length < 1.0) {
      return;
    }
  } else if (bounds.width() < 1.0 || bounds.height() < 1.0) {
    return;
  }

  LiveShapeParams params;
  params.kind = kind;
  params.resolution = document().print_settings().horizontal_ppi;
  if (kind == LiveShapeKind::Line) {
    params.line_start_x = line_start.x();
    params.line_start_y = line_start.y();
    params.line_end_x = line_end.x();
    params.line_end_y = line_end.y();
    params.line_weight = std::max(1.0, current_vector_line_weight_);
    // Photoshop's default arrowhead proportions: width 5x, length 10x weight.
    params.arrow_start = current_line_arrow_start_;
    params.arrow_end = current_line_arrow_end_;
    if (current_line_arrow_start_ || current_line_arrow_end_) {
      params.arrow_width = params.line_weight * 5.0;
      params.arrow_length = params.line_weight * 10.0;
    }
  } else {
    params.left = bounds.left();
    params.top = bounds.top();
    params.right = bounds.right();
    params.bottom = bounds.bottom();
    if (kind == LiveShapeKind::Rectangle && current_shape_corner_radius_ > 0) {
      params.kind = LiveShapeKind::RoundedRectangle;
      const auto radius = static_cast<double>(current_shape_corner_radius_);
      params.corner_radii = {radius, radius, radius, radius};
    }
  }
  commit_live_shape(std::move(params));
}

void MainWindow::commit_live_shape(LiveShapeParams params) {
  const auto kind = params.kind;
  populate_live_shape_box_corners(params);
  if (kind == LiveShapeKind::Line) {
    // The line's bbox is the generated quad's hull (the drag endpoints sit on
    // the centerline, half a weight inside it).
    VectorPath preview;
    preview.subpaths = generate_live_shape_subpaths(params);
    if (const auto hull = preview.bounds(); hull.has_value()) {
      params.left = hull->left;
      params.top = hull->top;
      params.right = hull->right;
      params.bottom = hull->bottom;
    }
  }

  // While the vector-mask target is active, drags extend the mask path
  // regardless of the Shape/Path mode (the Photoshop behavior).
  if (canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::VectorMask) {
    canvas_->add_subpaths_to_vector_mask(generate_live_shape_subpaths(params),
                                         tr("Add to vector mask"));
    return;
  }
  if (current_vector_tool_mode_ == VectorToolMode::Path) {
    add_drag_to_work_path(params);
  } else {
    create_shape_layer_from_drag(params);
  }
}

void MainWindow::handle_shape_create_requested(CanvasTool tool, QPointF document_point) {
  if (!has_active_document() || canvas_ == nullptr) {
    return;
  }
  auto& memory = shape_create_memory_[static_cast<int>(tool)];
  ShapeCreateRequest request;
  request.tool = tool;
  request.width = memory.width;
  request.height = memory.height;
  request.from_center = memory.from_center;
  const auto radius = static_cast<double>(std::max(0, current_shape_corner_radius_));
  request.corner_radii = {radius, radius, radius, radius};
  request.units = dialog_field_units();
  const auto result = request_shape_create_settings(this, request);
  if (!result.has_value() || !has_active_document() || canvas_ == nullptr) {
    return;
  }
  memory.width = result->width;
  memory.height = result->height;
  memory.from_center = result->from_center;
  // Photoshop's placement: the click is the top-left corner, or the center.
  const QPointF top_left =
      result->from_center
          ? document_point - QPointF(result->width / 2.0, result->height / 2.0)
          : document_point;
  const QRectF bounds(top_left, QSizeF(result->width, result->height));
  if (tool == CanvasTool::Rectangle || tool == CanvasTool::Ellipse) {
    LiveShapeParams params;
    params.kind = tool == CanvasTool::Rectangle ? LiveShapeKind::Rectangle : LiveShapeKind::Ellipse;
    params.resolution = document().print_settings().horizontal_ppi;
    params.left = bounds.left();
    params.top = bounds.top();
    params.right = bounds.right();
    params.bottom = bounds.bottom();
    if (tool == CanvasTool::Rectangle &&
        std::any_of(result->corner_radii.begin(), result->corner_radii.end(),
                    [](double value) { return value > 0.0; })) {
      params.kind = LiveShapeKind::RoundedRectangle;
      params.corner_radii = result->corner_radii;
    }
    commit_live_shape(std::move(params));
    return;
  }
  VectorPath path;
  if (tool == CanvasTool::Polygon) {
    // Unit polygon (first vertex pointing up) scaled into the box, so W and H
    // may differ, like Photoshop's Create Polygon dialog.
    auto subpath = generate_polygon_subpath(0.0, 0.0, 1.0, -std::acos(-1.0) / 2.0,
                                            canvas_->polygon_sides(),
                                            canvas_->polygon_star_inset());
    if (subpath.anchors.empty()) {
      return;
    }
    path.subpaths.push_back(std::move(subpath));
    transform_vector_path(path, {bounds.width() / 2.0, 0.0, 0.0, bounds.height() / 2.0,
                                 bounds.center().x(), bounds.center().y()});
    handle_vector_path_committed(std::move(path), true, VectorPathSource::Polygon);
    return;
  }
  const auto* custom = canvas_->custom_shape_path();
  if (custom == nullptr || custom->empty()) {
    show_status_error(tr("Pick a custom shape first"));
    return;
  }
  path = *custom;
  transform_vector_path(path, {bounds.width(), 0.0, 0.0, bounds.height(), bounds.x(), bounds.y()});
  handle_vector_path_committed(std::move(path), true, VectorPathSource::CustomShape);
}

void MainWindow::create_shape_layer_from_drag(const LiveShapeParams& params) {
  auto subpaths = generate_live_shape_subpaths(params);
  create_or_extend_shape_layer(std::move(subpaths), params, shape_layer_base_name(params.kind));
}

void MainWindow::create_or_extend_shape_layer(std::vector<PathSubpath> subpaths,
                                              std::optional<LiveShapeParams> origination,
                                              const QString& name_pattern) {
  auto& doc = document();
  if (subpaths.empty()) {
    return;
  }
  const auto canvas_rect = Rect::from_size(doc.width(), doc.height());
  const auto* patterns = &doc.metadata().patterns;

  // A non-default combine op extends the active shape layer (Photoshop's
  // add/subtract/intersect/exclude shape-area modes).
  if (current_vector_combine_index_ != 0) {
    if (const auto active = doc.active_layer_id(); active.has_value()) {
      if (auto* layer = doc.find_layer(*active);
          layer != nullptr && layer_is_vector_shape(*layer) && vector_lock_reason(*layer).empty()) {
        push_undo_snapshot(tr("Edit shape"));
        const auto old_effect_rect =
            to_qrect(layer_bounds_with_effects(std::as_const(*layer), std::as_const(*layer).bounds()));
        auto content = *layer->vector_shape();
        const auto group = content.path.next_shape_group();
        const auto op = combine_op_for_index(current_vector_combine_index_);
        if (!content.parts.empty()) {
          if (op == PathCombineOp::Add) {
            VectorShapePart part;
            part.groups = {group};
            part.fill = content.fill;
            part.stroke = content.stroke;
            part.pattern_anchor = layer_effects_reference_point(*layer);
            content.parts.push_back(std::move(part));
          } else {
            for (auto& part : content.parts) { part.groups.push_back(group); }
          }
        }
        for (auto& subpath : subpaths) {
          subpath.shape_group = group;
          subpath.op = op;
          content.path.subpaths.push_back(std::move(subpath));
        }
        if (origination.has_value()) {
          origination->index = group;
          content.origination.push_back(std::move(*origination));
        }
        layer->set_vector_shape(std::move(content));
        layer->metadata()[kLayerMetadataVectorRasterStatus] = kVectorRasterStatusPatchy;
        mark_layer_vector_block_dirty(*layer);
        update_vector_shape_raster(*layer, canvas_rect, patterns);
        // Extending a shape layer changes no row structure, and the commit
        // only dirties the layer's own effect rect: a full layer-list rebuild
        // plus full-canvas recomposite per combine drag dominated the shape
        // workflow (synchronously so below the async-defer threshold and
        // always on wasm).
        refresh_layer_thumbnails();
        refresh_layer_controls();
        path_row_hidden_for_layer_.reset();  // a fresh drag re-shows the outline
        refresh_paths_panel();
        canvas_->document_changed_effect_bounds(old_effect_rect.united(
            to_qrect(layer_bounds_with_effects(std::as_const(*layer), std::as_const(*layer).bounds()))));
        return;
      }
    }
  }

  std::set<std::string> existing_names;
  collect_shape_layer_names(doc.layers(), existing_names);
  int suffix = 1;
  std::string name;
  do {
    name = name_pattern.arg(suffix++).toStdString();
  } while (existing_names.contains(name));

  auto anchor_id = doc.active_layer_id();
  if (const auto selected_ids = selected_layer_ids(); !selected_ids.empty()) {
    anchor_id = selected_ids.front();
  }

  push_undo_snapshot(tr("New shape layer"));
  Layer layer(doc.allocate_layer_id(), name, PixelBuffer());
  const auto layer_id = layer.id();
  auto content = current_shape_appearance_content();
  content.path.subpaths = std::move(subpaths);
  if (origination.has_value()) {
    content.origination = {std::move(*origination)};
  }
  // A pattern default picked from the library must land in the document store
  // before the rasterize (and before the writer's Patt collection).
  ensure_vector_fill_patterns(doc, content, pattern_library());
  layer.set_vector_shape(std::move(content));
  layer.metadata()[kLayerMetadataVectorShape] = "1";
  layer.metadata()[kLayerMetadataVectorRasterStatus] = kVectorRasterStatusPatchy;
  update_vector_shape_raster(layer, canvas_rect, patterns);
  insert_layer_after_anchor(doc, std::move(layer), anchor_id);
  doc.set_active_layer(layer_id);
  refresh_layer_list();
  refresh_layer_controls();
  path_row_hidden_for_layer_.reset();
  refresh_paths_panel();  // the transient layer-path row auto-targets the new shape
  // Bounded: a new shape only dirties its own effect rect; the full-canvas
  // recomposite per drag-out dominated shape workflows at small canvas sizes
  // (synchronous below the async-defer threshold, always synchronous on wasm).
  if (const auto* created = std::as_const(doc).find_layer(layer_id); created != nullptr) {
    canvas_->document_changed_effect_bounds(to_qrect(layer_bounds_with_effects(*created, created->bounds())));
  } else {
    canvas_->document_changed();
  }
  statusBar()->showMessage(tr("Created shape layer %1.").arg(QString::fromStdString(name)));
}

void MainWindow::handle_vector_path_committed(VectorPath path, bool closed,
                                              VectorPathSource source) {
  (void)closed;  // open pen paths fill their implied chord like Photoshop
  if (!has_active_document() || canvas_ == nullptr || path.subpaths.empty()) {
    return;
  }
  // While the vector-mask target is active, polygon/custom commits extend the
  // mask path (pen commits handle this canvas-side before reaching here).
  if (canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::VectorMask) {
    canvas_->add_subpaths_to_vector_mask(std::move(path.subpaths), tr("Add to vector mask"));
    return;
  }
  // These tools have no Pixels behavior: Shape creates/extends a shape layer,
  // anything else lands on the work path.
  if (current_vector_tool_mode_ == VectorToolMode::Shape) {
    const auto name_pattern = source == VectorPathSource::Polygon ? tr("Polygon %1")
                              : source == VectorPathSource::CustomShape
                                  ? tr("Custom Shape %1")
                                  : tr("Shape %1");
    create_or_extend_shape_layer(std::move(path.subpaths), std::nullopt, name_pattern);
    return;
  }
  add_subpaths_to_work_path(std::move(path.subpaths));
}

void MainWindow::add_subpaths_to_work_path(std::vector<PathSubpath> subpaths) {
  auto& doc = document();
  if (subpaths.empty()) {
    return;
  }
  const auto op = combine_op_for_index(current_vector_combine_index_);
  // A path explicitly selected in the Paths panel receives the subpaths
  // instead of the work path.
  if (active_document_path_id_.has_value()) {
    if (auto* target = doc.find_path(*active_document_path_id_); target != nullptr) {
      push_undo_snapshot(tr("Add to path"));
      auto path = target->path();
      const auto group = path.next_shape_group();
      for (auto& subpath : subpaths) {
        subpath.shape_group = group;
        subpath.op = op;
        path.subpaths.push_back(std::move(subpath));
      }
      target->set_path(std::move(path));
      refresh_paths_panel();
      statusBar()->showMessage(tr("Added the shape to %1.").arg(QString::fromStdString(target->name())));
      return;
    }
  }
  push_undo_snapshot(tr("Add to work path"));
  DocumentPathId work_id = 0;
  if (auto* work = doc.work_path(); work != nullptr) {
    auto path = work->path();
    const auto group = path.next_shape_group();
    for (auto& subpath : subpaths) {
      subpath.shape_group = group;
      subpath.op = op;
      path.subpaths.push_back(std::move(subpath));
    }
    work->set_path(std::move(path));
    work_id = work->id();
  } else {
    VectorPath path;
    for (auto& subpath : subpaths) {
      subpath.op = op;
      path.subpaths.push_back(std::move(subpath));
    }
    DocumentPath created(doc.allocate_path_id(), tr("Work Path").toStdString(),
                         DocumentPathKind::Work, std::move(path));
    created.mark_dirty();  // authored: no original resource bytes to re-emit
    work_id = doc.add_path(std::move(created)).id();
  }
  // Photoshop highlights the Work Path row as soon as you draw, keeping the
  // outline visible across tool switches.
  active_document_path_id_ = work_id;
  path_row_hidden_for_layer_.reset();
  if (canvas_ != nullptr) {
    canvas_->set_active_document_path(work_id);
  }
  refresh_paths_panel();
  statusBar()->showMessage(tr("Added the path to the work path."));
}

void MainWindow::add_drag_to_work_path(const LiveShapeParams& params) {
  add_subpaths_to_work_path(generate_live_shape_subpaths(params));
}

VectorShapeContent MainWindow::current_shape_appearance_content() const {
  VectorShapeContent content;
  content.fill = current_vector_fill_;
  content.stroke.enabled = current_vector_stroke_enabled_;
  content.stroke.width = current_vector_stroke_width_;
  // Photoshop's default for new shapes: the stroke hugs the inside of the path.
  content.stroke.alignment = VectorStrokeAlignment::Inside;
  content.stroke.content = current_vector_stroke_paint_;
  return content;
}

void MainWindow::refresh_path_point_count_chip() {
  if (path_point_count_chip_ == nullptr) {
    return;
  }
  // Every per-point path tool shows the multi-point selection count (Direct
  // Select, and the Pen family whose Ctrl latch selects the same way); Path
  // Select selects whole shapes, where an anchor count would be noise.
  const bool per_point_tool = current_tool_ == CanvasTool::Pen ||
                              current_tool_ == CanvasTool::DirectSelect ||
                              current_tool_ == CanvasTool::AddAnchor ||
                              current_tool_ == CanvasTool::DeleteAnchor ||
                              current_tool_ == CanvasTool::ConvertPoint;
  const auto count = per_point_tool && canvas_ != nullptr
                         ? canvas_->path_edit_selected_anchor_count()
                         : 0;
  if (count >= 2) {
    path_point_count_chip_->setText(tr("%n points selected", nullptr, count));
    path_point_count_chip_->show();
  } else {
    path_point_count_chip_->hide();
  }
}

void MainWindow::refresh_vector_tool_options_visibility() {
  refresh_path_point_count_chip();
  const bool shape_tool = current_tool_ == CanvasTool::Line ||
                          current_tool_ == CanvasTool::Rectangle ||
                          current_tool_ == CanvasTool::Ellipse ||
                          current_tool_ == CanvasTool::Pen ||
                          current_tool_ == CanvasTool::Polygon ||
                          current_tool_ == CanvasTool::CustomShape;
  const bool select_tool = current_tool_ == CanvasTool::PathSelect ||
                           current_tool_ == CanvasTool::DirectSelect;
  // Visibility itself is decided per widget in refresh_options_bar through
  // vector_option_widget_visible (one final setVisible per widget); this pass
  // only syncs control state.
  if (select_tool) {
    // The appearance controls live-edit the selected shape layer; they only
    // show while one is editable (the Combine combo keeps its per-tool
    // visibility for plain path selections).
    if (editable_active_vector_shape_layer() != nullptr) {
      sync_shape_appearance_options_from_active_layer();
    }
    sync_vector_shape_size_spins();
    update_vector_swatch_icons();
    return;
  }
  if (current_tool_ == CanvasTool::Move) {
    sync_vector_shape_size_spins();
    return;
  }
  if (!shape_tool) {
    return;  // per-tool visibility already hid every mode-specific widget
  }
  // Pen/Polygon/Custom Shape never rasterize: a persisted Pixels mode behaves
  // as Path for them, so the combo greys the Pixels entry out and displays the
  // effective mode instead - without rewriting the setting the raster-capable
  // Line/Rect/Ellipse tools still use.
  const bool vector_only_tool = current_tool_ == CanvasTool::Pen ||
                                current_tool_ == CanvasTool::Polygon ||
                                current_tool_ == CanvasTool::CustomShape;
  const auto effective_mode =
      vector_only_tool && current_vector_tool_mode_ == VectorToolMode::Pixels
          ? VectorToolMode::Path
          : current_vector_tool_mode_;
  if (vector_mode_combo_ != nullptr) {
    if (auto* model = qobject_cast<QStandardItemModel*>(vector_mode_combo_->model());
        model != nullptr && model->item(2) != nullptr) {
      model->item(2)->setEnabled(!vector_only_tool);
    }
    const int display_index = effective_mode == VectorToolMode::Path     ? 1
                              : effective_mode == VectorToolMode::Pixels ? 2
                                                                         : 0;
    if (vector_mode_combo_->currentIndex() != display_index) {
      QSignalBlocker blocker(vector_mode_combo_);
      vector_mode_combo_->setCurrentIndex(display_index);
    }
  }
  const bool shape_mode = effective_mode == VectorToolMode::Shape;
  if (shape_mode) {
    // Photoshop-style: with a shape layer selected the bar reflects (and
    // edits) that layer, and its appearance sticks as the next-shape default.
    sync_shape_appearance_options_from_active_layer();
  }
  sync_vector_shape_size_spins();
  update_vector_swatch_icons();
}

std::vector<LayerId> MainWindow::editable_selected_shape_layer_ids() const {
  if (!has_active_document()) return {};
  auto ids = selected_or_active_layer_ids();
  const auto active = document().active_layer_id();
  if (active) {
    const auto found = std::find(ids.begin(), ids.end(), *active);
    if (found != ids.end()) std::rotate(ids.begin(), found, std::next(found));
  }
  std::erase_if(ids, [this](LayerId id) {
    const auto* layer = std::as_const(document()).find_layer(id);
    return !layer || !layer_is_vector_shape(*layer) || !vector_lock_reason(*layer).empty() ||
           layer_id_locks_image_pixels(id);
  });
  return ids;
}

bool MainWindow::edit_active_shape_appearance(bool record_undo,
    std::function<std::optional<ShapeAppearanceSettings>(const ShapeAppearanceSettings&,
        std::function<void(const ShapeAppearanceSettings&)>)> editor) {
  if (preview_dialog_edit_locked()) { show_preview_dialog_edit_lock_message(); return false; }
  if (refuse_layer_dialog_during_transform() || canvas_ == nullptr) return false;
  finish_pending_shape_appearance_edit();
  finish_pending_layer_opacity_edit();
  finish_pending_layer_fill_opacity_edit();
  finish_pending_layer_blend_edit();
  auto& doc = document();
  auto ids = editable_selected_shape_layer_ids();
  // Creation already owns its outer transaction and edits only its staged layer.
  if (!record_undo && doc.active_layer_id()) ids = {*doc.active_layer_id()};
  if (ids.empty()) {
    show_status_error(tr("Select an unlocked, editable shape layer"));
    return false;
  }
  const auto selected_ids = selected_layer_ids();
  const auto active_id = doc.active_layer_id();
  const auto original_patterns = doc.metadata().patterns;
  auto originals = std::make_shared<std::vector<Layer>>();
  AppearanceDialogContext<ShapeAppearanceSettings> context;
  context.selected_count = record_undo ? selected_or_active_layer_ids().size() : 1;
  context.skipped_reason = tr("Locked layers and layers without editable shapes are skipped.");
  for (const auto id : ids) {
    const auto* layer = std::as_const(doc).find_layer(id);
    if (!layer || !layer->vector_shape()) continue;
    originals->push_back(*layer);
    context.originals.push_back(shape_appearance_settings(*layer));
    context.names.push_back(QString::fromStdString(layer->name()));
  }
  if (originals->empty()) return false;
  const QPointer<CanvasWidget> target_canvas(canvas_);
  auto latest = std::make_shared<std::vector<Layer>>();
  auto render_failed = std::make_shared<bool>(false);
  auto preview_state = std::make_shared<AsyncPixelPreviewState<ShapeAppearanceSettings>>();
  auto request_serial = std::make_shared<std::atomic<std::uint64_t>>(0);

  const auto restore = [this, originals, original_patterns, target_canvas] {
    if (!target_canvas || canvas_ != target_canvas) return;
    document().metadata().patterns = original_patterns;
    for (const auto& original : *originals)
      if (auto* target = document().find_layer(original.id())) *target = original;
    canvas_->document_changed();
    refresh_layer_thumbnails();
  };
  const auto prepare = [this, originals, original_patterns](const ShapeAppearanceSettings& settings) {
    auto result = std::make_shared<std::vector<Layer>>(*originals);
    // Preserve collision aliases made by a previous picker preview.
    const auto available = document().metadata().patterns;
    document().metadata().patterns = original_patterns;
    for (auto& layer : *result) {
      const auto baseline = shape_appearance_settings(std::as_const(layer));
      auto next = apply_shape_appearance_edits(baseline, settings);
      const auto* original_content = std::as_const(layer).vector_shape();
      auto content = assemble_shape_appearance(*original_content, next);
      ensure_vector_fill_patterns(document(), content, pattern_library(), &available);
      if (!shape_vector_appearance_equal(*original_content, content)) layer.set_vector_shape(std::move(content));
      if (next.layer_opacity != baseline.layer_opacity) layer.set_opacity(next.layer_opacity);
      if (next.fill_opacity != baseline.fill_opacity) layer.set_fill_opacity(next.fill_opacity);
    }
    return result;
  };
  const auto render = [originals, request_serial](std::vector<Layer>& layers, Rect bounds, const PatternStore& patterns,
                                                 std::uint64_t serial) {
    for (std::size_t index = 0; index < layers.size(); ++index) {
      if (request_serial->load() != serial) return false;
      if (!shape_vector_appearance_equal(*(*originals)[index].vector_shape(), *std::as_const(layers[index]).vector_shape()))
        update_vector_shape_raster(layers[index], bounds, &patterns);
    }
    return true;
  };
  preview_state->start = [this, preview_state, latest, render_failed, prepare, render, target_canvas, request_serial](
                             const ShapeAppearanceSettings& settings) {
    auto result = prepare(settings);
    const auto patterns = std::make_shared<PatternStore>(document().metadata().patterns);
    const auto bounds = Rect::from_size(document().width(), document().height());
    const auto generation = ++preview_state->generation;
    const auto serial = request_serial->load();
    preview_state->in_flight = true;
    *render_failed = false;
    for (const auto& layer : *result)
      if (auto* target = document().find_layer(layer.id())) *target = layer;
    canvas_->begin_processing_operation(tr("Updating shapes..."));
    const QPointer<MainWindow> window(this);
    auto* app = QCoreApplication::instance();
    run_tracked_background_worker([app, window, preview_state, latest, render_failed, result,
                                   patterns, bounds, generation, render, target_canvas, serial] {
      bool failed = false;
      try { failed = !render(*result, bounds, *patterns, serial); } catch (...) { failed = true; }
      if (!app) return;
      QMetaObject::invokeMethod(app, [window, preview_state, latest, render_failed, result,
                                     generation, failed, target_canvas] {
        preview_state->in_flight = false;
        if (target_canvas) target_canvas->end_processing_operation();
        if (window && target_canvas && window->canvas_ == target_canvas &&
            !preview_state->closed && !preview_state->pending &&
            generation == preview_state->generation) {
          *render_failed = failed;
          if (!failed) {
            *latest = *result;
            for (const auto& layer : *result)
              if (auto* target = window->document().find_layer(layer.id())) *target = layer;
            window->canvas_->document_changed();
            window->refresh_layer_thumbnails();
          }
        }
        if (!preview_state->closed && preview_state->pending && preview_state->start) {
          auto next = std::move(*preview_state->pending);
          preview_state->pending.reset();
          preview_state->start(next);
        }
      }, Qt::QueuedConnection);
    });
  };
  auto edit_lock = lock_preview_dialog_edits();
  auto cleanup = qScopeGuard([preview_state, restore, request_serial] {
    ++*request_serial;
    close_async_pixel_preview(preview_state);
    restore();
  });
  const auto foreground = canvas_->primary_color();
  const auto background = canvas_->secondary_color();
  ShapeAppearanceSettings defaults;
  defaults.fill.kind = VectorFillKind::Solid;
  defaults.fill.color = {static_cast<std::uint8_t>(foreground.red()),
                         static_cast<std::uint8_t>(foreground.green()),
                         static_cast<std::uint8_t>(foreground.blue())};
  defaults.stroke.enabled = false;
  defaults.stroke.width = 3.0;
  defaults.stroke.alignment = VectorStrokeAlignment::Inside;
  const auto preview_changed = [preview_state, restore, request_serial](const ShapeAppearanceSettings& value) {
    ++*request_serial;
    if (!value.preview_enabled) restore();
    enqueue_async_pixel_preview(preview_state, value);
  };
  const auto accepted = editor ? editor(context.originals.front(), preview_changed) : request_shape_appearance_settings(
      this, preview_changed, context.originals.front(), defaults, &gradient_library(), &pattern_library(),
      &doc.metadata().patterns, defaults.fill.color,
      {static_cast<std::uint8_t>(background.red()), static_cast<std::uint8_t>(background.green()),
       static_cast<std::uint8_t>(background.blue())}, dialog_field_units(), &context);

  if (accepted) {
    QElapsedTimer drain;
    drain.start();
    while ((preview_state->in_flight || preview_state->pending) && drain.elapsed() < 60000)
      QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  }
  const bool complete = !preview_state->in_flight && !preview_state->pending &&
                        !latest->empty() && !*render_failed;
  ++*request_serial;
  close_async_pixel_preview(preview_state);
  if (accepted && !complete && accepted->edits && !accepted->edits->operations.empty()) {
    latest = prepare(*accepted);
    render(*latest, Rect::from_size(doc.width(), doc.height()), doc.metadata().patterns, request_serial->load());
  }
  auto final_patterns = doc.metadata().patterns;
  restore();
  if (!accepted) {
    cleanup.dismiss();
    edit_lock.release();
    statusBar()->showMessage(tr("Cancelled shape appearance"));
    refresh_layer_controls();
    return false;
  }
  bool changed = false;
  for (std::size_t i = 0; i < latest->size(); ++i) {
    const auto& before = (*originals)[i];
    const auto& after = (*latest)[i];
    changed = changed || !shape_appearance_equal(shape_appearance_settings(before), shape_appearance_settings(after));
    const auto* a = before.vector_shape();
    const auto* b = after.vector_shape();
    for (std::size_t part = 0; part < a->parts.size(); ++part)
      changed = changed || a->parts[part].fill != b->parts[part].fill ||
                a->parts[part].stroke != b->parts[part].stroke;
  }
  if (changed) {
    // Prepare native-source invalidation before recording the completed edit.
    for (std::size_t i = 0; i < latest->size(); ++i) {
      auto& layer = (*latest)[i];
      const auto& before = (*originals)[i];
      const auto* a = before.vector_shape();
      const auto* b = std::as_const(layer).vector_shape();
      const bool vector_changed = !shape_vector_appearance_equal(*a, *b);
      if (!vector_changed && before.opacity() == layer.opacity() && before.fill_opacity() == layer.fill_opacity()) continue;
      if (vector_changed) {
        mark_layer_vector_block_dirty(layer);
        layer.metadata()[kLayerMetadataVectorRasterStatus] = kVectorRasterStatusPatchy;
      }
    }
    if (record_undo) push_undo_snapshot(tr("Shape appearance"));
    doc.metadata().patterns = std::move(final_patterns);
    for (auto& layer : *latest)
      if (auto* target = doc.find_layer(layer.id())) *target = std::move(layer);
    canvas_->document_changed();
  }
  cleanup.dismiss();
  edit_lock.release();
  refresh_layer_list();
  select_layers_in_layer_list(selected_ids, active_id.value_or(ids.front()));
  refresh_layer_controls();
  refresh_options_bar();
  refresh_paths_panel();
  return true;
}

Layer MainWindow::build_fill_layer(const VectorFill& fill, const QString& name) {
  auto& doc = document();
  Layer layer(doc.allocate_layer_id(), name.toStdString(), PixelBuffer());
  VectorShapeContent content;  // empty path = the whole canvas
  // Photoshop's "current path" rule: a targeted Paths-panel row clips the new
  // fill layer to that path (it becomes the layer's shape path); with no row
  // targeted the fill spans the canvas.
  if (canvas_ != nullptr && canvas_->panel_path_targeted()) {
    if (const auto* path = resolved_panel_path(); path != nullptr && !path->empty()) {
      content.path = *path;
    }
  }
  content.fill = fill;
  content.stroke.enabled = false;
  ensure_vector_fill_patterns(doc, content, pattern_library());
  layer.set_vector_shape(std::move(content));
  layer.metadata()[kLayerMetadataVectorShape] = "1";
  layer.metadata()[kLayerMetadataVectorRasterStatus] = kVectorRasterStatusPatchy;
  update_vector_shape_raster(layer, Rect::from_size(doc.width(), doc.height()),
                             &doc.metadata().patterns);
  const auto selection = canvas_->selected_document_region();
  const auto selection_rect =
      selection.boundingRect().intersected(QRect(0, 0, doc.width(), doc.height()));
  if (!selection.isEmpty() && !selection_rect.isEmpty()) {
    layer.set_mask(LayerMask{to_core_rect(selection_rect),
                             coverage_at_document_depth(doc, selection_mask_pixels(*canvas_, selection_rect)), 0,
                             false});
  }
  return layer;
}

QString MainWindow::unique_fill_layer_name(const QString& base) {
  std::set<std::string> existing_names;
  collect_shape_layer_names(document().layers(), existing_names);
  int suffix = 1;
  std::string name;
  do {
    name = base.arg(suffix++).toStdString();
  } while (existing_names.contains(name));
  return QString::fromStdString(name);
}

void MainWindow::create_fill_layer(const VectorFill& fill, const QString& name, QString label) {
  auto& doc = document();
  auto anchor_id = doc.active_layer_id();
  if (const auto selected_ids = selected_layer_ids(); !selected_ids.empty()) {
    anchor_id = selected_ids.front();
  }
  push_undo_snapshot(std::move(label));
  auto layer = build_fill_layer(fill, name);
  const auto layer_id = layer.id();
  insert_layer_after_anchor(doc, std::move(layer), anchor_id);
  doc.set_active_layer(layer_id);
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
  statusBar()->showMessage(tr("Created fill layer %1.").arg(name));
}

void MainWindow::new_solid_color_fill_layer() {
  if (canvas_ == nullptr || !has_active_document()) {
    return;
  }
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return;
  }
  std::optional<LayerId> preview_id;
  const auto restore_active_layer = document().active_layer_id();
  const auto fill_for_color = [](QColor color) {
    VectorFill fill;
    fill.kind = VectorFillKind::Solid;
    fill.color = RgbColor{static_cast<std::uint8_t>(color.red()),
                          static_cast<std::uint8_t>(color.green()),
                          static_cast<std::uint8_t>(color.blue())};
    return fill;
  };
  const auto preview_name = unique_fill_layer_name(tr("Color Fill %1"));
  const auto update_preview = [this, &preview_id, restore_active_layer, fill_for_color,
                               preview_name](QColor color) {
    auto& doc = document();
    if (preview_id.has_value()) {
      if (auto* layer = doc.find_layer(*preview_id);
          layer != nullptr && layer->vector_shape() != nullptr) {
        auto content = *layer->vector_shape();
        content.fill = fill_for_color(color);
        layer->set_vector_shape(std::move(content));
        update_vector_shape_raster(*layer, Rect::from_size(doc.width(), doc.height()),
                                   &doc.metadata().patterns);
        canvas_->document_changed();
        return;
      }
      preview_id.reset();
    }
    auto preview = build_fill_layer(fill_for_color(color), preview_name);
    preview_id = preview.id();
    doc.add_layer(std::move(preview));
    if (restore_active_layer.has_value() && doc.find_layer(*restore_active_layer) != nullptr) {
      doc.set_active_layer(*restore_active_layer);
    }
    canvas_->document_changed();
  };

  auto preview_edit_lock = lock_preview_dialog_edits();
  const auto initial = canvas_->primary_color();
  update_preview(initial);
  const auto chosen =
      request_patchy_color(this, initial, tr("New Fill Layer"),
                           [&update_preview](QColor color) { update_preview(color); });
  if (preview_id.has_value()) {
    auto& doc = document();
    doc.remove_layer(*preview_id);
    preview_id.reset();
    if (restore_active_layer.has_value() && doc.find_layer(*restore_active_layer) != nullptr) {
      doc.set_active_layer(*restore_active_layer);
    }
    canvas_->document_changed();
  }
  preview_edit_lock.release();
  if (!chosen.has_value()) {
    statusBar()->showMessage(tr("Cancelled fill layer"));
    return;
  }
  create_fill_layer(fill_for_color(*chosen), preview_name, tr("New fill layer"));
}

void MainWindow::new_gradient_fill_layer() {
  if (canvas_ == nullptr || !has_active_document()) {
    return;
  }
  const auto foreground = canvas_->primary_color();
  const auto background = canvas_->secondary_color();
  VectorFill fill;
  fill.kind = VectorFillKind::Gradient;
  fill.gradient.type = LayerStyleGradientType::Linear;
  fill.gradient.angle_degrees = 90.0F;
  fill.gradient.color_stops = {
      GradientColorStop{0.0F,
                        RgbColor{static_cast<std::uint8_t>(foreground.red()),
                                 static_cast<std::uint8_t>(foreground.green()),
                                 static_cast<std::uint8_t>(foreground.blue())},
                        0.5F},
      GradientColorStop{1.0F,
                        RgbColor{static_cast<std::uint8_t>(background.red()),
                                 static_cast<std::uint8_t>(background.green()),
                                 static_cast<std::uint8_t>(background.blue())},
                        0.5F}};
  fill.gradient.alpha_stops = {GradientAlphaStop{0.0F, 1.0F, 0.5F},
                               GradientAlphaStop{1.0F, 1.0F, 0.5F}};
  create_fill_layer_with_appearance(fill, unique_fill_layer_name(tr("Gradient Fill %1")));
}

void MainWindow::new_pattern_fill_layer() {
  if (canvas_ == nullptr || !has_active_document()) {
    return;
  }
  VectorFill fill;
  fill.kind = VectorFillKind::Pattern;
  if (const auto& patterns = document().metadata().patterns; !patterns.empty()) {
    fill.pattern_id = patterns.patterns.front().id;
    fill.pattern_name = patterns.patterns.front().name;
  } else if (!pattern_library().entries().empty()) {
    const auto& entry = pattern_library().entries().front();
    fill.pattern_id = entry.id.toStdString();
    fill.pattern_name = entry.name.toStdString();
  } else {
    show_status_error(tr("No patterns are available."));
    return;
  }
  create_fill_layer_with_appearance(fill, unique_fill_layer_name(tr("Pattern Fill %1")));
}

void MainWindow::create_fill_layer_with_appearance(const VectorFill& fill, const QString& name) {
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return;
  }
  auto& doc = document();
  const auto original = doc;
  auto restore = qScopeGuard([&] {
    doc = original;
    canvas_->document_changed();
    refresh_layer_list();
    refresh_layer_controls();
  });
  auto temporary = build_fill_layer(fill, name);
  const auto id = temporary.id();
  doc.add_layer(std::move(temporary));
  doc.set_active_layer(id);
  canvas_->document_changed();
  if (!edit_active_shape_appearance(false)) {
    return;
  }
  auto completed = doc;
  doc = original;
  push_undo_snapshot(tr("New fill layer"));
  doc = std::move(completed);
  restore.dismiss();
  canvas_->document_changed();
  refresh_layer_list();
  refresh_layer_controls();
}

void MainWindow::populate_new_fill_layer_menu(QMenu* menu, const QString& object_name_prefix) {
  if (menu == nullptr) {
    return;
  }
  const auto add_fill = [this, menu, &object_name_prefix](const char* source,
                                                          const QString& object_key, auto callback) {
    auto* action = menu->addAction(tr(source));
    bind_action_text(action, source);
    if (!object_name_prefix.isEmpty()) {
      action->setObjectName(object_name_prefix + object_key + QStringLiteral("Action"));
      register_document_action(action);
    }
    connect(action, &QAction::triggered, this, callback);
    return action;
  };
  add_fill(QT_TR_NOOP("&Solid Color..."), QStringLiteral("SolidColorFill"),
           [this] { new_solid_color_fill_layer(); });
  add_fill(QT_TR_NOOP("&Gradient..."), QStringLiteral("GradientFill"),
           [this] { new_gradient_fill_layer(); });
  add_fill(QT_TR_NOOP("&Pattern..."), QStringLiteral("PatternFill"),
           [this] { new_pattern_fill_layer(); });
}

Layer* MainWindow::vector_mask_command_layer(bool require_mask) {
  if (!has_active_document()) {
    return nullptr;
  }
  auto& doc = document();
  select_only_layer_if_none_active();
  const auto active = doc.active_layer_id();
  auto* layer = active.has_value() ? doc.find_layer(*active) : nullptr;
  if (layer == nullptr) {
    show_status_error(tr("Select a layer to work with vector masks"));
    return nullptr;
  }
  if (!require_mask && layer->vector_shape()) {
    show_status_error(tr("Put shape layers in a group and apply the vector mask to that group."));
    return nullptr;
  }
  if (!vector_lock_reason(*layer).empty()) {
    show_status_error(tr("This layer's vector data is preserved but can't be edited."));
    return nullptr;
  }
  if (require_mask && layer->vector_mask() == nullptr) {
    show_status_error(tr("The active layer has no vector mask"));
    return nullptr;
  }
  if (!require_mask && layer->vector_mask() != nullptr) {
    show_status_error(tr("The active layer already has a vector mask"));
    return nullptr;
  }
  return layer;
}

void MainWindow::add_vector_mask(bool hide_all, bool from_work_path) {
  auto* layer = vector_mask_command_layer(false);
  if (layer == nullptr) {
    return;
  }
  auto& doc = document();
  LayerVectorMask mask;
  if (from_work_path) {
    const auto* work = doc.work_path();
    if (work == nullptr || work->path().empty()) {
      show_status_error(tr("Draw a work path first"));
      return;
    }
    mask.path = work->path();
  }
  mask.inverted = hide_all;
  push_undo_snapshot(tr("Add vector mask"));
  layer->set_vector_mask(std::move(mask));
  mark_layer_vector_block_dirty(*layer);
  update_vector_mask_raster(*layer, Rect::from_size(doc.width(), doc.height()));
  if (canvas_ != nullptr) {
    canvas_->set_layer_edit_target(CanvasWidget::LayerEditTarget::VectorMask);
  }
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
  statusBar()->showMessage(tr("Added a vector mask"));
}

void MainWindow::delete_active_vector_mask() {
  auto* layer = vector_mask_command_layer(true);
  if (layer == nullptr) {
    return;
  }
  push_undo_snapshot(tr("Delete vector mask"));
  layer->clear_vector_mask();
  auto& blocks = layer->unknown_psd_blocks();
  std::erase_if(blocks, [](const UnknownPsdBlock& block) {
    return block.key == "vmsk" || block.key == "vsms";
  });
  mark_layer_vector_block_dirty(*layer);
  if (canvas_ != nullptr &&
      canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::VectorMask) {
    canvas_->set_layer_edit_target(CanvasWidget::LayerEditTarget::Content);
  }
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
  statusBar()->showMessage(tr("Deleted the vector mask"));
}

void MainWindow::set_active_layer_vector_mask_disabled(bool disabled) {
  auto* layer = vector_mask_command_layer(true);
  if (layer == nullptr) {
    return;
  }
  if (layer->vector_mask()->disabled == disabled) {
    return;
  }
  push_undo_snapshot(disabled ? tr("Disable vector mask") : tr("Enable vector mask"));
  auto mask = *layer->vector_mask();
  mask.disabled = disabled;
  layer->set_vector_mask(std::move(mask));
  mark_layer_vector_block_dirty(*layer);
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
  statusBar()->showMessage(disabled ? tr("Disabled the vector mask")
                                    : tr("Enabled the vector mask"));
}

void MainWindow::rasterize_active_vector_mask() {
  auto* layer = vector_mask_command_layer(true);
  if (layer == nullptr) {
    return;
  }
  auto& doc = document();
  push_undo_snapshot(tr("Rasterize vector mask"));
  bake_vector_mask(*layer, doc.width(), doc.height());
  if (canvas_ != nullptr &&
      canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::VectorMask) {
    canvas_->set_layer_edit_target(CanvasWidget::LayerEditTarget::Mask);
  }
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
  statusBar()->showMessage(tr("Rasterized the vector mask into the layer mask"));
}

void MainWindow::populate_vector_mask_menu(QMenu* menu, const QString& object_name_prefix) {
  if (menu == nullptr) {
    return;
  }
  const auto add_command = [this, menu, &object_name_prefix](const char* source,
                                                             const QString& object_key,
                                                             auto callback) {
    auto* action = menu->addAction(tr(source));
    bind_action_text(action, source);
    if (!object_name_prefix.isEmpty()) {
      action->setObjectName(object_name_prefix + object_key + QStringLiteral("Action"));
      register_document_action(action);
    }
    connect(action, &QAction::triggered, this, callback);
    return action;
  };
  add_command(QT_TR_NOOP("&Reveal All"), QStringLiteral("VectorMaskRevealAll"),
              [this] { add_vector_mask(false, false); });
  add_command(QT_TR_NOOP("&Hide All"), QStringLiteral("VectorMaskHideAll"),
              [this] { add_vector_mask(true, false); });
  add_command(QT_TR_NOOP("&Current Path"), QStringLiteral("VectorMaskCurrentPath"),
              [this] { add_vector_mask(false, true); });
  menu->addSeparator();
  add_command(QT_TR_NOOP("&Delete Vector Mask"), QStringLiteral("VectorMaskDelete"),
              [this] { delete_active_vector_mask(); });
  add_command(QT_TR_NOOP("D&isable Vector Mask"), QStringLiteral("VectorMaskDisable"), [this] {
    if (auto* layer = vector_mask_command_layer(true); layer != nullptr) {
      set_active_layer_vector_mask_disabled(!layer->vector_mask()->disabled);
    }
  });
  add_command(QT_TR_NOOP("Ras&terize Vector Mask"), QStringLiteral("VectorMaskRasterize"),
              [this] { rasterize_active_vector_mask(); });
}

CustomShapeLibrary& MainWindow::custom_shape_library() {
  if (custom_shape_library_ == nullptr) {
    custom_shape_library_ = new CustomShapeLibrary({}, this);
    custom_shape_library_->restore_default_shapes();
    connect(custom_shape_library_, &CustomShapeLibrary::changed, this, [this] {
      refresh_custom_shape_combo();
      apply_custom_shape_selection();
    });
  }
  return *custom_shape_library_;
}

void MainWindow::refresh_custom_shape_combo() {
  if (custom_shape_combo_ == nullptr) {
    return;
  }
  const auto previous = custom_shape_combo_->currentData().toString();
  QSignalBlocker blocker(custom_shape_combo_);
  custom_shape_combo_->clear();
  for (const auto& entry : custom_shape_library().entries()) {
    custom_shape_combo_->addItem(QIcon(entry.thumbnail), custom_shape_display_name(entry),
                                 entry.id);
  }
  if (const auto index = custom_shape_combo_->findData(previous); index >= 0) {
    custom_shape_combo_->setCurrentIndex(index);
  }
}

void MainWindow::apply_custom_shape_selection() {
  if (custom_shape_combo_ == nullptr || canvas_ == nullptr) {
    return;
  }
  const auto shape_id = custom_shape_combo_->currentData().toString();
  const auto* entry = custom_shape_library().find_entry_by_shape_id(shape_id);
  canvas_->set_custom_shape_path(entry != nullptr
                                     ? std::make_shared<const VectorPath>(entry->path)
                                     : std::shared_ptr<const VectorPath>{});
}

void MainWindow::define_custom_shape_from_path() {
  QString path_name;
  const auto* path = resolved_panel_path(&path_name);
  if (path == nullptr || path->empty()) {
    // Fall back to the active layer's path / work path without a panel selection.
    if (canvas_ != nullptr) {
      select_only_layer_if_none_active();
      path = canvas_->path_edit_target_path();
    }
  }
  if (path == nullptr || path->empty()) {
    show_status_error(tr("Select a path or shape layer to define a custom shape"));
    return;
  }
  const auto bounds = path->bounds();
  if (!bounds.has_value() || bounds->right - bounds->left < 1e-6 ||
      bounds->bottom - bounds->top < 1e-6) {
    show_status_error(tr("The path is too small to define a shape"));
    return;
  }
  // Normalize into the unit box.
  auto normalized = *path;
  const auto width = bounds->right - bounds->left;
  const auto height = bounds->bottom - bounds->top;
  const auto scale = 1.0 / std::max(width, height);
  transform_vector_path(normalized, {scale, 0.0, 0.0, scale, -bounds->left * scale,
                                     -bounds->top * scale});
  // Name prompt, prefilled with the generated fallback (Photoshop's flow).
  const auto default_name =
      tr("Custom Shape %1").arg(custom_shape_library().entries().size() + 1);
  QDialog dialog(this);
  dialog.setObjectName(QStringLiteral("defineCustomShapeDialog"));
  dialog.setWindowTitle(tr("Define Custom Shape"));
  auto* layout = new QVBoxLayout(&dialog);
  auto* form = new QFormLayout();
  auto* name_edit = new QLineEdit(default_name, &dialog);
  name_edit->setObjectName(QStringLiteral("defineCustomShapeNameEdit"));
  name_edit->selectAll();
  form->addRow(tr("Name:"), name_edit);
  layout->addLayout(form);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget(buttons);
  if (run_non_modal_dialog(dialog) != QDialog::Accepted) {
    statusBar()->showMessage(tr("Cancelled defining a custom shape"));
    return;
  }
  const auto trimmed_name = name_edit->text().trimmed();
  const auto name = trimmed_name.isEmpty() ? default_name : trimmed_name;
  const auto storage_id = custom_shape_library().add_shape(name, normalized);
  if (storage_id.isEmpty()) {
    show_status_error(tr("Could not save the custom shape"));
    return;
  }
  if (custom_shape_combo_ != nullptr) {
    if (const auto* entry = custom_shape_library().find_entry(storage_id); entry != nullptr) {
      if (const auto index = custom_shape_combo_->findData(entry->id); index >= 0) {
        QSignalBlocker blocker(custom_shape_combo_);
        custom_shape_combo_->setCurrentIndex(index);
      }
      apply_custom_shape_selection();
    }
  }
  statusBar()->showMessage(tr("Defined %1 from the path.").arg(name));
}

void MainWindow::define_custom_shape_from_svg_file() {
  const auto path = get_open_file_name(this, tr("Define Custom Shape from SVG File"),
                                       file_dialog_initial_path(QString(), QString()),
                                       tr("SVG Files (*.svg *.svgz);;All Files (*.*)"), nullptr,
                                       QStringLiteral("defineCustomShapeSvgFileDialog"));
  if (path.isEmpty()) {
    return;
  }
  define_custom_shape_from_svg_path(path);
}

bool MainWindow::define_custom_shape_from_svg_path(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    show_status_error(tr("Could not read %1").arg(path));
    return false;
  }
  const auto raw = file.readAll();
  patchy::Document imported;
  try {
    imported = svg::DocumentIo::read(
        std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(raw.constData()),
                                      static_cast<std::size_t>(raw.size())));
  } catch (const std::exception& error) {
    show_status_error(tr("Could not read the SVG: %1").arg(translate_data_text(error.what())));
    return false;
  }
  // The Photoshop Shapes-panel behavior: one stampable shape per file, all
  // drawable geometry merged (paint ignored), combine ops preserved so holes
  // keep cutting when stamped. Shape groups renumber per source layer.
  VectorPath merged;
  std::int32_t group_base = 0;
  const auto collect = [&](auto&& self, const std::vector<Layer>& layers) -> void {
    for (const auto& layer : layers) {
      if (!layer.children().empty()) {
        self(self, layer.children());
      }
      const auto* shape = layer.vector_shape();
      if (shape == nullptr || shape->path.subpaths.empty()) {
        continue;
      }
      std::int32_t highest = 0;
      for (const auto& subpath : shape->path.subpaths) {
        auto copy = subpath;
        highest = std::max(highest, copy.shape_group);
        copy.shape_group += group_base;
        merged.subpaths.push_back(std::move(copy));
      }
      group_base += highest + 1;
    }
  };
  collect(collect, imported.layers());
  const auto bounds = merged.bounds();
  if (!bounds.has_value() || bounds->right - bounds->left < 1e-6 || bounds->bottom - bounds->top < 1e-6) {
    show_status_error(tr("The SVG has no shape geometry to define"));
    return false;
  }
  const auto scale = 1.0 / std::max(bounds->right - bounds->left, bounds->bottom - bounds->top);
  transform_vector_path(merged, {scale, 0.0, 0.0, scale, -bounds->left * scale, -bounds->top * scale});
  const auto name = QFileInfo(path).completeBaseName();
  const auto storage_id = custom_shape_library().add_shape(name.isEmpty() ? tr("SVG Shape") : name, merged);
  if (storage_id.isEmpty()) {
    show_status_error(tr("Could not save the custom shape"));
    return false;
  }
  if (custom_shape_combo_ != nullptr) {
    if (const auto* entry = custom_shape_library().find_entry(storage_id); entry != nullptr) {
      if (const auto index = custom_shape_combo_->findData(entry->id); index >= 0) {
        QSignalBlocker blocker(custom_shape_combo_);
        custom_shape_combo_->setCurrentIndex(index);
      }
      apply_custom_shape_selection();
    }
  }
  statusBar()->showMessage(tr("Defined custom shape %1 from the SVG").arg(name));
  return true;
}

std::optional<PatternResource> MainWindow::resolve_vector_pattern_resource(
    const std::string& pattern_id) {
  if (pattern_id.empty()) {
    return std::nullopt;
  }
  if (has_active_document()) {
    if (const auto* existing = std::as_const(document()).metadata().patterns.find(pattern_id);
        existing != nullptr && !existing->tile.empty()) {
      return *existing;
    }
  }
  if (auto resource = pattern_library().resource(QString::fromStdString(pattern_id));
      resource.has_value() && !resource->tile.empty()) {
    return resource;
  }
  if (auto bundled = bundled_pattern_resource(pattern_id);
      bundled.has_value() && !bundled->tile.empty()) {
    return bundled;
  }
  return std::nullopt;
}

void MainWindow::update_vector_swatch_icons() {
  const auto make_swatch = [this](const VectorFill& paint) {
    QPixmap pixmap(20, 14);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF box(0.5, 0.5, 19.0, 13.0);
    switch (paint.kind) {
      case VectorFillKind::None: {
        // Photoshop's "no paint" swatch: white chip with a red slash.
        painter.setPen(QColor(0, 0, 0, 160));
        painter.setBrush(Qt::white);
        painter.drawRoundedRect(box, 2.0, 2.0);
        painter.setPen(QPen(QColor(214, 44, 44), 2.0));
        painter.drawLine(QPointF(3.0, 11.0), QPointF(17.0, 3.0));
        break;
      }
      case VectorFillKind::Gradient: {
        const auto ramp = gradient_thumbnail(paint.gradient, 20, 14);
        painter.setBrush(QBrush(ramp));
        painter.setPen(QColor(0, 0, 0, 160));
        painter.drawRoundedRect(box, 2.0, 2.0);
        break;
      }
      case VectorFillKind::Pattern: {
        if (const auto resource = resolve_vector_pattern_resource(paint.pattern_id);
            resource.has_value()) {
          const auto tile = pattern_thumbnail(resource->tile, 20);
          painter.setBrush(QBrush(tile));
          painter.setPen(QColor(0, 0, 0, 160));
          painter.drawRoundedRect(box, 2.0, 2.0);
        } else {
          // Unresolvable pattern renders as no paint (the rasterizer's rule).
          painter.setPen(QColor(0, 0, 0, 160));
          painter.setBrush(QColor(190, 190, 190));
          painter.drawRoundedRect(box, 2.0, 2.0);
        }
        break;
      }
      case VectorFillKind::Solid: {
        painter.setPen(QColor(0, 0, 0, 160));
        painter.setBrush(QColor(paint.color.red, paint.color.green, paint.color.blue));
        painter.drawRoundedRect(box, 2.0, 2.0);
        break;
      }
    }
    return QIcon(pixmap);
  };
  if (vector_fill_swatch_button_ != nullptr) {
    vector_fill_swatch_button_->setIcon(make_swatch(current_vector_fill_));
  }
  if (vector_stroke_swatch_button_ != nullptr) {
    vector_stroke_swatch_button_->setIcon(make_swatch(current_vector_stroke_paint_));
  }
}

// --- Options-bar paint pickers and live editing of the selected shape ---

patchy::Layer* MainWindow::editable_active_vector_shape_layer() {
  if (!has_active_document()) {
    return nullptr;
  }
  auto& doc = document();
  const auto active = doc.active_layer_id();
  auto* layer = active.has_value() ? doc.find_layer(*active) : nullptr;
  if (layer == nullptr || !layer_is_vector_shape(*layer) || !vector_lock_reason(*layer).empty()) {
    return nullptr;
  }
  return layer;
}

bool MainWindow::vector_shape_size_controls_live() {
  return selected_or_active_layer_ids().size() == 1 && editable_active_vector_shape_layer() != nullptr;
}

bool MainWindow::vector_appearance_controls_live() const {
  if (current_tool_ == CanvasTool::PathSelect || current_tool_ == CanvasTool::DirectSelect) {
    return true;
  }
  const bool shape_tool = current_tool_ == CanvasTool::Line ||
                          current_tool_ == CanvasTool::Rectangle ||
                          current_tool_ == CanvasTool::Ellipse ||
                          current_tool_ == CanvasTool::Pen ||
                          current_tool_ == CanvasTool::Polygon ||
                          current_tool_ == CanvasTool::CustomShape;
  if (!shape_tool) {
    return false;
  }
  // The appearance controls only show in (effective) Shape mode; the
  // vector-only tools coerce a persisted Pixels mode to Path.
  const bool vector_only_tool = current_tool_ == CanvasTool::Pen ||
                                current_tool_ == CanvasTool::Polygon ||
                                current_tool_ == CanvasTool::CustomShape;
  const auto effective_mode =
      vector_only_tool && current_vector_tool_mode_ == VectorToolMode::Pixels
          ? VectorToolMode::Path
          : current_vector_tool_mode_;
  return effective_mode == VectorToolMode::Shape;
}

void MainWindow::refresh_vector_stroke_controls() {
  const auto ids = editable_selected_shape_layer_ids();
  const bool any_enabled = std::any_of(ids.begin(), ids.end(), [&](LayerId id) {
    const auto* layer = std::as_const(document()).find_layer(id);
    return layer && layer->vector_shape() && layer->vector_shape()->stroke.enabled;
  });
  const bool enabled = has_active_document() && !preview_dialog_edit_locked() &&
                       (current_vector_stroke_enabled_ || any_enabled);
  for (const char* name : {"vectorStrokeSwatchButton", "vectorStrokeWidthLabel", "vectorStrokeWidthSpin"}) {
    if (auto* widget = findChild<QWidget*>(QLatin1String(name)); widget != nullptr) {
      widget->setEnabled(enabled);
    }
  }
}

void MainWindow::sync_shape_appearance_options_from_active_layer() {
  // A pending debounced user edit outranks a passive sync; without this guard
  // a refresh between the spin edit and the apply would revert the mirror and
  // silently drop the edit.
  if (vector_appearance_apply_timer_ != nullptr && vector_appearance_apply_timer_->isActive()) {
    return;
  }
  const auto ids = editable_selected_shape_layer_ids();
  const auto* layer = ids.empty() ? nullptr : std::as_const(document()).find_layer(ids.front());
  if (layer == nullptr) {
    for (const char* name : {"vectorFillSwatchButton", "vectorStrokeSwatchButton", "vectorStrokeCheck",
                             "vectorStrokeWidthSpin", "shapeCornerRadiusSpin"})
      set_appearance_mixed(findChild<QWidget*>(QLatin1String(name)), false);
    if (auto* spin = findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"))) {
      const QSignalBlocker blocker(spin);
      spin->setValue(current_shape_corner_radius_);
    }
    return;
  }
  const auto* content = std::as_const(*layer).vector_shape();
  if (content == nullptr) {
    return;
  }
  current_vector_fill_ = content->fill;
  current_vector_stroke_enabled_ = content->stroke.enabled;
  current_vector_stroke_width_ = std::clamp(content->stroke.width, 0.1, 1000.0);
  current_vector_stroke_paint_ = content->stroke.content;
  refresh_vector_stroke_controls();
  if (auto* stroke_check = findChild<QCheckBox*>(QStringLiteral("vectorStrokeCheck"));
      stroke_check != nullptr) {
    QSignalBlocker blocker(stroke_check);
    stroke_check->setChecked(current_vector_stroke_enabled_);
  }
  if (auto* stroke_width = findChild<QDoubleSpinBox*>(QStringLiteral("vectorStrokeWidthSpin"));
      stroke_width != nullptr) {
    QSignalBlocker blocker(stroke_width);
    stroke_width->setValue(current_vector_stroke_width_);
  }
  const auto reference = shape_appearance_settings(*layer);
  const auto mixed = [&](const auto& read) {
    return std::any_of(ids.begin(), ids.end(), [&](LayerId id) {
      const auto* target = std::as_const(document()).find_layer(id);
      return target && read(shape_appearance_settings(*target)) != read(reference);
    });
  };
  set_appearance_mixed(vector_fill_swatch_button_, mixed([](const auto& value) { return value.fill; }));
  set_appearance_mixed(vector_stroke_swatch_button_, mixed([](const auto& value) { return value.stroke.content; }));
  set_appearance_mixed(findChild<QWidget*>(QStringLiteral("vectorStrokeCheck")),
                       mixed([](const auto& value) { return value.stroke.enabled; }));
  set_appearance_mixed(findChild<QWidget*>(QStringLiteral("vectorStrokeWidthSpin")),
                       mixed([](const auto& value) { return value.stroke.width; }));
  if (auto* spin = findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"))) {
    std::optional<double> radius;
    bool different = false;
    for (const auto id : ids) {
      const auto value = shape_appearance_settings(*std::as_const(document()).find_layer(id));
      if (!appearance_has_editable_radii(value)) continue;
      if (!radius) radius = value.geometry->corner_radii.front();
      for (const auto corner : value.geometry->corner_radii) different |= corner != *radius;
    }
    QSignalBlocker blocker(spin);
    spin->setValue(radius ? static_cast<int>(std::lround(*radius)) : current_shape_corner_radius_);
    set_appearance_mixed(spin, different);
  }
  update_vector_swatch_icons();
}

bool MainWindow::commit_shape_appearance_edit(const std::vector<LayerId>& ids,
                                              const ShapeAppearanceSettings& settings) {
  if (!has_active_document() || canvas_ == nullptr) return false;
  auto& doc = document();
  const auto patterns = doc.metadata().patterns;
  auto restore_patterns = qScopeGuard([&] { doc.metadata().patterns = patterns; });
  std::vector<Layer> changed;
  for (const auto id : ids) {
    const auto* original = std::as_const(doc).find_layer(id);
    if (!original || !original->vector_shape() || !vector_lock_reason(*original).empty() ||
        layer_id_locks_image_pixels(id)) continue;
    const auto before = shape_appearance_settings(*original);
    const auto after = apply_shape_appearance_edits(before, settings);
    auto content = assemble_shape_appearance(*original->vector_shape(), after);
    if (shape_appearance_equal(before, after) &&
        shape_vector_appearance_equal(*original->vector_shape(), content)) continue;
    Layer layer = *original;
    if (!shape_vector_appearance_equal(*original->vector_shape(), content)) {
      ensure_vector_fill_patterns(doc, content, pattern_library());
      layer.set_vector_shape(std::move(content));
      update_vector_shape_raster(layer, Rect::from_size(doc.width(), doc.height()), &doc.metadata().patterns);
      mark_layer_vector_block_dirty(layer);
      layer.metadata()[kLayerMetadataVectorRasterStatus] = kVectorRasterStatusPatchy;
    }
    if (after.layer_opacity != before.layer_opacity) layer.set_opacity(after.layer_opacity);
    if (after.fill_opacity != before.fill_opacity) layer.set_fill_opacity(after.fill_opacity);
    changed.push_back(std::move(layer));
  }
  const auto final_patterns = doc.metadata().patterns;
  doc.metadata().patterns = patterns;
  if (changed.empty()) return false;
  push_undo_snapshot(tr("Shape appearance"));
  doc.metadata().patterns = final_patterns;
  for (auto& layer : changed) {
    const auto id = layer.id();
    if (auto* target = doc.find_layer(id)) *target = std::move(layer);
  }
  restore_patterns.dismiss();
  canvas_->document_changed();
  refresh_layer_thumbnails();
  refresh_paths_panel();
  return true;
}

bool MainWindow::apply_options_bar_appearance_to_active_shape(const std::vector<std::string>& fields) {
  finish_pending_shape_appearance_edit();
  if (!vector_appearance_controls_live()) return false;
  return commit_options_bar_appearance_fields(fields);
}

ShapeAppearanceSettings MainWindow::options_bar_appearance_settings(const std::vector<std::string>& fields) const {
  ShapeAppearanceSettings settings;
  settings.fill = current_vector_fill_;
  settings.stroke.enabled = current_vector_stroke_enabled_;
  settings.stroke.width = current_vector_stroke_width_;
  settings.stroke.content = current_vector_stroke_paint_;
  auto edits = std::make_shared<AppearanceEdits<ShapeAppearanceSettings>>();
  for (const auto& property : shape_appearance_properties())
    if (std::find(fields.begin(), fields.end(), property.key) != fields.end() ||
        (fields.empty() && (property.key == "fill.kind" || property.key == "stroke.content.kind" ||
                            property.key == "stroke.enabled" || property.key == "stroke.width")))
      edits->append(property.capture(settings));
  settings.edits = std::move(edits);
  return settings;
}

bool MainWindow::commit_options_bar_appearance_fields(const std::vector<std::string>& fields) {
  return commit_shape_appearance_edit(editable_selected_shape_layer_ids(), options_bar_appearance_settings(fields));
}

void MainWindow::finish_pending_shape_appearance_edit() {
  if (vector_appearance_apply_timer_) vector_appearance_apply_timer_->stop();
  auto pending = std::move(pending_shape_appearance_edit_);
  pending_shape_appearance_edit_ = {};
  pending_shape_appearance_property_.clear();
  if (pending) pending();
}

void MainWindow::queue_shape_appearance_edit(const ShapeAppearanceSettings& settings) {
  if (!has_active_document() || !vector_appearance_controls_live()) return;
  const auto property = settings.edits && !settings.edits->operations.empty()
      ? settings.edits->operations.front().key : std::string();
  if (pending_shape_appearance_edit_ && property != pending_shape_appearance_property_)
    finish_pending_shape_appearance_edit();
  const auto ids = editable_selected_shape_layer_ids();
  if (ids.empty()) return;
  if (!vector_appearance_apply_timer_) {
    vector_appearance_apply_timer_ = new QTimer(this);
    vector_appearance_apply_timer_->setSingleShot(true);
    vector_appearance_apply_timer_->setInterval(250);
    connect(vector_appearance_apply_timer_, &QTimer::timeout, this,
            [this] { finish_pending_shape_appearance_edit(); });
  }
  const auto session_id = session().session_id;
  pending_shape_appearance_property_ = property;
  pending_shape_appearance_edit_ = [this, session_id, ids, settings] {
    if (has_active_document() && session().session_id == session_id)
      commit_shape_appearance_edit(ids, settings);
  };
  vector_appearance_apply_timer_->start();
}

void MainWindow::schedule_vector_appearance_apply() {
  ShapeAppearanceSettings settings;
  settings.stroke.width = current_vector_stroke_width_;
  auto edits = std::make_shared<AppearanceEdits<ShapeAppearanceSettings>>();
  for (const auto& property : shape_appearance_properties())
    if (property.key == "stroke.width") edits->append(property.capture(settings));
  settings.edits = std::move(edits);
  queue_shape_appearance_edit(settings);
}

void MainWindow::apply_selected_shape_corner_radius(double radius) {
  ShapeAppearanceSettings settings;
  settings.geometry = LiveShapeParams{};
  settings.geometry->kind = LiveShapeKind::RoundedRectangle;
  settings.geometry->corner_radii.fill(radius);
  auto edits = std::make_shared<AppearanceEdits<ShapeAppearanceSettings>>();
  for (const auto& property : shape_appearance_properties())
    if (property.key.starts_with("radius.")) edits->append(property.capture(settings));
  settings.edits = std::move(edits);
  queue_shape_appearance_edit(settings);
}

MainWindow::VectorOptionModeRules MainWindow::vector_option_mode_rules() {
  VectorOptionModeRules rules;
  const bool shape_tool = current_tool_ == CanvasTool::Line ||
                          current_tool_ == CanvasTool::Rectangle ||
                          current_tool_ == CanvasTool::Ellipse ||
                          current_tool_ == CanvasTool::Pen ||
                          current_tool_ == CanvasTool::Polygon ||
                          current_tool_ == CanvasTool::CustomShape;
  rules.select_tool = current_tool_ == CanvasTool::PathSelect ||
                      current_tool_ == CanvasTool::DirectSelect;
  rules.live_shape = !editable_selected_shape_layer_ids().empty();
  // Move exposes only the active shape's W/H readouts. The appearance controls
  // remain scoped to shape and path tools, but size editing follows the selected
  // shape layer just like the Properties panel does.
  rules.refine = shape_tool || rules.select_tool || current_tool_ == CanvasTool::Move;
  if (rules.select_tool || current_tool_ == CanvasTool::Move) {
    return rules;
  }
  // Pen/Polygon/Custom Shape never rasterize: a persisted Pixels mode behaves
  // as Path for them (refresh_vector_tool_options_visibility shows the
  // effective mode without rewriting the setting).
  const bool vector_only_tool = current_tool_ == CanvasTool::Pen ||
                                current_tool_ == CanvasTool::Polygon ||
                                current_tool_ == CanvasTool::CustomShape;
  const auto effective_mode =
      vector_only_tool && current_vector_tool_mode_ == VectorToolMode::Pixels
          ? VectorToolMode::Path
          : current_vector_tool_mode_;
  rules.vector_mode = effective_mode != VectorToolMode::Pixels;
  rules.shape_mode = effective_mode == VectorToolMode::Shape;
  return rules;
}

bool MainWindow::vector_option_widget_visible(const VectorOptionModeRules& rules,
                                              QWidget* widget) const {
  if (!rules.refine || widget == nullptr) {
    return true;
  }
  const auto in = [widget](const std::vector<QWidget*>& widgets) {
    return std::find(widgets.begin(), widgets.end(), widget) != widgets.end();
  };
  if (in(vector_shape_size_option_widgets_)) {
    // The W / H readouts: Shape mode keeps them as a disabled readout before
    // the first shape exists (Path and Pixels modes have no shape layer to
    // size, and Pixels already shows the fixed-size Width / Height row); Move
    // and the path selection tools show them only with an editable shape layer.
    if (rules.select_tool || current_tool_ == CanvasTool::Move) {
      return rules.live_shape;
    }
    return rules.shape_mode;
  }
  if (rules.select_tool) {
    // Appearance controls only while an editable shape layer is active; the
    // Combine combo keeps its per-tool visibility for plain path selections.
    return !in(vector_shape_mode_option_widgets_) || rules.live_shape;
  }
  if (in(vector_pixel_only_option_widgets_) && rules.vector_mode) {
    return false;
  }
  if (in(vector_shape_mode_option_widgets_) && !rules.shape_mode) {
    return false;
  }
  if (in(vector_vector_mode_option_widgets_) && !rules.vector_mode) {
    return false;
  }
  return true;
}

void MainWindow::sync_vector_shape_size_spins() {
  if (vector_shape_width_spin_ == nullptr || vector_shape_height_spin_ == nullptr) {
    return;
  }
  // A pending debounced edit outranks a passive sync (see the appearance sync).
  if (vector_shape_size_apply_timer_ != nullptr && vector_shape_size_apply_timer_->isActive()) {
    return;
  }
  std::optional<VectorPathBounds> bounds;
  if (const auto* layer = editable_active_vector_shape_layer(); layer != nullptr) {
    if (const auto* content = std::as_const(*layer).vector_shape(); content != nullptr) {
      bounds = content->path.bounds();
    }
  }
  const bool live = bounds.has_value() && vector_shape_size_controls_live();
  const double width = live ? bounds->right - bounds->left : 0.0;
  const double height = live ? bounds->bottom - bounds->top : 0.0;
  for (auto* spin : {vector_shape_width_spin_, vector_shape_height_spin_,
                     properties_shape_width_spin_, properties_shape_height_spin_}) {
    if (spin == nullptr) {
      continue;
    }
    QSignalBlocker blocker(spin);
    spin->setValue(spin == vector_shape_width_spin_ || spin == properties_shape_width_spin_ ? width : height);
    spin->setEnabled(live);
  }
  const bool linked = vector_shape_link_size_button_ != nullptr && vector_shape_link_size_button_->isChecked();
  for (auto* link_button : {vector_shape_link_size_button_, properties_shape_link_size_button_}) {
    if (link_button == nullptr) {
      continue;
    }
    QSignalBlocker blocker(link_button);
    link_button->setChecked(linked);
    link_button->setEnabled(live);
  }
  if (vector_appearance_button_ != nullptr) {
    vector_appearance_button_->setEnabled(!editable_selected_shape_layer_ids().empty() && vector_appearance_controls_live());
  }
  if (properties_shape_size_panel_ != nullptr) {
    properties_shape_size_panel_->setVisible(live);
    properties_shape_size_panel_->setEnabled(live);
  }
  vector_shape_size_ratio_ = live && height > 1e-9 ? width / height : 1.0;
}

void MainWindow::handle_vector_shape_size_value_changed(bool width_changed, double value) {
  if (vector_shape_width_spin_ == nullptr || vector_shape_height_spin_ == nullptr) {
    return;
  }
  const bool linked = (vector_shape_link_size_button_ != nullptr && vector_shape_link_size_button_->isChecked()) ||
                      (properties_shape_link_size_button_ != nullptr &&
                       properties_shape_link_size_button_->isChecked());
  double width = width_changed ? value : vector_shape_width_spin_->value();
  double height = width_changed ? vector_shape_height_spin_->value() : value;
  if (linked && vector_shape_size_ratio_ > 0.0) {
    if (width_changed) {
      height = value / vector_shape_size_ratio_;
    } else {
      width = value * vector_shape_size_ratio_;
    }
  }
  const QSignalBlocker width_blocker(vector_shape_width_spin_);
  const QSignalBlocker height_blocker(vector_shape_height_spin_);
  const std::optional<QSignalBlocker> properties_width_blocker =
      properties_shape_width_spin_ != nullptr
          ? std::optional<QSignalBlocker>(std::in_place, properties_shape_width_spin_)
          : std::nullopt;
  const std::optional<QSignalBlocker> properties_height_blocker =
      properties_shape_height_spin_ != nullptr
          ? std::optional<QSignalBlocker>(std::in_place, properties_shape_height_spin_)
          : std::nullopt;
  vector_shape_width_spin_->setValue(width);
  vector_shape_height_spin_->setValue(height);
  if (properties_shape_width_spin_ != nullptr) {
    properties_shape_width_spin_->setValue(width);
  }
  if (properties_shape_height_spin_ != nullptr) {
    properties_shape_height_spin_->setValue(height);
  }
  schedule_vector_shape_size_apply();
}

bool MainWindow::apply_options_bar_size_to_active_shape() {
  if (canvas_ == nullptr || vector_shape_width_spin_ == nullptr ||
      vector_shape_height_spin_ == nullptr || !vector_shape_size_controls_live()) {
    return false;
  }
  auto* layer = editable_active_vector_shape_layer();
  if (layer == nullptr) {
    return false;
  }
  const auto* content = std::as_const(*layer).vector_shape();
  if (content == nullptr) {
    return false;
  }
  const auto bounds = content->path.bounds();
  if (!bounds.has_value()) {
    return false;
  }
  const double old_width = bounds->right - bounds->left;
  const double old_height = bounds->bottom - bounds->top;
  const double new_width = vector_shape_width_spin_->value();
  const double new_height = vector_shape_height_spin_->value();
  if (old_width < 1e-6 || old_height < 1e-6 || new_width < 0.5 || new_height < 0.5) {
    sync_vector_shape_size_spins();  // nothing sensible to scale to; show the truth
    return false;
  }
  if (std::abs(new_width - old_width) < 0.05 && std::abs(new_height - old_height) < 0.05) {
    return false;  // no-op; also keeps stale debounced applies harmless
  }
  const double scale_x = new_width / old_width;
  const double scale_y = new_height / old_height;
  // Top-left anchored axis-aligned scale: transform_layer_vector_data keeps
  // live-shape annotations for exactly this matrix family.
  const std::array<double, 6> matrix{scale_x, 0.0, 0.0, scale_y,
                                     bounds->left - bounds->left * scale_x,
                                     bounds->top - bounds->top * scale_y};
  const auto layer_id = layer->id();
  auto& doc = document();
  push_undo_snapshot(tr("Shape size"));
  auto* target = doc.find_layer(layer_id);
  if (target == nullptr) {
    return false;
  }
  const auto old_effect_rect =
      to_qrect(layer_bounds_with_effects(std::as_const(*target), std::as_const(*target).bounds()));
  transform_layer_vector_data(doc, *target, matrix, Rect::from_size(doc.width(), doc.height()));
  canvas_->document_changed_effect_bounds(old_effect_rect.united(
      to_qrect(layer_bounds_with_effects(std::as_const(*target), std::as_const(*target).bounds()))));
  refresh_layer_thumbnails();
  refresh_paths_panel();
  sync_vector_shape_size_spins();
  return true;
}

void MainWindow::schedule_vector_shape_size_apply() {
  if (!vector_shape_size_controls_live()) {
    return;
  }
  if (vector_shape_size_apply_timer_ == nullptr) {
    vector_shape_size_apply_timer_ = new QTimer(this);
    vector_shape_size_apply_timer_->setSingleShot(true);
    vector_shape_size_apply_timer_->setInterval(250);
    connect(vector_shape_size_apply_timer_, &QTimer::timeout, this,
            [this] { apply_options_bar_size_to_active_shape(); });
  }
  vector_shape_size_apply_timer_->start();
}

void MainWindow::show_vector_paint_menu(bool for_stroke) {
  auto* button = for_stroke ? vector_stroke_swatch_button_ : vector_fill_swatch_button_;
  if (button == nullptr) {
    return;
  }
  const auto& paint = for_stroke ? current_vector_stroke_paint_ : current_vector_fill_;
  QMenu menu(button);
  menu.setObjectName(for_stroke ? QStringLiteral("vectorStrokePaintMenu")
                                : QStringLiteral("vectorFillPaintMenu"));
  const auto add_kind = [&](const QString& label, const char* object_name, VectorFillKind kind,
                            auto callback) {
    auto* action = menu.addAction(label);
    action->setObjectName(QLatin1String(object_name));
    action->setCheckable(true);
    action->setChecked(paint.kind == kind);
    connect(action, &QAction::triggered, this, callback);
    return action;
  };
  if (!for_stroke) {
    add_kind(tr("No Fill"), "vectorFillNoneAction", VectorFillKind::None, [this] {
      current_vector_fill_.kind = VectorFillKind::None;
      update_vector_swatch_icons();
      schedule_save_tool_settings();
      apply_options_bar_appearance_to_active_shape({"fill.kind"});
    });
  }
  add_kind(tr("Solid Color..."), for_stroke ? "vectorStrokeSolidAction" : "vectorFillSolidAction",
           VectorFillKind::Solid, [this, for_stroke] { pick_vector_solid_color(for_stroke); });
  add_kind(tr("Gradient..."), for_stroke ? "vectorStrokeGradientAction" : "vectorFillGradientAction",
           VectorFillKind::Gradient, [this, for_stroke] { pick_vector_gradient(for_stroke); });
  add_kind(tr("Pattern..."), for_stroke ? "vectorStrokePatternAction" : "vectorFillPatternAction",
           VectorFillKind::Pattern, [this, for_stroke] { pick_vector_pattern(for_stroke); });
  menu.exec(button->mapToGlobal(QPoint(0, button->height())));
}

void MainWindow::pick_vector_solid_color(bool for_stroke) {
  finish_pending_shape_appearance_edit();
  if (preview_dialog_edit_locked()) { show_preview_dialog_edit_lock_message(); return; }
  const auto paint = for_stroke ? current_vector_stroke_paint_ : current_vector_fill_;
  const QColor initial(paint.color.red, paint.color.green, paint.color.blue);
  const auto title = for_stroke ? tr("Shape Stroke Color") : tr("Shape Fill Color");
  const auto commit_mirror = [&](QColor color) {
    auto& target = for_stroke ? current_vector_stroke_paint_ : current_vector_fill_;
    target.kind = VectorFillKind::Solid;
    target.color = {static_cast<std::uint8_t>(color.red()), static_cast<std::uint8_t>(color.green()),
                    static_cast<std::uint8_t>(color.blue())};
    update_vector_swatch_icons();
    schedule_save_tool_settings();
  };
  if (!vector_appearance_controls_live() || editable_selected_shape_layer_ids().empty()) {
    if (const auto chosen = request_patchy_color(this, initial, title)) commit_mirror(*chosen);
    return;
  }
  edit_active_shape_appearance(true, [&](const ShapeAppearanceSettings& baseline,
                                        std::function<void(const ShapeAppearanceSettings&)> preview) {
    auto settings = baseline;
    const auto apply = [&](QColor color) {
      auto& target = for_stroke ? settings.stroke.content : settings.fill;
      target.kind = VectorFillKind::Solid;
      target.color = {static_cast<std::uint8_t>(color.red()), static_cast<std::uint8_t>(color.green()),
                      static_cast<std::uint8_t>(color.blue())};
      auto edits = std::make_shared<AppearanceEdits<ShapeAppearanceSettings>>();
      for (const auto& property : shape_appearance_properties())
        if (property.key == (for_stroke ? "stroke.content.color" : "fill.color")) edits->append(property.capture(settings));
      settings.edits = std::move(edits);
      preview(settings);
    };
    const auto chosen = request_patchy_color(this, initial, title, apply);
    if (!chosen) return std::optional<ShapeAppearanceSettings>{};
    // Choosing Solid Color and accepting is an explicit paint-kind selection.
    apply(*chosen);
    commit_mirror(*chosen);
    return std::optional<ShapeAppearanceSettings>{settings};
  });
}

void MainWindow::apply_swatch_color_to_shape_paint(QColor color) {
  if (!vector_appearance_controls_live() || preview_dialog_edit_locked()) return;
  apply_solid_color_to_shape_paint(color, /*debounce=*/false);
}

void MainWindow::apply_picked_color_to_selected_shapes(QColor color) {
  // GitHub issue 67: a pick is a deliberate color choice for whatever is
  // selected, so it reaches the selected shapes from the Eyedropper tool even
  // though no shape controls are live. Alt-picks keep the swatch gate: a brush
  // painter sampling a color with a shape layer selected must not recolor it.
  if (preview_dialog_edit_locked() || editable_selected_shape_layer_ids().empty()) return;
  if (current_tool_ != CanvasTool::Eyedropper && !vector_appearance_controls_live()) return;
  apply_solid_color_to_shape_paint(color, /*debounce=*/false);
}

void MainWindow::apply_foreground_color_to_shape_paint(QColor color) {
  // The Foreground color panel while the shape controls are live (GitHub issue
  // 67): the same rule as a swatch click, debounced because the panel reports
  // every step of a drag through the picker and each commit is an undo step.
  if (!vector_appearance_controls_live() || preview_dialog_edit_locked()) return;
  apply_solid_color_to_shape_paint(color, /*debounce=*/true);
}

void MainWindow::apply_solid_color_to_shape_paint(QColor color, bool debounce) {
  // A Solid fill takes the color, else a Solid stroke behind a None fill. Gradient
  // and pattern fills, and outline-only shapes, are deliberately left alone: the
  // paint KIND is an explicit options-bar choice (Seth, October 2026).
  const bool to_fill = current_vector_fill_.kind == VectorFillKind::Solid;
  const bool to_stroke = !to_fill && current_vector_fill_.kind == VectorFillKind::None &&
                         current_vector_stroke_enabled_ &&
                         current_vector_stroke_paint_.kind == VectorFillKind::Solid;
  if (!to_fill && !to_stroke) return;
  if (!debounce) finish_pending_shape_appearance_edit();
  auto& target = to_fill ? current_vector_fill_ : current_vector_stroke_paint_;
  target.color = {static_cast<std::uint8_t>(color.red()), static_cast<std::uint8_t>(color.green()),
                  static_cast<std::uint8_t>(color.blue())};
  update_vector_swatch_icons();
  schedule_save_tool_settings();
  const std::vector<std::string> fields{to_fill ? "fill.color" : "stroke.content.color"};
  if (debounce) {
    queue_shape_appearance_edit(options_bar_appearance_settings(fields));
  } else {
    commit_options_bar_appearance_fields(fields);
  }
}

void MainWindow::pick_vector_gradient(bool for_stroke) {
  finish_pending_shape_appearance_edit();
  if (preview_dialog_edit_locked()) { show_preview_dialog_edit_lock_message(); return; }
  auto edit_lock = lock_preview_dialog_edits();
  auto& paint = for_stroke ? current_vector_stroke_paint_ : current_vector_fill_;
  auto& stored_id = for_stroke ? current_vector_stroke_gradient_id_ : current_vector_fill_gradient_id_;
  std::optional<GradientDefinition> current;
  if (paint.kind == VectorFillKind::Gradient) {
    current = static_cast<const GradientDefinition&>(paint.gradient);
  }
  const auto selected = request_gradient_manager(this, gradient_library(), stored_id, current);
  if (selected.isEmpty()) {
    return;
  }
  const auto* entry = gradient_library().find_entry(selected);
  if (entry == nullptr) {
    return;
  }
  const auto foreground = canvas_ != nullptr ? canvas_->primary_color() : QColor(Qt::black);
  const auto background = canvas_ != nullptr ? canvas_->secondary_color() : QColor(Qt::white);
  const bool was_gradient = paint.kind == VectorFillKind::Gradient;
  paint.kind = VectorFillKind::Gradient;
  // Presets replace the definition; existing placement (type/angle/scale/
  // reverse) is the user's and stays - fresh gradients start linear at 90.
  static_cast<GradientDefinition&>(paint.gradient) = resolve_gradient_definition(
      entry->definition,
      RgbColor{static_cast<std::uint8_t>(foreground.red()),
               static_cast<std::uint8_t>(foreground.green()),
               static_cast<std::uint8_t>(foreground.blue())},
      RgbColor{static_cast<std::uint8_t>(background.red()),
               static_cast<std::uint8_t>(background.green()),
               static_cast<std::uint8_t>(background.blue())});
  if (!was_gradient) {
    paint.gradient.type = LayerStyleGradientType::Linear;
    paint.gradient.angle_degrees = 90.0F;
    paint.gradient.scale = 1.0F;
    paint.gradient.reverse = false;
  }
  const auto chosen_paint = paint;
  edit_lock.release();
  paint = chosen_paint;
  stored_id = selected;
  update_vector_swatch_icons();
  schedule_save_tool_settings();
  apply_options_bar_appearance_to_active_shape({for_stroke ? "stroke.content.kind" : "fill.kind"});
}

void MainWindow::pick_vector_pattern(bool for_stroke) {
  finish_pending_shape_appearance_edit();
  if (preview_dialog_edit_locked()) { show_preview_dialog_edit_lock_message(); return; }
  if (vector_appearance_controls_live() && !editable_selected_shape_layer_ids().empty()) {
    edit_active_shape_appearance(true, [&](const ShapeAppearanceSettings& baseline,
                                          std::function<void(const ShapeAppearanceSettings&)> preview) {
      auto settings = baseline;
      auto& paint = for_stroke ? settings.stroke.content : settings.fill;
      const auto storage_id = request_pattern_manager(this, pattern_library(), QString::fromStdString(paint.pattern_id));
      auto resource = pattern_library().resource_for_entry(storage_id);
      if (!resource || resource->tile.empty()) return std::optional<ShapeAppearanceSettings>{};
      auto& patterns = document().metadata().patterns;
      if (const auto* embedded = patterns.find(resource->id);
          embedded && !presets::pattern_tiles_equal(embedded->tile, resource->tile)) {
        const auto prior = std::find_if(patterns.patterns.begin(), patterns.patterns.end(), [&](const auto& candidate) {
          return candidate.name == resource->name && presets::pattern_tiles_equal(candidate.tile, resource->tile);
        });
        if (prior != patterns.patterns.end()) resource->id = prior->id;
        else do { resource->id = generate_pattern_uuid(); }
          while (patterns.find(resource->id) || pattern_library().find_entry_by_pattern_id(QString::fromStdString(resource->id)));
      }
      resource->provenance = PatternProvenance::Authored;
      patterns.adopt(*resource);
      if (paint.kind != VectorFillKind::Pattern) {
        paint.pattern_scale = 1.0;
        paint.pattern_angle_degrees = 0.0;
        paint.pattern_linked = true;
        paint.pattern_phase_x = 0.0;
        paint.pattern_phase_y = 0.0;
      }
      paint.kind = VectorFillKind::Pattern;
      paint.pattern_id = resource->id;
      paint.pattern_name = resource->name;
      auto edits = std::make_shared<AppearanceEdits<ShapeAppearanceSettings>>();
      for (const auto& property : shape_appearance_properties())
        if (property.key == (for_stroke ? "stroke.content.kind" : "fill.kind")) edits->append(property.capture(settings));
      settings.edits = std::move(edits);
      preview(settings);
      (for_stroke ? current_vector_stroke_paint_ : current_vector_fill_) = paint;
      schedule_save_tool_settings();
      return std::optional<ShapeAppearanceSettings>{settings};
    });
    return;
  }
  auto edit_lock = lock_preview_dialog_edits();
  auto& paint = for_stroke ? current_vector_stroke_paint_ : current_vector_fill_;
  const auto storage_id = request_pattern_manager(this, pattern_library(),
                                                  QString::fromStdString(paint.pattern_id));
  if (storage_id.isEmpty()) {
    return;
  }
  const auto resource = pattern_library().resource_for_entry(storage_id);
  if (!resource.has_value() || resource->tile.empty()) {
    return;
  }
  const bool was_pattern = paint.kind == VectorFillKind::Pattern;
  paint.kind = VectorFillKind::Pattern;
  paint.pattern_id = resource->id;
  paint.pattern_name = resource->name;
  if (!was_pattern) {
    // Placement params reset only on a fresh switch to Pattern; re-picking a
    // tile keeps the layer's tuned scale/angle/offset (the appearance dialog
    // owns detailed placement).
    paint.pattern_scale = 1.0;
    paint.pattern_angle_degrees = 0.0;
    paint.pattern_linked = true;
    paint.pattern_phase_x = 0.0;
    paint.pattern_phase_y = 0.0;
  }
  const auto chosen_paint = paint;
  edit_lock.release();
  paint = chosen_paint;
  update_vector_swatch_icons();
  schedule_save_tool_settings();
  apply_options_bar_appearance_to_active_shape({for_stroke ? "stroke.content.kind" : "fill.kind"});
}

QBrush MainWindow::vector_fill_preview_brush(const patchy::VectorFill& paint) const {
  switch (paint.kind) {
    case VectorFillKind::None:
      return QBrush(Qt::NoBrush);
    case VectorFillKind::Solid:
      return QBrush(QColor(paint.color.red, paint.color.green, paint.color.blue));
    case VectorFillKind::Gradient: {
      // Drag-preview approximation (the commit rasterizes exactly): ObjectMode
      // gradients span the painted path's bounds; Reflected/Diamond fall back
      // to linear, easing/dither are skipped, and alpha stops apply at the
      // color-stop locations.
      const auto& gradient = paint.gradient;
      QGradientStops stops;
      for (const auto& stop : gradient.color_stops) {
        // Color and alpha sample the raw ramp position; reverse only flips
        // where the stop lands visually (matching gradient_position's rule).
        const auto source = std::clamp(stop.location, 0.0F, 1.0F);
        const auto location = gradient.reverse ? 1.0F - source : source;
        QColor color(stop.color.red, stop.color.green, stop.color.blue);
        color.setAlphaF(std::clamp(gradient_stop_opacity(gradient, source, false), 0.0F, 1.0F));
        stops.append({location, color});
      }
      std::sort(stops.begin(), stops.end(),
                [](const QGradientStop& a, const QGradientStop& b) { return a.first < b.first; });
      const auto angle_radians = gradient.angle_degrees * 3.14159265358979323846 / 180.0;
      const auto scale = std::max(0.05F, gradient.scale);
      const QPointF center(0.5, 0.5);
      const QPointF half(std::cos(angle_radians) * 0.5 * scale,
                         -std::sin(angle_radians) * 0.5 * scale);
      if (gradient.type == LayerStyleGradientType::Radial) {
        QRadialGradient radial(center, 0.5 * scale);
        radial.setCoordinateMode(QGradient::ObjectMode);
        radial.setStops(stops);
        return QBrush(radial);
      }
      if (gradient.type == LayerStyleGradientType::Angle) {
        QConicalGradient conical(center, gradient.angle_degrees);
        conical.setCoordinateMode(QGradient::ObjectMode);
        conical.setStops(stops);
        return QBrush(conical);
      }
      QLinearGradient linear(center - half, center + half);
      linear.setCoordinateMode(QGradient::ObjectMode);
      if (gradient.type == LayerStyleGradientType::Reflected) {
        linear.setStart(center);
        linear.setFinalStop(center + half);
        linear.setSpread(QGradient::ReflectSpread);
      }
      linear.setStops(stops);
      return QBrush(linear);
    }
    case VectorFillKind::Pattern: {
      auto* self = const_cast<MainWindow*>(this);
      const auto resource = self->resolve_vector_pattern_resource(paint.pattern_id);
      if (!resource.has_value()) {
        return QBrush(Qt::NoBrush);  // missing tiles render as no paint
      }
      QBrush brush(QPixmap::fromImage(qimage_from_pixel_buffer(resource->tile)));
      // Document-space placement mirroring PatternTileSampler: anchor at the
      // phase (a NEW layer's effects reference point is the origin), rotate,
      // then scale. The sampler's doc-to-tile mapping is R(angle); tile-to-doc
      // is its inverse, which in Qt's y-down rotate() convention is -angle
      // (the pattern appears rotated counterclockwise, the PS dial).
      QTransform placement;
      placement.translate(paint.pattern_phase_x, paint.pattern_phase_y);
      placement.rotate(-paint.pattern_angle_degrees);
      placement.scale(std::max(0.01, paint.pattern_scale), std::max(0.01, paint.pattern_scale));
      brush.setTransform(placement);
      return brush;
    }
  }
  return QBrush(Qt::NoBrush);
}

namespace {

// QSettings keys for the last-used trace options (persisted identifiers).
constexpr const char* kTraceSettingsPrefix = "imageTrace/";

ImageTraceOptions image_trace_options_from_settings() {
  auto settings = app_settings();
  const auto key = [](const char* name) { return QLatin1String(kTraceSettingsPrefix) + QLatin1String(name); };
  ImageTraceOptions options;
  options.mode = static_cast<ImageTraceOptions::Mode>(
      std::clamp(settings.value(key("mode"), static_cast<int>(options.mode)).toInt(), 0, 2));
  options.colors = settings.value(key("colors"), options.colors).toInt();
  options.threshold = settings.value(key("threshold"), options.threshold).toInt();
  options.paths = settings.value(key("paths"), options.paths).toInt();
  options.corners = settings.value(key("corners"), options.corners).toInt();
  options.noise = settings.value(key("noise"), options.noise).toInt();
  options.smoothing = settings.value(key("smoothing"), options.smoothing).toInt();
  options.merge_colors = settings.value(key("mergeColors"), options.merge_colors).toInt();
  options.max_anchors = settings.value(key("maxAnchors"), options.max_anchors).toInt();
  options.method = static_cast<ImageTraceOptions::Method>(
      std::clamp(settings.value(key("method"), static_cast<int>(options.method)).toInt(), 0, 1));
  options.snap_curves_to_lines = settings.value(key("snapCurvesToLines"), options.snap_curves_to_lines).toBool();
  options.ignore_white = settings.value(key("ignoreWhite"), options.ignore_white).toBool();
  return options;
}

void save_image_trace_options(const ImageTraceOptions& options) {
  auto settings = app_settings();
  const auto key = [](const char* name) { return QLatin1String(kTraceSettingsPrefix) + QLatin1String(name); };
  settings.setValue(key("mode"), static_cast<int>(options.mode));
  settings.setValue(key("colors"), options.colors);
  settings.setValue(key("threshold"), options.threshold);
  settings.setValue(key("paths"), options.paths);
  settings.setValue(key("corners"), options.corners);
  settings.setValue(key("noise"), options.noise);
  settings.setValue(key("smoothing"), options.smoothing);
  settings.setValue(key("mergeColors"), options.merge_colors);
  settings.setValue(key("maxAnchors"), options.max_anchors);
  settings.setValue(key("method"), static_cast<int>(options.method));
  settings.setValue(key("snapCurvesToLines"), options.snap_curves_to_lines);
  settings.setValue(key("ignoreWhite"), options.ignore_white);
}

// "Pick colors from the whole layer" for selection traces. Deliberately
// outside ImageTraceOptions (it is a scope switch, not a preset option);
// persisted identifier, never rename.
constexpr const char* kTracePaletteFromLayerKey = "imageTrace/paletteFromLayer";

bool image_trace_palette_from_layer_from_settings() {
  auto settings = app_settings();
  return settings.value(QLatin1String(kTracePaletteFromLayerKey), true).toBool();
}

void save_image_trace_palette_from_layer(bool palette_from_layer) {
  auto settings = app_settings();
  settings.setValue(QLatin1String(kTracePaletteFromLayerKey), palette_from_layer);
}

}  // namespace

void MainWindow::trace_image_to_shapes() {
  if (canvas_ == nullptr || !has_active_document()) {
    return;
  }
  if (canvas_->quick_mask_active()) {
    show_status_error(tr("Tracing is unavailable in Quick Mask mode"));
    return;
  }
  auto& doc = document();
  select_only_layer_if_none_active();
  const auto active = doc.active_layer_id();
  if (!active.has_value()) {
    show_status_error(tr("Select a pixel layer to trace"));
    return;
  }
  auto* layer = doc.find_layer(*active);
  if (layer == nullptr || layer->kind() != LayerKind::Pixel) {
    show_status_error(tr("Select a pixel layer to trace"));
    return;
  }
  if (layer_pixels_are_procedural(*layer)) {
    // Text, shape, and Smart Object layers trace their rendered pixels; the
    // shared prompt rasterizes first (the destructive-edit contract).
    if (!prompt_rasterize_procedural_layer(*active, tr("Trace Image to Shapes"), false)) {
      return;
    }
    layer = doc.find_layer(*active);
    if (layer == nullptr) {
      return;
    }
  }
  const auto& source_pixels = std::as_const(*layer).pixels();
  if (source_pixels.empty()) {
    show_status_error(tr("The layer has no pixels to trace"));
    return;
  }
  // An active selection limits the traced area: pixels outside it become
  // untraced on the input copy, a per-pixel predicate like alpha < 128. The
  // parameters stay global (docs/legal-constraints.md, "Vector tracing").
  // The unmasked layer rides along as the dialog's optional palette source,
  // so the traced colors can match a whole-layer trace.
  const bool inside_selection = canvas_->has_selection();
  auto pixels = std::make_shared<const PixelBuffer>(
      inside_selection ? pixels_limited_to_selection(*canvas_, source_pixels, std::as_const(*layer).bounds())
                       : source_pixels);
  std::shared_ptr<const PixelBuffer> whole_layer_pixels;
  if (inside_selection) {
    whole_layer_pixels = std::make_shared<const PixelBuffer>(source_pixels);
  }
  const auto chosen = request_image_trace(this, pixels, image_trace_options_from_settings(), inside_selection,
                                          whole_layer_pixels, image_trace_palette_from_layer_from_settings());
  if (!chosen.has_value()) {
    return;
  }
  save_image_trace_options(chosen->options);
  if (inside_selection) {
    save_image_trace_palette_from_layer(chosen->palette_from_layer);
  }
  if (chosen->result == nullptr || chosen->result->layers.empty()) {
    show_status_error(tr("Nothing to trace with these settings"));
    return;
  }
  if (const auto group = insert_image_trace_layers(*active, *chosen->result); group.has_value()) {
    statusBar()->showMessage(tr("Traced the layer into %n shape layer(s).", nullptr,
                                static_cast<int>(chosen->result->layers.size())));
  }
}

std::optional<LayerId> MainWindow::insert_image_trace_layers(LayerId source_id, const ImageTraceResult& result) {
  if (canvas_ == nullptr || !has_active_document() || result.layers.empty()) {
    return std::nullopt;
  }
  auto& doc = document();
  auto* source = doc.find_layer(source_id);
  if (source == nullptr) {
    return std::nullopt;
  }
  const auto source_bounds = std::as_const(*source).bounds();

  push_undo_snapshot(tr("Trace image to shapes"));
  source = doc.find_layer(source_id);
  if (source == nullptr) {
    return std::nullopt;
  }
  auto group = build_image_trace_group(doc, result, source_bounds.x, source_bounds.y,
                                       tr("Traced %1").arg(QString::fromStdString(source->name())).toStdString());
  const auto group_id = group.id();
  // The frontmost traced shape becomes active (not the group): the pen and
  // path tools edit the active layer's path, so the trace is editable at once.
  const auto active_id = group.children().empty() ? group_id : group.children().back().id();
  source->set_visible(false);  // before the insert: it may reallocate the siblings
  insert_layer_after_anchor(doc, std::move(group), source_id);
  doc.set_active_layer(active_id);
  refresh_layer_list();
  reveal_layer_in_layer_list(active_id);
  refresh_layer_controls();
  refresh_document_info();
  path_row_hidden_for_layer_.reset();
  refresh_paths_panel();
  canvas_->document_changed();
  return group_id;
}


namespace {

constexpr const char* kSimplifyToleranceKey = "paths/simplifyTolerance";
constexpr const char* kSimplifyCornerKey = "paths/simplifyCornerAngle";
constexpr const char* kSimplifySnapKey = "paths/simplifySnapCurvesToLines";

}  // namespace

void MainWindow::simplify_target_path() {
  if (canvas_ == nullptr || !has_active_document()) {
    return;
  }
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return;
  }
  select_only_layer_if_none_active();
  const auto* target = canvas_->path_edit_target_path();
  if (target == nullptr || target->subpaths.empty()) {
    show_status_error(tr("Select a path or shape layer to simplify"));
    return;
  }
  auto& doc = document();
  Document* const doc_identity = &doc;
  const VectorPath original = *target;
  // Snapshot the owner for cancel, in path_edit_target_path's precedence: the
  // vector-mask target, a targeted Paths-panel row, the active shape layer,
  // else the work path. Whole objects, so a saved path keeps its verbatim PSD
  // record bytes and dirty flag on cancel.
  std::optional<LayerId> snapshot_layer_id;
  std::optional<Layer> snapshot_layer;
  std::optional<DocumentPathId> snapshot_path_id;
  std::optional<DocumentPath> snapshot_path;
  const auto active_layer_id = doc.active_layer_id();
  const auto* active =
      active_layer_id.has_value() ? std::as_const(doc).find_layer(*active_layer_id) : nullptr;
  const auto panel_path = canvas_->active_document_path();
  if (canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::VectorMask && active != nullptr &&
      active->vector_mask() != nullptr) {
    snapshot_layer_id = active_layer_id;
    snapshot_layer = *active;
  } else if (panel_path.has_value() && std::as_const(doc).find_path(*panel_path) != nullptr) {
    snapshot_path_id = panel_path;
    snapshot_path = *std::as_const(doc).find_path(*panel_path);
  } else if (active != nullptr && layer_is_vector_shape(*active) && vector_lock_reason(*active).empty()) {
    snapshot_layer_id = active_layer_id;
    snapshot_layer = *active;
  } else if (const auto* work = doc.work_path(); work != nullptr) {
    snapshot_path_id = work->id();
    snapshot_path = *work;
  } else {
    show_status_error(tr("Select a path or shape layer to simplify"));
    return;
  }
  const auto restore = [this, doc_identity, snapshot_layer_id, snapshot_layer, snapshot_path_id,
                        snapshot_path] {
    // Non-modal: the document may have closed or switched meanwhile.
    if (!has_active_document() || &document() != doc_identity) {
      return false;
    }
    auto& current = document();
    if (snapshot_layer.has_value()) {
      auto* layer = current.find_layer(*snapshot_layer_id);
      if (layer == nullptr) {
        return false;
      }
      *layer = *snapshot_layer;
    }
    if (snapshot_path.has_value()) {
      auto* path = current.find_path(*snapshot_path_id);
      if (path == nullptr) {
        return false;
      }
      *path = *snapshot_path;
    }
    if (canvas_ != nullptr) {
      canvas_->document_changed();
    }
    refresh_layer_thumbnails();
    refresh_paths_panel();
    return true;
  };

  auto settings = app_settings();
  QDialog dialog(this);
  dialog.setObjectName(QStringLiteral("simplifyPathDialog"));
  dialog.setWindowTitle(tr("Simplify Path"));
  auto* layout = new QVBoxLayout(&dialog);
  auto* form = new QFormLayout();
  auto* tolerance = new UnitSpinBox(SpinUnit::Pixels, &dialog);
  tolerance->setObjectName(QStringLiteral("simplifyPathToleranceSpin"));
  tolerance->set_context_provider(document_unit_context_provider(true));
  tolerance->setRange(0.1, 20.0);
  tolerance->setDecimals(1);
  tolerance->setSingleStep(0.5);
  tolerance->setValue(std::clamp(settings.value(QLatin1String(kSimplifyToleranceKey), 1.0).toDouble(), 0.1, 20.0));
  tolerance->setToolTip(tr("Larger values remove more points"));
  form->addRow(tr("Tolerance:"), tolerance);
  auto* corners = new UnitIntSpinBox(SpinUnit::Degrees, &dialog);
  corners->setObjectName(QStringLiteral("simplifyPathCornerSpin"));
  corners->setRange(10, 170);
  corners->setValue(std::clamp(settings.value(QLatin1String(kSimplifyCornerKey), 60).toInt(), 10, 170));
  corners->setToolTip(tr("Bends sharper than this angle stay corners"));
  form->addRow(tr("Corners:"), corners);
  auto* snap = new QCheckBox(tr("Snap curves to lines"), &dialog);
  snap->setObjectName(QStringLiteral("simplifyPathSnapLinesCheck"));
  snap->setChecked(settings.value(QLatin1String(kSimplifySnapKey), false).toBool());
  form->addRow(QString(), snap);
  layout->addLayout(form);
  auto* readout = new QLabel(&dialog);
  readout->setObjectName(QStringLiteral("simplifyPathAnchorsLabel"));
  layout->addWidget(readout);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget(buttons);
  append_themed_style(dialog, dialog_spinbox_button_style());

  // Live preview: the vector model applies to the real target, undo stays
  // un-armed (the snapshot above restores), one entry lands on accept.
  auto preview_lock = lock_preview_dialog_edits();
  canvas_->clear_path_edit_selection();
  const auto current_options = [&] {
    PathSimplifyOptions options;
    options.tolerance = tolerance->value();
    options.corner_angle_degrees = corners->value();
    options.snap_curves_to_lines = snap->isChecked();
    return options;
  };
  const auto apply_preview = [&] {
    const auto result = simplify_vector_path(original, current_options());
    canvas_->replace_path_edit_target(result.path, result.changed_groups);
    readout->setText(tr("Anchors: %1 -> %2")
                         .arg(static_cast<qulonglong>(result.anchors_before))
                         .arg(static_cast<qulonglong>(result.anchors_after)));
    refresh_paths_panel();
    refresh_layer_thumbnails();
  };
  connect(tolerance, &QDoubleSpinBox::valueChanged, &dialog, [&](double) { apply_preview(); });
  connect(corners, &QSpinBox::valueChanged, &dialog, [&](int) { apply_preview(); });
  connect(snap, &QCheckBox::toggled, &dialog, [&](bool) { apply_preview(); });
  apply_preview();
  const auto code = run_non_modal_dialog(dialog);
  const auto final_options = current_options();
  if (!restore()) {
    return;
  }
  if (code != QDialog::Accepted) {
    statusBar()->showMessage(tr("Cancelled simplifying the path"));
    return;
  }
  settings.setValue(QLatin1String(kSimplifyToleranceKey), final_options.tolerance);
  settings.setValue(QLatin1String(kSimplifyCornerKey), static_cast<int>(final_options.corner_angle_degrees));
  settings.setValue(QLatin1String(kSimplifySnapKey), final_options.snap_curves_to_lines);
  push_undo_snapshot(tr("Simplify path"));
  const auto result = simplify_vector_path(original, final_options);
  canvas_->replace_path_edit_target(result.path, result.changed_groups);
  refresh_paths_panel();
  refresh_layer_thumbnails();
  refresh_layer_controls();
  statusBar()->showMessage(tr("Simplified the path: %1 -> %2 anchors")
                               .arg(static_cast<qulonglong>(result.anchors_before))
                               .arg(static_cast<qulonglong>(result.anchors_after)));
}

void MainWindow::refresh_combine_shapes_action_states() {
  if (layer_shape_appearance_action_ != nullptr) {
    layer_shape_appearance_action_->setEnabled(has_active_document() && !preview_dialog_edit_locked() &&
                                               !editable_selected_shape_layer_ids().empty());
  }
  const bool enabled =
      has_active_document() && !preview_dialog_edit_locked() &&
      combine_shape_candidates(std::as_const(document()).layers(), selected_or_active_layer_ids()).refusal ==
          ShapeCombineRefusal::None;
  for (auto* action : layer_combine_actions_) {
    if (action != nullptr) {
      action->setEnabled(enabled);
    }
  }
}

void MainWindow::combine_selected_shape_layers(PathCombineOp op) {
  if (canvas_ == nullptr || !has_active_document()) {
    return;
  }
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return;
  }
  canvas_->finish_free_transform();
  auto& doc = document();
  const auto candidates = combine_shape_candidates(std::as_const(doc).layers(), selected_or_active_layer_ids());
  switch (candidates.refusal) {
    case ShapeCombineRefusal::NeedTwoLayers:
      show_status_error(tr("Select two or more shape layers to combine"));
      return;
    case ShapeCombineRefusal::NotShapeLayer:
      show_status_error(tr("Only editable shape layers can be combined"));
      return;
    case ShapeCombineRefusal::Locked:
      show_status_error(tr("Shape layers are locked"));
      return;
    case ShapeCombineRefusal::EmptyPath:
      show_status_error(tr("Fill layers without a path cannot be combined"));
      return;
    case ShapeCombineRefusal::DifferentParents:
      show_status_error(tr("Shape layers must be in the same folder to combine"));
      return;
    case ShapeCombineRefusal::None:
      break;
  }
  QString label;
  switch (op) {
    case PathCombineOp::Add:
      label = tr("Unite shapes");
      break;
    case PathCombineOp::Subtract:
      label = tr("Subtract front shape");
      break;
    case PathCombineOp::Intersect:
      label = tr("Intersect shapes");
      break;
    case PathCombineOp::Xor:
      label = tr("Exclude overlapping shapes");
      break;
  }
  push_undo_snapshot(label);
  const auto result = combine_shape_layers(document(), candidates.bottom_to_top, op);
  if (!result.has_value()) {
    show_status_error(tr("Only editable shape layers can be combined"));
    return;
  }
  document().set_active_layer(result->layer_id);
  canvas_->clear_path_edit_selection();
  refresh_layer_list();
  refresh_layer_controls();
  refresh_document_info();
  path_row_hidden_for_layer_.reset();
  refresh_paths_panel();
  canvas_->document_changed();
  statusBar()->showMessage(
      tr("Combined %n shape layer(s)", nullptr, static_cast<int>(candidates.bottom_to_top.size())));
}

}  // namespace patchy::ui
