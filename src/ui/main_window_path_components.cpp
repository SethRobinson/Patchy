// Point, segment and component commands on the target path (GitHub #555; docs/vector-path-edits.md,
// "Point and segment commands" and "Component commands"): the Layer > Shape actions, the
// component clipboard behind Edit > Cut/Copy/Paste, Color Selected Shapes and Move/Copy Selected
// Shapes to New Layer.
#include "ui/main_window.hpp"

#include "core/path_point_edits.hpp"
#include "core/shape_combine.hpp"
#include "core/pattern_resource.hpp"
#include "core/vector_shape.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/color_panel.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/main_window_shared.hpp"

#include <QAction>
#include <QColor>
#include <QLatin1String>
#include <QMenu>
#include <QPushButton>
#include <QStatusBar>

#include <algorithm>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

// The status tip follows a language switch (main_window_actions_menus.cpp has the same helper).
void bind_status_tip(QAction* action, const char* source) {
  action->setProperty(kTranslationContextProperty, QString::fromLatin1(kMainWindowTranslationContext));
  action->setProperty(kTranslationStatusTipProperty, QString::fromLatin1(source));
}

QString component_color_refusal_text(ComponentColorRefusal refusal) {
  switch (refusal) {
    case ComponentColorRefusal::OnlyCutters:
      return MainWindow::tr("The selected shapes only cut into others and paint nothing of their own");
    case ComponentColorRefusal::SharedOverlap:
      return MainWindow::tr("Shapes combined with Intersect or Exclude share one color");
    case ComponentColorRefusal::InvertedShape:
      return MainWindow::tr("An inverted shape has one color");
    case ComponentColorRefusal::NoComponents:
    case ComponentColorRefusal::None:
      break;
  }
  return MainWindow::tr("Select shapes on a shape layer with Path Select or Direct Select");
}

std::vector<int> all_groups(const VectorPath& path) {
  std::vector<int> groups;
  for (const auto& subpath : path.subpaths) {
    if (std::find(groups.begin(), groups.end(), subpath.shape_group) == groups.end()) {
      groups.push_back(subpath.shape_group);
    }
  }
  return groups;
}

}  // namespace

