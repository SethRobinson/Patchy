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
#include "psd/psd_io_internal.hpp"
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

using patchy::test::RgbDiffMetrics;
using patchy::test::arrows_fixture_path;
using patchy::test::close_float;
using patchy::test::find_layer_named;
using patchy::test::fnv1a_hash_bytes;
using patchy::test::layer_has_psd_block;
using patchy::test::psd_first_layer_extra_data;
using patchy::test::psd_layer_block_payload;
using patchy::test::rgb_diff_metrics;
using patchy::test::solid_rgb;
using patchy::test::solid_rgba;
using patchy::test::test_image_resource_payload;
using patchy::test::write_rgb8_bmp_artifact;

std::filesystem::path qual_rca_pinout_fixture_path() {
  return patchy::test::committed_psd_fixture_path("qual_rca_pinout.psd");
}

const patchy::LayerDropShadow* first_enabled_drop_shadow(const patchy::Layer& layer) {
  const auto& shadows = layer.layer_style().drop_shadows;
  const auto found = std::find_if(shadows.begin(), shadows.end(), [](const patchy::LayerDropShadow& shadow) {
    return shadow.enabled;
  });
  return found == shadows.end() ? nullptr : &*found;
}

const patchy::LayerInnerShadow* first_enabled_inner_shadow(const patchy::Layer& layer) {
  const auto& shadows = layer.layer_style().inner_shadows;
  const auto found = std::find_if(shadows.begin(), shadows.end(), [](const patchy::LayerInnerShadow& shadow) {
    return shadow.enabled;
  });
  return found == shadows.end() ? nullptr : &*found;
}

const patchy::LayerInnerGlow* first_enabled_inner_glow(const patchy::Layer& layer) {
  const auto& glows = layer.layer_style().inner_glows;
  const auto found = std::find_if(glows.begin(), glows.end(), [](const patchy::LayerInnerGlow& glow) {
    return glow.enabled;
  });
  return found == glows.end() ? nullptr : &*found;
}

patchy::PixelBuffer rgb_diff_image(const patchy::PixelBuffer& left, const patchy::PixelBuffer& right) {
  CHECK(left.width() == right.width());
  CHECK(left.height() == right.height());
  patchy::PixelBuffer diff(left.width(), left.height(), patchy::PixelFormat::rgb8());
  for (std::int32_t y = 0; y < left.height(); ++y) {
    for (std::int32_t x = 0; x < left.width(); ++x) {
      const auto* a = left.pixel(x, y);
      const auto* b = right.pixel(x, y);
      auto* out = diff.pixel(x, y);
      for (int channel = 0; channel < 3; ++channel) {
        out[channel] =
            static_cast<std::uint8_t>(std::min(255, std::abs(static_cast<int>(a[channel]) -
                                                             static_cast<int>(b[channel])) * 4));
      }
    }
  }
  return diff;
}

void write_qual_rca_pinout_report(const RgbDiffMetrics& metrics, const patchy::Document& editable_document) {
  std::filesystem::create_directories("test-artifacts");
  std::vector<std::string> recommended_features;
  recommended_features.push_back("Improve Photoshop text rasterization/font metric parity for editable text layers.");
  recommended_features.push_back("Decode additional Photoshop layer effects advertised by lfx2: inner shadow, inner glow, satin, pattern overlay.");
  recommended_features.push_back("Classify preserved PSD metadata blocks such as shmd and fxrp so reports can name unsupported data precisely.");

  int styled_layers = 0;
  int text_layers = 0;
  for (const auto& layer : editable_document.layers()) {
    if (!layer.layer_style().empty()) {
      ++styled_layers;
    }
    if (patchy::layer_is_text(layer)) {
      ++text_layers;
    }
  }

  {
    std::ofstream report(std::filesystem::path("test-artifacts") / "psd_qual_rca_pinout_compatibility_report.txt");
    report << "PSD compatibility comparison: qual_rca_pinout.psd\n";
    report << "Reference: embedded Photoshop composite\n";
    report << "Patchy render: editable layer composite\n";
    report << "Pixels: " << metrics.pixels << "\n";
    report << "Differing pixels: " << metrics.differing_pixels << "\n";
    report << "Mean absolute channel delta: " << std::fixed << std::setprecision(3)
           << metrics.mean_abs_channel_delta << "\n";
    report << "Max channel delta: " << metrics.max_channel_delta << "\n";
    report << "Parsed styled layers: " << styled_layers << "\n";
    report << "Parsed editable text layers: " << text_layers << "\n";
    report << "Recommendations:\n";
    for (const auto& recommendation : recommended_features) {
      report << "- " << recommendation << "\n";
    }
  }

  {
    std::ofstream json(std::filesystem::path("test-artifacts") / "psd_qual_rca_pinout_compatibility_report.json");
    json << "{\n";
    json << "  \"fixture\": \"qual_rca_pinout.psd\",\n";
    json << "  \"reference\": \"embedded Photoshop composite\",\n";
    json << "  \"pixels\": " << metrics.pixels << ",\n";
    json << "  \"differing_pixels\": " << metrics.differing_pixels << ",\n";
    json << "  \"mean_abs_channel_delta\": " << std::fixed << std::setprecision(3)
         << metrics.mean_abs_channel_delta << ",\n";
    json << "  \"max_channel_delta\": " << metrics.max_channel_delta << ",\n";
    json << "  \"styled_layers\": " << styled_layers << ",\n";
    json << "  \"text_layers\": " << text_layers << ",\n";
    json << "  \"recommendations\": [\n";
    for (std::size_t i = 0; i < recommended_features.size(); ++i) {
      json << "    \"" << recommended_features[i] << "\"" << (i + 1U == recommended_features.size() ? "\n" : ",\n");
    }
    json << "  ]\n";
    json << "}\n";
  }
}

void pattern_presets_generate_stable_tiles() {
  // Byte-stability canary: preset tiles are embedded into user PSDs, so the
  // generators may never drift. Re-pin only for a deliberate art change.
  const auto presets = patchy::builtin_pattern_presets();
  CHECK(presets.size() == 12U);
  for (const auto& preset : presets) {
    const auto tile = patchy::generate_builtin_pattern_tile(preset.id);
    CHECK(!tile.empty());
    CHECK(tile.format() == patchy::PixelFormat::rgba8());
    CHECK(patchy::find_builtin_pattern_preset(preset.id) == &preset);
    const auto resource = patchy::builtin_pattern_resource(preset.id);
    CHECK(resource.id == preset.id);
    CHECK(resource.name == preset.english_name);
    CHECK(!resource.tile.empty());
  }
  const auto tile_hash = [](std::string_view id) {
    const auto tile = patchy::generate_builtin_pattern_tile(id);
    return fnv1a_hash_bytes(tile.data());
  };
  struct PinnedTile {
    const char* id;
    std::uint64_t hash;
  };
  static constexpr PinnedTile kPins[] = {
      {"c4a11e00-0001-4b1d-9c3e-7a7c9e55b001", 0x6137c1aa7e0f4b25ULL},  // Checkerboard
      {"c4a11e00-0002-4b1d-9c3e-7a7c9e55b002", 0x7f79c7ddf5d50325ULL},  // Diagonal Stripes
      {"c4a11e00-0003-4b1d-9c3e-7a7c9e55b003", 0x9ef6fbb8dfcb2565ULL},  // Polka Dots
      {"c4a11e00-0004-4b1d-9c3e-7a7c9e55b004", 0xf176d3fb46b4db25ULL},  // Grid
      {"c4a11e00-0005-4b1d-9c3e-7a7c9e55b005", 0x52c58a65db0dcd82ULL},  // Fine Grain
      {"c4a11e00-0006-4b1d-9c3e-7a7c9e55b006", 0x1911819944c1c245ULL},  // Canvas Weave
      {"c4a11e00-0007-4b1d-9c3e-7a7c9e55b007", 0xab6e44f661f0d545ULL},  // Wood Grain
      {"c4a11e00-0008-4b1d-9c3e-7a7c9e55b008", 0xb945032d60433d4bULL},  // Brushed Metal
      {"c4a11e00-0009-4b1d-9c3e-7a7c9e55b009", 0x623c8276985ebde9ULL},  // Bumps
      {"c4a11e00-000a-4b1d-9c3e-7a7c9e55b00a", 0xcbaecc95b939c271ULL},  // Bricks
      {"c4a11e00-000b-4b1d-9c3e-7a7c9e55b00b", 0xea2e47fb35c6f525ULL},  // Scales
      {"c4a11e00-000c-4b1d-9c3e-7a7c9e55b00c", 0x99ab545bbb251625ULL},  // Basketweave
  };
  for (const auto& pin : kPins) {
    CHECK(tile_hash(pin.id) == pin.hash);
  }
}

void style_contour_lut_handles_presets_and_corners() {
  // Empty points and the explicit two-point ramp are both the Linear identity.
  const auto identity = patchy::build_style_contour_lut(patchy::StyleContour{});
  for (int input = 0; input < 256; ++input) {
    CHECK(identity[static_cast<std::size_t>(input)] == input);
  }
  const auto* linear = patchy::find_builtin_contour_preset("contour.linear");
  CHECK(linear != nullptr);
  CHECK(patchy::style_contour_is_linear(linear->contour));
  CHECK(patchy::build_style_contour_lut(linear->contour) == identity);

  // Cone is an all-corner polyline: exact linear ramps with the apex at 128.
  const auto* cone = patchy::find_builtin_contour_preset("contour.cone");
  CHECK(cone != nullptr);
  const auto cone_lut = patchy::build_style_contour_lut(cone->contour);
  CHECK(cone_lut[0] == 0);
  CHECK(cone_lut[128] == 255);
  CHECK(cone_lut[255] == 0);
  CHECK(cone_lut[64] == 128);  // straight segment, not a spline bulge

  // Ring is smooth: rises then falls, symmetric-ish, peak at the middle.
  const auto* ring = patchy::find_builtin_contour_preset("contour.ring");
  CHECK(ring != nullptr);
  CHECK(patchy::find_builtin_contour_preset(ring->contour) == ring);
  const auto ring_lut = patchy::build_style_contour_lut(ring->contour);
  CHECK(ring_lut[128] == 255);
  CHECK(ring_lut[0] == 0 && ring_lut[255] == 0);
  CHECK(ring_lut[64] > 100);

  // Anti-aliased sampling interpolates between entries; quantized snaps.
  const auto smooth = patchy::sample_style_contour_lut(cone_lut, 0.25F + 0.5F / 255.0F, true);
  const auto stepped = patchy::sample_style_contour_lut(cone_lut, 0.25F + 0.5F / 255.0F, false);
  CHECK(std::abs(smooth - (cone_lut[static_cast<std::size_t>(std::lround(0.25F * 255.0F))] / 255.0F)) < 0.02F);
  CHECK(stepped == cone_lut[64] / 255.0F || stepped == cone_lut[65] / 255.0F);
}

void psd_photoshop_pattern_overlay_fixture_imports() {
  const auto document =
      patchy::psd::DocumentIo::read_file(patchy::test::committed_psd_fixture_path("photoshop-pattern-overlay.psd"));
  CHECK(document.metadata().patterns.patterns.size() == 1U);
  const auto* resource = document.metadata().patterns.find("2317675a-e95e-b147-8612-bb6e28bcf146");
  CHECK(resource != nullptr);
  CHECK(resource->name == "PatchyProbePattern");
  CHECK(resource->tile.width() == 8 && resource->tile.height() == 8);
  const auto* corner = resource->tile.pixel(0, 0);
  CHECK(corner[0] == 200 && corner[1] == 40 && corner[2] == 40 && corner[3] == 255);
  const auto* marker = resource->tile.pixel(6, 6);
  CHECK(marker[0] == 255 && marker[1] == 255 && marker[2] == 255);

  const auto* layer = find_layer_named(document.layers(), "patterned");
  CHECK(layer != nullptr);
  CHECK(layer->layer_style().pattern_overlays.size() == 1U);
  const auto& overlay = layer->layer_style().pattern_overlays.front();
  CHECK(overlay.enabled);
  CHECK(overlay.pattern_id == "2317675a-e95e-b147-8612-bb6e28bcf146");
  CHECK(std::abs(overlay.scale - 1.0F) < 0.001F);
  CHECK(overlay.link_with_layer);
  CHECK(overlay.phase_x == 0.0F && overlay.phase_y == 0.0F);
  CHECK(overlay.angle_degrees == 0.0F);

  // Untouched resave: the raw Patt block re-emits byte-identically and the
  // writer does not add a duplicate pattern block for the covered id.
  const auto resaved = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto reread = patchy::psd::DocumentIo::read(resaved);
  std::vector<std::vector<std::uint8_t>> original_blocks;
  for (const auto& block : document.metadata().unknown_psd_resources) {
    if (block.key == "Patt" || block.key == "Pat2" || block.key == "Pat3") {
      original_blocks.push_back(block.payload);
    }
  }
  std::vector<std::vector<std::uint8_t>> reread_blocks;
  for (const auto& block : reread.metadata().unknown_psd_resources) {
    if (block.key == "Patt" || block.key == "Pat2" || block.key == "Pat3") {
      reread_blocks.push_back(block.payload);
    }
  }
  CHECK(original_blocks.size() == 1U);
  CHECK(reread_blocks == original_blocks);
  CHECK(reread.metadata().patterns.patterns.size() == 1U);
}

void psd_photoshop_pattern_transparent_fixture_decodes_alpha() {
  const auto document = patchy::psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path("photoshop-pattern-transparent.psd"));
  const auto* resource = document.metadata().patterns.find("7c444b0a-d81e-0e4e-a721-239e25f8fc4f");
  CHECK(resource != nullptr);
  const auto* opaque = resource->tile.pixel(1, 1);
  CHECK(opaque[0] == 255 && opaque[1] == 120 && opaque[2] == 0 && opaque[3] == 255);
  CHECK(resource->tile.pixel(5, 1)[3] == 128);  // 50% fill
  CHECK(resource->tile.pixel(5, 5)[3] == 0);    // untouched transparent quadrant

  // 16-bit grayscale pattern (PS trims the tile to its 1-px repeating unit).
  const auto deep = patchy::psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path("photoshop-pattern-deep.psd"));
  CHECK(deep.metadata().patterns.patterns.size() == 1U);
  const auto& deep_tile = deep.metadata().patterns.patterns.front().tile;
  CHECK(deep_tile.width() == 1 && deep_tile.height() == 8);
  CHECK(deep_tile.pixel(0, 0)[0] < deep_tile.pixel(0, 7)[0]);  // dark band above light band
  CHECK(deep_tile.pixel(0, 0)[0] == deep_tile.pixel(0, 0)[1]);
}

