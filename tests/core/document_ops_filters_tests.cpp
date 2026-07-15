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
using patchy::test::make_tool_document;
using patchy::test::solid_rgb;
using patchy::test::solid_rgba;
using patchy::test::tool_options;
using patchy::test::write_bmp_artifact;

patchy::Document make_filter_document() {
  patchy::Document document(32, 24, patchy::PixelFormat::rgb8());
  patchy::PixelBuffer pixels(32, 24, patchy::PixelFormat::rgb8());
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = static_cast<std::uint8_t>(x * 8);
      px[1] = static_cast<std::uint8_t>(y * 10);
      px[2] = static_cast<std::uint8_t>(80 + (x + y) % 120);
    }
  }
  document.add_pixel_layer("Filter Source", std::move(pixels));
  return document;
}

void tool_flip_horizontal_changes_pixels_and_writes_artifact() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(255, 0, 0);
  CHECK(!patchy::fill_rect(document, layer_id, patchy::Rect{0, 0, 8, 48}, options).empty());
  CHECK(!patchy::flip_layer_horizontal(document, layer_id).empty());
  CHECK(document.find_layer(layer_id)->pixels().pixel(63, 10)[3] == 255);
  CHECK(document.find_layer(layer_id)->pixels().pixel(0, 10)[3] == 0);
  write_bmp_artifact("tool_flip_horizontal", document);
}

void tool_flip_vertical_changes_pixels_and_writes_artifact() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(0, 0, 255);
  CHECK(!patchy::fill_rect(document, layer_id, patchy::Rect{0, 0, 64, 8}, options).empty());
  CHECK(!patchy::flip_layer_vertical(document, layer_id).empty());
  CHECK(document.find_layer(layer_id)->pixels().pixel(10, 47)[3] == 255);
  CHECK(document.find_layer(layer_id)->pixels().pixel(10, 0)[3] == 0);
  write_bmp_artifact("tool_flip_vertical", document);
}

void document_crop_to_selection_changes_canvas_and_writes_artifact() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(255, 0, 180);
  CHECK(!patchy::fill_rect(document, layer_id, patchy::Rect{12, 8, 4, 4}, options).empty());
  CHECK(patchy::crop_document(document, patchy::Rect{8, 6, 32, 20}));
  CHECK(document.width() == 32);
  CHECK(document.height() == 20);
  const auto* px = document.find_layer(layer_id)->pixels().pixel(4, 2);
  CHECK(px[0] == 255);
  CHECK(px[1] == 0);
  CHECK(px[2] == 180);
  CHECK(px[3] == 255);
  write_bmp_artifact("document_crop", document);
}

void document_canvas_resize_expands_layers_for_editing() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(10, 90, 220);
  CHECK(!patchy::paint_brush(document, layer_id, 20, 20, options, false).empty());

  patchy::resize_canvas_and_layers(document, 96, 72);
  CHECK(document.width() == 96);
  CHECK(document.height() == 72);
  const auto* layer = document.find_layer(layer_id);
  CHECK(layer != nullptr);
  CHECK(layer->bounds().x == 0);
  CHECK(layer->bounds().y == 0);
  CHECK(layer->pixels().width() == 96);
  CHECK(layer->pixels().height() == 72);
  CHECK(layer->pixels().pixel(20, 20)[3] == 255);

  CHECK(!patchy::paint_brush(document, layer_id, 90, 66, options, false).empty());
  CHECK(layer->pixels().pixel(90, 66)[3] == 255);
  write_bmp_artifact("document_canvas_resize", document);
}

void document_canvas_resize_honors_anchor_and_extension_color() {
  patchy::Document document(4, 4, patchy::PixelFormat::rgb8());
  const auto& background = document.add_pixel_layer("Background", solid_rgb(4, 4, 255, 255, 255));
  const auto background_id = background.id();
  patchy::Layer sticker(document.allocate_layer_id(), "Sticker", solid_rgba(1, 1, 220, 10, 90, 255));
  const auto sticker_id = sticker.id();
  sticker.set_bounds(patchy::Rect{1, 1, 1, 1});
  document.add_layer(std::move(sticker));

  patchy::resize_canvas_and_layers(document, 6, 6, patchy::CanvasAnchor::Center,
                                      patchy::EditColor{12, 34, 56, 255});
  CHECK(document.width() == 6);
  CHECK(document.height() == 6);

  const auto* background_layer = document.find_layer(background_id);
  CHECK(background_layer != nullptr);
  CHECK(background_layer->pixels().pixel(0, 0)[0] == 12);
  CHECK(background_layer->pixels().pixel(0, 0)[1] == 34);
  CHECK(background_layer->pixels().pixel(0, 0)[2] == 56);
  CHECK(background_layer->pixels().pixel(1, 1)[0] == 255);

  const auto* sticker_layer = document.find_layer(sticker_id);
  CHECK(sticker_layer != nullptr);
  CHECK(sticker_layer->bounds().x == 0);
  CHECK(sticker_layer->bounds().y == 0);
  CHECK(sticker_layer->pixels().pixel(1, 1)[3] == 0);
  CHECK(sticker_layer->pixels().pixel(2, 2)[0] == 220);
  CHECK(sticker_layer->pixels().pixel(2, 2)[1] == 10);
  CHECK(sticker_layer->pixels().pixel(2, 2)[2] == 90);
  CHECK(sticker_layer->pixels().pixel(2, 2)[3] == 255);
}

void document_image_resize_scales_layers_and_writes_artifact() {
  patchy::Document document(64, 48, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(64, 48, 255, 255, 255));
  patchy::Layer sticker(document.allocate_layer_id(), "Sticker", solid_rgba(4, 4, 230, 20, 150, 255));
  const auto sticker_id = sticker.id();
  sticker.set_bounds(patchy::Rect{10, 5, 4, 4});
  document.add_layer(std::move(sticker));

  patchy::resize_image_and_layers(document, 128, 96);
  CHECK(document.width() == 128);
  CHECK(document.height() == 96);
  const auto* layer = document.find_layer(sticker_id);
  CHECK(layer != nullptr);
  CHECK(layer->bounds().x == 20);
  CHECK(layer->bounds().y == 10);
  CHECK(layer->bounds().width == 8);
  CHECK(layer->bounds().height == 8);
  CHECK(layer->pixels().width() == 8);
  CHECK(layer->pixels().height() == 8);
  const auto* px = layer->pixels().pixel(4, 4);
  CHECK(px[0] == 230);
  CHECK(px[1] == 20);
  CHECK(px[2] == 150);
  CHECK(px[3] == 255);
  write_bmp_artifact("document_image_resize", document);
}

void document_rotate_clockwise_changes_canvas_and_writes_artifact() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(255, 120, 0);
  CHECK(!patchy::fill_rect(document, layer_id, patchy::Rect{0, 0, 8, 6}, options).empty());
  patchy::rotate_document_clockwise(document);
  CHECK(document.width() == 48);
  CHECK(document.height() == 64);
  CHECK(document.find_layer(layer_id)->pixels().pixel(47, 0)[3] == 255);
  CHECK(document.find_layer(layer_id)->pixels().pixel(0, 0)[3] == 0);
  write_bmp_artifact("document_rotate_clockwise", document);
}

void document_rotate_counterclockwise_changes_canvas_and_writes_artifact() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(40, 180, 255);
  CHECK(!patchy::fill_rect(document, layer_id, patchy::Rect{0, 0, 8, 6}, options).empty());
  patchy::rotate_document_counterclockwise(document);
  CHECK(document.width() == 48);
  CHECK(document.height() == 64);
  CHECK(document.find_layer(layer_id)->pixels().pixel(0, 63)[3] == 255);
  CHECK(document.find_layer(layer_id)->pixels().pixel(47, 0)[3] == 0);
  write_bmp_artifact("document_rotate_counterclockwise", document);
}

void tool_stroke_selection_draws_border_and_writes_artifact() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(20, 20, 20);
  options.brush_size = 3;
  options.selection = patchy::Rect{14, 10, 30, 22};
  const auto dirty = patchy::draw_rectangle(document, layer_id, *options.selection, options, false);
  CHECK(!dirty.empty());
  CHECK(document.find_layer(layer_id)->pixels().pixel(14, 10)[3] == 255);
  CHECK(document.find_layer(layer_id)->pixels().pixel(20, 16)[3] == 0);
  write_bmp_artifact("tool_stroke_selection", document);
}

void layer_merge_visible_creates_flattened_artifact() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(0, 120, 255);
  CHECK(!patchy::fill_rect(document, layer_id, patchy::Rect{4, 4, 24, 18}, options).empty());
  auto merged_pixels = patchy::Compositor{}.flatten_rgb8(document);
  document.add_pixel_layer("Merged Visible", std::move(merged_pixels));
  CHECK(document.layers().size() == 3);
  CHECK(document.layers().back().pixels().format() == patchy::PixelFormat::rgb8());
  CHECK(document.layers().back().pixels().pixel(5, 5)[2] == 255);
  write_bmp_artifact("layer_merge_visible", document);
}

void filters_register_and_apply() {
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  CHECK(registry.find("patchy.filters.invert") != nullptr);
  CHECK(registry.find("patchy.filters.brightness_contrast") != nullptr);
  CHECK(registry.find("patchy.filters.brightness_plus") == nullptr);
  CHECK(registry.find("patchy.filters.contrast_plus") == nullptr);
  CHECK(registry.find("patchy.filters.grayscale") != nullptr);
  CHECK(registry.find("patchy.filters.desaturate") != nullptr);
  CHECK(registry.find("patchy.filters.auto_contrast") != nullptr);
  CHECK(registry.find("patchy.filters.soft_glow") != nullptr);
  CHECK(registry.find("patchy.filters.punchy_color") != nullptr);
  CHECK(registry.find("patchy.filters.noir") != nullptr);
  CHECK(registry.find("patchy.filters.cinematic_matte") != nullptr);
  CHECK(registry.find("patchy.filters.vintage_fade") != nullptr);
  CHECK(registry.find("patchy.filters.sepia") != nullptr);
  CHECK(registry.find("patchy.filters.threshold") != nullptr);
  CHECK(registry.find("patchy.filters.posterize") != nullptr);
  CHECK(registry.find("patchy.filters.box_blur") != nullptr);
  CHECK(registry.find("patchy.filters.sharpen") != nullptr);
  CHECK(registry.find("patchy.filters.unsharp_mask") != nullptr);
  CHECK(registry.find("patchy.filters.gaussian_blur") != nullptr);
  CHECK(registry.find("patchy.filters.motion_blur") != nullptr);
  CHECK(registry.find("patchy.filters.radial_blur") != nullptr);
  CHECK(registry.find("patchy.filters.edge_detect") != nullptr);
  CHECK(registry.find("patchy.filters.emboss") != nullptr);
  CHECK(registry.find("patchy.filters.glowing_edges") != nullptr);
  CHECK(registry.find("patchy.filters.twirl") != nullptr);
  CHECK(registry.find("patchy.filters.wave") != nullptr);
  CHECK(registry.find("patchy.filters.pinch_bloat") != nullptr);
  CHECK(registry.find("patchy.filters.clouds") != nullptr);
  CHECK(registry.find("patchy.filters.pixelate") != nullptr);
  CHECK(registry.find("patchy.filters.color_halftone") != nullptr);
  CHECK(registry.find("patchy.filters.film_grain") != nullptr);
  CHECK(registry.find("patchy.filters.vignette") != nullptr);
  CHECK(registry.find("patchy.filters.plastic_wrap") != nullptr);

  auto pixels = solid_rgb(1, 1, 1, 2, 3);
  registry.apply("patchy.filters.invert", pixels);
  const auto* px = pixels.pixel(0, 0);
  CHECK(px[0] == 254);
  CHECK(px[1] == 253);
  CHECK(px[2] == 252);
}

