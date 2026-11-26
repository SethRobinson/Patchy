#include "core/book_scan.hpp"
#include "core/photo_divide.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {

using patchy::BookOutputMode;
using patchy::BookOutputOptions;
using patchy::BookPageSide;
using patchy::BookPageSize;
using patchy::CoverLayout;
using patchy::OrientedBox;
using patchy::PixelBuffer;
using patchy::PixelFormat;
using patchy::Rect;
using patchy::SpreadLayout;

constexpr double kPi = 3.14159265358979323846;

PixelBuffer solid_image(std::int32_t width, std::int32_t height, std::uint8_t gray) {
  PixelBuffer pixels(width, height, PixelFormat::rgb8());
  pixels.clear(gray);
  return pixels;
}

void set_rgb(PixelBuffer& pixels, std::int32_t x, std::int32_t y, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
  auto* px = pixels.pixel(x, y);
  px[0] = r;
  px[1] = g;
  px[2] = b;
}

// The box's local coordinates of a pixel center, or false when it lies outside.
bool local_of(const OrientedBox& box, double x, double y, double& local_x, double& local_y) {
  const double radians = box.angle_degrees * kPi / 180.0;
  const double c = std::cos(radians);
  const double s = std::sin(radians);
  const double dx = x - box.center_x;
  const double dy = y - box.center_y;
  local_x = dx * c + dy * s + box.width / 2.0;
  local_y = -dx * s + dy * c + box.height / 2.0;
  return local_x >= 0.0 && local_y >= 0.0 && local_x < box.width && local_y < box.height;
}

// An open book spread: off-white pages, a dark fold shadow at `gutter` (fraction of the
// width), and dark "text" bars, on a background of `background` gray.
PixelBuffer synthetic_spread(std::int32_t width, std::int32_t height, std::uint8_t background,
                             const OrientedBox& spread, double gutter) {
  PixelBuffer pixels = solid_image(width, height, background);
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      double lx = 0.0;
      double ly = 0.0;
      if (!local_of(spread, x + 0.5, y + 0.5, lx, ly)) {
        continue;
      }
      const double fold = std::abs(lx - gutter * spread.width) / spread.width;
      int value = 238;
      if (fold < 0.02) {
        value = 150 + static_cast<int>(fold * 4000.0);  // shadow deepest at the fold
      }
      const double page_x = lx < gutter * spread.width ? lx / (gutter * spread.width)
                                                        : (lx - gutter * spread.width) / ((1.0 - gutter) * spread.width);
      const bool text_area = page_x > 0.15 && page_x < 0.85 && ly > spread.height * 0.12 && ly < spread.height * 0.88;
      if (text_area && static_cast<int>(ly) % 14 < 4) {
        value = 40;
      }
      const auto v = static_cast<std::uint8_t>(std::min(value, 255));
      set_rgb(pixels, x, y, v, v, v);
    }
  }
  return pixels;
}

bool near(double a, double b, double tolerance) {
  return std::abs(a - b) <= tolerance;
}

void book_scan_outline_on_dark_background() {
  const OrientedBox truth{400.0, 300.0, 600.0, 400.0, 0.0};
  const PixelBuffer image = synthetic_spread(800, 600, 25, truth, 0.5);
  const auto outline = patchy::detect_book_outline(image, {});
  CHECK(outline.has_value());
  CHECK(near(outline->center_x, 400.0, 3.0));
  CHECK(near(outline->center_y, 300.0, 3.0));
  CHECK(near(outline->width, 600.0, 6.0));
  CHECK(near(outline->height, 400.0, 6.0));
  CHECK(outline->angle_degrees == 0.0);
}