void psd_photoshop_bevel_subs_fixture_round_trips() {
  auto document = patchy::psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path("photoshop-bevel-subs.psd"));
  const auto* contour_layer = find_layer_named(document.layers(), "contourSub");
  const auto* texture_layer = find_layer_named(document.layers(), "textureSub");
  CHECK(contour_layer != nullptr && texture_layer != nullptr);
  const auto& contour_bevel = contour_layer->layer_style().bevels.front();
  CHECK(contour_bevel.contour.enabled);
  CHECK(!contour_bevel.texture.enabled);
  CHECK(contour_bevel.contour.anti_aliased);
  CHECK(std::abs(contour_bevel.contour.range - 0.73F) < 0.001F);
  CHECK(contour_bevel.contour.contour.points.size() == 4U);
  CHECK(contour_bevel.contour.contour.points[1].x == 80.0F);
  CHECK(contour_bevel.contour.contour.points[1].y == 255.0F);
  CHECK(contour_bevel.contour.contour.points[1].corner);   // Cnty=false in the file
  CHECK(!contour_bevel.contour.contour.points[2].corner);  // smooth point
  CHECK(contour_bevel.style == patchy::BevelEmbossStyleKind::OuterBevel);  // AM default
  CHECK(contour_bevel.technique == patchy::BevelTechnique::Smooth);
  const auto& texture_bevel = texture_layer->layer_style().bevels.front();
  CHECK(texture_bevel.texture.enabled);
  CHECK(texture_bevel.texture.invert);
  CHECK(!texture_bevel.texture.link_with_layer);
  CHECK(std::abs(texture_bevel.texture.scale - 1.52F) < 0.001F);
  CHECK(std::abs(texture_bevel.texture.depth + 0.37F) < 0.001F);
  CHECK(texture_bevel.texture.phase_x == -3.0F && texture_bevel.texture.phase_y == 5.0F);
  CHECK(texture_bevel.texture.pattern_id == "2317675a-e95e-b147-8612-bb6e28bcf146");
  CHECK(document.metadata().patterns.find(texture_bevel.texture.pattern_id) != nullptr);

  // Simulate an edit (drop the preserved style blocks) and require the
  // regenerated descriptors to re-read with identical modeled values —
  // including EXACT custom contour points, the better-than-Satin guarantee.
  for (auto& layer : document.layers()) {
    std::erase_if(layer.unknown_psd_blocks(), [](const patchy::UnknownPsdBlock& block) {
      return block.key == "lfx2" || block.key == "lrFX" || block.key == "plFX";
    });
  }
  const auto regenerated = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto reread = patchy::psd::DocumentIo::read(regenerated);
  const auto* contour_reread = find_layer_named(reread.layers(), "contourSub");
  const auto* texture_reread = find_layer_named(reread.layers(), "textureSub");
  CHECK(contour_reread != nullptr && texture_reread != nullptr);
  const auto& contour_after = contour_reread->layer_style().bevels.front();
  CHECK(contour_after.contour.enabled == contour_bevel.contour.enabled);
  CHECK(contour_after.contour.anti_aliased == contour_bevel.contour.anti_aliased);
  CHECK(contour_after.contour.contour.points == contour_bevel.contour.contour.points);
  CHECK(contour_after.contour.contour.name == contour_bevel.contour.contour.name);
  CHECK(contour_after.style == contour_bevel.style);
  CHECK(contour_after.technique == contour_bevel.technique);
  const auto& texture_after = texture_reread->layer_style().bevels.front();
  CHECK(texture_after.texture.enabled);
  CHECK(texture_after.texture.invert == texture_bevel.texture.invert);
  CHECK(texture_after.texture.link_with_layer == texture_bevel.texture.link_with_layer);
  CHECK(texture_after.texture.pattern_id == texture_bevel.texture.pattern_id);
  CHECK(texture_after.texture.phase_x == texture_bevel.texture.phase_x);
  CHECK(texture_after.texture.phase_y == texture_bevel.texture.phase_y);
  // The texture's pattern still resolves after the edited save: its pixels ride
  // the preserved raw Patt block.
  CHECK(reread.metadata().patterns.find(texture_bevel.texture.pattern_id) != nullptr);
}

void psd_photoshop_pattern_bevel_roundtrip_fixture_imports() {
  // Photoshop 2026's resave of a Patchy-authored file (built-in Bricks overlay +
  // Ring contour sub + Bumps texture sub). PS opened it without warnings and
  // returned every modeled value through Action Manager; this pins the return trip.
  const auto document = patchy::psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path("photoshop-pattern-bevel-roundtrip.psd"));
  const auto* styled = find_layer_named(document.layers(), "styled");
  CHECK(styled != nullptr);
  const auto& style = styled->layer_style();
  CHECK(style.pattern_overlays.size() == 1U);
  const auto& overlay = style.pattern_overlays.front();
  CHECK(overlay.enabled);
  CHECK(overlay.blend_mode == patchy::BlendMode::Multiply);
  CHECK(std::abs(overlay.opacity - 0.8F) < 0.01F);
  CHECK(std::abs(overlay.scale - 2.0F) < 0.01F);
  CHECK(overlay.pattern_id == "c4a11e00-000a-4b1d-9c3e-7a7c9e55b00a");
  CHECK(style.bevels.size() == 1U);
  const auto& bevel = style.bevels.front();
  CHECK(bevel.contour.enabled);
  CHECK(bevel.contour.anti_aliased);
  CHECK(std::abs(bevel.contour.range - 0.75F) < 0.01F);
  CHECK(bevel.contour.contour.points.size() == 3U);
  CHECK(bevel.contour.contour.points[1].x == 128.0F && bevel.contour.contour.points[1].y == 255.0F);
  CHECK(bevel.texture.enabled);
  CHECK(std::abs(bevel.texture.depth - 2.0F) < 0.01F);
  CHECK(bevel.texture.pattern_id == "c4a11e00-0009-4b1d-9c3e-7a7c9e55b009");
  // Photoshop re-embedded both built-in tiles in its own pattern block.
  CHECK(document.metadata().patterns.find("c4a11e00-000a-4b1d-9c3e-7a7c9e55b00a") != nullptr);
  CHECK(document.metadata().patterns.find("c4a11e00-0009-4b1d-9c3e-7a7c9e55b009") != nullptr);
}

void psd_pattern_overlay_added_in_patchy_writes_pattern_block() {
  patchy::Document document(32, 32, patchy::PixelFormat::rgba8());
  auto& layer = document.add_pixel_layer("Styled", solid_rgba(32, 32, 120, 120, 120, 255));
  const auto presets = patchy::builtin_pattern_presets();
  patchy::LayerPatternOverlay overlay;
  overlay.enabled = true;
  overlay.pattern_id = presets[0].id;
  overlay.pattern_name = presets[0].english_name;
  overlay.scale = 2.5F;
  overlay.phase_x = 3.0F;
  overlay.link_with_layer = false;
  layer.layer_style().pattern_overlays.push_back(overlay);
  patchy::LayerBevelEmboss bevel;
  bevel.enabled = true;
  bevel.texture.enabled = true;
  bevel.texture.pattern_id = presets[8].id;  // Bumps
  bevel.texture.pattern_name = presets[8].english_name;
  bevel.texture.depth = -2.5F;
  layer.layer_style().bevels.push_back(bevel);
  document.metadata().patterns.adopt(patchy::builtin_pattern_resource(presets[0].id));
  document.metadata().patterns.adopt(patchy::builtin_pattern_resource(presets[8].id));
  // An unreferenced store entry must NOT be written (orphans prune at save).
  document.metadata().patterns.adopt(patchy::builtin_pattern_resource(presets[3].id));

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  CHECK(patchy::psd::DocumentIo::write_layered_rgb8(document) == bytes);  // deterministic
  const auto reread = patchy::psd::DocumentIo::read(bytes);
  CHECK(reread.metadata().patterns.patterns.size() == 2U);
  const auto* checker = reread.metadata().patterns.find(presets[0].id);
  CHECK(checker != nullptr);
  const auto reference = patchy::generate_builtin_pattern_tile(presets[0].id);
  CHECK(checker->tile.width() == reference.width() && checker->tile.height() == reference.height());
  CHECK(std::equal(checker->tile.data().begin(), checker->tile.data().end(), reference.data().begin(),
                   reference.data().end()));
  const auto& style = reread.layers().front().layer_style();
  CHECK(style.pattern_overlays.size() == 1U);
  CHECK(std::abs(style.pattern_overlays.front().scale - 2.5F) < 0.001F);
  CHECK(style.pattern_overlays.front().phase_x == 3.0F);
  CHECK(!style.pattern_overlays.front().link_with_layer);
  CHECK(style.bevels.size() == 1U);
  CHECK(style.bevels.front().texture.enabled);
  CHECK(style.bevels.front().texture.depth == -2.5F);
}

