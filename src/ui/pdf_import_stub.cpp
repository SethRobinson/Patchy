#include "ui/pdf_import.hpp"

#include <QObject>

// Built instead of pdf_import.cpp wherever the optional Qt PDF add-on is missing: the
// WebAssembly kit (Qt publishes no wasm qtpdf) and any desktop Qt installed without the
// module. Mirrors print_dialog_wasm.cpp. Only the extension and magic helpers stay real,
// so callers can still recognize a .pdf and say why it cannot be opened.

namespace patchy::ui {

bool pdf_import_is_available() {
  return false;
}

std::optional<PdfImportResult> load_pdf_document(const QString&, const PdfImportOptions&, const QString&,
                                                 QString* error) {
  if (error != nullptr) {
    *error = QObject::tr("This build of Patchy cannot open PDF files.");
  }
  return std::nullopt;
}

std::optional<PdfImportResult> run_pdf_import_dialog(QWidget*, const QString&) {
  return std::nullopt;
}

}  // namespace patchy::ui
