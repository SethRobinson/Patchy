#include "core/book_scan.hpp"
#include "core/photo_divide.hpp"
#include "test_harness.hpp"

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

void book_scan_extract_anchors_fore_edge() {
  SpreadLayout layout;
  layout.spread = {300.0, 200.0, 400.0, 200.0, 0.0};
  layout.gutter = 0.45;
  // Uniform page 190 x 180 px: the left page starts at the spread's left edge (x = 100),
  // the right page ends at its right edge (x = 500), both vertically centered.
  const OrientedBox left = patchy::uniform_spread_page_box(layout, BookPageSide::Left, 190.0, 180.0);
  const OrientedBox right = patchy::uniform_spread_page_box(layout, BookPageSide::Right, 190.0, 180.0);
  CHECK(near(left.center_x - left.width / 2.0, 100.0, 1e-9));
  CHECK(near(right.center_x + right.width / 2.0, 500.0, 1e-9));
  CHECK(near(left.center_y, 200.0, 1e-9));
  CHECK(left.width == 190.0 && left.height == 180.0);

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
      {"book_scan_extract_anchors_fore_edge", book_scan_extract_anchors_fore_edge},
      {"book_scan_output_threshold", book_scan_output_threshold},
      {"book_scan_grayscale_suggestion", book_scan_grayscale_suggestion},
      {"book_scan_spread_fits_bed", book_scan_spread_fits_bed},
      {"book_scan_unturned_rect_inverts_quarter_turns", book_scan_unturned_rect_inverts_quarter_turns},
      {"book_scan_box_edge_and_bounds", book_scan_box_edge_and_bounds},
      {"book_scan_half_turn_keeps_layouts", book_scan_half_turn_keeps_layouts},
  };
}