void MainWindow::create_path_component_actions(QMenu* layer_shape_menu) {
  struct PointEntry {
    const char* text;
    const char* tip;
    const char* object_name;
    const char* hotkey_id;
  };
  // PathPointCommand order.
  const PointEntry point_entries[] = {
      {QT_TR_NOOP("Break Path at Point"), QT_TR_NOOP("Open the path at the selected points"),
       "pathBreakAtPointAction", "path.break_at_point"},
      {QT_TR_NOOP("Join End Points"),
       QT_TR_NOOP("Join the two selected end points with a straight segment, or close each open path with a "
                  "selected point"),
       "pathJoinEndPointsAction", "path.join_end_points"},
      {QT_TR_NOOP("Merge into One Point"),
       QT_TR_NOOP("Merge the selected end points, or a row of selected points, into one point at their middle"),
       "pathMergePointsAction", "path.merge_points"},
      {QT_TR_NOOP("Delete Segment"), QT_TR_NOOP("Delete the selected segments and leave the path open"),
       "pathDeleteSegmentAction", "path.delete_segment"},
  };
  // Merge Shapes ends the Combine Shapes section: it bakes what those set up (GitHub #555 part 1).
  path_merge_shapes_action_ = layer_shape_menu->addAction(tr("Merge Shapes"));
  path_merge_shapes_action_->setObjectName(QStringLiteral("pathMergeShapesAction"));
  path_merge_shapes_action_->setProperty("patchy.channelViewBlocked", true);
  path_merge_shapes_action_->setStatusTip(
      tr("Turn the selected shapes, combined as they show, into one outline you can move and edit"));
  bind_action_text(path_merge_shapes_action_, QT_TR_NOOP("Merge Shapes"));
  bind_status_tip(path_merge_shapes_action_,
                  QT_TR_NOOP("Turn the selected shapes, combined as they show, into one outline you can move and edit"));
  register_hotkey(path_merge_shapes_action_, QStringLiteral("path.merge_shapes"));
  connect(path_merge_shapes_action_, &QAction::triggered, this, [this] { merge_selected_path_components(); });
  path_merge_shapes_action_->setEnabled(false);
  layer_shape_menu->addSeparator();
  for (std::size_t i = 0; i < path_point_actions_.size(); ++i) {
    const auto& entry = point_entries[i];
    auto* action = layer_shape_menu->addAction(tr(entry.text));
    action->setObjectName(QLatin1String(entry.object_name));
    action->setProperty("patchy.channelViewBlocked", true);
    action->setStatusTip(tr(entry.tip));
    bind_action_text(action, entry.text);
    bind_status_tip(action, entry.tip);
    register_hotkey(action, QLatin1String(entry.hotkey_id));
    const auto command = static_cast<int>(i);
    connect(action, &QAction::triggered, this, [this, command] { run_path_point_command(command); });
    action->setEnabled(false);
    path_point_actions_[i] = action;
  }

  struct ComponentEntry {
    const char* text;
    const char* tip;
    const char* object_name;
    const char* hotkey_id;  // null: no hotkey (Edit > Cut/Copy/Paste cover these)
    QAction** slot;
    bool in_menu;
  };
  const ComponentEntry component_entries[] = {
      {QT_TR_NOOP("Color Selected Shapes..."), QT_TR_NOOP("Give the selected shapes a fill color of their own"),
       "pathShapesColorAction", "path.shapes_color", &path_shapes_color_action_, true},
      {QT_TR_NOOP("Move Selected Shapes to New Layer"),
       QT_TR_NOOP("Move the selected shapes to a new shape layer above"), "pathShapesToNewLayerAction",
       "path.shapes_to_new_layer", &path_shapes_to_new_layer_action_, true},
      {QT_TR_NOOP("Copy Selected Shapes to New Layer"),
       QT_TR_NOOP("Copy the selected shapes to a new shape layer above"), "pathShapesCopyToNewLayerAction",
       "path.shapes_copy_to_new_layer", &path_shapes_copy_to_new_layer_action_, true},
      {QT_TR_NOOP("Cut Shapes"), QT_TR_NOOP("Cut the selected shapes"), "pathShapesCutAction", nullptr,
       &path_shapes_cut_action_, false},
      {QT_TR_NOOP("Copy Shapes"), QT_TR_NOOP("Copy the selected shapes"), "pathShapesCopyAction", nullptr,
       &path_shapes_copy_action_, false},
      {QT_TR_NOOP("Paste Shapes"), QT_TR_NOOP("Paste the copied shapes into the target path"),
       "pathShapesPasteAction", nullptr, &path_shapes_paste_action_, false},
  };
  layer_shape_menu->addSeparator();
  for (const auto& entry : component_entries) {
    auto* action = entry.in_menu ? layer_shape_menu->addAction(tr(entry.text)) : new QAction(tr(entry.text), this);
    action->setObjectName(QLatin1String(entry.object_name));
    action->setProperty("patchy.channelViewBlocked", true);
    action->setStatusTip(tr(entry.tip));
    bind_action_text(action, entry.text);
    bind_status_tip(action, entry.tip);
    if (entry.hotkey_id != nullptr) {
      register_hotkey(action, QLatin1String(entry.hotkey_id));
    }
    action->setEnabled(false);
    *entry.slot = action;
  }
  connect(path_shapes_color_action_, &QAction::triggered, this, [this] { color_selected_path_components(); });
  connect(path_shapes_to_new_layer_action_, &QAction::triggered, this,
          [this] { selected_path_components_to_new_layer(false); });
  connect(path_shapes_copy_to_new_layer_action_, &QAction::triggered, this,
          [this] { selected_path_components_to_new_layer(true); });
  connect(path_shapes_cut_action_, &QAction::triggered, this, [this] { copy_path_components(true); });
  connect(path_shapes_copy_action_, &QAction::triggered, this, [this] { copy_path_components(false); });
  connect(path_shapes_paste_action_, &QAction::triggered, this, [this] { static_cast<void>(paste_path_components()); });
  connect(layer_shape_menu, &QMenu::aboutToShow, this, [this] { refresh_path_edit_action_states(); });
}

