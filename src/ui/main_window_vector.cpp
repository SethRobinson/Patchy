// Vector shape tool flows: a released Shape/Path-mode drag from the canvas
// becomes a new shape layer, an addition to the active shape layer, or work
// path subpaths. The options-bar swatches and per-mode widget visibility for
// the Line/Rectangle/Ellipse tools live here too (built in
// main_window_actions.cpp, refined by refresh_vector_tool_options_visibility).
#include "ui/main_window.hpp"

#include "core/document_path.hpp"
#include "core/layer_tree.hpp"
#include "core/pattern_resource.hpp"
#include "core/vector_live_shapes.hpp"
#include "core/vector_raster.hpp"
#include "core/vector_shape.hpp"
#include "ui/color_panel.hpp"
#include "ui/custom_shape_library.hpp"
#include "ui/main_window_shared.hpp"
#include "ui/pattern_library.hpp"
#include "ui/photo_pattern_presets.hpp"
#include "ui/qt_geometry.hpp"
#include "ui/shape_appearance_dialog.hpp"

#include <QComboBox>
#include <QIcon>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QStatusBar>
#include <QToolButton>

#include <algorithm>
#include <cmath>
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
  layer.set_vector_shape(std::move(content));
  layer.metadata()[kLayerMetadataVectorShape] = "1";
  layer.metadata()[kLayerMetadataVectorRasterStatus] = kVectorRasterStatusPatchy;
  update_vector_shape_raster(layer, canvas_rect, patterns);
  insert_layer_after_anchor(doc, std::move(layer), anchor_id);
  doc.set_active_layer(layer_id);
  refresh_layer_list();
  refresh_layer_controls();
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
  if (auto* work = doc.work_path(); work != nullptr) {
    auto path = work->path();
    const auto group = path.next_shape_group();
    for (auto& subpath : subpaths) {
      subpath.shape_group = group;
      subpath.op = op;
      path.subpaths.push_back(std::move(subpath));
    }
    work->set_path(std::move(path));
  } else {
    VectorPath path;
    for (auto& subpath : subpaths) {
      subpath.op = op;
      path.subpaths.push_back(std::move(subpath));
    }
    DocumentPath created(doc.allocate_path_id(), tr("Work Path").toStdString(),
                         DocumentPathKind::Work, std::move(path));
    created.mark_dirty();  // authored: no original resource bytes to re-emit
    doc.add_path(std::move(created));
  }
  refresh_paths_panel();
  statusBar()->showMessage(tr("Added the path to the work path."));
}

void MainWindow::add_drag_to_work_path(const LiveShapeParams& params) {
  add_subpaths_to_work_path(generate_live_shape_subpaths(params));
}

VectorShapeContent MainWindow::current_shape_appearance_content() const {
  VectorShapeContent content;
  content.fill.kind = VectorFillKind::Solid;
  content.fill.color = RgbColor{static_cast<std::uint8_t>(current_vector_fill_color_.red()),
                                static_cast<std::uint8_t>(current_vector_fill_color_.green()),
                                static_cast<std::uint8_t>(current_vector_fill_color_.blue())};
  content.stroke.enabled = current_vector_stroke_enabled_;
  content.stroke.width = current_vector_stroke_width_;
  // Photoshop's default for new shapes: the stroke hugs the inside of the path.
  content.stroke.alignment = VectorStrokeAlignment::Inside;
  content.stroke.content.kind = VectorFillKind::Solid;
  content.stroke.content.color =
      RgbColor{static_cast<std::uint8_t>(current_vector_stroke_color_.red()),
               static_cast<std::uint8_t>(current_vector_stroke_color_.green()),
               static_cast<std::uint8_t>(current_vector_stroke_color_.blue())};
  return content;
}

