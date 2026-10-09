#include "ui/text_area_layout.hpp"
#include "core/text_area.hpp"

#include <QTextBlock>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextLayout>
#include <QFontMetricsF>
#include <QTextBoundaryFinder>
#include <algorithm>
#include <cmath>
#include <map>

namespace patchy::ui {
namespace {
constexpr int kMaxEdges = 200000;
constexpr int kMaxRows = 100000;

PhotoshopLineMetrics area_line_metrics(const QTextBlock& block, const QTextLine& line, double fraction) {
  auto metrics = photoshop_line_metrics(block, line, fraction);
  double cap = 0.0;
  const auto start = block.position() + line.textStart(), end = start + std::max(1, line.textLength());
  for (auto it = block.begin(); !it.atEnd(); ++it) {
    const auto fragment = it.fragment();
    if (fragment.isValid() && fragment.position() < end && fragment.position() + fragment.length() > start) {
      cap = std::max(cap, QFontMetricsF(fragment.charFormat().font()).capHeight());
    }
  }
  metrics.first_baseline = cap > 0.0 ? cap : QFontMetricsF(block.charFormat().font()).capHeight();
  return metrics;
}

bool flatten(QPolygonF& points, QPointF a, QPointF b, QPointF c, QPointF d, int depth) {
  if (points.size() >= kMaxEdges) return false;
  // Distance to the chord alone misses collinear curves that double back.
  const auto u = b * 3.0 - a * 2.0 - d;
  const auto v = c * 3.0 - d * 2.0 - a;
  if (depth == 18 || std::max(QPointF::dotProduct(u, u), QPointF::dotProduct(v, v)) < 0.0025) {
    points.push_back(d);
    return true;
  }
  const auto ab = (a + b) / 2.0, bc = (b + c) / 2.0, cd = (c + d) / 2.0;
  const auto abc = (ab + bc) / 2.0, bcd = (bc + cd) / 2.0, mid = (abc + bcd) / 2.0;
  return flatten(points, a, ab, abc, mid, depth + 1) && flatten(points, mid, bcd, cd, d, depth + 1);
}
}  // namespace

TextAreaGeometry::TextAreaGeometry(const VectorPath& boundary, double scale) {
  if (!valid_text_area(boundary) || !std::isfinite(scale) || scale <= 0.0) return;
  const auto& anchors = boundary.subpaths.front().anchors;
  polygon_.push_back(QPointF(anchors.front().anchor_x, anchors.front().anchor_y) * scale);
  for (std::size_t i = 0; i < anchors.size(); ++i) {
    const auto& a = anchors[i];
    const auto& b = anchors[(i + 1) % anchors.size()];
    if (!flatten(polygon_, QPointF(a.anchor_x, a.anchor_y) * scale, QPointF(a.out_x, a.out_y) * scale,
                 QPointF(b.in_x, b.in_y) * scale, QPointF(b.anchor_x, b.anchor_y) * scale, 0)) {
      polygon_.clear();
      return;
    }
  }
  bounds_ = polygon_.boundingRect();
  edges_.reserve(static_cast<std::size_t>(polygon_.size() - 1));
  for (qsizetype i = 1; i < polygon_.size(); ++i) {
    auto a = polygon_[i - 1], b = polygon_[i];
    if (a.y() > b.y()) std::swap(a, b);
    edges_.push_back({a,b});
  }
  std::stable_sort(edges_.begin(), edges_.end(), [](const auto& a, const auto& b) { return a.a.y() < b.a.y(); });
  max_y_.resize(edges_.size() * 4);
  const auto build = [&](auto&& self, std::size_t node, std::size_t first, std::size_t last) -> double {
    if (last - first == 1) return max_y_[node] = edges_[first].b.y();
    const auto mid = (first + last) / 2;
    return max_y_[node] = std::max(self(self,node*2,first,mid),self(self,node*2+1,mid,last));
  };
  if (!edges_.empty()) build(build,1,0,edges_.size());
}

void TextAreaGeometry::collect_edges(std::size_t node, std::size_t first, std::size_t last,
                                    double top, double bottom, std::vector<std::size_t>& result) const {
  if (max_y_[node] < top || edges_[first].a.y() > bottom) return;
  if (last - first == 1) { result.push_back(first); return; }
  const auto mid = (first + last) / 2;
  collect_edges(node*2,first,mid,top,bottom,result);
  collect_edges(node*2+1,mid,last,top,bottom,result);
}

bool TextAreaGeometry::contains(QPointF point) const {
  return polygon_.containsPoint(point, Qt::OddEvenFill);
}

std::vector<std::pair<double, double>> TextAreaGeometry::spans(double top, double bottom) const {
  if (!std::isfinite(top) || !std::isfinite(bottom) || top >= bottom ||
      top < bounds_.top() - 1e-7 || bottom > bounds_.bottom() + 1e-7) return {};
  // Intersect the top slice with the complement of every boundary edge swept
  // across this band. A surviving vertical segment is wholly inside the contour,
  // including at concave corners and self-intersections between sample rows.
  const double y = top + std::min(1e-7, (bottom - top) / 4.0);
  std::vector<double> crossings;
  std::vector<std::pair<double, double>> excluded;
  std::vector<std::size_t> active;
  if (!edges_.empty()) collect_edges(1,0,edges_.size(),top,bottom,active);
  for (auto i : active) {
    const auto a = edges_[i].a, b = edges_[i].b;
    const auto x_at = [&](double value) {
      return a.x() + (b.x() - a.x()) * (value - a.y()) / (b.y() - a.y());
    };
    if (a.y() <= y && b.y() > y) crossings.push_back(x_at(y));
    if (b.y() <= top || a.y() >= bottom) continue;
    if (a.y() == b.y()) {
      excluded.emplace_back(std::min(a.x(), b.x()), std::max(a.x(), b.x()));
    } else {
      const auto x0 = x_at(std::max(top, a.y())), x1 = x_at(std::min(bottom, b.y()));
      excluded.emplace_back(std::min(x0, x1), std::max(x0, x1));
    }
  }
  std::sort(crossings.begin(), crossings.end());
  std::sort(excluded.begin(), excluded.end());
  std::vector<std::pair<double, double>> result;
  std::size_t edge = 0;
  for (std::size_t i = 1; i < crossings.size(); i += 2) {
    double left = crossings[i - 1];
    const double right = crossings[i];
    while (edge < excluded.size() && excluded[edge].second <= left) ++edge;
    for (auto j = edge; j < excluded.size() && excluded[j].first < right; ++j) {
      if (excluded[j].first > left + 1e-6) result.emplace_back(left, excluded[j].first);
      left = std::max(left, excluded[j].second);
    }
    if (right > left + 1e-6) result.emplace_back(left, right);
  }
  return result;
}

PhotoshopTextLayoutPlan area_text_layout_plan(const QTextDocument& document, const VectorPath& boundary, double scale) {
  const TextAreaGeometry area(boundary, scale);
  PhotoshopTextLayoutPlan plan;
  // Valid but empty is significant: overflow must not fall back to a rectangle.
  plan.valid = true;
  if (area.bounds().isEmpty()) return plan;
  (void)document.size();  // Finish Qt's initial layout before replacing its block lines.
  double baseline = area.bounds().top();
  double previous_after = 0.0;
  bool first = true;
  int rows = 0;
  for (auto block = document.begin(); block.isValid() && rows < kMaxRows; block = block.next()) {
    auto* layout = block.layout();
    if (!layout) continue;
    const auto format = block.blockFormat();
    const auto stored_fraction = format.property(kTextBlockAutoLeadFractionProperty).toDouble();
    const double fraction = stored_fraction > 0.01 && stored_fraction < 10.0 ? stored_fraction : 1.2;
    auto option = layout->textOption();
    option.setWrapMode(QTextOption::WordWrap);
    option.setAlignment(format.alignment());
    layout->setTextOption(option);
    const auto block_baseline = baseline;
    const auto block_first = first;
    const auto block_after = previous_after;
    const auto block_line_start = plan.lines.size();
    const auto block_text = block.text();
    QTextBoundaryFinder breaks(QTextBoundaryFinder::Line, block_text);
    std::map<int,PhotoshopLineMetrics> row_metrics;
    bool retry = false;
    bool overflow = false;
    do {
      retry = false;
      baseline = block_baseline;
      first = block_first;
      previous_after = block_after;
      plan.lines.resize(block_line_start);
      layout->beginLayout();
      auto line = layout->createLine();
      bool first_in_block = true;
      while (line.isValid() && rows++ < kMaxRows) {
        line.setLineWidth(1.0);
        auto metrics = area_line_metrics(block, line, fraction);
        const auto row_start = line.textStart();
        if (const auto found = row_metrics.find(row_start); found != row_metrics.end()) {
          metrics.first_baseline = std::max(metrics.first_baseline, found->second.first_baseline);
          metrics.leading = std::max(metrics.leading, found->second.leading);
        }
        // Captured at 12/24/48/96 px: the first baseline is the cap height;
        // the flow band covers 90% of that height above the baseline. Descenders
        // may overhang. This admits the first line at an ellipse's curved top.
        double ascent = metrics.first_baseline * 0.9;
        const double before = first_in_block ? std::max(0.0, format.topMargin()) + previous_after : 0.0;
        baseline += before + (first ? metrics.first_baseline : std::max(0.01, metrics.leading));
        first = false;
        first_in_block = false;
        previous_after = 0.0;
        if (baseline > area.bounds().bottom() + 1e-6) break;
        auto spans = area.spans(baseline - ascent, baseline);
        bool used = false;
        for (auto [left, right] : spans) {
          const double indent = layout->lineCount() == 1 ? format.textIndent() : 0.0;
          left += std::max(0.0, format.leftMargin()) + indent;
          right -= std::max(0.0, format.rightMargin());
          if (right <= left) continue;
          line.setLineWidth(right - left);
          auto actual = area_line_metrics(block, line, fraction);
          // The next word may introduce a larger font that moves this row down
          // into a wider part of the contour. Measure that opportunity before
          // fixing the line break (the calibrated mixed-size triangle does this
          // at "two three"). Only adopt it if the larger band's span fits it.
          const int fitted_end = line.textStart() + line.textLength();
          if (fitted_end < block_text.size()) {
            breaks.setPosition(fitted_end);
            const int next_end = breaks.toNextBoundary();
            if (next_end > fitted_end) {
              line.setNumColumns(next_end - line.textStart());
              const auto upcoming = area_line_metrics(block, line, fraction);
              if (upcoming.leading > metrics.leading + 1e-5 ||
                  upcoming.first_baseline > metrics.first_baseline + 1e-5) {
                const double candidate_baseline = baseline + std::max(0.0, upcoming.leading - metrics.leading);
                for (const auto& candidate : area.spans(candidate_baseline - upcoming.first_baseline * .9,
                                                       candidate_baseline)) {
                  if (candidate.first <= left + 1e-6 && candidate.second >= right - 1e-6) {
                    line.setLineWidth(candidate.second - candidate.first - std::max(0.0, format.leftMargin()) -
                                      std::max(0.0, format.rightMargin()) - indent);
                    if (line.textStart() + line.textLength() >= next_end) actual = upcoming;
                    break;
                  }
                }
              }
              line.setLineWidth(right - left);
            }
          }
          // A newly admitted larger run changes both the row advance and its
          // band. Replay this block so every span on the baseline uses the same
          // metrics, including spans already visited. Hints only grow; the total
          // row-work bound includes replays.
          if (actual.first_baseline > metrics.first_baseline + 1e-5 || actual.leading > metrics.leading + 1e-5) {
            row_metrics[row_start] = {std::max(metrics.leading,actual.leading),
                                     std::max(metrics.first_baseline,actual.first_baseline)};
            retry = true;
            break;
          }
          if (line.naturalTextWidth() > right - left + 0.01) continue;
          line.setPosition(QPointF(left, baseline - line.ascent()));
          const auto rect = line.rect();
          plan.lines.push_back({line, {}, rect.adjusted(-2.0, -2.0, 2.0, 2.0), block.position()});
          used = true;
          line = layout->createLine();
          if (!line.isValid()) break;
        }
        if (retry || (!used && metrics.leading < 0.01)) break;
      }
      layout->endLayout();
      overflow = line.isValid();
    } while (retry && rows < kMaxRows);
    if (overflow) break;  // hidden overflow, including following paragraphs
    previous_after = std::max(0.0, format.bottomMargin());
  }
  for (const auto& item : plan.lines) {
    plan.ink_rect = plan.ink_rect.isNull() ? item.line.rect() : plan.ink_rect.united(item.line.rect());
  }
  return plan;
}
VerticalTextLayoutPlan vertical_area_text_layout_plan(const QTextDocument& document,
                                                      const VectorPath& boundary, double scale) {
  // Shape graphemes once with the existing vertical composer. Rotate only the
  // flow geometry: glyph orientation and per-character formatting stay intact.
  (void)document.size();
  for(auto block=document.begin();block.isValid();block=block.next()) {
    auto* layout=block.layout();if(!layout)continue;
    auto option=layout->textOption();option.setWrapMode(QTextOption::NoWrap);layout->setTextOption(option);
    layout->beginLayout();
    auto line=layout->createLine();
    if(line.isValid()) {line.setLineWidth(100000000.0);line.setPosition({0,0});}
    layout->endLayout();
  }
  auto shaped = vertical_text_layout_plan(document, false, 0.0, 0.0);
  auto rotated = boundary;
  transform_vector_path(rotated, {0.0, -scale, scale, 0.0, 0.0, 0.0});
  const TextAreaGeometry flow(rotated);
  VerticalTextLayoutPlan plan;
  plan.valid = true;
  plan.cell_rect = TextAreaGeometry(boundary, scale).bounds();
  double axis = -flow.bounds().top();
  bool first_column = true;
  int count = 0;
  for (const auto& paragraph : shaped.columns) {
    double cap = 0.0;
    for (const auto& cell : paragraph.cells) cap = std::max(cap, QFontMetricsF(cell.format.font()).capHeight());
    cap = std::max(1.0, cap);
    const auto block = document.findBlock(paragraph.block_position);
    QTextBoundaryFinder breaks(QTextBoundaryFinder::Line, block.text());
    if(paragraph.cells.empty()) {
      cap=std::max(1.0,QFontMetricsF(block.charFormat().font()).capHeight());
      axis-=first_column?cap/2.0:std::max(.01,paragraph.leading);
      first_column=false;
      const auto spans=flow.spans(-axis-cap*.45,-axis+cap*.45);
      if(!spans.empty()) {
        auto empty=paragraph;empty.axis=axis;empty.top=spans.front().first;
        plan.columns.push_back(std::move(empty));
      }
      continue;
    }
    std::size_t index = 0;
    while (index < paragraph.cells.size() && count++ < kMaxRows) {
      const double em = paragraph.em;
      axis -= first_column ? cap / 2.0 : std::max(0.01, paragraph.leading);
      first_column = false;
      if (axis - cap * 0.45 < -flow.bounds().bottom()) return plan;
      const auto spans = flow.spans(-axis - cap * 0.45, -axis + cap * 0.45);
      for (const auto& span : spans) {
        // Preserve Unicode line-break opportunities, including CJK. A narrow
        // early column can be skipped without consuming a word that fits later.
        std::size_t end = index;
        double measured = span.first;
        for (auto candidate = index; candidate < paragraph.cells.size(); ++candidate) {
          const auto& cell = paragraph.cells[candidate];
          if (measured + cell.advance > span.second + 0.01) break;
          measured += cell.advance + cell.gap;
          breaks.setPosition(cell.position - paragraph.block_position + cell.length);
          if (breaks.isAtBoundary() || candidate + 1 == paragraph.cells.size()) end = candidate + 1;
        }
        if (end == index) continue;
        VerticalTextColumn column;
        column.line = paragraph.line;
        column.block_position = paragraph.block_position;
        column.em = em;
        column.leading = paragraph.leading;
        column.axis = axis;
        column.top = span.first;
        column.start = paragraph.cells[index].position;
        double y = span.first;
        while (index < end) {
          auto cell = paragraph.cells[index];
          if (y + cell.advance > span.second + 0.01) break;
          cell.baseline += y - cell.top;
          cell.top = y;
          y += cell.advance + cell.gap;
          column.end = cell.position + cell.length;
          column.cells.push_back(std::move(cell));
          ++index;
        }
        if (column.cells.empty()) continue;
        column.cells.back().gap = 0.0;
        const auto alignment = document.findBlock(column.block_position).blockFormat().alignment();
        const double fraction = alignment.testFlag(Qt::AlignHCenter) ? 0.5 : alignment.testFlag(Qt::AlignRight) ? 1.0 : 0.0;
        const double offset = fraction * std::max(0.0, span.second - column.cells.back().top - column.cells.back().advance);
        column.top += offset;
        for (auto& cell : column.cells) { cell.top += offset; cell.baseline += offset; }
        column.last_in_block = index == paragraph.cells.size();
        plan.columns.push_back(std::move(column));
        if (index == paragraph.cells.size()) break;
      }
    }
  }
  return plan;
}
}  // namespace patchy::ui
