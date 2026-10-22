// CanvasWidget's Perspective Crop session (GitHub issue 328, docs/crop-tool.md). A
// drag lays out a box whose four corners then move one at a time, or four clicks
// place the corners directly; on a finished quad an edge moves its two corners, the
// inside moves it whole and the reach outside a corner rotates it. A marker on the
// edge that becomes the result's top shows its orientation. Enter
// or Apply hands the quad to MainWindow through the commit callback, which rectifies
// the document onto a rectangle (perspective_crop_document); Esc and the X clear the
// quad. A tool switch, an edit lock or a document swap cancel it, like Crop.

#include "ui/canvas_widget.hpp"

#include "ui/canvas_types.hpp"
#include "ui/tool_cursors.hpp"

#include <QApplication>
#include <QColor>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QTransform>
#include <Qt>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <numbers>
#include <optional>
#include <utility>

namespace patchy::ui {

namespace {

constexpr double kPerspectiveCropHandleSize = 8.0;

// Corners in output order: the one nearest the top left first, then clockwise on
// screen, so the result is upright whichever corner was clicked first and in which
// direction the clicks went round.
std::array<QPointF, 4> upright_quad(std::array<QPointF, 4> quad) {
  double signed_area = 0.0;
  for (std::size_t i = 0; i < 4U; ++i) {
    const auto& a = quad[i];
    const auto& b = quad[(i + 1U) % 4U];
    signed_area += a.x() * b.y() - b.x() * a.y();
  }
  if (signed_area < 0.0) {
    // Counterclockwise on screen (y down): reverse, keeping the first corner.
    std::swap(quad[1], quad[3]);
  }
  std::size_t first = 0;
  for (std::size_t i = 1; i < 4U; ++i) {
    if (quad[i].x() + quad[i].y() < quad[first].x() + quad[first].y()) {
      first = i;
    }
  }
  std::rotate(quad.begin(), quad.begin() + static_cast<std::ptrdiff_t>(first), quad.end());
  return quad;
}

}  // namespace

bool CanvasWidget::perspective_crop_session_active() const noexcept {
  return perspective_crop_points_.size() == 4U;
}

bool CanvasWidget::perspective_crop_session_pending() const noexcept {
  return !perspective_crop_points_.empty() || perspective_crop_dragging_out_;
}

std::optional<std::array<QPointF, 4>> CanvasWidget::perspective_crop_quad() const {
  if (!perspective_crop_session_active()) {
    return std::nullopt;
  }
  return std::array<QPointF, 4>{perspective_crop_points_[0], perspective_crop_points_[1],
                                perspective_crop_points_[2], perspective_crop_points_[3]};
}

bool CanvasWidget::set_perspective_crop_quad(const std::array<QPointF, 4>& quad) {
  if (tool_ != CanvasTool::PerspectiveCrop || document_ == nullptr || edit_locked_ ||
      transform_quad_winding(quad) == 0) {
    return false;
  }
  const auto upright = upright_quad(quad);
  perspective_crop_points_.assign(upright.begin(), upright.end());
  perspective_crop_dragging_out_ = false;
  perspective_crop_drag_part_ = -1;
  update_tool_cursor();
  update();
  notify_crop_session_changed();
  return true;
}

void CanvasWidget::set_perspective_crop_commit_requested_callback(
    std::function<void(std::array<QPointF, 4>)> callback) {
  perspective_crop_commit_requested_callback_ = std::move(callback);
}

void CanvasWidget::set_perspective_crop_show_grid(bool show) {
  if (perspective_crop_show_grid_ == show) {
    return;
  }
  perspective_crop_show_grid_ = show;
  if (perspective_crop_session_active()) {
    update();
  }
}

bool CanvasWidget::perspective_crop_show_grid() const noexcept {
  return perspective_crop_show_grid_;
}

void CanvasWidget::commit_perspective_crop_session() {
  const auto quad = perspective_crop_quad();
  if (!quad.has_value()) {
    if (status_callback_) {
      status_callback_(tr("Nothing to crop: place four corners first"));
    }
    return;
  }
  // The quad stays until MainWindow cancels it after a successful commit, so a
  // refusal (smart filters, artboards) leaves it adjustable.
  if (perspective_crop_commit_requested_callback_) {
    perspective_crop_commit_requested_callback_(*quad);
  }
}

void CanvasWidget::cancel_perspective_crop_session() {
  if (!perspective_crop_session_pending()) {
    return;
  }
  const auto had_quad = perspective_crop_session_active();
  perspective_crop_points_.clear();
  perspective_crop_dragging_out_ = false;
  perspective_crop_drag_part_ = -1;
  update_tool_cursor();
  update();
  if (had_quad && status_callback_) {
    status_callback_(tr("Perspective crop cancelled"));
  }
  notify_crop_session_changed();
}

std::array<QPointF, 4> CanvasWidget::perspective_crop_widget_quad() const {
  std::array<QPointF, 4> quad{};
  for (std::size_t i = 0; i < 4U && i < perspective_crop_points_.size(); ++i) {
    quad[i] = widget_position_f(perspective_crop_points_[i]);
  }
  return quad;
}

int CanvasWidget::perspective_crop_part_at(QPoint widget_point) const {
  if (!perspective_crop_session_active()) {
    return kPerspectiveCropPartNone;
  }
  // Free Transform's quad hit zones (quad_handle_at), in output corner order: the
  // handle names follow the result, whichever way the quad is turned on screen.
  switch (quad_handle_at(perspective_crop_widget_quad(), std::nullopt, QPointF(widget_point))) {
    case TransformHandle::TopLeft:
      return 0;
    case TransformHandle::TopRight:
      return 1;
    case TransformHandle::BottomRight:
      return 2;
    case TransformHandle::BottomLeft:
      return 3;
    case TransformHandle::Move:
      return kPerspectiveCropPartMove;
    case TransformHandle::Top:
      return kPerspectiveCropPartEdge + 0;
    case TransformHandle::Right:
      return kPerspectiveCropPartEdge + 1;
    case TransformHandle::Bottom:
      return kPerspectiveCropPartEdge + 2;
    case TransformHandle::Left:
      return kPerspectiveCropPartEdge + 3;
    case TransformHandle::Rotate:
      return kPerspectiveCropPartRotate;
    default:
      return kPerspectiveCropPartNone;
  }
}

void CanvasWidget::turn_perspective_crop_top() {
  if (!perspective_crop_session_active()) {
    return;
  }
  // The left edge becomes the top: the result turns a quarter clockwise.
  std::rotate(perspective_crop_points_.rbegin(), perspective_crop_points_.rbegin() + 1,
              perspective_crop_points_.rend());
  update();
  notify_crop_session_changed();
}

void CanvasWidget::handle_perspective_crop_press(QMouseEvent* event) {
  const auto document_point = document_position_f(event->position());
  if (perspective_crop_session_active()) {
    // A corner moves on its own, an edge moves its two corners, the inside moves the
    // whole quad, and the reach just outside a corner rotates it about its center. A
    // press elsewhere keeps the quad (Esc clears it).
    const auto part = perspective_crop_part_at(event->pos());
    if (part == kPerspectiveCropPartNone) {
      return;
    }
    perspective_crop_drag_part_ = part;
    perspective_crop_drag_start_point_ = document_point;
    std::copy(perspective_crop_points_.begin(), perspective_crop_points_.end(),
              perspective_crop_drag_start_quad_.begin());
    if (part == kPerspectiveCropPartRotate) {
      const auto& start = perspective_crop_drag_start_quad_;
      perspective_crop_rotate_center_ = (start[0] + start[1] + start[2] + start[3]) / 4.0;
      perspective_crop_rotate_start_degrees_ =
          std::atan2(document_point.y() - perspective_crop_rotate_center_.y(),
                     document_point.x() - perspective_crop_rotate_center_.x()) *
          180.0 / std::numbers::pi;
    }
    update_perspective_crop_cursor(event->pos());
    return;
  }
  if (!perspective_crop_points_.empty()) {
    // Click placement: the next corner. The fourth closes the quad.
    perspective_crop_points_.push_back(document_point);
    perspective_crop_hover_document_ = document_point;
    if (perspective_crop_points_.size() == 4U) {
      const std::array<QPointF, 4> quad{perspective_crop_points_[0], perspective_crop_points_[1],
                                        perspective_crop_points_[2], perspective_crop_points_[3]};
      if (transform_quad_winding(quad) == 0) {
        perspective_crop_points_.pop_back();
        report_status_error(tr("The corners must form a convex shape"));
      } else {
        const auto upright = upright_quad(quad);
        perspective_crop_points_.assign(upright.begin(), upright.end());
      }
    }
    update_tool_cursor();
    update();
    notify_crop_session_changed();
    return;
  }
  // First press: a drag lays out a box, a click places the first corner.
  perspective_crop_dragging_out_ = true;
  perspective_crop_press_widget_point_ = event->pos();
  perspective_crop_anchor_document_ = document_point;
  perspective_crop_hover_document_ = document_point;
  update();
}

void CanvasWidget::update_perspective_crop_drag(QPointF document_point, Qt::KeyboardModifiers modifiers) {
  perspective_crop_hover_document_ = document_point;
  if (perspective_crop_dragging_out_) {
    update();
    return;
  }
  if (perspective_crop_drag_part_ < 0 || !perspective_crop_session_active()) {
    // Only corners being clicked in draw a rubber band to the pointer; a hover over
    // a finished quad changes nothing on screen.
    if (!perspective_crop_session_active()) {
      update();
    }
    return;
  }
  const auto delta = document_point - perspective_crop_drag_start_point_;
  auto quad = perspective_crop_drag_start_quad_;
  const auto part = perspective_crop_drag_part_;
  if (part == kPerspectiveCropPartMove) {
    for (auto& corner : quad) {
      corner += delta;
    }
  } else if (part == kPerspectiveCropPartRotate) {
    // About the quad's center, Shift snapping the turn to 15 degrees like Crop's and
    // Free Transform's rotate drags. The corners keep their output order, so the
    // result's top turns with the quad.
    const auto& center = perspective_crop_rotate_center_;
    auto degrees = std::atan2(document_point.y() - center.y(), document_point.x() - center.x()) * 180.0 /
                       std::numbers::pi -
                   perspective_crop_rotate_start_degrees_;
    if (modifiers.testFlag(Qt::ShiftModifier)) {
      degrees = std::round(degrees / 15.0) * 15.0;
    }
    QTransform turn;
    turn.translate(center.x(), center.y());
    turn.rotate(degrees);
    turn.translate(-center.x(), -center.y());
    for (auto& corner : quad) {
      corner = turn.map(corner);
    }
  } else {
    if (part >= kPerspectiveCropPartEdge) {
      // An edge moves its two corners together: the side translates, the
      // perspective of the others follows.
      const auto edge = static_cast<std::size_t>(part - kPerspectiveCropPartEdge);
      quad[edge] += delta;
      quad[(edge + 1U) % 4U] += delta;
    } else {
      quad[static_cast<std::size_t>(part)] += delta;
    }
    // The quad never folds: a frame that would make it concave or turn it inside
    // out keeps the last good shape (Free Transform's distort rule).
    if (transform_quad_winding(quad) != transform_quad_winding(perspective_crop_drag_start_quad_)) {
      return;
    }
  }
  std::copy(quad.begin(), quad.end(), perspective_crop_points_.begin());
  update();
}

void CanvasWidget::finish_perspective_crop_mouse_release(QMouseEvent* event) {
  if (perspective_crop_dragging_out_) {
    perspective_crop_dragging_out_ = false;
    const auto widget_delta = event->pos() - perspective_crop_press_widget_point_;
    const auto end = document_position_f(event->position());
    if (widget_delta.manhattanLength() < QApplication::startDragDistance()) {
      perspective_crop_points_ = {perspective_crop_anchor_document_};
      perspective_crop_hover_document_ = end;
    } else {
      const QRectF box = QRectF(perspective_crop_anchor_document_, end).normalized();
      perspective_crop_points_ = {box.topLeft(), box.topRight(), box.bottomRight(), box.bottomLeft()};
    }
    update_tool_cursor();
    update();
    notify_crop_session_changed();
    return;
  }
  if (perspective_crop_drag_part_ >= 0) {
    // The corners keep their order: the top marker stays on the edge it was on, so
    // a turned quad turns the result.
    update_perspective_crop_drag(document_position_f(event->position()), event->modifiers());
    perspective_crop_drag_part_ = kPerspectiveCropPartNone;
    update_perspective_crop_cursor(event->pos());
    update();
    notify_crop_session_changed();
  }
}

void CanvasWidget::update_perspective_crop_cursor(QPoint widget_point) {
  const auto part =
      perspective_crop_drag_part_ >= 0 ? perspective_crop_drag_part_ : perspective_crop_part_at(widget_point);
  const auto quad = perspective_crop_widget_quad();
  constexpr std::array<TransformHandle, 4> kCornerHandles{TransformHandle::TopLeft, TransformHandle::TopRight,
                                                          TransformHandle::BottomRight, TransformHandle::BottomLeft};
  constexpr std::array<TransformHandle, 4> kEdgeHandles{TransformHandle::Top, TransformHandle::Right,
                                                        TransformHandle::Bottom, TransformHandle::Left};
  if (part == kPerspectiveCropPartMove) {
    setCursor(Qt::SizeAllCursor);
  } else if (part >= 0 && part < 4) {
    setCursor(transform_gesture_cursor(TransformGestureCursor::Distort));
  } else if (part >= kPerspectiveCropPartEdge && part < kPerspectiveCropPartEdge + 4) {
    // Right's base direction is 0 degrees, so the edge's own direction picks the shape
    // (the distorted Free Transform box's rule).
    set_transform_cursor_for_handle(
        TransformHandle::Right,
        transform_quad_handle_direction(quad, kEdgeHandles[static_cast<std::size_t>(part - kPerspectiveCropPartEdge)]));
  } else if (part == kPerspectiveCropPartRotate) {
    // The corner the pointer is nearest picks the rotate cursor's orientation.
    std::size_t nearest = 0;
    double nearest_distance = std::numeric_limits<double>::max();
    for (std::size_t i = 0; i < 4U; ++i) {
      const auto distance = std::hypot(widget_point.x() - quad[i].x(), widget_point.y() - quad[i].y());
      if (distance < nearest_distance) {
        nearest = i;
        nearest_distance = distance;
      }
    }
    setCursor(corner_rotate_cursor(transform_quad_handle_direction(quad, kCornerHandles[nearest])));
  } else {
    setCursor(Qt::CrossCursor);
  }
}

void CanvasWidget::draw_perspective_crop_overlay(QPainter& painter) const {
  if (tool_ != CanvasTool::PerspectiveCrop || !perspective_crop_session_pending()) {
    return;
  }
  // Same unthemed overlay inks as the Crop box (docs/crop-tool.md): they sit on
  // artwork, not chrome.
  const QColor box_color(95, 170, 255);
  const QColor handle_ink(10, 14, 20);
  const QColor handle_fill(245, 248, 252);

  QPolygonF quad;
  if (perspective_crop_dragging_out_) {
    const auto box = QRectF(widget_position_f(perspective_crop_anchor_document_),
                            widget_position_f(perspective_crop_hover_document_))
                         .normalized();
    quad << box.topLeft() << box.topRight() << box.bottomRight() << box.bottomLeft();
  } else {
    for (const auto& point : perspective_crop_points_) {
      quad << widget_position_f(point);
    }
  }

  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);
  if (!perspective_crop_dragging_out_ && !perspective_crop_session_active()) {
    // Corners being clicked in: the placed outline plus a rubber band to the pointer.
    QPolygonF band = quad;
    band << widget_position_f(perspective_crop_hover_document_);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(box_color, 1.0, Qt::DashLine));
    painter.drawPolyline(band);
  } else {
    const auto& options = crop_overlay_options_;
    if (options.show_cropped_area && options.shield_enabled && options.shield_opacity_percent > 0) {
      QColor shield_color = options.shield_color;
      shield_color.setAlpha(
          static_cast<int>(std::lround(std::clamp(options.shield_opacity_percent, 0, 100) * 255.0 / 100.0)));
      QPainterPath shield;
      shield.addRect(QRectF(rect()));
      QPainterPath inside;
      inside.addPolygon(quad);
      inside.closeSubpath();
      painter.fillPath(shield.subtracted(inside), shield_color);
    }
    QTransform unit_to_widget;
    if (perspective_crop_show_grid_ && QTransform::squareToQuad(quad, unit_to_widget)) {
      // Lines of the rectified grid, straight under the homography; about one
      // per 40 widget pixels of the longer side.
      double longest = 0.0;
      for (int i = 0; i < 4; ++i) {
        longest = std::max(longest, QLineF(quad[i], quad[(i + 1) % 4]).length());
      }
      const int divisions = std::clamp(static_cast<int>(longest / 40.0), 2, 12);
      QPen dark_pen(QColor(10, 14, 20, 110), 1.0);
      dark_pen.setCosmetic(true);
      QPen light_pen(QColor(245, 248, 252, 110), 1.0);
      light_pen.setCosmetic(true);
      for (int pass = 0; pass < 2; ++pass) {
        // Dark under light, the dark twin one pixel down and right.
        painter.setPen(pass == 0 ? dark_pen : light_pen);
        const QPointF nudge = pass == 0 ? QPointF(1.0, 1.0) : QPointF();
        for (int i = 1; i < divisions; ++i) {
          const double t = static_cast<double>(i) / divisions;
          painter.drawLine(QLineF(unit_to_widget.map(QPointF(t, 0.0)), unit_to_widget.map(QPointF(t, 1.0)))
                               .translated(nudge));
          painter.drawLine(QLineF(unit_to_widget.map(QPointF(0.0, t)), unit_to_widget.map(QPointF(1.0, t)))
                               .translated(nudge));
        }
      }
    }
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(box_color, 1.0, Qt::DashLine));
    painter.drawPolygon(quad);
    if (!perspective_crop_dragging_out_) {
      draw_perspective_crop_up_marker(painter, quad);
    }
  }
  if (!perspective_crop_dragging_out_) {
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(QPen(handle_ink, 1.0));
    painter.setBrush(handle_fill);
    const auto draw_handle = [&painter](QPointF point) {
      painter.drawRect(QRectF(point.x() - kPerspectiveCropHandleSize / 2.0,
                              point.y() - kPerspectiveCropHandleSize / 2.0, kPerspectiveCropHandleSize,
                              kPerspectiveCropHandleSize));
    };
    for (const auto& point : quad) {
      draw_handle(point);
    }
    if (perspective_crop_session_active()) {
      // Mid-edge handles: an edge drag moves its two corners together.
      for (int i = 0; i < 4; ++i) {
        draw_handle((quad[i] + quad[(i + 1) % 4]) / 2.0);
      }
    }
  }
  painter.restore();
}