void book_scan_outline_on_white_lid_with_skew() {
  // A colored cover on a white lid, turned 3 degrees.
  const OrientedBox truth{450.0, 320.0, 640.0, 420.0, 3.0};
  PixelBuffer image = solid_image(900, 650, 250);
  for (std::int32_t y = 0; y < image.height(); ++y) {
    for (std::int32_t x = 0; x < image.width(); ++x) {
      double lx = 0.0;
      double ly = 0.0;
      if (local_of(truth, x + 0.5, y + 0.5, lx, ly)) {
        set_rgb(image, x, y, 30, 60, 140);
      }
    }
  }
  const auto outline = patchy::detect_book_outline(image, {});
  CHECK(outline.has_value());
  CHECK(near(outline->angle_degrees, 3.0, 0.5));
  CHECK(near(outline->width, 640.0, 8.0));
  CHECK(near(outline->height, 420.0, 8.0));
  const auto layout = patchy::default_cover_layout(*outline);
  const auto back = patchy::cover_half_box(layout, BookPageSide::Left);
  const auto front = patchy::cover_half_box(layout, BookPageSide::Right);
  CHECK(near(back.width + front.width, outline->width * 0.98, 0.01));
  CHECK(front.center_x > back.center_x);
  // 100 ppi: about (640 * 0.98 / 2) / 100 by 420 / 100 inches.
  const BookPageSize estimate = patchy::cover_page_size(layout, 100.0);
  CHECK(near(estimate.width, 3.136, 0.08));
  CHECK(near(estimate.height, 4.2, 0.08));
}

void book_scan_gutter_found_off_center() {
  const OrientedBox truth{400.0, 300.0, 600.0, 400.0, 0.0};
  const PixelBuffer image = synthetic_spread(800, 600, 25, truth, 0.46);
  const double gutter = patchy::detect_gutter_fraction(image, truth);
  CHECK(near(gutter, 0.46, 0.01));
}

void book_scan_gutter_defaults_to_middle_without_fold() {
  const PixelBuffer image = solid_image(400, 300, 240);
  const OrientedBox box{200.0, 150.0, 400.0, 300.0, 0.0};
  CHECK(patchy::detect_gutter_fraction(image, box) == 0.5);
}

void book_scan_spread_detects_skewed_spread_and_pages() {
  const OrientedBox truth{450.0, 330.0, 640.0, 420.0, -2.0};
  const PixelBuffer image = synthetic_spread(900, 660, 20, truth, 0.52);
  const SpreadLayout layout = patchy::detect_spread(image, {}, std::nullopt);
  CHECK(near(layout.spread.angle_degrees, -2.0, 0.5));
  CHECK(near(layout.gutter, 0.52, 0.015));
  const OrientedBox left = patchy::spread_page_box(layout, BookPageSide::Left);
  const OrientedBox right = patchy::spread_page_box(layout, BookPageSide::Right);
  CHECK(near(left.width, 0.52 * 640.0, 12.0));
  CHECK(near(right.width, 0.48 * 640.0, 12.0));
  CHECK(left.center_x < right.center_x);
}

void book_scan_spread_keeps_fallback_on_white_lid() {
  // White pages on a white lid: no outline stands out, so the fallback is kept.
  PixelBuffer image = solid_image(800, 600, 238);
  const OrientedBox fallback{400.0, 300.0, 600.0, 400.0, 0.0};
  const SpreadLayout layout = patchy::detect_spread(image, {}, fallback);
  CHECK(layout.spread == fallback);
  CHECK(layout.gutter == 0.5);
}

void book_scan_detection_is_deterministic() {
  const OrientedBox truth{450.0, 330.0, 640.0, 420.0, 1.5};
  const PixelBuffer image = synthetic_spread(900, 660, 30, truth, 0.5);
  const SpreadLayout first = patchy::detect_spread(image, {}, std::nullopt);
  const SpreadLayout second = patchy::detect_spread(image, {}, std::nullopt);
  CHECK(first.spread == second.spread);
  CHECK(first.gutter == second.gutter);
}

void book_scan_uniform_page_size_median_and_largest() {
  const std::vector<BookPageSize> sizes{{5.9, 9.0}, {6.1, 8.9}, {6.0, 9.2}, {4.0, 9.1}};
  const BookPageSize median = patchy::median_page_size(sizes);
  CHECK(near(median.width, 5.95, 1e-9));
  CHECK(near(median.height, 9.05, 1e-9));
  const BookPageSize largest = patchy::largest_page_size(sizes);
  CHECK(largest.width == 6.1);
  CHECK(largest.height == 9.2);
  CHECK(patchy::median_page_size({}).width == 0.0);
}

