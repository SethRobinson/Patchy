#include <string_view>
#include "core/clone_source_transform.hpp"
#include "core/document.hpp"
#include "core/document_depth.hpp"
#include "core/pixel_depth.hpp"
#include "core/palette.hpp"
#include "core/pixel_tools.hpp"
#include "test_harness.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <exception>
#include <cstdint>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <numeric>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>
#include "core_test_support.hpp"
#include "test_groups.hpp"

namespace {

using patchy::test::active_tool_layer;
using patchy::test::make_bar_brush_tip;
using patchy::test::make_tool_document;
using patchy::test::solid_rgb;
using patchy::test::solid_rgba;
using patchy::test::tool_options;
using patchy::test::write_bmp_artifact;

void tool_one_pixel_brush_segment_snaps_fractional_points_to_one_pixel() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);

  auto options = tool_options(0, 0, 0);
  options.brush_size = 1;
  options.brush_softness = 100;
  const auto dirty = patchy::paint_brush_segment(document, layer_id, 20.1, 20.4, 20.8, 20.4, options, false);

  CHECK(dirty.x == 20);
  CHECK(dirty.y == 20);
  CHECK(dirty.width == 1);
  CHECK(dirty.height == 1);
  const auto* layer = document.find_layer(layer_id);
  CHECK(layer != nullptr);
  CHECK(layer->pixels().pixel(20, 20)[3] == 255);
  CHECK(layer->pixels().pixel(21, 20)[3] == 0);
}

void tool_wide_brush_segment_is_fast_and_writes_artifact() {
  patchy::Document document(1600, 1000, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(1600, 1000, 255, 255, 255));
  patchy::PixelBuffer pixels(1600, 1000, patchy::PixelFormat::rgba8());
  pixels.clear(0);
  const auto layer_id = document.add_pixel_layer("Paint", std::move(pixels)).id();

  auto options = tool_options(20, 80, 230);
  options.brush_size = 240;
  const auto started = std::chrono::steady_clock::now();
  const auto dirty = patchy::paint_brush_segment(document, layer_id, 140, 500, 1460, 500, options, false);
  const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - started)
                              .count();

  CHECK(!dirty.empty());
  CHECK(elapsed_ms < 1000);
  const auto* layer = document.find_layer(layer_id);
  CHECK(layer != nullptr);
  CHECK(layer->bounds().contains(800, 500));
  const auto* center = layer->pixels().pixel(800 - layer->bounds().x, 500 - layer->bounds().y);
  CHECK(center[0] == 20);
  CHECK(center[1] == 80);
  CHECK(center[2] == 230);
  CHECK(center[3] == 255);
  write_bmp_artifact("tool_wide_brush_segment", document);
}

// A 731 px hard round stroke advanced in short segments, like a high-rate mouse (GitHub issue
// 135). Each segment skips the previous segment's end disk (EditOptions::stroke_covered_disk),
// whose pixels the stroke gate would reject: the bytes match the unskipped stroke, and a 2 px
// step tests a sliver of the brush instead of its whole area.
void tool_hard_round_segment_skips_its_covered_disk() {
  constexpr std::int32_t side = 1100;
  constexpr double radius = 365.0;  // brush_size 731
  struct Result {
    patchy::PixelBuffer pixels;
    std::int64_t last_segment_tests{0};
    std::int64_t full_segment_tests{0};
  };
  const auto paint = [](bool skip) {
    patchy::Document document(side, side, patchy::PixelFormat::rgb8());
    patchy::PixelBuffer layer_pixels(side, side, patchy::PixelFormat::rgba8());
    layer_pixels.clear(0);
    const auto layer_id = document.add_pixel_layer("Paint", std::move(layer_pixels)).id();
    auto options = tool_options(30, 60, 200);
    options.primary.a = 160;
    options.brush_size = 731;
    // The canvas's stroke compositor in miniature: an opacity cap per pixel.
    const auto cap = static_cast<float>(options.primary.a) / 255.0F;
    std::vector<float> accumulated(static_cast<std::size_t>(side) * side, 0.0F);
    std::int64_t tests = 0;
    options.stroke_pixel_gate = [&](std::int32_t x, std::int32_t y) {
      ++tests;
      return accumulated[static_cast<std::size_t>(y) * side + x] < cap - 0.0005F;
    };
    options.stroke_pixel_writer = [&](std::int32_t x, std::int32_t y, std::uint8_t* pixel, std::uint16_t,
                                      float coverage, const patchy::EditColor& primary) {
      auto& alpha = accumulated[static_cast<std::size_t>(y) * side + x];
      const auto target = std::min(cap, 1.0F - (1.0F - alpha) * (1.0F - cap * coverage));
      if (target <= alpha + 0.0005F) {
        return false;
      }
      alpha = target;
      pixel[0] = primary.r;
      pixel[1] = primary.g;
      pixel[2] = primary.b;
      pixel[3] = static_cast<std::uint8_t>(std::lround(target * 255.0F));
      return true;
    };
    Result result;
    double x = 380.25;
    double y = 540.5;
    for (int step = 0; step < 90; ++step) {
      const auto next_x = x + 2.0 + 0.25 * (step % 3);
      const auto next_y = y + 1.5 * std::sin(step * 0.2);
      if (skip && step > 0) {
        options.stroke_covered_disk = patchy::EditOptions::CoveredDisk{x, y, radius};
      }
      const auto before = tests;
      (void)patchy::paint_brush_segment(document, layer_id, x, y, next_x, next_y, options, false);
      if (step == 0) {
        result.full_segment_tests = tests - before;
      }
      result.last_segment_tests = tests - before;
      x = next_x;
      y = next_y;
    }
    // One long jump to the right: the skip covers only the old end disk, the rest paints.
    if (skip) {
      options.stroke_covered_disk = patchy::EditOptions::CoveredDisk{x, y, radius};
    }
    CHECK(!patchy::paint_brush_segment(document, layer_id, x, y, x + 160.0, y + 40.0, options, false).empty());
    result.pixels = document.find_layer(layer_id)->pixels();
    return result;
  };
  const auto full = paint(false);
  const auto skipped = paint(true);
  bool identical = full.pixels.width() == skipped.pixels.width() && full.pixels.height() == skipped.pixels.height();
  for (std::int32_t y = 0; identical && y < full.pixels.height(); ++y) {
    const auto left = full.pixels.row(y);
    const auto right = skipped.pixels.row(y);
    identical = std::equal(left.begin(), left.end(), right.begin(), right.end());
  }
  CHECK(identical);
  // Unskipped, every short segment tests about the brush's whole area (420k px); skipped, a
  // crescent and the disk's rim.
  CHECK(full.last_segment_tests > 400000);
  CHECK(skipped.full_segment_tests == full.full_segment_tests);
  CHECK(skipped.last_segment_tests * 20 < full.last_segment_tests);
}

// A round dab visits only the columns its circle reaches on each row, not its bounding square's
// corners (GitHub issue 135).
void tool_round_dab_skips_its_bounding_square_corners() {
  patchy::Document document(128, 128, patchy::PixelFormat::rgb8());
  const auto layer_id = document.add_pixel_layer("Paint", solid_rgba(128, 128, 0, 0, 0, 0)).id();
  auto options = tool_options(10, 200, 10);
  options.brush_size = 61;
  options.brush_softness = 100;
  std::int64_t visits = 0;
  options.selection_coverage = [&](std::int32_t, std::int32_t) {
    ++visits;
    return 1.0F;
  };
  const auto dirty = patchy::paint_brush_dab(document, layer_id, 60.5, 60.25, options, false);
  CHECK(!dirty.empty());
  // 30 px radius: the circle is 2827 px of the 61 x 61 = 3721 px square; each row adds a pixel
  // of padding on each side.
  CHECK(visits > 2827);
  CHECK(visits < 3300);
  const auto& pixels = document.find_layer(layer_id)->pixels();
  const auto bounds = document.find_layer(layer_id)->bounds();
  CHECK(pixels.pixel(60 - bounds.x, 60 - bounds.y)[1] == 200);
}

// A pasted layer is only as big as what was pasted; a stroke that leaves it grows it dab by
// dab. That growth must stay cheap (it used to re-copy the whole layer per dab, freezing a
// long stroke), and the result must match painting on a canvas-sized layer. Under a
// LayerGrowthReserve (the canvas opens one per stroke) the layer reallocates a few times
// and close() trims it to the same bounds and bytes as exact per-dab growth.
void tool_small_layer_grows_cheaply_under_a_long_stroke() {
  constexpr std::int32_t side = 3000;
  const auto paint = [](patchy::Document& document, patchy::LayerId layer_id) {
    auto options = tool_options(200, 40, 40);
    options.brush_size = 30;
    double x = 32.0;
    double y = 32.0;
    while (x < side - 100) {
      CHECK(!patchy::paint_brush_segment(document, layer_id, x, y, x + 4.0, y + 3.0, options, false).empty());
      x += 4.0;
      y += 3.0;
    }
    // Back over painted ground and out past the top edge of the grown area.
    CHECK(!patchy::paint_brush_segment(document, layer_id, x, y, 600.0, 20.0, options, false).empty());
  };
  const auto small_document = [] {
    patchy::Document document(side, side, patchy::PixelFormat::rgb8());
    patchy::PixelBuffer pixels(64, 64, patchy::PixelFormat::rgba8());
    pixels.clear(0);
    pixels.pixel(10, 10)[3] = 255;
    patchy::Layer layer(document.allocate_layer_id(), "Pasted Layer", std::move(pixels));
    layer.set_bounds(patchy::Rect{0, 0, 64, 64});
    document.add_layer(std::move(layer));
    return document;
  };
  const auto timed = [](const std::function<void()>& work) {
    const auto started = std::chrono::steady_clock::now();
    work();
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
  };

  auto exact_document = small_document();
  const auto exact_id = active_tool_layer(exact_document);
  const auto exact_ms = timed([&] { paint(exact_document, exact_id); });

  auto reserved_document = small_document();
  const auto reserved_id = active_tool_layer(reserved_document);
  bool trimmed = false;
  const auto reserved_ms = timed([&] {
    patchy::LayerGrowthReserve reserve(reserved_id, patchy::Rect::from_size(side, side));
    paint(reserved_document, reserved_id);
    trimmed = reserve.close(*reserved_document.find_layer(reserved_id));
  });
  std::printf("  small-layer stroke: exact %lld ms, reserved %lld ms\n", static_cast<long long>(exact_ms),
              static_cast<long long>(reserved_ms));
  CHECK(trimmed);

  patchy::Document full_document(side, side, patchy::PixelFormat::rgb8());
  patchy::PixelBuffer full_pixels(side, side, patchy::PixelFormat::rgba8());
  full_pixels.clear(0);
  full_pixels.pixel(10, 10)[3] = 255;
  const auto full_id = full_document.add_pixel_layer("Paint", std::move(full_pixels)).id();
  paint(full_document, full_id);

  const auto& exact = *std::as_const(exact_document).find_layer(exact_id);
  const auto& reserved = *std::as_const(reserved_document).find_layer(reserved_id);
  const auto& reference = *std::as_const(full_document).find_layer(full_id);
  const auto bounds = exact.bounds();
  CHECK(exact.pixels().width() == bounds.width && exact.pixels().height() == bounds.height);
  CHECK(reserved.bounds().x == bounds.x && reserved.bounds().y == bounds.y &&
        reserved.bounds().width == bounds.width && reserved.bounds().height == bounds.height);
  CHECK(reserved.pixels().format() == exact.pixels().format());
  CHECK(reserved.pixels().data().size() == exact.pixels().data().size() &&
        std::equal(reserved.pixels().data().begin(), reserved.pixels().data().end(), exact.pixels().data().begin()));
  bool same = true;
  for (std::int32_t y = 0; y < side && same; ++y) {
    for (std::int32_t x = 0; x < side; ++x) {
      const auto* expected = reference.pixels().pixel(x, y);
      if (!bounds.contains(x, y)) {
        same = expected[3] == 0;
      } else {
        same = std::memcmp(expected, exact.pixels().pixel(x - bounds.x, y - bounds.y), 4) == 0;
      }
      if (!same) {
        break;
      }
    }
  }
  CHECK(same);
  CHECK(exact_ms < 8000);
  CHECK(reserved_ms < 2000);
}

void tool_brush_segment_accepts_float_endpoints() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(20, 120, 240);
  options.brush_size = 9;
  options.brush_softness = 100;

  const auto dirty = patchy::paint_brush_segment(document, layer_id, 10.25, 20.5, 45.75, 21.25, options, false);

  CHECK(!dirty.empty());
  const auto* layer = document.find_layer(layer_id);
  CHECK(layer != nullptr);
  CHECK(layer->bounds().contains(28, 21));
  const auto* center = layer->pixels().pixel(28 - layer->bounds().x, 21 - layer->bounds().y);
  CHECK(center[0] == 20);
  CHECK(center[1] == 120);
  CHECK(center[2] == 240);
  CHECK(center[3] > 0);
}

