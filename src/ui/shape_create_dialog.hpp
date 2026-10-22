#pragma once

#include "ui/canvas_widget.hpp"
#include "ui/unit_spin_box.hpp"

#include <array>
#include <optional>

#include <QString>

class QWidget;

namespace patchy::ui {

class CustomShapeLibrary;

// A bare click (no drag) with the Rectangle, Ellipse, Polygon, or Custom
// Shape tool opens this dialog instead of committing a degenerate shape
// (Photoshop's Create <Shape> dialog). The click point is the shape's
// top-left corner, or its center when from_center is set. Rectangle also
// takes per-corner radii (top-left, top-right, bottom-right, bottom-left,
// the LiveShapeParams order) behind one linked Radius field; unequal radii
// open unlinked. Polygon takes its side count and Custom Shape its library
// shape (the options bar's picker list), both prefilled from the options bar.
// `units` shows Width/Height in the ruler unit (the radii stay px but convert
// a typed unit at the document PPI); every value is document pixels.
struct ShapeCreateRequest {
  CanvasTool tool{CanvasTool::Rectangle};
  double width{100.0};
  double height{100.0};
  bool from_center{false};
  std::array<double, 4> corner_radii{0.0, 0.0, 0.0, 0.0};
  int polygon_sides{5};
  const CustomShapeLibrary* custom_shapes{nullptr};
  QString custom_shape_id;
  DocumentFieldUnits units;
};

struct ShapeCreateResult {
  double width{100.0};
  double height{100.0};
  bool from_center{false};
  std::array<double, 4> corner_radii{0.0, 0.0, 0.0, 0.0};
  int polygon_sides{5};
  QString custom_shape_id;
};

// Modal; returns nullopt on cancel. Sizes are document pixels.
[[nodiscard]] std::optional<ShapeCreateResult> request_shape_create_settings(
    QWidget* parent, const ShapeCreateRequest& request);

}  // namespace patchy::ui
