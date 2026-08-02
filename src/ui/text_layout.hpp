#pragma once

// Text line layout: the single geometric authority for a laid-out text object.
//
// The rasterizer draws a text layer by walking a *line plan* - one QTextLine per
// visual line, each with the origin it is drawn at - rather than letting
// QTextDocument place the lines itself, because Photoshop's leading model moves
// baselines away from Qt's natural spacing. Caret geometry, selection geometry
// and mouse hit-testing must read the SAME plan, or the highlight sits on one
// layout while the glyphs sit on another (the pre-August-2026 bug: the caret
// path used Qt's natural block origins and never set photoshop_layout at all).
//
// TextLineGeometry is that shared reader. Build it from the same QTextDocument
// and the same `boxed` / `photoshop_layout` flags the render pass uses and its
// answers are guaranteed to agree with the drawn glyphs.
//
// Lifetime: QTextLine is a handle into the QTextLayout owned by the document's
// blocks. Every type here is valid only while the document that produced it is
// alive and has not been laid out again. Build, use, discard.
//
// The document construction itself (build_text_render_document) and the raster
// pass still live in main_window.cpp; see docs/text-tool.md.

#include <QFont>
#include <QPointF>
#include <QRectF>
#include <QTextFormat>

#include <optional>
#include <vector>

class QTextBlock;
class QTextCharFormat;
class QTextDocument;
class QTextLine;

#include <QTextLine>

namespace patchy::ui {

// Char/block format properties carrying the Photoshop text model through a QTextDocument.
// Runs v3 records the unrounded size because QFont pixel sizes are ints and leading math
// needs the fractional value.
inline constexpr int kTextDisplayFamilyFormatProperty = QTextFormat::UserProperty + 31;
inline constexpr int kTextLeadingFormatProperty = QTextFormat::UserProperty + 32;
inline constexpr int kTextAutoLeadingFormatProperty = QTextFormat::UserProperty + 33;
inline constexpr int kTextTrackingFormatProperty = QTextFormat::UserProperty + 34;
inline constexpr int kTextExactSizeFormatProperty = QTextFormat::UserProperty + 35;
// Block property: paragraph auto-leading fraction (Photoshop default 1.2).
inline constexpr int kTextBlockAutoLeadFractionProperty = QTextFormat::UserProperty + 36;
// Character-panel glyph scales (runs v3): width x horizontal, height x vertical. The glyph
// pixel size folds the vertical scale in; leading math stays FontSize-based, so the exact-size
// property intentionally excludes it.
inline constexpr int kTextHorizontalScaleFormatProperty = QTextFormat::UserProperty + 37;
inline constexpr int kTextVerticalScaleFormatProperty = QTextFormat::UserProperty + 38;
// Photoshop's faux bold (runs v4): a synthetic embolden of the run's own face, never a request
// for the family's real bold face. Carried as a flag and turned into a glyph-outline stroke by
// apply_faux_bold_to_document at render time, so it follows later colour and size edits.
inline constexpr int kTextFauxBoldFormatProperty = QTextFormat::UserProperty + 39;

// Photoshop's faux bold widens each glyph by this fraction of the em, in both the stroke it
// paints and the advance it adds. Calibrated against Photoshop's own rasters for Georgia-Italic
// at 12px ("Dungeon:" 58px, "Fights Left:" 69px in the Dungeon Scroll probe); both land exactly
// anywhere in 0.025-0.030, and the real Bold Italic face they used to resolve to is 5-6px wider.
inline constexpr double kFauxBoldEmFraction = 0.03;

// One visual line as the renderer draws it: the line, the origin it is drawn at, and the
// rect it is clipped to. `block_position` is the owning block's document position, which
// QTextLine alone cannot recover (its lineNumber() is an index within its own block's
// layout); TextLineGeometry needs it to convert between block-relative and document
// positions. The render pass ignores it.
struct BoxTextLineRenderItem {
  QTextLine line;
  QPointF block_origin;
  QRectF clip_rect;
  int block_position{0};
};

struct BoxTextRenderPlan {
  QRectF local_rect;
  std::vector<BoxTextLineRenderItem> lines;
};

struct PhotoshopTextLayoutPlan {
  std::vector<BoxTextLineRenderItem> lines;
  QRectF ink_rect;  // union of the repositioned line rects (line-box based, pre-bleed)
  bool valid{false};
};

struct PhotoshopLineMetrics {
  double leading{0.0};         // max effective leading among the line's chars
  double first_baseline{0.0};  // box text: max typoAscender x size among the line's chars
};

// OS/2 sTypoAscender as a fraction of the em. Photoshop positions the first baseline of box
// (paragraph) text at typoAscender x size below the box top (COM-calibrated against PS 2026:
// Arial/Times/Verdana/Courier probes land within ~1% of typoAscender; Qt's ascent() is the
// much larger usWinAscent and would sit the first line visibly too low). Cached per face.
[[nodiscard]] double typographic_ascent_fraction(const QFont& font);

// The fractional font size a Photoshop-layout char format contributes to leading math.
[[nodiscard]] double photoshop_char_exact_size(const QTextCharFormat& format);

// Photoshop effective leading of one char format: the fixed value, or auto leading =
// paragraph auto-leading fraction x font size.
[[nodiscard]] double photoshop_char_leading(const QTextCharFormat& format, double paragraph_fraction);

// Char formats intersecting one visual line, folded into the line's leading metrics. An empty
// line (blank paragraph) has no fragments and uses the block's char format.
[[nodiscard]] PhotoshopLineMetrics photoshop_line_metrics(const QTextBlock& block, const QTextLine& line,
                                                          double paragraph_fraction);

// Every line intersecting `gate_rect`, clipped with the given bleeds. Whole LINES are gated,
// never raster rows: a line straddling the frame bottom draws completely.
[[nodiscard]] std::vector<BoxTextLineRenderItem> boxed_text_line_render_items(const QTextDocument& document,
                                                                             QRectF gate_rect, qreal top_bleed,
                                                                             qreal bottom_bleed,
                                                                             qreal horizontal_bleed);

[[nodiscard]] BoxTextRenderPlan boxed_text_render_plan(const QTextDocument& document, const QFont& font,
                                                       QRectF frame_rect,
                                                       std::optional<QRectF> requested_local_rect);

// Lay the document's lines out with Photoshop's leading model: the first line keeps Qt's
// natural position for point text (the anchor machinery aligns rasters by the first line) or
// sits typoAscender below the box top for box text; every following baseline advances by the
// *entered* line's max leading (auto = paragraph fraction x size) plus paragraph spacing.
// Line x positions stay Qt's own (alignment against the layout width).
[[nodiscard]] PhotoshopTextLayoutPlan photoshop_text_layout_plan(const QTextDocument& document, bool boxed);

// Caret, selection and hit-test geometry over the line plan the renderer draws, in the
// document's own coordinate space. Read the file header comment on lifetime.
class TextLineGeometry {
public:
  // `photoshop_layout` must match what the render pass was given, so the lines are the same
  // ones the glyphs came from. Falls back to the document's natural lines when the Photoshop
  // plan does not apply, which is exactly what the renderer falls back to.
  [[nodiscard]] static TextLineGeometry build(const QTextDocument& document, bool boxed, bool photoshop_layout);

