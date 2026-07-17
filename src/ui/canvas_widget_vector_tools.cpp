// Pen tool session (anchor placement, smooth-handle drags, close/finish/
// cancel, construction overlay) and path editing (PathSelect/DirectSelect
// drags, marquee, nudges, pen add/delete/convert on the target path). The
// committed pen path leaves through vector_path_committed_callback_; path
// edits mutate the active shape layer or work path directly with undo armed
// through before_edit_callback_ (the painting-tools convention).
//
// NOTE: canvas_widget_pen.cpp is TABLET INPUT (pressure, tilt, pen buttons),
// not this vector Pen tool.
#include "ui/canvas_widget.hpp"

#include "core/document_path.hpp"
#include "core/vector_raster.hpp"
#include "core/vector_shape.hpp"

#include <QDateTime>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

constexpr double kPenCloseHitRadiusPx = 8.0;   // screen pixels
constexpr double kPenSmoothDragThresholdPx = 2.0;  // document pixels
constexpr double kPathHitRadiusPx = 7.0;       // half of the 14 px hit rect
constexpr qint64 kPathNudgeCoalesceMs = 800;

// De Casteljau split of the cubic (a.anchor, a.out, b.in, b.anchor) at t,
// yielding the inserted anchor and the adjusted neighbor handles. Preserves
// the curve exactly (the Pen add-anchor rule).
PathAnchor split_segment_anchor(PathAnchor& a, PathAnchor& b, double t) {
  const auto lerp = [](double p, double q, double t_value) { return p + (q - p) * t_value; };
  const double ax = a.anchor_x, ay = a.anchor_y;
  const double bx = b.anchor_x, by = b.anchor_y;
  const double p1x = a.out_x, p1y = a.out_y;
  const double p2x = b.in_x, p2y = b.in_y;
  const double q0x = lerp(ax, p1x, t), q0y = lerp(ay, p1y, t);
  const double q1x = lerp(p1x, p2x, t), q1y = lerp(p1y, p2y, t);
  const double q2x = lerp(p2x, bx, t), q2y = lerp(p2y, by, t);
  const double r0x = lerp(q0x, q1x, t), r0y = lerp(q0y, q1y, t);
  const double r1x = lerp(q1x, q2x, t), r1y = lerp(q1y, q2y, t);
  PathAnchor inserted;
  inserted.anchor_x = lerp(r0x, r1x, t);
  inserted.anchor_y = lerp(r0y, r1y, t);
  inserted.in_x = r0x;
  inserted.in_y = r0y;
  inserted.out_x = r1x;
  inserted.out_y = r1y;
  inserted.smooth = true;
  a.out_x = q0x;
  a.out_y = q0y;
  b.in_x = q2x;
  b.in_y = q2y;
  return inserted;
}

// A direct edit invalidates the live-shape annotation of the touched groups
// (Photoshop's keyShapeInvalidated rule); the path stays.
void drop_origination_for_groups(VectorShapeContent& content, const std::vector<int>& groups) {
  if (groups.empty()) {
    return;
  }
  std::erase_if(content.origination, [&groups](const LiveShapeParams& params) {
    return std::find(groups.begin(), groups.end(), params.index) != groups.end();
  });
}

}  // namespace

void CanvasWidget::set_vector_path_committed_callback(
    std::function<void(patchy::VectorPath, bool, VectorPathSource)> callback) {
  vector_path_committed_callback_ = std::move(callback);
}

void CanvasWidget::set_polygon_sides(int sides) noexcept {
  polygon_sides_ = std::clamp(sides, 3, 100);
}

int CanvasWidget::polygon_sides() const noexcept {
  return polygon_sides_;
}

void CanvasWidget::set_polygon_star_inset(int percent) noexcept {
  polygon_star_inset_ = std::clamp(percent, 0, 99);
}

int CanvasWidget::polygon_star_inset() const noexcept {
  return polygon_star_inset_;
}

void CanvasWidget::set_custom_shape_path(std::shared_ptr<const patchy::VectorPath> path) {
  custom_shape_path_ = std::move(path);
}

const patchy::VectorPath* CanvasWidget::custom_shape_path() const noexcept {
  return custom_shape_path_.get();
}

// Center-out polygon/star: the first vertex points at the drag cursor.
PathSubpath CanvasWidget::polygon_drag_subpath(QPointF center, QPointF radius_point) const {
  PathSubpath subpath;
  const double radius = std::hypot(radius_point.x() - center.x(), radius_point.y() - center.y());
  if (radius < 0.5) {
    return subpath;
  }
  const double base_angle =
      std::atan2(radius_point.y() - center.y(), radius_point.x() - center.x());
  const int sides = std::clamp(polygon_sides_, 3, 100);
  const bool star = polygon_star_inset_ > 0;
  const double inner_radius = radius * (100 - polygon_star_inset_) / 100.0;
  const int point_count = star ? sides * 2 : sides;
  for (int i = 0; i < point_count; ++i) {
    const double point_radius = star && (i % 2) != 0 ? inner_radius : radius;
    const double angle =
        base_angle + i * 2.0 * std::numbers::pi / point_count;
    PathAnchor anchor;
    anchor.anchor_x = center.x() + point_radius * std::cos(angle);
    anchor.anchor_y = center.y() + point_radius * std::sin(angle);
    anchor.in_x = anchor.anchor_x;
    anchor.in_y = anchor.anchor_y;
    anchor.out_x = anchor.anchor_x;
    anchor.out_y = anchor.anchor_y;
    subpath.anchors.push_back(anchor);
  }
  return subpath;
}

void CanvasWidget::commit_polygon_drag(QPointF center, QPointF radius_point) {
  auto subpath = polygon_drag_subpath(center, radius_point);
  if (subpath.anchors.empty() || !vector_path_committed_callback_) {
    return;
  }
  VectorPath path;
  path.subpaths.push_back(std::move(subpath));
  vector_path_committed_callback_(std::move(path), true, VectorPathSource::Polygon);
}

void CanvasWidget::commit_custom_shape_drag(QRectF bounds) {
  if (custom_shape_path_ == nullptr || custom_shape_path_->empty() ||
      !vector_path_committed_callback_ || bounds.width() < 1.0 || bounds.height() < 1.0) {
    return;
  }
  auto path = *custom_shape_path_;
  transform_vector_path(path, {bounds.width(), 0.0, 0.0, bounds.height(), bounds.x(), bounds.y()});
  vector_path_committed_callback_(std::move(path), true, VectorPathSource::CustomShape);
}

bool CanvasWidget::pen_session_active() const noexcept {
  return pen_session_active_;
}

