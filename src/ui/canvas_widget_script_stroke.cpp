#include "ui/canvas_widget.hpp"
#include <QScopeGuard>
#include <cmath>
#include <limits>

namespace patchy::ui {
ScriptStroke CanvasWidget::current_script_brush() const {
  ScriptStroke s;
  s.erase = tool_ == CanvasTool::Eraser; s.mixer = tool_ == CanvasTool::MixerBrush;
  s.color = primary_color_; s.background = secondary_color_;
  s.size = brush_size_; s.opacity = s.mixer ? 100 : brush_opacity_;
  s.flow = s.mixer ? mixer_flow_ : brush_flow_; s.softness = brush_softness_;
  s.tip = brush_tip_; s.tip_id = brush_tip_id_; s.dynamics = brush_dynamics_;
  s.angle = brush_base_angle_degrees_; s.roundness = brush_base_roundness_;
  s.spacing = script_brush_spacing_;
  if (!s.spacing && s.tip) s.spacing = s.tip->default_spacing;
  s.airbrush = !s.erase && !s.mixer && brush_build_up_;
  s.wet = mixer_wet_; s.load = mixer_load_; s.mix = mixer_mix_; s.sample_all_layers = mixer_sample_all_layers_;
  s.pressure_size = pen_input_settings_.enabled && pen_input_settings_.pressure_size;
  s.pressure_opacity = pen_input_settings_.enabled && pen_input_settings_.pressure_opacity;
  s.pressure_size_min = pen_input_settings_.pressure_size_min_percent;
  s.pressure_opacity_min = pen_input_settings_.pressure_opacity_min_percent;
  s.tilt_shape = pen_input_settings_.enabled && pen_input_settings_.tilt_shape;
  s.tilt_min_roundness = pen_input_settings_.tilt_min_roundness_percent;
  s.smoothing = brush_smoothing_; s.pulled_string = brush_smoothing_pulled_string_;
  s.catch_up = brush_smoothing_catch_up_; s.catch_up_end = brush_smoothing_catch_up_end_;
  s.zoom_adjust = brush_smoothing_zoom_adjust_; s.reference_zoom = zoom_ * 100;
  return s;
}
void CanvasWidget::apply_script_brush(const ScriptStroke& s) {
  tool_ = s.mixer ? CanvasTool::MixerBrush : s.erase ? CanvasTool::Eraser : CanvasTool::Brush;
  primary_color_ = s.color; secondary_color_ = s.background;
  brush_size_ = s.size; brush_opacity_ = s.opacity; brush_flow_ = s.flow;
  brush_softness_ = s.softness; brush_build_up_ = s.airbrush;
  set_brush_tip(s.tip, s.tip_id); script_brush_spacing_ = s.spacing;
  brush_dynamics_ = s.dynamics; brush_base_angle_degrees_ = s.angle; brush_base_roundness_ = s.roundness;
  mixer_wet_ = s.wet; mixer_load_ = s.load; mixer_mix_ = s.mix; mixer_flow_ = s.flow;
  mixer_sample_all_layers_ = s.sample_all_layers;
  pen_input_settings_ = PenInputSettings{};
  pen_input_settings_.pressure_size = s.pressure_size; pen_input_settings_.pressure_opacity = s.pressure_opacity;
  pen_input_settings_.pressure_size_min_percent = s.pressure_size_min;
  pen_input_settings_.pressure_opacity_min_percent = s.pressure_opacity_min;
  pen_input_settings_.tilt_shape = s.tilt_shape;
  pen_input_settings_.tilt_min_roundness_percent = s.tilt_min_roundness;
  brush_smoothing_ = s.smoothing; brush_smoothing_pulled_string_ = s.pulled_string;
  brush_smoothing_catch_up_ = s.catch_up; brush_smoothing_catch_up_end_ = s.catch_up_end;
  brush_smoothing_zoom_adjust_ = s.zoom_adjust;
}
QRect CanvasWidget::paint_script_stroke(const ScriptStroke& stroke, const std::function<bool(const QRect&)>& progress) {
  if (stroke.points.empty()) return {};
  const auto saved = current_script_brush();
  const auto saved_tool = tool_;
  const auto saved_target = layer_edit_target_;
  const auto saved_quick_mask = quick_mask_active_;
  const auto saved_sample = active_pen_input_sample_;
  const auto saved_pen = pen_input_settings_;
  const auto saved_seed = brush_dynamics_test_seed_;
  const auto saved_spacing = script_brush_spacing_;
  const auto saved_mixer_flow = mixer_flow_;
  const auto saved_brush_flow = brush_flow_;
  const auto saved_brush_opacity = brush_opacity_;
  const auto saved_build_up = brush_build_up_;
  const auto saved_progress = script_brush_progress_;
  const auto saved_cancelled = script_brush_cancelled_;
  const auto restore = qScopeGuard([&] {
    reset_brush_smoothing(); clear_brush_stroke_tracking(); stroke_stabilizer_ = {};
    apply_script_brush(saved); tool_ = saved_tool;
    layer_edit_target_ = saved_target; quick_mask_active_ = saved_quick_mask;
    active_pen_input_sample_ = saved_sample; pen_input_settings_ = saved_pen;
    brush_dynamics_test_seed_ = saved_seed; script_brush_spacing_ = saved_spacing;
    mixer_flow_ = saved_mixer_flow; brush_flow_ = saved_brush_flow;
    brush_opacity_ = saved_brush_opacity; brush_build_up_ = saved_build_up;
    script_brush_progress_ = saved_progress; script_brush_cancelled_ = saved_cancelled;
  });
  apply_script_brush(stroke);
  script_brush_cancelled_ = false;
  script_brush_progress_ = [&](const QRect& changed) {
    if (progress && progress(changed)) script_brush_cancelled_ = true;
    return script_brush_cancelled_;
  };
  layer_edit_target_ = LayerEditTarget::Content; quick_mask_active_ = false;
  brush_dynamics_test_seed_ = stroke.dynamics.seed;
  clear_brush_stroke_tracking(); reset_brush_smoothing();
  if (stroke.mixer) begin_mixer_brush_stroke();
  auto apply_sample = [&](const ScriptStrokePoint& p) {
    if (!p.pressure && !p.x_tilt && !p.rotation && !p.tangential_pressure) {
      active_pen_input_sample_.reset();
      return;
    }
    PenInputSample sample;
    sample.pressure = p.pressure.value_or(1); sample.pressure_available = p.pressure.has_value();
    sample.x_tilt = p.x_tilt.value_or(0); sample.y_tilt = p.y_tilt.value_or(0);
    sample.tilt_available = p.x_tilt.has_value() && p.y_tilt.has_value();
    sample.rotation_degrees = p.rotation.value_or(0); sample.rotation_available = p.rotation.has_value();
    sample.tangential_pressure = p.tangential_pressure.value_or(0);
    sample.tangential_pressure_available = p.tangential_pressure.has_value();
    active_pen_input_sample_ = sample;
  };
  apply_sample(stroke.points.front());
  auto previous = stroke.points.front().position; auto raw = previous;
  const auto rounded = [](QPointF p) { return QPoint(static_cast<int>(std::lround(p.x())), static_cast<int>(std::lround(p.y()))); };
  StrokeStabilizerConfig config;
  config.leash_radius = stroke.smoothing / (stroke.zoom_adjust ? 1 : stroke.reference_zoom / 100);
  config.pulled_string = stroke.pulled_string; config.catch_up = stroke.catch_up;
  config.catch_up_on_end = stroke.catch_up_end;
  stroke_stabilizer_.begin(raw.x(), raw.y(), config);
  if (effective_brush_input().size != 1) begin_brush_smoothing(previous);
  QRect dirty = draw_brush_at(rounded(previous), stroke.erase);
  auto move = [&](QPointF point) {
    dirty = dirty.united(effective_brush_input().size == 1
      ? draw_brush_segment(rounded(previous), rounded(point), stroke.erase)
      : advance_smoothed_brush_stroke(point, stroke.erase));
    previous = point;
  };
  constexpr int never = std::numeric_limits<int>::max();
  int air_tick = stroke.airbrush ? 50 : never;
  int smooth_tick = stroke.smoothing > 0 && stroke.catch_up ? 16 : never;
  auto ticks_until = [&](int end, bool inclusive) {
    while (std::min(air_tick, smooth_tick) < end || (inclusive && std::min(air_tick, smooth_tick) == end)) {
      const auto now = std::min(air_tick, smooth_tick);
      if (smooth_tick == now) {
        if (const auto p = stroke_stabilizer_.tick(.016)) move(QPointF(p->x, p->y));
        smooth_tick += 16;
      }
      if (air_tick == now) { dirty = dirty.united(draw_airbrush_dab(previous)); air_tick += 50; }
      if (progress && progress(dirty)) return false;
    }
    return true;
  };
  for (std::size_t i = 1; i < stroke.points.size(); ++i) {
    if (progress && progress(dirty)) return dirty;
    const auto& p = stroke.points[i];
    if (p.time_ms && !ticks_until(*p.time_ms, false)) return dirty;
    apply_sample(p); raw = p.position;
    const auto point = stroke_stabilizer_.move(raw.x(), raw.y()); move(QPointF(point.x, point.y));
    if (script_brush_cancelled_) return dirty;
    if (p.time_ms && !ticks_until(*p.time_ms, true)) return dirty;
  }
  const auto end = stroke_stabilizer_.finish(raw.x(), raw.y()); const auto endpoint = QPointF(end.x, end.y);
  if (effective_brush_input().size == 1) dirty = dirty.united(draw_brush_segment(rounded(previous), rounded(endpoint), stroke.erase));
  else dirty = dirty.united(finish_smoothed_brush_stroke(endpoint, stroke.erase));
  return dirty;
}
}  // namespace patchy::ui
