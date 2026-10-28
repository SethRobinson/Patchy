#pragma once
// One document-owned line plan drives raster output, editable PDF and text geometry.
// QTextLine handles remain valid only while built.document lives and is not relaid out.
#include "ui/text_document.hpp"
#include "ui/text_layout.hpp"
#include "core/pixel_buffer.hpp"
#include <QRectF>
#include <QTransform>
#include <optional>
#include <vector>
class QPainter;
namespace patchy::ui {
// Layout metrics of a render, stored as kLayerMetadataTextFirstBaseline & co. so the Qt-free
// PSD writer can place Photoshop's re-layout on this raster. An empty optional means "does not
// apply" (vertical type; the inset and fraction on Photoshop-layout layers, whose plan already
// follows Photoshop's rules and must stay byte-stable).
struct TextLayoutMetrics {
  std::optional<double> first_baseline;
  std::optional<double> box_baseline_inset;
  std::optional<double> auto_leading;
  std::optional<double> raster_top;  // point text: local_rect.top(), the raster's top row from the origin
};


struct RenderedTextPixels {
  PixelBuffer pixels;
  QRectF local_rect;
  TextLayoutMetrics metrics;
};


// The vertical layout plan a render pass draws, bleed included, so the caret geometry and the
// glyphs come from one placement. `box_scale` scales the frame dims (a transform fold),
// `size_scale` the bleed's size basis (the fold plus any PSD-frame layout scale).
struct VerticalRenderPlan {
  VerticalTextLayoutPlan plan;
  double bleed{0.0};
  QRectF local_rect;
};


// Everything the rasterizer decides BEFORE it touches a paint device: the laid-out
// document, the line plan, the local rect, and the residual document transform. The
// QImage render and the editable-PDF text export both draw the same plan, so the PDF's
// real text lands exactly where the layer's raster does.
// One underline or strikethrough bar in the plan's local units (GitHub issue 346).
struct TextDecorationRect {
  QRectF rect;
  QColor color;
};

// Where Photoshop draws a font's lines, as fractions of the em (COM captures of 18 faces at
// 200 px): the underline is centred halfway between the face's underline position and its
// lowest point (head yMin), with the post table's thickness; a CFF face's underline position is
// its CFF value (the post value minus half the thickness). The strikethrough is centred at half
// the x-height, with the same thickness.
struct TextDecorationMetrics {
  double underline_center{0.15};  // below the baseline
  double strike_center{0.26};     // above the baseline
  double thickness{0.05};
};
TextDecorationMetrics text_decoration_metrics(const QFont& font);

// Photoshop's Strong anti-aliasing (/AntiAlias 2) draws each glyph heavier than Smooth: its
// rasters match the Smooth outline filled and stroked with a pen this many DOCUMENT pixels wide,
// at every size from 8 to 72 px in Arial and Georgia (docs/text-anti-alias.md). Advances do not
// change.
inline constexpr double kStrongAntiAliasStrokePixels = 0.25;
// Photoshop samples coverage on a 4 x 4 grid (alpha in sixteenths, edges on quarter pixels), so
// an edge that covers less than 1/8 of a pixel misses every sample: such faint pixels are dropped.
inline constexpr int kStrongAntiAliasMinimumAlpha = 32;

struct TextRenderPlan {
  TextRenderDocument built;
  QRectF local_rect;
  std::vector<BoxTextLineRenderItem> line_render_items;
  std::vector<TextDecorationRect> decorations;
  QTransform document_transform;  // post-fold residual (identity when none was supplied)
  bool has_document_transform{false};
  bool faux_italic_render{false};
  // Photoshop layout through an axis-aligned transform: every glyph origin is moved onto
  // the document pixel grid as it is drawn (draw_line_glyphs_pixel_aligned).
  bool pixel_align_glyphs{false};
  // Strong anti-aliasing: the extra outline pen in the plan's LOCAL units
  // (kStrongAntiAliasStrokePixels over the transform's scale); 0 for every other mode. A
  // raster-only weight: the editable PDF export clears it so its glyphs stay real text.
  double strong_stroke{0.0};
  // Vertical type: the cell plan replaces the line items (which stay empty).
  bool vertical{false};
  bool area{false};
  VerticalTextLayoutPlan vertical_plan;
};


VerticalRenderPlan vertical_render_plan(const QTextDocument& document, const TextToolSettings& settings,
                                        double box_scale, double size_scale);
TextRenderPlan build_text_render_plan(const TextToolSettings& settings, QColor color, std::int32_t max_width,
                                      const QString& paragraph_runs, const QString& rich_text_runs,
                                      std::optional<QRectF> requested_local_rect, double metric_scale,
                                      const QTransform& document_transform_in, double layout_scale_in);
void draw_text_render_plan(const TextRenderPlan& plan, QPainter& painter);
RenderedTextPixels render_text_pixels_with_local_rect(const TextToolSettings& settings, QColor color,
                                                      std::int32_t max_width,
                                                      const QString& paragraph_runs = QString(),
                                                      const QString& rich_text_runs = QString(),
                                                      std::optional<QRectF> requested_local_rect = std::nullopt,
                                                      double metric_scale = 1.0,
                                                      const QTransform& document_transform_in = QTransform(),
                                                      double layout_scale_in = 1.0);

}  // namespace patchy::ui