void CanvasWidget::commit_pen_path(bool closed) {
  if (!pen_session_active_) {
    return;
  }
  auto anchors = std::move(pen_anchors_);
  pen_anchors_.clear();
  pen_session_active_ = false;
  pen_handle_dragging_ = false;
  pen_session_drag_anchor_ = -1;
  update();
  const auto minimum_anchors = closed ? 3U : 2U;
  if (anchors.size() < minimum_anchors) {
    return;
  }
  PathSubpath subpath;
  subpath.anchors = std::move(anchors);
  subpath.closed = closed;
  subpath.op = PathCombineOp::Add;
  if (layer_edit_target_ == LayerEditTarget::VectorMask) {
    std::vector<PathSubpath> subpaths;
    subpaths.push_back(std::move(subpath));
    add_subpaths_to_vector_mask(std::move(subpaths), tr("Add to vector mask"));
    return;
  }
  if (!vector_path_committed_callback_) {
    return;
  }
  VectorPath path;
  path.subpaths.push_back(std::move(subpath));
  vector_path_committed_callback_(std::move(path), closed, VectorPathSource::Pen);
}

void CanvasWidget::cancel_pen_path() {
  if (!pen_session_active_) {
    return;
  }
  pen_anchors_.clear();
  pen_session_active_ = false;
  pen_handle_dragging_ = false;
  pen_session_drag_anchor_ = -1;
  update();
}

bool CanvasWidget::pen_click_closes_path(QPointF document_point) const {
  if (!pen_session_active_ || pen_anchors_.size() < 3) {
    return false;
  }
  const auto& first = pen_anchors_.front();
  const auto dx = (document_point.x() - first.anchor_x) * zoom_;
  const auto dy = (document_point.y() - first.anchor_y) * zoom_;
  return std::hypot(dx, dy) <= kPenCloseHitRadiusPx;
}

bool CanvasWidget::handle_pen_press(QMouseEvent* event, QPointF document_point) {
  if (tool_ != CanvasTool::Pen || event->button() != Qt::LeftButton) {
    return false;
  }
  if (edit_locked_) {
    show_edit_locked_message();
    return true;
  }
  const bool vector_mask_target =
      layer_edit_target_ == LayerEditTarget::VectorMask && vector_mask_target_layer() != nullptr;
  if (document_ == nullptr || quick_mask_active_ ||
      (layer_edit_target_ != LayerEditTarget::Content && !vector_mask_target)) {
    report_status_error(tr("The Pen tool draws paths on layer content"));
    return true;
  }
  // Ctrl temporarily acts as Direct Select (the classic pen accelerator):
  // adjust anchors or handles without leaving the Pen, release to keep drawing.
  if ((event->modifiers() & Qt::ControlModifier) != 0) {
    return handle_pen_ctrl_press(event, document_point);
  }
  if (pen_click_closes_path(document_point)) {
    commit_pen_path(true);
    return true;
  }
  // Clicking the target path's anchors/segments edits instead of starting a
  // new subpath (Photoshop's auto add/delete/convert pen behavior).
  if (pen_modifies_existing_path(event, document_point)) {
    return true;
  }
  const bool starting_session = !pen_session_active_;
  const auto snapped = snapped_document_point_f(document_point);
  PathAnchor anchor;
  anchor.anchor_x = snapped.x();
  anchor.anchor_y = snapped.y();
  anchor.in_x = snapped.x();
  anchor.in_y = snapped.y();
  anchor.out_x = snapped.x();
  anchor.out_y = snapped.y();
  anchor.smooth = false;
  pen_anchors_.push_back(anchor);
  pen_session_active_ = true;
  pen_handle_dragging_ = true;
  pen_handles_broken_ = false;
  pen_hover_document_ = snapped;
  if (starting_session && status_callback_) {
    status_callback_(tr("Click to add points, drag for curves. Click the first point to close; "
                        "Enter commits an open path; Esc cancels."));
  }
  update();
  return true;
}

// Ctrl+press with the Pen: Direct Select semantics without switching tools.
// Mid-session the drag moves an anchor of the in-progress path; otherwise the
// gesture latches onto the path-edit handlers until release.
bool CanvasWidget::handle_pen_ctrl_press(QMouseEvent* event, QPointF document_point) {
  if (pen_session_active_) {
    const auto index = pen_session_anchor_at(QPointF(event->position()));
    if (index >= 0) {
      pen_session_drag_anchor_ = index;
      pen_session_drag_last_document_ = document_point;
      update();
    }
    // Swallow even on a miss: Ctrl must never extend or close the path.
    return true;
  }
  if (path_edit_target_path() == nullptr) {
    return true;  // nothing to select; a modifier chord should not flash an error
  }
  pen_temp_direct_select_ = true;
  return handle_path_edit_press(event, document_point);
}

int CanvasWidget::pen_session_anchor_at(QPointF widget_point) const {
  for (int i = 0; i < static_cast<int>(pen_anchors_.size()); ++i) {
    const auto& anchor = pen_anchors_[static_cast<std::size_t>(i)];
    const auto screen = path_point_to_screen(anchor.anchor_x, anchor.anchor_y);
    if (std::hypot(screen.x() - widget_point.x(), screen.y() - widget_point.y()) <=
        kPathHitRadiusPx) {
      return i;
    }
  }
  return -1;
}

bool CanvasWidget::handle_pen_move(QMouseEvent* event, QPointF document_point) {
  if (tool_ != CanvasTool::Pen) {
    return false;
  }
  pen_hover_document_ = document_point;
  if (pen_temp_direct_select_) {
    return handle_path_edit_move(event, document_point);
  }
  if (pen_session_drag_anchor_ >= 0) {
    if (pen_session_drag_anchor_ >= static_cast<int>(pen_anchors_.size()) ||
        (event->buttons() & Qt::LeftButton) == 0) {
      pen_session_drag_anchor_ = -1;  // popped anchor or a lost release
      return true;
    }
    auto& anchor = pen_anchors_[static_cast<std::size_t>(pen_session_drag_anchor_)];
    const auto dx = document_point.x() - pen_session_drag_last_document_.x();
    const auto dy = document_point.y() - pen_session_drag_last_document_.y();
    pen_session_drag_last_document_ = document_point;
    anchor.anchor_x += dx;
    anchor.anchor_y += dy;
    anchor.in_x += dx;
    anchor.in_y += dy;
    anchor.out_x += dx;
    anchor.out_y += dy;
    update();
    return true;
  }
  if (pen_handle_dragging_ && !pen_anchors_.empty() &&
      (event->buttons() & Qt::LeftButton) != 0) {
    auto& anchor = pen_anchors_.back();
    const auto distance = std::hypot(document_point.x() - anchor.anchor_x,
                                     document_point.y() - anchor.anchor_y);
    if (distance >= kPenSmoothDragThresholdPx) {
      anchor.out_x = document_point.x();
      anchor.out_y = document_point.y();
      if ((event->modifiers() & Qt::AltModifier) != 0) {
        // Alt breaks the pair: the incoming handle keeps its position.
        pen_handles_broken_ = true;
        anchor.smooth = false;
      } else if (!pen_handles_broken_) {
        anchor.in_x = 2.0 * anchor.anchor_x - document_point.x();
        anchor.in_y = 2.0 * anchor.anchor_y - document_point.y();
        anchor.smooth = true;
      }
    }
  }
  if (pen_session_active_) {
    update();
  }
  return pen_session_active_;
}

