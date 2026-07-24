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

using patchy::test::PsdLayerChannelRecord;
using patchy::test::arrows_fixture_path;
using patchy::test::find_layer_named;
using patchy::test::psd_first_layer_extra_data;
using patchy::test::psd_layer_block_payload;
using patchy::test::psd_layer_channel_records;
using patchy::test::psd_layer_extra_data;
using patchy::test::solid_rgb;
using patchy::test::solid_rgba;

void psb_write_accepts_over_30k_dimension_psd_rejects() {
  patchy::Document document(30001, 1, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Wide", solid_rgb(30001, 1, 5, 6, 7));
  bool psd_threw = false;
  try {
    (void)patchy::psd::DocumentIo::write_layered_rgb8(document);
  } catch (const std::exception&) {
    psd_threw = true;
  }
  CHECK(psd_threw);  // over the 30k PSD cap: the writer must direct users to .psb

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document, patchy::psd::WriteOptions{true});
  const auto read = patchy::psd::DocumentIo::read(bytes);
  CHECK(read.width() == 30001);
  CHECK(read.layers().size() == 1);
  const auto* px = read.layers().front().pixels().pixel(30000, 0);
  CHECK(px[0] == 5 && px[1] == 6 && px[2] == 7);
}

void psd_layered_writer_bytes_are_stable() {
  // PSB support threads a large_document flag through every PSD length/RLE write
  // site; this FNV-1a pin proves the default PSD path emits the exact same bytes.
  // Re-pin only for deliberate format changes (the failure output prints the hash).
  patchy::Document document(8, 6, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Base", solid_rgb(8, 6, 250, 240, 20));
  patchy::Layer top(document.allocate_layer_id(), "Top", solid_rgba(4, 3, 10, 20, 30, 200));
  top.set_bounds(patchy::Rect{2, 1, 4, 3});
  top.set_opacity(0.5F);
  top.unknown_psd_blocks().push_back(patchy::UnknownPsdBlock{"fxrp", std::vector<std::uint8_t>(16, 3)});
  document.add_layer(std::move(top));
  document.metadata().unknown_psd_resources.push_back(
      patchy::UnknownPsdBlock{"lnk2", {9, 9, 9, 9, 9, 9, 9, 9}});
  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  std::uint64_t hash = 1469598103934665603ULL;
  for (const auto byte : bytes) {
    hash ^= byte;
    hash *= 1099511628211ULL;
  }
  constexpr std::uint64_t kExpected = 0xe7ed1d1689e2c94dULL;
  if (hash != kExpected) {
    std::cout << "psd layered writer hash: 0x" << std::hex << hash << std::dec << " size " << bytes.size() << "\n";
  }
  CHECK(hash == kExpected);
}

void psd_layered_write_keeps_merged_transparency_in_composite() {
  // Regression for the July 2026 "smart object turns transparent parts black" bug: the
  // layered writer matted the merged composite onto black and dropped its alpha, so any
  // reader trusting the stored composite (the smart-object preview decode, external
  // thumbnailers) saw an opaque black canvas. A transparent flatten now writes
  // Photoshop's shape: four channels with resource 1006 naming the extra one
  // "Transparency".
  for (const bool large_document : {false, true}) {
    patchy::Document document(8, 6, patchy::PixelFormat::rgb8());
    auto pixels = solid_rgba(4, 3, 240, 120, 20, 255);
    auto* semi = pixels.pixel(3, 2);
    semi[0] = 20;
    semi[1] = 200;
    semi[2] = 60;
    semi[3] = 128;
    patchy::Layer layer(document.allocate_layer_id(), "Paint Layer", std::move(pixels));
    layer.set_bounds(patchy::Rect{2, 1, 4, 3});
    document.add_layer(std::move(layer));

    patchy::psd::WriteOptions options;
    options.large_document = large_document;
    const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document, options);
    CHECK(bytes[12] == 0);
    CHECK(bytes[13] == 4);  // the merged composite carries the transparency channel

    // The layer count is NEGATIVE: the spec's "first alpha channel is the merged
    // transparency" flag, without which Photoshop shows a phantom saved channel.
    const auto read_u32_be = [&bytes](std::size_t offset) {
      return (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
             (static_cast<std::uint32_t>(bytes[offset + 1]) << 16U) |
             (static_cast<std::uint32_t>(bytes[offset + 2]) << 8U) | static_cast<std::uint32_t>(bytes[offset + 3]);
    };
    std::size_t offset = 26;
    offset += 4U + read_u32_be(offset);                     // color mode data
    offset += 4U + read_u32_be(offset);                     // image resources
    offset += large_document ? 8U : 4U;                     // layer+mask section length
    offset += large_document ? 8U : 4U;                     // layer info length
    const auto layer_count =
        static_cast<std::int16_t>((static_cast<std::uint16_t>(bytes[offset]) << 8U) | bytes[offset + 1]);
    CHECK(layer_count == -1);

    // The flat-composite read (the smart-object preview path) recovers the canvas
    // alpha as a document-alpha mask over the straight (unmatted) colors.
    patchy::psd::ReadOptions flat_options;
    flat_options.prefer_flat_composite = true;
    const auto flat = patchy::psd::DocumentIo::read(bytes, flat_options);
    CHECK(flat.layers().size() == 1);
    const auto& background = flat.layers().front();
    CHECK(background.mask().has_value());
    const auto& mask_pixels = background.mask()->pixels;
    CHECK(mask_pixels.pixel(0, 0)[0] == 0);    // transparent canvas corner
    CHECK(mask_pixels.pixel(2, 1)[0] == 255);  // opaque block
    CHECK(std::abs(static_cast<int>(mask_pixels.pixel(5, 3)[0]) - 128) <= 1);  // semi pixel coverage
    CHECK(background.pixels().pixel(2, 1)[0] == 240);
    CHECK(std::abs(static_cast<int>(background.pixels().pixel(5, 3)[1]) - 200) <= 1);  // straight color

    // A normal layered open never adopts the "Transparency" channel as a layer mask
    // (it is merged canvas alpha, not a saved channel; the layers own transparency).
    const auto layered = patchy::psd::DocumentIo::read(bytes);
    CHECK(layered.layers().size() == 1);
    CHECK(!layered.layers().front().mask().has_value());
    CHECK(layered.layers().front().pixels().pixel(0, 0)[3] == 255);
  }
}

void psd_legacy_black_composite_fixture_keeps_layer_transparency() {
  // Pins the committed pre-fix fixture (written by the July 2026 broken writer: a
  // 3-channel merged composite matted onto black while the layer keeps its real
  // alpha). ui_smart_object_legacy_black_composite_decodes_transparent consumes it to
  // prove the smart-object decode fallback heals such files.
  const auto path = patchy::test::committed_psd_fixture_path("patchy-legacy-black-composite.psb");
  CHECK(std::filesystem::exists(path));

  patchy::psd::ReadOptions flat_options;
  flat_options.prefer_flat_composite = true;
  const auto flat = patchy::psd::DocumentIo::read_file(path, flat_options);
  CHECK(flat.layers().size() == 1);
  CHECK(!flat.layers().front().mask().has_value());  // the legacy composite lost the alpha

  const auto layered = patchy::psd::DocumentIo::read_file(path);
  CHECK(layered.layers().size() == 1);
  const auto& layer = layered.layers().front();
  CHECK(layer.bounds().x == 2);
  CHECK(layer.bounds().y == 1);
  CHECK(layer.pixels().pixel(0, 0)[3] == 255);  // opaque block pixel
  CHECK(layer.pixels().pixel(3, 2)[3] == 128);  // semi-transparent pixel survived
}

void psd_photoshop_unlinked_mask_fixture_reads_unlinked() {
  const auto document =
      patchy::psd::DocumentIo::read_file(patchy::test::committed_psd_fixture_path("photoshop-unlinked-mask.psd"));
  const auto* layer = find_layer_named(document.layers(), "Layer 1");
  CHECK(layer != nullptr);
  CHECK(layer->mask().has_value());
  CHECK(!patchy::layer_mask_linked(*layer));
}

constexpr std::array<std::uint8_t, 12> kLfx2UglgItem{0, 0, 0, 0, 'u', 'g', 'l', 'g', 'b', 'o', 'o', 'l'};

void psd_generated_drop_shadow_marks_angle_as_local() {
  patchy::Document document(4, 4, patchy::PixelFormat::rgb8());
  auto& layer = document.add_pixel_layer("Styled", solid_rgba(4, 4, 10, 20, 30, 255));
  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.angle_degrees = 30.0F;
  layer.layer_style().drop_shadows.push_back(shadow);

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto payload = psd_layer_block_payload(psd_first_layer_extra_data(bytes), "lfx2");
  CHECK(payload.has_value());
  // 'uglg' must be false: Photoshop would otherwise ignore the stored angle and swing the
  // shadow to the document's global light direction.
  const auto found = std::search(payload->begin(), payload->end(), kLfx2UglgItem.begin(), kLfx2UglgItem.end());
  CHECK(found != payload->end());
  CHECK(*(found + kLfx2UglgItem.size()) == 0U);
}

void psd_drop_shadow_resolves_photoshop_global_light() {
  patchy::Document document(4, 4, patchy::PixelFormat::rgb8());
  // Global light angle resource (1037) holding -60 degrees.
  patchy::psd::BigEndianWriter resources;
  resources.write_u8('8');
  resources.write_u8('B');
  resources.write_u8('I');
  resources.write_u8('M');
  resources.write_u16(1037);
  resources.write_u16(0);  // empty pascal name, padded
  resources.write_u32(4);
  resources.write_u32(static_cast<std::uint32_t>(-60));
  document.metadata().raw_psd_image_resources = resources.bytes();

  auto& layer = document.add_pixel_layer("Styled", solid_rgba(4, 4, 10, 20, 30, 255));
  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.angle_degrees = 120.0F;
  layer.layer_style().drop_shadows.push_back(shadow);

  auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  // Flip the written 'uglg' flag to true, simulating a Photoshop-authored
  // "use global light" shadow stored with a stale local angle.
  auto found = std::search(bytes.begin(), bytes.end(), kLfx2UglgItem.begin(), kLfx2UglgItem.end());
  CHECK(found != bytes.end());
  *(found + kLfx2UglgItem.size()) = 1U;

  auto read = patchy::psd::DocumentIo::read(bytes);
  CHECK(read.layers().size() == 1);
  const auto& style = read.layers().front().layer_style();
  CHECK(style.drop_shadows.size() == 1);
  CHECK(std::lround(style.drop_shadows.front().angle_degrees) == -60);
  CHECK(!style.drop_shadows.front().use_global_light);

  // An untouched layer re-saves the preserved Photoshop lfx2 block byte-for-byte, which still
  // means -60 degrees to Photoshop via the raw global light resource. Once the style is edited
  // the preserved block is dropped, and the regenerated descriptor must carry the resolved
  // angle as a local one so Photoshop keeps rendering -60.
  std::erase_if(read.layers().front().unknown_psd_blocks(),
                [](const patchy::UnknownPsdBlock& block) { return block.key == "lfx2" || block.key == "lrFX"; });
  const auto resaved = patchy::psd::DocumentIo::write_layered_rgb8(read);
  const auto payload = psd_layer_block_payload(psd_first_layer_extra_data(resaved), "lfx2");
  CHECK(payload.has_value());
  const auto resaved_uglg =
      std::search(payload->begin(), payload->end(), kLfx2UglgItem.begin(), kLfx2UglgItem.end());
  CHECK(resaved_uglg != payload->end());
  CHECK(*(resaved_uglg + kLfx2UglgItem.size()) == 0U);
  constexpr std::array<std::uint8_t, 16> lagl_item{0, 0, 0,   0,   'l', 'a', 'g', 'l',
                                                   'U', 'n', 't', 'F', '#', 'A', 'n', 'g'};
  const auto resaved_lagl = std::search(payload->begin(), payload->end(), lagl_item.begin(), lagl_item.end());
  CHECK(resaved_lagl != payload->end());
  // IEEE-754 big-endian -60.0
  constexpr std::array<std::uint8_t, 8> minus_sixty{0xC0, 0x4E, 0, 0, 0, 0, 0, 0};
  CHECK(std::equal(minus_sixty.begin(), minus_sixty.end(), resaved_lagl + lagl_item.size()));
}

void psd_photoshop_global_light_shadow_fixture_resolves_angle() {
  const auto document = patchy::psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path("photoshop-global-light-shadow.psd"));
  const auto* layer = find_layer_named(document.layers(), "Layer 1");
  CHECK(layer != nullptr);
  CHECK(layer->layer_style().drop_shadows.size() == 1);
  // The file stores lagl=30 with uglg=true and a document global light of 90; Photoshop
  // renders 90, so the import must too.
  CHECK(std::lround(layer->layer_style().drop_shadows.front().angle_degrees) == 90);
}

