// CanvasWidget's event dispatchers, split out of canvas_widget.cpp: the
// app-level eventFilter, event() (the magnetic-lasso/guide ShortcutOverride
// and native pinch gesture), the wheel/resize/mouse/key/focus/enter/leave/
// timer handlers, handle_opacity_digit_key, refresh_tool_cursor, begin_edit,
// and effective_tool_for_input. Pure function moves from canvas_widget.cpp;
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
#include "core/vector_shape.hpp"
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

bool render_trace_enabled() noexcept {
  static const bool enabled = qEnvironmentVariableIsSet("PATCHY_RENDER_TRACE");
  return enabled;
}

// Expand a (non-negative) document rect outward so its edges land on the
// 2^level mip-block grid. Patches aligned this way downscale to exactly the
// same pixels the full-image mip chain produces for that area.
QRect rect_aligned_to_mip_grid(QRect rect, int level) {
  const int block = 1 << level;
  const int left = (rect.left() / block) * block;
  const int top = (rect.top() / block) * block;
  const int right = ((rect.right() / block) + 1) * block - 1;
  const int bottom = ((rect.bottom() / block) + 1) * block - 1;
  return QRect(QPoint(left, top), QPoint(right, bottom));
}

bool move_layer_has_expensive_style(const Layer& layer) {
  const auto& style = layer.layer_style();
  return style.effects_visible && !style.empty();
}

bool tool_supports_off_canvas_brush_strokes(CanvasTool tool) noexcept {
  switch (tool) {
    case CanvasTool::Brush:
    case CanvasTool::MixerBrush:
    case CanvasTool::PatternStamp:
    case CanvasTool::Clone:
    case CanvasTool::Healing:
    case CanvasTool::Smudge:
    case CanvasTool::Dodge:
    case CanvasTool::Burn:
    case CanvasTool::Sponge:
    case CanvasTool::BlurBrush:
    case CanvasTool::SharpenBrush:
    case CanvasTool::Eraser:
      return true;
    default:
      return false;
  }
}

// Painting tools where Shift+click extends the previous stroke with a straight
// segment from its end point (Photoshop behaviour).
bool tool_supports_shift_click_stroke_connect(CanvasTool tool) noexcept {
  switch (tool) {
    case CanvasTool::Brush:
    case CanvasTool::MixerBrush:
    case CanvasTool::PatternStamp:
    case CanvasTool::Clone:
    case CanvasTool::Healing:
    case CanvasTool::Smudge:
    case CanvasTool::Dodge:
    case CanvasTool::Burn:
    case CanvasTool::Sponge:
    case CanvasTool::BlurBrush:
    case CanvasTool::SharpenBrush:
    case CanvasTool::Eraser:
      return true;
    default:
      return false;
  }
}

bool is_local_adjustment_tool(CanvasTool tool) noexcept {
  switch (tool) {
    case CanvasTool::Dodge:
    case CanvasTool::Burn:
    case CanvasTool::Sponge:
    case CanvasTool::BlurBrush:
    case CanvasTool::SharpenBrush:
      return true;
    default:
      return false;
  }
}

// Tools whose opacity the bare digit keys adjust while the canvas has focus.
bool tool_supports_opacity_digit_keys(CanvasTool tool) noexcept {
  switch (tool) {
    case CanvasTool::Brush:
    case CanvasTool::PatternStamp:
    case CanvasTool::Clone:
    case CanvasTool::Healing:
    case CanvasTool::Smudge:
    case CanvasTool::Eraser:
    case CanvasTool::Gradient:
    case CanvasTool::Line:
    case CanvasTool::Rectangle:
    case CanvasTool::Ellipse:
      return true;
    default:
      return false;
  }
}

constexpr int kAirbrushTimerIntervalMs = 50;

}  // namespace

bool CanvasWidget::eventFilter(QObject* watched, QEvent* event) {
  // Refresh the selection-mode cursor badge on any Shift/Alt change, regardless
  // of which widget holds focus. Gated on visibility (so only the active
  // document tab reacts); setting the cursor while the pointer is elsewhere is
  // harmless since it only shows once the pointer is back over the canvas.
  if ((event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) && isVisible() &&
      !selecting_ && !lassoing_ && !magnetic_lassoing_ && !quick_selecting_ && !moving_selection_ &&
      !spacebar_panning_ && !panning_) {
    auto* key_event = static_cast<QKeyEvent*>(event);
    if (!key_event->isAutoRepeat() &&
        (key_event->key() == Qt::Key_Shift || key_event->key() == Qt::Key_Alt)) {
      // The event reports the modifier state before this key, so fold the
      // pressed/released key into the modifiers we evaluate.
      const auto bit = key_event->key() == Qt::Key_Shift ? Qt::ShiftModifier : Qt::AltModifier;
      const auto modifiers = event->type() == QEvent::KeyPress ? (key_event->modifiers() | bit)
                                                               : (key_event->modifiers() & ~bit);
      if (tool_ == CanvasTool::Zoom) {
        if (key_event->key() == Qt::Key_Alt) {
          // Idle: Alt flips the magnifier badge between + and -. Mid-drag: Alt
          // suppresses the marquee, so repaint to show/hide the preview.
          if (zooming_) {
            update();
          } else {
            apply_zoom_cursor((modifiers & Qt::AltModifier) != 0);
          }
        }
      } else if (key_event->key() == Qt::Key_Alt && tool_uses_alt_left_for_color_pick(tool_) &&
                 !painting_ && !drawing_shape_) {
        // Alt is the temporary-eyedropper modifier for paint/shape/fill tools;
        // swap to (or back from) the eyedropper cursor the instant it toggles.
        // Drive it from the folded modifier state (authoritative here, unlike the
        // global keyboard state, which can lag this filter and leave the cursor
        // stuck on release). Skipped mid-stroke, where Alt has no picking effect.
        alt_color_pick_cursor_override_ = (modifiers & Qt::AltModifier) != 0;
        update_tool_cursor();
        alt_color_pick_cursor_override_.reset();
      } else {
        const auto mode = selection_operation(modifiers);
        apply_selection_cursor_for_mode(mode);
        if (selection_mode_changed_callback_) {
          selection_mode_changed_callback_(mode);
        }
      }
    }
  }
  return QWidget::eventFilter(watched, event);
}

bool CanvasWidget::event(QEvent* event) {
  if (event->type() == QEvent::ShortcutOverride) {
    const auto* key_event = static_cast<QKeyEvent*>(event);
    if (key_event->modifiers() == Qt::NoModifier &&
        (key_event->key() == Qt::Key_Backspace || key_event->key() == Qt::Key_Delete)) {
      // While a magnetic-lasso trace is live (Backspace pops the last anchor) or guides
      // are selected (Delete/Backspace removes them), the canvas owns these keys.
      // Accepting the override suppresses the app-level shortcuts (layer.clear binds
      // Backspace on macOS and Delete everywhere) so keyPressEvent receives a plain key
      // event instead of QShortcutMap consuming it first.
      if (magnetic_lasso_active() || pen_session_active_ ||
          ((tool_ == CanvasTool::PathSelect || tool_ == CanvasTool::DirectSelect) &&
           !path_selected_anchors_.empty()) ||
          (!guides_locked_ && has_selected_guides())) {
        event->accept();
        return true;
      }
    }
  }
  if (event->type() == QEvent::NativeGesture) {
    const auto* gesture = static_cast<QNativeGestureEvent*>(event);
    if (gesture->gestureType() == Qt::ZoomNativeGesture) {
      // macOS trackpad pinch: value() is this step's incremental scale delta. Zoom about
      // the pointer exactly like Alt+wheel (Photoshop-mac behavior).
      zoom_at_widget_point(gesture->position(), 1.0 + gesture->value());
      event->accept();
      return true;
    }
  }
  return QWidget::event(event);
}

void CanvasWidget::wheelEvent(QWheelEvent* event) {
  const auto wheel_delta = !event->pixelDelta().isNull() ? event->pixelDelta() : event->angleDelta();
  const auto primary_delta = wheel_delta.y() != 0 ? wheel_delta.y() : wheel_delta.x();
  if (primary_delta == 0) {
    event->accept();
    return;
  }

  // Alt+wheel always zooms, in either mode.
  if ((event->modifiers() & Qt::AltModifier) != 0) {
    zoom_at_widget_point(event->position(), primary_delta > 0 ? 1.1 : 0.9);
    event->accept();
    return;
  }

  // In wheel-zoom mode a plain wheel zooms (centered on the cursor) and the
  // modifiers pan; otherwise a plain wheel pans (Photoshop-style navigation).
  // A pen button configured as "scroll" arrives here as a wheel event, so this
  // mode also lets that button zoom.
  if (wheel_zooms_) {
    const auto modifiers = event->modifiers();
    if ((modifiers & Qt::ControlModifier) == 0 && (modifiers & Qt::ShiftModifier) == 0) {
      zoom_at_widget_point(event->position(), primary_delta > 0 ? 1.1 : 0.9);
      event->accept();
      return;
    }
  }

  constexpr double kWheelPanScale = 0.5;
  const auto old_pan = pan_;
  const bool pan_vertically =
      wheel_zooms_ ? (event->modifiers() & Qt::ShiftModifier) == 0
                   : (event->modifiers() & Qt::ControlModifier) != 0;
  if (pan_vertically) {
    pan_.ry() += static_cast<double>(primary_delta) * kWheelPanScale;
  } else {
    pan_.rx() += static_cast<double>(primary_delta) * kWheelPanScale;
  }
  constrain_pan();
  event->accept();
  if (pan_ != old_pan) {
    update();
    notify_view_changed();
  }
}

void CanvasWidget::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  if (isVisible() && constrain_pan()) {
    update();
    notify_view_changed();
  }
}

