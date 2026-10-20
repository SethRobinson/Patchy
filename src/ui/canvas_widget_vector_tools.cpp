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
#include "core/layer.hpp"
#include "core/layer_metadata.hpp"
#include "core/layer_render_utils.hpp"
#include "core/text_area.hpp"
#include "core/vector_live_shapes.hpp"
#include "core/vector_raster.hpp"
#include "core/vector_shape.hpp"
#include "ui/canvas_types.hpp"
#include "ui/main_window_shared.hpp"
#include "ui/modifier_names.hpp"
#include "ui/qt_geometry.hpp"
#include "ui/tool_cursors.hpp"

#include <QAction>
#include <QBrush>
#include <QColor>
#include <QDateTime>
#include <QKeyEvent>
#include <QLatin1String>
#include <QLineF>
#include <QMenu>
#include <QMouseEvent>
#include <QPaintDevice>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPoint>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QScopeGuard>
#include <QString>
#include <QTransform>
#include <Qt>
#include <QtGlobal>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <qlatin1stringview.h>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

constexpr double kPenCloseHitRadiusPx = 8.0;   // screen pixels
constexpr double kPenSmoothDragThresholdPx = 2.0;  // document pixels
constexpr double kPathHitRadiusPx = 7.0;       // half of the 14 px hit rect
// Handle knobs grab from a little farther than anchors (GitHub issue 105: the
// knobs were too small to hit); a press closer to the anchor stays the anchor's.
constexpr double kHandleHitRadiusPx = 9.0;
constexpr qint64 kPathNudgeCoalesceMs = 800;

// One subpath in document space; contains() closes an open one implicitly.
QPainterPath subpath_painter_path(const PathSubpath& subpath) {
  QPainterPath outline;
  if (subpath.anchors.empty()) {
    return outline;
  }
  outline.moveTo(subpath.anchors[0].anchor_x, subpath.anchors[0].anchor_y);
  const auto anchor_count = subpath.anchors.size();
  const auto segment_count = subpath.closed ? anchor_count : anchor_count - 1;
  for (std::size_t i = 0; i < segment_count; ++i) {
    const auto& a = subpath.anchors[i];
    const auto& b = subpath.anchors[(i + 1) % anchor_count];
    outline.cubicTo(a.out_x, a.out_y, b.in_x, b.in_y, b.anchor_x, b.anchor_y);
  }
  if (subpath.closed) {
    outline.closeSubpath();
  }
  return outline;
}

// The topmost shape group whose filled outline contains the document point
// (GitHub issue 248: a Path Select press inside a shape grabs it), or nullopt.
std::optional<int> shape_group_containing(const VectorPath& path, QPointF document_point) {
  for (auto it = path.subpaths.rbegin(); it != path.subpaths.rend(); ++it) {
    if (it->anchors.size() >= 3 && subpath_painter_path(*it).contains(document_point)) {
      return it->shape_group;
    }
  }
  return std::nullopt;
}

// True when a marquee rect touches the subpath: its outline crosses the rect,
// or (closed) its fill overlaps it.
bool subpath_touches_rect(const PathSubpath& subpath, const QRectF& rect) {
  const auto outline = subpath_painter_path(subpath);
  if (subpath.closed) {
    return outline.intersects(rect);
  }
  const QLineF edges[] = {QLineF(rect.topLeft(), rect.topRight()), QLineF(rect.topRight(), rect.bottomRight()),
                          QLineF(rect.bottomRight(), rect.bottomLeft()),
                          QLineF(rect.bottomLeft(), rect.topLeft())};
  for (const auto& polygon : outline.toSubpathPolygons()) {
    for (int i = 0; i < polygon.size(); ++i) {
      if (rect.contains(polygon[i])) {
        return true;
      }
      if (i + 1 < polygon.size()) {
        const QLineF piece(polygon[i], polygon[i + 1]);
        for (const auto& edge : edges) {
          if (piece.intersects(edge, nullptr) == QLineF::BoundedIntersection) {
            return true;
          }
        }
      }
    }
  }
  return false;
}

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

}  // namespace

void CanvasWidget::set_vector_path_committed_callback(
    std::function<void(patchy::VectorPath, bool, VectorPathSource)> callback) {
  vector_path_committed_callback_ = std::move(callback);
}