void filters_builtin_effects_apply_and_write_artifacts() {
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);

  const std::vector<std::pair<std::string, std::string>> filters = {
      {"patchy.filters.brightness_contrast", "filter_brightness_contrast"},
      {"patchy.filters.grayscale", "filter_grayscale"},
      {"patchy.filters.desaturate", "filter_desaturate"},
      {"patchy.filters.auto_contrast", "filter_auto_contrast"},
      {"patchy.filters.soft_glow", "filter_soft_glow"},
      {"patchy.filters.punchy_color", "filter_punchy_color"},
      {"patchy.filters.noir", "filter_noir"},
      {"patchy.filters.cinematic_matte", "filter_cinematic_matte"},
      {"patchy.filters.vintage_fade", "filter_vintage_fade"},
      {"patchy.filters.sepia", "filter_sepia"},
      {"patchy.filters.threshold", "filter_threshold"},
      {"patchy.filters.posterize", "filter_posterize"},
      {"patchy.filters.box_blur", "filter_box_blur"},
      {"patchy.filters.sharpen", "filter_sharpen"},
      {"patchy.filters.unsharp_mask", "filter_unsharp_mask"},
      {"patchy.filters.gaussian_blur", "filter_gaussian_blur"},
      {"patchy.filters.motion_blur", "filter_motion_blur"},
      {"patchy.filters.radial_blur", "filter_radial_blur"},
      {"patchy.filters.edge_detect", "filter_edge_detect"},
      {"patchy.filters.emboss", "filter_emboss"},
      {"patchy.filters.glowing_edges", "filter_glowing_edges"},
      {"patchy.filters.twirl", "filter_twirl"},
      {"patchy.filters.wave", "filter_wave"},
      {"patchy.filters.pinch_bloat", "filter_pinch_bloat"},
      {"patchy.filters.clouds", "filter_clouds"},
      {"patchy.filters.pixelate", "filter_pixelate"},
      {"patchy.filters.color_halftone", "filter_color_halftone"},
      {"patchy.filters.film_grain", "filter_film_grain"},
      {"patchy.filters.vignette", "filter_vignette"},
      {"patchy.filters.plastic_wrap", "filter_plastic_wrap"},
  };

  for (const auto& [identifier, artifact_name] : filters) {
    auto document = make_filter_document();
    auto& pixels = document.layers().front().pixels();
    registry.apply(identifier, pixels);
    CHECK(!pixels.empty());
    write_bmp_artifact(artifact_name, document);
  }

  auto brightness_contrast = make_filter_document();
  const auto* original_brightness_contrast_px = brightness_contrast.layers().front().pixels().pixel(0, 0);
  const auto original_brightness_contrast_red = original_brightness_contrast_px[0];
  const auto original_brightness_contrast_green = original_brightness_contrast_px[1];
  const auto original_brightness_contrast_blue = original_brightness_contrast_px[2];
  registry.apply("patchy.filters.brightness_contrast", brightness_contrast.layers().front().pixels());
  const auto* brightness_contrast_px = brightness_contrast.layers().front().pixels().pixel(0, 0);
  CHECK(brightness_contrast_px[0] == original_brightness_contrast_red);
  CHECK(brightness_contrast_px[1] == original_brightness_contrast_green);
  CHECK(brightness_contrast_px[2] == original_brightness_contrast_blue);

  auto threshold = make_filter_document();
  registry.apply("patchy.filters.threshold", threshold.layers().front().pixels());
  const auto* threshold_px = threshold.layers().front().pixels().pixel(0, 0);
  CHECK(threshold_px[0] == 0);
  CHECK(threshold_px[1] == 0);
  CHECK(threshold_px[2] == 0);

  auto desaturate = make_filter_document();
  registry.apply("patchy.filters.desaturate", desaturate.layers().front().pixels());
  const auto* desaturated_px = desaturate.layers().front().pixels().pixel(3, 2);
  CHECK(desaturated_px[0] == desaturated_px[1]);
  CHECK(desaturated_px[1] == desaturated_px[2]);

  auto auto_contrast = make_filter_document();
  registry.apply("patchy.filters.auto_contrast", auto_contrast.layers().front().pixels());
  const auto* low_px = auto_contrast.layers().front().pixels().pixel(0, 0);
  const auto* high_px = auto_contrast.layers().front().pixels().pixel(31, 23);
  CHECK(low_px[0] == 0);
  CHECK(low_px[1] == 0);
  CHECK(low_px[2] == 0);
  CHECK(high_px[0] == 255);
  CHECK(high_px[1] == 255);
  CHECK(high_px[2] == 255);

  auto noir = make_filter_document();
  registry.apply("patchy.filters.noir", noir.layers().front().pixels());
  const auto* noir_px = noir.layers().front().pixels().pixel(10, 10);
  CHECK(noir_px[0] == noir_px[1]);
  CHECK(noir_px[1] == noir_px[2]);

  auto pin_blur = patchy::PixelBuffer(5, 5, patchy::PixelFormat::rgb8());
  pin_blur.pixel(2, 2)[0] = 255;
  pin_blur.pixel(2, 2)[1] = 255;
  pin_blur.pixel(2, 2)[2] = 255;
  registry.apply("patchy.filters.gaussian_blur", pin_blur);
  CHECK(pin_blur.pixel(2, 2)[0] > pin_blur.pixel(1, 2)[0]);
  CHECK(pin_blur.pixel(1, 2)[0] > 0);
  CHECK(pin_blur.pixel(2, 2)[0] < 255);

  auto unsharp = solid_rgb(5, 1, 40, 40, 40);
  for (std::int32_t x = 2; x < unsharp.width(); ++x) {
    auto* px = unsharp.pixel(x, 0);
    px[0] = 160;
    px[1] = 160;
    px[2] = 160;
  }
  registry.apply("patchy.filters.unsharp_mask", unsharp);
  CHECK(unsharp.pixel(1, 0)[0] < 40);
  CHECK(unsharp.pixel(2, 0)[0] > 160);

  auto motion = solid_rgb(31, 1, 0, 0, 0);
  motion.pixel(15, 0)[0] = 255;
  motion.pixel(15, 0)[1] = 255;
  motion.pixel(15, 0)[2] = 255;
  registry.apply("patchy.filters.motion_blur", motion);
  CHECK(motion.pixel(15, 0)[0] < 255);
  CHECK(motion.pixel(4, 0)[0] > 0);
  CHECK(motion.pixel(26, 0)[0] > 0);

  const auto make_transparent_red_stroke = [] {
    auto pixels = solid_rgba(65, 65, 0, 0, 0, 0);
    for (std::int32_t y = 10; y <= 54; ++y) {
      for (std::int32_t x = 43; x <= 45; ++x) {
        auto* px = pixels.pixel(x, y);
        px[0] = 230;
        px[1] = 20;
        px[2] = 20;
        px[3] = 255;
      }
    }
    for (std::int32_t i = 0; i < 30; ++i) {
      auto* px = pixels.pixel(16 + i, 42 - i / 2);
      px[0] = 230;
      px[1] = 20;
      px[2] = 20;
      px[3] = 255;
    }
    return pixels;
  };
  const auto check_transparent_spatial_filter = [&](std::string_view identifier) {
    const auto before = make_transparent_red_stroke();
    auto after = before;
    registry.apply(identifier, after);
    int spread_pixels = 0;
    bool kept_clean_red = false;
    for (std::int32_t y = 0; y < after.height(); ++y) {
      for (std::int32_t x = 0; x < after.width(); ++x) {
        const auto* src = before.pixel(x, y);
        const auto* dst = after.pixel(x, y);
        if (src[3] == 0 && dst[3] > 8) {
          ++spread_pixels;
          kept_clean_red = kept_clean_red || (dst[0] > 180 && dst[1] < 80 && dst[2] < 80);
        }
      }
    }
    CHECK(spread_pixels > 0);
    CHECK(kept_clean_red);
  };
  check_transparent_spatial_filter("patchy.filters.box_blur");
  check_transparent_spatial_filter("patchy.filters.gaussian_blur");
  check_transparent_spatial_filter("patchy.filters.motion_blur");
  check_transparent_spatial_filter("patchy.filters.radial_blur");
  check_transparent_spatial_filter("patchy.filters.pixelate");
  auto transparent_clouds = solid_rgba(16, 16, 0, 0, 0, 0);
  registry.apply("patchy.filters.clouds", transparent_clouds);
  CHECK(transparent_clouds.pixel(0, 0)[3] == 255);
  CHECK(transparent_clouds.pixel(15, 15)[3] == 255);

  auto edge = solid_rgb(3, 3, 0, 0, 0);
  for (std::int32_t y = 0; y < edge.height(); ++y) {
    for (std::int32_t x = 1; x < edge.width(); ++x) {
      auto* px = edge.pixel(x, y);
      px[0] = 255;
      px[1] = 255;
      px[2] = 255;
    }
  }
  registry.apply("patchy.filters.edge_detect", edge);
  CHECK(edge.pixel(1, 1)[0] == 255);
  CHECK(edge.pixel(1, 1)[1] == 255);
  CHECK(edge.pixel(1, 1)[2] == 255);

  auto glowing = solid_rgb(3, 3, 0, 0, 0);
  for (std::int32_t y = 0; y < glowing.height(); ++y) {
    for (std::int32_t x = 1; x < glowing.width(); ++x) {
      auto* px = glowing.pixel(x, y);
      px[0] = 255;
      px[1] = 255;
      px[2] = 255;
    }
  }
  registry.apply("patchy.filters.glowing_edges", glowing);
  CHECK(glowing.pixel(1, 1)[0] > 0);
  CHECK(glowing.pixel(0, 1)[0] < glowing.pixel(1, 1)[0]);

  auto relief = solid_rgb(3, 3, 100, 110, 120);
  registry.apply("patchy.filters.emboss", relief);
  CHECK(relief.pixel(1, 1)[0] == 128);
  CHECK(relief.pixel(1, 1)[1] == 128);
  CHECK(relief.pixel(1, 1)[2] == 128);

  auto twirled = make_filter_document();
  const auto twirl_before = twirled.layers().front().pixels();
  registry.apply("patchy.filters.twirl", twirled.layers().front().pixels());
  const auto twirl_after_data = twirled.layers().front().pixels().data();
  const auto twirl_before_data = twirl_before.data();
  CHECK(!std::equal(twirl_after_data.begin(), twirl_after_data.end(), twirl_before_data.begin()));

  auto wave = patchy::PixelBuffer(64, 16, patchy::PixelFormat::rgb8());
  for (std::int32_t y = 0; y < wave.height(); ++y) {
    for (std::int32_t x = 0; x < wave.width(); ++x) {
      auto* px = wave.pixel(x, y);
      px[0] = static_cast<std::uint8_t>(x * 3);
      px[1] = 0;
      px[2] = 0;
    }
  }
  const auto wave_before = wave.pixel(10, 12)[0];
  registry.apply("patchy.filters.wave", wave);
  CHECK(wave.pixel(10, 12)[0] > wave_before);

  auto pinch = patchy::PixelBuffer(33, 33, patchy::PixelFormat::rgb8());
  for (std::int32_t y = 0; y < pinch.height(); ++y) {
    for (std::int32_t x = 0; x < pinch.width(); ++x) {
      auto* px = pinch.pixel(x, y);
      px[0] = static_cast<std::uint8_t>(x * 6);
      px[1] = 0;
      px[2] = 0;
    }
  }
  const auto pinch_before = pinch.pixel(20, 16)[0];
  registry.apply("patchy.filters.pinch_bloat", pinch);
  CHECK(pinch.pixel(20, 16)[0] < pinch_before);

  auto clouds = make_filter_document();
  registry.apply("patchy.filters.clouds", clouds.layers().front().pixels());
  const auto* cloud_a = clouds.layers().front().pixels().pixel(0, 0);
  const auto* cloud_b = clouds.layers().front().pixels().pixel(31, 23);
  CHECK(cloud_a[0] == cloud_a[1]);
  CHECK(cloud_a[1] == cloud_a[2]);
  CHECK(cloud_a[0] != cloud_b[0]);

  auto pixelated = make_filter_document();
  registry.apply("patchy.filters.pixelate", pixelated.layers().front().pixels());
  const auto* pixelated_px = pixelated.layers().front().pixels().pixel(0, 0);
  CHECK(pixelated_px[0] == 12);
  CHECK(pixelated_px[1] == 15);
  CHECK(pixelated_px[2] == 83);

  auto halftone = solid_rgb(24, 24, 128, 128, 128);
  registry.apply("patchy.filters.color_halftone", halftone);
  bool halftone_varied = false;
  const auto* halftone_first = halftone.pixel(0, 0);
  for (std::int32_t y = 0; y < halftone.height(); ++y) {
    for (std::int32_t x = 0; x < halftone.width(); ++x) {
      const auto* px = halftone.pixel(x, y);
      halftone_varied = halftone_varied || px[0] != halftone_first[0] || px[1] != halftone_first[1] ||
                        px[2] != halftone_first[2];
    }
  }
  CHECK(halftone_varied);

  auto grain_a = solid_rgb(2, 2, 128, 128, 128);
  auto grain_b = solid_rgb(2, 2, 128, 128, 128);
  registry.apply("patchy.filters.film_grain", grain_a);
  registry.apply("patchy.filters.film_grain", grain_b);
  bool grain_changed = false;
  for (std::int32_t y = 0; y < grain_a.height(); ++y) {
    for (std::int32_t x = 0; x < grain_a.width(); ++x) {
      const auto* a = grain_a.pixel(x, y);
      const auto* b = grain_b.pixel(x, y);
      for (std::uint16_t channel = 0; channel < 3; ++channel) {
        CHECK(a[channel] == b[channel]);
        grain_changed = grain_changed || a[channel] != 128;
      }
    }
  }
  CHECK(grain_changed);

  auto vignetted = solid_rgb(5, 5, 255, 255, 255);
  registry.apply("patchy.filters.vignette", vignetted);
  CHECK(vignetted.pixel(2, 2)[0] == 255);
  CHECK(vignetted.pixel(0, 0)[0] < 130);
}

