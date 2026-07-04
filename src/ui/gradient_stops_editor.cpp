#include "ui/gradient_stops_editor.hpp"

#include "core/blend_math.hpp"

#include <QApplication>
#include <QCursor>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

namespace patchy::ui {

namespace {

constexpr int kHandleHalfWidth = 8;
constexpr int kHorizontalGutter = kHandleHalfWidth + 2;
constexpr int kOpacityAreaHeight = 28;

QColor qcolor_from_edit_color(EditColor color) {
  return QColor(color.r, color.g, color.b, color.a);
}

}  // namespace

GradientStopsEditorWidget::GradientStopsEditorWidget(QWidget* parent) : QWidget(parent) {
  setMinimumSize(320, 66);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  setMouseTracking(true);
}

void GradientStopsEditorWidget::set_opacity_track_enabled(bool enabled) {
  if (opacity_track_enabled_ == enabled) {
    return;
  }
  opacity_track_enabled_ = enabled;
  setMinimumSize(320, enabled ? 96 : 66);
  updateGeometry();
  update();
}

QSize GradientStopsEditorWidget::sizeHint() const {
  return QSize(420, opacity_track_enabled_ ? 96 : 66);
}

void GradientStopsEditorWidget::set_stops(std::vector<GradientStop> stops) {
  stops_ = std::move(stops);
  // Two-track mode allows no color selection while an opacity stop is selected;
  // only an existing color selection is clamped. Single-track keeps the legacy
  // "always select something" clamp.
  if (!opacity_track_enabled_ || current_row_ >= 0) {
    current_row_ = stops_.empty() ? -1 : std::clamp(current_row_, 0, static_cast<int>(stops_.size()) - 1);
  }
  update_cursor(mapFromGlobal(QCursor::pos()));
  update();
}

void GradientStopsEditorWidget::set_current_row(int row) {
  int bounded = stops_.empty() ? -1 : std::clamp(row, 0, static_cast<int>(stops_.size()) - 1);
  if (opacity_track_enabled_ && row < 0) {
    bounded = -1;
  }
  if (current_row_ == bounded) {
    return;
  }
  current_row_ = bounded;
  if (opacity_track_enabled_ && bounded >= 0) {
    current_opacity_row_ = -1;
  }
  update_cursor(mapFromGlobal(QCursor::pos()));
  update();
}

void GradientStopsEditorWidget::set_opacity_stops(std::vector<GradientAlphaStop> stops) {
  opacity_stops_ = std::move(stops);
  if (current_opacity_row_ >= 0) {
    current_opacity_row_ = opacity_stops_.empty()
                               ? -1
                               : std::clamp(current_opacity_row_, 0, static_cast<int>(opacity_stops_.size()) - 1);
  }
  update_cursor(mapFromGlobal(QCursor::pos()));
  update();
}

void GradientStopsEditorWidget::set_current_opacity_row(int row) {
  const int bounded =
      (row < 0 || opacity_stops_.empty()) ? -1 : std::clamp(row, 0, static_cast<int>(opacity_stops_.size()) - 1);
  if (current_opacity_row_ == bounded) {
    return;
  }
  current_opacity_row_ = bounded;
  if (bounded >= 0) {
    current_row_ = -1;
  }
  update_cursor(mapFromGlobal(QCursor::pos()));
  update();
}

void GradientStopsEditorWidget::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);

  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, false);

  const auto bar = bar_rect();
  painter.save();
  painter.setClipRect(bar);
  constexpr int checker = 8;
  for (int y = bar.top(); y <= bar.bottom(); y += checker) {
    for (int x = bar.left(); x <= bar.right(); x += checker) {
      painter.fillRect(QRect(x, y, checker, checker),
                       ((x / checker) + (y / checker)) % 2 == 0 ? QColor(210, 215, 222) : QColor(140, 148, 158));
    }
  }

  QLinearGradient gradient(QPointF(bar.left(), 0.0), QPointF(bar.right(), 0.0));
  if (!opacity_track_enabled_) {
    for (const auto& stop : normalized_gradient_stops(stops_)) {
      gradient.setColorAt(static_cast<double>(stop.location), qcolor_from_edit_color(stop.color));
    }
  } else {
    // Composite the color ramp with the opacity ramp: sample at the union of
    // both tracks' knot locations (both ramps are piecewise linear, so between
    // union knots the product is close enough for a preview).
    auto opaque_stops = stops_;
    for (auto& stop : opaque_stops) {
      stop.color.a = 255;
    }
    const auto sorted_colors = normalized_gradient_stops(opaque_stops);
    std::vector<float> knots{0.0F, 1.0F};
    knots.reserve(knots.size() + sorted_colors.size() + opacity_stops_.size());
    for (const auto& stop : sorted_colors) {
      knots.push_back(std::clamp(stop.location, 0.0F, 1.0F));
    }
    for (const auto& stop : opacity_stops_) {
      knots.push_back(std::clamp(stop.location, 0.0F, 1.0F));
    }
    std::sort(knots.begin(), knots.end());
    knots.erase(std::unique(knots.begin(), knots.end()), knots.end());
    for (const auto knot : knots) {
      const auto color = gradient_color_at(sorted_colors, 1.0F, false, static_cast<double>(knot));
      const auto alpha = std::clamp(opacity_at(static_cast<double>(knot)), 0.0F, 1.0F);
      gradient.setColorAt(static_cast<double>(knot),
                          QColor(color.r, color.g, color.b, static_cast<int>(std::lround(alpha * 255.0F))));
    }
  }
  painter.fillRect(bar, gradient);
  painter.restore();

  painter.setPen(QColor(154, 164, 178));
  painter.setBrush(Qt::NoBrush);
  painter.drawRect(bar);

  painter.setRenderHint(QPainter::Antialiasing, true);
  for (int row = 0; row < static_cast<int>(stops_.size()); ++row) {
    if (active_track_ == Track::Color && row == active_row_ && pending_delete_) {
      continue;
    }
    const auto path = handle_path(row);
    painter.setPen(QPen(row == current_row_ ? QColor(76, 154, 255) : QColor(255, 255, 255), 2.0));
    painter.setBrush(qcolor_from_edit_color(stops_[static_cast<std::size_t>(row)].color));
    painter.drawPath(path);
  }
  if (opacity_track_enabled_) {
    for (int row = 0; row < static_cast<int>(opacity_stops_.size()); ++row) {
      if (active_track_ == Track::Opacity && row == active_row_ && pending_delete_) {
        continue;
      }
      // Photoshop convention: opaque stops read black, transparent stops white.
      const auto opacity = std::clamp(opacity_stops_[static_cast<std::size_t>(row)].opacity, 0.0F, 1.0F);
      const auto gray = 255 - static_cast<int>(std::lround(opacity * 255.0F));
      painter.setPen(QPen(row == current_opacity_row_ ? QColor(76, 154, 255) : QColor(255, 255, 255), 2.0));
      painter.setBrush(QColor(gray, gray, gray));
      painter.drawPath(opacity_handle_path(row));
    }
  }
}