void CanvasWidget::mousePressEvent(QMouseEvent* event) {
  if (!handling_tablet_event_) {
    active_pen_input_sample_.reset();
  }
  setFocus(Qt::MouseFocusReason);
  last_mouse_position_ = event->pos();
  emit_info_for_widget_position(event->pos());

  // Right-click on a ruler opens the unit menu (Photoshop's gesture); it must win
  // over the right-button drag-to-pan below.
  if (event->button() == Qt::RightButton && rulers_visible_ && widget_position_in_ruler(event->pos())) {
    show_ruler_unit_menu(event->globalPosition().toPoint());
    event->accept();
    return;
  }

  if (brush_adjust_dragging_) {
    if ((event->buttons() & Qt::RightButton) != 0) {
      event->accept();
      return;
    }
    // Stale drag after a lost right-button release: end it and let this
    // press behave normally.
    end_brush_adjust_drag(true);
  }

  // Photoshop-style brush resize: Alt+Right-drag adjusts size (horizontal)
  // and softness (vertical). Must win over the right-button pan below. Pen
  // barrel buttons get the same Alt chord in dispatch_tablet_as_mouse; without
  // Alt their synthesized right presses keep the configured pen action, so the
  // tablet path is excluded here.
  if (event->button() == Qt::RightButton && (event->modifiers() & Qt::AltModifier) != 0 &&
      !handling_tablet_event_ && !edit_locked_ && !spacebar_panning_ && !painting_ && !drawing_shape_ &&
      !transforming_layer_ && tool_supports_brush_adjust_drag(tool_)) {
    begin_brush_adjust_drag(event->pos());
    event->accept();
    return;
  }

  // A stale pen-button mouse gesture after a lost release, or a left press
  // that must win over it, ends the gesture so this press behaves normally.
  if (mouse_pen_action_button_ != Qt::NoButton &&
      ((event->buttons() & mouse_pen_action_button_) == 0 || event->button() == Qt::LeftButton)) {
    mouse_pen_action_button_ = Qt::NoButton;
    if (pen_zoom_dragging_) {
      end_zoom_drag();
    }
  }

  // Pen buttons mapped to "Right/Middle Mouse Click" in the tablet driver
  // arrive as plain mouse presses, not tablet events, so resolve the
  // configured pen-button action here. Gated on the pen actually hovering
  // (tablet events streamed a moment ago), which a bare mouse never
  // satisfies, so a real mouse keeps the classic middle/right drag-to-pan.
  if (!handling_tablet_event_ && pen_input_settings_.enabled && !painting_ && !drawing_shape_ &&
      !spacebar_panning_ && (event->modifiers() & Qt::AltModifier) == 0 &&
      (event->button() == Qt::RightButton || event->button() == Qt::MiddleButton) &&
      pen_recently_in_proximity()) {
    if (mouse_pen_action_button_ != Qt::NoButton) {
      // A second pen button while a gesture is active: swallow it.
      event->accept();
      return;
    }
    const auto action = pen_action_for_button(event->button());
    if (action == PenButtonAction::ZoomCanvas) {
      mouse_pen_action_button_ = event->button();
      begin_zoom_drag(event->position());
      event->accept();
      return;
    }
    if (action != PenButtonAction::PanCanvas) {
      // One-shot actions fire on press; None just swallows. Either way the
      // held button must not fall through and start a pan.
      mouse_pen_action_button_ = event->button();
      auto sample = last_pen_input_sample_.value_or(PenInputSample{});
      sample.widget_position = event->position();
      sample.document_position = document_position_f(event->position());
      sample.button = event->button();
      sample.buttons = event->buttons();
      sample.modifiers = event->modifiers();
      perform_pen_button_action(action, sample);
      event->accept();
      return;
    }
    // PanCanvas falls through to the standard drag-to-pan below.
  }

  if (spacebar_panning_ || tool_ == CanvasTool::Pan || (event->buttons() & Qt::MiddleButton) != 0 ||
      (event->buttons() & Qt::RightButton) != 0) {
    panning_ = true;
    setCursor(Qt::ClosedHandCursor);
    return;
  }

  if (transient_read_callback_ && event->button() == Qt::LeftButton) {
    const auto point = document_position(event->pos());
    if (document_contains(point)) {
      transient_read_dragging_ = true;
      grabMouse();
      auto callback = transient_read_callback_;
      callback(CanvasReadGesture{point, event->globalPosition().toPoint(), event->modifiers(),
                                 CanvasReadPhase::Press});
    }
    event->accept();
    return;
  }

  if (edit_locked_ && tool_ != CanvasTool::Zoom) {
    show_edit_locked_message();
    event->accept();
    return;
  }

  if (event->button() == Qt::LeftButton && widget_position_in_ruler(event->pos())) {
    begin_new_guide_drag(event->pos());
    event->accept();
    return;
  }

  if (warping_layer_ && event->button() == Qt::LeftButton) {
    // Unlike free transform, a click off the cage keeps the warp session alive
    // (Photoshop behavior); only Enter/Esc or the options-bar buttons end it.
    const auto handle = warp_handle_at(event->pos());
    if (handle >= 0) {
      dragging_warp_handle_ = true;
      warp_drag_index_ = handle;
      update();
    }
    event->accept();
    return;
  }

  if (transforming_layer_ && event->button() == Qt::LeftButton) {
    const auto handle = transform_handle_at(event->pos());
    if (handle != TransformHandle::None) {
      if (!prepare_free_transform_source()) {
        cancel_free_transform();
        event->accept();
        return;
      }
      dragging_transform_ = true;
      transform_drag_handle_ = handle;
      transform_drag_start_point_ = document_position_f(event->position());
      transform_drag_start_rect_ = transform_current_rect_;
      transform_start_angle_ = transform_angle_;
      transform_drag_start_scale_x_sign_ = transform_scale_x_sign_;
      transform_drag_start_scale_y_sign_ = transform_scale_y_sign_;
      event->accept();
      return;
    }
    // Like the warp cage, a click off the box keeps the session alive (Photoshop
    // behavior); only Enter/Esc, the options-bar buttons, or a tool/layer switch
    // end it. Discarding a half-built transform on a stray click was too harsh.
    event->accept();
    return;
  }

  if (tool_ == CanvasTool::Pen && event->button() == Qt::LeftButton) {
    if (handle_pen_press(event, document_position_f(event->position()))) {
      event->accept();
      return;
    }
  }

  if ((tool_ == CanvasTool::PathSelect || tool_ == CanvasTool::DirectSelect) &&
      event->button() == Qt::LeftButton) {
    if (handle_path_edit_press(event, document_position_f(event->position()))) {
      event->accept();
      return;
    }
  }

  const auto document_point = document_position(event->pos());
  const auto document_point_f = document_position_f(event->position());
  const auto effective_tool = effective_tool_for_input();
  const auto quick_mask_tool_is_unavailable = [](CanvasTool tool) {
    switch (tool) {
      case CanvasTool::Move:
      case CanvasTool::Marquee:
      case CanvasTool::EllipticalMarquee:
      case CanvasTool::Lasso:
      case CanvasTool::MagneticLasso:
      case CanvasTool::MagicWand:
      case CanvasTool::QuickSelect:
      case CanvasTool::Clone:
      case CanvasTool::PatternStamp:
      case CanvasTool::Healing:
      case CanvasTool::Smudge:
      case CanvasTool::MixerBrush:
      case CanvasTool::Dodge:
      case CanvasTool::Burn:
      case CanvasTool::Sponge:
      case CanvasTool::BlurBrush:
      case CanvasTool::SharpenBrush:
      case CanvasTool::Text:
      case CanvasTool::Pen:
      case CanvasTool::PathSelect:
      case CanvasTool::DirectSelect:
        return true;
      default:
        return false;
    }
  };
  if (quick_mask_active_ && event->button() == Qt::LeftButton &&
      quick_mask_tool_is_unavailable(effective_tool)) {
    report_status_error(tr("This tool is unavailable in Quick Mask mode"));
    event->accept();
    return;
  }
  if (layer_edit_target_ == LayerEditTarget::SmartFilterMask &&
      event->button() == Qt::LeftButton &&
      quick_mask_tool_is_unavailable(effective_tool)) {
    report_status_error(tr("This tool is unavailable while editing a Smart Filter mask"));
    event->accept();
    return;
  }
  if (event->button() == Qt::LeftButton) {
    const auto guide_index = guide_at_widget_position(event->pos());
    const auto guide_drag_allowed = tool_ == CanvasTool::Move || event->modifiers().testFlag(Qt::ControlModifier);
    if (guide_index >= 0 && guide_drag_allowed) {
      begin_guide_drag(guide_index, event->pos());
      event->accept();
      return;
    }
    clear_guide_selection();
  }
  const bool color_pick_press =
      event->button() == Qt::LeftButton &&
      (tool_ == CanvasTool::Eyedropper ||
       ((event->modifiers() & Qt::AltModifier) != 0 && tool_uses_alt_left_for_color_pick(tool_)));
  if (color_pick_press) {
    begin_color_pick(event->pos(), event->globalPosition().toPoint());
    event->accept();
    return;
  }
  const bool channel_view_active = layer_edit_target_ == LayerEditTarget::DocumentChannel ||
                                   layer_edit_target_ == LayerEditTarget::ComponentRed ||
                                   layer_edit_target_ == LayerEditTarget::ComponentGreen ||
                                   layer_edit_target_ == LayerEditTarget::ComponentBlue;
  if (event->button() == Qt::LeftButton && channel_view_active &&
      (effective_tool == CanvasTool::Move || effective_tool == CanvasTool::Clone ||
       effective_tool == CanvasTool::Healing || effective_tool == CanvasTool::PatternStamp ||
       effective_tool == CanvasTool::Smudge || effective_tool == CanvasTool::MixerBrush ||
       is_local_adjustment_tool(effective_tool) ||
       effective_tool == CanvasTool::Text)) {
    report_status_error(tr("This tool is unavailable while viewing a document channel"));
    event->accept();
    return;
  }

  if (!document_contains(document_point)) {
    // Marquee/lasso and brush strokes may begin in the grey area, and the zoom
    // tool zooms toward the nearest frame point when clicked outside the
    // canvas. Other tools discard an out-of-bounds press.
    const bool allows_off_canvas_press = tool_ == CanvasTool::Marquee ||
                                         tool_ == CanvasTool::EllipticalMarquee ||
                                         tool_ == CanvasTool::Lasso ||
                                         tool_ == CanvasTool::MagneticLasso ||
                                         tool_ == CanvasTool::QuickSelect ||
                                         tool_ == CanvasTool::Zoom ||
                                         (event->button() == Qt::LeftButton &&
                                          tool_supports_off_canvas_brush_strokes(effective_tool));
    if (!allows_off_canvas_press) {
      // The Move tool's transform resize and rotate handles can sit outside the
      // document bounds (for example after scaling a layer larger than the
      // canvas). Let a press that lands on one of those handles fall through to
      // the transform-handle hit-test below instead of discarding it here;
      // otherwise the handles can be hovered but never grabbed once they extend
      // past the canvas edge. The interior Move hit still falls back to the
      // normal out-of-bounds behaviour.
      const auto off_canvas_transform_rect = move_transform_controls_rect();
      const auto off_canvas_handle =
          off_canvas_transform_rect.has_value()
              ? transform_handle_at(event->pos(), *off_canvas_transform_rect, 0.0)
              : TransformHandle::None;
      if (off_canvas_handle == TransformHandle::None || off_canvas_handle == TransformHandle::Move) {
        set_move_transform_controls_layer(std::nullopt);
        return;
      }
    }
  }

  // Photoshop-style stroke connect: Shift+click joins the new stroke to the
  // previous stroke's end point with a straight brush segment.
  std::optional<QPointF> connect_from;
  if (event->button() == Qt::LeftButton && (event->modifiers() & Qt::ShiftModifier) != 0 &&
      last_stroke_end_document_.has_value() && tool_supports_shift_click_stroke_connect(effective_tool)) {
    connect_from = last_stroke_end_document_;
  }

  if (effective_tool == CanvasTool::Clone || effective_tool == CanvasTool::Healing) {
    const auto healing = effective_tool == CanvasTool::Healing;
    if (editing_grayscale_target()) {
      report_status_error(healing ? tr("Healing is unavailable while editing a grayscale channel")
                                  : tr("Clone is unavailable while editing a grayscale channel"));
      return;
    }
    if ((event->modifiers() & Qt::AltModifier) != 0) {
      set_clone_source(document_point);
      return;
    }
    if (!clone_source_set_) {
      report_status_error(healing ? tr("Alt-click to set a healing source")
                                  : tr("Alt-click to set a clone source"));
      return;
    }
    if (begin_edit(healing ? tr("Healing brush") : tr("Clone stamp"))) {
      clone_source_cache_ = render_document_image_with_processing();
      if (!clone_aligned_ || !clone_aligned_offset_set_) {
        clone_source_offset_ = clone_source_point_ - document_point;
        clone_aligned_offset_set_ = clone_aligned_;
      }
      clear_brush_stroke_tracking();
      begin_axis_constrained_stroke(QPointF(document_point));
      painting_ = true;
      last_document_position_ = document_point;
      last_document_position_f_ = QPointF(document_point);
      const auto dirty = connect_from.has_value() ? clone_brush_segment(connect_from->toPoint(), document_point)
                                                  : clone_brush_at(document_point);
      if (!dirty.isEmpty()) {
        active_edit_target_changed_impl(QRegion(dirty), DocumentChangeReason::BrushStrokePreview);
      }
    }
    return;
  }

  if (is_local_adjustment_tool(effective_tool)) {
    if (editing_grayscale_target()) {
      report_status_error(tr("Local adjustment brushes are unavailable while editing a grayscale channel"));
      return;
    }
    QString label;
    switch (effective_tool) {
      case CanvasTool::Dodge:
        label = tr("Dodge");
        break;
      case CanvasTool::Burn:
        label = tr("Burn");
        break;
      case CanvasTool::Sponge:
        label = tr("Sponge");
        break;
      case CanvasTool::BlurBrush:
        label = tr("Blur brush");
        break;
      case CanvasTool::SharpenBrush:
        label = tr("Sharpen brush");
        break;
      default:
        break;
    }
    if (begin_edit(label)) {
      clear_brush_stroke_tracking();
      if (auto* layer = active_pixel_layer(); layer != nullptr && document_->active_layer_id().has_value()) {
        ensure_brush_stroke_layer_snapshot(*document_->active_layer_id(), std::as_const(*layer));
      }
      begin_axis_constrained_stroke(QPointF(document_point));
      painting_ = true;
      last_document_position_ = document_point;
      last_document_position_f_ = QPointF(document_point);
      const auto dirty = connect_from.has_value()
                             ? local_adjustment_brush_segment(connect_from->toPoint(), document_point)
                             : local_adjustment_brush_segment(document_point, document_point);
      if (!dirty.isEmpty()) {
        active_edit_target_changed_impl(QRegion(dirty), DocumentChangeReason::BrushStrokePreview);
      }
    }
    return;
  }

  if (tool_ == CanvasTool::Text) {
    if (auto* layer = topmost_text_layer_at(document_point); layer != nullptr) {
      if (layer_effectively_locks_image_pixels(*layer)) {
        show_layer_pixels_locked_message();
        return;
      }
      activate_layer(*layer);
      if (text_requested_callback_) {
        text_requested_callback_(document_point, QRect());
      }
      event->accept();
      update();
      return;
    }
    dragging_text_rect_ = true;
    text_rect_start_ = snapped_document_point(document_point);
    text_rect_current_ = text_rect_start_;
    update();
    return;
  }

  if (tool_ == CanvasTool::Move) {
    const auto passive_transform_rect = move_transform_controls_rect();
    auto passive_handle = TransformHandle::None;
    if (passive_transform_rect.has_value()) {
      passive_handle = transform_handle_at(event->pos(), *passive_transform_rect, 0.0);
      if (passive_handle != TransformHandle::None && passive_handle != TransformHandle::Move) {
        if (begin_free_transform() && prepare_free_transform_source()) {
          dragging_transform_ = true;
          transform_drag_handle_ = passive_handle;
          transform_drag_start_point_ = document_position_f(event->position());
          transform_drag_start_rect_ = transform_current_rect_;
          transform_start_angle_ = transform_angle_;
          transform_drag_start_scale_x_sign_ = transform_scale_x_sign_;
          transform_drag_start_scale_y_sign_ = transform_scale_y_sign_;
        } else {
          cancel_free_transform();
        }
        event->accept();
        return;
      }
    }
    auto* top_clicked_layer = topmost_move_layer_at(document_point, false);
    auto* clicked_layer = topmost_move_layer_at(document_point, true);
    Layer* hit_layer = nullptr;
    Layer* transform_controls_layer = nullptr;
    std::vector<LayerId> layer_ids;
    const auto selected_move_layer_ids = movable_layer_ids();
    if (auto_select_layer_) {
      hit_layer = clicked_layer;
      const auto hit_selected_layer =
          hit_layer != nullptr && !selected_layer_ids_.empty() &&
          std::find(selected_move_layer_ids.begin(), selected_move_layer_ids.end(), hit_layer->id()) !=
              selected_move_layer_ids.end();
      if (hit_selected_layer) {
        layer_ids = selected_move_layer_ids;
        if (selected_layer_ids_.size() < 2U && selected_move_layer_ids.size() == 1U) {
          transform_controls_layer = hit_layer;
        }
      } else if (hit_layer != nullptr) {
        activate_layer(*hit_layer);
        layer_ids.push_back(hit_layer->id());
        transform_controls_layer = hit_layer;
      }
      if (hit_layer == nullptr && selected_layer_ids_.size() < 2U && passive_handle == TransformHandle::Move) {
        layer_ids = selected_move_layer_ids;
      }
    } else if (selected_layer_ids_.size() < 2U) {
      auto target_id = document_->active_layer_id();
      if (!selected_layer_ids_.empty()) {
        target_id = selected_layer_ids_.front();
      }
      if (target_id.has_value()) {
        auto* layer = document_->find_layer(*target_id);
        if (layer != nullptr && move_layer_contains_document_point(*layer, document_point)) {
          transform_controls_layer = layer;
        }
      }
    }
    if (show_transform_controls_ && (auto_select_layer_ || selected_layer_ids_.size() < 2U)) {
      if (transform_controls_layer != nullptr) {
        set_move_transform_controls_layer(transform_controls_layer->id());
      } else if (transform_controls_layer == nullptr && passive_transform_rect.has_value() &&
                 passive_handle == TransformHandle::None) {
        set_move_transform_controls_layer(std::nullopt);
        event->accept();
        return;
      } else if (passive_handle == TransformHandle::Move) {
        // Keep existing controls while the normal Move path handles the drag.
      } else {
        set_move_transform_controls_layer(std::nullopt);
      }
    }
    if (transform_controls_layer == nullptr && passive_transform_rect.has_value() &&
        passive_handle == TransformHandle::None) {
      set_move_transform_controls_layer(std::nullopt);
      event->accept();
      return;
    }
    if (!auto_select_layer_) {
      layer_ids = selected_move_layer_ids;
    }
    if (layer_ids.empty()) {
      if (top_clicked_layer != nullptr && layer_effectively_locks_position(*top_clicked_layer)) {
        report_status_error(tr("Layer position is locked."));
      } else {
        report_status_error(tr("Click an editable layer to move"));
      }
      return;
    }
    move_drag_pending_ = true;
    moving_layer_ = false;
    move_start_ = document_point;
    begin_axis_constrained_stroke(QPointF(move_start_));
    move_press_widget_position_ = event->pos();
    move_preview_delta_ = QPoint();
    moving_layers_.clear();
    moving_layers_use_outline_preview_ = false;
    clear_move_base_cache();
    moving_layers_.reserve(layer_ids.size());
    for (const auto id : layer_ids) {
      auto* layer = document_->find_layer(id);
      if (layer != nullptr) {
        moving_layers_.push_back(
            MovingLayer{id, layer->bounds(), move_layer_outline_bounds(*layer), move_layer_has_expensive_style(*layer)});
      }
    }
    move_preview_patches_.clear();
    move_preview_patches_delta_.reset();
    return;
  }

  if (can_move_selection_at(document_point, event->modifiers())) {
    // Grab inside an existing selection to drag the outline (pixels stay put).
    // A press that does not turn into a drag falls through to click-to-deselect
    // in the release handler.
    moving_selection_ = true;
    selection_edges_visible_ = true;
    selection_press_widget_position_ = event->pos();
    selection_move_origin_document_ = document_point;
    selection_before_edit_ = selection_;
    selection_display_region_before_edit_ = selection_display_region_;
    selection_mask_before_edit_bounds_ = selection_mask_bounds_;
    selection_mask_before_edit_alpha_ = selection_mask_alpha_;
    selection_operation_ = SelectionMode::Replace;
    setCursor(Qt::SizeAllCursor);
    update();
    return;
  }

  if (tool_ == CanvasTool::Marquee || tool_ == CanvasTool::EllipticalMarquee) {
    const auto snapped_point = snapped_document_point(document_point);
    selecting_ = true;
    spacebar_repositioning_drag_rect_ = false;
    selection_edges_visible_ = true;
    selection_press_widget_position_ = event->pos();
    // Shift adds to an existing selection, but constrains to a square when there
    // is nothing to add to.
    selection_shift_at_press_ = (event->modifiers() & Qt::ShiftModifier) != 0 && !selection_.isEmpty();
    selection_shift_released_since_press_ = false;
    selection_square_constrained_ = false;
    // With no existing selection Alt does not subtract; instead it mirrors the
    // marquee about the press point (Photoshop's draw-from-center).
    marquee_from_center_ = (event->modifiers() & Qt::AltModifier) != 0 && selection_.isEmpty();
    selection_start_ = snapped_point;
    selection_current_ = snapped_point;
    selection_before_edit_ = selection_;
    selection_display_region_before_edit_ = selection_display_region_;
    selection_mask_before_edit_bounds_ = selection_mask_bounds_;
    selection_mask_before_edit_alpha_ = selection_mask_alpha_;
    selection_operation_ = selection_operation(event->modifiers());
    // Replace shows the rectangle live as you drag; Add/Subtract/Intersect keep
    // the existing selection visible and only draw the candidate outline,
    // committing the combine on release (like the Lasso) so you can see what
    // you are about to add/subtract/intersect.
    if (selection_operation_ == SelectionMode::Replace) {
      combine_selection_from_region(marquee_selection_region(selection_start_, selection_current_));
    }
    emit_info_for_widget_position(event->pos());
    update();
    return;
  }

  if (tool_ == CanvasTool::Lasso) {
    lassoing_ = true;
    selection_edges_visible_ = true;
    selection_press_widget_position_ = event->pos();
    lasso_points_.clear();
    // Clamp the first point to the canvas (as the move/release handlers do) so a
    // drag begun in the grey area starts at the edge rather than drawing a
    // preview line in from outside the canvas.
    lasso_points_ << (document_ != nullptr ? clamped_document_point(*document_, document_point) : document_point);
    selection_before_edit_ = selection_;
    selection_display_region_before_edit_ = selection_display_region_;
    selection_mask_before_edit_bounds_ = selection_mask_bounds_;
    selection_mask_before_edit_alpha_ = selection_mask_alpha_;
    selection_operation_ = selection_operation(event->modifiers());
    restore_selection_before_edit();
    update();
    return;
  }

  if (tool_ == CanvasTool::MagneticLasso) {
    if (document_ == nullptr || event->button() != Qt::LeftButton) {
      return;
    }
    const auto point = clamped_document_point(*document_, document_point);
    if (!magnetic_lassoing_) {
      start_magnetic_lasso(point, event->modifiers());
    } else {
      // Close when the click lands back on the start anchor; otherwise the click
      // freezes the live segment as a manual anchor.
      const auto start_hit =
          (event->pos() - widget_position(magnetic_anchors_.first())).manhattanLength() <= 9;
      if (start_hit && magnetic_committed_path_.size() + magnetic_live_path_.size() >= 3) {
        finish_magnetic_lasso();
      } else {
        extract_magnetic_live_path(point, /*snap_target=*/false);
        add_magnetic_anchor();
      }
    }
    emit_info_for_widget_position(event->pos());
    update();
    return;
  }

  if (tool_ == CanvasTool::QuickSelect) {
    if (document_ == nullptr) {
      return;
    }
    selection_edges_visible_ = true;
    selection_before_edit_ = selection_;
    selection_display_region_before_edit_ = selection_display_region_;
    selection_mask_before_edit_bounds_ = selection_mask_bounds_;
    selection_mask_before_edit_alpha_ = selection_mask_alpha_;
    auto operation = selection_operation(event->modifiers());
    if (operation == SelectionMode::Intersect) {
      // Quick Select has no Intersect mode (Photoshop parity); Shift+Alt acts as Add.
      operation = SelectionMode::Add;
    }
    selection_operation_ = operation;
    begin_quick_select_stroke(document_point);
    emit_info_for_widget_position(event->pos());
    update();
    return;
  }

  if (tool_ == CanvasTool::MagicWand) {
    selection_edges_visible_ = true;
    selection_before_edit_ = selection_;
    selection_display_region_before_edit_ = selection_display_region_;
    selection_mask_before_edit_bounds_ = selection_mask_bounds_;
    selection_mask_before_edit_alpha_ = selection_mask_alpha_;
    selection_operation_ = selection_operation(event->modifiers());
    begin_processing_operation();
    magic_wand_select(document_point);
    end_processing_operation();
    record_selection_history(tr("Magic Wand"), selection_snapshot_before_edit());
    selection_before_edit_ = QRegion();
    selection_display_region_before_edit_ = QRegion();
    selection_mask_before_edit_bounds_ = {};
    selection_mask_before_edit_alpha_ = QImage();
    return;
  }

  if (tool_ == CanvasTool::Zoom) {
    zooming_ = true;
    // Clamp the anchor to the frame so a marquee that begins in the grey margin
    // stays clamped to the canvas edges instead of spanning into the margin.
    zoom_start_ = document_ != nullptr ? clamped_document_point(*document_, document_point) : document_point;
    zoom_current_ = zoom_start_;
    emit_info_for_widget_position(event->pos());
    update();
    return;
  }

  if (tool_ == CanvasTool::Fill) {
    if (begin_edit(tr("Fill"))) {
      const auto processing = !editing_smart_filter_mask();
      if (processing) {
        begin_processing_operation();
      }
      const auto dirty = flood_fill(document_point);
      if (processing) {
        tick_processing_operation();
      }
      active_edit_target_changed_impl(QRegion(dirty));
      if (processing) {
        end_processing_operation();
      }
    }
    return;
  }

  if (effective_tool == CanvasTool::Brush || effective_tool == CanvasTool::MixerBrush ||
      effective_tool == CanvasTool::PatternStamp ||
      effective_tool == CanvasTool::Smudge ||
      effective_tool == CanvasTool::Eraser) {
    if ((effective_tool == CanvasTool::Smudge || effective_tool == CanvasTool::MixerBrush) &&
        editing_grayscale_target()) {
      report_status_error(effective_tool == CanvasTool::MixerBrush
                              ? tr("Mixer Brush is unavailable while editing a grayscale channel")
                              : tr("Smudge is unavailable while editing a grayscale channel"));
      return;
    }
    auto label = tr("Erase");
    if (effective_tool == CanvasTool::Brush) {
      label = tr("Brush stroke");
    } else if (effective_tool == CanvasTool::MixerBrush) {
      label = tr("Mixer Brush stroke");
    } else if (effective_tool == CanvasTool::PatternStamp) {
      if (!begin_pattern_stamp_stroke(document_point)) {
        report_status_error(tr("Choose a pattern before painting"));
        return;
      }
      label = tr("Pattern stamp");
    } else if (effective_tool == CanvasTool::Smudge) {
      label = tr("Smudge");
    }
    if (begin_edit(label)) {
      clear_brush_stroke_tracking();
      smudge_state_ = {};
      mixer_brush_state_ = {};
      if (effective_tool == CanvasTool::MixerBrush) {
        begin_mixer_brush_stroke(document_point);
      }
      const auto stroke_brush_size = effective_brush_input().size;
      begin_axis_constrained_stroke(stroke_brush_size == 1 ? QPointF(document_point) : document_point_f);
      painting_ = true;
      last_document_position_ = document_point;
      last_document_position_f_ = document_point_f;
      if (effective_tool != CanvasTool::Smudge) {
        if (stroke_brush_size == 1) {
          reset_brush_smoothing();
        } else {
          begin_brush_smoothing(document_point_f);
        }
        QRect dirty;
        if (connect_from.has_value()) {
          const auto erase = effective_tool == CanvasTool::Eraser;
          dirty = stroke_brush_size == 1
                      ? draw_brush_segment(connect_from->toPoint(), document_point, erase, true)
                      : draw_brush_segment(*connect_from, document_point_f, erase, true);
        } else {
          dirty = draw_brush_at(document_point, effective_tool == CanvasTool::Eraser);
        }
        if (!dirty.isEmpty()) {
          active_edit_target_changed_impl(QRegion(dirty), DocumentChangeReason::BrushStrokePreview);
        }
        if (effective_tool == CanvasTool::Brush && brush_build_up_) {
          airbrush_timer_.start(kAirbrushTimerIntervalMs, this);
        }
      } else {
        reset_brush_smoothing();
        if (connect_from.has_value()) {
          const auto dirty = smudge_brush_segment(connect_from->toPoint(), document_point);
          if (!dirty.isEmpty()) {
            active_edit_target_changed_impl(QRegion(dirty), DocumentChangeReason::BrushStrokePreview);
          }
        }
      }
    }
    return;
  }

  if (tool_ == CanvasTool::Gradient || tool_ == CanvasTool::Line || tool_ == CanvasTool::Rectangle ||
      tool_ == CanvasTool::Ellipse) {
    // A Shape/Path-mode drag never edits the active layer's pixels (the
    // release routes to MainWindow, which pushes its own undo entry), so it
    // skips begin_edit; that also lets it start while a shape/text/smart
    // layer is active, where the raster guard would refuse.
    const bool vector_shape_drag = tool_ != CanvasTool::Gradient &&
                                   vector_tool_mode_ != VectorToolMode::Pixels &&
                                   vector_shape_drawn_callback_ && !quick_mask_active_ &&
                                   layer_edit_target_ == LayerEditTarget::Content;
    if (vector_shape_drag ||
        begin_edit(tool_ == CanvasTool::Gradient ? tr("Gradient") : tr("Shape"))) {
      const auto snapped_point = snapped_document_point(document_point);
      clear_brush_stroke_tracking();
      drawing_shape_ = true;
      spacebar_repositioning_drag_rect_ = false;
      shape_start_ = snapped_point;
      shape_current_ = snapped_point;
      shape_square_constrained_ = (event->modifiers() & Qt::ShiftModifier) != 0 &&
                                  (tool_ == CanvasTool::Rectangle || tool_ == CanvasTool::Ellipse);
      shape_from_center_ = (event->modifiers() & Qt::AltModifier) != 0 &&
                           (tool_ == CanvasTool::Rectangle || tool_ == CanvasTool::Ellipse);
      update();
    }
  }
}

