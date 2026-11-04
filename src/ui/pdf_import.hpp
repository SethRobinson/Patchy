#pragma once

#include "core/document.hpp"

#include <QString>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

class QWidget;

namespace patchy::ui {

// PDF import rasterizes pages through the optional Qt PDF add-on (QPdfDocument, which
// wraps PDFium). Qt ships no PDF module for WebAssembly, and the module is not part of a
// default Qt install, so every entry point here has a stub build (pdf_import_stub.cpp)
// that reports the feature as unavailable. Callers must check pdf_import_is_available()
// before offering .pdf in an open dialog; file_format_entries() already does.
//
// PDF EXPORT is unrelated and always available: see ui/pdf_export.hpp.

// The Import PDF dialog's Mode choice (permanent tokens in imports/pdfColorMode: rgb,
// grayscale, cmyk, palette). Patchy edits RGB, so Grayscale is gray CONTENT in an RGB
// document (applied while the pages are read), CMYK opens the sessions in Image > Mode >
// CMYK Proof, and Palette converts to Indexed editing; the last two are applied by the
// MainWindow once the pages are documents (PdfImportResult carries the choice).
enum class PdfImportColorMode { Rgb, Grayscale, Cmyk, Palette };

// How the MainWindow folds the per-page documents of an import into ONE document once
// their text and images are rendered (GitHub issues 154 and 19, docs/pdf-import.md "Import"):
// Layers puts each page's layers in a "Page N" folder (only the first visible), Artboards
// makes each page an artboard of its own size. None opens what the import returned.
enum class PdfPagesMerge { None, Layers, Artboards };

struct PdfImportOptions {
  // 0-based page indices, in the order they should become layers. Empty means page 0.
  std::vector<int> pages;
  int resolution_ppi{300};
  bool annotations{true};
  bool anti_alias{true};
  // Crops each rendered page to its non-uniform content after rendering. Qt PDF exposes
  // only the page rect PDFium picks (CropBox intersected with MediaBox); the Art, Bleed,
  // and Trim boxes behind Photoshop's full Crop To menu are not reachable through its
  // public API, so this is Patchy's own post-render trim rather than a real BleedBox.
  bool trim_to_bounding_box{false};
  // Every selected page becomes its own document (Photoshop's Import PDF behavior)
  // instead of one layer per page on a single canvas. The first page is
  // PdfImportResult::document; the rest ride extra_documents in selection order.
  bool separate_documents{false};
  // Called before each selected page is read (1-based position in the selection,
  // selection size). Returning false stops the import after the pages already done,
  // which are kept, with a notice saying where it stopped. Empty = no reporting.
  std::function<bool(int position, int count)> progress;
  // PDFium renders onto a transparent page and PDF paints no paper; true composites
  // every page over white, the page a viewer (and Photoshop's import) shows.
  bool white_background{true};
  PdfImportColorMode color_mode{PdfImportColorMode::Rgb};
  // Applied after the pages are documents (convert_document_depth); Palette forces 8.
  BitDepth bit_depth{BitDepth::UInt8};
  // Set with separate_documents: the pages come back as documents and the MainWindow
  // merges them. The Artboards target paints no paper into the pages: the artboard's own
  // background is the paper.
  PdfPagesMerge merge{PdfPagesMerge::None};
};

// One page opened as its own document. `title` is the page label ("Page 3"); the
// caller composes the session title from it and the file name.
struct PdfImportedDocument {
  Document document;
  QString title;
};

struct PdfImportResult {
  Document document;
  // Plain-English import notes, surfaced through the same path as FormatReadResult::notices.
  std::vector<std::string> notices;
  // Set only for separate_documents imports: the first page's label, and every page
  // after it as its own document.
  QString document_title;
  std::vector<PdfImportedDocument> extra_documents;
  // What the MainWindow still has to apply once every page is a finished document
  // (pending text and images rendered): CMYK Proof, Indexed conversion, bit depth.
  PdfImportColorMode color_mode{PdfImportColorMode::Rgb};
  BitDepth bit_depth{BitDepth::UInt8};
  PdfPagesMerge merge{PdfPagesMerge::None};
  // The White page background choice, for the merged artboards' background.
  bool white_background{true};
};

// True when this build linked Qt PDF, i.e. when opening a .pdf can work at all.
[[nodiscard]] bool pdf_import_is_available();

// Marker prefix on the wasm stub's open error, heif-marker style: the open-failed box
// strips it from the display text and adds a "Get the Desktop Version" download button
// (the desktop build reads and writes PDFs; the web build can only write them).
inline constexpr std::string_view kPdfDesktopOnlyMarker = "[pdf-desktop-only]";

// Inline so the stub build gets them too: recognizing a .pdf must work even where the
// module is missing, so the open path can explain itself instead of failing obscurely.
[[nodiscard]] inline bool is_pdf_extension(const QString& extension) {
  return extension.compare(QStringLiteral("pdf"), Qt::CaseInsensitive) == 0;
}

// %PDF- magic. The spec allows the header to sit up to 1024 bytes into the file, and real
// files do carry leading junk, so scan that window instead of testing offset 0.
[[nodiscard]] inline bool bytes_look_like_pdf(std::span<const std::uint8_t> bytes) {
  static constexpr std::string_view kMagic = "%PDF-";
  const std::string_view window(reinterpret_cast<const char*>(bytes.data()),
                                std::min<std::size_t>(bytes.size(), 1024 + kMagic.size()));
  return window.find(kMagic) != std::string_view::npos;
}

// Non-interactive render (CLI opens, tests, the document-tab Reopen command). password may
// be empty. Returns nullopt with a message in *error on failure.
[[nodiscard]] std::optional<PdfImportResult> load_pdf_document(const QString& path, const PdfImportOptions& options,
                                                               const QString& password, QString* error);

// The Import PDF dialog: page thumbnails with multi-select, target resolution, annotation
// and anti-alias toggles, bounding-box trim, and a password prompt for encrypted files.
// nullopt means the user cancelled or the file could not be opened (the dialog reports).
[[nodiscard]] std::optional<PdfImportResult> run_pdf_import_dialog(QWidget* parent, const QString& path);

}  // namespace patchy::ui
