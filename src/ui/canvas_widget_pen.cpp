// CanvasWidget's tablet/pen input, split out of canvas_widget.cpp:
// tabletEvent and the pen-input settings, the tablet-event sample builder,
// the pen barrel-button action mapping with the proximity tracking, the
// tablet pan/zoom gesture tests, and dispatch_tablet_as_mouse (the synthetic
// mouse bridge with the Alt+barrel brush-adjust gesture and the
// paint-suppression rules). Pure function moves from canvas_widget.cpp;
// behavior must stay identical.

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

void CanvasWidget::set_pen_input_settings(PenInputSettings settings) noexcept {
  settings.pressure_size_min_percent = std::clamp(settings.pressure_size_min_percent, 1, 100);
  settings.pressure_opacity_min_percent = std::clamp(settings.pressure_opacity_min_percent, 1, 100);
  settings.tilt_min_roundness_percent = std::clamp(settings.tilt_min_roundness_percent, 1, 100);
  pen_input_settings_ = settings;
  if (!pen_input_settings_.enabled) {
    active_pen_input_sample_.reset();
  }
}

const CanvasWidget::PenInputSettings& CanvasWidget::pen_input_settings() const noexcept {
  return pen_input_settings_;
}

std::optional<CanvasWidget::PenInputSample> CanvasWidget::last_pen_input_sample() const {
  return last_pen_input_sample_;
}

void CanvasWidget::tabletEvent(QTabletEvent* event) {
  if (event == nullptr || !pen_input_settings_.enabled) {
    active_pen_input_sample_.reset();
    QWidget::tabletEvent(event);
    return;
  }

  last_tablet_event_ms_ = pen_proximity_clock_.elapsed();
  const auto sample = pen_input_sample_from_tablet_event(*event);
  last_pen_input_sample_ = sample;
  active_pen_input_sample_ = sample;
  if (dispatch_tablet_as_mouse(event, sample)) {
    event->accept();
    if (event->type() == QEvent::TabletRelease) {
      active_pen_input_sample_.reset();
    }
    return;
  }

  active_pen_input_sample_.reset();
  event->ignore();
}

CanvasWidget::PenInputSample CanvasWidget::pen_input_sample_from_tablet_event(const QTabletEvent& event) const {
  PenInputSample sample;
  sample.widget_position = event.position();
  sample.document_position = document_position_f(event.position());
  sample.button = event.button();
  sample.buttons = event.buttons();
  sample.modifiers = event.modifiers();

  const auto* device = event.pointingDevice();
  const auto capabilities = device != nullptr ? device->capabilities() : QInputDevice::Capabilities{};
  sample.device_id = device != nullptr ? device->uniqueId().numericId() : qint64{-1};
  if (device != nullptr) {
    switch (device->pointerType()) {
      case QPointingDevice::PointerType::Pen:
        sample.pointer_type = PenInputSample::PointerType::Pen;
        break;
      case QPointingDevice::PointerType::Eraser:
        sample.pointer_type = PenInputSample::PointerType::Eraser;
        break;
      case QPointingDevice::PointerType::Cursor:
        sample.pointer_type = PenInputSample::PointerType::Cursor;
        break;
      default:
        sample.pointer_type = PenInputSample::PointerType::Unknown;
        break;
    }
  }

  sample.pressure_available = capabilities.testFlag(QInputDevice::Capability::Pressure);
  sample.pressure = sample.pressure_available && std::isfinite(event.pressure())
                        ? std::clamp(static_cast<float>(event.pressure()), 0.0F, 1.0F)
                        : 1.0F;
  sample.x_tilt = std::isfinite(event.xTilt()) ? static_cast<float>(event.xTilt()) : 0.0F;
  sample.y_tilt = std::isfinite(event.yTilt()) ? static_cast<float>(event.yTilt()) : 0.0F;
  sample.tilt_available = capabilities.testFlag(QInputDevice::Capability::XTilt) ||
                          capabilities.testFlag(QInputDevice::Capability::YTilt);
  sample.tangential_pressure_available =
      capabilities.testFlag(QInputDevice::Capability::TangentialPressure);
  sample.tangential_pressure =
      sample.tangential_pressure_available && std::isfinite(event.tangentialPressure())
          ? std::clamp(static_cast<float>(event.tangentialPressure()), -1.0F, 1.0F)
          : 0.0F;
  sample.rotation_available = capabilities.testFlag(QInputDevice::Capability::Rotation);
  sample.rotation_degrees =
      sample.rotation_available && std::isfinite(event.rotation()) ? static_cast<double>(event.rotation()) : 0.0;
  sample.z_available = capabilities.testFlag(QInputDevice::Capability::ZPosition);
  sample.z = sample.z_available && std::isfinite(event.z()) ? static_cast<float>(event.z()) : 0.0F;
  return sample;
}