void CanvasWidget::mouseMoveEvent(QMouseEvent* event) {
  if (!handling_tablet_event_) {
    active_pen_input_sample_.reset();
  }
  emit_info_for_widget_position(event->pos());
  track_brush_hover_position(event->pos());
  if (brush_adjust_dragging_) {
    if ((event->buttons() & Qt::RightButton) != 0) {
      update_brush_adjust_drag(event->pos());
    } else {
      // The right-button release was lost (see the tablet path): commit
      // instead of tracking a button nobody is holding.
      end_brush_adjust_drag(true);
    }
    last_mouse_position_ = event->pos();
    event->accept();
    return;
  }
  if (mouse_pen_action_button_ != Qt::NoButton) {
    // Driver-injected pen-button clicks are invisible to the tablet stream,
    // so moves synthesized from hover tablet events report no held buttons;
    // only a real mouse move saying the button is gone ends the gesture.
    if (!handling_tablet_event_ && (event->buttons() & mouse_pen_action_button_) == 0) {
      mouse_pen_action_button_ = Qt::NoButton;
      if (pen_zoom_dragging_) {
        end_zoom_drag();
      }
    } else {
      if (pen_zoom_dragging_) {
        update_zoom_drag(event->position());
      }
      last_mouse_position_ = event->pos();
      event->accept();
      return;
    }
  }
  if (color_picking_) {
    if ((event->buttons() & Qt::LeftButton) != 0) {
      update_color_pick(event->pos(), event->globalPosition().toPoint());
    } else {
      end_color_pick();
    }
    last_mouse_position_ = event->pos();
    event->accept();
    return;
  }
  if (panning_) {
    clear_move_hover_outline();
    const auto delta = event->pos() - last_mouse_position_;
    const auto old_pan = pan_;
    pan_ += QPointF(delta);
    constrain_pan();
    last_mouse_position_ = event->pos();
    if (pan_ != old_pan) {
      update();
      notify_view_changed();
    }
    return;
  }

  if (transient_read_dragging_) {
    const auto phase = (event->buttons() & Qt::LeftButton) != 0 ? CanvasReadPhase::Drag
                                                               : CanvasReadPhase::Cancel;
    auto callback = transient_read_callback_;
    if (phase == CanvasReadPhase::Cancel) {
      transient_read_dragging_ = false;
      if (QWidget::mouseGrabber() == this) {
        releaseMouse();
      }
    }
    if (callback) {
      callback(CanvasReadGesture{document_position(event->pos()), event->globalPosition().toPoint(),
                                 event->modifiers(), phase});
    }
    last_mouse_position_ = event->pos();
    event->accept();
    return;
  }

  if (edit_locked_ && !zooming_) {
    clear_move_hover_outline();
    last_mouse_position_ = event->pos();
    event->accept();
    return;
  }

  if (dragging_guide_) {
    clear_move_hover_outline();
    update_guide_drag(event->pos(), event->modifiers());
    last_mouse_position_ = event->pos();
    return;
  }

  if (tool_ == CanvasTool::Pen) {
    handle_pen_move(event, document_position_f(event->position()));
    last_mouse_position_ = event->pos();
    event->accept();
    return;
  }

  if (tool_ == CanvasTool::PathSelect || tool_ == CanvasTool::DirectSelect) {
    handle_path_edit_move(event, document_position_f(event->position()));
    last_mouse_position_ = event->pos();
    event->accept();
    return;
  }

  if (dragging_warp_handle_) {
    clear_move_hover_outline();
    set_warp_handle_document_position(warp_drag_index_, document_position_f(event->position()));
    last_mouse_position_ = event->pos();
    return;
  }

  if (warping_layer_) {
    clear_move_hover_outline();
    setCursor(warp_handle_at(event->pos()) >= 0 ? Qt::SizeAllCursor : Qt::ArrowCursor);
    last_mouse_position_ = event->pos();
    return;
  }

  if (dragging_transform_) {
    clear_move_hover_outline();
    update_free_transform_preview(document_position_f(event->position()), event->modifiers());
    last_mouse_position_ = event->pos();
    return;
  }

  if (transforming_layer_) {
    clear_move_hover_outline();
    set_transform_cursor_for_handle(transform_handle_at(event->pos()));
    last_mouse_position_ = event->pos();
    return;
  }

  const auto document_point = document_position(event->pos());
  const auto document_point_f = document_position_f(event->position());
  const auto effective_tool = effective_tool_for_input();
  if (dragging_text_rect_) {
    clear_move_hover_outline();
    text_rect_current_ = snapped_document_point(document_point);
    emit_info_for_widget_position(event->pos());
    update();
  } else if (painting_) {
    clear_move_hover_outline();
    QRect dirty;
    if (effective_tool == CanvasTool::Clone || effective_tool == CanvasTool::Healing) {
      const auto constrained_point = axis_constrained_stroke_point(document_point, event->modifiers());
      dirty = clone_brush_segment(last_document_position_, constrained_point);
      last_document_position_ = constrained_point;
      last_document_position_f_ = QPointF(constrained_point);
    } else if (effective_tool == CanvasTool::Smudge) {
      dirty = smudge_brush_segment(last_document_position_, document_point);
      last_document_position_ = document_point;
      last_document_position_f_ = document_point_f;
    } else if (is_local_adjustment_tool(effective_tool)) {
      const auto constrained_point = axis_constrained_stroke_point(document_point, event->modifiers());
      dirty = local_adjustment_brush_segment(last_document_position_, constrained_point);
      last_document_position_ = constrained_point;
      last_document_position_f_ = QPointF(constrained_point);
    } else if (effective_brush_input().size == 1) {
      const auto constrained_point = axis_constrained_stroke_point(document_point, event->modifiers());
      dirty = draw_brush_segment(last_document_position_, constrained_point, effective_tool == CanvasTool::Eraser);
      last_document_position_ = constrained_point;
      last_document_position_f_ = QPointF(constrained_point);
    } else {
      const auto constrained_point = axis_constrained_stroke_point(document_point_f, event->modifiers());
      dirty = advance_smoothed_brush_stroke(constrained_point, effective_tool == CanvasTool::Eraser);
      last_document_position_ = QPoint(static_cast<int>(std::lround(constrained_point.x())),
                                       static_cast<int>(std::lround(constrained_point.y())));
      last_document_position_f_ = constrained_point;
    }
    if (!dirty.isEmpty()) {
      active_edit_target_changed_impl(QRegion(dirty), DocumentChangeReason::BrushStrokePreview);
    }
  } else if (drawing_shape_) {
    clear_move_hover_outline();
    if (spacebar_repositioning_drag_rect_) {
      const auto delta = document_point - spacebar_reposition_last_document_position_;
      shape_start_ += delta;
      shape_current_ += delta;
      spacebar_reposition_last_document_position_ = document_point;
    } else {
      shape_current_ = snapped_document_point(document_point);
    }
    shape_square_constrained_ = (event->modifiers() & Qt::ShiftModifier) != 0 &&
                                (tool_ == CanvasTool::Rectangle || tool_ == CanvasTool::Ellipse);
    shape_from_center_ = (event->modifiers() & Qt::AltModifier) != 0 &&
                         (tool_ == CanvasTool::Rectangle || tool_ == CanvasTool::Ellipse);
    update();
  } else if (move_drag_pending_ || moving_layer_) {
    std::optional<QRectF> old_transform_controls_rect;
    if (move_drag_pending_) {
      const auto widget_delta = event->pos() - move_press_widget_position_;
      if (widget_delta.manhattanLength() < QApplication::startDragDistance()) {
        last_mouse_position_ = event->pos();
        return;
      }
      old_transform_controls_rect = move_transform_controls_rect();
      move_drag_pending_ = false;
      moving_layer_ = true;
      update_move_transform_controls_dirty(old_transform_controls_rect);
    }
    clear_move_hover_outline();
    const auto old_delta = move_preview_delta_;
    const auto constrained_delta = axis_constrained_move_delta(document_point - move_start_, event->modifiers());
    move_preview_delta_ = axis_constrained_move_delta(snapped_move_delta(constrained_delta), event->modifiers());
    if (move_preview_delta_ == old_delta || document_ == nullptr || moving_layers_.empty()) {
      last_mouse_position_ = event->pos();
      return;
    }
    if (!moving_layers_use_outline_preview_ &&
        moving_layers_should_use_outline_preview(old_delta, move_preview_delta_)) {
      moving_layers_use_outline_preview_ = true;
      ++render_cache_diagnostics_.move_outline_previews;
      move_preview_patches_.clear();
      move_preview_patches_delta_.reset();
      clear_move_base_cache();
    }
    if (moving_layers_use_outline_preview_) {
      move_preview_patches_.clear();
      move_preview_patches_delta_.reset();
      const auto dirty = moving_layers_outline_dirty_rect(old_delta, move_preview_delta_);
      if (!dirty.isEmpty()) {
        update(widget_rect_for_document_rect(dirty));
      }
      last_mouse_position_ = event->pos();
      return;
    }
    ensure_render_cache();
    ensure_move_base_cache();
    const QRegion canvas_region(QRect(0, 0, document_->width(), document_->height()));
    auto previous_preview_region = QRegion();
    for (const auto& patch : move_preview_patches_) {
      if (!patch.document_rect.isEmpty()) {
        previous_preview_region += patch.document_rect;
      }
    }
    auto preview_region = moving_layers_dirty_region(QPoint(), move_preview_delta_).intersected(canvas_region);
    // At mip-rendered zoom levels the preview patches must cover whole mip
    // blocks so their downscale matches the surrounding display mips; see
    // draw_document_patch in paintEvent.
    if (const auto preview_mip_level = display_mip_level_for_zoom(zoom_);
        preview_mip_level > 0 && !preview_region.isEmpty()) {
      QRegion aligned_region;
      for (const auto& rect : preview_region) {
        aligned_region += rect_aligned_to_mip_grid(rect, preview_mip_level);
      }
      preview_region = aligned_region.intersected(canvas_region);
    }
    auto update_region = previous_preview_region.united(preview_region).intersected(canvas_region);
    const auto outline_dirty = moving_layers_outline_dirty_rect(old_delta, move_preview_delta_);
    if (!outline_dirty.isEmpty()) {
      update_region += outline_dirty;
      update_region = update_region.intersected(canvas_region);
    }
    if (!preview_region.isEmpty()) {
      move_preview_patches_ = qimage_patches_from_document_region_with_layer_bounds(
          *document_, preview_region, true, moving_layer_bounds(move_preview_delta_));
      for (auto& patch : move_preview_patches_) {
        patch.image = patch.image.convertToFormat(QImage::Format_RGBA8888);
      }
      move_preview_patches_delta_ = move_preview_delta_;
    } else {
      move_preview_patches_.clear();
      move_preview_patches_delta_.reset();
    }
    if (!update_region.isEmpty()) {
      QRegion widget_region;
      for (const auto& rect : update_region) {
        widget_region += widget_rect_for_document_rect(rect);
      }
      update(widget_region);
    }
  } else if (moving_selection_) {
    clear_move_hover_outline();
    apply_selection_move(document_point - selection_move_origin_document_);
    setCursor(Qt::SizeAllCursor);
    emit_info_for_widget_position(event->pos());
    update();
  } else if (selecting_) {
    clear_move_hover_outline();
    if (spacebar_repositioning_drag_rect_) {
      const auto raw_delta = document_point - spacebar_reposition_origin_document_position_;
      const auto delta = snapped_rect_delta(
          marquee_selection_rect(spacebar_reposition_start_selection_start_,
                                 spacebar_reposition_start_selection_current_),
          raw_delta);
      selection_start_ = spacebar_reposition_start_selection_start_ + delta;
      selection_current_ = spacebar_reposition_start_selection_current_ + delta;
    } else {
      update_selection_square_constraint(event->modifiers());
      selection_current_ = snapped_marquee_current_point(selection_start_, document_point);
    }
    // Replace updates the live selection; the combine modes defer to release and
    // only redraw the candidate outline (see draw_selection_overlay).
    if (selection_operation_ == SelectionMode::Replace) {
      combine_selection_from_region(marquee_selection_region(selection_start_, selection_current_));
    }
    emit_info_for_widget_position(event->pos());
    update();
  } else if (lassoing_ && document_ != nullptr) {
    clear_move_hover_outline();
    const auto point = clamped_document_point(*document_, document_point);
    if (lasso_points_.isEmpty() || (point - lasso_points_.last()).manhattanLength() >= 1) {
      lasso_points_ << point;
      update();
    }
  } else if (magnetic_lassoing_ && document_ != nullptr) {
    // The trace runs button-up (mouseTracking delivers hover moves): keep the live
    // segment snapped to the cursor and let long segments cool into anchors.
    clear_move_hover_outline();
    extract_magnetic_live_path(clamped_document_point(*document_, document_point));
    cool_magnetic_live_path();
    update();
  } else if (quick_selecting_) {
    clear_move_hover_outline();
    extend_quick_select_stroke(document_point);
    emit_info_for_widget_position(event->pos());
  } else if (zooming_ && document_ != nullptr) {
    clear_move_hover_outline();
    zoom_current_ = clamped_document_point(*document_, document_point);
    emit_info_for_widget_position(event->pos());
    update();
  } else {
    const auto guide_index = guide_at_widget_position(event->pos());
    const auto guide_drag_allowed = tool_ == CanvasTool::Move || event->modifiers().testFlag(Qt::ControlModifier);
    if (guide_index >= 0 && !guides_locked_ && guide_drag_allowed) {
      clear_move_hover_outline();
      const auto orientation = document_->guides()[static_cast<std::size_t>(guide_index)].orientation;
      setCursor(orientation == GuideOrientation::Vertical ? Qt::SplitHCursor : Qt::SplitVCursor);
    } else {
      if (tool_ == CanvasTool::Move) {
        if (const auto rect = move_transform_controls_rect(); rect.has_value()) {
          const auto handle = transform_handle_at(event->pos(), *rect, 0.0);
          if (handle != TransformHandle::None) {
            set_transform_cursor_for_handle(handle);
            if (handle == TransformHandle::Move && auto_select_layer_) {
              update_move_hover_outline(event->pos(), event->modifiers());
            } else {
              clear_move_hover_outline();
            }
            last_mouse_position_ = event->pos();
            return;
          }
        }
      }
      if (can_move_selection_at(document_point, event->modifiers())) {
        // Signal that grabbing here drags the selection outline.
        setCursor(Qt::SizeAllCursor);
      } else {
        update_tool_cursor();
      }
      update_move_hover_outline(event->pos(), event->modifiers());
    }
  }

  last_mouse_position_ = event->pos();
}

