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
#include "psd_test_support.hpp"
#include "test_groups.hpp"

namespace {

using patchy::test::find_layer_named;
using patchy::test::psd_first_layer_extra_data;
using patchy::test::psd_layer_block_payload;
using patchy::test::solid_rgb;
using patchy::test::solid_rgba;

void layer_mask_shapes_effects_regardless_of_link() {
  // Photoshop shapes layer effects with the layer mask whether or not the mask
  // is linked (verified against Photoshop 2026 with both drop shadows and
  // strokes): the chain toggle only controls move behavior, not rendering.
  for (const auto linked : {true, false}) {
    patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Background", solid_rgb(64, 64, 255, 255, 255));
    patchy::Layer shadowed(document.allocate_layer_id(), "Shadowed", solid_rgba(16, 16, 200, 30, 30, 255));
    shadowed.set_bounds(patchy::Rect{16, 16, 16, 16});
    patchy::PixelBuffer mask_pixels(64, 64, patchy::PixelFormat::gray8());
    mask_pixels.clear(0);  // hide the whole layer
    shadowed.set_mask(patchy::LayerMask{patchy::Rect{0, 0, 64, 64}, std::move(mask_pixels), 0, false});
    patchy::LayerDropShadow shadow;
    shadow.enabled = true;
    shadow.opacity = 1.0F;
    shadow.angle_degrees = 90.0F;  // shadow straight down
    shadow.distance = 20.0F;
    shadow.size = 2.0F;
    shadowed.layer_style().drop_shadows.push_back(shadow);
    patchy::set_layer_mask_linked(shadowed, linked);
    document.add_layer(std::move(shadowed));

    const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
    // The square sits at (16..32)^2; its shadow would land around (16..32)x(36..52).
    CHECK(flattened.pixel(24, 24)[0] > 240);  // content hidden
    CHECK(flattened.pixel(24, 44)[0] > 240);  // hidden content casts no shadow either
  }

  // Photoshop-authored reference: unlinked mask (reveal rect (24,8)-(56,40),
  // default 0) over a red square at (16..48)^2 with a black drop shadow
  // (down 10px, blur 4). Expectations measured from Photoshop 2026's render.
  const auto fixture_path = patchy::test::committed_psd_fixture_path("photoshop-unlinked-mask-shadow.psd");
  const auto fixture = patchy::psd::DocumentIo::read_file(fixture_path);
  const auto* layer = find_layer_named(fixture.layers(), "Layer 1");
  CHECK(layer != nullptr);
  CHECK(!patchy::layer_mask_linked(*layer));
  const auto rendered = patchy::Compositor{}.flatten_rgb8(fixture);
  CHECK(rendered.pixel(40, 30)[0] > 150 && rendered.pixel(40, 30)[1] < 90);  // revealed red content
  CHECK(rendered.pixel(20, 30)[0] > 240);                                    // content hidden by default color
  CHECK(rendered.pixel(40, 44)[0] < 90);                                     // shadow of revealed content
  CHECK(rendered.pixel(40, 53)[0] > 240);  // no shadow where the mask hides the source
}

void psd_layer_mask_hides_effects_round_trip() {
  for (const auto hides : {false, true}) {
    patchy::Document document(4, 2, patchy::PixelFormat::rgb8());
    auto& layer = document.add_pixel_layer("Styled", solid_rgba(4, 2, 10, 20, 30, 255));
    patchy::LayerDropShadow shadow;
    shadow.enabled = true;
    layer.layer_style().drop_shadows.push_back(shadow);
    layer.layer_style().layer_mask_hides_effects = hides;

    const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
    const auto payload = psd_layer_block_payload(psd_first_layer_extra_data(bytes), "lmgm");
    CHECK(payload.has_value() == hides);
    if (payload.has_value()) {
      CHECK(payload->size() == 4U);
      CHECK((*payload)[0] == 1U);
    }

    const auto read = patchy::psd::DocumentIo::read(bytes);
    CHECK(read.layers().size() == 1);
    CHECK(read.layers().front().layer_style().layer_mask_hides_effects == hides);
  }
}

void layer_mask_hides_effects_clips_exterior_effect_output() {
  // "Layer Mask Hides Effects" off (default): the mask shapes what casts the
  // effect, but shadow/glow/stroke output may still land on mask-hidden areas.
  // On: the mask also clips the effect output (Photoshop's Blending Options box).
  for (const auto hides : {false, true}) {
    patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Background", solid_rgb(64, 64, 255, 255, 255));
    patchy::Layer shadowed(document.allocate_layer_id(), "Shadowed", solid_rgba(16, 16, 200, 30, 30, 255));
    shadowed.set_bounds(patchy::Rect{16, 16, 16, 16});
    patchy::PixelBuffer mask_pixels(64, 64, patchy::PixelFormat::gray8());
    for (std::int32_t y = 0; y < 64; ++y) {
      for (std::int32_t x = 0; x < 64; ++x) {
        *mask_pixels.pixel(x, y) = x < 32 ? 255 : 0;  // hide the canvas right half
      }
    }
    shadowed.set_mask(patchy::LayerMask{patchy::Rect{0, 0, 64, 64}, std::move(mask_pixels), 0, false});
    patchy::LayerDropShadow shadow;
    shadow.enabled = true;
    shadow.opacity = 1.0F;
    shadow.angle_degrees = 180.0F;  // light from the left; shadow lands 20px to the right
    shadow.distance = 20.0F;
    shadow.size = 2.0F;
    shadowed.layer_style().drop_shadows.push_back(shadow);
    shadowed.layer_style().layer_mask_hides_effects = hides;
    document.add_layer(std::move(shadowed));

    const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
    CHECK(flattened.pixel(24, 24)[0] > 150 && flattened.pixel(24, 24)[1] < 90);  // visible red square
    // The square (16..32)^2 casts its shadow at (36..52)x(16..32), inside the
    // mask-hidden half of the canvas.
    if (hides) {
      CHECK(flattened.pixel(44, 24)[0] > 240);  // output clipped by the mask
    } else {
      CHECK(flattened.pixel(44, 24)[0] < 60);  // output spills onto hidden area
    }
  }
}

void psd_photoshop_mask_hides_effects_fixture_clips_shadow() {
  // Same Photoshop document as photoshop-global-light-shadow.psd, saved with
  // "Layer Mask Hides Effects" checked ('lmgm'). The mask hides x >= 32; the
  // shadow body sits at x16..31 with blur spill just past the boundary.
  const auto clipped = patchy::psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path("photoshop-mask-hides-effects.psd"));
  const auto* layer = find_layer_named(clipped.layers(), "Layer 1");
  CHECK(layer != nullptr);
  CHECK(layer->layer_style().layer_mask_hides_effects);
  const auto clipped_render = patchy::Compositor{}.flatten_rgb8(clipped);
  CHECK(clipped_render.pixel(24, 52)[0] < 110);   // shadow body
  CHECK(clipped_render.pixel(33, 52)[0] > 250);   // spill clipped at the mask boundary

