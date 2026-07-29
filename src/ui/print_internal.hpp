#pragma once

#include "ui/print_dialog.hpp"

#include <QPageLayout>

#include <cmath>

// Shared between print_layout.cpp (portable placement/rendering, compiled on
// every platform) and print_dialog.cpp (the QtPrintSupport dialog TU, which
// the wasm build replaces with print_dialog_wasm.cpp). Internal seam only;
// nothing here is part of the print_dialog.hpp API.
namespace patchy::ui::print_detail {

inline double sanitized_ppi(double value) noexcept {
  return std::isfinite(value) && value > 0.0 ? value : 300.0;
}

inline double document_horizontal_ppi(const Document& document) noexcept {
  return sanitized_ppi(document.print_settings().horizontal_ppi);
}

inline double document_vertical_ppi(const Document& document) noexcept {
  return sanitized_ppi(document.print_settings().vertical_ppi);
}

inline QPageLayout valid_page_layout(QPageLayout page_layout) {
  return page_layout.isValid() ? page_layout : default_print_page_layout();
}

}  // namespace patchy::ui::print_detail