bool CanvasWidget::handle_pen_release(QMouseEvent* event) {
  if (tool_ != CanvasTool::Pen || event->button() != Qt::LeftButton) {
    return false;
  }
  if (pen_temp_direct_select_) {
    const bool handled = handle_path_edit_release(event);
    pen_temp_direct_select_ = false;
    apply_pen_cursor(QPointF(event->position()), event->modifiers());
    return handled;
  }
  if (pen_session_drag_anchor_ >= 0) {
    pen_session_drag_anchor_ = -1;
    apply_pen_cursor(QPointF(event->position()), event->modifiers());
    update();
    return true;
  }
  pen_handle_dragging_ = false;
  return pen_session_active_;
}

bool CanvasWidget::handle_pen_key(QKeyEvent* event) {
  if (tool_ != CanvasTool::Pen || !pen_session_active_) {
    return false;
  }
  switch (event->key()) {
    case Qt::Key_Backspace:
    case Qt::Key_Delete:
      if (!pen_anchors_.empty()) {
        pen_anchors_.pop_back();
      }
      pen_session_drag_anchor_ = -1;  // the dragged anchor may just have popped
      if (pen_anchors_.empty()) {
        pen_session_active_ = false;
        pen_handle_dragging_ = false;
      }
      update();
      return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
      commit_pen_path(false);
      return true;
    case Qt::Key_Escape:
      cancel_pen_path();
      return true;
    default:
      return false;
  }
}

void CanvasWidget::draw_pen_overlay(QPainter& painter) {
  if (tool_ != CanvasTool::Pen || !pen_session_active_ || pen_anchors_.empty()) {
    return;
  }
  const auto to_screen = [this](double x, double y) {
    const auto origin = widget_position_for_document_point(QPoint(0, 0));
    return QPointF(origin.x() + x * zoom_, origin.y() + y * zoom_);
  };

  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);
  const QColor accent(116, 192, 255);

  QPainterPath outline;
  outline.moveTo(to_screen(pen_anchors_[0].anchor_x, pen_anchors_[0].anchor_y));
  for (std::size_t i = 1; i < pen_anchors_.size(); ++i) {
    const auto& previous = pen_anchors_[i - 1];
    const auto& current = pen_anchors_[i];
    outline.cubicTo(to_screen(previous.out_x, previous.out_y),
                    to_screen(current.in_x, current.in_y),
                    to_screen(current.anchor_x, current.anchor_y));
  }
  // Preview segment from the last anchor to the cursor.
  const auto& last = pen_anchors_.back();
  const auto preview_target = pen_click_closes_path(pen_hover_document_)
                                  ? QPointF(pen_anchors_.front().anchor_x,
                                            pen_anchors_.front().anchor_y)
                                  : pen_hover_document_;
  QPainterPath preview;
  preview.moveTo(to_screen(last.anchor_x, last.anchor_y));
  preview.cubicTo(to_screen(last.out_x, last.out_y),
                  to_screen(preview_target.x(), preview_target.y()),
                  to_screen(preview_target.x(), preview_target.y()));

  QPen outline_pen(accent, 1.4);
  painter.setPen(outline_pen);
  painter.setBrush(Qt::NoBrush);
  painter.drawPath(outline);
  QPen preview_pen(accent, 1.0, Qt::DashLine);
  painter.setPen(preview_pen);
  painter.drawPath(preview);

  // Handles of the anchor under construction.
  if (pen_handle_dragging_ && last.smooth) {
    painter.setPen(QPen(accent, 1.0));
    painter.drawLine(to_screen(last.in_x, last.in_y), to_screen(last.anchor_x, last.anchor_y));
    painter.drawLine(to_screen(last.anchor_x, last.anchor_y), to_screen(last.out_x, last.out_y));
    painter.setBrush(accent);
    painter.drawEllipse(to_screen(last.in_x, last.in_y), 2.5, 2.5);
    painter.drawEllipse(to_screen(last.out_x, last.out_y), 2.5, 2.5);
  }

  // Anchor squares; the first anchor doubles as the close target.
  painter.setPen(QPen(QColor(30, 34, 40), 1.0));
  for (std::size_t i = 0; i < pen_anchors_.size(); ++i) {
    const auto center = to_screen(pen_anchors_[i].anchor_x, pen_anchors_[i].anchor_y);
    painter.setBrush(i == 0 && pen_anchors_.size() >= 3 ? QBrush(Qt::white) : QBrush(accent));
    painter.drawRect(QRectF(center.x() - 2.5, center.y() - 2.5, 5.0, 5.0));
  }
  painter.restore();
}

// --- Path editing ---

bool CanvasWidget::path_edit_tool_active() const noexcept {
  return tool_ == CanvasTool::PathSelect || tool_ == CanvasTool::DirectSelect ||
         tool_ == CanvasTool::Pen;
}

CanvasTool CanvasWidget::path_edit_tool() const noexcept {
  return tool_ == CanvasTool::Pen && pen_temp_direct_select_ ? CanvasTool::DirectSelect : tool_;
}

Layer* CanvasWidget::path_edit_target_layer() const {
  if (document_ == nullptr) {
    return nullptr;
  }
  const auto active = document_->active_layer_id();
  if (!active.has_value()) {
    return nullptr;
  }
  auto* layer = document_->find_layer(*active);
  if (layer != nullptr && layer_is_vector_shape(*layer) && vector_lock_reason(*layer).empty()) {
    return layer;
  }
  return nullptr;
}

void CanvasWidget::set_active_document_path(std::optional<DocumentPathId> id) {
  if (active_document_path_ == id) {
    return;
  }
  active_document_path_ = id;
  clear_path_edit_selection();
  update();
}