void CanvasWidget::refresh_tool_cursor() {
  // Re-apply the tool cursor unless an in-progress gesture owns it. Used when
  // the pointer enters the canvas or the canvas/window regains focus, since a
  // pen does not emit move events on its own and Windows may reset the cursor
  // to an arrow when the window is re-activated.
  if (!panning_ && !pen_zoom_dragging_ && !dragging_transform_ && !transforming_layer_ && !color_picking_) {
    update_tool_cursor();
  }
}

void CanvasWidget::enterEvent(QEnterEvent* event) {
  // Apply the tool cursor as soon as the pointer enters the canvas. With a
  // mouse the cursor is refreshed by the steady stream of move events, but a
  // pen only emits events once it moves, so without this the brush cursor can
  // lag behind the pen when it first comes into proximity over the canvas.
  refresh_tool_cursor();
  QWidget::enterEvent(event);
}

void CanvasWidget::focusInEvent(QFocusEvent* event) {
  refresh_tool_cursor();
  QWidget::focusInEvent(event);
}

void CanvasWidget::leaveEvent(QEvent* event) {
  clear_move_hover_outline();
  if (brush_hover_position_valid_) {
    const auto stale = brush_outline_uses_overlay() ? brush_hover_outline_rect() : QRect();
    brush_hover_position_valid_ = false;
    if (!stale.isNull()) {
      update(stale.adjusted(-2, -2, 2, 2));
    }
  }
  QWidget::leaveEvent(event);
}

