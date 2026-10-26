#pragma once

// Ruler tool geometry and readout text (GitHub issue 331, docs/pick-tool.md), free of
// CanvasWidget so tests reach it directly. Points are document pixels (y down); the
// canvas keeps the line, MainWindow formats the options-bar readout through these.

#include "ui/measurement_units.hpp"

#include <QPointF>
#include <QString>

#include <optional>

namespace patchy::ui {

// The measuring line. `start` is the vertex: a protractor's second leg runs from it to
// `second`.
struct RulerLine {
  QPointF start;
  QPointF end;
  std::optional<QPointF> second;
};

struct RulerMeasurement {
  double x{0.0};       // start point
  double y{0.0};
  double width{0.0};   // first leg, end minus start, document axes (y down)
  double height{0.0};
  // Counterclockwise positive, (-180, 180], 0 pointing right; a protractor's angle
  // between its legs, 0 to 180.
  double angle_degrees{0.0};
  double length1{0.0};
  std::optional<double> length2;
  double width2{0.0};  // second leg, second minus start (protractor only)
  double height2{0.0};
};

[[nodiscard]] RulerMeasurement measure_ruler(const RulerLine& line);

// Counterclockwise degrees that turn the first leg onto the nearest axis (horizontal or
// vertical), the turn Straighten Layer and Straighten Image apply. 0 when the leg has no
// length.
[[nodiscard]] double ruler_straighten_degrees(const RulerLine& line);

// `point` constrained to 45-degree steps around `anchor` (Shift), on whole pixels:
// horizontal and vertical keep one coordinate, diagonals keep |dx| == |dy|.
[[nodiscard]] QPointF constrain_ruler_point(QPointF anchor, QPointF point);

// The largest width x height rectangle, centered on the canvas, that stays inside the
// canvas when turned by `degrees`: Straighten Image's crop box (whole pixels, at least 1).
struct RulerStraightenCrop {
  int x{0};
  int y{0};
  int width{1};
  int height{1};
};
[[nodiscard]] RulerStraightenCrop ruler_straighten_crop(int width, int height, double degrees);

// The options-bar values in `unit` (horizontal values through horizontal_ppi and the
// document width, vertical ones through vertical_ppi and the height). Lengths have no
// Percent basis and read in pixels then.
struct RulerReadout {
  QString x;
  QString y;
  QString width;
  QString height;
  QString angle;
  QString length1;
  QString length2;  // empty without a protractor
};
[[nodiscard]] RulerReadout format_ruler_readout(const RulerMeasurement& measurement, MeasurementUnit unit,
                                                double horizontal_ppi, double vertical_ppi, double document_width,
                                                double document_height);

}  // namespace patchy::ui
