#include "ui/text_render.hpp"
#include "ui/image_document_io.hpp"
#include "ui/text_font_service.hpp"
#include "ui/text_layout.hpp"
#include "ui/text_area_layout.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/edit_conversions.hpp"
#include "core/pixel_grid.hpp"
#include "core/layer_metadata.hpp"
#include "core/text_area.hpp"
#include "psd/psd_text_runs.hpp"
#include <QApplication>
#include <QAbstractTextDocumentLayout>
#include <QBrush>
#include <QByteArray>
#include <QFontInfo>
#include <QFontMetricsF>
#include <QGlyphRun>
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QRawFont>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextLayout>
#include <QTextOption>
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <utility>

namespace patchy::ui {
bool document_has_faux_italic(const QTextDocument& document);
QTransform faux_italic_shear(double baseline_y);
double dominant_text_run_size(const TextToolSettings& settings, const QString& rich_text_runs);
double dominant_run_width_residual(const QTextDocument& document, double metric_scale);
void grow_point_text_rect_to_glyph_ink(QRectF& local_rect, const QRectF& ink);
void draw_line_glyphs_pixel_aligned(const QTextBlock& block, const BoxTextLineRenderItem& item,
                                    const QTransform& document_transform, QPainter& painter, bool snap = true,
                                    double strong_stroke = 0.0);
void draw_vertical_text_plan(const TextRenderPlan& plan, QPainter& painter);
TextLayoutMetrics text_layout_metrics_for_plan(const TextRenderPlan& plan, const TextToolSettings& settings,
                                               const QString& rich_text_runs);

namespace {

std::int16_t font_table_int16(const QByteArray& table, int offset) {
  if (offset < 0 || offset + 2 > table.size()) {
    return 0;
  }
  const auto high = static_cast<unsigned char>(table[offset]);
  const auto low = static_cast<unsigned char>(table[offset + 1]);
  return static_cast<std::int16_t>(static_cast<std::uint16_t>((high << 8U) | low));
}

std::uint16_t font_table_uint16(const QByteArray& table, int offset) {
  return static_cast<std::uint16_t>(font_table_int16(table, offset));
}

}  // namespace

TextDecorationMetrics text_decoration_metrics(const QFont& font) {
  static QMutex mutex;
  static QHash<QString, TextDecorationMetrics> cache;
  auto unscaled = font;
  unscaled.setPixelSize(1000);
  const auto key = unscaled.key();
  {
    const QMutexLocker locker(&mutex);
    if (const auto found = cache.constFind(key); found != cache.constEnd()) {
      return *found;
    }
  }
  TextDecorationMetrics metrics;
  const QFontMetricsF qt_metrics(unscaled);
  metrics.underline_center = std::max(0.0, qt_metrics.underlinePos()) / 1000.0;
  metrics.thickness = std::max(1.0, qt_metrics.lineWidth()) / 1000.0;
  metrics.strike_center = qt_metrics.xHeight() / 2000.0;
  const auto raw = QRawFont::fromFont(unscaled);
  if (raw.isValid()) {
    const auto head = raw.fontTable("head");
    const auto post = raw.fontTable("post");
    const auto os2 = raw.fontTable("OS/2");
    const double units = head.size() >= 54 ? font_table_uint16(head, 18) : 0.0;
    if (units > 0.0 && post.size() >= 12) {
      const double thickness = std::max<double>(1.0, font_table_int16(post, 10));
      double position = font_table_int16(post, 8);
      if (!raw.fontTable("CFF ").isEmpty() || !raw.fontTable("CFF2").isEmpty()) {
        position -= thickness / 2.0;
      }
      const double lowest = font_table_int16(head, 38);
      metrics.underline_center = std::max(0.0, -(position + lowest) / 2.0) / units;
      metrics.thickness = thickness / units;
    }
    if (units > 0.0 && os2.size() >= 90 && font_table_uint16(os2, 0) >= 2) {
      const auto x_height = font_table_int16(os2, 86);
      if (x_height > 0) {
        metrics.strike_center = x_height / 2.0 / units;
      }
    }
  }
  const QMutexLocker locker(&mutex);
  cache.insert(key, metrics);
  return metrics;
}

namespace {

bool format_has_decoration(const QTextCharFormat& format) {
  return format.property(kTextUnderlineFormatProperty).toBool() ||
         format.property(kTextStrikethroughFormatProperty).toBool();
}

// A fragment drawn glyph run by glyph run even in Qt-natural layout: featured glyphs, or a
// superscript or subscript, whose shift QTextLine::draw would round through Qt's percentage of
// the font height (a subscript landed a pixel high) instead of the exact engine-pixel property.
bool format_needs_glyph_run_draw(const QTextCharFormat& format) {
  // Faux italic shears each run on its own (issue 466), which only the glyph-run draw can do.
  return text_format_has_render_features(format) || format.property(kTextFontBaselineFormatProperty).toInt() != 0 ||
         format.property(kTextFauxItalicFormatProperty).toBool();
}

// Whether any fragment needs the per-line draw path: a bar to place, or glyph-run drawing.
bool document_needs_line_items(const QTextDocument& document) {
  for (auto block = document.begin(); block.isValid(); block = block.next()) {
    for (auto it = block.begin(); !it.atEnd(); ++it) {
      const auto fragment = it.fragment();
      if (!fragment.isValid() || fragment.length() <= 0) {
        continue;
      }
      const auto format = fragment.charFormat();
      if (format_has_decoration(format) || format_needs_glyph_run_draw(format)) {
        return true;
      }
    }
  }
  return false;
}

bool line_has_render_features(const QTextBlock& block, const QTextLine& line) {
  const auto line_start = block.position() + line.textStart();
  const auto line_end = line_start + line.textLength();
  for (auto it = block.begin(); !it.atEnd(); ++it) {
    const auto fragment = it.fragment();
    if (!fragment.isValid() || fragment.length() <= 0 || fragment.position() >= line_end ||
        fragment.position() + fragment.length() <= line_start) {
      continue;
    }
    if (format_needs_glyph_run_draw(fragment.charFormat())) {
      return true;
    }
  }
  return false;
}

// The document's own line placement as plan items (the lines drawContents would draw), for point
// text whose fragments need the per-line path.
std::vector<BoxTextLineRenderItem> natural_line_items(const QTextDocument& document, const QRectF& clip) {
  std::vector<BoxTextLineRenderItem> items;
  const auto* layout = document.documentLayout();
  if (layout == nullptr) {
    return items;
  }
  for (auto block = document.begin(); block.isValid(); block = block.next()) {
    auto* text_layout = block.layout();
    if (text_layout == nullptr) {
      continue;
    }
    const auto origin = layout->blockBoundingRect(block).topLeft();
    for (int i = 0; i < text_layout->lineCount(); ++i) {
      const auto line = text_layout->lineAt(i);
      if (line.isValid()) {
        items.push_back(BoxTextLineRenderItem{line, origin, clip, block.position()});
      }
    }
  }
  return items;
}

// Underline and strikethrough bars for every drawn line: one bar per run segment, at the run's
// own baseline (a superscript's bar rides with it) and in its own font's metrics, from the
// segment's first caret stop to its last, so tracking after the last letter is underlined as
// Photoshop does. Trailing spaces where a box line wraps are left bare. Touching segments of one
// colour and height merge, so no antialiased seam shows between them.
std::vector<TextDecorationRect> text_decorations_for_lines(const QTextDocument& document,
                                                           const std::vector<BoxTextLineRenderItem>& items) {
  std::vector<TextDecorationRect> bars;
  for (const auto& item : items) {
    if (!item.line.isValid() || item.line.textLength() <= 0) {
      continue;
    }
    const auto block = document.findBlock(item.block_position);
    if (!block.isValid()) {
      continue;
    }
    const auto text = block.text();
    const int line_start = item.line.textStart();
    int line_end = line_start + item.line.textLength();
    while (line_end > line_start && line_end <= text.size() && text.at(line_end - 1).isSpace() &&
           block.layout() != nullptr && block.layout()->lineCount() > 1 &&
           item.line.lineNumber() + 1 < block.layout()->lineCount()) {
      --line_end;
    }
    for (auto it = block.begin(); !it.atEnd(); ++it) {
      const auto fragment = it.fragment();
      if (!fragment.isValid() || fragment.length() <= 0) {
        continue;
      }
      const auto format = fragment.charFormat();
      const bool underline = format.property(kTextUnderlineFormatProperty).toBool();
      const bool strike = format.property(kTextStrikethroughFormatProperty).toBool();
      if (!underline && !strike) {
        continue;
      }
      const int from = std::max(fragment.position() - block.position(), line_start);
      const int to = std::min(fragment.position() - block.position() + fragment.length(), line_end);
      if (to <= from) {
        continue;
      }
      auto x0 = item.line.cursorToX(from);
      auto x1 = item.line.cursorToX(to);
      if (x1 < x0) {
        std::swap(x0, x1);
      }
      if (!(x1 - x0 > 0.0)) {
        continue;
      }
      const auto font = format.font();
      const double em = font.pixelSize() > 0 ? font.pixelSize() : std::max(1.0, font.pointSizeF());
      const auto metrics = text_decoration_metrics(font);
      const auto thickness = std::max(0.0, metrics.thickness * em);
      const auto baseline = item.block_origin.y() + item.line.y() + item.line.ascent() -
                            format.property(kTextBaselineShiftFormatProperty).toDouble();
      auto color = format.foreground().color();
      if (!color.isValid()) {
        color = QColor(Qt::black);
      }
      const auto add = [&](double center) {
        const QRectF rect(item.block_origin.x() + x0, center - thickness / 2.0, x1 - x0, thickness);
        if (!bars.empty()) {
          auto& last = bars.back();
          if (last.color == color && std::abs(last.rect.top() - rect.top()) < 1e-6 &&
              std::abs(last.rect.height() - rect.height()) < 1e-6 && std::abs(last.rect.right() - rect.left()) < 0.01) {
            last.rect.setRight(rect.right());
            return;
          }
        }
        bars.push_back(TextDecorationRect{rect, color});
      };
      if (underline) {
        add(baseline + metrics.underline_center * em);
      }
      if (strike) {
        add(baseline - metrics.strike_center * em);
      }
    }
  }
  return bars;
}

// The pen a glyph-path draw strokes with: the format's faux-bold outline, widened by Strong
// anti-aliasing's pen (TextRenderPlan::strong_stroke) when that is set, or Strong's pen alone in
// the run's colour. Qt::NoPen when neither applies, so the glyph images draw as they are.
QPen glyph_outline_pen(const QTextCharFormat& format, const QColor& color, double strong_stroke) {
  auto pen = format.textOutline();
  const bool faux_bold = pen.style() != Qt::NoPen && pen.widthF() > 0.0;
  if (!(strong_stroke > 0.0)) {
    return pen;
  }
  if (faux_bold) {
    pen.setWidthF(pen.widthF() + strong_stroke);
    return pen;
  }
  QPen strong(color, strong_stroke);
  strong.setJoinStyle(Qt::RoundJoin);
  strong.setCapStyle(Qt::RoundCap);
  return strong;
}

// The glyph runs of [from, to) on `line`: Qt's own, or, for a fragment carrying OpenType
// features (kTextRenderFeaturesFormatProperty), the fragment's text shaped with them and moved
// onto the line where Qt's first glyph of the range starts.
QList<QGlyphRun> fragment_glyph_runs(const QTextBlock& block, const QTextLine& line, int from, int to,
                                     const QTextCharFormat& format) {
  auto plain = line.glyphRuns(from - block.position(), to - from);
  if (!text_format_has_render_features(format) || plain.isEmpty() || plain.front().positions().isEmpty()) {
    return plain;
  }
  QTextLayout layout(block.text().mid(from - block.position(), to - from), text_render_featured_font(format));
  QTextOption option;
  option.setWrapMode(QTextOption::NoWrap);
  option.setUseDesignMetrics(true);
  layout.setTextOption(option);
  layout.beginLayout();
  const auto featured_line = layout.createLine();
  layout.endLayout();
  if (!featured_line.isValid()) {
    return plain;
  }
  auto featured = featured_line.glyphRuns();
  if (featured.isEmpty() || featured.front().positions().isEmpty()) {
    return plain;
  }
  const auto offset = plain.front().positions().front() - featured.front().positions().front();
  for (auto& run : featured) {
    auto positions = run.positions();
    for (auto& position : positions) {
      position += offset;
    }
    run.setPositions(positions);
  }
  return featured;
}

}  // namespace

// Photoshop's faux bold, applied to every run that carries kTextFauxBoldFormatProperty: widen
// each advance by kFauxBoldAdvanceEmFraction of the em and stroke the glyph outlines with a pen
// as wide, at most kFauxBoldMaxStrokePixels. That is what Photoshop does -- it thickens the run's OWN face rather than
// switching to the family's real bold face, which is a different, wider typeface.
//
// Applied here rather than when the runs are parsed so it follows the format's current colour
// and pixel size, and so both the raster pass and the caret layout (which share this function)
// see the identical advances.
bool document_has_faux_italic(const QTextDocument& document) {
  for (auto block = document.begin(); block.isValid(); block = block.next()) {
    for (auto it = block.begin(); !it.atEnd(); ++it) {
      const auto fragment = it.fragment();
      if (fragment.isValid() && fragment.length() > 0 &&
          fragment.charFormat().property(kTextFauxItalicFormatProperty).toBool()) {
        return true;
      }
    }
  }
  return false;
}

// Slant about a baseline: a point ON the baseline stays put and everything above it moves right,
// which is what a synthetic italic is. Advances are untouched, exactly as Photoshop leaves them.
QTransform faux_italic_shear(double baseline_y) {
  return QTransform(1.0, 0.0, -kFauxItalicSlant, 1.0, kFauxItalicSlant * baseline_y, 0.0);
}

// The largest glyph size any run asks for, in the runs' own (raw, unscaled) units and already
// including that run's vertical glyph scale -- the value apply_patchy_text_runs_to_document
// rounds to a whole pixel. Falls back to the layer-level size for text with no runs.
double dominant_text_run_size(const TextToolSettings& settings, const QString& rich_text_runs) {
  double dominant = 0.0;
  for (const auto& raw_line : rich_text_runs.split(QLatin1Char('\n'))) {
    const auto fields = raw_line.trimmed().split(QLatin1Char('\t'));
    if (fields.size() < 7) {
      continue;  // version header / malformed line
    }
    bool size_ok = false;
    const auto size = fields[2].toDouble(&size_ok);
    if (!size_ok || !std::isfinite(size) || size <= 0.0) {
      continue;
    }
    double vertical_scale = 1.0;
    if (fields.size() >= 11) {
      bool scale_ok = false;
      const auto value = fields[10].toDouble(&scale_ok);
      if (scale_ok && std::isfinite(value) && value > 0.01 && value < 100.0) {
        vertical_scale = value;
      }
    }
    dominant = std::max(dominant, size * vertical_scale);
  }
  return dominant > 0.0 ? dominant : static_cast<double>(std::max(1, settings.size));
}

// The width Photoshop's HorizontalScale asks for over what the dominant run's font really
// renders. QFont::setStretch is a whole percent, so H/V 0.95/0.93 (102.15%) renders at 102% and
// the 1080 px "ethode" of issue 20 came out a pixel narrow at its far edge. The ratio is
// MEASURED on the font (set_stretch_for_advance_ratio may have picked a sqrt-encoded value on
// CoreText, and a fallback engine may ignore stretch altogether), so the residual is exactly
// what the matrix still has to supply. The dominant run is the largest pixel size, the same
// choice dominant_text_run_size makes for the vertical fold.
//
// metric_scale is the session's advance calibration (calibrated_box_text_metric_scale), which
// build_text_render_document already folded into every font's stretch. It is part of the width
// the render WANTS, not a shortfall: left out, the residual read the narrowing as lost stretch
// and widened the glyphs back through the matrix, so calibrated box text wrapped at the narrow
// advances and then drew 1 / metric_scale wider than its frame (tips.psd: 716 px of ink in a
// 574 px box), and every re-edit grew the render rect by the same factor again.
double dominant_run_width_residual(const QTextDocument& document, double metric_scale) {
  double best_size = 0.0;
  QTextCharFormat best;
  bool found = false;
  for (auto block = document.begin(); block.isValid(); block = block.next()) {
    for (auto it = block.begin(); !it.atEnd(); ++it) {
      const auto fragment = it.fragment();
      if (!fragment.isValid() || fragment.length() <= 0) {
        continue;
      }
      const auto format = fragment.charFormat();
      const auto font = format.font();
      const auto size = std::max(static_cast<double>(font.pixelSize()), font.pointSizeF());
      if (size > best_size) {
        best_size = size;
        best = format;
        found = true;
      }
    }
  }
  if (!found) {
    return 1.0;
  }
  const auto scale_property = [&best](int property_id) {
    if (best.hasProperty(property_id)) {
      const auto value = best.property(property_id).toDouble();
      if (std::isfinite(value) && value > 0.01 && value < 100.0) {
        return value;
      }
    }
    return 1.0;
  };
  metric_scale = std::clamp(std::isfinite(metric_scale) ? metric_scale : 1.0, 0.5, 1.5);
  const auto wanted = scale_property(kTextHorizontalScaleFormatProperty) /
                      scale_property(kTextVerticalScaleFormatProperty) * metric_scale;
  QFont stretched = best.font();
  stretched.setLetterSpacing(QFont::AbsoluteSpacing, 0.0);
  // The face at its OWN width is QFont::AnyStretch, never stretch 100: Qt synthesizes
  // request / face-width-class, so 100 on a Condensed face (width class 75, Futura Bold
  // Condensed BT) is a 133% stretch. Measuring that as the reference reported a 4/3 residual
  // for a font with no stretch at all and drew Title02's WWW.COCKPITMASTER.COM a third wider
  // than the caret layout (Seth's "the letter appears at the end of the string" report).
  QFont unstretched = stretched;
  unstretched.setStretch(QFont::AnyStretch);
  const auto reference = QFontMetricsF(unstretched).horizontalAdvance(QLatin1Char('H'));
  const auto current = QFontMetricsF(stretched).horizontalAdvance(QLatin1Char('H'));
  if (!(reference > 0.0) || !(current > 0.0) || !std::isfinite(wanted) || wanted <= 0.0) {
    return 1.0;
  }
  const auto residual = wanted / (current / reference);
  return std::isfinite(residual) && residual > 0.5 && residual < 2.0 ? residual : 1.0;
}

VerticalRenderPlan vertical_render_plan(const QTextDocument& document, const TextToolSettings& settings,
                                        double box_scale, double size_scale) {
  VerticalRenderPlan result;
  box_scale = std::isfinite(box_scale) && box_scale > 0.01 ? box_scale : 1.0;
  size_scale = std::isfinite(size_scale) && size_scale > 0.01 ? size_scale : 1.0;
  const auto box_width = static_cast<double>(std::max(kMinimumTextBoxDocumentSize,
                                                      static_cast<int>(std::lround(settings.box_width * box_scale))));
  const auto box_height = static_cast<double>(std::max(kMinimumTextBoxDocumentSize,
                                                       static_cast<int>(std::lround(settings.box_height * box_scale))));
  result.plan = settings.area ? vertical_area_text_layout_plan(document, *settings.area, box_scale)
                              : vertical_text_layout_plan(document, settings.boxed, box_width, box_height);
  result.bleed = psd::vertical_text_bleed_for_size(std::max(1, settings.size) * size_scale);
  const auto bleed = result.bleed;
  if (settings.area) {
    result.local_rect = result.plan.cell_rect.adjusted(-bleed,-bleed,bleed,bleed);
    if (result.local_rect.width() * result.local_rect.height() > 16777216.0) {
      QRectF cells;
      for (const auto& column : result.plan.columns) {
        const double bottom = column.cells.empty() ? column.top + column.em
            : column.cells.back().top + column.cells.back().advance;
        const QRectF rect(column.axis-column.em/2.0, column.top, column.em, std::max(1.0,bottom-column.top));
        cells = cells.isNull() ? rect : cells.united(rect);
      }
      result.local_rect = cells.isEmpty() ? QRectF(result.plan.cell_rect.topLeft(), QSizeF(1.0,1.0))
                                          : cells.adjusted(-bleed,-bleed,bleed,bleed);
    }
  } else if (settings.boxed) {
    // The buffer keeps the frame's origin and size and grows only where cells overhang, the
    // rule the horizontal boxed paths follow for pixels-only callers.
    result.local_rect = QRectF(0.0, 0.0, std::max(box_width, result.plan.cell_rect.right() + bleed),
                               std::max(box_height, result.plan.cell_rect.bottom() + bleed));
  } else {
    result.plan.translate(bleed, bleed);
    result.local_rect = QRectF(0.0, 0.0, std::max(1.0, result.plan.cell_rect.right() + bleed),
                               std::max(1.0, result.plan.cell_rect.bottom() + bleed));
  }
  return result;
}

// Photoshop rasterizes every glyph whole; Qt's line rect is the ADVANCE box. Ink outside it --
// a negative left side bearing (script and italic faces), ink past the last advance, an accent or
// swash above Qt's ascent -- used to be cut off at the buffer edge, and the imported-layer
// anchoring, which pins ink to ink, then moved the whole line by the clipped amount (issue 20:
// Balmoral LET's "é" lost 9 px on the left and the text slid left by the same 9 px on every
// unchanged commit). Grow the buffer around the ORIGIN only where the measured ink exceeds it:
// local (0, 0) stays the line start / first line top, so a raster whose ink fits keeps its exact
// pinned bytes, and one that overhangs starts left of or above the origin. Every placement adds
// local_rect.topLeft() to the transform translation (docs/text-tool.md).
void grow_point_text_rect_to_glyph_ink(QRectF& local_rect, const QRectF& ink) {
  if (ink.isNull() || !std::isfinite(ink.left()) || !std::isfinite(ink.top()) || !std::isfinite(ink.right()) ||
      !std::isfinite(ink.bottom())) {
    return;
  }
  constexpr qreal kInkBleed = 2.0;
  auto left = local_rect.left();
  auto top = local_rect.top();
  auto right = local_rect.right();
  auto bottom = local_rect.bottom();
  if (ink.left() < left) {
    left = std::floor(ink.left()) - kInkBleed;
  }
  if (ink.top() < top) {
    top = std::floor(ink.top()) - kInkBleed;
  }
  if (ink.right() > right) {
    right = std::ceil(ink.right()) + kInkBleed;
  }
  if (ink.bottom() > bottom) {
    bottom = std::ceil(ink.bottom()) + kInkBleed;
  }
  local_rect = QRectF(QPointF(left, top), QPointF(right, bottom));
}

TextRenderPlan build_text_render_plan(const TextToolSettings& settings, QColor color, std::int32_t max_width,
                                      const QString& paragraph_runs, const QString& rich_text_runs,
                                      std::optional<QRectF> requested_local_rect, double metric_scale,
                                      const QTransform& document_transform_in, double layout_scale_in) {
  // Photoshop-layout text rendered through a scaling transform folds the transform's vertical
  // scale into the glyph sizes (fractional engine sizes round to whole pixels AFTER scaling,
  // not before) and renders through the residual matrix. The returned rect is post-transform
  // either way, so callers see identical geometry with crisper, correctly-sized glyphs.
  // layout_scale_in additionally scales run sizes WITHOUT scaling box dims -- a PSD-frame box
  // session works in a document-space frame while its runs stay in raw engine units.
  //
  // Photoshop rasterizes a type layer with each LINE START (the anchor minus the justification
  // offset, in document space) rounded to a whole pixel, halves up: for left-aligned text that is
  // the anchor itself (tx 100.3 renders exactly like 100.0 and 100.5 like 101.0), while a centered
  // line moves a column for the same .3 fraction because its start sits half a line width to the
  // left of the anchor (PS 27.9 captures, docs/text-render-calibration.md, "Pixel grid"). The
  // stored transform keeps its fraction; only the raster phase snaps. Axis-aligned transforms
  // round per line below, once the lines are known; rotated or sheared ones keep the anchor
  // rounding, the only rule those captures established. The baseline (dy) always rounds here.
  const bool round_line_starts = !document_transform_in.isIdentity() && document_transform_in.m12() == 0.0 &&
                                 document_transform_in.m21() == 0.0 && std::abs(document_transform_in.m11()) > 1e-9;
  QTransform document_transform(document_transform_in.m11(), document_transform_in.m12(),
                                document_transform_in.m21(), document_transform_in.m22(),
                                round_line_starts ? document_transform_in.dx()
                                                  : snap_to_pixel_grid(document_transform_in.dx()),
                                snap_to_pixel_grid(document_transform_in.dy()));
  double fold_scale = 1.0;
  if (settings.photoshop_layout && !settings.area) {
    const auto vertical_scale =
        document_transform_in.isIdentity() ? 1.0 : std::hypot(document_transform_in.m21(), document_transform_in.m22());
    if (std::isfinite(vertical_scale) && vertical_scale > 0.01) {
      fold_scale = vertical_scale;
      // Fold only as far as a WHOLE pixel size and leave the remainder in the matrix. QFont
      // takes an integer pixel size and Qt quantizes a fractional point size to the same whole
      // pixel, so folding the full scale silently rounds the glyphs off Photoshop's size:
      // 18 x 0.9 = 16.2 became 16 (~1.2% narrow, which is the Dungeon Scroll buttons shifting a
      // pixel or two on edit) while 14.44444 x 0.9 = 13.0 was already whole and never moved.
      // The leftover ~1.0125 rides in document_transform, which the glyph rasterizer applies
      // exactly -- these lines are drawn THROUGH the matrix, not resampled after the fact.
      // The same applies with no scaling transform at all: FontSize x VerticalScale is
      // fractional as often as not (1086.61 x 0.93 = 1010.55 in issue 20's file), and the
      // fraction it rounds away is a size error the matrix takes back.
      const auto exact_dominant_size =
          dominant_text_run_size(settings, rich_text_runs) * fold_scale *
          (std::isfinite(layout_scale_in) && layout_scale_in > 0.01 ? layout_scale_in : 1.0);
      const auto whole_dominant_size = std::round(exact_dominant_size);
      if (whole_dominant_size >= 1.0 && std::abs(exact_dominant_size - whole_dominant_size) > 0.0001) {
        fold_scale *= whole_dominant_size / exact_dominant_size;
      }
      if (std::abs(fold_scale - 1.0) > 1e-9) {
        document_transform =
            QTransform(document_transform_in.m11() / fold_scale, document_transform_in.m12() / fold_scale,
                       document_transform_in.m21() / fold_scale, document_transform_in.m22() / fold_scale,
                       document_transform.dx(), document_transform.dy());
      } else {
        fold_scale = 1.0;
      }
    }
  }
  const double layout_scale =
      fold_scale * (std::isfinite(layout_scale_in) && layout_scale_in > 0.01 ? layout_scale_in : 1.0);
  TextRenderPlan result;
  result.area = settings.area.has_value();
  result.built = build_text_render_document(settings, color, max_width, paragraph_runs, rich_text_runs,
                                            metric_scale, layout_scale, fold_scale);
  auto& document = *result.built.document;
  const auto& font = result.built.font;
  const auto text_width = result.built.text_width;
  if (settings.photoshop_layout && !settings.vertical) {
    // Whatever width the whole-percent stretch could not express scales the local x axis
    // (dominant_run_width_residual), applied before the document transform so rotation and
    // shear still act on the corrected advances.
    const auto width_residual = dominant_run_width_residual(document, metric_scale);
    if (std::abs(width_residual - 1.0) > 1e-6) {
      document_transform = QTransform::fromScale(width_residual, 1.0) * document_transform;
    }
  }

  const auto size = document.size();
  QRectF local_rect;
  std::vector<BoxTextLineRenderItem> line_render_items;
  PhotoshopTextLayoutPlan photoshop_plan;
  if (settings.photoshop_layout && !settings.vertical && !settings.area) {
    photoshop_plan = photoshop_text_layout_plan(document, settings.boxed);
  }
  if (settings.vertical) {
    auto vertical = vertical_render_plan(document, settings, fold_scale, layout_scale);
    local_rect = vertical.local_rect;
    result.vertical = true;
    result.vertical_plan = std::move(vertical.plan);
  } else if (settings.area) {
    auto area_plan = area_text_layout_plan(document, *settings.area, fold_scale);
    const TextAreaGeometry geometry(*settings.area, fold_scale);
    local_rect = geometry.bounds();
    line_render_items = std::move(area_plan.lines);
    // A sparse story in an enormous contour must not allocate a contour-sized
    // bitmap. The frame stays in metadata; only visible glyphs need pixels.
    if (local_rect.width() * local_rect.height() > 16777216.0) {
      const auto ink = line_items_glyph_ink_rect(document, line_render_items);
      local_rect = ink.isEmpty() ? QRectF(local_rect.topLeft(), QSizeF(1.0, 1.0))
                                : ink.adjusted(-2.0, -2.0, 2.0, 2.0);
    }
    grow_point_text_rect_to_glyph_ink(local_rect, line_items_glyph_ink_rect(document, line_render_items));
    for (auto& item : line_render_items) item.clip_rect = local_rect;
  } else if (settings.boxed) {
    local_rect = QRectF(0.0, 0.0, static_cast<qreal>(text_width),
                        static_cast<qreal>(std::max(
                            kMinimumTextBoxDocumentSize,
                            static_cast<int>(std::lround(settings.box_height * fold_scale)))));
    if (photoshop_plan.valid) {
      // Photoshop-model line positions, gated and clipped with the same bleed rules as the
      // native boxed plan (descenders paint past the line box; neighbours stay clipped).
      auto gate_rect = local_rect;
      if (requested_local_rect.has_value()) {
        gate_rect = gate_rect.united(requested_local_rect->normalized());
      }
      const QFontMetricsF metrics(font);
      const qreal top_bleed = 2.0;
      const qreal bottom_bleed =
          std::max<qreal>(2.0, std::ceil(std::max<qreal>(metrics.descent(), metrics.leading())) + 2.0);
      constexpr qreal kHorizontalBleed = 2.0;
      constexpr qreal kLineGateTolerance = 0.01;
      auto united_rect = gate_rect;
      for (auto& item : photoshop_plan.lines) {
        const auto positioned = item.line.rect().translated(item.block_origin);
        if (positioned.top() >= gate_rect.bottom() - kLineGateTolerance ||
            positioned.bottom() <= gate_rect.top() - kLineGateTolerance) {
          continue;
        }
        item.clip_rect = QRectF(gate_rect.left() - kHorizontalBleed, positioned.top() - top_bleed,
                                gate_rect.width() + kHorizontalBleed * 2.0,
                                std::max<qreal>(1.0, positioned.height() + top_bleed + bottom_bleed));
        united_rect = united_rect.united(item.clip_rect);
        line_render_items.push_back(item);
      }
      local_rect = united_rect;
    } else if (requested_local_rect.has_value()) {
      auto plan = boxed_text_render_plan(document, font, local_rect, requested_local_rect);
      local_rect = plan.local_rect;
      line_render_items = std::move(plan.lines);
    } else {
      // Metadata re-renders (Affinity import, reopened documents) must gate lines the same
      // way as the editor and Photoshop-layout paths: a line straddling the frame bottom
      // draws whole (Affinity and Photoshop both do), never a mid-glyph raster cut. Callers
      // of the pixels-only wrapper place the buffer at the frame corner, so the origin and
      // width stay the frame's; only the bottom may grow to hold the straddler's bleed.
      auto plan = boxed_text_render_plan(document, font, local_rect, std::nullopt);
      line_render_items = std::move(plan.lines);
      local_rect.setBottom(std::max(local_rect.bottom(), plan.local_rect.bottom()));
    }
  } else if (photoshop_plan.valid) {
    // Point text under the Photoshop leading model: width still comes from the document's
    // ideal width, height and top from the repositioned lines (tight leading can push a
    // later line's ascender above the first line's top).
    const auto ink = photoshop_plan.ink_rect;
    const auto top = std::min<qreal>(0.0, std::floor(ink.top()));
    const auto bottom = std::max<qreal>(top + 1.0, std::ceil(ink.bottom()) + 2.0);
    local_rect = QRectF(0.0, top, std::max<qreal>(1.0, std::ceil(size.width()) + 2.0), bottom - top);
    line_render_items = std::move(photoshop_plan.lines);
    grow_point_text_rect_to_glyph_ink(local_rect, line_items_glyph_ink_rect(document, line_render_items));
    for (auto& item : line_render_items) {
      item.clip_rect = local_rect;
    }
  } else {
    local_rect = QRectF(0.0, 0.0, std::max<qreal>(1.0, std::ceil(size.width()) + 2.0),
                        std::max<qreal>(1.0, std::ceil(size.height()) + 2.0));
    grow_point_text_rect_to_glyph_ink(local_rect, document_glyph_ink_rect(document));
    if (!settings.vertical && (document_needs_line_items(document) || settings.anti_alias == kTextAntiAliasStrong)) {
      // drawContents cannot place bars or featured glyphs, nor stroke Strong's outlines; the same
      // lines, drawn one by one, can.
      line_render_items = natural_line_items(document, local_rect);
    }
  }
  if (!settings.vertical && !line_render_items.empty()) {
    result.decorations = text_decorations_for_lines(document, line_render_items);
    for (const auto& bar : result.decorations) {
      if (settings.boxed) {
        // The frame keeps its origin and width (pixels-only callers place the buffer at the
        // frame corner); a bar below the last line only grows the bottom.
        local_rect.setBottom(std::max(local_rect.bottom(), std::ceil(bar.rect.bottom()) + 1.0));
      } else {
        local_rect = local_rect.united(bar.rect.adjusted(-1.0, -1.0, 1.0, 1.0));
      }
    }
  }
  if (!std::isfinite(local_rect.left()) || !std::isfinite(local_rect.top()) ||
      !std::isfinite(local_rect.right()) || !std::isfinite(local_rect.bottom()) ||
      local_rect.width() <= 0.0 || local_rect.height() <= 0.0) {
    local_rect = QRectF(0.0, 0.0, static_cast<qreal>(std::max(1, text_width)),
                        static_cast<qreal>(std::max(kMinimumTextBoxDocumentSize, settings.box_height)));
  }
  // Faux italic leans the glyph tops to the RIGHT of where the unsheared layout put them, so the
  // buffer (and every line's clip band) has to grow by the lean or the tallest ascenders are cut
  // off at the old right edge. Advances are unchanged, which is why only the bleed moves.
  const bool faux_italic_render = document_has_faux_italic(document);
  if (faux_italic_render) {
    const auto lean = kFauxItalicSlant * QFontMetricsF(font).ascent() + 2.0;
    local_rect.setRight(local_rect.right() + lean);
    for (auto& item : line_render_items) {
      item.clip_rect.setRight(item.clip_rect.right() + lean);
    }
  }
  // Photoshop rounds every glyph's absolute x from the UNROUNDED line start (the g of "Hg" at
  // 100.5 + 34.67 = 135.17 lands on 135, not on 100.5 -> 101 + 34.67 = 135.67 -> 136), so a plan
  // whose glyphs snap individually at draw time keeps its fractional line starts; the first
  // glyph's own rounding is the line-start rule.
  const bool pixel_align_glyphs = settings.photoshop_layout && !settings.vertical && !line_render_items.empty() &&
                                  document_transform.m12() == 0.0 && document_transform.m21() == 0.0 &&
                                  document_transform.m11() > 0.0 && document_transform.m22() > 0.0;
  if (round_line_starts && !pixel_align_glyphs) {
    if (line_render_items.empty() || settings.vertical) {
      // No per-line plan to adjust (vertical type, drawContents fallback): the anchor rounds.
      document_transform =
          QTransform(document_transform.m11(), document_transform.m12(), document_transform.m21(),
                     document_transform.m22(), snap_to_pixel_grid(document_transform_in.dx()), document_transform.dy());
    } else {
      // Each line's start (its leftmost glyph origin) lands on a whole document pixel; the local
      // shift is at most half a pixel over m11, well inside the buffer's bleed.
      for (auto& item : line_render_items) {
        const auto start_x = document_transform.map(QPointF(item.block_origin.x() + item.line.x(), 0.0)).x();
        const auto delta = snap_to_pixel_grid(start_x) - start_x;
        item.block_origin.rx() += delta / document_transform.m11();
      }
    }
  }
  result.local_rect = local_rect;
  result.pixel_align_glyphs = pixel_align_glyphs;
  result.line_render_items = std::move(line_render_items);
  result.document_transform = document_transform;
  result.has_document_transform = !document_transform.isIdentity();
  result.faux_italic_render = faux_italic_render;
  if (settings.anti_alias == kTextAntiAliasStrong) {
    // The pen is measured in document pixels, so it shrinks by the transform's area scale (a
    // 2x scaled layer strokes 0.125 local units, which the matrix draws 0.25 px wide).
    const auto area_scale = std::sqrt(std::abs(document_transform.determinant()));
    result.strong_stroke =
        kStrongAntiAliasStrokePixels / (std::isfinite(area_scale) && area_scale > 1e-6 ? area_scale : 1.0);
  }
  return result;
}

// Photoshop rasterizes each glyph with its absolute x rounded to a whole document pixel,
// halves up, glyph by glyph rather than once per line: in the PS 27.9 anchor captures "Hg" at
// x 100.5 moves the H a column while the g (100.5 + 34.67 = 135.17 -> 135) stays where the
// 100.0 capture had it (docs/text-render-calibration.md, "Pixel grid"). QTextLine::draw places
// glyphs at their fractional advances, so a Photoshop-layout line is drawn run by run instead,
// each glyph origin moved onto the grid in DOCUMENT space and converted back through the
// axis-aligned residual transform the painter already carries (the y rounding is what Qt did
// anyway for untransformed text; it keeps later lines on whole rows under the residual). Per
// fragment so the run keeps its own colour, and faux bold fills and strokes glyph paths the way
// the vertical plan does, because drawGlyphRun ignores the char format's outline.
//
// `snap` false draws the same runs at their fractional positions: the path a Qt-laid-out line takes
// when a fragment on it carries OpenType features, which QTextLine::draw cannot apply, or when
// `strong_stroke` (Strong anti-aliasing's pen, glyph_outline_pen) has to stroke its glyphs.
void draw_line_glyphs_pixel_aligned(const QTextBlock& block, const BoxTextLineRenderItem& item,
                                    const QTransform& document_transform, QPainter& painter, bool snap,
                                    double strong_stroke) {
  const auto& line = item.line;
  if (!line.isValid() || line.textLength() <= 0) {
    return;
  }
  const int line_start = block.position() + line.textStart();
  const int line_end = line_start + line.textLength();
  const double scale_x = document_transform.m11() > 0.0 ? document_transform.m11() : 1.0;
  const double scale_y = document_transform.m22() > 0.0 ? document_transform.m22() : 1.0;
  for (auto it = block.begin(); !it.atEnd(); ++it) {
    const auto fragment = it.fragment();
    if (!fragment.isValid() || fragment.length() <= 0) {
      continue;
    }
    const int from = std::max(fragment.position(), line_start);
    const int to = std::min(fragment.position() + fragment.length(), line_end);
    if (to <= from) {
      continue;
    }
    const auto format = fragment.charFormat();
    auto color = format.foreground().color();
    if (!color.isValid()) {
      color = QColor(Qt::black);
    }
    const auto outline = glyph_outline_pen(format, color, strong_stroke);
    const bool outlined = outline.style() != Qt::NoPen && outline.widthF() > 0.0;
    // Faux italic is a per-run flag: only this fragment's glyphs lean, sheared about the line's
    // baseline so neighbouring upright runs keep their place (issue 466).
    painter.save();
    if (format.property(kTextFauxItalicFormatProperty).toBool()) {
      painter.setTransform(faux_italic_shear(item.block_origin.y() + line.y() + line.ascent()), true);
    }
    // A stretched QFont is two different things to Qt's engines: FreeType (the offscreen
    // suite) stretches the glyph images and the advances, DirectWrite (a Windows window)
    // stretches the advances only and rasterizes every glyph at its design width, so the
    // issue 20 "M" (103.2%) came out 3% narrow on screen while every pin passed. Drawing
    // each glyph from the UNSTRETCHED face through an explicit x scale equal to the advance
    // ratio this font really delivered (measured, like dominant_run_width_residual) puts the
    // image on the advances on both engines. Unstretched runs keep the plain glyph-run draw.
    QFont unstretched = format.font();
    unstretched.setLetterSpacing(QFont::AbsoluteSpacing, 0.0);
    double image_scale = 1.0;
    if (unstretched.stretch() != 100 && unstretched.stretch() != QFont::AnyStretch) {
      const auto current = QFontMetricsF(unstretched).horizontalAdvance(QLatin1Char('H'));
      unstretched.setStretch(QFont::AnyStretch);
      const auto reference = QFontMetricsF(unstretched).horizontalAdvance(QLatin1Char('H'));
      if (reference > 0.0 && current > 0.0) {
        image_scale = current / reference;
      }
    }
    const bool scale_images = std::abs(image_scale - 1.0) > 1e-6;
    for (auto run : fragment_glyph_runs(block, line, from, to, format)) {
      auto positions = run.positions();
      for (auto& position : positions) {
        // QGlyphRun positions omit QTextCharFormat's baseline offset, unlike
        // QTextLine::draw. Apply the engine-pixel shift before device snapping.
        position.ry() -= format.property(kTextBaselineShiftFormatProperty).toDouble();
        if (!snap) {
          continue;
        }
        const auto document_point = document_transform.map(item.block_origin + position);
        position.rx() += (snap_to_pixel_grid(document_point.x()) - document_point.x()) / scale_x;
        position.ry() += (snap_to_pixel_grid(document_point.y()) - document_point.y()) / scale_y;
      }
      const auto indexes = run.glyphIndexes();
      if (scale_images) {
        // The run's own face (a fallback run names another family than the format), unstretched.
        const auto run_font = run.rawFont();
        QFont face = unstretched;
        if (run_font.familyName() != QFontInfo(unstretched).family()) {
          face = QFont(run_font.familyName());
          face.setStyleName(run_font.styleName());
          face.setHintingPreference(unstretched.hintingPreference());
        }
        auto image_font = QRawFont::fromFont(face);
        if (image_font.isValid()) {
          image_font.setPixelSize(run_font.pixelSize());
        }
        const bool own_face = image_font.isValid() && image_font.familyName() == run_font.familyName();
        painter.save();
        if (outlined) {
          painter.setRenderHint(QPainter::Antialiasing, painter.testRenderHint(QPainter::TextAntialiasing));
        } else {
          painter.setPen(color);
        }
        for (int index = 0; index < indexes.size() && index < positions.size(); ++index) {
          painter.save();
          painter.translate(item.block_origin + positions[index]);
          if (own_face) {
            painter.scale(image_scale, 1.0);
          }
          const auto& glyph_font = own_face ? image_font : run_font;
          if (outlined) {
            const auto path = glyph_font.pathForGlyph(indexes[index]);
            painter.fillPath(path, color);
            painter.strokePath(path, outline);
          } else {
            QGlyphRun single;
            single.setRawFont(glyph_font);
            single.setGlyphIndexes({indexes[index]});
            single.setPositions({QPointF(0.0, 0.0)});
            single.setUnderline(run.underline());
            single.setOverline(run.overline());
            single.setStrikeOut(run.strikeOut());
            painter.drawGlyphRun(QPointF(0.0, 0.0), single);
          }
          painter.restore();
        }
        painter.restore();
        continue;
      }
      if (outlined) {
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, painter.testRenderHint(QPainter::TextAntialiasing));
        const auto raw_font = run.rawFont();
        for (int index = 0; index < indexes.size() && index < positions.size(); ++index) {
          auto path = raw_font.pathForGlyph(indexes[index]);
          path.translate(item.block_origin + positions[index]);
          painter.fillPath(path, color);
          painter.strokePath(path, outline);
        }
        painter.restore();
        continue;
      }
      run.setPositions(positions);
      painter.setPen(color);
      painter.drawGlyphRun(item.block_origin, run);
    }
    painter.restore();
  }
}