PenButtonAction CanvasWidget::pen_action_for_button(Qt::MouseButton button) const noexcept {
  switch (button) {
    case Qt::RightButton:
      return pen_input_settings_.primary_button_action;
    case Qt::MiddleButton:
      return pen_input_settings_.secondary_button_action;
    default:
      return PenButtonAction::None;
  }
}

bool CanvasWidget::pen_recently_in_proximity() const {
  // Hover tablet events stream continuously while the pen is over the canvas,
  // so "a tablet event arrived a moment ago" is a reliable in-proximity test.
  constexpr qint64 kPenProximityWindowMs = 500;
  return last_tablet_event_ms_ >= 0 &&
         pen_proximity_clock_.elapsed() - last_tablet_event_ms_ <= kPenProximityWindowMs;
}

bool CanvasWidget::tablet_event_should_pan(const PenInputSample& sample, QEvent::Type event_type) const noexcept {
  if (event_type == QEvent::TabletRelease && panning_) {
    return true;
  }
  // An active stroke and a fresh tip press are unambiguous "paint" intents.
  // Some drivers keep reporting a barrel button as held until the pen leaves
  // proximity, and trusting that phantom state here turned every stroke after
  // a barrel gesture into a pan until the pen was lifted.
  if (painting_) {
    return false;
  }
  if (event_type == QEvent::TabletPress && sample.button == Qt::LeftButton) {
    return false;
  }
  if (pen_action_for_button(sample.button) == PenButtonAction::PanCanvas) {
    return true;
  }
  for (const auto button : {Qt::RightButton, Qt::MiddleButton}) {
    if ((sample.buttons & button) != 0 && pen_action_for_button(button) == PenButtonAction::PanCanvas) {
      return true;
    }
  }
  return false;
}

bool CanvasWidget::tablet_event_should_zoom(const PenInputSample& sample, QEvent::Type event_type) const noexcept {
  if (event_type == QEvent::TabletRelease && pen_zoom_dragging_) {
    return true;
  }
  // Same phantom-held-button defense as tablet_event_should_pan.
  if (painting_) {
    return false;
  }
  if (event_type == QEvent::TabletPress && sample.button == Qt::LeftButton) {
    return false;
  }
  if (pen_action_for_button(sample.button) == PenButtonAction::ZoomCanvas) {
    return true;
  }
  for (const auto button : {Qt::RightButton, Qt::MiddleButton}) {
    if ((sample.buttons & button) != 0 && pen_action_for_button(button) == PenButtonAction::ZoomCanvas) {
      return true;
    }
  }
  return false;
}

bool CanvasWidget::perform_pen_button_action(PenButtonAction action, const PenInputSample& sample) {
  switch (action) {
    case PenButtonAction::PickColor:
      pick_color(sample.document_position.toPoint());
      return true;
    case PenButtonAction::SetCloneSource:
      if (tool_ == CanvasTool::Clone || tool_ == CanvasTool::Healing) {
        set_clone_source(sample.document_position.toPoint());
      } else if (status_callback_) {
        status_callback_(tr("Select the Clone or Healing Brush tool to set a sample source"));
      }
      return true;
    case PenButtonAction::SwapColors:
    case PenButtonAction::Undo:
    case PenButtonAction::Redo:
    case PenButtonAction::ToggleEraser:
    case PenButtonAction::IncreaseBrushSize:
    case PenButtonAction::DecreaseBrushSize:
      if (pen_button_action_callback_) {
        pen_button_action_callback_(action);
      }
      return true;
    case PenButtonAction::PanCanvas:
    case PenButtonAction::ZoomCanvas:
    case PenButtonAction::None:
      return false;
  }
  return false;
}