  const auto spilling = patchy::psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path("photoshop-global-light-shadow.psd"));
  const auto* spilling_layer = find_layer_named(spilling.layers(), "Layer 1");
  CHECK(spilling_layer != nullptr);
  CHECK(!spilling_layer->layer_style().layer_mask_hides_effects);
  const auto spilling_render = patchy::Compositor{}.flatten_rgb8(spilling);
  CHECK(spilling_render.pixel(33, 52)[0] < 250);  // blur spill crosses the boundary
}

void layer_stroke_outlines_semi_transparent_regions_without_fill() {
  // Content under a UNIFORM 50% gray layer mask keeps its raw-pixel stroke
  // contour: only fully-black mask regions reshape the shape (see
  // layer_stroke_follows_mask_contour). The band paints at full strength — the
  // July 2026 PS re-probe killed the old "mask attenuates the stroke where it
  // lands" model. Fractional mask values also must not fold into the coverage
  // math: the pre-June-2026 formula derived the stroke from alpha x mask and
  // painted a constant wash across the region's whole interior. (Photoshop
  // actually composites gray masks with its content-knockout model — the same
  // documented divergence as semi-transparent fills, docs/ps-compat.md.)
  patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(64, 64, 255, 255, 255));
  patchy::Layer stroked(document.allocate_layer_id(), "Stroked", solid_rgba(32, 32, 20, 90, 200, 255));
  stroked.set_bounds(patchy::Rect{16, 16, 32, 32});
  patchy::PixelBuffer mask_pixels(64, 64, patchy::PixelFormat::gray8());
  mask_pixels.clear(128);
  stroked.set_mask(patchy::LayerMask{patchy::Rect{0, 0, 64, 64}, std::move(mask_pixels), 128, false});
  patchy::LayerStroke stroke;
  stroke.enabled = true;
  stroke.opacity = 1.0F;
  stroke.size = 3.0F;
  stroke.position = patchy::LayerStrokePosition::Outside;
  stroke.color = patchy::RgbColor{255, 0, 0};
  stroked.layer_style().strokes.push_back(stroke);
  document.add_layer(std::move(stroked));

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  // Interior of the square: half-visible blue content, no red stroke wash.
  const auto* interior = flattened.pixel(32, 32);
  CHECK(interior[2] > 200);
  CHECK(interior[0] < 150);
  // Just outside the square: the full-strength stroke band along the pixel
  // contour (the gray mask neither moves nor attenuates it).
  const auto* edge = flattened.pixel(14, 32);
  CHECK(edge[0] > 240);
  CHECK(edge[1] < 40);
}