void MainWindow::refresh_path_edit_action_states() {
  const bool usable = has_active_document() && canvas_ != nullptr && !preview_dialog_edit_locked();
  for (std::size_t i = 0; i < path_point_actions_.size(); ++i) {
    if (path_point_actions_[i] != nullptr) {
      const auto command = static_cast<CanvasWidget::PathPointCommand>(i);
      const bool available = usable && canvas_->path_point_command_available(command);
      path_point_actions_[i]->setEnabled(available);
      path_point_actions_[i]->setProperty(
          kActionDisabledReasonProperty,
          available || canvas_ == nullptr ? QString() : CanvasWidget::path_point_command_requirement(command));
    }
  }
  const auto merge_refusal = usable ? merge_shapes_refusal() : QString(QLatin1String("-"));
  if (path_merge_shapes_action_ != nullptr) {
    path_merge_shapes_action_->setEnabled(merge_refusal.isEmpty());
    path_merge_shapes_action_->setProperty(kActionDisabledReasonProperty,
                                           merge_refusal == QLatin1String("-") ? QString() : merge_refusal);
  }
  if (path_merge_shapes_button_ != nullptr) {
    path_merge_shapes_button_->setEnabled(merge_refusal.isEmpty());
    path_merge_shapes_button_->setToolTip(merge_refusal.isEmpty() || merge_refusal == QLatin1String("-")
                                              ? tr("Merge Shapes: turn the selected shapes, combined as they "
                                                   "show, into one outline")
                                              : merge_refusal);
  }
  const auto kind = usable ? canvas_->path_edit_target_kind() : CanvasWidget::PathEditTargetKind::None;
  const auto groups = usable ? canvas_->path_edit_selected_groups() : std::vector<int>{};
  const bool components = !groups.empty() && kind != CanvasWidget::PathEditTargetKind::None &&
                          kind != CanvasWidget::PathEditTargetKind::TextArea;
  if (path_shapes_color_action_ != nullptr) {
    auto refusal = ComponentColorRefusal::NoComponents;
    if (components && kind == CanvasWidget::PathEditTargetKind::ShapeLayer) {
      if (const auto active = document().active_layer_id(); active.has_value()) {
        if (const auto* layer = std::as_const(document()).find_layer(*active);
            layer != nullptr && layer->vector_shape() != nullptr) {
          refusal = vector_shape_components_color_refusal(*layer->vector_shape(), groups);
        }
      }
    }
    path_shapes_color_action_->setEnabled(refusal == ComponentColorRefusal::None);
    path_shapes_color_action_->setProperty(
        kActionDisabledReasonProperty,
        refusal == ComponentColorRefusal::None || !components ? QString() : component_color_refusal_text(refusal));
  }
  for (auto* action : {path_shapes_to_new_layer_action_, path_shapes_copy_to_new_layer_action_}) {
    if (action != nullptr) {
      action->setEnabled(components);
      action->setProperty(kActionDisabledReasonProperty,
                          components ? QString() : tr("Select shapes with Path Select or Direct Select first"));
    }
  }
  if (path_shapes_cut_action_ != nullptr) {
    path_shapes_cut_action_->setEnabled(usable && path_component_clipboard_active());
    path_shapes_copy_action_->setEnabled(usable && path_component_clipboard_active());
    path_shapes_paste_action_->setEnabled(usable && clipboard_.has_value() && clipboard_->path_components.has_value());
  }
}

void MainWindow::run_path_point_command(int command) {
  if (canvas_ == nullptr || !has_active_document()) {
    return;
  }
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return;
  }
  const auto which = static_cast<CanvasWidget::PathPointCommand>(command);
  if (!canvas_->run_path_point_command(which)) {
    show_status_error(CanvasWidget::path_point_command_requirement(which));
    return;
  }
  refresh_paths_panel();
  refresh_layer_thumbnails();
  refresh_path_edit_action_states();
}

