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

patchy::ScaledBrushTip make_solid_scaled_tip(std::int32_t size) {
  patchy::BrushTip tip;
  tip.width = size;
  tip.height = size;
  tip.mask.assign(static_cast<std::size_t>(size) * static_cast<std::size_t>(size), 255U);
  return patchy::make_scaled_brush_tip(patchy::build_brush_tip_mips(tip), size);
}

std::vector<std::uint8_t> render_effect_dab(const patchy::ScaledBrushTip& tip,
                                            const patchy::BrushDynamics& dynamics,
                                            patchy::EditColor primary = {220, 20, 40, 255},
                                            patchy::EditColor secondary = {255, 255, 255, 255}) {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(primary.r, primary.g, primary.b);
  options.primary = primary;
  options.secondary = secondary;
  options.brush_size = tip.width;
  options.brush_tip = &tip;
  options.brush_dynamics = dynamics;
  patchy::BrushTipStrokeState state;
  CHECK(!patchy::paint_brush_segment(document, layer_id, 24.0, 24.0, 24.0, 24.0, options,
                                     false, state)
             .empty());
  const auto data = std::as_const(*document.find_layer(layer_id)).pixels().data();
  return {data.begin(), data.end()};
}

void tool_brush_draws_color_and_writes_artifact() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  const auto dirty = patchy::paint_brush(document, layer_id, 20, 20, tool_options(12, 140, 240), false);
  CHECK(!dirty.empty());
  const auto* px = document.find_layer(layer_id)->pixels().pixel(20, 20);
  CHECK(px[0] == 12);
  CHECK(px[1] == 140);
  CHECK(px[2] == 240);
  CHECK(px[3] == 255);
  write_bmp_artifact("tool_brush", document);
}

void tool_brush_opacity_and_bounded_layer_expansion_work() {
  patchy::Document document(64, 48, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(64, 48, 255, 255, 255));
  patchy::Layer pasted(document.allocate_layer_id(), "Pasted", solid_rgba(8, 8, 0, 0, 0, 0));
  pasted.pixels().pixel(0, 0)[0] = 30;
  pasted.pixels().pixel(0, 0)[1] = 40;
  pasted.pixels().pixel(0, 0)[2] = 50;
  pasted.pixels().pixel(0, 0)[3] = 255;
  pasted.set_bounds(patchy::Rect{10, 10, 8, 8});
  const auto layer_id = pasted.id();
  document.add_layer(std::move(pasted));

  auto options = tool_options(240, 20, 30);
  options.brush_size = 5;
  options.primary.a = 128;
  const auto dirty = patchy::paint_brush(document, layer_id, 44, 32, options, false);
  CHECK(!dirty.empty());
  const auto* layer = document.find_layer(layer_id);
  CHECK(layer != nullptr);
  CHECK(layer->bounds().contains(44, 32));
  CHECK(layer->bounds().contains(10, 10));
  CHECK(layer->pixels().pixel(10 - layer->bounds().x, 10 - layer->bounds().y)[3] == 255);
  const auto* painted = layer->pixels().pixel(44 - layer->bounds().x, 32 - layer->bounds().y);
  CHECK(painted[0] == 240);
  CHECK(painted[1] == 20);
  CHECK(painted[2] == 30);
  CHECK(painted[3] >= 120);
  CHECK(painted[3] <= 136);
  write_bmp_artifact("tool_brush_expand_layer", document);
}

void tool_brush_softness_feathers_edge_alpha() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(30, 90, 240);
  options.brush_size = 21;
  options.brush_softness = 100;

  const auto dirty = patchy::paint_brush(document, layer_id, 24, 24, options, false);
  CHECK(!dirty.empty());
  const auto* layer = document.find_layer(layer_id);
  CHECK(layer != nullptr);
  const auto& pixels = layer->pixels();
  const auto* center = pixels.pixel(24, 24);
  const auto* feather = pixels.pixel(32, 24);
  const auto* outside = pixels.pixel(35, 24);
  CHECK(center[0] == 30);
  CHECK(center[1] == 90);
  CHECK(center[2] == 240);
  CHECK(center[3] == 255);
  CHECK(feather[0] == 30);
  CHECK(feather[1] == 90);
  CHECK(feather[2] == 240);
  CHECK(feather[3] > 0);
  CHECK(feather[3] < 255);
  CHECK(outside[3] == 0);
  write_bmp_artifact("tool_soft_brush", document);
}

void tool_brush_repaints_translucent_pixels_without_color_halo() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);

  auto black = tool_options(0, 0, 0);
  black.brush_size = 1;
  black.primary.a = 128;
  CHECK(!patchy::paint_brush(document, layer_id, 20, 20, black, false).empty());

  auto red = tool_options(255, 0, 0);
  red.brush_size = 1;
  red.primary.a = 128;
  CHECK(!patchy::paint_brush(document, layer_id, 20, 20, red, false).empty());

  const auto* px = document.find_layer(layer_id)->pixels().pixel(20, 20);
  CHECK(px[0] >= 165);
  CHECK(px[0] <= 176);
  CHECK(px[1] == 0);
  CHECK(px[2] == 0);
  CHECK(px[3] >= 190);
  CHECK(px[3] <= 194);
}

void brush_tip_mips_and_scaling_preserve_shape() {
  patchy::BrushTip tip;
  tip.width = 64;
  tip.height = 32;
  tip.mask.assign(static_cast<std::size_t>(64 * 32), 0);
  for (std::int32_t y = 8; y < 24; ++y) {
    for (std::int32_t x = 16; x < 48; ++x) {
      tip.mask[static_cast<std::size_t>(y) * 64U + static_cast<std::size_t>(x)] = 255;
    }
  }

  const auto mips = patchy::build_brush_tip_mips(tip);
  CHECK(!mips.empty());
  CHECK(mips.levels.size() >= 5);
  CHECK(mips.levels[1].width == 32);
  CHECK(mips.levels[1].height == 16);

  const auto scaled = patchy::make_scaled_brush_tip(mips, 16);
  CHECK(scaled.width == 16);
  CHECK(scaled.height == 8);
  CHECK(scaled.anchor_x == 8.0);
  CHECK(scaled.anchor_y == 4.0);
  // Center of the inner rectangle stays opaque; far corners stay empty.
  CHECK(scaled.mask[4U * 16U + 8U] == 255);
  CHECK(scaled.mask[0] == 0);
  CHECK(scaled.mask[scaled.mask.size() - 1U] == 0);

  const auto upscaled = patchy::make_scaled_brush_tip(mips, 128);
  CHECK(upscaled.width == 128);
  CHECK(upscaled.height == 64);
  CHECK(upscaled.mask[32U * 128U + 64U] == 255);
}

void tool_brush_tip_stamps_bitmap_mask() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);

  const auto tip = make_bar_brush_tip();
  const auto mips = patchy::build_brush_tip_mips(tip);
  const auto scaled = patchy::make_scaled_brush_tip(mips, 9);

  auto options = tool_options(10, 200, 60);
  options.brush_size = 9;
  options.brush_tip = &scaled;
  const auto dirty = patchy::paint_brush(document, layer_id, 24, 24, options, false);
  CHECK(!dirty.empty());

  const auto& pixels = document.find_layer(layer_id)->pixels();
  CHECK(pixels.pixel(24, 24)[3] == 255);  // bar center
  CHECK(pixels.pixel(28, 24)[3] == 255);  // bar end
  CHECK(pixels.pixel(24, 28)[3] == 0);    // above/below the bar stays empty
  CHECK(pixels.pixel(24, 22)[3] == 0);
  CHECK(pixels.pixel(24, 24)[0] == 10);
  CHECK(pixels.pixel(24, 24)[1] == 200);
  CHECK(pixels.pixel(24, 24)[2] == 60);
}

void tool_brush_tip_rotation_and_roundness_transform_stamp() {
  const auto tip = make_bar_brush_tip();
  const auto mips = patchy::build_brush_tip_mips(tip);
  const auto scaled = patchy::make_scaled_brush_tip(mips, 9);

  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(0, 0, 0);
  options.brush_size = 9;
  options.brush_tip = &scaled;
  options.brush_angle_degrees = 90.0;
  CHECK(!patchy::paint_brush(document, layer_id, 24, 24, options, false).empty());
  const auto& rotated = document.find_layer(layer_id)->pixels();
  CHECK(rotated.pixel(24, 28)[3] == 255);  // bar now vertical
  CHECK(rotated.pixel(28, 24)[3] == 0);

  auto squashed_document = make_tool_document();
  const auto squashed_layer = active_tool_layer(squashed_document);
  auto squash_options = tool_options(0, 0, 0);
  squash_options.brush_size = 9;
  squash_options.brush_tip = &scaled;
  squash_options.brush_roundness = 50;  // halves the tip height; the bar itself stays a bar
  CHECK(!patchy::paint_brush(squashed_document, squashed_layer, 24, 24, squash_options, false).empty());
  const auto& squashed = squashed_document.find_layer(squashed_layer)->pixels();
  CHECK(squashed.pixel(24, 24)[3] == 255);
  CHECK(squashed.pixel(28, 24)[3] == 255);
  CHECK(squashed.pixel(24, 27)[3] == 0);
}

