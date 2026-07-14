// CanvasWidget's view implementation, split out of canvas_widget.cpp: the
// zoom accessors and zoom/fit/center commands, spacebar and pen panning,
// pan constraining and view-changed notification, the wheel-zoom setting,
// widget/document coordinate mapping, and the Zoom tool's drag preview and
// pen zoom drag. Pure function moves from canvas_widget.cpp; behavior must
// stay identical.

#include "ui/canvas_widget.hpp"
#include "ui/canvas_widget_shared.hpp"

#include "core/adjustment_layer.hpp"
#include "core/blend_math.hpp"
#include "core/layer_metadata.hpp"
#include "core/smart_object.hpp"
#include "core/smart_filter.hpp"
#include "core/layer_render_utils.hpp"
#include "core/layer_tree.hpp"
#include "core/pixel_tools.hpp"
#include "core/quick_select.hpp"
#include "ui/edit_conversions.hpp"
#include "ui/image_document_io.hpp"
#include "ui/qt_geometry.hpp"
#include "ui/smart_object_render.hpp"
#include "ui/tool_cursors.hpp"

#include <QApplication>
#include <QCursor>
#include <QEnterEvent>
#include <QEventLoop>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QInputDevice>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QMenu>
#include <QMetaObject>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPointingDevice>
#include <QPolygon>
#include <QPolygonF>
#include <QPointer>
#include <QRadialGradient>
#include <QResizeEvent>
#include <QScreen>
#include <QSet>
#include <QTabletEvent>
#include <QTimerEvent>
#include <QTransform>
#include <QWheelEvent>
#include <QRandomGenerator>
#include <QtGlobal>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <future>
#include <functional>
#include <iostream>
#include <limits>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

constexpr double kMinZoom = 0.05;
constexpr double kMaxZoom = 128.0;
constexpr double kMinimumVisibleDocumentFraction = 0.10;

double constrained_document_axis(double pan, double viewport_span, double document_span) noexcept {
  if (!std::isfinite(pan) || viewport_span <= 0.0 || document_span <= 0.0) {
    return pan;
  }

  const auto minimum_visible =
      std::max(1.0, std::min(viewport_span, document_span) * kMinimumVisibleDocumentFraction);
  return std::clamp(pan, minimum_visible - document_span, viewport_span - minimum_visible);
}

}  // namespace

double CanvasWidget::zoom() const noexcept {
  return zoom_;
}

void CanvasWidget::set_zoom(double zoom) {
  const auto clamped = std::clamp(zoom, kMinZoom, kMaxZoom);
  if (std::abs(clamped - zoom_) < 0.0001) {
    return;
  }
  zoom_ = clamped;
  constrain_pan();
  update_tool_cursor();
  update();
  notify_view_changed();
}

void CanvasWidget::set_zoom_centered(double zoom) {
  const auto clamped = std::clamp(zoom, kMinZoom, kMaxZoom);
  zoom_at_widget_point(QPointF(static_cast<double>(width()) / 2.0, static_cast<double>(height()) / 2.0),
                       clamped / zoom_);
}

void CanvasWidget::zoom_at_widget_point(QPointF widget_position, double factor) {
  ZoomTraceScope trace("zoom_step", zoom_);
  if (factor <= 0.0 || !std::isfinite(factor)) {
    return;
  }
  const QPointF document_anchor((widget_position.x() - pan_.x()) / zoom_,
                                (widget_position.y() - pan_.y()) / zoom_);
  const auto old_zoom = zoom_;
  zoom_ = std::clamp(zoom_ * factor, kMinZoom, kMaxZoom);
  if (std::abs(old_zoom - zoom_) < 0.0001) {
    return;
  }
  pan_ = QPointF(widget_position.x() - document_anchor.x() * zoom_,
                 widget_position.y() - document_anchor.y() * zoom_);
  constrain_pan();
  update_tool_cursor();
  update();
  notify_view_changed();
}

void CanvasWidget::fit_to_view() {
  if (document_ == nullptr || document_->width() <= 0 || document_->height() <= 0 || width() <= 0 || height() <= 0) {
    return;
  }

  const auto available_width = std::max(1.0, static_cast<double>(width() - 80));
  const auto available_height = std::max(1.0, static_cast<double>(height() - 80));
  zoom_ = std::clamp(std::min(available_width / static_cast<double>(document_->width()),
                              available_height / static_cast<double>(document_->height())),
                     kMinZoom, kMaxZoom);
  pan_ = QPointF((static_cast<double>(width()) - static_cast<double>(document_->width()) * zoom_) / 2.0,
                 (static_cast<double>(height()) - static_cast<double>(document_->height()) * zoom_) / 2.0);
  constrain_pan();
  update();
  notify_view_changed();
}