bool MainWindow::path_component_clipboard_active() const {
  if (canvas_ == nullptr || !has_active_document()) {
    return false;
  }
  const auto tool = canvas_->tool();
  if (tool != CanvasTool::PathSelect && tool != CanvasTool::DirectSelect) {
    return false;
  }
  const auto kind = canvas_->path_edit_target_kind();
  return kind != CanvasWidget::PathEditTargetKind::None && kind != CanvasWidget::PathEditTargetKind::TextArea &&
         !canvas_->path_edit_selected_groups().empty();
}

void MainWindow::remove_path_components_in_open_undo_step(const std::vector<int>& groups) {
  const auto* path = canvas_->path_edit_target_path();
  if (path == nullptr) {
    return;
  }
  auto remaining = path_without_components(*path, groups);
  canvas_->clear_path_edit_selection();
  if (canvas_->path_edit_target_kind() == CanvasWidget::PathEditTargetKind::ShapeLayer) {
    auto* layer = document().find_layer(*document().active_layer_id());
    if (remaining.subpaths.empty()) {
      // An empty shape path fills the whole canvas: the layer goes instead (GitHub issue 315).
      remove_layers_in_open_undo_step({layer->id()});
      return;
    }
    auto content = *std::as_const(*layer).vector_shape();
    content.path = std::move(remaining);
    drop_live_shape_origination(content, groups);
    write_shape_layer_content(*layer, std::move(content));
  } else {
    canvas_->replace_path_edit_target(std::move(remaining), groups);
  }
}

void MainWindow::copy_path_components(bool cutting) {
  if (!path_component_clipboard_active()) {
    return;
  }
  if (cutting && preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return;
  }
  const auto groups = canvas_->path_edit_selected_groups();
  ClipboardPayload payload;
  payload.path_components = path_components(*canvas_->path_edit_target_path(), groups);
  if (canvas_->path_edit_target_kind() == CanvasWidget::PathEditTargetKind::ShapeLayer) {
    const auto* layer = std::as_const(document()).find_layer(*document().active_layer_id());
    Layer copy = *layer;
    copy.set_vector_shape(vector_shape_content_for_groups(*layer->vector_shape(), groups));
    collect_referenced_pattern_resources(copy, document().metadata().patterns, payload.pattern_resources);
    payload.path_components_layer.push_back(std::move(copy));
  }
  payload.source_session_id = session().session_id;
  clipboard_ = std::move(payload);
  const auto count = static_cast<int>(groups.size());
  if (cutting) {
    push_undo_snapshot(tr("Cut shapes"));
    remove_path_components_in_open_undo_step(groups);
    refresh_paths_panel();
    refresh_layer_thumbnails();
    refresh_layer_controls();
    statusBar()->showMessage(tr("Cut %1 shape(s)").arg(count));
  } else {
    statusBar()->showMessage(tr("Copied %1 shape(s)").arg(count));
  }
  refresh_path_edit_action_states();
}

