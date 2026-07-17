// Vector shape tool flows: a released Shape/Path-mode drag from the canvas
// becomes a new shape layer, an addition to the active shape layer, or work
// path subpaths. The options-bar swatches and per-mode widget visibility for
// the Line/Rectangle/Ellipse tools live here too (built in
// main_window_actions.cpp, refined by refresh_vector_tool_options_visibility).
#include "ui/main_window.hpp"

#include "core/blend_math.hpp"
#include "core/document_path.hpp"
#include "core/layer_tree.hpp"
#include "core/pattern_resource.hpp"
#include "core/vector_live_shapes.hpp"
#include "core/vector_raster.hpp"
#include "core/vector_shape.hpp"
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

#include <QBrush>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFormLayout>
#include <QIcon>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QSignalBlocker>
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
                                 const PatternLibrary& library) {
  for (const auto* fill : {&content.fill, &content.stroke.content}) {
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
    if (auto resource = library.resource(QString::fromStdString(fill->pattern_id));
        resource.has_value()) {
      resource->provenance = PatternProvenance::Authored;
      doc.metadata().patterns.adopt(*resource);
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
    params.line_weight = std::max(1, current_vector_line_weight_);
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
        auto content = *layer->vector_shape();
        const auto group = content.path.next_shape_group();
        const auto op = combine_op_for_index(current_vector_combine_index_);
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
        refresh_layer_list();
        refresh_layer_controls();
        path_row_hidden_for_layer_.reset();  // a fresh drag re-shows the outline
        refresh_paths_panel();
        canvas_->document_changed();
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
  canvas_->document_changed();
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

void MainWindow::refresh_vector_tool_options_visibility() {
  const bool shape_tool = current_tool_ == CanvasTool::Line ||
                          current_tool_ == CanvasTool::Rectangle ||
                          current_tool_ == CanvasTool::Ellipse ||
                          current_tool_ == CanvasTool::Pen ||
                          current_tool_ == CanvasTool::Polygon ||
                          current_tool_ == CanvasTool::CustomShape;
  const bool select_tool = current_tool_ == CanvasTool::PathSelect ||
                           current_tool_ == CanvasTool::DirectSelect;
  if (select_tool) {
    // The appearance controls live-edit the selected shape layer; they only
    // show while one is editable (the Combine combo keeps its per-tool
    // visibility for plain path selections).
    const bool live = editable_active_vector_shape_layer() != nullptr;
    for (auto* widget : vector_shape_mode_option_widgets_) {
      if (widget != nullptr) {
        widget->setVisible(live);
      }
    }
    if (live) {
      sync_shape_appearance_options_from_active_layer();
    }
    update_vector_swatch_icons();
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
  const bool vector_mode = effective_mode != VectorToolMode::Pixels;
  const bool shape_mode = effective_mode == VectorToolMode::Shape;
  for (auto* widget : vector_pixel_only_option_widgets_) {
    if (widget != nullptr && vector_mode) {
      widget->setVisible(false);
    }
  }
  for (auto* widget : vector_shape_mode_option_widgets_) {
    if (widget != nullptr && !shape_mode) {
      widget->setVisible(false);
    }
  }
  for (auto* widget : vector_vector_mode_option_widgets_) {
    if (widget != nullptr && !vector_mode) {
      widget->setVisible(false);
    }
  }
  if (shape_mode) {
    // Photoshop-style: with a shape layer selected the bar reflects (and
    // edits) that layer, and its appearance sticks as the next-shape default.
    sync_shape_appearance_options_from_active_layer();
  }
  update_vector_swatch_icons();
}

void MainWindow::edit_active_shape_appearance() {
  // The appearance dialog is a preview dialog; never stack one on another.
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return;
  }
  auto& doc = document();
  const auto active = doc.active_layer_id();
  auto* layer = active.has_value() ? doc.find_layer(*active) : nullptr;
  if (layer == nullptr || !layer_is_vector_shape(*layer)) {
    show_status_error(tr("Select a shape layer to edit its appearance"));
    return;
  }
  if (!vector_lock_reason(*layer).empty()) {
    show_status_error(tr("This shape layer's vector data is preserved but can't be edited."));
    return;
  }
  const auto layer_id = *active;
  const Layer original_layer = *layer;
  ShapeAppearanceSettings initial{layer->vector_shape()->fill, layer->vector_shape()->stroke, {}};
  // Geometry is editable for single-live-shape layers whose every subpath
  // belongs to that origination group (the regeneration replaces the whole
  // group; anything else keeps the section hidden).
  {
    const auto& content = *layer->vector_shape();
    if (content.origination.size() == 1 && content.origination[0].raw_descriptor.empty()) {
      const auto kind = content.origination[0].kind;
      const auto group = content.origination[0].index;
      const bool single_group =
          std::all_of(content.path.subpaths.begin(), content.path.subpaths.end(),
                      [group](const PathSubpath& subpath) { return subpath.shape_group == group; });
      if (single_group &&
          (kind == LiveShapeKind::Rectangle || kind == LiveShapeKind::RoundedRectangle ||
           kind == LiveShapeKind::Ellipse || kind == LiveShapeKind::Line)) {
        initial.geometry = content.origination[0];
      }
    }
  }

  const auto assemble_content = [](const ShapeAppearanceSettings& settings,
                                   VectorShapeContent content) {
    content.fill = settings.fill;
    content.stroke = settings.stroke;
    if (settings.geometry.has_value() && content.origination.size() == 1) {
      // Regenerate the live shape from the edited parameters; the shape STAYS
      // live (this is a parameter edit, not a direct path edit).
      auto params = *settings.geometry;
      params.index = content.origination[0].index;
      populate_live_shape_box_corners(params);
      if (params.kind == LiveShapeKind::Line) {
        // The line's bbox is the generated quad's hull.
        VectorPath preview;
        preview.subpaths = generate_live_shape_subpaths(params);
        if (const auto hull = preview.bounds(); hull.has_value()) {
          params.left = hull->left;
          params.top = hull->top;
          params.right = hull->right;
          params.bottom = hull->bottom;
        }
      }
      const auto group = params.index;
      auto op = PathCombineOp::Add;
      for (const auto& subpath : content.path.subpaths) {
        if (subpath.shape_group == group) {
          op = subpath.op;
          break;
        }
      }
      std::erase_if(content.path.subpaths, [group](const PathSubpath& subpath) {
        return subpath.shape_group == group;
      });
      for (auto& subpath : generate_live_shape_subpaths(params)) {
        subpath.shape_group = group;
        subpath.op = op;
        content.path.subpaths.push_back(std::move(subpath));
      }
      content.origination[0] = params;
    }
    return content;
  };
  const auto apply_settings = [this, layer_id,
                               assemble_content](const ShapeAppearanceSettings& settings) {
    auto& target_doc = document();
    auto* target = target_doc.find_layer(layer_id);
    if (target == nullptr || target->vector_shape() == nullptr) {
      return;
    }
    auto content = assemble_content(settings, *target->vector_shape());
    ensure_vector_fill_patterns(target_doc, content, pattern_library());
    target->set_vector_shape(std::move(content));
    target->metadata()[kLayerMetadataVectorRasterStatus] = kVectorRasterStatusPatchy;
    update_vector_shape_raster(*target, Rect::from_size(target_doc.width(), target_doc.height()),
                               &target_doc.metadata().patterns);
    canvas_->document_changed();
    refresh_layer_thumbnails();
  };

  // The preview rasterizes on a background worker (pattern fills at small
  // scales can take seconds) with the canvas processing overlay; requests
  // coalesce while one is in flight. The layer's vector MODEL updates
  // immediately - only the baked pixels lag.
  struct ShapePreviewRequest {
    ShapeAppearanceSettings settings;
  };
  auto preview_state = std::make_shared<AsyncPixelPreviewState<ShapePreviewRequest>>();
  preview_state->start = [this, preview_state, layer_id,
                          assemble_content](const ShapePreviewRequest& request) {
    auto& target_doc = document();
    auto* target = target_doc.find_layer(layer_id);
    if (target == nullptr || target->vector_shape() == nullptr || canvas_ == nullptr) {
      return;
    }
    auto content = assemble_content(request.settings, *target->vector_shape());
    ensure_vector_fill_patterns(target_doc, content, pattern_library());
    target->set_vector_shape(content);
    target->metadata()[kLayerMetadataVectorRasterStatus] = kVectorRasterStatusPatchy;
    const auto canvas_rect = Rect::from_size(target_doc.width(), target_doc.height());
    auto patterns = std::make_shared<const PatternStore>(target_doc.metadata().patterns);
    const auto reference =
        layer_effects_reference_point(*std::as_const(target_doc).find_layer(layer_id));
    auto shared_content = std::make_shared<const VectorShapeContent>(std::move(content));
    preview_state->in_flight = true;
    const auto generation = ++preview_state->generation;
    canvas_->begin_processing_operation(tr("Updating shape..."));
    auto* app = QCoreApplication::instance();
    auto window = QPointer<MainWindow>(this);
    run_tracked_background_worker([app, window, preview_state, generation, layer_id, canvas_rect,
                                   patterns, reference, shared_content] {
      auto result = std::make_shared<ShapeRasterResult>();
      try {
        // The pattern sampler anchors at the layer's effects reference point;
        // a scratch anchor layer carries the snapshot so the live Layer is
        // never touched off-thread.
        Layer anchor(0, "", PixelBuffer());
        set_layer_effects_reference_point(anchor, reference[0], reference[1]);
        *result = rasterize_vector_shape(*shared_content, canvas_rect, patterns.get(), &anchor);
      } catch (const std::exception&) {
        result.reset();
      }
      if (app == nullptr) {
        return;
      }
      QMetaObject::invokeMethod(
          app,
          [window, preview_state, generation, layer_id, result]() mutable {
            preview_state->in_flight = false;
            if (window != nullptr && window->canvas_ != nullptr) {
              window->canvas_->end_processing_operation();
            }
            const auto has_pending = preview_state->pending.has_value();
            if (!preview_state->closed && !has_pending &&
                generation == preview_state->generation && window != nullptr &&
                result != nullptr) {
              if (auto* preview_layer = window->document().find_layer(layer_id);
                  preview_layer != nullptr && preview_layer->vector_shape() != nullptr) {
                preview_layer->set_pixels(std::move(result->pixels));
                preview_layer->set_bounds(result->bounds);
                if (window->canvas_ != nullptr) {
                  window->canvas_->document_changed();
                }
                window->refresh_layer_thumbnails();
              }
            }
            if (!preview_state->closed && preview_state->pending.has_value() &&
                preview_state->start) {
              auto next = *preview_state->pending;
              preview_state->pending.reset();
              preview_state->start(next);
            }
          },
          Qt::QueuedConnection);
    });
  };
  const auto restore_original_layer = [this, layer_id, original_layer] {
    if (auto* target = document().find_layer(layer_id); target != nullptr) {
      *target = original_layer;
      canvas_->document_changed();
      refresh_layer_thumbnails();
    }
  };

  auto preview_edit_lock = lock_preview_dialog_edits();
  const auto foreground = canvas_->primary_color();
  const auto background = canvas_->secondary_color();
  const auto preview_changed = [preview_state](const ShapeAppearanceSettings& settings) {
    enqueue_async_pixel_preview(preview_state, ShapePreviewRequest{settings});
  };
  const auto accepted = request_shape_appearance_settings(
      this, preview_changed, std::move(initial), &gradient_library(), &pattern_library(),
      &doc.metadata().patterns,
      RgbColor{static_cast<std::uint8_t>(foreground.red()),
               static_cast<std::uint8_t>(foreground.green()),
               static_cast<std::uint8_t>(foreground.blue())},
      RgbColor{static_cast<std::uint8_t>(background.red()),
               static_cast<std::uint8_t>(background.green()),
               static_cast<std::uint8_t>(background.blue())});
  // On accept, drain the in-flight preview: its result IS the final raster,
  // so the commit reuses it instead of re-rasterizing (the second freeze).
  if (accepted.has_value()) {
    QElapsedTimer drain;
    drain.start();
    while ((preview_state->in_flight || preview_state->pending.has_value()) &&
           drain.elapsed() < 60000) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
  }
  const bool preview_current = accepted.has_value() && !preview_state->in_flight &&
                               !preview_state->pending.has_value();
  close_async_pixel_preview(preview_state);
  std::optional<Layer> preview_result;
  if (preview_current) {
    if (const auto* target = std::as_const(document()).find_layer(layer_id); target != nullptr) {
      preview_result = *target;
    }
  }
  restore_original_layer();
  preview_edit_lock.release();
  if (!accepted.has_value()) {
    statusBar()->showMessage(tr("Cancelled shape appearance"));
    return;
  }
  push_undo_snapshot(tr("Shape appearance"));
  if (preview_result.has_value()) {
    if (auto* target = document().find_layer(layer_id); target != nullptr) {
      *target = std::move(*preview_result);
      mark_layer_vector_block_dirty(*target);
      canvas_->document_changed();
      refresh_layer_thumbnails();
    }
  } else {
    // The drain timed out (still rendering after 60s): fall back to the
    // synchronous apply so the commit is never stale.
    apply_settings(*accepted);
    if (auto* target = document().find_layer(layer_id); target != nullptr) {
      mark_layer_vector_block_dirty(*target);
    }
  }
  refresh_layer_list();
  refresh_layer_controls();
  statusBar()->showMessage(tr("Updated the shape appearance"));
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
                             selection_mask_pixels(*canvas_, selection_rect), 0, false});
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
  create_fill_layer(fill, unique_fill_layer_name(tr("Gradient Fill %1")),
                    tr("New fill layer"));
  edit_active_shape_appearance();
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
  create_fill_layer(fill, unique_fill_layer_name(tr("Pattern Fill %1")),
                    tr("New fill layer"));
  edit_active_shape_appearance();
}

void MainWindow::populate_new_fill_layer_menu(QMenu* menu, const QString& object_name_prefix) {
  if (menu == nullptr) {
    return;
  }
  const auto add_fill = [this, menu, &object_name_prefix](const QString& label,
                                                          const QString& object_key, auto callback) {
    auto* action = menu->addAction(label);
    if (!object_name_prefix.isEmpty()) {
      action->setObjectName(object_name_prefix + object_key + QStringLiteral("Action"));
      register_document_action(action);
    }
    connect(action, &QAction::triggered, this, callback);
    return action;
  };
  add_fill(tr("&Solid Color..."), QStringLiteral("SolidColorFill"),
           [this] { new_solid_color_fill_layer(); });
  add_fill(tr("&Gradient..."), QStringLiteral("GradientFill"),
           [this] { new_gradient_fill_layer(); });
  add_fill(tr("&Pattern..."), QStringLiteral("PatternFill"),
           [this] { new_pattern_fill_layer(); });
}

namespace {

// Materializes the vector-mask coverage cache onto the full canvas (zero
// outside cache_bounds).
PixelBuffer vector_mask_full_coverage(const LayerVectorMask& mask, int width, int height) {
  PixelBuffer coverage(width, height, PixelFormat::gray8());
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto local_x = x - mask.cache_bounds.x;
      const auto local_y = y - mask.cache_bounds.y;
      std::uint8_t value = 0;
      if (!mask.cache.empty() && local_x >= 0 && local_y >= 0 && local_x < mask.cache.width() &&
          local_y < mask.cache.height()) {
        value = *mask.cache.pixel(local_x, local_y);
      }
      *coverage.pixel(x, y) = value;
    }
  }
  return coverage;
}

}  // namespace

