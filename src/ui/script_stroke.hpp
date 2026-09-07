#pragma once

#include "core/brush_dynamics.hpp"
#include "core/brush_tip.hpp"
#include <QColor>
#include <QPointF>
#include <optional>
#include <memory>
#include <vector>

namespace patchy::ui {

struct ScriptStrokePoint {
  QPointF position;
  std::optional<float> pressure;
  std::optional<float> x_tilt, y_tilt, tangential_pressure;
  std::optional<double> rotation;
  std::optional<int> time_ms;
};

// Fully resolved settings: automation never inherits the artist's tool preferences.
struct ScriptStroke {
  std::vector<ScriptStrokePoint> points;
  QColor color{Qt::black};
  int size{1};
  int opacity{100};
  int flow{100};
  int softness{0};
  bool erase{false};
  bool mixer{false};
  QColor background{Qt::white};
  QString tip_id, preset_id, label;
  std::shared_ptr<const BrushTip> tip;
  std::optional<double> spacing;
  double angle{0};
  int roundness{100};
  bool airbrush{false};
  int wet{50}, load{50}, mix{50};
  bool sample_all_layers{false};
  int smoothing{0};
  bool pulled_string{false}, catch_up{true}, catch_up_end{true}, zoom_adjust{true};
  double reference_zoom{100};
  bool pressure_size{true}, pressure_opacity{true}, tilt_shape{false};
  int pressure_size_min{20}, pressure_opacity_min{15}, tilt_min_roundness{35};
  BrushDynamics dynamics;
};

}  // namespace patchy::ui