bool MainWindow::paste_path_components() {
  if (!clipboard_.has_value() || !clipboard_->path_components.has_value() || canvas_ == nullptr ||
      !has_active_document()) {
    return false;
  }
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return true;
  }
  canvas_->finish_free_transform();
  auto& doc = document();
  const auto& components = *clipboard_->path_components;
  const auto count = static_cast<int>(all_groups(components).size());
  const auto kind = canvas_->path_edit_target_kind();
  for (const auto& resource : clipboard_->pattern_resources) {
    if (doc.metadata().patterns.find(resource.id) == nullptr) {
      PatternResource adopted = resource;
      adopted.provenance = PatternProvenance::Authored;  // the target file has no raw block for it
      doc.metadata().patterns.adopt(adopted);
    }
  }
  if (kind != CanvasWidget::PathEditTargetKind::None && kind != CanvasWidget::PathEditTargetKind::TextArea) {
    // Into the target path: the components join it with their ops (a shape layer paints them
    // with its own paint, as a path pasted into a shape does).
    push_undo_snapshot(tr("Paste shapes"));
    auto path = *canvas_->path_edit_target_path();
    const auto renumbered = append_path_components(path, components);
    std::vector<int> pasted;
    for (const auto& [source, group] : renumbered) {
      pasted.push_back(group);
    }
    if (kind == CanvasWidget::PathEditTargetKind::ShapeLayer) {
      auto* layer = doc.find_layer(*doc.active_layer_id());
      auto content = *std::as_const(*layer).vector_shape();
      content.path = std::move(path);
      if (!content.parts.empty()) {
        // A merged shape paints only listed groups: the pasted ones get the copied paint.
        VectorShapePart part;
        part.groups.assign(pasted.begin(), pasted.end());
        const auto* source = clipboard_->path_components_layer.empty()
                                 ? nullptr
                                 : clipboard_->path_components_layer.front().vector_shape();
        const auto* paint_source = source != nullptr ? source : &content;
        part.fill = paint_source->parts.empty() ? paint_source->fill : paint_source->parts.front().fill;
        part.stroke = paint_source->parts.empty() ? paint_source->stroke : paint_source->parts.front().stroke;
        part.pattern_anchor = layer_effects_reference_point(std::as_const(*layer));
        content.parts.push_back(std::move(part));
      }
      write_shape_layer_content(*layer, std::move(content));
    } else {
      canvas_->replace_path_edit_target(std::move(path), {});
    }
    canvas_->select_path_groups(pasted);
    refresh_paths_panel();
    refresh_layer_thumbnails();
    refresh_layer_controls();
    statusBar()->showMessage(tr("Pasted %1 shape(s)").arg(count));
    refresh_path_edit_action_states();
    return true;
  }
  // No target path: a new shape layer, painted like the shapes' own layer when they came from one.
  push_undo_snapshot(tr("Paste shapes"));
  std::optional<Layer> created;
  if (!clipboard_->path_components_layer.empty()) {
    const auto& source = clipboard_->path_components_layer.front();
    created = clone_layer_tree_with_document_ids(doc, source);
    if (created.has_value()) {
      created->set_name(unused_duplicate_layer_name(source.name()));
      auto content = *std::as_const(*created).vector_shape();
      make_first_component_paint(content.path);
      write_shape_layer_content(*created, std::move(content));
    }
  }
  if (!created.has_value()) {
    auto path = components;
    make_first_component_paint(path);
    created = shape_layer_from_subpaths(std::move(path.subpaths));
  }
  const auto created_id = created->id();
  const auto selected = selected_layer_ids();
  insert_layer_after_anchor(doc, std::move(*created), selected.empty() ? doc.active_layer_id() : selected.front());
  doc.set_active_layer(created_id);
  refresh_layer_list();
  refresh_layer_controls();
  path_row_hidden_for_layer_.reset();
  refresh_paths_panel();
  canvas_->document_changed();
  statusBar()->showMessage(tr("Pasted %1 shape(s)").arg(count));
  refresh_path_edit_action_states();
  return true;
}

bool MainWindow::set_selected_path_components_color(QColor color) {
  if (canvas_ == nullptr || !has_active_document() ||
      canvas_->path_edit_target_kind() != CanvasWidget::PathEditTargetKind::ShapeLayer) {
    show_status_error(component_color_refusal_text(ComponentColorRefusal::NoComponents));
    return false;
  }
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return false;
  }
  const auto groups = canvas_->path_edit_selected_groups();
  auto* layer = document().find_layer(*document().active_layer_id());
  auto content = *std::as_const(*layer).vector_shape();
  const auto refusal = recolor_vector_shape_components(
      content, groups, RgbColor{static_cast<std::uint8_t>(color.red()), static_cast<std::uint8_t>(color.green()),
                                static_cast<std::uint8_t>(color.blue())},
      layer_effects_reference_point(std::as_const(*layer)));
  if (refusal != ComponentColorRefusal::None) {
    show_status_error(component_color_refusal_text(refusal));
    return false;
  }
  push_undo_snapshot(tr("Shape color"));
  write_shape_layer_content(*layer, std::move(content));
  refresh_layer_thumbnails();
  refresh_layer_controls();
  statusBar()->showMessage(tr("Colored %1 shape(s)").arg(groups.size()));
  return true;
}