void tool_brush_tip_segment_spacing_carries_across_segments() {
  const auto square = [] {
    patchy::BrushTip tip;
    tip.width = 2;
    tip.height = 2;
    tip.mask.assign(4, 255);
    return tip;
  }();
  const auto mips = patchy::build_brush_tip_mips(square);
  const auto scaled = patchy::make_scaled_brush_tip(mips, 4);

  auto options = tool_options(0, 0, 0);
  options.brush_size = 4;
  options.brush_tip = &scaled;
  options.brush_tip_spacing = 2.0;  // dabs every 8px: sparse enough to count

  const auto painted_alpha_pixels = [](const patchy::Document& document, patchy::LayerId layer_id) {
    const auto& pixels = document.find_layer(layer_id)->pixels();
    int count = 0;
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        if (pixels.pixel(x, y)[3] > 0U) {
          ++count;
        }
      }
    }
    return count;
  };

  // One long segment...
  auto whole_document = make_tool_document();
  const auto whole_layer = active_tool_layer(whole_document);
  patchy::BrushTipStrokeState whole_state;
  CHECK(!patchy::paint_brush_segment(whole_document, whole_layer, 10.0, 20.0, 25.0, 20.0, options, false,
                                     whole_state)
             .empty());

  // ...must paint the same pixels as the same path chopped into short segments (the canvas
  // stroke smoother emits steps shorter than the dab spacing).
  auto chopped_document = make_tool_document();
  const auto chopped_layer = active_tool_layer(chopped_document);
  patchy::BrushTipStrokeState chopped_state;
  auto chopped_dirty = patchy::paint_brush_segment(chopped_document, chopped_layer, 10.0, 20.0, 15.0, 20.0,
                                                   options, false, chopped_state);
  chopped_dirty = patchy::unite_rect(
      chopped_dirty, patchy::paint_brush_segment(chopped_document, chopped_layer, 15.0, 20.0, 20.0, 20.0, options,
                                                 false, chopped_state));
  chopped_dirty = patchy::unite_rect(
      chopped_dirty, patchy::paint_brush_segment(chopped_document, chopped_layer, 20.0, 20.0, 25.0, 20.0, options,
                                                 false, chopped_state));
  CHECK(!chopped_dirty.empty());

  const auto whole_count = painted_alpha_pixels(whole_document, whole_layer);
  const auto chopped_count = painted_alpha_pixels(chopped_document, chopped_layer);
  CHECK(whole_count == chopped_count);
  // 15px path, spacing 8: a dab at the start plus one 8px in. Each 4x4 stamp antialiases into a
  // 5x5 footprint, and the two footprints are disjoint.
  CHECK(whole_count == 50);
  const auto& whole_pixels = whole_document.find_layer(whole_layer)->pixels();
  const auto& chopped_pixels = chopped_document.find_layer(chopped_layer)->pixels();
  for (std::int32_t y = 16; y < 24; ++y) {
    for (std::int32_t x = 6; x < 30; ++x) {
      CHECK(whole_pixels.pixel(x, y)[3] == chopped_pixels.pixel(x, y)[3]);
    }
  }
}

void brush_dynamics_rng_sequence_is_stable() {
  // Freezes the splitmix64 stream: dynamics tests assert exact pixels, so the generator must
  // never change behavior (and std::uniform_* distributions must never sneak in).
  patchy::BrushDynamicsRng rng;
  rng.seed(42);
  CHECK(rng.next_u64() == 18047550643177690414ULL);
  CHECK(rng.next_u64() == 13520925650152286267ULL);
  CHECK(rng.next_u64() == 11490439774882940890ULL);
  CHECK(rng.next_u64() == 11819688046890619624ULL);

  rng.seed(42);
  CHECK(rng.next_unit() == 0.97835968076877067);
  CHECK(rng.next_unit() == 0.73297084819550451);

  rng.seed(0);
  CHECK(rng.next_u64() == 7960286522194355700ULL);

  rng.seed(7);
  for (int i = 0; i < 100; ++i) {
    const auto value = rng.next_signed_unit();
    CHECK(value >= -1.0);
    CHECK(value < 1.0);
  }

  rng.seed(3);
  bool saw_true = false;
  bool saw_false = false;
  for (int i = 0; i < 64 && !(saw_true && saw_false); ++i) {
    if (rng.next_bool()) {
      saw_true = true;
    } else {
      saw_false = true;
    }
  }
  CHECK(saw_true);
  CHECK(saw_false);
}

void brush_dynamics_activation_gates_fields() {
  patchy::BrushDynamics dynamics;
  CHECK(!dynamics.active());

  // Floors, axis flags, fade length, and the per-stroke inputs never activate on their own.
  dynamics.minimum_diameter = 0.5;
  dynamics.minimum_roundness = 0.5;
  dynamics.scatter_both_axes = true;
  dynamics.count_jitter = 1.0;
  dynamics.angle_fade_steps = 5;
  dynamics.seed = 77;
  dynamics.pen_pressure = 0.5;
  dynamics.pen_tilt = 0.25;
  dynamics.pen_wheel = 0.75;
  dynamics.pen_tilt_azimuth_degrees = 45.0;
  dynamics.pen_tilt_azimuth_valid = true;
  dynamics.pen_rotation_degrees = 12.0;
  dynamics.pen_rotation_valid = true;
  CHECK(!dynamics.active());

  // Controls of Off / GlobalDefault only steer global-preference precedence; they must not
  // flip the per-dab path on (a Round brush that merely disables pressure stays procedural).
  dynamics.size_control = patchy::BrushDynamicControl::Off;
  dynamics.roundness_control = patchy::BrushDynamicControl::Off;
  dynamics.opacity_control = patchy::BrushDynamicControl::Off;
  dynamics.flow_control = patchy::BrushDynamicControl::Off;
  dynamics.size_fade_steps = 7;
  dynamics.minimum_opacity = 0.4;
  dynamics.minimum_flow = 0.4;
  CHECK(!dynamics.active());

  CHECK(patchy::BrushDynamics{.size_jitter = 0.1}.active());
  CHECK(patchy::BrushDynamics{.angle_jitter = 0.1}.active());
  CHECK(patchy::BrushDynamics{.angle_control = patchy::BrushDynamicControl::Direction}.active());
  CHECK(patchy::BrushDynamics{.roundness_jitter = 0.1}.active());
  CHECK(patchy::BrushDynamics{.flip_x_jitter = true}.active());
  CHECK(patchy::BrushDynamics{.flip_y_jitter = true}.active());
  CHECK(patchy::BrushDynamics{.scatter = 0.5}.active());
  CHECK(patchy::BrushDynamics{.count = 2}.active());
  CHECK(patchy::BrushDynamics{.opacity_jitter = 0.1}.active());
  CHECK(patchy::BrushDynamics{.flow_jitter = 0.1}.active());
  // A control with a real source activates even with zero jitter...
  CHECK(patchy::BrushDynamics{.size_control = patchy::BrushDynamicControl::PenPressure}.active());
  CHECK(patchy::BrushDynamics{.roundness_control = patchy::BrushDynamicControl::Fade}.active());
  CHECK(patchy::BrushDynamics{.opacity_control = patchy::BrushDynamicControl::StylusWheel}.active());
  CHECK(patchy::BrushDynamics{.flow_control = patchy::BrushDynamicControl::PenPressure}.active());
  // ...but scatter/count controls are inert until scatter/count themselves are non-default.
  CHECK(!patchy::BrushDynamics{.scatter_control = patchy::BrushDynamicControl::PenPressure}.active());
  CHECK(!patchy::BrushDynamics{.count_control = patchy::BrushDynamicControl::Fade}.active());
  CHECK(patchy::BrushDynamics{.texture_enabled = true}.active());
  CHECK((!patchy::BrushDynamics{.texture_enabled = true, .texture_depth = 0.0}.active()));
  CHECK(patchy::BrushDynamics{.dual_brush_enabled = true}.active());
  CHECK((patchy::BrushDynamics{.color_dynamics_enabled = true,
                               .foreground_background_jitter = 0.1}
             .active()));
  CHECK(!patchy::BrushDynamics{.color_dynamics_enabled = true}.active());
  CHECK(patchy::BrushDynamics{.wet_edges = true}.active());
}

void brush_dynamics_control_values() {
  // Per-dynamic controls map their input into [minimum, 1] deterministically (no RNG draws),
  // Photoshop-style: pressure/tilt/wheel read directly, rotation wraps into 0..1, fade uses the
  // per-dynamic step count, and a missing input reads as full value (mouse strokes).
  const auto approx = [](double value, double expected) { return std::abs(value - expected) < 1e-9; };
  patchy::BrushDynamicsRng rng;
  patchy::BrushDynamicsStrokeContext context;

  patchy::BrushDynamics size;
  size.size_control = patchy::BrushDynamicControl::PenPressure;
  size.minimum_diameter = 0.2;
  size.pen_pressure = 0.0;
  rng.seed(1);
  CHECK(approx(patchy::sample_dab_variation(size, rng, context, 100).scale, 0.2));
  size.pen_pressure = 0.5;
  CHECK(approx(patchy::sample_dab_variation(size, rng, context, 100).scale, 0.6));
  size.pen_pressure = 1.0;
  CHECK(approx(patchy::sample_dab_variation(size, rng, context, 100).scale, 1.0));

  // Defaulted pen inputs (no pen) read as full value, not zero.
  patchy::BrushDynamics mouse;
  mouse.size_control = patchy::BrushDynamicControl::PenPressure;
  CHECK(approx(patchy::sample_dab_variation(mouse, rng, context, 100).scale, 1.0));

  patchy::BrushDynamics roundness;
  roundness.roundness_control = patchy::BrushDynamicControl::PenTilt;
  roundness.minimum_roundness = 0.4;
  roundness.pen_tilt = 0.5;
  CHECK(approx(patchy::sample_dab_variation(roundness, rng, context, 100).roundness_multiplier, 0.7));

  patchy::BrushDynamics opacity;
  opacity.opacity_control = patchy::BrushDynamicControl::StylusWheel;
  opacity.minimum_opacity = 0.2;
  opacity.pen_wheel = 0.25;
  CHECK(approx(patchy::sample_dab_variation(opacity, rng, context, 100).opacity_multiplier, 0.4));
  opacity.opacity_control = patchy::BrushDynamicControl::PenRotation;
  opacity.pen_rotation_degrees = -90.0;  // wraps to 270deg = 0.75
  opacity.pen_rotation_valid = true;
  CHECK(approx(patchy::sample_dab_variation(opacity, rng, context, 100).opacity_multiplier, 0.8));
  opacity.pen_rotation_valid = false;  // no rotation hardware: full value
  CHECK(approx(patchy::sample_dab_variation(opacity, rng, context, 100).opacity_multiplier, 1.0));

  patchy::BrushDynamics flow;
  flow.flow_control = patchy::BrushDynamicControl::PenPressure;
  flow.minimum_flow = 0.15;
  flow.pen_pressure = 0.5;
  CHECK(approx(patchy::sample_dab_variation(flow, rng, context, 100).flow_multiplier, 0.575));
  flow.pen_pressure = 0.0;
  CHECK(approx(patchy::sample_dab_variation(flow, rng, context, 100).flow_multiplier, 0.15));

  // Fade scales scatter offsets by the per-dynamic step curve: identical RNG draws, half range.
  patchy::BrushDynamics scatter;
  scatter.scatter = 1.0;
  scatter.scatter_control = patchy::BrushDynamicControl::Fade;
  scatter.scatter_fade_steps = 10;
  context.step_index = 0;
  rng.seed(9);
  const auto full_offset = patchy::sample_dab_variation(scatter, rng, context, 100).offset_y;
  context.step_index = 5;
  rng.seed(9);
  const auto faded_offset = patchy::sample_dab_variation(scatter, rng, context, 100).offset_y;
  CHECK(std::abs(full_offset) > 1.0);
  CHECK(approx(faded_offset, full_offset * 0.5));
  context.step_index = 0;

  // Count control fades the dab count toward 1; the count-jitter draw gate is untouched.
  patchy::BrushDynamics count;
  count.count = 5;
  count.count_control = patchy::BrushDynamicControl::PenPressure;
  count.pen_pressure = 1.0;
  CHECK(patchy::sample_dab_count(count, rng, context) == 5);
  count.pen_pressure = 0.5;
  CHECK(patchy::sample_dab_count(count, rng, context) == 3);
  count.pen_pressure = 0.0;
  CHECK(patchy::sample_dab_count(count, rng, context) == 1);
  count.count_jitter = 1.0;
  rng.seed(4);
  const auto jittered = patchy::sample_dab_count(count, rng, context);
  CHECK(jittered >= 1 && jittered <= 5);
}