void filter_catalog_defines_stable_named_contracts() {
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);

  using Category = patchy::FilterCategory;
  struct ExpectedFilter {
    const char* identifier;
    Category category;
    bool adjustment_only;
    std::vector<std::pair<const char*, std::int64_t>> defaults;
  };
  const std::vector<ExpectedFilter> expected = {
      {"patchy.filters.invert", Category::Adjustment, true, {{"amount", 100}}},
      {"patchy.filters.brightness_contrast", Category::Adjustment, true,
       {{"brightness", 0}, {"contrast", 0}}},
      {"patchy.filters.grayscale", Category::Adjustment, true, {{"amount", 100}}},
      {"patchy.filters.desaturate", Category::Adjustment, true, {{"amount", 100}}},
      {"patchy.filters.auto_contrast", Category::Adjustment, true, {{"amount", 100}}},
      {"patchy.filters.soft_glow", Category::PhotoLooks, false, {{"amount", 100}}},
      {"patchy.filters.punchy_color", Category::PhotoLooks, false, {{"amount", 100}}},
      {"patchy.filters.noir", Category::PhotoLooks, false, {{"amount", 100}}},
      {"patchy.filters.cinematic_matte", Category::PhotoLooks, false, {{"amount", 100}}},
      {"patchy.filters.vintage_fade", Category::PhotoLooks, false, {{"amount", 100}}},
      {"patchy.filters.sepia", Category::PhotoLooks, false, {{"amount", 100}}},
      {"patchy.filters.threshold", Category::Adjustment, true, {{"threshold", 128}}},
      {"patchy.filters.posterize", Category::Adjustment, true, {{"levels", 4}}},
      {"patchy.filters.box_blur", Category::Blur, false, {{"radius", 1}}},
      {"patchy.filters.sharpen", Category::Sharpen, false, {{"amount", 100}}},
      {"patchy.filters.unsharp_mask", Category::Sharpen, false,
       {{"amount", 150}, {"radius", 2}, {"threshold", 8}}},
      {"patchy.filters.gaussian_blur", Category::Blur, false, {{"radius", 2}}},
      {"patchy.filters.motion_blur", Category::Blur, false, {{"angle", 0}, {"distance", 12}}},
      {"patchy.filters.radial_blur", Category::Blur, false, {{"amount", 35}, {"samples", 16}}},
      {"patchy.filters.edge_detect", Category::Stylize, false, {{"strength", 100}}},
      {"patchy.filters.emboss", Category::Stylize, false,
       {{"angle", 135}, {"height", 2}, {"amount", 100}}},
      {"patchy.filters.glowing_edges", Category::Stylize, false,
       {{"edge_width", 2}, {"brightness", 140}, {"smoothness", 2}}},
      {"patchy.filters.twirl", Category::Distort, false, {{"angle", 180}, {"radius", 100}}},
      {"patchy.filters.wave", Category::Distort, false,
       {{"amplitude", 12}, {"wavelength", 48}, {"phase", 0}}},
      {"patchy.filters.pinch_bloat", Category::Distort, false,
       {{"amount", 35}, {"radius", 100}}},
      {"patchy.filters.clouds", Category::Render, false,
       {{"scale", 96}, {"detail", 6}, {"contrast", 40}, {"seed", 1}}},
      {"patchy.filters.pixelate", Category::Pixelate, false, {{"block_size", 4}}},
      {"patchy.filters.color_halftone", Category::Pixelate, false,
       {{"cell_size", 10}, {"intensity", 75}, {"contrast", 60}}},
      {"patchy.filters.film_grain", Category::Noise, false, {{"amount", 50}}},
      {"patchy.filters.vignette", Category::PhotoLooks, false, {{"strength", 55}}},
      {"patchy.filters.high_pass", Category::Sharpen, false, {{"radius", 10}}},
      {"patchy.filters.median", Category::Noise, false, {{"radius", 1}}},
      {"patchy.filters.dust_and_scratches", Category::Noise, false,
       {{"radius", 1}, {"threshold", 0}}},
      {"patchy.filters.surface_blur", Category::Blur, false,
       {{"radius", 5}, {"threshold", 15}}},
      {"patchy.filters.tilt_shift_blur", Category::Blur, false, {}},
      {"patchy.filters.plastic_wrap", Category::Artistic, false,
       {{"highlight_strength", 9}, {"detail", 7}, {"smoothness", 5}}},
  };

  const auto has_center_parameters = [](std::string_view identifier) {
    return identifier == "patchy.filters.radial_blur" ||
           identifier == "patchy.filters.twirl" ||
           identifier == "patchy.filters.pinch_bloat" ||
           identifier == "patchy.filters.vignette";
  };
  const auto expected_presentation = [](std::string_view identifier,
                                        std::string_view key) {
    using Presentation = patchy::FilterParameterPresentation;
    if (key == "center_x") {
      return Presentation::CenterXPercent;
    }
    if (key == "center_y") {
      return Presentation::CenterYPercent;
    }
    if (key == "angle" &&
        (identifier == "patchy.filters.motion_blur" ||
         identifier == "patchy.filters.emboss" ||
         identifier == "patchy.filters.twirl")) {
      return Presentation::Angle;
    }
    if (key == "radius" &&
        (identifier == "patchy.filters.twirl" ||
         identifier == "patchy.filters.pinch_bloat")) {
      return Presentation::EffectRadiusPercent;
    }
    if (identifier == "patchy.filters.wave") {
      if (key == "amplitude") {
        return Presentation::WaveAmplitude;
      }
      if (key == "wavelength") {
        return Presentation::WaveWavelength;
      }
      if (key == "phase") {
        return Presentation::WavePhase;
      }
    }
    return Presentation::Standard;
  };

  CHECK(registry.filters().size() == expected.size());
  std::unordered_set<std::string> spatial_parameters;
  for (std::size_t filter_index = 0; filter_index < expected.size(); ++filter_index) {
    const auto& actual = registry.filters()[filter_index];
    const auto& wanted = expected[filter_index];
    CHECK(actual.identifier == wanted.identifier);
    CHECK(actual.catalog.category == wanted.category);
    CHECK(actual.catalog.adjustment_only == wanted.adjustment_only);
    CHECK(actual.catalog.schema_version == 1);
    CHECK(static_cast<bool>(actual.catalog.execute));
    if (actual.identifier == "patchy.filters.tilt_shift_blur") {
      using Kind = patchy::FilterParameterKind;
      using Presentation = patchy::FilterParameterPresentation;
      using Scale = patchy::FilterSpatialScale;
      using Unit = patchy::FilterParameterUnit;
      CHECK(actual.catalog.parameters.size() == 6U);
      const auto invocation = registry.default_invocation(actual.identifier);
      const auto check_double = [&](std::size_t index, std::string_view key,
                                    std::string_view object_name,
                                    double minimum, double maximum,
                                    double default_value, Unit unit,
                                    Scale scale, Presentation presentation) {
        const auto& parameter = actual.catalog.parameters[index];
        CHECK(parameter.key == key);
        CHECK(parameter.control_object_name == object_name);
        CHECK(parameter.kind == Kind::Double);
        CHECK(parameter.minimum == minimum);
        CHECK(parameter.maximum == maximum);
        CHECK(parameter.step == 0.1);
        CHECK(parameter.unit == unit);
        CHECK(parameter.spatial_scale == scale);
        CHECK(parameter.presentation == presentation);
        CHECK(std::get<double>(parameter.default_value) == default_value);
        CHECK(std::get<double>(invocation.parameters.at(std::string(key))) ==
              default_value);
      };
      check_double(0, "blur", "filterBlur", 0.0, 500.0, 15.0,
                   Unit::Pixels, Scale::Pixels, Presentation::Standard);
      CHECK(actual.catalog.parameters[0].practical_minimum == 0.0);
      CHECK(actual.catalog.parameters[0].practical_maximum == 50.0);
      check_double(1, "center_x", "filterCenterX", 0.0, 100.0, 50.0,
                   Unit::Percent, Scale::None,
                   Presentation::CenterXPercent);
      check_double(2, "center_y", "filterCenterY", 0.0, 100.0, 50.0,
                   Unit::Percent, Scale::None,
                   Presentation::CenterYPercent);
      const auto& angle = actual.catalog.parameters[3];
      CHECK(angle.key == "angle");
      CHECK(angle.control_object_name == "filterAngle");
      CHECK(angle.kind == Kind::Integer);
      CHECK(angle.minimum == -180.0 && angle.maximum == 180.0);
      CHECK(angle.step == 1.0);
      CHECK(angle.unit == Unit::Degrees);
      CHECK(angle.spatial_scale == Scale::None);
      CHECK(angle.presentation == Presentation::Angle);
      CHECK(std::get<std::int64_t>(angle.default_value) == 0);
      CHECK(std::get<std::int64_t>(invocation.parameters.at("angle")) == 0);
      check_double(4, "focus_half_width", "filterFocusHalfWidth", 0.0,
                   100.0, 10.0, Unit::Percent, Scale::None,
                   Presentation::TiltFocusHalfWidthPercent);
      check_double(5, "transition_width", "filterTransitionWidth", 0.0,
                   100.0, 20.0, Unit::Percent, Scale::None,
                   Presentation::TiltTransitionWidthPercent);
      for (std::size_t index = 1; index < 6U; ++index) {
        CHECK(!actual.catalog.parameters[index].practical_minimum.has_value());
        CHECK(!actual.catalog.parameters[index].practical_maximum.has_value());
      }
      spatial_parameters.insert("patchy.filters.tilt_shift_blur/blur");
      continue;
    }
    const auto center_count = has_center_parameters(actual.identifier) ? 2U : 0U;
    CHECK(actual.catalog.parameters.size() == wanted.defaults.size() + center_count);
    const auto invocation = registry.default_invocation(actual.identifier);
    CHECK(invocation.filter_id == actual.identifier);
    CHECK(invocation.schema_version == 1);
    for (std::size_t parameter_index = 0; parameter_index < wanted.defaults.size(); ++parameter_index) {
      const auto& parameter = actual.catalog.parameters[parameter_index];
      const auto& [key, default_value] = wanted.defaults[parameter_index];
      CHECK(parameter.key == key);
      const auto is_high_pass_radius =
          actual.identifier == "patchy.filters.high_pass" &&
          parameter.key == "radius";
      const auto is_median_radius =
          actual.identifier == "patchy.filters.median" &&
          parameter.key == "radius";
      const auto is_surface_blur_radius =
          actual.identifier == "patchy.filters.surface_blur" &&
          parameter.key == "radius";
      const auto is_unsharp_mask_radius =
          actual.identifier == "patchy.filters.unsharp_mask" &&
          parameter.key == "radius";
      const auto is_fractional_radius =
          is_high_pass_radius || is_median_radius || is_surface_blur_radius ||
          is_unsharp_mask_radius;
      CHECK(parameter.kind == (is_fractional_radius
                                   ? patchy::FilterParameterKind::Double
                                   : patchy::FilterParameterKind::Integer));
      CHECK(parameter.display_name.size() > 0);
      CHECK(parameter.control_object_name.size() > 0);
      CHECK(parameter.minimum.has_value());
      CHECK(parameter.maximum.has_value());
      const auto expected_step =
          is_median_radius || is_surface_blur_radius
              ? 0.01
              : (is_high_pass_radius || is_unsharp_mask_radius ? 0.1 : 1.0);
      CHECK(parameter.step == expected_step);
      if (is_fractional_radius) {
        CHECK(std::get<double>(parameter.default_value) ==
              static_cast<double>(default_value));
        CHECK(std::get<double>(invocation.parameters.at(key)) ==
              static_cast<double>(default_value));
      } else {
        CHECK(std::get<std::int64_t>(parameter.default_value) == default_value);
        CHECK(std::get<std::int64_t>(invocation.parameters.at(key)) ==
              default_value);
      }
      CHECK(parameter.presentation ==
            expected_presentation(actual.identifier, parameter.key));
      CHECK(static_cast<double>(default_value) >= *parameter.minimum);
      CHECK(static_cast<double>(default_value) <= *parameter.maximum);
      if (is_high_pass_radius) {
        CHECK(parameter.minimum == 0.1);
        CHECK(parameter.maximum == 1000.0);
        CHECK(parameter.practical_minimum == 0.1);
        CHECK(parameter.practical_maximum == 12.0);
      } else if (is_median_radius) {
        CHECK(parameter.minimum == 1.0);
        CHECK(parameter.maximum == 500.0);
        CHECK(parameter.practical_minimum == 1.0);
        CHECK(parameter.practical_maximum == 25.0);
      } else if (actual.identifier ==
                     "patchy.filters.dust_and_scratches" &&
                 parameter.key == "radius") {
        CHECK(parameter.minimum == 1.0);
        CHECK(parameter.maximum == 100.0);
        CHECK(parameter.practical_minimum == 1.0);
        CHECK(parameter.practical_maximum == 25.0);
      } else if (is_surface_blur_radius) {
        CHECK(parameter.minimum == 1.0);
        CHECK(parameter.maximum == 100.0);
        CHECK(parameter.practical_minimum == 1.0);
        CHECK(parameter.practical_maximum == 25.0);
      } else if (is_unsharp_mask_radius) {
        CHECK(parameter.minimum == 0.1);
        CHECK(parameter.maximum == 1000.0);
        CHECK(parameter.practical_minimum == 0.1);
        CHECK(parameter.practical_maximum == 12.0);
      } else if (actual.identifier == "patchy.filters.motion_blur" &&
                 parameter.key == "angle") {
        CHECK(parameter.minimum == -360.0);
        CHECK(parameter.maximum == 360.0);
        CHECK(parameter.practical_minimum == -180.0);
        CHECK(parameter.practical_maximum == 180.0);
      } else if (actual.identifier == "patchy.filters.motion_blur" &&
                 parameter.key == "distance") {
        CHECK(parameter.minimum == 1.0);
        CHECK(parameter.maximum == 999.0);
        CHECK(parameter.practical_minimum == 1.0);
        CHECK(parameter.practical_maximum == 64.0);
      } else {
        CHECK(!parameter.practical_minimum.has_value());
        CHECK(!parameter.practical_maximum.has_value());
      }
      if (parameter.spatial_scale == patchy::FilterSpatialScale::Pixels) {
        spatial_parameters.insert(actual.identifier + "/" + parameter.key);
        CHECK(parameter.unit == patchy::FilterParameterUnit::Pixels);
      }
    }
    if (center_count != 0U) {
      constexpr std::array center_keys{"center_x", "center_y"};
      constexpr std::array center_names{"filterCenterX", "filterCenterY"};
      for (std::size_t center_index = 0; center_index < center_keys.size();
           ++center_index) {
        const auto &parameter = actual.catalog.parameters[
            wanted.defaults.size() + center_index];
        CHECK(parameter.key == center_keys[center_index]);
        CHECK(parameter.control_object_name == center_names[center_index]);
        CHECK(parameter.kind == patchy::FilterParameterKind::Double);
        CHECK(std::get<double>(parameter.default_value) == 50.0);
        CHECK(parameter.minimum == 0.0);
        CHECK(parameter.maximum == 100.0);
        CHECK(parameter.step == 0.1);
        CHECK(parameter.unit == patchy::FilterParameterUnit::Percent);
        CHECK(parameter.spatial_scale == patchy::FilterSpatialScale::None);
        CHECK(parameter.presentation ==
              expected_presentation(actual.identifier, parameter.key));
        CHECK(std::get<double>(invocation.parameters.at(parameter.key)) ==
              50.0);
      }
    }
  }

  const std::unordered_set<std::string> expected_spatial = {
      "patchy.filters.box_blur/radius",          "patchy.filters.unsharp_mask/radius",
      "patchy.filters.gaussian_blur/radius",     "patchy.filters.motion_blur/distance",
      "patchy.filters.emboss/height",             "patchy.filters.glowing_edges/edge_width",
      "patchy.filters.glowing_edges/smoothness",  "patchy.filters.wave/amplitude",
      "patchy.filters.wave/wavelength",           "patchy.filters.clouds/scale",
      "patchy.filters.pixelate/block_size",        "patchy.filters.color_halftone/cell_size",
      "patchy.filters.high_pass/radius",
      "patchy.filters.median/radius",
      "patchy.filters.dust_and_scratches/radius",
      "patchy.filters.surface_blur/radius",
      "patchy.filters.tilt_shift_blur/blur",
  };
  CHECK(spatial_parameters == expected_spatial);
}