void psd_arrows_load_save_stays_compressed_if_available() {
  const auto path = arrows_fixture_path();
  if (!std::filesystem::exists(path)) {
    std::cout << "[SKIP] arrows PSD fixture missing: " << path.string() << '\n';
    return;
  }

  const auto source_size = std::filesystem::file_size(path);
  const auto document = patchy::psd::DocumentIo::read_file(path);
  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto channels = psd_layer_channel_records(bytes);

  CHECK(std::any_of(channels.begin(), channels.end(),
                    [](const PsdLayerChannelRecord& channel) { return channel.compression == 1U; }));
  CHECK(bytes.size() < source_size * 5U);
}

void psd_layer_styles_round_trip_patchy_effects() {
  patchy::Document document(14, 14, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(14, 14, 255, 255, 255));
  patchy::Layer styled_layer(document.allocate_layer_id(), "Styled", solid_rgba(5, 5, 180, 40, 70, 255));
  auto& layer = document.add_layer(std::move(styled_layer));
  layer.set_bounds(patchy::Rect{4, 4, 5, 5});

  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.blend_mode = patchy::BlendMode::Multiply;
  shadow.color = patchy::RgbColor{10, 20, 30};
  shadow.opacity = 0.6F;
  shadow.angle_degrees = 135.0F;
  shadow.distance = 4.0F;
  shadow.spread = 15.0F;
  shadow.size = 6.0F;
  shadow.layer_conceals = false;
  layer.layer_style().drop_shadows.push_back(shadow);

  patchy::LayerInnerShadow inner_shadow;
  inner_shadow.enabled = true;
  inner_shadow.blend_mode = patchy::BlendMode::Multiply;
  inner_shadow.color = patchy::RgbColor{8, 9, 10};
  inner_shadow.opacity = 0.7F;
  inner_shadow.angle_degrees = 120.0F;
  inner_shadow.distance = 2.0F;
  inner_shadow.choke = 20.0F;
  inner_shadow.size = 7.0F;
  layer.layer_style().inner_shadows.push_back(inner_shadow);
  auto second_inner_shadow = inner_shadow;
  second_inner_shadow.color = patchy::RgbColor{44, 45, 46};
  second_inner_shadow.distance = 0.0F;
  second_inner_shadow.size = 3.0F;
  layer.layer_style().inner_shadows.push_back(second_inner_shadow);

  patchy::LayerOuterGlow glow;
  glow.enabled = true;
  glow.blend_mode = patchy::BlendMode::Screen;
  glow.color = patchy::RgbColor{250, 230, 80};
  glow.opacity = 0.5F;
  glow.spread = 25.0F;
  glow.size = 3.0F;
  layer.layer_style().outer_glows.push_back(glow);

  patchy::LayerInnerGlow inner_glow;
  inner_glow.enabled = true;
  inner_glow.blend_mode = patchy::BlendMode::Screen;
  inner_glow.color = patchy::RgbColor{240, 245, 210};
  inner_glow.opacity = 0.4F;
  inner_glow.choke = 10.0F;
  inner_glow.size = 4.0F;
  inner_glow.source = patchy::LayerInnerGlowSource::Edge;
  layer.layer_style().inner_glows.push_back(inner_glow);

  patchy::LayerColorOverlay overlay;
  overlay.enabled = true;
  overlay.blend_mode = patchy::BlendMode::Normal;
  overlay.color = patchy::RgbColor{180, 30, 210};
  overlay.opacity = 0.85F;
  layer.layer_style().color_overlays.push_back(overlay);

  patchy::LayerGradientFill fill;
  fill.enabled = true;
  fill.blend_mode = patchy::BlendMode::Overlay;
  fill.opacity = 0.75F;
  fill.gradient.type = patchy::LayerStyleGradientType::Radial;
  fill.gradient.angle_degrees = 45.0F;
  fill.gradient.scale = 0.8F;
  fill.gradient.reverse = true;
  fill.gradient.color_stops.push_back(patchy::GradientColorStop{0.0F, patchy::RgbColor{20, 60, 240}});
  fill.gradient.color_stops.push_back(patchy::GradientColorStop{1.0F, patchy::RgbColor{20, 220, 80}});
  fill.gradient.alpha_stops.push_back(patchy::GradientAlphaStop{0.0F, 0.25F});
  fill.gradient.alpha_stops.push_back(patchy::GradientAlphaStop{1.0F, 1.0F});
  layer.layer_style().gradient_fills.push_back(fill);

  patchy::LayerStroke stroke;
  stroke.enabled = true;
  stroke.blend_mode = patchy::BlendMode::Normal;
  stroke.color = patchy::RgbColor{255, 220, 0};
  stroke.opacity = 0.9F;
  stroke.size = 2.0F;
  stroke.position = patchy::LayerStrokePosition::Inside;
  stroke.overprint = true;
  stroke.uses_gradient = true;
  stroke.gradient = fill.gradient;
  layer.layer_style().strokes.push_back(stroke);

  patchy::LayerBevelEmboss bevel;
  bevel.enabled = true;
  bevel.highlight_blend_mode = patchy::BlendMode::Screen;
  bevel.highlight_color = patchy::RgbColor{255, 250, 220};
  bevel.highlight_opacity = 0.7F;
  bevel.shadow_blend_mode = patchy::BlendMode::Multiply;
  bevel.shadow_color = patchy::RgbColor{20, 15, 10};
  bevel.shadow_opacity = 0.65F;
  bevel.angle_degrees = 100.0F;
  bevel.altitude_degrees = 35.0F;
  bevel.depth = 1.5F;
  bevel.size = 4.0F;
  bevel.direction_up = false;
  layer.layer_style().bevels.push_back(bevel);

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto extra_data = psd_layer_extra_data(bytes, 1);
  const auto lfx2_payload = psd_layer_block_payload(extra_data, "lfx2");
  CHECK(lfx2_payload.has_value());
  CHECK(!psd_layer_block_payload(extra_data, "plFX").has_value());
  // Photoshop 2026 only resolves 'BlnM' enum values written as full stringIDs
  // ("overlay"); the 4-char codes ('Ovrl') are silently read as Normal.
  const std::string lfx2_text(lfx2_payload->begin(), lfx2_payload->end());
  CHECK(lfx2_text.find("overlay") != std::string::npos);
  CHECK(lfx2_text.find("Ovrl") == std::string::npos);
  // Photoshop also resets a GrFl blend mode unless the descriptor carries its
  // own present/showInDialog shape, so the writer mirrors it exactly.
  CHECK(lfx2_text.find("showInDialog") != std::string::npos);
  const auto read = patchy::psd::DocumentIo::read(bytes);
  CHECK(read.layers().size() == 2);
  const auto& style = read.layers()[1].layer_style();
  CHECK(!style.empty());
  CHECK(style.drop_shadows.size() == 1);
  CHECK(style.drop_shadows.front().blend_mode == patchy::BlendMode::Multiply);
  CHECK(style.drop_shadows.front().color.red == 10);
  CHECK(style.drop_shadows.front().opacity == 0.6F);
  CHECK(!style.drop_shadows.front().layer_conceals);
  CHECK(style.inner_shadows.size() == 2);
  CHECK(style.inner_shadows.front().color.blue == 10);
  CHECK(style.inner_shadows.front().choke == 20.0F);
  CHECK(style.inner_shadows[1].color.red == 44);
  CHECK(style.inner_shadows[1].distance == 0.0F);
  CHECK(style.outer_glows.size() == 1);
  CHECK(style.outer_glows.front().color.green == 230);
  CHECK(style.inner_glows.size() == 1);
  CHECK(style.inner_glows.front().color.red == 240);
  CHECK(style.inner_glows.front().source == patchy::LayerInnerGlowSource::Edge);
  CHECK(style.color_overlays.size() == 1);
  CHECK(style.color_overlays.front().color.blue == 210);
  CHECK(style.color_overlays.front().opacity == 0.85F);
  CHECK(style.gradient_fills.size() == 1);
  CHECK(style.gradient_fills.front().blend_mode == patchy::BlendMode::Overlay);
  CHECK(style.gradient_fills.front().gradient.type == patchy::LayerStyleGradientType::Radial);
  CHECK(style.gradient_fills.front().gradient.reverse);
  CHECK(style.gradient_fills.front().gradient.alpha_stops.size() == 2);
  CHECK(style.strokes.size() == 1);
  CHECK(style.strokes.front().position == patchy::LayerStrokePosition::Inside);
  CHECK(style.strokes.front().overprint);
  CHECK(style.strokes.front().uses_gradient);
  CHECK(style.bevels.size() == 1);
  CHECK(style.bevels.front().shadow_color.blue == 10);
  CHECK(!style.bevels.front().direction_up);
}

