#include "ui/text_layout.hpp"

#include <QAbstractTextDocumentLayout>
#include <QFontMetricsF>
#include <QGlyphRun>
#include <QHash>
#include <QLatin1Char>
#include <QLatin1String>
#include <QMutex>
#include <QMutexLocker>
#include <QRawFont>
#include <QString>
#include <QTextBlock>
#include <QTextBoundaryFinder>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextLayout>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

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

namespace {

// Per-line inflation the drawn glyphs need beyond their design boxes: the largest stretch of
// any char format on the line (applied about each glyph origin), half the widest faux-bold
// stroke, and the faux-italic lean of the deepest descender.
struct GlyphInkInflation {
  double stretch{1.0};
  double stroke{0.0};
  double italic_lean{0.0};
};

GlyphInkInflation glyph_ink_inflation(const QTextBlock& block, const QTextLine& line) {
  GlyphInkInflation inflation;
  const auto line_start = block.position() + line.textStart();
  const auto line_end = line_start + std::max(1, line.textLength());
  const auto fold = [&inflation](const QTextCharFormat& format) {
    const auto font = format.font();
    if (font.stretch() > 100) {
      inflation.stretch = std::max(inflation.stretch, font.stretch() / 100.0);
    }
    const auto outline = format.textOutline();
    if (outline.style() != Qt::NoPen && outline.widthF() > 0.0) {
      inflation.stroke = std::max(inflation.stroke, outline.widthF() / 2.0);
    }
    if (format.property(kTextFauxItalicFormatProperty).toBool()) {
      inflation.italic_lean =
          std::max(inflation.italic_lean, kFauxItalicSlant * QFontMetricsF(font).descent());
    }
  };
  bool found = false;
  for (auto it = block.begin(); !it.atEnd(); ++it) {
    const auto fragment = it.fragment();
    if (!fragment.isValid() || fragment.length() <= 0 || fragment.position() + fragment.length() <= line_start ||
        fragment.position() >= line_end) {
      continue;
    }
    fold(fragment.charFormat());
    found = true;
  }
  if (!found) {
    fold(block.charFormat());
  }
  return inflation;
}

}  // namespace

QRectF line_glyph_ink_rect(const QTextBlock& block, const QTextLine& line, QPointF block_origin) {
  if (!line.isValid() || line.textLength() <= 0) {
    return QRectF();
  }
  const auto inflation = glyph_ink_inflation(block, line);
  QRectF ink;
  for (const auto& run : line.glyphRuns()) {
    const auto raw_font = run.rawFont();
    const auto indexes = run.glyphIndexes();
    const auto positions = run.positions();
    for (int index = 0; index < indexes.size() && index < positions.size(); ++index) {
      auto box = raw_font.boundingRect(indexes[index]);
      if (!box.isValid() || box.isEmpty()) {
        continue;
      }
      if (inflation.stretch > 1.0) {
        // Widen about the glyph origin (x 0 of the design box) by the stretch; the engine that
        // already stretched its boxes gets a harmless extra bleed.
        box.setLeft(std::min(box.left(), box.left() * inflation.stretch));
        box.setRight(std::max(box.right(), box.right() * inflation.stretch));
      }
      box.translate(positions[index]);
      ink = ink.isNull() ? box : ink.united(box);
    }
  }
  if (ink.isNull()) {
    return QRectF();
  }
  // CoreText's glyph boxes come back a pixel tighter than its rasterization (the Balmoral LET
  // accent of issue 20 reported as fitting the ascent, then painted its antialiased top row
  // on the buffer edge), so on macOS the measured ink carries one pixel of slack. FreeType and
  // DirectWrite boxes cover their pixels, and their pinned rasters stay byte-identical.
#ifdef Q_OS_MACOS
  constexpr double kEngineBoxSlack = 1.0;
#else
  constexpr double kEngineBoxSlack = 0.0;
#endif
  ink.adjust(-inflation.stroke - inflation.italic_lean - kEngineBoxSlack, -inflation.stroke - kEngineBoxSlack,
             inflation.stroke + kEngineBoxSlack, inflation.stroke + kEngineBoxSlack);
  return ink.translated(block_origin);
}

