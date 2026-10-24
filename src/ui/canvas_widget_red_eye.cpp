// CanvasWidget's Red Eye tool (docs/red-eye.md). The user picks the eye: a click
// works in a fixed box around the press point, a drag inside the dragged box. The correction
// (core/red_eye.hpp) weighs pixels by fixed color rules, grows the red area from the user's
// point, and desaturates and darkens it by each pixel's redness, once, on release. Nothing here
// may look for faces or eyes, scan the whole image, or correct every eye at once: that needs
// a new patent check first (docs/legal-constraints.md, "Red Eye").

#include "ui/canvas_widget.hpp"

#include "core/layer.hpp"
#include "core/red_eye.hpp"
#include "ui/canvas_widget_shared.hpp"

#include <QPainter>
#include <QPen>
#include <QPoint>
#include <QRect>
#include <QRectF>
#include <QRegion>
#include <QString>
#include <Qt>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <utility>

namespace patchy::ui {

void CanvasWidget::set_red_eye_pupil_size(int percent) noexcept {
  red_eye_pupil_size_ = std::clamp(percent, 1, 100);
}

int CanvasWidget::red_eye_pupil_size() const noexcept {
  return red_eye_pupil_size_;
}

void CanvasWidget::set_red_eye_darken_amount(int percent) noexcept {
  red_eye_darken_amount_ = std::clamp(percent, 1, 100);
}

int CanvasWidget::red_eye_darken_amount() const noexcept {
  return red_eye_darken_amount_;
}

void CanvasWidget::begin_red_eye_gesture(QPoint document_point) {
  red_eye_dragging_ = true;
  red_eye_press_point_ = document_point;
  red_eye_current_point_ = document_point;
}

void CanvasWidget::update_red_eye_gesture(QPoint document_point) {
  if (!red_eye_dragging_ || document_point == red_eye_current_point_) {
    return;
  }
  red_eye_current_point_ = document_point;
  update();
}

void CanvasWidget::cancel_red_eye_gesture() {
  if (red_eye_dragging_) {
    red_eye_dragging_ = false;
    update();
  }
}

// The dragged box in document pixels, or an empty rect while the gesture is still a click
// (under three pixels either way).
QRect CanvasWidget::red_eye_drag_box() const {
  const auto box = QRect(red_eye_press_point_, red_eye_current_point_).normalized();
  if (box.width() < 3 || box.height() < 3) {
    return {};
  }
  return box;
}

void CanvasWidget::finish_red_eye_gesture(QPoint document_point) {
  if (!red_eye_dragging_) {
    return;
  }
  red_eye_current_point_ = document_point;
  const auto box = red_eye_drag_box();
  red_eye_dragging_ = false;
  update();
  if (box.isEmpty()) {
    apply_red_eye_at(red_eye_press_point_, std::nullopt, red_eye_pupil_size_, red_eye_darken_amount_);
  } else {
    apply_red_eye_at(box.center(), box, red_eye_pupil_size_, red_eye_darken_amount_);
  }
}

CanvasWidget::RedEyeResult CanvasWidget::apply_red_eye_at(QPoint point, std::optional<QRect> box, int pupil_size,
                                                          int darken_amount, bool record_history) {
  RedEyeResult result;
  const auto refuse = [&](QString message) {
    result.error = std::move(message);
    report_status_error(result.error);
    return result;
  };
  if (document_ == nullptr) {
    return refuse(tr("Red Eye needs an open document"));
  }
  if (editing_grayscale_target()) {
    return refuse(tr("Red Eye is unavailable while editing a mask or channel"));
  }
  if (!can_begin_pixel_edit(/*report=*/record_history)) {
    result.error = tr("Red Eye needs an editable pixel layer");
    return result;
  }
  const auto* layer = active_pixel_layer();
  if (layer == nullptr) {
    result.error = tr("Red Eye needs an editable pixel layer");
    return result;
  }
  const auto layer_id = layer->id();
  const auto layer_bounds = std::as_const(*layer).bounds();

  // The working region in document pixels: the drag box, or a fixed box around a click.
  QRect region;
  std::int32_t search_radius = 0;
  if (box.has_value() && !box->isEmpty()) {
    region = box->normalized();
    search_radius = std::max(region.width(), region.height());
  } else {
    const auto side = patchy::red_eye_click_box_size(document_->width(), document_->height());
    region = QRect(point.x() - side / 2, point.y() - side / 2, side, side);
    search_radius = side / 6;
  }
  const patchy::RedEyeOptions options{std::clamp(pupil_size, 1, 100), std::clamp(darken_amount, 1, 100)};
  const patchy::Rect buffer_region{region.x() - layer_bounds.x, region.y() - layer_bounds.y, region.width(),
                           region.height()};
  const auto mask = patchy::find_red_eye(std::as_const(*layer).pixels(), buffer_region, point.x() - layer_bounds.x,
                                         point.y() - layer_bounds.y, search_radius, options,
                                         /*limit_to_disk=*/!box.has_value());
  if (mask.empty()) {
    return refuse(box.has_value() ? tr("Red Eye found no red pixels in the box")
                                  : tr("Red Eye found no red pixels near the click"));
  }

  std::function<float(std::int32_t, std::int32_t)> coverage;
  if (has_selection()) {
    coverage = [this, layer_bounds](std::int32_t x, std::int32_t y) {
      return static_cast<float>(selection_alpha_at(QPoint(x + layer_bounds.x, y + layer_bounds.y))) / 255.0F;
    };
    bool any_selected = false;
    for (std::int32_t y = mask.bounds.y; y < mask.bounds.y + mask.bounds.height && !any_selected; ++y) {
      for (std::int32_t x = mask.bounds.x; x < mask.bounds.x + mask.bounds.width; ++x) {
        if (mask.at(x, y) != 0U && coverage(x, y) > 0.0F) {
          any_selected = true;
          break;
        }
      }
    }
    if (!any_selected) {
      return refuse(tr("Red Eye: the red area is outside the selection"));
    }
  }

  if (record_history && !begin_edit(tr("Red Eye"))) {
    result.error = tr("Red Eye needs an editable pixel layer");
    return result;
  }
  auto* target = active_pixel_layer();
  if (target == nullptr || target->id() != layer_id) {
    result.error = tr("Red Eye needs an editable pixel layer");
    return result;
  }
  const auto changed = patchy::apply_red_eye(target->pixels(), mask, options, coverage, palette_snap_for_edits());
  if (changed.empty()) {
    notify_document_changed();
    return result;
  }
  result.pupil_pixels = mask.pupil_pixels;
  result.changed = QRect(changed.x + layer_bounds.x, changed.y + layer_bounds.y, changed.width, changed.height);
  active_edit_target_changed_impl(QRegion(result.changed));
  return result;
}

// The dragged box, drawn like the Object Selection box (dark line under white dashes).
void CanvasWidget::draw_red_eye_overlay(QPainter& painter) const {
  if (!red_eye_dragging_) {
    return;
  }
  const auto box = red_eye_drag_box();
  if (box.isEmpty()) {
    return;
  }
  const auto rect = QRectF(widget_position_f(QPointF(box.topLeft())),
                           widget_position_f(QPointF(box.bottomRight() + QPoint(1, 1))))
                        .normalized();
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, false);
  painter.setBrush(Qt::NoBrush);
  painter.setPen(QPen(Qt::black, 1.0, Qt::SolidLine));
  painter.drawRect(rect);
  QPen dashes(Qt::white, 1.0, Qt::CustomDashLine);
  dashes.setDashPattern({4.0, 4.0});
  painter.setPen(dashes);
  painter.drawRect(rect);
  painter.restore();
}

}  // namespace patchy::ui