void filter_invocations_normalize_scale_and_reject_bad_data() {
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);

  auto gaussian = registry.default_invocation("patchy.filters.gaussian_blur",
                                              patchy::RgbColor{1, 2, 3}, patchy::RgbColor{4, 5, 6});
  gaussian.parameters.erase("radius");
  gaussian.parameters["future_parameter"] = std::string("ignored");
  const auto normalized = registry.normalize(gaussian);
  CHECK(normalized.has_value());
  CHECK(normalized->parameters.size() == 1);
  CHECK(std::get<std::int64_t>(normalized->parameters.at("radius")) == 2);
  CHECK(normalized->foreground.red == 1);
  CHECK(normalized->background.blue == 6);

  gaussian.parameters["radius"] = std::int64_t{999};
  const auto clamped = registry.normalize(gaussian);
  CHECK(clamped.has_value());
  CHECK(std::get<std::int64_t>(clamped->parameters.at("radius")) == 12);
  gaussian.parameters["radius"] = 2.0;
  CHECK(!registry.supports(gaussian));
  gaussian.parameters["radius"] = std::int64_t{2};
  gaussian.schema_version = 2;
  CHECK(!registry.supports(gaussian));
  gaussian.schema_version = 1;
  gaussian.filter_id = "patchy.filters.missing";
  CHECK(!registry.supports(gaussian));

  auto high_pass = registry.default_invocation("patchy.filters.high_pass");
  CHECK(std::abs(std::get<double>(
                     high_pass.parameters.at("radius")) -
                 10.0) < 0.000001);
  high_pass.parameters["radius"] = 999.0;
  const auto clamped_high_pass = registry.normalize(high_pass);
  CHECK(clamped_high_pass.has_value());
  CHECK(std::abs(std::get<double>(
                     clamped_high_pass->parameters.at("radius")) -
                 999.0) < 0.000001);
  high_pass.parameters["radius"] = 2000.0;
  const auto maximum_high_pass = registry.normalize(high_pass);
  CHECK(maximum_high_pass.has_value());
  CHECK(std::abs(std::get<double>(
                     maximum_high_pass->parameters.at("radius")) -
                 1000.0) < 0.000001);
  high_pass.parameters["radius"] = std::int64_t{4};
  CHECK(!registry.supports(high_pass));

  auto median = registry.default_invocation("patchy.filters.median");
  CHECK(std::abs(std::get<double>(median.parameters.at("radius")) - 1.0) <
        0.000001);
  median.parameters["radius"] = 499.75;
  const auto normalized_median = registry.normalize(median);
  CHECK(normalized_median.has_value());
  CHECK(std::abs(std::get<double>(
                     normalized_median->parameters.at("radius")) -
                 499.75) < 0.000001);
  CHECK(!registry.translation_invariant_support(*normalized_median)
             .has_value());
  median.parameters["radius"] = 900.0;
  const auto maximum_median = registry.normalize(median);
  CHECK(maximum_median.has_value());
  CHECK(std::abs(std::get<double>(
                     maximum_median->parameters.at("radius")) -
                 500.0) < 0.000001);
  median.parameters["radius"] = 0.5;
  const auto minimum_median = registry.normalize(median);
  CHECK(minimum_median.has_value());
  CHECK(std::abs(std::get<double>(
                     minimum_median->parameters.at("radius")) -
                 1.0) < 0.000001);
  median.parameters["radius"] = std::int64_t{2};
  CHECK(!registry.supports(median));

  auto dust =
      registry.default_invocation("patchy.filters.dust_and_scratches");
  CHECK(std::get<std::int64_t>(dust.parameters.at("radius")) == 1);
  CHECK(std::get<std::int64_t>(dust.parameters.at("threshold")) == 0);
  dust.parameters["radius"] = std::int64_t{250};
  dust.parameters["threshold"] = std::int64_t{-20};
  const auto normalized_dust = registry.normalize(dust);
  CHECK(normalized_dust.has_value());
  CHECK(std::get<std::int64_t>(
            normalized_dust->parameters.at("radius")) == 100);
  CHECK(std::get<std::int64_t>(
            normalized_dust->parameters.at("threshold")) == 0);
  CHECK(!registry.translation_invariant_support(*normalized_dust)
             .has_value());
  dust.parameters["radius"] = std::int64_t{20};
  dust.parameters["threshold"] = std::int64_t{23};
  const auto scaled_dust = registry.scale(dust, 0.25);
  CHECK(scaled_dust.has_value());
  CHECK(std::get<std::int64_t>(scaled_dust->parameters.at("radius")) == 5);
  CHECK(std::get<std::int64_t>(scaled_dust->parameters.at("threshold")) ==
        23);
  dust.parameters["radius"] = 2.0;
  CHECK(!registry.supports(dust));
  dust = registry.default_invocation("patchy.filters.dust_and_scratches");
  dust.parameters["threshold"] = 4.0;
  CHECK(!registry.supports(dust));

  auto surface = registry.default_invocation("patchy.filters.surface_blur");
  CHECK(std::abs(std::get<double>(surface.parameters.at("radius")) - 5.0) <
        0.000001);
  CHECK(std::get<std::int64_t>(surface.parameters.at("threshold")) == 15);
  surface.parameters["radius"] = 250.0;
  surface.parameters["threshold"] = std::int64_t{1};
  const auto normalized_surface = registry.normalize(surface);
  CHECK(normalized_surface.has_value());
  CHECK(std::abs(std::get<double>(
                     normalized_surface->parameters.at("radius")) -
                 100.0) < 0.000001);
  CHECK(std::get<std::int64_t>(
            normalized_surface->parameters.at("threshold")) == 2);
  CHECK(!registry.translation_invariant_support(*normalized_surface)
             .has_value());
  surface.parameters["radius"] = 10.0;
  surface.parameters["threshold"] = std::int64_t{31};
  const auto scaled_surface = registry.scale(surface, 0.25);
  CHECK(scaled_surface.has_value());
  CHECK(std::abs(std::get<double>(
                     scaled_surface->parameters.at("radius")) -
                 2.5) < 0.000001);
  CHECK(std::get<std::int64_t>(
            scaled_surface->parameters.at("threshold")) == 31);
  surface.parameters["radius"] = std::int64_t{2};
  CHECK(!registry.supports(surface));
  surface = registry.default_invocation("patchy.filters.surface_blur");
  surface.parameters["threshold"] = 31.0;
  CHECK(!registry.supports(surface));

  auto tilt =
      registry.default_invocation("patchy.filters.tilt_shift_blur");
  CHECK(std::get<double>(tilt.parameters.at("blur")) == 15.0);
  CHECK(std::get<double>(tilt.parameters.at("center_x")) == 50.0);
  CHECK(std::get<double>(tilt.parameters.at("center_y")) == 50.0);
  CHECK(std::get<std::int64_t>(tilt.parameters.at("angle")) == 0);
  CHECK(std::get<double>(tilt.parameters.at("focus_half_width")) == 10.0);
  CHECK(std::get<double>(tilt.parameters.at("transition_width")) == 20.0);
  tilt.parameters["blur"] = 900.0;
  tilt.parameters["center_x"] = -5.0;
  tilt.parameters["center_y"] = 105.0;
  tilt.parameters["angle"] = std::int64_t{240};
  tilt.parameters["focus_half_width"] = -1.0;
  tilt.parameters["transition_width"] = 120.0;
  const auto normalized_tilt = registry.normalize(tilt);
  CHECK(normalized_tilt.has_value());
  CHECK(std::get<double>(normalized_tilt->parameters.at("blur")) == 500.0);
  CHECK(std::get<double>(normalized_tilt->parameters.at("center_x")) == 0.0);
  CHECK(std::get<double>(normalized_tilt->parameters.at("center_y")) ==
        100.0);
  CHECK(std::get<std::int64_t>(normalized_tilt->parameters.at("angle")) ==
        180);
  CHECK(std::get<double>(
            normalized_tilt->parameters.at("focus_half_width")) == 0.0);
  CHECK(std::get<double>(
            normalized_tilt->parameters.at("transition_width")) == 100.0);
  CHECK(!registry.translation_invariant_support(*normalized_tilt)
             .has_value());
  tilt = registry.default_invocation("patchy.filters.tilt_shift_blur");
  tilt.parameters["blur"] = 40.0;
  tilt.parameters["center_x"] = 25.5;
  tilt.parameters["center_y"] = 75.5;
  tilt.parameters["angle"] = std::int64_t{-37};
  tilt.parameters["focus_half_width"] = 12.5;
  tilt.parameters["transition_width"] = 32.5;
  const auto scaled_tilt = registry.scale(tilt, 0.25);
  CHECK(scaled_tilt.has_value());
  CHECK(std::get<double>(scaled_tilt->parameters.at("blur")) == 10.0);
  CHECK(std::get<double>(scaled_tilt->parameters.at("center_x")) == 25.5);
  CHECK(std::get<double>(scaled_tilt->parameters.at("center_y")) == 75.5);
  CHECK(std::get<std::int64_t>(scaled_tilt->parameters.at("angle")) == -37);
  CHECK(std::get<double>(
            scaled_tilt->parameters.at("focus_half_width")) == 12.5);
  CHECK(std::get<double>(
            scaled_tilt->parameters.at("transition_width")) == 32.5);
  tilt.parameters["blur"] = std::int64_t{40};
  CHECK(!registry.supports(tilt));
  tilt = registry.default_invocation("patchy.filters.tilt_shift_blur");
  tilt.parameters["angle"] = 10.0;
  CHECK(!registry.supports(tilt));

  auto wave = registry.default_invocation("patchy.filters.wave");
  const auto scaled_wave = registry.scale(wave, 0.25);
  CHECK(scaled_wave.has_value());
  CHECK(std::get<std::int64_t>(scaled_wave->parameters.at("amplitude")) == 3);
  CHECK(std::get<std::int64_t>(scaled_wave->parameters.at("wavelength")) == 12);
  CHECK(std::get<std::int64_t>(scaled_wave->parameters.at("phase")) == 0);
  auto twirl = registry.default_invocation("patchy.filters.twirl");
  const auto scaled_twirl = registry.scale(twirl, 0.25);
  CHECK(scaled_twirl.has_value());
  CHECK(std::get<std::int64_t>(scaled_twirl->parameters.at("radius")) == 100);
  CHECK(std::get<double>(scaled_twirl->parameters.at("center_x")) == 50.0);
  CHECK(std::get<double>(scaled_twirl->parameters.at("center_y")) == 50.0);
  CHECK(!registry.scale(twirl, 0.0).has_value());

  auto radial = registry.default_invocation("patchy.filters.radial_blur");
  radial.parameters["center_x"] = 17.5;
  radial.parameters["center_y"] = 82.5;
  const auto scaled_radial = registry.scale(radial, 0.125);
  CHECK(scaled_radial.has_value());
  CHECK(std::get<double>(scaled_radial->parameters.at("center_x")) == 17.5);
  CHECK(std::get<double>(scaled_radial->parameters.at("center_y")) == 82.5);
  radial.parameters.erase("center_x");
  const auto normalized_radial = registry.normalize(radial);
  CHECK(normalized_radial.has_value());
  CHECK(std::get<double>(normalized_radial->parameters.at("center_x")) == 50.0);
  radial.parameters["center_x"] = std::int64_t{25};
  CHECK(!registry.supports(radial));

  patchy::FilterCatalogMetadata custom_catalog;
  custom_catalog.schema_version = 1;
  custom_catalog.parameters = {
      patchy::FilterParameterDefinition{"real", "Real", "testReal", patchy::FilterParameterKind::Double,
                                        1.5, 0.0, 2.0, 0.1},
      patchy::FilterParameterDefinition{"flag", "Flag", "testFlag", patchy::FilterParameterKind::Boolean,
                                        true},
      patchy::FilterParameterDefinition{
          "option", "Option", "testOption", patchy::FilterParameterKind::Option, std::string("one"),
          std::nullopt, std::nullopt, std::nullopt, patchy::FilterParameterUnit::None,
          patchy::FilterSpatialScale::None,
          {patchy::FilterParameterOption{"one", "One"}, patchy::FilterParameterOption{"two", "Two"}}},
  };
  custom_catalog.execute = [](const patchy::FilterRegistry&, const patchy::FilterInvocation&,
                              patchy::PixelBuffer&, const patchy::FilterProgress*) {};
  registry.register_filter(
      {"test.filters.typed", "Typed", [](patchy::PixelBuffer&) {}, std::move(custom_catalog)});
  auto typed = registry.default_invocation("test.filters.typed");
  CHECK(registry.supports(typed));
  typed.parameters["real"] = 5.0;
  const auto typed_clamped = registry.normalize(typed);
  CHECK(typed_clamped.has_value());
  CHECK(std::get<double>(typed_clamped->parameters.at("real")) == 2.0);
  typed.parameters["real"] = std::int64_t{1};
  CHECK(!registry.supports(typed));
  typed = registry.default_invocation("test.filters.typed");
  typed.parameters["option"] = std::string("missing");
  CHECK(!registry.supports(typed));
  typed = registry.default_invocation("test.filters.typed");
  typed.parameters["flag"] = std::string("true");
  CHECK(!registry.supports(typed));
}