std::optional<DocumentPathId> CanvasWidget::active_document_path() const noexcept {
  return active_document_path_;
}

void CanvasWidget::set_panel_path_targeted(bool targeted) {
  if (panel_path_targeted_ == targeted) {
    return;
  }
  panel_path_targeted_ = targeted;
  update();
}

bool CanvasWidget::panel_path_targeted() const noexcept {
  return panel_path_targeted_;
}

void CanvasWidget::set_path_display_dismiss_callback(std::function<void()> callback) {
  path_display_dismiss_callback_ = std::move(callback);
}

const VectorPath* CanvasWidget::path_edit_target_path() const {
  if (layer_edit_target_ == LayerEditTarget::VectorMask) {
    if (const auto* layer = vector_mask_target_layer(); layer != nullptr) {
      return &layer->vector_mask()->path;
    }
    return nullptr;
  }
  if (active_document_path_.has_value() && document_ != nullptr) {
    if (const auto* path = std::as_const(*document_).find_path(*active_document_path_);
        path != nullptr) {
      return &path->path();
    }
  }
  if (const auto* layer = path_edit_target_layer(); layer != nullptr) {
    return &layer->vector_shape()->path;
  }
  if (document_ != nullptr) {
    if (const auto* work = document_->work_path(); work != nullptr) {
      return &work->path();
    }
  }
  return nullptr;
}

Layer* CanvasWidget::vector_mask_target_layer() const {
  if (document_ == nullptr) {
    return nullptr;
  }
  const auto active = document_->active_layer_id();
  if (!active.has_value()) {
    return nullptr;
  }
  auto* layer = document_->find_layer(*active);
  if (layer != nullptr && layer->vector_mask() != nullptr && vector_lock_reason(*layer).empty()) {
    return layer;
  }
  return nullptr;
}

void CanvasWidget::add_subpaths_to_vector_mask(std::vector<PathSubpath> subpaths,
                                               const QString& label) {
  auto* layer = vector_mask_target_layer();
  if (layer == nullptr || subpaths.empty() || document_ == nullptr) {
    return;
  }
  if (before_edit_callback_) {
    before_edit_callback_(label);
  }
  auto mask = *layer->vector_mask();
  const auto group = mask.path.next_shape_group();
  for (auto& subpath : subpaths) {
    subpath.shape_group = group;
    mask.path.subpaths.push_back(std::move(subpath));
  }
  layer->set_vector_mask(std::move(mask));
  mark_layer_vector_block_dirty(*layer);
  update_vector_mask_raster(*layer, Rect::from_size(document_->width(), document_->height()));
  document_changed();
}

void CanvasWidget::apply_path_edit(VectorPath path, const QString& label,
                                   const std::vector<int>& touched_groups) {
  if (document_ == nullptr) {
    return;
  }
  if (!path_edit_undo_armed_) {
    if (before_edit_callback_) {
      before_edit_callback_(label);
    }
    path_edit_undo_armed_ = true;
  }
  if (layer_edit_target_ == LayerEditTarget::VectorMask) {
    if (auto* layer = vector_mask_target_layer(); layer != nullptr) {
      auto mask = *layer->vector_mask();
      mask.path = std::move(path);
      layer->set_vector_mask(std::move(mask));
      mark_layer_vector_block_dirty(*layer);
      update_vector_mask_raster(*layer, Rect::from_size(document_->width(), document_->height()));
      document_changed();
    }
    return;
  }
  if (active_document_path_.has_value()) {
    if (auto* target = document_->find_path(*active_document_path_); target != nullptr) {
      target->set_path(std::move(path));
      update();
    }
    return;
  }
  if (auto* layer = path_edit_target_layer(); layer != nullptr) {
    auto content = *layer->vector_shape();
    content.path = std::move(path);
    drop_origination_for_groups(content, touched_groups);
    layer->set_vector_shape(std::move(content));
    layer->metadata()[kLayerMetadataVectorRasterStatus] = kVectorRasterStatusPatchy;
    mark_layer_vector_block_dirty(*layer);
    update_vector_shape_raster(*layer, Rect::from_size(document_->width(), document_->height()),
                               &document_->metadata().patterns);
    document_changed();
  } else if (auto* work = document_->work_path(); work != nullptr) {
    work->set_path(std::move(path));
    update();
  }
}

QPointF CanvasWidget::path_point_to_screen(double x, double y) const {
  const auto origin = widget_position_for_document_point(QPoint(0, 0));
  return QPointF(origin.x() + x * zoom_, origin.y() + y * zoom_);
}

std::pair<int, int> CanvasWidget::path_anchor_at(QPointF widget_point) const {
  const auto* path = path_edit_target_path();
  if (path == nullptr) {
    return {-1, -1};
  }
  for (int s = 0; s < static_cast<int>(path->subpaths.size()); ++s) {
    const auto& anchors = path->subpaths[static_cast<std::size_t>(s)].anchors;
    for (int a = 0; a < static_cast<int>(anchors.size()); ++a) {
      const auto screen = path_point_to_screen(anchors[static_cast<std::size_t>(a)].anchor_x,
                                               anchors[static_cast<std::size_t>(a)].anchor_y);
      if (std::hypot(screen.x() - widget_point.x(), screen.y() - widget_point.y()) <=
          kPathHitRadiusPx) {
        return {s, a};
      }
    }
  }
  return {-1, -1};
}

int CanvasWidget::path_handle_at(QPointF widget_point, std::pair<int, int>& anchor) const {
  const auto* path = path_edit_target_path();
  if (path == nullptr) {
    return 0;
  }
  // Handles are only visible (and grabbable) on selected anchors.
  for (const auto& key : path_selected_anchors_) {
    const auto s = static_cast<std::size_t>(key.first);
    const auto a = static_cast<std::size_t>(key.second);
    if (s >= path->subpaths.size() || a >= path->subpaths[s].anchors.size()) {
      continue;
    }
    const auto& anchor_data = path->subpaths[s].anchors[a];
    // A collapsed handle (corner anchors keep in == out == anchor) sits on the
    // anchor square; it is not grabbable there - the anchor drag wins.
    const auto anchor_screen = path_point_to_screen(anchor_data.anchor_x, anchor_data.anchor_y);
    const auto grabbable = [&](QPointF handle_screen) {
      return std::hypot(handle_screen.x() - anchor_screen.x(),
                        handle_screen.y() - anchor_screen.y()) > 0.5 &&
             std::hypot(handle_screen.x() - widget_point.x(),
                        handle_screen.y() - widget_point.y()) <= kPathHitRadiusPx;
    };
    if (grabbable(path_point_to_screen(anchor_data.in_x, anchor_data.in_y))) {
      anchor = key;
      return -1;
    }
    if (grabbable(path_point_to_screen(anchor_data.out_x, anchor_data.out_y))) {
      anchor = key;
      return 1;
    }
  }
  return 0;
}