void book_scan_extract_anchors_inner_edge() {
  SpreadLayout layout;
  layout.spread = {300.0, 200.0, 400.0, 200.0, 0.0};
  layout.gutter = 0.45;
  // Uniform page 190 x 180 px on a spread from x = 100 to 500 with its divider at x = 280:
  // the left page ends at the divider and the right page starts there, both vertically
  // centered. (They were anchored at the spread's outer edges before, x = 100 and 500,
  // which left the wider page short of the divider.)
  const OrientedBox left = patchy::uniform_spread_page_box(layout, BookPageSide::Left, 190.0, 180.0, 0.0);
  const OrientedBox right = patchy::uniform_spread_page_box(layout, BookPageSide::Right, 190.0, 180.0, 0.0);
  CHECK(near(left.center_x + left.width / 2.0, 280.0, 1e-9));
  CHECK(near(right.center_x - right.width / 2.0, 280.0, 1e-9));
  CHECK(near(left.center_y, 200.0, 1e-9) && near(right.center_y, 200.0, 1e-9));
  CHECK(left.width == 190.0 && left.height == 180.0);
  // A binding margin moves each inner edge that far from the divider.
  const OrientedBox left_margin = patchy::uniform_spread_page_box(layout, BookPageSide::Left, 190.0, 180.0, 10.0);
  const OrientedBox right_margin = patchy::uniform_spread_page_box(layout, BookPageSide::Right, 190.0, 180.0, 10.0);
  CHECK(near(left_margin.center_x + left_margin.width / 2.0, 270.0, 1e-9));
  CHECK(near(right_margin.center_x - right_margin.width / 2.0, 290.0, 1e-9));

  // Cutting an axis-aligned page out of a known gradient reproduces it exactly, and
  // whatever lies beyond the source is white.
  PixelBuffer source(40, 30, PixelFormat::rgb8());
  for (std::int32_t y = 0; y < 30; ++y) {
    for (std::int32_t x = 0; x < 40; ++x) {
      set_rgb(source, x, y, static_cast<std::uint8_t>(x * 5), static_cast<std::uint8_t>(y * 7), 9);
    }
  }
  const PixelBuffer page = patchy::extract_book_page(source, {15.0, 15.0, 10.0, 10.0, 0.0}, 10, 10);
  CHECK(page.width() == 10 && page.height() == 10);
  CHECK(page.pixel(0, 0)[0] == 50 && page.pixel(0, 0)[1] == 70);
  CHECK(page.pixel(9, 9)[0] == 95 && page.pixel(9, 9)[1] == 133);
  const PixelBuffer beyond = patchy::extract_book_page(source, {45.0, 15.0, 10.0, 10.0, 0.0}, 10, 10);
  CHECK(beyond.pixel(9, 5)[0] == 255 && beyond.pixel(9, 5)[1] == 255 && beyond.pixel(9, 5)[2] == 255);
  // Halving averages 2x2 blocks.
  const PixelBuffer half = patchy::extract_book_page(source, {20.0, 15.0, 40.0, 30.0, 0.0}, 20, 15);
  CHECK(half.pixel(0, 0)[0] == 3 || half.pixel(0, 0)[0] == 2);  // mean of 0 and 5
  CHECK(half.width() == 20 && half.height() == 15);
}

