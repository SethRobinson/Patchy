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
