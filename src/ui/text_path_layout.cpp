#include "ui/text_path_layout.hpp"

#include <QAbstractTextDocumentLayout>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>

#include <algorithm>
#include <cmath>

namespace patchy::ui {

QPointF TextPathLinePlan::map(QPointF straight) const {
  if (!curve) {
    return straight;
  }
  const auto sample = curve->sample(start_distance + straight.x());
  // "Down" from the baseline in y-down space is the tangent turned a quarter clockwise.
  return QPointF(sample.x - sample.tangent_y * straight.y(), sample.y + sample.tangent_x * straight.y());
}

QTransform TextPathLinePlan::glyph_transform(double center_x) const {
  if (!curve) {
    return QTransform();
  }
  const auto sample = curve->sample(start_distance + center_x);
  // Straight (x, y) -> contour point + (x - center_x) * tangent + y * down.
  return QTransform(sample.tangent_x, sample.tangent_y, -sample.tangent_y, sample.tangent_x,
                    sample.x - sample.tangent_x * center_x, sample.y - sample.tangent_y * center_x);
}

QPolygonF TextPathLinePlan::strip(const QRectF& straight) const {
  QPolygonF polygon;
  if (!curve || !(straight.width() > 0.0)) {
    return polygon;
  }
  // A point every two pixels of arc keeps the strip within a hair of the contour.
  const int steps = std::clamp(static_cast<int>(std::ceil(straight.width() / 2.0)), 1, 4096);
  polygon.reserve(2 * (steps + 1));
  for (int i = 0; i <= steps; ++i) {
    const double x = straight.left() + straight.width() * static_cast<double>(i) / steps;
    polygon.push_back(map(QPointF(x, straight.top())));
  }
  for (int i = steps; i >= 0; --i) {
    const double x = straight.left() + straight.width() * static_cast<double>(i) / steps;
    polygon.push_back(map(QPointF(x, straight.bottom())));
  }
  return polygon;
}

QPointF TextPathLinePlan::unmap(QPointF local) const {
  if (!curve) {
    return local;
  }
  const auto nearest = curve->nearest(local.x(), local.y());
  double along = nearest.distance - start_distance;
  if (curve->closed() && along < 0.0) {
    along += curve->length();
  }
  // Past the range on a closed contour: the nearer end wins.
  if (curve->closed() && along > span_length && along - span_length > curve->length() - along) {
    along = 0.0;
  }
  along = std::clamp(along, 0.0, span_length);
  return QPointF(along, nearest.left ? -nearest.gap : nearest.gap);
}

double text_path_layout_width(const VectorPath& path, TextPathRange range, double scale) {
  const TextPathCurve curve(path, scale);
  return std::max(1.0, text_path_span(curve, normalized_text_path_range(path, range)).length);
}

TextPathLinePlan text_path_line_plan(const QTextDocument& document, const VectorPath& path, TextPathRange range,
                                     double scale) {
  TextPathLinePlan plan;
  auto curve = std::make_shared<TextPathCurve>(path, scale);
  if (!curve->valid()) {
    return plan;
  }
  const auto span = text_path_span(*curve, normalized_text_path_range(path, range));
  plan.start_distance = span.start_distance;
  plan.span_length = span.length;
  plan.curve = std::move(curve);
  const auto block = document.begin();
  const auto* layout = block.isValid() ? block.layout() : nullptr;
  if (layout == nullptr || layout->lineCount() <= 0) {
    return plan;
  }
  const auto line = layout->lineAt(0);
  if (!line.isValid()) {
    return plan;
  }
  plan.line.line = line;
  plan.line.block_position = block.position();
  // The line's own x (Qt's alignment inside the layout width) stays; its baseline moves to y 0.
  plan.line.block_origin = QPointF(0.0, -(line.y() + line.ascent()));
  plan.line.clip_rect = QRectF();
  // Trailing white space where the line wraps does not count as hidden text.
  const auto text = block.text();
  int shown = line.textStart() + line.textLength();
  while (shown < text.size() && text.at(shown).isSpace()) {
    ++shown;
  }
  // A paragraph break starts a line the contour cannot hold, so any later paragraph is hidden.
  plan.hidden_lines = shown < text.size() || block.next().isValid();
  plan.valid = true;
  return plan;
}

}  // namespace patchy::ui