bool CanvasWidget::path_segment_at(QPointF widget_point, std::pair<int, int>& segment,
                                   double& segment_t) const {
  const auto* path = path_edit_target_path();
  if (path == nullptr) {
    return false;
  }
  double best_distance = kPathHitRadiusPx;
  bool found = false;
  for (int s = 0; s < static_cast<int>(path->subpaths.size()); ++s) {
    const auto& subpath = path->subpaths[static_cast<std::size_t>(s)];
    const auto anchor_count = static_cast<int>(subpath.anchors.size());
    if (anchor_count < 2) {
      continue;
    }
    const auto segment_count = subpath.closed ? anchor_count : anchor_count - 1;
    for (int i = 0; i < segment_count; ++i) {
      const auto& a = subpath.anchors[static_cast<std::size_t>(i)];
      const auto& b = subpath.anchors[static_cast<std::size_t>((i + 1) % anchor_count)];
      // Sample the cubic in screen space; 24 steps is plenty at hit precision.
      QPointF previous = path_point_to_screen(a.anchor_x, a.anchor_y);
      for (int step = 1; step <= 24; ++step) {
        const double t = static_cast<double>(step) / 24.0;
        const double u = 1.0 - t;
        const double x = u * u * u * a.anchor_x + 3 * u * u * t * a.out_x + 3 * u * t * t * b.in_x +
                         t * t * t * b.anchor_x;
        const double y = u * u * u * a.anchor_y + 3 * u * u * t * a.out_y + 3 * u * t * t * b.in_y +
                         t * t * t * b.anchor_y;
        const auto current = path_point_to_screen(x, y);
        // Distance from the click to this sample segment's midpoint suffices
        // at this sampling density.
        const auto mid = (previous + current) / 2.0;
        const auto distance = std::hypot(mid.x() - widget_point.x(), mid.y() - widget_point.y());
        if (distance < best_distance) {
          best_distance = distance;
          segment = {s, i};
          segment_t = t - 0.5 / 24.0;
          found = true;
        }
        previous = current;
      }
    }
  }
  return found;
}

bool CanvasWidget::handle_path_edit_press(QMouseEvent* event, QPointF document_point) {
  const auto edit_tool = path_edit_tool();
  if ((edit_tool != CanvasTool::PathSelect && edit_tool != CanvasTool::DirectSelect) ||
      event->button() != Qt::LeftButton) {
    return false;
  }
  if (edit_locked_) {
    show_edit_locked_message();
    return true;
  }
  const auto* path = path_edit_target_path();
  if (path == nullptr) {
    report_status_error(tr("Select a shape layer or draw a path first"));
    return true;
  }
  path_edit_undo_armed_ = false;
  path_edit_changed_ = false;
  path_drag_last_document_ = document_point;

  const auto widget_point = QPointF(event->position());
  if (edit_tool == CanvasTool::DirectSelect) {
    std::pair<int, int> handle_anchor{-1, -1};
    if (const auto side = path_handle_at(widget_point, handle_anchor); side != 0) {
      path_drag_mode_ = side < 0 ? PathEditDrag::HandleIn : PathEditDrag::HandleOut;
      path_drag_anchor_ = handle_anchor;
      update();
      return true;
    }
  }
  if (const auto anchor = path_anchor_at(widget_point); anchor.first >= 0) {
    const bool additive = (event->modifiers() & Qt::ShiftModifier) != 0;
    if (edit_tool == CanvasTool::PathSelect) {
      // Whole shape-group selection.
      const auto group =
          path->subpaths[static_cast<std::size_t>(anchor.first)].shape_group;
      if (!additive) {
        path_selected_anchors_.clear();
      }
      for (int s = 0; s < static_cast<int>(path->subpaths.size()); ++s) {
        if (path->subpaths[static_cast<std::size_t>(s)].shape_group != group) {
          continue;
        }
        for (int a = 0; a < static_cast<int>(path->subpaths[static_cast<std::size_t>(s)].anchors.size());
             ++a) {
          path_selected_anchors_.insert({s, a});
        }
      }
    } else {
      if (additive) {
        if (path_selected_anchors_.contains(anchor)) {
          path_selected_anchors_.erase(anchor);
        } else {
          path_selected_anchors_.insert(anchor);
        }
      } else if (!path_selected_anchors_.contains(anchor)) {
        path_selected_anchors_ = {anchor};
      }
    }
    path_drag_mode_ = PathEditDrag::Anchors;
    path_drag_anchor_ = anchor;
    update();
    return true;
  }
  std::pair<int, int> segment{-1, -1};
  double segment_t = 0.0;
  if (path_segment_at(widget_point, segment, segment_t)) {
    const auto& subpath = path->subpaths[static_cast<std::size_t>(segment.first)];
    const auto anchor_count = static_cast<int>(subpath.anchors.size());
    if (edit_tool == CanvasTool::PathSelect) {
      const auto group = subpath.shape_group;
      path_selected_anchors_.clear();
      for (int s = 0; s < static_cast<int>(path->subpaths.size()); ++s) {
        if (path->subpaths[static_cast<std::size_t>(s)].shape_group != group) {
          continue;
        }
        for (int a = 0; a < static_cast<int>(path->subpaths[static_cast<std::size_t>(s)].anchors.size());
             ++a) {
          path_selected_anchors_.insert({s, a});
        }
      }
    } else {
      path_selected_anchors_ = {{segment.first, segment.second},
                                {segment.first, (segment.second + 1) % anchor_count}};
    }
    path_drag_mode_ = PathEditDrag::Anchors;
    path_drag_anchor_ = segment;
    update();
    return true;
  }
  // Empty space: marquee selection.
  path_drag_mode_ = PathEditDrag::Marquee;
  path_marquee_start_ = document_point;
  path_marquee_current_ = document_point;
  if ((event->modifiers() & Qt::ShiftModifier) == 0) {
    path_selected_anchors_.clear();
  }
  update();
  return true;
}