// Draws a plan through `painter`, whose transform must already map the plan's DOCUMENT
// space onto the device; the plan's own residual transform is applied here.
// Vertical type: every cell's grapheme cluster is drawn as its own glyph runs at the cell,
// centred on the column axis with the baseline the plan computed. QPainter::drawGlyphRun keeps
// the font engine's rasterization (hinting/antialias settings) but ignores char-format outlines,
// so faux-bold cells fill and stroke glyph paths instead, the same stroke width
// apply_faux_bold_to_document merged into the format (plus Strong's pen, glyph_outline_pen).
void draw_vertical_text_plan(const TextRenderPlan& plan, QPainter& painter) {
  for (const auto& column : plan.vertical_plan.columns) {
    for (const auto& cell : column.cells) {
      if (cell.length <= 0 || !column.line.isValid()) {
        continue;
      }
      const auto runs = column.line.glyphRuns(cell.position - column.block_position, cell.length);
      if (runs.isEmpty()) {
        continue;
      }
      const auto glyph_center = (cell.glyph_start + cell.glyph_end) / 2.0;
      QPointF origin(column.axis - glyph_center, cell.baseline - (column.line.y() + column.line.ascent()));
      painter.save();
      if (cell.rotated) {
        // Rotated Roman: the horizontal glyph run turns 90 degrees clockwise so its advance runs
        // down the column and its ascenders point right, with the ascent+descent box centred on
        // the column axis (Photoshop's Standard Vertical Roman Alignment).
        const QFontMetricsF metrics(cell.format.font());
        painter.translate(column.axis + (metrics.descent() - metrics.ascent()) / 2.0, cell.top);
        painter.rotate(90.0);
        origin = QPointF(-cell.glyph_start, -(column.line.y() + column.line.ascent()));
      } else if (plan.faux_italic_render && cell.format.property(kTextFauxItalicFormatProperty).toBool()) {
        painter.setTransform(faux_italic_shear(cell.baseline), true);
      }
      auto color = cell.format.foreground().color();
      if (!color.isValid()) {
        color = QColor(Qt::black);
      }
      const auto outline = glyph_outline_pen(cell.format, color, plan.strong_stroke);
      const bool outlined = outline.style() != Qt::NoPen && outline.widthF() > 0.0;
      if (outlined) {
        painter.setRenderHint(QPainter::Antialiasing, painter.testRenderHint(QPainter::TextAntialiasing));
        for (const auto& run : runs) {
          const auto raw_font = run.rawFont();
          const auto indexes = run.glyphIndexes();
          const auto positions = run.positions();
          for (int index = 0; index < indexes.size() && index < positions.size(); ++index) {
            auto path = raw_font.pathForGlyph(indexes[index]);
            path.translate(origin + positions[index]);
            painter.fillPath(path, color);
            painter.strokePath(path, outline);
          }
        }
      } else {
        painter.setPen(color);
        for (const auto& run : runs) {
          painter.drawGlyphRun(origin, run);
        }
      }
      painter.restore();
    }
  }
}

