#include <string_view>
#include "core/document.hpp"
#include "core/layer_metadata.hpp"
#include "formats/acv_curves_io.hpp"
#include "formats/bmp_document_io.hpp"
#include "formats/aseprite_document_io.hpp"
#include "formats/ico_document_io.hpp"
#include "formats/ilbm_document_io.hpp"
#include "formats/pcx_document_io.hpp"
#include "formats/tga_document_io.hpp"
#include "psd/psd_document_io.hpp"
#include "render/compositor.hpp"
#include "render/layer_compositor.hpp"
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
using patchy::test::rgb_diff_metrics;
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

void psd_vector_mask_hides_effects_round_trip() {
  // "Vector Mask Hides Effects" ('vmgm'). Photoshop 2026 writes the block only
  // when the option is on, so absence is the off state on both sides.
  for (const auto hides : {false, true}) {
    patchy::Document document(4, 2, patchy::PixelFormat::rgb8());
    auto& layer = document.add_pixel_layer("Styled", solid_rgba(4, 2, 10, 20, 30, 255));
    patchy::LayerDropShadow shadow;
    shadow.enabled = true;
    layer.layer_style().drop_shadows.push_back(shadow);
    layer.layer_style().vector_mask_hides_effects = hides;

    const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
    const auto payload = psd_layer_block_payload(psd_first_layer_extra_data(bytes), "vmgm");
    CHECK(payload.has_value() == hides);
    if (payload.has_value()) {
      CHECK(payload->size() == 4U);
      CHECK((*payload)[0] == 1U);
    }

    const auto read = patchy::psd::DocumentIo::read(bytes);
    CHECK(read.layers().size() == 1);
    CHECK(read.layers().front().layer_style().vector_mask_hides_effects == hides);
    CHECK(!read.layers().front().layer_style().layer_mask_hides_effects);
  }
}

void psd_photoshop_vector_mask_hides_effects_fixture_matches_render() {
  // Photoshop 2026 authored photoshop-vector-mask-hides-effects.psd/.bmp via
  // COM (October 2026, GitHub issue 191). Four 200 px arms, each a red square
  // (cell x 70..130, y 40..100) with a 5 px Outside stroke and a hard drop
  // shadow 15 px to the right; the vector mask hides cell x >= 100 and, in
  // arms 3 and 4, a raster mask hides y >= 70. Each mask either shapes the
  // effects (its "Hides Effects" option off: the stroke follows the cut and
  // the shadow spills past it) or hides their output (option on: no stroke at
  // the cut, no shadow beyond it), independently of the other mask.
  const auto psd_path = patchy::test::committed_psd_fixture_path("photoshop-vector-mask-hides-effects.psd");
  const auto bmp_path = psd_path.parent_path() / "photoshop-vector-mask-hides-effects.bmp";
  CHECK(std::filesystem::exists(psd_path));
  CHECK(std::filesystem::exists(bmp_path));
  const auto document = patchy::psd::DocumentIo::read_file(psd_path);
  struct Arm {
    const char* name;
    bool vector_hides;
    bool layer_hides;
  };
  for (const auto& arm : {Arm{"vmgm off", false, false}, Arm{"vmgm on", true, false},
                          Arm{"vmgm on lmgm off", true, false}, Arm{"vmgm off lmgm on", false, true}}) {
    const auto* layer = find_layer_named(document.layers(), arm.name);
    CHECK(layer != nullptr);
    if (layer != nullptr) {
      CHECK(layer->vector_mask() != nullptr);
      CHECK(layer->layer_style().vector_mask_hides_effects == arm.vector_hides);
      CHECK(layer->layer_style().layer_mask_hides_effects == arm.layer_hides);
    }
  }
  const auto reference = patchy::Compositor{}.flatten_rgb8(patchy::bmp::DocumentIo::read_file(bmp_path));
  const auto flat = patchy::Compositor{}.flatten_rgb8(document);
  struct Probe {
    int x;
    int y;
    int red;
    int green;
    int blue;
  };
  const Probe probes[] = {
      {110, 70, 0, 0, 0},        // arm 1: the shadow spills past the vector cut
      {102, 70, 0, 200, 0},      // arm 1: the stroke follows the vector cut
      {310, 70, 255, 255, 255},  // arm 2: no shadow beyond the cut
      {302, 70, 255, 255, 255},  // arm 2: no stroke along the cut
      {267, 70, 0, 200, 0},      // arm 2: the stroke keeps the square's left edge
      {480, 72, 0, 200, 0},      // arm 3: the raster mask still shapes the stroke
      {502, 50, 255, 255, 255},  // arm 3: the vector mask hides the right side
      {710, 50, 0, 0, 0},        // arm 4: the vector cut casts a shadow
      {702, 50, 0, 200, 0},      // arm 4: and gets a stroke
      {680, 72, 255, 255, 255},  // arm 4: the raster mask hides everything below
      {710, 80, 255, 255, 255},  // arm 4: shadow included
  };
  for (const auto& probe : probes) {
    const auto* pixel = flat.pixel(probe.x, probe.y);
    CHECK(std::abs(static_cast<int>(pixel[0]) - probe.red) <= 2);
    CHECK(std::abs(static_cast<int>(pixel[1]) - probe.green) <= 2);
    CHECK(std::abs(static_cast<int>(pixel[2]) - probe.blue) <= 2);
  }
  const auto metrics = rgb_diff_metrics(reference, flat);
  std::cout << "vector mask hides effects fixture: max delta " << metrics.max_channel_delta << ", mean "
            << metrics.mean_abs_channel_delta << '\n';
  CHECK(metrics.max_channel_delta <= 160);  // anti-aliased stroke corners
  CHECK(metrics.mean_abs_channel_delta <= 0.25);
}