void psd_generated_stroke_descriptors_match_photoshop_shape() {
  using DescriptorType = patchy::psd::DescriptorValue::Type;

  const auto generated_stroke_descriptor = [](const patchy::LayerStroke& stroke) {
    patchy::Document document(3, 3, patchy::PixelFormat::rgb8());
    auto& layer = document.add_pixel_layer("Stroke", solid_rgba(3, 3, 40, 80, 120, 255));
    layer.layer_style().strokes.push_back(stroke);
    const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
    if (stroke.uses_gradient) {
      std::filesystem::create_directories("test-artifacts");
      patchy::psd::DocumentIo::write_layered_rgb8_file(
          document, std::filesystem::path("test-artifacts") / "patchy-gradient-stroke-com.psd");
    }
    const auto payload = psd_layer_block_payload(psd_first_layer_extra_data(bytes), "lfx2");
    CHECK(payload.has_value());
    patchy::psd::BigEndianReader reader(*payload);
    CHECK(reader.read_u32() == 0U);
    CHECK(reader.read_u32() == 16U);
    const auto root = patchy::psd::read_descriptor(reader);
    const auto* frame = patchy::psd::descriptor_object(root, "FrFX");
    CHECK(frame != nullptr);
    return *frame;
  };

  const auto check_key_order = [](const patchy::psd::DescriptorObject& object,
                                  const std::vector<std::pair<std::string, bool>>& expected) {
    CHECK(object.key_order.size() == expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index) {
      CHECK(object.key_order[index].key == expected[index].first);
      CHECK(object.key_order[index].long_form == expected[index].second);
    }
  };
  const auto require_value = [](const patchy::psd::DescriptorObject& object,
                                std::string_view key) -> const patchy::psd::DescriptorValue& {
    const auto* value = patchy::psd::descriptor_value(object, key);
    CHECK(value != nullptr);
    return *value;
  };
  const auto check_native_rgb = [&](const patchy::psd::DescriptorObject& object, patchy::RgbColor expected) {
    CHECK(object.name.empty());
    CHECK(object.class_id == "RGBC");
    CHECK(!object.class_id_long_form);
    check_key_order(object, {{"Rd  ", false}, {"Grn ", false}, {"Bl  ", false}});
    const auto& red = require_value(object, "Rd  ");
    const auto& green = require_value(object, "Grn ");
    const auto& blue = require_value(object, "Bl  ");
    CHECK(red.type == DescriptorType::Double);
    CHECK(green.type == DescriptorType::Double);
    CHECK(blue.type == DescriptorType::Double);
    CHECK(red.double_value == expected.red);
    CHECK(green.double_value == expected.green);
    CHECK(blue.double_value == expected.blue);
  };

  patchy::LayerStroke solid;
  solid.enabled = true;
  solid.position = patchy::LayerStrokePosition::Inside;
  solid.blend_mode = patchy::BlendMode::Multiply;
  solid.opacity = 0.62F;
  solid.size = 7.0F;
  solid.color = patchy::RgbColor{12, 34, 56};
  const auto solid_frame = generated_stroke_descriptor(solid);
  CHECK(solid_frame.class_id == "FrFX");
  check_key_order(solid_frame, {{"enab", false},
                                {"present", true},
                                {"showInDialog", true},
                                {"Styl", false},
                                {"PntT", false},
                                {"Md  ", false},
                                {"Opct", false},
                                {"Sz  ", false},
                                {"Clr ", false},
                                {"overprint", true}});
  const auto& solid_fill_type = require_value(solid_frame, "PntT");
  CHECK(solid_fill_type.type == DescriptorType::Enum);
  CHECK(solid_fill_type.enum_type == "FrFl");
  CHECK(solid_fill_type.enum_value == "SClr");
  const auto* solid_color = patchy::psd::descriptor_object(solid_frame, "Clr ");
  CHECK(solid_color != nullptr);
  check_native_rgb(*solid_color, solid.color);
  const auto& solid_overprint = require_value(solid_frame, "overprint");
  CHECK(solid_overprint.type == DescriptorType::Bool);
  CHECK(!solid_overprint.bool_value);

  // The Overprint checkbox writes its stored value through the same slot.
  patchy::LayerStroke overprinted = solid;
  overprinted.overprint = true;
  const auto overprinted_frame = generated_stroke_descriptor(overprinted);
  const auto& overprinted_value = require_value(overprinted_frame, "overprint");
  CHECK(overprinted_value.type == DescriptorType::Bool);
  CHECK(overprinted_value.bool_value);

  patchy::LayerStroke gradient = solid;
  gradient.uses_gradient = true;
  gradient.gradient.type = patchy::LayerStyleGradientType::Radial;
  gradient.gradient.angle_degrees = 37.0F;
  gradient.gradient.scale = 0.73F;
  gradient.gradient.reverse = true;
  gradient.gradient.color_stops = {{0.0F, patchy::RgbColor{250, 20, 30}},
                                   {1.0F, patchy::RgbColor{15, 40, 240}}};
  gradient.gradient.alpha_stops = {{0.0F, 1.0F}, {0.5F, 0.42F}, {1.0F, 1.0F}};
  const auto gradient_frame = generated_stroke_descriptor(gradient);
  check_key_order(gradient_frame, {{"enab", false},
                                   {"present", true},
                                   {"showInDialog", true},
                                   {"Styl", false},
                                   {"PntT", false},
                                   {"Md  ", false},
                                   {"Opct", false},
                                   {"Sz  ", false},
                                   {"Clr ", false},
                                   {"Grad", false},
                                   {"gradientsInterpolationMethod", true},
                                   {"Angl", false},
                                   {"Type", false},
                                   {"Rvrs", false},
                                   {"Dthr", false},
                                   {"Scl ", false},
                                   {"Algn", false},
                                   {"Ofst", false},
                                   {"overprint", true}});
  const auto* placeholder = patchy::psd::descriptor_object(gradient_frame, "Clr ");
  CHECK(placeholder != nullptr);
  check_native_rgb(*placeholder, patchy::RgbColor{0, 0, 0});

  const auto& interpolation = require_value(gradient_frame, "gradientsInterpolationMethod");
  CHECK(interpolation.type == DescriptorType::Enum);
  CHECK(interpolation.enum_type == "gradientInterpolationMethodType");
  CHECK(interpolation.enum_type_long_form);
  CHECK(interpolation.enum_value == "Gcls");
  CHECK(!interpolation.enum_value_long_form);
  const auto& angle = require_value(gradient_frame, "Angl");
  CHECK(angle.type == DescriptorType::UnitFloat);
  CHECK(angle.unit == "#Ang");
  CHECK(angle.double_value == 37.0);
  const auto& gradient_type = require_value(gradient_frame, "Type");
  CHECK(gradient_type.type == DescriptorType::Enum);
  CHECK(gradient_type.enum_type == "GrdT");
  CHECK(gradient_type.enum_value == "Rdl ");
  CHECK(require_value(gradient_frame, "Rvrs").bool_value);
  const auto& scale = require_value(gradient_frame, "Scl ");
  CHECK(scale.type == DescriptorType::UnitFloat);
  CHECK(scale.unit == "#Prc");
  CHECK(std::abs(scale.double_value - 73.0) < 0.001);

  const auto* gradient_object = patchy::psd::descriptor_object(gradient_frame, "Grad");
  CHECK(gradient_object != nullptr);
  CHECK(gradient_object->name == "Gradient");
  CHECK(gradient_object->class_id == "Grdn");
  check_key_order(*gradient_object,
                  {{"Nm  ", false}, {"GrdF", false}, {"Intr", false}, {"Clrs", false}, {"Trns", false}});
  const auto& color_list = require_value(*gradient_object, "Clrs");
  CHECK(color_list.type == DescriptorType::List);
  CHECK(color_list.list_value.size() == 2);
  CHECK(color_list.list_value.front().type == DescriptorType::Object);
  CHECK(color_list.list_value.front().object_value != nullptr);
  const auto& first_color_stop = *color_list.list_value.front().object_value;
  CHECK(first_color_stop.class_id == "Clrt");
  check_key_order(first_color_stop,
                  {{"Clr ", false}, {"Type", false}, {"Lctn", false}, {"Mdpn", false}});
  const auto* first_stop_color = patchy::psd::descriptor_object(first_color_stop, "Clr ");
  CHECK(first_stop_color != nullptr);
  check_native_rgb(*first_stop_color, gradient.gradient.color_stops.front().color);

  const auto& transparency_list = require_value(*gradient_object, "Trns");
  CHECK(transparency_list.type == DescriptorType::List);
  CHECK(transparency_list.list_value.size() == 3);
  CHECK(transparency_list.list_value[1].object_value != nullptr);
  const auto& middle_transparency_stop = *transparency_list.list_value[1].object_value;
  CHECK(middle_transparency_stop.class_id == "TrnS");
  check_key_order(middle_transparency_stop, {{"Opct", false}, {"Lctn", false}, {"Mdpn", false}});
  const auto& stop_opacity = require_value(middle_transparency_stop, "Opct");
  CHECK(stop_opacity.type == DescriptorType::UnitFloat);
  CHECK(stop_opacity.unit == "#Prc");
  CHECK(std::abs(stop_opacity.double_value - 42.0) < 0.001);

  const auto* offset = patchy::psd::descriptor_object(gradient_frame, "Ofst");
  CHECK(offset != nullptr);
  CHECK(offset->class_id == "Pnt ");
  check_key_order(*offset, {{"Hrzn", false}, {"Vrtc", false}});
  CHECK(require_value(*offset, "Hrzn").unit == "#Prc");
  CHECK(require_value(*offset, "Vrtc").unit == "#Prc");

  // Shape Burst is Stroke-only and its enum value is the stringID
  // "shapeburst" (Photoshop 2026's own spelling, pinned via COM readback —
  // an earlier calibration note wrongly claimed PS normalizes it to Linear).
  patchy::LayerStroke shape_burst = gradient;
  shape_burst.gradient.type = patchy::LayerStyleGradientType::ShapeBurst;
  const auto shape_burst_frame = generated_stroke_descriptor(shape_burst);
  const auto& shape_burst_type = require_value(shape_burst_frame, "Type");
  CHECK(shape_burst_type.type == DescriptorType::Enum);
  CHECK(shape_burst_type.enum_type == "GrdT");
  CHECK(shape_burst_type.enum_value == "shapeburst");

  patchy::Document shape_burst_document(3, 3, patchy::PixelFormat::rgb8());
  shape_burst_document.add_pixel_layer("Stroke", solid_rgba(3, 3, 40, 80, 120, 255))
      .layer_style()
      .strokes.push_back(shape_burst);
  const auto round_tripped =
      patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(shape_burst_document));
  CHECK(round_tripped.layers().size() == 1);
  CHECK(round_tripped.layers().front().layer_style().strokes.size() == 1);
  CHECK(round_tripped.layers().front().layer_style().strokes.front().gradient.type ==
        patchy::LayerStyleGradientType::ShapeBurst);
}