void MainWindow::refresh_vector_tool_options_visibility() {
  const bool shape_tool = current_tool_ == CanvasTool::Line ||
                          current_tool_ == CanvasTool::Rectangle ||
                          current_tool_ == CanvasTool::Ellipse ||
                          current_tool_ == CanvasTool::Pen ||
                          current_tool_ == CanvasTool::Polygon ||
                          current_tool_ == CanvasTool::CustomShape;
  if (!shape_tool) {
    return;  // per-tool visibility already hid every mode-specific widget
  }
  const bool vector_mode = current_vector_tool_mode_ != VectorToolMode::Pixels;
  const bool shape_mode = current_vector_tool_mode_ == VectorToolMode::Shape;
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
  update_vector_swatch_icons();
}

namespace {

// Copies pattern tiles referenced by the fill (and stroke paint) into the
// document store so the rasterizer and the PSD writer can resolve them
// (the ensure_patterns_for_style convention: library first, bundle repair).
void ensure_vector_fill_patterns(Document& doc, const VectorShapeContent& content,
                                 const PatternLibrary& library) {
  for (const auto* fill : {&content.fill, &content.stroke.content}) {
    if (fill->kind != VectorFillKind::Pattern || fill->pattern_id.empty() ||
        doc.metadata().patterns.find(fill->pattern_id) != nullptr) {
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
  ShapeAppearanceSettings initial{layer->vector_shape()->fill, layer->vector_shape()->stroke};

  const auto apply_settings = [this, layer_id](const ShapeAppearanceSettings& settings) {
    auto& target_doc = document();
    auto* target = target_doc.find_layer(layer_id);
    if (target == nullptr || target->vector_shape() == nullptr) {
      return;
    }
    auto content = *target->vector_shape();
    content.fill = settings.fill;
    content.stroke = settings.stroke;
    ensure_vector_fill_patterns(target_doc, content, pattern_library());
    target->set_vector_shape(std::move(content));
    target->metadata()[kLayerMetadataVectorRasterStatus] = kVectorRasterStatusPatchy;
    update_vector_shape_raster(*target, Rect::from_size(target_doc.width(), target_doc.height()),
                               &target_doc.metadata().patterns);
    canvas_->document_changed();
    refresh_layer_thumbnails();
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
  const auto accepted = request_shape_appearance_settings(
      this, apply_settings, std::move(initial), &gradient_library(), &pattern_library(),
      &doc.metadata().patterns,
      RgbColor{static_cast<std::uint8_t>(foreground.red()),
               static_cast<std::uint8_t>(foreground.green()),
               static_cast<std::uint8_t>(foreground.blue())},
      RgbColor{static_cast<std::uint8_t>(background.red()),
               static_cast<std::uint8_t>(background.green()),
               static_cast<std::uint8_t>(background.blue())});
  restore_original_layer();
  preview_edit_lock.release();
  if (!accepted.has_value()) {
    statusBar()->showMessage(tr("Cancelled shape appearance"));
    return;
  }
  push_undo_snapshot(tr("Shape appearance"));
  apply_settings(*accepted);
  if (auto* target = document().find_layer(layer_id); target != nullptr) {
    mark_layer_vector_block_dirty(*target);
  }
  refresh_layer_list();
  refresh_layer_controls();
  statusBar()->showMessage(tr("Updated the shape appearance"));
}

Layer MainWindow::build_fill_layer(const VectorFill& fill, const QString& name) {
  auto& doc = document();
  Layer layer(doc.allocate_layer_id(), name.toStdString(), PixelBuffer());
  VectorShapeContent content;  // empty path = the whole canvas
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
  const auto name = tr("Custom Shape %1").arg(custom_shape_library().entries().size() + 1);
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

void MainWindow::update_vector_swatch_icons() {
  const auto make_swatch = [](QColor color) {
    QPixmap pixmap(20, 14);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QColor(0, 0, 0, 160));
    painter.setBrush(color);
    painter.drawRoundedRect(QRectF(0.5, 0.5, 19.0, 13.0), 2.0, 2.0);
    return QIcon(pixmap);
  };
  if (vector_fill_swatch_button_ != nullptr) {
    vector_fill_swatch_button_->setIcon(make_swatch(current_vector_fill_color_));
  }
  if (vector_stroke_swatch_button_ != nullptr) {
    vector_stroke_swatch_button_->setIcon(make_swatch(current_vector_stroke_color_));
  }
}

}  // namespace patchy::ui