void layer_knockout_setting_patches_the_native_block() {
  // Knockout lives in the native 'knko' block the compositor renders from
  // (GitHub issue 191). Photoshop writes one on every layer (None included),
  // so an edit patches the mode byte in place; a layer without the block gets
  // one only for a non-None mode, keeping default output byte-identical.
  patchy::Document document(4, 2, patchy::PixelFormat::rgb8());
  auto& fresh = document.add_pixel_layer("Fresh", solid_rgba(4, 2, 10, 20, 30, 255));
  CHECK(patchy::layer_knockout_setting(fresh) == std::optional<std::uint8_t>{std::uint8_t{0}});
  patchy::set_layer_knockout(fresh, 0);
  CHECK(std::as_const(fresh).unknown_psd_blocks().empty());
  patchy::set_layer_knockout(fresh, 2);
  CHECK(patchy::layer_knockout_setting(fresh) == std::optional<std::uint8_t>{std::uint8_t{2}});
  CHECK(std::as_const(fresh).unknown_psd_blocks().size() == 1U);

  auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  auto payload = psd_layer_block_payload(psd_first_layer_extra_data(bytes), "knko");
  CHECK(payload.has_value());
  if (payload.has_value()) {
    CHECK((*payload == std::vector<std::uint8_t>{2, 0, 0, 0}));
  }
  auto read = patchy::psd::DocumentIo::read(bytes);
  CHECK(patchy::layer_knockout_setting(read.layers().front()) == std::optional<std::uint8_t>{std::uint8_t{2}});

  // A Photoshop-style None block between other blocks is patched where it sits.
  patchy::Document imported(4, 2, patchy::PixelFormat::rgb8());
  auto& layer = imported.add_pixel_layer("Imported", solid_rgba(4, 2, 10, 20, 30, 255));
  layer.unknown_psd_blocks().push_back({"tsly", {0, 0, 0, 0}});
  layer.unknown_psd_blocks().push_back({"knko", {0, 0, 0, 0}});
  layer.unknown_psd_blocks().push_back({"lyid", {0, 0, 0, 9}});
  patchy::set_layer_knockout(layer, 1);
  const auto& blocks = std::as_const(layer).unknown_psd_blocks();
  CHECK(blocks.size() == 3U);
  CHECK(blocks[1].key == "knko");
  CHECK((blocks[1].payload == std::vector<std::uint8_t>{1, 0, 0, 0}));
  const auto revision = std::as_const(layer).content_revision();
  patchy::set_layer_knockout(layer, 1);  // unchanged: no revision bump
  CHECK(std::as_const(layer).content_revision() == revision);

  // A payload Patchy cannot interpret stays raw and is not editable.
  auto& unknown = imported.add_pixel_layer("Unknown", solid_rgba(4, 2, 10, 20, 30, 255));
  unknown.unknown_psd_blocks().push_back({"knko", {9, 0, 0, 0}});
  CHECK(!patchy::layer_knockout_setting(unknown).has_value());
  patchy::set_layer_knockout(unknown, 2);
  CHECK((std::as_const(unknown).unknown_psd_blocks().front().payload == std::vector<std::uint8_t>{9, 0, 0, 0}));
}