void filter_centers_preserve_defaults_move_effects_and_survive_padding() {
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);

  patchy::PixelBuffer source(17, 13, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < source.height(); ++y) {
    for (std::int32_t x = 0; x < source.width(); ++x) {
      auto *pixel = source.pixel(x, y);
      pixel[0] = static_cast<std::uint8_t>((x * 31 + y * 7) % 256);
      pixel[1] = static_cast<std::uint8_t>((x * 11 + y * 29) % 256);
      pixel[2] = static_cast<std::uint8_t>((x * 19 + y * 13) % 256);
      pixel[3] = 255;
    }
  }
  const auto equal_pixels = [](const patchy::PixelBuffer &left,
                               const patchy::PixelBuffer &right) {
    return left.format() == right.format() && left.width() == right.width() &&
           left.height() == right.height() &&
           std::equal(left.data().begin(), left.data().end(),
                      right.data().begin());
  };

  // These three named defaults historically matched the legacy wrapper. The
  // newly appended 50/50 center parameters must not move that output.
  for (const auto *identifier : {"patchy.filters.radial_blur",
                                 "patchy.filters.twirl",
                                 "patchy.filters.pinch_bloat"}) {
    auto legacy = source;
    registry.apply(identifier, legacy);
    auto named = source;
    registry.apply(registry.default_invocation(identifier), named);
    CHECK(equal_pixels(legacy, named));
  }

  auto vignette_source = solid_rgb(9, 7, 255, 255, 255);
  registry.apply(registry.default_invocation("patchy.filters.vignette"),
                 vignette_source);
  CHECK(vignette_source.pixel(4, 3)[0] == 255);
  CHECK(vignette_source.pixel(0, 0)[0] == 115);

  for (const auto *identifier : {"patchy.filters.radial_blur",
                                 "patchy.filters.twirl",
                                 "patchy.filters.pinch_bloat",
                                 "patchy.filters.vignette"}) {
    auto centered = source;
    auto centered_invocation = registry.default_invocation(identifier);
    registry.apply(centered_invocation, centered);

    auto explicit_center = source;
    centered_invocation.parameters["center_x"] = 50.0;
    centered_invocation.parameters["center_y"] = 50.0;
    registry.apply(centered_invocation, explicit_center);
    CHECK(equal_pixels(centered, explicit_center));

    auto moved = source;
    auto moved_invocation = registry.default_invocation(identifier);
    moved_invocation.parameters["center_x"] = 25.0;
    moved_invocation.parameters["center_y"] = 75.0;
    registry.apply(moved_invocation, moved);
    CHECK(!equal_pixels(centered, moved));
    for (std::uint16_t channel = 0; channel < 4; ++channel) {
      CHECK(moved.pixel(4, 9)[channel] == source.pixel(4, 9)[channel]);
    }
  }

  // Radial Blur grows an RGBA buffer before execution. The center percentage
  // is relative to the unpadded source and must be remapped for that temporary
  // buffer, otherwise this stationary center pixel loses alpha after growth.
  patchy::PixelBuffer point_source(11, 11, patchy::PixelFormat::rgba8());
  point_source.clear(0);
  for (std::uint16_t channel = 0; channel < 4; ++channel) {
    point_source.pixel(2, 8)[channel] = 255;
  }
  auto off_center_radial =
      registry.default_invocation("patchy.filters.radial_blur");
  off_center_radial.parameters["center_x"] = 20.0;
  off_center_radial.parameters["center_y"] = 80.0;
  CHECK(registry.output_margin(off_center_radial, 11, 11) > 0);
  auto edge_center_radial =
      registry.default_invocation("patchy.filters.radial_blur");
  edge_center_radial.parameters["center_x"] = 0.0;
  edge_center_radial.parameters["center_y"] = 50.0;
  // The far corners of a 1024px layer sweep well beyond the old 256px safety
  // cap. Output growth must follow the actual sampled geometry so the halo is
  // not clipped before rendering.
  CHECK(registry.output_margin(edge_center_radial, 1024, 1024) > 600);
  const patchy::Rect point_bounds{100, 200, 11, 11};
  const auto confined =
      registry.render(off_center_radial, point_source, point_bounds, false);
  const auto expanded =
      registry.render(off_center_radial, point_source, point_bounds, true);
  const auto expanded_center_x = point_bounds.x + 2 - expanded.bounds.x;
  const auto expanded_center_y = point_bounds.y + 8 - expanded.bounds.y;
  CHECK(expanded_center_x >= 0 &&
        expanded_center_x < expanded.pixels.width());
  CHECK(expanded_center_y >= 0 &&
        expanded_center_y < expanded.pixels.height());
  for (std::uint16_t channel = 0; channel < 4; ++channel) {
    CHECK(expanded.pixels.pixel(expanded_center_x,
                                expanded_center_y)[channel] ==
          confined.pixels.pixel(2, 8)[channel]);
  }
}