bool CanvasWidget::handle_path_edit_move(QMouseEvent* event, QPointF document_point) {
  const auto edit_tool = path_edit_tool();
  if (edit_tool != CanvasTool::PathSelect && edit_tool != CanvasTool::DirectSelect) {
    return false;
  }
  if (path_drag_mode_ == PathEditDrag::None || (event->buttons() & Qt::LeftButton) == 0) {
    return true;  // hover only
  }
  if (path_drag_mode_ == PathEditDrag::Marquee) {
    path_marquee_current_ = document_point;
    update();
    return true;
  }
  const auto* path = path_edit_target_path();
  if (path == nullptr) {
    return true;
  }
  const auto dx = document_point.x() - path_drag_last_document_.x();
  const auto dy = document_point.y() - path_drag_last_document_.y();
  if (dx == 0.0 && dy == 0.0) {
    return true;
  }
  path_drag_last_document_ = document_point;
  prune_path_edit_selection(*path);
  const auto drag_anchor_valid =
      path_drag_anchor_.first >= 0 &&
      path_drag_anchor_.first < static_cast<int>(path->subpaths.size()) &&
      path_drag_anchor_.second >= 0 &&
      path_drag_anchor_.second <
          static_cast<int>(
              path->subpaths[static_cast<std::size_t>(path_drag_anchor_.first)].anchors.size());
  if (path_drag_mode_ != PathEditDrag::Anchors && !drag_anchor_valid) {
    return true;  // the dragged anchor vanished under an outside edit
  }
  auto working = *path;
  std::vector<int> touched_groups;
  const auto touch_group = [&working, &touched_groups](int subpath_index) {
    const auto group = working.subpaths[static_cast<std::size_t>(subpath_index)].shape_group;
    if (std::find(touched_groups.begin(), touched_groups.end(), group) == touched_groups.end()) {
      touched_groups.push_back(group);
    }
  };
  if (path_drag_mode_ == PathEditDrag::Anchors) {
    for (const auto& key : path_selected_anchors_) {
      auto& anchor = working.subpaths[static_cast<std::size_t>(key.first)]
                         .anchors[static_cast<std::size_t>(key.second)];
      anchor.anchor_x += dx;
      anchor.anchor_y += dy;
      anchor.in_x += dx;
      anchor.in_y += dy;
      anchor.out_x += dx;
      anchor.out_y += dy;
      touch_group(key.first);
    }
  } else {
    auto& anchor = working.subpaths[static_cast<std::size_t>(path_drag_anchor_.first)]
                       .anchors[static_cast<std::size_t>(path_drag_anchor_.second)];
    if (path_drag_mode_ == PathEditDrag::HandleIn) {
      anchor.in_x += dx;
      anchor.in_y += dy;
      if (anchor.smooth) {
        anchor.out_x = 2.0 * anchor.anchor_x - anchor.in_x;
        anchor.out_y = 2.0 * anchor.anchor_y - anchor.in_y;
      }
    } else {
      anchor.out_x += dx;
      anchor.out_y += dy;
      if (anchor.smooth) {
        anchor.in_x = 2.0 * anchor.anchor_x - anchor.out_x;
        anchor.in_y = 2.0 * anchor.anchor_y - anchor.out_y;
      }
    }
    touch_group(path_drag_anchor_.first);
  }
  path_edit_changed_ = true;
  apply_path_edit(std::move(working),
                  edit_tool == CanvasTool::PathSelect ? tr("Move shape") : tr("Edit path"),
                  touched_groups);
  return true;
}

bool CanvasWidget::handle_path_edit_release(QMouseEvent* event) {
  const auto edit_tool = path_edit_tool();
  if (edit_tool != CanvasTool::PathSelect && edit_tool != CanvasTool::DirectSelect) {
    return false;
  }
  if (event->button() != Qt::LeftButton) {
    return true;
  }
  if (path_drag_mode_ == PathEditDrag::Marquee) {
    const auto* path = path_edit_target_path();
    if (path != nullptr) {
      const auto rect = QRectF(path_marquee_start_, path_marquee_current_).normalized();
      std::set<int> groups_in_box;
      for (int s = 0; s < static_cast<int>(path->subpaths.size()); ++s) {
        const auto& subpath = path->subpaths[static_cast<std::size_t>(s)];
        for (int a = 0; a < static_cast<int>(subpath.anchors.size()); ++a) {
          const auto& anchor = subpath.anchors[static_cast<std::size_t>(a)];
          if (rect.contains(QPointF(anchor.anchor_x, anchor.anchor_y))) {
            if (edit_tool == CanvasTool::DirectSelect) {
              path_selected_anchors_.insert({s, a});
            } else {
              groups_in_box.insert(subpath.shape_group);
            }
          }
        }
      }
      if (edit_tool == CanvasTool::PathSelect) {
        for (int s = 0; s < static_cast<int>(path->subpaths.size()); ++s) {
          if (!groups_in_box.contains(path->subpaths[static_cast<std::size_t>(s)].shape_group)) {
            continue;
          }
          for (int a = 0;
               a < static_cast<int>(path->subpaths[static_cast<std::size_t>(s)].anchors.size());
               ++a) {
            path_selected_anchors_.insert({s, a});
          }
        }
      }
    }
  }
  path_drag_mode_ = PathEditDrag::None;
  update();
  return true;
}

void CanvasWidget::prune_path_edit_selection(const VectorPath& path) {
  std::erase_if(path_selected_anchors_, [&path](const std::pair<int, int>& key) {
    return key.first < 0 || key.first >= static_cast<int>(path.subpaths.size()) ||
           key.second < 0 ||
           key.second >= static_cast<int>(
               path.subpaths[static_cast<std::size_t>(key.first)].anchors.size());
  });
}

void CanvasWidget::delete_selected_path_anchors() {
  const auto* path = path_edit_target_path();
  if (path == nullptr) {
    return;
  }
  prune_path_edit_selection(*path);
  if (path_selected_anchors_.empty()) {
    return;
  }
  auto working = *path;
  std::vector<int> touched_groups;
  // Erase in reverse index order so earlier keys stay valid.
  for (auto it = path_selected_anchors_.rbegin(); it != path_selected_anchors_.rend(); ++it) {
    auto& subpath = working.subpaths[static_cast<std::size_t>(it->first)];
    subpath.anchors.erase(subpath.anchors.begin() + it->second);
    if (std::find(touched_groups.begin(), touched_groups.end(), subpath.shape_group) ==
        touched_groups.end()) {
      touched_groups.push_back(subpath.shape_group);
    }
  }
  std::erase_if(working.subpaths,
                [](const PathSubpath& subpath) { return subpath.anchors.size() < 2; });
  path_selected_anchors_.clear();
  path_edit_undo_armed_ = false;
  apply_path_edit(std::move(working), tr("Delete anchors"), touched_groups);
  path_edit_undo_armed_ = false;
}