void tool_brush_tip_controls_at_full_value_change_nothing() {
  // Controls modulate results without consuming randomness, so sourcing every control while its
  // input sits at full value must reproduce the jitter-only stroke pixel for pixel (this pins
  // the RNG draw-order contract across the control feature).
  const auto tip = make_bar_brush_tip();
  const auto mips = patchy::build_brush_tip_mips(tip);
  const auto scaled = patchy::make_scaled_brush_tip(mips, 9);

  patchy::BrushDynamics jitter_only;
  jitter_only.size_jitter = 0.5;
  jitter_only.minimum_diameter = 0.3;
  jitter_only.angle_jitter = 0.4;
  jitter_only.roundness_jitter = 0.5;
  jitter_only.scatter = 0.8;
  jitter_only.scatter_both_axes = true;
  jitter_only.count = 3;
  jitter_only.count_jitter = 0.5;
  jitter_only.opacity_jitter = 0.4;
  jitter_only.flow_jitter = 0.3;
  jitter_only.seed = 4242;

  auto with_controls = jitter_only;
  with_controls.size_control = patchy::BrushDynamicControl::PenPressure;
  with_controls.roundness_control = patchy::BrushDynamicControl::StylusWheel;
  with_controls.scatter_control = patchy::BrushDynamicControl::PenPressure;
  with_controls.count_control = patchy::BrushDynamicControl::StylusWheel;
  with_controls.opacity_control = patchy::BrushDynamicControl::PenTilt;
  with_controls.minimum_opacity = 0.3;  // irrelevant at full input value
  with_controls.flow_control = patchy::BrushDynamicControl::PenPressure;
  with_controls.minimum_flow = 0.2;  // irrelevant at full input value
  // All pen inputs stay at their 1.0 defaults = full value.

  const auto paint_stroke = [&scaled](patchy::Document& document, patchy::LayerId layer,
                                      const patchy::BrushDynamics& dynamics) {
    auto options = tool_options(0, 0, 0);
    options.brush_size = 9;
    options.brush_tip = &scaled;
    options.brush_tip_spacing = 0.5;
    options.brush_dynamics = dynamics;
    patchy::BrushTipStrokeState state;
    return patchy::paint_brush_segment(document, layer, 10.0, 20.0, 44.0, 30.0, options, false, state);
  };

  auto plain_document = make_tool_document();
  const auto plain_layer = active_tool_layer(plain_document);
  CHECK(!paint_stroke(plain_document, plain_layer, jitter_only).empty());
  auto controlled_document = make_tool_document();
  const auto controlled_layer = active_tool_layer(controlled_document);
  CHECK(!paint_stroke(controlled_document, controlled_layer, with_controls).empty());

  const auto& plain = plain_document.find_layer(plain_layer)->pixels();
  const auto& controlled = controlled_document.find_layer(controlled_layer)->pixels();
  for (std::int32_t y = 0; y < plain.height(); ++y) {
    for (std::int32_t x = 0; x < plain.width(); ++x) {
      CHECK(plain.pixel(x, y)[3] == controlled.pixel(x, y)[3]);
    }
  }
}

void tool_brush_tip_size_control_pressure_scales_dabs() {
  const auto tip = make_bar_brush_tip();
  const auto mips = patchy::build_brush_tip_mips(tip);
  const auto scaled = patchy::make_scaled_brush_tip(mips, 9);

  const auto stroke_extent = [&scaled](double pressure) {
    auto options = tool_options(0, 0, 0);
    options.brush_size = 9;
    options.brush_tip = &scaled;
    options.brush_tip_spacing = 2.0;  // dabs at x=10 and x=28
    options.brush_dynamics.size_control = patchy::BrushDynamicControl::PenPressure;
    options.brush_dynamics.minimum_diameter = 0.5;
    options.brush_dynamics.pen_pressure = pressure;
    options.brush_dynamics.seed = 77;
    auto document = make_tool_document();
    const auto layer = active_tool_layer(document);
    patchy::BrushTipStrokeState state;
    CHECK(!patchy::paint_brush_segment(document, layer, 10.0, 20.0, 30.0, 20.0, options, false, state).empty());
    const auto& pixels = document.find_layer(layer)->pixels();
    std::int32_t min_x = 1000;
    std::int32_t max_x = -1000;
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      for (std::int32_t x = 2; x <= std::min(pixels.width() - 1, 18); ++x) {
        if (pixels.pixel(x, y)[3] > 0U) {
          min_x = std::min(min_x, x);
          max_x = std::max(max_x, x);
        }
      }
    }
    CHECK(max_x >= min_x);
    return max_x - min_x + 1;
  };

  // Zero pressure sits on the minimum-diameter floor, full pressure paints the full stamp, and
  // the mapping is monotonic in between (no randomness is involved with zero jitter).
  const auto light = stroke_extent(0.0);
  const auto medium = stroke_extent(0.5);
  const auto full = stroke_extent(1.0);
  CHECK(light >= 3 && light <= 6);  // 0.5 x 9px, antialiased
  CHECK(medium > light);
  CHECK(full > medium);
  CHECK(full >= 9);
}

void tool_brush_tip_opacity_control_respects_minimum() {
  const auto tip = make_bar_brush_tip();
  const auto mips = patchy::build_brush_tip_mips(tip);
  const auto scaled = patchy::make_scaled_brush_tip(mips, 9);

  const auto dab_alpha = [&scaled](double pressure) {
    auto options = tool_options(0, 0, 0);
    options.brush_size = 9;
    options.brush_tip = &scaled;
    options.brush_tip_spacing = 2.0;
    options.brush_dynamics.opacity_control = patchy::BrushDynamicControl::PenPressure;
    options.brush_dynamics.minimum_opacity = 0.3;
    options.brush_dynamics.pen_pressure = pressure;
    options.brush_dynamics.seed = 5;
    auto document = make_tool_document();
    const auto layer = active_tool_layer(document);
    patchy::BrushTipStrokeState state;
    CHECK(!patchy::paint_brush_segment(document, layer, 10.0, 20.0, 10.0, 20.0, options, false, state).empty());
    const auto& pixels = document.find_layer(layer)->pixels();
    std::uint8_t max_alpha = 0;
    for (std::int32_t x = 5; x <= 15; ++x) {
      max_alpha = std::max(max_alpha, pixels.pixel(x, 20)[3]);
    }
    return static_cast<int>(max_alpha);
  };

  // Pressure 0 bottoms out at the Minimum Opacity floor (30% of 255 with a little AA slack),
  // pressure 1 paints at full strength.
  const auto floor_alpha = dab_alpha(0.0);
  const auto full_alpha = dab_alpha(1.0);
  CHECK(floor_alpha >= 66 && floor_alpha <= 88);
  CHECK(full_alpha >= 250);
}

void tool_brush_tip_inactive_dynamics_change_nothing() {
  // An inactive BrushDynamics (even with a seed and pen inputs set) must leave the stamp path
  // on the exact historical code: pixel-for-pixel identical strokes.
  const auto tip = make_bar_brush_tip();
  const auto mips = patchy::build_brush_tip_mips(tip);
  const auto scaled = patchy::make_scaled_brush_tip(mips, 9);

  const auto paint_stroke = [&scaled](patchy::Document& document, patchy::LayerId layer,
                                      const patchy::BrushDynamics& dynamics) {
    auto options = tool_options(0, 0, 0);
    options.brush_size = 9;
    options.brush_tip = &scaled;
    options.brush_tip_spacing = 0.5;
    options.brush_dynamics = dynamics;
    patchy::BrushTipStrokeState state;
    return patchy::paint_brush_segment(document, layer, 10.0, 20.0, 40.0, 26.0, options, false, state);
  };

  auto plain_document = make_tool_document();
  const auto plain_layer = active_tool_layer(plain_document);
  CHECK(!paint_stroke(plain_document, plain_layer, patchy::BrushDynamics{}).empty());

  patchy::BrushDynamics inactive;
  inactive.seed = 99;
  inactive.pen_pressure = 0.25;
  inactive.pen_tilt_azimuth_degrees = 33.0;
  inactive.pen_tilt_azimuth_valid = true;
  inactive.size_control = patchy::BrushDynamicControl::Off;  // suppression-only, still inactive
  auto seeded_document = make_tool_document();
  const auto seeded_layer = active_tool_layer(seeded_document);
  CHECK(!paint_stroke(seeded_document, seeded_layer, inactive).empty());

  const auto& plain = plain_document.find_layer(plain_layer)->pixels();
  const auto& seeded = seeded_document.find_layer(seeded_layer)->pixels();
  for (std::int32_t y = 0; y < plain.height(); ++y) {
    for (std::int32_t x = 0; x < plain.width(); ++x) {
      CHECK(plain.pixel(x, y)[3] == seeded.pixel(x, y)[3]);
    }
  }
}

