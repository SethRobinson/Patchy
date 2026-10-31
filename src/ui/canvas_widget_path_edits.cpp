// Point and segment commands on the target path (GitHub #555; docs/vector-path-edits.md, "Point
// and segment commands"): Break Path at Point, Join End Points, Merge into One Point and Delete
// Segment. The geometry lives in core/path_point_edits; this file picks the points they act on
// (the press under the pointer for the context menu, else the Direct Select selection) and
// applies the result as one path edit.
#include "ui/canvas_widget.hpp"

#include "core/path_point_edits.hpp"
#include "core/vector_shape.hpp"

#include <QAction>
#include <QLatin1String>
#include <QMenu>
#include <QString>

#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace patchy::ui {

CanvasWidget::PathEditTargetKind CanvasWidget::path_edit_target_kind() const {
  if (path_edit_target_path() == nullptr) {
    return PathEditTargetKind::None;
  }
  switch (path_edit_target_owner_key().first) {
    case 1:
      return PathEditTargetKind::VectorMask;
    case 2:
      return PathEditTargetKind::DocumentPath;
    case 3:
      return PathEditTargetKind::TextArea;
    case 4:
      return PathEditTargetKind::ShapeLayer;
    default:
      return PathEditTargetKind::WorkPath;
  }
}

std::optional<PathPointEditResult> CanvasWidget::plan_path_point_command(PathPointCommand command,
                                                                         const PenHoverHit* hit) const {
  const auto* path = path_edit_target_path();
  if (path == nullptr || document_ == nullptr || edit_locked_ || pen_session_active_ || path_transform_active_ ||
      path_edit_target_kind() == PathEditTargetKind::TextArea) {
    return std::nullopt;  // a text area must stay one closed outline
  }
  // Path Select's selection is whole shapes, so only the point under the pointer counts there.
  std::set<PathPointKey> selection;
  if (path_edit_tool() != CanvasTool::PathSelect &&
      (!path_selection_owner_.has_value() || *path_selection_owner_ == path_edit_target_owner_key())) {
    for (const auto& key : path_selected_anchors_) {
      if (key.first >= 0 && key.first < static_cast<int>(path->subpaths.size()) && key.second >= 0 &&
          key.second < static_cast<int>(path->subpaths[static_cast<std::size_t>(key.first)].anchors.size())) {
        selection.insert(key);
      }
    }
  }
  const bool over_anchor = hit != nullptr && hit->anchor.first >= 0;
  const bool over_segment = hit != nullptr && !over_anchor && hit->segment.first >= 0;
  switch (command) {
    case PathPointCommand::BreakAtPoint:
      if (over_anchor) {
        return break_path_at_points(*path, {hit->anchor});
      }
      return break_path_at_points(*path, selection);
    case PathPointCommand::JoinEndPoints:
      if (selection.size() != 2) {
        return std::nullopt;
      }
      return join_path_end_points(*path, *selection.begin(), *std::next(selection.begin()), false);
    case PathPointCommand::MergePoints:
      if (over_segment) {
        const auto& subpath = path->subpaths[static_cast<std::size_t>(hit->segment.first)];
        const auto count = static_cast<int>(subpath.anchors.size());
        return merge_path_points(*path, {hit->segment, {hit->segment.first, (hit->segment.second + 1) % count}});
      }
      return merge_path_points(*path, selection);
    case PathPointCommand::DeleteSegment:
      if (over_segment) {
        return delete_path_segments(*path, {hit->segment});
      }
      return delete_path_segments(*path, path_segments_between_points(*path, selection));
  }
  return std::nullopt;
}

bool CanvasWidget::path_point_command_available(PathPointCommand command) const {
  return plan_path_point_command(command, nullptr).has_value();
}

bool CanvasWidget::run_path_point_command(PathPointCommand command) {
  return run_path_point_command(command, nullptr);
}

bool CanvasWidget::run_path_point_command(PathPointCommand command, const PenHoverHit* hit) {
  auto plan = plan_path_point_command(command, hit);
  if (!plan.has_value()) {
    return false;
  }
  QString label;
  switch (command) {
    case PathPointCommand::BreakAtPoint:
      label = tr("Break path");
      break;
    case PathPointCommand::JoinEndPoints:
      label = tr("Join end points");
      break;
    case PathPointCommand::MergePoints:
      label = tr("Merge points");
      break;
    case PathPointCommand::DeleteSegment:
      label = tr("Delete segment");
      break;
  }
  // A shape layer left with no subpath would fill the whole canvas, so it goes in the same undo
  // step, as Delete does (GitHub issue 315).
  std::vector<LayerId> emptied;
  if (path_edit_target_kind() == PathEditTargetKind::ShapeLayer && plan->path.subpaths.empty()) {
    emptied.push_back(path_edit_target_layer()->id());
  }
  path_edit_undo_armed_ = false;
  apply_path_edit(std::move(plan->path), label, plan->touched_groups);
  path_selected_anchors_ = emptied.empty() ? std::move(plan->selection) : std::set<std::pair<int, int>>{};
  extra_selected_anchors_.clear();
  notify_path_selection_changed();
  if (!emptied.empty() && shape_layers_emptied_callback_) {
    shape_layers_emptied_callback_(emptied);
  }
  path_edit_undo_armed_ = false;
  update();
  return true;
}

void CanvasWidget::add_path_point_command_entries(QMenu& menu, const PenHoverHit& hit) {
  struct Entry {
    PathPointCommand command;
    const char* object_name;
    QString text;
  };
  const Entry entries[] = {
      {PathPointCommand::BreakAtPoint, "pathMenuBreakAtPointAction", tr("Break Path at Point")},
      {PathPointCommand::JoinEndPoints, "pathMenuJoinEndPointsAction", tr("Join End Points")},
      {PathPointCommand::MergePoints, "pathMenuMergePointsAction", tr("Merge into One Point")},
      {PathPointCommand::DeleteSegment, "pathMenuDeleteSegmentAction", tr("Delete Segment")},
  };
  for (const auto& entry : entries) {
    auto* action = menu.addAction(entry.text);
    action->setObjectName(QLatin1String(entry.object_name));
    action->setEnabled(plan_path_point_command(entry.command, &hit).has_value());
    const auto command = entry.command;
    connect(action, &QAction::triggered, this, [this, hit, command] { run_path_point_command(command, &hit); });
  }
}

}  // namespace patchy::ui