void book_scan_output_threshold() {
  PixelBuffer page(4, 1, PixelFormat::rgb8());
  set_rgb(page, 0, 0, 10, 10, 10);
  set_rgb(page, 1, 0, 150, 150, 150);
  set_rgb(page, 2, 0, 170, 170, 170);
  set_rgb(page, 3, 0, 255, 0, 0);
  BookOutputOptions options;
  options.mode = BookOutputMode::BlackAndWhite;
  options.threshold = 160;
  const PixelBuffer bw = patchy::apply_book_output(page, options);
  CHECK(bw.format() == PixelFormat::gray8());
  CHECK(bw.pixel(0, 0)[0] == 0);
  CHECK(bw.pixel(1, 0)[0] == 0);
  CHECK(bw.pixel(2, 0)[0] == 255);
  CHECK(bw.pixel(3, 0)[0] == 0);  // pure red is dark (luma 76)
  options.mode = BookOutputMode::Grayscale;
  options.black_point = 10;
  options.white_point = 170;
  const PixelBuffer gray = patchy::apply_book_output(page, options);
  CHECK(gray.pixel(0, 0)[0] == 0);
  CHECK(gray.pixel(2, 0)[0] == 255);
  options.mode = BookOutputMode::Color;
  options.black_point = 0;
  options.white_point = 255;
  const PixelBuffer color = patchy::apply_book_output(page, options);
  CHECK(color.format() == PixelFormat::rgb8());
  CHECK(color.pixel(3, 0)[0] == 255 && color.pixel(3, 0)[1] == 0);
}

void book_scan_grayscale_suggestion() {
  PixelBuffer page = solid_image(200, 200, 235);
  // Slightly yellowed paper stays "gray".
  for (std::int32_t y = 0; y < 200; ++y) {
    for (std::int32_t x = 0; x < 200; ++x) {
      set_rgb(page, x, y, 240, 232, 214);
    }
  }
  CHECK(patchy::book_page_looks_grayscale(page));
  for (std::int32_t y = 50; y < 100; ++y) {
    for (std::int32_t x = 50; x < 100; ++x) {
      set_rgb(page, x, y, 200, 40, 40);
    }
  }
  CHECK(!patchy::book_page_looks_grayscale(page));
}

void book_scan_spread_fits_bed() {
  // A 6 x 9 inch book opens to 12 x 9: too wide for an A4 bed (8.5 x 11.7) either way.
  CHECK(!patchy::book_spread_fits_bed(12.0, 9.0, 8.5, 11.7, 0.1));
  CHECK(patchy::book_spread_fits_bed(11.0, 8.0, 8.5, 11.7, 0.1));
  CHECK(!patchy::book_spread_fits_bed(17.0, 11.0, 8.5, 11.7, 0.1));
  CHECK(patchy::book_spread_fits_bed(17.0, 11.0, 0.0, 0.0, 0.1));  // unknown bed
}

void book_scan_unturned_rect_inverts_quarter_turns() {
  PixelBuffer raw(7, 5, PixelFormat::gray8());
  for (std::int32_t y = 0; y < 5; ++y) {
    for (std::int32_t x = 0; x < 7; ++x) {
      raw.pixel(x, y)[0] = static_cast<std::uint8_t>(y * 7 + x);
    }
  }
  for (int turns = 0; turns < 4; ++turns) {
    const PixelBuffer turned = patchy::rotated_quarter_turns(raw, turns);
    const Rect in_turned{1, 2, 2, 1};
    const Rect back = patchy::unturned_rect(in_turned, turns, 7, 5);
    // The single-row rect's two pixels map onto raw pixels inside `back`.
    for (std::int32_t dx = 0; dx < in_turned.width; ++dx) {
      const auto value = turned.pixel(in_turned.x + dx, in_turned.y)[0];
      const std::int32_t raw_x = value % 7;
      const std::int32_t raw_y = value / 7;
      CHECK(back.contains(raw_x, raw_y));
    }
    CHECK(back.width * back.height == 2);
  }
}

void book_scan_box_edge_and_bounds() {
  const OrientedBox box{50.0, 50.0, 40.0, 20.0, 0.0};
  const Rect bounds = patchy::oriented_box_bounds(box);
  CHECK(bounds.x == 30 && bounds.y == 40 && bounds.width == 40 && bounds.height == 20);
  CHECK(!patchy::oriented_box_touches_edge(box, 100, 100, 2.0));
  CHECK(patchy::oriented_box_touches_edge(box, 71, 100, 2.0));
  const auto round_trip = patchy::oriented_box_from_quad(patchy::oriented_box_corners({60.0, 40.0, 30.0, 10.0, 12.0}));
  CHECK(near(round_trip.angle_degrees, 12.0, 1e-9));
  CHECK(near(round_trip.width, 30.0, 1e-9));
  CHECK(near(round_trip.height, 10.0, 1e-9));
}

