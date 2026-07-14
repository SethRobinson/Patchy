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

using patchy::test::close_float;
using patchy::test::kTestBlendIfIdentityEntry;
using patchy::test::solid_rgb;
using patchy::test::solid_rgba;
using patchy::test::test_blend_if_identity_payload;

void blend_if_codec_decodes_default_and_identity() {
  const patchy::LayerBlendIf identity;
  const auto empty =
      patchy::decode_layer_blend_if(std::span<const std::uint8_t>{});
  CHECK(empty.status == patchy::BlendIfPayloadStatus::Empty);
  CHECK(empty.settings == identity);
  CHECK(patchy::blend_if_is_identity(empty.settings));
  CHECK(!patchy::blend_if_payload_has_non_identity_or_unsupported(
      std::span<const std::uint8_t>{}));

  const auto identity_payload = test_blend_if_identity_payload();
  const auto decoded = patchy::decode_layer_blend_if(identity_payload);
  CHECK(decoded.status == patchy::BlendIfPayloadStatus::Supported);
  CHECK(decoded.settings == identity);
  CHECK(patchy::blend_if_is_identity(decoded.settings));
  CHECK(!patchy::blend_if_payload_has_non_identity_or_unsupported(
      identity_payload));
  CHECK(patchy::encode_layer_blend_if(decoded.settings).empty());
  CHECK(patchy::encode_layer_blend_if(decoded.settings, identity_payload) ==
        identity_payload);
}

void blend_if_codec_round_trips_unique_rgb_ranges() {
  const std::vector<std::uint8_t> payload{
      1, 11, 201, 241, 2, 12, 202, 242, // Gray: This, Underlying
      3, 13, 203, 243, 4, 14, 204, 244, // Red
      5, 15, 205, 245, 6, 16, 206, 246, // Green
      7, 17, 207, 247, 8, 18, 208, 248, // Blue
      0, 0,  255, 255, 0, 0,  255, 255, // Photoshop's identity fifth pair
  };
  patchy::LayerBlendIf expected;
  expected.channels[static_cast<std::size_t>(patchy::BlendIfChannel::Gray)] = {
      patchy::BlendIfThresholds{1, 11, 201, 241},
      patchy::BlendIfThresholds{2, 12, 202, 242}};
  expected.channels[static_cast<std::size_t>(patchy::BlendIfChannel::Red)] = {
      patchy::BlendIfThresholds{3, 13, 203, 243},
      patchy::BlendIfThresholds{4, 14, 204, 244}};
  expected.channels[static_cast<std::size_t>(patchy::BlendIfChannel::Green)] = {
      patchy::BlendIfThresholds{5, 15, 205, 245},
      patchy::BlendIfThresholds{6, 16, 206, 246}};
  expected.channels[static_cast<std::size_t>(patchy::BlendIfChannel::Blue)] = {
      patchy::BlendIfThresholds{7, 17, 207, 247},
      patchy::BlendIfThresholds{8, 18, 208, 248}};

  const auto decoded = patchy::decode_layer_blend_if(payload);
  CHECK(decoded.status == patchy::BlendIfPayloadStatus::Supported);
  CHECK(decoded.settings == expected);
  CHECK(!patchy::blend_if_is_identity(decoded.settings));
  CHECK(patchy::blend_if_payload_has_non_identity_or_unsupported(payload));
  CHECK(patchy::encode_layer_blend_if(decoded.settings) == payload);
  CHECK(patchy::encode_layer_blend_if(decoded.settings, payload) == payload);
}