QRectF line_items_glyph_ink_rect(const QTextDocument& document, const std::vector<BoxTextLineRenderItem>& lines) {
  QRectF ink;
  for (const auto& item : lines) {
    const auto rect = line_glyph_ink_rect(document.findBlock(item.block_position), item.line, item.block_origin);
    if (rect.isNull()) {
      continue;
    }
    ink = ink.isNull() ? rect : ink.united(rect);
  }
  return ink;
}

QRectF document_glyph_ink_rect(const QTextDocument& document) {
  const auto* layout = document.documentLayout();
  if (layout == nullptr) {
    return QRectF();
  }
  QRectF ink;
  for (auto block = document.begin(); block.isValid(); block = block.next()) {
    auto* text_layout = block.layout();
    if (text_layout == nullptr) {
      continue;
    }
    const auto origin = layout->blockBoundingRect(block).topLeft();
    for (int i = 0; i < text_layout->lineCount(); ++i) {
      const auto rect = line_glyph_ink_rect(block, text_layout->lineAt(i), origin);
      if (rect.isNull()) {
        continue;
      }
      ink = ink.isNull() ? rect : ink.united(rect);
    }
  }
  return ink;
}

void VerticalTextLayoutPlan::translate(double dx, double dy) {
  for (auto& column : columns) {
    column.axis += dx;
    column.top += dy;
    for (auto& cell : column.cells) {
      cell.top += dy;
      cell.baseline += dy;
    }
  }
  cell_rect.translate(dx, dy);
  anchor += QPointF(dx, dy);
}

namespace {

struct BlockFormatRange {
  int start{0};  // block-relative
  int end{0};
  QTextCharFormat format;
};

// Char formats of a block by block-relative range; a blank block yields none and callers fall
// back to the block's own char format.
std::vector<BlockFormatRange> block_format_ranges(const QTextBlock& block) {
  std::vector<BlockFormatRange> ranges;
  for (auto fragment_it = block.begin(); !fragment_it.atEnd(); ++fragment_it) {
    const auto fragment = fragment_it.fragment();
    if (!fragment.isValid() || fragment.length() <= 0) {
      continue;
    }
    ranges.push_back(BlockFormatRange{fragment.position() - block.position(),
                                      fragment.position() - block.position() + fragment.length(),
                                      fragment.charFormat()});
  }
  return ranges;
}

const QTextCharFormat& block_format_at(const std::vector<BlockFormatRange>& ranges, int relative,
                                       const QTextCharFormat& fallback) {
  for (const auto& range : ranges) {
    if (relative >= range.start && relative < range.end) {
      return range.format;
    }
  }
  return ranges.empty() ? fallback : ranges.back().format;
}

double vertical_alignment_fraction(Qt::Alignment alignment) {
  if ((alignment & Qt::AlignHCenter) != 0) {
    return 0.5;
  }
  if ((alignment & Qt::AlignRight) != 0) {
    return 1.0;
  }
  return 0.0;
}

// Scripts whose glyphs stand upright in vertical text whatever the run says (Han, kana,
// Hangul, bopomofo, CJK symbols and punctuation, full-width forms).
bool cluster_stays_upright(const QString& text, int start, int end) {
  for (int index = start; index < end && index < text.size(); ++index) {
    const auto ch = text.at(index);
    if (ch.isHighSurrogate() && index + 1 < text.size()) {
      const auto code = QChar::surrogateToUcs4(ch, text.at(index + 1));
      if (code >= 0x20000 && code <= 0x3FFFF) {
        return true;
      }
      ++index;
      continue;
    }
    switch (ch.script()) {
      case QChar::Script_Han:
      case QChar::Script_Hiragana:
      case QChar::Script_Katakana:
      case QChar::Script_Hangul:
      case QChar::Script_Bopomofo:
        return true;
      default:
        break;
    }
    const auto code = ch.unicode();
    if ((code >= 0x3000 && code <= 0x30FF) || (code >= 0x3400 && code <= 0x9FFF) ||
        (code >= 0xAC00 && code <= 0xD7AF) || (code >= 0xF900 && code <= 0xFAFF) ||
        (code >= 0xFF00 && code <= 0xFFEF)) {
      return true;
    }
  }
  return false;
}

bool cluster_is_whitespace(const QString& text, int start, int end) {
  for (int index = start; index < end && index < text.size(); ++index) {
    if (!text.at(index).isSpace()) {
      return false;
    }
  }
  return true;
}

}  // namespace