bool CanvasWidget::handle_path_edit_key(QKeyEvent* event) {
  if (!path_edit_tool_active()) {
    return false;
  }
  if (path_selected_anchors_.empty() || tool_ == CanvasTool::Pen) {
    // Photoshop's second-stage Escape: with no anchors selected (and no pen
    // session - handle_pen_key already consumed that), Esc dismisses the
    // targeted path display via the Paths panel. Sessions whose own Escape
    // handlers run later in keyPressEvent (guide drags, warp/free transform,
    // text-rect drags) keep priority - never swallow their cancel key.
    if (event->key() == Qt::Key_Escape && panel_path_targeted_ &&
        path_display_dismiss_callback_ && !dragging_guide_ && !warping_layer_ &&
        !transforming_layer_ && !dragging_text_rect_) {
      path_display_dismiss_callback_();
      return true;
    }
    return false;
  }
  switch (event->key()) {
    case Qt::Key_Backspace:
    case Qt::Key_Delete:
      delete_selected_path_anchors();
      return true;
    case Qt::Key_Escape:
      clear_path_edit_selection();
      return true;
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Up:
    case Qt::Key_Down: {
      const auto* path = path_edit_target_path();
      if (path == nullptr) {
        return true;
      }
      prune_path_edit_selection(*path);
      if (path_selected_anchors_.empty()) {
        return true;
      }
      const double step = (event->modifiers() & Qt::ShiftModifier) != 0 ? 10.0 : 1.0;
      const double dx = event->key() == Qt::Key_Left ? -step
                        : event->key() == Qt::Key_Right ? step
                                                        : 0.0;
      const double dy = event->key() == Qt::Key_Up ? -step
                        : event->key() == Qt::Key_Down ? step
                                                       : 0.0;
      // Coalesce a burst of nudges into one undo entry.
      const auto now = QDateTime::currentMSecsSinceEpoch();
      if (now - path_nudge_last_ms_ > kPathNudgeCoalesceMs) {
        path_edit_undo_armed_ = false;
      }
      path_nudge_last_ms_ = now;
      auto working = *path;
      std::vector<int> touched_groups;
      for (const auto& key : path_selected_anchors_) {
        auto& anchor = working.subpaths[static_cast<std::size_t>(key.first)]
                           .anchors[static_cast<std::size_t>(key.second)];
        anchor.anchor_x += dx;
        anchor.anchor_y += dy;
        anchor.in_x += dx;
        anchor.in_y += dy;
        anchor.out_x += dx;
        anchor.out_y += dy;
        const auto group = working.subpaths[static_cast<std::size_t>(key.first)].shape_group;
        if (std::find(touched_groups.begin(), touched_groups.end(), group) ==
            touched_groups.end()) {
          touched_groups.push_back(group);
        }
      }
      apply_path_edit(std::move(working), tr("Nudge anchors"), touched_groups);
      return true;
    }
    default:
      return false;
  }
}

CanvasWidget::PenHoverHit CanvasWidget::pen_hover_hit(QPointF widget_point, QPointF document_point,
                                                      Qt::KeyboardModifiers modifiers) const {
  PenHoverHit hit;
  if (pen_click_closes_path(document_point)) {
    hit.action = PenHoverAction::Close;
    return hit;
  }
  if (pen_session_active_) {
    return hit;  // mid-session clicks always extend the path
  }
  if (path_edit_target_path() == nullptr) {
    return hit;
  }
  if (hit.anchor = path_anchor_at(widget_point); hit.anchor.first >= 0) {
    hit.action = (modifiers & Qt::AltModifier) != 0 ? PenHoverAction::Convert
                                                    : PenHoverAction::Delete;
    return hit;
  }
  if (path_segment_at(widget_point, hit.segment, hit.segment_t)) {
    hit.action = PenHoverAction::Add;  // Alt deliberately ignored over segments
  }
  return hit;
}

bool CanvasWidget::pen_modifies_existing_path(QMouseEvent* event, QPointF document_point) {
  const auto hit = pen_hover_hit(QPointF(event->position()), document_point, event->modifiers());
  const auto* path = path_edit_target_path();
  if (path == nullptr) {
    return false;
  }
  switch (hit.action) {
    case PenHoverAction::Convert: {
      // Convert point: smooth <-> corner.
      auto working = *path;
      auto& subpath = working.subpaths[static_cast<std::size_t>(hit.anchor.first)];
      const auto group = subpath.shape_group;
      path_edit_undo_armed_ = false;
      auto& anchor_data = subpath.anchors[static_cast<std::size_t>(hit.anchor.second)];
      if (anchor_data.smooth || anchor_data.in_x != anchor_data.anchor_x ||
          anchor_data.in_y != anchor_data.anchor_y || anchor_data.out_x != anchor_data.anchor_x ||
          anchor_data.out_y != anchor_data.anchor_y) {
        anchor_data.smooth = false;
        anchor_data.in_x = anchor_data.anchor_x;
        anchor_data.in_y = anchor_data.anchor_y;
        anchor_data.out_x = anchor_data.anchor_x;
        anchor_data.out_y = anchor_data.anchor_y;
      } else {
        // Corner -> smooth: derive handles from the neighbor direction.
        const auto anchor_count = static_cast<int>(subpath.anchors.size());
        const auto& previous =
            subpath.anchors[static_cast<std::size_t>((hit.anchor.second + anchor_count - 1) % anchor_count)];
        const auto& next =
            subpath.anchors[static_cast<std::size_t>((hit.anchor.second + 1) % anchor_count)];
        const double tangent_x = (next.anchor_x - previous.anchor_x) / 6.0;
        const double tangent_y = (next.anchor_y - previous.anchor_y) / 6.0;
        anchor_data.smooth = true;
        anchor_data.in_x = anchor_data.anchor_x - tangent_x;
        anchor_data.in_y = anchor_data.anchor_y - tangent_y;
        anchor_data.out_x = anchor_data.anchor_x + tangent_x;
        anchor_data.out_y = anchor_data.anchor_y + tangent_y;
      }
      apply_path_edit(std::move(working), tr("Convert point"), {group});
      return true;
    }
    case PenHoverAction::Delete: {
      // Delete the clicked anchor (Photoshop's auto delete-anchor).
      auto working = *path;
      auto& subpath = working.subpaths[static_cast<std::size_t>(hit.anchor.first)];
      const auto group = subpath.shape_group;
      path_edit_undo_armed_ = false;
      subpath.anchors.erase(subpath.anchors.begin() + hit.anchor.second);
      if (subpath.anchors.size() < 2) {
        working.subpaths.erase(working.subpaths.begin() + hit.anchor.first);
      }
      path_selected_anchors_.clear();
      apply_path_edit(std::move(working), tr("Delete anchor"), {group});
      return true;
    }
    case PenHoverAction::Add: {
      auto working = *path;
      auto& subpath = working.subpaths[static_cast<std::size_t>(hit.segment.first)];
      const auto group = subpath.shape_group;
      const auto anchor_count = static_cast<int>(subpath.anchors.size());
      auto& a = subpath.anchors[static_cast<std::size_t>(hit.segment.second)];
      auto& b = subpath.anchors[static_cast<std::size_t>((hit.segment.second + 1) % anchor_count)];
      const auto inserted = split_segment_anchor(a, b, std::clamp(hit.segment_t, 0.05, 0.95));
      subpath.anchors.insert(subpath.anchors.begin() + hit.segment.second + 1, inserted);
      path_edit_undo_armed_ = false;
      apply_path_edit(std::move(working), tr("Add anchor"), {group});
      return true;
    }
    default:
      return false;  // Draw and Close: the press handler owns those
  }
}

