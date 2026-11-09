#pragma once

#include <vector>

namespace patchy {

// Imposition for the PDF viewer's Print as Book (docs/pdf-book-print.md): which page goes in
// which cell of which side of which sheet, so that the printed stack folds, cuts or stacks
// into a book in reading order. Qt-free; the dialog renders and prints what this returns.
//
// Page numbers here are positions in the book (0 = the first page printed, the front cover),
// not PDF page indices; -1 is a blank page the imposition added. Sides are described as seen
// from the printed side with the sheet in landscape, after a turn over its vertical axis for
// a back (what a duplex printer set to "flip on short edge" produces).

enum class BookPrintStyle {
  // Fold-and-staple booklet (saddle stitch): every sheet nested in one stack, folded once.
  Booklet,
  // Sewn or glued book: groups of nested sheets (signatures), each folded on its own and
  // stacked spine to spine.
  Signatures,
  // Two pages per side, cut down the middle and stacked: single leaves for glue, comb or
  // staple binding along one edge.
  CutAndStack,
  // Eight-page mini zine from one sheet printed on one side: fold, one cut, fold.
  MiniZine,
};

enum class BookBindingEdge { Left, Right };

struct BookImpositionOptions {
  BookPrintStyle style{BookPrintStyle::Booklet};
  int page_count{0};
  // Signatures only: sheets folded together, 1 to 16 (4 sheets make 16 pages).
  int sheets_per_signature{4};
  bool right_to_left{false};
  // Blanks needed to fill the last sheet go before the last page instead of after it, so
  // that page stays the outside back cover.
  bool last_page_is_back_cover{false};
};

struct BookSheetSlot {
  int book_page{-1};  // position in book_pages; book_pages[book_page] is the source page
  int column{0};
  int row{0};
  // Printed upside down (the zine's top row), so it reads upright once folded.
  bool upside_down{false};
};

struct BookSheet {
  std::vector<BookSheetSlot> front;
  std::vector<BookSheetSlot> back;  // empty for one-sided styles
  int signature{0};
  int sheet_in_signature{0};  // 0 = the outermost sheet of its signature
  int signature_sheets{1};
};

struct BookImposition {
  int columns{2};
  int rows{1};
  bool two_sided{true};
  // Reading order: source page index (0 to page_count - 1) or -1 for an added blank.
  std::vector<int> book_pages;
  std::vector<BookSheet> sheets;
  int signatures{1};
  int blank_pages{0};
};

[[nodiscard]] BookImposition impose_book(const BookImpositionOptions& options);

// Source PDFs that already hold two book pages side by side on each page (a scanned booklet
// laid flat): the book's pages are the halves of every wide page.
enum class BookSpreads {
  None,  // every PDF page is one book page
  Split,  // wide pages are a left and a right page; tall pages stay whole
  // As Split, and the first page is the outside of the cover laid flat: front cover and back
  // cover side by side, so its other half becomes the last page of the book.
  SplitCoverSpread,
};
enum class BookPagePart { Whole, Left, Right };
// One page of the book as it comes from the source: a whole PDF page or one half of it.
struct BookSourcePage {
  int page{0};  // 0-based PDF page index
  BookPagePart part{BookPagePart::Whole};
  friend bool operator==(const BookSourcePage&, const BookSourcePage&) = default;
};
struct BookPageSize {
  double width{0.0};
  double height{0.0};
};
// A page that holds two pages side by side: clearly wider than tall.
[[nodiscard]] bool book_page_is_spread(const BookPageSize& size);
// The book's pages in reading order from PDF pages `first` to `last` (0-based, inclusive) of
// pages with `sizes`. Right to left reads a spread's right half first; a cover spread then
// has its front cover on the left.
[[nodiscard]] std::vector<BookSourcePage> book_source_pages(const std::vector<BookPageSize>& sizes, int first,
                                                            int last, BookSpreads spreads, bool right_to_left);
// The width of the part of a page the book uses: half of it for a half.
[[nodiscard]] double book_source_page_width(BookPagePart part, double page_width);

// Pages each style groups by: 4 for the folded and cut styles, 8 for the zine.
[[nodiscard]] int book_page_multiple(BookPrintStyle style);

// The edge of a page that ends up at the spine or binding: a right-hand page (front cover,
// odd page numbers counting from 1) binds on its left when reading left to right.
[[nodiscard]] BookBindingEdge book_binding_edge(int book_page, bool right_to_left);

struct BookRect {
  double x{0.0};
  double y{0.0};
  double width{0.0};
  double height{0.0};
};

struct BookPagePlacementOptions {
  double outer_margin{0.0};  // top, bottom and the edge away from the binding
  double spine_margin{0.0};  // the binding edge
  bool fill{false};          // cover the area and crop, instead of fitting the whole page
  // Moves the page toward the binding (creep compensation for nested sheets).
  double creep_shift{0.0};
};

struct BookPagePlacement {
  BookRect page;  // where the whole page is drawn (may extend past clip when filling)
  BookRect clip;  // the part of the cell the page may cover
};

// Places a page of `page_width` x `page_height` in `cell` (all in the same units, typically
// points), upright in the cell's own frame: fitted or filled into the cell less margins,
// against the binding edge horizontally and centred vertically, then shifted by creep. The
// clip never leaves the cell.
[[nodiscard]] BookPagePlacement place_book_page(const BookRect& cell, double page_width, double page_height,
                                                BookBindingEdge binding, const BookPagePlacementOptions& options);

}  // namespace patchy