void tool_brush_roundness_and_angle_shape_dabs() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(20, 120, 240);
  options.brush_size = 31;
  options.brush_softness = 0;
  options.brush_roundness = 25;
  options.brush_angle_degrees = 0.0;

  const auto dirty = patchy::paint_brush(document, layer_id, 32, 24, options, false);
  CHECK(!dirty.empty());
  const auto* layer = document.find_layer(layer_id);
  CHECK(layer != nullptr);
  const auto& pixels = layer->pixels();
  int horizontal = 0;
  for (std::int32_t x = 0; x < pixels.width(); ++x) {
    if (pixels.pixel(x, 24)[3] > 0U) {
      ++horizontal;
    }
  }
  int vertical = 0;
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    if (pixels.pixel(32, y)[3] > 0U) {
      ++vertical;
    }
  }
  CHECK(horizontal > vertical * 2);

  auto rotated = make_tool_document();
  const auto rotated_layer_id = active_tool_layer(rotated);
  options.brush_angle_degrees = 90.0;
  CHECK(!patchy::paint_brush(rotated, rotated_layer_id, 32, 24, options, false).empty());
  const auto& rotated_pixels = rotated.find_layer(rotated_layer_id)->pixels();
  horizontal = 0;
  for (std::int32_t x = 0; x < rotated_pixels.width(); ++x) {
    if (rotated_pixels.pixel(x, 24)[3] > 0U) {
      ++horizontal;
    }
  }
  vertical = 0;
  for (std::int32_t y = 0; y < rotated_pixels.height(); ++y) {
    if (rotated_pixels.pixel(32, y)[3] > 0U) {
      ++vertical;
    }
  }
  CHECK(vertical > horizontal * 2);
}

void tool_eraser_clears_alpha_and_writes_artifact() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options();
  CHECK(!patchy::paint_brush(document, layer_id, 20, 20, options, false).empty());
  CHECK(!patchy::paint_brush(document, layer_id, 20, 20, options, true).empty());
  const auto* px = document.find_layer(layer_id)->pixels().pixel(20, 20);
  CHECK(px[3] == 0);
  write_bmp_artifact("tool_eraser", document);
}

void tool_eraser_converts_rgb_layer_to_transparency() {
  patchy::Document document(12, 12, patchy::PixelFormat::rgb8());
  const auto layer_id = document.add_pixel_layer("Background", solid_rgb(12, 12, 255, 255, 255)).id();
  auto options = tool_options();
  options.brush_size = 5;
  CHECK(!patchy::paint_brush(document, layer_id, 6, 6, options, true).empty());
  const auto& pixels = document.find_layer(layer_id)->pixels();
  CHECK(pixels.format() == patchy::PixelFormat::rgba8());
  CHECK(pixels.pixel(6, 6)[3] == 0);
  CHECK(pixels.pixel(0, 0)[3] == 255);
}

void tool_smudge_drags_source_pixels_and_writes_artifact() {
  patchy::Document document(80, 40, patchy::PixelFormat::rgb8());
  auto pixels = solid_rgba(80, 40, 255, 255, 255, 255);
  for (std::int32_t y = 8; y < 32; ++y) {
    for (std::int32_t x = 8; x < 24; ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = 220;
      px[1] = 30;
      px[2] = 20;
      px[3] = 255;
    }
  }
  const auto layer_id = document.add_pixel_layer("Smudge", std::move(pixels)).id();
  auto options = tool_options();
  options.brush_size = 13;

  const auto dirty = patchy::smudge_brush_segment(document, layer_id, 20, 20, 48, 20, options);
  CHECK(!dirty.empty());
  const auto* smeared = document.find_layer(layer_id)->pixels().pixel(48, 20);
  const auto* untouched = document.find_layer(layer_id)->pixels().pixel(70, 20);
  CHECK(smeared[0] == 220);
  CHECK(smeared[1] == 30);
  CHECK(smeared[2] == 20);
  CHECK(untouched[0] == 255);
  CHECK(untouched[1] == 255);
  CHECK(untouched[2] == 255);
  write_bmp_artifact("tool_smudge", document);
}

void tool_line_draws_and_writes_artifact() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  const auto dirty = patchy::draw_line(document, layer_id, 5, 5, 55, 40, tool_options(20, 180, 80), false);
  CHECK(!dirty.empty());
  CHECK(document.find_layer(layer_id)->pixels().pixel(30, 22)[3] > 0);
  write_bmp_artifact("tool_line", document);
}

void tool_rectangle_draws_outline_and_writes_artifact() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(255, 120, 0);
  options.brush_size = 3;
  const auto dirty = patchy::draw_rectangle(document, layer_id, patchy::Rect{10, 8, 28, 20}, options, false);
  CHECK(!dirty.empty());
  CHECK(document.find_layer(layer_id)->pixels().pixel(10, 8)[3] > 0);
  write_bmp_artifact("tool_rectangle", document);
}

void tool_ellipse_draws_and_writes_artifact() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(150, 40, 220);
  options.brush_size = 3;
  const auto dirty = patchy::draw_ellipse(document, layer_id, patchy::Rect{12, 10, 30, 22}, options, false);
  CHECK(!dirty.empty());
  write_bmp_artifact("tool_ellipse", document);
}

void tool_filled_ellipse_uses_direct_fill_and_writes_artifact() {
  patchy::Document document(1200, 900, patchy::PixelFormat::rgb8());
  patchy::PixelBuffer pixels(1200, 900, patchy::PixelFormat::rgba8());
  pixels.clear(0);
  const auto layer_id = document.add_pixel_layer("Filled Ellipse", std::move(pixels)).id();

  auto options = tool_options(20, 150, 240);
  options.fill_shapes = true;
  options.brush_size = 96;

  const auto started = std::chrono::steady_clock::now();
  const auto dirty = patchy::draw_ellipse(document, layer_id, patchy::Rect{120, 90, 900, 620}, options, false);
  const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - started)
                              .count();

  CHECK(!dirty.empty());
  CHECK(elapsed_ms < 1000);
  const auto& filled = document.find_layer(layer_id)->pixels();
  CHECK(filled.pixel(570, 400)[0] == 20);
  CHECK(filled.pixel(570, 400)[1] == 150);
  CHECK(filled.pixel(570, 400)[2] == 240);
  CHECK(filled.pixel(570, 400)[3] == 255);
  CHECK(filled.pixel(120, 90)[3] == 0);
  write_bmp_artifact("tool_filled_ellipse", document);
}

void tool_thick_ellipse_outline_avoids_buildup() {
  patchy::Document document(200, 200, patchy::PixelFormat::rgb8());
  patchy::PixelBuffer pixels(200, 200, patchy::PixelFormat::rgba8());
  pixels.clear(0);
  const auto layer_id = document.add_pixel_layer("Outline", std::move(pixels)).id();

  auto options = tool_options(255, 0, 0);
  options.primary.a = 128;  // 50% opacity
  options.brush_size = 24;  // thick ring
  options.fill_shapes = false;
  const auto dirty = patchy::draw_ellipse(document, layer_id, patchy::Rect{40, 40, 120, 120}, options, false);
  CHECK(!dirty.empty());

  // Each pixel is composited exactly once, so the ring alpha stays near the single-stamp value
  // (~128). Under the old 720-segment brush-stamping the heavy overlaps built up toward 255.
  const auto& out = document.find_layer(layer_id)->pixels();
  std::uint8_t max_alpha = 0;
  for (int y = 0; y < 200; ++y) {
    for (int x = 0; x < 200; ++x) {
      max_alpha = std::max(max_alpha, out.pixel(x, y)[3]);
    }
  }
  CHECK(max_alpha > 0);
  CHECK(max_alpha <= 140);
  write_bmp_artifact("tool_thick_ellipse_outline", document);
}

void tool_filled_ellipse_respects_softness() {
  const auto draw = [](int softness) {
    patchy::Document document(200, 200, patchy::PixelFormat::rgb8());
    patchy::PixelBuffer pixels(200, 200, patchy::PixelFormat::rgba8());
    pixels.clear(0);
    const auto layer_id = document.add_pixel_layer("SoftFill", std::move(pixels)).id();
    auto options = tool_options(10, 20, 30);
    options.fill_shapes = true;
    options.brush_size = 60;
    options.brush_softness = softness;
    CHECK(!patchy::draw_ellipse(document, layer_id, patchy::Rect{40, 40, 120, 120}, options, false).empty());
    return document.find_layer(layer_id)->pixels().pixel(157, 100)[3];
  };

  const auto hard_edge = draw(0);
  const auto soft_edge = draw(90);
  // Soft=0 keeps a crisp (essentially binary) edge; Soft>0 feathers it so the same near-edge pixel
  // ends up only partially covered — the old fill ignored softness entirely.
  CHECK(hard_edge == 255);
  CHECK(soft_edge > 0);
  CHECK(soft_edge < hard_edge);
}

void tool_ellipse_outline_thickness_is_uniform() {
  // A thick outline on an elongated ellipse must keep uniform ring thickness — the exact
  // closest-point distance gives matching coverage at the major-axis tip and minor-axis tip for the
  // same outward offset, where the cheap first-order estimate would not.
  auto options = tool_options(0, 0, 0);
  options.brush_size = 12;     // half-thickness 6
  options.brush_softness = 50; // band 3
  options.fill_shapes = false;
  const auto params =
      patchy::make_shape_coverage_params(patchy::Rect{30, 30, 200, 40}, options, patchy::ShapeKind::Ellipse);
  // center (130,50), rx 100, ry 20. Major tip at x=230; minor tip at y=70.
  const auto major = patchy::shape_pixel_coverage(params, 236, 50);
  const auto minor = patchy::shape_pixel_coverage(params, 130, 76);
  CHECK(major > 0.02F);
  CHECK(major < 0.95F);
  CHECK(minor > 0.02F);
  CHECK(minor < 0.95F);
  CHECK(std::abs(major - minor) < 0.15F);
}

void tool_rounded_rectangle_rounds_corners() {
  const auto draw = [](int radius) {
    patchy::Document document(140, 120, patchy::PixelFormat::rgb8());
    patchy::PixelBuffer pixels(140, 120, patchy::PixelFormat::rgba8());
    pixels.clear(0);
    const auto layer_id = document.add_pixel_layer("RoundRect", std::move(pixels)).id();
    auto options = tool_options(200, 80, 40);
    options.fill_shapes = true;
    options.shape_corner_radius = radius;
    CHECK(!patchy::draw_rectangle(document, layer_id, patchy::Rect{20, 20, 80, 60}, options, false).empty());
    return document.find_layer(layer_id)->pixels();
  };

  const auto& sharp = draw(0);
  CHECK(sharp.pixel(21, 21)[3] == 255);  // sharp corner is filled

  const auto& rounded = draw(25);
  CHECK(rounded.pixel(21, 21)[3] == 0);    // corner rounded away
  CHECK(rounded.pixel(60, 21)[3] == 255);  // top-edge midpoint still filled
  CHECK(rounded.pixel(60, 50)[3] == 255);  // interior filled
}

void tool_fill_rect_honors_opacity_and_softness_feather() {
  patchy::Document document(120, 120, patchy::PixelFormat::rgb8());
  patchy::PixelBuffer pixels(120, 120, patchy::PixelFormat::rgba8());
  pixels.clear(0);
  const auto layer_id = document.add_pixel_layer("Fill", std::move(pixels)).id();

  auto options = tool_options(200, 50, 50);
  options.primary.a = 128;                       // 50% opacity
  options.selection = patchy::Rect{20, 20, 80, 80};
  options.fill_softness_feather = 12.0;          // inward edge feather band (px)

  CHECK(!patchy::fill_rect(document, layer_id, *options.selection, options).empty());
  const auto& filled = document.find_layer(layer_id)->pixels();
  // Deep inside: full feather coverage, alpha scaled by opacity (~128, not 255).
  const auto center_alpha = filled.pixel(60, 60)[3];
  CHECK(center_alpha > 110);
  CHECK(center_alpha < 150);
  // Just inside the selection edge: feathered down, so noticeably more transparent than the center.
  const auto edge_alpha = filled.pixel(21, 60)[3];
  CHECK(edge_alpha < center_alpha);
  // Outside the selection stays untouched.
  CHECK(filled.pixel(10, 60)[3] == 0);
}

void tool_fill_bucket_fills_region_and_writes_artifact() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  const auto dirty = patchy::flood_fill(document, layer_id, 10, 10, tool_options(0, 180, 210));
  CHECK(!dirty.empty());
  const auto* px = document.find_layer(layer_id)->pixels().pixel(10, 10);
  CHECK(px[0] == 0);
  CHECK(px[1] == 180);
  CHECK(px[2] == 210);
  CHECK(px[3] == 255);
  write_bmp_artifact("tool_fill_bucket", document);
}

// Fill tool tolerance (GitHub issue 30): a "white" background that is really a checkerboard of
// 255 and 250 grays (scan/JPEG noise), split into two islands by a black bar at rows 30..33.
// At tolerance 0 the 250 and 255 pixels are different colors and, being 4-disconnected, a
// click fills exactly one pixel; at the Wand's tolerance they are one region.
patchy::Document make_noisy_background_document() {
  patchy::Document document(64, 64, patchy::PixelFormat::rgba8());
  patchy::PixelBuffer pixels(64, 64, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < 64; ++y) {
    for (std::int32_t x = 0; x < 64; ++x) {
      auto* px = pixels.pixel(x, y);
      const bool bar = y >= 30 && y <= 33;
      const std::uint8_t gray = ((x + y) % 2 == 0) ? 255 : 250;
      px[0] = bar ? 0 : gray;
      px[1] = bar ? 0 : gray;
      px[2] = bar ? 0 : gray;
      px[3] = 255;
    }
  }
  document.add_pixel_layer("Background", std::move(pixels));
  return document;
}

bool pixel_is(const patchy::Document& document, patchy::LayerId layer_id, std::int32_t x, std::int32_t y,
              std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255) {
  const auto* px = document.find_layer(layer_id)->pixels().pixel(x, y);
  return px[0] == r && px[1] == g && px[2] == b && px[3] == a;
}