void CanvasWidget::set_shape_preview_appearance_callback(
    std::function<std::optional<ShapePreviewAppearance>()> callback) {
  shape_preview_appearance_callback_ = std::move(callback);
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
  const auto dx = radius_point.x() - center.x(), dy = radius_point.y() - center.y();
  return generate_polygon_subpath(center.x(), center.y(), std::hypot(dx, dy),
      std::atan2(dy, dx), polygon_sides_, polygon_star_inset_);
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
  if (!pen_family_tool_active() || event->button() != Qt::LeftButton) {
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
  if (tool_ != CanvasTool::Pen) {
    return true;  // the anchor tools never draw: a miss does nothing (Photoshop)
  }
  const bool starting_session = !pen_session_active_;
  if (starting_session) {
    // A leftover Ctrl selection must not keep Delete/Backspace targeting
    // committed anchors while the session owns those keys.
    clear_path_edit_selection();
  }
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
  if (!pen_family_tool_active()) {
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
  if (!pen_family_tool_active() || event->button() != Qt::LeftButton) {
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

// Path overlay glyphs. GitHub issue 105 asked for points and handles that read
// without zooming in. Size alone did not do it: the controls need CONTRAST on every
// background (a light pasteboard, a pale photo, a saturated shape fill), which is
// how Photoshop and Figma get away with small glyphs. So every glyph is three-tone:
// a dark 1 px edge, a white band, and the accent (a saturated blue rather than the
// old pale one); handle lines ride on a translucent dark halo. A second pass
// (October 2026) found the curve itself, a bare 1.2 px line, still lost on a busy
// photo at 100%: the outline now rides the same halo at 2 px. Glyphs stay at
// Photoshop's scale (8 px): 10 and 12 px read as too big beside it (Seth,
// October 2026); knobs are easier to hit through kHandleHitRadiusPx instead.
namespace {
constexpr double kAnchorHalf = 4.0;  // 8 px square
constexpr double kKnobRadius = 4.0;  // 8 px round knob
// Widget pixels a glyph can reach past its point (half size, edge, antialiasing and
// the line halo); repaint regions around path points pad by this much.
constexpr double kGlyphReach = 8.0;

[[nodiscard]] QColor path_overlay_accent() {
  return QColor(30, 136, 255);
}

[[nodiscard]] QColor glyph_ink() {
  return QColor(20, 24, 30);
}

// Unselected: white square, dark edge, accent inner ring (Photoshop's hollow anchor).
// Selected: accent square, dark edge, white inner ring (its solid anchor).
void draw_anchor_glyph(QPainter& painter, QPointF center, bool selected, const QColor& accent) {
  const QRectF box(center.x() - kAnchorHalf, center.y() - kAnchorHalf, 2.0 * kAnchorHalf, 2.0 * kAnchorHalf);
  painter.setPen(QPen(glyph_ink(), 1.0));
  painter.setBrush(selected ? QBrush(accent) : QBrush(Qt::white));
  painter.drawRect(box);
  painter.setPen(QPen(selected ? QColor(Qt::white) : accent, 1.0));
  painter.setBrush(Qt::NoBrush);
  painter.drawRect(box.adjusted(1.0, 1.0, -1.0, -1.0));
}

// A white disc with a dark edge and an accent dot in the middle.
void draw_knob_glyph(QPainter& painter, QPointF center, const QColor& accent) {
  painter.setPen(QPen(glyph_ink(), 1.0));
  painter.setBrush(Qt::white);
  painter.drawEllipse(center, kKnobRadius, kKnobRadius);
  painter.setPen(Qt::NoPen);
  painter.setBrush(accent);
  painter.drawEllipse(center, kKnobRadius - 1.75, kKnobRadius - 1.75);
}

// The direction line from an anchor to its handle knob: a 1.6 px accent line on a
// 3.5 px translucent dark halo, visible over light and saturated pixels alike.
void draw_handle_line(QPainter& painter, QPointF from, QPointF to, const QColor& accent) {
  painter.setBrush(Qt::NoBrush);
  painter.setPen(QPen(QColor(0, 0, 0, 120), 3.5, Qt::SolidLine, Qt::RoundCap));
  painter.drawLine(from, to);
  painter.setPen(QPen(accent, 1.6, Qt::SolidLine, Qt::RoundCap));
  painter.drawLine(from, to);
}

// A path outline: a 1-device-pixel accent hairline with no halo (GitHub issue 238), so it
// reads as an overlay rather than a stroke of the artwork. It shifts half a device pixel so an
// edge on whole widget pixels covers one pixel row instead of smearing over two.
void draw_path_outline(QPainter& painter, const QPainterPath& outline, const QColor& accent,
                       Qt::PenStyle style = Qt::SolidLine) {
  const qreal ratio = painter.device() != nullptr ? std::max<qreal>(1.0, painter.device()->devicePixelRatioF()) : 1.0;
  painter.save();
  painter.translate(0.5 / ratio, 0.5 / ratio);
  painter.setBrush(Qt::NoBrush);
  painter.setPen(QPen(accent, 1.0 / ratio, style, Qt::FlatCap, Qt::MiterJoin));
  painter.drawPath(outline);
  painter.restore();
}
}  // namespace

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
  const QColor accent = path_overlay_accent();

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

  draw_path_outline(painter, outline, accent);
  draw_path_outline(painter, preview, accent, Qt::DashLine);

  // Handles of the anchor under construction.
  if (pen_handle_dragging_ && last.smooth) {
    draw_handle_line(painter, to_screen(last.in_x, last.in_y), to_screen(last.anchor_x, last.anchor_y), accent);
    draw_handle_line(painter, to_screen(last.anchor_x, last.anchor_y), to_screen(last.out_x, last.out_y), accent);
    draw_knob_glyph(painter, to_screen(last.in_x, last.in_y), accent);
    draw_knob_glyph(painter, to_screen(last.out_x, last.out_y), accent);
  }

  // Anchor squares; the first anchor doubles as the close target and draws hollow.
  for (std::size_t i = 0; i < pen_anchors_.size(); ++i) {
    const auto center = to_screen(pen_anchors_[i].anchor_x, pen_anchors_[i].anchor_y);
    const bool close_target = i == 0 && pen_anchors_.size() >= 3;
    draw_anchor_glyph(painter, center, !close_target, accent);
  }
  painter.restore();
}

// --- Path editing ---

bool CanvasWidget::path_edit_tool_active() const noexcept {
  return tool_ == CanvasTool::PathSelect || tool_ == CanvasTool::DirectSelect ||
         pen_family_tool_active();
}

bool CanvasWidget::shape_tool_active() const noexcept {
  return tool_ == CanvasTool::Line || tool_ == CanvasTool::Rectangle || tool_ == CanvasTool::Ellipse ||
         tool_ == CanvasTool::Polygon || tool_ == CanvasTool::CustomShape;
}

bool CanvasWidget::target_path_outline_tool_allows() const {
  if (path_edit_tool_active()) {
    return true;
  }
  if (!panel_path_targeted_) {
    return false;
  }
  // A work path, saved path or vector mask outline draws with any tool. A shape layer's own
  // path draws only under the shape tools: under Brush or Move, Photoshop shows none, and
  // the outline read as part of the artwork.
  const auto* layer = path_edit_target_layer();
  const bool shape_layer_path = layer != nullptr && path_edit_target_path() == &layer->vector_shape()->path;
  return !shape_layer_path || shape_tool_active();
}

bool CanvasWidget::pen_family_tool_active() const noexcept {
  return tool_ == CanvasTool::Pen || tool_ == CanvasTool::AddAnchor ||
         tool_ == CanvasTool::DeleteAnchor || tool_ == CanvasTool::ConvertPoint;
}

CanvasTool CanvasWidget::path_edit_tool() const noexcept {
  return pen_family_tool_active() && pen_temp_direct_select_ ? CanvasTool::DirectSelect : tool_;
}

void CanvasWidget::set_pen_auto_add_delete(bool enabled) {
  if (pen_auto_add_delete_ == enabled) {
    return;
  }
  pen_auto_add_delete_ = enabled;
  update_tool_cursor();
}

bool CanvasWidget::pen_auto_add_delete() const noexcept {
  return pen_auto_add_delete_;
}

CanvasWidget::PenEditMode CanvasWidget::pen_edit_mode() const noexcept {
  switch (tool_) {
    case CanvasTool::AddAnchor:
      return PenEditMode::AddOnly;
    case CanvasTool::DeleteAnchor:
      return PenEditMode::DeleteOnly;
    case CanvasTool::ConvertPoint:
      return PenEditMode::ConvertOnly;
    default:
      return pen_auto_add_delete_ ? PenEditMode::Auto : PenEditMode::DrawOnly;
  }
}

CanvasWidget::PenHoverHit CanvasWidget::filter_pen_hit(PenHoverHit hit, PenEditMode mode) noexcept {
  if (hit.action == PenHoverAction::Draw || hit.action == PenHoverAction::Close) {
    return hit;
  }
  switch (mode) {
    case PenEditMode::Auto:
      return hit;
    case PenEditMode::DrawOnly:
      hit.action = PenHoverAction::Draw;
      return hit;
    case PenEditMode::AddOnly:
      if (hit.action != PenHoverAction::Add) {
        hit.action = PenHoverAction::Draw;
      }
      return hit;
    case PenEditMode::DeleteOnly:
      hit.action = hit.anchor.first >= 0 ? PenHoverAction::Delete : PenHoverAction::Draw;
      return hit;
    case PenEditMode::ConvertOnly:
      hit.action = hit.anchor.first >= 0 ? PenHoverAction::Convert : PenHoverAction::Draw;
      return hit;
  }
  return hit;
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

std::pair<int, std::uint64_t> CanvasWidget::path_edit_target_owner_key() const {
  if (document_ == nullptr) {
    return {0, 0};
  }
  const auto active = std::as_const(*document_).active_layer_id();
  if (layer_edit_target_ == LayerEditTarget::VectorMask) {
    return {1, active.value_or(0)};
  }
  if (active_document_path_.has_value()) {
    return {2, *active_document_path_};
  }
  if (text_area_edit_target_path() != nullptr) {
    return {3, active.value_or(0)};
  }
  if (const auto* layer = path_edit_target_layer(); layer != nullptr) {
    return {4, layer->id()};
  }
  return {5, 0};  // the work path
}

void CanvasWidget::drop_point_selection_if_target_changed() {
  if (path_selected_anchors_.empty()) {
    path_selection_owner_.reset();
    return;
  }
  const auto owner = path_edit_target_owner_key();
  if (!path_selection_owner_.has_value()) {
    path_selection_owner_ = owner;  // made without a notify: it belongs to this target
    return;
  }
  if (*path_selection_owner_ == owner) {
    return;
  }
  // The indices point into another path now: editing them would change the
  // wrong layer's points.
  path_selected_anchors_.clear();
  path_selection_owner_.reset();
  notify_path_selection_changed();
  update();
}

void CanvasWidget::expand_path_selection_to_groups() {
  bool changed = false;
  const auto expand = [&changed](const VectorPath& path, std::set<std::pair<int, int>>& keys) {
    std::set<int> groups;
    for (const auto& key : keys) {
      if (key.first >= 0 && key.first < static_cast<int>(path.subpaths.size())) {
        groups.insert(path.subpaths[static_cast<std::size_t>(key.first)].shape_group);
      }
    }
    for (int s = 0; s < static_cast<int>(path.subpaths.size()); ++s) {
      const auto& subpath = path.subpaths[static_cast<std::size_t>(s)];
      if (!groups.contains(subpath.shape_group)) {
        continue;
      }
      for (int a = 0; a < static_cast<int>(subpath.anchors.size()); ++a) {
        changed = keys.insert({s, a}).second || changed;
      }
    }
  };
  if (const auto* path = path_edit_target_path(); path != nullptr && !path_selected_anchors_.empty()) {
    expand(*path, path_selected_anchors_);
  }
  for (auto& [id, keys] : extra_selected_anchors_) {
    if (const auto* layer = extra_edit_shape_layer(id); layer != nullptr) {
      expand(layer->vector_shape()->path, keys);
    }
  }
  if (changed) {
    notify_path_selection_changed();
    update();
  }
}

bool CanvasWidget::path_select_press_group(const VectorPath& path, int group, bool additive) {
  std::vector<std::pair<int, int>> keys;
  for (int s = 0; s < static_cast<int>(path.subpaths.size()); ++s) {
    if (path.subpaths[static_cast<std::size_t>(s)].shape_group != group) {
      continue;
    }
    for (int a = 0; a < static_cast<int>(path.subpaths[static_cast<std::size_t>(s)].anchors.size()); ++a) {
      keys.push_back({s, a});
    }
  }
  const bool selected = !keys.empty() && std::all_of(keys.begin(), keys.end(), [this](const auto& key) {
    return path_selected_anchors_.contains(key);
  });
  if (additive && selected) {
    for (const auto& key : keys) {
      path_selected_anchors_.erase(key);
    }
    return false;
  }
  if (!additive && !selected) {
    path_selected_anchors_.clear();
    extra_selected_anchors_.clear();
  }
  path_selected_anchors_.insert(keys.begin(), keys.end());
  return true;
}

Layer* CanvasWidget::topmost_path_pick_layer_at(QPointF widget_point) const {
  if (document_ == nullptr) {
    return nullptr;
  }
  const auto document_point = document_position_f(widget_point);
  const std::function<const Layer*(const std::vector<Layer>&)> walk =
      [&](const std::vector<Layer>& layers) -> const Layer* {
    for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
      const auto& layer = *it;
      if (!layer.visible()) {
        continue;
      }
      if (layer.kind() == LayerKind::Group) {
        if (const auto* found = walk(layer.children()); found != nullptr) {
          return found;
        }
        continue;
      }
      if (!layer_is_vector_shape(layer) || !vector_lock_reason(layer).empty()) {
        continue;
      }
      const auto& path = layer.vector_shape()->path;
      std::pair<int, int> segment{-1, -1};
      double segment_t = 0.0;
      if (anchor_hit_in(path, widget_point).first >= 0 || segment_hit_in(path, widget_point, segment, segment_t) ||
          shape_group_containing(path, document_point).has_value()) {
        return &layer;
      }
    }
    return nullptr;
  };
  return const_cast<Layer*>(walk(std::as_const(*document_).layers()));
}

void CanvasWidget::set_active_document_path(std::optional<DocumentPathId> id) {
  if (active_document_path_ == id) {
    return;
  }
  cancel_path_transform();
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

void CanvasWidget::set_panel_selected_layer_ids(std::vector<LayerId> ids) {
  if (panel_selected_layer_ids_ == ids) {
    return;
  }
  panel_selected_layer_ids_ = std::move(ids);
  drop_point_selection_if_target_changed();
  // A layer dropped from the panel selection loses its point selection.
  const auto erased = std::erase_if(extra_selected_anchors_, [this](const auto& entry) {
    return std::find(panel_selected_layer_ids_.begin(), panel_selected_layer_ids_.end(),
                     entry.first) == panel_selected_layer_ids_.end();
  });
  if (erased > 0) {
    notify_path_selection_changed();
  }
  if (path_edit_tool_active()) {
    update();
  }
}

void CanvasWidget::set_target_path_visible(bool visible) {
  if (target_path_visible_ == visible) {
    return;
  }
  target_path_visible_ = visible;
  update();
}

bool CanvasWidget::target_path_visible() const noexcept {
  return target_path_visible_;
}

void CanvasWidget::set_path_display_dismiss_callback(std::function<void()> callback) {
  path_display_dismiss_callback_ = std::move(callback);
}

void CanvasWidget::set_path_load_selection_callback(std::function<void()> callback) {
  path_load_selection_callback_ = std::move(callback);
}

void CanvasWidget::set_path_edited_callback(std::function<void()> callback) {
  path_edited_callback_ = std::move(callback);
}

void CanvasWidget::set_path_selection_changed_callback(std::function<void()> callback) {
  path_selection_changed_callback_ = std::move(callback);
}

void CanvasWidget::notify_path_selection_changed() {
  if (path_selected_anchors_.empty()) {
    path_selection_owner_.reset();
  } else {
    path_selection_owner_ = path_edit_target_owner_key();
  }
  if (path_selection_changed_callback_) {
    path_selection_changed_callback_();
  }
}

const VectorPath* CanvasWidget::text_area_edit_target_path() const {
  if (!document_) return nullptr;
  const auto active = std::as_const(*document_).active_layer_id();
  const auto* layer = active ? std::as_const(*document_).find_layer(*active) : nullptr;
  if (!layer || !layer->metadata().contains(kLayerMetadataTextArea) || text_geometry_is_protected(*layer)) return nullptr;
  if (text_area_path_layer_ != active || text_area_path_revision_ != layer->render_revision()) {
    text_area_path_cache_ = text_area_in_document(*layer);
    text_area_path_layer_ = active;
    text_area_path_revision_ = layer->render_revision();
  }
  return text_area_path_cache_ ? &*text_area_path_cache_ : nullptr;
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
  if (const auto* area = text_area_edit_target_path()) return area;
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
  const auto old_effect_rect =
      to_qrect(layer_bounds_with_effects(std::as_const(*layer), std::as_const(*layer).bounds()));
  layer->set_vector_mask(std::move(mask));
  mark_layer_vector_block_dirty(*layer);
  update_vector_mask_raster(*layer, Rect::from_size(document_->width(), document_->height()));
  // Bounded: the mask only attenuates this layer, so a full-canvas
  // recomposite per commit is wasted work.
  document_changed_effect_bounds(old_effect_rect.united(
      to_qrect(layer_bounds_with_effects(std::as_const(*layer), std::as_const(*layer).bounds()))));
  if (path_edited_callback_) {
    path_edited_callback_();
  }
}

void CanvasWidget::arm_path_edit_undo(const QString& label) {
  if (!path_edit_undo_armed_) {
    if (before_edit_callback_) {
      before_edit_callback_(label);
    }
    path_edit_undo_armed_ = true;
  }
}

void CanvasWidget::apply_path_edit(VectorPath path, const QString& label,
                                   const std::vector<int>& touched_groups) {
  if (document_ == nullptr) {
    return;
  }
  arm_path_edit_undo(label);
  replace_path_edit_target(std::move(path), touched_groups);
}

namespace {

// The reach of the path overlay in document space: anchors plus the direction
// handles and their knobs, which the layer's own bounds do not cover (a handle
// pulled above a shape). Empty for a path with no anchors.
QRectF path_control_bounds(const VectorPath& path) {
  bool any = false;
  double left = 0.0;
  double top = 0.0;
  double right = 0.0;
  double bottom = 0.0;
  const auto include = [&](double x, double y) {
    if (!any) {
      left = right = x;
      top = bottom = y;
      any = true;
      return;
    }
    left = std::min(left, x);
    right = std::max(right, x);
    top = std::min(top, y);
    bottom = std::max(bottom, y);
  };
  for (const auto& subpath : path.subpaths) {
    for (const auto& anchor : subpath.anchors) {
      include(anchor.anchor_x, anchor.anchor_y);
      include(anchor.in_x, anchor.in_y);
      include(anchor.out_x, anchor.out_y);
    }
  }
  return any ? QRectF(QPointF(left, top), QPointF(right, bottom)) : QRectF();
}

}  // namespace

void CanvasWidget::replace_path_edit_target(VectorPath path, const std::vector<int>& touched_groups) {
  if (document_ == nullptr) {
    return;
  }
  // Every branch below repaints the layer's bounds, but a live anchor or handle
  // drag also moves the overlay, whose knobs reach past those bounds: without
  // this the old knobs stayed on screen as a trail (GitHub issue 105). Old and
  // new control bounds are joined and repainted whatever branch returns.
  QRectF overlay = path_control_bounds(path);
  if (const auto* current = path_edit_target_path(); current != nullptr) {
    overlay = overlay.united(path_control_bounds(*current));
  }
  const auto repaint_overlay = qScopeGuard([this, overlay] {
    if (!overlay.isEmpty()) {
      constexpr int kPad = static_cast<int>(kGlyphReach) + 2;  // glyphs plus antialiasing slack
      update(widget_rect_for_document_rect(overlay).toAlignedRect().adjusted(-kPad, -kPad, kPad, kPad));
    }
  });
  if (layer_edit_target_ == LayerEditTarget::VectorMask) {
    if (auto* layer = vector_mask_target_layer(); layer != nullptr) {
      const auto old_effect_rect =
          to_qrect(layer_bounds_with_effects(std::as_const(*layer), std::as_const(*layer).bounds()));
      auto mask = *layer->vector_mask();
      mask.path = std::move(path);
      layer->set_vector_mask(std::move(mask));
      mark_layer_vector_block_dirty(*layer);
      update_vector_mask_raster(*layer, Rect::from_size(document_->width(), document_->height()));
      // Bounded: this runs on EVERY anchor-drag mouse move; the full-canvas
      // recomposite it replaced was the drag's dominant cost.
      document_changed_effect_bounds(old_effect_rect.united(
          to_qrect(layer_bounds_with_effects(std::as_const(*layer), std::as_const(*layer).bounds()))));
      if (path_edited_callback_) {
        path_edited_callback_();
      }
    }
    return;
  }
  if (active_document_path_.has_value()) {
    if (auto* target = document_->find_path(*active_document_path_); target != nullptr) {
      target->set_path(std::move(path));
      update();
      if (path_edited_callback_) {
        path_edited_callback_();
      }
    }
    return;
  }
  if (text_area_edit_target_path()) {
    auto* layer = document_->find_layer(*document_->active_layer_id());
    if (!layer || !valid_text_area(path) || !vector_lock_reason(std::as_const(*layer)).empty()) return;
    const auto affine = text_area_transform(std::as_const(*layer));
    bool invertible = false;
    const auto inverse = QTransform(affine[0], affine[1], affine[2], affine[3], affine[4], affine[5]).inverted(&invertible);
    if (!invertible) return;
    transform_vector_path(path, {inverse.m11(), inverse.m12(), inverse.m21(), inverse.m22(), inverse.dx(), inverse.dy()});
    if (!valid_text_area(path)) return;
    const auto original = *layer;
    const auto old_rect = to_qrect(layer_bounds_with_effects(std::as_const(*layer), layer->bounds()));
    layer->metadata()[kLayerMetadataTextArea] = serialize_vector_path(path);
    layer->metadata()[kLayerMetadataTextRasterStatus] = "patchy_raster";
    layer->metadata()[kLayerMetadataTextLayoutMode] = kTextLayoutModePhotoshop;
    const auto bounds = path.bounds();
    layer->metadata()[kLayerMetadataTextBoxWidth] = std::to_string(std::max(16, static_cast<int>(std::ceil(bounds->right))));
    layer->metadata()[kLayerMetadataTextBoxHeight] = std::to_string(std::max(16, static_cast<int>(std::ceil(bounds->bottom))));
    if (!rerender_text_layer_through_stored_transform(*layer)) { *layer = original; return; }
    document_changed_effect_bounds(old_rect.united(to_qrect(layer_bounds_with_effects(std::as_const(*layer), layer->bounds()))));
    if (path_edited_callback_) path_edited_callback_();
    return;
  }
  if (auto* layer = path_edit_target_layer(); layer != nullptr) {
    write_shape_layer_path(*layer, std::move(path), touched_groups);
  } else if (auto* work = document_->work_path(); work != nullptr) {
    work->set_path(std::move(path));
    update();
    if (path_edited_callback_) {
      path_edited_callback_();
    }
  }
}

void CanvasWidget::write_shape_layer_path(Layer& layer, VectorPath path,
                                          const std::vector<int>& touched_groups) {
  const auto old_effect_rect =
      to_qrect(layer_bounds_with_effects(std::as_const(layer), std::as_const(layer).bounds()));
  auto content = *layer.vector_shape();
  content.path = std::move(path);
  drop_live_shape_origination(content, touched_groups);
  layer.set_vector_shape(std::move(content));
  layer.metadata()[kLayerMetadataVectorRasterStatus] = kVectorRasterStatusPatchy;
  mark_layer_vector_block_dirty(layer);
  update_vector_shape_raster(layer, Rect::from_size(document_->width(), document_->height()),
                             &document_->metadata().patterns);
  // Bounded: this runs on EVERY anchor-drag mouse move; the full-canvas
  // recomposite it replaced was the drag's dominant cost (the re-bake can
  // move the layer's bounds, hence the old/new union).
  document_changed_effect_bounds(old_effect_rect.united(
      to_qrect(layer_bounds_with_effects(std::as_const(layer), std::as_const(layer).bounds()))));
  if (path_edited_callback_) {
    path_edited_callback_();
  }
}

const Layer* CanvasWidget::extra_edit_shape_layer(LayerId id) const {
  if (document_ == nullptr) {
    return nullptr;
  }
  const auto* layer = std::as_const(*document_).find_layer(id);
  if (layer == nullptr || !layer_is_vector_shape(*layer) || layer->vector_shape() == nullptr ||
      !vector_lock_reason(*layer).empty()) {
    return nullptr;
  }
  const auto& layers = std::as_const(*document_).layers();
  if (patchy::layer_effectively_locks_image_pixels(layers, id) ||
      patchy::layer_is_effectively_locked(layers, id)) {
    return nullptr;
  }
  if (&layer->vector_shape()->path == path_edit_target_path()) {
    return nullptr;  // the primary target edits through apply_path_edit
  }
  return layer;
}

void CanvasWidget::prune_extra_path_selection() {
  bool changed = false;
  for (auto it = extra_selected_anchors_.begin(); it != extra_selected_anchors_.end();) {
    const auto* layer = extra_edit_shape_layer(it->first);
    if (layer == nullptr) {
      it = extra_selected_anchors_.erase(it);
      changed = true;
      continue;
    }
    const auto& path = layer->vector_shape()->path;
    const auto erased = std::erase_if(it->second, [&path](const std::pair<int, int>& key) {
      return key.first < 0 || key.first >= static_cast<int>(path.subpaths.size()) ||
             key.second < 0 ||
             key.second >= static_cast<int>(
                 path.subpaths[static_cast<std::size_t>(key.first)].anchors.size());
    });
    changed = changed || erased > 0;
    if (it->second.empty()) {
      it = extra_selected_anchors_.erase(it);
      changed = true;
    } else {
      ++it;
    }
  }
  if (changed) {
    notify_path_selection_changed();
  }
}

QPointF CanvasWidget::path_point_to_screen(double x, double y) const {
  const auto origin = widget_position_for_document_point(QPoint(0, 0));
  return QPointF(origin.x() + x * zoom_, origin.y() + y * zoom_);
}

QTransform CanvasWidget::layer_preview_transform(LayerId id) const {
  if (moving_layer_ && !move_preview_delta_.isNull()) {
    for (const auto& moving : moving_layers_) {
      if (moving.id == id) {
        return QTransform::fromTranslate(move_preview_delta_.x(), move_preview_delta_.y());
      }
    }
  }
  if (transforming_layer_) {
    if (transform_layer_id_.has_value() && *transform_layer_id_ == id) {
      return free_transform_preview_delta();
    }
    for (const auto& target : transform_targets_) {
      if (target.id == id) {
        return free_transform_preview_delta();
      }
    }
  }
  return {};
}

QRectF CanvasWidget::path_overlay_preview_document_rect() const {
  if ((!moving_layer_ && !transforming_layer_) || path_transform_active_ ||
      !target_path_visible_ || !selection_edges_visible_ || pen_session_active_ ||
      document_ == nullptr) {
    return {};
  }
  const bool editing = path_edit_tool_active();
  if (!target_path_outline_tool_allows()) {
    return {};
  }
  QRectF dirty;
  const auto add_layer = [this, &dirty](const Layer& layer, const VectorPath& path) {
    const auto transform = layer_preview_transform(layer.id());
    if (transform.isIdentity()) {
      return;
    }
    const auto bounds = path.bounds();
    if (!bounds.has_value()) {
      return;
    }
    const QRectF rect(QPointF(bounds->left, bounds->top), QPointF(bounds->right, bounds->bottom));
    // Anchor squares and handle knobs extend a few widget pixels past the path.
    const auto pad = kGlyphReach / std::max(zoom_, 1e-6);
    dirty = dirty.united(rect.adjusted(-pad, -pad, pad, pad))
                .united(transform.mapRect(rect).adjusted(-pad, -pad, pad, pad));
  };
  if (layer_edit_target_ == LayerEditTarget::VectorMask) {
    if (const auto* layer = vector_mask_target_layer(); layer != nullptr) {
      add_layer(*layer, layer->vector_mask()->path);
    }
  } else if (!active_document_path_.has_value()) {
    if (const auto* layer = path_edit_target_layer(); layer != nullptr) {
      add_layer(*layer, layer->vector_shape()->path);
    }
  }
  if (editing) {
    for (const auto id : panel_selected_layer_ids_) {
      const auto* layer = std::as_const(*document_).find_layer(id);
      if (layer != nullptr && layer->vector_shape() != nullptr) {
        add_layer(*layer, layer->vector_shape()->path);
      }
    }
  }
  return dirty;
}

std::pair<int, int> CanvasWidget::path_anchor_at(QPointF widget_point) const {
  const auto* path = path_edit_target_path();
  if (path == nullptr) {
    return {-1, -1};
  }
  return anchor_hit_in(*path, widget_point);
}

std::pair<int, int> CanvasWidget::anchor_hit_in(const VectorPath& path, QPointF widget_point) const {
  for (int s = 0; s < static_cast<int>(path.subpaths.size()); ++s) {
    const auto& anchors = path.subpaths[static_cast<std::size_t>(s)].anchors;
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
      const auto to_handle = std::hypot(handle_screen.x() - widget_point.x(), handle_screen.y() - widget_point.y());
      const auto to_anchor = std::hypot(anchor_screen.x() - widget_point.x(), anchor_screen.y() - widget_point.y());
      return std::hypot(handle_screen.x() - anchor_screen.x(),
                        handle_screen.y() - anchor_screen.y()) > 0.5 &&
             to_handle <= kHandleHitRadiusPx && (to_handle < to_anchor || to_anchor > kPathHitRadiusPx);
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
  return segment_hit_in(*path, widget_point, segment, segment_t);
}

bool CanvasWidget::segment_hit_in(const VectorPath& path, QPointF widget_point,
                                  std::pair<int, int>& segment, double& segment_t) const {
  double best_distance = kPathHitRadiusPx;
  bool found = false;
  for (int s = 0; s < static_cast<int>(path.subpaths.size()); ++s) {
    const auto& subpath = path.subpaths[static_cast<std::size_t>(s)];
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
  if (path_transform_active_) {
    return handle_path_transform_press(document_point, QPointF(event->position()), event->modifiers());
  }
  drop_point_selection_if_target_changed();
  const auto* path = path_edit_target_path();
  if (path == nullptr) {
    report_status_error(tr("Select a shape layer or draw a path first"));
    return true;
  }
  path_edit_undo_armed_ = false;
  path_edit_changed_ = false;
  path_drag_last_document_ = document_point;
  path_drag_origin_document_ = document_point;
  path_drag_raw_document_ = document_point;
  path_drag_applied_delta_ = QPointF(0.0, 0.0);
  const auto selection_before = path_selected_anchors_;
  const auto extras_before = extra_selected_anchors_;
  const auto notify_if_changed = [&] {
    if (path_selected_anchors_ != selection_before || extra_selected_anchors_ != extras_before) {
      notify_path_selection_changed();
    }
  };

  const auto widget_point = QPointF(event->position());
  // Handle knobs are Direct Select's (Path Select moves whole shapes and draws
  // none; GitHub issue 248 tool roles).
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
      if (!path_select_press_group(*path, path->subpaths[static_cast<std::size_t>(anchor.first)].shape_group,
                                   additive)) {
        path_drag_mode_ = PathEditDrag::None;  // Shift deselected it
        notify_if_changed();
        update();
        return true;
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
        extra_selected_anchors_.clear();
      }
    }
    path_drag_mode_ = PathEditDrag::Anchors;
    path_drag_anchor_ = anchor;
    notify_if_changed();
    update();
    return true;
  }
  // Anchors of the other panel-selected shape layers hit-test too (cross-layer
  // Direct Select); their keys live in extra_selected_anchors_ per layer.
  if (edit_tool == CanvasTool::DirectSelect) {
    for (const auto id : panel_selected_layer_ids_) {
      const auto* extra_layer = extra_edit_shape_layer(id);
      if (extra_layer == nullptr) {
        continue;
      }
      const auto hit = anchor_hit_in(extra_layer->vector_shape()->path, widget_point);
      if (hit.first < 0) {
        continue;
      }
      auto& keys = extra_selected_anchors_[id];
      if ((event->modifiers() & Qt::ShiftModifier) != 0) {
        if (keys.contains(hit)) {
          keys.erase(hit);
          if (keys.empty()) {
            extra_selected_anchors_.erase(id);
          }
        } else {
          keys.insert(hit);
        }
      } else if (!keys.contains(hit)) {
        path_selected_anchors_.clear();
        extra_selected_anchors_.clear();
        extra_selected_anchors_[id] = {hit};
      }
      path_drag_mode_ = PathEditDrag::Anchors;
      path_drag_anchor_ = {-1, -1};
      notify_if_changed();
      update();
      return true;
    }
  }
  std::pair<int, int> segment{-1, -1};
  double segment_t = 0.0;
  if (path_segment_at(widget_point, segment, segment_t)) {
    const auto& subpath = path->subpaths[static_cast<std::size_t>(segment.first)];
    const auto anchor_count = static_cast<int>(subpath.anchors.size());
    if (edit_tool == CanvasTool::PathSelect) {
      if (!path_select_press_group(*path, subpath.shape_group, (event->modifiers() & Qt::ShiftModifier) != 0)) {
        path_drag_mode_ = PathEditDrag::None;  // Shift deselected it
        notify_if_changed();
        update();
        return true;
      }
    } else {
      // A segment whose ends are already selected drags the whole selection
      // (a marquee followed by a drag on any of its lines); Shift adds the
      // segment to it; otherwise the segment becomes the selection.
      const std::pair<int, int> start{segment.first, segment.second};
      const std::pair<int, int> end{segment.first, (segment.second + 1) % anchor_count};
      if ((event->modifiers() & Qt::ShiftModifier) != 0) {
        path_selected_anchors_.insert(start);
        path_selected_anchors_.insert(end);
      } else if (!path_selected_anchors_.contains(start) || !path_selected_anchors_.contains(end)) {
        path_selected_anchors_ = {start, end};
        extra_selected_anchors_.clear();
      }
    }
    path_drag_mode_ = PathEditDrag::Anchors;
    path_drag_anchor_ = segment;
    notify_if_changed();
    update();
    return true;
  }
  // Segments of the other panel-selected shape layers select their two ends.
  if (edit_tool == CanvasTool::DirectSelect) {
    for (const auto id : panel_selected_layer_ids_) {
      const auto* extra_layer = extra_edit_shape_layer(id);
      if (extra_layer == nullptr) {
        continue;
      }
      const auto& extra_path = extra_layer->vector_shape()->path;
      std::pair<int, int> extra_segment{-1, -1};
      double extra_t = 0.0;
      if (!segment_hit_in(extra_path, widget_point, extra_segment, extra_t)) {
        continue;
      }
      const auto anchor_count = static_cast<int>(
          extra_path.subpaths[static_cast<std::size_t>(extra_segment.first)].anchors.size());
      const std::pair<int, int> start{extra_segment.first, extra_segment.second};
      const std::pair<int, int> end{extra_segment.first,
                                    (extra_segment.second + 1) % anchor_count};
      auto& keys = extra_selected_anchors_[id];
      if ((event->modifiers() & Qt::ShiftModifier) != 0) {
        keys.insert(start);
        keys.insert(end);
      } else if (!keys.contains(start) || !keys.contains(end)) {
        path_selected_anchors_.clear();
        extra_selected_anchors_.clear();
        extra_selected_anchors_[id] = {start, end};
      }
      path_drag_mode_ = PathEditDrag::Anchors;
      path_drag_anchor_ = {-1, -1};
      notify_if_changed();
      update();
      return true;
    }
  }
  // Another shape layer under the pointer (topmost first, by its points,
  // outline or fill) becomes the target and takes this same press, so one
  // press picks and drags it (GitHub issue 248). Pixel layers are never picked.
  if (!path_press_repicked_) {
    if (auto* picked = topmost_path_pick_layer_at(widget_point);
        picked != nullptr && &std::as_const(*picked).vector_shape()->path != path) {
      activate_layer(*picked);
      if (path_edit_target_path() == &std::as_const(*picked).vector_shape()->path) {
        clear_path_edit_selection();
        path_press_repicked_ = true;
        const auto reset = qScopeGuard([this] { path_press_repicked_ = false; });
        return handle_path_edit_press(event, document_point);
      }
    }
  }
  // Path Select: a press inside a shape's fill selects that shape and drags it.
  if (edit_tool == CanvasTool::PathSelect) {
    if (const auto group = shape_group_containing(*path, document_point); group.has_value()) {
      if (!path_select_press_group(*path, *group, (event->modifiers() & Qt::ShiftModifier) != 0)) {
        path_drag_mode_ = PathEditDrag::None;  // Shift deselected it
        notify_if_changed();
        update();
        return true;
      }
      path_drag_mode_ = PathEditDrag::Anchors;
      path_drag_anchor_ = {-1, -1};
      notify_if_changed();
      update();
      return true;
    }
  }
  // Empty space: marquee selection.
  path_drag_mode_ = PathEditDrag::Marquee;
  path_marquee_start_ = document_point;
  path_marquee_current_ = document_point;
  path_marquee_raw_current_ = document_point;
  if ((event->modifiers() & Qt::ShiftModifier) == 0) {
    path_selected_anchors_.clear();
    extra_selected_anchors_.clear();
  }
  notify_if_changed();
  update();
  return true;
}

bool CanvasWidget::handle_path_edit_move(QMouseEvent* event, QPointF document_point) {
  const auto edit_tool = path_edit_tool();
  if (edit_tool != CanvasTool::PathSelect && edit_tool != CanvasTool::DirectSelect) {
    return false;
  }
  if (path_transform_active_) {
    return handle_path_transform_move(event, document_point);
  }
  if (path_drag_mode_ == PathEditDrag::None || (event->buttons() & Qt::LeftButton) == 0) {
    return true;  // hover only
  }
  if (path_drag_mode_ == PathEditDrag::Marquee) {
    if (spacebar_repositioning_drag_rect_) {
      // Space held: the whole rect follows the pointer instead of resizing.
      const auto delta = document_point - QPointF(spacebar_reposition_last_document_position_);
      spacebar_reposition_last_document_position_ = document_point.toPoint();
      path_marquee_start_ += delta;
      path_marquee_current_ += delta;
      path_marquee_raw_current_ += delta;
      update();
      return true;
    }
    path_marquee_raw_current_ = document_point;
    path_marquee_current_ = constrain_marquee_current(document_point, event->modifiers());
    update();
    return true;
  }
  path_drag_raw_document_ = document_point;
  return update_path_edit_drag(document_point, event->modifiers());
}

QPointF CanvasWidget::constrain_marquee_current(QPointF current,
                                                Qt::KeyboardModifiers modifiers) const {
  if ((modifiers & Qt::ShiftModifier) == 0) {
    return current;
  }
  const auto dx = current.x() - path_marquee_start_.x();
  const auto dy = current.y() - path_marquee_start_.y();
  const auto side = std::min(std::abs(dx), std::abs(dy));
  return QPointF(path_marquee_start_.x() + std::copysign(side, dx),
                 path_marquee_start_.y() + std::copysign(side, dy));
}

QPointF CanvasWidget::constrain_drag_to_axes(QPointF total_delta) noexcept {
  if (total_delta.isNull()) {
    return total_delta;
  }
  // Photoshop-style: snap the drag direction to the nearest 45-degree axis,
  // re-evaluated per call against the TOTAL delta from the press point (never
  // latched), then project the delta onto that axis so the points follow the
  // mouse's projection along it. The exact 0.0/1.0 components matter: a
  // horizontal drag must produce a dy of exactly 0.0 so the no-op early-out
  // in update_path_edit_drag stays honest.
  constexpr double kSnap = std::numbers::pi / 4.0;
  const auto angle = std::atan2(total_delta.y(), total_delta.x());
  // lround(angle / kSnap) is in [-4, 4]; -4 & 7 == 4 folds -pi onto +pi.
  const auto octant = static_cast<int>(std::lround(angle / kSnap)) & 7;
  constexpr double kDiag = std::numbers::sqrt2 / 2.0;
  constexpr std::array<std::pair<double, double>, 8> kDirections{{{1.0, 0.0},
                                                                 {kDiag, kDiag},
                                                                 {0.0, 1.0},
                                                                 {-kDiag, kDiag},
                                                                 {-1.0, 0.0},
                                                                 {-kDiag, -kDiag},
                                                                 {0.0, -1.0},
                                                                 {kDiag, -kDiag}}};
  const QPointF direction(kDirections[static_cast<std::size_t>(octant)].first,
                          kDirections[static_cast<std::size_t>(octant)].second);
  const auto t = total_delta.x() * direction.x() + total_delta.y() * direction.y();
  return QPointF(direction.x() * t, direction.y() * t);
}

bool CanvasWidget::update_path_edit_drag(QPointF document_point, Qt::KeyboardModifiers modifiers) {
  const auto edit_tool = path_edit_tool();
  const auto* path = path_edit_target_path();
  if (path == nullptr) {
    return true;
  }
  double dx = 0.0;
  double dy = 0.0;
  if (path_drag_mode_ == PathEditDrag::Anchors) {
    // Anchor drags track the total delta from the press so Shift can constrain
    // (and un-constrain) against it; each frame still applies an increment.
    const auto raw_total = document_point - path_drag_origin_document_;
    const auto effective_total = (modifiers & Qt::ShiftModifier) != 0
                                     ? constrain_drag_to_axes(raw_total)
                                     : raw_total;
    dx = effective_total.x() - path_drag_applied_delta_.x();
    dy = effective_total.y() - path_drag_applied_delta_.y();
    if (dx == 0.0 && dy == 0.0) {
      return true;
    }
    path_drag_applied_delta_ = effective_total;
  } else {
    dx = document_point.x() - path_drag_last_document_.x();
    dy = document_point.y() - path_drag_last_document_.y();
    if (dx == 0.0 && dy == 0.0) {
      return true;
    }
  }
  path_drag_last_document_ = document_point;
  prune_path_edit_selection(*path);
  prune_extra_path_selection();
  const auto label = edit_tool == CanvasTool::PathSelect && path_drag_mode_ == PathEditDrag::Anchors
                         ? tr("Move shape")
                         : tr("Edit path");
  // Cross-layer part of the drag: selected anchors on the other panel-selected
  // shape layers move by the same delta, inside the same armed undo entry (the
  // snapshot is document-wide, so N layers still coalesce into one entry).
  if (path_drag_mode_ == PathEditDrag::Anchors) {
    for (const auto& [id, keys] : extra_selected_anchors_) {
      const auto* extra_layer = extra_edit_shape_layer(id);
      if (extra_layer == nullptr || keys.empty()) {
        continue;
      }
      auto extra_working = extra_layer->vector_shape()->path;
      std::vector<int> extra_touched;
      for (const auto& key : keys) {
        auto& anchor = extra_working.subpaths[static_cast<std::size_t>(key.first)]
                           .anchors[static_cast<std::size_t>(key.second)];
        anchor.anchor_x += dx;
        anchor.anchor_y += dy;
        anchor.in_x += dx;
        anchor.in_y += dy;
        anchor.out_x += dx;
        anchor.out_y += dy;
        const auto group =
            extra_working.subpaths[static_cast<std::size_t>(key.first)].shape_group;
        if (std::find(extra_touched.begin(), extra_touched.end(), group) == extra_touched.end()) {
          extra_touched.push_back(group);
        }
      }
      arm_path_edit_undo(label);
      path_edit_changed_ = true;
      write_shape_layer_path(*document_->find_layer(id), std::move(extra_working), extra_touched);
    }
    if (path_selected_anchors_.empty()) {
      update();
      return true;  // extras-only drag: nothing to write on the primary target
    }
  }
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
  apply_path_edit(std::move(working), label, touched_groups);
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
  if (path_transform_active_) {
    path_transform_drag_handle_ = TransformHandle::None;
    return true;
  }
  if (path_drag_mode_ == PathEditDrag::Marquee) {
    const auto* path = path_edit_target_path();
    if (path != nullptr) {
      const auto selection_before = path_selected_anchors_;
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
        // Any shape the rect touches, anchors inside it or not (GitHub issue 248).
        for (const auto& subpath : path->subpaths) {
          if (!groups_in_box.contains(subpath.shape_group) && subpath_touches_rect(subpath, rect)) {
            groups_in_box.insert(subpath.shape_group);
          }
        }
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
      // The marquee also captures anchors of the other panel-selected shape
      // layers (cross-layer Direct Select).
      bool extras_changed = false;
      if (edit_tool == CanvasTool::DirectSelect) {
        for (const auto id : panel_selected_layer_ids_) {
          const auto* extra_layer = extra_edit_shape_layer(id);
          if (extra_layer == nullptr) {
            continue;
          }
          const auto& extra_path = extra_layer->vector_shape()->path;
          std::set<std::pair<int, int>> hits;
          for (int s = 0; s < static_cast<int>(extra_path.subpaths.size()); ++s) {
            const auto& subpath = extra_path.subpaths[static_cast<std::size_t>(s)];
            for (int a = 0; a < static_cast<int>(subpath.anchors.size()); ++a) {
              const auto& anchor = subpath.anchors[static_cast<std::size_t>(a)];
              if (rect.contains(QPointF(anchor.anchor_x, anchor.anchor_y))) {
                hits.insert({s, a});
              }
            }
          }
          if (hits.empty()) {
            continue;
          }
          auto& keys = extra_selected_anchors_[id];
          for (const auto& hit : hits) {
            extras_changed = keys.insert(hit).second || extras_changed;
          }
        }
      }
      if (path_selected_anchors_ != selection_before || extras_changed) {
        notify_path_selection_changed();
      }
    }
  }
  path_drag_mode_ = PathEditDrag::None;
  update();
  return true;
}

void CanvasWidget::prune_path_edit_selection(const VectorPath& path) {
  const auto erased = std::erase_if(path_selected_anchors_, [&path](const std::pair<int, int>& key) {
    return key.first < 0 || key.first >= static_cast<int>(path.subpaths.size()) ||
           key.second < 0 ||
           key.second >= static_cast<int>(
               path.subpaths[static_cast<std::size_t>(key.first)].anchors.size());
  });
  if (erased > 0) {
    notify_path_selection_changed();
  }
}

void CanvasWidget::delete_selected_path_anchors() {
  const auto* path = path_edit_target_path();
  if (path != nullptr) {
    prune_path_edit_selection(*path);
  }
  prune_extra_path_selection();
  const bool primary = path != nullptr && !path_selected_anchors_.empty();
  if (!primary && extra_selected_anchors_.empty()) {
    return;
  }
  path_edit_undo_armed_ = false;
  if (primary) {
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
    apply_path_edit(std::move(working), tr("Delete anchors"), touched_groups);
  }
  // Cross-layer selections delete too, inside the same undo entry.
  for (const auto& [id, keys] : extra_selected_anchors_) {
    const auto* extra_layer = extra_edit_shape_layer(id);
    if (extra_layer == nullptr || keys.empty()) {
      continue;
    }
    auto working = extra_layer->vector_shape()->path;
    std::vector<int> touched_groups;
    for (auto it = keys.rbegin(); it != keys.rend(); ++it) {
      auto& subpath = working.subpaths[static_cast<std::size_t>(it->first)];
      subpath.anchors.erase(subpath.anchors.begin() + it->second);
      if (std::find(touched_groups.begin(), touched_groups.end(), subpath.shape_group) ==
          touched_groups.end()) {
        touched_groups.push_back(subpath.shape_group);
      }
    }
    std::erase_if(working.subpaths,
                  [](const PathSubpath& subpath) { return subpath.anchors.size() < 2; });
    arm_path_edit_undo(tr("Delete anchors"));
    write_shape_layer_path(*document_->find_layer(id), std::move(working), touched_groups);
  }
  extra_selected_anchors_.clear();
  notify_path_selection_changed();
  path_edit_undo_armed_ = false;
  update();
}

bool CanvasWidget::handle_path_edit_key(QKeyEvent* event) {
  if (!path_edit_tool_active()) {
    return false;
  }
  drop_point_selection_if_target_changed();
  if (path_transform_active_) {
    return handle_path_transform_key(event);
  }
  // The Pen family only owns the selection keys once a Ctrl+click selected
  // anchors (Delete/Backspace remove them, Escape clears); arrows stay off
  // so hotkeys keep them.
  const bool selection_key = event->key() == Qt::Key_Delete ||
                             event->key() == Qt::Key_Backspace ||
                             event->key() == Qt::Key_Escape;
  if (!path_edit_has_selection() || (pen_family_tool_active() && !selection_key)) {
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
      if (path != nullptr) {
        prune_path_edit_selection(*path);
      }
      prune_extra_path_selection();
      const bool primary = path != nullptr && !path_selected_anchors_.empty();
      if (!primary && extra_selected_anchors_.empty()) {
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
      const auto nudge_keys = [dx, dy](VectorPath& working,
                                       const std::set<std::pair<int, int>>& keys,
                                       std::vector<int>& touched_groups) {
        for (const auto& key : keys) {
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
      };
      if (primary) {
        auto working = *path;
        std::vector<int> touched_groups;
        nudge_keys(working, path_selected_anchors_, touched_groups);
        apply_path_edit(std::move(working), tr("Nudge anchors"), touched_groups);
      }
      for (const auto& [id, keys] : extra_selected_anchors_) {
        const auto* extra_layer = extra_edit_shape_layer(id);
        if (extra_layer == nullptr || keys.empty()) {
          continue;
        }
        auto working = extra_layer->vector_shape()->path;
        std::vector<int> touched_groups;
        nudge_keys(working, keys, touched_groups);
        arm_path_edit_undo(tr("Nudge anchors"));
        write_shape_layer_path(*document_->find_layer(id), std::move(working), touched_groups);
      }
      return true;
    }
    default:
      return false;
  }
}

CanvasWidget::PenHoverHit CanvasWidget::pen_hover_hit(QPointF widget_point, QPointF document_point,
                                                      Qt::KeyboardModifiers modifiers) const {
  return filter_pen_hit(pen_hover_hit_raw(widget_point, document_point, modifiers),
                        pen_edit_mode());
}

CanvasWidget::PenHoverHit CanvasWidget::pen_hover_hit_raw(QPointF widget_point,
                                                          QPointF document_point,
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
  return apply_pen_hover_edit(
      pen_hover_hit(QPointF(event->position()), document_point, event->modifiers()));
}

bool CanvasWidget::apply_pen_hover_edit(const PenHoverHit& hit) {
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
  return path_edit_selected_anchor_count() > 0;
}

int CanvasWidget::path_edit_selected_anchor_count() const noexcept {
  auto count = path_selected_anchors_.size();
  for (const auto& [id, keys] : extra_selected_anchors_) {
    count += keys.size();
  }
  return static_cast<int>(count);
}

void CanvasWidget::update_path_hover_hint(PenHoverAction action) {
  const auto previous = path_hover_hint_action_;
  path_hover_hint_action_ = action;
  if (action == previous || action == PenHoverAction::Draw || !status_callback_) {
    return;
  }
  // Ctrl latches Direct Select for the whole Pen family, so every hint names
  // the Ctrl-drag move gesture alongside the click edit.
  switch (action) {
    case PenHoverAction::Add:
      status_callback_(resolve_modifier_names(tr("Click to add a point here. %CTRL%-drag moves the segment.")));
      return;
    case PenHoverAction::Delete:
      status_callback_(resolve_modifier_names(
          tool_ == CanvasTool::Pen ? tr("Click to delete this point. %CTRL%-drag moves it. %ALT%+click "
                                        "converts it between corner and smooth.")
                                   : tr("Click to delete this point. %CTRL%-drag moves it.")));
      return;
    case PenHoverAction::Convert:
      status_callback_(tr("Click to convert this point between corner and smooth"));
      return;
    case PenHoverAction::Close:
      status_callback_(tr("Click to close the path"));
      return;
    case PenHoverAction::Draw:
      return;
  }
}

CanvasWidget::PathHoverTarget CanvasWidget::path_hover_target_at(QPointF widget_point) const {
  if (path_edit_target_path() == nullptr) {
    return PathHoverTarget::None;
  }
  std::pair<int, int> handle_anchor{-1, -1};
  if (path_edit_tool() == CanvasTool::DirectSelect && path_handle_at(widget_point, handle_anchor) != 0) {
    return PathHoverTarget::Handle;
  }
  if (path_anchor_at(widget_point).first >= 0) {
    return PathHoverTarget::Anchor;
  }
  std::pair<int, int> segment{-1, -1};
  double segment_t = 0.0;
  if (path_segment_at(widget_point, segment, segment_t)) {
    return PathHoverTarget::Segment;
  }
  if (path_edit_tool() == CanvasTool::PathSelect &&
      shape_group_containing(*path_edit_target_path(), document_position_f(widget_point)).has_value()) {
    return PathHoverTarget::Fill;
  }
  if (topmost_path_pick_layer_at(widget_point) != nullptr) {
    return PathHoverTarget::Fill;  // a press picks that shape layer
  }
  return PathHoverTarget::None;
}

void CanvasWidget::update_path_select_hover_hint(PathHoverTarget target) {
  const auto previous = path_hover_hint_target_;
  path_hover_hint_target_ = target;
  if (target == previous || target == PathHoverTarget::None || !status_callback_) {
    return;
  }
  if (path_edit_tool() == CanvasTool::PathSelect) {
    status_callback_(
        resolve_modifier_names(tr("Click to select the shape, drag to move it. %CTRL%+T transforms it.")));
    return;
  }
  switch (target) {
    case PathHoverTarget::Anchor:
      status_callback_(tr("Drag to move the point. Shift+click adds it to the selection; "
                          "Delete removes the selected points."));
      return;
    case PathHoverTarget::Handle:
      status_callback_(tr("Drag the handle to reshape the curve"));
      return;
    case PathHoverTarget::Segment:
      status_callback_(tr("Drag to move the segment, or the whole selection when its points are selected"));
      return;
    case PathHoverTarget::Fill:
    case PathHoverTarget::None:
      return;
  }
}

bool CanvasWidget::show_path_context_menu(QPointF widget_point, QPoint global_position) {
  if (!path_edit_tool_active() || document_ == nullptr || pen_session_active_ ||
      path_transform_active_) {
    return false;
  }
  const auto* path = path_edit_target_path();
  if (path == nullptr) {
    return false;
  }
  // Unfiltered on purpose: the menu names the edits explicitly, so it offers
  // them under every path tool and with Auto Add/Delete off (Photoshop's menu).
  const auto hit =
      pen_hover_hit_raw(widget_point, document_position_f(widget_point), Qt::NoModifier);
  const bool over_anchor = hit.anchor.first >= 0;
  const bool over_segment = !over_anchor && hit.segment.first >= 0;
  prune_path_edit_selection(*path);
  const bool has_selection = !path_selected_anchors_.empty();

  QMenu menu(this);
  menu.setObjectName(QStringLiteral("canvasPathContextMenu"));
  const auto add_edit_action = [this, &menu, hit](const QString& text, const char* object_name,
                                                  bool enabled, PenHoverAction action) {
    auto* menu_action = menu.addAction(text);
    menu_action->setObjectName(QLatin1String(object_name));
    menu_action->setEnabled(enabled);
    connect(menu_action, &QAction::triggered, this, [this, hit, action] {
      auto edit = hit;
      edit.action = action;
      apply_pen_hover_edit(edit);
    });
  };
  add_edit_action(tr("Add Anchor Point"), "pathMenuAddAnchorAction", over_segment,
                  PenHoverAction::Add);
  add_edit_action(tr("Delete Anchor Point"), "pathMenuDeleteAnchorAction", over_anchor,
                  PenHoverAction::Delete);
  add_edit_action(tr("Convert Point"), "pathMenuConvertPointAction", over_anchor,
                  PenHoverAction::Convert);
  menu.addSeparator();
  auto* delete_selected_action = menu.addAction(tr("Delete Selected Points"));
  delete_selected_action->setObjectName(QStringLiteral("pathMenuDeleteSelectedAction"));
  delete_selected_action->setEnabled(has_selection);
  connect(delete_selected_action, &QAction::triggered, this,
          [this] { delete_selected_path_anchors(); });
  auto* deselect_action = menu.addAction(tr("Deselect Points"));
  deselect_action->setObjectName(QStringLiteral("pathMenuDeselectAction"));
  deselect_action->setEnabled(has_selection);
  connect(deselect_action, &QAction::triggered, this, [this] { clear_path_edit_selection(); });
  menu.addSeparator();
  // begin_path_transform is Path Select / Direct Select only (the Pen family
  // falls through to the layer transform, like Photoshop).
  const bool select_tool = tool_ == CanvasTool::PathSelect || tool_ == CanvasTool::DirectSelect;
  const bool transform_points = tool_ == CanvasTool::DirectSelect && has_selection;
  auto* transform_action =
      menu.addAction(transform_points ? tr("Free Transform Points") : tr("Free Transform Path"));
  transform_action->setObjectName(QStringLiteral("pathMenuFreeTransformAction"));
  transform_action->setEnabled(select_tool && !path->subpaths.empty());
  connect(transform_action, &QAction::triggered, this, [this] { begin_path_transform(); });
  menu.exec(global_position);
  update_tool_cursor();
  update();
  return true;
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
  if (!path_selected_anchors_.empty() || !extra_selected_anchors_.empty()) {
    path_selected_anchors_.clear();
    extra_selected_anchors_.clear();
    notify_path_selection_changed();
    update();
  }
}

void CanvasWidget::draw_path_edit_overlay(QPainter& painter) {
  if (path_transform_active_) {
    draw_path_transform_overlay(painter);  // an active session always shows its box
    return;
  }
  if (!target_path_visible_) {
    return;  // View > Show > Target Path is off
  }
  if (!selection_edges_visible_) {
    return;  // Ctrl+H (Photoshop's Extras toggle) hides the points and outline too
  }
  // A targeted Paths-panel row draws its outline with other tools too (a shape
  // layer's own path only under the shape tools, GitHub issue 238); anchors,
  // handles, and the marquee are editing surfaces and stay path-tool-only.
  const bool editing = path_edit_tool_active();
  if (!target_path_outline_tool_allows() || pen_session_active_) {
    return;
  }
  const auto* path = path_edit_target_path();
  if (path == nullptr || path->subpaths.empty()) {
    return;
  }
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);
  const QColor accent = path_overlay_accent();

  // A layer-owned target path rides its layer's Move/Free Transform preview
  // (display only); document paths (work path, saved rows) stay put.
  QTransform target_transform;
  if (layer_edit_target_ == LayerEditTarget::VectorMask) {
    if (const auto* layer = vector_mask_target_layer(); layer != nullptr) {
      target_transform = layer_preview_transform(layer->id());
    }
  } else if (!active_document_path_.has_value()) {
    if (const auto* layer = path_edit_target_layer();
        layer != nullptr && &layer->vector_shape()->path == path) {
      target_transform = layer_preview_transform(layer->id());
    }
  }
  const auto to_screen = [this](const QTransform& transform, double x, double y) {
    const auto mapped = transform.map(QPointF(x, y));
    return path_point_to_screen(mapped.x(), mapped.y());
  };
  const auto build_outline = [&to_screen](const VectorPath& source, const QTransform& transform) {
    QPainterPath outline;
    for (const auto& subpath : source.subpaths) {
      if (subpath.anchors.empty()) {
        continue;
      }
      outline.moveTo(to_screen(transform, subpath.anchors[0].anchor_x, subpath.anchors[0].anchor_y));
      const auto anchor_count = subpath.anchors.size();
      const auto segment_count = subpath.closed ? anchor_count : anchor_count - 1;
      for (std::size_t i = 0; i < segment_count; ++i) {
        const auto& a = subpath.anchors[i];
        const auto& b = subpath.anchors[(i + 1) % anchor_count];
        outline.cubicTo(to_screen(transform, a.out_x, a.out_y), to_screen(transform, b.in_x, b.in_y),
                        to_screen(transform, b.anchor_x, b.anchor_y));
      }
    }
    return outline;
  };
  draw_path_outline(painter, build_outline(*path, target_transform), accent);

  if (!editing) {
    painter.restore();
    return;
  }

  // Other panel-selected shape layers draw too (outline + hollow anchors), so
  // a multi-layer selection shows every shape's points; only the target path
  // above gets filled anchors, handles, and edits.
  if (document_ != nullptr) {
    for (const auto id : panel_selected_layer_ids_) {
      const auto* layer = std::as_const(*document_).find_layer(id);
      if (layer == nullptr || layer->vector_shape() == nullptr) {
        continue;
      }
      const auto& extra = layer->vector_shape()->path;
      if (&extra == path || extra.subpaths.empty()) {
        continue;  // the editing target already drew
      }
      const auto extra_transform = layer_preview_transform(id);
      draw_path_outline(painter, build_outline(extra, extra_transform), accent);
      const auto selected_it = extra_selected_anchors_.find(id);
      for (int s = 0; s < static_cast<int>(extra.subpaths.size()); ++s) {
        const auto& subpath = extra.subpaths[static_cast<std::size_t>(s)];
        for (int a = 0; a < static_cast<int>(subpath.anchors.size()); ++a) {
          const auto& anchor = subpath.anchors[static_cast<std::size_t>(a)];
          const auto center = to_screen(extra_transform, anchor.anchor_x, anchor.anchor_y);
          const bool selected = selected_it != extra_selected_anchors_.end() &&
                                selected_it->second.contains({s, a});
          if (path_edit_tool() == CanvasTool::PathSelect && !selected) {
            continue;  // Path Select shows unselected shapes as outlines only
          }
          draw_anchor_glyph(painter, center, selected, accent);
        }
      }
    }
  }

  // Handles of selected anchors: Direct Select's editing surface only (Path Select
  // moves whole shapes, and the Pen grabs them through its Ctrl latch, which is
  // Direct Select).
  painter.setPen(QPen(accent, 1.0));
  static const std::set<std::pair<int, int>> kNoKnobs;
  const auto& knob_anchors = path_edit_tool() == CanvasTool::DirectSelect ? path_selected_anchors_ : kNoKnobs;
  for (const auto& key : knob_anchors) {
    const auto s = static_cast<std::size_t>(key.first);
    const auto a = static_cast<std::size_t>(key.second);
    if (s >= path->subpaths.size() || a >= path->subpaths[s].anchors.size()) {
      continue;
    }
    const auto& anchor = path->subpaths[s].anchors[a];
    const auto center = to_screen(target_transform, anchor.anchor_x, anchor.anchor_y);
    const auto in_screen = to_screen(target_transform, anchor.in_x, anchor.in_y);
    const auto out_screen = to_screen(target_transform, anchor.out_x, anchor.out_y);
    draw_handle_line(painter, in_screen, center, accent);
    draw_handle_line(painter, center, out_screen, accent);
    draw_knob_glyph(painter, in_screen, accent);
    draw_knob_glyph(painter, out_screen, accent);
  }

  // Anchor squares: filled when selected, hollow otherwise. Path Select draws
  // only the selected shape's (an unselected one is just its outline, so a
  // blank click visibly deselects; GitHub issue 248).
  const bool selected_anchors_only = path_edit_tool() == CanvasTool::PathSelect;
  for (int s = 0; s < static_cast<int>(path->subpaths.size()); ++s) {
    const auto& subpath = path->subpaths[static_cast<std::size_t>(s)];
    for (int a = 0; a < static_cast<int>(subpath.anchors.size()); ++a) {
      const bool selected = path_selected_anchors_.contains({s, a});
      if (selected_anchors_only && !selected) {
        continue;
      }
      const auto center = to_screen(target_transform, subpath.anchors[static_cast<std::size_t>(a)].anchor_x,
                                    subpath.anchors[static_cast<std::size_t>(a)].anchor_y);
      draw_anchor_glyph(painter, center, selected, accent);
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

// --- Path free-transform session ---

bool CanvasWidget::path_transform_active() const noexcept {
  return path_transform_active_;
}

bool CanvasWidget::begin_path_transform() {
  if (path_transform_active_) {
    return true;  // Ctrl+T during the session is a no-op, not a restart
  }
  // Path Select / Direct Select only: the Pen's mouse handlers would keep
  // adding anchors under a session (Pen users fall through to the layer
  // transform, like Photoshop).
  if (tool_ != CanvasTool::PathSelect && tool_ != CanvasTool::DirectSelect) {
    return false;
  }
  const auto* path = path_edit_target_path();
  if (path == nullptr || path->subpaths.empty()) {
    return false;
  }
  prune_path_edit_selection(*path);
  // Direct Select with anchors selected transforms just those anchors
  // (Photoshop's Free Transform Points); otherwise the whole path.
  std::set<std::pair<int, int>> subset;
  drop_point_selection_if_target_changed();
  if (tool_ == CanvasTool::PathSelect) {
    expand_path_selection_to_groups();  // Path Select transforms its selected shapes
  }
  if (!path_selected_anchors_.empty()) {
    subset = path_selected_anchors_;
  }
  // The box wraps the anchor points and their handles (the bezier hull).
  double min_x = std::numeric_limits<double>::max();
  double min_y = std::numeric_limits<double>::max();
  double max_x = std::numeric_limits<double>::lowest();
  double max_y = std::numeric_limits<double>::lowest();
  bool any = false;
  for (int s = 0; s < static_cast<int>(path->subpaths.size()); ++s) {
    const auto& anchors = path->subpaths[static_cast<std::size_t>(s)].anchors;
    for (int a = 0; a < static_cast<int>(anchors.size()); ++a) {
      if (!subset.empty() && !subset.contains({s, a})) {
        continue;
      }
      const auto& anchor = anchors[static_cast<std::size_t>(a)];
      for (const auto& [x, y] : {std::pair{anchor.anchor_x, anchor.anchor_y},
                                 std::pair{anchor.in_x, anchor.in_y},
                                 std::pair{anchor.out_x, anchor.out_y}}) {
        min_x = std::min(min_x, x);
        min_y = std::min(min_y, y);
        max_x = std::max(max_x, x);
        max_y = std::max(max_y, y);
        any = true;
      }
    }
  }
  if (!any) {
    return false;
  }
  path_transform_active_ = true;
  path_transform_original_ = *path;
  path_transform_subset_ = std::move(subset);
  path_transform_original_rect_ = QRectF(QPointF(min_x, min_y), QPointF(max_x, max_y));
  path_transform_current_rect_ = path_transform_original_rect_;
  path_transform_angle_ = 0.0;
  path_transform_drag_handle_ = TransformHandle::None;
  path_transform_corner_offsets_ = {};
  path_transform_drag_kind_ = TransformDragKind::Scale;
  path_transform_owner_snapshot_.reset();
  path_transform_live_path_.reset();
  path_transform_owner_document_ = document_;
  path_transform_owner_is_mask_ = layer_edit_target_ == LayerEditTarget::VectorMask;
  if (path_transform_owner_is_mask_) {
    if (const auto* layer = vector_mask_target_layer(); layer != nullptr) {
      path_transform_owner_snapshot_ = std::as_const(*layer);
    }
  } else if (!active_document_path_.has_value() && text_area_edit_target_path() == nullptr) {
    if (const auto* layer = path_edit_target_layer(); layer != nullptr) {
      path_transform_owner_snapshot_ = std::as_const(*layer);
    }
  }
  if (status_callback_) {
    status_callback_(tr("Transform path: drag inside to move, handles to scale, outside to "
                        "rotate. Enter commits, Esc cancels."));
  }
  update_tool_cursor();
  update();
  return true;
}

QPointF CanvasWidget::path_transform_map_point(QPointF document_point) const {
  const auto& original = path_transform_original_rect_;
  const auto& current = path_transform_current_rect_;
  const double sx =
      std::abs(original.width()) > 1e-9 ? current.width() / original.width() : 1.0;
  const double sy =
      std::abs(original.height()) > 1e-9 ? current.height() / original.height() : 1.0;
  QPointF mapped(current.left() + (document_point.x() - original.left()) * sx,
                 current.top() + (document_point.y() - original.top()) * sy);
  if (path_transform_angle_ != 0.0) {
    const auto center = current.center();
    const double cos_a = std::cos(path_transform_angle_);
    const double sin_a = std::sin(path_transform_angle_);
    const QPointF delta = mapped - center;
    mapped = center + QPointF(delta.x() * cos_a - delta.y() * sin_a,
                              delta.x() * sin_a + delta.y() * cos_a);
  }
  return mapped;
}

QTransform CanvasWidget::path_transform_affine() const {
  const auto& original = path_transform_original_rect_;
  const auto& current = path_transform_current_rect_;
  const double sx = std::abs(original.width()) > 1e-9 ? current.width() / original.width() : 1.0;
  const double sy = std::abs(original.height()) > 1e-9 ? current.height() / original.height() : 1.0;
  auto affine = QTransform::fromTranslate(-original.left(), -original.top()) * QTransform::fromScale(sx, sy) *
                QTransform::fromTranslate(current.left(), current.top());
  if (path_transform_angle_ != 0.0) {
    const auto center = current.center();
    QTransform rotation;
    rotation.rotateRadians(path_transform_angle_);
    affine = affine * QTransform::fromTranslate(-center.x(), -center.y()) * rotation *
             QTransform::fromTranslate(center.x(), center.y());
  }
  return affine;
}

std::array<QPointF, 4> CanvasWidget::path_transform_original_corners() const {
  const auto& rect = path_transform_original_rect_;
  return {rect.topLeft(), rect.topRight(), rect.bottomRight(), rect.bottomLeft()};
}

std::array<QPointF, 4> CanvasWidget::path_transform_quad(const std::array<QPointF, 4>& offsets) const {
  const auto affine = path_transform_affine();
  const auto corners = path_transform_original_corners();
  std::array<QPointF, 4> quad{};
  for (std::size_t i = 0; i < quad.size(); ++i) {
    quad[i] = affine.map(corners[i] + offsets[i]);
  }
  return quad;
}

bool CanvasWidget::path_transform_distorted() const noexcept {
  return std::any_of(path_transform_corner_offsets_.begin(), path_transform_corner_offsets_.end(),
                     [](QPointF offset) { return !offset.isNull(); });
}

VectorPath CanvasWidget::path_transform_preview_path() const {
  if (path_transform_distorted()) {
    // Every anchor and handle through the homography onto the corner quad, the
    // map a distorted Free Transform applies to a shape layer's path.
    const auto corners = path_transform_original_corners();
    const auto quad = path_transform_quad(path_transform_corner_offsets_);
    QTransform homography;
    if (QTransform::quadToQuad(QPolygonF({corners[0], corners[1], corners[2], corners[3]}),
                               QPolygonF({quad[0], quad[1], quad[2], quad[3]}), homography)) {
      auto preview = path_transform_original_;
      for (int s = 0; s < static_cast<int>(preview.subpaths.size()); ++s) {
        auto& anchors = preview.subpaths[static_cast<std::size_t>(s)].anchors;
        for (int a = 0; a < static_cast<int>(anchors.size()); ++a) {
          if (!path_transform_subset_.empty() && !path_transform_subset_.contains({s, a})) {
            continue;
          }
          auto& anchor = anchors[static_cast<std::size_t>(a)];
          const auto mapped = homography.map(QPointF(anchor.anchor_x, anchor.anchor_y));
          const auto mapped_in = homography.map(QPointF(anchor.in_x, anchor.in_y));
          const auto mapped_out = homography.map(QPointF(anchor.out_x, anchor.out_y));
          anchor.anchor_x = mapped.x();
          anchor.anchor_y = mapped.y();
          anchor.in_x = mapped_in.x();
          anchor.in_y = mapped_in.y();
          anchor.out_x = mapped_out.x();
          anchor.out_y = mapped_out.y();
        }
      }
      return preview;
    }
  }
  auto preview = path_transform_original_;
  for (int s = 0; s < static_cast<int>(preview.subpaths.size()); ++s) {
    auto& anchors = preview.subpaths[static_cast<std::size_t>(s)].anchors;
    for (int a = 0; a < static_cast<int>(anchors.size()); ++a) {
      if (!path_transform_subset_.empty() && !path_transform_subset_.contains({s, a})) {
        continue;
      }
      auto& anchor = anchors[static_cast<std::size_t>(a)];
      const auto mapped = path_transform_map_point(QPointF(anchor.anchor_x, anchor.anchor_y));
      const auto mapped_in = path_transform_map_point(QPointF(anchor.in_x, anchor.in_y));
      const auto mapped_out = path_transform_map_point(QPointF(anchor.out_x, anchor.out_y));
      anchor.anchor_x = mapped.x();
      anchor.anchor_y = mapped.y();
      anchor.in_x = mapped_in.x();
      anchor.in_y = mapped_in.y();
      anchor.out_x = mapped_out.x();
      anchor.out_y = mapped_out.y();
    }
  }
  return preview;
}

namespace {

// The transform box corners in document space, rotated about the rect center.
std::array<QPointF, 4> rotated_rect_corners(const QRectF& rect, double angle) {
  const auto center = rect.center();
  const double cos_a = std::cos(angle);
  const double sin_a = std::sin(angle);
  const auto rotate = [&](QPointF point) {
    const QPointF delta = point - center;
    return center + QPointF(delta.x() * cos_a - delta.y() * sin_a,
                            delta.x() * sin_a + delta.y() * cos_a);
  };
  return {rotate(rect.topLeft()), rotate(rect.topRight()), rotate(rect.bottomRight()),
          rotate(rect.bottomLeft())};
}

}  // namespace

int CanvasWidget::path_transform_corner_index(TransformHandle corner) const {
  // The box corner the affine puts nearest this handle of the normalized,
  // rotated box (a flip swaps them).
  int handle_slot = 0;
  switch (corner) {
    case TransformHandle::TopLeft:
      handle_slot = 0;
      break;
    case TransformHandle::TopRight:
      handle_slot = 1;
      break;
    case TransformHandle::BottomRight:
      handle_slot = 2;
      break;
    case TransformHandle::BottomLeft:
      handle_slot = 3;
      break;
    default:
      return -1;
  }
  const auto target = rotated_rect_corners(path_transform_current_rect_.normalized(),
                                           path_transform_angle_)[static_cast<std::size_t>(handle_slot)];
  const auto mapped = path_transform_quad({});
  int best = 0;
  double best_distance = std::numeric_limits<double>::max();
  for (int i = 0; i < 4; ++i) {
    const auto offset = mapped[static_cast<std::size_t>(i)] - target;
    const auto distance = offset.x() * offset.x() + offset.y() * offset.y();
    if (distance < best_distance) {
      best = i;
      best_distance = distance;
    }
  }
  return best;
}

std::array<QPointF, 4> CanvasWidget::path_transform_handle_corners() const {
  if (!path_transform_distorted()) {
    return rotated_rect_corners(path_transform_current_rect_.normalized(), path_transform_angle_);
  }
  const auto quad = path_transform_quad(path_transform_corner_offsets_);
  std::array<QPointF, 4> corners{};
  const std::array<TransformHandle, 4> handles = {TransformHandle::TopLeft, TransformHandle::TopRight,
                                                  TransformHandle::BottomRight, TransformHandle::BottomLeft};
  for (std::size_t i = 0; i < corners.size(); ++i) {
    corners[i] = quad[static_cast<std::size_t>(path_transform_corner_index(handles[i]))];
  }
  return corners;
}

CanvasWidget::TransformHandle CanvasWidget::path_transform_handle_at(QPointF widget_point) const {
  constexpr double kHandleRadiusPx = 7.0;
  const auto rect = path_transform_current_rect_.normalized();
  const auto corners = path_transform_handle_corners();
  const auto to_screen = [this](QPointF point) {
    return path_point_to_screen(point.x(), point.y());
  };
  const auto near = [&](QPointF document_point) {
    const auto screen = to_screen(document_point);
    return std::hypot(screen.x() - widget_point.x(), screen.y() - widget_point.y()) <=
           kHandleRadiusPx;
  };
  if (near(corners[0])) {
    return TransformHandle::TopLeft;
  }
  if (near(corners[1])) {
    return TransformHandle::TopRight;
  }
  if (near(corners[2])) {
    return TransformHandle::BottomRight;
  }
  if (near(corners[3])) {
    return TransformHandle::BottomLeft;
  }
  if (near((corners[0] + corners[1]) / 2.0)) {
    return TransformHandle::Top;
  }
  if (near((corners[1] + corners[2]) / 2.0)) {
    return TransformHandle::Right;
  }
  if (near((corners[2] + corners[3]) / 2.0)) {
    return TransformHandle::Bottom;
  }
  if (near((corners[3] + corners[0]) / 2.0)) {
    return TransformHandle::Left;
  }
  // Inside (in box-local space) moves; anywhere outside rotates.
  const auto document_point = document_position_f(widget_point);
  const auto center = rect.center();
  const double cos_a = std::cos(-path_transform_angle_);
  const double sin_a = std::sin(-path_transform_angle_);
  const QPointF delta = document_point - center;
  const QPointF local = center + QPointF(delta.x() * cos_a - delta.y() * sin_a,
                                         delta.x() * sin_a + delta.y() * cos_a);
  const bool inside = path_transform_distorted()
                          ? QPolygonF({corners[0], corners[1], corners[2], corners[3]})
                                .containsPoint(document_point, Qt::OddEvenFill)
                          : rect.adjusted(-1.0, -1.0, 1.0, 1.0).contains(local);
  // A band along each edge resizes that side (GitHub issue 176, the pixel
  // session's rule): kEdgeBandPx outside the line, and inside at most a sixth of
  // the shorter on-screen side, so a small box keeps its Move interior.
  constexpr double kEdgeBandPx = 6.0;
  std::array<QPointF, 4> screen{};
  for (std::size_t i = 0; i < screen.size(); ++i) {
    screen[i] = to_screen(corners[i]);
  }
  const auto side_length = [&](std::size_t a, std::size_t b) {
    return std::hypot(screen[b].x() - screen[a].x(), screen[b].y() - screen[a].y());
  };
  const auto inner_band = std::min(kEdgeBandPx, std::min(side_length(0, 1), side_length(1, 2)) / 6.0);
  const std::array<TransformHandle, 4> edges = {TransformHandle::Top, TransformHandle::Right, TransformHandle::Bottom,
                                                TransformHandle::Left};
  for (std::size_t i = 0; i < edges.size(); ++i) {
    const auto a = screen[i];
    const auto b = screen[(i + 1) % screen.size()];
    const QPointF ab = b - a;
    const auto length_squared = ab.x() * ab.x() + ab.y() * ab.y();
    if (length_squared < 1e-9) {
      continue;
    }
    const QPointF ap = widget_point - a;
    const auto t = (ap.x() * ab.x() + ap.y() * ab.y()) / length_squared;
    if (t < 0.0 || t > 1.0) {
      continue;
    }
    const auto distance = std::abs(ap.x() * ab.y() - ap.y() * ab.x()) / std::sqrt(length_squared);
    if (distance <= (inside ? inner_band : kEdgeBandPx)) {
      return edges[i];
    }
  }
  return inside ? TransformHandle::Move : TransformHandle::Rotate;
}

void CanvasWidget::apply_path_transform_cursor(QPoint widget_point, Qt::KeyboardModifiers modifiers) {
  path_transform_cursor_modifiers_ = modifiers;
  const auto rect = path_transform_current_rect_.normalized();
  const auto degrees = path_transform_angle_ * 180.0 / std::numbers::pi;
  // Mid-drag the grabbed zone keeps its cursor wherever the pointer goes.
  const bool dragging = path_transform_drag_handle_ != TransformHandle::None;
  const auto handle = dragging ? path_transform_drag_handle_ : path_transform_handle_at(QPointF(widget_point));
  // Free Transform's gesture cursors (GitHub issue 225) where the held modifiers
  // make a corner or side drag a distort, skew or perspective.
  if ((transform_handle_is_corner(handle) || transform_handle_is_edge(handle)) &&
      apply_transform_gesture_cursor(dragging ? path_transform_drag_kind_
                                              : transform_drag_kind_for_mode(handle, modifiers,
                                                                             TransformDistortMode::None))) {
    return;
  }
  if (!path_transform_distorted()) {
    if (handle == TransformHandle::Rotate) {
      setCursor(transform_rotate_cursor_at(widget_point, rect, degrees, false));  // no stem handle here
    } else {
      set_transform_cursor_for_handle(handle, degrees);
    }
    return;
  }
  // A distorted box: directions come from the true quad, as in the pixel session.
  const auto document_corners = path_transform_handle_corners();
  std::array<QPointF, 4> screen{};
  for (std::size_t i = 0; i < screen.size(); ++i) {
    screen[i] = path_point_to_screen(document_corners[i].x(), document_corners[i].y());
  }
  if (handle == TransformHandle::Rotate) {
    auto best = TransformHandle::TopLeft;
    auto best_distance = std::numeric_limits<double>::max();
    const std::array<TransformHandle, 4> corners = {TransformHandle::TopLeft, TransformHandle::TopRight,
                                                    TransformHandle::BottomRight, TransformHandle::BottomLeft};
    for (std::size_t i = 0; i < corners.size(); ++i) {
      const auto distance = std::hypot(widget_point.x() - screen[i].x(), widget_point.y() - screen[i].y());
      if (distance < best_distance) {
        best = corners[i];
        best_distance = distance;
      }
    }
    setCursor(corner_rotate_cursor(transform_quad_handle_direction(screen, best)));
    return;
  }
  if (transform_handle_is_corner(handle) || transform_handle_is_edge(handle)) {
    // Right's base direction is 0 degrees, so the handle's own direction picks the shape.
    set_transform_cursor_for_handle(TransformHandle::Right, transform_quad_handle_direction(screen, handle));
    return;
  }
  set_transform_cursor_for_handle(handle);
}

bool CanvasWidget::handle_path_transform_press(QPointF document_point, QPointF widget_point,
                                               Qt::KeyboardModifiers modifiers) {
  path_transform_drag_handle_ = path_transform_handle_at(widget_point);
  path_transform_drag_start_rect_ = path_transform_current_rect_;
  path_transform_drag_start_document_ = document_point;
  path_transform_drag_start_angle_ = path_transform_angle_;
  // Ctrl distorts a corner or moves a side, Ctrl+Shift slides a side (skew),
  // Ctrl+Alt+Shift drags a corner in perspective: Free Transform's gestures
  // (GitHub issue 225), here on every anchor and handle (GitHub issue 248).
  path_transform_drag_kind_ =
      transform_drag_kind_for_mode(path_transform_drag_handle_, modifiers, TransformDistortMode::None);
  if (path_transform_drag_kind_ != TransformDragKind::Scale) {
    path_transform_drag_start_offsets_ = path_transform_corner_offsets_;
    const auto quad = path_transform_quad(path_transform_corner_offsets_);
    QPointF grabbed;
    if (transform_handle_is_corner(path_transform_drag_handle_)) {
      grabbed = quad[static_cast<std::size_t>(path_transform_corner_index(path_transform_drag_handle_))];
    } else {
      const auto [first, second] = transform_edge_corner_handles(path_transform_drag_handle_);
      grabbed = (quad[static_cast<std::size_t>(path_transform_corner_index(first))] +
                 quad[static_cast<std::size_t>(path_transform_corner_index(second))]) /
                2.0;
    }
    path_transform_drag_grab_offset_ = grabbed - document_point;
  }
  apply_path_transform_cursor(widget_point.toPoint(), modifiers);
  return true;
}

bool CanvasWidget::handle_path_transform_move(QMouseEvent* event, QPointF document_point) {
  if (path_transform_drag_handle_ == TransformHandle::None ||
      (event->buttons() & Qt::LeftButton) == 0) {
    return true;  // hover only
  }
  if (path_transform_drag_kind_ != TransformDragKind::Scale) {
    // The box's affine stays fixed; the travel lands in the corner offsets in the
    // original box's frame, and Alt (read live) mirrors it about the center.
    bool invertible = false;
    const auto inverse = path_transform_affine().inverted(&invertible);
    if (!invertible) {
      return true;
    }
    const auto start = path_transform_drag_start_document_ + path_transform_drag_grab_offset_;
    const auto now = document_point + path_transform_drag_grab_offset_;
    const auto [first, second] = transform_edge_corner_handles(path_transform_drag_handle_);
    const auto offsets = transform_distorted_offsets(
        path_transform_drag_kind_, path_transform_corner_index(path_transform_drag_handle_),
        {path_transform_corner_index(first), path_transform_corner_index(second)},
        inverse.map(now) - inverse.map(start), (event->modifiers() & Qt::AltModifier) != 0,
        path_transform_drag_start_offsets_, path_transform_original_corners());
    // Never fold the quad (the #225 guard): a concave, self-crossing, collapsed or
    // inside-out frame keeps the last good shape.
    const auto start_winding = transform_quad_winding(path_transform_quad(path_transform_drag_start_offsets_));
    if (const auto winding = transform_quad_winding(path_transform_quad(offsets));
        winding == 0 || (start_winding != 0 && winding != start_winding)) {
      return true;
    }
    path_transform_corner_offsets_ = offsets;
    refresh_path_transform_live_fill();
    update();
    return true;
  }
  const auto delta = document_point - path_transform_drag_start_document_;
  auto rect = path_transform_drag_start_rect_;
  switch (path_transform_drag_handle_) {
    case TransformHandle::Move:
      rect.translate(delta);
      break;
    case TransformHandle::Rotate: {
      const auto center = path_transform_drag_start_rect_.normalized().center();
      const auto start = path_transform_drag_start_document_ - center;
      const auto now = document_point - center;
      if (std::hypot(start.x(), start.y()) > 1e-6 && std::hypot(now.x(), now.y()) > 1e-6) {
        auto angle = path_transform_drag_start_angle_ +
                     std::atan2(now.y(), now.x()) - std::atan2(start.y(), start.x());
        if ((event->modifiers() & Qt::ShiftModifier) != 0) {
          constexpr double kSnap = 15.0 * 3.14159265358979323846 / 180.0;
          angle = std::round(angle / kSnap) * kSnap;
        }
        path_transform_angle_ = angle;
      }
      refresh_path_transform_live_fill();
      update();
      return true;
    }
    default: {
      // Resize in box-local axes: rotate the drag delta back so the handles
      // stay attached to the rotated box edges.
      const double cos_a = std::cos(-path_transform_drag_start_angle_);
      const double sin_a = std::sin(-path_transform_drag_start_angle_);
      QPointF local_delta(delta.x() * cos_a - delta.y() * sin_a,
                          delta.x() * sin_a + delta.y() * cos_a);
      const bool moves_left = path_transform_drag_handle_ == TransformHandle::TopLeft ||
                              path_transform_drag_handle_ == TransformHandle::Left ||
                              path_transform_drag_handle_ == TransformHandle::BottomLeft;
      const bool moves_right = path_transform_drag_handle_ == TransformHandle::TopRight ||
                               path_transform_drag_handle_ == TransformHandle::Right ||
                               path_transform_drag_handle_ == TransformHandle::BottomRight;
      const bool moves_top = path_transform_drag_handle_ == TransformHandle::TopLeft ||
                             path_transform_drag_handle_ == TransformHandle::Top ||
                             path_transform_drag_handle_ == TransformHandle::TopRight;
      const bool moves_bottom = path_transform_drag_handle_ == TransformHandle::BottomLeft ||
                                path_transform_drag_handle_ == TransformHandle::Bottom ||
                                path_transform_drag_handle_ == TransformHandle::BottomRight;
      if (transform_drag_keeps_aspect(event->modifiers()) && (moves_left || moves_right) &&
          (moves_top || moves_bottom) && std::abs(path_transform_drag_start_rect_.width()) > 1e-9 &&
          std::abs(path_transform_drag_start_rect_.height()) > 1e-9) {
        // Aspect-locked corner: one shared magnitude from the per-axis scales
        // projected onto the box diagonal, each axis keeping its own sign, as
        // in the pixel session (update_free_transform_preview), so neither
        // axis is ignored and a pull straight across the anchor mirrors that
        // axis alone.
        const double width = std::abs(path_transform_drag_start_rect_.width());
        const double height = std::abs(path_transform_drag_start_rect_.height());
        const double scale_x = 1.0 + (moves_left ? -local_delta.x() : local_delta.x()) / width;
        const double scale_y = 1.0 + (moves_top ? -local_delta.y() : local_delta.y()) / height;
        const double magnitude = (std::abs(scale_x) * width * width + std::abs(scale_y) * height * height) /
                                 (width * width + height * height);
        const double factor_x = scale_x < 0.0 ? -magnitude : magnitude;
        const double factor_y = scale_y < 0.0 ? -magnitude : magnitude;
        local_delta.setX((moves_left ? -1.0 : 1.0) * (factor_x - 1.0) * width);
        local_delta.setY((moves_top ? -1.0 : 1.0) * (factor_y - 1.0) * height);
      }
      if (moves_left) {
        rect.setLeft(rect.left() + local_delta.x());
      }
      if (moves_right) {
        rect.setRight(rect.right() + local_delta.x());
      }
      if (moves_top) {
        rect.setTop(rect.top() + local_delta.y());
      }
      if (moves_bottom) {
        rect.setBottom(rect.bottom() + local_delta.y());
      }
      break;
    }
  }
  path_transform_current_rect_ = rect;
  refresh_path_transform_live_fill();
  update();
  return true;
}

bool CanvasWidget::handle_path_transform_key(QKeyEvent* event) {
  switch (event->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
      commit_path_transform();
      return true;
    case Qt::Key_Escape:
      cancel_path_transform();
      return true;
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Up:
    case Qt::Key_Down: {
      const double step = (event->modifiers() & Qt::ShiftModifier) != 0 ? 10.0 : 1.0;
      path_transform_current_rect_.translate(
          event->key() == Qt::Key_Left    ? -step
          : event->key() == Qt::Key_Right ? step
                                          : 0.0,
          event->key() == Qt::Key_Up     ? -step
          : event->key() == Qt::Key_Down ? step
                                         : 0.0);
      refresh_path_transform_live_fill();
      update();
      return true;
    }
    default:
      return false;
  }
}

void CanvasWidget::commit_path_transform() {
  if (!path_transform_active_) {
    return;
  }
  const bool changed = path_transform_current_rect_ != path_transform_original_rect_ ||
                       path_transform_angle_ != 0.0 || path_transform_distorted();
  auto transformed = path_transform_preview_path();
  const auto touched_groups = path_transform_touched_groups(transformed);
  // The live fill's writes were un-armed: put the original back so the undo
  // snapshot the commit arms holds it, then apply the result as one edit.
  restore_path_transform_owner();
  path_transform_active_ = false;
  path_transform_drag_handle_ = TransformHandle::None;
  if (changed) {
    path_edit_undo_armed_ = false;
    apply_path_edit(std::move(transformed), tr("Transform path"), touched_groups);
    path_edit_undo_armed_ = false;
    if (status_callback_) {
      status_callback_(tr("Transformed the path"));
    }
  }
  path_transform_original_ = VectorPath{};
  path_transform_subset_.clear();
  update_tool_cursor();
  update();
}

std::vector<int> CanvasWidget::path_transform_touched_groups(const VectorPath& transformed) const {
  // Direct edits drop the touched groups' live-shape annotations
  // (keyShapeInvalidated); collect the groups the transform touched.
  std::vector<int> touched_groups;
  for (int s = 0; s < static_cast<int>(transformed.subpaths.size()); ++s) {
    const auto& subpath = transformed.subpaths[static_cast<std::size_t>(s)];
    bool touched = path_transform_subset_.empty();
    if (!touched) {
      for (const auto& key : path_transform_subset_) {
        if (key.first == s) {
          touched = true;
          break;
        }
      }
    }
    if (touched && std::find(touched_groups.begin(), touched_groups.end(), subpath.shape_group) ==
                       touched_groups.end()) {
      touched_groups.push_back(subpath.shape_group);
    }
  }
  return touched_groups;
}

void CanvasWidget::refresh_path_transform_live_fill() {
  if (!path_transform_active_ || !path_transform_owner_snapshot_.has_value() || document_ == nullptr ||
      document_ != path_transform_owner_document_) {
    return;  // work paths and saved paths have no fill: the outline preview is all
  }
  auto preview = path_transform_preview_path();
  if (path_transform_live_path_.has_value() && *path_transform_live_path_ == preview) {
    return;
  }
  // One layer's re-raster per change, un-armed (the Direct Select drag cost);
  // the commit restores and re-applies it as the session's one undo entry.
  const auto touched_groups = path_transform_touched_groups(preview);
  path_transform_live_path_ = preview;
  replace_path_edit_target(std::move(preview), touched_groups);
}

void CanvasWidget::restore_path_transform_owner() {
  const auto snapshot = std::move(path_transform_owner_snapshot_);
  const auto written = std::move(path_transform_live_path_);
  path_transform_owner_snapshot_.reset();
  path_transform_live_path_.reset();
  if (!snapshot.has_value() || !written.has_value() || document_ == nullptr ||
      document_ != path_transform_owner_document_) {
    return;
  }
  auto* layer = document_->find_layer(snapshot->id());
  if (layer == nullptr) {
    return;
  }
  // Only while the layer still holds the session's last write: anything that
  // replaced it meanwhile (a history restore) wins.
  const auto& current = std::as_const(*layer);
  const auto* current_path = path_transform_owner_is_mask_
                                 ? (current.vector_mask() != nullptr ? &current.vector_mask()->path : nullptr)
                                 : (current.vector_shape() != nullptr ? &current.vector_shape()->path : nullptr);
  if (current_path == nullptr || *current_path != *written) {
    return;
  }
  const auto old_effect_rect = to_qrect(layer_bounds_with_effects(current, current.bounds()));
  *layer = *snapshot;
  document_changed_effect_bounds(old_effect_rect.united(
      to_qrect(layer_bounds_with_effects(std::as_const(*layer), std::as_const(*layer).bounds()))));
  if (path_edited_callback_) {
    path_edited_callback_();
  }
}

void CanvasWidget::cancel_path_transform() {
  if (!path_transform_active_) {
    return;
  }
  restore_path_transform_owner();
  path_transform_active_ = false;
  path_transform_drag_handle_ = TransformHandle::None;
  path_transform_original_ = VectorPath{};
  path_transform_subset_.clear();
  if (status_callback_) {
    status_callback_(tr("Cancelled the path transform"));
  }
  update_tool_cursor();
  update();
}

void CanvasWidget::draw_path_transform_overlay(QPainter& painter) {
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);
  const QColor accent = path_overlay_accent();

  // The transformed path outline.
  const auto preview = path_transform_preview_path();
  QPainterPath outline;
  for (const auto& subpath : preview.subpaths) {
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

  // The box (rotated, or the distorted quad) and its eight handles.
  const auto corners = path_transform_handle_corners();
  QPolygonF box;
  for (const auto& corner : corners) {
    box << path_point_to_screen(corner.x(), corner.y());
  }
  painter.setPen(QPen(QColor(95, 170, 255), 1.0, Qt::DashLine));
  painter.drawPolygon(box);
  painter.setPen(QPen(QColor(30, 34, 40), 1.0));
  painter.setBrush(QBrush(QColor(245, 248, 252)));
  const auto draw_handle = [&](QPointF document_point) {
    const auto screen = path_point_to_screen(document_point.x(), document_point.y());
    painter.drawRect(QRectF(screen.x() - 3.0, screen.y() - 3.0, 6.0, 6.0));
  };
  for (std::size_t i = 0; i < corners.size(); ++i) {
    draw_handle(corners[i]);
    draw_handle((corners[i] + corners[(i + 1) % corners.size()]) / 2.0);
  }
  painter.restore();
}

}  // namespace patchy::ui
