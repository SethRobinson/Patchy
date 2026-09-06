#pragma once

#include "core/brush_dynamics.hpp"
#include <QColor>
#include <QPointF>
#include <optional>
#include <vector>

namespace patchy::ui {

struct ScriptStrokePoint {
  QPointF position;
  std::optional<float> pressure;
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
  BrushDynamics dynamics;
};

}  // namespace patchy::ui