void compositor_renders_layer_style_pattern_overlay() {
  patchy::Document document(12, 12, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Base", solid_rgb(12, 12, 255, 255, 255));
  patchy::Layer styled_layer(document.allocate_layer_id(), "Patterned", solid_rgba(6, 6, 120, 120, 120, 255));
  auto& layer = document.add_layer(std::move(styled_layer));
  layer.set_bounds(patchy::Rect{3, 3, 6, 6});

  patchy::PatternResource checker;
  checker.id = "test-checker";
  checker.name = "Test Checker";
  checker.tile = patchy::PixelBuffer(2, 2, patchy::PixelFormat::rgba8());
  const auto set_px = [&](int x, int y, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    auto* px = checker.tile.pixel(x, y);
    px[0] = r;
    px[1] = g;
    px[2] = b;
    px[3] = 255;
  };
  set_px(0, 0, 200, 40, 40);
  set_px(1, 0, 40, 90, 200);
  set_px(0, 1, 40, 90, 200);
  set_px(1, 1, 200, 40, 40);
  document.metadata().patterns.adopt(checker);

  patchy::LayerPatternOverlay overlay;
  overlay.enabled = true;
  overlay.pattern_id = "test-checker";
  layer.layer_style().pattern_overlays.push_back(overlay);

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  // Document-origin anchoring: pixel (3,3) samples tile cell (3%2, 3%2) = (1,1).
  const auto* inside = flattened.pixel(3, 3);
  CHECK(inside[0] == 200 && inside[1] == 40 && inside[2] == 40);
  const auto* next = flattened.pixel(4, 3);
  CHECK(next[0] == 40 && next[1] == 90 && next[2] == 200);
  // Outside the layer the base stays untouched.
  const auto* outside = flattened.pixel(1, 1);
  CHECK(outside[0] == 255 && outside[1] == 255 && outside[2] == 255);

  // A missing pattern id renders exactly nothing.
  layer.layer_style().pattern_overlays.front().pattern_id = "no-such-pattern";
  const auto missing = patchy::Compositor{}.flatten_rgb8(document);
  const auto* untouched = missing.pixel(3, 3);
  CHECK(untouched[0] == 120 && untouched[1] == 120 && untouched[2] == 120);
}

void compositor_bevel_gloss_and_contour_subs_change_lighting() {
  const auto render_with = [](auto configure) {
    patchy::Document document(40, 40, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Base", solid_rgb(40, 40, 255, 255, 255));
    patchy::Layer styled_layer(document.allocate_layer_id(), "Bevel",
                               solid_rgba(28, 28, 120, 120, 120, 255));
    auto& layer = document.add_layer(std::move(styled_layer));
    layer.set_bounds(patchy::Rect{6, 6, 28, 28});
    patchy::LayerBevelEmboss bevel;
    bevel.enabled = true;
    bevel.highlight_blend_mode = patchy::BlendMode::Normal;
    bevel.highlight_opacity = 1.0F;
    bevel.shadow_blend_mode = patchy::BlendMode::Normal;
    bevel.shadow_opacity = 1.0F;
    bevel.size = 8.0F;
    configure(bevel, document);
    layer.layer_style().bevels.push_back(bevel);
    return patchy::Compositor{}.flatten_rgb8(document);
  };

  const auto plain = render_with([](patchy::LayerBevelEmboss&, patchy::Document&) {});
  // An explicit two-point Linear gloss contour must stay bit-identical.
  const auto linear_gloss = render_with([](patchy::LayerBevelEmboss& bevel, patchy::Document&) {
    bevel.gloss_contour.points = {patchy::StyleContourPoint{0.0F, 0.0F, false},
                                  patchy::StyleContourPoint{255.0F, 255.0F, false}};
  });
  CHECK(std::equal(plain.data().begin(), plain.data().end(), linear_gloss.data().begin(),
                   linear_gloss.data().end()));

  const auto* ring = patchy::find_builtin_contour_preset("contour.ring");
  CHECK(ring != nullptr);
  const auto ring_gloss = render_with([&](patchy::LayerBevelEmboss& bevel, patchy::Document&) {
    bevel.gloss_contour = ring->contour;
  });
  CHECK(!std::equal(plain.data().begin(), plain.data().end(), ring_gloss.data().begin(),
                    ring_gloss.data().end()));
  // Ring gloss maps flat-face lighting (0) to full highlight.
  const auto* face = ring_gloss.pixel(20, 20);
  CHECK(face[0] > 250 && face[1] > 250 && face[2] > 250);

  // The Contour sub with Ring flips the profile mid-band: the top edge gains a
  // shadow run where the plain bevel is pure highlight.
  const auto ring_sub = render_with([&](patchy::LayerBevelEmboss& bevel, patchy::Document&) {
    bevel.contour.enabled = true;
    bevel.contour.contour = ring->contour;
    bevel.contour.range = 1.0F;
  });
  CHECK(!std::equal(plain.data().begin(), plain.data().end(), ring_sub.data().begin(),
                    ring_sub.data().end()));
  int plain_dark = 0;
  int ring_dark = 0;
  for (int y = 7; y < 13; ++y) {
    if (plain.pixel(20, y)[0] < 100) {
      ++plain_dark;
    }
    if (ring_sub.pixel(20, y)[0] < 100) {
      ++ring_dark;
    }
  }
  CHECK(plain_dark == 0);
  CHECK(ring_dark > 0);

  // A Linear sub contour at full range stays bit-identical to the plain bevel.
  const auto linear_sub = render_with([](patchy::LayerBevelEmboss& bevel, patchy::Document&) {
    bevel.contour.enabled = true;
    bevel.contour.range = 1.0F;
  });
  CHECK(std::equal(plain.data().begin(), plain.data().end(), linear_sub.data().begin(),
                   linear_sub.data().end()));
}

void compositor_bevel_texture_responds_to_depth_and_invert() {
  const auto render_with = [](float depth, bool invert) {
    patchy::Document document(40, 40, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Base", solid_rgb(40, 40, 255, 255, 255));
    patchy::Layer styled_layer(document.allocate_layer_id(), "Textured",
                               solid_rgba(28, 28, 120, 120, 120, 255));
    auto& layer = document.add_layer(std::move(styled_layer));
    layer.set_bounds(patchy::Rect{6, 6, 28, 28});
    document.metadata().patterns.adopt(
        patchy::builtin_pattern_resource(patchy::builtin_pattern_presets()[0].id));  // Checkerboard
    patchy::LayerBevelEmboss bevel;
    bevel.enabled = true;
    bevel.highlight_blend_mode = patchy::BlendMode::Normal;
    bevel.highlight_opacity = 1.0F;
    bevel.shadow_blend_mode = patchy::BlendMode::Normal;
    bevel.shadow_opacity = 1.0F;
    bevel.size = 6.0F;
    bevel.texture.enabled = depth != 0.0F || invert;
    bevel.texture.pattern_id = patchy::builtin_pattern_presets()[0].id;
    bevel.texture.depth = depth;
    bevel.texture.invert = invert;
    layer.layer_style().bevels.push_back(bevel);
    return patchy::Compositor{}.flatten_rgb8(document);
  };

  const auto plain = render_with(0.0F, false);
  const auto textured = render_with(1.0F, false);
  CHECK(!std::equal(plain.data().begin(), plain.data().end(), textured.data().begin(),
                    textured.data().end()));
  // The bump shades the whole face, not just the bevel band.
  bool face_changed = false;
  for (int y = 16; y < 24 && !face_changed; ++y) {
    for (int x = 16; x < 24 && !face_changed; ++x) {
      face_changed = plain.pixel(x, y)[0] != textured.pixel(x, y)[0];
    }
  }
  CHECK(face_changed);
  // Invert equals negated depth, and renders are deterministic.
  const auto inverted = render_with(1.0F, true);
  const auto negated = render_with(-1.0F, false);
  CHECK(std::equal(inverted.data().begin(), inverted.data().end(), negated.data().begin(),
                   negated.data().end()));
  const auto repeated = render_with(1.0F, false);
  CHECK(std::equal(textured.data().begin(), textured.data().end(), repeated.data().begin(),
                   repeated.data().end()));
}

void psd_writer_uses_preserved_photoshop_style_blocks_without_private_duplicates() {
  patchy::Document document(3, 3, patchy::PixelFormat::rgb8());
  auto& layer = document.add_pixel_layer("Photoshop Style", solid_rgba(3, 3, 120, 80, 40, 255));

  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.color = patchy::RgbColor{10, 20, 30};
  shadow.opacity = 0.5F;
  layer.layer_style().drop_shadows.push_back(shadow);
  const std::vector<std::uint8_t> photoshop_style_payload{1, 2, 3, 4};
  layer.unknown_psd_blocks().push_back(patchy::UnknownPsdBlock{"lfx2", photoshop_style_payload});

  const auto extra_data = psd_first_layer_extra_data(patchy::psd::DocumentIo::write_layered_rgb8(document));
  CHECK(psd_layer_block_payload(extra_data, "lfx2").value() == photoshop_style_payload);
  CHECK(!psd_layer_block_payload(extra_data, "plFX").has_value());
}

void psd_arrows_imports_photoshop_inner_effects() {
  const auto path = arrows_fixture_path();
  CHECK(std::filesystem::exists(path));

  const auto document = patchy::psd::DocumentIo::read_file(path);
  const auto* layer = find_layer_named(document.layers(), "Layer 3 copy");
  CHECK(layer != nullptr);
  CHECK(layer_has_psd_block(*layer, "lfx2"));
  CHECK(layer_has_psd_block(*layer, "lrFX"));

  const auto* inner_shadow = first_enabled_inner_shadow(*layer);
  CHECK(inner_shadow != nullptr);
  CHECK(inner_shadow->blend_mode == patchy::BlendMode::Multiply);
  CHECK(inner_shadow->color.red == 0);
  CHECK(close_float(inner_shadow->opacity, 0.75F));
  CHECK(close_float(inner_shadow->distance, 0.0F));
  CHECK(close_float(inner_shadow->size, 24.0F));

  const auto* inner_glow = first_enabled_inner_glow(*layer);
  CHECK(inner_glow != nullptr);
  CHECK(inner_glow->blend_mode == patchy::BlendMode::Screen);
  CHECK(inner_glow->color.red == 255);
  CHECK(inner_glow->color.green == 255);
  CHECK(inner_glow->color.blue == 190);
  CHECK(close_float(inner_glow->opacity, 0.75F));
  CHECK(close_float(inner_glow->size, 5.0F));
  CHECK(inner_glow->source == patchy::LayerInnerGlowSource::Edge);

  CHECK(layer->layer_style().outer_glows.size() == 1);

  const auto* shape = find_layer_named(document.layers(), "Shape 1");
  CHECK(shape != nullptr);
  CHECK(first_enabled_drop_shadow(*shape) != nullptr);
  CHECK(!shape->layer_style().gradient_fills.empty());
  CHECK(!shape->layer_style().strokes.empty());

  const auto round_tripped =
      patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(document));
  const auto* round_tripped_layer = find_layer_named(round_tripped.layers(), "Layer 3 copy");
  CHECK(round_tripped_layer != nullptr);
  CHECK(layer_has_psd_block(*round_tripped_layer, "lfx2"));
  CHECK(layer_has_psd_block(*round_tripped_layer, "lrFX"));
  CHECK(first_enabled_inner_shadow(*round_tripped_layer) != nullptr);
  CHECK(first_enabled_inner_glow(*round_tripped_layer) != nullptr);
}

const patchy::LayerOuterGlow* first_enabled_outer_glow(const patchy::Layer& layer) {
  const auto& glows = layer.layer_style().outer_glows;
  const auto found = std::find_if(glows.begin(), glows.end(), [](const patchy::LayerOuterGlow& glow) {
    return glow.enabled;
  });
  return found == glows.end() ? nullptr : &*found;
}

// Photoshop 2026 authored both fixtures via COM (July 2026): white shapes on black with
// Screen-mode Softer outer glows, saved alongside Photoshop's own flatten as BMP.
// photoshop-outer-glow-range.psd sweeps Quality > Range (25/50/80/100) plus small sizes
// (1..5) and a fractional spread; every straight-edge profile pinned the tent kernel,
// the integer spread radius, and the 100/range gain, and the flatten comparison holds
// within 3/255. photoshop-outer-glow.psd carries Range-100 shapes including a spread-50
// size-40 dot (whose radius-20 expansion exercises the chamfer fallback) and a hard
// spread-100 band whose corner arcs differ from the area-sampled disc by design, so its
// bounds are looser.
void psd_photoshop_outer_glow_fixtures_match_render() {
  const auto range_path = patchy::test::committed_psd_fixture_path("photoshop-outer-glow-range.psd");
  const auto range_bmp = range_path.parent_path() / "photoshop-outer-glow-range.bmp";
  CHECK(std::filesystem::exists(range_path));
  CHECK(std::filesystem::exists(range_bmp));

  const auto document = patchy::psd::DocumentIo::read_file(range_path);
  const auto* bar50 = find_layer_named(document.layers(), "bar50");
  CHECK(bar50 != nullptr);
  const auto* bar50_glow = first_enabled_outer_glow(*bar50);
  CHECK(bar50_glow != nullptr);
  CHECK(bar50_glow->technique == patchy::LayerGlowTechnique::Softer);
  CHECK(bar50_glow->blend_mode == patchy::BlendMode::Screen);
  CHECK(close_float(bar50_glow->size, 17.0F));
  CHECK(close_float(bar50_glow->spread, 8.0F));
  CHECK(close_float(bar50_glow->opacity, 0.35F));
  CHECK(close_float(bar50_glow->range, 50.0F));
  const auto* sq25 = find_layer_named(document.layers(), "sq25");
  CHECK(sq25 != nullptr);
  const auto* sq25_glow = first_enabled_outer_glow(*sq25);
  CHECK(sq25_glow != nullptr);
  CHECK(close_float(sq25_glow->range, 25.0F));
  const auto* bar100 = find_layer_named(document.layers(), "bar100");
  CHECK(bar100 != nullptr);
  const auto* bar100_glow = first_enabled_outer_glow(*bar100);
  CHECK(bar100_glow != nullptr);
  CHECK(close_float(bar100_glow->range, 100.0F));

  const auto photoshop_render = patchy::bmp::DocumentIo::read_file(range_bmp);
  const auto reference_flat = patchy::Compositor{}.flatten_rgb8(photoshop_render);
  const auto patchy_flat = patchy::Compositor{}.flatten_rgb8(document);
  const auto metrics = rgb_diff_metrics(reference_flat, patchy_flat);
  CHECK(metrics.max_channel_delta <= 3);
  CHECK(metrics.mean_abs_channel_delta <= 0.10);

  // GlwT and Inpr survive a Patchy re-save.
  const auto round_tripped =
      patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(document));
  const auto* round_tripped_sq25 = find_layer_named(round_tripped.layers(), "sq25");
  CHECK(round_tripped_sq25 != nullptr);
  const auto* round_tripped_glow = first_enabled_outer_glow(*round_tripped_sq25);
  CHECK(round_tripped_glow != nullptr);
  CHECK(round_tripped_glow->technique == patchy::LayerGlowTechnique::Softer);
  CHECK(close_float(round_tripped_glow->range, 25.0F));

  const auto extreme_path = patchy::test::committed_psd_fixture_path("photoshop-outer-glow.psd");
  const auto extreme_bmp = extreme_path.parent_path() / "photoshop-outer-glow.bmp";
  CHECK(std::filesystem::exists(extreme_path));
  CHECK(std::filesystem::exists(extreme_bmp));
  const auto extreme_document = patchy::psd::DocumentIo::read_file(extreme_path);
  const auto extreme_reference =
      patchy::Compositor{}.flatten_rgb8(patchy::bmp::DocumentIo::read_file(extreme_bmp));
  const auto extreme_flat = patchy::Compositor{}.flatten_rgb8(extreme_document);
  const auto extreme_metrics = rgb_diff_metrics(extreme_reference, extreme_flat);
  CHECK(extreme_metrics.mean_abs_channel_delta <= 1.2);
  std::uint64_t over_aa_tolerance = 0;
  for (std::int32_t y = 0; y < extreme_reference.height(); ++y) {
    for (std::int32_t x = 0; x < extreme_reference.width(); ++x) {
      const auto* a = extreme_reference.pixel(x, y);
      const auto* b = extreme_flat.pixel(x, y);
      int max_delta = 0;
      for (int channel = 0; channel < 3; ++channel) {
        max_delta = std::max(max_delta,
                             std::abs(static_cast<int>(a[channel]) - static_cast<int>(b[channel])));
      }
      if (max_delta > 6) {
        ++over_aa_tolerance;
      }
    }
  }
  const auto extreme_pixels = static_cast<double>(extreme_reference.width()) *
                              static_cast<double>(extreme_reference.height());
  CHECK(static_cast<double>(over_aa_tolerance) / extreme_pixels <= 0.08);
}

double fraction_over_delta(const patchy::PixelBuffer& reference, const patchy::PixelBuffer& rendered,
                           int tolerance) {
  std::uint64_t over = 0;
  for (std::int32_t y = 0; y < reference.height(); ++y) {
    for (std::int32_t x = 0; x < reference.width(); ++x) {
      const auto* a = reference.pixel(x, y);
      const auto* b = rendered.pixel(x, y);
      int max_delta = 0;
      for (int channel = 0; channel < 3; ++channel) {
        max_delta = std::max(max_delta,
                             std::abs(static_cast<int>(a[channel]) - static_cast<int>(b[channel])));
      }
      if (max_delta > tolerance) {
        ++over;
      }
    }
  }
  return static_cast<double>(over) /
         (static_cast<double>(reference.width()) * static_cast<double>(reference.height()));
}

// Photoshop 2026 authored all three inner-effect fixtures via COM (July 2026):
// mid-gray backdrops with filled rectangles and bars, a square-with-hole, and
// an anti-aliased ellipse. They pin the calibrated interior pipeline (inverse
// matte -> integer choke dilation -> tent blur -> Range gain, Center source =
// complement of the gained Edge field) and the ColorDodge effect-alpha fold.
void psd_photoshop_inner_glow_fixtures_match_render() {
  const auto range_path = patchy::test::committed_psd_fixture_path("photoshop-inner-glow-range.psd");
  const auto range_bmp = range_path.parent_path() / "photoshop-inner-glow-range.bmp";
  CHECK(std::filesystem::exists(range_path));
  CHECK(std::filesystem::exists(range_bmp));

  const auto document = patchy::psd::DocumentIo::read_file(range_path);
  const auto* sq25 = find_layer_named(document.layers(), "sq25");
  CHECK(sq25 != nullptr);
  const auto* sq25_glow = first_enabled_inner_glow(*sq25);
  CHECK(sq25_glow != nullptr);
  CHECK(sq25_glow->technique == patchy::LayerGlowTechnique::Softer);
  CHECK(close_float(sq25_glow->range, 25.0F));
  CHECK(close_float(sq25_glow->size, 18.0F));
  CHECK(sq25_glow->source == patchy::LayerInnerGlowSource::Edge);
  const auto* bar50 = find_layer_named(document.layers(), "bar50");
  CHECK(bar50 != nullptr);
  const auto* bar50_glow = first_enabled_inner_glow(*bar50);
  CHECK(bar50_glow != nullptr);
  CHECK(bar50_glow->blend_mode == patchy::BlendMode::Screen);
  CHECK(close_float(bar50_glow->size, 17.0F));
  CHECK(close_float(bar50_glow->choke, 8.0F));
  CHECK(close_float(bar50_glow->opacity, 0.35F));
  CHECK(close_float(bar50_glow->range, 50.0F));
  const auto* ctr50 = find_layer_named(document.layers(), "ctr50");
  CHECK(ctr50 != nullptr);
  const auto* ctr50_glow = first_enabled_inner_glow(*ctr50);
  CHECK(ctr50_glow != nullptr);
  CHECK(ctr50_glow->source == patchy::LayerInnerGlowSource::Center);
  CHECK(close_float(ctr50_glow->range, 50.0F));

  const auto reference_flat =
      patchy::Compositor{}.flatten_rgb8(patchy::bmp::DocumentIo::read_file(range_bmp));
  const auto patchy_flat = patchy::Compositor{}.flatten_rgb8(document);
  const auto metrics = rgb_diff_metrics(reference_flat, patchy_flat);
  // Straight-edge profiles are byte-exact (the compositor unit tests pin them);
  // the residual is ~20 pixels of the ColorDodge arm's two-edge overlap where
  // Photoshop's between-pass tent rounding shifts the mask ~1/255 and dodge
  // amplifies it several-fold.
  CHECK(metrics.max_channel_delta <= 6);
  CHECK(metrics.mean_abs_channel_delta <= 0.10);

  // GlwT and Inpr survive a Patchy re-save.
  const auto round_tripped =
      patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(document));
  const auto* round_tripped_sq25 = find_layer_named(round_tripped.layers(), "sq25");
  CHECK(round_tripped_sq25 != nullptr);
  const auto* round_tripped_glow = first_enabled_inner_glow(*round_tripped_sq25);
  CHECK(round_tripped_glow != nullptr);
  CHECK(round_tripped_glow->technique == patchy::LayerGlowTechnique::Softer);
  CHECK(close_float(round_tripped_glow->range, 25.0F));

  // Extremes: choke-100 hard band around a hole, size 40, AA ellipse with
  // choke, small Center square with choke past the exact-dilation limit.
  const auto extreme_path = patchy::test::committed_psd_fixture_path("photoshop-inner-glow.psd");
  const auto extreme_bmp = extreme_path.parent_path() / "photoshop-inner-glow.bmp";
  CHECK(std::filesystem::exists(extreme_path));
  CHECK(std::filesystem::exists(extreme_bmp));
  const auto extreme_document = patchy::psd::DocumentIo::read_file(extreme_path);
  const auto extreme_reference =
      patchy::Compositor{}.flatten_rgb8(patchy::bmp::DocumentIo::read_file(extreme_bmp));
  const auto extreme_flat = patchy::Compositor{}.flatten_rgb8(extreme_document);
  const auto extreme_metrics = rgb_diff_metrics(extreme_reference, extreme_flat);
  CHECK(extreme_metrics.mean_abs_channel_delta <= 1.2);
  CHECK(fraction_over_delta(extreme_reference, extreme_flat, 6) <= 0.08);
}

