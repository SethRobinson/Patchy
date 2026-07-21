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

// The PS 5.x 'dsdw' record stores blur/intensity/angle/distance as 16.16 fixed
// point and opacity as a 0-255 byte. The pre-July-2026 parser read the fixed
// fields as raw integers one slot early, so a legacy shadow could carry a
// ~7.8-million-pixel distance and abort the whole flatten on allocation.
void psd_lrfx_legacy_drop_shadow_parses_fixed_point() {
  patchy::psd::BigEndianWriter writer;
  writer.write_u16(0);  // effects version
  writer.write_u16(1);  // effect count
  writer.write_bytes(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>("8BIMdsdw"), 8));
  patchy::psd::BigEndianWriter effect;
  effect.write_u32(51);  // record size
  effect.write_u32(2);   // version
  effect.write_u32(12U << 16U);   // blur (size), fixed
  effect.write_u32(0);            // intensity
  effect.write_u32(120U << 16U);  // angle, fixed
  effect.write_u32(5U << 16U);    // distance, fixed
  effect.write_u16(0);  // color space
  effect.write_u16(0xFFFF);
  effect.write_u16(0);
  effect.write_u16(0);
  effect.write_u16(0);
  effect.write_bytes(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>("8BIMlbrn"), 8));
  effect.write_u8(1);   // enabled
  effect.write_u8(1);   // use global angle
  effect.write_u8(71);  // opacity byte (28%)
  const auto& effect_bytes = effect.bytes();
  writer.write_u32(static_cast<std::uint32_t>(effect_bytes.size()));
  writer.write_bytes(effect_bytes);

  const auto style = patchy::psd::parse_lrfx_layer_style(writer.bytes());
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
      {"psd_lrfx_legacy_drop_shadow_parses_fixed_point",
       psd_lrfx_legacy_drop_shadow_parses_fixed_point},
      {"psd_lfx2_disabled_effect_suppresses_legacy_lrfx_if_available",
       psd_lfx2_disabled_effect_suppresses_legacy_lrfx_if_available},
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
