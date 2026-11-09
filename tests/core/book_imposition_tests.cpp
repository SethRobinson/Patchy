#include "core/book_imposition.hpp"
#include "core/page_view_layout.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

namespace {

using patchy::BookBindingEdge;
using patchy::BookImposition;
using patchy::BookImpositionOptions;
using patchy::BookPrintStyle;
using patchy::BookSheetSlot;

bool near(double a, double b, double tolerance = 1e-9) {
  return std::abs(a - b) <= tolerance;
}

// The source page in a slot (book_pages maps book positions to PDF pages; -1 is blank).
int source(const BookImposition& imposition, const BookSheetSlot& slot) {
  return imposition.book_pages.at(static_cast<std::size_t>(slot.book_page));
}

int slot_at(const std::vector<BookSheetSlot>& side, int column, int row = 0) {
  for (const auto& slot : side) {
    if (slot.column == column && slot.row == row) {
      return slot.book_page;
    }
  }
  return -100;
}

// Every book position appears exactly once over all sheets and sides.
void check_each_position_once(const BookImposition& imposition) {
  std::multiset<int> seen;
  for (const auto& sheet : imposition.sheets) {
    for (const auto& slot : sheet.front) {
      seen.insert(slot.book_page);
    }
    for (const auto& slot : sheet.back) {
      seen.insert(slot.book_page);
    }
  }
  CHECK(seen.size() == imposition.book_pages.size());
  for (int page = 0; page < static_cast<int>(imposition.book_pages.size()); ++page) {
    CHECK(seen.count(page) == 1U);
  }
}

void book_imposition_booklet_nests_sheets() {
  BookImpositionOptions options;
  options.style = BookPrintStyle::Booklet;
  options.page_count = 8;
  const auto imposition = patchy::impose_book(options);
  CHECK(imposition.sheets.size() == 2U);
  CHECK(imposition.blank_pages == 0);
  CHECK(imposition.two_sided);
  check_each_position_once(imposition);
  // [8|1] [2|7] then [6|3] [4|5], one-based.
  CHECK(slot_at(imposition.sheets[0].front, 0) == 7 && slot_at(imposition.sheets[0].front, 1) == 0);
  CHECK(slot_at(imposition.sheets[0].back, 0) == 1 && slot_at(imposition.sheets[0].back, 1) == 6);
  CHECK(slot_at(imposition.sheets[1].front, 0) == 5 && slot_at(imposition.sheets[1].front, 1) == 2);
  CHECK(slot_at(imposition.sheets[1].back, 0) == 3 && slot_at(imposition.sheets[1].back, 1) == 4);
  CHECK(imposition.sheets[1].sheet_in_signature == 1 && imposition.sheets[1].signature_sheets == 2);
  // A page and the page after it share a leaf: the back of a cell holds the next page.
  for (const auto& sheet : imposition.sheets) {
    for (const auto& front : sheet.front) {
      const int behind = slot_at(sheet.back, 1 - front.column);
      CHECK(std::abs(behind - front.book_page) == 1);
      CHECK(std::min(behind, front.book_page) % 2 == 0);
    }
  }
}

void book_imposition_booklet_right_to_left_mirrors() {
  BookImpositionOptions options;
  options.page_count = 8;
  options.right_to_left = true;
  const auto imposition = patchy::impose_book(options);
  CHECK(slot_at(imposition.sheets[0].front, 0) == 0 && slot_at(imposition.sheets[0].front, 1) == 7);
  CHECK(slot_at(imposition.sheets[0].back, 0) == 6 && slot_at(imposition.sheets[0].back, 1) == 1);
  CHECK(patchy::book_binding_edge(0, true) == BookBindingEdge::Right);
  CHECK(patchy::book_binding_edge(1, true) == BookBindingEdge::Left);
  CHECK(patchy::book_binding_edge(0, false) == BookBindingEdge::Left);
  CHECK(patchy::book_binding_edge(1, false) == BookBindingEdge::Right);
}

void book_imposition_pads_with_blanks_and_keeps_back_cover() {
  BookImpositionOptions options;
  options.page_count = 10;
  auto imposition = patchy::impose_book(options);
  CHECK(imposition.book_pages.size() == 12U);
  CHECK(imposition.blank_pages == 2);
  CHECK(imposition.book_pages[9] == 9 && imposition.book_pages[10] == -1 && imposition.book_pages[11] == -1);
  options.last_page_is_back_cover = true;
  imposition = patchy::impose_book(options);
  CHECK(imposition.book_pages[8] == 8 && imposition.book_pages[9] == -1 && imposition.book_pages[10] == -1);
  CHECK(imposition.book_pages[11] == 9);
  // The back cover shares the outside of the first sheet with the front cover.
  CHECK(source(imposition, imposition.sheets[0].front[0]) == 9);
  CHECK(source(imposition, imposition.sheets[0].front[1]) == 0);
  check_each_position_once(imposition);
  options.page_count = 0;
  CHECK(patchy::impose_book(options).sheets.empty());
}

void book_imposition_signatures_split_the_stack() {
  BookImpositionOptions options;
  options.style = BookPrintStyle::Signatures;
  options.page_count = 40;
  options.sheets_per_signature = 4;
  const auto imposition = patchy::impose_book(options);
  // 40 pages = 16 + 16 + 8.
  CHECK(imposition.signatures == 3);
  CHECK(imposition.sheets.size() == 10U);
  check_each_position_once(imposition);
  CHECK(imposition.sheets[4].signature == 1 && imposition.sheets[4].sheet_in_signature == 0);
  CHECK(imposition.sheets[8].signature == 2 && imposition.sheets[8].signature_sheets == 2);
  // The second signature starts at page 17: its outer sheet has [32|17].
  CHECK(slot_at(imposition.sheets[4].front, 0) == 31 && slot_at(imposition.sheets[4].front, 1) == 16);
  // The last signature's centre spread is 36|37 (one-based).
  CHECK(slot_at(imposition.sheets[9].back, 0) == 35 && slot_at(imposition.sheets[9].back, 1) == 36);
}

void book_imposition_cut_and_stack_reads_in_order() {
  BookImpositionOptions options;
  options.style = BookPrintStyle::CutAndStack;
  options.page_count = 12;
  const auto imposition = patchy::impose_book(options);
  CHECK(imposition.sheets.size() == 3U);
  check_each_position_once(imposition);
  // Cut every sheet, left pile on top of the right pile: leaves in order, each leaf's
  // front page followed by the page printed behind it.
  std::vector<int> leaves;
  for (int column = 0; column < 2; ++column) {
    for (const auto& sheet : imposition.sheets) {
      const int front = slot_at(sheet.front, column);
      const int back = slot_at(sheet.back, 1 - column);
      leaves.push_back(front);
      leaves.push_back(back);
    }
  }
  for (int page = 0; page < 12; ++page) {
    CHECK(leaves[static_cast<std::size_t>(page)] == page);
  }
}

void book_imposition_mini_zine_layout() {
  BookImpositionOptions options;
  options.style = BookPrintStyle::MiniZine;
  options.page_count = 6;
  auto imposition = patchy::impose_book(options);
  CHECK(imposition.columns == 4 && imposition.rows == 2 && !imposition.two_sided);
  CHECK(imposition.sheets.size() == 1U && imposition.blank_pages == 2);
  check_each_position_once(imposition);
  const auto& side = imposition.sheets[0].front;
  // Top row upside down 5 4 3 2, bottom row 6 7 back front (one-based).
  CHECK(slot_at(side, 0, 0) == 4 && slot_at(side, 1, 0) == 3 && slot_at(side, 2, 0) == 2 && slot_at(side, 3, 0) == 1);
  CHECK(slot_at(side, 0, 1) == 5 && slot_at(side, 1, 1) == 6 && slot_at(side, 2, 1) == 7 && slot_at(side, 3, 1) == 0);
  for (const auto& slot : side) {
    CHECK(slot.upside_down == (slot.row == 0));
  }
  options.right_to_left = true;
  options.page_count = 16;
  imposition = patchy::impose_book(options);
  CHECK(imposition.sheets.size() == 2U);
  const auto& mirrored = imposition.sheets[1].front;
  CHECK(slot_at(mirrored, 0, 1) == 8 && slot_at(mirrored, 3, 1) == 13 && slot_at(mirrored, 0, 0) == 9);
}

void book_imposition_places_pages_against_the_binding() {
  const patchy::BookRect cell{0.0, 0.0, 396.0, 612.0};  // half of a landscape Letter sheet
  patchy::BookPagePlacementOptions options;
  options.outer_margin = 18.0;
  // A Letter page fits by width (396 less the outer margin), against the binding (no spine
  // margin), centred vertically.
  auto placed = patchy::place_book_page(cell, 612.0, 792.0, BookBindingEdge::Left, options);
  CHECK(near(placed.page.width, 378.0) && near(placed.page.x, 0.0));
  CHECK(near(placed.page.y, 18.0 + (576.0 - 792.0 * 378.0 / 612.0) / 2.0));
  // A narrow page sits against the binding, the leftover space at the outer edge.
  placed = patchy::place_book_page(cell, 300.0, 1000.0, BookBindingEdge::Right, options);
  CHECK(near(placed.page.height, 576.0) && near(placed.page.x + placed.page.width, 396.0));
  // Creep pulls toward the binding; the clip never leaves the cell.
  options.creep_shift = 2.0;
  placed = patchy::place_book_page(cell, 612.0, 792.0, BookBindingEdge::Left, options);
  CHECK(near(placed.page.x, -2.0) && near(placed.clip.x, 0.0) && near(placed.clip.width, 376.0));
  // Fill covers the area and is centred on it.
  options.creep_shift = 0.0;
  options.fill = true;
  placed = patchy::place_book_page(cell, 612.0, 792.0, BookBindingEdge::Left, options);
  CHECK(near(placed.page.height, 576.0) && placed.page.width > 378.0);
  CHECK(near(placed.page.x + placed.page.width / 2.0, 378.0 / 2.0));
}

void page_view_spreads_pair_pages() {
  using patchy::PageReadingDirection;
  using patchy::PageSpreadMode;
  auto spreads = patchy::page_spreads(5, PageSpreadMode::TwoPages, PageReadingDirection::LeftToRight);
  CHECK(spreads.size() == 3U);
  CHECK(spreads[0].pages == std::vector<int>({0, 1}) && spreads[2].pages == std::vector<int>({4}));
  spreads = patchy::page_spreads(5, PageSpreadMode::TwoPagesCoverAlone, PageReadingDirection::RightToLeft);
  CHECK(spreads.size() == 3U);
  CHECK(spreads[0].pages == std::vector<int>({0}) && spreads[1].pages == std::vector<int>({2, 1}));
  CHECK(spreads[2].pages == std::vector<int>({4, 3}));
  CHECK(patchy::spread_index_for_page(spreads, 3) == 2 && patchy::spread_index_for_page(spreads, 9) == 0);
  spreads = patchy::page_spreads(3, PageSpreadMode::Single, PageReadingDirection::LeftToRight);
  CHECK(spreads.size() == 3U && spreads[1].pages == std::vector<int>({1}));
  CHECK(patchy::page_spreads(0, PageSpreadMode::TwoPages, PageReadingDirection::LeftToRight).empty());
}

void page_view_layout_centres_rows_and_fits() {
  using patchy::PageSizePoints;
  const std::vector<PageSizePoints> sizes{{600.0, 800.0}, {600.0, 800.0}, {300.0, 400.0}};
  const auto spreads = patchy::page_spreads(3, patchy::PageSpreadMode::TwoPages,
                                            patchy::PageReadingDirection::LeftToRight);
  patchy::PageViewLayoutOptions options;
  options.spread = patchy::PageSpreadMode::TwoPages;
  auto layout = patchy::layout_page_view(sizes, spreads, options, 0.5, 1000.0, 600.0);
  CHECK(layout.rows.size() == 2U && layout.pages.size() == 3U);
  // Row 0 is 600 x 400 pixels, centred in the 1000 wide content; its pages touch.
  CHECK(near(layout.rows[0].rect.x, 200.0) && near(layout.pages[1].rect.x, 500.0));
  CHECK(near(layout.pages[0].rect.y, 16.0) && near(layout.rows[1].rect.y, 16.0 + 400.0 + 16.0));
  CHECK(near(layout.height, 16.0 + 400.0 + 16.0 + 200.0 + 16.0));
  // One row at a time, centred vertically in the window.
  layout = patchy::layout_page_view(sizes, spreads, options, 0.5, 1000.0, 600.0, 1);
  CHECK(layout.rows.size() == 1U && layout.first_row == 1 && near(layout.height, 600.0));
  CHECK(near(layout.pages[0].rect.y, 16.0 + (600.0 - 232.0) / 2.0));
  // Fit whole page for the 1200 x 800 point spread in 1032 x 432: limited by height.
  CHECK(near(patchy::fit_pixels_per_point(sizes, spreads[0], options, patchy::PageFitMode::WholePage, 1032.0, 432.0),
             0.5));
  CHECK(near(patchy::fit_pixels_per_point(sizes, spreads[0], options, patchy::PageFitMode::Width, 1032.0, 432.0),
             0.83333333333333337));
  // A quarter turn swaps every page's sides.
  options.quarter_turns = 1;
  layout = patchy::layout_page_view(sizes, spreads, options, 1.0, 100.0, 100.0);
  CHECK(near(layout.pages[0].rect.width, 800.0) && near(layout.pages[0].rect.height, 600.0));
  CHECK(near(patchy::next_page_view_zoom(99.8, 1), 125.0) && near(patchy::next_page_view_zoom(100.0, -1), 75.0));
  CHECK(near(patchy::next_page_view_zoom(5000.0, 1), 1600.0) && near(patchy::next_page_view_zoom(1.0, -1), 10.0));
}

}  // namespace

std::vector<patchy::test::TestCase> book_imposition_tests() {
  return {
      {"book_imposition_booklet_nests_sheets", book_imposition_booklet_nests_sheets},
      {"book_imposition_booklet_right_to_left_mirrors", book_imposition_booklet_right_to_left_mirrors},
      {"book_imposition_pads_with_blanks_and_keeps_back_cover", book_imposition_pads_with_blanks_and_keeps_back_cover},
      {"book_imposition_signatures_split_the_stack", book_imposition_signatures_split_the_stack},
      {"book_imposition_cut_and_stack_reads_in_order", book_imposition_cut_and_stack_reads_in_order},
      {"book_imposition_mini_zine_layout", book_imposition_mini_zine_layout},
      {"book_imposition_places_pages_against_the_binding", book_imposition_places_pages_against_the_binding},
      {"page_view_spreads_pair_pages", page_view_spreads_pair_pages},
      {"page_view_layout_centres_rows_and_fits", page_view_layout_centres_rows_and_fits},
  };
}