void tool_fill_bucket_tolerance_metric_matches_magic_wand() {
  // The Wand accepts sum of squared RGBA differences <= 4 * tolerance^2.
  const std::array<std::uint8_t, 4> base{100, 100, 100, 255};
  const std::array<std::uint8_t, 4> one_channel_20{120, 100, 100, 255};
  const std::array<std::uint8_t, 4> one_channel_21{121, 100, 100, 255};
  const std::array<std::uint8_t, 4> each_channel_10{110, 110, 110, 245};
  const std::array<std::uint8_t, 4> each_channel_11{111, 111, 111, 244};
  CHECK(patchy::color_within_tolerance(base.data(), one_channel_20.data(), 4, 10));
  CHECK(!patchy::color_within_tolerance(base.data(), one_channel_21.data(), 4, 10));
  CHECK(patchy::color_within_tolerance(base.data(), each_channel_10.data(), 4, 10));
  CHECK(!patchy::color_within_tolerance(base.data(), each_channel_11.data(), 4, 10));
  // Tolerance 0 is an exact match; three-channel buffers ignore the fourth byte.
  CHECK(!patchy::color_within_tolerance(base.data(), one_channel_20.data(), 4, 0));
  CHECK(patchy::color_within_tolerance(base.data(), base.data(), 4, 0));
  const std::array<std::uint8_t, 4> alpha_only{100, 100, 100, 0};
  CHECK(patchy::color_within_tolerance(base.data(), alpha_only.data(), 3, 0));
  CHECK(!patchy::color_within_tolerance(base.data(), alpha_only.data(), 4, 0));
  // Single gray channel (mask floods): |delta| <= 2 * tolerance.
  const std::uint8_t g0 = 100;
  const std::uint8_t g20 = 120;
  const std::uint8_t g21 = 121;
  CHECK(patchy::color_within_tolerance(&g0, &g20, 1, 10));
  CHECK(!patchy::color_within_tolerance(&g0, &g21, 1, 10));
}

// GitHub issue 435: the Magic Wand's seed averages the Sample Size square around the
// click. On noise, an averaged seed selects the noisy area's mean color; Point Sample stays
// the clicked pixel exactly (the pre-435 seed). The square clips at the image edge.
void tool_magic_wand_seed_averages_the_sample_square() {
  constexpr int width = 64;
  constexpr int height = 48;
  constexpr std::size_t stride = static_cast<std::size_t>(width) * 4U;
  std::vector<std::uint8_t> rgba(stride * height);
  std::uint64_t state = 0x435'435'435ULL;
  const auto next = [&state] {
    std::uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31U);
  };
  // Gray 128 with +-24 noise per channel: a noisy flat area.
  for (std::size_t index = 0; index < rgba.size(); index += 4U) {
    for (std::size_t channel = 0; channel < 3U; ++channel) {
      rgba[index + channel] = static_cast<std::uint8_t>(104 + static_cast<int>(next() % 49U));
    }
    rgba[index + 3U] = 255U;
  }
  // The clicked pixel is a speck far from the area's mean.
  constexpr int click_x = 30;
  constexpr int click_y = 20;
  auto* speck = rgba.data() + static_cast<std::size_t>(click_y) * stride + static_cast<std::size_t>(click_x) * 4U;
  speck[0] = 160;
  speck[1] = 96;
  speck[2] = 150;

  const auto point = patchy::wand_seed_color(rgba.data(), width, height, stride, click_x, click_y, 1);
  CHECK(point[0] == 160 && point[1] == 96 && point[2] == 150 && point[3] == 255);

  // The 5 by 5 seed is the square's mean, rounded.
  const auto averaged = patchy::wand_seed_color(rgba.data(), width, height, stride, click_x, click_y, 5);
  for (std::size_t channel = 0; channel < 4U; ++channel) {
    int sum = 0;
    for (int y = click_y - 2; y <= click_y + 2; ++y) {
      for (int x = click_x - 2; x <= click_x + 2; ++x) {
        sum += rgba[static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x) * 4U + channel];
      }
    }
    CHECK(averaged[channel] == static_cast<std::uint8_t>((sum + 12) / 25));
  }

  // The selection (the Wand's non-contiguous test at tolerance 12) changes with the seed:
  // the speck matches almost nothing, the mean matches much of the noise.
  const auto selected_count = [&](const std::array<std::uint8_t, 4>& seed) {
    int count = 0;
    for (std::size_t index = 0; index < rgba.size(); index += 4U) {
      count += patchy::color_within_tolerance(seed.data(), rgba.data() + index, 4, 12) ? 1 : 0;
    }
    return count;
  };
  const int point_count = selected_count(point);
  const int averaged_count = selected_count(averaged);
  std::printf("  wand seed: point selects %d px, 5 by 5 selects %d px of %d\n", point_count, averaged_count,
              width * height);
  CHECK(point_count >= 1);
  CHECK(averaged_count > point_count * 10);

  // Clipped at the corner: a 5 by 5 at (0, 0) averages the 3 by 3 inside the image.
  const auto corner = patchy::wand_seed_color(rgba.data(), width, height, stride, 0, 0, 5);
  int corner_sum = 0;
  for (int y = 0; y <= 2; ++y) {
    for (int x = 0; x <= 2; ++x) {
      corner_sum += rgba[static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x) * 4U];
    }
  }
  CHECK(corner[0] == static_cast<std::uint8_t>((corner_sum + 4) / 9));

  // RGB is alpha-weighted: transparent pixels do not pull the color, only the alpha.
  std::array<std::uint8_t, 3U * 4U> row{200, 40, 40, 255, 0, 0, 0, 0, 200, 40, 40, 255};
  const auto weighted = patchy::wand_seed_color(row.data(), 3, 1, row.size(), 1, 0, 3);
  CHECK(weighted[0] == 200 && weighted[1] == 40 && weighted[2] == 40 && weighted[3] == 170);
  std::array<std::uint8_t, 3U * 4U> clear{30, 60, 90, 0, 30, 60, 90, 0, 30, 60, 90, 0};
  const auto transparent = patchy::wand_seed_color(clear.data(), 3, 1, clear.size(), 1, 0, 3);
  CHECK(transparent[0] == 30 && transparent[1] == 60 && transparent[2] == 90 && transparent[3] == 0);
}

void tool_fill_bucket_tolerance_fills_near_colors_but_not_text() {
  auto document = make_noisy_background_document();
  const auto layer_id = document.layers().front().id();
  auto options = tool_options(0, 180, 210);

  // Tolerance 0 (the pre-tolerance behavior): the click's own pixel only.
  options.flood_tolerance = 0;
  const auto exact = patchy::flood_fill(document, layer_id, 2, 2, options);
  CHECK(exact.width == 1);
  CHECK(exact.height == 1);
  CHECK(pixel_is(document, layer_id, 2, 2, 0, 180, 210));
  CHECK(pixel_is(document, layer_id, 3, 2, 250, 250, 250));

  // Wand-strength tolerance: the whole upper island, the bar untouched, the lower island
  // unreachable (contiguous).
  options.flood_tolerance = 32;
  const auto island = patchy::flood_fill(document, layer_id, 4, 4, options);
  CHECK(island.x == 0);
  CHECK(island.y == 0);
  CHECK(island.width == 64);
  CHECK(island.height == 30);
  CHECK(pixel_is(document, layer_id, 3, 2, 0, 180, 210));
  CHECK(pixel_is(document, layer_id, 63, 29, 0, 180, 210));
  CHECK(pixel_is(document, layer_id, 10, 31, 0, 0, 0));
  CHECK(pixel_is(document, layer_id, 10, 40, 255, 255, 255));
  CHECK(pixel_is(document, layer_id, 11, 40, 250, 250, 250));
}

void tool_fill_bucket_non_contiguous_fills_every_matching_pixel() {
  auto document = make_noisy_background_document();
  const auto layer_id = document.layers().front().id();
  auto options = tool_options(0, 180, 210);
  options.flood_tolerance = 32;
  options.flood_contiguous = false;
  const auto dirty = patchy::flood_fill(document, layer_id, 4, 4, options);
  CHECK(dirty.width == 64);
  CHECK(dirty.height == 64);
  CHECK(pixel_is(document, layer_id, 10, 40, 0, 180, 210));
  CHECK(pixel_is(document, layer_id, 11, 40, 0, 180, 210));
  CHECK(pixel_is(document, layer_id, 10, 31, 0, 0, 0));

  // A selection still clips the non-contiguous fill.
  auto clipped = make_noisy_background_document();
  const auto clipped_id = clipped.layers().front().id();
  options.selection = patchy::Rect{0, 40, 64, 24};
  options.selection_mask = [](std::int32_t, std::int32_t y) { return y >= 40; };
  CHECK(!patchy::flood_fill(clipped, clipped_id, 4, 44, options).empty());
  CHECK(pixel_is(clipped, clipped_id, 4, 44, 0, 180, 210));
  CHECK(pixel_is(clipped, clipped_id, 4, 36, 255, 255, 255));
  CHECK(pixel_is(clipped, clipped_id, 4, 4, 255, 255, 255));
}

void tool_fill_bucket_opacity_blends_and_terminates() {
  // A translucent fill at maximum tolerance: every pixel matches the click, including the
  // ones already written, so membership must come from the original pixels or the flood
  // never ends. The result is a 50% blend, not a 50% alpha stamp.
  auto document = make_noisy_background_document();
  const auto layer_id = document.layers().front().id();
  auto options = tool_options(0, 180, 210);
  options.primary.a = 128;
  options.flood_tolerance = 255;
  const auto dirty = patchy::flood_fill(document, layer_id, 10, 31, options);
  CHECK(dirty.width == 64);
  CHECK(dirty.height == 64);
  const auto* bar = document.find_layer(layer_id)->pixels().pixel(10, 31);
  CHECK(bar[0] == 0);
  CHECK(bar[1] >= 88 && bar[1] <= 92);
  CHECK(bar[2] >= 103 && bar[2] <= 107);
  CHECK(bar[3] == 255);
  const auto* white = document.find_layer(layer_id)->pixels().pixel(2, 2);
  CHECK(white[0] >= 126 && white[0] <= 129);
  CHECK(white[3] == 255);
}

void tool_fill_bucket_softness_feathers_inward_from_region_edge() {
  auto document = make_noisy_background_document();
  const auto layer_id = document.layers().front().id();
  auto options = tool_options(0, 180, 210);
  options.flood_tolerance = 32;
  options.fill_softness_feather = 8.0;
  CHECK(!patchy::flood_fill(document, layer_id, 4, 4, options).empty());
  // Deep inside the upper island: the full fill color.
  CHECK(pixel_is(document, layer_id, 32, 15, 0, 180, 210));
  // One pixel in from the island's top edge and from the black bar: mostly the original white.
  const auto* top = document.find_layer(layer_id)->pixels().pixel(32, 1);
  CHECK(top[0] > 200);
  const auto* above_bar = document.find_layer(layer_id)->pixels().pixel(32, 28);
  CHECK(above_bar[0] > 200);
  // The bar itself is outside the region and untouched.
  CHECK(pixel_is(document, layer_id, 32, 31, 0, 0, 0));
}

void tool_fill_bucket_transparency_lock_skips_transparent_pixels() {
  patchy::Document document(32, 16, patchy::PixelFormat::rgba8());
  patchy::PixelBuffer pixels(32, 16, patchy::PixelFormat::rgba8());
  pixels.clear(0);
  for (std::int32_t y = 0; y < 16; ++y) {
    for (std::int32_t x = 16; x < 32; ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = px[1] = px[2] = 200;
      px[3] = 255;
    }
  }
  document.add_pixel_layer("Layer", std::move(pixels));
  const auto layer_id = document.layers().front().id();
  auto options = tool_options(0, 180, 210);
  options.flood_tolerance = 255;
  options.lock_transparent_pixels = true;
  CHECK(patchy::flood_fill(document, layer_id, 4, 4, options).empty());
  const auto dirty = patchy::flood_fill(document, layer_id, 20, 4, options);
  CHECK(dirty.x == 16);
  CHECK(dirty.width == 16);
  CHECK(pixel_is(document, layer_id, 20, 4, 0, 180, 210));
  CHECK(pixel_is(document, layer_id, 4, 4, 0, 0, 0, 0));
}

