#include "ui/canvas_widget.hpp"
#include <QScopeGuard>
#include <cmath>

namespace patchy::ui {

QRect CanvasWidget::paint_script_stroke(const ScriptStroke& stroke, const std::function<bool(const QRect&)>& progress) {
  if (stroke.points.empty()) { return {}; }
  const auto saved_tool = tool_;
  const auto saved_target = layer_edit_target_;
  const auto saved_quick_mask = quick_mask_active_;
  const auto saved_color = primary_color_;
  const auto saved_size = brush_size_;
  const auto saved_opacity = brush_opacity_;
  const auto saved_flow = brush_flow_;
  const auto saved_build_up = brush_build_up_;
  const auto saved_softness = brush_softness_;
  const auto saved_tip = brush_tip_;
  const auto saved_tip_id = brush_tip_id_;
  const auto saved_dynamics = brush_dynamics_;
  const auto saved_sample = active_pen_input_sample_;
  const auto saved_pen = pen_input_settings_;
  const auto saved_seed = brush_dynamics_test_seed_;
  const auto saved_angle = brush_base_angle_degrees_;
  const auto saved_roundness = brush_base_roundness_;
  const auto restore = qScopeGuard([&] {
    reset_brush_smoothing();
    clear_brush_stroke_tracking();
    tool_ = saved_tool;
    layer_edit_target_ = saved_target;
    quick_mask_active_ = saved_quick_mask;
    primary_color_ = saved_color;
    brush_size_ = saved_size;
    brush_opacity_ = saved_opacity;
    brush_flow_ = saved_flow;
    brush_build_up_ = saved_build_up;
    brush_softness_ = saved_softness;
    set_brush_tip(saved_tip, saved_tip_id);
    brush_dynamics_ = saved_dynamics;
    active_pen_input_sample_ = saved_sample;
    pen_input_settings_ = saved_pen;
    brush_dynamics_test_seed_ = saved_seed;
    brush_base_angle_degrees_ = saved_angle;
    brush_base_roundness_ = saved_roundness;
  });
  tool_ = stroke.erase ? CanvasTool::Eraser : CanvasTool::Brush;
  layer_edit_target_ = LayerEditTarget::Content;
  quick_mask_active_ = false;
  primary_color_ = stroke.color;
  brush_size_ = stroke.size;
  brush_opacity_ = stroke.opacity;
  brush_flow_ = stroke.flow;
  brush_build_up_ = false;
  brush_softness_ = stroke.softness;
  set_brush_tip(nullptr, {});
  brush_dynamics_ = stroke.dynamics;
  brush_base_angle_degrees_ = 0;
  brush_base_roundness_ = 100;
  pen_input_settings_ = PenInputSettings{};
  brush_dynamics_test_seed_ = stroke.dynamics.seed;
  clear_brush_stroke_tracking();
  reset_brush_smoothing();
  auto apply_sample = [&](const ScriptStrokePoint& point) {
    PenInputSample sample;
    sample.pressure = point.pressure.value_or(1.0F);
    sample.pressure_available = point.pressure.has_value();
    active_pen_input_sample_ = sample;
  };
  apply_sample(stroke.points.front());
  auto previous = stroke.points.front().position;
  const auto rounded = [](QPointF p) {
    return QPoint(static_cast<int>(std::lround(p.x())), static_cast<int>(std::lround(p.y())));
  };
  // A size-one path is pixel exact. Larger paths share the native midpoint smoother.
  if (effective_brush_input().size != 1) { begin_brush_smoothing(previous); }
  QRect dirty = draw_brush_at(rounded(previous), stroke.erase);
  for (std::size_t i = 1; i < stroke.points.size(); ++i) {
    // Each completed sample is a safe native brush boundary for invalidation,
    // presentation and Stop. The host coalesces these regions between frames.
    if (progress && progress(dirty)) { return dirty; }
    apply_sample(stroke.points[i]);
    const auto point = stroke.points[i].position;
    dirty = dirty.united(effective_brush_input().size == 1
                            ? draw_brush_segment(rounded(previous), rounded(point), stroke.erase)
                            : advance_smoothed_brush_stroke(point, stroke.erase));
    previous = point;
  }
  if (effective_brush_input().size == 1) {
    dirty = dirty.united(draw_brush_segment(rounded(previous), rounded(previous), stroke.erase));
  } else {
    dirty = dirty.united(finish_smoothed_brush_stroke(previous, stroke.erase));
  }
  return dirty;
}

}  // namespace patchy::ui