void draw_text_render_plan(const TextRenderPlan& plan, QPainter& painter) {
  auto& document = *plan.built.document;
  if (plan.has_document_transform) {
    painter.setTransform(plan.document_transform, true);
  }
  if (plan.vertical) {
    draw_vertical_text_plan(plan, painter);
    return;
  }
  if (!plan.line_render_items.empty() || plan.area) {
    for (const auto& item : plan.line_render_items) {
      painter.save();
      painter.setClipRect(item.clip_rect);
      // Faux italic lines always take a glyph-run path (format_needs_glyph_run_draw), which shears
      // each faux italic run about the line's baseline; QTextLine::draw only sees upright lines.
      if (plan.pixel_align_glyphs) {
        draw_line_glyphs_pixel_aligned(document.findBlock(item.block_position), item, plan.document_transform,
                                       painter, true, plan.strong_stroke);
      } else if (const auto block = document.findBlock(item.block_position);
                 plan.strong_stroke > 0.0 || line_has_render_features(block, item.line)) {
        draw_line_glyphs_pixel_aligned(block, item, plan.document_transform, painter, false, plan.strong_stroke);
      } else {
        item.line.draw(&painter, item.block_origin);
      }
      painter.restore();
    }
    // Underline and strikethrough bars, unclipped and upright (faux italic slants the glyphs,
    // not the lines under them).
    for (const auto& bar : plan.decorations) {
      painter.fillRect(bar.rect, bar.color);
    }
  } else {
    // Only text with no faux italic run gets here: document_needs_line_items asks for line items.
    document.drawContents(&painter, plan.local_rect);
  }
}