void tool_brush_texture_and_dual_brush_render_deterministically() {
  const auto tip = make_solid_scaled_tip(21);
  const auto plain = render_effect_dab(tip, {});

  patchy::BrushDynamics texture;
  texture.texture_enabled = true;
  texture.texture_style = patchy::BrushTextureStyle::Speckle;
  texture.texture_scale = 0.8;
  texture.texture_depth = 1.0;
  texture.texture_seed = 0x12345678U;
  const auto textured = render_effect_dab(tip, texture);
  CHECK(textured != plain);
  CHECK(render_effect_dab(tip, texture) == textured);
  texture.texture_seed ^= 0x00ABCDEFU;
  CHECK(render_effect_dab(tip, texture) != textured);

  patchy::BrushDynamics dual;
  dual.dual_brush_enabled = true;
  dual.dual_brush_size = 0.25;
  dual.dual_brush_hardness = 1.0;
  dual.dual_brush_spacing = 2.0;
  const auto dual_pixels = render_effect_dab(tip, dual);
  CHECK(dual_pixels != plain);
  const auto painted_alpha_count = [](const std::vector<std::uint8_t>& bytes) {
    std::size_t count = 0;
    for (std::size_t offset = 3; offset < bytes.size(); offset += 4U) {
      count += bytes[offset] > 0U ? 1U : 0U;
    }
    return count;
  };
  CHECK(painted_alpha_count(dual_pixels) < painted_alpha_count(plain));

}

void mixer_brush_color_uses_one_stroke_start_sample_and_distance_decay() {
  patchy::MixerBrushState state;
  const patchy::EditColor sampled_blue{0, 30, 240, 255};
  const patchy::EditColor loaded_red{240, 20, 0, 255};
  patchy::begin_mixer_brush_stroke(state, sampled_blue);

  const auto first = patchy::mixer_brush_dab_color(state, 10.0, 10.0, 20, loaded_red,
                                                    100, 100, 100);
  CHECK(first.r == sampled_blue.r);
  CHECK(first.g == sampled_blue.g);
  CHECK(first.b == sampled_blue.b);
  CHECK(first.a == 255);

  const auto later = patchy::mixer_brush_dab_color(state, 130.0, 10.0, 20, loaded_red,
                                                    100, 100, 100);
  CHECK(later.r > first.r);
  CHECK(later.b < first.b);
  CHECK(later.a < first.a);
  CHECK(state.initial_sample.r == sampled_blue.r);
  CHECK(state.initial_sample.g == sampled_blue.g);
  CHECK(state.initial_sample.b == sampled_blue.b);

  patchy::MixerBrushState dry_state;
  patchy::begin_mixer_brush_stroke(dry_state, sampled_blue);
  const auto dry = patchy::mixer_brush_dab_color(dry_state, 10.0, 10.0, 20, loaded_red,
                                                 0, 100, 100);
  CHECK(dry.r == loaded_red.r);
  CHECK(dry.g == loaded_red.g);
  CHECK(dry.b == loaded_red.b);
  CHECK(dry.a == loaded_red.a);

  patchy::MixerBrushState low_load_state;
  patchy::begin_mixer_brush_stroke(low_load_state, sampled_blue);
  (void)patchy::mixer_brush_dab_color(low_load_state, 0.0, 0.0, 20, loaded_red,
                                      0, 1, 0);
  const auto low_load = patchy::mixer_brush_dab_color(low_load_state, 40.0, 0.0, 20,
                                                       loaded_red, 0, 1, 0);
  patchy::MixerBrushState full_load_state;
  patchy::begin_mixer_brush_stroke(full_load_state, sampled_blue);
  (void)patchy::mixer_brush_dab_color(full_load_state, 0.0, 0.0, 20, loaded_red,
                                      0, 100, 0);
  const auto full_load = patchy::mixer_brush_dab_color(full_load_state, 40.0, 0.0, 20,
                                                        loaded_red, 0, 100, 0);
  CHECK(low_load.a < full_load.a);
  CHECK(full_load.a == loaded_red.a);

  // The same per-dab source rides the bitmap-tip path used by imported ABR tips.
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  const auto tip = make_solid_scaled_tip(9);
  auto options = tool_options(loaded_red.r, loaded_red.g, loaded_red.b);
  options.primary = loaded_red;
  options.brush_size = 9;
  options.brush_tip = &tip;
  options.brush_tip_spacing = 0.5;
  patchy::MixerBrushState tip_state;
  patchy::begin_mixer_brush_stroke(tip_state, sampled_blue);
  options.dab_primary_provider = [&tip_state](double x, double y,
                                              const patchy::EditColor& loaded_color) {
    return patchy::mixer_brush_dab_color(tip_state, x, y, 9, loaded_color, 100, 100, 75);
  };
  patchy::BrushTipStrokeState stroke_state;
  CHECK(!patchy::paint_brush_segment(document, layer_id, 10.0, 24.0, 42.0, 24.0,
                                      options, false, stroke_state).empty());
  CHECK(tip_state.distance > 20.0);
  const auto* start_pixel = document.find_layer(layer_id)->pixels().pixel(10, 24);
  const auto* end_pixel = document.find_layer(layer_id)->pixels().pixel(42, 24);
  CHECK(start_pixel[2] > start_pixel[0]);
  CHECK(end_pixel[0] > start_pixel[0]);
  CHECK(end_pixel[2] < start_pixel[2]);
}

void tool_brush_color_dynamics_varies_selected_colors_only() {
  const auto tip = make_solid_scaled_tip(5);
  patchy::BrushDynamics dynamics;
  dynamics.color_dynamics_enabled = true;
  dynamics.foreground_background_jitter = 1.0;
  dynamics.hue_jitter = 0.08;
  dynamics.seed = 0xC0104U;

  const patchy::EditColor foreground{255, 0, 0, 255};
  const patchy::EditColor background{0, 0, 255, 255};
  const auto first = render_effect_dab(tip, dynamics, foreground, background);
  CHECK(render_effect_dab(tip, dynamics, foreground, background) == first);
  const auto center = (24U * 64U + 24U) * 4U;
  CHECK(first[center + 3U] == 255U);
  CHECK(first[center] != foreground.r || first[center + 1U] != foreground.g ||
        first[center + 2U] != foreground.b);

  // The UI installs a stroke compositor callback. Its contract must receive the varied dab
  // color rather than the stroke's captured foreground color.
  auto writer_document = make_tool_document();
  const auto writer_layer = active_tool_layer(writer_document);
  auto writer_options = tool_options(foreground.r, foreground.g, foreground.b);
  writer_options.primary = foreground;
  writer_options.secondary = background;
  writer_options.brush_size = tip.width;
  writer_options.brush_tip = &tip;
  writer_options.brush_dynamics = dynamics;
  patchy::EditColor callback_color{};
  writer_options.stroke_pixel_writer =
      [&callback_color](std::int32_t, std::int32_t, std::uint8_t* pixel,
                        std::uint16_t channels, float, const patchy::EditColor& dab_primary) {
        callback_color = dab_primary;
        pixel[0] = dab_primary.r;
        pixel[1] = dab_primary.g;
        pixel[2] = dab_primary.b;
        if (channels >= 4U) {
          pixel[3] = dab_primary.a;
        }
        return true;
      };
  patchy::BrushTipStrokeState writer_state;
  CHECK(!patchy::paint_brush_segment(writer_document, writer_layer, 24.0, 24.0, 24.0, 24.0,
                                     writer_options, false, writer_state)
             .empty());
  CHECK(callback_color.r == first[center]);
  CHECK(callback_color.g == first[center + 1U]);
  CHECK(callback_color.b == first[center + 2U]);

  // "Apply Per Tip" samples each dab; disabling it samples once and reuses the same selected
  // foreground/background variation for the stroke.
  patchy::BrushDynamicsRng rng;
  patchy::BrushDynamicsStrokeContext context;
  rng.seed(dynamics.seed);
  dynamics.color_per_tip = false;
  const auto stroke_first = patchy::sample_dab_variation(dynamics, rng, context, 5);
  const auto stroke_second = patchy::sample_dab_variation(dynamics, rng, context, 5);
  CHECK(stroke_first.foreground_background_mix == stroke_second.foreground_background_mix);
  CHECK(stroke_first.hue_shift == stroke_second.hue_shift);
  dynamics.color_per_tip = true;
  context = {};
  rng.seed(dynamics.seed);
  const auto tip_first = patchy::sample_dab_variation(dynamics, rng, context, 5);
  const auto tip_second = patchy::sample_dab_variation(dynamics, rng, context, 5);
  CHECK(tip_first.foreground_background_mix != tip_second.foreground_background_mix ||
        tip_first.hue_shift != tip_second.hue_shift);
}