bool CanvasWidget::path_edit_has_selection() const noexcept {
  return !path_selected_anchors_.empty();
}

std::vector<int> CanvasWidget::path_edit_selected_groups() const {
  std::vector<int> groups;
  const auto* path = path_edit_target_path();
  if (path == nullptr) {
    return groups;
  }
  for (const auto& key : path_selected_anchors_) {
    if (key.first < 0 || key.first >= static_cast<int>(path->subpaths.size())) {
      continue;
    }
    const auto group = path->subpaths[static_cast<std::size_t>(key.first)].shape_group;
    if (std::find(groups.begin(), groups.end(), group) == groups.end()) {
      groups.push_back(group);
    }
  }
  return groups;
}

void CanvasWidget::set_selected_subpaths_combine_op(PathCombineOp op) {
  const auto* path = path_edit_target_path();
  if (path == nullptr || path_selected_anchors_.empty()) {
    return;
  }
  const auto groups = path_edit_selected_groups();
  auto working = *path;
  bool changed = false;
  for (auto& subpath : working.subpaths) {
    if (std::find(groups.begin(), groups.end(), subpath.shape_group) != groups.end() &&
        subpath.op != op) {
      subpath.op = op;
      changed = true;
    }
  }
  if (!changed) {
    return;
  }
  path_edit_undo_armed_ = false;
  apply_path_edit(std::move(working), tr("Change shape combine mode"), {});
  path_edit_undo_armed_ = false;
}

void CanvasWidget::clear_path_edit_selection() {
  if (!path_selected_anchors_.empty()) {
    path_selected_anchors_.clear();
    update();
  }
}

void CanvasWidget::draw_path_edit_overlay(QPainter& painter) {
  // The outline draws with ANY tool while a Paths-panel row is targeted
  // (Photoshop's target-path display); anchors, handles, and the marquee are
  // editing surfaces and stay path-tool-only.
  const bool editing = path_edit_tool_active();
  if ((!editing && !panel_path_targeted_) || pen_session_active_) {
    return;
  }
  const auto* path = path_edit_target_path();
  if (path == nullptr || path->subpaths.empty()) {
    return;
  }
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);
  const QColor accent(116, 192, 255);

  QPainterPath outline;
  for (const auto& subpath : path->subpaths) {
    if (subpath.anchors.empty()) {
      continue;
    }
    outline.moveTo(path_point_to_screen(subpath.anchors[0].anchor_x, subpath.anchors[0].anchor_y));
    const auto anchor_count = subpath.anchors.size();
    const auto segment_count = subpath.closed ? anchor_count : anchor_count - 1;
    for (std::size_t i = 0; i < segment_count; ++i) {
      const auto& a = subpath.anchors[i];
      const auto& b = subpath.anchors[(i + 1) % anchor_count];
      outline.cubicTo(path_point_to_screen(a.out_x, a.out_y), path_point_to_screen(b.in_x, b.in_y),
                      path_point_to_screen(b.anchor_x, b.anchor_y));
    }
  }
  painter.setPen(QPen(accent, 1.2));
  painter.setBrush(Qt::NoBrush);
  painter.drawPath(outline);

  if (!editing) {
    painter.restore();
    return;
  }

  // Handles of selected anchors (DirectSelect editing surface).
  painter.setPen(QPen(accent, 1.0));
  for (const auto& key : path_selected_anchors_) {
    const auto s = static_cast<std::size_t>(key.first);
    const auto a = static_cast<std::size_t>(key.second);
    if (s >= path->subpaths.size() || a >= path->subpaths[s].anchors.size()) {
      continue;
    }
    const auto& anchor = path->subpaths[s].anchors[a];
    const auto center = path_point_to_screen(anchor.anchor_x, anchor.anchor_y);
    const auto in_screen = path_point_to_screen(anchor.in_x, anchor.in_y);
    const auto out_screen = path_point_to_screen(anchor.out_x, anchor.out_y);
    painter.drawLine(in_screen, center);
    painter.drawLine(center, out_screen);
    painter.setBrush(accent);
    painter.drawEllipse(in_screen, 2.5, 2.5);
    painter.drawEllipse(out_screen, 2.5, 2.5);
    painter.setBrush(Qt::NoBrush);
  }

  // Anchor squares: filled when selected, hollow otherwise.
  for (int s = 0; s < static_cast<int>(path->subpaths.size()); ++s) {
    const auto& subpath = path->subpaths[static_cast<std::size_t>(s)];
    for (int a = 0; a < static_cast<int>(subpath.anchors.size()); ++a) {
      const auto center = path_point_to_screen(subpath.anchors[static_cast<std::size_t>(a)].anchor_x,
                                               subpath.anchors[static_cast<std::size_t>(a)].anchor_y);
      const bool selected = path_selected_anchors_.contains({s, a});
      painter.setPen(QPen(selected ? accent : QColor(30, 34, 40), 1.0));
      painter.setBrush(selected ? QBrush(accent) : QBrush(Qt::white));
      painter.drawRect(QRectF(center.x() - 2.5, center.y() - 2.5, 5.0, 5.0));
    }
  }

  if (path_drag_mode_ == PathEditDrag::Marquee) {
    painter.setPen(QPen(accent, 1.0, Qt::DashLine));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(QRectF(path_point_to_screen(path_marquee_start_.x(), path_marquee_start_.y()),
                            path_point_to_screen(path_marquee_current_.x(),
                                                 path_marquee_current_.y())));
  }
  painter.restore();
}

}  // namespace patchy::ui