// The metrics the PSD writer needs to put Photoshop's re-layout on the pixels this plan draws
// (docs/text-render-calibration.md, "Patchy text re-renders where Patchy drew it"):
// - first_baseline: the first drawn line's baseline in the plan's local units, below the
//   text-local origin for point text (the transform origin; equal to the raster's top row unless
//   glyph ink overshoots the first line top) and below the raster's top row (the plan's
//   local_rect top) for box text. Photoshop anchors point text at ty = baseline.
// - box_baseline_inset: Qt's first baseline minus Photoshop's box rule for a Patchy-authored
//   block (space before + the line's max CAP HEIGHT: PS 27.9 re-renders of the same block with
//   Arial, Times, Georgia and Verdana at 268 px all put the first baseline at capHeight x size,
//   docs/text-render-calibration.md), so the writer can move the frame down by it. Qt-natural
//   box text only. Rounded to 1/64 px (QFixed's own grid) so the writer's shift and the reader's
//   restore are exact in binary and a reopened file re-saves byte-identically.
// - auto_leading: Qt's baseline advance (line 2 minus line 1 of the first paragraph, else the
//   first line's height, which is what Qt advances by) over the dominant run size, written as the
//   paragraph /AutoLeading so Photoshop's second line lands where Qt's did. Qt-natural only.
TextLayoutMetrics text_layout_metrics_for_plan(const TextRenderPlan& plan, const TextToolSettings& settings,
                                               const QString& rich_text_runs) {
  TextLayoutMetrics metrics;
  if (plan.built.document == nullptr || plan.vertical || settings.vertical) {
    return metrics;
  }
  const auto& document = *plan.built.document;
  const auto* layout = document.documentLayout();
  if (layout == nullptr) {
    return metrics;
  }
  struct NaturalLine {
    QTextBlock block;
    QTextLine line;
    double baseline{0.0};
  };
  std::vector<NaturalLine> natural;
  for (auto block = document.begin(); block.isValid() && natural.size() < 2U; block = block.next()) {
    auto* text_layout = block.layout();
    if (text_layout == nullptr) {
      continue;
    }
    const auto origin_y = layout->blockBoundingRect(block).top();
    for (int i = 0; i < text_layout->lineCount() && natural.size() < 2U; ++i) {
      const auto line = text_layout->lineAt(i);
      if (!line.isValid()) {
        continue;
      }
      natural.push_back(NaturalLine{block, line, origin_y + line.y() + line.ascent()});
    }
  }
  if (natural.empty()) {
    return metrics;
  }
  double first_baseline = natural.front().baseline;
  if (!plan.line_render_items.empty() && plan.line_render_items.front().line.isValid()) {
    const auto& item = plan.line_render_items.front();
    first_baseline = item.block_origin.y() + item.line.y() + item.line.ascent();
  }
  if (std::isfinite(first_baseline)) {
    // Point text: from the text-local origin (the transform's), which the PSD writer starts
    // from; the raster begins above it whenever glyph ink overshoots the first line's top
    // (grow_point_text_rect_to_glyph_ink). Box text keeps the raster-top convention.
    metrics.first_baseline = settings.boxed ? first_baseline - plan.local_rect.top() : first_baseline;
    if (!settings.boxed) {
      metrics.raster_top = plan.local_rect.top();
    }
  }
  if (settings.photoshop_layout) {
    return metrics;
  }
  if (settings.boxed) {
    const auto& first = natural.front();
    const auto line_start = first.block.position() + first.line.textStart();
    const auto line_end = line_start + std::max(1, first.line.textLength());
    double cap_height = 0.0;
    bool found_format = false;
    for (auto fragment_it = first.block.begin(); !fragment_it.atEnd(); ++fragment_it) {
      const auto fragment = fragment_it.fragment();
      if (!fragment.isValid() || fragment.length() <= 0 || fragment.position() + fragment.length() <= line_start ||
          fragment.position() >= line_end) {
        continue;
      }
      cap_height = std::max(cap_height, QFontMetricsF(fragment.charFormat().font()).capHeight());
      found_format = true;
    }
    if (!found_format) {
      cap_height = QFontMetricsF(first.block.charFormat().font()).capHeight();
    }
    const auto inset = first.baseline - (std::max(0.0, first.block.blockFormat().topMargin()) + cap_height);
    if (std::isfinite(inset)) {
      metrics.box_baseline_inset = std::round(inset * 64.0) / 64.0;
    }
  }
  const bool second_line_in_first_block = natural.size() >= 2U && natural[1].block == natural[0].block;
  const double pitch = second_line_in_first_block ? natural[1].baseline - natural[0].baseline
                                                  : natural.front().line.height();
  // The divisor is the largest run size ON THE LINES THE PITCH WAS MEASURED ON, not the layer's
  // largest run: Photoshop applies the fraction per line to that line's own sizes, so a layer
  // whose 49 px lines are separated by a 155 px spacer paragraph wrote 55 / 155 = 0.35 and
  // Photoshop stacked the 49 px lines 17 px apart (the September 2026 Steam Frame poster). The
  // layer-wide maximum stays the fallback for a line with no fragments (an empty paragraph).
  const auto line_max_size = [](const NaturalLine& natural_line) {
    double best = 0.0;
    const auto line_start = natural_line.block.position() + natural_line.line.textStart();
    const auto line_end = line_start + std::max(1, natural_line.line.textLength());
    for (auto fragment_it = natural_line.block.begin(); !fragment_it.atEnd(); ++fragment_it) {
      const auto fragment = fragment_it.fragment();
      if (!fragment.isValid() || fragment.length() <= 0 || fragment.position() + fragment.length() <= line_start ||
          fragment.position() >= line_end) {
        continue;
      }
      const auto format = fragment.charFormat();
      double vertical_scale = 1.0;
      if (format.hasProperty(kTextVerticalScaleFormatProperty)) {
        const auto value = format.property(kTextVerticalScaleFormatProperty).toDouble();
        if (std::isfinite(value) && value > 0.01 && value < 100.0) {
          vertical_scale = value;
        }
      }
      best = std::max(best, photoshop_char_exact_size(format) * vertical_scale);
    }
    return best;
  };
  double dominant = std::max(line_max_size(natural.front()),
                             second_line_in_first_block ? line_max_size(natural[1]) : 0.0);
  if (!(std::isfinite(dominant) && dominant > 0.0)) {
    dominant = dominant_text_run_size(settings, rich_text_runs);
  }
  if (std::isfinite(pitch) && pitch > 0.0 && std::isfinite(dominant) && dominant > 0.0) {
    const auto fraction = pitch / dominant;
    if (fraction > 0.01 && fraction < 10.0) {
      metrics.auto_leading = fraction;
    }
  }
  return metrics;
}