void tool_brush_effect_pixels_round_trip_exactly_through_psd() {
  const auto tip = make_solid_scaled_tip(17);
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(245, 35, 20);
  options.secondary = {15, 80, 240, 255};
  options.brush_size = tip.width;
  options.brush_tip = &tip;
  options.brush_tip_spacing = 0.5;
  options.brush_dynamics.texture_enabled = true;
  options.brush_dynamics.texture_style = patchy::BrushTextureStyle::Canvas;
  options.brush_dynamics.texture_depth = 0.6;
  options.brush_dynamics.dual_brush_enabled = true;
  options.brush_dynamics.dual_brush_size = 0.3;
  options.brush_dynamics.dual_brush_spacing = 1.5;
  options.brush_dynamics.color_dynamics_enabled = true;
  options.brush_dynamics.foreground_background_jitter = 0.8;
  options.brush_dynamics.wet_edges = true;
  options.brush_dynamics.seed = 0xB205D5U;
  patchy::BrushTipStrokeState state;
  CHECK(!patchy::paint_brush_segment(document, layer_id, 12.0, 24.0, 52.0, 24.0, options,
                                     false, state)
             .empty());

  const auto* painted = document.find_layer(layer_id);
  CHECK(painted != nullptr);
  const auto expected_span = std::as_const(*painted).pixels().data();
  const std::vector<std::uint8_t> expected_pixels(expected_span.begin(), expected_span.end());
  const auto reread =
      patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(document));
  const auto* reopened = patchy::test::find_layer_named(reread.layers(), "Paint");
  CHECK(reopened != nullptr);
  const auto reopened_pixels = std::as_const(*reopened).pixels().data();
  CHECK(std::vector<std::uint8_t>(reopened_pixels.begin(), reopened_pixels.end()) ==
        expected_pixels);
}

void tool_brush_tip_size_jitter_shrinks_dabs_deterministically() {
  const auto tip = make_bar_brush_tip();
  const auto mips = patchy::build_brush_tip_mips(tip);
  const auto scaled = patchy::make_scaled_brush_tip(mips, 9);

  auto options = tool_options(0, 0, 0);
  options.brush_size = 9;
  options.brush_tip = &scaled;
  options.brush_tip_spacing = 2.0;  // dabs every 18px: (10,20) and (28,20)
  options.brush_dynamics.size_jitter = 1.0;
  options.brush_dynamics.minimum_diameter = 0.5;
  options.brush_dynamics.seed = 1234;

  const auto paint_stroke = [&options](patchy::Document& document, patchy::LayerId layer) {
    patchy::BrushTipStrokeState state;
    return patchy::paint_brush_segment(document, layer, 10.0, 20.0, 30.0, 20.0, options, false, state);
  };

  auto document = make_tool_document();
  const auto layer = active_tool_layer(document);
  CHECK(!paint_stroke(document, layer).empty());
  const auto& pixels = document.find_layer(layer)->pixels();

  // Horizontal extent of the painted bar around each dab center: full size ~9-10 px with
  // antialiasing, minimum diameter 0.5 floors it at ~4.
  const auto dab_extent = [&pixels](std::int32_t center_x) {
    std::int32_t min_x = 1000;
    std::int32_t max_x = -1000;
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      for (std::int32_t x = std::max(0, center_x - 8); x <= std::min(pixels.width() - 1, center_x + 8); ++x) {
        if (pixels.pixel(x, y)[3] > 0U) {
          min_x = std::min(min_x, x);
          max_x = std::max(max_x, x);
        }
      }
    }
    CHECK(max_x >= min_x);
    return max_x - min_x + 1;
  };
  const auto first_extent = dab_extent(10);
  const auto second_extent = dab_extent(28);
  CHECK(first_extent <= 10);
  CHECK(second_extent <= 10);
  CHECK(first_extent >= 4);
  CHECK(second_extent >= 4);
  // Size jitter only shrinks; with jitter 1.0 the chosen seed visibly shrinks at least one dab.
  CHECK(std::min(first_extent, second_extent) <= 8);

  // Same seed reproduces the exact pixels; a different seed varies them.
  auto replay_document = make_tool_document();
  const auto replay_layer = active_tool_layer(replay_document);
  CHECK(!paint_stroke(replay_document, replay_layer).empty());
  const auto& replay = replay_document.find_layer(replay_layer)->pixels();
  bool replay_identical = true;
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      replay_identical = replay_identical && pixels.pixel(x, y)[3] == replay.pixel(x, y)[3];
    }
  }
  CHECK(replay_identical);

  options.brush_dynamics.seed = 5678;
  auto reseeded_document = make_tool_document();
  const auto reseeded_layer = active_tool_layer(reseeded_document);
  CHECK(!paint_stroke(reseeded_document, reseeded_layer).empty());
  const auto& reseeded = reseeded_document.find_layer(reseeded_layer)->pixels();
  bool reseeded_identical = true;
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      reseeded_identical = reseeded_identical && pixels.pixel(x, y)[3] == reseeded.pixel(x, y)[3];
    }
  }
  CHECK(!reseeded_identical);
}

void tool_brush_tip_angle_direction_follows_stroke() {
  const auto tip = make_bar_brush_tip();
  const auto mips = patchy::build_brush_tip_mips(tip);
  const auto scaled = patchy::make_scaled_brush_tip(mips, 9);

  const auto base_options = [&scaled] {
    auto options = tool_options(0, 0, 0);
    options.brush_size = 9;
    options.brush_tip = &scaled;
    options.brush_tip_spacing = 2.0;  // dabs every 18px
    return options;
  };

  // Direction control: a vertical stroke rotates the horizontal bar to vertical, including the
  // very first dab (the segment direction is latched before it stamps).
  auto vertical_document = make_tool_document();
  const auto vertical_layer = active_tool_layer(vertical_document);
  auto vertical_options = base_options();
  vertical_options.brush_dynamics.angle_control = patchy::BrushDynamicControl::Direction;
  patchy::BrushTipStrokeState vertical_state;
  CHECK(!patchy::paint_brush_segment(vertical_document, vertical_layer, 24.0, 10.0, 24.0, 30.0,
                                     vertical_options, false, vertical_state)
             .empty());
  const auto& vertical = vertical_document.find_layer(vertical_layer)->pixels();
  CHECK(vertical.pixel(24, 13)[3] == 255);  // dab at (24,10) spans y 6..14
  CHECK(vertical.pixel(27, 10)[3] == 0);
  CHECK(vertical.pixel(24, 25)[3] == 255);  // dab at (24,28) spans y 24..32
  CHECK(vertical.pixel(27, 28)[3] == 0);

  // A horizontal stroke keeps the bar horizontal (direction angle 0).
  auto horizontal_document = make_tool_document();
  const auto horizontal_layer = active_tool_layer(horizontal_document);
  patchy::BrushTipStrokeState horizontal_state;
  CHECK(!patchy::paint_brush_segment(horizontal_document, horizontal_layer, 10.0, 24.0, 30.0, 24.0,
                                     vertical_options, false, horizontal_state)
             .empty());
  const auto& horizontal = horizontal_document.find_layer(horizontal_layer)->pixels();
  CHECK(horizontal.pixel(13, 24)[3] == 255);
  CHECK(horizontal.pixel(10, 27)[3] == 0);

  // InitialDirection latches the first leg's angle: dabs on the descending leg stay horizontal.
  auto initial_document = make_tool_document();
  const auto initial_layer = active_tool_layer(initial_document);
  auto initial_options = base_options();
  initial_options.brush_dynamics.angle_control = patchy::BrushDynamicControl::InitialDirection;
  patchy::BrushTipStrokeState initial_state;
  CHECK(!patchy::paint_brush_segment(initial_document, initial_layer, 10.0, 20.0, 26.0, 20.0,
                                     initial_options, false, initial_state)
             .empty());
  CHECK(!patchy::paint_brush_segment(initial_document, initial_layer, 26.0, 20.0, 26.0, 40.0,
                                     initial_options, false, initial_state)
             .empty());
  const auto& initial = initial_document.find_layer(initial_layer)->pixels();
  CHECK(initial.pixel(29, 22)[3] == 255);  // dab at (26,22) on the vertical leg, still horizontal
  CHECK(initial.pixel(26, 25)[3] == 0);
  CHECK(initial.pixel(29, 40)[3] == 255);  // dab at (26,40)
  CHECK(initial.pixel(26, 43)[3] == 0);
}

void tool_brush_tip_angle_fade_and_jitter_rotate_dabs() {
  const auto tip = make_bar_brush_tip();
  const auto mips = patchy::build_brush_tip_mips(tip);
  const auto scaled = patchy::make_scaled_brush_tip(mips, 9);

  // Fade sweeps the angle over fade_steps spacing steps: step 0 = 360° (horizontal bar),
  // step 1 with fade_steps 4 = 270° (vertical bar).
  auto fade_document = make_tool_document();
  const auto fade_layer = active_tool_layer(fade_document);
  auto fade_options = tool_options(0, 0, 0);
  fade_options.brush_size = 9;
  fade_options.brush_tip = &scaled;
  fade_options.brush_tip_spacing = 2.0;
  fade_options.brush_dynamics.angle_control = patchy::BrushDynamicControl::Fade;
  fade_options.brush_dynamics.angle_fade_steps = 4;
  patchy::BrushTipStrokeState fade_state;
  CHECK(!patchy::paint_brush_segment(fade_document, fade_layer, 10.0, 20.0, 30.0, 20.0, fade_options,
                                     false, fade_state)
             .empty());
  const auto& fade = fade_document.find_layer(fade_layer)->pixels();
  CHECK(fade.pixel(13, 20)[3] == 255);  // step 0: horizontal
  CHECK(fade.pixel(10, 23)[3] == 0);
  CHECK(fade.pixel(28, 23)[3] == 255);  // step 1: rotated to vertical
  CHECK(fade.pixel(31, 20)[3] == 0);

  // Angle jitter is seed-deterministic.
  const auto paint_jittered = [&scaled](patchy::Document& document, patchy::LayerId layer,
                                        std::uint32_t seed) {
    auto options = tool_options(0, 0, 0);
    options.brush_size = 9;
    options.brush_tip = &scaled;
    options.brush_tip_spacing = 2.0;
    options.brush_dynamics.angle_jitter = 1.0;
    options.brush_dynamics.seed = seed;
    patchy::BrushTipStrokeState state;
    return patchy::paint_brush_segment(document, layer, 10.0, 20.0, 30.0, 20.0, options, false, state);
  };
  auto first_document = make_tool_document();
  const auto first_layer = active_tool_layer(first_document);
  CHECK(!paint_jittered(first_document, first_layer, 777).empty());
  auto second_document = make_tool_document();
  const auto second_layer = active_tool_layer(second_document);
  CHECK(!paint_jittered(second_document, second_layer, 777).empty());
  auto third_document = make_tool_document();
  const auto third_layer = active_tool_layer(third_document);
  CHECK(!paint_jittered(third_document, third_layer, 778).empty());

  const auto& first = first_document.find_layer(first_layer)->pixels();
  const auto& second = second_document.find_layer(second_layer)->pixels();
  const auto& third = third_document.find_layer(third_layer)->pixels();
  bool same_seed_identical = true;
  bool other_seed_identical = true;
  for (std::int32_t y = 0; y < first.height(); ++y) {
    for (std::int32_t x = 0; x < first.width(); ++x) {
      same_seed_identical = same_seed_identical && first.pixel(x, y)[3] == second.pixel(x, y)[3];
      other_seed_identical = other_seed_identical && first.pixel(x, y)[3] == third.pixel(x, y)[3];
    }
  }
  CHECK(same_seed_identical);
  CHECK(!other_seed_identical);
}

