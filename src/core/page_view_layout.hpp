#pragma once

#include <optional>
#include <span>
#include <vector>

namespace patchy {

// Page layout for the PDF viewer (docs/pdf-viewer.md): which pages share a row, where every
// page sits on the scrollable content, and the zoom that fits a row to the window. Qt-free so
// the rules are pinned by core tests; the view only scales and paints what this returns.

// How pages pair up on a row.
enum class PageSpreadMode {
  Single,              // one page per row
  TwoPages,            // 1-2, 3-4, 5-6 ...
  TwoPagesCoverAlone,  // 1, 2-3, 4-5 ... (a book or comic opened flat: the cover is a right-hand page)
};

// Right to left puts the earlier page of a pair on the right (manga, Arabic, Hebrew).
enum class PageReadingDirection { LeftToRight, RightToLeft };

struct PageSizePoints {
  double width{0.0};
  double height{0.0};
};

struct PageViewRect {
  double x{0.0};
  double y{0.0};
  double width{0.0};
  double height{0.0};
  [[nodiscard]] double right() const { return x + width; }
  [[nodiscard]] double bottom() const { return y + height; }
};

// One row: its pages left to right on screen (one or two page indices).
struct PageSpread {
  std::vector<int> pages;
};

// The rows of a document with `page_count` pages, in reading order.
[[nodiscard]] std::vector<PageSpread> page_spreads(int page_count, PageSpreadMode mode,
                                                   PageReadingDirection direction);
// The row that holds `page`, or 0 for an out-of-range page.
[[nodiscard]] int spread_index_for_page(std::span<const PageSpread> spreads, int page);

struct PageViewLayoutOptions {
  PageSpreadMode spread{PageSpreadMode::Single};
  PageReadingDirection direction{PageReadingDirection::LeftToRight};
  // Clockwise quarter turns applied to every page (0 to 3).
  int quarter_turns{0};
  // Screen pixels around the content and between rows; never scaled by the zoom. The two
  // pages of a spread touch, so a picture across the fold reads as one.
  double margin{16.0};
  double row_gap{16.0};
};

struct PageViewSlot {
  int page{0};
  int row{0};
  PageViewRect rect;  // content coordinates, pixels
};

struct PageViewRow {
  PageViewRect rect;
  std::vector<int> slot_indices;  // indices into PageViewLayout::pages
};

struct PageViewLayout {
  std::vector<PageViewSlot> pages;
  std::vector<PageViewRow> rows;
  double width{0.0};
  double height{0.0};
  // The row index of rows[0] (non-zero when only one row is laid out).
  int first_row{0};
};

// The page size after the view's quarter turns.
[[nodiscard]] PageSizePoints turned_page_size(PageSizePoints size, int quarter_turns);

// Lays out every row (continuous scrolling), or only `only_row` (one row at a time), at
// `pixels_per_point`. Rows are centred across max(viewport_width, widest row + margins) and
// centred vertically in viewport_height when shorter (only_row only). Pages of one spread are
// centred on the row's height. A page size that is not positive counts as US Letter.
[[nodiscard]] PageViewLayout layout_page_view(std::span<const PageSizePoints> sizes,
                                              std::span<const PageSpread> spreads,
                                              const PageViewLayoutOptions& options, double pixels_per_point,
                                              double viewport_width, double viewport_height,
                                              std::optional<int> only_row = std::nullopt);

// The row's size in points (pages side by side, the tallest page's height).
[[nodiscard]] PageSizePoints spread_size_points(std::span<const PageSizePoints> sizes, const PageSpread& spread,
                                                int quarter_turns);

enum class PageFitMode { None, WholePage, Width };

// Pixels per point that fit `spread` inside the viewport less margins (WholePage) or across
// it (Width). Returns 0 when nothing fits (an empty viewport).
[[nodiscard]] double fit_pixels_per_point(std::span<const PageSizePoints> sizes, const PageSpread& spread,
                                          const PageViewLayoutOptions& options, PageFitMode mode,
                                          double viewport_width, double viewport_height);

// Zoom steps for Zoom In / Zoom Out, in percent of actual size, ascending.
[[nodiscard]] std::span<const double> page_view_zoom_steps();
// The next step above (direction > 0) or below `percent`, clamped to the ends.
[[nodiscard]] double next_page_view_zoom(double percent, int direction);

}  // namespace patchy
