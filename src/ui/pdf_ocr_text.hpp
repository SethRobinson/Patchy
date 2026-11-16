#pragma once

// The text of one PDF page as text recognition read it, for the PDF viewer's Find and text
// selection on pages that have no text of their own (docs/pdf-viewer-ocr.md). It answers
// what the viewer otherwise asks PDFium: which character is under a point, and where a
// range of characters sits. Kept in memory for as long as the viewer window is open and
// never written anywhere: not to the PDF, not to a file, not to settings.
//
// Places are in points from the top left of the page as stored (before the view's
// rotation), like every other place the viewer's search and selection hand out.

#include "ocr/ocr_engine.hpp"

#include <QList>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QString>
#include <QTransform>

#include <utility>
#include <vector>

namespace patchy::ui {

// One recognized line: a run of the page's text and the strip it lies in.
struct PdfOcrLine {
  // The line's UTF-16 units in PdfOcrPageText::text, without the line break after it.
  int start{0};
  int count{0};
  // The middles of the edges the line starts and ends at (its reading axis), and its
  // thickness across that axis.
  QPointF from;
  QPointF to;
  double thickness{0.0};
  // count + 1 places along the axis, 0 at `from` and 1 at `to`, never decreasing: where
  // each unit starts and where the last one ends. From the engine's word boxes where it
  // gave them, the units of a word evenly inside it; evenly over the line otherwise. They
  // only place highlights and carets on screen.
  std::vector<double> edges;
  QRectF box;
};

struct PdfOcrPageText {
  // The lines in the engine's reading order, joined with '\n'.
  QString text;
  std::vector<PdfOcrLine> lines;

  [[nodiscard]] bool has_text() const { return !lines.empty(); }
  // The lines' boxes, in order.
  [[nodiscard]] QList<QRectF> line_boxes() const;
  // The caret nearest `point`: an offset into `text`, on the line the point is in or nearest
  // to. -1 without text.
  [[nodiscard]] int caret_at(QPointF point) const;
  // Where text[start, start + count) sits: a rectangle per line it touches.
  [[nodiscard]] QList<QRectF> range_rects(int start, int count) const;
  // The line a point belongs to as [start, start + count); count 0 without text.
  [[nodiscard]] std::pair<int, int> line_range_at(QPointF point) const;
};

// Image pixels of a page rendered at `image_pixels` and shown turned `quarter_turns`
// clockwise (the view's rotation, so the engine reads the page the way the user sees it)
// to points of the page as stored, whose size is `page_points`.
[[nodiscard]] QTransform pdf_ocr_image_to_page(QSize image_pixels, QSizeF page_points, int quarter_turns);

// `result` (in image pixels) as a page's text, through `image_to_page`. Lines without text
// are left out.
[[nodiscard]] PdfOcrPageText pdf_ocr_page_text(const ocr::Result& result, const QTransform& image_to_page);

}  // namespace patchy::ui