void layer_stroke_follows_mask_contour() {
  // A fully-black mask region reshapes the stroke contour exactly like pixel
  // transparency, and the band paints on mask-hidden ground at full strength
  // (Photoshop 2026 COM probes, July 2026): an opaque layer covering the whole
  // canvas with an inset rectangular mask strokes the MASK rectangle, not the
  // canvas edge. Pre-fix Patchy derived the contour from raw pixels and then
  // attenuated the output by the mask, which rendered no stroke at all here
  // (the stroke_test.psd bug: masked smart object + Stroke showed nothing).
  // PS band runs at y=48 around the mask contour at x=39.5, size 8:
  // Outside 32..39, Center 36..43, Inside 40..47 — full (255,0,0).
  struct Case {
    patchy::LayerStrokePosition position;
    int band_left;
    int band_right;
  };
  const std::array<Case, 3> cases{{{patchy::LayerStrokePosition::Outside, 32, 39},
                                   {patchy::LayerStrokePosition::Center, 36, 43},
                                   {patchy::LayerStrokePosition::Inside, 40, 47}}};
  for (const auto& test_case : cases) {
    patchy::Document document(160, 96, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Background", solid_rgb(160, 96, 255, 255, 255));
    patchy::Layer stroked(document.allocate_layer_id(), "Stroked", solid_rgba(160, 96, 0, 0, 200, 255));
    patchy::PixelBuffer mask_pixels(160, 96, patchy::PixelFormat::gray8());
    mask_pixels.clear(0);
    for (std::int32_t y = 24; y < 72; ++y) {
      for (std::int32_t x = 40; x < 120; ++x) {
        *mask_pixels.pixel(x, y) = 255;
      }
    }
    stroked.set_mask(patchy::LayerMask{patchy::Rect{0, 0, 160, 96}, std::move(mask_pixels), 0, false});
    patchy::LayerStroke stroke;
    stroke.enabled = true;
    stroke.opacity = 1.0F;
    stroke.size = 8.0F;
    stroke.position = test_case.position;
    stroke.color = patchy::RgbColor{255, 0, 0};
    stroked.layer_style().strokes.push_back(stroke);
    document.add_layer(std::move(stroked));

    const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
    const auto expect_full_red = [&](int x, int y) {
      const auto* px = flattened.pixel(x, y);
      CHECK(px[0] > 240 && px[1] < 15 && px[2] < 15);
    };
    const auto expect_not_red = [&](int x, int y) {
      const auto* px = flattened.pixel(x, y);
      CHECK(px[0] < 150 || px[1] > 150);
    };
    expect_not_red(test_case.band_left - 2, 48);
    expect_full_red(test_case.band_left, 48);
    expect_full_red(test_case.band_right, 48);
    expect_not_red(test_case.band_right + 2, 48);
    // Mirrored band on the right mask edge (contour 119.5) and above the top
    // edge (contour 23.5, band rows = band columns shifted by the contour
    // offset: 24 + (band_x - 40)).
    expect_full_red(159 - test_case.band_right, 48);
    expect_full_red(159 - test_case.band_left, 48);
    expect_full_red(80, test_case.band_left - 16);
    expect_full_red(80, test_case.band_right - 16);
    // The raw pixel contour (canvas edge) is not stroked, and the mask-hidden
    // ground shows the white background.
    expect_not_red(1, 48);
    expect_not_red(158, 48);
    expect_not_red(80, 2);
    // Content stays visible inside the mask.
    const auto* content = flattened.pixel(80, 48);
    CHECK(content[2] > 150 && content[0] < 90);
  }
}

void layer_stroke_mask_hides_effects_keeps_pixel_contour() {
  // "Layer Mask Hides Effects" (lmgm): the stroke keeps its raw-pixel contour
  // and the mask hides the output where it lands instead of reshaping it
  // (Photoshop 2026 COM probes, July 2026: opaque rect x=48..111 with a mask
  // revealing x<80 renders Outside at 40..47 and Inside at 48..55 on the
  // revealed side only — nothing at the mask cut, nothing on hidden ground).
  struct Case {
    patchy::LayerStrokePosition position;
    int band_left;
    int band_right;
  };
  const std::array<Case, 2> cases{{{patchy::LayerStrokePosition::Outside, 40, 47},
                                   {patchy::LayerStrokePosition::Inside, 48, 55}}};
  for (const auto& test_case : cases) {
    patchy::Document document(160, 96, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Background", solid_rgb(160, 96, 255, 255, 255));
    patchy::Layer stroked(document.allocate_layer_id(), "Stroked", solid_rgba(64, 48, 0, 0, 200, 255));
    stroked.set_bounds(patchy::Rect{48, 24, 64, 48});
    patchy::PixelBuffer mask_pixels(160, 96, patchy::PixelFormat::gray8());
    mask_pixels.clear(0);
    for (std::int32_t y = 0; y < 96; ++y) {
      for (std::int32_t x = 0; x < 80; ++x) {
        *mask_pixels.pixel(x, y) = 255;
      }
    }
    stroked.set_mask(patchy::LayerMask{patchy::Rect{0, 0, 160, 96}, std::move(mask_pixels), 0, false});
    patchy::LayerStroke stroke;
    stroke.enabled = true;
    stroke.opacity = 1.0F;
    stroke.size = 8.0F;
    stroke.position = test_case.position;
    stroke.color = patchy::RgbColor{255, 0, 0};
    stroked.layer_style().strokes.push_back(stroke);
    stroked.layer_style().layer_mask_hides_effects = true;
    document.add_layer(std::move(stroked));

    const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
    const auto expect_full_red = [&](int x, int y) {
      const auto* px = flattened.pixel(x, y);
      CHECK(px[0] > 240 && px[1] < 15 && px[2] < 15);
    };
    const auto expect_not_red = [&](int x, int y) {
      const auto* px = flattened.pixel(x, y);
      CHECK(px[0] < 150 || px[1] > 150);
    };
    // Band along the visible half of the raw pixel contour.
    expect_full_red(test_case.band_left, 48);
    expect_full_red(test_case.band_right, 48);
    // No band at the mask cut (x=79.5): the mask does not reshape the contour.
    expect_not_red(76, 48);
    expect_not_red(82, 48);
    // The right pixel contour's band is hidden by the mask.
    expect_not_red(108, 48);
    expect_not_red(116, 48);
  }
}

void psd_photoshop_stroke_masked_fixture_matches() {
  // Photoshop-authored reference: three opaque blue 40x40 rects, each with a
  // binary mask revealing an inner 24x24 and a green size-6 stroke — Outside
  // (mask x16..39), Center (x72..95), Inside (x128..151), mask rows y20..43.
  // Band runs measured from Photoshop 2026's flatten, which the mask-gated
  // stroke matches run-for-run: at y=32 Outside spans 10..15|40..45, Center
  // 69..74|93..98, Inside 128..133|146..151; blue content only inside each
  // mask; the raw pixel rects (x8.., x64.., x120..) stay unstroked white.
  const auto document = patchy::psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path("photoshop-stroke-masked.psd"));
  const auto rendered = patchy::Compositor{}.flatten_rgb8(document);
  const auto expect_green = [&](int x, int y) {
    const auto* px = rendered.pixel(x, y);
    CHECK(px[1] > 150 && px[0] < 110 && px[2] < 110);
  };
  const auto expect_blue = [&](int x, int y) {
    const auto* px = rendered.pixel(x, y);
    CHECK(px[2] > 150 && px[1] < 110 && px[0] < 110);
  };
  const auto expect_white = [&](int x, int y) {
    const auto* px = rendered.pixel(x, y);
    CHECK(px[0] > 240 && px[1] > 240 && px[2] > 240);
  };
  // Outside: 6px band strictly beyond the mask contour; pixel rect edge white.
  expect_white(8, 32);
  expect_white(9, 32);
  expect_green(10, 32);
  expect_green(15, 32);
  expect_blue(16, 32);
  expect_blue(39, 32);
  expect_green(40, 32);
  expect_green(45, 32);
  expect_white(46, 32);
  // Center: 3px out + 3px in around the mask contour.
  expect_white(68, 32);
  expect_green(69, 32);
  expect_green(74, 32);
  expect_blue(75, 32);
  expect_blue(92, 32);
  expect_green(93, 32);
  expect_green(98, 32);
  expect_white(99, 32);
  // Inside: 6px band strictly within the mask contour.
  expect_white(121, 32);
  expect_white(127, 32);
  expect_green(128, 32);
  expect_green(133, 32);
  expect_blue(134, 32);
  expect_blue(145, 32);
  expect_green(146, 32);
  expect_green(151, 32);
  expect_white(152, 32);
  // Vertical band through the Outside rect's mask (rows 20..43 at x=28).
  expect_white(28, 13);
  expect_green(28, 14);
  expect_green(28, 19);
  expect_blue(28, 20);
  expect_blue(28, 43);
  expect_green(28, 44);
  expect_green(28, 49);
  expect_white(28, 50);
}

void exact_squared_distance_transform_matches_bruteforce() {
  constexpr int width = 9;
  constexpr int height = 9;
  const std::array<std::pair<int, int>, 2> sources{{{2, 3}, {7, 8}}};
  std::vector<float> field(static_cast<std::size_t>(width) * height, patchy::render_detail::kEdtUnreached);
  for (const auto& [sx, sy] : sources) {
    field[static_cast<std::size_t>(sy) * width + sx] = 0.0F;
  }
  patchy::render_detail::exact_squared_distance_transform(field, width, height);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      int expected = 1 << 30;
      for (const auto& [sx, sy] : sources) {
        expected = std::min(expected, (x - sx) * (x - sx) + (y - sy) * (y - sy));
      }
      CHECK(field[static_cast<std::size_t>(y) * width + x] == static_cast<float>(expected));
    }
  }
}

