#pragma once

// Recognized text as editable text layers (docs/ocr.md, "Text layers"): each block of
// recognized lines is laid out, sized, coloured and drawn here, in document coordinates,
// ready for MainWindow to wrap in a layer (make_recognized_text_group in main_window.cpp,
// which owns the text metadata writers).
//
// Binding rules (the "Text recognition" entry of docs/legal-constraints-scanning.md): the
// font family is the caller's (the user's pick); nothing here names, classifies or matches a
// typeface, and no candidate font is rendered to compare with the image. Size comes from
// fitting each line's length against that family, colour from sampling the line's pixels,
// and an italic face from the measured slant of the line's ink. Weight and serif, sans or
// monospace class are not estimated.

#include "core/pixel_buffer.hpp"
#include "ocr/ocr_engine.hpp"
#include "ocr/ocr_layout.hpp"
#include "ui/text_document.hpp"
#include "ui/text_render.hpp"

#include <QColor>
#include <QImage>
#include <QPoint>
#include <QRectF>
#include <QString>
#include <QTransform>

#include <optional>
#include <vector>

namespace patchy::ui {

// One text layer per block of text, or one per recognized line. The tokens are persisted
// (ocr/grouping) and are script option values: never rename one.
enum class OcrGrouping { Blocks, Lines };
[[nodiscard]] QString ocr_grouping_token(OcrGrouping grouping);
[[nodiscard]] std::optional<OcrGrouping> ocr_grouping_from_token(const QString& token);

struct OcrLayerStyle {
  QString family;         // the user's font
  int anti_alias{3};      // the text tool's anti-aliasing
  // Size, colour and slant from the image. Off: `size` and `color` as given, upright.
  bool match_style{true};
  int size{36};
  QColor color{Qt::black};  // color-literal-ok: text color of a document layer
  // A colour the caller fixed (a script's `color`), used even while matching the rest.
  std::optional<QColor> fixed_color;
};

// A text layer drawn and placed: what MainWindow stores on a Layer.
struct RecognizedTextLayer {
  QString name;
  TextToolSettings settings;
  QColor color;
  QString rich_text_runs;
  QString paragraph_runs;
  PixelBuffer pixels;  // RGBA8
  Rect bounds;         // document coordinates
  TextLayoutMetrics metrics;
  // Text-local to document, for a line the engine reported turned; unset for upright text,
  // which is plain point text at `bounds`.
  std::optional<QTransform> transform;
  // The recognized lines this layer holds (indices into the Result), in reading order.
  std::vector<std::size_t> lines;
};

// `result` is in the pixels of `source` (straight-alpha RGBA8888, what the engine read);
// `origin` is where that image's top left sits in the document. Layers come back in reading
// order. Empty lines are skipped; a block whose text cannot be drawn is left out.
[[nodiscard]] std::vector<RecognizedTextLayer> build_recognized_text_layers(const ocr::Result& result,
                                                                           const QImage& source, QPoint origin,
                                                                           OcrGrouping grouping,
                                                                           const OcrLayerStyle& style);

// The colour of the text inside `box` of a straight-alpha RGBA8888 image read over white:
// the background is the per-channel median of the ring just outside the box, the text the
// mean of the fifth of the box's pixels farthest from it. Black when nothing in the box
// stands out from the background.
[[nodiscard]] QColor sampled_text_color(const QImage& source, const QRectF& box);

// The slant of the ink inside `box`, in degrees clockwise from upright (an italic face
// leans about 10 to 14): the shear that makes the box's column profile sharpest, tried in
// two-degree steps from -4 to 24. 0 when the box holds too little to measure.
[[nodiscard]] double measured_slant_degrees(const QImage& source, const QRectF& box);

// The name a text layer takes from its text, as an editor commit derives it.
[[nodiscard]] QString recognized_text_layer_name(const QString& text);

}  // namespace patchy::ui
