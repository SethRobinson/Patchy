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

// Page numbers printed under the pages: their place in the book, counting the front cover
// as 1; added blank pages get none.
enum class BookPageNumbers { None, All, SkipCovers };

struct PdfBookPrintOptions {
  BookPrintStyle style{BookPrintStyle::Booklet};
  int sheets_per_signature{4};
  QPageSize::PageSizeId paper{QPageSize::Letter};  // used landscape
  int first_page{0};  // 0-based, inclusive
  int last_page{-1};  // -1 = the last page
  // PDF pages that already hold two pages side by side. Not stored: it describes one file.
  BookSpreads spreads{BookSpreads::None};
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
  BookPageNumbers page_numbers{BookPageNumbers::None};
  QString quality_id{QStringLiteral("high")};
};

// Every page's size in points.
[[nodiscard]] std::vector<BookPageSize> book_pdf_page_sizes(QPdfDocument& document);
// The book's pages the options print, in reading order: whole PDF pages, or halves of them.
[[nodiscard]] std::vector<BookSourcePage> book_source_pages(const PdfBookPrintOptions& options,
                                                            const std::vector<BookPageSize>& page_sizes);
[[nodiscard]] BookImposition book_imposition_for(const PdfBookPrintOptions& options,
                                                 const std::vector<BookSourcePage>& source_pages);
// The landscape sheet in points.
[[nodiscard]] QSizeF book_sheet_points(const PdfBookPrintOptions& options);

// One side of one sheet as an opaque RGB image of the whole sheet at `dpi`.
[[nodiscard]] QImage render_book_sheet_side(QPdfDocument& document, const PdfBookPrintOptions& options,
                                            const BookImposition& imposition,
                                            const std::vector<BookSourcePage>& source_pages, int sheet, bool back,
                                            double dpi);

// The assembly steps for these options, numbers filled in: short titled steps, each with a
// few bullet points, then notes ("Good to know").
struct BookAssemblyStep {
  QString title;
  QStringList details;
};
struct BookAssemblyGuide {
  std::vector<BookAssemblyStep> steps;
  QStringList notes;
};
[[nodiscard]] BookAssemblyGuide book_assembly_guide(const PdfBookPrintOptions& options,
                                                    const BookImposition& imposition,
                                                    const std::vector<BookSourcePage>& source_pages);
// The guide as rich text (numbered steps, bulleted details) for the dialog and the steps page.
[[nodiscard]] QString book_assembly_html(const BookAssemblyGuide& guide);
// The guide as plain text, one line per step and bullet.
[[nodiscard]] QString book_assembly_text(const BookAssemblyGuide& guide);

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
