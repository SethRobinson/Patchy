#pragma once

#include "core/vector_shape.hpp"
#include "ui/text_layout.hpp"
#include <QPolygonF>

namespace patchy::ui {

// Flatten once per layout, then find all spans containing an entire line band.
// No raster masks, pixel-sized loops, or dependency on document storage depth.
class TextAreaGeometry {
public:
  explicit TextAreaGeometry(const VectorPath& boundary, double scale = 1.0);
  [[nodiscard]] std::vector<std::pair<double, double>> spans(double top, double bottom) const;
  [[nodiscard]] QRectF bounds() const { return bounds_; }
  [[nodiscard]] bool contains(QPointF point) const;
private:
  struct Edge { QPointF a, b; };
  void collect_edges(std::size_t node, std::size_t first, std::size_t last, double top, double bottom,
                     std::vector<std::size_t>& result) const;
  QPolygonF polygon_;
  QRectF bounds_;
  std::vector<Edge> edges_;
  std::vector<double> max_y_;
};

// Replaces the QTextBlock layouts with independently positioned shaped lines.
// Handles remain valid until another layout; render and editor use this same pass.
[[nodiscard]] PhotoshopTextLayoutPlan area_text_layout_plan(const QTextDocument& document,
                                                           const VectorPath& boundary, double scale = 1.0);
[[nodiscard]] VerticalTextLayoutPlan vertical_area_text_layout_plan(const QTextDocument& document,
                                                                   const VectorPath& boundary, double scale = 1.0);

}  // namespace patchy::ui
