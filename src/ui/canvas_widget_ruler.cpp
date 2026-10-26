// CanvasWidget's Ruler tool (GitHub issue 331, docs/pick-tool.md). A drag draws a
// measuring line, Shift keeping 45-degree steps; a press on an end moves that end, a
// press on the line moves it whole, and a press anywhere else starts a new line. Alt+drag
// from an end adds a second leg from that end (a protractor). The line is a view
// overlay in document pixels: it is drawn only with the Ruler active, never saved and
// never an undo step, and a document swap clears it. MainWindow reads it for the
// options-bar readout through the ruler changed callback.

#include "ui/canvas_widget.hpp"

#include "ui/canvas_types.hpp"

#include <QColor>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPointF>
#include <QRectF>
#include <QTransform>
#include <Qt>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <numbers>
#include <optional>
#include <utility>

namespace patchy::ui {

namespace {

// Screen-pixel reach of an end and of the line body for a press or hover.
constexpr double kRulerEndReach = 6.0;
constexpr double kRulerBodyReach = 4.0;
// Half the length of the tick marks across each end.
constexpr double kRulerEndTick = 5.0;

QPointF whole_pixels(QPointF point) {
  return {std::round(point.x()), std::round(point.y())};
}

// The pixel under the pointer: its top-left corner, so a press lands on the pixel it
// shows at any zoom and pan.
QPointF pointer_pixel(QPointF point) {
  return {std::floor(point.x()), std::floor(point.y())};
}

double distance_to_segment(QPointF point, QPointF a, QPointF b) {
  const QPointF ab = b - a;
  const double length_squared = QPointF::dotProduct(ab, ab);
  if (length_squared <= 0.0) {
    return QLineF(point, a).length();
  }
  const double t = std::clamp(QPointF::dotProduct(point - a, ab) / length_squared, 0.0, 1.0);
  return QLineF(point, a + ab * t).length();
}

}  // namespace

std::optional<RulerLine> CanvasWidget::ruler_line() const {
  return ruler_line_;
}

void CanvasWidget::set_ruler_line(const RulerLine& line) {
  cancel_ruler_drag();
  RulerLine rounded{whole_pixels(line.start), whole_pixels(line.end), std::nullopt};
  if (line.second.has_value()) {
    rounded.second = whole_pixels(*line.second);
  }
  if (rounded.start == rounded.end) {
    ruler_line_.reset();
  } else {
    ruler_line_ = rounded;
  }
  update();
  notify_ruler_changed();
}

void CanvasWidget::clear_ruler_line() {
  cancel_ruler_drag();
  if (!ruler_line_.has_value()) {
    return;
  }
  ruler_line_.reset();
  update();
  notify_ruler_changed();
}

void CanvasWidget::set_ruler_changed_callback(std::function<void()> callback) {
  ruler_changed_callback_ = std::move(callback);
}

void CanvasWidget::notify_ruler_changed() {
  if (ruler_changed_callback_) {
    ruler_changed_callback_();
  }
}

CanvasWidget::RulerPart CanvasWidget::ruler_part_at(QPoint widget_point) const {
  if (!ruler_line_.has_value()) {
    return RulerPart::None;
  }
  const QPointF point(widget_point);
  const auto start = widget_position_f(ruler_line_->start);
  const auto end = widget_position_f(ruler_line_->end);
  const auto near = [&point](QPointF end_point) { return QLineF(point, end_point).length() <= kRulerEndReach; };
  if (ruler_line_->second.has_value() && near(widget_position_f(*ruler_line_->second))) {
    return RulerPart::Second;
  }
  if (near(end)) {
    return RulerPart::End;
  }
  if (near(start)) {
    return RulerPart::Start;
  }
  if (distance_to_segment(point, start, end) <= kRulerBodyReach ||
      (ruler_line_->second.has_value() &&
       distance_to_segment(point, start, widget_position_f(*ruler_line_->second)) <= kRulerBodyReach)) {
    return RulerPart::Body;
  }
  return RulerPart::None;
}

void CanvasWidget::handle_ruler_press(QMouseEvent* event) {
  const auto document_point = pointer_pixel(document_position_f(event->position()));
  auto part = ruler_part_at(event->pos());
  ruler_drag_start_line_ = ruler_line_;
  ruler_drag_start_point_ = document_point;
  const bool alt = event->modifiers().testFlag(Qt::AltModifier);
  if (alt && ruler_line_.has_value() && !ruler_line_->second.has_value() &&
      (part == RulerPart::Start || part == RulerPart::End)) {
    // Alt+drag from an end starts the protractor's second leg there: that end becomes
    // the vertex.
    if (part == RulerPart::End) {
      std::swap(ruler_line_->start, ruler_line_->end);
    }
    ruler_line_->second = ruler_line_->start;
    part = RulerPart::Second;
  } else if (part == RulerPart::None) {
    // A press away from the line starts a new one.
    ruler_line_ = RulerLine{document_point, document_point, std::nullopt};
    part = RulerPart::End;
  }
  ruler_drag_part_ = part;
  update_ruler_cursor(event->pos());
  update();
  notify_ruler_changed();
}

void CanvasWidget::update_ruler_drag(QPointF document_point, Qt::KeyboardModifiers modifiers) {
  if (ruler_drag_part_ == RulerPart::None || !ruler_line_.has_value()) {
    return;
  }
  const auto point = pointer_pixel(document_point);
  const bool shift = modifiers.testFlag(Qt::ShiftModifier);
  auto& line = *ruler_line_;
  switch (ruler_drag_part_) {
    case RulerPart::Start:
      line.start = shift ? constrain_ruler_point(line.end, point) : point;
      break;
    case RulerPart::End:
      line.end = shift ? constrain_ruler_point(line.start, point) : point;
      break;
    case RulerPart::Second:
      line.second = shift ? constrain_ruler_point(line.start, point) : point;
      break;
    case RulerPart::Body:
      if (ruler_drag_start_line_.has_value()) {
        const auto delta = point - ruler_drag_start_point_;
        line = *ruler_drag_start_line_;
        line.start += delta;
        line.end += delta;
        if (line.second.has_value()) {
          *line.second += delta;
        }
      }
      break;
    case RulerPart::None:
      break;
  }
  update();
  notify_ruler_changed();
}

void CanvasWidget::finish_ruler_drag(QMouseEvent* event) {
  update_ruler_drag(document_position_f(event->position()), event->modifiers());
  ruler_drag_part_ = RulerPart::None;
  ruler_drag_start_line_.reset();
  if (ruler_line_.has_value()) {
    // A click without a drag leaves a line of no length: that clears the line. A
    // protractor leg dragged back onto its vertex is dropped the same way.
    if (ruler_line_->second.has_value() && *ruler_line_->second == ruler_line_->start) {
      ruler_line_->second.reset();
    }
    if (ruler_line_->start == ruler_line_->end) {
      ruler_line_.reset();
    }
  }
  update_ruler_cursor(event->pos());
  update();
  notify_ruler_changed();
}

void CanvasWidget::cancel_ruler_drag() {
  if (ruler_drag_part_ == RulerPart::None) {
    return;
  }
  ruler_drag_part_ = RulerPart::None;
  ruler_line_ = ruler_drag_start_line_;
  ruler_drag_start_line_.reset();
  update();
  notify_ruler_changed();
}

void CanvasWidget::update_ruler_cursor(QPoint widget_point) {
  const auto part = ruler_drag_part_ != RulerPart::None ? ruler_drag_part_ : ruler_part_at(widget_point);
  setCursor(part == RulerPart::Body ? Qt::SizeAllCursor : Qt::CrossCursor);
}

void CanvasWidget::draw_ruler_overlay(QPainter& painter) const {
  if (tool_ != CanvasTool::Ruler || !ruler_line_.has_value()) {
    return;
  }
  // Cursor ink (dark under light), exempt from theme roles like every tool overlay
  // (docs/ui-conventions.md): it sits on artwork, not chrome.
  const QColor dark(10, 14, 20);
  const QColor light(245, 248, 252);
  const auto vertex = widget_position_f(ruler_line_->start);
  std::array<QPointF, 2> ends{widget_position_f(ruler_line_->end), QPointF()};
  const int legs = ruler_line_->second.has_value() ? 2 : 1;
  if (legs == 2) {
    ends[1] = widget_position_f(*ruler_line_->second);
  }
  const auto tick = [](QPointF at, QPointF along) {
    // A short mark across the leg at `at`.
    const double length = std::hypot(along.x(), along.y());
    const QPointF normal = length > 0.0 ? QPointF(-along.y() / length, along.x() / length) : QPointF(0.0, 1.0);
    return QLineF(at - normal * kRulerEndTick, at + normal * kRulerEndTick);
  };
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setBrush(Qt::NoBrush);
  for (int pass = 0; pass < 2; ++pass) {
    painter.setPen(pass == 0 ? QPen(dark, 3.0, Qt::SolidLine, Qt::RoundCap) : QPen(light, 1.0, Qt::SolidLine, Qt::FlatCap));
    for (int leg = 0; leg < legs; ++leg) {
      const auto end = ends[static_cast<std::size_t>(leg)];
      painter.drawLine(QLineF(vertex, end));
      painter.drawLine(tick(end, end - vertex));
    }
    painter.drawLine(tick(vertex, ends[0] - vertex));
    if (legs == 2) {
      // The protractor's arc between its legs, about the vertex.
      const auto angle_of = [&vertex](QPointF end) {
        return std::atan2(-(end.y() - vertex.y()), end.x() - vertex.x()) * 180.0 / std::numbers::pi;
      };
      const double first = angle_of(ends[0]);
      double span = angle_of(ends[1]) - first;
      while (span > 180.0) {
        span -= 360.0;
      }
      while (span <= -180.0) {
        span += 360.0;
      }
      constexpr double kArcRadius = 18.0;
      painter.drawArc(QRectF(vertex.x() - kArcRadius, vertex.y() - kArcRadius, 2.0 * kArcRadius, 2.0 * kArcRadius),
                      static_cast<int>(std::lround(first * 16.0)), static_cast<int>(std::lround(span * 16.0)));
    }
  }
  painter.restore();
}

bool CanvasWidget::straighten_layer(LayerId layer_id, double ccw_degrees) {
  if (document_ == nullptr || transforming_layer_ || warping_layer_ || path_transform_active_) {
    return false;
  }
  if (std::abs(ccw_degrees) < 0.01) {
    return true;
  }
  // The interactive Free Transform commit, with the history entry renamed; the whole
  // layer turns even with a selection active (Free Transform would lift the selected
  // pixels only).
  const auto saved_before_edit = before_edit_callback_;
  before_edit_callback_ = [saved_before_edit](const QString&) {
    if (saved_before_edit) {
      saved_before_edit(tr("Straighten Layer"));
    }
  };
  const auto saved_selection = selected_layer_ids_;
  const auto saved_ignore = std::exchange(transform_ignores_selection_, true);
  set_selected_layer_ids({layer_id});
  bool committed = false;
  if (begin_free_transform()) {
    auto quad = free_transform_quad();
    const QPointF center = (quad[0] + quad[1] + quad[2] + quad[3]) / 4.0;
    QTransform turn;
    turn.translate(center.x(), center.y());
    turn.rotate(-ccw_degrees);  // QTransform turns clockwise on screen (y down)
    turn.translate(-center.x(), -center.y());
    for (auto& corner : quad) {
      corner = turn.map(corner);
    }
    if (set_free_transform_quad(quad)) {
      commit_free_transform();
      committed = true;
    } else {
      cancel_free_transform();
    }
  }
  transform_ignores_selection_ = saved_ignore;
  set_selected_layer_ids(saved_selection);
  before_edit_callback_ = saved_before_edit;
  return committed;
}

}  // namespace patchy::ui