// Turning the whole book half a turn keeps every edit: a page's corner lands where the
// turned image puts that pixel, and the gutter and spine lines mirror.
void book_scan_half_turn_keeps_layouts() {
  const std::int32_t width = 300;
  const std::int32_t height = 200;
  const SpreadLayout spread{{120.0, 90.0, 160.0, 100.0, 4.0}, 0.4};
  const SpreadLayout turned = patchy::half_turned_spread(spread, width, height);
  CHECK(near(turned.gutter, 0.6, 1e-12));
  CHECK(near(turned.spread.angle_degrees, 4.0, 1e-12));
  const auto before = patchy::oriented_box_corners(spread.spread);
  const auto after = patchy::oriented_box_corners(turned.spread);
  // A half turn maps (x, y) to (w - x, h - y): the old top-left corner is the new bottom-right.
  CHECK(near(after[4], width - before[0], 1e-9) && near(after[5], height - before[1], 1e-9));
  CHECK(near(after[0], width - before[4], 1e-9) && near(after[1], height - before[5], 1e-9));
  // Two half turns give the layout back.
  const SpreadLayout back = patchy::half_turned_spread(turned, width, height);
  CHECK(back.spread == spread.spread && near(back.gutter, spread.gutter, 1e-12));
  const CoverLayout cover{{150.0, 100.0, 240.0, 160.0, 0.0}, 0.47, 0.52};
  const CoverLayout cover_turned = patchy::half_turned_cover(cover, width, height);
  CHECK(near(cover_turned.spine_left, 0.48, 1e-12) && near(cover_turned.spine_right, 0.53, 1e-12));
  CHECK(cover_turned.spine_left < cover_turned.spine_right);
}

// A source where every pixel says whether its center lies in a page's own part of a
// spread: 200 from the outline's outer edge to the divider less the binding margin, 0 on
// the other page, inside the margin strip and beside the outline. A page cut that takes
// nothing from past the divider or from outside the outline holds nothing below 200:
// white only raises a value.
PixelBuffer own_part_marked_source(std::int32_t width, std::int32_t height, const SpreadLayout& layout,
                                   BookPageSide side, double margin_px) {
  PixelBuffer pixels(width, height, PixelFormat::gray8());
  const double radians = layout.spread.angle_degrees * kPi / 180.0;
  const double c = std::cos(radians);
  const double s = std::sin(radians);
  const double divider = layout.gutter * layout.spread.width;
  // A hair of slack at the limits themselves, where rounding may fall either way.
  const double slack = 1e-6;
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      const double dx = x + 0.5 - layout.spread.center_x;
      const double dy = y + 0.5 - layout.spread.center_y;
      const double along = dx * c + dy * s + layout.spread.width / 2.0;
      const bool own = side == BookPageSide::Left
                           ? along >= -slack && along <= divider - margin_px + slack
                           : along >= divider + margin_px - slack && along <= layout.spread.width + slack;
      pixels.pixel(x, y)[0] = own ? 200 : 0;
    }
  }
  return pixels;
}