void psd_photoshop_inner_shadow_fixture_matches_render() {
  const auto path = patchy::test::committed_psd_fixture_path("photoshop-inner-shadow.psd");
  const auto bmp = path.parent_path() / "photoshop-inner-shadow.bmp";
  CHECK(std::filesystem::exists(path));
  CHECK(std::filesystem::exists(bmp));

  const auto document = patchy::psd::DocumentIo::read_file(path);
  const auto* choke50 = find_layer_named(document.layers(), "choke50");
  CHECK(choke50 != nullptr);
  const auto* choke50_shadow = first_enabled_inner_shadow(*choke50);
  CHECK(choke50_shadow != nullptr);
  CHECK(close_float(choke50_shadow->choke, 50.0F));
  CHECK(close_float(choke50_shadow->size, 18.0F));
  const auto* dist = find_layer_named(document.layers(), "dist");
  CHECK(dist != nullptr);
  const auto* dist_shadow = first_enabled_inner_shadow(*dist);
  CHECK(dist_shadow != nullptr);
  CHECK(close_float(dist_shadow->distance, 5.0F));
  CHECK(close_float(dist_shadow->size, 7.0F));

  const auto reference_flat =
      patchy::Compositor{}.flatten_rgb8(patchy::bmp::DocumentIo::read_file(bmp));
  const auto patchy_flat = patchy::Compositor{}.flatten_rgb8(document);
  const auto metrics = rgb_diff_metrics(reference_flat, patchy_flat);
  CHECK(metrics.max_channel_delta <= 3);
  CHECK(metrics.mean_abs_channel_delta <= 0.10);
}

// Photoshop 2026 authored photoshop-gradient-overlay-geometry.psd via COM
// (July 2026, the capsule_v_top calibration): aliased rectangles carrying one
// Gradient Overlay each - reflected on even/odd extents at angles 90/0,
// radial at angles 90/0, diamond, conical with an asymmetric red stop, and
// linear, all at non-100 Scale - plus an "ordering" rectangle stacking a 15%
// linear overlay under a ColorDodge inner glow under a Multiply inner shadow.
// It pins the pixel-snapped gradient center, the whole-pixel half-ramp
// floor(projected_span * scale / 2) shared by every point-mapped type, the
// isotropic circular/L1 radial and diamond shapes, the scale-free conical
// sweep (quarter-sweep color on the singular center pixel), and the interior
// stack order overlays -> inner glow -> inner shadow.
void psd_photoshop_gradient_overlay_geometry_fixture_matches_render() {
  const auto path = patchy::test::committed_psd_fixture_path("photoshop-gradient-overlay-geometry.psd");
  const auto bmp = path.parent_path() / "photoshop-gradient-overlay-geometry.bmp";
  CHECK(std::filesystem::exists(path));
  CHECK(std::filesystem::exists(bmp));

  const auto document = patchy::psd::DocumentIo::read_file(path);
  const auto first_enabled_gradient_fill = [](const patchy::Layer& layer) -> const patchy::LayerGradientFill* {
    const auto& fills = layer.layer_style().gradient_fills;
    const auto found = std::find_if(fills.begin(), fills.end(),
                                    [](const patchy::LayerGradientFill& fill) { return fill.enabled; });
    return found == fills.end() ? nullptr : &*found;
  };
  const auto expect_gradient = [&](const char* layer_name, patchy::LayerStyleGradientType type,
                                   float angle) {
    const auto* layer = find_layer_named(document.layers(), layer_name);
    CHECK(layer != nullptr);
    const auto* fill = first_enabled_gradient_fill(*layer);
    CHECK(fill != nullptr);
    CHECK(fill->gradient.type == type);
    CHECK(close_float(fill->gradient.angle_degrees, angle));
    CHECK(fill->gradient.align_with_layer);
    return fill;
  };
  const auto* refl_even = expect_gradient("reflEven", patchy::LayerStyleGradientType::Reflected, 90.0F);
  CHECK(close_float(refl_even->gradient.scale, 1.46F));
  expect_gradient("reflOdd", patchy::LayerStyleGradientType::Reflected, 0.0F);
  expect_gradient("radial", patchy::LayerStyleGradientType::Radial, 90.0F);
  const auto* radial0 = expect_gradient("radial0", patchy::LayerStyleGradientType::Radial, 0.0F);
  CHECK(close_float(radial0->gradient.scale, 1.33F));
  expect_gradient("diamond", patchy::LayerStyleGradientType::Diamond, 90.0F);
  expect_gradient("conical", patchy::LayerStyleGradientType::Angle, 90.0F);
  expect_gradient("linear", patchy::LayerStyleGradientType::Linear, 90.0F);

  const auto* ordering = find_layer_named(document.layers(), "ordering");
  CHECK(ordering != nullptr);
  CHECK(first_enabled_gradient_fill(*ordering) != nullptr);
  const auto* ordering_shadow = first_enabled_inner_shadow(*ordering);
  CHECK(ordering_shadow != nullptr);
  CHECK(close_float(ordering_shadow->distance, 5.0F));
  const auto* ordering_glow = first_enabled_inner_glow(*ordering);
  CHECK(ordering_glow != nullptr);
  CHECK(ordering_glow->blend_mode == patchy::BlendMode::ColorDodge);
  CHECK(close_float(ordering_glow->range, 50.0F));

  const auto reference_flat =
      patchy::Compositor{}.flatten_rgb8(patchy::bmp::DocumentIo::read_file(bmp));
  const auto patchy_flat = patchy::Compositor{}.flatten_rgb8(document);
  const auto metrics = rgb_diff_metrics(reference_flat, patchy_flat);
  // Gradient arms are within 1/255 everywhere; the ordering arm's ColorDodge
  // two-edge overlap rounding reaches 6 (the same class as the inner-glow
  // range fixture).
  CHECK(metrics.max_channel_delta <= 6);
  CHECK(metrics.mean_abs_channel_delta <= 0.30);
}

// Photoshop 2026 authored photoshop-bevel-texture-{ramp,clouds}.psd/.bmp via
// COM (July 2026): 256x256 crops of a CS-era painted PSD (tree_world_a) whose
// "Shading" layer carries a smooth inner bevel (size 5, altitude 30) with the
// Texture sub-option. Both files store the texture pattern in a mode-7
// (Multichannel) Patt block, which Photoshop preserved through the resave and
// reads exactly like a grayscale plane (byte-patching mode 7 to 1 rendered
// byte-identically), so the fixtures pin the multichannel pattern decode
// end to end. The ramp variant's plane bytes were byte-patched to a
// low-contrast horizontal triangle wave (period 64, amplitude 32/255) with
// texture scale/depth normalized to 100%: its stripes sit in the LINEAR
// shading regime, pinning the recalibrated texture gain (bump plane feeds
// the height field with no extra amplitude; the old checker-calibrated 6x
// gain only fit because hard edges saturate, and overshot ~5x on gentle
// slopes). The clouds variant keeps the original Clouds texture at scale
// 162% / depth 95%, the real-world regression this calibration fixed.
void psd_photoshop_bevel_texture_fixtures_match_render() {
  const struct {
    const char* base;
    double max_mean;
    double max_over_tolerance_fraction;
  } fixtures[] = {
      // Capture-time metrics: ramp mean 0.79, over-6 0.20%; clouds mean 1.84,
      // over-6 1.56% (texture magnification filtering and bevel edge AA).
      {"photoshop-bevel-texture-ramp", 1.2, 0.005},
      {"photoshop-bevel-texture-clouds", 2.5, 0.03},
  };
  for (const auto& fixture : fixtures) {
    const auto psd_path =
        patchy::test::committed_psd_fixture_path(std::string(fixture.base) + ".psd");
    const auto bmp_path = psd_path.parent_path() / (std::string(fixture.base) + ".bmp");
    CHECK(std::filesystem::exists(psd_path));
    CHECK(std::filesystem::exists(bmp_path));
    const auto document = patchy::psd::DocumentIo::read_file(psd_path);
    const auto* shading = find_layer_named(document.layers(), "Shading");
    CHECK(shading != nullptr);
    CHECK(shading->layer_style().bevels.size() == 1);
    const auto& bevel = shading->layer_style().bevels.front();
    CHECK(bevel.enabled);
    CHECK(bevel.texture.enabled);
    CHECK(bevel.texture.pattern_id == "cffe046e-c525-11da-8325-c0b12431a07b");
    // The mode-7 Patt block must decode into a usable tile.
    const auto* pattern = document.metadata().patterns.find(bevel.texture.pattern_id);
    CHECK(pattern != nullptr);
    CHECK(!pattern->tile.empty());
    CHECK(pattern->tile.width() == 128 && pattern->tile.height() == 128);

    const auto reference =
        patchy::Compositor{}.flatten_rgb8(patchy::bmp::DocumentIo::read_file(bmp_path));
    const auto flat = patchy::Compositor{}.flatten_rgb8(document);
    const auto metrics = rgb_diff_metrics(reference, flat);
    CHECK(metrics.mean_abs_channel_delta <= fixture.max_mean);
    std::uint64_t over_tolerance = 0;
    for (std::int32_t y = 0; y < reference.height(); ++y) {
      for (std::int32_t x = 0; x < reference.width(); ++x) {
        const auto* a = reference.pixel(x, y);
        const auto* b = flat.pixel(x, y);
        int max_delta = 0;
        for (int channel = 0; channel < 3; ++channel) {
          max_delta = std::max(
              max_delta, std::abs(static_cast<int>(a[channel]) - static_cast<int>(b[channel])));
        }
        if (max_delta > 6) {
          ++over_tolerance;
        }
      }
    }
    const auto total_pixels =
        static_cast<double>(reference.width()) * static_cast<double>(reference.height());
    CHECK(static_cast<double>(over_tolerance) / total_pixels <=
          fixture.max_over_tolerance_fraction);
  }
}

// Photoshop 2026 authored photoshop-bevel-smooth.psd via COM (July 2026): mid-tone
// rectangles and a thin bar on white with smooth inner bevels at sizes 5/10,
// altitudes 30/60, and the Contour sub-option off / Linear-Range-50 /
// Linear-Range-100, saved alongside Photoshop's own flatten. It pins the
// calibrated Lambert bevel: tent height field `size` pixels deep, highlight
// (L - sin(alt)) / (1 - sin(alt)), shadow (sin(alt) - L) / sin(alt), and the
// linear-contour Range slope gain of 100/range (Range 100 identical to
// contour-off).
void psd_photoshop_bevel_smooth_fixture_matches_render() {
  const auto psd_path = patchy::test::committed_psd_fixture_path("photoshop-bevel-smooth.psd");
  const auto bmp_path = psd_path.parent_path() / "photoshop-bevel-smooth.bmp";
  CHECK(std::filesystem::exists(psd_path));
  CHECK(std::filesystem::exists(bmp_path));
  const auto document = patchy::psd::DocumentIo::read_file(psd_path);
  const auto* contoured = find_layer_named(document.layers(), "s10c50");
  CHECK(contoured != nullptr);
  CHECK(contoured->layer_style().bevels.size() == 1);
  const auto& bevel = contoured->layer_style().bevels.front();
  CHECK(bevel.enabled);
  CHECK(bevel.contour.enabled);
  CHECK(close_float(bevel.contour.range, 0.5F));
  const auto reference =
      patchy::Compositor{}.flatten_rgb8(patchy::bmp::DocumentIo::read_file(bmp_path));
  const auto flat = patchy::Compositor{}.flatten_rgb8(document);
  const auto metrics = rgb_diff_metrics(reference, flat);
  CHECK(metrics.max_channel_delta <= 6);
  CHECK(metrics.mean_abs_channel_delta <= 0.10);
}

// Photoshop 2026 authored photoshop-gloss-contour.psd/.bmp via COM (July 2026):
// the bevel-smooth shapes with non-linear GLOSS contours - the Ring preset at
// altitudes 30/60 and sizes 5/10, a monotone custom curve, a raised-floor
// curve (LUT(0) = 128), and a pillow-emboss chisel-hard bar at depth 282 with
// Ring, mirroring the pinball_from_photoshop.psd bug report. It pins the
// calibrated gloss model: the LUT remaps the Lambert LIGHT VALUE
// (L' = LUT(clamp(L, 0, 1))) before the highlight/shadow split, interior flat
// plateaus genuinely carry the constant LUT(sin alt) wash, and locally flat
// pixels weight their shading by the matte alpha so exterior flat ground
// stays clean. The old signed-lighting remap painted LUT(0.5) as constant fog
// across the whole padded effect rect. Known residual: the lit-side exterior
// rim of chisel pillow bevels renders brighter than PS's dark tone (the
// exact lit-rim mapping was not identified; docs/ps-compat.md).
void psd_photoshop_bevel_gloss_fixture_matches_render() {
  const auto psd_path = patchy::test::committed_psd_fixture_path("photoshop-gloss-contour.psd");
  const auto bmp_path = psd_path.parent_path() / "photoshop-gloss-contour.bmp";
  CHECK(std::filesystem::exists(psd_path));
  CHECK(std::filesystem::exists(bmp_path));
  const auto document = patchy::psd::DocumentIo::read_file(psd_path);
  const auto* ring = find_layer_named(document.layers(), "s5");
  CHECK(ring != nullptr);
  CHECK(ring->layer_style().bevels.size() == 1);
  const auto& ring_bevel = ring->layer_style().bevels.front();
  CHECK(ring_bevel.enabled);
  CHECK(ring_bevel.gloss_contour.points.size() == 9U);
  CHECK(!ring_bevel.contour.enabled);
  const auto* bar = find_layer_named(document.layers(), "bar6");
  CHECK(bar != nullptr);
  CHECK(bar->layer_style().bevels.size() == 1);
  const auto& bar_bevel = bar->layer_style().bevels.front();
  CHECK(bar_bevel.style == patchy::BevelEmbossStyleKind::PillowEmboss);
  CHECK(bar_bevel.technique == patchy::BevelTechnique::ChiselHard);
  CHECK(!bar_bevel.direction_up);
  CHECK(close_float(bar_bevel.depth, 2.82F));

  const auto reference =
      patchy::Compositor{}.flatten_rgb8(patchy::bmp::DocumentIo::read_file(bmp_path));
  const auto flat = patchy::Compositor{}.flatten_rgb8(document);
  CHECK(reference.width() == flat.width());
  CHECK(reference.height() == flat.height());

  // Interior flat plateaus carry the constant remap (Ring at altitude 30
  // brightens the fill, at altitude 60 it darkens it - pinning the L-domain
  // input), and exterior flat ground stays clean.
  const auto expect_pixel = [&](std::int32_t x, std::int32_t y, int r, int g, int b,
                                int tolerance) {
    const auto* pixel = flat.pixel(x, y);
    CHECK(std::abs(static_cast<int>(pixel[0]) - r) <= tolerance);
    CHECK(std::abs(static_cast<int>(pixel[1]) - g) <= tolerance);
    CHECK(std::abs(static_cast<int>(pixel[2]) - b) <= tolerance);
  };
  expect_pixel(160, 60, 136, 110, 91, 2);    // s10 ring, flat interior wash
  expect_pixel(160, 170, 72, 56, 45, 3);     // s10a60 ring, darkened interior
  expect_pixel(260, 60, 223, 216, 211, 2);   // cove curve interior
  expect_pixel(60, 170, 176, 158, 146, 2);   // raised-floor curve interior
  expect_pixel(232, 170, 255, 255, 255, 1);  // flat ground west of the bar
  expect_pixel(254, 170, 255, 255, 255, 1);  // flat ground east of the bar

  // Whole-canvas agreement; the tolerance headroom covers the documented
  // chisel-pillow lit-rim residual and Ring's steep-cliff LUT quantization.
  std::uint64_t over_tolerance = 0;
  for (std::int32_t y = 0; y < reference.height(); ++y) {
    for (std::int32_t x = 0; x < reference.width(); ++x) {
      const auto* a = reference.pixel(x, y);
      const auto* b = flat.pixel(x, y);
      int max_delta = 0;
      for (int channel = 0; channel < 3; ++channel) {
        max_delta = std::max(
            max_delta, std::abs(static_cast<int>(a[channel]) - static_cast<int>(b[channel])));
      }
      if (max_delta > 6) {
        ++over_tolerance;
      }
    }
  }
  const auto total_pixels =
      static_cast<double>(reference.width()) * static_cast<double>(reference.height());
  CHECK(static_cast<double>(over_tolerance) / total_pixels <= 0.025);
}

