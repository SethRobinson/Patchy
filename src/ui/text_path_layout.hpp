#pragma once

// Text along a path (docs/path-text.md): the story's first line, laid out straight by Qt at the
// width of the path range, then set glyph by glyph along the contour. The render pass, the
// caret geometry and the Type tool's hit test all read this one plan.
//
// Straight space: x is the arc length from the range's start point, y is 0 on the baseline and
// grows downward (glyph ink above the baseline has negative y). The plan's line item carries a
// block origin that puts the line there, so TextLineGeometry::from_lines answers in straight
// space and the mapping below bends its answers onto the contour.

#include "core/text_path.hpp"
#include "ui/text_layout.hpp"

#include <QPolygonF>
#include <QTransform>

#include <memory>
#include <vector>

namespace patchy::ui {

struct TextPathLinePlan {
  bool valid{false};
  // The first line of the first paragraph. Its handle lives as long as the document's layout.
  BoxTextLineRenderItem line;
  // More of the story than that line holds (wrapped words, later paragraphs): hidden, kept.
  bool hidden_lines{false};
  // Arc length of the range's start from the contour's first anchor, and the range's length.
  double start_distance{0.0};
  double span_length{0.0};
  std::shared_ptr<const TextPathCurve> curve;

  // A straight-space point bent onto the contour (text-local coordinates times the scale).
  [[nodiscard]] QPointF map(QPointF straight) const;
  // The rigid placement of one glyph: straight space turned about (center_x, 0), which lands
  // on the contour point that far along the range. A glyph drawn through it at its straight
  // position sits tangent to the contour at the middle of its advance.
  [[nodiscard]] QTransform glyph_transform(double center_x) const;
  // A straight rect (an underline bar, a selection) as a strip that follows the contour.
  [[nodiscard]] QPolygonF strip(const QRectF& straight) const;
  // The straight-space point nearest a contour-space one: x from the closest contour point,
  // clamped to the range, y from the signed distance (negative on the text's side).
  [[nodiscard]] QPointF unmap(QPointF local) const;
};

// The layout width a path-text document wraps at: the arc length of the range.
[[nodiscard]] double text_path_layout_width(const VectorPath& path, TextPathRange range, double scale = 1.0);

// `document` must already be laid out at text_path_layout_width (build_text_render_document
// does that for TextToolSettings::path). `scale` multiplies the contour's coordinates.
[[nodiscard]] TextPathLinePlan text_path_line_plan(const QTextDocument& document, const VectorPath& path,
                                                   TextPathRange range, double scale = 1.0);

}  // namespace patchy::ui