// Both pages follow the divider: each page's inner edge is the divider less the binding
// margin, wherever the divider is (on the fold, far left, far right), upright or tilted,
// at the scan's size, shrunk and enlarged. The page runs outward from there; nothing from
// past the divider or from outside the outline reaches it, and where its box passes the
// outline's outer edge it is white.
void book_scan_page_cut_starts_at_the_divider() {
  for (const double angle : {0.0, 7.5, -4.0}) {
    for (const double margin : {0.0, 12.0}) {
      for (const double gutter : {0.56, 0.2, 0.8}) {
        for (const BookPageSide side : {BookPageSide::Left, BookPageSide::Right}) {
          SpreadLayout layout;
          layout.spread = {320.0, 220.0, 400.0, 240.0, angle};
          layout.gutter = gutter;
          const bool left = side == BookPageSide::Left;
          const PixelBuffer source = own_part_marked_source(640, 440, layout, side, margin);
          // A page 248 px wide on a 400 px spread: narrower than the wide side of a divider
          // at 0.2 or 0.8, wider than every other side.
          const double page_width = 248.0;
          const double own = (left ? gutter : 1.0 - gutter) * 400.0 - margin;
          const double taken = std::min(own, page_width);
          const patchy::BookPageCut cut = patchy::uniform_spread_page_cut(layout, side, page_width, 240.0, margin);
          CHECK(near(patchy::spread_page_width(layout, side, margin), own, 1e-9));
          CHECK(cut.box == patchy::uniform_spread_page_box(layout, side, page_width, 240.0, margin));
          CHECK(cut.box.width == page_width && cut.box.height == 240.0 && cut.box.angle_degrees == angle);
          // The box's inner edge lies on the divider less the margin, at the spread's height.
          const double radians = angle * kPi / 180.0;
          const double dx = cut.box.center_x - layout.spread.center_x;
          const double dy = cut.box.center_y - layout.spread.center_y;
          const double center_along = dx * std::cos(radians) + dy * std::sin(radians) + 200.0;
          const double inner_edge = left ? center_along + page_width / 2.0 : center_along - page_width / 2.0;
          CHECK(near(inner_edge, left ? gutter * 400.0 - margin : gutter * 400.0 + margin, 1e-9));
          CHECK(near(-dx * std::sin(radians) + dy * std::cos(radians), 0.0, 1e-9));
          // The source range runs from the inner edge to the outline's outer edge.
          if (left) {
            CHECK(near(cut.source_from, page_width - own, 1e-9) && near(cut.source_to, page_width, 1e-9));
          } else {
            CHECK(near(cut.source_from, 0.0, 1e-9) && near(cut.source_to, own, 1e-9));
          }
          for (const double zoom : {1.0, 0.5, 1.5}) {
            const auto out_width = static_cast<std::int32_t>(std::lround(page_width * zoom));
            const auto out_height = static_cast<std::int32_t>(std::lround(240.0 * zoom));
            const PixelBuffer page = patchy::extract_book_page_cut(source, cut, out_width, out_height);
            CHECK(page.width() == out_width && page.height() == out_height);
            if (page.width() != out_width || page.height() != out_height) {
              continue;
            }
            int darkest = 255;
            for (std::int32_t y = 0; y < out_height; ++y) {
              for (std::int32_t x = 0; x < out_width; ++x) {
                darkest = std::min(darkest, static_cast<int>(page.pixel(x, y)[0]));
              }
            }
            CHECK(darkest >= 200);
            // Columns are counted from the inner edge (the divider side).
            const auto column = [&](double from_inner_edge) {
              const double x = left ? page_width - from_inner_edge : from_inner_edge;
              return std::clamp(static_cast<std::int32_t>(x * zoom), 0, out_width - 1);
            };
            const std::int32_t middle_row = out_height / 2;
            // The page starts at the divider: the scan from its first columns on.
            CHECK(page.pixel(column(4.0), middle_row)[0] == 200);
            CHECK(page.pixel(column(taken / 2.0), middle_row)[0] == 200);
            if (own < page_width - 8.0) {
              // Past the outline's outer edge the page is white.
              CHECK(page.pixel(column(own - 6.0), middle_row)[0] == 200);
              CHECK(page.pixel(column(own + 6.0), middle_row)[0] == 255);
              CHECK(page.pixel(column(page_width - 3.0), middle_row)[0] == 255);
            } else {
              // A side wider than the page: the page is the scan to its outer end.
              CHECK(page.pixel(column(page_width - 4.0), middle_row)[0] == 200);
            }
          }
          // The same cut on a copy of the scan at a third of its size (the wizard's thumbnails).
          const patchy::BookPageCut small = patchy::scaled_page_cut(cut, 1.0 / 3.0);
          CHECK(near(small.box.width, page_width / 3.0, 1e-9) && near(small.box.center_x, cut.box.center_x / 3.0, 1e-9));
          CHECK(near(small.source_from, cut.source_from / 3.0, 1e-9) && near(small.source_to, cut.source_to / 3.0, 1e-9));
          // Half a turn of the scan mirrors the divider: the same page is then the other
          // side, cut the same way.
          const SpreadLayout turned = patchy::half_turned_spread(layout, 640, 440);
          const BookPageSide other_side = left ? BookPageSide::Right : BookPageSide::Left;
          const PixelBuffer turned_source = own_part_marked_source(640, 440, turned, other_side, margin);
          const PixelBuffer turned_page = patchy::extract_book_page_cut(
              turned_source, patchy::uniform_spread_page_cut(turned, other_side, page_width, 240.0, margin), 248, 240);
          int darkest = 255;
          int own_pixels = 0;
          for (std::int32_t y = 0; y < turned_page.height(); ++y) {
            for (std::int32_t x = 0; x < turned_page.width(); ++x) {
              darkest = std::min(darkest, static_cast<int>(turned_page.pixel(x, y)[0]));
              own_pixels += turned_page.pixel(x, y)[0] == 200 ? 1 : 0;
            }
          }
          CHECK(darkest >= 200);
          CHECK(own_pixels > static_cast<int>(taken * 240.0 * 0.6));
        }
      }
    }
  }
}