void layer_stroke_center_band_width_matches_size() {
  // Photoshop's Center stroke spans size/2 outward + size/2 inward (total = size).
  // The legacy dilation grew size in BOTH directions, doubling the band.
  patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(64, 64, 255, 255, 255));
  patchy::Layer shape(document.allocate_layer_id(), "Shape", solid_rgba(16, 16, 0, 0, 255, 255));
  shape.set_bounds(patchy::Rect{24, 24, 16, 16});
  patchy::LayerStroke stroke;
  stroke.enabled = true;
  stroke.opacity = 1.0F;
  stroke.size = 6.0F;
  stroke.position = patchy::LayerStrokePosition::Center;
  stroke.color = patchy::RgbColor{255, 0, 0};
  shape.layer_style().strokes.push_back(stroke);
  document.add_layer(std::move(shape));

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  // Left edge at x=24: band covers x=21..23 outside and x=24..26 inside.
  for (int x = 21; x <= 26; ++x) {
    const auto* px = flattened.pixel(x, 32);
    CHECK(px[0] > 240 && px[1] < 15 && px[2] < 15);
  }
  const auto* beyond_outside = flattened.pixel(20, 32);
  CHECK(beyond_outside[0] > 240 && beyond_outside[1] > 240 && beyond_outside[2] > 240);
  const auto* beyond_inside = flattened.pixel(27, 32);
  CHECK(beyond_inside[2] > 240 && beyond_inside[0] < 15);
}