// GitHub issue 498: moving content off a transparent layer leaves alpha-0 pixels that still
// hold the old RGB. Fill and the Magic Wand treat every alpha-0 pixel as one color, so a
// click on clean transparency covers those ghosts too, at 8, 16 and 32 bits; opaque pixels
// keep the ordinary metric and stay out at tolerance 0 and 32.
void tool_fill_bucket_transparent_pixels_match_whatever_rgb_they_hold() {
  const std::array<std::uint8_t, 4> clear{0, 0, 0, 0};
  const std::array<std::uint8_t, 4> ghost{200, 30, 30, 0};
  const std::array<std::uint8_t, 4> faint{0, 0, 0, 10};
  const std::array<std::uint8_t, 4> faint_red{220, 10, 10, 10};
  CHECK(patchy::color_within_tolerance(clear.data(), ghost.data(), 4, 0));
  CHECK(patchy::color_within_tolerance(ghost.data(), clear.data(), 4, 0));
  CHECK(!patchy::color_within_tolerance(ghost.data(), faint.data(), 4, 0));
  // Against an alpha-0 side only alpha counts: 10 <= 2 * 5, 10 > 2 * 4.
  CHECK(patchy::color_within_tolerance(ghost.data(), faint_red.data(), 4, 5));
  CHECK(!patchy::color_within_tolerance(ghost.data(), faint_red.data(), 4, 4));
  // Two visible pixels still compare RGB.
  CHECK(!patchy::color_within_tolerance(faint.data(), faint_red.data(), 4, 5));

  for (const auto depth : {patchy::BitDepth::UInt8, patchy::BitDepth::UInt16, patchy::BitDepth::Float32}) {
    patchy::Document document(32, 16, patchy::PixelFormat::rgba8());
    patchy::PixelBuffer pixels(32, 16, patchy::PixelFormat::rgba8());
    pixels.clear(0);
    document.add_pixel_layer("Layer", std::move(pixels));
    if (depth != patchy::BitDepth::UInt8) {
      patchy::convert_document_depth(document, depth);
    }
    const auto layer_id = document.layers().front().id();
    auto& layer_pixels = document.find_layer(layer_id)->pixels();
    const auto format = layer_pixels.format();
    CHECK(format.bit_depth == depth);
    // Ghost letters: alpha 0 with leftover color in columns 8..23, and an opaque block at
    // columns 26..29, rows 4..11, that stays outside the fill (the fill goes around it).
    for (std::int32_t y = 0; y < 16; ++y) {
      for (std::int32_t x = 8; x < 24; ++x) {
        patchy::store_pixel(format, layer_pixels.pixel(x, y), {200.0F, 30.0F, 30.0F, 0.0F});
      }
      for (std::int32_t x = 26; x < 30 && y >= 4 && y < 12; ++x) {
        patchy::store_pixel(format, layer_pixels.pixel(x, y), {40.0F, 40.0F, 40.0F, 255.0F});
      }
    }
    for (const int tolerance : {0, 32}) {
      auto copy = document;
      auto options = tool_options(0, 180, 210);
      options.flood_tolerance = tolerance;
      const auto dirty = patchy::flood_fill(copy, layer_id, 2, 2, options);
      CHECK(dirty.x == 0);
      CHECK(dirty.width == 32);
      const auto& filled = std::as_const(copy).find_layer(layer_id)->pixels();
      const auto at = [&](std::int32_t x, std::int32_t y) { return patchy::load_pixel(format, filled.pixel(x, y)); };
      for (const std::int32_t x : {2, 8, 15, 23, 25, 31}) {
        const auto value = at(x, 7);
        CHECK(value[3] >= 254.0F);
        if (depth != patchy::BitDepth::Float32) {  // 32-bit stores linear values
          CHECK(std::abs(value[1] - 180.0F) < 1.0F);
        }
      }
      const auto opaque = at(27, 7);
      CHECK(std::abs(opaque[0] - 40.0F) < 1.0F && std::abs(opaque[1] - 40.0F) < 1.0F);
    }
    // Clicking a ghost pixel fills the clean transparency around it too.
    auto from_ghost = document;
    const auto ghost_dirty = patchy::flood_fill(from_ghost, layer_id, 12, 3, tool_options(0, 180, 210));
    CHECK(ghost_dirty.x == 0);
    CHECK(patchy::load_pixel(format, std::as_const(from_ghost).find_layer(layer_id)->pixels().pixel(2, 2))[3] >= 254.0F);
  }
}

// GitHub issue 498, second cause: a rasterized text layer's buffer covers only the text, and
// the rest of the canvas is that layer's transparency. The bucket floods the whole canvas
// from a click inside or outside the buffer, reading outside as transparent, and grows the
// layer to hold the fill (clipped to the selection's rect); the transparency lock keeps it
// inside the buffer. 8, 16 and 32 bits; Contiguous on and off.
void tool_fill_bucket_grows_a_layer_smaller_than_the_canvas() {
  constexpr patchy::Rect kBuffer{20, 10, 16, 12};
  const auto make = [&](patchy::BitDepth depth) {
    patchy::Document document(64, 48, patchy::PixelFormat::rgba8());
    patchy::PixelBuffer canvas_pixels(64, 48, patchy::PixelFormat::rgba8());
    canvas_pixels.clear(0);
    const auto id = document.add_pixel_layer("Type", std::move(canvas_pixels)).id();
    patchy::PixelBuffer pixels(kBuffer.width, kBuffer.height, patchy::PixelFormat::rgba8());
    pixels.clear(0);
    document.find_layer(id)->set_pixels(std::move(pixels));
    document.find_layer(id)->set_bounds(kBuffer);
    if (depth != patchy::BitDepth::UInt8) {
      patchy::convert_document_depth(document, depth);
    }
    // An opaque "letter" at document (24..27, 14..17), inside the buffer.
    auto& layer_pixels = document.find_layer(id)->pixels();
    const auto format = layer_pixels.format();
    for (std::int32_t y = 14; y < 18; ++y) {
      for (std::int32_t x = 24; x < 28; ++x) {
        patchy::store_pixel(format, layer_pixels.pixel(x - kBuffer.x, y - kBuffer.y), {40.0F, 40.0F, 40.0F, 255.0F});
      }
    }
    return std::pair{std::move(document), id};
  };
  const auto alpha_at = [](const patchy::Document& document, patchy::LayerId id, std::int32_t x, std::int32_t y) {
    const auto& layer = *document.find_layer(id);
    const auto bounds = layer.bounds();
    if (x < bounds.x || y < bounds.y || x >= bounds.x + bounds.width || y >= bounds.y + bounds.height) {
      return 0.0F;
    }
    return patchy::load_pixel(layer.pixels().format(), layer.pixels().pixel(x - bounds.x, y - bounds.y))[3];
  };
  const auto letter_kept = [](const patchy::Document& document, patchy::LayerId id) {
    const auto& layer = *document.find_layer(id);
    const auto bounds = layer.bounds();
    const auto value = patchy::load_pixel(layer.pixels().format(), layer.pixels().pixel(25 - bounds.x, 15 - bounds.y));
    return std::abs(value[1] - 40.0F) < 1.0F;
  };
  for (const auto depth : {patchy::BitDepth::UInt8, patchy::BitDepth::UInt16, patchy::BitDepth::Float32}) {
    for (const bool contiguous : {true, false}) {
      for (const auto [click_x, click_y] : {std::pair{2, 2}, std::pair{21, 11}}) {
        auto [document, id] = make(depth);
        auto options = tool_options(0, 180, 210);
        options.flood_tolerance = 0;
        options.flood_contiguous = contiguous;
        const auto dirty = patchy::flood_fill(document, id, click_x, click_y, options);
        CHECK(dirty.x == 0 && dirty.y == 0 && dirty.width == 64 && dirty.height == 48);
        const auto& layer = *std::as_const(document).find_layer(id);
        CHECK(layer.bounds().x == 0 && layer.bounds().y == 0 && layer.bounds().width == 64 &&
              layer.bounds().height == 48);
        CHECK(layer.pixels().format().bit_depth == depth);
        for (const auto [x, y] : {std::pair{0, 0}, std::pair{63, 47}, std::pair{21, 11}, std::pair{35, 21},
                                  std::pair{50, 5}}) {
          CHECK(alpha_at(document, id, x, y) >= 254.0F);
        }
        CHECK(letter_kept(document, id));
      }
    }
    // A selection clips the flood and the growth to its rect.
    {
      auto [document, id] = make(depth);
      auto options = tool_options(0, 180, 210);
      options.selection = patchy::Rect{0, 0, 30, 30};
      CHECK(!patchy::flood_fill(document, id, 2, 2, options).empty());
      const auto bounds = std::as_const(document).find_layer(id)->bounds();
      CHECK(bounds.x == 0 && bounds.y == 0 && bounds.width == 36 && bounds.height == 30);
      CHECK(alpha_at(document, id, 29, 29) >= 254.0F);
      CHECK(alpha_at(document, id, 31, 12) <= 0.0F);
      CHECK(letter_kept(document, id));
    }
    // The transparency lock: outside the buffer is not fillable and nothing grows.
    {
      auto [document, id] = make(depth);
      auto options = tool_options(0, 180, 210);
      options.lock_transparent_pixels = true;
      CHECK(patchy::flood_fill(document, id, 2, 2, options).empty());
      CHECK(patchy::flood_fill(document, id, 21, 11, options).empty());
      const auto bounds = std::as_const(document).find_layer(id)->bounds();
      CHECK(bounds.x == kBuffer.x && bounds.width == kBuffer.width);
    }
  }
}

// GitHub issue 498: Gradient and Fill (fill_rect) on a layer smaller than the canvas cover
// the canvas, growing the layer; under the transparency lock they stay inside the buffer.
void tool_gradient_and_fill_rect_grow_a_layer_smaller_than_the_canvas() {
  constexpr patchy::Rect kBuffer{20, 10, 16, 12};
  for (const auto depth : {patchy::BitDepth::UInt8, patchy::BitDepth::UInt16}) {
    for (const bool locked : {false, true}) {
      for (const bool gradient : {true, false}) {
        patchy::Document document(64, 48, patchy::PixelFormat::rgba8());
        patchy::PixelBuffer canvas_pixels(64, 48, patchy::PixelFormat::rgba8());
        canvas_pixels.clear(0);
        const auto id = document.add_pixel_layer("Type", std::move(canvas_pixels)).id();
        patchy::PixelBuffer pixels(kBuffer.width, kBuffer.height, patchy::PixelFormat::rgba8());
        pixels.clear(255);
        document.find_layer(id)->set_pixels(std::move(pixels));
        document.find_layer(id)->set_bounds(kBuffer);
        if (depth != patchy::BitDepth::UInt8) {
          patchy::convert_document_depth(document, depth);
        }
        auto options = tool_options(255, 0, 0);
        options.secondary = patchy::EditColor{0, 0, 255, 255};
        options.lock_transparent_pixels = locked;
        const auto dirty = gradient ? patchy::draw_linear_gradient(document, id, 0, 0, 63, 0, options)
                                    : patchy::fill_rect(document, id, patchy::Rect{0, 0, 64, 48}, options);
        const auto bounds = std::as_const(document).find_layer(id)->bounds();
        if (locked) {
          CHECK(bounds.x == kBuffer.x && bounds.y == kBuffer.y && bounds.width == kBuffer.width);
          CHECK(dirty.x == kBuffer.x && dirty.width == kBuffer.width);
        } else {
          CHECK(bounds.x == 0 && bounds.y == 0 && bounds.width == 64 && bounds.height == 48);
          CHECK(dirty.x == 0 && dirty.width == 64 && dirty.height == 48);
          const auto& layer_pixels = std::as_const(document).find_layer(id)->pixels();
          CHECK(patchy::load_pixel(layer_pixels.format(), layer_pixels.pixel(1, 40))[3] >= 254.0F);
        }
      }
    }
  }
}

void tool_gradient_draws_foreground_to_background_and_writes_artifact() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(255, 0, 0);
  options.secondary = patchy::EditColor{0, 0, 255, 255};
  const auto dirty = patchy::draw_linear_gradient(document, layer_id, 0, 0, 63, 0, options);
  CHECK(!dirty.empty());
  const auto* left = document.find_layer(layer_id)->pixels().pixel(0, 20);
  const auto* right = document.find_layer(layer_id)->pixels().pixel(63, 20);
  CHECK(left[0] == 255);
  CHECK(left[1] == 0);
  CHECK(left[2] == 0);
  CHECK(right[0] == 0);
  CHECK(right[1] == 0);
  CHECK(right[2] == 255);
  CHECK(right[3] == 255);
  write_bmp_artifact("tool_gradient", document);
}

void tool_gradient_supports_custom_stops_radial_reverse_and_alpha() {
  {
    auto document = make_tool_document();
    const auto layer_id = active_tool_layer(document);
    auto options = tool_options();
    patchy::GradientOptions gradient;
    gradient.stops = {
        patchy::GradientStop{0.0F, patchy::EditColor{255, 0, 0, 255}},
        patchy::GradientStop{0.5F, patchy::EditColor{0, 255, 0, 255}},
        patchy::GradientStop{1.0F, patchy::EditColor{0, 0, 255, 128}},
    };
    CHECK(!patchy::draw_gradient(document, layer_id, 0, 0, 63, 0, options, gradient).empty());
    const auto* middle = document.find_layer(layer_id)->pixels().pixel(32, 20);
    const auto* right = document.find_layer(layer_id)->pixels().pixel(63, 20);
    CHECK(middle[1] > 245);
    CHECK(middle[0] < 20);
    CHECK(middle[2] < 20);
    CHECK(right[2] == 255);
    CHECK(right[3] >= 127);
    CHECK(right[3] <= 129);
  }

  {
    auto document = make_tool_document();
    const auto layer_id = active_tool_layer(document);
    auto options = tool_options();
    patchy::GradientOptions gradient;
    gradient.reverse = true;
    gradient.stops = {
        patchy::GradientStop{0.0F, patchy::EditColor{255, 0, 0, 0}},
        patchy::GradientStop{1.0F, patchy::EditColor{0, 0, 255, 255}},
    };
    CHECK(!patchy::draw_gradient(document, layer_id, 0, 0, 63, 0, options, gradient).empty());
    const auto* left = document.find_layer(layer_id)->pixels().pixel(0, 20);
    const auto* right = document.find_layer(layer_id)->pixels().pixel(63, 20);
    CHECK(left[2] == 255);
    CHECK(left[3] == 255);
    CHECK(right[3] == 0);
  }

  {
    auto document = make_tool_document();
    const auto layer_id = active_tool_layer(document);
    auto options = tool_options();
    patchy::GradientOptions gradient;
    gradient.method = patchy::GradientMethod::Radial;
    gradient.opacity = 0.5F;
    gradient.stops = {
        patchy::GradientStop{0.0F, patchy::EditColor{255, 0, 0, 255}},
        patchy::GradientStop{1.0F, patchy::EditColor{0, 0, 255, 255}},
    };
    CHECK(!patchy::draw_gradient(document, layer_id, 32, 24, 42, 24, options, gradient).empty());
    const auto* center = document.find_layer(layer_id)->pixels().pixel(32, 24);
    const auto* edge = document.find_layer(layer_id)->pixels().pixel(42, 24);
    CHECK(center[0] == 255);
    CHECK(center[2] == 0);
    CHECK(center[3] >= 127);
    CHECK(center[3] <= 129);
    CHECK(edge[0] == 0);
    CHECK(edge[2] == 255);
    CHECK(edge[3] >= 127);
    CHECK(edge[3] <= 129);
  }
}

