// Lifting the selected pixels out of a layer and compositing them back (GitHub
// issue 399, Free Transform of a selection; docs/free-transform.md).
#include "core/floating_selection.hpp"
#include "core/pixel_depth.hpp"
#include "test_harness.hpp"
#include "test_groups.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

namespace {

using patchy::BitDepth;
using patchy::PixelBuffer;
using patchy::PixelFormat;
using patchy::Rect;

// An 8-bit RGBA layer whose pixel (x, y) holds (x * 10, y * 10, 77, 255).
PixelBuffer ramp_pixels(std::int32_t width, std::int32_t height) {
  PixelBuffer pixels(width, height, PixelFormat::rgba8());
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = static_cast<std::uint8_t>(x * 10);
      px[1] = static_cast<std::uint8_t>(y * 10);
      px[2] = 77;
      px[3] = 255;
    }
  }
  return pixels;
}

PixelBuffer coverage_plane(std::int32_t width, std::int32_t height, std::uint8_t value) {
  PixelBuffer coverage(width, height, PixelFormat::gray8());
  coverage.clear(value);
  return coverage;
}

bool same_pixel(const std::uint8_t* a, const std::uint8_t* b, std::size_t channels) {
  for (std::size_t c = 0; c < channels; ++c) {
    if (a[c] != b[c]) {
      return false;
    }
  }
  return true;
}

// A hard rectangle lifts byte for byte and leaves a transparent hole; landing the
// floating pixels back in place restores the layer exactly.
void floating_selection_lift_and_land_back_is_exact() {
  const Rect bounds{10, 20, 12, 8};
  const auto pixels = ramp_pixels(bounds.width, bounds.height);
  const Rect selection{13, 22, 4, 3};
  const auto lifted =
      patchy::lift_selected_pixels(pixels, bounds, coverage_plane(4, 3, 255), selection, std::nullopt);
  CHECK(lifted.floating_bounds == selection);
  CHECK(lifted.floating.width() == 4 && lifted.floating.height() == 3);
  CHECK(lifted.remainder.format() == PixelFormat::rgba8());
  for (std::int32_t y = 0; y < bounds.height; ++y) {
    for (std::int32_t x = 0; x < bounds.width; ++x) {
      const bool selected = selection.contains(bounds.x + x, bounds.y + y);
      const auto* hole = lifted.remainder.pixel(x, y);
      CHECK(selected ? hole[3] == 0 : same_pixel(hole, pixels.pixel(x, y), 4));
      if (selected) {
        const auto* lifted_px = lifted.floating.pixel(bounds.x + x - selection.x, bounds.y + y - selection.y);
        CHECK(same_pixel(lifted_px, pixels.pixel(x, y), 4));
      }
    }
  }
  const auto landed = patchy::composite_floating_pixels(lifted.remainder, bounds, lifted.floating,
                                                        lifted.floating_bounds, bounds, false);
  CHECK(landed.format() == pixels.format());
  bool identical = true;
  for (std::int32_t y = 0; y < bounds.height; ++y) {
    for (std::int32_t x = 0; x < bounds.width; ++x) {
      identical = identical && same_pixel(landed.pixel(x, y), pixels.pixel(x, y), 4);
    }
  }
  CHECK(identical);
}

// A soft edge splits alpha between the floating and the remaining pixels; a
// moved floating buffer lands over the hole and grows the output bounds.
void floating_selection_soft_edge_and_moved_landing() {
  const Rect bounds{0, 0, 4, 1};
  const auto pixels = ramp_pixels(4, 1);
  auto coverage = coverage_plane(4, 1, 255);
  *coverage.pixel(3, 0) = 51;  // 20% selected
  const auto lifted = patchy::lift_selected_pixels(pixels, bounds, coverage, bounds, std::nullopt);
  CHECK(lifted.floating.pixel(3, 0)[3] == 51);
  CHECK(lifted.remainder.pixel(3, 0)[3] == 204);
  CHECK(lifted.remainder.pixel(0, 0)[3] == 0);

  // The floating row moved 3 px right: it overlaps the soft pixel and reaches past
  // the layer, so the output covers both.
  const Rect moved{3, 0, 4, 1};
  const Rect output{0, 0, 7, 1};
  const auto landed =
      patchy::composite_floating_pixels(lifted.remainder, bounds, lifted.floating, moved, output, false);
  CHECK(landed.width() == 7);
  CHECK(landed.pixel(0, 0)[3] == 0);  // the hole
  CHECK(same_pixel(landed.pixel(4, 0), pixels.pixel(1, 0), 4));
  CHECK(same_pixel(landed.pixel(3, 0), pixels.pixel(0, 0), 4));  // opaque over the soft remainder
  CHECK(landed.pixel(6, 0)[3] == 51);
}