VerticalTextLayoutPlan vertical_text_layout_plan(const QTextDocument& document, bool boxed, double box_width,
                                                 double box_height) {
  VerticalTextLayoutPlan plan;
  const auto* layout = document.documentLayout();
  if (layout == nullptr) {
    return plan;
  }
  constexpr double kWrapTolerance = 0.01;
  // Columns are built with the first axis at x = 0 and every column's cells starting at y = 0;
  // the alignment offset and the normalisation to a (0, 0) top-left corner are applied after.
  double axis = 0.0;
  double previous_space_after = 0.0;
  bool first_column = true;
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
    const auto alignment_fraction = vertical_alignment_fraction(block_format.alignment());
    const auto text = block.text();
    const auto ranges = block_format_ranges(block);
    const auto fallback_format = block.charFormat();
    const auto space_before = std::max(0.0, block_format.topMargin());
    const auto space_after = std::max(0.0, block_format.bottomMargin());
    bool first_column_of_block = true;
    const auto line_count = text_layout->lineCount();
    if (line_count <= 0) {
      continue;
    }
    for (int line_index = 0; line_index < line_count; ++line_index) {
      const auto line = text_layout->lineAt(line_index);
      if (!line.isValid()) {
        continue;
      }
      const auto line_start = line.textStart();
      const auto line_end = std::min(static_cast<int>(text.size()), line_start + line.textLength());

      // Grapheme clusters of the line: one cell each.
      std::vector<std::pair<int, int>> clusters;
      if (line_end > line_start) {
        QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, text);
        int cluster_start = line_start;
        finder.setPosition(line_start);
        while (cluster_start < line_end) {
          int next = finder.toNextBoundary();
          if (next < 0 || next > line_end) {
            next = line_end;
          }
          if (next <= cluster_start) {
            next = cluster_start + 1;
            finder.setPosition(next);
          }
          clusters.emplace_back(cluster_start, next);
          cluster_start = next;
        }
      }

      std::vector<VerticalTextColumn> line_columns;
      const auto start_column = [&](int first_relative) {
        VerticalTextColumn column;
        column.line = line;
        column.block_position = block.position();
        column.start = block.position() + first_relative;
        column.end = column.start;
        return column;
      };
      const auto finish_column = [&](VerticalTextColumn&& column) {
        if (!column.cells.empty()) {
          column.cells.back().gap = 0.0;
        }
        if (column.em <= 0.0) {
          column.em = photoshop_char_exact_size(fallback_format);
        }
        if (column.leading <= 0.0) {
          column.leading = photoshop_char_leading(fallback_format, paragraph_fraction);
        }
        line_columns.push_back(std::move(column));
      };

      auto column = start_column(line_start);
      double y = 0.0;
      for (const auto& [cluster_start, cluster_end] : clusters) {
        const auto& format = block_format_at(ranges, cluster_start, fallback_format);
        const auto exact_size = photoshop_char_exact_size(format);
        const auto vertical_scale = format.hasProperty(kTextVerticalScaleFormatProperty)
                                        ? std::clamp(format.property(kTextVerticalScaleFormatProperty).toDouble(),
                                                     0.01, 100.0)
                                        : 1.0;
        const auto em = std::max(1.0, exact_size * vertical_scale);
        const auto tracking = format.hasProperty(kTextTrackingFormatProperty)
                                  ? format.property(kTextTrackingFormatProperty).toDouble()
                                  : 0.0;
        const auto gap = std::isfinite(tracking) ? tracking / 1000.0 * exact_size : 0.0;
        const auto font = format.font();
        const auto letter_spacing = font.letterSpacingType() == QFont::AbsoluteSpacing ? font.letterSpacing() : 0.0;
        const auto x0 = line.cursorToX(cluster_start);
        const auto x1 = line.cursorToX(cluster_end);
        const auto glyph_start = std::min(x0, x1);
        const auto glyph_end = std::max(glyph_start, std::max(x0, x1) - letter_spacing);
        const bool whitespace = cluster_is_whitespace(text, cluster_start, cluster_end);
        const bool rotated = !whitespace && format.hasProperty(kTextRotatedRomanFormatProperty) &&
                             format.property(kTextRotatedRomanFormatProperty).toBool() &&
                             !cluster_stays_upright(text, cluster_start, cluster_end);
        const auto advance = whitespace || rotated ? std::max(0.0, glyph_end - glyph_start) : em;
        if (boxed && !column.cells.empty() && y + advance > box_height + kWrapTolerance) {
          finish_column(std::move(column));
          column = start_column(cluster_start);
          y = 0.0;
        }
        const QFontMetricsF metrics(font);
        VerticalTextCell cell;
        cell.position = block.position() + cluster_start;
        cell.length = cluster_end - cluster_start;
        cell.top = y;
        cell.advance = advance;
        cell.gap = gap;
        cell.baseline = y + (em - (metrics.ascent() + metrics.descent())) / 2.0 + metrics.ascent();
        cell.glyph_start = glyph_start;
        cell.glyph_end = glyph_end;
        cell.rotated = rotated;
        cell.format = format;
        column.cells.push_back(std::move(cell));
        column.end = block.position() + cluster_end;
        column.em = std::max(column.em, em);
        column.leading = std::max(column.leading, photoshop_char_leading(format, paragraph_fraction));
        y += advance + gap;
      }
      finish_column(std::move(column));

      for (auto& item : line_columns) {
        if (first_column) {
          axis = boxed ? box_width - item.em / 2.0 : 0.0;
          first_column = false;
        } else {
          const auto block_space_before = first_column_of_block ? space_before : 0.0;
          axis -= std::max(0.01, item.leading) + block_space_before + previous_space_after;
        }
        first_column_of_block = false;
        previous_space_after = 0.0;
        // Alignment along the column: the run's top, middle or bottom sits at the anchor
        // (point) or the run is placed inside the frame height (box).
        double run_height = 0.0;
        if (!item.cells.empty()) {
          run_height = item.cells.back().top + item.cells.back().advance;
        }
        const auto offset = boxed ? alignment_fraction * std::max(0.0, box_height - run_height)
                                  : -alignment_fraction * run_height;
        item.axis = axis;
        item.top = offset;
        for (auto& cell : item.cells) {
          cell.top += offset;
          cell.baseline += offset;
        }
        plan.columns.push_back(std::move(item));
      }
    }
    if (!plan.columns.empty()) {
      plan.columns.back().last_in_block = true;
    }
    previous_space_after = space_after;
  }
  if (plan.columns.empty()) {
    return plan;
  }
  if (boxed) {
    // Whole columns past the frame's left edge stay hidden, the way overflowing lines do.
    std::erase_if(plan.columns, [](const VerticalTextColumn& column) { return column.axis + column.em / 2.0 < 0.0; });
    if (plan.columns.empty()) {
      return plan;
    }
    for (std::size_t index = 0; index + 1 < plan.columns.size(); ++index) {
      if (plan.columns[index].block_position != plan.columns[index + 1].block_position) {
        plan.columns[index].last_in_block = true;
      }
    }
    plan.columns.back().last_in_block = true;
  }
  QRectF cells;
  for (const auto& column : plan.columns) {
    const auto bottom = column.cells.empty() ? column.top + column.em : column.cells.back().top + column.cells.back().advance;
    const QRectF rect(column.axis - column.em / 2.0, column.top, column.em, std::max(1.0, bottom - column.top));
    cells = cells.isNull() ? rect : cells.united(rect);
  }
  if (boxed) {
    plan.cell_rect = QRectF(0.0, 0.0, std::max(1.0, box_width), std::max(1.0, box_height)).united(cells);
    plan.anchor = QPointF(0.0, 0.0);
  } else {
    plan.cell_rect = cells;
    plan.anchor = QPointF(0.0, 0.0);
    plan.translate(-cells.left(), -cells.top());
  }
  plan.valid = true;
  return plan;
}