void tool_fill_selection_draws_only_selection_and_writes_artifact() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(40, 200, 80);
  options.selection = patchy::Rect{8, 8, 16, 12};
  const auto dirty = patchy::fill_rect(document, layer_id, patchy::Rect{0, 0, 64, 48}, options);
  CHECK(dirty.x == 8);
  CHECK(dirty.y == 8);
  CHECK(document.find_layer(layer_id)->pixels().pixel(8, 8)[3] == 255);
  CHECK(document.find_layer(layer_id)->pixels().pixel(2, 2)[3] == 0);
  write_bmp_artifact("tool_fill_selection", document);
}

void tool_clear_selection_erases_only_selection_and_writes_artifact() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(40, 200, 80);
  CHECK(!patchy::fill_rect(document, layer_id, patchy::Rect{0, 0, 64, 48}, options).empty());
  options.selection = patchy::Rect{8, 8, 16, 12};
  const auto dirty = patchy::clear_rect(document, layer_id, patchy::Rect{0, 0, 64, 48}, options);
  CHECK(!dirty.empty());
  CHECK(document.find_layer(layer_id)->pixels().pixel(8, 8)[3] == 0);
  CHECK(document.find_layer(layer_id)->pixels().pixel(2, 2)[3] == 255);
  write_bmp_artifact("tool_clear_selection", document);
}

void tool_clear_transparent_selection_is_noop() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(40, 200, 80);
  options.selection = patchy::Rect{8, 8, 16, 12};

  CHECK(patchy::clear_rect_change_bounds(document, layer_id, patchy::Rect{0, 0, 64, 48}, options).empty());
  CHECK(patchy::clear_rect(document, layer_id, patchy::Rect{0, 0, 64, 48}, options).empty());
  CHECK(document.find_layer(layer_id)->pixels().format() == patchy::PixelFormat::rgba8());
  CHECK(document.find_layer(layer_id)->pixels().pixel(8, 8)[3] == 0);
}

void tool_clear_selected_opaque_pixels_reports_exact_bounds() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(40, 200, 80);
  CHECK(!patchy::fill_rect(document, layer_id, patchy::Rect{8, 8, 16, 12}, options).empty());

  options.selection = patchy::Rect{10, 9, 3, 2};
  const auto planned = patchy::clear_rect_change_bounds(document, layer_id, patchy::Rect{0, 0, 64, 48}, options);
  CHECK(planned.x == 10);
  CHECK(planned.y == 9);
  CHECK(planned.width == 3);
  CHECK(planned.height == 2);

  const auto dirty = patchy::clear_rect(document, layer_id, patchy::Rect{0, 0, 64, 48}, options);
  CHECK(dirty.x == 10);
  CHECK(dirty.y == 9);
  CHECK(dirty.width == 3);
  CHECK(dirty.height == 2);
  CHECK(document.find_layer(layer_id)->pixels().pixel(10, 9)[3] == 0);
  CHECK(document.find_layer(layer_id)->pixels().pixel(9, 9)[3] == 255);
}

void tool_fill_clear_gradient_respect_complex_selection_mask() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(250, 20, 40);
  options.selection = patchy::Rect{4, 4, 24, 24};
  options.selection_mask = [](std::int32_t x, std::int32_t y) { return (x >= 4 && x < 12) || (y >= 20 && y < 28); };

  CHECK(!patchy::fill_rect(document, layer_id, patchy::Rect{0, 0, 64, 48}, options).empty());
  CHECK(document.find_layer(layer_id)->pixels().pixel(6, 6)[3] == 255);
  CHECK(document.find_layer(layer_id)->pixels().pixel(18, 10)[3] == 0);
  CHECK(document.find_layer(layer_id)->pixels().pixel(18, 22)[3] == 255);

  options.selection_mask = [](std::int32_t, std::int32_t y) { return y >= 20 && y < 28; };
  CHECK(!patchy::clear_rect(document, layer_id, patchy::Rect{0, 0, 64, 48}, options).empty());
  CHECK(document.find_layer(layer_id)->pixels().pixel(18, 22)[3] == 0);
  CHECK(document.find_layer(layer_id)->pixels().pixel(6, 6)[3] == 255);

  options.primary = patchy::EditColor{0, 0, 255, 255};
  options.secondary = patchy::EditColor{0, 255, 0, 255};
  options.selection = patchy::Rect{0, 0, 64, 48};
  options.selection_mask = [](std::int32_t x, std::int32_t y) { return x >= 40 && y < 20; };
  CHECK(!patchy::draw_linear_gradient(document, layer_id, 40, 0, 63, 0, options).empty());
  CHECK(document.find_layer(layer_id)->pixels().pixel(42, 8)[3] == 255);
  CHECK(document.find_layer(layer_id)->pixels().pixel(42, 24)[3] == 0);
  CHECK(document.find_layer(layer_id)->pixels().pixel(18, 10)[3] == 0);
  write_bmp_artifact("tool_complex_selection_mask_ops", document);
}

void tool_lock_transparent_pixels_preserves_alpha() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(200, 30, 40);
  options.lock_transparent_pixels = true;
  CHECK(!patchy::fill_rect(document, layer_id, patchy::Rect{0, 0, 64, 48}, options).empty());
  CHECK(document.find_layer(layer_id)->pixels().pixel(10, 10)[3] == 0);

  options.lock_transparent_pixels = false;
  CHECK(!patchy::paint_brush(document, layer_id, 20, 20, options, false).empty());
  auto* painted = document.find_layer(layer_id)->pixels().pixel(20, 20);
  CHECK(painted[0] == 200);
  CHECK(painted[3] == 255);

  options.primary = patchy::EditColor{20, 90, 220, 255};
  options.lock_transparent_pixels = true;
  CHECK(!patchy::paint_brush(document, layer_id, 20, 20, options, false).empty());
  CHECK(document.find_layer(layer_id)->pixels().pixel(20, 20)[0] == 20);
  CHECK(document.find_layer(layer_id)->pixels().pixel(20, 20)[3] == 255);
  CHECK(!patchy::paint_brush(document, layer_id, 4, 4, options, false).empty());
  CHECK(document.find_layer(layer_id)->pixels().pixel(4, 4)[3] == 0);

  // Clear under the lock paints the background color (white here) and keeps alpha.
  CHECK(!patchy::clear_rect_change_bounds(document, layer_id, patchy::Rect{18, 18, 6, 6}, options).empty());
  CHECK(!patchy::clear_rect(document, layer_id, patchy::Rect{18, 18, 6, 6}, options).empty());
  CHECK(pixel_is(document, layer_id, 20, 20, 255, 255, 255, 255));
  write_bmp_artifact("tool_lock_transparency", document);
}

// GitHub issue 97 / Photoshop: with transparency locked, the Eraser and Clear paint the
// background color and keep each pixel's alpha. Empty pixels stay empty, and a layer without
// alpha (the opaque Background) never gains an alpha channel.
void tool_eraser_and_clear_paint_background_color_under_transparency_lock() {
  const patchy::EditColor background{200, 100, 50, 255};
  {
    patchy::Document document(16, 16, patchy::PixelFormat::rgba8());
    auto pixels = solid_rgba(16, 16, 10, 20, 30, 255);
    pixels.pixel(6, 8)[3] = 128;
    pixels.pixel(10, 8)[3] = 0;
    const auto layer_id = document.add_pixel_layer("Locked", std::move(pixels)).id();
    auto options = tool_options(0, 0, 0);
    options.secondary = background;
    options.brush_size = 15;
    options.lock_transparent_pixels = true;
    CHECK(!patchy::paint_brush(document, layer_id, 8, 8, options, true).empty());
    CHECK(pixel_is(document, layer_id, 8, 8, 200, 100, 50, 255));
    CHECK(pixel_is(document, layer_id, 6, 8, 200, 100, 50, 128));
    CHECK(pixel_is(document, layer_id, 10, 8, 10, 20, 30, 0));
  }
  {
    patchy::Document document(12, 12, patchy::PixelFormat::rgb8());
    const auto layer_id = document.add_pixel_layer("Background", solid_rgb(12, 12, 255, 255, 255)).id();
    auto options = tool_options(0, 0, 0);
    options.secondary = background;
    options.brush_size = 5;
    options.lock_transparent_pixels = true;
    CHECK(!patchy::paint_brush(document, layer_id, 6, 6, options, true).empty());
    const auto& pixels = document.find_layer(layer_id)->pixels();
    CHECK(pixels.format() == patchy::PixelFormat::rgb8());
    const auto* erased = pixels.pixel(6, 6);
    CHECK(erased[0] == 200 && erased[1] == 100 && erased[2] == 50);
    CHECK(pixels.pixel(0, 0)[0] == 255);

    options.selection = patchy::Rect{0, 0, 2, 2};
    CHECK(!patchy::clear_rect_change_bounds(document, layer_id, patchy::Rect{0, 0, 12, 12}, options).empty());
    CHECK(!patchy::clear_rect(document, layer_id, patchy::Rect{0, 0, 12, 12}, options).empty());
    const auto& cleared = document.find_layer(layer_id)->pixels();
    CHECK(cleared.format() == patchy::PixelFormat::rgb8());
    CHECK(cleared.pixel(1, 1)[0] == 200 && cleared.pixel(1, 1)[1] == 100 && cleared.pixel(1, 1)[2] == 50);
    CHECK(cleared.pixel(3, 3)[0] == 255);
  }
  {
    // 16-bit: the same rule at the layer's depth, for the Eraser and for Clear.
    patchy::Document document(16, 16, patchy::PixelFormat::rgba8());
    auto pixels = solid_rgba(16, 16, 10, 20, 30, 255);
    pixels.pixel(10, 8)[3] = 0;
    const auto layer_id =
        document
            .add_pixel_layer("Deep", patchy::convert_pixel_buffer_depth(pixels, patchy::BitDepth::UInt16,
                                                                         patchy::SampleKind::Color))
            .id();
    auto options = tool_options(0, 0, 0);
    options.secondary = background;
    options.brush_size = 9;
    options.lock_transparent_pixels = true;
    CHECK(!patchy::paint_brush(document, layer_id, 8, 8, options, true).empty());
    const auto deep_close = [](float value, float expected) { return std::abs(value - expected) <= 0.6F; };
    const auto& deep = document.find_layer(layer_id)->pixels();
    const auto erased = patchy::load_pixel(deep.format(), deep.pixel(8, 8));
    CHECK(deep_close(erased[0], 200.0F) && deep_close(erased[1], 100.0F) && deep_close(erased[2], 50.0F) &&
          deep_close(erased[3], 255.0F));
    CHECK(deep_close(patchy::load_pixel(deep.format(), deep.pixel(10, 8))[3], 0.0F));

    options.selection = patchy::Rect{0, 0, 2, 2};
    options.secondary = patchy::EditColor{0, 255, 0, 255};
    CHECK(!patchy::clear_rect(document, layer_id, patchy::Rect{0, 0, 16, 16}, options).empty());
    const auto& cleared = document.find_layer(layer_id)->pixels();
    const auto cleared_px = patchy::load_pixel(cleared.format(), cleared.pixel(1, 1));
    CHECK(deep_close(cleared_px[0], 0.0F) && deep_close(cleared_px[1], 255.0F) && deep_close(cleared_px[3], 255.0F));
    const auto untouched = patchy::load_pixel(cleared.format(), cleared.pixel(4, 4));
    CHECK(deep_close(untouched[0], 10.0F) && deep_close(untouched[3], 255.0F));
  }
}

void tool_clear_rgb_selection_converts_only_when_pixels_change() {
  patchy::Document document(6, 5, patchy::PixelFormat::rgb8());
  const auto layer_id = document.add_pixel_layer("RGB", solid_rgb(6, 5, 12, 34, 56)).id();
  auto options = tool_options(40, 200, 80);
  options.selection = patchy::Rect{1, 1, 2, 2};

  const auto planned = patchy::clear_rect_change_bounds(document, layer_id, patchy::Rect{0, 0, 6, 5}, options);
  CHECK(planned.x == 1);
  CHECK(planned.y == 1);
  CHECK(planned.width == 2);
  CHECK(planned.height == 2);

  const auto dirty = patchy::clear_rect(document, layer_id, patchy::Rect{0, 0, 6, 5}, options);
  CHECK(dirty.x == 1);
  CHECK(dirty.y == 1);
  CHECK(dirty.width == 2);
  CHECK(dirty.height == 2);
  const auto* layer = document.find_layer(layer_id);
  CHECK(layer->pixels().format() == patchy::PixelFormat::rgba8());
  CHECK(layer->pixels().pixel(1, 1)[3] == 0);
  CHECK(layer->pixels().pixel(0, 0)[3] == 255);
}

// Baseline for the palette-mode work: pins the exact bytes every core tool write
// path produces today, so the palette snap hook (EditOptions::palette_snap, null =
// legacy path) can prove that mode-off behavior stays bit-identical. If a deliberate
// rendering change lands, the failure output prints every current digest; re-pin the
// table from that output in the same change.
std::uint64_t layer_pixels_digest(const patchy::Document& document, patchy::LayerId layer_id) {
  const auto* layer = document.find_layer(layer_id);
  CHECK(layer != nullptr);
  const auto& pixels = layer->pixels();
  const auto channels = pixels.format().channels;
  std::uint64_t hash = 1469598103934665603ULL;
  const auto mix = [&hash](std::uint8_t byte) {
    hash ^= byte;
    hash *= 1099511628211ULL;
  };
  mix(static_cast<std::uint8_t>(pixels.width() & 0xFF));
  mix(static_cast<std::uint8_t>(pixels.height() & 0xFF));
  mix(static_cast<std::uint8_t>(channels & 0xFF));
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto* px = pixels.pixel(x, y);
      for (std::uint16_t channel = 0; channel < channels; ++channel) {
        mix(px[channel]);
      }
    }
  }
  return hash;
}