void MainWindow::color_selected_path_components() {
  if (canvas_ == nullptr || !has_active_document()) {
    return;
  }
  refresh_path_edit_action_states();
  if (path_shapes_color_action_ != nullptr && !path_shapes_color_action_->isEnabled()) {
    const auto reason = path_shapes_color_action_->property(kActionDisabledReasonProperty).toString();
    show_status_error(reason.isEmpty() ? component_color_refusal_text(ComponentColorRefusal::NoComponents) : reason);
    return;
  }
  // Start from the color the first selected shape paints with.
  const auto groups = canvas_->path_edit_selected_groups();
  const auto* layer = std::as_const(document()).find_layer(*document().active_layer_id());
  const auto& content = *layer->vector_shape();
  RgbColor initial = content.fill.color;
  for (const auto& part : content.parts) {
    if (std::find(part.groups.begin(), part.groups.end(), groups.front()) != part.groups.end()) {
      initial = part.fill.color;
      break;
    }
  }
  const auto color = request_patchy_color(this, QColor(initial.red, initial.green, initial.blue), tr("Shape Color"));
  if (color.has_value()) {
    static_cast<void>(set_selected_path_components_color(*color));
  }
}

void MainWindow::selected_path_components_to_new_layer(bool keep_originals) {
  if (canvas_ == nullptr || !has_active_document()) {
    return;
  }
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return;
  }
  const auto kind = canvas_->path_edit_target_kind();
  const auto groups = canvas_->path_edit_selected_groups();
  if (groups.empty() || kind == CanvasWidget::PathEditTargetKind::None ||
      kind == CanvasWidget::PathEditTargetKind::TextArea) {
    show_status_error(tr("Select shapes with Path Select or Direct Select first"));
    return;
  }
  canvas_->finish_free_transform();
  auto& doc = document();
  const auto components = path_components(*canvas_->path_edit_target_path(), groups);
  push_undo_snapshot(keep_originals ? tr("Copy shapes to new layer") : tr("Move shapes to new layer"));
  std::optional<Layer> created;
  const auto anchor = doc.active_layer_id();
  if (kind == CanvasWidget::PathEditTargetKind::ShapeLayer) {
    // The layer itself, styles and masks included, with only the selected shapes.
    const auto& source = *std::as_const(doc).find_layer(*anchor);
    created = clone_layer_tree_with_document_ids(doc, source);
    if (created.has_value()) {
      created->set_name(unused_duplicate_layer_name(source.name()));
      auto content = vector_shape_content_for_groups(*source.vector_shape(), groups);
      make_first_component_paint(content.path);
      write_shape_layer_content(*created, std::move(content));
    }
  }
  if (!created.has_value()) {
    auto path = components;
    make_first_component_paint(path);
    created = shape_layer_from_subpaths(std::move(path.subpaths));
  }
  if (!keep_originals) {
    remove_path_components_in_open_undo_step(groups);
  }
  const auto created_id = created->id();
  const auto created_groups = all_groups(created->vector_shape()->path);
  // Above the source layer; a source that went (all its shapes moved) leaves the active layer.
  const auto insert_anchor = anchor.has_value() && std::as_const(doc).find_layer(*anchor) != nullptr
                                 ? anchor
                                 : doc.active_layer_id();
  insert_layer_after_anchor(doc, std::move(*created), insert_anchor);
  doc.set_active_layer(created_id);
  refresh_layer_list();
  refresh_layer_controls();
  path_row_hidden_for_layer_.reset();
  refresh_paths_panel();
  canvas_->document_changed();
  // The moved shapes stay selected, now on their new layer.
  canvas_->select_path_groups(created_groups);
  const auto count = static_cast<int>(groups.size());
  statusBar()->showMessage(keep_originals ? tr("Copied %1 shape(s) to a new layer").arg(count)
                                          : tr("Moved %1 shape(s) to a new layer").arg(count));
  refresh_path_edit_action_states();
}

