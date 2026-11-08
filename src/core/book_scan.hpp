#pragma once

#include "core/layer.hpp"
#include "core/pixel_buffer.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

// Scan a Book to PDF: the still-image geometry behind the book scanning wizard
// (docs/book-scan.md). Qt-free.
//
// A cover scan (the open book lying face down, back cover, spine and front cover on
// the glass) gives the book's outline and an estimate of the page size. Each spread
// scan gives the spread's outer rectangle and the gutter, which splits it into a left
// and a right page. Every page is then cut at ONE uniform size, anchored at its outer
// fore-edge (the gutter edge bends under the binding and is unreliable), so the PDF's
// pages all match.
//
// All geometry works on an UPRIGHT scan: the caller turns the scan by whole quarter
// turns first (rotated_quarter_turns, the book's top edge pointing up), so a "left
// page" is always the left half of the spread.
//
// Legal boundary (docs/legal-constraints.md, "Book scanning"): detection runs once per
// explicit scan or dialog parameter change on a still image, with fixed deterministic
// geometry and global per-book parameters, and every box stays user-editable. Never
// add page-curvature dewarping, finger removal, automatic capture, ML or content
// classification, per-region parameter adaptation (adaptive thresholds included), or
// automatic selection between algorithms.
namespace patchy {

// A rectangle turned by angle_degrees about its center, in source pixels (pixel-corner
// convention, y down). A positive angle turns the box's x axis clockwise on screen.
struct OrientedBox {
  double center_x{0.0};
  double center_y{0.0};
  double width{0.0};
  double height{0.0};
  double angle_degrees{0.0};
};

bool operator==(const OrientedBox& lhs, const OrientedBox& rhs);

// Corners TL, TR, BR, BL as (x, y) pairs.
[[nodiscard]] std::array<double, 8> oriented_box_corners(const OrientedBox& box);
// The box whose corners are closest to a min-area-rect quad (TL, TR, BR, BL).
[[nodiscard]] OrientedBox oriented_box_from_quad(const std::array<double, 8>& quad);
// The integer axis-aligned bounds of the box's corners (not clipped).
[[nodiscard]] Rect oriented_box_bounds(const OrientedBox& box);
// True when some corner lies within `margin` pixels of the image edge or outside it:
// the book may extend past the scanned area.
[[nodiscard]] bool oriented_box_touches_edge(const OrientedBox& box, std::int32_t image_width,
                                             std::int32_t image_height, double margin);

struct BookDetectOptions {
  int sensitivity{50};     // 0..100, as Divide Scanned Photos
  double source_ppi{0.0};  // 0 = unknown
};

// The largest object on the scanner background (the open book), or nullopt when
// nothing was found. Uses the Divide Scanned Photos detector (core/photo_divide.hpp).
[[nodiscard]] std::optional<OrientedBox> detect_book_outline(const PixelBuffer& source,
                                                             const BookDetectOptions& options,
                                                             const std::function<bool()>& cancelled = {});

enum class BookPageSide : std::uint8_t { Left, Right };

// The outside of the open book: outline split into back cover, spine and front cover.
// The spine edges are fractions of the outline width from its left edge.
struct CoverLayout {
  OrientedBox outline;
  double spine_left{0.49};
  double spine_right{0.51};
};

[[nodiscard]] CoverLayout default_cover_layout(const OrientedBox& outline);
// The part of the outline left of the spine (Left) or right of it (Right).
[[nodiscard]] OrientedBox cover_half_box(const CoverLayout& cover, BookPageSide side);

// One open spread: its outer rectangle and the gutter, a fraction of the spread width
// from its left edge.
struct SpreadLayout {
  OrientedBox spread;
  double gutter{0.5};
};

// Darkest column of a smoothed brightness profile inside the central 30% of the
// straightened spread; 0.5 when the profile shows no clear fold.
[[nodiscard]] double detect_gutter_fraction(const PixelBuffer& source, const OrientedBox& spread);

// The spread's outline and gutter. When no plausible outline is found (white pages on
// a white lid), `fallback` (normally the previous spread or the cover outline) is kept
// and only the gutter is detected inside it; with neither, the whole image is the spread.
[[nodiscard]] SpreadLayout detect_spread(const PixelBuffer& source, const BookDetectOptions& options,
                                         const std::optional<OrientedBox>& fallback,
                                         const std::function<bool()>& cancelled = {});

// Page rectangles as found in a spread (before uniform sizing).
[[nodiscard]] OrientedBox spread_page_box(const SpreadLayout& layout, BookPageSide side);

// Page size in inches.
struct BookPageSize {
  double width{0.0};
  double height{0.0};
};

// Median width and median height of the measured pages (each axis on its own; an even
// count averages the two middle values). Zero when `sizes` is empty.
[[nodiscard]] BookPageSize median_page_size(std::vector<BookPageSize> sizes);
[[nodiscard]] BookPageSize largest_page_size(const std::vector<BookPageSize>& sizes);
// The estimate from a cover: half the outline minus the spine, full outline height.
[[nodiscard]] BookPageSize cover_page_size(const CoverLayout& cover, double ppi);

// The box a page is cut with: `size_px` exactly, the spread's angle, centered
// vertically on the spread and anchored at the page's fore-edge (left edge for a left
// page, right edge for a right page).
[[nodiscard]] OrientedBox uniform_spread_page_box(const SpreadLayout& layout, BookPageSide side,
                                                  double width_px, double height_px);
// A page box of `size_px` centered where `measured` is (single-page scans, covers).
[[nodiscard]] OrientedBox centered_page_box(const OrientedBox& measured, double width_px,
                                            double height_px);

// Cuts `box` out of an 8-bit gray, RGB or RGBA source into an image of out_width x
// out_height (scaling when they differ from the box's size: area-averaged
// supersampling when shrinking). Everything outside the source is white; alpha is
// composited over white. The result is RGB8, or Gray8 for a gray source. Returns an
// empty buffer for unsupported input.
[[nodiscard]] PixelBuffer extract_book_page(const PixelBuffer& source, const OrientedBox& box,
                                            std::int32_t out_width, std::int32_t out_height);

enum class BookOutputMode : std::uint8_t { Color, Grayscale, BlackAndWhite };

struct BookOutputOptions {
  BookOutputMode mode{BookOutputMode::Color};
  int black_point{0};    // 0..254
  int white_point{255};  // black_point+1..255
  int threshold{160};    // Black and White: luminance at or above is white
};

// Global levels, then the output mode: Color stays RGB8, Grayscale and Black and White
// become Gray8 (fixed Rec. 601 weights); Black and White holds only 0 and 255.
[[nodiscard]] PixelBuffer apply_book_output(const PixelBuffer& page, const BookOutputOptions& options);

// Share of sampled pixels whose chroma (max - min channel) exceeds a fixed level. Used
// only to OFFER grayscale capture; the user decides.
[[nodiscard]] double book_page_color_fraction(const PixelBuffer& page);
// True when book_page_color_fraction is below the fixed suggestion level.
[[nodiscard]] bool book_page_looks_grayscale(const PixelBuffer& page);

// Whether a book `width` x `height` inches (the open spread) fits on a bed with
// `margin` inches free on every side, in either orientation.
[[nodiscard]] bool book_spread_fits_bed(double width, double height, double bed_width,
                                        double bed_height, double margin);

// Maps an axis-aligned rect in an image turned `clockwise_turns` quarter turns (by
// rotated_quarter_turns) back to the unturned image of raw_width x raw_height.
[[nodiscard]] Rect unturned_rect(const Rect& turned, int clockwise_turns, std::int32_t raw_width,
                                 std::int32_t raw_height);

}  // namespace patchy