void tool_write_paths_digest_baseline() {
  std::vector<std::pair<std::string, std::uint64_t>> digests;

  {  // Procedural hard round dab.
    auto document = make_tool_document();
    const auto layer_id = active_tool_layer(document);
    auto options = tool_options(220, 20, 40);
    options.brush_size = 12;
    CHECK(!patchy::paint_brush(document, layer_id, 20, 20, options, false).empty());
    digests.emplace_back("procedural_dab_hard", layer_pixels_digest(document, layer_id));
  }
  {  // Procedural soft-edged segment.
    auto document = make_tool_document();
    const auto layer_id = active_tool_layer(document);
    auto options = tool_options(30, 160, 220);
    options.brush_size = 15;
    options.brush_softness = 60;
    CHECK(!patchy::paint_brush_segment(document, layer_id, 10.0, 20.0, 44.0, 30.0, options, false).empty());
    digests.emplace_back("procedural_segment_soft", layer_pixels_digest(document, layer_id));
  }
  {  // Bitmap tip segment through the stateful spacing overload.
    auto document = make_tool_document();
    const auto layer_id = active_tool_layer(document);
    const auto tip = make_bar_brush_tip();
    const auto mips = patchy::build_brush_tip_mips(tip);
    const auto scaled = patchy::make_scaled_brush_tip(mips, 9);
    auto options = tool_options(10, 200, 60);
    options.brush_size = 9;
    options.brush_tip = &scaled;
    patchy::BrushTipStrokeState state;
    CHECK(!patchy::paint_brush_segment(document, layer_id, 10.0, 20.0, 40.0, 28.0, options, false, state).empty());
    digests.emplace_back("bitmap_tip_segment", layer_pixels_digest(document, layer_id));
  }
  {  // Soft erase over solid RGBA content.
    auto document = make_tool_document();
    const auto layer_id = active_tool_layer(document);
    CHECK(!patchy::fill_rect(document, layer_id, patchy::Rect{0, 0, 64, 48}, tool_options(90, 140, 30)).empty());
    auto erase_options = tool_options();
    erase_options.brush_size = 14;
    erase_options.brush_softness = 40;
    CHECK(!patchy::paint_brush_segment(document, layer_id, 12.0, 12.0, 40.0, 30.0, erase_options, true).empty());
    digests.emplace_back("erase_soft_rgba", layer_pixels_digest(document, layer_id));
  }
  {  // Erase on a 3-channel layer blends toward the secondary color.
    auto document = make_tool_document();
    const auto background_id = document.layers().front().id();
    auto erase_options = tool_options();
    erase_options.brush_size = 12;
    erase_options.secondary = patchy::EditColor{40, 60, 200, 255};
    CHECK(!patchy::paint_brush_segment(document, background_id, 8.0, 8.0, 30.0, 24.0, erase_options, true).empty());
    digests.emplace_back("erase_rgb_background", layer_pixels_digest(document, background_id));
  }
  {  // Filled rounded rectangle through the signed-distance shape renderer.
    auto document = make_tool_document();
    const auto layer_id = active_tool_layer(document);
    auto options = tool_options(255, 120, 0);
    options.fill_shapes = true;
    options.shape_corner_radius = 6;
    CHECK(!patchy::draw_rectangle(document, layer_id, patchy::Rect{8, 6, 40, 30}, options, false).empty());
    digests.emplace_back("shape_fill_rounded_rect", layer_pixels_digest(document, layer_id));
  }
  {  // Ellipse outline ring.
    auto document = make_tool_document();
    const auto layer_id = active_tool_layer(document);
    auto options = tool_options(150, 40, 220);
    options.brush_size = 3;
    CHECK(!patchy::draw_ellipse(document, layer_id, patchy::Rect{12, 10, 30, 22}, options, false).empty());
    digests.emplace_back("shape_outline_ellipse", layer_pixels_digest(document, layer_id));
  }
  {  // fill_rect with an inward feather band and a selection rect.
    auto document = make_tool_document();
    const auto layer_id = active_tool_layer(document);
    auto options = tool_options(20, 90, 200);
    options.selection = patchy::Rect{10, 8, 30, 24};
    options.fill_softness_feather = 4.0;
    CHECK(!patchy::fill_rect(document, layer_id, patchy::Rect{10, 8, 30, 24}, options).empty());
    digests.emplace_back("fill_rect_feathered_selection", layer_pixels_digest(document, layer_id));
  }
  {  // Flood fill on the white 3-channel background.
    auto document = make_tool_document();
    const auto background_id = document.layers().front().id();
    CHECK(!patchy::flood_fill(document, background_id, 5, 5, tool_options(0, 180, 210)).empty());
    digests.emplace_back("flood_fill_background", layer_pixels_digest(document, background_id));
  }
  {  // Linear gradient with alpha-varying custom stops.
    auto document = make_tool_document();
    const auto layer_id = active_tool_layer(document);
    patchy::GradientOptions gradient;
    gradient.method = patchy::GradientMethod::Linear;
    gradient.opacity = 0.85F;
    gradient.stops = {{0.0F, patchy::EditColor{255, 0, 0, 255}},
                      {0.45F, patchy::EditColor{0, 200, 60, 128}},
                      {1.0F, patchy::EditColor{20, 40, 255, 0}}};
    CHECK(!patchy::draw_gradient(document, layer_id, 0, 0, 63, 47, tool_options(), gradient).empty());
    digests.emplace_back("gradient_linear_alpha_stops", layer_pixels_digest(document, layer_id));
  }
  {  // Radial gradient.
    auto document = make_tool_document();
    const auto layer_id = active_tool_layer(document);
    patchy::GradientOptions gradient;
    gradient.method = patchy::GradientMethod::Radial;
    gradient.opacity = 1.0F;
    gradient.stops = {{0.0F, patchy::EditColor{250, 240, 40, 255}},
                      {1.0F, patchy::EditColor{40, 20, 120, 255}}};
    CHECK(!patchy::draw_gradient(document, layer_id, 32, 24, 60, 40, tool_options(), gradient).empty());
    digests.emplace_back("gradient_radial", layer_pixels_digest(document, layer_id));
  }
  {  // clear_rect limited by a selection.
    auto document = make_tool_document();
    const auto layer_id = active_tool_layer(document);
    CHECK(!patchy::fill_rect(document, layer_id, patchy::Rect{0, 0, 64, 48}, tool_options(200, 80, 40)).empty());
    auto options = tool_options();
    options.selection = patchy::Rect{6, 6, 20, 16};
    CHECK(!patchy::clear_rect(document, layer_id, patchy::Rect{0, 0, 64, 48}, options).empty());
    digests.emplace_back("clear_rect_selection", layer_pixels_digest(document, layer_id));
  }
  {  // Hard line.
    auto document = make_tool_document();
    const auto layer_id = active_tool_layer(document);
    auto options = tool_options(20, 180, 80);
    options.brush_size = 3;
    CHECK(!patchy::draw_line(document, layer_id, 5, 5, 55, 40, options, false).empty());
    digests.emplace_back("line_hard", layer_pixels_digest(document, layer_id));
  }
  {  // Smudge drag from solid color into empty space.
    auto document = make_tool_document();
    const auto layer_id = active_tool_layer(document);
    CHECK(!patchy::fill_rect(document, layer_id, patchy::Rect{0, 0, 32, 48}, tool_options(220, 30, 20)).empty());
    auto options = tool_options();
    options.brush_size = 13;
    CHECK(!patchy::smudge_brush_segment(document, layer_id, 20, 20, 48, 20, options).empty());
    digests.emplace_back("smudge_segment", layer_pixels_digest(document, layer_id));
  }
  {  // Mixer dab colors through the bitmap-tip provider path.
    auto document = make_tool_document();
    const auto layer_id = active_tool_layer(document);
    CHECK(!patchy::fill_rect(document, layer_id, patchy::Rect{0, 0, 24, 48}, tool_options(20, 40, 230)).empty());
    const auto tip = make_bar_brush_tip();
    const auto mips = patchy::build_brush_tip_mips(tip);
    const auto scaled = patchy::make_scaled_brush_tip(mips, 9);
    auto options = tool_options(220, 30, 20);
    options.brush_size = 9;
    options.brush_tip = &scaled;
    options.brush_tip_spacing = 0.5;
    patchy::MixerBrushState mixer;
    patchy::begin_mixer_brush_stroke(mixer);
    options.dab_primary_provider = [&mixer](double x, double y, const patchy::EditColor& loaded) {
      return patchy::mixer_brush_dab_color(mixer, x, y, 9, loaded,
                                           patchy::EditColor{20, 40, 230, 255}, 60, 40, 70);
    };
    patchy::BrushTipStrokeState state;
    CHECK(!patchy::paint_brush_segment(document, layer_id, 12.0, 24.0, 52.0, 24.0, options, false, state).empty());
    digests.emplace_back("mixer_dab_segment", layer_pixels_digest(document, layer_id));
  }

  static constexpr std::array<std::pair<const char*, std::uint64_t>, 15> kExpected = {{
      {"procedural_dab_hard", 0x1f41304572a13fd8ULL},
      {"procedural_segment_soft", 0x7312aa1cfea0b16aULL},
      {"bitmap_tip_segment", 0xfb394573f9c5c112ULL},
      {"erase_soft_rgba", 0x2a54e34a973125f8ULL},
      {"erase_rgb_background", 0x41bb48610e4dd790ULL},
      {"shape_fill_rounded_rect", 0xb2f0360277d9a89dULL},
      {"shape_outline_ellipse", 0xa79e54e64046011dULL},
      {"fill_rect_feathered_selection", 0x94472d040684f83dULL},
      {"flood_fill_background", 0x77e535fda417f2b8ULL},
      {"gradient_linear_alpha_stops", 0x2eb6832d2d931867ULL},
      {"gradient_radial", 0x41832d6d40cf21eaULL},
      {"clear_rect_selection", 0x440d479ec3f19a9dULL},
      {"line_hard", 0x0e5f22f86e99266dULL},
      {"smudge_segment", 0xcc409264f89c224aULL},
      {"mixer_dab_segment", 0x041a3aec246efad4ULL},
  }};

  CHECK(digests.size() == kExpected.size());
  bool all_match = true;
  for (std::size_t i = 0; i < digests.size(); ++i) {
    if (digests[i].first != kExpected[i].first || digests[i].second != kExpected[i].second) {
      all_match = false;
    }
  }
  if (!all_match) {
    std::cout << "tool_write_paths_digest_baseline current digests (pin these on deliberate changes):\n";
    for (const auto& [name, value] : digests) {
      std::cout << "      {\"" << name << "\", 0x" << std::hex << std::setw(16) << std::setfill('0') << value
                << std::dec << "ULL},\n";
    }
  }
  CHECK(all_match);
}

}  // namespace