bool MainWindow::align_selected_path_components(AlignEdge edge) {
  if (canvas_ == nullptr || !has_active_document() ||
      (canvas_->tool() != CanvasTool::PathSelect && canvas_->tool() != CanvasTool::DirectSelect)) {
    return false;
  }
  const auto kind = canvas_->path_edit_target_kind();
  const auto groups = canvas_->path_edit_selected_groups();
  const auto* path = canvas_->path_edit_target_path();
  if (groups.size() < 2 || path == nullptr || kind == CanvasWidget::PathEditTargetKind::None ||
      kind == CanvasWidget::PathEditTargetKind::TextArea) {
    return false;
  }
  std::vector<VectorPathBounds> boxes;
  for (const int group : groups) {
    boxes.push_back(*path_components(*path, {group}).bounds());
  }
  VectorPathBounds target = boxes.front();
  for (const auto& box : boxes) {
    target.left = std::min(target.left, box.left);
    target.top = std::min(target.top, box.top);
    target.right = std::max(target.right, box.right);
    target.bottom = std::max(target.bottom, box.bottom);
  }
  if (align_to_canvas_) {
    target = {0.0, 0.0, static_cast<double>(document().width()), static_cast<double>(document().height())};
  }
  auto working = *path;
  bool moved = false;
  for (std::size_t i = 0; i < groups.size(); ++i) {
    const auto& box = boxes[i];
    double dx = 0.0;
    double dy = 0.0;
    switch (edge) {
      case AlignEdge::Left: dx = target.left - box.left; break;
      case AlignEdge::HorizontalCenter: dx = (target.left + target.right - box.left - box.right) / 2.0; break;
      case AlignEdge::Right: dx = target.right - box.right; break;
      case AlignEdge::Top: dy = target.top - box.top; break;
      case AlignEdge::VerticalCenter: dy = (target.top + target.bottom - box.top - box.bottom) / 2.0; break;
      case AlignEdge::Bottom: dy = target.bottom - box.bottom; break;
    }
    if (dx == 0.0 && dy == 0.0) {
      continue;
    }
    moved = true;
    for (auto& subpath : working.subpaths) {
      if (subpath.shape_group != groups[i]) {
        continue;
      }
      for (auto& anchor : subpath.anchors) {
        anchor.anchor_x += dx;
        anchor.in_x += dx;
        anchor.out_x += dx;
        anchor.anchor_y += dy;
        anchor.in_y += dy;
        anchor.out_y += dy;
      }
    }
  }
  if (!moved) {
    statusBar()->showMessage(tr("The selected shapes are already aligned"));
    return true;
  }
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return true;
  }
  push_undo_snapshot(tr("Align shapes"));
  // A move keeps live shapes live (resolve_live_shape_group allows a translation).
  canvas_->replace_path_edit_target(std::move(working), {});
  canvas_->select_path_groups(groups);
  refresh_paths_panel();
  refresh_layer_thumbnails();
  statusBar()->showMessage(tr("Aligned %1 shape(s)").arg(groups.size()));
  return true;
}

