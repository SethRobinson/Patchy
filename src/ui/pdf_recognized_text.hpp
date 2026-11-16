#pragma once

// Recognized text on its way into an exported PDF (docs/pdf-searchable-text.md, "From a
// document"): the lines a document's Recognized Text groups hold, where each sits on the
// image, and the same lines in a page's own coordinates for the image writer's invisible
// text.
//
// Binding rule (the "Text recognition" entry of docs/legal-constraints-scanning.md): a line
// goes out whole, as one run. Nothing here splits a line into words or characters to place
// them, and nothing may be added that does.

#include "core/document.hpp"
#include "core/layer.hpp"
#include "formats/pdf_image_writer.hpp"
#include "ocr/ocr_engine.hpp"

#include <QPointF>
#include <QSize>
#include <QSizeF>
#include <QString>
#include <QTransform>

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace patchy::ui {

// Where one recognized line was found: the corners of its box in document pixels, in the
// frame a reader of that line sees (top left, top right, bottom right, bottom left).
struct RecognizedLinePlace {
  std::array<QPointF, 4> quad;
  bool vertical{false};  // a column: its characters run top to bottom
};

// The value of kLayerMetadataOcrLine: the layer's bounds when it was made and the place of
// each of its lines, in order: "1|x,y,w,h|" then per line eight corner numbers and 0 or 1
// for a column, lines separated by '|'. Session data, never written to a file. A layer
// moved since keeps its places (they move with it); one whose line count or size changed
// falls back to its bounds (recognized_text_lines).
[[nodiscard]] std::string serialize_recognized_line_places(const Rect& layer_bounds,
                                                           const std::vector<RecognizedLinePlace>& places);
struct RecognizedLinePlaces {
  Rect layer_bounds;
  std::vector<RecognizedLinePlace> places;
};
[[nodiscard]] std::optional<RecognizedLinePlaces> parse_recognized_line_places(std::string_view value);

struct RecognizedTextLine {
  QString text;
  RecognizedLinePlace place;
};

// True when a Recognized Text group anywhere in the document holds a text layer.
[[nodiscard]] bool document_has_recognized_text(const Document& document);
// True when some of that text does not show: the layer, its group or a group above it is
// hidden. An export through Qt's PDF engine writes only the text a page shows.
[[nodiscard]] bool document_has_hidden_recognized_text(const Document& document);

// Every line of every text layer inside a Recognized Text group, shown or hidden, in
// reading order (the layer stack from the top), with the text as it is now (a correction
// typed into a layer is what goes out). A layer's lines sit
// where they were recognized, moved along with the layer; when the layer's line count no
// longer matches or its size is more than a tenth off what was recognized (lines were added
// or removed, the layer was resized, the document was reopened), its lines share its bounds
// evenly instead: stacked for level text, side by side from the right for a column. A line
// whose middle is off the canvas is left out (it belongs to another page).
[[nodiscard]] std::vector<RecognizedTextLine> recognized_text_lines(const Document& document);

// Document pixels (y down) to a PDF page's own space (points, y up, before /Rotate), for a
// document that fills the page as displayed. `rotate` is the page's /Rotate (0, 90, 180,
// 270, clockwise when displayed); `page_points` is the page's size before that rotation.
[[nodiscard]] QTransform document_to_pdf_page(QSize document_pixels, QSizeF page_points, int rotate);

// The lines as the image writer takes them, through `to_page`. One TextLine per line: the
// baseline along the box's bottom edge (its left edge for a column, read downward), the
// text size the box's thickness, the length the box's length.
[[nodiscard]] std::vector<pdf::TextLine> pdf_text_lines(const std::vector<RecognizedTextLine>& lines,
                                                        const QTransform& to_page);

// A recognition of an image that fills an upright page (`result` in that image's pixels),
// as the image writer takes it: one TextLine per recognized line, in the engine's order.
// What Scan a Book writes for a page it has just read.
[[nodiscard]] std::vector<pdf::TextLine> pdf_text_lines(const ocr::Result& result, QSize image_pixels,
                                                        QSizeF page_points);

}  // namespace patchy::ui