// The bug report behind the gloss calibration: pinball_from_photoshop.psd
// stacks pillow-emboss chisel bevels with the Ring gloss contour at style
// scale 416.67% over a black backdrop. The old signed-lighting remap painted
// a constant ~8% highlight fog with EDT medial-axis streaks across the whole
// padded effect rect (Photoshop renders those flats clean black). The points
// below sat in that fog at 21-34/255; assert they stay near black.
void psd_pinball_gloss_pillow_renders_clean_background_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("pinball_from_photoshop.psd");
  if (!std::filesystem::exists(path)) {
    std::cout << "[SKIP] local pinball_from_photoshop.psd fixture missing: " << path.string()
              << '\n';
    return;
  }
  const auto document = patchy::psd::DocumentIo::read_file(path);
  const auto flat = patchy::Compositor{}.flatten_rgb8(document);
  CHECK(flat.width() == 1200);
  CHECK(flat.height() == 849);
  const std::array<std::pair<std::int32_t, std::int32_t>, 6> fog_points{{
      {996, 220}, {216, 376}, {700, 392}, {896, 396}, {992, 424}, {300, 544},
  }};
  for (const auto& [x, y] : fog_points) {
    const auto* pixel = flat.pixel(x, y);
    CHECK(pixel[0] <= 10);
    CHECK(pixel[1] <= 10);
    CHECK(pixel[2] <= 10);
  }
}

// Photoshop 2026 authored photoshop-stroke-aa-matte.psd/.bmp via COM (July
// 2026): an anti-aliased ellipse and a rotated square with solid 10 px
// outside strokes, plus an AA ellipse with a Shape Burst gradient stroke.
// They pin the SUBPIXEL stroke contour anchor (stroke_subpixel_distance_fields):
// the band and the Shape Burst ramp anchor at the matte's bilinear
// half-coverage crossing (3x supersampled EDT with the +1/3 px binary
// compensation), which removes the whole-pixel staircase a 0.5 threshold
// produces on curved edges. Straight runs match byte-for-byte; the remaining
// over-tolerance pixels are the band-limit arc AA of the shared coverage
// ramp (documented divergence, docs/ps-compat.md).
void psd_photoshop_stroke_aa_matte_fixture_matches_render() {
  const auto psd_path = patchy::test::committed_psd_fixture_path("photoshop-stroke-aa-matte.psd");
  const auto bmp_path = psd_path.parent_path() / "photoshop-stroke-aa-matte.bmp";
  CHECK(std::filesystem::exists(psd_path));
  CHECK(std::filesystem::exists(bmp_path));
  const auto document = patchy::psd::DocumentIo::read_file(psd_path);
  const auto* ellipse = find_layer_named(document.layers(), "ellipse");
  const auto* shape_burst = find_layer_named(document.layers(), "sbellipse");
  CHECK(ellipse != nullptr);
  CHECK(shape_burst != nullptr);
  CHECK(ellipse->layer_style().strokes.size() == 1);
  CHECK(!ellipse->layer_style().strokes.front().uses_gradient);
  CHECK(shape_burst->layer_style().strokes.size() == 1);
  CHECK(shape_burst->layer_style().strokes.front().gradient.type ==
        patchy::LayerStyleGradientType::ShapeBurst);
  const auto reference =
      patchy::Compositor{}.flatten_rgb8(patchy::bmp::DocumentIo::read_file(bmp_path));
  const auto flat = patchy::Compositor{}.flatten_rgb8(document);
  const auto metrics = rgb_diff_metrics(reference, flat);
  CHECK(metrics.mean_abs_channel_delta <= 1.0);
  std::uint64_t over_aa_tolerance = 0;
  for (std::int32_t y = 0; y < reference.height(); ++y) {
    for (std::int32_t x = 0; x < reference.width(); ++x) {
      const auto* a = reference.pixel(x, y);
      const auto* b = flat.pixel(x, y);
      int max_delta = 0;
      for (int channel = 0; channel < 3; ++channel) {
        max_delta = std::max(max_delta,
                             std::abs(static_cast<int>(a[channel]) - static_cast<int>(b[channel])));
      }
      if (max_delta > 6) {
        ++over_aa_tolerance;
      }
    }
  }
  const auto total_pixels =
      static_cast<double>(reference.width()) * static_cast<double>(reference.height());
  // 2.13% at capture time (the pre-subpixel threshold anchor measured 3.50%).
  CHECK(static_cast<double>(over_aa_tolerance) / total_pixels <= 0.03);
}

// Photoshop 2026 authored photoshop-stroke-overprint.psd/.bmp via COM (July
// 2026): eight stroke arms over an orange (255,128,0) backdrop, gray (110)
// content, pinning the Overprint knockout model. Arms: Inside 29% white
// overprint off (band = 0.29*white + 0.71*orange — content fully knocked
// out), the same with overprint ON (band = stroke over the gray content),
// 60% off, Multiply (200,100,50) 100% off (blend mode applies against the
// BACKDROP), an AA ellipse at 29% off (fringe compensation), a
// layer-opacity-50 arm (the knocked-out plane scales with layer opacity),
// and 0%-opacity arms: overprint off still knocks the band out to the pure
// backdrop while painting nothing, overprint on is a complete no-op.
void psd_photoshop_stroke_overprint_fixture_matches_render() {
  const auto psd_path = patchy::test::committed_psd_fixture_path("photoshop-stroke-overprint.psd");
  const auto bmp_path = psd_path.parent_path() / "photoshop-stroke-overprint.bmp";
  CHECK(std::filesystem::exists(psd_path));
  CHECK(std::filesystem::exists(bmp_path));
  const auto document = patchy::psd::DocumentIo::read_file(psd_path);
  const auto* off_arm = find_layer_named(document.layers(), "ins29 off");
  const auto* on_arm = find_layer_named(document.layers(), "ins29 on");
  CHECK(off_arm != nullptr);
  CHECK(on_arm != nullptr);
  CHECK(off_arm->layer_style().strokes.size() == 1);
  CHECK(!off_arm->layer_style().strokes.front().overprint);
  CHECK(on_arm->layer_style().strokes.size() == 1);
  CHECK(on_arm->layer_style().strokes.front().overprint);
  const auto reference =
      patchy::Compositor{}.flatten_rgb8(patchy::bmp::DocumentIo::read_file(bmp_path));
  const auto flat = patchy::Compositor{}.flatten_rgb8(document);
  const auto metrics = rgb_diff_metrics(reference, flat);
  CHECK(metrics.mean_abs_channel_delta <= 1.0);
  std::uint64_t over_tolerance = 0;
  for (std::int32_t y = 0; y < reference.height(); ++y) {
    for (std::int32_t x = 0; x < reference.width(); ++x) {
      const auto* a = reference.pixel(x, y);
      const auto* b = flat.pixel(x, y);
      int max_delta = 0;
      for (int channel = 0; channel < 3; ++channel) {
        max_delta = std::max(max_delta,
                             std::abs(static_cast<int>(a[channel]) - static_cast<int>(b[channel])));
      }
      if (max_delta > 6) {
        ++over_tolerance;
      }
    }
  }
  const auto total_pixels =
      static_cast<double>(reference.width()) * static_cast<double>(reference.height());
  CHECK(static_cast<double>(over_tolerance) / total_pixels <= 0.01);
  // Mid-band spot checks against Photoshop's exact bytes.
  const auto check_near = [&](std::int32_t x, std::int32_t y, int red, int green, int blue) {
    const auto* pixel = flat.pixel(x, y);
    CHECK(std::abs(static_cast<int>(pixel[0]) - red) <= 2);
    CHECK(std::abs(static_cast<int>(pixel[1]) - green) <= 2);
    CHECK(std::abs(static_cast<int>(pixel[2]) - blue) <= 2);
  };
  check_near(24, 45, 255, 165, 74);     // ins29 off: knocked out, orange shows
  check_near(124, 45, 152, 152, 152);   // ins29 on: over the gray content
  check_near(224, 45, 255, 204, 153);   // ins60 off
  check_near(24, 135, 200, 50, 0);      // multiply off: mult against backdrop
  check_near(120, 135, 255, 165, 74);   // AA ellipse band interior
  check_near(224, 135, 255, 147, 37);   // layer opacity 50
  check_near(24, 225, 255, 128, 0);     // ins0 off: knocked out, nothing drawn
  check_near(55, 225, 110, 110, 110);   // ins0 off: interior beyond band intact
  check_near(124, 225, 110, 110, 110);  // ins0 on: complete no-op
}

// Photoshop 2026 authored photoshop-interior-exterior-blending.psd/.bmp via COM
// (July 2026, the options.psd "highlight" report): six 12x12 cells on one
// (100, 120, 140) backdrop pinning where a layer's blend mode sits relative to
// its own effects.
//
// satOverlayOff/satOverlayOn are the same Saturation layer under a Linear Dodge
// Color Overlay with "Blend Interior Effects as Group" ('infx') off and on. Off
// - Photoshop's default - the layer's mode carries its own pixels alone and the
// overlay blends over that result at full strength; on, the overlay folds into
// the layer color and the Saturation mode desaturates it too. Rendering every
// layer the "on" way is what washed out the DungeonScroll options screen.
//
// glowSemi/shadowSemi/glowMult/shadowFill0 pin the exterior effects as ADDITIVE
// contributions against the layer's original backdrop: a half-alpha square does
// not attenuate its own glow a second time, a Multiply layer blends with the
// backdrop rather than with the shadow underneath it, and a Fill-0 layer still
// knocks its shadow out of its own shape.
void psd_photoshop_interior_exterior_blending_fixture_matches_render() {
  const auto psd_path =
      patchy::test::committed_psd_fixture_path("photoshop-interior-exterior-blending.psd");
  const auto bmp_path = psd_path.parent_path() / "photoshop-interior-exterior-blending.bmp";
  CHECK(std::filesystem::exists(psd_path));
  CHECK(std::filesystem::exists(bmp_path));
  const auto document = patchy::psd::DocumentIo::read_file(psd_path);

  const auto* overlay_off = find_layer_named(document.layers(), "satOverlayOff");
  const auto* overlay_on = find_layer_named(document.layers(), "satOverlayOn");
  CHECK(overlay_off != nullptr);
  CHECK(overlay_on != nullptr);
  CHECK(overlay_off->blend_mode() == patchy::BlendMode::Saturation);
  CHECK(overlay_on->blend_mode() == patchy::BlendMode::Saturation);
  CHECK(!overlay_off->layer_style().blend_interior_elements);
  CHECK(overlay_on->layer_style().blend_interior_elements);
  CHECK(overlay_off->layer_style().color_overlays.size() == 1);
  CHECK(overlay_off->layer_style().color_overlays.front().blend_mode == patchy::BlendMode::LinearDodge);
  const auto* glow_mult = find_layer_named(document.layers(), "glowMult");
  CHECK(glow_mult != nullptr);
  CHECK(glow_mult->blend_mode() == patchy::BlendMode::Multiply);
  const auto* shadow_fill0 = find_layer_named(document.layers(), "shadowFill0");
  CHECK(shadow_fill0 != nullptr);
  CHECK(shadow_fill0->fill_opacity() <= 0.01F);

  const auto reference =
      patchy::Compositor{}.flatten_rgb8(patchy::bmp::DocumentIo::read_file(bmp_path));
  const auto flat = patchy::Compositor{}.flatten_rgb8(document);
  const auto metrics = rgb_diff_metrics(reference, flat);
  CHECK(metrics.max_channel_delta <= 2);
  CHECK(metrics.mean_abs_channel_delta <= 0.10);
  const auto check_near = [&](std::int32_t x, std::int32_t y, int red, int green, int blue) {
    const auto* pixel = flat.pixel(x, y);
    CHECK(std::abs(static_cast<int>(pixel[0]) - red) <= 2);
    CHECK(std::abs(static_cast<int>(pixel[1]) - green) <= 2);
    CHECK(std::abs(static_cast<int>(pixel[2]) - blue) <= 2);
  };
  check_near(20, 32, 25, 137, 255);    // infx off: the overlay keeps Linear Dodge
  check_near(56, 32, 38, 134, 229);    // infx on: the layer's mode takes the overlay too
  check_near(92, 32, 255, 32, 16);     // half-alpha square over an unattenuated glow
  check_near(128, 32, 217, 62, 51);    // half-alpha square over its own knocked-out shadow
  check_near(164, 32, 177, 15, 9);     // Multiply blends the backdrop, not the glow
  check_near(200, 32, 100, 120, 140);  // fill 0 still hides the shadow inside the shape
}

// Photoshop 2026 authored photoshop-shadow-conceals.psd/.bmp via COM (July
// 2026): six drop-shadow arms over an orange backdrop pinning "Layer Knocks
// Out Drop Shadow" (DrSh layerConceals, default on): the layer's transparency
// shape punches a hole in its own shadow regardless of fill opacity, master
// opacity, or a stroke knockout. Arms: 0%-stroke knockout band with conceals
// on (band = pure backdrop) and off (band = shadowed backdrop), fill-50 with
// conceals on (content over pure backdrop) and off (shadow through the
// fill), master-50 on, and fill-0 on (the classic no-shadow-inside case).
void psd_photoshop_shadow_conceals_fixture_matches_render() {
  const auto psd_path = patchy::test::committed_psd_fixture_path("photoshop-shadow-conceals.psd");
  const auto bmp_path = psd_path.parent_path() / "photoshop-shadow-conceals.bmp";
  CHECK(std::filesystem::exists(psd_path));
  CHECK(std::filesystem::exists(bmp_path));
  const auto document = patchy::psd::DocumentIo::read_file(psd_path);
  const auto* on_arm = find_layer_named(document.layers(), "knock on");
  const auto* off_arm = find_layer_named(document.layers(), "knock off");
  CHECK(on_arm != nullptr);
  CHECK(off_arm != nullptr);
  CHECK(on_arm->layer_style().drop_shadows.size() == 1);
  CHECK(on_arm->layer_style().drop_shadows.front().layer_conceals);
  CHECK(off_arm->layer_style().drop_shadows.size() == 1);
  CHECK(!off_arm->layer_style().drop_shadows.front().layer_conceals);
  const auto reference =
      patchy::Compositor{}.flatten_rgb8(patchy::bmp::DocumentIo::read_file(bmp_path));
  const auto flat = patchy::Compositor{}.flatten_rgb8(document);
  const auto metrics = rgb_diff_metrics(reference, flat);
  CHECK(metrics.mean_abs_channel_delta <= 1.0);
  const auto check_near = [&](std::int32_t x, std::int32_t y, int red, int green, int blue) {
    const auto* pixel = flat.pixel(x, y);
    CHECK(std::abs(static_cast<int>(pixel[0]) - red) <= 2);
    CHECK(std::abs(static_cast<int>(pixel[1]) - green) <= 2);
    CHECK(std::abs(static_cast<int>(pixel[2]) - blue) <= 2);
  };
  check_near(24, 45, 255, 128, 0);     // knock on: band shows the pure backdrop
  check_near(124, 45, 148, 74, 0);     // knock off: shadow fills the band
  check_near(255, 45, 182, 119, 55);   // fill50 on: content over pure backdrop
  check_near(55, 135, 87, 71, 55);     // fill50 off: shadow through the fill
  check_near(155, 135, 182, 119, 55);  // master50 on
  check_near(255, 135, 255, 128, 0);   // fill0 on: no shadow inside the shape
}

