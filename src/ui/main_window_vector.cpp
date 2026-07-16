// Vector shape tool flows: a released Shape/Path-mode drag from the canvas
// becomes a new shape layer, an addition to the active shape layer, or work
// path subpaths. The options-bar swatches and per-mode widget visibility for
// the Line/Rectangle/Ellipse tools live here too (built in
// main_window_actions.cpp, refined by refresh_vector_tool_options_visibility).
#include "ui/main_window.hpp"

#include "core/document_path.hpp"
#include "core/layer_tree.hpp"
#include "core/vector_live_shapes.hpp"
#include "core/vector_raster.hpp"
#include "core/vector_shape.hpp"
#include "ui/main_window_shared.hpp"

#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QStatusBar>
#include <QToolButton>

#include <algorithm>
#include <cmath>
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

  if (current_vector_tool_mode_ == VectorToolMode::Path) {
    add_drag_to_work_path(params);
  } else {
    create_shape_layer_from_drag(params);
  }
}

void MainWindow::create_shape_layer_from_drag(const LiveShapeParams& params) {
  auto& doc = document();
  auto subpaths = generate_live_shape_subpaths(params);
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
        auto origination = params;
        origination.index = group;
        content.origination.push_back(std::move(origination));
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
    name = shape_layer_base_name(params.kind).arg(suffix++).toStdString();
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
  content.origination = {params};
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

void MainWindow::add_drag_to_work_path(const LiveShapeParams& params) {
  auto& doc = document();
  auto subpaths = generate_live_shape_subpaths(params);
  if (subpaths.empty()) {
    return;
  }
  push_undo_snapshot(tr("Add to work path"));
  const auto op = combine_op_for_index(current_vector_combine_index_);
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
  statusBar()->showMessage(tr("Added the shape to the work path."));
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
                          current_tool_ == CanvasTool::Ellipse;
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