TextLineGeometry TextLineGeometry::from_vertical_plan(const QTextDocument& document,
                                                      const VerticalTextLayoutPlan& plan) {
  TextLineGeometry geometry;
  geometry.vertical_ = true;
  geometry.maximum_position_ = std::max(0, document.characterCount() - 1);
  geometry.columns_ = plan.columns;
  geometry.vertical_rect_ = plan.cell_rect;
  return geometry;
}

QRectF TextLineGeometry::vertical_caret_rect(int position) const {
  if (columns_.empty()) {
    return {};
  }
  position = std::clamp(position, 0, maximum_position_);
  const VerticalTextColumn* target = nullptr;
  for (const auto& column : columns_) {
    if (position >= column.start && (position < column.end || (position == column.end && column.last_in_block))) {
      target = &column;
      break;
    }
  }
  if (target == nullptr) {
    target = &columns_.back();
  }
  double y = target->top;
  for (const auto& cell : target->cells) {
    if (position < cell.position + cell.length) {
      y = cell.top;
      return QRectF(target->axis - target->em / 2.0, y, std::max(1.0, target->em), 1.0);
    }
    y = cell.top + cell.advance;
  }
  return QRectF(target->axis - target->em / 2.0, y, std::max(1.0, target->em), 1.0);
}