void filter_named_engine_recipes_bounds_colors_and_legacy_stay_distinct() {
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);

  auto posterize_source = solid_rgb(1, 1, 50, 50, 50);
  auto legacy_posterize = posterize_source;
  registry.apply("patchy.filters.posterize", legacy_posterize);
  CHECK(legacy_posterize.pixel(0, 0)[0] == 0);
  auto named_posterize = posterize_source;
  registry.apply(registry.default_invocation("patchy.filters.posterize"), named_posterize);
  CHECK(named_posterize.pixel(0, 0)[0] == 85);

  patchy::PixelBuffer blur_source(5, 5, patchy::PixelFormat::rgb8());
  blur_source.pixel(2, 2)[0] = 255;
  blur_source.pixel(2, 2)[1] = 255;
  blur_source.pixel(2, 2)[2] = 255;
  auto legacy_gaussian = blur_source;
  registry.apply("patchy.filters.gaussian_blur", legacy_gaussian);
  CHECK(legacy_gaussian.pixel(2, 2)[0] == 36);
  auto named_gaussian = blur_source;
  registry.apply(registry.default_invocation("patchy.filters.gaussian_blur"), named_gaussian);
  CHECK(named_gaussian.pixel(2, 2)[0] == 28);

  auto clouds = registry.default_invocation("patchy.filters.clouds", patchy::RgbColor{240, 20, 10},
                                            patchy::RgbColor{5, 15, 230});
  auto cloud_pixels = solid_rgba(32, 24, 0, 0, 0, 0);
  registry.apply(clouds, cloud_pixels);
  bool saw_red_bias = false;
  bool saw_blue_bias = false;
  for (std::int32_t y = 0; y < cloud_pixels.height(); ++y) {
    for (std::int32_t x = 0; x < cloud_pixels.width(); ++x) {
      const auto* px = cloud_pixels.pixel(x, y);
      CHECK(px[3] == 255);
      saw_red_bias = saw_red_bias || px[0] > px[2];
      saw_blue_bias = saw_blue_bias || px[2] > px[0];
    }
  }
  CHECK(saw_red_bias);
  CHECK(saw_blue_bias);

  auto rgba_source = solid_rgba(24, 24, 220, 28, 24, 255);
  auto box = registry.default_invocation("patchy.filters.box_blur");
  box.parameters["radius"] = std::int64_t{4};
  CHECK(registry.output_margin(box, 24, 24) == 4);
  CHECK(registry.translation_invariant_support(box) == 4);
  const patchy::Rect source_bounds{10, 20, 24, 24};
  const auto grown = registry.render(box, rgba_source, source_bounds);
  CHECK(grown.bounds.x == 6);
  CHECK(grown.bounds.y == 16);
  CHECK(grown.bounds.width == 32);
  CHECK(grown.bounds.height == 32);
  CHECK(grown.pixels.width() == 32);
  const auto confined = registry.render(box, rgba_source, source_bounds, false);
  CHECK(confined.bounds.x == source_bounds.x);
  CHECK(confined.bounds.width == source_bounds.width);
  CHECK(confined.pixels.width() == rgba_source.width());
  auto motion = registry.default_invocation("patchy.filters.motion_blur");
  motion.parameters["distance"] = std::int64_t{12};
  CHECK(registry.translation_invariant_support(motion) == 13);
  CHECK(!registry.translation_invariant_support(
             registry.default_invocation("patchy.filters.clouds"))
             .has_value());

  const auto recipe_source = solid_rgb(4, 3, 20, 40, 80);
  const auto invert = registry.default_invocation("patchy.filters.invert");
  patchy::FilterRecipe twice{{patchy::FilterRecipeEntry{invert}, patchy::FilterRecipeEntry{invert}}};
  auto twice_pixels = recipe_source;
  registry.apply(twice, twice_pixels);
  CHECK(std::equal(twice_pixels.data().begin(), twice_pixels.data().end(), recipe_source.data().begin()));
  patchy::FilterRecipe disabled{{patchy::FilterRecipeEntry{invert, false}}};
  auto disabled_pixels = recipe_source;
  registry.apply(disabled, disabled_pixels);
  CHECK(std::equal(disabled_pixels.data().begin(), disabled_pixels.data().end(), recipe_source.data().begin()));
  patchy::FilterRecipe transparent{{patchy::FilterRecipeEntry{invert, true, 0.0}}};
  auto transparent_pixels = recipe_source;
  registry.apply(transparent, transparent_pixels);
  CHECK(std::equal(transparent_pixels.data().begin(), transparent_pixels.data().end(),
                   recipe_source.data().begin()));
  patchy::FilterRecipe multiply{{patchy::FilterRecipeEntry{invert, true, 0.5,
                                                           patchy::BlendMode::Multiply}}};
  auto multiply_pixels = recipe_source;
  registry.apply(multiply, multiply_pixels);
  CHECK(!std::equal(multiply_pixels.data().begin(), multiply_pixels.data().end(),
                    recipe_source.data().begin()));
  patchy::FilterRecipe pass_through{{patchy::FilterRecipeEntry{invert, true, 1.0,
                                                               patchy::BlendMode::PassThrough}}};
  CHECK(!registry.supports(pass_through));
  patchy::FilterRecipe bad_opacity{{patchy::FilterRecipeEntry{invert, true, 1.5}}};
  CHECK(!registry.supports(bad_opacity));

  bool saw_blur = false;
  patchy::FilterProgress progress{[&](int completed, int, patchy::FilterProgressStage stage) {
    saw_blur = saw_blur || stage == patchy::FilterProgressStage::Blurring;
    return completed < 2;
  }};
  bool cancelled = false;
  try {
    auto pixels = solid_rgb(32, 32, 80, 120, 160);
    registry.apply(registry.default_invocation("patchy.filters.gaussian_blur"), pixels, &progress);
  } catch (const patchy::FilterCancelled&) {
    cancelled = true;
  }
  CHECK(cancelled);
  CHECK(saw_blur);
}

void filter_recipe_opacity_interpolates_rgba_results() {
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  const auto invert = registry.default_invocation("patchy.filters.invert");

  // A replacement filter that preserves alpha must not inflate that alpha when
  // its recipe opacity is below 100%.
  auto equal_alpha = solid_rgba(1, 1, 20, 40, 80, 128);
  const patchy::FilterRecipe half_invert{
      {patchy::FilterRecipeEntry{invert, true, 0.5, patchy::BlendMode::Normal}}};
  registry.apply(half_invert, equal_alpha);
  const auto* equal_px = equal_alpha.pixel(0, 0);
  CHECK(equal_px[0] == 128);
  CHECK(equal_px[1] == 128);
  CHECK(equal_px[2] == 128);
  CHECK(equal_px[3] == 128);

  auto box = registry.default_invocation("patchy.filters.box_blur");
  box.parameters["radius"] = std::int64_t{2};
  const patchy::Rect bounds{10, 20, 7, 7};
  const auto opaque_red = solid_rgba(7, 7, 220, 28, 24, 255);
  const auto full_blur = registry.render(box, opaque_red, bounds);
  const patchy::FilterRecipe half_blur{
      {patchy::FilterRecipeEntry{box, true, 0.5, patchy::BlendMode::Normal}}};
  const auto faded_blur = registry.render(half_blur, opaque_red, bounds);
  CHECK(faded_blur.bounds.x == full_blur.bounds.x);
  CHECK(faded_blur.bounds.y == full_blur.bounds.y);
  CHECK(faded_blur.pixels.width() == full_blur.pixels.width());
  const auto halo_y = full_blur.pixels.height() / 2;
  const auto* full_halo = full_blur.pixels.pixel(0, halo_y);
  const auto* faded_halo = faded_blur.pixels.pixel(0, halo_y);
  CHECK(full_halo[3] > 0 && full_halo[3] < 255);
  constexpr std::uint64_t kOpacityScale = 65535U;
  constexpr std::uint64_t kHalfWeight = 32768U;
  const auto expected_halo_alpha = static_cast<std::uint8_t>(
      (static_cast<std::uint64_t>(full_halo[3]) * kHalfWeight + kOpacityScale / 2U) /
      kOpacityScale);
  CHECK(faded_halo[3] == expected_halo_alpha);
  CHECK(faded_halo[0] == full_halo[0]);
  CHECK(faded_halo[1] == full_halo[1]);
  CHECK(faded_halo[2] == full_halo[2]);

  // A non-Normal entry still keeps the filtered color on source-only expanded
  // pixels instead of blending that halo against transparent black.
  const patchy::FilterRecipe multiply_blur{
      {patchy::FilterRecipeEntry{box, true, 0.5, patchy::BlendMode::Multiply}}};
  const auto multiplied_blur = registry.render(multiply_blur, opaque_red, bounds);
  const auto* multiplied_halo = multiplied_blur.pixels.pixel(0, halo_y);
  CHECK(multiplied_halo[3] == expected_halo_alpha);
  CHECK(multiplied_halo[0] == full_halo[0]);
  CHECK(multiplied_halo[1] == full_halo[1]);
  CHECK(multiplied_halo[2] == full_halo[2]);

  // When the filter itself changes coverage, opacity interpolates toward that
  // replacement alpha rather than compositing it over the old coverage.
  patchy::PixelBuffer alpha_source(3, 1, patchy::PixelFormat::rgba8());
  alpha_source.clear(0);
  auto* center = alpha_source.pixel(1, 0);
  center[0] = 200;
  center[1] = 40;
  center[2] = 20;
  center[3] = 255;
  auto radius_one = registry.default_invocation("patchy.filters.box_blur");
  auto filtered_alpha = alpha_source;
  registry.apply(radius_one, filtered_alpha);
  CHECK(filtered_alpha.pixel(1, 0)[3] == 85);
  const patchy::FilterRecipe half_alpha{
      {patchy::FilterRecipeEntry{radius_one, true, 0.5, patchy::BlendMode::Normal}}};
  auto interpolated_alpha = alpha_source;
  registry.apply(half_alpha, interpolated_alpha);
  const auto* interpolated_center = interpolated_alpha.pixel(1, 0);
  CHECK(interpolated_center[0] == 200);
  CHECK(interpolated_center[1] == 40);
  CHECK(interpolated_center[2] == 20);
  CHECK(interpolated_center[3] == 170);
}