void tool_brush_tip_flip_jitter_mirrors_stamp() {
  // A tip with a bar only in its left half: unflipped dabs paint left of center, flip-X dabs
  // paint right of center. Across several seeded clicks both orientations must appear.
  patchy::BrushTip half_bar;
  half_bar.width = 9;
  half_bar.height = 9;
  half_bar.mask.assign(81, 0);
  for (std::int32_t x = 0; x < 4; ++x) {
    half_bar.mask[4U * 9U + static_cast<std::size_t>(x)] = 255;
  }
  const auto mips = patchy::build_brush_tip_mips(half_bar);
  const auto scaled = patchy::make_scaled_brush_tip(mips, 9);

  auto document = make_tool_document();
  const auto layer = active_tool_layer(document);
  bool saw_left = false;
  bool saw_right = false;
  for (std::uint32_t click = 0; click < 11; ++click) {
    auto options = tool_options(0, 0, 0);
    options.brush_size = 9;
    options.brush_tip = &scaled;
    options.brush_dynamics.flip_x_jitter = true;
    options.brush_dynamics.seed = click;
    patchy::BrushTipStrokeState state;
    const auto y = 4.0 + 4.0 * static_cast<double>(click);
    CHECK(!patchy::paint_brush_segment(document, layer, 24.0, y, 24.0, y, options, false, state).empty());

    const auto& pixels = document.find_layer(layer)->pixels();
    const auto row = static_cast<std::int32_t>(y);
    bool left = false;
    bool right = false;
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      if (pixels.pixel(x, row)[3] > 128U) {
        left = left || x <= 22;
        right = right || x >= 26;
      }
    }
    CHECK(left != right);  // each dab is either unflipped or mirrored, never both
    saw_left = saw_left || left;
    saw_right = saw_right || right;
  }
  CHECK(saw_left);
  CHECK(saw_right);
}

void tool_brush_tip_scatter_offsets_perpendicular_to_stroke() {
  const auto tip = make_bar_brush_tip();
  const auto mips = patchy::build_brush_tip_mips(tip);
  const auto scaled = patchy::make_scaled_brush_tip(mips, 9);

  const auto painted_bounds = [](const patchy::PixelBuffer& pixels, std::int32_t& min_x,
                                 std::int32_t& max_x, std::int32_t& min_y, std::int32_t& max_y) {
    min_x = min_y = 1000;
    max_x = max_y = -1000;
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        if (pixels.pixel(x, y)[3] > 0U) {
          min_x = std::min(min_x, x);
          max_x = std::max(max_x, x);
          min_y = std::min(min_y, y);
          max_y = std::max(max_y, y);
        }
      }
    }
  };

  // Scatter on a horizontal stroke moves dabs vertically only: the dab centers stay on the
  // spacing grid horizontally, so x stays within the bar footprint of the grid positions.
  auto document = make_tool_document();
  const auto layer = active_tool_layer(document);
  auto options = tool_options(0, 0, 0);
  options.brush_size = 9;
  options.brush_tip = &scaled;
  options.brush_tip_spacing = 2.0;  // dabs at x=10 and x=28
  options.brush_dynamics.scatter = 1.5;  // offsets up to 13.5px
  options.brush_dynamics.seed = 4321;    // draws +0.64 / -0.86: dabs land ~20px apart vertically
  patchy::BrushTipStrokeState state;
  CHECK(!patchy::paint_brush_segment(document, layer, 10.0, 20.0, 30.0, 20.0, options, false, state).empty());

  std::int32_t min_x = 0;
  std::int32_t max_x = 0;
  std::int32_t min_y = 0;
  std::int32_t max_y = 0;
  painted_bounds(document.find_layer(layer)->pixels(), min_x, max_x, min_y, max_y);
  CHECK(min_x >= 5);   // 10 - bar half width
  CHECK(max_x <= 33);  // 28 + bar half width
  CHECK(min_y >= 6);   // 20 - 13.5 - antialias
  CHECK(max_y <= 34);
  CHECK(max_y - min_y >= 4);  // the chosen seed visibly leaves the stroke line

  // Both Axes adds parallel offsets: painted pixels now stray outside the grid columns.
  auto both_document = make_tool_document();
  const auto both_layer = active_tool_layer(both_document);
  options.brush_dynamics.scatter_both_axes = true;
  patchy::BrushTipStrokeState both_state;
  CHECK(!patchy::paint_brush_segment(both_document, both_layer, 10.0, 20.0, 30.0, 20.0, options, false,
                                     both_state)
             .empty());
  painted_bounds(both_document.find_layer(both_layer)->pixels(), min_x, max_x, min_y, max_y);
  CHECK(min_x < 5 || max_x > 33);
}

void tool_brush_tip_count_stamps_multiple_dabs_per_step() {
  const auto tip = make_bar_brush_tip();
  const auto mips = patchy::build_brush_tip_mips(tip);
  const auto scaled = patchy::make_scaled_brush_tip(mips, 9);

  const auto painted_pixels = [](const patchy::Document& document, patchy::LayerId layer_id) {
    const auto& pixels = document.find_layer(layer_id)->pixels();
    int count = 0;
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        if (pixels.pixel(x, y)[3] > 0U) {
          ++count;
        }
      }
    }
    return count;
  };

  // A single click with count 3 and scatter stamps three scattered bars instead of one.
  auto single_document = make_tool_document();
  const auto single_layer = active_tool_layer(single_document);
  auto options = tool_options(0, 0, 0);
  options.brush_size = 9;
  options.brush_tip = &scaled;
  patchy::BrushTipStrokeState single_state;
  CHECK(!patchy::paint_brush_segment(single_document, single_layer, 24.0, 24.0, 24.0, 24.0, options, false,
                                     single_state)
             .empty());
  const auto single_count = painted_pixels(single_document, single_layer);

  auto counted_document = make_tool_document();
  const auto counted_layer = active_tool_layer(counted_document);
  options.brush_dynamics.count = 3;
  options.brush_dynamics.scatter = 1.0;
  options.brush_dynamics.seed = 555;
  patchy::BrushTipStrokeState counted_state;
  CHECK(!patchy::paint_brush_segment(counted_document, counted_layer, 24.0, 24.0, 24.0, 24.0, options,
                                     false, counted_state)
             .empty());
  const auto counted_count = painted_pixels(counted_document, counted_layer);
  CHECK(counted_count >= single_count * 2);  // three bars minus possible overlap

  // Count jitter stays within [1, count] and is seed-deterministic.
  options.brush_dynamics.count_jitter = 1.0;
  auto jittered_document = make_tool_document();
  const auto jittered_layer = active_tool_layer(jittered_document);
  patchy::BrushTipStrokeState jittered_state;
  CHECK(!patchy::paint_brush_segment(jittered_document, jittered_layer, 24.0, 24.0, 24.0, 24.0, options,
                                     false, jittered_state)
             .empty());
  const auto jittered_count = painted_pixels(jittered_document, jittered_layer);
  CHECK(jittered_count >= 8);   // at least one full bar
  CHECK(jittered_count <= 60);  // never more than `count` bars (3 × ~20px antialiased)

  // Heavier dynamics stay fast: count 8 with scatter across a long stroke.
  patchy::Document wide_document(512, 128, patchy::PixelFormat::rgb8());
  wide_document.add_pixel_layer("Background", solid_rgb(512, 128, 255, 255, 255));
  wide_document.add_pixel_layer("Paint", solid_rgba(512, 128, 0, 0, 0, 0));
  const auto wide_layer = active_tool_layer(wide_document);
  auto wide_options = tool_options(0, 0, 0);
  wide_options.brush_size = 64;
  const auto wide_mips = patchy::build_brush_tip_mips([&] {
    patchy::BrushTip block;
    block.width = 64;
    block.height = 64;
    block.mask.assign(64U * 64U, 255);
    return block;
  }());
  const auto wide_scaled = patchy::make_scaled_brush_tip(wide_mips, 64);
  wide_options.brush_tip = &wide_scaled;
  wide_options.brush_tip_spacing = 0.25;
  wide_options.brush_dynamics.count = 8;
  wide_options.brush_dynamics.scatter = 1.0;
  wide_options.brush_dynamics.size_jitter = 0.5;
  wide_options.brush_dynamics.angle_jitter = 1.0;
  wide_options.brush_dynamics.seed = 42;
  patchy::BrushTipStrokeState wide_state;
  const auto start = std::chrono::steady_clock::now();
  CHECK(!patchy::paint_brush_segment(wide_document, wide_layer, 40.0, 64.0, 460.0, 64.0, wide_options,
                                     false, wide_state)
             .empty());
  const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - start)
                              .count();
  CHECK(elapsed_ms < 1000);
}