void layer_stroke_center_has_no_seam_on_antialiased_contour() {
  // A partially covered contour pixel sits in the middle of a Center band: the
  // inside and outside halves must sum to full stroke coverage there. The legacy
  // max() combine dipped to ~50%, leaving a bright seam along anti-aliased edges.
  patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(64, 64, 255, 255, 255));
  auto pixels = solid_rgba(16, 16, 0, 0, 255, 255);
  for (std::int32_t y = 0; y < 16; ++y) {
    pixels.pixel(0, y)[3] = 128;  // anti-aliased left edge column
  }
  patchy::Layer shape(document.allocate_layer_id(), "Shape", std::move(pixels));
  shape.set_bounds(patchy::Rect{24, 24, 16, 16});
  patchy::LayerStroke stroke;
  stroke.enabled = true;
  stroke.opacity = 1.0F;
  stroke.size = 4.0F;
  stroke.position = patchy::LayerStrokePosition::Center;
  stroke.color = patchy::RgbColor{255, 0, 0};
  shape.layer_style().strokes.push_back(stroke);
  document.add_layer(std::move(shape));

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  // The 50%-alpha contour column and its full-coverage neighbors are pure stroke.
  for (int x = 23; x <= 25; ++x) {
    const auto* px = flattened.pixel(x, 32);
    CHECK(px[0] > 240 && px[1] < 15 && px[2] < 15);
  }
}

void layer_stroke_outside_antialiases_corners() {
  // Band edges follow Euclidean distance with a 1px anti-aliasing ramp: rectangle
  // corners get partially covered pixels instead of the legacy hard staircase.
  patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(64, 64, 255, 255, 255));
  patchy::Layer shape(document.allocate_layer_id(), "Shape", solid_rgba(8, 8, 0, 0, 255, 255));
  shape.set_bounds(patchy::Rect{24, 24, 8, 8});
  patchy::LayerStroke stroke;
  stroke.enabled = true;
  stroke.opacity = 1.0F;
  stroke.size = 2.0F;
  stroke.position = patchy::LayerStrokePosition::Outside;
  stroke.color = patchy::RgbColor{255, 0, 0};
  shape.layer_style().strokes.push_back(stroke);
  document.add_layer(std::move(shape));

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  // Axis-aligned: full band at d <= 2, nothing at d = 3.
  const auto* on_band = flattened.pixel(24, 22);
  CHECK(on_band[0] > 240 && on_band[1] < 15);
  const auto* past_band = flattened.pixel(24, 21);
  CHECK(past_band[1] > 245);
  // Diagonal by the corner: d = sqrt(2) is full, d = 2*sqrt(2) ~ 2.83 is a partial
  // ~17% pixel (green channel ~211 over white), d = 3*sqrt(2) is untouched.
  const auto* diagonal_full = flattened.pixel(23, 23);
  CHECK(diagonal_full[0] > 240 && diagonal_full[1] < 15);
  const auto* diagonal_partial = flattened.pixel(22, 22);
  CHECK(diagonal_partial[1] > 185 && diagonal_partial[1] < 235);
  const auto* diagonal_outside = flattened.pixel(21, 21);
  CHECK(diagonal_outside[1] > 245);
}

void layer_stroke_fractional_size_renders_partial_ring() {
  // Fractional sizes land as partial coverage at the band edge instead of the
  // legacy ceil() snap to the next integer radius.
  patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(64, 64, 255, 255, 255));
  patchy::Layer shape(document.allocate_layer_id(), "Shape", solid_rgba(8, 8, 0, 0, 255, 255));
  shape.set_bounds(patchy::Rect{24, 24, 8, 8});
  patchy::LayerStroke stroke;
  stroke.enabled = true;
  stroke.opacity = 1.0F;
  stroke.size = 2.5F;
  stroke.position = patchy::LayerStrokePosition::Outside;
  stroke.color = patchy::RgbColor{255, 0, 0};
  shape.layer_style().strokes.push_back(stroke);
  document.add_layer(std::move(shape));

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  const auto* full = flattened.pixel(22, 28);  // d = 2 -> full coverage
  CHECK(full[0] > 240 && full[1] < 15);
  const auto* half = flattened.pixel(21, 28);  // d = 3 -> 50% coverage over white
  CHECK(half[1] > 100 && half[1] < 155);
  const auto* outside = flattened.pixel(20, 28);  // d = 4 -> untouched
  CHECK(outside[1] > 245);
}

void psd_photoshop_stroke_positions_fixture_matches() {
  // Photoshop-authored reference: three opaque red rects (y16..47) with a green
  // size-6 stroke at each position — outside (x8..39), center (x64..95), inside
  // (x120..151). Band expectations measured from Photoshop 2026's render, which
  // Patchy's distance-band stroke matches run-for-run: outside spans d=1..6
  // beyond the contour, center splits 3+3, inside spans d=1..6 within.
  const auto document = patchy::psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path("photoshop-stroke-positions.psd"));
  const auto rendered = patchy::Compositor{}.flatten_rgb8(document);
  const auto expect_green = [&](int x, int y) {
    const auto* px = rendered.pixel(x, y);
    CHECK(px[1] > 150 && px[0] < 110);
  };
  const auto expect_red = [&](int x, int y) {
    const auto* px = rendered.pixel(x, y);
    CHECK(px[0] > 150 && px[1] < 110);
  };
  const auto expect_white = [&](int x, int y) {
    const auto* px = rendered.pixel(x, y);
    CHECK(px[0] > 240 && px[1] > 240 && px[2] > 240);
  };
  // Outside: 6px band strictly beyond the contour.
  expect_white(1, 32);
  expect_green(2, 32);
  expect_green(7, 32);
  expect_red(8, 32);
  expect_red(24, 32);
  // Center: 3px out + 3px in.
  expect_white(60, 32);
  expect_green(61, 32);
  expect_green(66, 32);
  expect_red(67, 32);
  expect_red(80, 32);
  expect_red(92, 32);
  expect_green(93, 32);
  expect_green(98, 32);
  expect_white(99, 32);
  // Inside: 6px band strictly within the contour.
  expect_white(119, 32);
  expect_green(120, 32);
  expect_green(125, 32);
  expect_red(126, 32);
  expect_red(140, 32);
  // Vertical band through the outside rect (top and bottom edges).
  expect_green(36, 10);
  expect_green(36, 15);
  expect_red(36, 16);
  expect_red(36, 47);
  expect_green(36, 48);
  expect_green(36, 53);
  expect_white(36, 54);
}