void psd_gradient_midpoints_write_mdpn_and_round_trip() {
  using DescriptorType = patchy::psd::DescriptorValue::Type;

  patchy::Document document(4, 4, patchy::PixelFormat::rgb8());
  auto& layer = document.add_pixel_layer("Midpoint gradients", solid_rgba(4, 4, 40, 80, 120, 255));

  patchy::LayerSatin satin;
  satin.enabled = true;
  satin.blend_mode = patchy::BlendMode::Screen;
  satin.color = patchy::RgbColor{17, 83, 211};
  satin.opacity = 0.37F;
  satin.angle_degrees = -23.0F;
  satin.distance = 9.0F;
  satin.size = 6.0F;
  satin.invert = false;
  layer.layer_style().satins.push_back(satin);

  patchy::LayerGradientFill fill;
  fill.enabled = true;
  fill.gradient.color_stops = {{0.0F, patchy::RgbColor{12, 34, 56}, 0.17F},
                               {1.0F, patchy::RgbColor{210, 220, 230}, 0.23F}};
  fill.gradient.alpha_stops = {{0.0F, 1.0F, 0.83F}, {1.0F, 0.4F, 0.77F}};
  layer.layer_style().gradient_fills.push_back(fill);

  patchy::LayerStroke stroke;
  stroke.enabled = true;
  stroke.size = 2.0F;
  stroke.uses_gradient = true;
  stroke.gradient = fill.gradient;
  layer.layer_style().strokes.push_back(stroke);

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  std::filesystem::create_directories("test-artifacts");
  patchy::psd::DocumentIo::write_layered_rgb8_file(
      document, std::filesystem::path("test-artifacts") / "patchy-layer-style-4a-com.psd");
  const auto payload = psd_layer_block_payload(psd_first_layer_extra_data(bytes), "lfx2");
  CHECK(payload.has_value());
  patchy::psd::BigEndianReader reader(*payload);
  CHECK(reader.read_u32() == 0U);
  CHECK(reader.read_u32() == 16U);
  const auto root = patchy::psd::read_descriptor(reader);

  const auto check_gradient_midpoints = [&](std::string_view effect_key) {
    const auto* effect = patchy::psd::descriptor_object(root, effect_key);
    CHECK(effect != nullptr);
    const auto* gradient = patchy::psd::descriptor_object(*effect, "Grad");
    CHECK(gradient != nullptr);

    const auto* colors = patchy::psd::descriptor_value(*gradient, "Clrs");
    CHECK(colors != nullptr);
    CHECK(colors->type == DescriptorType::List);
    CHECK(colors->list_value.size() == 2);
    CHECK(colors->list_value[0].object_value != nullptr);
    CHECK(colors->list_value[1].object_value != nullptr);
    const auto* first_color_midpoint =
        patchy::psd::descriptor_value(*colors->list_value[0].object_value, "Mdpn");
    const auto* second_color_midpoint =
        patchy::psd::descriptor_value(*colors->list_value[1].object_value, "Mdpn");
    CHECK(first_color_midpoint != nullptr);
    CHECK(second_color_midpoint != nullptr);
    CHECK(first_color_midpoint->type == DescriptorType::Integer);
    CHECK(second_color_midpoint->type == DescriptorType::Integer);
    CHECK(first_color_midpoint->integer_value == 17);
    CHECK(second_color_midpoint->integer_value == 23);

    const auto* transparency = patchy::psd::descriptor_value(*gradient, "Trns");
    CHECK(transparency != nullptr);
    CHECK(transparency->type == DescriptorType::List);
    CHECK(transparency->list_value.size() == 2);
    CHECK(transparency->list_value[0].object_value != nullptr);
    CHECK(transparency->list_value[1].object_value != nullptr);
    const auto* first_alpha_midpoint =
        patchy::psd::descriptor_value(*transparency->list_value[0].object_value, "Mdpn");
    const auto* second_alpha_midpoint =
        patchy::psd::descriptor_value(*transparency->list_value[1].object_value, "Mdpn");
    CHECK(first_alpha_midpoint != nullptr);
    CHECK(second_alpha_midpoint != nullptr);
    CHECK(first_alpha_midpoint->type == DescriptorType::Integer);
    CHECK(second_alpha_midpoint->type == DescriptorType::Integer);
    CHECK(first_alpha_midpoint->integer_value == 83);
    CHECK(second_alpha_midpoint->integer_value == 77);
  };
  check_gradient_midpoints("GrFl");
  check_gradient_midpoints("FrFX");

  const auto round_tripped = patchy::psd::DocumentIo::read(bytes);
  CHECK(round_tripped.layers().size() == 1);
  const auto& style = round_tripped.layers().front().layer_style();
  CHECK(style.gradient_fills.size() == 1);
  CHECK(style.strokes.size() == 1);
  for (const auto* gradient :
       {&style.gradient_fills.front().gradient, &style.strokes.front().gradient}) {
    CHECK(gradient->color_stops.size() == 2);
    CHECK(gradient->alpha_stops.size() == 2);
    CHECK(std::abs(gradient->color_stops[0].midpoint - 0.17F) < 1.0e-6F);
    CHECK(std::abs(gradient->color_stops[1].midpoint - 0.23F) < 1.0e-6F);
    CHECK(std::abs(gradient->alpha_stops[0].midpoint - 0.83F) < 1.0e-6F);
    CHECK(std::abs(gradient->alpha_stops[1].midpoint - 0.77F) < 1.0e-6F);
  }

  patchy::Document hard_stop_document(4, 4, patchy::PixelFormat::rgb8());
  auto& hard_stop_layer =
      hard_stop_document.add_pixel_layer("Hard stop", solid_rgba(4, 4, 40, 80, 120, 255));
  patchy::LayerGradientFill hard_stop_fill;
  hard_stop_fill.enabled = true;
  hard_stop_fill.gradient.color_stops = {
      {0.0F, patchy::RgbColor{0, 0, 0}, 0.5F},
      {0.5F, patchy::RgbColor{255, 0, 0}, 0.31F},
      {0.5F, patchy::RgbColor{0, 255, 0}, 0.73F},
      {1.0F, patchy::RgbColor{255, 255, 255}, 0.62F}};
  hard_stop_fill.gradient.alpha_stops = {
      {0.0F, 1.0F, 0.5F}, {0.5F, 0.8F, 0.22F},
      {0.5F, 0.2F, 0.88F}, {1.0F, 1.0F, 0.44F}};
  hard_stop_layer.layer_style().gradient_fills.push_back(hard_stop_fill);
  const auto hard_stop_round_trip = patchy::psd::DocumentIo::read(
      patchy::psd::DocumentIo::write_layered_rgb8(hard_stop_document));
  const auto& hard_gradient =
      hard_stop_round_trip.layers().front().layer_style().gradient_fills.front().gradient;
  CHECK(hard_gradient.color_stops.size() == 4);
  CHECK(hard_gradient.color_stops[1].color.red == 255U);
  CHECK(hard_gradient.color_stops[2].color.green == 255U);
  CHECK(std::abs(hard_gradient.color_stops[1].midpoint - 0.31F) < 1.0e-6F);
  CHECK(std::abs(hard_gradient.color_stops[2].midpoint - 0.73F) < 1.0e-6F);
  CHECK(hard_gradient.alpha_stops.size() == 4);
  CHECK(std::abs(hard_gradient.alpha_stops[1].opacity - 0.8F) < 1.0e-6F);
  CHECK(std::abs(hard_gradient.alpha_stops[2].opacity - 0.2F) < 1.0e-6F);
  CHECK(std::abs(hard_gradient.alpha_stops[1].midpoint - 0.22F) < 1.0e-6F);
  CHECK(std::abs(hard_gradient.alpha_stops[2].midpoint - 0.88F) < 1.0e-6F);
}