void GradientStopsEditorWidget::mousePressEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton) {
    QWidget::mousePressEvent(event);
    return;
  }

  const auto pos = event->position().toPoint();
  const int opacity_hit = hit_opacity_stop(pos);
  if (opacity_hit >= 0) {
    current_opacity_row_ = opacity_hit;
    current_row_ = -1;
    active_track_ = Track::Opacity;
    active_row_ = opacity_hit;
    press_position_ = pos;
    dragging_ = false;
    pending_delete_ = false;
    open_color_on_release_ = false;
    if (opacity_stop_selected) {
      opacity_stop_selected(opacity_hit);
    }
    update_cursor(pos);
    update();
    event->accept();
    return;
  }

  const int hit = hit_stop(pos);
  if (hit >= 0) {
    const bool was_selected = hit == current_row_;
    current_row_ = hit;
    if (opacity_track_enabled_) {
      current_opacity_row_ = -1;
    }
    active_track_ = Track::Color;
    active_row_ = hit;
    press_position_ = pos;
    dragging_ = false;
    pending_delete_ = false;
    open_color_on_release_ = was_selected;
    if (stop_selected) {
      stop_selected(hit);
    }
    update_cursor(pos);
    update();
    event->accept();
    return;
  }

  if (current_row_ >= 0 && current_row_ < static_cast<int>(stops_.size()) && bar_rect().contains(pos)) {
    const double position = position_from_x(pos.x());
    if (stop_color_picked) {
      const auto color = gradient_color_at(normalized_gradient_stops(stops_), 1.0F, false, position);
      stop_color_picked(current_row_, QColor(color.r, color.g, color.b));
    }
    event->accept();
    return;
  }
  if (opacity_track_enabled_ && bar_rect().contains(pos)) {
    // An opacity stop is selected; sampling a color from the bar only applies
    // to color stops.
    event->accept();
    return;
  }

  if (opacity_track_enabled_ && opacity_handle_area_rect().contains(pos)) {
    const double position = position_from_x(pos.x());
    if (opacity_stop_add_requested) {
      const int added_row =
          opacity_stop_add_requested(GradientAlphaStop{static_cast<float>(position), opacity_at(position)});
      if (added_row >= 0) {
        current_opacity_row_ = added_row;
        current_row_ = -1;
        active_track_ = Track::Opacity;
        active_row_ = added_row;
        press_position_ = pos;
        dragging_ = true;
        pending_delete_ = false;
        open_color_on_release_ = false;
        update_cursor(pos);
        update();
      }
    }
    event->accept();
    return;
  }

  if (handle_area_rect().contains(pos)) {
    const double position = position_from_x(pos.x());
    if (stop_add_requested) {
      const int added_row = stop_add_requested(GradientStop{
          static_cast<float>(position), gradient_color_at(normalized_gradient_stops(stops_), 1.0F, false, position)});
      if (added_row >= 0) {
        current_row_ = added_row;
        if (opacity_track_enabled_) {
          current_opacity_row_ = -1;
        }
        active_track_ = Track::Color;
        active_row_ = added_row;
        press_position_ = pos;
        dragging_ = true;
        pending_delete_ = false;
        open_color_on_release_ = false;
        update_cursor(pos);
        update();
      }
    }
    event->accept();
    return;
  }

  QWidget::mousePressEvent(event);
}