void psd_photoshop_stroke_partial_alpha_fixture_matches() {
  // Photoshop-authored reference: regions painted at 100% (x8..28), 50% (x28..56),
  // and 25% alpha (y40..52) with a green 3px outside stroke. Photoshop treats any
  // painted pixel as inside the stroked shape — the stroke fills the binary shape
  // and the content covers it per its alpha — so semi-transparent regions show a
  // green wash, while the opaque region stays clean. Expectations measured from
  // Photoshop 2026's render.
  const auto document = patchy::psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path("photoshop-stroke-partial-alpha.psd"));
  const auto rendered = patchy::Compositor{}.flatten_rgb8(document);
  const auto* opaque = rendered.pixel(16, 24);
  CHECK(opaque[0] > 150 && opaque[1] < 110);  // 100% region: pure content, no stroke
  const auto* half = rendered.pixel(44, 24);
  CHECK(half[1] > 150 && half[1] > half[0] + 30);  // 50% region: stroke shows through
  const auto* quarter = rendered.pixel(32, 46);
  CHECK(quarter[1] > 200 && quarter[0] < 110);  // 25% region: stroke dominates
  const auto* band = rendered.pixel(6, 24);
  CHECK(band[1] > 200 && band[0] < 80);  // outer band at full strength
}

// Shared canvas for the Overprint knockout cases: orange backdrop, opaque
// gray square, Inside white stroke size 10 (COM probes July 2026,
// docs/ps-compat.md). Band samples sit mid-band at (20, 32); the interior
// clear of the band is (32, 32).
patchy::Document make_overprint_document(const patchy::LayerStroke& stroke, float layer_opacity,
                                         bool with_red_overlay) {
  patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Backdrop", solid_rgb(64, 64, 255, 128, 0));
  patchy::Layer content(document.allocate_layer_id(), "Content", solid_rgba(32, 32, 110, 110, 110, 255));
  content.set_bounds(patchy::Rect{16, 16, 32, 32});
  content.set_opacity(layer_opacity);
  if (with_red_overlay) {
    patchy::LayerColorOverlay overlay;
    overlay.enabled = true;
    overlay.blend_mode = patchy::BlendMode::Normal;
    overlay.color = patchy::RgbColor{255, 0, 0};
    overlay.opacity = 1.0F;
    content.layer_style().color_overlays.push_back(overlay);
  }
  content.layer_style().strokes.push_back(stroke);
  document.add_layer(std::move(content));
  return document;
}

patchy::LayerStroke white_inside_stroke(float opacity, bool overprint) {
  patchy::LayerStroke stroke;
  stroke.enabled = true;
  stroke.blend_mode = patchy::BlendMode::Normal;
  stroke.color = patchy::RgbColor{255, 255, 255};
  stroke.opacity = opacity;
  stroke.size = 10.0F;
  stroke.position = patchy::LayerStrokePosition::Inside;
  stroke.overprint = overprint;
  return stroke;
}

void check_pixel_near(const patchy::PixelBuffer& image, std::int32_t x, std::int32_t y, int red, int green,
                      int blue, int tolerance) {
  const auto* pixel = image.pixel(x, y);
  CHECK(std::abs(static_cast<int>(pixel[0]) - red) <= tolerance);
  CHECK(std::abs(static_cast<int>(pixel[1]) - green) <= tolerance);
  CHECK(std::abs(static_cast<int>(pixel[2]) - blue) <= tolerance);
}

void layer_stroke_overprint_off_knocks_out_content_under_band() {
  // Overprint off (the Photoshop default): the band knocks the gray content
  // out and the 29% white blends against the orange backdrop — no gray in
  // the band at all (probe P1: PS renders exactly 0.29*white + 0.71*orange).
  const auto flattened = patchy::Compositor{}.flatten_rgb8(
      make_overprint_document(white_inside_stroke(0.29F, false), 1.0F, false));
  check_pixel_near(flattened, 20, 32, 255, 165, 74, 2);
  check_pixel_near(flattened, 32, 32, 110, 110, 110, 0);  // interior untouched
  check_pixel_near(flattened, 10, 32, 255, 128, 0, 0);    // outside untouched
}

void layer_stroke_overprint_on_blends_over_content() {
  // Overprint on: the stroke blends over the layer's own gray (probe P7).
  const auto flattened = patchy::Compositor{}.flatten_rgb8(
      make_overprint_document(white_inside_stroke(0.29F, true), 1.0F, false));
  check_pixel_near(flattened, 20, 32, 152, 152, 152, 2);
  check_pixel_near(flattened, 32, 32, 110, 110, 110, 0);
}