// Photoshop 2026 authored photoshop-pillow-emboss.psd/.bmp and
// photoshop-pillow-emboss2.psd/.bmp via COM (July 2026): squares on a gray-128
// backdrop carrying Smooth Pillow Emboss at depth 1/5/10/25/50/100/200/500/1000,
// sizes 7 and 12, altitude 30 (plus altitude-60 controls and two solid-stroke
// interplay squares). They pin the calibrated pillow model: half-size smooth
// ramp lit directly with the interior flipped, slope factor
// 0.5 x max(depth, 25%) x tent peak, normalized-Lambert highlights,
// UNNORMALIZED linear shadows, and bevel shading composited over the stroke
// band. Known residuals stay bounded by the ratio check: corner arcs at
// saturated depths and the altitude-60 mid-depth highlight tail
// (docs/ps-compat.md).
void psd_photoshop_pillow_emboss_fixtures_match_render() {
  // photoshop-emboss-styles adds AA-ellipse pillow probes (fringe pixels
  // carry BOTH sides' shading — the two-sided composite) and plain-Emboss
  // squares/ellipse (same calibrated model, one global sign, no flip).
  for (const auto* stem :
       {"photoshop-pillow-emboss", "photoshop-pillow-emboss2", "photoshop-emboss-styles"}) {
    const auto psd_path = patchy::test::committed_psd_fixture_path(std::string(stem) + ".psd");
    const auto bmp_path = psd_path.parent_path() / (std::string(stem) + ".bmp");
    CHECK(std::filesystem::exists(psd_path));
    CHECK(std::filesystem::exists(bmp_path));
    const auto document = patchy::psd::DocumentIo::read_file(psd_path);
    const auto reference =
        patchy::Compositor{}.flatten_rgb8(patchy::bmp::DocumentIo::read_file(bmp_path));
    const auto flat = patchy::Compositor{}.flatten_rgb8(document);
    const auto metrics = rgb_diff_metrics(reference, flat);
    CHECK(metrics.mean_abs_channel_delta <= 0.15);
    std::uint64_t over_aa_tolerance = 0;
    for (std::int32_t y = 0; y < reference.height(); ++y) {
      for (std::int32_t x = 0; x < reference.width(); ++x) {
        const auto* a = reference.pixel(x, y);
        const auto* b = flat.pixel(x, y);
        int max_delta = 0;
        for (int channel = 0; channel < 3; ++channel) {
          max_delta = std::max(max_delta,
                               std::abs(static_cast<int>(a[channel]) - static_cast<int>(b[channel])));
        }
        if (max_delta > 6) {
          ++over_aa_tolerance;
        }
      }
    }
    const auto total_pixels =
        static_cast<double>(reference.width()) * static_cast<double>(reference.height());
    CHECK(static_cast<double>(over_aa_tolerance) / total_pixels <= 0.01);
  }
  const auto document = patchy::psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path("photoshop-pillow-emboss.psd"));
  const auto* low_depth = find_layer_named(document.layers(), "p7d1");
  CHECK(low_depth != nullptr);
  CHECK(low_depth->layer_style().bevels.size() == 1);
  CHECK(low_depth->layer_style().bevels.front().style == patchy::BevelEmbossStyleKind::PillowEmboss);
  CHECK(close_float(low_depth->layer_style().bevels.front().depth, 0.01F));
}

// Photoshop 2026 authored photoshop-stroke-shapeburst.psd via COM (July 2026):
// three L-shaped mattes carrying Shape Burst gradient strokes — Outside 10 px,
// Center 12 px with Reverse, and Inside 10 px at Scale 50 — saved alongside
// Photoshop's own flatten. It pins the calibrated mapping (docs/ps-compat.md):
// the ramp is linear in the band's Euclidean distance field with position 0 at
// the outer band limit, Center strokes ramp continuously across the contour,
// Reverse flips the ramp, Scale is ignored, and the two-stop colors ease
// through the full Intr smoothing.
void psd_photoshop_stroke_shapeburst_fixture_matches_render() {
  const auto psd_path = patchy::test::committed_psd_fixture_path("photoshop-stroke-shapeburst.psd");
  const auto bmp_path = psd_path.parent_path() / "photoshop-stroke-shapeburst.bmp";
  CHECK(std::filesystem::exists(psd_path));
  CHECK(std::filesystem::exists(bmp_path));
  const auto document = patchy::psd::DocumentIo::read_file(psd_path);
  const auto* outside = find_layer_named(document.layers(), "out10");
  const auto* center = find_layer_named(document.layers(), "ctr12rev");
  const auto* inside = find_layer_named(document.layers(), "in10s50");
  CHECK(outside != nullptr);
  CHECK(center != nullptr);
  CHECK(inside != nullptr);
  for (const auto* layer : {outside, center, inside}) {
    CHECK(layer->layer_style().strokes.size() == 1);
    CHECK(layer->layer_style().strokes.front().uses_gradient);
    CHECK(layer->layer_style().strokes.front().gradient.type ==
          patchy::LayerStyleGradientType::ShapeBurst);
  }
  CHECK(center->layer_style().strokes.front().gradient.reverse);
  CHECK(close_float(inside->layer_style().strokes.front().gradient.scale, 0.5F));
  const auto reference =
      patchy::Compositor{}.flatten_rgb8(patchy::bmp::DocumentIo::read_file(bmp_path));
  const auto flat = patchy::Compositor{}.flatten_rgb8(document);
  const auto metrics = rgb_diff_metrics(reference, flat);
  // Straight band runs match within +/-1; the only pixels past the 6/255 AA
  // tolerance (0.09% of the canvas at capture time) are band-edge arcs at
  // shape corners, where the shared stroke coverage ramp (linear in center
  // distance) overfills slightly against Photoshop's rounder arc coverage —
  // a solid-stroke coverage property, not part of the Shape Burst mapping.
  CHECK(metrics.mean_abs_channel_delta <= 0.10);
  std::uint64_t over_aa_tolerance = 0;
  for (std::int32_t y = 0; y < reference.height(); ++y) {
    for (std::int32_t x = 0; x < reference.width(); ++x) {
      const auto* a = reference.pixel(x, y);
      const auto* b = flat.pixel(x, y);
      int max_delta = 0;
      for (int channel = 0; channel < 3; ++channel) {
        max_delta = std::max(max_delta,
                             std::abs(static_cast<int>(a[channel]) - static_cast<int>(b[channel])));
      }
      if (max_delta > 6) {
        ++over_aa_tolerance;
      }
    }
  }
  const auto total_pixels =
      static_cast<double>(reference.width()) * static_cast<double>(reference.height());
  CHECK(static_cast<double>(over_aa_tolerance) / total_pixels <= 0.002);
}

// The PS 5.x 'dsdw' record stores blur/intensity/angle/distance as 16.16 fixed
// point and opacity as a 0-255 byte. The pre-July-2026 parser read the fixed
// fields as raw integers one slot early, so a legacy shadow could carry a
// ~7.8-million-pixel distance and abort the whole flatten on allocation.
//
// The effect's own size field is the block header ('8BIM' + key + u32 size), so
// the payload handed to the parser STARTS at the version. This test used to
// write that size twice - once as the header and again inside the payload -
// which is what the parser assumed, so both agreed on a shape Photoshop never
// writes. A real version-0 record is 41 bytes, and skipping the phantom size
// ran the reader off its end mid-blend-key: every legacy effect on the layer
// was thrown away. Photoshop's own bytes are what the test builds now.
void psd_lrfx_legacy_drop_shadow_parses_fixed_point() {
  const patchy::psd::CmykColorConverter cmyk{};
  const auto lrfx_block = [](const std::vector<std::uint8_t>& effect_bytes) {
    patchy::psd::BigEndianWriter writer;
    writer.write_u16(0);  // effects version
    writer.write_u16(1);  // effect count
    writer.write_bytes(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>("8BIMdsdw"), 8));
    writer.write_u32(static_cast<std::uint32_t>(effect_bytes.size()));
    writer.write_bytes(effect_bytes);
    return writer.bytes();
  };

  patchy::psd::BigEndianWriter effect;
  effect.write_u32(2);            // version (2 = PS 5.5, carries a trailing native color)
  effect.write_u32(12U << 16U);   // blur (size), fixed
  effect.write_u32(0);            // intensity
  effect.write_u32(120U << 16U);  // angle, fixed
  effect.write_u32(5U << 16U);    // distance, fixed
  effect.write_u16(0);            // color space: RGB
  effect.write_u16(0xFFFF);
  effect.write_u16(0);
  effect.write_u16(0);
  effect.write_u16(0);
  effect.write_bytes(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>("8BIMlbrn"), 8));
  effect.write_u8(1);   // enabled
  effect.write_u8(1);   // use global angle
  effect.write_u8(71);  // opacity byte (28%)
  for (int i = 0; i < 10; ++i) {
    effect.write_u8(0);  // native color (version 2 only)
  }
  CHECK(effect.bytes().size() == 51U);

  const auto style = patchy::psd::parse_lrfx_layer_style(lrfx_block(effect.bytes()), cmyk);
  CHECK(style.drop_shadows.size() == 1U);
  const auto& shadow = style.drop_shadows.front();
  CHECK(shadow.enabled);
  CHECK(shadow.size == 12.0F);
  CHECK(shadow.angle_degrees == 120.0F);
  CHECK(shadow.distance == 5.0F);
  CHECK(shadow.use_global_light);
  CHECK(shadow.blend_mode == patchy::BlendMode::LinearBurn);
  CHECK(shadow.color.red == 255 && shadow.color.green == 0 && shadow.color.blue == 0);
  CHECK(close_float(shadow.opacity, 71.0F / 255.0F));

  // The 41-byte version-0 record PS 5.x actually writes, with the CMYK black
  // ('ffff ffff ffff 0000' - the components are stored INVERTED, so 0xFFFF is
  // 0% ink) that Title02.psd carries. Reading that color as RGB yielded white,
  // and a white multiply shadow renders as nothing at all.
  patchy::psd::BigEndianWriter legacy;
  legacy.write_u32(0);            // version 0 (PS 5.0)
  legacy.write_u32(0);            // blur (size) 0 - a hard-edged shadow
  legacy.write_u32(0);            // intensity
  legacy.write_u32(120U << 16U);  // angle, fixed
  legacy.write_u32(6U << 16U);    // distance, fixed
  legacy.write_u16(2);            // color space: CMYK
  legacy.write_u16(0xFFFF);       // cyan    0%
  legacy.write_u16(0xFFFF);       // magenta 0%
  legacy.write_u16(0xFFFF);       // yellow  0%
  legacy.write_u16(0);            // black 100%
  legacy.write_bytes(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>("8BIMmul "), 8));
  legacy.write_u8(1);     // enabled
  legacy.write_u8(1);     // use global angle
  legacy.write_u8(0x59);  // opacity byte (35%)
  CHECK(legacy.bytes().size() == 41U);

  const auto legacy_style = patchy::psd::parse_lrfx_layer_style(lrfx_block(legacy.bytes()), cmyk);
  CHECK(legacy_style.drop_shadows.size() == 1U);
  const auto& legacy_shadow = legacy_style.drop_shadows.front();
  CHECK(legacy_shadow.enabled);
  CHECK(legacy_shadow.size == 0.0F);
  CHECK(legacy_shadow.spread == 0.0F);
  CHECK(legacy_shadow.angle_degrees == 120.0F);
  CHECK(legacy_shadow.distance == 6.0F);
  CHECK(legacy_shadow.use_global_light);
  CHECK(legacy_shadow.blend_mode == patchy::BlendMode::Multiply);
  CHECK(legacy_shadow.color.red == 0 && legacy_shadow.color.green == 0 && legacy_shadow.color.blue == 0);
  CHECK(close_float(legacy_shadow.opacity, 0x59 / 255.0F));

  // A Grayscale-space legacy color carries its level in the first component on
  // Photoshop's 0-10000 scale, not as a 16-bit channel.
  auto gray_bytes = legacy.bytes();
  gray_bytes[20] = 0;      // color space high byte
  gray_bytes[21] = 8;      // Grayscale
  gray_bytes[22] = 0x13;   // 5000 / 10000 = 50% gray
  gray_bytes[23] = 0x88;
  const auto gray_style = patchy::psd::parse_lrfx_layer_style(lrfx_block(gray_bytes), cmyk);
  CHECK(gray_style.drop_shadows.size() == 1U);
  CHECK(gray_style.drop_shadows.front().color.red == 128);
  CHECK(gray_style.drop_shadows.front().color.green == 128);
  CHECK(gray_style.drop_shadows.front().color.blue == 128);
}

// CS-era Photoshop stores lfx2 'BlnM' enum values as length-0 charIDs ('Drkn',
// 'Lghn', ...), a namespace distinct from both the modern stringIDs ("darken")
// and the layer-record blend signatures ('dark'). The pre-July-2026 table only
// aliased a subset, so a CS gradient overlay set to Darken imported as Normal.
// Ground truth: Photoshop 2026 typeIDToStringID(charIDToTypeID(x)) plus the
// weedkiller_skin.psd fixture below, which PS 2026 reads as Darken.
void psd_lfx2_charid_blend_mode_enum_parses() {
  const std::array<char, 4> norm{'n', 'o', 'r', 'm'};
  const auto mode = [&](std::string_view value) {
    return patchy::psd::blend_mode_from_descriptor_enum(value, norm);
  };
  CHECK(mode("Drkn") == patchy::BlendMode::Darken);
  CHECK(mode("Lghn") == patchy::BlendMode::Lighten);
  CHECK(mode("HrdL") == patchy::BlendMode::HardLight);
  CHECK(mode("Dfrn") == patchy::BlendMode::Difference);
  CHECK(mode("Xclu") == patchy::BlendMode::Exclusion);
  CHECK(mode("H   ") == patchy::BlendMode::Hue);
  CHECK(mode("Strt") == patchy::BlendMode::Saturation);
  CHECK(mode("Clr ") == patchy::BlendMode::Color);
  CHECK(mode("Lmns") == patchy::BlendMode::Luminosity);
  // Existing forms stay mapped; dissolve has no Patchy mode and resolves Normal.
  CHECK(mode("darken") == patchy::BlendMode::Darken);
  CHECK(mode("dark") == patchy::BlendMode::Darken);
  CHECK(mode("Mltp") == patchy::BlendMode::Multiply);
  CHECK(mode("Dslv") == patchy::BlendMode::Normal);

  // Integration: a minimal lfx2 payload whose GrFl blend mode rides the
  // length-0 charID enum form, like Photoshop CS wrote it.
  patchy::psd::DescriptorObject gradient;
  gradient.class_id = "GrFl";
  patchy::psd::DescriptorValue enab;
  enab.type = patchy::psd::DescriptorValue::Type::Bool;
  enab.bool_value = true;
  gradient.values["enab"] = enab;
  gradient.key_order.push_back({"enab", false});
  patchy::psd::DescriptorValue blend;
  blend.type = patchy::psd::DescriptorValue::Type::Enum;
  blend.enum_type = "BlnM";
  blend.enum_value = "Drkn";  // long_form stays false: written as a charID
  gradient.values["Md  "] = blend;
  gradient.key_order.push_back({"Md  ", false});

  patchy::psd::DescriptorObject root;
  root.class_id = "null";
  patchy::psd::DescriptorValue effect;
  effect.type = patchy::psd::DescriptorValue::Type::Object;
  effect.object_value = std::make_shared<patchy::psd::DescriptorObject>(gradient);
  root.values["GrFl"] = effect;
  root.key_order.push_back({"GrFl", false});

  patchy::psd::BigEndianWriter writer;
  writer.write_u32(0);   // lfx2 version
  writer.write_u32(16);  // descriptor version
  patchy::psd::write_descriptor(writer, root);
  const auto style = patchy::psd::parse_lfx2_layer_style(writer.bytes(), patchy::psd::CmykColorConverter{});
  CHECK(style.gradient_fills.size() == 1U);
  CHECK(style.gradient_fills.front().enabled);
  CHECK(style.gradient_fills.front().blend_mode == patchy::BlendMode::Darken);
}

