// CanvasWidget's view implementation, split out of canvas_widget.cpp: the
// zoom accessors and zoom/fit/center commands, spacebar and pen panning,
// pan constraining and view-changed notification, the wheel-zoom setting,
// widget/document coordinate mapping, and the Zoom tool's drag preview and
// pen zoom drag. Pure function moves from canvas_widget.cpp; behavior must
// stay identical.

#include "ui/canvas_widget.hpp"

#include "core/smart_object.hpp"
#include "ui/canvas_types.hpp"
#include "ui/canvas_widget_shared.hpp"

#include <QApplication>
#include <QColor>
#include <QPainter>
#include <QPen>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QScrollBar>
#include <Qt>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <functional>

namespace patchy::ui {

namespace {

constexpr double kMinZoom = 0.0001;  // 0.01%, Photoshop's floor: a grid of artboards fits the window
constexpr double kMaxZoom = 128.0;
// One pixel of drag-zoom travel (the pen ZoomCanvas button vertically, Scrubby
// Zoom horizontally) multiplies the zoom by this: about 70 px doubles it.
constexpr double kZoomDragFactorPerPixel = 1.01;
constexpr double kMinimumVisibleDocumentFraction = 0.10;
constexpr int kScrollBarSingleStep = 20;
// Fit on Screen's and the Zoom-tool marquee's margin, per side of the view area.
// Just enough to keep edge handles and marching ants clear of the rulers and
// scroll bars (GitHub issue 585).
constexpr double kFitMarginPerSide = 10.0;

// The allowed pan range per axis: at least 10% of the document (or of the
// viewport, whichever is smaller) must stay visible. The scroll bars derive
// their range from the same rule so bar scrolling and hand-tool panning can
// never disagree.
struct PanAxisRange {
  double minimum;
  double maximum;
};

PanAxisRange pan_axis_range(double viewport_span, double document_span) noexcept {
  const auto minimum_visible =
      std::max(1.0, std::min(viewport_span, document_span) * kMinimumVisibleDocumentFraction);
  return {minimum_visible - document_span, viewport_span - minimum_visible};
}

double constrained_document_axis(double pan, double viewport_span, double document_span) noexcept {
  if (!std::isfinite(pan) || viewport_span <= 0.0 || document_span <= 0.0) {
    return pan;
  }

  const auto range = pan_axis_range(viewport_span, document_span);
  return std::clamp(pan, range.minimum, range.maximum);
}

// The pan that centers the (rotated) document's bounding box in `area`.
QPointF centered_pan(const QRectF& area, const CanvasViewTransform& view) noexcept {
  const auto extent = view.extent();
  const auto offset = view.bounds_offset();
  return QPointF(area.left() + (area.width() - extent.width()) / 2.0 - offset.x(),
                 area.top() + (area.height() - extent.height()) / 2.0 - offset.y());
}

}  // namespace

double CanvasWidget::zoom() const noexcept {
  return zoom_;
}

CanvasViewTransform CanvasWidget::view_transform() const noexcept {
  const QSizeF document_size = document_ != nullptr ? QSizeF(document_->width(), document_->height()) : QSizeF();
  const auto angle = upright_overlay_depth_ > 0 ? 0.0 : view_rotation_degrees_;
  return CanvasViewTransform(zoom_, pan_, angle, document_size);
}

bool CanvasWidget::view_axis_aligned() const noexcept {
  return upright_overlay_depth_ > 0 || view_rotation_degrees_ == 0.0;
}

CanvasWidget::UprightOverlayScope::UprightOverlayScope(const CanvasWidget& canvas, QPainter* painter)
    : canvas_(canvas), active_(canvas.view_rotation_degrees_ != 0.0) {
  if (!active_) {
    return;
  }
  // Only the outermost scope turns the painter; a nested one is already in the frame.
  if (painter != nullptr && canvas_.upright_overlay_depth_ == 0) {
    painter_ = painter;
    painter_->save();
    painter_->setTransform(canvas_.upright_overlay_to_widget(), true);
  }
  ++canvas_.upright_overlay_depth_;
}

CanvasWidget::UprightOverlayScope::~UprightOverlayScope() {
  if (!active_) {
    return;
  }
  --canvas_.upright_overlay_depth_;
  if (painter_ != nullptr) {
    painter_->restore();
  }
}

QTransform CanvasWidget::upright_overlay_to_widget() const {
  if (view_rotation_degrees_ == 0.0) {
    return {};
  }
  // The rotated view is the upright one turned about the document origin's screen point.
  const auto turn = CanvasViewTransform(1.0, QPointF(), view_rotation_degrees_, QSizeF()).to_widget();
  return QTransform::fromTranslate(-pan_.x(), -pan_.y()) * turn * QTransform::fromTranslate(pan_.x(), pan_.y());
}

QPointF CanvasWidget::upright_overlay_point(QPointF widget_point) const {
  if (view_rotation_degrees_ == 0.0 || upright_overlay_depth_ > 0) {
    return widget_point;
  }
  const CanvasViewTransform turn(1.0, QPointF(), view_rotation_degrees_, QSizeF());
  return pan_ + turn.map_vector_to_document(widget_point - pan_);
}

QPoint CanvasWidget::upright_overlay_point(QPoint widget_point) const {
  return upright_overlay_point(QPointF(widget_point)).toPoint();
}

QRect CanvasWidget::overlay_view_rect() const {
  return overlay_exposed_rect(rect());
}

QRect CanvasWidget::overlay_exposed_rect(QRect exposed_rect) const {
  if (view_rotation_degrees_ == 0.0 || upright_overlay_depth_ == 0) {
    return exposed_rect;
  }
  return upright_overlay_to_widget().inverted().mapRect(QRectF(exposed_rect)).toAlignedRect().adjusted(-1, -1, 1, 1);
}

double CanvasWidget::view_rotation() const noexcept {
  return view_rotation_degrees_;
}

void CanvasWidget::set_view_rotation(double degrees) {
  const auto angle = CanvasViewTransform::normalized_angle(degrees);
  if (angle == view_rotation_degrees_) {
    return;
  }
  if (document_ == nullptr || document_->width() <= 0 || document_->height() <= 0) {
    view_rotation_degrees_ = angle;
    update();
    notify_view_changed();
    return;
  }
  // Pivot on the view area's center: the document point there stays there.
  const auto pivot = view_content_rect().center();
  const auto document_anchor = view_transform().map_to_document(pivot);
  view_rotation_degrees_ = angle;
  pan_ = view_transform().pan_placing(document_anchor, pivot);
  constrain_pan();
  update_tool_cursor();
  update();
  notify_view_changed();
}

double CanvasWidget::view_zoom() const noexcept {
  return zoom_ * devicePixelRatioF();
}

void CanvasWidget::set_view_zoom(double view_zoom) {
  set_zoom(view_zoom / std::max(0.01, devicePixelRatioF()));
}

void CanvasWidget::set_view_zoom_centered(double view_zoom) {
  set_zoom_centered(view_zoom / std::max(0.01, devicePixelRatioF()));
}

double CanvasWidget::clamp_logical_zoom(double logical_zoom) const noexcept {
  const auto ratio = std::max(0.01, devicePixelRatioF());
  return std::clamp(logical_zoom * ratio, kMinZoom, kMaxZoom) / ratio;
}

void CanvasWidget::set_zoom(double zoom) {
  const auto clamped = clamp_logical_zoom(zoom);
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
  const auto clamped = clamp_logical_zoom(zoom);
  if (document_ == nullptr || document_->width() <= 0 || document_->height() <= 0) {
    set_zoom(clamped);
    return;
  }
  ZoomTraceScope trace("zoom_step", zoom_);
  const auto area = view_content_rect();
  const auto viewport_center = area.center();
  // Anchor the document point under the viewport center, clamped to the
  // document bounds: when a geometry change left the view off-center, a raw
  // center anchor can sit in the grey margin and pin empty space, pushing the
  // document almost entirely off screen at the new zoom.
  const auto center_point = view_transform().map_to_document(viewport_center);
  const QPointF document_anchor(std::clamp(center_point.x(), 0.0, static_cast<double>(document_->width())),
                                std::clamp(center_point.y(), 0.0, static_cast<double>(document_->height())));
  const auto old_zoom = zoom_;
  const auto old_pan = pan_;
  zoom_ = clamped;
  pan_ = view_transform().pan_placing(document_anchor, viewport_center);
  // Photoshop's scroll model, applied per axis at preset time: a document
  // that fits the window is centered, and one that overflows never shows grey
  // past its edges (hand-tool overscroll stays free afterwards).
  // Both measured in the view area, so neither the rulers nor the scroll bars
  // cover a document edge. A rotated view applies the rule to the document's
  // bounding box.
  const auto view = view_transform();
  const auto extent = view.extent();
  const auto offset = view.bounds_offset();
  const auto document_width = extent.width();
  const auto document_height = extent.height();
  if (document_width <= area.width()) {
    pan_.setX(area.left() + (area.width() - document_width) / 2.0 - offset.x());
  } else {
    pan_.setX(std::clamp(pan_.x() + offset.x(), area.right() - document_width, area.left()) - offset.x());
  }
  if (document_height <= area.height()) {
    pan_.setY(area.top() + (area.height() - document_height) / 2.0 - offset.y());
  } else {
    pan_.setY(std::clamp(pan_.y() + offset.y(), area.bottom() - document_height, area.top()) - offset.y());
  }
  constrain_pan();
  if (std::abs(old_zoom - zoom_) < 0.0001 && (pan_ - old_pan).manhattanLength() < 0.01) {
    return;
  }
  update_tool_cursor();
  update();
  notify_view_changed();
}

void CanvasWidget::zoom_at_widget_point(QPointF widget_position, double factor) {
  ZoomTraceScope trace("zoom_step", zoom_);
  if (factor <= 0.0 || !std::isfinite(factor)) {
    return;
  }
  const auto document_anchor = view_transform().map_to_document(widget_position);
  const auto old_zoom = zoom_;
  zoom_ = clamp_logical_zoom(zoom_ * factor);
  if (std::abs(old_zoom - zoom_) < 0.0001) {
    return;
  }
  pan_ = view_transform().pan_placing(document_anchor, widget_position);
  constrain_pan();
  update_tool_cursor();
  update();
  notify_view_changed();
}

QRectF CanvasWidget::view_content_rect() const {
  const auto left = rulers_visible_ ? kLeftRulerWidth : 0;
  const auto top = rulers_visible_ ? kTopRulerHeight : 0;
  // The bars are shown whenever a document is open (sync_scroll_bars) and take
  // the same sizeHint thickness there, so a Fill that overflows one axis does
  // not bring a bar in afterwards.
  int right = 0;
  int bottom = 0;
  if (document_ != nullptr && document_->width() > 0 && document_->height() > 0 &&
      vertical_scroll_bar_ != nullptr && horizontal_scroll_bar_ != nullptr) {
    right = std::max(1, vertical_scroll_bar_->sizeHint().width());
    bottom = std::max(1, horizontal_scroll_bar_->sizeHint().height());
  }
  return QRectF(left, top, std::max(1, width() - left - right), std::max(1, height() - top - bottom));
}

void CanvasWidget::fit_to_view() {
  if (document_ == nullptr || document_->width() <= 0 || document_->height() <= 0 || width() <= 0 || height() <= 0) {
    return;
  }

  const auto area = view_content_rect();
  const auto available_width = std::max(1.0, area.width() - 2.0 * kFitMarginPerSide);
  const auto available_height = std::max(1.0, area.height() - 2.0 * kFitMarginPerSide);
  const auto unit_extent = CanvasViewTransform(1.0, QPointF(), view_rotation_degrees_,
                                               QSizeF(document_->width(), document_->height()))
                               .extent();
  zoom_ = clamp_logical_zoom(
      std::min(available_width / unit_extent.width(), available_height / unit_extent.height()));
  pan_ = centered_pan(area, view_transform());
  constrain_pan();
  update();
  notify_view_changed();
}

void CanvasWidget::fit_to_view_without_enlarging() {
  fit_to_view();
  if (document_ != nullptr && view_zoom() > 1.0) {
    set_view_zoom(1.0);
    center_document_in_view();
  }
}

void CanvasWidget::fill_to_view() {
  if (document_ == nullptr || document_->width() <= 0 || document_->height() <= 0 || width() <= 0 || height() <= 0) {
    return;
  }

  const auto area = view_content_rect();
  const auto unit_extent = CanvasViewTransform(1.0, QPointF(), view_rotation_degrees_,
                                               QSizeF(document_->width(), document_->height()))
                               .extent();
  zoom_ = clamp_logical_zoom(std::max(area.width() / unit_extent.width(), area.height() / unit_extent.height()));
  pan_ = centered_pan(area, view_transform());
  constrain_pan();
  update();
  notify_view_changed();
}

void CanvasWidget::center_document_in_view() {
  if (document_ == nullptr || document_->width() <= 0 || document_->height() <= 0 || width() <= 0 || height() <= 0) {
    return;
  }

  const auto area = view_content_rect();
  pan_ = centered_pan(area, view_transform());
  constrain_pan();
  update();
  notify_view_changed();
}

void CanvasWidget::scroll_view(ViewScroll scroll) {
  if (document_ == nullptr || document_->width() <= 0 || document_->height() <= 0 || width() <= 0 || height() <= 0) {
    return;
  }
  const auto area = view_content_rect();
  // Per axis: the pan range that keeps the image covering the view area, from the far edge
  // (`last`, the image's end at the area's end) to the near one (`first`, its start at the
  // area's start). Paging moves by one area span inside that range; a view already parked
  // past an edge (a Hand drag into the pasteboard) is not pulled back by a page the other way.
  const auto axis = [](double pan, double area_start, double area_span, double image_span, int step) {
    if (image_span <= area_span) {
      return pan;
    }
    const double first = area_start;
    const double last = area_start + area_span - image_span;
    if (step == -2) {
      return first;
    }
    if (step == 2) {
      return last;
    }
    if (step < 0) {
      return std::max(pan, std::min(pan + area_span, first));
    }
    return std::min(pan, std::max(pan - area_span, last));
  };
  // A rotated view pages over the document's bounding box.
  const auto view = view_transform();
  const auto image_width = view.extent().width();
  const auto image_height = view.extent().height();
  const auto offset = view.bounds_offset();
  int horizontal = 0;
  int vertical = 0;
  switch (scroll) {
    case ViewScroll::TopLeft:
      horizontal = vertical = -2;
      break;
    case ViewScroll::BottomRight:
      horizontal = vertical = 2;
      break;
    case ViewScroll::PageUp:
      vertical = -1;
      break;
    case ViewScroll::PageDown:
      vertical = 1;
      break;
    case ViewScroll::PageLeft:
      horizontal = -1;
      break;
    case ViewScroll::PageRight:
      horizontal = 1;
      break;
  }
  const auto old_pan = pan_;
  if (horizontal != 0) {
    pan_.setX(axis(pan_.x() + offset.x(), area.left(), area.width(), image_width, horizontal) - offset.x());
  }
  if (vertical != 0) {
    pan_.setY(axis(pan_.y() + offset.y(), area.top(), area.height(), image_height, vertical) - offset.y());
  }
  constrain_pan();
  if (pan_ == old_pan) {
    return;
  }
  update_tool_cursor();
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

  const auto area = view_content_rect();
  const auto available_width = std::max(1.0, area.width() - 2.0 * kFitMarginPerSide);
  const auto available_height = std::max(1.0, area.height() - 2.0 * kFitMarginPerSide);
  if (view_axis_aligned()) {
    zoom_ = clamp_logical_zoom(
        std::min(available_width / std::max(1.0, static_cast<double>(document_rect.width())),
                 available_height / std::max(1.0, static_cast<double>(document_rect.height()))));
    pan_ = QPointF(area.left() + (area.width() - static_cast<double>(document_rect.width()) * zoom_) / 2.0 -
                       static_cast<double>(document_rect.x()) * zoom_,
                   area.top() + (area.height() - static_cast<double>(document_rect.height()) * zoom_) / 2.0 -
                       static_cast<double>(document_rect.y()) * zoom_);
  } else {
    // Rotated: fit the rect's on-screen bounding box and center the rect.
    const auto unit_bounds =
        CanvasViewTransform(1.0, QPointF(), view_rotation_degrees_, QSizeF(document_->width(), document_->height()))
            .widget_bounds_of_document_rect(QRectF(document_rect));
    zoom_ = clamp_logical_zoom(std::min(available_width / std::max(1.0, unit_bounds.width()),
                                        available_height / std::max(1.0, unit_bounds.height())));
    pan_ = view_transform().pan_placing(QRectF(document_rect).center(), area.center());
  }
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
  invalidate_brush_overlay();
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

  // Per axis on the document's bounding box (the document itself while upright).
  const auto view = view_transform();
  const auto extent = view.extent();
  const auto offset = view.bounds_offset();
  const auto constrained =
      QPointF(constrained_document_axis(pan_.x() + offset.x(), static_cast<double>(width()), extent.width()) -
                  offset.x(),
              constrained_document_axis(pan_.y() + offset.y(), static_cast<double>(height()), extent.height()) -
                  offset.y());
  if (constrained == pan_) {
    return false;
  }

  pan_ = constrained;
  return true;
}

bool CanvasWidget::keep_view_centered_across_resize(QSize old_size) noexcept {
  if (document_ == nullptr || document_->width() <= 0 || document_->height() <= 0 || width() <= 0 ||
      height() <= 0 || !old_size.isValid() || old_size.width() <= 0 || old_size.height() <= 0) {
    return false;
  }
  // Per axis: a document that fits stays centered; otherwise the document point under the
  // viewport center stays there. A fitting document the user parked off center keeps its
  // offset from the center instead of snapping back.
  const auto axis = [](double pan, double old_span, double new_span, double document_span) {
    const bool was_centered =
        document_span > old_span || std::abs(pan - (old_span - document_span) / 2.0) < 1.0;
    if (document_span <= new_span && was_centered) {
      return (new_span - document_span) / 2.0;
    }
    return pan + (new_span - old_span) / 2.0;
  };
  // Spans and pans are taken in the view area (the rulers and scroll bars keep
  // their thickness across a resize), so a document Fit placed stays centered there.
  const auto area = view_content_rect();
  const auto band_width = static_cast<double>(width()) - area.width();
  const auto band_height = static_cast<double>(height()) - area.height();
  const auto view = view_transform();
  const auto extent = view.extent();
  const auto offset = view.bounds_offset();
  const auto old_pan = pan_;
  pan_ = QPointF(area.left() +
                     axis(pan_.x() + offset.x() - area.left(),
                          static_cast<double>(old_size.width()) - band_width, area.width(), extent.width()) -
                     offset.x(),
                 area.top() +
                     axis(pan_.y() + offset.y() - area.top(),
                          static_cast<double>(old_size.height()) - band_height, area.height(), extent.height()) -
                     offset.y());
  return pan_ != old_pan;
}

void CanvasWidget::shift_view_for_document_offset(QPoint document_shift) {
  if (document_shift.isNull()) {
    return;
  }
  pan_ -= view_transform().map_vector_to_widget(QPointF(document_shift));
  constrain_pan();
  update();
  notify_view_changed();
}

void CanvasWidget::notify_view_changed() {
  ZoomTraceScope trace("view_changed", zoom_);
  view_size_ = size();
  sync_scroll_bars();  // before the callback so observers see consistent bars
  // The warp preview covers only the area on screen at the view's level.
  schedule_warp_preview_view_refresh();
  if (view_changed_callback_) {
    view_changed_callback_();
  }
}

void CanvasWidget::sync_scroll_bars() {
  if (horizontal_scroll_bar_ == nullptr || vertical_scroll_bar_ == nullptr) {
    return;
  }
  syncing_scroll_bars_ = true;

  // Right and bottom edges, each shortened so the corner square stays free
  // (Photoshop layout). setGeometry no-ops when unchanged.
  const auto bar_thickness_v = std::max(1, vertical_scroll_bar_->sizeHint().width());
  const auto bar_thickness_h = std::max(1, horizontal_scroll_bar_->sizeHint().height());
  horizontal_scroll_bar_->setGeometry(0, height() - bar_thickness_h,
                                      std::max(0, width() - bar_thickness_v), bar_thickness_h);
  vertical_scroll_bar_->setGeometry(width() - bar_thickness_v, 0, bar_thickness_v,
                                    std::max(0, height() - bar_thickness_h));

  const bool bars_visible = document_ != nullptr && document_->width() > 0 && document_->height() > 0 &&
                            width() > 0 && height() > 0;
  horizontal_scroll_bar_->setVisible(bars_visible);
  vertical_scroll_bar_->setVisible(bars_visible);
  if (bars_visible) {
    const auto sync_axis = [](QScrollBar& bar, double pan, double viewport_span, double document_span) {
      const auto range = pan_axis_range(viewport_span, document_span);
      // Worst case fits int: a 300000 px document side at 128x zoom plus the
      // viewport is ~38.4M, and Qt's slider pixel math is qint64.
      const auto maximum = std::max(0, qRound(range.maximum - range.minimum));
      bar.setRange(0, maximum);
      bar.setPageStep(qRound(viewport_span));  // proportional thumb, like Photoshop
      bar.setSingleStep(kScrollBarSingleStep);
      // maximum and value are rounded independently, hence the explicit clamp.
      bar.setValue(std::clamp(qRound(range.maximum - pan), 0, maximum));
    };
    const auto view = view_transform();
    const auto extent = view.extent();
    const auto offset = view.bounds_offset();
    sync_axis(*horizontal_scroll_bar_, pan_.x() + offset.x(), static_cast<double>(width()), extent.width());
    sync_axis(*vertical_scroll_bar_, pan_.y() + offset.y(), static_cast<double>(height()), extent.height());
    horizontal_scroll_bar_->raise();  // stay above later-created children (inline text editor)
    vertical_scroll_bar_->raise();
  }
  syncing_scroll_bars_ = false;
}

void CanvasWidget::handle_scroll_bar_value_changed(Qt::Orientation orientation, int value) {
  if (syncing_scroll_bars_ || document_ == nullptr || document_->width() <= 0 || document_->height() <= 0) {
    return;
  }
  const bool horizontal = orientation == Qt::Horizontal;
  const auto viewport_span = static_cast<double>(horizontal ? width() : height());
  const auto view = view_transform();
  const auto document_span = horizontal ? view.extent().width() : view.extent().height();
  if (viewport_span <= 0.0 || document_span <= 0.0) {
    return;
  }
  const auto offset = horizontal ? view.bounds_offset().x() : view.bounds_offset().y();
  const auto target =
      pan_axis_range(viewport_span, document_span).maximum - static_cast<double>(value) - offset;
  auto& component = horizontal ? pan_.rx() : pan_.ry();
  if (component == target) {
    return;
  }
  component = target;
  constrain_pan();  // no-op by construction (target is inside the clamp range); safety net
  update();
  notify_view_changed();
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

void CanvasWidget::set_zoom_scrubby(bool enabled) noexcept {
  zoom_scrubby_ = enabled;
}

bool CanvasWidget::zoom_scrubby() const noexcept {
  return zoom_scrubby_;
}

void CanvasWidget::set_zoom_tool_zooms_out(bool enabled) {
  if (zoom_tool_zooms_out_ == enabled) {
    return;
  }
  zoom_tool_zooms_out_ = enabled;
  if (tool_ == CanvasTool::Zoom) {
    update_tool_cursor();
    update();
  }
}

bool CanvasWidget::zoom_tool_zooms_out() const noexcept {
  return zoom_tool_zooms_out_;
}

bool CanvasWidget::zoom_tool_zoom_out_active(Qt::KeyboardModifiers modifiers) const noexcept {
  return zoom_tool_zooms_out_ != ((modifiers & Qt::AltModifier) != 0);
}

QPointF CanvasWidget::zoom_click_anchor(QPointF widget_pos) const {
  // A press in the grey margin zooms toward the nearest point on the document
  // frame rather than toward the empty space under the cursor.
  if (document_ == nullptr) {
    return widget_pos;
  }
  if (!view_axis_aligned()) {
    const auto view = view_transform();
    const auto point = view.map_to_document(widget_pos);
    return view.map_to_widget(QPointF(std::clamp(point.x(), 0.0, static_cast<double>(document_->width())),
                                      std::clamp(point.y(), 0.0, static_cast<double>(document_->height()))));
  }
  const QRectF frame(widget_position_f(QPointF(0.0, 0.0)),
                     widget_position_f(QPointF(document_->width(), document_->height())));
  return QPointF(std::clamp(widget_pos.x(), frame.left(), frame.right()),
                 std::clamp(widget_pos.y(), frame.top(), frame.bottom()));
}

void CanvasWidget::zoom_click_step(QPointF widget_position, bool zoom_out) {
  if (document_ == nullptr) {
    return;
  }
  // A click in the grey margin zooms toward the nearest point on the document
  // frame rather than toward the empty space under the cursor. The step follows
  // Photoshop's zoom ladder (GitHub issue 77): from a rung to its neighbour,
  // from between rungs to the next one.
  const auto current_view_zoom = view_zoom();
  const auto next_view_zoom = next_zoom_ladder_step(current_view_zoom, !zoom_out);
  zoom_at_widget_point(zoom_click_anchor(widget_position), next_view_zoom / current_view_zoom);
}

bool CanvasWidget::hand_click_zooms(Qt::KeyboardModifiers modifiers) noexcept {
  return (modifiers & (Qt::ControlModifier | Qt::AltModifier)) != 0;
}

void CanvasWidget::draw_zoom_preview(QPainter& painter) const {
  // No marquee while the click would zoom out (Alt, or the Zoom Out mode: a
  // point zoom, not a rectangle) or while a Scrubby Zoom drag is zooming live.
  if (!zooming_ || zoom_scrubbing_ || zoom_tool_zoom_out_active(QApplication::keyboardModifiers())) {
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
  if (!view_axis_aligned()) {
    const auto point = view_transform().map_to_document(QPointF(widget_position));
    return QPoint(static_cast<int>(std::floor(point.x())), static_cast<int>(std::floor(point.y())));
  }
  const auto coordinate_from_widget = [this](int widget_coordinate, double pan, int limit) {
    auto coordinate = static_cast<int>(std::floor((static_cast<double>(widget_coordinate) - pan) / zoom_));
    if (document_ == nullptr || !deep_zoom_pixels_active()) {
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
  return view_transform().map_to_document(widget_position);
}

QPointF CanvasWidget::stroke_position_f(QPointF widget_position) const {
  return document_position_f(widget_position) - QPointF(0.5, 0.5);
}

QPointF CanvasWidget::widget_position_for_stroke_point(QPointF stroke_point) const {
  return widget_position_f(stroke_point + QPointF(0.5, 0.5));
}

QPoint CanvasWidget::widget_position(const QPoint& document_position) const {
  const auto point = view_transform().map_to_widget(QPointF(document_position));
  return QPoint(static_cast<int>(std::round(point.x())), static_cast<int>(std::round(point.y())));
}

QPointF CanvasWidget::widget_position_f(QPointF document_position) const {
  return view_transform().map_to_widget(document_position);
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
  apply_zoom_drag_step(delta);
}

void CanvasWidget::apply_zoom_drag_step(double delta_pixels) {
  if (std::abs(delta_pixels) < 0.001) {
    return;
  }
  zoom_at_widget_point(zoom_drag_anchor_widget_, std::pow(kZoomDragFactorPerPixel, delta_pixels));
}

void CanvasWidget::end_zoom_drag() {
  if (!pen_zoom_dragging_) {
    return;
  }
  pen_zoom_dragging_ = false;
  update_tool_cursor();
}

}  // namespace patchy::ui