void psd_generated_satin_descriptor_matches_photoshop_shape() {
  using DescriptorType = patchy::psd::DescriptorValue::Type;

  patchy::Document document(4, 4, patchy::PixelFormat::rgb8());
  auto& layer = document.add_pixel_layer("Native Satin", solid_rgba(4, 4, 80, 120, 160, 255));
  patchy::LayerSatin satin;
  satin.enabled = true;
  satin.blend_mode = patchy::BlendMode::Screen;
  satin.color = patchy::RgbColor{17, 83, 211};
  satin.opacity = 0.37F;
  satin.angle_degrees = -23.0F;
  satin.distance = 9.0F;
  satin.size = 6.0F;
  satin.invert = false;
  layer.layer_style().satins.push_back(satin);

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto payload = psd_layer_block_payload(psd_first_layer_extra_data(bytes), "lfx2");
  CHECK(payload.has_value());
  patchy::psd::BigEndianReader reader(*payload);
  CHECK(reader.read_u32() == 0U);
  CHECK(reader.read_u32() == 16U);
  const auto root = patchy::psd::read_descriptor(reader);
  const auto* descriptor = patchy::psd::descriptor_object(root, "ChFX");
  CHECK(descriptor != nullptr);
  CHECK(descriptor->name.empty());
  CHECK(descriptor->class_id == "ChFX");
  CHECK(!descriptor->class_id_long_form);

  const auto check_key_order = [](const patchy::psd::DescriptorObject& object,
                                  const std::vector<std::pair<std::string, bool>>& expected) {
    CHECK(object.key_order.size() == expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index) {
      CHECK(object.key_order[index].key == expected[index].first);
      CHECK(object.key_order[index].long_form == expected[index].second);
    }
  };
  const auto require_value = [](const patchy::psd::DescriptorObject& object,
                                std::string_view key) -> const patchy::psd::DescriptorValue& {
    const auto* value = patchy::psd::descriptor_value(object, key);
    CHECK(value != nullptr);
    return *value;
  };

  check_key_order(*descriptor, {{"enab", false},
                                {"present", true},
                                {"showInDialog", true},
                                {"Md  ", false},
                                {"Clr ", false},
                                {"AntA", false},
                                {"Invr", false},
                                {"Opct", false},
                                {"lagl", false},
                                {"Dstn", false},
                                {"blur", false},
                                {"MpgS", false}});
  CHECK(require_value(*descriptor, "enab").type == DescriptorType::Bool);
  CHECK(require_value(*descriptor, "enab").bool_value);
  CHECK(require_value(*descriptor, "present").bool_value);
  CHECK(require_value(*descriptor, "showInDialog").bool_value);

  const auto& mode = require_value(*descriptor, "Md  ");
  CHECK(mode.type == DescriptorType::Enum);
  CHECK(mode.enum_type == "BlnM");
  CHECK(!mode.enum_type_long_form);
  CHECK(mode.enum_value == "screen");
  CHECK(mode.enum_value_long_form);

  const auto* color = patchy::psd::descriptor_object(*descriptor, "Clr ");
  CHECK(color != nullptr);
  CHECK(color->name.empty());
  CHECK(color->class_id == "RGBC");
  CHECK(!color->class_id_long_form);
  check_key_order(*color, {{"Rd  ", false}, {"Grn ", false}, {"Bl  ", false}});
  for (const auto& [key, expected] :
       std::array<std::pair<std::string_view, double>, 3>{{{"Rd  ", 17.0}, {"Grn ", 83.0}, {"Bl  ", 211.0}}}) {
    const auto& channel = require_value(*color, key);
    CHECK(channel.type == DescriptorType::Double);
    CHECK(channel.double_value == expected);
  }

  CHECK(require_value(*descriptor, "AntA").type == DescriptorType::Bool);
  CHECK(!require_value(*descriptor, "AntA").bool_value);
  CHECK(require_value(*descriptor, "Invr").type == DescriptorType::Bool);
  CHECK(!require_value(*descriptor, "Invr").bool_value);
  const auto check_unit = [&](std::string_view key, std::string_view unit, double expected) {
    const auto& value = require_value(*descriptor, key);
    CHECK(value.type == DescriptorType::UnitFloat);
    CHECK(value.unit == unit);
    CHECK(std::abs(value.double_value - expected) < 1.0e-6);
  };
  check_unit("Opct", "#Prc", 37.0);
  check_unit("lagl", "#Ang", -23.0);
  check_unit("Dstn", "#Pxl", 9.0);
  check_unit("blur", "#Pxl", 6.0);

  const auto* contour = patchy::psd::descriptor_object(*descriptor, "MpgS");
  CHECK(contour != nullptr);
  CHECK(contour->name.empty());
  CHECK(contour->class_id == "ShpC");
  CHECK(!contour->class_id_long_form);
  check_key_order(*contour, {{"Nm  ", false}, {"Crv ", false}});
  const auto& contour_name = require_value(*contour, "Nm  ");
  CHECK(contour_name.type == DescriptorType::String);
  CHECK(contour_name.string_value == "$$$/Contours/Defaults/Linear=Linear");
  const auto& points = require_value(*contour, "Crv ");
  CHECK(points.type == DescriptorType::List);
  CHECK(points.list_value.size() == 2);
  for (std::size_t index = 0; index < points.list_value.size(); ++index) {
    CHECK(points.list_value[index].type == DescriptorType::Object);
    CHECK(points.list_value[index].object_value != nullptr);
    const auto& point = *points.list_value[index].object_value;
    CHECK(point.name.empty());
    CHECK(point.class_id == "CrPt");
    CHECK(!point.class_id_long_form);
    check_key_order(point, {{"Hrzn", false}, {"Vrtc", false}});
    const auto expected = index == 0U ? 0.0 : 255.0;
    CHECK(require_value(point, "Hrzn").type == DescriptorType::Double);
    CHECK(require_value(point, "Vrtc").type == DescriptorType::Double);
    CHECK(require_value(point, "Hrzn").double_value == expected);
    CHECK(require_value(point, "Vrtc").double_value == expected);
  }
  CHECK(patchy::psd::descriptor_value(*descriptor, "Nose") == nullptr);

  const auto round_tripped = patchy::psd::DocumentIo::read(bytes);
  CHECK(round_tripped.layers().size() == 1);
  CHECK(round_tripped.layers().front().layer_style().satins.size() == 1);
  const auto& imported = round_tripped.layers().front().layer_style().satins.front();
  CHECK(imported.enabled);
  CHECK(imported.blend_mode == satin.blend_mode);
  CHECK(imported.color.red == satin.color.red);
  CHECK(imported.color.green == satin.color.green);
  CHECK(imported.color.blue == satin.color.blue);
  CHECK(std::abs(imported.opacity - satin.opacity) < 1.0e-6F);
  CHECK(std::abs(imported.angle_degrees - satin.angle_degrees) < 1.0e-6F);
  CHECK(std::abs(imported.distance - satin.distance) < 1.0e-6F);
  CHECK(std::abs(imported.size - satin.size) < 1.0e-6F);
  CHECK(imported.invert == satin.invert);
  CHECK(!imported.unsupported_contour_options);

  auto anti_aliased_bytes = bytes;
  constexpr std::array<std::uint8_t, 12> kAntialiasItem{
      0, 0, 0, 0, 'A', 'n', 't', 'A', 'b', 'o', 'o', 'l'};
  const auto antialias_item = std::search(anti_aliased_bytes.begin(), anti_aliased_bytes.end(),
                                          kAntialiasItem.begin(), kAntialiasItem.end());
  CHECK(antialias_item != anti_aliased_bytes.end());
  *(antialias_item + kAntialiasItem.size()) = 1U;
  const auto anti_aliased_import = patchy::psd::DocumentIo::read(anti_aliased_bytes);
  CHECK(anti_aliased_import.layers().front().layer_style().satins.size() == 1);
  CHECK(anti_aliased_import.layers().front().layer_style().satins.front().unsupported_contour_options);

  patchy::Document disabled_document(4, 4, patchy::PixelFormat::rgb8());
  auto& disabled_layer =
      disabled_document.add_pixel_layer("Disabled Satin", solid_rgba(4, 4, 80, 120, 160, 255));
  satin.enabled = false;
  disabled_layer.layer_style().satins.push_back(satin);
  const auto disabled_bytes = patchy::psd::DocumentIo::write_layered_rgb8(disabled_document);
  CHECK(psd_layer_block_payload(psd_first_layer_extra_data(disabled_bytes), "lfx2").has_value());
  const auto disabled_round_trip = patchy::psd::DocumentIo::read(disabled_bytes);
  CHECK(disabled_round_trip.layers().front().layer_style().satins.size() == 1);
  const auto& disabled_imported = disabled_round_trip.layers().front().layer_style().satins.front();
  CHECK(!disabled_imported.enabled);
  CHECK(disabled_imported.blend_mode == satin.blend_mode);
  CHECK(disabled_imported.color.red == satin.color.red);
  CHECK(disabled_imported.color.green == satin.color.green);
  CHECK(disabled_imported.color.blue == satin.color.blue);
  CHECK(std::abs(disabled_imported.opacity - satin.opacity) < 1.0e-6F);
  CHECK(disabled_imported.angle_degrees == satin.angle_degrees);
  CHECK(disabled_imported.distance == satin.distance);
  CHECK(disabled_imported.size == satin.size);
  CHECK(disabled_imported.invert == satin.invert);
}