void psd_transparency_shapes_layer_block_survives_style_edits() {
  // "Transparency Shapes Layer" ('tsly', Photoshop writes it only when off)
  // is not rendered yet (GitHub issue 191), but a file that has it must keep
  // it: an effects edit regenerates lfx2 and must not drop the block.
  patchy::Document document(4, 2, patchy::PixelFormat::rgb8());
  auto& layer = document.add_pixel_layer("Shapes Off", solid_rgba(4, 2, 10, 20, 30, 255));
  layer.unknown_psd_blocks().push_back({"tsly", {0, 0, 0, 0}});
  auto read = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(document));
  CHECK(read.layers().size() == 1U);
  patchy::LayerColorOverlay overlay;
  overlay.enabled = true;
  read.layers().front().layer_style().color_overlays.push_back(overlay);
  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(read);
  const auto payload = psd_layer_block_payload(psd_first_layer_extra_data(bytes), "tsly");
  CHECK(payload.has_value());
  if (payload.has_value()) {
    CHECK((*payload == std::vector<std::uint8_t>{0, 0, 0, 0}));
  }
  CHECK(psd_layer_block_payload(psd_first_layer_extra_data(bytes), "lfx2").has_value());
}

void psd_blend_interior_elements_round_trip() {
  // "Blend Interior Effects as Group" ('infx'). Photoshop's default is off and
  // it writes no block for that, so absence is the off state on both sides.
  for (const auto grouped : {false, true}) {
    patchy::Document document(4, 2, patchy::PixelFormat::rgb8());
    auto& layer = document.add_pixel_layer("Styled", solid_rgba(4, 2, 10, 20, 30, 255));
    patchy::LayerColorOverlay overlay;
    overlay.enabled = true;
    layer.layer_style().color_overlays.push_back(overlay);
    layer.layer_style().blend_interior_elements = grouped;

    const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
    const auto payload = psd_layer_block_payload(psd_first_layer_extra_data(bytes), "infx");
    CHECK(payload.has_value() == grouped);
    if (payload.has_value()) {
      CHECK(payload->size() == 4U);
      CHECK((*payload)[0] == 1U);
    }

    const auto read = patchy::psd::DocumentIo::read(bytes);
    CHECK(read.layers().size() == 1);
    CHECK(read.layers().front().layer_style().blend_interior_elements == grouped);
  }
}

void psd_blend_clipped_elements_round_trip() {
  // "Blend Clipped Layers as Group" ('clbl'). Photoshop's default is on and it
  // reads absence as on, so only the off state writes a block; an imported
  // explicit block is regenerated from the model rather than re-emitted raw.
  for (const auto grouped : {true, false}) {
    patchy::Document document(4, 2, patchy::PixelFormat::rgb8());
    auto& layer = document.add_pixel_layer("Styled", solid_rgba(4, 2, 10, 20, 30, 255));
    layer.layer_style().blend_clipped_elements = grouped;

    const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
    const auto payload = psd_layer_block_payload(psd_first_layer_extra_data(bytes), "clbl");
    CHECK(payload.has_value() == !grouped);
    if (payload.has_value()) {
      CHECK(payload->size() == 4U);
      CHECK((*payload)[0] == 0U);
    }

    const auto read = patchy::psd::DocumentIo::read(bytes);
    CHECK(read.layers().size() == 1);
    CHECK(read.layers().front().layer_style().blend_clipped_elements == grouped);
  }
}

void psd_channel_restrictions_round_trip() {
  // Advanced Blending "Channels" ('brst'): ascending big-endian u32 indices of
  // the EXCLUDED channels, written only when at least one channel is
  // restricted (Photoshop 2026 omits the block otherwise).
  const std::array<std::uint8_t, 5> masks{
      0U, patchy::kRestrictRed, patchy::kRestrictGreen,
      static_cast<std::uint8_t>(patchy::kRestrictRed | patchy::kRestrictBlue),
      patchy::kRestrictAllChannels};
  for (const auto mask : masks) {
    patchy::Document document(4, 2, patchy::PixelFormat::rgb8());
    auto& layer = document.add_pixel_layer("Restricted", solid_rgba(4, 2, 10, 20, 30, 255));
    layer.set_restricted_channels(mask);

    const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
    const auto payload = psd_layer_block_payload(psd_first_layer_extra_data(bytes), "brst");
    CHECK(payload.has_value() == (mask != 0U));
    if (payload.has_value()) {
      std::vector<std::uint8_t> expected;
      for (std::uint8_t index = 0; index < 3U; ++index) {
        if (((mask >> index) & 1U) != 0U) {
          expected.push_back(0U);
          expected.push_back(0U);
          expected.push_back(0U);
          expected.push_back(index);
        }
      }
      CHECK(*payload == expected);
    }

    const auto read = patchy::psd::DocumentIo::read(bytes);
    CHECK(read.layers().size() == 1);
    CHECK(read.layers().front().restricted_channels() == mask);
    CHECK(read.layers().front().channel_restriction_supported());
  }
}