namespace {
void tool_paint_pixel_block_native_blend_selection_and_palette() {
  auto document = make_tool_document();
  const auto id = active_tool_layer(document);
  patchy::PixelBuffer input(2, 1, patchy::PixelFormat::rgba8());
  for (int x = 0; x < 2; ++x) { input.pixel(x, 0)[0] = 240; input.pixel(x, 0)[3] = 128; }
  auto* layer = document.find_layer(id);
  auto options = tool_options(0, 0, 0);
  options.selection = patchy::Rect{10, 10, 1, 1};
  CHECK(!patchy::paint_pixel_block(*layer, input, {10, 10, 2, 1}, options).empty());
  CHECK(std::as_const(*layer).pixels().pixel(10, 10)[0] == 240);
  CHECK(std::as_const(*layer).pixels().pixel(10, 10)[3] == 128);
  CHECK(std::as_const(*layer).pixels().pixel(11, 10)[3] == 0);
  options.selection.reset();
  patchy::PaletteLut lut;
  const std::array<patchy::RgbColor, 2> colors{{{0, 0, 0}, {255, 0, 0}}};
  lut.build(colors);
  patchy::PaletteSnapContext snap; snap.lut = &lut; snap.coverage_threshold = 0.5F;
  options.palette_snap = &snap;
  input.pixel(0, 0)[3] = 127;
  CHECK(!patchy::paint_pixel_block(*layer, input, {20, 10, 2, 1}, options).empty());
  CHECK(std::as_const(*layer).pixels().pixel(20, 10)[3] == 0);
  CHECK(std::as_const(*layer).pixels().pixel(21, 10)[0] == 255);
  CHECK(std::as_const(*layer).pixels().pixel(21, 10)[3] == 255);
}

// Image Size resampling (core/resample.hpp, docs/resampling.md).

patchy::PixelBuffer gray8_row(std::initializer_list<int> values) {
  patchy::PixelBuffer row(static_cast<std::int32_t>(values.size()), 1, patchy::PixelFormat::gray8());
  std::int32_t x = 0;
  for (const auto value : values) {
    *row.pixel(x++, 0) = static_cast<std::uint8_t>(value);
  }
  return row;
}

void resample_nearest_replicates_pixels_at_every_depth() {
  using patchy::BitDepth;
  for (const auto format : {patchy::PixelFormat::rgba8(), patchy::PixelFormat::rgb16(), patchy::PixelFormat::rgbf32()}) {
    patchy::PixelBuffer source(2, 2, format);
    const auto pixel_bytes = patchy::bytes_per_pixel(format);
    for (std::int32_t y = 0; y < 2; ++y) {
      for (std::int32_t x = 0; x < 2; ++x) {
        auto* px = source.pixel(x, y);
        for (std::size_t byte = 0; byte < pixel_bytes; ++byte) {
          px[byte] = static_cast<std::uint8_t>(17 * (x + 2 * y) + 3 * byte + 1);
        }
      }
    }
    const auto scaled = patchy::resample_pixels(source, 4, 4, patchy::ResampleMethod::NearestNeighbor);
    CHECK(scaled.width() == 4 && scaled.height() == 4 && scaled.format() == format);
    for (std::int32_t y = 0; y < 4; ++y) {
      for (std::int32_t x = 0; x < 4; ++x) {
        CHECK(std::equal(scaled.pixel(x, y), scaled.pixel(x, y) + pixel_bytes, source.pixel(x / 2, y / 2)));
      }
    }
  }

  // A reduction picks the pixel under each output center: 16 -> 4 reads columns 2, 6, 10, 14.
  patchy::PixelBuffer ramp(16, 1, patchy::PixelFormat::gray8());
  for (std::int32_t x = 0; x < 16; ++x) {
    *ramp.pixel(x, 0) = static_cast<std::uint8_t>(x * 16);
  }
  const auto reduced = patchy::resample_pixels(ramp, 4, 1, patchy::ResampleMethod::NearestNeighbor);
  const int expected[4] = {32, 96, 160, 224};
  for (std::int32_t x = 0; x < 4; ++x) {
    CHECK(*reduced.pixel(x, 0) == expected[x]);
  }
}

void resample_bilinear_enlargement_matches_clamped_edge_ramp() {
  // Source x = (x + 0.5) / 2 - 0.5 with clamped edges gives 0, 25, 75, 100 exactly (the
  // export resize pins the same numbers through write_flat_image_file).
  const auto horizontal = patchy::resample_pixels(gray8_row({0, 100}), 4, 1, patchy::ResampleMethod::Bilinear);
  const int expected[4] = {0, 25, 75, 100};
  for (std::int32_t x = 0; x < 4; ++x) {
    CHECK(*horizontal.pixel(x, 0) == expected[x]);
  }
  patchy::PixelBuffer column(1, 2, patchy::PixelFormat::gray8());
  *column.pixel(0, 0) = 0;
  *column.pixel(0, 1) = 100;
  const auto vertical = patchy::resample_pixels(column, 1, 4, patchy::ResampleMethod::Bilinear);
  for (std::int32_t y = 0; y < 4; ++y) {
    CHECK(*vertical.pixel(0, y) == expected[y]);
  }
}

void resample_reduction_widens_the_kernel() {
  // Eight alternating columns halved: a point-sampled filter would keep pure 0/255
  // (aliasing); the widened kernels average toward 128. Nearest keeps source values.
  const auto source = gray8_row({0, 255, 0, 255, 0, 255, 0, 255});
  for (const auto method : {patchy::ResampleMethod::Bilinear, patchy::ResampleMethod::Bicubic,
                            patchy::ResampleMethod::BicubicSmoother, patchy::ResampleMethod::BicubicSharper}) {
    const auto reduced = patchy::resample_pixels(source, 4, 1, method);
    CHECK(reduced.width() == 4);
    for (std::int32_t x = 0; x < 4; ++x) {
      CHECK(std::abs(static_cast<int>(*reduced.pixel(x, 0)) - 128) <= 40);
    }
  }
  const auto nearest = patchy::resample_pixels(source, 4, 1, patchy::ResampleMethod::NearestNeighbor);
  for (std::int32_t x = 0; x < 4; ++x) {
    const int value = *nearest.pixel(x, 0);
    CHECK(value == 0 || value == 255);
  }
}

void resample_cubic_variants_are_distinct_and_keep_flat_color() {
  // Normalized weights leave a flat buffer untouched under every method.
  patchy::PixelBuffer flat(5, 5, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < 5; ++y) {
    for (std::int32_t x = 0; x < 5; ++x) {
      auto* px = flat.pixel(x, y);
      px[0] = 37;
      px[1] = 120;
      px[2] = 200;
      px[3] = 255;
    }
  }
  for (const auto method : patchy::all_resample_methods()) {
    const auto scaled = patchy::resample_pixels(flat, 9, 7, method);
    for (std::int32_t y = 0; y < 7; ++y) {
      for (std::int32_t x = 0; x < 9; ++x) {
        const auto* px = scaled.pixel(x, y);
        CHECK(px[0] == 37 && px[1] == 120 && px[2] == 200 && px[3] == 255);
      }
    }
  }

  // A 64 -> 192 step doubled: the cubics overshoot in increasing order Smoother < Bicubic
  // < Sharper (Mitchell, Catmull-Rom, Keys -0.75), while Bilinear and Nearest stay inside
  // the source range.
  const auto step = gray8_row({64, 64, 64, 64, 192, 192, 192, 192});
  const auto max_of = [&](patchy::ResampleMethod method) {
    const auto scaled = patchy::resample_pixels(step, 16, 1, method);
    int maximum = 0;
    for (std::int32_t x = 0; x < 16; ++x) {
      maximum = std::max(maximum, static_cast<int>(*scaled.pixel(x, 0)));
    }
    return maximum;
  };
  const auto min_of = [&](patchy::ResampleMethod method) {
    const auto scaled = patchy::resample_pixels(step, 16, 1, method);
    int minimum = 255;
    for (std::int32_t x = 0; x < 16; ++x) {
      minimum = std::min(minimum, static_cast<int>(*scaled.pixel(x, 0)));
    }
    return minimum;
  };
  const int smoother = max_of(patchy::ResampleMethod::BicubicSmoother);
  const int bicubic = max_of(patchy::ResampleMethod::Bicubic);
  const int sharper = max_of(patchy::ResampleMethod::BicubicSharper);
  CHECK(sharper > 192);
  CHECK(smoother < bicubic);
  CHECK(bicubic < sharper);
  CHECK(min_of(patchy::ResampleMethod::BicubicSharper) < 64);
  for (const auto method : {patchy::ResampleMethod::Bilinear, patchy::ResampleMethod::NearestNeighbor}) {
    CHECK(max_of(method) == 192);
    CHECK(min_of(method) == 64);
  }
}

void resample_interpolates_premultiplied_alpha() {
  // Opaque red beside transparent black: the fringe keeps the red hue with reduced
  // coverage instead of darkening toward the transparent pixel's color.
  patchy::PixelBuffer source(2, 1, patchy::PixelFormat::rgba8());
  source.clear(0);
  auto* red = source.pixel(0, 0);
  red[0] = 255;
  red[3] = 255;
  const auto scaled = patchy::resample_pixels(source, 4, 1, patchy::ResampleMethod::Bilinear);
  const int expected_alpha[4] = {255, 191, 64, 0};
  for (std::int32_t x = 0; x < 4; ++x) {
    const auto* px = scaled.pixel(x, 0);
    CHECK(px[3] == expected_alpha[x]);
    CHECK(px[1] == 0 && px[2] == 0);
    if (px[3] > 0) {
      CHECK(px[0] >= 250);
    } else {
      CHECK(px[0] == 0);
    }
  }

  // Gray + alpha (two channels) follows the same rule.
  patchy::PixelBuffer gray_alpha(2, 1, patchy::PixelFormat{patchy::ColorMode::Grayscale, patchy::BitDepth::UInt8, 2});
  gray_alpha.clear(0);
  gray_alpha.pixel(0, 0)[0] = 200;
  gray_alpha.pixel(0, 0)[1] = 255;
  const auto scaled_gray = patchy::resample_pixels(gray_alpha, 4, 1, patchy::ResampleMethod::Bilinear);
  CHECK(scaled_gray.pixel(1, 0)[1] == 191);
  CHECK(scaled_gray.pixel(1, 0)[0] >= 196);
}

void resample_sixteen_bit_and_float_interpolate() {
  patchy::PixelBuffer deep(2, 1, patchy::PixelFormat::rgb16());
  const std::uint16_t low = 0;
  const std::uint16_t high = 60000;
  for (std::uint16_t channel = 0; channel < 3; ++channel) {
    std::memcpy(deep.pixel(0, 0) + channel * 2, &low, 2);
    std::memcpy(deep.pixel(1, 0) + channel * 2, &high, 2);
  }
  const auto deep_scaled = patchy::resample_pixels(deep, 4, 1, patchy::ResampleMethod::Bilinear);
  const std::uint16_t expected_deep[4] = {0, 15000, 45000, 60000};
  for (std::int32_t x = 0; x < 4; ++x) {
    std::uint16_t value = 0;
    std::memcpy(&value, deep_scaled.pixel(x, 0), 2);
    CHECK(value == expected_deep[x]);
  }

  patchy::PixelBuffer floating(2, 1, patchy::PixelFormat::rgbf32());
  const float zero = 0.0F;
  const float one = 1.0F;
  for (std::uint16_t channel = 0; channel < 3; ++channel) {
    std::memcpy(floating.pixel(0, 0) + channel * 4, &zero, 4);
    std::memcpy(floating.pixel(1, 0) + channel * 4, &one, 4);
  }
  const auto float_scaled = patchy::resample_pixels(floating, 4, 1, patchy::ResampleMethod::Bilinear);
  const float expected_float[4] = {0.0F, 0.25F, 0.75F, 1.0F};
  for (std::int32_t x = 0; x < 4; ++x) {
    float value = 0.0F;
    std::memcpy(&value, float_scaled.pixel(x, 0), 4);
    CHECK(std::abs(value - expected_float[x]) < 1e-6F);
  }
}

void resample_automatic_resolves_by_direction() {
  using patchy::ResampleMethod;
  CHECK(patchy::resolve_automatic_resample_method(ResampleMethod::Automatic, 100, 100, 50, 50) ==
        ResampleMethod::BicubicSharper);
  CHECK(patchy::resolve_automatic_resample_method(ResampleMethod::Automatic, 100, 100, 200, 200) ==
        ResampleMethod::BicubicSmoother);
  CHECK(patchy::resolve_automatic_resample_method(ResampleMethod::Automatic, 100, 100, 200, 50) ==
        ResampleMethod::Bicubic);
  CHECK(patchy::resolve_automatic_resample_method(ResampleMethod::Bilinear, 100, 100, 50, 50) ==
        ResampleMethod::Bilinear);
}

void resample_method_ids_round_trip() {
  const auto methods = patchy::all_resample_methods();
  CHECK(methods.size() == 6);
  CHECK(methods.front() == patchy::ResampleMethod::Automatic);
  for (const auto method : methods) {
    const auto parsed = patchy::parse_resample_method(patchy::resample_method_id(method));
    CHECK(parsed.has_value() && *parsed == method);
  }
  CHECK(patchy::resample_method_id(patchy::ResampleMethod::NearestNeighbor) == "nearest");
  CHECK(patchy::resample_method_id(patchy::ResampleMethod::BicubicSharper) == "bicubicSharper");
  CHECK(!patchy::parse_resample_method("nearestNeighbor").has_value());
  CHECK(!patchy::parse_resample_method("").has_value());
}

// Clone source transform (GitHub issue 424): at 100% and 0 degrees the mapping is the
// plain offset, whole pixels with no bilinear fraction.
void clone_source_identity_mapping_is_the_plain_offset() {
  for (const auto& transform : {patchy::CloneSourceTransform{}, patchy::CloneSourceTransform{100.0, 360.0},
                                patchy::CloneSourceTransform{100.0, -0.0}}) {
    CHECK(transform.is_identity());
    const patchy::CloneSourceMapping mapping(40, 12, -25, 7, transform);
    CHECK(mapping.identity());
    for (std::int32_t y = -3; y < 30; y += 4) {
      for (std::int32_t x = -5; x < 80; x += 7) {
        const auto q8 = mapping.map_q8(x, y);
        CHECK(q8[0] == static_cast<std::int64_t>(x - 25) * 256);
        CHECK(q8[1] == static_cast<std::int64_t>(y + 7) * 256);
        const auto nearest = mapping.map_nearest(x, y);
        CHECK(nearest[0] == x - 25 && nearest[1] == y + 7);
      }
    }
  }
  CHECK((!patchy::CloneSourceTransform{50.0, 0.0}.is_identity()));
  CHECK((!patchy::CloneSourceTransform{100.0, 15.0}.is_identity()));
  // Out-of-range values clamp and fold.
  const auto folded = patchy::CloneSourceTransform{1000.0, 270.0}.normalized();
  CHECK(folded.scale_percent == patchy::CloneSourceTransform::kMaxScalePercent);
  CHECK(folded.rotation_degrees == -90.0);
}

// Scale and rotation turn about the source point: the destination anchor (source minus
// offset) samples the source point itself, and a step away from it is divided by the
// scale and turned back by the rotation. Quarter turns map whole pixels exactly.
void clone_source_mapping_scales_and_rotates_about_the_source_point() {
  // 200%: two destination pixels per source pixel.
  const patchy::CloneSourceMapping doubled(10, 10, 20, 0, patchy::CloneSourceTransform{200.0, 0.0});
  CHECK(!doubled.identity());
  CHECK((doubled.map_q8(-10, 10) == std::array<std::int64_t, 2>{10 * 256, 10 * 256}));
  CHECK((doubled.map_q8(-8, 10) == std::array<std::int64_t, 2>{11 * 256, 10 * 256}));
  CHECK((doubled.map_q8(-9, 11) == std::array<std::int64_t, 2>{10 * 256 + 128, 10 * 256 + 128}));
  // 50%: every other source pixel.
  const patchy::CloneSourceMapping halved(10, 10, 0, 0, patchy::CloneSourceTransform{50.0, 0.0});
  CHECK((halved.map_q8(13, 9) == std::array<std::int64_t, 2>{16 * 256, 8 * 256}));
  // 90 degrees clockwise: a destination step right samples a source step up (the content
  // turns clockwise, so what was above the source point lands to its right).
  const patchy::CloneSourceMapping turned(10, 10, 0, 0, patchy::CloneSourceTransform{100.0, 90.0});
  CHECK((turned.map_q8(11, 10) == std::array<std::int64_t, 2>{10 * 256, 9 * 256}));
  CHECK((turned.map_q8(10, 11) == std::array<std::int64_t, 2>{11 * 256, 10 * 256}));
  CHECK((turned.map_nearest(13, 12) == std::array<std::int32_t, 2>{12, 7}));
  const patchy::CloneSourceMapping half_turn(10, 10, 5, 5, patchy::CloneSourceTransform{100.0, 180.0});
  CHECK((half_turn.map_q8(6, 2) == std::array<std::int64_t, 2>{9 * 256, 13 * 256}));
  // A free angle: the anchor still samples the source point, and the step lands on the
  // circle of the same radius (Q8 rounding within one step).
  const patchy::CloneSourceMapping angled(50, 50, 0, 0, patchy::CloneSourceTransform{100.0, 30.0});
  CHECK((angled.map_q8(50, 50) == std::array<std::int64_t, 2>{50 * 256, 50 * 256}));
  const auto step = angled.map_q8(60, 50);
  const auto ux = static_cast<double>(step[0]) / 256.0 - 50.0;
  const auto uy = static_cast<double>(step[1]) / 256.0 - 50.0;
  CHECK(std::abs(ux - 10.0 * std::cos(30.0 * 3.14159265358979323846 / 180.0)) < 0.01);
  CHECK(std::abs(uy + 10.0 * std::sin(30.0 * 3.14159265358979323846 / 180.0)) < 0.01);
}

// Flip Horizontal and Flip Vertical (GitHub issue 424 follow-up) mirror the source about
// the source point before the scale and rotation, so a step right of the anchor samples a
// step left of the source point. Both flips together are a half turn.
void clone_source_mapping_flips_about_the_source_point() {
  const auto flipped = [](bool horizontal, bool vertical, double scale = 100.0, double rotation = 0.0) {
    patchy::CloneSourceTransform transform{scale, rotation};
    transform.flip_horizontal = horizontal;
    transform.flip_vertical = vertical;
    return transform;
  };
  CHECK(!flipped(true, false).is_identity());
  CHECK(!flipped(false, true).is_identity());
  const auto kept = flipped(true, true, 1000.0, 270.0).normalized();
  CHECK(kept.flip_horizontal && kept.flip_vertical);
  CHECK(flipped(true, false) != flipped(false, true));

  const patchy::CloneSourceMapping horizontal(10, 10, 0, 0, flipped(true, false));
  CHECK(!horizontal.identity());
  CHECK((horizontal.map_q8(10, 10) == std::array<std::int64_t, 2>{10 * 256, 10 * 256}));
  CHECK((horizontal.map_q8(13, 12) == std::array<std::int64_t, 2>{7 * 256, 12 * 256}));
  const patchy::CloneSourceMapping vertical(10, 10, 0, 0, flipped(false, true));
  CHECK((vertical.map_q8(13, 12) == std::array<std::int64_t, 2>{13 * 256, 8 * 256}));
  // With an offset the mirror axis passes through the anchor (source point minus offset).
  const patchy::CloneSourceMapping offset(40, 20, 30, -5, flipped(true, false));
  CHECK((offset.map_nearest(10, 25) == std::array<std::int32_t, 2>{40, 20}));
  CHECK((offset.map_nearest(14, 27) == std::array<std::int32_t, 2>{36, 22}));
  // Both flips are the half turn, whole pixels.
  const patchy::CloneSourceMapping both(10, 10, 5, 5, flipped(true, true));
  const patchy::CloneSourceMapping half_turn(10, 10, 5, 5, patchy::CloneSourceTransform{100.0, 180.0});
  for (std::int32_t y = -2; y < 20; y += 3) {
    for (std::int32_t x = -4; x < 24; x += 5) {
      CHECK(both.map_q8(x, y) == half_turn.map_q8(x, y));
    }
  }
  // Scale: two destination pixels per mirrored source pixel.
  const patchy::CloneSourceMapping doubled(10, 10, 0, 0, flipped(true, false, 200.0));
  CHECK((doubled.map_q8(14, 10) == std::array<std::int64_t, 2>{8 * 256, 10 * 256}));
  CHECK((doubled.map_q8(10, 12) == std::array<std::int64_t, 2>{10 * 256, 11 * 256}));
  // Rotation applies after the mirror: 90 degrees clockwise of the mirrored content turns
  // a destination step right into a source step up and a step down into a step left.
  const patchy::CloneSourceMapping turned(10, 10, 0, 0, flipped(true, false, 100.0, 90.0));
  CHECK((turned.map_q8(11, 10) == std::array<std::int64_t, 2>{10 * 256, 9 * 256}));
  CHECK((turned.map_q8(10, 11) == std::array<std::int64_t, 2>{9 * 256, 10 * 256}));
  // Both flips and a quarter turn are the opposite quarter turn.
  const patchy::CloneSourceMapping both_turned(10, 10, 0, 0, flipped(true, true, 100.0, 90.0));
  const patchy::CloneSourceMapping opposite(10, 10, 0, 0, patchy::CloneSourceTransform{100.0, -90.0});
  for (std::int32_t y = 0; y < 20; y += 3) {
    for (std::int32_t x = 0; x < 20; x += 4) {
      CHECK(both_turned.map_q8(x, y) == opposite.map_q8(x, y));
    }
  }
}

// The bilinear sample: whole positions read the pixel unchanged, a half step averages in
// premultiplied form (a transparent neighbor adds no color), and positions whose nearest
// pixel is outside the source give no taps.
void clone_source_bilinear_sample_is_premultiplied_and_bounded() {
  // 2 x 1: opaque red, then transparent green.
  const std::array<std::uint8_t, 8> pixels = {200, 40, 20, 255, 0, 255, 0, 0};
  const auto whole = patchy::clone_source_taps(0, 0, 2, 1);
  CHECK(whole.has_value());
  if (whole.has_value()) {
    CHECK((patchy::sample_clone_source_rgba8(pixels.data(), 8, *whole) ==
           std::array<std::uint8_t, 4>{200, 40, 20, 255}));
  }
  const auto half = patchy::clone_source_taps(128, 0, 2, 1);
  CHECK(half.has_value());
  if (half.has_value()) {
    CHECK(half->x0 == 0 && half->x1 == 1 && half->wx == 128 && half->y0 == 0 && half->y1 == 0);
    CHECK((patchy::sample_clone_source_rgba8(pixels.data(), 8, *half) ==
           std::array<std::uint8_t, 4>{200, 40, 20, 128}));
  }
  // The last column's right neighbor clamps to the edge.
  const auto edge = patchy::clone_source_taps(256 + 64, 0, 2, 1);
  CHECK(edge.has_value() && edge->x0 == 1 && edge->x1 == 1);
  CHECK(!patchy::clone_source_taps(-129, 0, 2, 1).has_value());
  CHECK(patchy::clone_source_taps(-128, 0, 2, 1).has_value());
  CHECK(!patchy::clone_source_taps(2 * 256 - 128, 0, 2, 1).has_value());
  CHECK(!patchy::clone_source_taps(0, 0, 0, 0).has_value());
}
}
std::vector<patchy::test::TestCase> pixel_tools_tests() {
  return {
      {"clone_source_identity_mapping_is_the_plain_offset", clone_source_identity_mapping_is_the_plain_offset},
      {"clone_source_mapping_scales_and_rotates_about_the_source_point",
       clone_source_mapping_scales_and_rotates_about_the_source_point},
      {"clone_source_bilinear_sample_is_premultiplied_and_bounded",
       clone_source_bilinear_sample_is_premultiplied_and_bounded},
      {"clone_source_mapping_flips_about_the_source_point", clone_source_mapping_flips_about_the_source_point},
      {"resample_nearest_replicates_pixels_at_every_depth", resample_nearest_replicates_pixels_at_every_depth},
      {"resample_bilinear_enlargement_matches_clamped_edge_ramp",
       resample_bilinear_enlargement_matches_clamped_edge_ramp},
      {"resample_reduction_widens_the_kernel", resample_reduction_widens_the_kernel},
      {"resample_cubic_variants_are_distinct_and_keep_flat_color",
       resample_cubic_variants_are_distinct_and_keep_flat_color},
      {"resample_interpolates_premultiplied_alpha", resample_interpolates_premultiplied_alpha},
      {"resample_sixteen_bit_and_float_interpolate", resample_sixteen_bit_and_float_interpolate},
      {"resample_automatic_resolves_by_direction", resample_automatic_resolves_by_direction},
      {"resample_method_ids_round_trip", resample_method_ids_round_trip},
      {"tool_one_pixel_brush_segment_snaps_fractional_points_to_one_pixel",
       tool_one_pixel_brush_segment_snaps_fractional_points_to_one_pixel},
      {"tool_wide_brush_segment_is_fast_and_writes_artifact",
       tool_wide_brush_segment_is_fast_and_writes_artifact},
      {"tool_small_layer_grows_cheaply_under_a_long_stroke", tool_small_layer_grows_cheaply_under_a_long_stroke},
      {"tool_hard_round_segment_skips_its_covered_disk", tool_hard_round_segment_skips_its_covered_disk},
      {"tool_round_dab_skips_its_bounding_square_corners", tool_round_dab_skips_its_bounding_square_corners},
      {"tool_brush_segment_accepts_float_endpoints", tool_brush_segment_accepts_float_endpoints},
      {"tool_brush_roundness_and_angle_shape_dabs", tool_brush_roundness_and_angle_shape_dabs},
      {"tool_eraser_clears_alpha_and_writes_artifact", tool_eraser_clears_alpha_and_writes_artifact},
      {"tool_eraser_converts_rgb_layer_to_transparency", tool_eraser_converts_rgb_layer_to_transparency},
      {"tool_smudge_drags_source_pixels_and_writes_artifact", tool_smudge_drags_source_pixels_and_writes_artifact},
      {"tool_line_draws_and_writes_artifact", tool_line_draws_and_writes_artifact},
      {"tool_rectangle_draws_outline_and_writes_artifact", tool_rectangle_draws_outline_and_writes_artifact},
      {"tool_ellipse_draws_and_writes_artifact", tool_ellipse_draws_and_writes_artifact},
      {"tool_filled_ellipse_uses_direct_fill_and_writes_artifact",
       tool_filled_ellipse_uses_direct_fill_and_writes_artifact},
      {"tool_thick_ellipse_outline_avoids_buildup", tool_thick_ellipse_outline_avoids_buildup},
      {"tool_filled_ellipse_respects_softness", tool_filled_ellipse_respects_softness},
      {"tool_ellipse_outline_thickness_is_uniform", tool_ellipse_outline_thickness_is_uniform},
      {"tool_rounded_rectangle_rounds_corners", tool_rounded_rectangle_rounds_corners},
      {"tool_fill_rect_honors_opacity_and_softness_feather",
       tool_fill_rect_honors_opacity_and_softness_feather},
      {"tool_fill_bucket_fills_region_and_writes_artifact", tool_fill_bucket_fills_region_and_writes_artifact},
      {"tool_fill_bucket_tolerance_metric_matches_magic_wand", tool_fill_bucket_tolerance_metric_matches_magic_wand},
      {"tool_magic_wand_seed_averages_the_sample_square", tool_magic_wand_seed_averages_the_sample_square},
      {"tool_fill_bucket_tolerance_fills_near_colors_but_not_text",
       tool_fill_bucket_tolerance_fills_near_colors_but_not_text},
      {"tool_fill_bucket_non_contiguous_fills_every_matching_pixel",
       tool_fill_bucket_non_contiguous_fills_every_matching_pixel},
      {"tool_fill_bucket_opacity_blends_and_terminates", tool_fill_bucket_opacity_blends_and_terminates},
      {"tool_fill_bucket_softness_feathers_inward_from_region_edge",
       tool_fill_bucket_softness_feathers_inward_from_region_edge},
      {"tool_fill_bucket_transparency_lock_skips_transparent_pixels",
       tool_fill_bucket_transparency_lock_skips_transparent_pixels},
      {"tool_fill_bucket_transparent_pixels_match_whatever_rgb_they_hold",
       tool_fill_bucket_transparent_pixels_match_whatever_rgb_they_hold},
      {"tool_fill_bucket_grows_a_layer_smaller_than_the_canvas", tool_fill_bucket_grows_a_layer_smaller_than_the_canvas},
      {"tool_gradient_and_fill_rect_grow_a_layer_smaller_than_the_canvas",
       tool_gradient_and_fill_rect_grow_a_layer_smaller_than_the_canvas},
      {"tool_gradient_draws_foreground_to_background_and_writes_artifact",
       tool_gradient_draws_foreground_to_background_and_writes_artifact},
      {"tool_gradient_supports_custom_stops_radial_reverse_and_alpha",
       tool_gradient_supports_custom_stops_radial_reverse_and_alpha},
      {"tool_fill_selection_draws_only_selection_and_writes_artifact", tool_fill_selection_draws_only_selection_and_writes_artifact},
      {"tool_clear_selection_erases_only_selection_and_writes_artifact", tool_clear_selection_erases_only_selection_and_writes_artifact},
      {"tool_clear_transparent_selection_is_noop", tool_clear_transparent_selection_is_noop},
      {"tool_clear_selected_opaque_pixels_reports_exact_bounds",
       tool_clear_selected_opaque_pixels_reports_exact_bounds},
      {"tool_fill_clear_gradient_respect_complex_selection_mask",
       tool_fill_clear_gradient_respect_complex_selection_mask},
      {"tool_lock_transparent_pixels_preserves_alpha", tool_lock_transparent_pixels_preserves_alpha},
      {"tool_eraser_and_clear_paint_background_color_under_transparency_lock",
       tool_eraser_and_clear_paint_background_color_under_transparency_lock},
      {"tool_clear_rgb_selection_converts_only_when_pixels_change",
       tool_clear_rgb_selection_converts_only_when_pixels_change},
      {"tool_write_paths_digest_baseline", tool_write_paths_digest_baseline},
      {"tool_paint_pixel_block_native_blend_selection_and_palette", tool_paint_pixel_block_native_blend_selection_and_palette},
  };
}
