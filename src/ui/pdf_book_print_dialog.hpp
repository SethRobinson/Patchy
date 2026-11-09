#pragma once

#include "core/book_imposition.hpp"

#include <QImage>
#include <QPageSize>
#include <QString>
#include <QStringList>

#include <functional>
#include <vector>

class QPdfDocument;
class QWidget;

namespace patchy::ui {

// The PDF viewer's Print as Book (docs/pdf-book-print.md): the document's pages imposed on
// landscape sheets by core/book_imposition, previewed sheet by sheet with assembly steps,
// then printed or saved as a PDF of sheets. Built only with Qt PDF (PATCHY_HAVE_QT_PDF).

struct PdfBookPrintOptions {
  BookPrintStyle style{BookPrintStyle::Booklet};
  int sheets_per_signature{4};
  QPageSize::PageSizeId paper{QPageSize::Letter};  // used landscape
  int first_page{0};  // 0-based, inclusive
  int last_page{-1};  // -1 = the last page
  bool right_to_left{false};
  bool last_page_is_back_cover{false};
  bool fill{false};
  double outer_margin_mm{5.0};
  double spine_margin_mm{0.0};
  bool creep{true};
  double paper_thickness_mm{0.1};
  // Fold and cut ticks at the sheet edges, the zine's cut line, signature collation marks.
  bool marks{true};
  // For printers that flip on the long edge: every back is turned 180 degrees.
  bool turn_backs{false};
  bool manual_duplex{false};
  bool reverse_backs{false};
  int dpi{300};
  bool steps_page{false};
  QString quality_id{QStringLiteral("high")};
};

// The PDF page indices the options print, in order.
[[nodiscard]] std::vector<int> book_source_pages(const PdfBookPrintOptions& options, int page_count);
[[nodiscard]] BookImposition book_imposition_for(const PdfBookPrintOptions& options, int page_count);
// The landscape sheet in points.
[[nodiscard]] QSizeF book_sheet_points(const PdfBookPrintOptions& options);

// One side of one sheet as an opaque RGB image of the whole sheet at `dpi`.
[[nodiscard]] QImage render_book_sheet_side(QPdfDocument& document, const PdfBookPrintOptions& options,
                                            const BookImposition& imposition,
                                            const std::vector<int>& source_pages, int sheet, bool back, double dpi);

// The assembly steps for these options, one paragraph each, numbers filled in.
[[nodiscard]] QStringList book_assembly_steps(const PdfBookPrintOptions& options, const BookImposition& imposition,
                                              int page_count);

// Called after each page of the file is written; false cancels.
using PdfBookProgress = std::function<bool(int done, int count)>;
struct PdfBookWriteResult {
  bool ok{false};
  bool cancelled{false};
  QString error;
  int pages{0};
};
// The imposed sheets as a PDF: front, back, front, back ... (fronts only for one-sided
// styles), plus the steps page when asked. Written through pdf::ImageWriter, so an existing
// file is replaced only when the whole book is written.
[[nodiscard]] PdfBookWriteResult write_pdf_book(QPdfDocument& document, const PdfBookPrintOptions& options,
                                                const QString& path, const PdfBookProgress& progress = {});

[[nodiscard]] PdfBookPrintOptions load_pdf_book_print_options();
void store_pdf_book_print_options(const PdfBookPrintOptions& options);

struct PdfBookPrintStart {
  bool right_to_left{false};
  // The viewer showed the cover alone: a comic or book, so the last page is its back cover.
  bool cover_alone{false};
};

void run_pdf_book_print_dialog(QWidget* parent, QPdfDocument& document, const QString& path,
                               const PdfBookPrintStart& start);

}  // namespace patchy::ui