void CanvasWidget::center_document_in_view() {
  if (document_ == nullptr || document_->width() <= 0 || document_->height() <= 0 || width() <= 0 || height() <= 0) {
    return;
  }

  pan_ = QPointF((static_cast<double>(width()) - static_cast<double>(document_->width()) * zoom_) / 2.0,
                 (static_cast<double>(height()) - static_cast<double>(document_->height()) * zoom_) / 2.0);
  constrain_pan();
  update();
  notify_view_changed();
}

void CanvasWidget::zoom_to_document_rect(QRect document_rect) {
  if (document_ == nullptr || document_->width() <= 0 || document_->height() <= 0 || width() <= 0 || height() <= 0) {
    return;
  }

  document_rect = document_rect.normalized().intersected(QRect(0, 0, document_->width(), document_->height()));
  // Reject only a degenerate point; a thin strip still zooms to fit its longer
  // axis (the divisions below stay safe since a non-empty rect is at least 1px).
  if (document_rect.width() <= 1 && document_rect.height() <= 1) {
    return;
  }

  const auto available_width = std::max(1.0, static_cast<double>(width() - 80));
  const auto available_height = std::max(1.0, static_cast<double>(height() - 80));
  zoom_ = std::clamp(std::min(available_width / std::max(1.0, static_cast<double>(document_rect.width())),
                              available_height / std::max(1.0, static_cast<double>(document_rect.height()))),
                     kMinZoom, kMaxZoom);
  pan_ = QPointF((static_cast<double>(width()) - static_cast<double>(document_rect.width()) * zoom_) / 2.0 -
                     static_cast<double>(document_rect.x()) * zoom_,
                 (static_cast<double>(height()) - static_cast<double>(document_rect.height()) * zoom_) / 2.0 -
                     static_cast<double>(document_rect.y()) * zoom_);
  constrain_pan();
  update_tool_cursor();
  update();
  notify_view_changed();
}

void CanvasWidget::set_spacebar_panning(bool enabled) {
  if (spacebar_panning_ == enabled) {
    return;
  }
  spacebar_panning_ = enabled;
  if (!panning_) {
    update_tool_cursor();
  }
}

bool CanvasWidget::begin_pan_at_global_position(QPoint global_position) {
  if (document_ == nullptr || document_->width() <= 0 || document_->height() <= 0) {
    return false;
  }
  clear_move_hover_outline();
  last_mouse_position_ = mapFromGlobal(global_position);
  panning_ = true;
  setCursor(Qt::ClosedHandCursor);
  return true;
}

bool CanvasWidget::pan_to_global_position(QPoint global_position) {
  if (!panning_) {
    return false;
  }
  clear_move_hover_outline();
  const auto position = mapFromGlobal(global_position);
  const auto delta = position - last_mouse_position_;
  const auto old_pan = pan_;
  pan_ += QPointF(delta);
  constrain_pan();
  last_mouse_position_ = position;
  if (pan_ != old_pan) {
    update();
    notify_view_changed();
  }
  return true;
}

bool CanvasWidget::end_pan() {
  if (!panning_) {
    return false;
  }
  panning_ = false;
  update_tool_cursor();
  return true;
}

bool CanvasWidget::constrain_pan() noexcept {
  if (document_ == nullptr || document_->width() <= 0 || document_->height() <= 0 || width() <= 0 || height() <= 0) {
    return false;
  }

  const auto constrained =
      QPointF(constrained_document_axis(pan_.x(), static_cast<double>(width()),
                                        static_cast<double>(document_->width()) * zoom_),
              constrained_document_axis(pan_.y(), static_cast<double>(height()),
                                        static_cast<double>(document_->height()) * zoom_));
  if (constrained == pan_) {
    return false;
  }

  pan_ = constrained;
  return true;
}

void CanvasWidget::notify_view_changed() {
  ZoomTraceScope trace("view_changed", zoom_);
  if (view_changed_callback_) {
    view_changed_callback_();
  }
}

QPoint CanvasWidget::widget_position_for_document_point(QPoint document_position) const {
  return widget_position(document_position);
}