void psd_photoshop_satin_fixture_preserves_native_lfx2() {
  const auto path = patchy::test::committed_psd_fixture_path("photoshop-satin-default.psd");
  CHECK(std::filesystem::exists(path));
  const auto document = patchy::psd::DocumentIo::read_file(path);
  const auto* layer = find_layer_named(document.layers(), "Satin default");
  CHECK(layer != nullptr);
  CHECK(layer->layer_style().satins.size() == 1);
  const auto& satin = layer->layer_style().satins.front();
  CHECK(satin.enabled);
  CHECK(satin.blend_mode == patchy::BlendMode::Multiply);
  CHECK(satin.color.red == 0U);
  CHECK(satin.color.green == 0U);
  CHECK(satin.color.blue == 0U);
  CHECK(std::abs(satin.opacity - 0.5F) < 1.0e-6F);
  CHECK(std::abs(satin.angle_degrees - 19.0F) < 1.0e-6F);
  CHECK(std::abs(satin.distance - 11.0F) < 1.0e-6F);
  CHECK(std::abs(satin.size - 14.0F) < 1.0e-6F);
  CHECK(satin.invert);
  CHECK(!satin.unsupported_contour_options);

  const auto preserved = std::find_if(layer->unknown_psd_blocks().begin(), layer->unknown_psd_blocks().end(),
                                      [](const patchy::UnknownPsdBlock& block) { return block.key == "lfx2"; });
  CHECK(preserved != layer->unknown_psd_blocks().end());
  CHECK(preserved->payload.size() == 5580U);

  // The untouched Photoshop payload wins over modeled regeneration and remains
  // byte-for-byte identical on save.
  const auto resaved = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto resaved_payload = psd_layer_block_payload(psd_layer_extra_data(resaved, 1), "lfx2");
  CHECK(resaved_payload.has_value());
  CHECK(*resaved_payload == preserved->payload);

  const auto reread = patchy::psd::DocumentIo::read(resaved);
  const auto* reread_layer = find_layer_named(reread.layers(), "Satin default");
  CHECK(reread_layer != nullptr);
  CHECK(reread_layer->layer_style().satins.size() == 1);
  CHECK(reread_layer->layer_style().satins.front().invert);
  CHECK(std::abs(reread_layer->layer_style().satins.front().size - 14.0F) < 1.0e-6F);
}