RenderedTextPixels render_text_pixels_with_local_rect(const TextToolSettings& settings, QColor color,
                                                      std::int32_t max_width,
                                                      const QString& paragraph_runs,
                                                      const QString& rich_text_runs,
                                                      std::optional<QRectF> requested_local_rect,
                                                      double metric_scale,
                                                      const QTransform& document_transform_in,
                                                      double layout_scale_in) {
  const auto plan = build_text_render_plan(settings, color, max_width, paragraph_runs, rich_text_runs,
                                           requested_local_rect, metric_scale, document_transform_in,
                                           layout_scale_in);
  if (plan.built.document == nullptr) {
    return RenderedTextPixels{PixelBuffer{}, QRectF(), TextLayoutMetrics{}};
  }
  const auto& local_rect = plan.local_rect;
  const auto& document_transform = plan.document_transform;
  // When a document transform is supplied the glyphs are rasterized *through* the affine (scale,
  // rotation, shear), so scaled-up text stays crisp instead of resampling an already-rendered bitmap.
  // The output image is sized to the transformed bounds and `local_rect` is returned in document space.
  const bool has_document_transform = plan.has_document_transform;
  const auto target_rect = has_document_transform ? document_transform.mapRect(local_rect) : local_rect;
  const auto image_left = static_cast<int>(std::floor(target_rect.left()));
  const auto image_top = static_cast<int>(std::floor(target_rect.top()));
  const auto image_right = static_cast<int>(std::ceil(target_rect.right()));
  const auto image_bottom = static_cast<int>(std::ceil(target_rect.bottom()));
  // Guard against a degenerate transform ballooning the dimensions: an over-large QImage allocates a
  // null image, which would silently drop the crisp render and fall back to the blocky resample.
  constexpr int kMaxTextRasterDimension = 16384;
  const auto image_width = std::clamp(image_right - image_left, 1, kMaxTextRasterDimension);
  const auto image_height = std::clamp(image_bottom - image_top, 1, kMaxTextRasterDimension);
  QImage image(image_width, image_height, QImage::Format_RGBA8888);
  if (image.isNull()) {
    return RenderedTextPixels{PixelBuffer{}, local_rect, TextLayoutMetrics{}};
  }
  image.fill(Qt::transparent);

  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing, settings.anti_alias > 0);
  painter.setRenderHint(QPainter::TextAntialiasing, settings.anti_alias > 0);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, settings.anti_alias > 0);
  painter.translate(-static_cast<qreal>(image_left), -static_cast<qreal>(image_top));
  draw_text_render_plan(plan, painter);
  painter.end();
  if (settings.anti_alias <= 0) {
    for (int y = 0; y < image.height(); ++y) {
      auto* line = image.scanLine(y);
      for (int x = 0; x < image.width(); ++x) {
        auto* pixel = line + static_cast<std::size_t>(x) * 4U;
        pixel[3] = pixel[3] >= 128 ? 255 : 0;
      }
    }
  } else if (plan.strong_stroke > 0.0) {
    // Photoshop's 4 x 4 coverage samples miss an edge that covers less than 1/8 of a pixel, so
    // the stroke's thinnest graze stays empty there (kStrongAntiAliasMinimumAlpha). Here it would
    // be a faint new row or column, and the imported-text placement pins the ink's top-left corner
    // to Photoshop's (anchored_text_transform_for_pixels): one such pixel moved the whole re-render
    // a pixel down or right.
    for (int y = 0; y < image.height(); ++y) {
      auto* line = image.scanLine(y);
      for (int x = 0; x < image.width(); ++x) {
        auto* pixel = line + static_cast<std::size_t>(x) * 4U;
        if (pixel[3] < kStrongAntiAliasMinimumAlpha) {
          pixel[0] = pixel[1] = pixel[2] = pixel[3] = 0;
        }
      }
    }
  }
  return RenderedTextPixels{pixels_from_image_rgba(image),
                            QRectF(static_cast<qreal>(image_left), static_cast<qreal>(image_top),
                                   static_cast<qreal>(image_width), static_cast<qreal>(image_height)),
                            text_layout_metrics_for_plan(plan, settings, rich_text_runs)};
}

}  // namespace patchy::ui