void psd_channel_restrictions_unsupported_payload_preserved() {
  // Channel indices past Blue (non-RGB sources) and payloads whose length is
  // not a multiple of four cannot map onto the RGB model: they import as
  // preserved raw blocks, render nothing, and re-emit byte-identically with no
  // authored duplicate.
  const std::vector<std::vector<std::uint8_t>> payloads{
      {0x00, 0x00, 0x00, 0x05},
      {0x00, 0x00, 0x00, 0x01, 0x00, 0x00},
  };
  for (const auto& exotic : payloads) {
    patchy::Document document(4, 2, patchy::PixelFormat::rgb8());
    auto& layer = document.add_pixel_layer("Exotic", solid_rgba(4, 2, 10, 20, 30, 255));
    layer.unknown_psd_blocks().push_back(patchy::UnknownPsdBlock{"brst", exotic, false});
    layer.set_channel_restriction_unsupported();

    const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
    const auto payload = psd_layer_block_payload(psd_first_layer_extra_data(bytes), "brst");
    CHECK(payload.has_value());
    CHECK(*payload == exotic);

    const auto read = patchy::psd::DocumentIo::read(bytes);
    CHECK(read.layers().size() == 1);
    CHECK(!read.layers().front().channel_restriction_supported());
    CHECK(read.layers().front().restricted_channels() == 0U);

    const auto resaved = patchy::psd::DocumentIo::write_layered_rgb8(read);
    const auto extra = psd_first_layer_extra_data(resaved);
    const auto resaved_payload = psd_layer_block_payload(extra, "brst");
    CHECK(resaved_payload.has_value());
    CHECK(*resaved_payload == exotic);
    // Exactly one block: the preserved original, never an authored twin.
    std::size_t brst_blocks = 0;
    for (std::size_t index = 0; index + 8U <= extra.size(); ++index) {
      if (extra[index] == '8' && extra[index + 1] == 'B' && extra[index + 2] == 'I' &&
          extra[index + 3] == 'M' && extra[index + 4] == 'b' && extra[index + 5] == 'r' &&
          extra[index + 6] == 's' && extra[index + 7] == 't') {
        ++brst_blocks;
      }
    }
    CHECK(brst_blocks == 1U);
  }
}