void tool_brush_tip_opacity_jitter_varies_dab_alpha() {
  const auto tip = make_bar_brush_tip();
  const auto mips = patchy::build_brush_tip_mips(tip);
  const auto scaled = patchy::make_scaled_brush_tip(mips, 9);

  auto document = make_tool_document();
  const auto layer = active_tool_layer(document);
  auto options = tool_options(0, 0, 0);
  options.brush_size = 9;
  options.brush_tip = &scaled;
  options.brush_tip_spacing = 2.0;  // dabs at x=10, 28, 46
  options.brush_dynamics.opacity_jitter = 1.0;
  options.brush_dynamics.seed = 999;
  patchy::BrushTipStrokeState state;
  CHECK(!patchy::paint_brush_segment(document, layer, 10.0, 20.0, 48.0, 20.0, options, false, state).empty());

  const auto& pixels = document.find_layer(layer)->pixels();
  const auto dab_alpha = [&pixels](std::int32_t center_x) {
    std::uint8_t max_alpha = 0;
    for (std::int32_t x = std::max(0, center_x - 5); x <= std::min(pixels.width() - 1, center_x + 5); ++x) {
      max_alpha = std::max(max_alpha, pixels.pixel(x, 20)[3]);
    }
    return max_alpha;
  };
  const auto first = dab_alpha(10);
  const auto second = dab_alpha(28);
  const auto third = dab_alpha(46);
  CHECK(first >= 7);  // opacity floor is 3%
  CHECK(second >= 7);
  CHECK(third >= 7);
  CHECK(first <= 255);
  // The jitter must actually vary alpha across dabs for this seed.
  CHECK(!(first == second && second == third));
}

void tool_brush_tip_flow_jitter_varies_dab_alpha() {
  const auto tip = make_bar_brush_tip();
  const auto mips = patchy::build_brush_tip_mips(tip);
  const auto scaled = patchy::make_scaled_brush_tip(mips, 9);

  auto document = make_tool_document();
  const auto layer = active_tool_layer(document);
  auto options = tool_options(0, 0, 0);
  options.brush_size = 9;
  options.brush_tip = &scaled;
  options.brush_tip_spacing = 2.0;  // dabs at x=10, 28, 46
  options.brush_dynamics.flow_jitter = 1.0;
  options.brush_dynamics.seed = 999;
  patchy::BrushTipStrokeState state;
  CHECK(!patchy::paint_brush_segment(document, layer, 10.0, 20.0, 48.0, 20.0, options, false, state).empty());

  const auto& pixels = document.find_layer(layer)->pixels();
  const auto dab_alpha = [&pixels](std::int32_t center_x) {
    std::uint8_t max_alpha = 0;
    for (std::int32_t x = std::max(0, center_x - 5); x <= std::min(pixels.width() - 1, center_x + 5); ++x) {
      max_alpha = std::max(max_alpha, pixels.pixel(x, 20)[3]);
    }
    return max_alpha;
  };
  const auto first = dab_alpha(10);
  const auto second = dab_alpha(28);
  const auto third = dab_alpha(46);
  CHECK(first > 0 || second > 0 || third > 0);
  CHECK(std::min({first, second, third}) < 250);
  CHECK(!(first == second && second == third));
}

void tool_stationary_airbrush_dynamics_advance_without_scatter_bursts() {
  const auto tip = make_bar_brush_tip();
  const auto mips = patchy::build_brush_tip_mips(tip);
  const auto scaled = patchy::make_scaled_brush_tip(mips, 9);

  auto plain_document = make_tool_document();
  const auto plain_layer = active_tool_layer(plain_document);
  auto plain_options = tool_options(0, 0, 0);
  plain_options.brush_size = 9;
  plain_options.brush_tip = &scaled;
  CHECK(!patchy::paint_brush_dab(plain_document, plain_layer, 24.0, 24.0, plain_options, false).empty());

  // Stationary Airbrush ticks ignore movement-only Scattering and Count: even an extreme
  // configuration remains exactly one centered flat stamp.
  auto stationary_document = make_tool_document();
  const auto stationary_layer = active_tool_layer(stationary_document);
  auto stationary_options = plain_options;
  stationary_options.brush_dynamics.scatter = 10.0;
  stationary_options.brush_dynamics.scatter_both_axes = true;
  stationary_options.brush_dynamics.count = 16;
  stationary_options.brush_dynamics.count_jitter = 1.0;
  stationary_options.brush_dynamics.seed = 98765;
  patchy::BrushTipStrokeState stationary_state;
  CHECK(!patchy::paint_stationary_airbrush_dab(stationary_document, stationary_layer, 24.0, 24.0,
                                               stationary_options, stationary_state)
             .empty());
  const auto& plain_pixels = std::as_const(plain_document).find_layer(plain_layer)->pixels();
  const auto& stationary_pixels =
      std::as_const(stationary_document).find_layer(stationary_layer)->pixels();
  CHECK(std::equal(plain_pixels.data().begin(), plain_pixels.data().end(),
                   stationary_pixels.data().begin(), stationary_pixels.data().end()));
  CHECK(stationary_state.initialized);
  CHECK(stationary_state.dynamics.step_index == 1U);

  // Shape/Transfer random draws and Fade steps do advance. The expected RNG stream omits only
  // the suppressed Count/Scatter draws, so a later moving segment continues deterministically.
  auto dynamic_document = make_tool_document();
  const auto dynamic_layer = active_tool_layer(dynamic_document);
  auto dynamic_options = stationary_options;
  dynamic_options.brush_dynamics.size_jitter = 0.25;
  dynamic_options.brush_dynamics.opacity_jitter = 0.2;
  dynamic_options.brush_dynamics.flow_jitter = 0.2;
  dynamic_options.brush_dynamics.opacity_control = patchy::BrushDynamicControl::Fade;
  dynamic_options.brush_dynamics.opacity_fade_steps = 2;
  dynamic_options.brush_dynamics.minimum_opacity = 0.2;
  dynamic_options.brush_dynamics.flow_control = patchy::BrushDynamicControl::Fade;
  dynamic_options.brush_dynamics.flow_fade_steps = 2;
  dynamic_options.brush_dynamics.minimum_flow = 0.4;
  patchy::BrushTipStrokeState dynamic_state;
  patchy::BrushDynamicsRng expected_rng;
  expected_rng.seed(dynamic_options.brush_dynamics.seed);
  auto expected_dynamics = dynamic_options.brush_dynamics;
  expected_dynamics.scatter = 0.0;
  patchy::BrushDynamicsStrokeContext expected_context;
  for (const auto x : {12.0, 24.0, 36.0}) {
    (void)patchy::sample_dab_variation(expected_dynamics, expected_rng, expected_context,
                                       dynamic_options.brush_size);
    CHECK(!patchy::paint_stationary_airbrush_dab(dynamic_document, dynamic_layer, x, 12.0,
                                                 dynamic_options, dynamic_state)
               .empty());
    ++expected_context.step_index;
    CHECK(dynamic_state.rng.state == expected_rng.state);
    CHECK(dynamic_state.dynamics.step_index == expected_context.step_index);
  }
  const auto& dynamic_pixels = std::as_const(dynamic_document).find_layer(dynamic_layer)->pixels();
  const auto first_alpha = dynamic_pixels.pixel(12, 12)[3];
  const auto second_alpha = dynamic_pixels.pixel(24, 12)[3];
  const auto third_alpha = dynamic_pixels.pixel(36, 12)[3];
  CHECK(first_alpha > second_alpha);
  CHECK(second_alpha > third_alpha);
  CHECK(third_alpha > 0U);
}

void tool_brush_tip_dynamics_carry_across_segments() {
  // With every dynamic active, one long segment and the same path chopped into short segments
  // must paint identical pixels: the RNG stream, fade step index, and stroke direction all live
  // in BrushTipStrokeState, not per-call.
  const auto tip = make_bar_brush_tip();
  const auto mips = patchy::build_brush_tip_mips(tip);
  const auto scaled = patchy::make_scaled_brush_tip(mips, 9);

  auto options = tool_options(0, 0, 0);
  options.brush_size = 9;
  options.brush_tip = &scaled;
  options.brush_tip_spacing = 0.5;  // dabs every 4.5px
  options.brush_dynamics.size_jitter = 0.5;
  options.brush_dynamics.minimum_diameter = 0.3;
  options.brush_dynamics.angle_jitter = 0.5;
  options.brush_dynamics.angle_control = patchy::BrushDynamicControl::Direction;
  options.brush_dynamics.roundness_jitter = 0.4;
  options.brush_dynamics.flip_x_jitter = true;
  options.brush_dynamics.flip_y_jitter = true;
  options.brush_dynamics.scatter = 0.5;
  options.brush_dynamics.scatter_both_axes = true;
  options.brush_dynamics.count = 2;
  options.brush_dynamics.count_jitter = 0.5;
  options.brush_dynamics.opacity_jitter = 0.5;
  options.brush_dynamics.flow_jitter = 0.5;
  options.brush_dynamics.seed = 424242;

  auto whole_document = make_tool_document();
  const auto whole_layer = active_tool_layer(whole_document);
  patchy::BrushTipStrokeState whole_state;
  CHECK(!patchy::paint_brush_segment(whole_document, whole_layer, 10.0, 20.0, 25.0, 20.0, options, false,
                                     whole_state)
             .empty());

  auto chopped_document = make_tool_document();
  const auto chopped_layer = active_tool_layer(chopped_document);
  patchy::BrushTipStrokeState chopped_state;
  CHECK(!patchy::paint_brush_segment(chopped_document, chopped_layer, 10.0, 20.0, 15.0, 20.0, options,
                                     false, chopped_state)
             .empty());
  auto chopped_dirty = patchy::paint_brush_segment(chopped_document, chopped_layer, 15.0, 20.0, 20.0, 20.0,
                                                   options, false, chopped_state);
  chopped_dirty = patchy::unite_rect(
      chopped_dirty, patchy::paint_brush_segment(chopped_document, chopped_layer, 20.0, 20.0, 25.0, 20.0,
                                                 options, false, chopped_state));
  CHECK(!chopped_dirty.empty());

  const auto& whole_pixels = whole_document.find_layer(whole_layer)->pixels();
  const auto& chopped_pixels = chopped_document.find_layer(chopped_layer)->pixels();
  for (std::int32_t y = 0; y < whole_pixels.height(); ++y) {
    for (std::int32_t x = 0; x < whole_pixels.width(); ++x) {
      CHECK(whole_pixels.pixel(x, y)[3] == chopped_pixels.pixel(x, y)[3]);
    }
  }
}