void CanvasWidget::mouseReleaseEvent(QMouseEvent* event) {
  if (!handling_tablet_event_) {
    active_pen_input_sample_.reset();
  }
  if (brush_adjust_dragging_) {
    if (event->button() == Qt::RightButton || (event->buttons() & Qt::RightButton) == 0) {
      end_brush_adjust_drag(true);
    }
    event->accept();
    return;
  }
  if (mouse_pen_action_button_ != Qt::NoButton) {
    if (event->button() == mouse_pen_action_button_) {
      mouse_pen_action_button_ = Qt::NoButton;
      if (pen_zoom_dragging_) {
        end_zoom_drag();
      }
      event->accept();
      return;
    }
    if (!handling_tablet_event_ && (event->buttons() & mouse_pen_action_button_) == 0) {
      // Lost release: clean up, then let this event take its normal path.
      mouse_pen_action_button_ = Qt::NoButton;
      if (pen_zoom_dragging_) {
        end_zoom_drag();
      }
    }
  }
  if (color_picking_) {
    if (event->button() == Qt::LeftButton || (event->buttons() & Qt::LeftButton) == 0) {
      update_color_pick(event->pos(), event->globalPosition().toPoint());
      end_color_pick();
    }
    event->accept();
    return;
  }
  if (panning_) {
    panning_ = false;
    update_tool_cursor();
    return;
  }

  if (transient_read_dragging_ &&
      (event->button() == Qt::LeftButton || (event->buttons() & Qt::LeftButton) == 0)) {
    transient_read_dragging_ = false;
    if (QWidget::mouseGrabber() == this) {
      releaseMouse();
    }
    auto callback = transient_read_callback_;
    if (callback) {
      callback(CanvasReadGesture{document_position(event->pos()), event->globalPosition().toPoint(),
                                 event->modifiers(), CanvasReadPhase::Release});
    }
    last_mouse_position_ = event->pos();
    event->accept();
    return;
  }

  if (edit_locked_ && !zooming_) {
    clear_move_hover_outline();
    event->accept();
    return;
  }

  if (tool_ == CanvasTool::Pen) {
    if (handle_pen_release(event)) {
      event->accept();
      return;
    }
  }

  if (tool_ == CanvasTool::PathSelect || tool_ == CanvasTool::DirectSelect) {
    if (handle_path_edit_release(event)) {
      event->accept();
      return;
    }
  }

  if (dragging_guide_) {
    finish_guide_drag(event->pos(), event->modifiers());
    return;
  }

  if (magnetic_lassoing_) {
    // The trace is driven by presses and hover moves; releases (including the
    // start click's) must not fall through to the deselect/selection logic.
    event->accept();
    return;
  }

  if (dragging_warp_handle_) {
    set_warp_handle_document_position(warp_drag_index_, document_position_f(event->position()));
    dragging_warp_handle_ = false;
    warp_drag_index_ = -1;
    update();
    return;
  }

  if (dragging_transform_) {
    update_free_transform_preview(document_position_f(event->position()), event->modifiers());
    dragging_transform_ = false;
    transform_drag_handle_ = TransformHandle::None;
    update_tool_cursor();
    update();
    notify_transform_controls_changed();
    return;
  }

  if (painting_) {
    QRect dirty;
    const auto document_point = document_position(event->pos());
    const auto document_point_f = document_position_f(event->position());
    const auto effective_tool = effective_tool_for_input();
    if (effective_tool == CanvasTool::Clone || effective_tool == CanvasTool::Healing) {
      const auto constrained_point = axis_constrained_stroke_point(document_point, event->modifiers());
      dirty = clone_brush_segment(last_document_position_, constrained_point);
      last_document_position_ = constrained_point;
      last_document_position_f_ = QPointF(constrained_point);
    } else if (is_local_adjustment_tool(effective_tool)) {
      const auto constrained_point = axis_constrained_stroke_point(document_point, event->modifiers());
      dirty = local_adjustment_brush_segment(last_document_position_, constrained_point);
      last_document_position_ = constrained_point;
      last_document_position_f_ = QPointF(constrained_point);
    } else if (effective_tool == CanvasTool::Brush || effective_tool == CanvasTool::MixerBrush ||
               effective_tool == CanvasTool::PatternStamp ||
               effective_tool == CanvasTool::Eraser) {
      if (effective_brush_input().size == 1) {
        const auto constrained_point = axis_constrained_stroke_point(document_point, event->modifiers());
        dirty = draw_brush_segment(last_document_position_, constrained_point, effective_tool == CanvasTool::Eraser);
        last_document_position_ = constrained_point;
        last_document_position_f_ = QPointF(constrained_point);
      } else {
        const auto constrained_point = axis_constrained_stroke_point(document_point_f, event->modifiers());
        dirty = finish_smoothed_brush_stroke(constrained_point, effective_tool == CanvasTool::Eraser);
        last_document_position_ = QPoint(static_cast<int>(std::lround(constrained_point.x())),
                                         static_cast<int>(std::lround(constrained_point.y())));
        last_document_position_f_ = constrained_point;
      }
    } else {
      last_document_position_ = document_point;
      last_document_position_f_ = document_point_f;
    }
    painting_ = false;
    last_stroke_end_document_ = last_document_position_f_;
    clone_source_cache_ = QImage();
    smudge_state_ = {};
    mixer_brush_state_ = {};
    reset_brush_smoothing();
    reset_axis_constrained_stroke();
    clear_brush_stroke_tracking();
    if (!dirty.isEmpty()) {
      active_edit_target_changed_impl(QRegion(dirty), DocumentChangeReason::BrushStrokeFinished);
    } else if (quick_mask_active_) {
      finish_quick_mask_edit();
    } else if (layer_edit_target_ == LayerEditTarget::SmartFilterMask) {
      // The press dab may already have changed the temporary mask even when
      // release adds no final segment. Complete that gesture exactly once.
      finish_smart_filter_mask_edit();
    } else {
      notify_document_changed(DocumentChangeReason::BrushStrokeFinished);
    }
    return;
  }

  if (dragging_text_rect_) {
    dragging_text_rect_ = false;
    text_rect_current_ = snapped_document_point(document_position(event->pos()));
    const auto rect = normalized_rect(text_rect_start_, text_rect_current_);
    QRect requested_box;
    if (rect.width() >= 16 && rect.height() >= 16) {
      requested_box = rect;
    }
    if (text_requested_callback_) {
      text_requested_callback_(requested_box.isValid() && !requested_box.isEmpty() ? requested_box.topLeft()
                                                                                   : text_rect_start_,
                               requested_box);
    }
    update();
    return;
  }

  if (move_drag_pending_) {
    move_drag_pending_ = false;
    moving_layers_.clear();
    move_preview_delta_ = QPoint();
    move_preview_patches_.clear();
    move_preview_patches_delta_.reset();
    moving_layers_use_outline_preview_ = false;
    clear_move_base_cache();
    reset_axis_constrained_stroke();
    update_move_hover_outline(event->pos(), event->modifiers());
    update();
    return;
  }

  if (moving_layer_) {
    const auto trace_move_release = render_trace_enabled();
    const auto trace_start =
        trace_move_release ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    const auto constrained_delta =
        axis_constrained_move_delta(document_position(event->pos()) - move_start_, event->modifiers());
    move_preview_delta_ = axis_constrained_move_delta(snapped_move_delta(constrained_delta), event->modifiers());
    QRegion dirty_region;
    QRegion patched_region;
    std::vector<RenderedDocumentPatch> precommit_patches;
    bool attempted_precommit_patch = false;
    bool used_precommit_patch = false;
    bool reused_preview_patch = false;
    const auto move_layer_count = moving_layers_.size();
    const auto move_operation_active = !move_preview_delta_.isNull();
    const bool rerender_smart_filters =
        document_ != nullptr &&
        std::any_of(moving_layers_.begin(), moving_layers_.end(),
                    [this](const MovingLayer& moving_layer) {
                      const auto* layer = document_->find_layer(moving_layer.id);
                      return layer != nullptr &&
                             move_layer_requires_smart_filter_rerender(*layer);
                    });
    if (move_operation_active) {
      begin_processing_operation();
    }
    if (!move_preview_delta_.isNull()) {
      const auto move_label =
          moving_layers_.size() > 1U ? tr("Move layers") : tr("Move layer");
      std::optional<Document> rollback_document;
      if (rerender_smart_filters && document_ != nullptr) {
        rollback_document.emplace(*document_);
      }
      dirty_region = moving_layers_dirty_region(QPoint(), move_preview_delta_);
      patched_region = dirty_region;
      if (document_ != nullptr) {
        patched_region = patched_region.intersected(QRect(0, 0, document_->width(), document_->height()));
      }
      tick_processing_operation();
      if (!rerender_smart_filters && before_edit_callback_) {
        before_edit_callback_(move_label);
      }
      if (document_ != nullptr && !rerender_smart_filters &&
          !patched_region.isEmpty() && !render_cache_dirty_ &&
          !render_cache_.isNull() &&
          render_cache_.size() == QSize(document_->width(), document_->height())) {
        attempted_precommit_patch = true;
        if (move_preview_patches_delta_.has_value() && *move_preview_patches_delta_ == move_preview_delta_ &&
            !move_preview_patches_.empty()) {
          precommit_patches = std::move(move_preview_patches_);
          reused_preview_patch = true;
        } else {
          const auto final_bounds = moving_layer_bounds(move_preview_delta_);
          const auto force_processing_wait =
              moving_layers_use_outline_preview_ ||
              std::any_of(moving_layers_.begin(), moving_layers_.end(),
                          [](const MovingLayer& layer) { return layer.expensive_style; });
          precommit_patches =
              render_document_patches_with_processing(patched_region, final_bounds, force_processing_wait);
          for (auto& patch : precommit_patches) {
            patch.image = patch.image.convertToFormat(QImage::Format_RGBA8888);
          }
        }
      }
      bool smart_filter_rerender_failed = false;
      for (const auto& moving_layer : moving_layers_) {
        auto* layer = document_->find_layer(moving_layer.id);
        if (layer == nullptr) {
          continue;
        }
        auto new_bounds = moving_layer.original_bounds;
        new_bounds.x += move_preview_delta_.x();
        new_bounds.y += move_preview_delta_.y();
        layer->set_bounds(new_bounds);
        patchy::translate_moved_layer_metadata(*layer, move_preview_delta_.x(), move_preview_delta_.y(),
                                               document_->width(), document_->height());
        if (move_layer_requires_smart_filter_rerender(*layer)) {
          if (!smart_object_transform_render_callback_ ||
              !smart_object_transform_render_callback_(moving_layer.id)) {
            smart_filter_rerender_failed = true;
            break;
          }
          layer = document_->find_layer(moving_layer.id);
          if (layer != nullptr) {
            dirty_region +=
                to_qrect(layer_bounds_with_effects(*layer, layer->bounds()));
          }
        }
      }
      if (smart_filter_rerender_failed && rollback_document.has_value()) {
        *document_ = std::move(*rollback_document);
        precommit_patches.clear();
      } else if (rerender_smart_filters && rollback_document.has_value()) {
        auto committed_document = *document_;
        *document_ = std::move(*rollback_document);
        if (before_edit_callback_) {
          before_edit_callback_(move_label);
        }
        *document_ = std::move(committed_document);
      }
    }
    moving_layer_ = false;
    move_drag_pending_ = false;
    moving_layers_.clear();
    move_preview_patches_.clear();
    move_preview_patches_delta_.reset();
    moving_layers_use_outline_preview_ = false;
    clear_move_base_cache();
    reset_axis_constrained_stroke();
    update_move_transform_controls_dirty(std::nullopt);
    update_move_hover_outline(event->pos(), event->modifiers());
    if (!dirty_region.isEmpty()) {
      if (!precommit_patches.empty() && patch_render_cache_patches(precommit_patches)) {
        ++render_cache_diagnostics_.move_precommit_patches;
        if (reused_preview_patch) {
          ++render_cache_diagnostics_.move_preview_patch_reuses;
        }
        used_precommit_patch = true;
        notify_document_changed();
        if (zoom_ < 1.0) {
          update();
        } else {
          QRegion widget_region;
          for (const auto& rect : patched_region) {
            widget_region += widget_rect_for_document_rect(rect);
          }
          update(widget_region);
        }
      } else {
        document_changed_effect_bounds(dirty_region);
      }
    } else {
      update();
    }
    if (move_operation_active) {
      end_processing_operation();
    }
    if (trace_move_release) {
      const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                                trace_start);
      const auto trace_dirty = patched_region.boundingRect();
      std::cerr << "PATCHY_RENDER_TRACE move_release dirty=" << trace_dirty.x() << "," << trace_dirty.y() << ","
                << trace_dirty.width() << "," << trace_dirty.height() << " layers=" << move_layer_count
                << " precommit_attempted=" << (attempted_precommit_patch ? 1 : 0)
                << " precommit_patched=" << (used_precommit_patch ? 1 : 0)
                << " preview_reused=" << (reused_preview_patch ? 1 : 0) << " elapsed_ms=" << elapsed.count()
                << '\n';
    }
    return;
  }

  if (moving_selection_) {
    moving_selection_ = false;
    const auto document_point = document_position(event->pos());
    const auto widget_delta = event->pos() - selection_press_widget_position_;
    const bool was_click = widget_delta.manhattanLength() < QApplication::startDragDistance();
    if (was_click) {
      // A press inside the selection that never became a drag is a plain click,
      // which deselects (matching the click-to-deselect behaviour elsewhere).
      restore_selection_before_edit();
      clear_selection();
      record_selection_history(tr("Deselect"), selection_snapshot_before_edit());
    } else {
      apply_selection_move(document_point - selection_move_origin_document_);
      // Coalesce with any preceding move/nudge so a run of repositions is one
      // undo step that returns to the pre-move location.
      record_selection_history(tr("Move Selection"), selection_snapshot_before_edit(), /*coalesce=*/true);
    }
    selection_before_edit_ = QRegion();
    selection_display_region_before_edit_ = QRegion();
    selection_mask_before_edit_bounds_ = {};
    selection_mask_before_edit_alpha_ = QImage();
    emit_info_for_widget_position(event->pos());
    update_tool_cursor();
    update();
    return;
  }

  if (selecting_) {
    selecting_ = false;
    const auto document_point = document_position(event->pos());
    const auto widget_delta = event->pos() - selection_press_widget_position_;
    const bool was_click = !spacebar_repositioning_drag_rect_ &&
                           marquee_style_ != MarqueeStyle::FixedSize &&
                           widget_delta.manhattanLength() < QApplication::startDragDistance();
    const auto marquee_label =
        tool_ == CanvasTool::EllipticalMarquee ? tr("Elliptical Marquee") : tr("Rectangular Marquee");
    if (was_click) {
      // A plain click (no drag) deselects in Replace mode; add/subtract are no-ops.
      restore_selection_before_edit();
      if (selection_operation_ == SelectionMode::Replace) {
        clear_selection();
      }
      record_selection_history(tr("Deselect"), selection_snapshot_before_edit());
      selection_before_edit_ = QRegion();
      selection_display_region_before_edit_ = QRegion();
      selection_mask_before_edit_bounds_ = {};
      selection_mask_before_edit_alpha_ = QImage();
      emit_info_for_widget_position(event->pos());
      update();
      return;
    }
    if (spacebar_repositioning_drag_rect_) {
      const auto raw_delta = document_point - spacebar_reposition_origin_document_position_;
      const auto delta = snapped_rect_delta(
          marquee_selection_rect(spacebar_reposition_start_selection_start_,
                                 spacebar_reposition_start_selection_current_),
          raw_delta);
      selection_start_ = spacebar_reposition_start_selection_start_ + delta;
      selection_current_ = spacebar_reposition_start_selection_current_ + delta;
      spacebar_repositioning_drag_rect_ = false;
    } else {
      update_selection_square_constraint(event->modifiers());
      selection_current_ = snapped_marquee_current_point(selection_start_, document_point);
    }
    // Rounded corners commit through the mask path too so the curved edges pick
    // up the Anti-alias setting (the QRegion path is hard-edged).
    if (selection_feather_radius_ > 0 ||
        marquee_effective_corner_radius(marquee_selection_rect(selection_start_, selection_current_)) > 0.0) {
      QRect mask_bounds;
      auto mask = marquee_selection_mask(selection_start_, selection_current_, mask_bounds);
      combine_selection_from_mask(mask_bounds, std::move(mask));
    } else {
      combine_selection_from_region(marquee_selection_region(selection_start_, selection_current_));
    }
    record_selection_history(marquee_label, selection_snapshot_before_edit());
    selection_before_edit_ = QRegion();
    selection_display_region_before_edit_ = QRegion();
    selection_mask_before_edit_bounds_ = {};
    selection_mask_before_edit_alpha_ = QImage();
    marquee_from_center_ = false;
    emit_info_for_widget_position(event->pos());
    update();
    return;
  }

  if (quick_selecting_) {
    extend_quick_select_stroke(document_position(event->pos()));
    finish_quick_select_stroke();
    emit_info_for_widget_position(event->pos());
    update();
    return;
  }

  if (lassoing_) {
    lassoing_ = false;
    const auto widget_delta = event->pos() - selection_press_widget_position_;
    const bool was_click = widget_delta.manhattanLength() < QApplication::startDragDistance();
    if (was_click) {
      // A plain click (no drag) deselects in Replace mode; add/subtract are no-ops.
      restore_selection_before_edit();
      if (selection_operation_ == SelectionMode::Replace) {
        clear_selection();
      }
      record_selection_history(tr("Deselect"), selection_snapshot_before_edit());
      selection_before_edit_ = QRegion();
      selection_display_region_before_edit_ = QRegion();
      selection_mask_before_edit_bounds_ = {};
      selection_mask_before_edit_alpha_ = QImage();
      lasso_points_.clear();
      emit_info_for_widget_position(event->pos());
      update();
      return;
    }
    if (document_ != nullptr) {
      const auto point = clamped_document_point(*document_, document_position(event->pos()));
      if (lasso_points_.isEmpty() || lasso_points_.last() != point) {
        lasso_points_ << point;
      }
      if (lasso_points_.size() >= 3) {
        // A freehand outline is all diagonal edges: with Anti-alias on it
        // commits through the mask path for partial edge coverage, like the
        // marquee's rounded corners (the QRegion path is hard-edged).
        if (selection_feather_radius_ > 0 || selection_antialias_) {
          QRect mask_bounds;
          auto mask = lasso_selection_mask(lasso_points_, mask_bounds);
          combine_selection_from_mask(mask_bounds, std::move(mask));
        } else {
          auto lasso_region = QRegion(lasso_points_, Qt::WindingFill);
          lasso_region =
              lasso_region.intersected(QRegion(QRect(0, 0, document_->width(), document_->height())));
          combine_selection_from_region(lasso_region);
        }
      } else {
        restore_selection_before_edit();
      }
    }
    record_selection_history(tr("Lasso"), selection_snapshot_before_edit());
    selection_before_edit_ = QRegion();
    selection_display_region_before_edit_ = QRegion();
    selection_mask_before_edit_bounds_ = {};
    selection_mask_before_edit_alpha_ = QImage();
    lasso_points_.clear();
    emit_info_for_widget_position(event->pos());
    update();
    return;
  }

  if (zooming_) {
    zooming_ = false;
    if (document_ != nullptr) {
      zoom_current_ = clamped_document_point(*document_, document_position(event->pos()));
      const bool zoom_out = (event->modifiers() & Qt::AltModifier) != 0;
      const auto widget_drag = (event->pos() - widget_position(zoom_start_)).manhattanLength();
      const auto zoom_rect = normalized_rect(zoom_start_, zoom_current_);
      // Alt is always a point zoom-out, never a marquee. A drag counts as a
      // marquee when it covers real distance and spans more than a pixel in at
      // least one axis, so a thin strip clamped to an edge still zooms to fit.
      if (!zoom_out && widget_drag >= 8 && (zoom_rect.width() > 1 || zoom_rect.height() > 1)) {
        zoom_to_document_rect(zoom_rect);
      } else {
        // A click in the grey margin zooms toward the nearest point on the
        // document frame rather than toward the empty space under the cursor.
        const QRectF frame(widget_position_f(QPointF(0.0, 0.0)),
                           widget_position_f(QPointF(document_->width(), document_->height())));
        const QPointF clicked = event->position();
        const QPointF anchor(std::clamp(clicked.x(), frame.left(), frame.right()),
                             std::clamp(clicked.y(), frame.top(), frame.bottom()));
        zoom_at_widget_point(anchor, zoom_out ? 0.5 : 2.0);
      }
    }
    emit_info_for_widget_position(event->pos());
    update();
    return;
  }

  if (drawing_shape_) {
    const auto document_point = document_position(event->pos());
    const auto snapped_point = snapped_document_point(document_point);
    if (spacebar_repositioning_drag_rect_) {
      const auto delta = document_point - spacebar_reposition_last_document_position_;
      shape_start_ += delta;
      shape_current_ += delta;
      spacebar_repositioning_drag_rect_ = false;
    } else {
      shape_current_ = snapped_point;
    }
    shape_square_constrained_ = (event->modifiers() & Qt::ShiftModifier) != 0 &&
                                (tool_ == CanvasTool::Rectangle || tool_ == CanvasTool::Ellipse);
    shape_from_center_ = (event->modifiers() & Qt::AltModifier) != 0 &&
                         (tool_ == CanvasTool::Rectangle || tool_ == CanvasTool::Ellipse);
    const auto erase = false;
    auto shape_from = shape_start_;
    auto shape_end = shape_current_;
    if (tool_ == CanvasTool::Rectangle || tool_ == CanvasTool::Ellipse) {
      const auto rect = shape_drag_rect(shape_start_, shape_current_);
      shape_from = rect.topLeft();
      shape_end = rect.bottomRight();
    }
    // Shape/Path mode hands the drag geometry to MainWindow (shape layer or
    // work path) instead of painting; mask/channel/quick-mask targets keep the
    // raster behavior so the drag still edits the targeted plane.
    if ((tool_ == CanvasTool::Line || tool_ == CanvasTool::Rectangle || tool_ == CanvasTool::Ellipse) &&
        vector_tool_mode_ != VectorToolMode::Pixels && vector_shape_drawn_callback_ &&
        !quick_mask_active_ && layer_edit_target_ == LayerEditTarget::Content) {
      drawing_shape_ = false;
      clear_brush_stroke_tracking();
      update();
      const auto kind = tool_ == CanvasTool::Line        ? patchy::LiveShapeKind::Line
                        : tool_ == CanvasTool::Rectangle ? patchy::LiveShapeKind::Rectangle
                                                         : patchy::LiveShapeKind::Ellipse;
      const auto corner_rect = normalized_rect(shape_from, shape_end);
      // Edge semantics: the drag corners are edge coordinates, so the vector
      // width is right - left (not QRect's inclusive-pixel width).
      const QRectF bounds(QPointF(corner_rect.topLeft()), QPointF(corner_rect.bottomRight()));
      vector_shape_drawn_callback_(kind, bounds, QPointF(shape_start_), QPointF(shape_current_));
      return;
    }
    QRect preview_rect = normalized_rect(shape_from, shape_end);
    if (document_ != nullptr) {
      const auto margin = std::max(4, brush_size_ + 4);
      preview_rect = preview_rect.adjusted(-margin, -margin, margin, margin)
                         .intersected(QRect(0, 0, document_->width(), document_->height()));
    }
    const auto processing = !editing_smart_filter_mask();
    if (processing) {
      begin_processing_operation();
    }
    QRect dirty;
    if (tool_ == CanvasTool::Line) {
      dirty = draw_line(shape_start_, shape_current_, erase);
    } else if (tool_ == CanvasTool::Gradient) {
      dirty = draw_gradient(shape_start_, shape_current_);
    } else if (tool_ == CanvasTool::Rectangle) {
      dirty = draw_rectangle(shape_from, shape_end, erase);
    } else if (tool_ == CanvasTool::Ellipse) {
      dirty = draw_ellipse(shape_from, shape_end, erase);
    }
    if (processing) {
      tick_processing_operation();
    }
    drawing_shape_ = false;
    clear_brush_stroke_tracking();
    const auto repaint_rect =
        !preview_rect.isEmpty() && !dirty.isEmpty() ? preview_rect.united(dirty)
        : !preview_rect.isEmpty()                  ? preview_rect
                                                   : dirty;
    active_edit_target_changed_impl(QRegion(repaint_rect));
    // The drag size readout is drawn offset from the drag corner, outside the shape's
    // dirty margin, so the bounded repaint above would leave it on screen after the
    // commit; repaint the whole viewport once to clear it.
    update();
    if (processing) {
      end_processing_operation();
    }
    return;
  }
}