// Inside its own part of the outline a page is the plain cut of its box: the limits only
// take away what lies past the divider or beside the outline. A cut without a limit is
// the plain box cut everywhere.
void book_scan_page_cut_matches_the_box_inside_the_outline() {
  SpreadLayout layout;
  layout.spread = {150.0, 100.0, 240.0, 160.0, 3.0};
  layout.gutter = 0.5;
  PixelBuffer source(300, 200, PixelFormat::rgb8());
  for (std::int32_t y = 0; y < 200; ++y) {
    for (std::int32_t x = 0; x < 300; ++x) {
      set_rgb(source, x, y, static_cast<std::uint8_t>((x * 7 + y * 3) % 251), static_cast<std::uint8_t>((x + y * 5) % 241),
              static_cast<std::uint8_t>((x * 2 + y) % 239));
    }
  }
  // True when the two pages agree on columns [from, to).
  const auto same = [](const PixelBuffer& a, const PixelBuffer& b, std::int32_t from, std::int32_t to) {
    if (a.width() != b.width() || a.height() != b.height()) {
      return false;
    }
    for (std::int32_t y = 0; y < a.height(); ++y) {
      for (std::int32_t x = from; x < to; ++x) {
        for (int channel = 0; channel < 3; ++channel) {
          if (a.pixel(x, y)[channel] != b.pixel(x, y)[channel]) {
            return false;
          }
        }
      }
    }
    return true;
  };
  for (const BookPageSide side : {BookPageSide::Left, BookPageSide::Right}) {
    const bool left = side == BookPageSide::Left;
    // 100 px wide on a 120 px side: the page ends 20 px short of the outline's outer edge.
    const OrientedBox box = patchy::uniform_spread_page_box(layout, side, 100.0, 150.0, 0.0);
    const patchy::BookPageCut cut = patchy::uniform_spread_page_cut(layout, side, 100.0, 150.0, 0.0);
    const PixelBuffer plain = patchy::extract_book_page(source, box, 100, 150);
    const PixelBuffer limited = patchy::extract_book_page_cut(source, cut, 100, 150);
    // All but the columns at the divider itself, where the limit drops the source pixels
    // that lie just past it.
    CHECK(same(plain, limited, left ? 0 : 2, left ? 98 : 100));
    CHECK(same(plain, patchy::extract_book_page_cut(source, patchy::BookPageCut{box}, 100, 150), 0, 100));
    // A margin moves the whole page outward with its inner edge: 130 px wide with a 25 px
    // margin on a 120 px side, the outer 35 px lie past the outline and are white.
    const PixelBuffer wide =
        patchy::extract_book_page_cut(source, patchy::uniform_spread_page_cut(layout, side, 130.0, 150.0, 25.0), 130, 150);
    const std::int32_t outer = left ? 2 : 127;
    const std::int32_t inner = left ? 120 : 9;
    CHECK(wide.pixel(outer, 75)[0] == 255 && wide.pixel(outer, 75)[1] == 255 && wide.pixel(outer, 75)[2] == 255);
    // Its inner columns are the scan 25 px from the divider: the plain cut of a box that
    // starts there.
    const PixelBuffer shifted =
        patchy::extract_book_page(source, patchy::uniform_spread_page_box(layout, side, 130.0, 150.0, 25.0), 130, 150);
    for (int channel = 0; channel < 3; ++channel) {
      CHECK(wide.pixel(inner, 75)[channel] == shifted.pixel(inner, 75)[channel]);
    }
  }
  // The measured width leaves the margin out and never goes below zero.
  CHECK(near(patchy::spread_page_width(layout, BookPageSide::Left, 0.0), 120.0, 1e-9));
  CHECK(near(patchy::spread_page_width(layout, BookPageSide::Right, 30.0), 90.0, 1e-9));
  CHECK(patchy::spread_page_width(layout, BookPageSide::Left, 500.0) == 0.0);
  CHECK(near(patchy::spread_page_width(layout, BookPageSide::Left, -5.0), 120.0, 1e-9));
  // A divider at the outline's edge leaves one page nothing to take: it is all white.
  layout.gutter = 0.0;
  const PixelBuffer nothing =
      patchy::extract_book_page_cut(source, patchy::uniform_spread_page_cut(layout, BookPageSide::Left, 100.0, 150.0, 10.0), 100, 150);
  bool all_white = !nothing.empty();
  for (std::int32_t y = 0; y < nothing.height() && all_white; ++y) {
    for (std::int32_t x = 0; x < nothing.width(); ++x) {
      all_white = all_white && nothing.pixel(x, y)[0] == 255 && nothing.pixel(x, y)[1] == 255 && nothing.pixel(x, y)[2] == 255;
    }
  }
  CHECK(all_white);
}

}  // namespace