void GradientStopsEditorWidget::mouseMoveEvent(QMouseEvent* event) {
  const auto pos = event->position().toPoint();
  if ((event->buttons() & Qt::LeftButton) != 0 && active_row_ >= 0) {
    const auto active_track_size = active_track_ == Track::Color ? stops_.size() : opacity_stops_.size();
    const bool in_delete_zone = is_delete_zone(active_track_, pos) && active_track_size > 2U;
    if (in_delete_zone) {
      pending_delete_ = true;
      open_color_on_release_ = false;
      dragging_ = true;
      update();
      event->accept();
      return;
    }

    const auto drag_delta = pos - press_position_;
    if (dragging_ || drag_delta.manhattanLength() >= QApplication::startDragDistance()) {
      dragging_ = true;
      open_color_on_release_ = false;
      pending_delete_ = false;
      const auto percent = static_cast<int>(std::lround(position_from_x(pos.x()) * 100.0));
      if (active_track_ == Track::Color && stop_location_changed) {
        stop_location_changed(active_row_, percent);
      } else if (active_track_ == Track::Opacity && opacity_stop_location_changed) {
        opacity_stop_location_changed(active_row_, percent);
      }
      event->accept();
      return;
    }
  }

  update_cursor(pos);
  QWidget::mouseMoveEvent(event);
}

void GradientStopsEditorWidget::mouseReleaseEvent(QMouseEvent* event) {
  const int row = active_row_;
  const auto track = active_track_;
  active_row_ = -1;
  const auto& delete_callback = track == Track::Color ? stop_delete_requested : opacity_stop_delete_requested;
  if (event->button() == Qt::LeftButton && row >= 0 && pending_delete_ && delete_callback) {
    pending_delete_ = false;
    open_color_on_release_ = false;
    delete_callback(row);
    event->accept();
    return;
  }
  if (event->button() == Qt::LeftButton && row >= 0 && track == Track::Color && open_color_on_release_ &&
      !dragging_) {
    open_color_on_release_ = false;
    if (choose_stop_color_requested) {
      choose_stop_color_requested(row);
    }
    event->accept();
    return;
  }
  open_color_on_release_ = false;
  pending_delete_ = false;
  QWidget::mouseReleaseEvent(event);
}

void GradientStopsEditorWidget::leaveEvent(QEvent* event) {
  if (active_row_ < 0) {
    unsetCursor();
  }
  QWidget::leaveEvent(event);
}

QRect GradientStopsEditorWidget::bar_rect() const {
  const int top = opacity_track_enabled_ ? kOpacityAreaHeight + 2 : 2;
  return QRect(kHorizontalGutter, top, std::max(1, width() - kHorizontalGutter * 2), 32).adjusted(0, 0, -1, -1);
}

QRect GradientStopsEditorWidget::handle_area_rect() const {
  const auto bar = bar_rect();
  return QRect(0, bar.bottom() + 1, width(), std::max(0, height() - bar.bottom() - 1));
}

QRect GradientStopsEditorWidget::opacity_handle_area_rect() const {
  return QRect(0, 0, width(), kOpacityAreaHeight);
}

bool GradientStopsEditorWidget::is_delete_zone(Track track, QPoint pos) const {
  const auto area = track == Track::Color ? handle_area_rect() : opacity_handle_area_rect();
  return pos.y() < area.top() - kHandleHalfWidth || pos.y() > area.bottom() + kHandleHalfWidth;
}