void layer_stroke_opaque_knockout_is_invisible() {
  // An opaque Normal stroke covers exactly what it would have knocked out, so
  // Overprint has no visible effect and the knockout path must reproduce the
  // historical bytes exactly (the identity property of the (1-C)/(1-s)
  // compensation).
  const auto with_knockout = patchy::Compositor{}.flatten_rgb8(
      make_overprint_document(white_inside_stroke(1.0F, false), 1.0F, false));
  const auto without = patchy::Compositor{}.flatten_rgb8(
      make_overprint_document(white_inside_stroke(1.0F, true), 1.0F, false));
  CHECK(with_knockout.width() == without.width());
  CHECK(with_knockout.height() == without.height());
  std::size_t mismatches = 0;
  for (std::int32_t y = 0; y < with_knockout.height(); ++y) {
    for (std::int32_t x = 0; x < with_knockout.width(); ++x) {
      const auto* a = with_knockout.pixel(x, y);
      const auto* b = without.pixel(x, y);
      if (a[0] != b[0] || a[1] != b[1] || a[2] != b[2]) {
        ++mismatches;
      }
    }
  }
  CHECK(mismatches == 0);
}

void layer_stroke_knockout_composes_with_layer_opacity() {
  // Layer opacity scales the whole knocked-out plane (probe P4: band =
  // 0.145*white + 0.855*orange, interior = half gray over orange).
  const auto flattened = patchy::Compositor{}.flatten_rgb8(
      make_overprint_document(white_inside_stroke(0.29F, false), 0.5F, false));
  check_pixel_near(flattened, 20, 32, 255, 146, 37, 2);
  check_pixel_near(flattened, 32, 32, 183, 119, 55, 2);
}

void layer_stroke_knockout_removes_interior_overlay_under_band() {
  // Interior effects ride the content plane: a 100% red Color Overlay is
  // knocked out under the band too (probe P2 — PS shows pure
  // stroke-over-backdrop, not stroke-over-red).
  const auto flattened = patchy::Compositor{}.flatten_rgb8(
      make_overprint_document(white_inside_stroke(0.29F, false), 1.0F, true));
  check_pixel_near(flattened, 20, 32, 255, 165, 74, 2);
  check_pixel_near(flattened, 32, 32, 255, 0, 0, 0);  // overlay intact inside
}

void layer_stroke_zero_opacity_knockout_still_knocks_out() {
  // A 0%-opacity Overprint-off stroke paints nothing but still knocks the
  // content out of its band at full coverage — the band shows the pure
  // backdrop (PS COM probe July 2026; the stroke_from_photoshop2.psd case,
  // where a size-109 inside stroke at 0% wipes the whole layer interior).
  // With Overprint on the same stroke is a complete no-op.
  const auto knocked = patchy::Compositor{}.flatten_rgb8(
      make_overprint_document(white_inside_stroke(0.0F, false), 1.0F, false));
  check_pixel_near(knocked, 20, 32, 255, 128, 0, 0);   // band: pure backdrop
  check_pixel_near(knocked, 32, 32, 110, 110, 110, 0);  // interior intact
  const auto untouched = patchy::Compositor{}.flatten_rgb8(
      make_overprint_document(white_inside_stroke(0.0F, true), 1.0F, false));
  check_pixel_near(untouched, 20, 32, 110, 110, 110, 0);
}

patchy::LayerDropShadow test_drop_shadow(float distance, float size, bool conceals) {
  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.blend_mode = patchy::BlendMode::Multiply;
  shadow.color = patchy::RgbColor{0, 0, 0};
  shadow.opacity = 0.75F;
  shadow.angle_degrees = 120.0F;
  shadow.distance = distance;
  shadow.size = size;
  shadow.layer_conceals = conceals;
  return shadow;
}

void drop_shadow_layer_conceals_hides_shadow_under_shape() {
  // "Layer Knocks Out Drop Shadow" (layerConceals, PS default on): the layer's
  // transparency shape punches a hole in its own shadow regardless of fill
  // opacity (COM probes July 2026: a 50%-fill interior shows content over the
  // PURE backdrop; with the option off the shadow shows through).
  patchy::Document on_document(64, 64, patchy::PixelFormat::rgb8());
  on_document.add_pixel_layer("Backdrop", solid_rgb(64, 64, 255, 128, 0));
  patchy::Layer on_layer(on_document.allocate_layer_id(), "Content", solid_rgba(32, 32, 110, 110, 110, 255));
  on_layer.set_bounds(patchy::Rect{16, 16, 32, 32});
  on_layer.set_fill_opacity(0.5F);
  on_layer.layer_style().drop_shadows.push_back(test_drop_shadow(0.0F, 5.0F, true));
  on_document.add_layer(std::move(on_layer));
  const auto concealed = patchy::Compositor{}.flatten_rgb8(on_document);
  check_pixel_near(concealed, 32, 32, 183, 119, 55, 2);  // no shadow under shape

  patchy::Document off_document(64, 64, patchy::PixelFormat::rgb8());
  off_document.add_pixel_layer("Backdrop", solid_rgb(64, 64, 255, 128, 0));
  patchy::Layer off_layer(off_document.allocate_layer_id(), "Content", solid_rgba(32, 32, 110, 110, 110, 255));
  off_layer.set_bounds(patchy::Rect{16, 16, 32, 32});
  off_layer.set_fill_opacity(0.5F);
  off_layer.layer_style().drop_shadows.push_back(test_drop_shadow(0.0F, 5.0F, false));
  off_document.add_layer(std::move(off_layer));
  const auto revealed = patchy::Compositor{}.flatten_rgb8(off_document);
  check_pixel_near(revealed, 32, 32, 87, 71, 55, 2);  // shadow shows through
}