// A transparency-locked layer without alpha (the Background) fills the hole with the
// background color and keeps its format; landing keeps its alpha and bounds.
void floating_selection_background_fills_hole_and_keeps_alpha() {
  const Rect bounds{0, 0, 3, 2};
  PixelBuffer pixels(3, 2, PixelFormat::rgb8());
  pixels.clear(40);
  pixels.pixel(1, 0)[0] = 90;
  pixels.pixel(1, 1)[0] = 90;
  const std::array<std::uint8_t, 3> fill{250, 240, 230};
  const auto lifted = patchy::lift_selected_pixels(pixels, bounds, coverage_plane(1, 2, 255), Rect{1, 0, 1, 2}, fill);
  CHECK(lifted.remainder.format() == PixelFormat::rgb8());
  CHECK(lifted.remainder.pixel(1, 0)[0] == 250 && lifted.remainder.pixel(1, 1)[2] == 230);
  CHECK(lifted.remainder.pixel(0, 0)[0] == 40);
  CHECK(lifted.floating.pixel(0, 0)[3] == 255 && lifted.floating.pixel(0, 0)[0] == 90);
  const auto landed =
      patchy::composite_floating_pixels(lifted.remainder, bounds, lifted.floating, Rect{2, 0, 1, 2}, bounds, true);
  CHECK(landed.format() == PixelFormat::rgb8());
  CHECK(landed.width() == 3 && landed.height() == 2);
  CHECK(landed.pixel(1, 0)[0] == 250);  // the hole keeps the fill
  CHECK(landed.pixel(2, 1)[0] == 90);   // the moved column landed
  CHECK(landed.pixel(0, 1)[0] == 40);
}

// 16 and 32-bit layers lift at their depth: a hard selection keeps 16-bit samples
// exactly, and the 32-bit remainder keeps its linear values.
void floating_selection_lifts_deep_pixels() {
  for (const auto depth : {BitDepth::UInt16, BitDepth::Float32}) {
    const auto format = patchy::with_bit_depth(PixelFormat::rgba8(), depth);
    PixelBuffer pixels(4, 2, format);
    std::vector<float> row(16U);
    for (std::size_t i = 0; i < 4U; ++i) {
      row[i * 4U] = 10.0F + static_cast<float>(i) * 50.0F;
      row[i * 4U + 1U] = 200.0F;
      row[i * 4U + 2U] = 33.5F;
      row[i * 4U + 3U] = 255.0F;
    }
    const auto domain = patchy::deep_domain_for(depth);
    patchy::store_rgba_row(pixels, 0, 0, 4, domain, row);
    patchy::store_rgba_row(pixels, 1, 0, 4, domain, row);
    const Rect bounds{5, 5, 4, 2};
    const auto lifted =
        patchy::lift_selected_pixels(pixels, bounds, coverage_plane(2, 2, 255), Rect{6, 5, 2, 2}, std::nullopt);
    CHECK(lifted.floating.format() == format);
    CHECK(lifted.remainder.format() == format);
    std::vector<float> floating_row(8U);
    patchy::load_rgba_row(lifted.floating, 1, 0, 2, domain, floating_row);
    std::vector<float> source_row(8U);
    patchy::load_rgba_row(pixels, 1, 1, 2, domain, source_row);
    bool equal = true;
    for (std::size_t i = 0; i < 8U; ++i) {
      equal = equal && std::abs(floating_row[i] - source_row[i]) <= (depth == BitDepth::UInt16 ? 0.0F : 1e-3F);
    }
    CHECK(equal);
    std::vector<float> hole(16U);
    patchy::load_rgba_row(lifted.remainder, 0, 0, 4, domain, hole);
    CHECK(hole[3] == 255.0F && hole[7] == 0.0F && hole[11] == 0.0F && hole[15] == 255.0F);
    const auto landed =
        patchy::composite_floating_pixels(lifted.remainder, bounds, lifted.floating, Rect{6, 5, 2, 2}, bounds, false);
    std::vector<float> back(16U);
    patchy::load_rgba_row(landed, 1, 0, 4, domain, back);
    bool restored = true;
    for (std::size_t i = 0; i < 16U; ++i) {
      restored = restored && std::abs(back[i] - row[i]) <= (depth == BitDepth::UInt16 ? 0.01F : 1e-3F);
    }
    CHECK(restored);
  }
}

// A selection that misses the layer lifts nothing.
void floating_selection_outside_layer_lifts_nothing() {
  const auto pixels = ramp_pixels(4, 4);
  const auto lifted = patchy::lift_selected_pixels(pixels, Rect{0, 0, 4, 4}, coverage_plane(2, 2, 255),
                                                   Rect{10, 10, 2, 2}, std::nullopt);
  CHECK(lifted.floating.empty());
  CHECK(lifted.floating_bounds.empty());
}

}  // namespace

std::vector<patchy::test::TestCase> floating_selection_tests() {
  return {
      {"floating_selection_lift_and_land_back_is_exact", floating_selection_lift_and_land_back_is_exact},
      {"floating_selection_soft_edge_and_moved_landing", floating_selection_soft_edge_and_moved_landing},
      {"floating_selection_background_fills_hole_and_keeps_alpha",
       floating_selection_background_fills_hole_and_keeps_alpha},
      {"floating_selection_lifts_deep_pixels", floating_selection_lifts_deep_pixels},
      {"floating_selection_outside_layer_lifts_nothing", floating_selection_outside_layer_lifts_nothing},
  };
}
