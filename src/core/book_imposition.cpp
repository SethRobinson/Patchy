#include "core/book_imposition.hpp"

#include <algorithm>

namespace patchy {

namespace {

// Nested sheets of one signature holding `pages` pages from `base`, outermost first. Each
// sheet has the signature's last unplaced page beside its first, and on the back the page
// after the first beside the page before the last: 8 pages give [8|1][2|7] and [6|3][4|5]
// (one-based) for left-to-right reading, mirrored for right to left.
void add_nested_sheets(BookImposition& imposition, int base, int pages, int signature, bool right_to_left) {
  const int sheets = pages / 4;
  for (int sheet = 0; sheet < sheets; ++sheet) {
    const int outer_front = base + pages - 1 - 2 * sheet;
    const int inner_front = base + 2 * sheet;
    const int inner_back = base + 2 * sheet + 1;
    const int outer_back = base + pages - 2 - 2 * sheet;
    BookSheet printed;
    printed.signature = signature;
    printed.sheet_in_signature = sheet;
    printed.signature_sheets = sheets;
    if (right_to_left) {
      printed.front = {{inner_front, 0, 0, false}, {outer_front, 1, 0, false}};
      printed.back = {{outer_back, 0, 0, false}, {inner_back, 1, 0, false}};
    } else {
      printed.front = {{outer_front, 0, 0, false}, {inner_front, 1, 0, false}};
      printed.back = {{inner_back, 0, 0, false}, {outer_back, 1, 0, false}};
    }
    imposition.sheets.push_back(std::move(printed));
  }
}

}  // namespace

bool book_page_is_spread(const BookPageSize& size) {
  // Two tall pages side by side are at least about 1.2 times as wide as tall (two square
  // pages make 2); a little past square keeps a near-square single page whole.
  return size.height > 0.0 && size.width > size.height * 1.05;
}

std::vector<BookSourcePage> book_source_pages(const std::vector<BookPageSize>& sizes, int first, int last,
                                              BookSpreads spreads, bool right_to_left) {
  std::vector<BookSourcePage> pages;
  const int count = static_cast<int>(sizes.size());
  if (count <= 0) {
    return pages;
  }
  first = std::clamp(first, 0, count - 1);
  last = last < 0 ? count - 1 : std::clamp(last, first, count - 1);
  const BookPagePart reading_first = right_to_left ? BookPagePart::Right : BookPagePart::Left;
  const BookPagePart reading_second = right_to_left ? BookPagePart::Left : BookPagePart::Right;
  std::vector<BookSourcePage> back_cover;
  for (int page = first; page <= last; ++page) {
    const bool spread = spreads != BookSpreads::None && book_page_is_spread(sizes[static_cast<std::size_t>(page)]);
    if (!spread) {
      pages.push_back({page, BookPagePart::Whole});
    } else if (page == first && spreads == BookSpreads::SplitCoverSpread) {
      // Laid flat, the outside of a cover shows the back cover where the book's last page
      // sits and the front cover where its first page would be read.
      pages.push_back({page, reading_second});
      back_cover.push_back({page, reading_first});
    } else {
      pages.push_back({page, reading_first});
      pages.push_back({page, reading_second});
    }
  }
  pages.insert(pages.end(), back_cover.begin(), back_cover.end());
  return pages;
}

double book_source_page_width(BookPagePart part, double page_width) {
  return part == BookPagePart::Whole ? page_width : page_width / 2.0;
}

int book_page_multiple(BookPrintStyle style) {
  return style == BookPrintStyle::MiniZine ? 8 : 4;
}

BookBindingEdge book_binding_edge(int book_page, bool right_to_left) {
  const bool right_hand = book_page % 2 == 0;
  if (right_to_left) {
    return right_hand ? BookBindingEdge::Right : BookBindingEdge::Left;
  }
  return right_hand ? BookBindingEdge::Left : BookBindingEdge::Right;
}

BookImposition impose_book(const BookImpositionOptions& options) {
  BookImposition imposition;
  const int count = std::max(0, options.page_count);
  if (count == 0) {
    return imposition;
  }
  const int multiple = book_page_multiple(options.style);
  const int padded = (count + multiple - 1) / multiple * multiple;
  imposition.blank_pages = padded - count;
  imposition.book_pages.reserve(static_cast<std::size_t>(padded));
  const bool blanks_before_last = options.last_page_is_back_cover && count >= 2;
  for (int page = 0; page < count; ++page) {
    if (blanks_before_last && page == count - 1) {
      imposition.book_pages.insert(imposition.book_pages.end(), static_cast<std::size_t>(imposition.blank_pages), -1);
    }
    imposition.book_pages.push_back(page);
  }
  if (!blanks_before_last) {
    imposition.book_pages.insert(imposition.book_pages.end(), static_cast<std::size_t>(imposition.blank_pages), -1);
  }

  switch (options.style) {
    case BookPrintStyle::Booklet:
      add_nested_sheets(imposition, 0, padded, 0, options.right_to_left);
      imposition.signatures = 1;
      break;
    case BookPrintStyle::Signatures: {
      const int sheets = std::clamp(options.sheets_per_signature, 1, 16);
      const int per_signature = sheets * 4;
      int signature = 0;
      for (int base = 0; base < padded; base += per_signature) {
        add_nested_sheets(imposition, base, std::min(per_signature, padded - base), signature, options.right_to_left);
        ++signature;
      }
      imposition.signatures = signature;
      break;
    }
    case BookPrintStyle::CutAndStack: {
      // Leaf n (one half of a sheet) carries pages 2n and 2n + 1. The left halves of the
      // stack hold the first half of the leaves in order, the right halves the second half,
      // so cutting the whole stack and putting the right pile under the left reads in order.
      const int sheets = padded / 4;
      for (int sheet = 0; sheet < sheets; ++sheet) {
        const int left_leaf = sheet;
        const int right_leaf = sheets + sheet;
        BookSheet printed;
        printed.sheet_in_signature = sheet;
        printed.signature_sheets = sheets;
        printed.front = {{2 * left_leaf, 0, 0, false}, {2 * right_leaf, 1, 0, false}};
        // Turned over, the back of the left half is on the right.
        printed.back = {{2 * right_leaf + 1, 0, 0, false}, {2 * left_leaf + 1, 1, 0, false}};
        imposition.sheets.push_back(std::move(printed));
      }
      break;
    }
    case BookPrintStyle::MiniZine: {
      // Four columns by two rows on one side. Folded in half lengthwise with the print
      // outside, cut along the middle of the two centre panels, and pushed together, the
      // panels read: bottom right = front cover, top row (upside down) = 2, 3, 4, 5 from the
      // right, bottom left = 6, 7, then the back cover beside the front.
      imposition.columns = 4;
      imposition.rows = 2;
      imposition.two_sided = false;
      for (int base = 0; base < padded; base += 8) {
        BookSheet printed;
        printed.signature = base / 8;
        const int top[4] = {base + 4, base + 3, base + 2, base + 1};
        const int bottom[4] = {base + 5, base + 6, base + 7, base + 0};
        for (int column = 0; column < 4; ++column) {
          const int sheet_column = options.right_to_left ? 3 - column : column;
          printed.front.push_back({top[column], sheet_column, 0, true});
          printed.front.push_back({bottom[column], sheet_column, 1, false});
        }
        imposition.sheets.push_back(std::move(printed));
      }
      imposition.signatures = static_cast<int>(imposition.sheets.size());
      break;
    }
  }
  return imposition;
}

BookPagePlacement place_book_page(const BookRect& cell, double page_width, double page_height,
                                  BookBindingEdge binding, const BookPagePlacementOptions& options) {
  BookRect area = cell;
  const double outer = std::max(0.0, options.outer_margin);
  const double spine = std::max(0.0, options.spine_margin);
  area.y += outer;
  area.height -= 2.0 * outer;
  area.width -= outer + spine;
  if (binding == BookBindingEdge::Left) {
    area.x += spine;
  } else {
    area.x += outer;
  }
  if (area.width <= 0.0 || area.height <= 0.0) {
    area = cell;
  }
  BookPagePlacement placement;
  if (!(page_width > 0.0) || !(page_height > 0.0)) {
    placement.page = area;
    placement.clip = area;
    return placement;
  }
  const double across = area.width / page_width;
  const double down = area.height / page_height;
  const double scale = options.fill ? std::max(across, down) : std::min(across, down);
  placement.page.width = page_width * scale;
  placement.page.height = page_height * scale;
  placement.page.y = area.y + (area.height - placement.page.height) / 2.0;
  if (options.fill) {
    placement.page.x = area.x + (area.width - placement.page.width) / 2.0;
  } else if (binding == BookBindingEdge::Left) {
    placement.page.x = area.x;
  } else {
    placement.page.x = area.x + area.width - placement.page.width;
  }
  const double shift = binding == BookBindingEdge::Left ? -options.creep_shift : options.creep_shift;
  placement.page.x += shift;
  placement.clip = area;
  placement.clip.x += shift;
  // The clip stays inside the cell: a page never prints onto its neighbour.
  const double left = std::max(cell.x, placement.clip.x);
  const double right = std::min(cell.x + cell.width, placement.clip.x + placement.clip.width);
  placement.clip.x = left;
  placement.clip.width = std::max(0.0, right - left);
  return placement;
}

}  // namespace patchy