void blend_if_codec_rejects_unsupported_payloads() {
  const std::vector<std::uint8_t> short_payload(
      kTestBlendIfIdentityEntry.begin(), kTestBlendIfIdentityEntry.end());
  auto odd_payload = test_blend_if_identity_payload();
  odd_payload.resize(39U);
  auto invalid_order = test_blend_if_identity_payload();
  invalid_order[0] = 20;
  invalid_order[1] = 10;
  auto non_identity_tail = test_blend_if_identity_payload();
  non_identity_tail[33] = 1;

  const std::array<std::span<const std::uint8_t>, 4> unsupported_payloads{
      short_payload, odd_payload, invalid_order, non_identity_tail};
  for (const auto payload : unsupported_payloads) {
    const auto decoded = patchy::decode_layer_blend_if(payload);
    CHECK(decoded.status == patchy::BlendIfPayloadStatus::Unsupported);
    CHECK(patchy::blend_if_is_identity(decoded.settings));
    CHECK(patchy::blend_if_payload_has_non_identity_or_unsupported(payload));
  }

  patchy::LayerBlendIf invalid_settings;
  invalid_settings.channels.front().this_layer =
      patchy::BlendIfThresholds{20, 10, 200, 240};
  CHECK(!patchy::blend_if_thresholds_are_valid(
      invalid_settings.channels.front().this_layer));
  bool threw = false;
  try {
    (void)patchy::encode_layer_blend_if(invalid_settings);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  CHECK(threw);
}

void layer_blend_if_setter_tracks_revisions_and_replacement() {
  patchy::Layer layer(11, "Blend If", solid_rgba(4, 4, 20, 40, 60, 255));
  const auto identity_payload = test_blend_if_identity_payload();
  const auto initial_render_revision = layer.render_revision();
  const auto initial_content_revision = layer.content_revision();
  layer.raw_psd_blending_ranges() = identity_payload;
  CHECK(layer.render_revision() == initial_render_revision);
  CHECK(layer.content_revision() == initial_content_revision);
  CHECK(layer.blend_if_payload_status() ==
        patchy::BlendIfPayloadStatus::Supported);

  auto settings = layer.blend_if();
  settings.channels[static_cast<std::size_t>(patchy::BlendIfChannel::Red)]
      .this_layer = patchy::BlendIfThresholds{9, 21, 190, 231};
  CHECK(layer.set_blend_if(settings));
  CHECK(layer.render_revision() > initial_render_revision);
  CHECK(layer.content_revision() > initial_content_revision);
  CHECK(layer.raw_psd_blending_ranges() ==
        patchy::encode_layer_blend_if(settings, identity_payload));

  const auto unchanged_render_revision = layer.render_revision();
  const auto unchanged_content_revision = layer.content_revision();
  CHECK(layer.set_blend_if(settings));
  CHECK(layer.render_revision() == unchanged_render_revision);
  CHECK(layer.content_revision() == unchanged_content_revision);

  const std::vector<std::uint8_t> unsupported(kTestBlendIfIdentityEntry.begin(),
                                              kTestBlendIfIdentityEntry.end());
  layer.raw_psd_blending_ranges() = unsupported;
  CHECK(layer.render_revision() == unchanged_render_revision);
  CHECK(layer.content_revision() == unchanged_content_revision);
  CHECK(layer.blend_if_payload_status() ==
        patchy::BlendIfPayloadStatus::Unsupported);
  CHECK(!layer.set_blend_if(settings));
  CHECK(layer.raw_psd_blending_ranges() == unsupported);
  CHECK(layer.render_revision() == unchanged_render_revision);
  CHECK(layer.content_revision() == unchanged_content_revision);

  CHECK(layer.set_blend_if(settings, true));
  CHECK(layer.blend_if_payload_status() ==
        patchy::BlendIfPayloadStatus::Supported);
  CHECK(layer.raw_psd_blending_ranges() ==
        patchy::encode_layer_blend_if(settings));
  CHECK(layer.render_revision() > unchanged_render_revision);
  CHECK(layer.content_revision() > unchanged_content_revision);
}

void blend_if_thresholds_feather_endpoints_and_multiply_channels() {
  constexpr float kTolerance = 0.000001F;
  const patchy::BlendIfThresholds joined{64, 64, 192, 192};
  CHECK(close_float(patchy::blend_if_threshold_factor(joined, 63), 0.0F,
                    kTolerance));
  CHECK(close_float(patchy::blend_if_threshold_factor(joined, 64), 1.0F,
                    kTolerance));
  CHECK(close_float(patchy::blend_if_threshold_factor(joined, 192), 1.0F,
                    kTolerance));
  CHECK(close_float(patchy::blend_if_threshold_factor(joined, 193), 0.0F,
                    kTolerance));

  const patchy::BlendIfThresholds split{10, 13, 20, 23};
  CHECK(close_float(patchy::blend_if_threshold_factor(split, 9), 0.0F,
                    kTolerance));
  CHECK(close_float(patchy::blend_if_threshold_factor(split, 10), 0.25F,
                    kTolerance));
  CHECK(close_float(patchy::blend_if_threshold_factor(split, 11), 0.50F,
                    kTolerance));
  CHECK(close_float(patchy::blend_if_threshold_factor(split, 12), 0.75F,
                    kTolerance));
  CHECK(close_float(patchy::blend_if_threshold_factor(split, 13), 1.0F,
                    kTolerance));
  CHECK(close_float(patchy::blend_if_threshold_factor(split, 20), 1.0F,
                    kTolerance));
  CHECK(close_float(patchy::blend_if_threshold_factor(split, 21), 0.75F,
                    kTolerance));
  CHECK(close_float(patchy::blend_if_threshold_factor(split, 22), 0.50F,
                    kTolerance));
  CHECK(close_float(patchy::blend_if_threshold_factor(split, 23), 0.25F,
                    kTolerance));
  CHECK(close_float(patchy::blend_if_threshold_factor(split, 24), 0.0F,
                    kTolerance));

  CHECK(patchy::blend_if_gray_value(patchy::RgbColor{255, 0, 0}) == 76);
  CHECK(patchy::blend_if_gray_value(patchy::RgbColor{0, 255, 0}) == 150);
  CHECK(patchy::blend_if_gray_value(patchy::RgbColor{0, 0, 255}) == 28);

  patchy::LayerBlendIf settings;
  auto &gray =
      settings.channels[static_cast<std::size_t>(patchy::BlendIfChannel::Gray)];
  auto &red =
      settings.channels[static_cast<std::size_t>(patchy::BlendIfChannel::Red)];
  auto &green =
      settings
          .channels[static_cast<std::size_t>(patchy::BlendIfChannel::Green)];
  auto &blue =
      settings.channels[static_cast<std::size_t>(patchy::BlendIfChannel::Blue)];
  gray.this_layer = patchy::BlendIfThresholds{9, 12, 255, 255}; // 3/4 at 11
  red.this_layer = patchy::BlendIfThresholds{10, 13, 255, 255}; // 1/2 at 11
  blue.this_layer = patchy::BlendIfThresholds{0, 0, 9, 12};     // 1/2 at 11
  gray.underlying_layer = patchy::BlendIfThresholds{10, 13, 255, 255}; // 1/2
  green.underlying_layer = patchy::BlendIfThresholds{0, 0, 10, 13};    // 3/4
  blue.underlying_layer = patchy::BlendIfThresholds{9, 12, 255, 255};  // 3/4

  const patchy::RgbColor value{11, 11, 11};
  CHECK(patchy::blend_if_gray_value(value) == 11);
  CHECK(close_float(patchy::blend_if_source_factor(settings, value),
                    3.0F / 16.0F, kTolerance));
  CHECK(close_float(patchy::blend_if_underlying_factor(settings, value),
                    9.0F / 32.0F, kTolerance));
}

void compositor_blend_if_scales_this_layer_alpha_and_tests_underlying_coverage() {
  constexpr auto gray_index = static_cast<std::size_t>(patchy::BlendIfChannel::Gray);

  // This Layer coverage multiplies the source alpha instead of changing the
  // straight source color. A 128-alpha pixel at 127/255 Blend If coverage
  // therefore exports with 64 alpha and its original RGB.
  {
    patchy::Document document(1, 1, patchy::PixelFormat::rgba8());
    patchy::Layer layer(document.allocate_layer_id(), "This Layer",
                        solid_rgba(1, 1, 100, 100, 100, 128));
    patchy::LayerBlendIf settings;
    settings.channels[gray_index].this_layer =
        patchy::BlendIfThresholds{99, 102, 255, 255};
    CHECK(patchy::blend_if_source_alpha_byte(settings, patchy::RgbColor{100, 100, 100}) == 127);
    CHECK(layer.set_blend_if(settings));
    document.add_layer(std::move(layer));

    std::vector<std::uint8_t> merged_alpha;
    const auto flattened = patchy::Compositor{}.flatten_rgb8(document, &merged_alpha);
    CHECK(merged_alpha.size() == 1U);
    CHECK(merged_alpha[0] == 64);
    CHECK(flattened.pixel(0, 0)[0] == 100);
    CHECK(flattened.pixel(0, 0)[1] == 100);
    CHECK(flattened.pixel(0, 0)[2] == 100);
  }

  // Photoshop treats the transparent fraction of the backdrop as passing the
  // Underlying Layer test. The same black backdrop therefore passes fully at
  // alpha 0, passes halfway at alpha 128, and is blocked at alpha 255.
  {
    patchy::Document document(3, 1, patchy::PixelFormat::rgba8());
    auto backdrop = solid_rgba(3, 1, 0, 0, 0, 255);
    backdrop.pixel(0, 0)[3] = 0;
    backdrop.pixel(1, 0)[3] = 128;
    document.add_pixel_layer("Backdrop", std::move(backdrop));

    patchy::Layer layer(document.allocate_layer_id(), "Underlying Layer",
                        solid_rgba(3, 1, 255, 0, 0, 255));
    patchy::LayerBlendIf settings;
    settings.channels[gray_index].underlying_layer =
        patchy::BlendIfThresholds{128, 128, 255, 255};
    CHECK(layer.set_blend_if(settings));
    document.add_layer(std::move(layer));

    std::vector<std::uint8_t> merged_alpha;
    const auto flattened = patchy::Compositor{}.flatten_rgb8(document, &merged_alpha);
    CHECK(merged_alpha.size() == 3U);
    CHECK(flattened.pixel(0, 0)[0] == 255);
    CHECK(merged_alpha[0] == 255);
    CHECK(flattened.pixel(1, 0)[0] == 169);
    CHECK(merged_alpha[1] == 191);
    CHECK(flattened.pixel(2, 0)[0] == 0);
    CHECK(merged_alpha[2] == 255);
  }
}

void compositor_blend_if_does_not_gate_layer_effects() {
  patchy::Document document(1, 1, patchy::PixelFormat::rgba8());
  patchy::Layer layer(document.allocate_layer_id(), "Hidden Blue",
                      solid_rgba(1, 1, 0, 0, 255, 255));

  patchy::LayerBlendIf settings;
  settings.channels[static_cast<std::size_t>(patchy::BlendIfChannel::Gray)].this_layer =
      patchy::BlendIfThresholds{128, 128, 255, 255};
  CHECK(patchy::blend_if_source_alpha_byte(settings, patchy::RgbColor{0, 0, 255}) == 0);
  CHECK(layer.set_blend_if(settings));

  patchy::LayerColorOverlay overlay;
  overlay.enabled = true;
  overlay.blend_mode = patchy::BlendMode::Normal;
  overlay.color = patchy::RgbColor{0, 255, 0};
  overlay.opacity = 1.0F;
  layer.layer_style().color_overlays.push_back(overlay);
  document.add_layer(std::move(layer));

  std::vector<std::uint8_t> merged_alpha;
  const auto flattened = patchy::Compositor{}.flatten_rgb8(document, &merged_alpha);
  CHECK(flattened.pixel(0, 0)[0] == 0);
  CHECK(flattened.pixel(0, 0)[1] == 255);
  CHECK(flattened.pixel(0, 0)[2] == 0);
  CHECK(merged_alpha.size() == 1U);
  CHECK(merged_alpha[0] == 255);
}

void compositor_blend_if_adjustment_tests_adjusted_this_and_original_underlying() {
  constexpr auto gray_index = static_cast<std::size_t>(patchy::BlendIfChannel::Gray);
  const auto render = [](bool gate_this_layer) {
    patchy::Document document(1, 1, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Backdrop", solid_rgb(1, 1, 50, 50, 50));

    patchy::AdjustmentSettings levels;
    levels.kind = patchy::AdjustmentKind::Levels;
    levels.levels.black_output = 200;
    levels.levels.white_output = 255;
    patchy::Layer adjustment(document.allocate_layer_id(), "Levels", patchy::LayerKind::Adjustment);
    adjustment.set_bounds(patchy::Rect::from_size(1, 1));
    patchy::configure_adjustment_layer(adjustment, levels);

    patchy::LayerBlendIf blend_if;
    auto& ranges = blend_if.channels[gray_index];
    if (gate_this_layer) {
      ranges.this_layer = patchy::BlendIfThresholds{128, 128, 255, 255};
    } else {
      ranges.underlying_layer = patchy::BlendIfThresholds{128, 128, 255, 255};
    }
    CHECK(adjustment.set_blend_if(blend_if));
    document.add_layer(std::move(adjustment));
    return patchy::Compositor{}.flatten_rgb8(document);
  };

  const auto gated_by_this = render(true);
  // Levels maps the original 50 to 211. This Layer evaluates that adjusted
  // result, so the adjustment passes the 128 cutoff.
  CHECK(gated_by_this.pixel(0, 0)[0] == 211);
  CHECK(gated_by_this.pixel(0, 0)[1] == 211);
  CHECK(gated_by_this.pixel(0, 0)[2] == 211);

  const auto gated_by_underlying = render(false);
  // Underlying Layer evaluates the pre-adjustment 50, so it blocks the same
  // adjustment and leaves the backdrop unchanged.
  CHECK(gated_by_underlying.pixel(0, 0)[0] == 50);
  CHECK(gated_by_underlying.pixel(0, 0)[1] == 50);
  CHECK(gated_by_underlying.pixel(0, 0)[2] == 50);
}

void compositor_blend_if_gates_group_composite() {
  patchy::Document document(2, 1, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Backdrop", solid_rgb(2, 1, 20, 20, 20));

  auto child_pixels = solid_rgb(2, 1, 100, 100, 100);
  auto* bright = child_pixels.pixel(1, 0);
  bright[0] = 200;
  bright[1] = 200;
  bright[2] = 200;

  patchy::Layer group(document.allocate_layer_id(), "Normal Group", patchy::LayerKind::Group);
  group.set_blend_mode(patchy::BlendMode::Normal);
  group.add_child(patchy::Layer(document.allocate_layer_id(), "Child", std::move(child_pixels)));
  patchy::LayerBlendIf settings;
  settings.channels[static_cast<std::size_t>(patchy::BlendIfChannel::Gray)].this_layer =
      patchy::BlendIfThresholds{128, 128, 255, 255};
  CHECK(group.set_blend_if(settings));
  document.add_layer(std::move(group));

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  CHECK(flattened.pixel(0, 0)[0] == 20);
  CHECK(flattened.pixel(1, 0)[0] == 200);
}

void compositor_blend_if_clip_base_keeps_original_coverage() {
  patchy::Document document(1, 1, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Blue Backdrop", solid_rgb(1, 1, 0, 0, 255));

  patchy::Layer base(document.allocate_layer_id(), "Hidden Red Base",
                     solid_rgba(1, 1, 255, 0, 0, 255));
  patchy::LayerBlendIf settings;
  settings.channels[static_cast<std::size_t>(patchy::BlendIfChannel::Gray)].this_layer =
      patchy::BlendIfThresholds{128, 128, 255, 255};
  CHECK(patchy::blend_if_source_alpha_byte(settings, patchy::RgbColor{255, 0, 0}) == 0);
  CHECK(base.set_blend_if(settings));
  document.add_layer(std::move(base));

  patchy::Layer clipped(document.allocate_layer_id(), "Green Clip",
                        solid_rgba(1, 1, 0, 255, 0, 255));
  clipped.set_clipped(true);
  document.add_layer(std::move(clipped));

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  // Blend If hides the base color, but the base's original alpha still defines
  // the clipping shape, so the clipped member remains visible.
  CHECK(flattened.pixel(0, 0)[0] == 0);
  CHECK(flattened.pixel(0, 0)[1] == 255);
  CHECK(flattened.pixel(0, 0)[2] == 0);
}

void compositor_pass_through_group_blend_if_isolates_adjustment_child() {
  const auto gray_pair = [](std::uint8_t left, std::uint8_t right) {
    patchy::PixelBuffer pixels(2, 1, patchy::PixelFormat::rgb8());
    for (int channel = 0; channel < 3; ++channel) {
      pixels.pixel(0, 0)[channel] = left;
      pixels.pixel(1, 0)[channel] = right;
    }
    return pixels;
  };
  const auto render = [&](const patchy::LayerBlendIf& blend_if, bool with_pixel_child) {
    patchy::Document document(2, 1, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Background", with_pixel_child ? gray_pair(20, 220) : gray_pair(50, 150));

    patchy::Layer group(document.allocate_layer_id(), "Pass Through Blend If", patchy::LayerKind::Group);
    group.set_blend_mode(patchy::BlendMode::PassThrough);
    CHECK(group.set_blend_if(blend_if));
    if (with_pixel_child) {
      group.add_child(patchy::Layer(document.allocate_layer_id(), "Group Pixels", gray_pair(50, 150)));
    }

    patchy::AdjustmentSettings settings;
    settings.kind = patchy::AdjustmentKind::Curves;
    settings.curves.rgb = {{0, 255}, {255, 0}};
    patchy::Layer adjustment(document.allocate_layer_id(), "Invert Curves", patchy::LayerKind::Adjustment);
    adjustment.set_bounds(patchy::Rect::from_size(2, 1));
    patchy::configure_adjustment_layer(adjustment, settings);
    group.add_child(std::move(adjustment));
    document.add_layer(std::move(group));
    return patchy::Compositor{}.flatten_rgb8(document);
  };
  const auto expect_gray_pair = [](const patchy::PixelBuffer& pixels, int left, int right) {
    for (int channel = 0; channel < 3; ++channel) {
      CHECK(pixels.pixel(0, 0)[channel] == left);
      CHECK(pixels.pixel(1, 0)[channel] == right);
    }
  };

  // Photoshop 27.8 COM (July 2026): identity Pass Through lets the adjustment affect the
  // outside backdrop. Any nonidentity group range isolates the children first,
  // so an adjustment-only group has no source pixels and becomes a no-op.
  expect_gray_pair(render({}, false), 205, 105);
  patchy::LayerBlendIf underlying_black;
  underlying_black.channels[0].underlying_layer = {100, 100, 255, 255};
  expect_gray_pair(render(underlying_black, false), 50, 150);
  patchy::LayerBlendIf this_black;
  this_black.channels[0].this_layer = {150, 150, 255, 255};
  expect_gray_pair(render(this_black, false), 50, 150);

  // With pixel content, the Curves child adjusts the isolated source to
  // [205,105]. Underlying samples the outside [20,220], while This samples the
  // adjusted isolated result.
  expect_gray_pair(render({}, true), 205, 105);
  expect_gray_pair(render(underlying_black, true), 20, 105);
  expect_gray_pair(render(this_black, true), 205, 220);
  patchy::LayerBlendIf this_white;
  this_white.channels[0].this_layer = {0, 0, 130, 130};
  expect_gray_pair(render(this_white, true), 20, 105);
}

void compositor_flattens_visible_layers() {
  patchy::Document document(1, 1, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Base", solid_rgb(1, 1, 10, 20, 30));
  auto top_pixels = solid_rgb(1, 1, 110, 120, 130);
  auto& top = document.add_pixel_layer("Top", std::move(top_pixels));
  top.set_opacity(0.5F);

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  const auto* px = flattened.pixel(0, 0);
  CHECK(px[0] == 60);
  CHECK(px[1] == 70);
  CHECK(px[2] == 80);
}

void compositor_multiply_uses_empty_backdrop_as_transparent() {
  patchy::Document transparent_document(1, 1, patchy::PixelFormat::rgba8());
  auto& transparent_multiply =
      transparent_document.add_pixel_layer("Multiply", solid_rgba(1, 1, 200, 100, 50, 128));
  transparent_multiply.set_blend_mode(patchy::BlendMode::Multiply);

  const auto transparent_flattened = patchy::Compositor{}.flatten_rgb8(transparent_document);
  const auto* transparent_px = transparent_flattened.pixel(0, 0);
  CHECK(transparent_px[0] == 200);
  CHECK(transparent_px[1] == 100);
  CHECK(transparent_px[2] == 50);

  patchy::Document opaque_document(1, 1, patchy::PixelFormat::rgb8());
  opaque_document.add_pixel_layer("Base", solid_rgb(1, 1, 100, 160, 240));
  auto& opaque_multiply = opaque_document.add_pixel_layer("Multiply", solid_rgba(1, 1, 200, 100, 50, 255));
  opaque_multiply.set_blend_mode(patchy::BlendMode::Multiply);

  const auto opaque_flattened = patchy::Compositor{}.flatten_rgb8(opaque_document);
  const auto* opaque_px = opaque_flattened.pixel(0, 0);
  CHECK(opaque_px[0] == 78);
  CHECK(opaque_px[1] == 62);
  CHECK(opaque_px[2] == 47);
}

void compositor_applies_extended_blend_modes() {
  struct ExpectedBlend {
    patchy::BlendMode mode;
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
  };

  // Saturation/Luminosity were re-pinned July 2026 when the non-separable modes moved from
  // an HSL-lightness approximation to the PDF-spec luma algorithm Photoshop and Aseprite
  // share (Hue/Color/Exclusion/LinearDodge/Subtract/Divide were added at the same time).
  const std::vector<ExpectedBlend> expected = {
      {patchy::BlendMode::Darken, 100, 60, 100},
      {patchy::BlendMode::Lighten, 200, 120, 140},
      {patchy::BlendMode::ColorDodge, 255, 156, 230},
      {patchy::BlendMode::ColorBurn, 58, 0, 0},
      {patchy::BlendMode::HardLight, 189, 56, 109},
      {patchy::BlendMode::SoftLight, 134, 86, 126},
      {patchy::BlendMode::Difference, 100, 60, 40},
      {patchy::BlendMode::LinearBurn, 45, 0, 0},
      {patchy::BlendMode::PinLight, 144, 120, 140},
      {patchy::BlendMode::Saturation, 60, 130, 200},
      {patchy::BlendMode::Luminosity, 90, 110, 130},
      {patchy::BlendMode::Exclusion, 144, 124, 130},
      {patchy::BlendMode::Hue, 143, 103, 114},
      {patchy::BlendMode::Color, 210, 70, 110},
      {patchy::BlendMode::LinearDodge, 255, 180, 240},
      {patchy::BlendMode::Subtract, 0, 60, 40},
      {patchy::BlendMode::Divide, 128, 255, 255},
  };

  for (const auto& blend : expected) {
    patchy::Document document(1, 1, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Base", solid_rgb(1, 1, 100, 120, 140));
    auto& top = document.add_pixel_layer("Top", solid_rgba(1, 1, 200, 60, 100, 255));
    top.set_blend_mode(blend.mode);

    const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
    const auto* px = flattened.pixel(0, 0);
    CHECK(px[0] == blend.r);
    CHECK(px[1] == blend.g);
    CHECK(px[2] == blend.b);
  }
}

}  // namespace

std::vector<patchy::test::TestCase> compositor_blend_if_tests() {
  return {
      {"compositor_flattens_visible_layers", compositor_flattens_visible_layers},
      {"compositor_multiply_uses_empty_backdrop_as_transparent",
       compositor_multiply_uses_empty_backdrop_as_transparent},
      {"compositor_applies_extended_blend_modes", compositor_applies_extended_blend_modes},
      {"blend_if_codec_decodes_default_and_identity", blend_if_codec_decodes_default_and_identity},
      {"blend_if_codec_round_trips_unique_rgb_ranges", blend_if_codec_round_trips_unique_rgb_ranges},
      {"blend_if_codec_rejects_unsupported_payloads", blend_if_codec_rejects_unsupported_payloads},
      {"layer_blend_if_setter_tracks_revisions_and_replacement",
       layer_blend_if_setter_tracks_revisions_and_replacement},
      {"blend_if_thresholds_feather_endpoints_and_multiply_channels",
       blend_if_thresholds_feather_endpoints_and_multiply_channels},
      {"compositor_blend_if_scales_this_layer_alpha_and_tests_underlying_coverage",
       compositor_blend_if_scales_this_layer_alpha_and_tests_underlying_coverage},
      {"compositor_blend_if_does_not_gate_layer_effects",
       compositor_blend_if_does_not_gate_layer_effects},
      {"compositor_blend_if_adjustment_tests_adjusted_this_and_original_underlying",
       compositor_blend_if_adjustment_tests_adjusted_this_and_original_underlying},
      {"compositor_blend_if_gates_group_composite",
       compositor_blend_if_gates_group_composite},
      {"compositor_blend_if_clip_base_keeps_original_coverage",
       compositor_blend_if_clip_base_keeps_original_coverage},
      {"compositor_pass_through_group_blend_if_isolates_adjustment_child",
       compositor_pass_through_group_blend_if_isolates_adjustment_child},
  };
}
