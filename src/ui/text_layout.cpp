#include "ui/text_layout.hpp"

#include <QAbstractTextDocumentLayout>
#include <QFontMetricsF>
#include <QHash>
#include <QLatin1Char>
#include <QLatin1String>
#include <QMutex>
#include <QMutexLocker>
#include <QRawFont>
#include <QString>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QTextLayout>

#include <algorithm>
#include <cmath>
#include <limits>

namespace patchy::ui {

namespace {

constexpr qreal kLineGateTolerance = 0.01;

}  // namespace

double typographic_ascent_fraction(const QFont& font) {
  static QHash<QString, double> cache;
  static QMutex cache_mutex;
  const auto key = font.families().join(QLatin1Char('|')) + QLatin1Char('#') + font.styleName() +
                   QLatin1Char('#') + QString::number(font.weight()) + (font.italic() ? QLatin1String("i") : QLatin1String("r"));
  {
    QMutexLocker lock(&cache_mutex);
    if (const auto found = cache.constFind(key); found != cache.constEnd()) {
      return found.value();
    }
  }
  double fraction = 0.0;
  const auto raw_font = QRawFont::fromFont(font);
  if (raw_font.isValid()) {
    const auto table = raw_font.fontTable("OS/2");
    const auto upem = raw_font.unitsPerEm();
    if (table.size() >= 70 && upem > 0.0) {
      const auto* bytes = reinterpret_cast<const unsigned char*>(table.constData());
      const auto ascender = static_cast<qint16>(static_cast<quint16>((bytes[68] << 8) | bytes[69]));
      if (ascender > 0) {
        fraction = static_cast<double>(ascender) / upem;
      }
    }
  }
  if (fraction <= 0.0 || fraction > 2.0) {
    const QFontMetricsF metrics(font);
    const auto pixel_size = font.pixelSize() > 0 ? static_cast<double>(font.pixelSize())
                                                 : std::max(1.0, metrics.height());
    fraction = std::clamp(metrics.ascent() / pixel_size, 0.5, 1.2);
  }
  QMutexLocker lock(&cache_mutex);
  cache.insert(key, fraction);
  return fraction;
}

double photoshop_char_exact_size(const QTextCharFormat& format) {
  if (format.hasProperty(kTextExactSizeFormatProperty)) {
    const auto exact = format.property(kTextExactSizeFormatProperty).toDouble();
    if (std::isfinite(exact) && exact > 0.0) {
      return exact;
    }
  }
  const auto font = format.font();
  if (font.pixelSize() > 0) {
    return font.pixelSize();
  }
  if (font.pointSizeF() > 0.0) {
    return font.pointSizeF();
  }
  return 12.0;
}

double photoshop_char_leading(const QTextCharFormat& format, double paragraph_fraction) {
  const bool auto_leading = format.hasProperty(kTextAutoLeadingFormatProperty) &&
                            format.property(kTextAutoLeadingFormatProperty).toBool();
  if (!auto_leading && format.hasProperty(kTextLeadingFormatProperty)) {
    const auto fixed = format.property(kTextLeadingFormatProperty).toDouble();
    if (std::isfinite(fixed) && fixed > 0.0) {
      return fixed;
    }
  }
  return paragraph_fraction * photoshop_char_exact_size(format);
}

PhotoshopLineMetrics photoshop_line_metrics(const QTextBlock& block, const QTextLine& line,
                                            double paragraph_fraction) {
  PhotoshopLineMetrics metrics;
  const auto line_start = block.position() + line.textStart();
  const auto line_end = line_start + std::max(1, line.textLength());
  bool found_format = false;
  for (auto fragment_it = block.begin(); !fragment_it.atEnd(); ++fragment_it) {
    const auto fragment = fragment_it.fragment();
    if (!fragment.isValid() || fragment.length() <= 0) {
      continue;
    }
    const auto fragment_start = fragment.position();
    const auto fragment_end = fragment_start + fragment.length();
    if (fragment_end <= line_start || fragment_start >= line_end) {
      continue;
    }
    const auto format = fragment.charFormat();
    metrics.leading = std::max(metrics.leading, photoshop_char_leading(format, paragraph_fraction));
    metrics.first_baseline =
        std::max(metrics.first_baseline, typographic_ascent_fraction(format.font()) * photoshop_char_exact_size(format));
    found_format = true;
  }
  if (!found_format) {
    const auto format = block.charFormat();
    metrics.leading = photoshop_char_leading(format, paragraph_fraction);
    metrics.first_baseline = typographic_ascent_fraction(format.font()) * photoshop_char_exact_size(format);
  }
  return metrics;
}

std::vector<BoxTextLineRenderItem> boxed_text_line_render_items(const QTextDocument& document, QRectF gate_rect,
                                                                qreal top_bleed, qreal bottom_bleed,
                                                                qreal horizontal_bleed) {
  gate_rect = gate_rect.normalized();
  std::vector<BoxTextLineRenderItem> items;
  if (!std::isfinite(gate_rect.left()) || !std::isfinite(gate_rect.top()) ||
      !std::isfinite(gate_rect.right()) || !std::isfinite(gate_rect.bottom()) ||
      gate_rect.width() <= 0.0 || gate_rect.height() <= 0.0) {
    return items;
  }

  const auto* layout = document.documentLayout();
  if (layout == nullptr) {
    return items;
  }

  for (auto block = document.begin(); block.isValid(); block = block.next()) {
    auto* text_layout = block.layout();
    if (text_layout == nullptr) {
      continue;
    }
    const auto block_rect = layout->blockBoundingRect(block);
    for (int i = 0; i < text_layout->lineCount(); ++i) {
      const auto line = text_layout->lineAt(i);
      if (!line.isValid()) {
        continue;
      }
      const auto line_rect = line.rect().translated(block_rect.topLeft());
      const auto line_top = line_rect.top();
      const auto line_bottom = line_rect.bottom();
      if (!std::isfinite(line_top) || !std::isfinite(line_bottom)) {
        continue;
      }
      if (line_top >= gate_rect.bottom() - kLineGateTolerance ||
          line_bottom <= gate_rect.top() - kLineGateTolerance) {
        continue;
      }
      items.push_back(BoxTextLineRenderItem{
          line,
          block_rect.topLeft(),
          QRectF(gate_rect.left() - horizontal_bleed,
                 line_top - top_bleed,
                 gate_rect.width() + horizontal_bleed * 2.0,
                 std::max<qreal>(1.0, line_rect.height() + top_bleed + bottom_bleed)),
          block.position()});
    }
  }
  return items;
}

BoxTextRenderPlan boxed_text_render_plan(const QTextDocument& document, const QFont& font, QRectF frame_rect,
                                         std::optional<QRectF> requested_local_rect) {
  frame_rect = frame_rect.normalized();
  QRectF gate_rect = frame_rect;
  if (requested_local_rect.has_value()) {
    gate_rect = gate_rect.united(requested_local_rect->normalized());
  }

  const QFontMetricsF metrics(font);
  const auto top_bleed = 2.0;
  const auto bottom_bleed =
      std::max<qreal>(2.0, std::ceil(std::max<qreal>(metrics.descent(), metrics.leading())) + 2.0);
  constexpr qreal kHorizontalBleed = 2.0;

  BoxTextRenderPlan plan{gate_rect, boxed_text_line_render_items(document, gate_rect, top_bleed, bottom_bleed,
                                                                 kHorizontalBleed)};
  if (plan.lines.empty()) {
    return plan;
  }
  for (const auto& item : plan.lines) {
    plan.local_rect = plan.local_rect.united(item.clip_rect);
  }
  return plan;
}

PhotoshopTextLayoutPlan photoshop_text_layout_plan(const QTextDocument& document, bool boxed) {
  PhotoshopTextLayoutPlan plan;
  const auto* layout = document.documentLayout();
  if (layout == nullptr) {
    return plan;
  }

  bool first_line = true;
  double baseline = 0.0;
  double previous_space_after = 0.0;
  for (auto block = document.begin(); block.isValid(); block = block.next()) {
    auto* text_layout = block.layout();
    if (text_layout == nullptr) {
      continue;
    }
    const auto block_format = block.blockFormat();
    const auto paragraph_fraction = [&block_format] {
      if (block_format.hasProperty(kTextBlockAutoLeadFractionProperty)) {
        const auto fraction = block_format.property(kTextBlockAutoLeadFractionProperty).toDouble();
        if (std::isfinite(fraction) && fraction > 0.01 && fraction < 10.0) {
          return fraction;
        }
      }
      return 1.2;
    }();
    const auto block_rect = layout->blockBoundingRect(block);
    for (int i = 0; i < text_layout->lineCount(); ++i) {
      const auto line = text_layout->lineAt(i);
      if (!line.isValid()) {
        continue;
      }
      const auto metrics = photoshop_line_metrics(block, line, paragraph_fraction);
      const auto natural_rect = line.rect().translated(block_rect.topLeft());
      if (first_line) {
        if (boxed) {
          // Box text: first baseline = box top + paragraph space-before + typographic ascent.
          baseline = std::max(0.0, block_format.topMargin()) + metrics.first_baseline;
        } else {
          // Point text: keep Qt's own first line so raster anchoring stays put.
          baseline = natural_rect.top() + line.ascent();
        }
        first_line = false;
      } else {
        const auto space_before = i == 0 ? std::max(0.0, block_format.topMargin()) : 0.0;
        baseline += std::max(0.01, metrics.leading) + space_before + previous_space_after;
      }
      previous_space_after =
          i == text_layout->lineCount() - 1 ? std::max(0.0, block_format.bottomMargin()) : 0.0;

      const auto target_top = baseline - line.ascent();
      const auto offset_y = target_top - natural_rect.top();
      const auto block_origin = block_rect.topLeft() + QPointF(0.0, offset_y);
      plan.lines.push_back(BoxTextLineRenderItem{line, block_origin, QRectF(), block.position()});
      plan.ink_rect = plan.ink_rect.isNull() ? natural_rect.translated(0.0, offset_y)
                                             : plan.ink_rect.united(natural_rect.translated(0.0, offset_y));
    }
  }
  plan.valid = !plan.lines.empty();
  return plan;
}

TextLineGeometry TextLineGeometry::build(const QTextDocument& document, bool boxed, bool photoshop_layout) {
  if (photoshop_layout) {
    if (auto plan = photoshop_text_layout_plan(document, boxed); plan.valid) {
      return from_lines(document, plan.lines);
    }
  }

  // Qt-natural layout: the renderer draws through QTextDocument::drawContents, which places
  // every line at its block's own bounding-rect origin. Mirror that exactly.
  std::vector<BoxTextLineRenderItem> natural;
  const auto* layout = document.documentLayout();
  if (layout == nullptr) {
    return {};
  }
  for (auto block = document.begin(); block.isValid(); block = block.next()) {
    auto* text_layout = block.layout();
    if (text_layout == nullptr) {
      continue;
    }
    const auto block_origin = layout->blockBoundingRect(block).topLeft();
    for (int i = 0; i < text_layout->lineCount(); ++i) {
      const auto line = text_layout->lineAt(i);
      if (!line.isValid()) {
        continue;
      }
      natural.push_back(BoxTextLineRenderItem{line, block_origin, QRectF(), block.position()});
    }
  }
  return from_lines(document, natural);
}

TextLineGeometry TextLineGeometry::from_lines(const QTextDocument& document,
                                              const std::vector<BoxTextLineRenderItem>& lines) {
  TextLineGeometry geometry;
  geometry.maximum_position_ = std::max(0, document.characterCount() - 1);
  geometry.lines_.reserve(lines.size());
  for (const auto& item : lines) {
    if (!item.line.isValid()) {
      continue;
    }
    const auto block = document.findBlock(item.block_position);
    geometry.lines_.push_back(Line{item.line, item.block_origin, item.block_position,
                                   block.isValid() ? std::max(1, block.length()) : 1});
  }
  return geometry;
}

QRectF TextLineGeometry::caret_rect(int position) const {
  if (lines_.empty()) {
    return {};
  }
  position = std::clamp(position, 0, maximum_position_);

  // Resolve the owning block FIRST, the way QTextDocument::findBlock does. A block's last
  // line ends before the paragraph separator, so a document-wide scan would answer the
  // previous block's last line for a position sitting at the start of the next block.
  int target_block = lines_.back().block_position;
  for (const auto& entry : lines_) {
    if (position >= entry.block_position && position < entry.block_position + entry.block_length) {
      target_block = entry.block_position;
      break;
    }
  }

  for (std::size_t index = 0; index < lines_.size(); ++index) {
    const auto& entry = lines_[index];
    if (entry.block_position != target_block) {
      continue;
    }
    const auto line_start = entry.block_position + entry.line.textStart();
    const auto line_end = line_start + entry.line.textLength();
    const bool last_line_of_block =
        index + 1 >= lines_.size() || lines_[index + 1].block_position != entry.block_position;
    if (position < line_start || (position > line_end && !last_line_of_block)) {
      continue;
    }
    const auto relative = std::clamp(position - entry.block_position, entry.line.textStart(),
                                     entry.line.textStart() + entry.line.textLength());
    const auto x = entry.line.cursorToX(relative);
    const auto glyph_height = std::max<qreal>(
        1.0, std::ceil(std::max<qreal>(1.0, entry.line.ascent()) + std::max<qreal>(0.0, entry.line.descent())));
    const auto top_padding = std::max<qreal>(0.0, (entry.line.height() - glyph_height) / 2.0);
    return QRectF(entry.block_origin.x() + x, entry.block_origin.y() + entry.line.y() + top_padding, 1.0,
                  glyph_height);
  }
  return {};
}

std::vector<QRectF> TextLineGeometry::selection_rects(int start, int end) const {
  start = std::clamp(start, 0, maximum_position_);
  end = std::clamp(end, 0, maximum_position_);
  if (start > end) {
    std::swap(start, end);
  }
  std::vector<QRectF> rects;
  if (start == end) {
    return rects;
  }
  for (const auto& entry : lines_) {
    const auto line_start = entry.block_position + entry.line.textStart();
    const auto line_end = line_start + entry.line.textLength();
    const auto selected_start = std::max(start, line_start);
    const auto selected_end = std::min(end, line_end);
    if (selected_start >= selected_end) {
      continue;
    }
    const auto start_x = entry.line.cursorToX(selected_start - entry.block_position);
    const auto end_x = entry.line.cursorToX(selected_end - entry.block_position);
    rects.push_back(QRectF(entry.block_origin.x() + std::min(start_x, end_x),
                           entry.block_origin.y() + entry.line.y(),
                           std::max<qreal>(1.0, std::abs(end_x - start_x)), entry.line.height()));
  }
  return rects;
}

int TextLineGeometry::position_at(QPointF local_point) const {
  if (lines_.empty()) {
    return 0;
  }
  const auto* best = &lines_.front();
  qreal best_distance = std::numeric_limits<qreal>::max();
  for (const auto& entry : lines_) {
    const auto top = entry.block_origin.y() + entry.line.y();
    const auto bottom = top + std::max<qreal>(1.0, entry.line.height());
    const qreal distance = local_point.y() < top    ? top - local_point.y()
                           : local_point.y() > bottom ? local_point.y() - bottom
                                                      : 0.0;
    if (distance < best_distance) {
      best_distance = distance;
      best = &entry;
      if (distance == 0.0) {
        break;
      }
    }
  }
  const auto relative = best->line.xToCursor(local_point.x() - best->block_origin.x());
  return std::clamp(best->block_position + relative, 0, maximum_position_);
}

}  // namespace patchy::ui