// Real-file regression: Photoshop CS authored this PSD with a Darken gradient
// overlay and Normal strokes, all in the charID enum form. Photoshop 2026
// displays Darken; Patchy imported Normal before the charID table was filled in.
void psd_weedkiller_legacy_charid_styles_parse_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("weedkiller_skin.psd");
  if (!std::filesystem::exists(path)) {
    std::cout << "[SKIP] local weedkiller_skin.psd fixture missing: " << path.string() << '\n';
    return;
  }
  std::ifstream stream(path, std::ios::binary);
  std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
  const auto document = patchy::psd::DocumentIo::read(bytes);
  const auto* layer = find_layer_named(document.layers(), "Layer 2");
  CHECK(layer != nullptr);
  const auto& style = layer->layer_style();
  CHECK(style.gradient_fills.size() == 1U);
  CHECK(style.gradient_fills.front().blend_mode == patchy::BlendMode::Darken);
  CHECK(style.strokes.size() == 1U);
  CHECK(style.strokes.front().blend_mode == patchy::BlendMode::Normal);
}

// Photoshop ignores the legacy lrFX compatibility mirror whenever lfx2 exists;
// merging it resurrected effects the lfx2 deliberately disables (and the
// misparsed legacy values then aborted the flatten). Reproduced by disabling
// the tips.psd title's lfx2 drop shadow in memory: the layer also carries an
// lrFX block whose legacy shadow must NOT leak back in.
void psd_lfx2_disabled_effect_suppresses_legacy_lrfx_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("tips.psd");
  if (!std::filesystem::exists(path)) {
    std::cout << "[SKIP] local tips.psd fixture missing: " << path.string() << '\n';
    return;
  }
  std::ifstream stream(path, std::ios::binary);
  std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
  const std::vector<std::uint8_t> lfx2_marker{'8', 'B', 'I', 'M', 'l', 'f', 'x', '2'};
  const auto lfx2_at = std::search(bytes.rbegin(), bytes.rend(), lfx2_marker.rbegin(), lfx2_marker.rend());
  CHECK(lfx2_at != bytes.rend());
  const auto lfx2_offset = static_cast<std::size_t>(bytes.rend() - lfx2_at) - lfx2_marker.size();
  const std::vector<std::uint8_t> shadow_key{'D', 'r', 'S', 'h'};
  auto search_begin = bytes.begin() + static_cast<std::ptrdiff_t>(lfx2_offset);
  const auto shadow_at = std::search(search_begin, bytes.end(), shadow_key.begin(), shadow_key.end());
  CHECK(shadow_at != bytes.end());
  const std::vector<std::uint8_t> enab_marker{'e', 'n', 'a', 'b', 'b', 'o', 'o', 'l'};
  const auto enab_at = std::search(shadow_at, bytes.end(), enab_marker.begin(), enab_marker.end());
  CHECK(enab_at != bytes.end());
  CHECK(*(enab_at + 8) == 1U);
  *(enab_at + static_cast<std::ptrdiff_t>(enab_marker.size())) = 0U;

  const auto document = patchy::psd::DocumentIo::read(bytes);
  const auto* title = find_layer_named(document.layers(), "Quick Tips");
  CHECK(title != nullptr);
  CHECK(layer_has_psd_block(*title, "lrFX"));
  CHECK(first_enabled_drop_shadow(*title) == nullptr);
  const auto flat = patchy::Compositor{}.flatten_rgb8(document);
  CHECK(flat.width() == 800 && flat.height() == 512);
}

// End-to-end guard on real PS 5.x bytes, because the synthetic payload above is
// exactly what let the shape bug hide. Title02.psd (the Cockpit Master title
// screen, authored 2000) carries no lfx2 at all: two of its layers hold only an
// 'lrFX' block whose six effect records have just the drop shadow enabled.
// Photoshop 2026 reads both through COM as multiply shadows at global angle 120
// with the CMYK black that converts to RGB (35, 31, 32) - "box" at 75% opacity,
// 10 px distance, 10 px blur, and "highlighted buttons" at 35%, 6 px distance,
// 0 px blur. Patchy imported neither and the title screen rendered flat.
void psd_lrfx_legacy_title_screen_imports_drop_shadows_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("Title02.psd");
  if (!std::filesystem::exists(path)) {
    std::cout << "[SKIP] local Title02.psd fixture missing: " << path.string() << '\n';
    return;
  }
  const auto document = patchy::psd::DocumentIo::read_file(path);

  const auto* buttons = find_layer_named(document.layers(), "highlighted buttons");
  CHECK(buttons != nullptr);
  CHECK(layer_has_psd_block(*buttons, "lrFX"));
  const auto* buttons_shadow = first_enabled_drop_shadow(*buttons);
  CHECK(buttons_shadow != nullptr);
  CHECK(buttons_shadow->blend_mode == patchy::BlendMode::Multiply);
  CHECK(close_float(buttons_shadow->opacity, 0x59 / 255.0F));
  CHECK(buttons_shadow->distance == 6.0F);
  CHECK(buttons_shadow->size == 0.0F);
  CHECK(buttons_shadow->spread == 0.0F);
  // Resource 1037 holds the same 120 degrees, so the global-light resolve is a
  // no-op here and the flag is cleared on import.
  CHECK(buttons_shadow->angle_degrees == 120.0F);
  CHECK(!buttons_shadow->use_global_light);
  // Photoshop's color-managed CMYK gives (35, 31, 32); with no CMYK profile to
  // ride, Patchy's naive ink mix lands on black, the same rule lfx2 CMYK colors
  // follow. What must never happen again is white, which multiplies to nothing.
  CHECK(buttons_shadow->color.red == 0 && buttons_shadow->color.green == 0 && buttons_shadow->color.blue == 0);

  const auto* box = find_layer_named(document.layers(), "box");
  CHECK(box != nullptr);
  CHECK(layer_has_psd_block(*box, "lrFX"));
  const auto* box_shadow = first_enabled_drop_shadow(*box);
  CHECK(box_shadow != nullptr);
  CHECK(box_shadow->blend_mode == patchy::BlendMode::Multiply);
  CHECK(close_float(box_shadow->opacity, 0xBF / 255.0F));
  CHECK(box_shadow->distance == 10.0F);
  CHECK(box_shadow->size == 10.0F);
  CHECK(box_shadow->angle_degrees == 120.0F);

  // The five disabled records beside each shadow stay out of the style.
  CHECK(buttons->layer_style().inner_shadows.empty());
  CHECK(buttons->layer_style().outer_glows.empty());
  CHECK(buttons->layer_style().inner_glows.empty());
  CHECK(buttons->layer_style().bevels.empty());

  // The shadow has to reach the canvas: sample a pixel beside the "PLAY ONLINE"
  // row that only the hard-edged 6 px offset shadow darkens, against clear sky
  // nine rows up. Photoshop renders (117, 136, 157) there over a (166, 195, 225)
  // backdrop; Patchy's blacker shadow color lands ~10/255 darker still.
  const auto flat = patchy::Compositor{}.flatten_rgb8(document);
  CHECK(flat.width() == 640 && flat.height() == 480);
  const auto* shadowed = flat.pixel(201, 189);
  const auto* clear = flat.pixel(201, 180);
  CHECK(shadowed[0] < clear[0] - 20);
  CHECK(shadowed[1] < clear[1] - 20);
  CHECK(shadowed[2] < clear[2] - 20);
}

// Same file, the other half of its rendering: layers 2-9 are one clipping run
// over the "box" frame, whose LAYER MASK cuts a hole exactly where the torn-edge
// ghost plane sits. Photoshop clips the run to the base's transparency, so the
// hole stays empty and the sky plus the frame's own drop shadow show through.
// Patchy froze the clip shape from the group buffer's accumulated alpha, which
// already carried that shadow, so the clipped "torn" layer repainted the hole
// and washed the plane out (Photoshop 2026 renders (71, 78, 88) at 400,350; the
// bug rendered (136, 147, 160)).
//
// The pinned values are Photoshop's, with a tolerance for the one known
// divergence in this file: its legacy shadow color is CMYK black, which
// Photoshop color-manages to RGB (35, 31, 32) and Patchy mixes to 0, leaving
// shadowed pixels up to ~24/255 darker. Anything that lets the clipped stack
// back into the hole moves these by 60+.
void psd_lrfx_legacy_title_screen_clips_stack_to_base_mask_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("Title02.psd");
  if (!std::filesystem::exists(path)) {
    std::cout << "[SKIP] local Title02.psd fixture missing: " << path.string() << '\n';
    return;
  }
  const auto document = patchy::psd::DocumentIo::read_file(path);
  const auto* box = find_layer_named(document.layers(), "box");
  CHECK(box != nullptr);
  CHECK(box->mask().has_value());
  CHECK(!box->clipped());
  for (const char* name : {"shade", "shade inner", "torn", "Layer 1"}) {
    const auto* member = find_layer_named(document.layers(), name);
    CHECK(member != nullptr);
    CHECK(member->clipped());
  }

  const auto flat = patchy::Compositor{}.flatten_rgb8(document);
  CHECK(flat.width() == 640 && flat.height() == 480);
  const auto near_photoshop = [&](std::int32_t x, std::int32_t y, std::array<int, 3> expected) {
    const auto* px = flat.pixel(x, y);
    for (int channel = 0; channel < 3; ++channel) {
      if (std::abs(static_cast<int>(px[channel]) - expected[static_cast<std::size_t>(channel)]) > 24) {
        return false;
      }
    }
    return true;
  };
  // Inside the base's mask hole, under opaque "torn" pixels: the clipped stack
  // must stay out and leave sky plus the frame's shadow.
  CHECK(near_photoshop(400, 350, {71, 78, 88}));
  CHECK(near_photoshop(450, 345, {118, 132, 150}));
  CHECK(near_photoshop(420, 360, {161, 182, 204}));
  // Where the base's mask is open, the same layer paints and both agree exactly.
  CHECK(near_photoshop(470, 335, {213, 229, 243}));
  CHECK(near_photoshop(500, 350, {237, 243, 249}));
}

// CMYK-mode documents store lfx2 effect colors as 'CMYC' descriptors (ink percentages) and
// text engine fill colors as /Type 2 values; both convert to sRGB through the document's
// embedded ICC profile, with the SAME transform as the pixel decode. The fixture is a
// PS 2026 CMYK/8 document embedding "U.S. Web Coated (SWOP) v2": "Overlay" is a
// C43 Y98 green fill carrying a color overlay of C42 M45 Y67 K13, "Label" is text colored
// C0 M100 Y100 K0 (Photoshop's classic CMYK red).
void psd_cmyk_document_converts_style_and_text_colors() {
  std::vector<std::string> notices;
  patchy::psd::ReadOptions options;
  options.notices = &notices;
  const auto document = patchy::psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path("photoshop-cmyk-style-colors.psd"), options);
  CHECK(std::any_of(notices.begin(), notices.end(), [](const std::string& notice) {
    return notice.find("U.S. Web Coated (SWOP) v2") != std::string::npos;
  }));

  const auto* overlay_layer = find_layer_named(document.layers(), "Overlay");
  CHECK(overlay_layer != nullptr);
  CHECK(overlay_layer->layer_style().color_overlays.size() == 1);
  const auto& overlay = overlay_layer->layer_style().color_overlays.front();
  CHECK(overlay.blend_mode == patchy::BlendMode::Normal);
  CHECK(overlay.opacity == 1.0F);
  CHECK(overlay.color.red == 143);
  CHECK(overlay.color.green == 123);
  CHECK(overlay.color.blue == 92);

  // The layer's filled pixels convert through the same profile: the C43 Y98 green ink
  // must land on the same sRGB value whether it arrives as pixels or as a descriptor.
  const auto& overlay_pixels = overlay_layer->pixels();
  CHECK(!overlay_pixels.empty());
  const auto* center = overlay_pixels.pixel(overlay_pixels.width() / 2, overlay_pixels.height() / 2);
  CHECK(center[0] == 158);
  CHECK(center[1] == 204);
  CHECK(center[2] == 62);

  const auto* text_layer = find_layer_named(document.layers(), "Label");
  CHECK(text_layer != nullptr);
  const auto text_color = text_layer->metadata().find(patchy::kLayerMetadataTextColor);
  CHECK(text_color != text_layer->metadata().end());
  CHECK(text_color->second == "#ed1c24");
}

void color_cmyk_transform_rejects_garbage_profile() {
  const std::vector<std::uint8_t> garbage{1, 2, 3, 4};
  CHECK(!patchy::CmykToRgbTransform::from_icc_profile(garbage).has_value());
  CHECK(!patchy::CmykToRgbTransform::from_icc_profile(std::vector<std::uint8_t>{}).has_value());
}

// Pins the lcms2 conversion of the real SWOP profile (extracted at runtime from the
// committed fixture's 1039 resource; Adobe profiles may only be distributed embedded in
// image files, never as standalone assets). Inputs use the inverted PSD convention.
void color_cmyk_transform_matches_pinned_swop_values() {
  const auto document = patchy::psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path("photoshop-cmyk-style-colors.psd"));
  const auto profile = test_image_resource_payload(document.metadata().raw_psd_image_resources, 1039);
  CHECK(profile.has_value());
  const auto transform = patchy::CmykToRgbTransform::from_icc_profile(*profile);
  CHECK(transform.has_value());
  CHECK(transform->profile_description() == "U.S. Web Coated (SWOP) v2");

  const auto check_color = [&](patchy::RgbColor color, int red, int green, int blue) {
    CHECK(color.red == red);
    CHECK(color.green == green);
    CHECK(color.blue == blue);
  };
  // Ink-free paper is white; pure K black is SWOP's warm dark gray, not RGB black.
  check_color(transform->convert_single(255, 255, 255, 255), 255, 255, 255);
  check_color(transform->convert_single(255, 255, 255, 0), 35, 31, 32);
  check_color(transform->convert_single(0, 0, 0, 0), 0, 0, 0);
  // C0 M100 Y100 K0: Photoshop's classic CMYK red.
  check_color(transform->convert_single(255, 0, 0, 255), 237, 28, 36);
}