void psd_photoshop_layer_style_4a_round_trip_fixture_imports() {
  const auto path =
      patchy::test::committed_psd_fixture_path("photoshop-layer-style-4a-roundtrip.psd");
  CHECK(std::filesystem::exists(path));
  const auto document = patchy::psd::DocumentIo::read_file(path);
  const auto* layer = find_layer_named(document.layers(), "Midpoint gradients");
  CHECK(layer != nullptr);
  const auto& style = layer->layer_style();
  CHECK(style.satins.size() == 1);
  const auto& satin = style.satins.front();
  CHECK(satin.enabled);
  CHECK(satin.blend_mode == patchy::BlendMode::Screen);
  CHECK(satin.color.red == 17U && satin.color.green == 83U && satin.color.blue == 211U);
  CHECK(std::abs(satin.opacity - 0.37F) < 1.0e-6F);
  CHECK(satin.angle_degrees == -23.0F);
  CHECK(satin.distance == 9.0F);
  CHECK(satin.size == 6.0F);
  CHECK(!satin.invert);
  CHECK(!satin.unsupported_contour_options);

  CHECK(style.gradient_fills.size() == 1);
  CHECK(style.strokes.size() == 1);
  for (const auto* gradient :
       {&style.gradient_fills.front().gradient, &style.strokes.front().gradient}) {
    CHECK(gradient->color_stops.size() == 2);
    CHECK(gradient->alpha_stops.size() == 2);
    CHECK(std::abs(gradient->color_stops[1].midpoint - 0.23F) < 1.0e-6F);
    CHECK(std::abs(gradient->alpha_stops[1].midpoint - 0.77F) < 1.0e-6F);
  }

  const auto preserved = std::find_if(layer->unknown_psd_blocks().begin(), layer->unknown_psd_blocks().end(),
                                      [](const patchy::UnknownPsdBlock& block) { return block.key == "lfx2"; });
  CHECK(preserved != layer->unknown_psd_blocks().end());
  const auto resaved = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto resaved_payload = psd_layer_block_payload(psd_first_layer_extra_data(resaved), "lfx2");
  CHECK(resaved_payload.has_value());
  CHECK(*resaved_payload == preserved->payload);
}

}  // namespace