std::vector<patchy::test::TestCase> book_scan_tests() {
  return {
      {"book_scan_outline_on_dark_background", book_scan_outline_on_dark_background},
      {"book_scan_outline_on_white_lid_with_skew", book_scan_outline_on_white_lid_with_skew},
      {"book_scan_gutter_found_off_center", book_scan_gutter_found_off_center},
      {"book_scan_gutter_defaults_to_middle_without_fold", book_scan_gutter_defaults_to_middle_without_fold},
      {"book_scan_spread_detects_skewed_spread_and_pages", book_scan_spread_detects_skewed_spread_and_pages},
      {"book_scan_spread_keeps_fallback_on_white_lid", book_scan_spread_keeps_fallback_on_white_lid},
      {"book_scan_detection_is_deterministic", book_scan_detection_is_deterministic},
      {"book_scan_uniform_page_size_median_and_largest", book_scan_uniform_page_size_median_and_largest},
      {"book_scan_extract_anchors_inner_edge", book_scan_extract_anchors_inner_edge},
      {"book_scan_output_threshold", book_scan_output_threshold},
      {"book_scan_grayscale_suggestion", book_scan_grayscale_suggestion},
      {"book_scan_spread_fits_bed", book_scan_spread_fits_bed},
      {"book_scan_unturned_rect_inverts_quarter_turns", book_scan_unturned_rect_inverts_quarter_turns},
      {"book_scan_box_edge_and_bounds", book_scan_box_edge_and_bounds},
      {"book_scan_half_turn_keeps_layouts", book_scan_half_turn_keeps_layouts},
      {"book_scan_page_cut_starts_at_the_divider", book_scan_page_cut_starts_at_the_divider},
      {"book_scan_page_cut_matches_the_box_inside_the_outline", book_scan_page_cut_matches_the_box_inside_the_outline},
  };
}