QString MainWindow::merge_shapes_refusal() const {
  if (canvas_ == nullptr || !has_active_document()) {
    return QStringLiteral("-");
  }
  // Two or more shape layers selected in the Layers panel (and no shapes picked on the canvas):
  // they combine into the bottom one, then merge.
  if (canvas_->path_edit_selected_groups().empty() &&
      combine_shape_candidates(std::as_const(document()).layers(), selected_or_active_layer_ids()).refusal ==
          ShapeCombineRefusal::None) {
    return {};
  }
  const auto kind = canvas_->path_edit_target_kind();
  if (kind == CanvasWidget::PathEditTargetKind::None) {
    return tr("Select a shape layer or a path first");
  }
  if (kind == CanvasWidget::PathEditTargetKind::TextArea) {
    return tr("A text area keeps one outline");
  }
  const auto* path = canvas_->path_edit_target_path();
  if (path == nullptr || path->subpaths.empty()) {
    return tr("The path has no shapes");
  }
  auto groups = canvas_->path_edit_selected_groups();
  if (groups.empty()) {
    groups = all_groups(*path);
  }
  if (kind == CanvasWidget::PathEditTargetKind::ShapeLayer) {
    const auto* layer = std::as_const(document()).find_layer(*document().active_layer_id());
    const auto& parts = layer->vector_shape()->parts;
    // Shapes painted by different parts (their own colors) cannot become one outline.
    for (const auto& part : parts) {
      const auto in_part = [&part](int group) {
        return std::find(part.groups.begin(), part.groups.end(), group) != part.groups.end();
      };
      const bool any = std::any_of(groups.begin(), groups.end(), in_part);
      if (any && !std::all_of(groups.begin(), groups.end(), in_part)) {
        return tr("Shapes with their own colors cannot merge with others");
      }
    }
  }
  return {};
}

void MainWindow::merge_selected_path_components() {
  if (canvas_ == nullptr || !has_active_document()) {
    return;
  }
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return;
  }
  const auto refusal = merge_shapes_refusal();
  if (!refusal.isEmpty()) {
    show_status_error(refusal == QLatin1String("-") ? tr("Select a shape layer or a path first") : refusal);
    return;
  }
  canvas_->finish_free_transform();
  auto& doc = document();
  push_undo_snapshot(tr("Merge shapes"));
  if (canvas_->path_edit_selected_groups().empty()) {
    const auto candidates = combine_shape_candidates(std::as_const(doc).layers(), selected_or_active_layer_ids());
    if (candidates.refusal == ShapeCombineRefusal::None) {
      const auto combined = combine_shape_layers(doc, candidates.bottom_to_top, PathCombineOp::Add);
      if (combined.has_value()) {
        doc.set_active_layer(combined->layer_id);
        canvas_->clear_path_edit_selection();
        refresh_layer_list();
        refresh_layer_controls();
        path_row_hidden_for_layer_.reset();
        refresh_paths_panel();
      }
    }
  }
  const auto* path = canvas_->path_edit_target_path();
  if (path == nullptr) {
    return;
  }
  auto groups = canvas_->path_edit_selected_groups();
  const bool whole = groups.empty();
  if (whole) {
    groups = all_groups(*path);
  }
  auto merged = merge_path_components(*path, groups);
  if (!merged.has_value()) {
    return;
  }
  const auto kind = canvas_->path_edit_target_kind();
  canvas_->clear_path_edit_selection();
  if (kind == CanvasWidget::PathEditTargetKind::ShapeLayer) {
    auto* layer = doc.find_layer(*doc.active_layer_id());
    if (merged->path.subpaths.empty()) {
      remove_layers_in_open_undo_step({layer->id()});  // nothing left: an empty shape would fill the canvas
      statusBar()->showMessage(tr("The shapes left no area; the layer was removed"));
      return;
    }
    auto content = *std::as_const(*layer).vector_shape();
    content.path = std::move(merged->path);
    drop_live_shape_origination(content, groups);
    write_shape_layer_content(*layer, std::move(content));
  } else {
    canvas_->replace_path_edit_target(std::move(merged->path), groups);
  }
  if (merged->merged_subpaths > 0) {
    canvas_->select_path_groups({merged->merged_group});
  }
  refresh_layer_list();
  refresh_layer_controls();
  refresh_paths_panel();
  refresh_layer_thumbnails();
  canvas_->document_changed();
  statusBar()->showMessage(tr("Merged %1 shape(s) into one outline").arg(groups.size()));
  refresh_path_edit_action_states();
}

}  // namespace patchy::ui
