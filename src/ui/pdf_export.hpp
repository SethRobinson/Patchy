#pragma once

#include "core/document.hpp"

#include <QString>

namespace patchy::ui {

// Single-page PDF export sized to the document itself, not to a sheet of paper: the
// page is pixels / document PPI inches per axis, matching Photoshop's Save As PDF.
// The paper-relative flow (page layout, margins, crop marks, scale-to-fit) stays in
// print_dialog.hpp's write_print_pdf.
//
// Qt's PDF engine re-encodes every non-grayscale image as JPEG quality 94 unless the
// painter asks for QPainter::LosslessImageRendering, and it exposes no quality knob,
// so the choice really is one bool. Lossless is the default: an image editor's PDF
// export must not silently degrade pixels.
struct PdfExportOptions {
  bool lossless{true};
};

// Writes a one-page PDF holding the flattened document. Document alpha becomes a PDF
// /SMask. Throws std::runtime_error when the file cannot be written.
void write_pdf_document_file(const Document& document, const QString& path, const PdfExportOptions& options = {});

}  // namespace patchy::ui