void CanvasWidget::mouseDoubleClickEvent(QMouseEvent* event) {
  const auto document_point = document_position(event->pos());
  if (edit_locked_) {
    show_edit_locked_message();
    event->accept();
    return;
  }
  if (quick_mask_active_ && event->button() == Qt::LeftButton &&
      (tool_ == CanvasTool::Move || tool_ == CanvasTool::Marquee ||
       tool_ == CanvasTool::EllipticalMarquee || tool_ == CanvasTool::Lasso ||
       tool_ == CanvasTool::MagneticLasso || tool_ == CanvasTool::MagicWand ||
       tool_ == CanvasTool::QuickSelect || tool_ == CanvasTool::Clone ||
       tool_ == CanvasTool::Healing ||
       tool_ == CanvasTool::Smudge || is_local_adjustment_tool(tool_) || tool_ == CanvasTool::Text)) {
    report_status_error(tr("This tool is unavailable in Quick Mask mode"));
    event->accept();
    return;
  }
  if (layer_edit_target_ == LayerEditTarget::SmartFilterMask && event->button() == Qt::LeftButton &&
      (tool_ == CanvasTool::Move || tool_ == CanvasTool::Marquee ||
       tool_ == CanvasTool::EllipticalMarquee || tool_ == CanvasTool::Lasso ||
       tool_ == CanvasTool::MagneticLasso || tool_ == CanvasTool::MagicWand ||
       tool_ == CanvasTool::QuickSelect || tool_ == CanvasTool::Clone ||
       tool_ == CanvasTool::Healing ||
       tool_ == CanvasTool::Smudge || is_local_adjustment_tool(tool_) || tool_ == CanvasTool::Text)) {
    report_status_error(tr("This tool is unavailable while editing a Smart Filter mask"));
    event->accept();
    return;
  }
  if (tool_ == CanvasTool::MagneticLasso && magnetic_lassoing_ && event->button() == Qt::LeftButton) {
    // Must run before the text-layer branch below (which is not tool-gated).
    // Qt already delivered this double-click's press, so the path gained a
    // harmless final manual anchor before closing. Alt+double-click closes
    // with a straight segment, plain double-click magnetically (PS).
    finish_magnetic_lasso(!event->modifiers().testFlag(Qt::AltModifier));
    event->accept();
    return;
  }
  if (event->button() == Qt::LeftButton && document_contains(document_point)) {
    if (transforming_layer_ && transform_layer_id_.has_value()) {
      const auto transform_hit = transform_handle_at(event->pos());
      const auto text_layer_id = *transform_layer_id_;
      auto* transformed_layer = document_ != nullptr ? document_->find_layer(text_layer_id) : nullptr;
      if (transformed_layer != nullptr && layer_is_text(*transformed_layer) &&
          transform_hit != TransformHandle::None) {
        finish_free_transform();
        if (auto* layer = document_ != nullptr ? document_->find_layer(text_layer_id) : nullptr; layer != nullptr) {
          activate_layer(*layer);
          if (text_requested_callback_) {
            text_requested_callback_(document_point, QRect());
          }
          event->accept();
          return;
        }
      }
    }
    if (transforming_layer_) {
      event->accept();
      return;
    }
    if (auto* layer = topmost_text_layer_at(document_point); layer != nullptr) {
      activate_layer(*layer);
      if (text_requested_callback_) {
        text_requested_callback_(document_point, QRect());
      }
      event->accept();
      return;
    }
  }
  QWidget::mouseDoubleClickEvent(event);
}

