// Point and segment commands on the target path (GitHub #555; docs/vector-path-edits.md, "Point
// and segment commands"): Break Path at Point, Join End Points, Merge into One Point and Delete
// Segment. The geometry lives in core/path_point_edits; this file picks the points they act on
// (the press under the pointer for the context menu, else the Direct Select selection) and
// applies the result as one path edit.
#include "ui/canvas_widget.hpp"

#include "core/path_point_edits.hpp"
#include "core/vector_shape.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/modifier_names.hpp"

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
      // Two end points join each other; otherwise every open path with a selected point closes.
      if (selection.size() == 2) {
        if (auto joined = join_path_end_points(*path, *selection.begin(), *std::next(selection.begin()), false);
            joined.has_value()) {
          return joined;
        }
      }
      return close_open_subpaths(*path, selection);
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

QString CanvasWidget::path_point_command_requirement(PathPointCommand command) {
  switch (command) {
    case PathPointCommand::BreakAtPoint:
      return tr("Select a point with Direct Select (inside an open path, or any point of a closed one), or "
                "right-click a point");
    case PathPointCommand::JoinEndPoints:
      return tr("Select two end points to join, or a point of an open path to close it, with Direct Select");
    case PathPointCommand::MergePoints:
      return tr("Select two end points, or neighbouring points of one path, with Direct Select, or right-click "
                "a segment");
    case PathPointCommand::DeleteSegment:
      return tr("Click a segment with Direct Select to select it, or right-click it");
  }
  return {};
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

CanvasWidget::PathWeld CanvasWidget::plan_path_weld(const VectorPath& path) const {
  PathWeld weld;
  if (!path_weld_target_.has_value() || path_selected_anchors_.size() != 1) {
    return weld;
  }
  const auto dragged = *path_selected_anchors_.begin();
  const auto target = *path_weld_target_;
  // Two end points (of two open paths, or the two ends of one): they join into one point.
  if (auto joined = join_path_end_points(path, dragged, target, true); joined.has_value()) {
    weld.kind = PathWeld::Kind::Join;
    weld.result = std::move(joined);
    return weld;
  }
  // The next or previous point of the same path (cyclic on a closed one): the segment between
  // them shrank to nothing, so they merge into one point, as Merge into One Point does for a
  // segment (the snapped position, the outer handles). A path cannot branch, so any other pair
  // only snaps; so do the two points of a two-point line and a triangle's corners, which would
  // leave nothing to fill or stroke.
  if (dragged.first != target.first || dragged.first < 0 ||
      dragged.first >= static_cast<int>(path.subpaths.size())) {
    return weld;
  }
  const auto& subpath = path.subpaths[static_cast<std::size_t>(dragged.first)];
  const auto count = static_cast<int>(subpath.anchors.size());
  const bool next = (dragged.second + 1) % count == target.second && (subpath.closed || dragged.second + 1 < count);
  const bool previous = (target.second + 1) % count == dragged.second && (subpath.closed || target.second + 1 < count);
  if ((!next && !previous) || count <= (subpath.closed ? 3 : 2)) {
    return weld;
  }
  if (auto merged = merge_path_points(path, {dragged, target}); merged.has_value()) {
    weld.kind = PathWeld::Kind::MergeNeighbours;
    weld.result = std::move(merged);
  }
  return weld;
}

void CanvasWidget::weld_dragged_path_point() {
  const auto* path = path_edit_target_path();
  if (path == nullptr || path_edit_target_kind() == PathEditTargetKind::TextArea) {
    return;
  }
  auto weld = plan_path_weld(*path);
  if (weld.kind == PathWeld::Kind::SnapOnly || !weld.result.has_value()) {
    return;  // a snap only: the drag just lands there
  }
  // The drag armed the undo entry: the weld joins it, so one Undo takes both back.
  apply_path_edit(std::move(weld.result->path), tr("Edit path"), weld.result->touched_groups);
  path_selected_anchors_ = std::move(weld.result->selection);
  notify_path_selection_changed();
  if (status_callback_) {
    status_callback_(weld.kind == PathWeld::Kind::Join ? tr("Joined the end points") : tr("Merged the points"));
  }
}

QString CanvasWidget::path_weld_hint(const VectorPath& path) const {
  switch (plan_path_weld(path).kind) {
    case PathWeld::Kind::Join:
      return resolve_modifier_names(tr("Release to join the end points. Hold %ALT% to place the point freely."));
    case PathWeld::Kind::MergeNeighbours:
      return resolve_modifier_names(tr("Release to merge the points. Hold %ALT% to place the point freely."));
    case PathWeld::Kind::SnapOnly:
      break;
  }
  return resolve_modifier_names(
      tr("Snapped to a point. Only end points or neighbouring points become one point. Hold %ALT% to place "
         "the point freely."));
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
    const bool available = plan_path_point_command(entry.command, &hit).has_value();
    action->setEnabled(available);
    if (!available) {
      action->setProperty(kActionDisabledReasonProperty, path_point_command_requirement(entry.command));
    }
    const auto command = entry.command;
    connect(action, &QAction::triggered, this, [this, hit, command] { run_path_point_command(command, &hit); });
  }
}

}  // namespace patchy::ui
