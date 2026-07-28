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

// Photoshop clips members to the base layer's TRANSPARENCY alone. The base's
// layer styles still render, but they must not widen the clipping shape: a
// clipped layer never paints where the base itself is absent, only where its
// drop shadow, glow, or stroke landed. Patchy used to freeze the clip from the
// group buffer's accumulated alpha, which by then included that effect output,
// so every clipped member spilled across the base's whole shadow (the Cockpit
// Master title screen: its plane stack is clipped to a frame layer whose mask
// cuts a hole, and the frame's drop shadow relicensed the hole).
void compositor_clip_base_effects_do_not_widen_the_clip_shape() {
  patchy::Document document(8, 1, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(8, 1, 255, 255, 255));

  // Opaque red at x = 1..2, transparent elsewhere.
  patchy::PixelBuffer base_pixels(8, 1, patchy::PixelFormat::rgba8());
  for (std::int32_t x = 0; x < 8; ++x) {
    auto* px = base_pixels.pixel(x, 0);
    px[0] = 255;
    px[1] = 0;
    px[2] = 0;
    px[3] = (x == 1 || x == 2) ? 255 : 0;
  }
  patchy::Layer base(document.allocate_layer_id(), "Shadowed Base", std::move(base_pixels));
  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.blend_mode = patchy::BlendMode::Normal;
  shadow.color = patchy::RgbColor{0, 0, 0};
  shadow.opacity = 1.0F;
  // (180 - angle) puts angle 180 straight to the right; size/spread 0 keeps the
  // shadow a hard 2 px copy at x = 3..4, with no blur fringe to reason about.
  shadow.angle_degrees = 180.0F;
  shadow.distance = 2.0F;
  shadow.size = 0.0F;
  shadow.spread = 0.0F;
  base.layer_style().drop_shadows.push_back(shadow);
  document.add_layer(std::move(base));

  patchy::Layer clipped(document.allocate_layer_id(), "Green Clip", solid_rgba(8, 1, 0, 255, 0, 255));
  clipped.set_clipped(true);
  document.add_layer(std::move(clipped));

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  const auto pixel = [&](std::int32_t x) {
    const auto* px = flattened.pixel(x, 0);
    return std::array<int, 3>{px[0], px[1], px[2]};
  };
  const std::array<int, 3> green{0, 255, 0};
  const std::array<int, 3> black{0, 0, 0};
  const std::array<int, 3> white{255, 255, 255};
  // Inside the base's own matte the clipped green wins (it covers the red).
  CHECK(pixel(1) == green);
  CHECK(pixel(2) == green);
  // The shadow still renders, and stays the shadow: no green leaks onto it.
  CHECK(pixel(3) == black);
  CHECK(pixel(4) == black);
  // Neither the base, its shadow, nor the clipped member reaches the rest.
  CHECK(pixel(0) == white);
  CHECK(pixel(5) == white);
  CHECK(pixel(7) == white);
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

// Pass-through group Opacity is a single post-composite fade toward the
// pre-group backdrop (the PDF non-isolated-group formula): at an overlap the
// upper child fully covers the lower one FIRST, then the whole result fades
// once. Per-child opacity scaling would instead leak the lower child through.
void compositor_pass_through_group_opacity_fades_once_at_overlap() {
  patchy::Document document(3, 1, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Backdrop", solid_rgb(3, 1, 40, 40, 40));

  patchy::Layer group(document.allocate_layer_id(), "Faded", patchy::LayerKind::Group);
  group.set_blend_mode(patchy::BlendMode::PassThrough);
  group.set_opacity(0.5F);
  patchy::Layer red(document.allocate_layer_id(), "Red", solid_rgba(2, 1, 200, 0, 0, 255));
  red.set_bounds(patchy::Rect{0, 0, 2, 1});
  patchy::Layer blue(document.allocate_layer_id(), "Blue", solid_rgba(2, 1, 0, 0, 200, 255));
  blue.set_bounds(patchy::Rect{1, 0, 2, 1});
  group.add_child(std::move(red));
  group.add_child(std::move(blue));
  document.add_layer(std::move(group));

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  CHECK(flattened.pixel(0, 0)[0] == 120);  // red only: lerp(40, 200, 0.5)
  CHECK(flattened.pixel(0, 0)[2] == 20);
  // Overlap: blue covered red at full strength before the fade. Per-child
  // scaling would leave red at 60 here.
  CHECK(flattened.pixel(1, 0)[0] == 20);
  CHECK(flattened.pixel(1, 0)[2] == 120);
  CHECK(flattened.pixel(2, 0)[0] == 20);
  CHECK(flattened.pixel(2, 0)[2] == 120);
}

// A Multiply child inside a faded pass-through group still meets the TRUE
// backdrop (no isolation); the fade interpolates the multiplied result.
void compositor_pass_through_group_opacity_with_multiply_child() {
  const auto render = [](float group_opacity) {
    patchy::Document document(1, 1, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Backdrop", solid_rgb(1, 1, 100, 100, 100));
    patchy::Layer group(document.allocate_layer_id(), "Faded", patchy::LayerKind::Group);
    group.set_blend_mode(patchy::BlendMode::PassThrough);
    group.set_opacity(group_opacity);
    patchy::Layer child(document.allocate_layer_id(), "Multiply", solid_rgba(1, 1, 128, 128, 128, 255));
    child.set_blend_mode(patchy::BlendMode::Multiply);
    group.add_child(std::move(child));
    document.add_layer(std::move(group));
    return patchy::Compositor{}.flatten_rgb8(document);
  };

  const auto full = static_cast<int>(render(1.0F).pixel(0, 0)[0]);
  CHECK(full < 60);  // multiplied against the backdrop, far below either input
  const auto faded = static_cast<int>(render(0.5F).pixel(0, 0)[0]);
  const auto expected = static_cast<int>(std::lround(100.0 * 0.5 + static_cast<double>(full) * 0.5));
  CHECK(faded == expected);  // lerp(backdrop, full-strength result, 0.5)
}

// An interior adjustment keeps reaching the backdrop below the group and its
// effect fades with the group opacity.
void compositor_pass_through_group_opacity_fades_adjustment() {
  patchy::Document document(1, 1, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Backdrop", solid_rgb(1, 1, 100, 100, 100));

  patchy::AdjustmentSettings invert;
  invert.kind = patchy::AdjustmentKind::Invert;
  patchy::Layer adjustment(document.allocate_layer_id(), "Invert", patchy::LayerKind::Adjustment);
  adjustment.set_bounds(patchy::Rect::from_size(1, 1));
  patchy::configure_adjustment_layer(adjustment, invert);

  patchy::Layer group(document.allocate_layer_id(), "Faded", patchy::LayerKind::Group);
  group.set_blend_mode(patchy::BlendMode::PassThrough);
  group.set_opacity(0.5F);
  group.add_child(std::move(adjustment));
  document.add_layer(std::move(group));

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  CHECK(flattened.pixel(0, 0)[0] == 128);  // lerp(100, inverted 155, 0.5)
}

// The group mask attenuates the children in place, then the opacity fade
// applies once on top; fully masked pixels stay untouched.
void compositor_pass_through_group_opacity_respects_group_mask() {
  patchy::Document document(2, 1, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Backdrop", solid_rgb(2, 1, 40, 40, 40));

  patchy::Layer group(document.allocate_layer_id(), "Masked Faded", patchy::LayerKind::Group);
  group.set_blend_mode(patchy::BlendMode::PassThrough);
  group.set_opacity(0.5F);
  group.add_child(
      patchy::Layer(document.allocate_layer_id(), "White", solid_rgba(2, 1, 255, 255, 255, 255)));
  patchy::PixelBuffer mask_pixels(2, 1, patchy::PixelFormat::gray8());
  *mask_pixels.pixel(0, 0) = 255;
  *mask_pixels.pixel(1, 0) = 0;
  group.set_mask(patchy::LayerMask{patchy::Rect{0, 0, 2, 1}, std::move(mask_pixels), 255, false});
  document.add_layer(std::move(group));

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  CHECK(flattened.pixel(0, 0)[0] == 148);  // lerp(40, 255, 0.5) = 147.5 -> 148
  CHECK(flattened.pixel(1, 0)[0] == 40);   // fully masked: untouched
}

// Nested faded pass-through groups snapshot LIFO; the fades compose.
void compositor_nested_pass_through_group_opacities_compose() {
  patchy::Document document(1, 1, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Backdrop", solid_rgb(1, 1, 40, 40, 40));

  patchy::Layer inner(document.allocate_layer_id(), "Inner", patchy::LayerKind::Group);
  inner.set_blend_mode(patchy::BlendMode::PassThrough);
  inner.set_opacity(0.5F);
  inner.add_child(
      patchy::Layer(document.allocate_layer_id(), "White", solid_rgba(1, 1, 255, 255, 255, 255)));

  patchy::Layer outer(document.allocate_layer_id(), "Outer", patchy::LayerKind::Group);
  outer.set_blend_mode(patchy::BlendMode::PassThrough);
  outer.set_opacity(0.5F);
  outer.add_child(std::move(inner));
  document.add_layer(std::move(outer));

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  // Inner fade: lerp(40, 255, 0.5) = 148; outer fade: lerp(40, 148, 0.5) = 94.
  CHECK(flattened.pixel(0, 0)[0] == 94);
}

// A non-pass-through group isolates its children: a Multiply child no longer
// sees the backdrop, and an adjustment-only Normal group is a no-op.
void compositor_normal_group_isolates_children() {
  {
    patchy::Document document(1, 1, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Backdrop", solid_rgb(1, 1, 100, 100, 100));
    patchy::Layer group(document.allocate_layer_id(), "Normal Group", patchy::LayerKind::Group);
    group.set_blend_mode(patchy::BlendMode::Normal);
    patchy::Layer child(document.allocate_layer_id(), "Multiply", solid_rgba(1, 1, 128, 128, 128, 255));
    child.set_blend_mode(patchy::BlendMode::Multiply);
    group.add_child(std::move(child));
    document.add_layer(std::move(group));
    const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
    // Multiply against the isolated transparent buffer keeps the source color;
    // the group then replaces the backdrop in Normal mode.
    CHECK(flattened.pixel(0, 0)[0] == 128);
  }
  {
    patchy::Document document(1, 1, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Backdrop", solid_rgb(1, 1, 100, 100, 100));
    patchy::AdjustmentSettings invert;
    invert.kind = patchy::AdjustmentKind::Invert;
    patchy::Layer adjustment(document.allocate_layer_id(), "Invert", patchy::LayerKind::Adjustment);
    adjustment.set_bounds(patchy::Rect::from_size(1, 1));
    patchy::configure_adjustment_layer(adjustment, invert);
    patchy::Layer group(document.allocate_layer_id(), "Normal Group", patchy::LayerKind::Group);
    group.set_blend_mode(patchy::BlendMode::Normal);
    group.add_child(std::move(adjustment));
    document.add_layer(std::move(group));
    const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
    CHECK(flattened.pixel(0, 0)[0] == 100);  // no source pixels to adjust
  }
}

// A group's blend mode and opacity merge its isolated result exactly like an
// equivalent single layer with that mode and opacity.
void compositor_group_blend_mode_and_opacity_apply() {
  patchy::Document reference(1, 1, patchy::PixelFormat::rgb8());
  reference.add_pixel_layer("Backdrop", solid_rgb(1, 1, 100, 100, 100));
  auto& reference_layer = reference.add_pixel_layer("Multiply", solid_rgba(1, 1, 128, 128, 128, 255));
  reference_layer.set_blend_mode(patchy::BlendMode::Multiply);
  reference_layer.set_opacity(0.5F);
  const auto expected = patchy::Compositor{}.flatten_rgb8(reference).pixel(0, 0)[0];

  patchy::Document document(1, 1, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Backdrop", solid_rgb(1, 1, 100, 100, 100));
  patchy::Layer group(document.allocate_layer_id(), "Multiply Group", patchy::LayerKind::Group);
  group.set_blend_mode(patchy::BlendMode::Multiply);
  group.set_opacity(0.5F);
  group.add_child(
      patchy::Layer(document.allocate_layer_id(), "Gray", solid_rgba(1, 1, 128, 128, 128, 255)));
  document.add_layer(std::move(group));
  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  CHECK(flattened.pixel(0, 0)[0] == expected);
  CHECK(expected < 100);  // sanity: multiply at half strength darkened the backdrop
}

// The reported bug scenario: a faded pass-through group over a TRANSPARENT
// canvas must reduce output coverage, which only the alpha-aware flatten path
// can show (source-over alone cannot reduce alpha).
void compositor_pass_through_group_opacity_fades_over_transparency() {
  patchy::Document document(2, 1, patchy::PixelFormat::rgba8());
  patchy::Layer group(document.allocate_layer_id(), "Faded", patchy::LayerKind::Group);
  group.set_blend_mode(patchy::BlendMode::PassThrough);
  group.set_opacity(0.5F);
  auto child = solid_rgba(2, 1, 255, 0, 0, 255);
  child.pixel(1, 0)[3] = 128;  // second pixel half-covered
  group.add_child(patchy::Layer(document.allocate_layer_id(), "Red", std::move(child)));
  document.add_layer(std::move(group));

  const auto flattened = patchy::flatten_document_rgba8(document);
  CHECK(flattened.pixel(0, 0)[0] == 255);
  CHECK(flattened.pixel(0, 0)[3] == 128);  // opaque child faded to half coverage
  CHECK(flattened.pixel(1, 0)[0] == 255);
  CHECK(flattened.pixel(1, 0)[3] == 64);  // half-covered child faded to a quarter
}

// Photoshop 2026 authored photoshop-group-opacity.psd via COM (July 2026): a
// gray-100 backdrop with four arms — Pass Through groups at 46% holding
// overlapping opaque red/blue rects, a Multiply gray-160 rect, and a masked
// Invert adjustment, plus a Normal group at 60% holding a Multiply gray-160
// rect — saved alongside Photoshop's own flatten. It pins group Opacity
// semantics against ground truth: pass-through groups composite children at
// full strength against the true backdrop and then fade ONCE toward the
// pre-group backdrop (the overlap pixel keeps only the upper child; the
// Multiply child sees the backdrop), while a non-pass-through group isolates
// its children (Multiply against transparency keeps the source color) and
// merges with the group's mode and opacity.
void psd_photoshop_group_opacity_fixture_matches_render() {
  const auto psd_path = patchy::test::committed_psd_fixture_path("photoshop-group-opacity.psd");
  const auto bmp_path = psd_path.parent_path() / "photoshop-group-opacity.bmp";
  CHECK(std::filesystem::exists(psd_path));
  CHECK(std::filesystem::exists(bmp_path));

  const auto document = patchy::psd::DocumentIo::read_file(psd_path);
  const auto reference_flat =
      patchy::Compositor{}.flatten_rgb8(patchy::bmp::DocumentIo::read_file(bmp_path));
  const auto patchy_flat = patchy::Compositor{}.flatten_rgb8(document);

  // Photoshop's own flatten at the probe pixels (COM color samplers):
  // fade-once overlap keeps only the upper blue child, pass-through Multiply
  // lands 83, the masked Invert 125, and the isolated Normal-group Multiply
  // 136 (pass-through math would put it near 78).
  const auto expect_reference = [&](std::int32_t x, std::int32_t y, int red, int green, int blue) {
    const auto* pixel = reference_flat.pixel(x, y);
    CHECK(static_cast<int>(pixel[0]) == red);
    CHECK(static_cast<int>(pixel[1]) == green);
    CHECK(static_cast<int>(pixel[2]) == blue);
  };
  expect_reference(3, 8, 146, 54, 54);
  expect_reference(6, 8, 54, 54, 146);
  expect_reference(17, 8, 83, 83, 83);
  expect_reference(25, 8, 100, 100, 100);
  expect_reference(30, 8, 125, 125, 125);
  expect_reference(41, 8, 136, 136, 136);

  const auto metrics = patchy::test::rgb_diff_metrics(reference_flat, patchy_flat);
  CHECK(metrics.max_channel_delta <= 1);
  CHECK(metrics.mean_abs_channel_delta <= 0.25);
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
      // ColorDodge/ColorBurn re-pinned July 2026 when their kernels moved from floor to
      // Photoshop's nearest rounding (see blend_math_color_burn_dodge_match_photoshop_captures).
      {patchy::BlendMode::ColorDodge, 255, 157, 230},
      {patchy::BlendMode::ColorBurn, 57, 0, 0},
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
      // Dissolve at full coverage never dithers, so it is exactly Normal here.
      {patchy::BlendMode::Dissolve, 200, 60, 100},
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

void compositor_fill_opacity_matches_photoshop_modes() {
  struct Expected {
    patchy::BlendMode mode;
    std::array<std::uint8_t, 3> rgb;
  };
  const std::vector<Expected> expected{
      {patchy::BlendMode::Normal, {120, 80, 150}},
      {patchy::BlendMode::ColorBurn, {13, 3, 153}},
      {patchy::BlendMode::LinearBurn, {12, 2, 112}},
      {patchy::BlendMode::ColorDodge, {66, 113, 235}},
      {patchy::BlendMode::LinearDodge, {140, 130, 240}},
      {patchy::BlendMode::Difference, {60, 70, 120}},
      // The July 2026 light-mode calibration, read off the Fill-50 captures.
      {patchy::BlendMode::VividLight, {56, 44, 178}},
      {patchy::BlendMode::LinearLight, {112, 31, 171}},
      {patchy::BlendMode::HardMix, {26, 6, 225}},
  };
  for (const auto& item : expected) {
    patchy::Document document(1, 1, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Base", solid_rgb(1, 1, 40, 100, 180));
    auto& top = document.add_pixel_layer("Fill", solid_rgba(1, 1, 200, 60, 120, 255));
    top.set_blend_mode(item.mode);
    top.set_fill_opacity(128.0F / 255.0F);
    const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
    const auto* pixel = flattened.pixel(0, 0);
    CHECK(pixel[0] == item.rgb[0]);
    CHECK(pixel[1] == item.rgb[1]);
    CHECK(pixel[2] == item.rgb[2]);
  }
}

// The July 2026 special-Fill calibration for Vivid Light, Linear Light and
// Hard Mix, pinned against 256x256 Photoshop 2026 flatten captures of crossed
// gray ramps at Fill 1 through 99 percent (fill_byte = lround(fill * 255)).
// The quads sample both kernel halves, every clamp edge, and the points that
// discriminated the calibrated rounding during fitting; blend_math.cpp
// documents the kernels. Fill 0 is identity (Photoshop skips the layer).
void blend_math_light_modes_special_fill_match_photoshop_captures() {
  struct Quad {
    int source;
    int destination;
    int fill_byte;
    int expected;
  };
  static constexpr Quad kLinearLight[] = {
      {0, 64, 26, 37}, {32, 64, 26, 44}, {128, 128, 26, 127},
      {192, 0, 26, 12}, {255, 128, 26, 153}, {128, 64, 64, 63},
      {128, 200, 125, 199}, {0, 200, 128, 71}, {32, 128, 128, 31},
      {128, 0, 128, 0}, {128, 128, 128, 128}, {192, 0, 128, 64},
      {255, 64, 128, 191}, {192, 0, 191, 96}, {255, 64, 191, 254},
      {64, 128, 230, 12}, {255, 0, 230, 229}, {0, 128, 3, 124},
      {255, 128, 3, 130}, {0, 254, 252, 1}, {255, 1, 252, 252},
  };
  static constexpr Quad kVividLight[] = {
      {136, 64, 26, 65}, {135, 64, 26, 64}, {130, 192, 26, 192},
      {131, 192, 26, 193}, {129, 64, 64, 64}, {131, 64, 64, 65},
      {0, 129, 128, 2}, {0, 192, 128, 129}, {32, 128, 128, 51},
      {96, 64, 128, 37}, {128, 64, 128, 64}, {160, 64, 128, 73},
      {160, 128, 128, 146}, {192, 128, 128, 172}, {255, 64, 128, 129},
      {129, 192, 191, 193}, {1, 254, 191, 251}, {64, 128, 230, 24},
      {255, 32, 230, 255}, {0, 128, 3, 126}, {255, 128, 3, 130},
      {127, 254, 252, 254}, {128, 1, 252, 1},
  };
  static constexpr Quad kHardMix[] = {
      {0, 32, 26, 7}, {128, 128, 26, 128}, {255, 224, 26, 249},
      {0, 100, 64, 48}, {127, 64, 64, 43}, {255, 32, 64, 43},
      {0, 128, 125, 6}, {127, 128, 125, 128}, {128, 128, 125, 129},
      {255, 128, 125, 251}, {0, 128, 128, 2}, {127, 64, 128, 0},
      {128, 64, 128, 2}, {192, 32, 128, 2}, {255, 64, 128, 128},
      {128, 128, 130, 130}, {128, 128, 153, 129}, {1, 190, 191, 4},
      {254, 2, 230, 10}, {0, 128, 3, 126}, {255, 128, 3, 130},
      {100, 154, 252, 64}, {100, 156, 252, 191},
  };
  const auto gray = [](int value) {
    return std::array<std::uint8_t, 3>{static_cast<std::uint8_t>(value),
                                       static_cast<std::uint8_t>(value),
                                       static_cast<std::uint8_t>(value)};
  };
  const auto run = [&](patchy::BlendMode mode, const Quad& quad) {
    // Full coverage over an opaque backdrop isolates the blend kernel: the
    // composite result IS the kernel output.
    const auto result = patchy::composite_special_fill_rgb(
        gray(quad.source), gray(quad.destination), mode, 1.0F,
        static_cast<float>(quad.fill_byte) / 255.0F, 1.0F, 1.0F);
    CHECK(result.color[0] == quad.expected);
    CHECK(result.color[1] == quad.expected);
    CHECK(result.color[2] == quad.expected);
  };
  for (const auto& quad : kLinearLight) {
    run(patchy::BlendMode::LinearLight, quad);
  }
  for (const auto& quad : kVividLight) {
    run(patchy::BlendMode::VividLight, quad);
  }
  for (const auto& quad : kHardMix) {
    run(patchy::BlendMode::HardMix, quad);
  }
  // Fill 0 is identity for all three (blend_mode_has_special_fill routes them
  // here even at Fill 0, so the kernels must not shift the backdrop).
  for (const auto mode : {patchy::BlendMode::VividLight, patchy::BlendMode::LinearLight,
                          patchy::BlendMode::HardMix}) {
    const auto identity =
        patchy::composite_special_fill_rgb(gray(200), gray(77), mode, 1.0F, 0.0F, 1.0F, 1.0F);
    CHECK(identity.color[0] == 77);
  }
}

// Dissolve is the one blend mode that is not a colour function: coverage
// becomes the probability that a pixel is painted at all, and the threshold is
// a deterministic function of the document coordinate. Patchy's field is its
// own, not a reconstruction of Photoshop's, so what is pinned here is the
// behaviour the renderer depends on, not Photoshop's exact pixels.
void blend_dissolve_coverage_is_deterministic_and_uniform() {
  // The two ends are exact and out-of-range inputs clamp: a fully opaque
  // Dissolve layer must be byte-identical to Normal, never speckled.
  for (std::int32_t y = 0; y < 8; ++y) {
    for (std::int32_t x = 0; x < 8; ++x) {
      CHECK(patchy::dissolve_coverage(x, y, 0.0F) == 0.0F);
      CHECK(patchy::dissolve_coverage(x, y, 1.0F) == 1.0F);
      CHECK(patchy::dissolve_coverage(x, y, -1.0F) == 0.0F);
      CHECK(patchy::dissolve_coverage(x, y, 2.0F) == 1.0F);
    }
  }

  // Every partial coverage resolves to exactly 0 or 1, and repeats. Repeating
  // is the load-bearing property: a dirty-rect repaint re-evaluates the same
  // pixels and must reach the same answer.
  int painted = 0;
  for (std::int32_t y = 0; y < 256; ++y) {
    for (std::int32_t x = 0; x < 256; ++x) {
      const auto first = patchy::dissolve_coverage(x, y, 0.5F);
      CHECK(first == 0.0F || first == 1.0F);
      CHECK(patchy::dissolve_coverage(x, y, 0.5F) == first);
      painted += first > 0.0F ? 1 : 0;
    }
  }
  // 65536 samples at p = 0.5. Deliberately a loose window rather than a pinned
  // count: the point is that the field is uniform, not which hash produced it.
  CHECK(painted > 32000);
  CHECK(painted < 33500);

  // Coverage is monotone in alpha, so raising Opacity only ever adds pixels
  // instead of reshuffling the pattern.
  for (std::int32_t y = 0; y < 64; ++y) {
    for (std::int32_t x = 0; x < 64; ++x) {
      if (patchy::dissolve_coverage(x, y, 0.25F) > 0.0F) {
        CHECK(patchy::dissolve_coverage(x, y, 0.75F) > 0.0F);
      }
    }
  }

  // Separate fields decorrelate, so a dissolved drop shadow and a dissolved
  // outer glow on one layer do not dither onto identical pixels.
  int agreements = 0;
  for (std::int32_t y = 0; y < 128; ++y) {
    for (std::int32_t x = 0; x < 128; ++x) {
      const auto layer = patchy::dissolve_coverage(x, y, 0.5F, patchy::DissolveField::Layer);
      const auto glow = patchy::dissolve_coverage(x, y, 0.5F, patchy::DissolveField::OuterGlow);
      agreements += layer == glow ? 1 : 0;
    }
  }
  // Two independent fields agree on about half of the 16384 pixels; two
  // identical ones would agree on all of them.
  CHECK(agreements > 7500);
  CHECK(agreements < 9000);
}

// The field is anchored to the DOCUMENT coordinate, which is what lets the
// dirty-rect patch machinery and the strip-parallel renderer reproduce the same
// pattern that a full flatten produces. Rendering the same content at the same
// document position on two different canvas sizes must agree pixel for pixel.
void compositor_dissolve_is_anchored_to_document_coordinates() {
  const auto render = [](std::int32_t canvas) {
    patchy::Document document(canvas, canvas, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Base", solid_rgb(canvas, canvas, 0, 0, 0));
    auto& top = document.add_pixel_layer("Dissolve", solid_rgba(canvas, canvas, 255, 255, 255, 255));
    top.set_blend_mode(patchy::BlendMode::Dissolve);
    top.set_opacity(0.5F);
    return patchy::Compositor{}.flatten_rgb8(document);
  };
  // Not named "small": <rpcndr.h> defines that as a macro for char on Windows.
  const auto narrow_canvas = render(48);
  const auto wide_canvas = render(96);
  int painted = 0;
  for (std::int32_t y = 0; y < 48; ++y) {
    for (std::int32_t x = 0; x < 48; ++x) {
      const auto* a = narrow_canvas.pixel(x, y);
      const auto* b = wide_canvas.pixel(x, y);
      CHECK(a[0] == b[0]);
      CHECK(a[1] == b[1]);
      CHECK(a[2] == b[2]);
      // All or nothing: a dissolved pixel is never a blended grey.
      CHECK(a[0] == 0 || a[0] == 255);
      painted += a[0] == 255 ? 1 : 0;
    }
  }
  CHECK(painted > 1000);
  CHECK(painted < 1300);
}

// Fill and Opacity are not special-cased for Dissolve (it is not one of
// Photoshop's eight special-Fill modes), so both simply compound into the paint
// probability. Fill 50% x Opacity 50% therefore paints about a quarter of the
// pixels, and Fill 0% paints none.
void compositor_dissolve_compounds_fill_and_opacity() {
  const auto painted_fraction = [](float opacity, float fill) {
    patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Base", solid_rgb(64, 64, 0, 0, 0));
    auto& top = document.add_pixel_layer("Dissolve", solid_rgba(64, 64, 255, 255, 255, 255));
    top.set_blend_mode(patchy::BlendMode::Dissolve);
    top.set_opacity(opacity);
    top.set_fill_opacity(fill);
    const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
    int painted = 0;
    for (std::int32_t y = 0; y < 64; ++y) {
      for (std::int32_t x = 0; x < 64; ++x) {
        painted += flattened.pixel(x, y)[0] == 255 ? 1 : 0;
      }
    }
    return painted;
  };
  CHECK(painted_fraction(1.0F, 0.0F) == 0);
  CHECK(painted_fraction(0.0F, 1.0F) == 0);
  CHECK(painted_fraction(1.0F, 1.0F) == 64 * 64);
  const auto quarter = painted_fraction(0.5F, 0.5F);
  CHECK(quarter > 900);
  CHECK(quarter < 1150);
}

// Layer-style effects dissolve too, each on its own field. The probe is a hard
// 2 px drop shadow (no blur fringe to reason about) at 50% opacity: every
// shadow pixel is either the full shadow colour or the untouched backdrop, and
// the run is a mix of both.
void compositor_dissolve_dithers_layer_effects() {
  patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(64, 64, 255, 255, 255));

  patchy::PixelBuffer base_pixels(64, 64, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < 64; ++y) {
    for (std::int32_t x = 0; x < 64; ++x) {
      auto* px = base_pixels.pixel(x, y);
      px[0] = 255;
      px[1] = 0;
      px[2] = 0;
      px[3] = x < 32 ? 255 : 0;
    }
  }
  patchy::Layer base(document.allocate_layer_id(), "Shadowed", std::move(base_pixels));
  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.blend_mode = patchy::BlendMode::Dissolve;
  shadow.color = patchy::RgbColor{0, 0, 0};
  shadow.opacity = 0.5F;
  shadow.angle_degrees = 180.0F;  // straight to the right
  shadow.distance = 8.0F;
  shadow.size = 0.0F;
  shadow.spread = 0.0F;
  base.layer_style().drop_shadows.push_back(shadow);
  document.add_layer(std::move(base));

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  int shadowed = 0;
  int clear = 0;
  // x = 32..39 is the shadow band the layer itself does not cover.
  for (std::int32_t y = 0; y < 64; ++y) {
    for (std::int32_t x = 32; x < 40; ++x) {
      const auto* px = flattened.pixel(x, y);
      const auto black = px[0] == 0 && px[1] == 0 && px[2] == 0;
      const auto white = px[0] == 255 && px[1] == 255 && px[2] == 255;
      CHECK(black || white);  // never a 50% grey
      shadowed += black ? 1 : 0;
      clear += white ? 1 : 0;
    }
  }
  CHECK(shadowed + clear == 64 * 8);
  CHECK(shadowed > 200);
  CHECK(clear > 200);
}

}  // namespace

// The July 2026 blend modes (Vivid/Linear Light, Hard Mix, Darker/Lighter
// Color), pinned against full 256x256 Photoshop 2026 flatten captures of
// crossed gray gradients (source = row value, destination = column value). The
// triples below sample that ground truth across both kernel halves and every
// clamp edge; blend_math.cpp documents the calibrated rounding.
void blend_math_new_modes_match_photoshop_captures() {
  struct Triple {
    int source;
    int destination;
    int expected;
  };
  static constexpr Triple kVividLight[] = {
      {0, 0, 0}, {0, 255, 0}, {1, 200, 0}, {1, 254, 127}, {1, 255, 255},
      {2, 254, 191}, {2, 255, 255}, {37, 200, 65}, {37, 254, 252},
      {64, 128, 2}, {64, 200, 145}, {64, 254, 253},
      {65, 128, 4}, {65, 200, 146}, {65, 254, 253},
      {100, 63, 9}, {100, 126, 90}, {100, 128, 92}, {100, 200, 185},
      {127, 63, 61}, {127, 126, 125}, {127, 128, 127}, {127, 200, 200},
      {128, 1, 1}, {128, 63, 63}, {128, 128, 128}, {128, 254, 254},
      {129, 126, 127}, {129, 128, 129}, {129, 200, 202}, {129, 254, 255},
      {170, 63, 94}, {170, 126, 188}, {170, 128, 191}, {170, 200, 255},
      {192, 1, 2}, {192, 63, 128}, {192, 126, 255},
      {254, 0, 0}, {254, 1, 128}, {254, 63, 255},
      {255, 0, 255}, {255, 255, 255},
  };
  static constexpr Triple kLinearLight[] = {
      {0, 255, 0}, {1, 254, 0}, {1, 255, 1}, {2, 254, 2}, {2, 255, 3},
      {37, 200, 18}, {64, 200, 72}, {64, 254, 126},
      {65, 128, 2}, {65, 254, 128}, {100, 126, 70}, {100, 200, 144},
      {127, 63, 61}, {127, 200, 198}, {127, 255, 253},
      {128, 0, 0}, {128, 128, 128}, {128, 255, 255},
      {129, 0, 2}, {129, 126, 128}, {170, 0, 84}, {170, 128, 212},
      {192, 0, 128}, {192, 126, 254}, {254, 0, 252}, {255, 0, 254}, {255, 1, 255},
  };
  static constexpr Triple kHardMix[] = {
      {0, 254, 0}, {0, 255, 255}, {1, 254, 255}, {37, 200, 0}, {37, 254, 255},
      {64, 128, 0}, {64, 200, 255}, {127, 126, 0}, {127, 128, 255},
      {128, 126, 0}, {128, 128, 255}, {170, 63, 0}, {170, 126, 255},
      {254, 1, 0}, {254, 63, 255}, {255, 0, 0}, {255, 1, 255},
  };
  const auto gray = [](int value) {
    return std::array<std::uint8_t, 3>{static_cast<std::uint8_t>(value),
                                       static_cast<std::uint8_t>(value),
                                       static_cast<std::uint8_t>(value)};
  };
  for (const auto& t : kVividLight) {
    CHECK(patchy::blend_rgb(gray(t.source), gray(t.destination),
                            patchy::BlendMode::VividLight)[0] == t.expected);
  }
  for (const auto& t : kLinearLight) {
    CHECK(patchy::blend_rgb(gray(t.source), gray(t.destination),
                            patchy::BlendMode::LinearLight)[0] == t.expected);
  }
  for (const auto& t : kHardMix) {
    CHECK(patchy::blend_rgb(gray(t.source), gray(t.destination),
                            patchy::BlendMode::HardMix)[0] == t.expected);
  }

  // Darker/Lighter Color pick whole colors by rounded 0.3/0.59/0.11 luma
  // (gray inputs = plain min/max; ties keep the destination).
  const std::array<std::uint8_t, 3> reddish{200, 60, 40};    // luma 97
  const std::array<std::uint8_t, 3> greenish{40, 160, 40};   // luma 111
  CHECK(patchy::blend_rgb(reddish, greenish, patchy::BlendMode::DarkerColor) == reddish);
  CHECK(patchy::blend_rgb(reddish, greenish, patchy::BlendMode::LighterColor) == greenish);
  CHECK(patchy::blend_rgb(gray(90), gray(64), patchy::BlendMode::DarkerColor) == gray(64));
  CHECK(patchy::blend_rgb(gray(90), gray(64), patchy::BlendMode::LighterColor) == gray(90));
  // Equal rounded luma (100 both): the destination wins either way.
  const std::array<std::uint8_t, 3> tie_source{100, 100, 100};
  const std::array<std::uint8_t, 3> tie_destination{150, 84, 50};  // 30*150+59*84+11*50 = 10006
  CHECK(patchy::blend_rgb(tie_source, tie_destination, patchy::BlendMode::DarkerColor) ==
        tie_destination);
  CHECK(patchy::blend_rgb(tie_source, tie_destination, patchy::BlendMode::LighterColor) ==
        tie_destination);
}

// Color Burn and Color Dodge, re-calibrated July 2026 against the same full
// 256x256 Photoshop 2026 flatten captures: the quotient rounds to NEAREST
// (half up) and the 0/0 corner follows the destination (Burn: d=255 -> 255
// even at s=0; Dodge: d=0 -> 0 even at s=255). The triples sample corners,
// clamp edges, an exact-half case, and entries where the old floor kernels
// were off by one.
void blend_math_color_burn_dodge_match_photoshop_captures() {
  struct Triple {
    int source;
    int destination;
    int expected;
  };
  static constexpr Triple kColorBurn[] = {
      {0, 0, 0}, {0, 254, 0}, {0, 255, 255}, {1, 255, 255},
      {255, 0, 0}, {255, 1, 1}, {254, 0, 0}, {255, 255, 255},
      {1, 254, 0}, {37, 200, 0}, {64, 128, 0}, {128, 128, 2},
      {128, 254, 253}, {200, 100, 57}, {254, 254, 254},
  };
  static constexpr Triple kColorDodge[] = {
      {0, 0, 0}, {0, 254, 254}, {0, 255, 255}, {255, 0, 0},
      {255, 1, 255}, {254, 0, 0}, {1, 1, 1}, {1, 254, 255},
      {37, 200, 234}, {64, 128, 171}, {100, 63, 104}, {60, 120, 157},
      {100, 140, 230}, {128, 128, 255}, {254, 254, 255},
  };
  const auto gray = [](int value) {
    return std::array<std::uint8_t, 3>{static_cast<std::uint8_t>(value),
                                       static_cast<std::uint8_t>(value),
                                       static_cast<std::uint8_t>(value)};
  };
  for (const auto& t : kColorBurn) {
    CHECK(patchy::blend_rgb(gray(t.source), gray(t.destination),
                            patchy::BlendMode::ColorBurn)[0] == t.expected);
  }
  for (const auto& t : kColorDodge) {
    CHECK(patchy::blend_rgb(gray(t.source), gray(t.destination),
                            patchy::BlendMode::ColorDodge)[0] == t.expected);
  }
}

std::vector<patchy::test::TestCase> compositor_blend_if_tests() {
  return {
      {"blend_math_new_modes_match_photoshop_captures",
       blend_math_new_modes_match_photoshop_captures},
      {"blend_math_color_burn_dodge_match_photoshop_captures",
       blend_math_color_burn_dodge_match_photoshop_captures},
      {"blend_math_light_modes_special_fill_match_photoshop_captures",
       blend_math_light_modes_special_fill_match_photoshop_captures},
      {"compositor_flattens_visible_layers", compositor_flattens_visible_layers},
      {"compositor_multiply_uses_empty_backdrop_as_transparent",
       compositor_multiply_uses_empty_backdrop_as_transparent},
      {"compositor_applies_extended_blend_modes", compositor_applies_extended_blend_modes},
      {"compositor_fill_opacity_matches_photoshop_modes",
       compositor_fill_opacity_matches_photoshop_modes},
      {"blend_dissolve_coverage_is_deterministic_and_uniform",
       blend_dissolve_coverage_is_deterministic_and_uniform},
      {"compositor_dissolve_is_anchored_to_document_coordinates",
       compositor_dissolve_is_anchored_to_document_coordinates},
      {"compositor_dissolve_compounds_fill_and_opacity",
       compositor_dissolve_compounds_fill_and_opacity},
      {"compositor_dissolve_dithers_layer_effects",
       compositor_dissolve_dithers_layer_effects},
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
      {"compositor_clip_base_effects_do_not_widen_the_clip_shape",
       compositor_clip_base_effects_do_not_widen_the_clip_shape},
      {"compositor_pass_through_group_blend_if_isolates_adjustment_child",
       compositor_pass_through_group_blend_if_isolates_adjustment_child},
      {"compositor_pass_through_group_opacity_fades_once_at_overlap",
       compositor_pass_through_group_opacity_fades_once_at_overlap},
      {"compositor_pass_through_group_opacity_with_multiply_child",
       compositor_pass_through_group_opacity_with_multiply_child},
      {"compositor_pass_through_group_opacity_fades_adjustment",
       compositor_pass_through_group_opacity_fades_adjustment},
      {"compositor_pass_through_group_opacity_respects_group_mask",
       compositor_pass_through_group_opacity_respects_group_mask},
      {"compositor_nested_pass_through_group_opacities_compose",
       compositor_nested_pass_through_group_opacities_compose},
      {"compositor_normal_group_isolates_children",
       compositor_normal_group_isolates_children},
      {"compositor_group_blend_mode_and_opacity_apply",
       compositor_group_blend_mode_and_opacity_apply},
      {"compositor_pass_through_group_opacity_fades_over_transparency",
       compositor_pass_through_group_opacity_fades_over_transparency},
      {"psd_photoshop_group_opacity_fixture_matches_render",
       psd_photoshop_group_opacity_fixture_matches_render},
  };
}
