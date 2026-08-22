#pragma once

#include "core/document.hpp"

#include <QString>

#include <algorithm>
#include <cstddef>
#include <cstdint>
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
};

struct PdfImportResult {
  Document document;
  // Plain-English import notes, surfaced through the same path as FormatReadResult::notices.
  std::vector<std::string> notices;
};

// True when this build linked Qt PDF, i.e. when opening a .pdf can work at all.
[[nodiscard]] bool pdf_import_is_available();

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