void filter_recipe_native_smart_filter_mapping_is_all_or_nothing() {
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  auto first = registry.default_invocation(
      "patchy.filters.gaussian_blur", patchy::RgbColor{1, 2, 3},
      patchy::RgbColor{4, 5, 6});
  first.parameters["radius"] = std::int64_t{3};
  auto second = first;
  second.parameters["radius"] = std::int64_t{9};
  auto high_pass =
      registry.default_invocation("patchy.filters.high_pass");
  high_pass.parameters["radius"] = 4.25;
  auto median = registry.default_invocation("patchy.filters.median");
  median.parameters["radius"] = 7.5;
  auto dust =
      registry.default_invocation("patchy.filters.dust_and_scratches");
  dust.parameters["radius"] = std::int64_t{7};
  dust.parameters["threshold"] = std::int64_t{23};
  auto surface = registry.default_invocation("patchy.filters.surface_blur");
  surface.parameters["radius"] = 9.25;
  surface.parameters["threshold"] = std::int64_t{31};
  auto unsharp = registry.default_invocation("patchy.filters.unsharp_mask");
  unsharp.parameters["amount"] = std::int64_t{225};
  unsharp.parameters["radius"] = 4.75;
  unsharp.parameters["threshold"] = std::int64_t{11};
  auto motion = registry.default_invocation("patchy.filters.motion_blur");
  motion.parameters["angle"] = std::int64_t{-61};
  motion.parameters["distance"] = std::int64_t{27};
  const patchy::FilterRecipe recipe{{
      patchy::FilterRecipeEntry{first, true, 0.37,
                                patchy::BlendMode::Multiply},
      patchy::FilterRecipeEntry{high_pass, true, 0.75,
                                patchy::BlendMode::Overlay},
      patchy::FilterRecipeEntry{median, true, 0.5,
                                patchy::BlendMode::SoftLight},
      patchy::FilterRecipeEntry{dust, true, 0.25, patchy::BlendMode::Screen},
      patchy::FilterRecipeEntry{surface, true, 0.8, patchy::BlendMode::Color},
      patchy::FilterRecipeEntry{unsharp, true, 0.6,
                                patchy::BlendMode::Luminosity},
      patchy::FilterRecipeEntry{motion, true, 0.4, patchy::BlendMode::Lighten},
      patchy::FilterRecipeEntry{second, false, 1.0, patchy::BlendMode::Normal},
  }};
  const auto mapped =
      patchy::smart_filter_entries_from_recipe(recipe, registry);
  CHECK(mapped.has_value() && mapped->size() == 8U);
  CHECK((*mapped)[0].kind == patchy::SmartFilterKind::GaussianBlur);
  CHECK((*mapped)[0].enabled);
  CHECK(std::abs((*mapped)[0].opacity - 0.37) < 0.000001);
  CHECK((*mapped)[0].blend_mode == patchy::BlendMode::Multiply);
  CHECK((*mapped)[0].foreground.red == 1U);
  CHECK((*mapped)[0].background.blue == 6U);
  CHECK(std::abs(std::get<patchy::GaussianBlurSmartFilter>(
                     (*mapped)[0].parameters)
                     .radius_pixels -
                 3.0) < 0.000001);
  CHECK((*mapped)[1].kind == patchy::SmartFilterKind::HighPass);
  CHECK((*mapped)[1].enabled);
  CHECK(std::abs((*mapped)[1].opacity - 0.75) < 0.000001);
  CHECK((*mapped)[1].blend_mode == patchy::BlendMode::Overlay);
  CHECK(std::abs(std::get<patchy::HighPassSmartFilter>(
                     (*mapped)[1].parameters)
                     .radius_pixels -
                 4.25) < 0.000001);
  CHECK((*mapped)[2].kind == patchy::SmartFilterKind::Median);
  CHECK((*mapped)[2].enabled);
  CHECK(std::abs((*mapped)[2].opacity - 0.5) < 0.000001);
  CHECK((*mapped)[2].blend_mode == patchy::BlendMode::SoftLight);
  CHECK(std::abs(std::get<patchy::MedianSmartFilter>(
                     (*mapped)[2].parameters)
                     .radius_pixels -
                 7.5) < 0.000001);
  CHECK((*mapped)[3].kind == patchy::SmartFilterKind::DustAndScratches);
  CHECK((*mapped)[3].native_name == "Dust && Scratches...");
  CHECK((*mapped)[3].native_class_id == "DstS");
  CHECK((*mapped)[3].native_filter_id == 0x44737453U);
  CHECK((*mapped)[3].enabled);
  CHECK(std::abs((*mapped)[3].opacity - 0.25) < 0.000001);
  CHECK((*mapped)[3].blend_mode == patchy::BlendMode::Screen);
  CHECK(std::get<patchy::DustAndScratchesSmartFilter>(
            (*mapped)[3].parameters)
            .radius_pixels == 7);
  CHECK(std::get<patchy::DustAndScratchesSmartFilter>(
            (*mapped)[3].parameters)
            .threshold == 23);
  CHECK((*mapped)[4].kind == patchy::SmartFilterKind::SurfaceBlur);
  CHECK((*mapped)[4].native_name == "Surface Blur...");
  CHECK((*mapped)[4].native_class_id == "surfaceBlur");
  CHECK((*mapped)[4].native_filter_id == 854U);
  CHECK((*mapped)[4].enabled);
  CHECK(std::abs((*mapped)[4].opacity - 0.8) < 0.000001);
  CHECK((*mapped)[4].blend_mode == patchy::BlendMode::Color);
  CHECK(std::abs(std::get<patchy::SurfaceBlurSmartFilter>(
                     (*mapped)[4].parameters)
                     .radius_pixels -
                 9.25) < 0.000001);
  CHECK(std::get<patchy::SurfaceBlurSmartFilter>(
            (*mapped)[4].parameters)
            .threshold == 31);
  CHECK((*mapped)[5].kind == patchy::SmartFilterKind::UnsharpMask);
  CHECK((*mapped)[5].native_name == "Unsharp Mask...");
  CHECK((*mapped)[5].native_class_id == "UnsM");
  CHECK((*mapped)[5].native_filter_id == 0x556e734dU);
  CHECK(std::abs((*mapped)[5].opacity - 0.6) < 0.000001);
  CHECK((*mapped)[5].blend_mode == patchy::BlendMode::Luminosity);
  const auto &mapped_unsharp =
      std::get<patchy::UnsharpMaskSmartFilter>((*mapped)[5].parameters);
  CHECK(std::abs(mapped_unsharp.amount_percent - 225.0) < 0.000001);
  CHECK(std::abs(mapped_unsharp.radius_pixels - 4.75) < 0.000001);
  CHECK(mapped_unsharp.threshold == 11);
  CHECK((*mapped)[6].kind == patchy::SmartFilterKind::MotionBlur);
  CHECK((*mapped)[6].native_name == "Motion Blur...");
  CHECK((*mapped)[6].native_class_id == "MtnB");
  CHECK((*mapped)[6].native_filter_id == 0x4d746e42U);
  CHECK(std::abs((*mapped)[6].opacity - 0.4) < 0.000001);
  CHECK((*mapped)[6].blend_mode == patchy::BlendMode::Lighten);
  const auto &mapped_motion =
      std::get<patchy::MotionBlurSmartFilter>((*mapped)[6].parameters);
  CHECK(mapped_motion.angle_degrees == -61);
  CHECK(mapped_motion.distance_pixels == 27);
  CHECK(!(*mapped)[7].enabled);
  CHECK(std::abs(
            std::get<patchy::GaussianBlurSmartFilter>((*mapped)[7].parameters)
                .radius_pixels -
            9.0) < 0.000001);

  auto mixed = recipe;
  mixed.entries.push_back(patchy::FilterRecipeEntry{
      registry.default_invocation("patchy.filters.sharpen"), false});
  CHECK(!patchy::smart_filter_entries_from_recipe(mixed, registry)
             .has_value());
  auto wrong_schema = recipe;
  wrong_schema.entries.front().invocation.schema_version = 2U;
  CHECK(!patchy::smart_filter_entries_from_recipe(wrong_schema, registry)
             .has_value());
  auto malformed_dust = recipe;
  malformed_dust.entries[3].invocation.parameters["threshold"] = 23.0;
  CHECK(!patchy::smart_filter_entries_from_recipe(malformed_dust, registry)
             .has_value());
  auto malformed_surface = recipe;
  malformed_surface.entries[4].invocation.parameters["threshold"] = 31.0;
  CHECK(!patchy::smart_filter_entries_from_recipe(malformed_surface, registry)
             .has_value());
  auto malformed_unsharp = recipe;
  malformed_unsharp.entries[5].invocation.parameters["amount"] = 225.0;
  CHECK(!patchy::smart_filter_entries_from_recipe(malformed_unsharp, registry)
             .has_value());
  auto malformed_motion = recipe;
  malformed_motion.entries[6].invocation.parameters["distance"] = 27.0;
  CHECK(!patchy::smart_filter_entries_from_recipe(malformed_motion, registry)
             .has_value());
  CHECK(!patchy::smart_filter_entries_from_recipe({}, registry).has_value());
}

void filter_recipe_scales_supports_validates_and_skips_zero_opacity() {
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);

  auto box = registry.default_invocation("patchy.filters.box_blur",
                                         patchy::RgbColor{1, 2, 3},
                                         patchy::RgbColor{4, 5, 6});
  box.parameters["radius"] = std::int64_t{4};
  auto twirl = registry.default_invocation("patchy.filters.twirl",
                                           patchy::RgbColor{7, 8, 9},
                                           patchy::RgbColor{10, 11, 12});
  twirl.parameters["angle"] = std::int64_t{270};
  twirl.parameters["radius"] = std::int64_t{80};
  twirl.parameters["center_x"] = 25.5;
  twirl.parameters["center_y"] = 75.5;
  const patchy::FilterRecipe recipe{{
      patchy::FilterRecipeEntry{box, false, 0.25,
                                patchy::BlendMode::Multiply},
      patchy::FilterRecipeEntry{twirl, true, 0.75,
                                patchy::BlendMode::Screen},
  }};
  const auto scaled = registry.scale(recipe, 0.5);
  CHECK(scaled.has_value());
  CHECK(scaled->entries.size() == 2);
  CHECK(!scaled->entries[0].enabled);
  CHECK(scaled->entries[0].opacity == 0.25);
  CHECK(scaled->entries[0].blend_mode == patchy::BlendMode::Multiply);
  CHECK(std::get<std::int64_t>(
            scaled->entries[0].invocation.parameters.at("radius")) == 2);
  CHECK(scaled->entries[0].invocation.foreground.red == 1);
  CHECK(scaled->entries[0].invocation.background.blue == 6);
  CHECK(scaled->entries[1].enabled);
  CHECK(scaled->entries[1].opacity == 0.75);
  CHECK(scaled->entries[1].blend_mode == patchy::BlendMode::Screen);
  CHECK(std::get<std::int64_t>(
            scaled->entries[1].invocation.parameters.at("angle")) == 270);
  CHECK(std::get<std::int64_t>(
            scaled->entries[1].invocation.parameters.at("radius")) == 80);
  CHECK(std::get<double>(
            scaled->entries[1].invocation.parameters.at("center_x")) == 25.5);
  CHECK(std::get<double>(
            scaled->entries[1].invocation.parameters.at("center_y")) == 75.5);
  CHECK(scaled->entries[1].invocation.foreground.green == 8);
  CHECK(scaled->entries[1].invocation.background.red == 10);
  CHECK(!registry.scale(recipe, 0.0).has_value());

  auto gaussian = registry.default_invocation("patchy.filters.gaussian_blur");
  gaussian.parameters["radius"] = std::int64_t{3};
  const auto sharpen =
      registry.default_invocation("patchy.filters.sharpen");
  auto vignette = registry.default_invocation("patchy.filters.vignette");
  patchy::FilterRecipe supported{{
      patchy::FilterRecipeEntry{box},
      patchy::FilterRecipeEntry{gaussian},
      patchy::FilterRecipeEntry{sharpen},
      patchy::FilterRecipeEntry{vignette, false},
  }};
  CHECK(registry.translation_invariant_support(supported) == 8);
  supported.entries.back().enabled = true;
  supported.entries.back().opacity = 0.0;
  CHECK(registry.translation_invariant_support(supported) == 8);
  supported.entries.back().opacity = 1.0;
  CHECK(!registry.translation_invariant_support(supported).has_value());
  CHECK(registry.translation_invariant_support(patchy::FilterRecipe{}) == 0);

  patchy::FilterCatalogMetadata maximum_support_metadata;
  maximum_support_metadata.execute =
      [](const patchy::FilterRegistry&, const patchy::FilterInvocation&,
         patchy::PixelBuffer&, const patchy::FilterProgress*) {};
  maximum_support_metadata.translation_support =
      [](const patchy::FilterInvocation&) -> std::optional<int> {
    return std::numeric_limits<int>::max();
  };
  registry.register_filter(
      {"test.filters.maximum_support", "Maximum Support",
       [](patchy::PixelBuffer&) {}, std::move(maximum_support_metadata)});
  const auto maximum_support =
      registry.default_invocation("test.filters.maximum_support");
  const patchy::FilterRecipe overflowing{{
      patchy::FilterRecipeEntry{maximum_support},
      patchy::FilterRecipeEntry{maximum_support},
  }};
  CHECK(!registry.translation_invariant_support(overflowing).has_value());

  for (const auto opacity :
       {-0.01, 1.01, std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::quiet_NaN()}) {
    const patchy::FilterRecipe invalid{
        {patchy::FilterRecipeEntry{box, true, opacity}}};
    CHECK(!registry.supports(invalid));
    CHECK(!registry.scale(invalid, 0.5).has_value());
    CHECK(!registry.translation_invariant_support(invalid).has_value());
  }
  const patchy::FilterRecipe pass_through{{patchy::FilterRecipeEntry{
      box, true, 1.0, patchy::BlendMode::PassThrough}}};
  CHECK(!registry.supports(pass_through));
  const patchy::FilterRecipe invalid_mode{{patchy::FilterRecipeEntry{
      box, true, 1.0, static_cast<patchy::BlendMode>(999)}}};
  CHECK(!registry.supports(invalid_mode));
  auto missing = box;
  missing.filter_id = "test.filters.missing";
  const patchy::FilterRecipe disabled_missing{
      {patchy::FilterRecipeEntry{missing, false}}};
  CHECK(!registry.supports(disabled_missing));
  CHECK(!registry.scale(disabled_missing, 0.5).has_value());

  int executions = 0;
  patchy::FilterCatalogMetadata counted_metadata;
  counted_metadata.execute =
      [&executions](const patchy::FilterRegistry&,
                    const patchy::FilterInvocation&, patchy::PixelBuffer& pixels,
                    const patchy::FilterProgress*) {
    ++executions;
    pixels.pixel(0, 0)[0] = 255;
  };
  counted_metadata.output_margin =
      [](const patchy::FilterInvocation&, std::int32_t,
         std::int32_t) { return 5; };
  counted_metadata.translation_support =
      [](const patchy::FilterInvocation&) -> std::optional<int> { return 2; };
  registry.register_filter({"test.filters.counted", "Counted",
                            [](patchy::PixelBuffer&) {},
                            std::move(counted_metadata)});
  const auto counted = registry.default_invocation("test.filters.counted");
  const patchy::FilterRecipe zero_opacity{
      {patchy::FilterRecipeEntry{counted, true, 0.0}}};
  auto source = solid_rgba(3, 2, 10, 20, 30, 255);
  const auto source_copy = source;
  int progress_calls = 0;
  const patchy::FilterProgress progress{
      [&progress_calls](int, int, patchy::FilterProgressStage) {
        ++progress_calls;
        return true;
      }};
  registry.apply(zero_opacity, source, &progress);
  CHECK(executions == 0);
  CHECK(progress_calls == 0);
  CHECK(std::equal(source.data().begin(), source.data().end(),
                   source_copy.data().begin()));
  const patchy::Rect source_bounds{13, 17, 3, 2};
  const auto rendered =
      registry.render(zero_opacity, source, source_bounds, true, &progress);
  CHECK(executions == 0);
  CHECK(progress_calls == 0);
  CHECK(rendered.bounds.x == source_bounds.x);
  CHECK(rendered.bounds.y == source_bounds.y);
  CHECK(rendered.bounds.width == source_bounds.width);
  CHECK(rendered.bounds.height == source_bounds.height);
  CHECK(std::equal(rendered.pixels.data().begin(),
                   rendered.pixels.data().end(), source.data().begin()));
}