Layer* MainWindow::vector_mask_command_layer(bool require_mask) {
  if (!has_active_document()) {
    return nullptr;
  }
  auto& doc = document();
  const auto active = doc.active_layer_id();
  auto* layer = active.has_value() ? doc.find_layer(*active) : nullptr;
  if (layer == nullptr || layer->kind() == LayerKind::Group) {
    show_status_error(tr("Select a layer to work with vector masks"));
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
  auto coverage = vector_mask_full_coverage(*layer->vector_mask(), doc.width(), doc.height());
  if (layer->vector_mask()->density != 255) {
    // Bake the density the way the compositor applies it.
    const auto density = static_cast<int>(layer->vector_mask()->density);
    for (int y = 0; y < coverage.height(); ++y) {
      for (int x = 0; x < coverage.width(); ++x) {
        auto* value = coverage.pixel(x, y);
        *value = static_cast<std::uint8_t>((*value * density) / 255 + (255 - density));
      }
    }
  }
  if (const auto& existing = std::as_const(*layer).mask(); existing.has_value()) {
    // Both masks multiply in the compositor; the baked result does the same.
    for (int y = 0; y < coverage.height(); ++y) {
      for (int x = 0; x < coverage.width(); ++x) {
        const auto local_x = x - existing->bounds.x;
        const auto local_y = y - existing->bounds.y;
        std::uint8_t raster_value = existing->default_color;
        if (!existing->pixels.empty() && local_x >= 0 && local_y >= 0 &&
            local_x < existing->pixels.width() && local_y < existing->pixels.height()) {
          raster_value = *existing->pixels.pixel(local_x, local_y);
        }
        auto* value = coverage.pixel(x, y);
        *value = static_cast<std::uint8_t>((*value * raster_value) / 255);
      }
    }
  }
  layer->set_mask(LayerMask{Rect::from_size(doc.width(), doc.height()), std::move(coverage), 255,
                            false});
  layer->clear_vector_mask();
  auto& blocks = layer->unknown_psd_blocks();
  std::erase_if(blocks, [](const UnknownPsdBlock& block) {
    return block.key == "vmsk" || block.key == "vsms";
  });
  mark_layer_vector_block_dirty(*layer);
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
  const auto add_command = [this, menu, &object_name_prefix](const QString& label,
                                                             const QString& object_key,
                                                             auto callback) {
    auto* action = menu->addAction(label);
    if (!object_name_prefix.isEmpty()) {
      action->setObjectName(object_name_prefix + object_key + QStringLiteral("Action"));
      register_document_action(action);
    }
    connect(action, &QAction::triggered, this, callback);
    return action;
  };
  add_command(tr("&Reveal All"), QStringLiteral("VectorMaskRevealAll"),
              [this] { add_vector_mask(false, false); });
  add_command(tr("&Hide All"), QStringLiteral("VectorMaskHideAll"),
              [this] { add_vector_mask(true, false); });
  add_command(tr("&Current Path"), QStringLiteral("VectorMaskCurrentPath"),
              [this] { add_vector_mask(false, true); });
  menu->addSeparator();
  add_command(tr("&Delete Vector Mask"), QStringLiteral("VectorMaskDelete"),
              [this] { delete_active_vector_mask(); });
  add_command(tr("D&isable Vector Mask"), QStringLiteral("VectorMaskDisable"), [this] {
    if (auto* layer = vector_mask_command_layer(true); layer != nullptr) {
      set_active_layer_vector_mask_disabled(!layer->vector_mask()->disabled);
    }
  });
  add_command(tr("Ras&terize Vector Mask"), QStringLiteral("VectorMaskRasterize"),
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

void MainWindow::sync_shape_appearance_options_from_active_layer() {
  // A pending debounced user edit outranks a passive sync; without this guard
  // a refresh between the spin edit and the apply would revert the mirror and
  // silently drop the edit.
  if (vector_appearance_apply_timer_ != nullptr && vector_appearance_apply_timer_->isActive()) {
    return;
  }
  auto* layer = editable_active_vector_shape_layer();
  if (layer == nullptr) {
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
  update_vector_swatch_icons();
}

bool MainWindow::apply_options_bar_appearance_to_active_shape() {
  if (canvas_ == nullptr || !vector_appearance_controls_live()) {
    return false;
  }
  auto* layer = editable_active_vector_shape_layer();
  if (layer == nullptr) {
    return false;
  }
  const auto* existing = std::as_const(*layer).vector_shape();
  if (existing == nullptr) {
    return false;
  }
  auto content = *existing;
  content.fill = current_vector_fill_;
  content.stroke.enabled = current_vector_stroke_enabled_;
  content.stroke.width = current_vector_stroke_width_;
  content.stroke.content = current_vector_stroke_paint_;
  if (existing->fill == content.fill && existing->stroke == content.stroke) {
    return false;  // no-op; also keeps stale debounced applies harmless
  }
  const auto layer_id = layer->id();
  auto& doc = document();
  push_undo_snapshot(tr("Shape appearance"));
  auto* target = doc.find_layer(layer_id);
  if (target == nullptr) {
    return false;
  }
  ensure_vector_fill_patterns(doc, content, pattern_library());
  target->set_vector_shape(std::move(content));
  target->metadata()[kLayerMetadataVectorRasterStatus] = kVectorRasterStatusPatchy;
  mark_layer_vector_block_dirty(*target);
  update_vector_shape_raster(*target, Rect::from_size(doc.width(), doc.height()),
                             &doc.metadata().patterns);
  canvas_->document_changed();
  refresh_layer_thumbnails();
  return true;
}

void MainWindow::schedule_vector_appearance_apply() {
  if (!vector_appearance_controls_live() || editable_active_vector_shape_layer() == nullptr) {
    return;
  }
  if (vector_appearance_apply_timer_ == nullptr) {
    vector_appearance_apply_timer_ = new QTimer(this);
    vector_appearance_apply_timer_->setSingleShot(true);
    vector_appearance_apply_timer_->setInterval(250);
    connect(vector_appearance_apply_timer_, &QTimer::timeout, this,
            [this] { apply_options_bar_appearance_to_active_shape(); });
  }
  vector_appearance_apply_timer_->start();
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
      apply_options_bar_appearance_to_active_shape();
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
  auto& paint = for_stroke ? current_vector_stroke_paint_ : current_vector_fill_;
  const QColor initial(paint.color.red, paint.color.green, paint.color.blue);
  const auto title = for_stroke ? tr("Shape Stroke Color") : tr("Shape Fill Color");
  const auto commit_mirror = [this, for_stroke](QColor color) {
    auto& target = for_stroke ? current_vector_stroke_paint_ : current_vector_fill_;
    target.kind = VectorFillKind::Solid;
    target.color = RgbColor{static_cast<std::uint8_t>(color.red()),
                            static_cast<std::uint8_t>(color.green()),
                            static_cast<std::uint8_t>(color.blue())};
    update_vector_swatch_icons();
    schedule_save_tool_settings();
  };
  auto* layer = vector_appearance_controls_live() ? editable_active_vector_shape_layer() : nullptr;
  if (layer == nullptr || canvas_ == nullptr) {
    if (const auto chosen = request_patchy_color(this, initial, title); chosen.has_value()) {
      commit_mirror(*chosen);
    }
    return;
  }
  // Live scrub on the selected shape (the new_solid_color_fill_layer pattern):
  // preview by direct mutation, restore on cancel, commit undoably on accept.
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return;
  }
  auto preview_edit_lock = lock_preview_dialog_edits();
  const auto layer_id = layer->id();
  const Layer original_layer = *layer;
  const auto preview_color = [this, layer_id, for_stroke](QColor color) {
    auto* target = document().find_layer(layer_id);
    if (target == nullptr || target->vector_shape() == nullptr || canvas_ == nullptr) {
      return;
    }
    auto content = *std::as_const(*target).vector_shape();
    auto& target_paint = for_stroke ? content.stroke.content : content.fill;
    target_paint.kind = VectorFillKind::Solid;
    target_paint.color = RgbColor{static_cast<std::uint8_t>(color.red()),
                                  static_cast<std::uint8_t>(color.green()),
                                  static_cast<std::uint8_t>(color.blue())};
    target->set_vector_shape(std::move(content));
    target->metadata()[kLayerMetadataVectorRasterStatus] = kVectorRasterStatusPatchy;
    update_vector_shape_raster(*target, Rect::from_size(document().width(), document().height()),
                               &document().metadata().patterns);
    canvas_->document_changed();
  };
  preview_color(initial);
  const auto chosen = request_patchy_color(this, initial, title, preview_color);
  if (auto* target = document().find_layer(layer_id); target != nullptr) {
    *target = original_layer;
    canvas_->document_changed();
  }
  preview_edit_lock.release();
  refresh_layer_thumbnails();
  if (!chosen.has_value()) {
    statusBar()->showMessage(tr("Cancelled shape appearance"));
    return;
  }
  commit_mirror(*chosen);
  apply_options_bar_appearance_to_active_shape();
}

void MainWindow::pick_vector_gradient(bool for_stroke) {
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
  stored_id = selected;
  update_vector_swatch_icons();
  schedule_save_tool_settings();
  apply_options_bar_appearance_to_active_shape();
}

void MainWindow::pick_vector_pattern(bool for_stroke) {
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
  update_vector_swatch_icons();
  schedule_save_tool_settings();
  apply_options_bar_appearance_to_active_shape();
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

}  // namespace patchy::ui
