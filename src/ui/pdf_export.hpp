#pragma once

#include "core/document.hpp"

#include <QString>

#include <string>
#include <vector>

class QPdfWriter;

namespace patchy::ui {

// Single-page PDF export sized to the document itself, not to a sheet of paper: the
// page is pixels / document PPI inches per axis, matching Photoshop's Save As PDF.
// The paper-relative flow (page layout, margins, crop marks, scale-to-fit) stays in
// print_dialog.hpp's write_print_pdf.
//
// Qt's PDF engine re-encodes every non-grayscale image as JPEG quality 94 unless the
// painter asks for QPainter::LosslessImageRendering, and it exposes no quality knob,
// so the image choice really is one bool. Lossless is the default: an image editor's
// PDF export must not silently degrade pixels.
struct PdfExportOptions {
  bool lossless{true};
  // Keep layers as editable objects instead of one flattened image: shape layers become
  // PDF paths, text layers real text with embedded fonts, pixel and smart-object layers
  // images. What Qt's PDF engine cannot composite per object (blend modes, adjustment
  // layers, group opacity, raster masks on vectors, layer styles) flattens into an
  // image chunk with a notice, so the page can look different from the canvas.
  bool editable_layers{false};
};

// Writes a one-page PDF of the document. Flat mode holds the flattened composite
// (document alpha becomes a PDF /SMask); editable mode walks the layer stack (see
// pdf_export_editable.cpp). `notices` receives one line per structural loss in editable
// mode. Throws std::runtime_error when the file cannot be written.
void write_pdf_document_file(const Document& document, const QString& path, const PdfExportOptions& options = {},
                             std::vector<std::string>* notices = nullptr);

namespace pdf_detail {
// Page sized from the document (pixels / PPI inches per axis, exact-match size, zero
// margins, the 14400 pt cap) and the device resolution pinned to the document PPI so the
// painter's logical grid is one unit per document pixel. Shared by both export modes.
void configure_document_page(QPdfWriter& writer, const Document& document);
// The editable-layers writer.
void write_editable_pdf_document_file(const Document& document, const QString& path, const PdfExportOptions& options,
                                      std::vector<std::string>* notices);
}  // namespace pdf_detail

}  // namespace patchy::ui