void CanvasWidget::draw_perspective_crop_up_marker(QPainter& painter, const QPolygonF& quad) const {
  if (quad.size() != 4) {
    return;
  }
  // The edge that becomes the result's top (corners 0 and 1 in output order) gets a
  // solid light stroke over a dark one, and an arrow outside its middle points away
  // from the quad: the same light-over-dark pairing as the marching ants, so it reads
  // on dark and light artwork alike.
  const QPointF top_left = quad[0];
  const QPointF top_right = quad[1];
  const QPointF middle = (top_left + top_right) / 2.0;
  const QPointF center = (quad[0] + quad[1] + quad[2] + quad[3]) / 4.0;
  const QPointF along = top_right - top_left;
  const double length = std::hypot(along.x(), along.y());
  if (length < 1.0) {
    return;
  }
  QPointF outward(along.y() / length, -along.x() / length);
  if ((middle.x() - center.x()) * outward.x() + (middle.y() - center.y()) * outward.y() < 0.0) {
    outward = -outward;
  }
  const QPointF tangent = along / length;
  const QColor dark(10, 14, 20);
  const QColor light(245, 248, 252);
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setBrush(Qt::NoBrush);
  painter.setPen(QPen(dark, 4.0, Qt::SolidLine, Qt::RoundCap));
  painter.drawLine(top_left, top_right);
  painter.setPen(QPen(light, 2.0, Qt::SolidLine, Qt::RoundCap));
  painter.drawLine(top_left, top_right);
  // A filled arrowhead with a short stem, 10 to 26 px out from the edge.
  const QPointF stem_start = middle + outward * 10.0;
  const QPointF tip = middle + outward * 26.0;
  const QPointF base = middle + outward * 16.0;
  QPolygonF head;
  head << tip << base + tangent * 7.0 << base - tangent * 7.0;
  painter.setPen(QPen(dark, 4.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  painter.drawLine(stem_start, base);
  painter.setBrush(light);
  painter.drawPolygon(head);
  painter.setPen(QPen(light, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  painter.drawLine(stem_start, base);
  painter.setPen(QPen(dark, 1.0));
  painter.drawPolygon(head);
  painter.restore();
}

}  // namespace patchy::ui