void CanvasWidget::keyPressEvent(QKeyEvent* event) {
  if (brush_adjust_dragging_ && event->key() == Qt::Key_Escape) {
    end_brush_adjust_drag(false);
    event->accept();
    return;
  }

  if (transient_read_callback_ && event->key() == Qt::Key_Escape) {
    if (transient_read_dragging_) {
      transient_read_dragging_ = false;
      if (QWidget::mouseGrabber() == this) {
        releaseMouse();
      }
      auto callback = transient_read_callback_;
      callback(CanvasReadGesture{document_position(last_mouse_position_), mapToGlobal(last_mouse_position_),
                                 event->modifiers(), CanvasReadPhase::Cancel});
      event->accept();
      return;
    }
    // The canvas and non-modal owner are sibling windows, so an unhandled key
    // cannot bubble back to the dialog after an on-canvas sample takes focus.
    auto callback = transient_read_callback_;
    callback(CanvasReadGesture{document_position(last_mouse_position_), mapToGlobal(last_mouse_position_),
                               event->modifiers(), CanvasReadPhase::Dismiss});
    event->accept();
    return;
  }

  if (edit_locked_) {
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
      spacebar_panning_ = true;
      setCursor(Qt::OpenHandCursor);
      event->accept();
      return;
    }
    show_edit_locked_message();
    event->accept();
    return;
  }

  // An active pen-path session owns Escape/Backspace/Delete/Enter the same way
  // the magnetic lasso does (ShortcutOverride accepted in event()).
  if (handle_pen_key(event)) {
    event->accept();
    return;
  }

  // Path-edit selections own Delete/Backspace (delete anchors), Escape
  // (deselect), and the arrow keys (nudge).
  if (handle_path_edit_key(event)) {
    event->accept();
    return;
  }

  // An active magnetic-lasso trace owns Escape/Backspace/Delete/Enter. Backspace and
  // Delete both pop the last anchor (Photoshop behavior); they reach this handler even
  // though layer.clear binds them app-level because CanvasWidget::event() accepts the
  // ShortcutOverride for these keys while a trace is live.
  if (magnetic_lassoing_) {
    if (event->key() == Qt::Key_Escape) {
      cancel_magnetic_lasso();
      event->accept();
      return;
    }
    if (event->key() == Qt::Key_Backspace || event->key() == Qt::Key_Delete) {
      pop_magnetic_anchor();
      event->accept();
      return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
      // Alt+Enter closes with a straight segment, plain Enter magnetically (PS).
      finish_magnetic_lasso(!event->modifiers().testFlag(Qt::AltModifier));
      event->accept();
      return;
    }
  }

  if (dragging_guide_ && event->key() == Qt::Key_Escape) {
    cancel_guide_drag();
    event->accept();
    return;
  }

  if (!guides_locked_ && has_selected_guides() &&
      (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)) {
    clear_selected_guides();
    event->accept();
    return;
  }

  if (!event->isAutoRepeat() && event->key() == Qt::Key_A &&
      event->modifiers() == Qt::ControlModifier) {
    if (quick_mask_active_) {
      report_status_error(tr("Select All is unavailable in Quick Mask mode"));
    } else {
      select_all();
    }
    event->accept();
    return;
  }

  // Shift toggled mid-drag arrives as a key event, not a mouse move; update the
  // constraint here so a stationary cursor still responds.
  if (selecting_ && !spacebar_repositioning_drag_rect_ && event->key() == Qt::Key_Shift &&
      !event->isAutoRepeat()) {
    update_selection_square_constraint(event->modifiers() | Qt::ShiftModifier);
    refresh_active_marquee_selection();
    event->accept();
    return;
  }

  // Shift/Alt toggled mid-shape-drag arrives as a key event, not a mouse move; update
  // the square/from-center constraints so a stationary cursor still snaps.
  if (drawing_shape_ && !spacebar_repositioning_drag_rect_ &&
      (event->key() == Qt::Key_Shift || event->key() == Qt::Key_Alt) && !event->isAutoRepeat() &&
      (tool_ == CanvasTool::Rectangle || tool_ == CanvasTool::Ellipse)) {
    if (event->key() == Qt::Key_Shift) {
      shape_square_constrained_ = true;
    } else {
      shape_from_center_ = true;
    }
    update();
    event->accept();
    return;
  }

  if (warping_layer_) {
    if (event->key() == Qt::Key_Escape) {
      cancel_warp_transform();
      event->accept();
      return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
      commit_warp_transform();
      event->accept();
      return;
    }
  }

  if (transforming_layer_) {
    if (event->key() == Qt::Key_Escape) {
      cancel_free_transform();
      event->accept();
      return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
      commit_free_transform();
      event->accept();
      return;
    }
    // Arrow keys nudge the pending transform (box + preview together), the same way a
    // Move-handle drag does — never the destructive layer nudge below. Shift = 10px.
    // Auto-repeat is allowed (holding an arrow scrolls the box); the move is pending
    // inside the transform, so there is no per-keystroke undo to spam.
    if (event->modifiers() == Qt::NoModifier || event->modifiers() == Qt::ShiftModifier) {
      const auto step = event->modifiers() == Qt::ShiftModifier ? 10 : 1;
      QPoint delta;
      switch (event->key()) {
        case Qt::Key_Left:
          delta = QPoint(-step, 0);
          break;
        case Qt::Key_Right:
          delta = QPoint(step, 0);
          break;
        case Qt::Key_Up:
          delta = QPoint(0, -step);
          break;
        case Qt::Key_Down:
          delta = QPoint(0, step);
          break;
        default:
          break;
      }
      if (!delta.isNull()) {
        if (prepare_free_transform_source()) {
          transform_current_rect_.translate(delta.x(), delta.y());
          refresh_transform_composited_preview_cache();
          update();
          notify_transform_controls_changed();
        }
        event->accept();
        return;
      }
    }
  }

  if (dragging_text_rect_ && event->key() == Qt::Key_Escape) {
    dragging_text_rect_ = false;
    text_rect_current_ = text_rect_start_;
    emit_info_for_widget_position(last_mouse_position_);
    update();
    event->accept();
    return;
  }

  if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
    if (selecting_) {
      spacebar_repositioning_drag_rect_ = true;
      spacebar_reposition_last_document_position_ = document_position(last_mouse_position_);
      spacebar_reposition_origin_document_position_ = spacebar_reposition_last_document_position_;
      spacebar_reposition_start_selection_start_ = selection_start_;
      spacebar_reposition_start_selection_current_ = selection_current_;
      setCursor(Qt::SizeAllCursor);
    } else if (drawing_shape_) {
      spacebar_repositioning_drag_rect_ = true;
      spacebar_reposition_last_document_position_ = document_position(last_mouse_position_);
      setCursor(Qt::SizeAllCursor);
    } else {
      spacebar_panning_ = true;
      setCursor(Qt::OpenHandCursor);
    }
    event->accept();
    return;
  }

  if (handle_opacity_digit_key(event->key(), event->modifiers(), event->isAutoRepeat())) {
    event->accept();
    return;
  }

  // With a selection tool active and a live selection, arrow keys nudge the
  // selection outline (Shift = 10px); the Move tool still nudges layer pixels.
  // Auto-repeat is allowed here so holding an arrow scrolls the selection along;
  // the nudges coalesce into a single undo step (see nudge_selection), so
  // auto-repeat does not spam the history.
  if (layer_edit_target_ != LayerEditTarget::SmartFilterMask &&
      !selection_.isEmpty() && !moving_selection_ &&
      (tool_ == CanvasTool::Marquee || tool_ == CanvasTool::EllipticalMarquee ||
       tool_ == CanvasTool::Lasso || tool_ == CanvasTool::MagneticLasso ||
       tool_ == CanvasTool::MagicWand) &&
      (event->modifiers() == Qt::NoModifier || event->modifiers() == Qt::ShiftModifier)) {
    const auto step = event->modifiers() == Qt::ShiftModifier ? 10 : 1;
    QPoint delta;
    switch (event->key()) {
      case Qt::Key_Left:
        delta = QPoint(-step, 0);
        break;
      case Qt::Key_Right:
        delta = QPoint(step, 0);
        break;
      case Qt::Key_Up:
        delta = QPoint(0, -step);
        break;
      case Qt::Key_Down:
        delta = QPoint(0, step);
        break;
      default:
        break;
    }
    if (!delta.isNull()) {
      nudge_selection(delta);
      event->accept();
      return;
    }
  }

  if (!event->isAutoRepeat() && (event->modifiers() == Qt::NoModifier || event->modifiers() == Qt::ShiftModifier)) {
    const auto step = event->modifiers() == Qt::ShiftModifier ? 10 : 1;
    QPoint delta;
    switch (event->key()) {
      case Qt::Key_Left:
        delta = QPoint(-step, 0);
        break;
      case Qt::Key_Right:
        delta = QPoint(step, 0);
        break;
      case Qt::Key_Up:
        delta = QPoint(0, -step);
        break;
      case Qt::Key_Down:
        delta = QPoint(0, step);
        break;
      default:
        break;
    }
    const auto movable_ids = movable_layer_ids();
    if (!delta.isNull() && !movable_ids.empty()) {
      begin_processing_operation();
      tick_processing_operation();
      const auto dirty = move_active_layer_by(delta);
      if (!dirty.isEmpty()) {
        document_changed_effect_bounds(dirty);
      }
      end_processing_operation();
      event->accept();
      return;
    } else if (!delta.isNull() && active_layer_locks_position()) {
      show_layer_position_locked_message();
      event->accept();
      return;
    }
  }
  QWidget::keyPressEvent(event);
}

