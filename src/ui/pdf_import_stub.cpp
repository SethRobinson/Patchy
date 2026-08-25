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
#ifdef Q_OS_WASM
    // The marker makes show_open_failed_message_box add a download button; a web
    // visitor has a real fix (the desktop build), so point straight at it.
    *error = QString::fromUtf8(kPdfDesktopOnlyMarker.data(),
                               static_cast<qsizetype>(kPdfDesktopOnlyMarker.size())) +
             QLatin1Char(' ') +
             QObject::tr("Only the desktop version of Patchy can import PDF files. "
                         "All versions, including this one, can export PDF.");
#else
    // A desktop Qt installed without the optional Qt PDF add-on.
    *error = QObject::tr("This build of Patchy cannot open PDF files.");
#endif
  }
  return std::nullopt;
}

std::optional<PdfImportResult> run_pdf_import_dialog(QWidget*, const QString&) {
  return std::nullopt;
}

}  // namespace patchy::ui
