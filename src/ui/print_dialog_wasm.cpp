#include "ui/print_dialog.hpp"

// Qt does not ship QtPrintSupport for WebAssembly, so the wasm build compiles
// these inert stubs instead of print_dialog.cpp and hides the File menu's
// Print/Page Setup actions. The portable placement and rendering half of the
// print code (print_layout.cpp) stays real on every platform.
namespace patchy::ui {

void run_page_setup_dialog(QWidget* /*parent*/, QPageLayout* /*page_layout*/) {}

bool run_print_dialog(QWidget* /*parent*/, const Document& /*document*/, const QString& /*document_title*/,
                      std::optional<QRect> /*selection_bounds*/, QPageLayout* /*page_layout*/) {
  return false;
}

bool write_print_pdf(const QString& /*path*/, const Document& /*document*/, const PrintSettings& /*settings*/,
                     const QPageLayout& /*page_layout*/, const QString& /*document_name*/) {
  return false;
}

bool run_photocopy_dialog(QWidget* /*parent*/, const Document& /*document*/) {
  return false;
}

}  // namespace patchy::ui