bool CanvasWidget::dispatch_tablet_as_mouse(QTabletEvent* event, const PenInputSample& sample) {
  if (event == nullptr) {
    return false;
  }

  QEvent::Type mouse_type = QEvent::None;
  switch (event->type()) {
    case QEvent::TabletPress:
      mouse_type = QEvent::MouseButtonPress;
      break;
    case QEvent::TabletMove:
      mouse_type = QEvent::MouseMove;
      break;
    case QEvent::TabletRelease:
      mouse_type = QEvent::MouseButtonRelease;
      break;
    default:
      return false;
  }

  // Alt+barrel-button drag mirrors the Alt+Right mouse brush size/softness
  // gesture. It must win over the pan/zoom drags and the configured pen-button
  // action below, just as the mouse gesture wins over right-button pan, and it
  // is driven directly (like the zoom drag) because barrel buttons keep the
  // synthetic mouse path suppressed while held.
  if (brush_adjust_dragging_) {
    const bool right_button_held = (sample.buttons & Qt::RightButton) != 0;
    if (event->type() == QEvent::TabletMove) {
      if (right_button_held) {
        update_brush_adjust_drag(sample.widget_position.toPoint());
      } else {
        // The barrel release never reached us as a tablet event (drivers can
        // re-route it as a plain mouse event, or drop it when the pen leaves
        // proximity mid-drag). Commit the on-screen values instead of leaving
        // the overlay stuck on a button nobody is holding.
        end_brush_adjust_drag(true);
        pen_button_suppressing_paint_ = false;
      }
      return true;
    }
    if (event->type() == QEvent::TabletRelease) {
      if (sample.button == Qt::RightButton || !right_button_held) {
        end_brush_adjust_drag(true);
      }
      if ((sample.buttons & (Qt::RightButton | Qt::MiddleButton)) == 0) {
        pen_button_suppressing_paint_ = false;
      }
      return true;
    }
    // Only TabletPress reaches here. A re-press of the gesture button keeps
    // the drag; anything else — a tip touch, or any press after the release
    // was lost or the driver latched the barrel as held — ends the drag and
    // falls through so painting resumes immediately.
    if (sample.button == Qt::RightButton && right_button_held) {
      return true;
    }
    end_brush_adjust_drag(true);
    pen_button_suppressing_paint_ = false;
  }
  if (event->type() == QEvent::TabletPress && sample.button == Qt::RightButton &&
      (sample.modifiers & Qt::AltModifier) != 0 && !edit_locked_ && !spacebar_panning_ && !painting_ &&
      !drawing_shape_ && !transforming_layer_ && tool_supports_brush_adjust_drag(tool_)) {
    pen_button_suppressing_paint_ = true;
    begin_brush_adjust_drag(sample.widget_position.toPoint(), true);
    return true;
  }

  const auto pan = tablet_event_should_pan(sample, event->type());

  // Zoom is a drag gesture like pan, but the canvas has no equivalent mouse
  // button, so it is handled directly rather than through a synthetic event.
  if (!pan && tablet_event_should_zoom(sample, event->type())) {
    switch (event->type()) {
      case QEvent::TabletPress:
        begin_zoom_drag(sample.widget_position);
        pen_button_suppressing_paint_ = true;
        break;
      case QEvent::TabletMove:
        update_zoom_drag(sample.widget_position);
        break;
      case QEvent::TabletRelease:
        end_zoom_drag();
        pen_button_suppressing_paint_ = false;
        break;
      default:
        break;
    }
    return true;
  }

  // Resolve pen barrel/side buttons to their configured actions. Pan is a drag
  // gesture handled below through the synthetic right-button path; every other
  // mapped action fires once on press and then suppresses painting until the
  // button is released so that moving the pen with the button held does not
  // leave a stray stroke.
  if (!pan) {
    const auto pressed_action = pen_action_for_button(sample.button);
    if (event->type() == QEvent::TabletPress) {
      if (sample.button != Qt::LeftButton && sample.button != Qt::NoButton) {
        pen_button_suppressing_paint_ = true;
        perform_pen_button_action(pressed_action, sample);
        return true;
      }
      // A pen-tip press with no barrel button held should always be able to
      // paint. Clear any stale suppression left over from a barrel release that
      // the tablet driver may have delivered as a plain mouse event.
      if ((sample.buttons & (Qt::RightButton | Qt::MiddleButton)) == 0) {
        pen_button_suppressing_paint_ = false;
      }
    }
    if (pen_button_suppressing_paint_) {
      if (event->type() == QEvent::TabletRelease &&
          (sample.buttons & (Qt::RightButton | Qt::MiddleButton)) == 0) {
        pen_button_suppressing_paint_ = false;
      }
      return true;
    }
  } else {
    pen_button_suppressing_paint_ = false;
  }

  Qt::MouseButton button = Qt::NoButton;
  Qt::MouseButtons buttons = Qt::NoButton;
  if (pan) {
    button = event->type() == QEvent::TabletMove ? Qt::NoButton : Qt::RightButton;
    buttons = event->type() == QEvent::TabletRelease ? Qt::NoButton : Qt::RightButton;
  } else {
    switch (event->type()) {
      case QEvent::TabletPress:
        button = Qt::LeftButton;
        buttons = Qt::LeftButton;
        break;
      case QEvent::TabletMove:
        button = Qt::NoButton;
        buttons = sample.buttons;
        if (painting_ || (sample.buttons & Qt::LeftButton) != 0 ||
            (sample.pressure_available && sample.pressure > 0.0F)) {
          buttons |= Qt::LeftButton;
        }
        break;
      case QEvent::TabletRelease:
        button = Qt::LeftButton;
        buttons = Qt::NoButton;
        break;
      default:
        break;
    }
  }

  QMouseEvent mouse_event(mouse_type, sample.widget_position,
                          QPointF(mapToGlobal(sample.widget_position.toPoint())), button, buttons,
                          sample.modifiers);
  handling_tablet_event_ = true;
  switch (mouse_type) {
    case QEvent::MouseButtonPress:
      mousePressEvent(&mouse_event);
      break;
    case QEvent::MouseMove:
      mouseMoveEvent(&mouse_event);
      break;
    case QEvent::MouseButtonRelease:
      mouseReleaseEvent(&mouse_event);
      break;
    default:
      handling_tablet_event_ = false;
      return false;
  }
  handling_tablet_event_ = false;
  return true;
}

}  // namespace patchy::ui
