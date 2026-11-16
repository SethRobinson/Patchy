#include "ui/pdf_ocr_text.hpp"

#include <QLineF>

#include <algorithm>
#include <cmath>
#include <limits>

namespace patchy::ui {

namespace {

QPointF middle(const ocr::Point& a, const ocr::Point& b) {
  return {(a.x + b.x) / 2.0, (a.y + b.y) / 2.0};
}

// Where `point` falls along from -> to: 0 at `from`, 1 at `to`, unclamped.
double along(QPointF from, QPointF to, QPointF point) {
  const QPointF axis = to - from;
  const double length_squared = axis.x() * axis.x() + axis.y() * axis.y();
  if (length_squared <= 0.0) {
    return 0.0;
  }
  return ((point.x() - from.x()) * axis.x() + (point.y() - from.y()) * axis.y()) / length_squared;
}

// The strip of a line between two places along its axis, as the rectangle around it.
QRectF strip_rect(const PdfOcrLine& line, double a, double b) {
  const QPointF axis = line.to - line.from;
  const double length = std::hypot(axis.x(), axis.y());
  const QPointF p0 = line.from + axis * a;
  const QPointF p1 = line.from + axis * b;
  // Half the thickness across the axis.
  const QPointF across = length > 0.0 ? QPointF(-axis.y(), axis.x()) * (line.thickness / (2.0 * length))
                                      : QPointF(0.0, line.thickness / 2.0);
  const QPointF corners[] = {p0 + across, p0 - across, p1 + across, p1 - across};
  double left = corners[0].x();
  double right = left;
  double top = corners[0].y();
  double bottom = top;
  for (const QPointF& corner : corners) {
    left = std::min(left, corner.x());
    right = std::max(right, corner.x());
    top = std::min(top, corner.y());
    bottom = std::max(bottom, corner.y());
  }
  return QRectF(QPointF(left, top), QPointF(right, bottom));
}

// The edges of a line's units from the engine's word boxes: each word's units evenly inside
// the word, what lies between two words (a space) over the gap. Empty when the words do not
// spell the line.
std::vector<double> edges_from_words(const QString& text, const ocr::Line& line, const QTransform& image_to_page,
                                     QPointF from, QPointF to) {
  if (line.words.size() < 2) {
    return {};  // one word is the line: even spacing says the same
  }
  std::vector<double> edges(static_cast<std::size_t>(text.size()) + 1U, 0.0);
  qsizetype cursor = 0;
  double reached = 0.0;
  const auto spread = [&edges](qsizetype first, qsizetype last, double a, double b) {
    const auto count = static_cast<double>(last - first);
    for (qsizetype index = first; index < last; ++index) {
      edges[static_cast<std::size_t>(index)] = a + (b - a) * static_cast<double>(index - first) / count;
    }
  };
  for (const auto& word : line.words) {
    const QString word_text = QString::fromUtf8(word.text.data(), static_cast<qsizetype>(word.text.size())).trimmed();
    if (word_text.isEmpty()) {
      continue;
    }
    const qsizetype at = text.indexOf(word_text, cursor);
    if (at < 0) {
      return {};
    }
    const bool vertical = line.vertical;
    const QPointF word_from = image_to_page.map(vertical ? middle(word.quad[0], word.quad[1]) : middle(word.quad[0], word.quad[3]));
    const QPointF word_to = image_to_page.map(vertical ? middle(word.quad[3], word.quad[2]) : middle(word.quad[1], word.quad[2]));
    double a = std::clamp(along(from, to, word_from), 0.0, 1.0);
    double b = std::clamp(along(from, to, word_to), 0.0, 1.0);
    if (b < a) {
      std::swap(a, b);
    }
    a = std::max(a, reached);
    b = std::max(b, a);
    spread(cursor, at, reached, a);
    spread(at, at + word_text.size(), a, b);
    cursor = at + word_text.size();
    reached = b;
  }
  spread(cursor, text.size(), reached, 1.0);
  edges.back() = 1.0;
  return edges;
}

int nearest_line(const std::vector<PdfOcrLine>& lines, QPointF point) {
  int best = -1;
  double best_distance = std::numeric_limits<double>::max();
  for (std::size_t index = 0; index < lines.size(); ++index) {
    const QRectF& box = lines[index].box;
    const double dx = std::max({box.left() - point.x(), 0.0, point.x() - box.right()});
    const double dy = std::max({box.top() - point.y(), 0.0, point.y() - box.bottom()});
    const double distance = std::hypot(dx, dy);
    if (distance < best_distance) {
      best_distance = distance;
      best = static_cast<int>(index);
    }
  }
  return best;
}

}  // namespace

QList<QRectF> PdfOcrPageText::line_boxes() const {
  QList<QRectF> boxes;
  boxes.reserve(static_cast<qsizetype>(lines.size()));
  for (const auto& line : lines) {
    boxes.append(line.box);
  }
  return boxes;
}

int PdfOcrPageText::caret_at(QPointF point) const {
  const int index = nearest_line(lines, point);
  if (index < 0) {
    return -1;
  }
  const PdfOcrLine& line = lines[static_cast<std::size_t>(index)];
  const double place = std::clamp(along(line.from, line.to, point), 0.0, 1.0);
  int caret = 0;
  double best = std::numeric_limits<double>::max();
  for (int unit = 0; unit <= line.count; ++unit) {
    const double distance = std::abs(line.edges[static_cast<std::size_t>(unit)] - place);
    if (distance < best) {
      best = distance;
      caret = unit;
    }
  }
  // Never between the two halves of one character.
  if (caret > 0 && caret < line.count && text[line.start + caret].isLowSurrogate()) {
    ++caret;
  }
  return line.start + caret;
}

QList<QRectF> PdfOcrPageText::range_rects(int start, int count) const {
  QList<QRectF> rects;
  const int end = start + count;
  for (const auto& line : lines) {
    const int a = std::max(start, line.start) - line.start;
    const int b = std::min(end, line.start + line.count) - line.start;
    if (b <= a) {
      continue;
    }
    const QRectF rect =
        strip_rect(line, line.edges[static_cast<std::size_t>(a)], line.edges[static_cast<std::size_t>(b)]);
    if (rect.width() > 0.0 && rect.height() > 0.0) {
      rects.append(rect);
    }
  }
  return rects;
}

std::pair<int, int> PdfOcrPageText::line_range_at(QPointF point) const {
  const int index = nearest_line(lines, point);
  if (index < 0) {
    return {0, 0};
  }
  return {lines[static_cast<std::size_t>(index)].start, lines[static_cast<std::size_t>(index)].count};
}

QTransform pdf_ocr_image_to_page(QSize image_pixels, QSizeF page_points, int quarter_turns) {
  if (image_pixels.isEmpty() || page_points.isEmpty()) {
    return {};
  }
  const int turns = ((quarter_turns % 4) + 4) % 4;
  const double width = page_points.width();
  const double height = page_points.height();
  // The page as shown: turned pages swap their sides.
  const QSizeF shown = turns % 2 == 0 ? page_points : QSizeF(height, width);
  const QTransform to_shown =
      QTransform::fromScale(shown.width() / image_pixels.width(), shown.height() / image_pixels.height());
  // A point (xs, ys) of the page turned clockwise, back on the page as stored (x, y).
  QTransform back;
  switch (turns) {
    case 1:
      back = QTransform(0.0, -1.0, 1.0, 0.0, 0.0, height);  // x = ys, y = height - xs
      break;
    case 2:
      back = QTransform(-1.0, 0.0, 0.0, -1.0, width, height);  // x = width - xs, y = height - ys
      break;
    case 3:
      back = QTransform(0.0, 1.0, -1.0, 0.0, width, 0.0);  // x = width - ys, y = xs
      break;
    default:
      break;
  }
  return to_shown * back;
}

PdfOcrPageText pdf_ocr_page_text(const ocr::Result& result, const QTransform& image_to_page) {
  PdfOcrPageText page;
  for (const auto& found : result.lines) {
    const QString text = QString::fromUtf8(found.text.data(), static_cast<qsizetype>(found.text.size())).simplified();
    if (text.isEmpty()) {
      continue;
    }
    PdfOcrLine line;
    // A level line runs from its left edge to its right one; a column from its top edge down.
    const QPointF from = image_to_page.map(found.vertical ? middle(found.quad[0], found.quad[1])
                                                          : middle(found.quad[0], found.quad[3]));
    const QPointF to = image_to_page.map(found.vertical ? middle(found.quad[3], found.quad[2])
                                                        : middle(found.quad[1], found.quad[2]));
    const QPointF first = image_to_page.map(QPointF(found.quad[0].x, found.quad[0].y));
    const QPointF across = image_to_page.map(found.vertical ? QPointF(found.quad[1].x, found.quad[1].y)
                                                            : QPointF(found.quad[3].x, found.quad[3].y));
    line.from = from;
    line.to = to;
    line.thickness = QLineF(first, across).length();
    if (QLineF(from, to).length() <= 0.0 || line.thickness <= 0.0) {
      continue;
    }
    if (!page.text.isEmpty()) {
      page.text += QLatin1Char('\n');
    }
    line.start = static_cast<int>(page.text.size());
    line.count = static_cast<int>(text.size());
    line.edges = edges_from_words(text, found, image_to_page, from, to);
    if (line.edges.empty()) {
      line.edges.resize(static_cast<std::size_t>(line.count) + 1U);
      for (int unit = 0; unit <= line.count; ++unit) {
        line.edges[static_cast<std::size_t>(unit)] = static_cast<double>(unit) / line.count;
      }
    }
    line.box = strip_rect(line, 0.0, 1.0);
    page.text += text;
    page.lines.push_back(std::move(line));
  }
  return page;
}

}  // namespace patchy::ui