double GradientStopsEditorWidget::position_from_x(int x) const {
  const auto bar = bar_rect();
  const int max_x = std::max(1, bar.width() - 1);
  return std::clamp(static_cast<double>(x - bar.left()) / static_cast<double>(max_x), 0.0, 1.0);
}

int GradientStopsEditorWidget::x_from_position(float location) const {
  const auto bar = bar_rect();
  return bar.left() + static_cast<int>(std::lround(std::clamp(location, 0.0F, 1.0F) *
                                                   static_cast<float>(std::max(1, bar.width() - 1))));
}

QPainterPath GradientStopsEditorWidget::handle_path(int row) const {
  const auto area = handle_area_rect();
  const auto& stop = stops_[static_cast<std::size_t>(row)];
  const double center_x = static_cast<double>(x_from_position(stop.location));
  const double top = static_cast<double>(area.top() + 5);
  const double body_top = top + 7.0;
  const double bottom = std::min(static_cast<double>(height() - 2), top + 23.0);
  constexpr double half_width = static_cast<double>(kHandleHalfWidth);

  QPainterPath path;
  path.moveTo(center_x, top);
  path.lineTo(center_x + half_width, body_top);
  path.lineTo(center_x + half_width, bottom);
  path.lineTo(center_x - half_width, bottom);
  path.lineTo(center_x - half_width, body_top);
  path.closeSubpath();
  return path;
}

QPainterPath GradientStopsEditorWidget::opacity_handle_path(int row) const {
  // Mirror of handle_path: the tag points down at the bar.
  const auto area = opacity_handle_area_rect();
  const auto& stop = opacity_stops_[static_cast<std::size_t>(row)];
  const double center_x = static_cast<double>(x_from_position(stop.location));
  const double bottom = static_cast<double>(area.bottom() - 3);
  const double body_bottom = bottom - 7.0;
  const double top = std::max(2.0, bottom - 23.0);
  constexpr double half_width = static_cast<double>(kHandleHalfWidth);

  QPainterPath path;
  path.moveTo(center_x, bottom);
  path.lineTo(center_x + half_width, body_bottom);
  path.lineTo(center_x + half_width, top);
  path.lineTo(center_x - half_width, top);
  path.lineTo(center_x - half_width, body_bottom);
  path.closeSubpath();
  return path;
}

int GradientStopsEditorWidget::hit_stop(QPoint pos) const {
  for (int row = static_cast<int>(stops_.size()) - 1; row >= 0; --row) {
    const auto widened = handle_path(row).controlPointRect().adjusted(-3.0, -3.0, 3.0, 3.0);
    if (widened.contains(QPointF(pos))) {
      return row;
    }
  }
  return -1;
}

int GradientStopsEditorWidget::hit_opacity_stop(QPoint pos) const {
  if (!opacity_track_enabled_) {
    return -1;
  }
  for (int row = static_cast<int>(opacity_stops_.size()) - 1; row >= 0; --row) {
    const auto widened = opacity_handle_path(row).controlPointRect().adjusted(-3.0, -3.0, 3.0, 3.0);
    if (widened.contains(QPointF(pos))) {
      return row;
    }
  }
  return -1;
}

float GradientStopsEditorWidget::opacity_at(double position) const {
  LayerStyleGradient ramp;
  ramp.alpha_stops = opacity_stops_;
  std::sort(ramp.alpha_stops.begin(), ramp.alpha_stops.end(),
            [](const GradientAlphaStop& lhs, const GradientAlphaStop& rhs) { return lhs.location < rhs.location; });
  return gradient_stop_opacity(ramp, static_cast<float>(position));
}

void GradientStopsEditorWidget::update_cursor(QPoint pos) {
  if (!rect().contains(pos)) {
    unsetCursor();
    return;
  }
  if (hit_stop(pos) >= 0 || hit_opacity_stop(pos) >= 0) {
    setCursor(Qt::PointingHandCursor);
    return;
  }
  if (current_row_ >= 0 && current_row_ < static_cast<int>(stops_.size()) && bar_rect().contains(pos)) {
    setCursor(Qt::CrossCursor);
    return;
  }
  if (handle_area_rect().contains(pos)) {
    setCursor(Qt::CrossCursor);
    return;
  }
  if (opacity_track_enabled_ && opacity_handle_area_rect().contains(pos)) {
    setCursor(Qt::CrossCursor);
    return;
  }
  unsetCursor();
}

}  // namespace patchy::ui