void psd_qual_rca_pinout_imports_white_drop_shadows() {
  const auto path = qual_rca_pinout_fixture_path();
  CHECK(std::filesystem::exists(path));

  const auto document = patchy::psd::DocumentIo::read_file(path);
  const std::vector<std::string> label_names = {
      "1=G",
      "10=G",
      "9=Video",
      "4=Audio (R)",
      "5=Audio (W)",
  };
  for (const auto& name : label_names) {
    const auto* layer = find_layer_named(document.layers(), name);
    CHECK(layer != nullptr);
    const auto* shadow = first_enabled_drop_shadow(*layer);
    CHECK(shadow != nullptr);
    CHECK(shadow->blend_mode == patchy::BlendMode::Normal);
    CHECK(shadow->color.red == 255);
    CHECK(shadow->color.green == 255);
    CHECK(shadow->color.blue == 255);
    CHECK(close_float(shadow->opacity, 1.0F));
    CHECK(close_float(shadow->angle_degrees, 90.0F));
    CHECK(close_float(shadow->distance, 1.0F));
    CHECK(close_float(shadow->spread, 100.0F));
    CHECK(close_float(shadow->size, 21.0F));
  }
}

void psd_qual_rca_pinout_point_text_imports_as_point_text() {
  const auto path = qual_rca_pinout_fixture_path();
  CHECK(std::filesystem::exists(path));

  const auto document = patchy::psd::DocumentIo::read_file(path);
  const std::vector<std::string> point_text_layers = {
      "1=G",
      "10=G",
      "9=Video",
      "4=Audio (R)",
      "5=Audio (W)",
      "12345678910",
  };
  for (const auto& name : point_text_layers) {
    const auto* layer = find_layer_named(document.layers(), name);
    CHECK(layer != nullptr);
    CHECK(layer->metadata().at(patchy::kLayerMetadataTextFlow) == "point");
    CHECK(layer->metadata().at(patchy::kLayerMetadataTextSourceBlock) == "TySh");
    CHECK(layer->metadata().contains(patchy::kLayerMetadataTextTransform));
    CHECK(layer->metadata().contains(patchy::kLayerMetadataPsdTextTransform));
    CHECK(layer->metadata().contains(patchy::kLayerMetadataPsdTextBoundingBox));
    CHECK(std::stoi(layer->metadata().at(patchy::kLayerMetadataTextBoxWidth)) == layer->bounds().width);
    CHECK(std::stoi(layer->metadata().at(patchy::kLayerMetadataTextBoxHeight)) == layer->bounds().height);
  }
}

void psd_qual_rca_pinout_round_trips_styles_and_text_metadata() {
  const auto path = qual_rca_pinout_fixture_path();
  CHECK(std::filesystem::exists(path));

  const auto document = patchy::psd::DocumentIo::read_file(path);
  const auto round_tripped =
      patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(document));
  const auto* layer = find_layer_named(round_tripped.layers(), "5=Audio (W)");
  CHECK(layer != nullptr);
  CHECK(layer_has_psd_block(*layer, "lfx2"));
  CHECK(layer_has_psd_block(*layer, "lrFX"));
  CHECK(layer_has_psd_block(*layer, "TySh"));
  CHECK(!layer_has_psd_block(*layer, "plFX"));
  CHECK(layer->metadata().at(patchy::kLayerMetadataTextFlow) == "point");
  CHECK(layer->metadata().at(patchy::kLayerMetadataTextSourceBlock) == "TySh");
  const auto* shadow = first_enabled_drop_shadow(*layer);
  CHECK(shadow != nullptr);
  CHECK(shadow->blend_mode == patchy::BlendMode::Normal);
  CHECK(shadow->color.red == 255);
  CHECK(shadow->color.green == 255);
  CHECK(shadow->color.blue == 255);
  CHECK(close_float(shadow->opacity, 1.0F));
  CHECK(close_float(shadow->spread, 100.0F));
  CHECK(close_float(shadow->size, 21.0F));
}

void psd_qual_rca_pinout_writes_comparison_artifacts() {
  const auto path = qual_rca_pinout_fixture_path();
  CHECK(std::filesystem::exists(path));

  patchy::psd::ReadOptions flat_options;
  flat_options.prefer_flat_composite = true;
  const auto photoshop_reference = patchy::psd::DocumentIo::read_file(path, flat_options);
  const auto editable_document = patchy::psd::DocumentIo::read_file(path);
  const auto reference_flat = patchy::Compositor{}.flatten_rgb8(photoshop_reference);
  const auto patchy_flat = patchy::Compositor{}.flatten_rgb8(editable_document);
  CHECK(reference_flat.width() == patchy_flat.width());
  CHECK(reference_flat.height() == patchy_flat.height());

  const auto diff = rgb_diff_image(reference_flat, patchy_flat);
  const auto metrics = rgb_diff_metrics(reference_flat, patchy_flat);
  write_rgb8_bmp_artifact("psd_qual_rca_pinout_photoshop_composite", reference_flat);
  write_rgb8_bmp_artifact("psd_qual_rca_pinout_patchy_composite", patchy_flat);
  write_rgb8_bmp_artifact("psd_qual_rca_pinout_diff", diff);
  write_qual_rca_pinout_report(metrics, editable_document);

  CHECK(metrics.pixels == static_cast<std::uint64_t>(reference_flat.width()) *
                              static_cast<std::uint64_t>(reference_flat.height()));
  CHECK(std::filesystem::exists(std::filesystem::path("test-artifacts") /
                                "psd_qual_rca_pinout_compatibility_report.txt"));
  CHECK(std::filesystem::exists(std::filesystem::path("test-artifacts") /
                                "psd_qual_rca_pinout_compatibility_report.json"));
}

void psd_checkbox_bevel_emboss_writes_comparison_artifacts_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("checkbox.psd");
  if (!std::filesystem::exists(path)) {
    return;
  }

  patchy::psd::ReadOptions flat_options;
  flat_options.prefer_flat_composite = true;
  const auto photoshop_reference = patchy::psd::DocumentIo::read_file(path, flat_options);
  const auto editable_document = patchy::psd::DocumentIo::read_file(path);
  const auto reference_flat = patchy::Compositor{}.flatten_rgb8(photoshop_reference);
  const auto patchy_flat = patchy::Compositor{}.flatten_rgb8(editable_document);
  CHECK(reference_flat.width() == patchy_flat.width());
  CHECK(reference_flat.height() == patchy_flat.height());

  int bevel_layers = 0;
  std::function<void(const std::vector<patchy::Layer>&)> visit_layers = [&](const std::vector<patchy::Layer>& layers) {
    for (const auto& layer : layers) {
      if (!layer.layer_style().bevels.empty()) {
        ++bevel_layers;
      }
      visit_layers(layer.children());
    }
  };
  visit_layers(editable_document.layers());
  CHECK(bevel_layers >= 1);

  const auto diff = rgb_diff_image(reference_flat, patchy_flat);
  const auto metrics = rgb_diff_metrics(reference_flat, patchy_flat);
  write_rgb8_bmp_artifact("psd_checkbox_photoshop_composite", reference_flat);
  write_rgb8_bmp_artifact("psd_checkbox_patchy_composite", patchy_flat);
  write_rgb8_bmp_artifact("psd_checkbox_diff", diff);

  std::filesystem::create_directories("test-artifacts");
  std::ofstream report(std::filesystem::path("test-artifacts") / "psd_checkbox_compatibility_report.txt");
  report << "PSD compatibility comparison: checkbox.psd\n";
  report << "pixels: " << metrics.pixels << "\n";
  report << "differing_pixels: " << metrics.differing_pixels << "\n";
  report << "mean_abs_channel_delta: " << std::fixed << std::setprecision(3) << metrics.mean_abs_channel_delta
         << "\n";
  report << "max_channel_delta: " << metrics.max_channel_delta << "\n";
  report << "bevel_layers: " << bevel_layers << "\n";
}

void psd_adjustment_layers_render_and_round_trip() {
  patchy::Document document(2, 2, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Base", solid_rgb(2, 2, 120, 40, 40));

  patchy::AdjustmentSettings settings;
  settings.kind = patchy::AdjustmentKind::ColorBalance;
  settings.color_balance = patchy::ColorBalanceAdjustment{50, 0, 0};
  patchy::Layer adjustment(document.allocate_layer_id(), "Warmth", patchy::LayerKind::Adjustment);
  adjustment.set_bounds(patchy::Rect::from_size(document.width(), document.height()));
  patchy::configure_adjustment_layer(adjustment, settings);
  document.add_layer(std::move(adjustment));

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  CHECK(flattened.pixel(0, 0)[0] > 240);
  CHECK(flattened.pixel(0, 0)[1] == 40);

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  auto round_tripped = patchy::psd::DocumentIo::read(bytes);
  CHECK(round_tripped.layers().size() == 2);
  CHECK(round_tripped.layers().back().kind() == patchy::LayerKind::Adjustment);
  const auto round_tripped_settings = patchy::adjustment_settings_from_layer(round_tripped.layers().back());
  CHECK(round_tripped_settings.has_value());
  CHECK(round_tripped_settings->kind == patchy::AdjustmentKind::ColorBalance);
  CHECK(round_tripped_settings->color_balance.cyan_red == 50);
  const auto round_tripped_flattened = patchy::Compositor{}.flatten_rgb8(round_tripped);
  CHECK(round_tripped_flattened.pixel(0, 0)[0] == flattened.pixel(0, 0)[0]);
  CHECK(round_tripped_flattened.pixel(0, 0)[1] == flattened.pixel(0, 0)[1]);
}

}  // namespace

std::vector<patchy::test::TestCase> pattern_styles_fixtures_tests() {
  return {
      {"pattern_presets_generate_stable_tiles", pattern_presets_generate_stable_tiles},
      {"style_contour_lut_handles_presets_and_corners", style_contour_lut_handles_presets_and_corners},
      {"psd_photoshop_pattern_overlay_fixture_imports", psd_photoshop_pattern_overlay_fixture_imports},
      {"psd_photoshop_pattern_transparent_fixture_decodes_alpha",
       psd_photoshop_pattern_transparent_fixture_decodes_alpha},
      {"psd_photoshop_bevel_subs_fixture_round_trips", psd_photoshop_bevel_subs_fixture_round_trips},
      {"psd_photoshop_pattern_bevel_roundtrip_fixture_imports",
       psd_photoshop_pattern_bevel_roundtrip_fixture_imports},
      {"psd_pattern_overlay_added_in_patchy_writes_pattern_block",
       psd_pattern_overlay_added_in_patchy_writes_pattern_block},
      {"compositor_renders_layer_style_pattern_overlay", compositor_renders_layer_style_pattern_overlay},
      {"compositor_bevel_gloss_and_contour_subs_change_lighting",
       compositor_bevel_gloss_and_contour_subs_change_lighting},
      {"compositor_bevel_texture_responds_to_depth_and_invert",
       compositor_bevel_texture_responds_to_depth_and_invert},
      {"psd_writer_uses_preserved_photoshop_style_blocks_without_private_duplicates",
       psd_writer_uses_preserved_photoshop_style_blocks_without_private_duplicates},
      {"psd_arrows_imports_photoshop_inner_effects",
       psd_arrows_imports_photoshop_inner_effects},
      {"psd_photoshop_outer_glow_fixtures_match_render",
       psd_photoshop_outer_glow_fixtures_match_render},
      {"psd_photoshop_inner_glow_fixtures_match_render",
       psd_photoshop_inner_glow_fixtures_match_render},
      {"psd_photoshop_inner_shadow_fixture_matches_render",
       psd_photoshop_inner_shadow_fixture_matches_render},
      {"psd_photoshop_gradient_overlay_geometry_fixture_matches_render",
       psd_photoshop_gradient_overlay_geometry_fixture_matches_render},
      {"psd_photoshop_bevel_smooth_fixture_matches_render",
       psd_photoshop_bevel_smooth_fixture_matches_render},
      {"psd_photoshop_bevel_gloss_fixture_matches_render",
       psd_photoshop_bevel_gloss_fixture_matches_render},
      {"psd_pinball_gloss_pillow_renders_clean_background_if_available",
       psd_pinball_gloss_pillow_renders_clean_background_if_available},
      {"psd_photoshop_bevel_texture_fixtures_match_render",
       psd_photoshop_bevel_texture_fixtures_match_render},
      {"psd_photoshop_stroke_aa_matte_fixture_matches_render",
       psd_photoshop_stroke_aa_matte_fixture_matches_render},
      {"psd_photoshop_stroke_overprint_fixture_matches_render",
       psd_photoshop_stroke_overprint_fixture_matches_render},
      {"psd_photoshop_shadow_conceals_fixture_matches_render",
       psd_photoshop_shadow_conceals_fixture_matches_render},
      {"psd_photoshop_interior_exterior_blending_fixture_matches_render",
       psd_photoshop_interior_exterior_blending_fixture_matches_render},
      {"psd_photoshop_pillow_emboss_fixtures_match_render",
       psd_photoshop_pillow_emboss_fixtures_match_render},
      {"psd_photoshop_stroke_shapeburst_fixture_matches_render",
       psd_photoshop_stroke_shapeburst_fixture_matches_render},
      {"psd_lrfx_legacy_drop_shadow_parses_fixed_point",
       psd_lrfx_legacy_drop_shadow_parses_fixed_point},
      {"psd_lfx2_charid_blend_mode_enum_parses", psd_lfx2_charid_blend_mode_enum_parses},
      {"psd_weedkiller_legacy_charid_styles_parse_if_available",
       psd_weedkiller_legacy_charid_styles_parse_if_available},
      {"psd_lfx2_disabled_effect_suppresses_legacy_lrfx_if_available",
       psd_lfx2_disabled_effect_suppresses_legacy_lrfx_if_available},
      {"psd_lrfx_legacy_title_screen_imports_drop_shadows_if_available",
       psd_lrfx_legacy_title_screen_imports_drop_shadows_if_available},
      {"psd_lrfx_legacy_title_screen_clips_stack_to_base_mask_if_available",
       psd_lrfx_legacy_title_screen_clips_stack_to_base_mask_if_available},
      {"psd_cmyk_document_converts_style_and_text_colors", psd_cmyk_document_converts_style_and_text_colors},
      {"color_cmyk_transform_rejects_garbage_profile", color_cmyk_transform_rejects_garbage_profile},
      {"color_cmyk_transform_matches_pinned_swop_values", color_cmyk_transform_matches_pinned_swop_values},
      {"psd_qual_rca_pinout_imports_white_drop_shadows",
       psd_qual_rca_pinout_imports_white_drop_shadows},
      {"psd_qual_rca_pinout_point_text_imports_as_point_text",
       psd_qual_rca_pinout_point_text_imports_as_point_text},
      {"psd_qual_rca_pinout_round_trips_styles_and_text_metadata",
       psd_qual_rca_pinout_round_trips_styles_and_text_metadata},
      {"psd_qual_rca_pinout_writes_comparison_artifacts",
       psd_qual_rca_pinout_writes_comparison_artifacts},
      {"psd_checkbox_bevel_emboss_writes_comparison_artifacts_if_available",
       psd_checkbox_bevel_emboss_writes_comparison_artifacts_if_available},
      {"psd_adjustment_layers_render_and_round_trip", psd_adjustment_layers_render_and_round_trip},
  };
}