void CanvasWidget::keyReleaseEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
    spacebar_repositioning_drag_rect_ = false;
    spacebar_panning_ = false;
    set_tool(tool_);
    event->accept();
    return;
  }
  if (selecting_ && !spacebar_repositioning_drag_rect_ && event->key() == Qt::Key_Shift &&
      !event->isAutoRepeat()) {
    update_selection_square_constraint(event->modifiers() & ~Qt::ShiftModifier);
    refresh_active_marquee_selection();
    event->accept();
    return;
  }
  if (drawing_shape_ && !spacebar_repositioning_drag_rect_ &&
      (event->key() == Qt::Key_Shift || event->key() == Qt::Key_Alt) && !event->isAutoRepeat()) {
    if (event->key() == Qt::Key_Shift) {
      shape_square_constrained_ = false;
    } else {
      shape_from_center_ = false;
    }
    update();
    event->accept();
    return;
  }
  QWidget::keyReleaseEvent(event);
}

bool CanvasWidget::handle_opacity_digit_key(int key, Qt::KeyboardModifiers modifiers, bool auto_repeat) {
  // Photoshop-style numeric entry: Brush uses Shift+digits for Flow, except
  // while Airbrush is on, when bare digits set Flow and Shift+digits set
  // Opacity. Pattern Stamp uses bare digits for Opacity and Shift+digits for
  // Flow. Other painting tools keep the historical bare-digit Opacity path.
  if (auto_repeat || key < Qt::Key_0 || key > Qt::Key_9 ||
      !tool_supports_opacity_digit_keys(tool_)) {
    return false;
  }
  const auto semantic_modifiers = modifiers & ~Qt::KeypadModifier;
  const auto shift = semantic_modifiers == Qt::ShiftModifier;
  if ((semantic_modifiers != Qt::NoModifier && !shift) ||
      (shift && tool_ != CanvasTool::Brush && tool_ != CanvasTool::PatternStamp)) {
    return false;
  }
  const auto targets_flow =
      (tool_ == CanvasTool::Brush && (brush_build_up_ ? !shift : shift)) ||
      (tool_ == CanvasTool::PatternStamp && shift);
  if (opacity_pending_digit_ >= 0 && targets_flow != opacity_digit_targets_flow_) {
    opacity_pending_digit_ = -1;
    opacity_digit_timer_.invalidate();
  }
  opacity_digit_targets_flow_ = targets_flow;
  constexpr qint64 kDigitPairWindowMs = 800;
  const auto digit = key - Qt::Key_0;
  int value = 0;
  if (opacity_pending_digit_ >= 0 && opacity_digit_timer_.isValid() &&
      opacity_digit_timer_.elapsed() < kDigitPairWindowMs) {
    value = opacity_pending_digit_ * 10 + digit;
    opacity_pending_digit_ = -1;
    opacity_digit_timer_.invalidate();
  } else {
    value = digit == 0 ? 100 : digit * 10;
    opacity_pending_digit_ = digit;
    opacity_digit_timer_.start();
  }
  value = std::clamp(value, 1, 100);
  if (targets_flow) {
    set_brush_flow(value);
    if (status_callback_) {
      status_callback_(tr("Brush flow: %1%").arg(brush_flow_));
    }
  } else if (tool_ == CanvasTool::Gradient) {
    set_gradient_opacity(value);
    if (status_callback_) {
      status_callback_(tr("Gradient opacity: %1%").arg(gradient_opacity_));
    }
  } else {
    set_brush_opacity(value);
    if (status_callback_) {
      status_callback_(tr("Brush opacity: %1%").arg(brush_opacity_));
    }
  }
  notify_brush_settings_changed();
  return true;
}

void CanvasWidget::focusOutEvent(QFocusEvent* event) {
  const auto was_painting = painting_;
  const auto was_drawing_smart_filter_mask_shape =
      drawing_shape_ && layer_edit_target_ == LayerEditTarget::SmartFilterMask;
  if (transient_read_dragging_) {
    transient_read_dragging_ = false;
    if (QWidget::mouseGrabber() == this) {
      releaseMouse();
    }
    auto callback = transient_read_callback_;
    if (callback) {
      callback(CanvasReadGesture{document_position(last_mouse_position_), mapToGlobal(last_mouse_position_),
                                 Qt::NoModifier, CanvasReadPhase::Cancel});
    }
  }
  if (brush_adjust_dragging_) {
    end_brush_adjust_drag(true);
  }
  if (color_picking_) {
    end_color_pick();
  }
  spacebar_repositioning_drag_rect_ = false;
  spacebar_panning_ = false;
  dragging_text_rect_ = false;
  if (was_drawing_smart_filter_mask_shape) {
    // Shape pixels are applied only on release. Losing focus before that point
    // cancels the visual drag and its untouched pre-edit snapshot.
    drawing_shape_ = false;
    cancel_smart_filter_mask_edit();
  }
  if (was_painting) {
    painting_ = false;
    last_stroke_end_document_ = last_document_position_f_;
  }
  if (!was_painting && !drawing_shape_) {
    clear_brush_stroke_tracking();
  }
  clone_source_cache_ = QImage();
  smudge_state_ = {};
  mixer_brush_state_ = {};
  reset_brush_smoothing();
  reset_axis_constrained_stroke();
  zooming_ = false;
  // A hover trace cannot survive losing the keyboard: Backspace/Enter/Escape
  // would land elsewhere while the wire keeps following the pointer.
  cancel_magnetic_lasso();
  set_tool(tool_);
  if (was_painting) {
    clear_brush_stroke_tracking();
    if (quick_mask_active_) {
      finish_quick_mask_edit();
    } else if (layer_edit_target_ == LayerEditTarget::SmartFilterMask) {
      finish_smart_filter_mask_edit();
    } else {
      notify_document_changed(DocumentChangeReason::BrushStrokeFinished);
    }
  }
  QWidget::focusOutEvent(event);
}

void CanvasWidget::timerEvent(QTimerEvent* event) {
  if (event->timerId() == airbrush_timer_.timerId()) {
    if (!painting_ || !brush_build_up_ || effective_tool_for_input() != CanvasTool::Brush) {
      airbrush_timer_.stop();
    } else {
      // Patent boundary (July 2026): classic Airbrush is only a fixed-rate
      // repetition of one current flat 2D brush stamp. Shape and Transfer
      // dynamics advance, but stationary ticks suppress Scattering and Count;
      // they do not model particles, a 3D spray cone or stylus pose,
      // velocity-dependent flow, bristles, fluid surfaces, wet paint, or
      // bidirectional paint transfer.
      const auto dirty = draw_airbrush_dab(last_document_position_f_);
      if (!dirty.isEmpty()) {
        active_edit_target_changed_impl(QRegion(dirty),
                                        DocumentChangeReason::BrushStrokePreview);
      }
    }
    event->accept();
    return;
  }
  if (event->timerId() == processing_animation_timer_.timerId()) {
    processing_animation_frame_ = (processing_animation_frame_ + 1) % 12;
    ++render_cache_diagnostics_.processing_overlay_frames;
    if (processing_overlay_visible_) {
      update();
    } else {
      processing_animation_timer_.stop();
    }
    event->accept();
    return;
  }
  if (event->timerId() == selection_timer_.timerId()) {
    selection_dash_offset_ = (selection_dash_offset_ + 1) % 8;
    if ((!quick_mask_active_ && !selection_.isEmpty() &&
         selection_edges_visible_) ||
        lassoing_ || magnetic_lassoing_ || zooming_) {
      update();
    }
    event->accept();
    return;
  }
  QWidget::timerEvent(event);
}

bool CanvasWidget::begin_edit(QString label) {
  if (quick_mask_active_) {
    if (!quick_mask_edit_before_.has_value()) {
      quick_mask_edit_before_ = capture_selection_snapshot();
      quick_mask_edit_label_ = std::move(label);
      quick_mask_edit_dirty_ = QRegion();
    }
    return true;
  }
  if (layer_edit_target_ == LayerEditTarget::SmartFilterMask) {
    if (!editing_smart_filter_mask()) {
      report_status_error(tr("The Smart Filter mask is no longer available"));
      clear_smart_filter_mask_edit_target();
      return false;
    }
    if (!smart_filter_mask_edit_before_.has_value()) {
      smart_filter_mask_edit_before_ = smart_filter_mask_pixels_;
      smart_filter_mask_edit_label_ = std::move(label);
      smart_filter_mask_edit_dirty_ = QRegion();
    }
    return true;
  }
  if (layer_edit_target_ == LayerEditTarget::DocumentChannel) {
    const auto* channel = active_document_channel_const();
    if (channel == nullptr) {
      report_status_error(tr("Select a saved channel to edit"));
      return false;
    }
    if (channel->kind() != DocumentChannelKind::Alpha) {
      report_status_error(tr("Spot channels are read-only"));
      return false;
    }
    if (before_edit_callback_) {
      before_edit_callback_(label);
    }
    return true;
  }
  if (layer_edit_target_ == LayerEditTarget::ComponentRed ||
      layer_edit_target_ == LayerEditTarget::ComponentGreen ||
      layer_edit_target_ == LayerEditTarget::ComponentBlue) {
    report_status_error(tr("Color component channels are read-only"));
    return false;
  }
  if (active_layer_locks_image_pixels()) {
    show_layer_pixels_locked_message();
    return false;
  }

  if (layer_edit_target_ == LayerEditTarget::Mask) {
    // editing_layer_mask() is the non-bumping const check; the mutable
    // active_layer_mask() accessor would bump revisions on this read-only
    // precondition (rejected edits must not invalidate caches).
    if (!editing_layer_mask()) {
      report_status_error(tr("Select a layer mask to edit"));
      return false;
    }
    if (before_edit_callback_) {
      before_edit_callback_(label);
    }
    return true;
  }

  auto* layer = active_pixel_layer();
  // Format check through const: the non-const pixels() accessor bumps all
  // three revisions on access, so a REJECTED edit (non-8-bit layer) must not
  // invalidate the layer's caches. Accepted edits bump when they write.
  if (layer == nullptr || std::as_const(*layer).pixels().format().bit_depth != BitDepth::UInt8) {
    report_status_error(tr("Select an editable 8-bit pixel layer first"));
    return false;
  }
  if (layer_is_text(*layer)) {
    report_status_error(tr("Select a normal pixel layer before painting on text"));
    return false;
  }
  if (layer_is_smart_object(*layer)) {
    report_status_error(tr("Smart object contents can't be painted. Rasterize the layer to edit its pixels."));
    return false;
  }
  if (layer_is_vector_shape(*layer)) {
    report_status_error(tr("Shape layers can't be painted. Rasterize the layer to edit its pixels."));
    return false;
  }

  if (before_edit_callback_) {
    before_edit_callback_(label);
  }
  return true;
}

CanvasTool CanvasWidget::effective_tool_for_input() const noexcept {
  if (active_pen_input_sample_.has_value() && pen_input_settings_.enabled && pen_input_settings_.use_eraser_tip &&
      active_pen_input_sample_->pointer_type == PenInputSample::PointerType::Eraser) {
    switch (tool_) {
      case CanvasTool::Brush:
      case CanvasTool::MixerBrush:
      case CanvasTool::PatternStamp:
      case CanvasTool::Clone:
      case CanvasTool::Healing:
      case CanvasTool::Smudge:
      case CanvasTool::Dodge:
      case CanvasTool::Burn:
      case CanvasTool::Sponge:
      case CanvasTool::BlurBrush:
      case CanvasTool::SharpenBrush:
        return CanvasTool::Eraser;
      default:
        break;
    }
  }
  return tool_;
}

}  // namespace patchy::ui