void drop_shadow_conceals_composes_with_stroke_knockout() {
  // The stroke_from_photoshop2.psd case: a 0%-opacity Overprint-off stroke
  // knocks the content out of the band, and layerConceals keeps the layer's
  // own drop shadow from filling the hole — the band shows the pure backdrop
  // while the shadow stays visible outside the shape.
  patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Backdrop", solid_rgb(64, 64, 255, 128, 0));
  patchy::Layer content(document.allocate_layer_id(), "Content", solid_rgba(32, 32, 110, 110, 110, 255));
  content.set_bounds(patchy::Rect{16, 16, 32, 32});
  content.layer_style().strokes.push_back(white_inside_stroke(0.0F, false));
  content.layer_style().drop_shadows.push_back(test_drop_shadow(8.0F, 8.0F, true));
  document.add_layer(std::move(content));
  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  check_pixel_near(flattened, 20, 32, 255, 128, 0, 0);   // band: pure backdrop, no shadow
  check_pixel_near(flattened, 32, 32, 110, 110, 110, 0);  // interior content intact
  const auto* fringe = flattened.pixel(52, 52);           // outside SE corner: shadow present
  CHECK(fringe[0] < 250);
  CHECK(fringe[2] < 10);
}

void layer_stroke_knockout_blend_mode_uses_backdrop() {
  // Overprint off applies the stroke's blend mode against the BACKDROP even
  // at 100% opacity (probe P3: multiply band = mult(stroke, orange), not
  // mult(stroke, gray)); Overprint on keeps the over-content multiply.
  auto multiply = white_inside_stroke(1.0F, false);
  multiply.blend_mode = patchy::BlendMode::Multiply;
  multiply.color = patchy::RgbColor{200, 100, 50};
  const auto knocked =
      patchy::Compositor{}.flatten_rgb8(make_overprint_document(multiply, 1.0F, false));
  check_pixel_near(knocked, 20, 32, 200, 50, 0, 2);
  multiply.overprint = true;
  const auto over_content =
      patchy::Compositor{}.flatten_rgb8(make_overprint_document(multiply, 1.0F, false));
  check_pixel_near(over_content, 20, 32, 86, 43, 22, 2);
}

}  // namespace

std::vector<patchy::test::TestCase> stroke_mask_effects_tests() {
  return {
      {"layer_mask_shapes_effects_regardless_of_link", layer_mask_shapes_effects_regardless_of_link},
      {"psd_layer_mask_hides_effects_round_trip", psd_layer_mask_hides_effects_round_trip},
      {"layer_mask_hides_effects_clips_exterior_effect_output",
       layer_mask_hides_effects_clips_exterior_effect_output},
      {"psd_photoshop_mask_hides_effects_fixture_clips_shadow",
       psd_photoshop_mask_hides_effects_fixture_clips_shadow},
      {"layer_stroke_outlines_semi_transparent_regions_without_fill",
       layer_stroke_outlines_semi_transparent_regions_without_fill},
      {"layer_stroke_follows_mask_contour", layer_stroke_follows_mask_contour},
      {"layer_stroke_mask_hides_effects_keeps_pixel_contour",
       layer_stroke_mask_hides_effects_keeps_pixel_contour},
      {"psd_photoshop_stroke_masked_fixture_matches", psd_photoshop_stroke_masked_fixture_matches},
      {"exact_squared_distance_transform_matches_bruteforce",
       exact_squared_distance_transform_matches_bruteforce},
      {"layer_stroke_center_band_width_matches_size", layer_stroke_center_band_width_matches_size},
      {"layer_stroke_center_has_no_seam_on_antialiased_contour",
       layer_stroke_center_has_no_seam_on_antialiased_contour},
      {"layer_stroke_outside_antialiases_corners", layer_stroke_outside_antialiases_corners},
      {"layer_stroke_fractional_size_renders_partial_ring",
       layer_stroke_fractional_size_renders_partial_ring},
      {"layer_stroke_overprint_off_knocks_out_content_under_band",
       layer_stroke_overprint_off_knocks_out_content_under_band},
      {"layer_stroke_overprint_on_blends_over_content", layer_stroke_overprint_on_blends_over_content},
      {"layer_stroke_opaque_knockout_is_invisible", layer_stroke_opaque_knockout_is_invisible},
      {"layer_stroke_knockout_composes_with_layer_opacity",
       layer_stroke_knockout_composes_with_layer_opacity},
      {"layer_stroke_knockout_removes_interior_overlay_under_band",
       layer_stroke_knockout_removes_interior_overlay_under_band},
      {"layer_stroke_zero_opacity_knockout_still_knocks_out",
       layer_stroke_zero_opacity_knockout_still_knocks_out},
      {"drop_shadow_layer_conceals_hides_shadow_under_shape",
       drop_shadow_layer_conceals_hides_shadow_under_shape},
      {"drop_shadow_conceals_composes_with_stroke_knockout",
       drop_shadow_conceals_composes_with_stroke_knockout},
      {"layer_stroke_knockout_blend_mode_uses_backdrop", layer_stroke_knockout_blend_mode_uses_backdrop},
      {"psd_photoshop_stroke_partial_alpha_fixture_matches",
       psd_photoshop_stroke_partial_alpha_fixture_matches},
      {"psd_photoshop_stroke_positions_fixture_matches",
       psd_photoshop_stroke_positions_fixture_matches},
  };
}