std::vector<patchy::test::TestCase> psd_writer_stability_tests() {
  return {
      {"psb_write_accepts_over_30k_dimension_psd_rejects",
       psb_write_accepts_over_30k_dimension_psd_rejects},
      {"psd_layered_writer_bytes_are_stable", psd_layered_writer_bytes_are_stable},
      {"psd_layered_write_keeps_merged_transparency_in_composite",
       psd_layered_write_keeps_merged_transparency_in_composite},
      {"psd_legacy_black_composite_fixture_keeps_layer_transparency",
       psd_legacy_black_composite_fixture_keeps_layer_transparency},
      {"psd_photoshop_unlinked_mask_fixture_reads_unlinked",
       psd_photoshop_unlinked_mask_fixture_reads_unlinked},
      {"psd_generated_drop_shadow_marks_angle_as_local",
       psd_generated_drop_shadow_marks_angle_as_local},
      {"psd_drop_shadow_resolves_photoshop_global_light",
       psd_drop_shadow_resolves_photoshop_global_light},
      {"psd_photoshop_global_light_shadow_fixture_resolves_angle",
       psd_photoshop_global_light_shadow_fixture_resolves_angle},
      {"psd_arrows_load_save_stays_compressed_if_available",
       psd_arrows_load_save_stays_compressed_if_available},
      {"psd_layer_styles_round_trip_patchy_effects", psd_layer_styles_round_trip_patchy_effects},
      {"psd_generated_stroke_descriptors_match_photoshop_shape",
       psd_generated_stroke_descriptors_match_photoshop_shape},
      {"psd_gradient_midpoints_write_mdpn_and_round_trip",
       psd_gradient_midpoints_write_mdpn_and_round_trip},
      {"psd_generated_satin_descriptor_matches_photoshop_shape",
       psd_generated_satin_descriptor_matches_photoshop_shape},
      {"psd_photoshop_satin_fixture_preserves_native_lfx2",
       psd_photoshop_satin_fixture_preserves_native_lfx2},
      {"psd_photoshop_layer_style_4a_round_trip_fixture_imports",
       psd_photoshop_layer_style_4a_round_trip_fixture_imports},
  };
}