void blend_interior_elements_moves_the_layer_blend_over_the_overlay() {
  // Photoshop's default ('infx' off) applies the layer's blend mode to its own
  // pixels only; the interior overlay then blends over that result with its own
  // mode. Turning the option on folds the overlay into the layer color first,
  // so the layer's mode carries it as well. Multiply over a mid backdrop makes
  // the two readings unmistakable: off, an opaque Normal overlay simply wins;
  // on, it is multiplied down by the backdrop.
  for (const auto grouped : {false, true}) {
    patchy::Document document(4, 4, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Base", solid_rgb(4, 4, 200, 100, 50));
    auto& layer = document.add_pixel_layer("Panel", solid_rgba(4, 4, 255, 64, 32, 255));
    layer.set_blend_mode(patchy::BlendMode::Multiply);
    patchy::LayerColorOverlay overlay;
    overlay.enabled = true;
    overlay.blend_mode = patchy::BlendMode::Normal;
    overlay.color = patchy::RgbColor{0, 0, 255};
    overlay.opacity = 1.0F;
    layer.layer_style().color_overlays.push_back(overlay);
    layer.layer_style().blend_interior_elements = grouped;

    const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
    const auto* pixel = flattened.pixel(1, 1);
    // Photoshop 2026 COM probes h_mult_group0 / i_mult_group1.
    const std::array<int, 3> expected = grouped ? std::array<int, 3>{0, 0, 50}
                                                : std::array<int, 3>{0, 0, 255};
    CHECK(std::abs(static_cast<int>(pixel[0]) - expected[0]) <= 1);
    CHECK(std::abs(static_cast<int>(pixel[1]) - expected[1]) <= 1);
    CHECK(std::abs(static_cast<int>(pixel[2]) - expected[2]) <= 1);
  }
}

void exterior_effect_is_knocked_out_once_by_semi_transparent_content() {
  // Photoshop contributes a layer and its exterior effects additively against
  // the backdrop they both met: a half-alpha square renders its outer glow at
  // the glow's own coverage AND itself at half coverage. Pre-multiplying the
  // glow by (1 - alpha) and then compositing the square over it charged that
  // factor twice, which thinned every glow and shadow along anti-aliased text
  // edges (the DungeonScroll options screen report).
  // A 12x12 square inside a 32x32 canvas, matching the COM probe geometry so
  // the size-5 glow saturates over the shape.
  const auto square = [](std::uint8_t alpha) {
    patchy::PixelBuffer pixels(32, 32, patchy::PixelFormat::rgba8());
    for (std::int32_t y = 0; y < 32; ++y) {
      for (std::int32_t x = 0; x < 32; ++x) {
        auto* px = pixels.pixel(x, y);
        const auto inside = x >= 10 && x < 22 && y >= 10 && y < 22;
        px[0] = 0;
        px[1] = 0;
        px[2] = 255;
        px[3] = inside ? alpha : 0U;
      }
    }
    return pixels;
  };

  patchy::Document document(32, 32, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Base", solid_rgb(32, 32, 128, 128, 128));
  auto& layer = document.add_pixel_layer("Panel", square(128));
  patchy::LayerOuterGlow glow;
  glow.enabled = true;
  glow.blend_mode = patchy::BlendMode::Normal;
  glow.color = patchy::RgbColor{255, 0, 0};
  glow.opacity = 1.0F;
  glow.size = 5.0F;
  glow.range = 50.0F;
  layer.layer_style().outer_glows.push_back(glow);

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  const auto* pixel = flattened.pixel(16, 16);
  // Half the blue square over a fully glowing backdrop: (127, 0, 128), the
  // Photoshop 2026 COM probe x2_glow_a50. The doubled knockout gave (95, 32, 160).
  CHECK(std::abs(static_cast<int>(pixel[0]) - 127) <= 1);
  CHECK(std::abs(static_cast<int>(pixel[1]) - 0) <= 1);
  CHECK(std::abs(static_cast<int>(pixel[2]) - 128) <= 1);

  // Fill 0 removes the square's pixels but not the knockout: the shape still
  // hides its own glow, so the pure backdrop remains (probe x7_glow_a100_fill0).
  layer.pixels() = square(255);
  layer.set_fill_opacity(0.0F);
  const auto hidden = patchy::Compositor{}.flatten_rgb8(document);
  const auto* hidden_pixel = hidden.pixel(16, 16);
  CHECK(hidden_pixel[0] == 128 && hidden_pixel[1] == 128 && hidden_pixel[2] == 128);
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
  // Content under a UNIFORM 50% gray layer mask: the mask value is part of the
  // content's alpha, so the semi-transparent rules apply (Photoshop 2026 COM
  // probe, October 2026, local-test-fixtures/stroke-alpha-probes/mask50_out100):
  // the interior reads half content, half stroke, (137, 45, 100) for this blue
  // under a red Outside stroke, and the band outside paints at full strength
  // along the raw-pixel contour (the gray mask neither moves nor attenuates it).
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
  const auto* interior = flattened.pixel(32, 32);
  CHECK(std::abs(interior[0] - 137) <= 3 && std::abs(interior[1] - 45) <= 3 && std::abs(interior[2] - 100) <= 3);
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
  // painted pixel as inside the stroked shape: the stroke fills the shape beneath
  // the content, which covers it by its own alpha (the Photoshop 2026 render,
  // re-sampled via COM in October 2026: the 50% region is exactly half content
  // (200, 30, 30) and half green, (100, 142, 15); the 25% region (50, 199, 8); no
  // backdrop shows through either). The opaque region stays clean.
  const auto document = patchy::psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path("photoshop-stroke-partial-alpha.psd"));
  const auto rendered = patchy::Compositor{}.flatten_rgb8(document);
  const auto expect = [&](int x, int y, int r, int g, int b, int tolerance) {
    const auto* px = rendered.pixel(x, y);
    CHECK(std::abs(px[0] - r) <= tolerance && std::abs(px[1] - g) <= tolerance && std::abs(px[2] - b) <= tolerance);
  };
  expect(16, 24, 200, 30, 30, 2);   // 100% region: pure content, no stroke
  expect(44, 24, 100, 142, 15, 3);  // 50% region: half content, half stroke
  expect(32, 46, 50, 199, 8, 3);    // 25% region: a quarter content, the rest stroke
  expect(6, 24, 0, 255, 0, 2);      // outer band at full strength
  expect(32, 53, 0, 255, 0, 2);     // the band below the 25% strip is full too
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

// Computes the raw stroke band mask for `pixels` the way prepare_stroke_render
// does (no layer mask, mask shapes the source).
std::vector<float> stroke_mask_for_pixels(const patchy::PixelBuffer& pixels, patchy::Rect bounds,
                                          patchy::Rect mask_bounds, float size,
                                          patchy::LayerStrokePosition position) {
  patchy::Document document(1, 1, patchy::PixelFormat::rgb8());
  patchy::Layer layer(document.allocate_layer_id(), "Stroke source", solid_rgba(1, 1, 0, 0, 0, 0));
  layer.set_bounds(bounds);
  return patchy::render_detail::stroke_alpha_mask(pixels, layer, bounds, mask_bounds, size, position,
                                                  std::nullopt, true, nullptr);
}

// Largest Euclidean distance from any half-or-more-covered stroke band pixel
// to the nearest half-or-more-covered matte pixel.
float max_stroke_distance_from_solid(const std::vector<float>& mask, const patchy::PixelBuffer& pixels,
                                     patchy::Rect bounds, patchy::Rect mask_bounds) {
  std::vector<float> solid(mask.size(), 0.0F);
  for (std::int32_t y = 0; y < mask_bounds.height; ++y) {
    for (std::int32_t x = 0; x < mask_bounds.width; ++x) {
      const auto source_x = mask_bounds.x + x - bounds.x;
      const auto source_y = mask_bounds.y + y - bounds.y;
      if (source_x < 0 || source_y < 0 || source_x >= pixels.width() || source_y >= pixels.height()) {
        continue;
      }
      if (pixels.pixel(source_x, source_y)[3] >= 128) {
        solid[static_cast<std::size_t>(y) * static_cast<std::size_t>(mask_bounds.width) +
              static_cast<std::size_t>(x)] = 1.0F;
      }
    }
  }
  const auto distances = patchy::render_detail::stroke_distance_field(solid, mask_bounds.width,
                                                                      mask_bounds.height, true);
  float worst = 0.0F;
  for (std::size_t index = 0; index < mask.size(); ++index) {
    if (mask[index] >= 0.5F) {
      worst = std::max(worst, distances[index]);
    }
  }
  return worst;
}

void layer_stroke_aa_fringe_tip_bands_like_photoshop() {
  // The bad_stroke.psd layout (July 2026): smooth-AA text carries fringe
  // pixels sitting sqrt(5) px from the nearest half-covered pixel center.
  // Photoshop bands such a pixel like any painted pixel (October 2026 probes,
  // local-test-fixtures/stroke-soft-probes/lattice-merged.png, the exact
  // layout below with a 2 px Outside stroke): its 4-neighbours take the full
  // band, the diagonals clamp(band + a - sqrt 2) and the ring at distance 2
  // reads the pixel's own alpha, so the "nub" is a faint diamond, never a
  // square size+1 block and never nothing.
  constexpr std::int32_t kSide = 48;
  auto pixels = solid_rgba(kSide, kSide, 0, 0, 255, 0);
  for (std::int32_t y = 0; y < kSide; ++y) {
    for (std::int32_t x = 0; x < kSide; ++x) {
      if (y >= 21 || (y == 20 && x >= 15)) {
        pixels.pixel(x, y)[3] = 255;  // solid shape with a step at (15, 20)
      } else if (y == 19 || y == 20) {
        pixels.pixel(x, y)[3] = 125;  // just-under-half AA fringe
      }
    }
  }
  pixels.pixel(14, 18)[3] = 51;  // the fringe tip (alpha 0.2)
  const auto bounds = patchy::Rect{0, 0, kSide, kSide};
  const auto mask_bounds = patchy::outset_rect(bounds, 3);
  const auto mask =
      stroke_mask_for_pixels(pixels, bounds, mask_bounds, 2.0F, patchy::LayerStrokePosition::Outside);
  const auto at = [&](std::int32_t x, std::int32_t y) {
    return mask[static_cast<std::size_t>(y - mask_bounds.y) * static_cast<std::size_t>(mask_bounds.width) +
                static_cast<std::size_t>(x - mask_bounds.x)];
  };
  // Photoshop: (14,16) = 51, (13,17) = (15,17) = 201, (14,17) = 255, rows 18 and
  // 19 full (the 125 fringe row carries the stroke under itself: 125 + 130).
  CHECK(std::abs(at(14, 16) - 51.0F / 255.0F) < 0.02F);
  CHECK(std::abs(at(13, 17) - 201.0F / 255.0F) < 0.02F);
  CHECK(std::abs(at(15, 17) - 201.0F / 255.0F) < 0.02F);
  CHECK(at(14, 17) > 0.99F);
  CHECK(at(25, 18) > 0.99F);                                 // 1 px above the 125 fringe row
  CHECK(std::abs(at(25, 17) - 125.0F / 255.0F) < 0.02F);    // the 2 px ring reads the fringe's 125
  CHECK(at(25, 16) < 0.01F);                                 // 3 px: nothing
}

void layer_stroke_flat_wash_far_from_solid_still_strokes_in_full() {
  // Painted regions that never reach half coverage still stroke as solid
  // shapes (the pinned photoshop-stroke-partial-alpha semantics): the band
  // measures from every painted pixel, full inside the band and the last
  // ring at the wash's own alpha (Photoshop 2026 probes of a flat 50% block,
  // local-test-fixtures/stroke-soft-probes/lone-merged.png).
  constexpr std::int32_t kSide = 48;
  auto pixels = solid_rgba(kSide, kSide, 0, 0, 255, 0);
  for (std::int32_t y = 4; y < 20; ++y) {
    for (std::int32_t x = 8; x < 24; ++x) {
      pixels.pixel(x, y)[3] = 255;  // solid square
    }
  }
  for (std::int32_t y = 22; y < 44; ++y) {
    for (std::int32_t x = 4; x < 44; ++x) {
      pixels.pixel(x, y)[3] = 64;  // flat 25% wash, top row 3 px from the square
    }
  }
  const auto bounds = patchy::Rect{0, 0, kSide, kSide};
  const auto mask_bounds = patchy::outset_rect(bounds, 4);
  const auto mask =
      stroke_mask_for_pixels(pixels, bounds, mask_bounds, 3.0F, patchy::LayerStrokePosition::Outside);
  const auto at = [&](std::int32_t x, std::int32_t y) {
    return mask[static_cast<std::size_t>(y - mask_bounds.y) * static_cast<std::size_t>(mask_bounds.width) +
                static_cast<std::size_t>(x - mask_bounds.x)];
  };
  CHECK(at(32, 33) > 0.70F);  // wash interior: stroke fills the shape and the
  CHECK(at(32, 33) < 0.80F);  // 25% content covers it
  CHECK(at(24, 45) > 0.9F);   // full band on clean ground below the wash (2 px out)
  CHECK(std::abs(at(24, 46) - 0.25F) < 0.02F);  // the 3 px ring reads the wash's 25%
  CHECK(at(16, 1) > 0.9F);    // solid square's own band unaffected
}

void layer_stroke_transformed_corner_tip_bands_like_photoshop() {
  // Alpha block lifted verbatim from bad_stroke_transformed.psd around one of
  // its glyph corners: transform re-rasterization stretches the corner ramp
  // so a 6/255 tip sits sqrt(8) from every half-covered pixel center.
  // Photoshop bands the tip like any painted pixel (lone-pixel probes,
  // October 2026): full at 1 px, clamp(band + a - sqrt 2) diagonally and the
  // tip's own alpha on the ring, a faint diamond past the glyph.
  constexpr std::int32_t kSide = 48;
  constexpr std::array<std::array<std::uint8_t, 9>, 5> kCornerRows{{
      {44, 44, 40, 21, 6, 0, 0, 0, 0},
      {135, 136, 123, 63, 18, 0, 0, 0, 0},
      {241, 241, 218, 110, 31, 0, 0, 0, 0},
      {255, 255, 229, 112, 32, 0, 0, 0, 0},
      {255, 255, 229, 108, 30, 0, 0, 0, 0},
  }};
  auto pixels = solid_rgba(kSide, kSide, 0, 0, 255, 0);
  constexpr std::int32_t kTipX = 30;
  constexpr std::int32_t kTipY = 10;
  for (std::size_t row = 0; row < kCornerRows.size(); ++row) {
    for (std::size_t column = 0; column < kCornerRows[row].size(); ++column) {
      pixels.pixel(kTipX - 4 + static_cast<std::int32_t>(column),
                   kTipY + static_cast<std::int32_t>(row))[3] = kCornerRows[row][column];
    }
  }
  const auto bounds = patchy::Rect{0, 0, kSide, kSide};
  const auto mask_bounds = patchy::outset_rect(bounds, 3);
  const auto mask =
      stroke_mask_for_pixels(pixels, bounds, mask_bounds, 2.0F, patchy::LayerStrokePosition::Outside);
  const auto at = [&](std::int32_t x, std::int32_t y) {
    return mask[static_cast<std::size_t>(y - mask_bounds.y) * static_cast<std::size_t>(mask_bounds.width) +
                static_cast<std::size_t>(x - mask_bounds.x)];
  };
  CHECK(at(kTipX, kTipY - 1) > 0.99F);                           // 1 px above the 6/255 tip
  CHECK(std::abs(at(kTipX, kTipY - 2) - 6.0F / 255.0F) < 0.02F);  // the ring reads the tip
  CHECK(at(kTipX + 2, kTipY) < 0.05F);                           // nothing 2 px right of it
  CHECK(at(kTipX + 1, kTipY) > 0.99F);                           // 1 px right: full
  // The whole band stays within band + sqrt(8) + sqrt(2) of the half-covered matte.
  CHECK(max_stroke_distance_from_solid(mask, pixels, bounds, mask_bounds) <= 4.3F);
}

void psd_bad_stroke_text_has_no_stroke_nubs_if_available() {
  // The original report files: 119pt smooth-AA text with a 2px outside
  // stroke, plain and free-transformed. The July 2026 promotion rule turned
  // lattice-phase fringe pixels (and, on the transformed raster, stretched
  // glyph-corner ramp tips) into isolated solid islands and the band bloomed a
  // square size+1 nub around each one. Photoshop itself bands a faint tip only
  // to its 4-neighbours at full strength (a diamond that fades with the tip's
  // alpha), so the band stays within size + sqrt(8) + sqrt(2) of the
  // half-covered matte.
  int checked = 0;
  for (const auto* name : {"bad_stroke.psd", "bad_stroke_transformed.psd"}) {
    const auto path = patchy::test::local_psd_fixture_path(name);
    if (!std::filesystem::exists(path)) {
      std::cout << "  note: optional fixture missing: " << patchy::test::fixture_display_path(path) << '\n';
      continue;
    }
    ++checked;
    const auto document = patchy::psd::DocumentIo::read_file(path);
    const patchy::Layer* stroked = nullptr;
    for (const auto& layer : document.layers()) {
      if (!layer.layer_style().strokes.empty() && layer.layer_style().strokes.front().enabled &&
          !layer.pixels().empty()) {
        stroked = &layer;
        break;
      }
    }
    CHECK(stroked != nullptr);
    if (stroked == nullptr) {
      continue;
    }
    const auto& stroke = stroked->layer_style().strokes.front();
    CHECK(stroke.position == patchy::LayerStrokePosition::Outside);
    const auto bounds = stroked->bounds();
    const auto radius = std::max(1, static_cast<int>(std::ceil(stroke.size)));
    const auto mask_bounds = patchy::outset_rect(bounds, radius + 1);
    const auto mask = patchy::render_detail::stroke_alpha_mask(
        stroked->pixels(), *stroked, bounds, mask_bounds, stroke.size, stroke.position, std::nullopt,
        !stroked->layer_style().layer_mask_hides_effects, nullptr);
    const auto worst = max_stroke_distance_from_solid(mask, stroked->pixels(), bounds, mask_bounds);
    std::cout << name << " worst band distance from solid: " << worst << " (limit "
              << stroke.size + 2.3F << ")\n";
    CHECK(worst <= stroke.size + 2.3F);
  }
  if (checked == 0) {
    patchy::test::skip("no optional fixture present (bad_stroke.psd, bad_stroke_transformed.psd)");
  }
}

}  // namespace

std::vector<patchy::test::TestCase> stroke_mask_effects_tests() {
  return {
      {"layer_mask_shapes_effects_regardless_of_link", layer_mask_shapes_effects_regardless_of_link},
      {"psd_layer_mask_hides_effects_round_trip", psd_layer_mask_hides_effects_round_trip},
      {"psd_vector_mask_hides_effects_round_trip", psd_vector_mask_hides_effects_round_trip},
      {"psd_photoshop_vector_mask_hides_effects_fixture_matches_render",
       psd_photoshop_vector_mask_hides_effects_fixture_matches_render},
      {"layer_knockout_setting_patches_the_native_block", layer_knockout_setting_patches_the_native_block},
      {"psd_transparency_shapes_layer_block_survives_style_edits",
       psd_transparency_shapes_layer_block_survives_style_edits},
      {"psd_blend_interior_elements_round_trip", psd_blend_interior_elements_round_trip},
      {"psd_blend_clipped_elements_round_trip", psd_blend_clipped_elements_round_trip},
      {"psd_channel_restrictions_round_trip", psd_channel_restrictions_round_trip},
      {"psd_channel_restrictions_unsupported_payload_preserved",
       psd_channel_restrictions_unsupported_payload_preserved},
      {"blend_interior_elements_moves_the_layer_blend_over_the_overlay",
       blend_interior_elements_moves_the_layer_blend_over_the_overlay},
      {"exterior_effect_is_knocked_out_once_by_semi_transparent_content",
       exterior_effect_is_knocked_out_once_by_semi_transparent_content},
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
      {"layer_stroke_aa_fringe_tip_bands_like_photoshop",
       layer_stroke_aa_fringe_tip_bands_like_photoshop},
      {"layer_stroke_flat_wash_far_from_solid_still_strokes_in_full",
       layer_stroke_flat_wash_far_from_solid_still_strokes_in_full},
      {"layer_stroke_transformed_corner_tip_bands_like_photoshop",
       layer_stroke_transformed_corner_tip_bands_like_photoshop},
      {"psd_bad_stroke_text_has_no_stroke_nubs_if_available",
       psd_bad_stroke_text_has_no_stroke_nubs_if_available},
      {"psd_photoshop_stroke_partial_alpha_fixture_matches",
       psd_photoshop_stroke_partial_alpha_fixture_matches},
      {"psd_photoshop_stroke_positions_fixture_matches",
       psd_photoshop_stroke_positions_fixture_matches},
  };
}