std::vector<QRectF> TextLineGeometry::vertical_selection_rects(int start, int end) const {
  std::vector<QRectF> rects;
  for (const auto& column : columns_) {
    std::optional<double> top;
    double bottom = 0.0;
    for (const auto& cell : column.cells) {
      if (cell.position + cell.length <= start || cell.position >= end) {
        continue;
      }
      if (!top.has_value()) {
        top = cell.top;
      }
      bottom = cell.top + cell.advance;
    }
    if (top.has_value()) {
      rects.push_back(QRectF(column.axis - column.em / 2.0, *top, std::max(1.0, column.em),
                             std::max(1.0, bottom - *top)));
    }
  }
  return rects;
}

int TextLineGeometry::vertical_position_at(QPointF local_point) const {
  if (columns_.empty()) {
    return 0;
  }
  const VerticalTextColumn* best = &columns_.front();
  qreal best_distance = std::numeric_limits<qreal>::max();
  for (const auto& column : columns_) {
    const auto half = std::max(column.em, column.leading) / 2.0;
    const auto left = column.axis - half;
    const auto right = column.axis + half;
    const qreal distance = local_point.x() < left ? left - local_point.x()
                           : local_point.x() > right ? local_point.x() - right
                                                     : 0.0;
    if (distance < best_distance) {
      best_distance = distance;
      best = &column;
      if (distance == 0.0) {
        break;
      }
    }
  }
  for (const auto& cell : best->cells) {
    if (local_point.y() < cell.top + cell.advance / 2.0) {
      return std::clamp(cell.position, 0, maximum_position_);
    }
  }
  return std::clamp(best->end, 0, maximum_position_);
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

QRectF TextLineGeometry::bounding_rect() const {
  if (vertical_) {
    return vertical_rect_;
  }
  QRectF bounds;
  for (const auto& entry : lines_) {
    const auto rect = entry.line.rect().translated(entry.block_origin);
    bounds = bounds.isNull() ? rect : bounds.united(rect);
  }
  return bounds;
}

QRectF TextLineGeometry::caret_rect(int position) const {
  if (vertical_) {
    return vertical_caret_rect(position);
  }
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
  if (vertical_) {
    return vertical_selection_rects(start, end);
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
  if (vertical_) {
    return vertical_position_at(local_point);
  }
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