void CanvasWidget::set_wheel_zooms(bool enabled) noexcept {
  wheel_zooms_ = enabled;
}

bool CanvasWidget::wheel_zooms() const noexcept {
  return wheel_zooms_;
}

void CanvasWidget::draw_zoom_preview(QPainter& painter) const {
  // No marquee while Alt is held (Alt is a point zoom-out, not a rectangle).
  if (!zooming_ || (QApplication::keyboardModifiers() & Qt::AltModifier) != 0) {
    return;
  }

  const auto preview_rect = QRect(widget_position(zoom_start_), widget_position(zoom_current_)).normalized();
  if (preview_rect.width() < 2 && preview_rect.height() < 2) {
    return;
  }

  painter.save();
  painter.setBrush(QColor(65, 135, 220, 35));
  painter.setPen(Qt::NoPen);
  painter.drawRect(preview_rect);

  QPen dark(QColor(15, 18, 22), 1.0);
  dark.setDashPattern({4.0, 4.0});
  dark.setDashOffset(selection_dash_offset_ + 4);
  dark.setCosmetic(true);
  QPen light(QColor(248, 250, 253), 1.0);
  light.setDashPattern({4.0, 4.0});
  light.setDashOffset(selection_dash_offset_);
  light.setCosmetic(true);
  painter.setBrush(Qt::NoBrush);
  painter.setPen(dark);
  painter.drawRect(preview_rect);
  painter.setPen(light);
  painter.drawRect(preview_rect);
  painter.restore();
}

QPoint CanvasWidget::document_position(const QPoint& widget_position) const {
  const auto coordinate_from_widget = [this](int widget_coordinate, double pan, int limit) {
    auto coordinate = static_cast<int>(std::floor((static_cast<double>(widget_coordinate) - pan) / zoom_));
    if (document_ == nullptr || !uses_deep_zoom_pixel_renderer(zoom_)) {
      return coordinate;
    }
    const auto edge = [pan, this](int document_coordinate) {
      return static_cast<int>(std::round(pan + static_cast<double>(document_coordinate) * zoom_));
    };
    while (coordinate > 0 && edge(coordinate) > widget_coordinate) {
      --coordinate;
    }
    while (coordinate < limit && edge(coordinate + 1) <= widget_coordinate) {
      ++coordinate;
    }
    return coordinate;
  };
  const auto x = coordinate_from_widget(widget_position.x(), pan_.x(), document_ != nullptr ? document_->width() : 0);
  const auto y = coordinate_from_widget(widget_position.y(), pan_.y(), document_ != nullptr ? document_->height() : 0);
  return QPoint(x, y);
}

QPointF CanvasWidget::document_position_f(QPointF widget_position) const {
  return QPointF((widget_position.x() - pan_.x()) / zoom_, (widget_position.y() - pan_.y()) / zoom_);
}

QPoint CanvasWidget::widget_position(const QPoint& document_position) const {
  return QPoint(static_cast<int>(std::round(pan_.x() + static_cast<double>(document_position.x()) * zoom_)),
                static_cast<int>(std::round(pan_.y() + static_cast<double>(document_position.y()) * zoom_)));
}

QPointF CanvasWidget::widget_position_f(QPointF document_position) const {
  return QPointF(pan_.x() + document_position.x() * zoom_, pan_.y() + document_position.y() * zoom_);
}

void CanvasWidget::begin_zoom_drag(QPointF widget_position) {
  pen_zoom_dragging_ = true;
  zoom_drag_anchor_widget_ = widget_position;
  zoom_drag_last_pos_ = widget_position;
  setCursor(Qt::SizeVerCursor);
}

void CanvasWidget::update_zoom_drag(QPointF widget_position) {
  if (!pen_zoom_dragging_) {
    return;
  }
  // Dragging up zooms in, dragging down zooms out, anchored on the press point.
  const auto delta = zoom_drag_last_pos_.y() - widget_position.y();
  zoom_drag_last_pos_ = widget_position;
  if (std::abs(delta) < 0.001) {
    return;
  }
  const auto factor = std::pow(1.01, delta);
  zoom_at_widget_point(zoom_drag_anchor_widget_, factor);
}

void CanvasWidget::end_zoom_drag() {
  if (!pen_zoom_dragging_) {
    return;
  }
  pen_zoom_dragging_ = false;
  update_tool_cursor();
}

}  // namespace patchy::ui