void filter_recipe_order_and_bounds_match_explicit_execution() {
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  auto clouds = registry.default_invocation(
      "patchy.filters.clouds", patchy::RgbColor{240, 20, 10},
      patchy::RgbColor{5, 15, 230});
  auto box = registry.default_invocation("patchy.filters.box_blur");
  box.parameters["radius"] = std::int64_t{2};
  const auto source = solid_rgba(9, 7, 30, 70, 110, 200);
  const patchy::Rect bounds{40, 60, 9, 7};

  const auto first = registry.render(clouds, source, bounds);
  const auto explicit_result =
      registry.render(box, first.pixels, first.bounds);
  const patchy::FilterRecipe ordered{{patchy::FilterRecipeEntry{clouds},
                                      patchy::FilterRecipeEntry{box}}};
  const auto recipe_result = registry.render(ordered, source, bounds);
  CHECK(recipe_result.bounds.x == explicit_result.bounds.x);
  CHECK(recipe_result.bounds.y == explicit_result.bounds.y);
  CHECK(recipe_result.bounds.width == explicit_result.bounds.width);
  CHECK(recipe_result.bounds.height == explicit_result.bounds.height);
  CHECK(recipe_result.bounds.x == bounds.x - 2);
  CHECK(recipe_result.bounds.y == bounds.y - 2);
  CHECK(recipe_result.bounds.width == bounds.width + 4);
  CHECK(recipe_result.bounds.height == bounds.height + 4);
  CHECK(recipe_result.pixels.format() == explicit_result.pixels.format());
  CHECK(recipe_result.pixels.width() == explicit_result.pixels.width());
  CHECK(recipe_result.pixels.height() == explicit_result.pixels.height());
  CHECK(std::equal(recipe_result.pixels.data().begin(),
                   recipe_result.pixels.data().end(),
                   explicit_result.pixels.data().begin()));

  const patchy::FilterRecipe reversed{{patchy::FilterRecipeEntry{box},
                                       patchy::FilterRecipeEntry{clouds}}};
  const auto reversed_result = registry.render(reversed, source, bounds);
  CHECK(reversed_result.bounds.x == recipe_result.bounds.x);
  CHECK(reversed_result.bounds.y == recipe_result.bounds.y);
  CHECK(reversed_result.pixels.width() == recipe_result.pixels.width());
  CHECK(!std::equal(reversed_result.pixels.data().begin(),
                    reversed_result.pixels.data().end(),
                    recipe_result.pixels.data().begin()));
}

void filter_recipe_render_trace_records_every_entry_input_bounds() {
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  auto gaussian_two =
      registry.default_invocation("patchy.filters.gaussian_blur");
  gaussian_two.parameters["radius"] = std::int64_t{2};
  auto gaussian_six = gaussian_two;
  gaussian_six.parameters["radius"] = std::int64_t{6};
  auto gaussian_nine = gaussian_two;
  gaussian_nine.parameters["radius"] = std::int64_t{9};
  auto twirl = registry.default_invocation("patchy.filters.twirl");
  twirl.parameters["angle"] = std::int64_t{210};
  twirl.parameters["radius"] = std::int64_t{80};
  twirl.parameters["center_x"] = 35.0;
  twirl.parameters["center_y"] = 60.0;
  auto gaussian_three = gaussian_two;
  gaussian_three.parameters["radius"] = std::int64_t{3};

  const patchy::FilterRecipe recipe{{
      patchy::FilterRecipeEntry{gaussian_two},
      patchy::FilterRecipeEntry{gaussian_six, false},
      patchy::FilterRecipeEntry{gaussian_nine, true, 0.0},
      patchy::FilterRecipeEntry{twirl},
      patchy::FilterRecipeEntry{gaussian_three},
  }};
  const auto source = solid_rgba(17, 13, 45, 95, 155, 255);
  const patchy::Rect bounds{70, 90, source.width(), source.height()};
  patchy::FilterRecipeRenderTrace trace;
  const auto rendered =
      registry.render(recipe, source, bounds, true, nullptr, &trace);
  CHECK(trace.entry_input_bounds.size() == recipe.entries.size());

  const auto same_rect = [](patchy::Rect left, patchy::Rect right) {
    return left.x == right.x && left.y == right.y &&
           left.width == right.width && left.height == right.height;
  };
  patchy::FilterRenderResult expected{source, bounds};
  for (std::size_t index = 0; index < recipe.entries.size(); ++index) {
    CHECK(same_rect(trace.entry_input_bounds[index], expected.bounds));
    const auto& entry = recipe.entries[index];
    if (entry.enabled && entry.opacity > 0.0) {
      expected = registry.render(entry.invocation, expected.pixels,
                                 expected.bounds);
    }
  }
  CHECK(same_rect(rendered.bounds, expected.bounds));
  CHECK(rendered.pixels.format() == expected.pixels.format());
  CHECK(rendered.pixels.width() == expected.pixels.width());
  CHECK(rendered.pixels.height() == expected.pixels.height());
  CHECK(std::equal(rendered.pixels.data().begin(),
                   rendered.pixels.data().end(),
                   expected.pixels.data().begin()));
}

void filter_recipe_expansion_keeps_fully_transparent_bounds_stable() {
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  patchy::PixelBuffer transparent(7, 5, patchy::PixelFormat::rgba8());
  transparent.clear(0);
  const patchy::Rect bounds{30, 40, 7, 5};
  auto box = registry.default_invocation("patchy.filters.box_blur");
  box.parameters["radius"] = std::int64_t{3};

  const auto single = registry.render(box, transparent, bounds);
  CHECK(single.bounds.x == bounds.x);
  CHECK(single.bounds.y == bounds.y);
  CHECK(single.bounds.width == bounds.width);
  CHECK(single.bounds.height == bounds.height);
  CHECK(single.pixels.width() == transparent.width());
  CHECK(single.pixels.height() == transparent.height());
  CHECK(std::equal(single.pixels.data().begin(), single.pixels.data().end(),
                   transparent.data().begin()));

  const patchy::FilterRecipe repeated{{patchy::FilterRecipeEntry{box},
                                       patchy::FilterRecipeEntry{box}}};
  const auto stacked = registry.render(repeated, transparent, bounds);
  CHECK(stacked.bounds.x == bounds.x);
  CHECK(stacked.bounds.y == bounds.y);
  CHECK(stacked.bounds.width == bounds.width);
  CHECK(stacked.bounds.height == bounds.height);
  CHECK(stacked.pixels.width() == transparent.width());
  CHECK(stacked.pixels.height() == transparent.height());
  CHECK(std::equal(stacked.pixels.data().begin(), stacked.pixels.data().end(),
                   transparent.data().begin()));

  // Recipe opacity uses a deterministic 16-bit fixed-point weight. A positive
  // value that quantizes to zero must also retain the input bounds; otherwise
  // an invisible blur would still grow the layer and create a no-op undo.
  const auto opaque = solid_rgba(7, 5, 35, 85, 145, 255);
  const patchy::FilterRecipe microscopic{{patchy::FilterRecipeEntry{
      box, true, 0.25 / 65535.0, patchy::BlendMode::Normal}}};
  const auto microscopic_result =
      registry.render(microscopic, opaque, bounds);
  CHECK(microscopic_result.bounds.x == bounds.x);
  CHECK(microscopic_result.bounds.y == bounds.y);
  CHECK(microscopic_result.bounds.width == bounds.width);
  CHECK(microscopic_result.bounds.height == bounds.height);
  CHECK(std::equal(microscopic_result.pixels.data().begin(),
                   microscopic_result.pixels.data().end(),
                   opaque.data().begin()));
}

}  // namespace

std::vector<patchy::test::TestCase> document_ops_filters_tests() {
  return {
      {"tool_flip_horizontal_changes_pixels_and_writes_artifact", tool_flip_horizontal_changes_pixels_and_writes_artifact},
      {"tool_flip_vertical_changes_pixels_and_writes_artifact", tool_flip_vertical_changes_pixels_and_writes_artifact},
      {"document_crop_to_selection_changes_canvas_and_writes_artifact",
       document_crop_to_selection_changes_canvas_and_writes_artifact},
      {"document_canvas_resize_expands_layers_for_editing", document_canvas_resize_expands_layers_for_editing},
      {"document_canvas_resize_honors_anchor_and_extension_color",
       document_canvas_resize_honors_anchor_and_extension_color},
      {"document_image_resize_scales_layers_and_writes_artifact",
       document_image_resize_scales_layers_and_writes_artifact},
      {"document_rotate_clockwise_changes_canvas_and_writes_artifact",
       document_rotate_clockwise_changes_canvas_and_writes_artifact},
      {"document_rotate_counterclockwise_changes_canvas_and_writes_artifact",
       document_rotate_counterclockwise_changes_canvas_and_writes_artifact},
      {"tool_stroke_selection_draws_border_and_writes_artifact", tool_stroke_selection_draws_border_and_writes_artifact},
      {"layer_merge_visible_creates_flattened_artifact", layer_merge_visible_creates_flattened_artifact},
      {"filters_register_and_apply", filters_register_and_apply},
      {"filters_builtin_effects_apply_and_write_artifacts", filters_builtin_effects_apply_and_write_artifacts},
      {"filter_catalog_defines_stable_named_contracts", filter_catalog_defines_stable_named_contracts},
      {"filter_invocations_normalize_scale_and_reject_bad_data",
       filter_invocations_normalize_scale_and_reject_bad_data},
      {"filter_centers_preserve_defaults_move_effects_and_survive_padding",
       filter_centers_preserve_defaults_move_effects_and_survive_padding},
      {"filter_named_engine_recipes_bounds_colors_and_legacy_stay_distinct",
       filter_named_engine_recipes_bounds_colors_and_legacy_stay_distinct},
      {"filter_recipe_opacity_interpolates_rgba_results",
       filter_recipe_opacity_interpolates_rgba_results},
      {"filter_recipe_native_smart_filter_mapping_is_all_or_nothing",
       filter_recipe_native_smart_filter_mapping_is_all_or_nothing},
      {"filter_recipe_scales_supports_validates_and_skips_zero_opacity",
       filter_recipe_scales_supports_validates_and_skips_zero_opacity},
      {"filter_recipe_order_and_bounds_match_explicit_execution",
       filter_recipe_order_and_bounds_match_explicit_execution},
      {"filter_recipe_render_trace_records_every_entry_input_bounds",
       filter_recipe_render_trace_records_every_entry_input_bounds},
      {"filter_recipe_expansion_keeps_fully_transparent_bounds_stable",
       filter_recipe_expansion_keeps_fully_transparent_bounds_stable},
  };
}
