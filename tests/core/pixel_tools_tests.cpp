#include "color/color_management.hpp"
#include "core/adjustment_layer.hpp"
#include "core/blend_math.hpp"
#include "core/document.hpp"
#include "core/layer_metadata.hpp"
#include "core/layer_tree.hpp"
#include "core/gradient_presets.hpp"
#include "filters/filter_engine.hpp"
#include "filters/filter_registry.hpp"
#include "filters/smart_filter_recipe_mapping.hpp"
#include "filters/smart_filter_renderer.hpp"
#include "formats/acv_curves_io.hpp"
#include "formats/bmp_document_io.hpp"
#include "formats/aseprite_document_io.hpp"
#include "formats/document_flatten.hpp"
#include "formats/format_registry.hpp"
#include "formats/gif_document_io.hpp"
#include "formats/heif_document_io.hpp"
#include "formats/ico_document_io.hpp"
#include "formats/ilbm_document_io.hpp"
#include "formats/image_density_probe.hpp"
#include "formats/palette_io.hpp"
#include "formats/pcx_document_io.hpp"
#include "formats/raw_document_io.hpp"
#include "formats/raw_tone.hpp"
#include "formats/raw_white_balance.hpp"
#include "formats/tga_document_io.hpp"
#include "plugins/legacy_photoshop_adapter.hpp"
#include "plugins/plugin_host.hpp"
#include "psd/abr_reader.hpp"
#include "psd/grd_io.hpp"
#include "psd/asl_io.hpp"
#include "psd/pat_reader.hpp"
#include "psd/psd_binary.hpp"
#include "psd/psd_descriptor.hpp"
#include "psd/psd_filter_effects.hpp"
#include "psd/psd_layer_effects.hpp"
#include "psd/psd_patterns.hpp"
#include "psd/psd_smart_objects.hpp"
#include "core/text_warp.hpp"
#include "core/warp_mesh.hpp"
#include "psd/psd_document_io.hpp"
#include "core/contour_presets.hpp"
#include "core/magnetic_lasso.hpp"
#include "core/palette.hpp"
#include "core/palette_presets.hpp"
#include "core/pattern_presets.hpp"
#include "core/style_contour.hpp"
#include "core/style_presets.hpp"
#include "core/pixel_tools.hpp"
#include "core/quick_select.hpp"
#include "render/compositor.hpp"
#include "render/layer_compositor.hpp"
#include "render/tile_cache.hpp"
#include "support/string_utils.hpp"
#include "test_harness.hpp"
#include "local_psd_fixtures.hpp"
#include "synthetic_dng.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <exception>
#include <cstdint>
#include <fstream>
#include <functional>
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

  CHECK(patchy::clear_rect_change_bounds(document, layer_id, patchy::Rect{18, 18, 6, 6}, options).empty());
  CHECK(patchy::clear_rect(document, layer_id, patchy::Rect{18, 18, 6, 6}, options).empty());
  CHECK(document.find_layer(layer_id)->pixels().pixel(20, 20)[3] == 255);
  write_bmp_artifact("tool_lock_transparency", document);
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

  static constexpr std::array<std::pair<const char*, std::uint64_t>, 14> kExpected = {{
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

std::vector<patchy::test::TestCase> pixel_tools_tests() {
  return {
      {"tool_one_pixel_brush_segment_snaps_fractional_points_to_one_pixel",
       tool_one_pixel_brush_segment_snaps_fractional_points_to_one_pixel},
      {"tool_wide_brush_segment_is_fast_and_writes_artifact",
       tool_wide_brush_segment_is_fast_and_writes_artifact},
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
      {"tool_clear_rgb_selection_converts_only_when_pixels_change",
       tool_clear_rgb_selection_converts_only_when_pixels_change},
      {"tool_write_paths_digest_baseline", tool_write_paths_digest_baseline},
  };
}