void brush_tip_softening_feathers_edges() {
  // A hard 16x16 block: after feathering, the stamp grows by the feather on each side, the
  // anchor shifts to match, and the edge becomes a gradient instead of a step.
  patchy::BrushTip tip;
  tip.width = 16;
  tip.height = 16;
  tip.mask.assign(256, 255);
  const auto mips = patchy::build_brush_tip_mips(tip);
  auto scaled = patchy::make_scaled_brush_tip(mips, 16);
  const auto hard_width = scaled.width;
  const auto hard_anchor = scaled.anchor_x;

  patchy::soften_scaled_brush_tip(scaled, 6);
  CHECK(scaled.width == hard_width + 12);
  CHECK(scaled.anchor_x == hard_anchor + 6.0);
  CHECK(scaled.mask.size() == static_cast<std::size_t>(scaled.width) * scaled.height);

  // Center stays essentially solid; the rim carries intermediate coverage.
  const auto at = [&scaled](int x, int y) {
    return scaled.mask[static_cast<std::size_t>(y) * scaled.width + x];
  };
  CHECK(at(scaled.width / 2, scaled.height / 2) > 240);
  int intermediate = 0;
  for (const auto value : scaled.mask) {
    if (value > 20 && value < 235) {
      ++intermediate;
    }
  }
  CHECK(intermediate > 100);

  // Zero feather is a no-op.
  auto untouched = patchy::make_scaled_brush_tip(mips, 16);
  patchy::soften_scaled_brush_tip(untouched, 0);
  CHECK(untouched.width == hard_width);
}

void tool_brush_tip_large_stamp_stroke_is_fast() {
  // A 500px tip painted across a wide canvas must stay interactive: 12 dabs at 25% spacing.
  patchy::BrushTip tip;
  tip.width = 500;
  tip.height = 500;
  tip.mask.assign(500U * 500U, 0);
  for (std::int32_t y = 0; y < 500; ++y) {
    for (std::int32_t x = 0; x < 500; ++x) {
      const auto dx = x - 250;
      const auto dy = y - 250;
      if (dx * dx + dy * dy <= 240 * 240) {
        tip.mask[static_cast<std::size_t>(y) * 500U + x] = 255;
      }
    }
  }

  patchy::Document document(1600, 1000, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(1600, 1000, 255, 255, 255));
  patchy::PixelBuffer pixels(1600, 1000, patchy::PixelFormat::rgba8());
  pixels.clear(0);
  const auto layer_id = document.add_pixel_layer("Paint", std::move(pixels)).id();

  const auto started = std::chrono::steady_clock::now();
  const auto mips = patchy::build_brush_tip_mips(tip);
  const auto scaled = patchy::make_scaled_brush_tip(mips, 500);
  auto options = tool_options(30, 60, 200);
  options.brush_size = 500;
  options.brush_tip = &scaled;
  options.brush_tip_spacing = 0.25;
  patchy::BrushTipStrokeState state;
  const auto dirty =
      patchy::paint_brush_segment(document, layer_id, 250.0, 500.0, 1450.0, 500.0, options, false, state);
  const auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();

  CHECK(!dirty.empty());
  CHECK(elapsed_ms < 1000);
  const auto* layer = document.find_layer(layer_id);
  CHECK(layer != nullptr);
  const auto* center = layer->pixels().pixel(800 - layer->bounds().x, 500 - layer->bounds().y);
  CHECK(center[3] == 255);
  write_bmp_artifact("tool_brush_tip_large_stroke", document);
}

void tool_brush_tip_erases_and_respects_gates() {
  const auto square = [] {
    patchy::BrushTip tip;
    tip.width = 4;
    tip.height = 4;
    tip.mask.assign(16, 255);
    return tip;
  }();
  const auto mips = patchy::build_brush_tip_mips(square);
  const auto scaled = patchy::make_scaled_brush_tip(mips, 4);

  patchy::Document document(64, 48, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(64, 48, 255, 255, 255));
  const auto layer_id = document.add_pixel_layer("Paint", solid_rgba(64, 48, 10, 20, 30, 255)).id();

  auto options = tool_options(0, 0, 0);
  options.brush_size = 4;
  options.brush_tip = &scaled;
  options.primary.a = 255;
  CHECK(!patchy::paint_brush(document, layer_id, 20, 20, options, true).empty());
  const auto& pixels = document.find_layer(layer_id)->pixels();
  CHECK(pixels.pixel(20, 20)[3] == 0);   // erased under the stamp
  CHECK(pixels.pixel(26, 20)[3] == 255); // untouched outside

  // stroke_pixel_gate blocks the left half of a fresh stamp.
  auto gated_document = make_tool_document();
  const auto gated_layer = active_tool_layer(gated_document);
  auto gated_options = tool_options(200, 10, 10);
  gated_options.brush_size = 4;
  gated_options.brush_tip = &scaled;
  gated_options.stroke_pixel_gate = [](std::int32_t x, std::int32_t) { return x >= 24; };
  CHECK(!patchy::paint_brush(gated_document, gated_layer, 24, 24, gated_options, false).empty());
  const auto& gated_pixels = gated_document.find_layer(gated_layer)->pixels();
  CHECK(gated_pixels.pixel(23, 24)[3] == 0);
  CHECK(gated_pixels.pixel(24, 24)[3] == 255);
}

}  // namespace

std::vector<patchy::test::TestCase> brush_engine_tests() {
  return {
      {"tool_brush_draws_color_and_writes_artifact", tool_brush_draws_color_and_writes_artifact},
      {"tool_brush_opacity_and_bounded_layer_expansion_work",
       tool_brush_opacity_and_bounded_layer_expansion_work},
      {"tool_brush_softness_feathers_edge_alpha", tool_brush_softness_feathers_edge_alpha},
      {"tool_brush_repaints_translucent_pixels_without_color_halo",
       tool_brush_repaints_translucent_pixels_without_color_halo},
      {"brush_tip_mips_and_scaling_preserve_shape", brush_tip_mips_and_scaling_preserve_shape},
      {"tool_brush_tip_stamps_bitmap_mask", tool_brush_tip_stamps_bitmap_mask},
      {"tool_brush_tip_rotation_and_roundness_transform_stamp",
       tool_brush_tip_rotation_and_roundness_transform_stamp},
      {"tool_brush_tip_segment_spacing_carries_across_segments",
       tool_brush_tip_segment_spacing_carries_across_segments},
      {"brush_dynamics_rng_sequence_is_stable", brush_dynamics_rng_sequence_is_stable},
      {"brush_dynamics_activation_gates_fields", brush_dynamics_activation_gates_fields},
      {"brush_dynamics_control_values", brush_dynamics_control_values},
      {"tool_brush_tip_controls_at_full_value_change_nothing",
       tool_brush_tip_controls_at_full_value_change_nothing},
      {"tool_brush_tip_size_control_pressure_scales_dabs", tool_brush_tip_size_control_pressure_scales_dabs},
      {"tool_brush_tip_opacity_control_respects_minimum", tool_brush_tip_opacity_control_respects_minimum},
      {"tool_brush_tip_inactive_dynamics_change_nothing", tool_brush_tip_inactive_dynamics_change_nothing},
      {"tool_brush_texture_and_dual_brush_render_deterministically",
       tool_brush_texture_and_dual_brush_render_deterministically},
      {"mixer_brush_color_uses_one_stroke_start_sample_and_distance_decay",
       mixer_brush_color_uses_one_stroke_start_sample_and_distance_decay},
      {"tool_brush_color_dynamics_varies_selected_colors_only",
       tool_brush_color_dynamics_varies_selected_colors_only},
      {"tool_brush_effect_pixels_round_trip_exactly_through_psd",
       tool_brush_effect_pixels_round_trip_exactly_through_psd},
      {"tool_brush_tip_size_jitter_shrinks_dabs_deterministically",
       tool_brush_tip_size_jitter_shrinks_dabs_deterministically},
      {"tool_brush_tip_angle_direction_follows_stroke", tool_brush_tip_angle_direction_follows_stroke},
      {"tool_brush_tip_angle_fade_and_jitter_rotate_dabs", tool_brush_tip_angle_fade_and_jitter_rotate_dabs},
      {"tool_brush_tip_flip_jitter_mirrors_stamp", tool_brush_tip_flip_jitter_mirrors_stamp},
      {"tool_brush_tip_scatter_offsets_perpendicular_to_stroke",
       tool_brush_tip_scatter_offsets_perpendicular_to_stroke},
      {"tool_brush_tip_count_stamps_multiple_dabs_per_step", tool_brush_tip_count_stamps_multiple_dabs_per_step},
      {"tool_brush_tip_opacity_jitter_varies_dab_alpha", tool_brush_tip_opacity_jitter_varies_dab_alpha},
      {"tool_brush_tip_flow_jitter_varies_dab_alpha", tool_brush_tip_flow_jitter_varies_dab_alpha},
      {"tool_stationary_airbrush_dynamics_advance_without_scatter_bursts",
       tool_stationary_airbrush_dynamics_advance_without_scatter_bursts},
      {"tool_brush_tip_dynamics_carry_across_segments", tool_brush_tip_dynamics_carry_across_segments},
      {"tool_brush_tip_erases_and_respects_gates", tool_brush_tip_erases_and_respects_gates},
      {"brush_tip_softening_feathers_edges", brush_tip_softening_feathers_edges},
      {"tool_brush_tip_large_stamp_stroke_is_fast", tool_brush_tip_large_stamp_stroke_is_fast},
  };
}