  // Lines already positioned by a render pass (the boxed plan's clipped items, say). The
  // items must carry the origins they were drawn at.
  [[nodiscard]] static TextLineGeometry from_lines(const QTextDocument& document,
                                                   const std::vector<BoxTextLineRenderItem>& lines);

  [[nodiscard]] bool empty() const noexcept {
    return lines_.empty();
  }

  // Union of the positioned line boxes, in document space. This is how tall the text actually
  // is; QTextDocument::size() reports Qt's natural layout, which is shorter than the Photoshop
  // one whenever leading exceeds the natural line spacing.
  [[nodiscard]] QRectF bounding_rect() const;

  // Zero-width caret rect (height = ascent + descent, centred in the line box) for a
  // character position. Empty when the position cannot be placed.
  [[nodiscard]] QRectF caret_rect(int position) const;

  // One rect per visual line covered by [start, end).
  [[nodiscard]] std::vector<QRectF> selection_rects(int start, int end) const;

  // Nearest character position to a point. Clamps to the first/last line vertically and to
  // the line ends horizontally, so a click anywhere always lands somewhere sensible.
  [[nodiscard]] int position_at(QPointF local_point) const;

private:
  struct Line {
    QTextLine line;
    QPointF block_origin;
    int block_position{0};
    int block_length{1};
  };

  std::vector<Line> lines_;
  int maximum_position_{0};
};

}  // namespace patchy::ui
