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

using patchy::test::rgb_diff_metrics;
using patchy::test::solid_rgb;
using patchy::test::solid_rgba;

void gradient_midpoints_remap_color_and_alpha_segments() {
  patchy::LayerStyleGradient identity;
  identity.color_stops = {{0.0F, patchy::RgbColor{0, 0, 0}},
                          {1.0F, patchy::RgbColor{255, 255, 255}}};
  identity.alpha_stops = {{0.0F, 0.0F}, {1.0F, 1.0F}};

  const auto identity_quarter = patchy::gradient_color(identity, 0.25F);
  CHECK(identity_quarter.red == 64U);
  CHECK(identity_quarter.green == 64U);
  CHECK(identity_quarter.blue == 64U);
  CHECK(std::abs(patchy::gradient_stop_opacity(identity, 0.25F) - 0.25F) < 1.0e-6F);

  auto shifted = identity;
  // Midpoints ride the destination stop. Values on the first stop are unused;
  // the right stop says where this segment reaches its 50% blend.
  shifted.color_stops.front().midpoint = 0.01F;
  shifted.color_stops.back().midpoint = 0.25F;
  shifted.alpha_stops.front().midpoint = 0.99F;
  shifted.alpha_stops.back().midpoint = 0.75F;

  const auto color_at_midpoint = patchy::gradient_color(shifted, 0.25F);
  CHECK(color_at_midpoint.red == 128U);
  CHECK(color_at_midpoint.green == 128U);
  CHECK(color_at_midpoint.blue == 128U);
  CHECK(std::abs(patchy::gradient_stop_opacity(shifted, 0.75F) - 0.5F) < 1.0e-6F);

  // Both halves remain linear around the moved midpoint and still pin their
  // endpoints. These values catch accidentally treating Mdpn as a global
  // location or attaching it to the source stop.
  CHECK(patchy::gradient_color(shifted, 0.125F).red == 64U);
  CHECK(patchy::gradient_color(shifted, 0.625F).red == 191U);
  CHECK(std::abs(patchy::gradient_stop_opacity(shifted, 0.375F) - 0.25F) < 1.0e-6F);
  CHECK(std::abs(patchy::gradient_stop_opacity(shifted, 0.875F) - 0.75F) < 1.0e-6F);
  CHECK(patchy::gradient_color(shifted, 0.0F).red == 0U);
  CHECK(patchy::gradient_color(shifted, 1.0F).red == 255U);
}

void gradient_presets_have_stable_ids_and_recipes() {
  const auto presets = patchy::builtin_gradient_presets();
  CHECK(presets.size() == 20U);
  std::unordered_set<std::string> ids;
  for (std::size_t index = 0; index < presets.size(); ++index) {
    const auto& preset = presets[index];
    CHECK(ids.insert(preset.id).second);
    CHECK(preset.introduced_version == 1);
    CHECK(preset.definition.smoothness == 4096U);
    CHECK(preset.definition.color_stops.size() >= 2U);
    CHECK(preset.definition.alpha_stops.size() >= 2U);
    CHECK(std::string(preset.id).ends_with(std::to_string(200000000001ULL + index)));
  }
  CHECK(presets.front().definition.color_stops.front().kind == patchy::GradientColorStop::Kind::Foreground);
  CHECK(presets.front().definition.color_stops.back().kind == patchy::GradientColorStop::Kind::Background);
  CHECK(std::string(presets[5].english_folder) == "Photo Toning");
  CHECK(std::string(presets[10].english_folder) == "Light & Atmosphere");
  CHECK(std::string(presets[15].english_folder) == "Illustration");
  CHECK(presets[18].definition.color_stops[1].location == 0.49F);
  CHECK(presets[18].definition.color_stops[2].location == 0.50F);
}

void grd_v5_round_trips_solid_dynamic_noise_and_hierarchy() {
  std::vector<patchy::psd::GrdGradient> source;
  source.push_back({"Dynamic", "Essentials/Live", patchy::builtin_gradient_presets().front().definition});
  patchy::GradientDefinition noise;
  noise.name = "Noise";
  noise.form = patchy::GradientDefinitionForm::Noise;
  noise.noise.seed = 0x12345678U;
  noise.noise.roughness = 1377U;
  noise.noise.add_transparency = true;
  noise.noise.restrict_colors = false;
  noise.noise.color_model = patchy::GradientNoiseColorModel::Lab;
  noise.noise.minimum = {3, 7, 11, 13};
  noise.noise.maximum = {91, 87, 83, 79};
  source.push_back({"Noise", "Photo Toning", noise});
  auto solid = patchy::builtin_gradient_presets()[18].definition;
  solid.smoothness = 2048U;
  source.push_back({"Hard", "Illustration", solid});

  const auto bytes = patchy::psd::write_grd(source);
  CHECK(bytes.size() > 64U);
  CHECK(bytes == patchy::psd::write_grd(source));
  CHECK(std::search(bytes.begin(), bytes.end(), std::begin("8BIMphry"), std::end("8BIMphry") - 1) != bytes.end());
  std::string error;
  const auto decoded = patchy::psd::read_grd(bytes, error);
  CHECK(decoded.has_value());
  CHECK(error.empty());
  CHECK(decoded->warnings.empty());
  CHECK(decoded->gradients.size() == source.size());
  CHECK(decoded->gradients[0].folder == "Essentials/Live");
  CHECK(decoded->gradients[1].folder == "Photo Toning");
  CHECK(decoded->gradients[2].folder == "Illustration");
  CHECK(decoded->gradients[0].definition.color_stops.front().kind == patchy::GradientColorStop::Kind::Foreground);
  CHECK(decoded->gradients[0].definition.color_stops.back().kind == patchy::GradientColorStop::Kind::Background);
  CHECK(decoded->gradients[1].definition.form == patchy::GradientDefinitionForm::Noise);
  CHECK(decoded->gradients[1].definition.noise.seed == noise.noise.seed);
  CHECK(decoded->gradients[1].definition.noise.roughness == noise.noise.roughness);
  CHECK(decoded->gradients[1].definition.noise.color_model == patchy::GradientNoiseColorModel::Lab);
  CHECK(decoded->gradients[1].definition.noise.minimum == noise.noise.minimum);
  CHECK(decoded->gradients[2].definition.smoothness == 2048U);

  auto truncated = bytes;
  truncated.resize(bytes.size() - 24U);
  const auto recovered = patchy::psd::read_grd(truncated, error);
  CHECK(recovered.has_value());
  CHECK(recovered->gradients.size() == source.size());
}

void gradient_methods_noise_dither_and_geometry_are_deterministic() {
  const auto same_color = [](patchy::RgbColor lhs, patchy::RgbColor rhs) {
    return lhs.red == rhs.red && lhs.green == rhs.green && lhs.blue == rhs.blue;
  };
  patchy::LayerStyleGradient gradient;
  gradient.color_stops = {{0.0F, patchy::RgbColor{255, 0, 0}}, {0.45F, patchy::RgbColor{0, 255, 0}},
                          {1.0F, patchy::RgbColor{0, 0, 255}}};
  gradient.alpha_stops = {{0.0F, 1.0F}, {1.0F, 1.0F}};
  gradient.interpolation = patchy::GradientInterpolationMethod::Classic;
  const auto classic = patchy::gradient_color(gradient, 0.25F);
  gradient.interpolation = patchy::GradientInterpolationMethod::Perceptual;
  const auto perceptual = patchy::gradient_color(gradient, 0.25F);
  gradient.interpolation = patchy::GradientInterpolationMethod::Linear;
  const auto linear = patchy::gradient_color(gradient, 0.25F);
  CHECK(!same_color(classic, perceptual));
  CHECK(!same_color(perceptual, linear));
  CHECK(!same_color(classic, linear));

  gradient.form = patchy::GradientDefinitionForm::Noise;
  gradient.noise.seed = 991827U;
  gradient.noise.roughness = 3171U;
  gradient.noise.add_transparency = true;
  gradient.noise.color_model = patchy::GradientNoiseColorModel::HSB;
  const auto noise_color = patchy::gradient_color(gradient, 0.371F);
  CHECK(same_color(noise_color, patchy::gradient_color(gradient, 0.371F)));
  CHECK(std::abs(patchy::gradient_stop_opacity(gradient, 0.371F) -
                 patchy::gradient_stop_opacity(gradient, 0.371F)) < 1.0e-7F);
  gradient.dither = true;
  CHECK(same_color(patchy::gradient_color_dithered(gradient, 0.371F, 17, 29),
                   patchy::gradient_color_dithered(gradient, 0.371F, 17, 29)));

  const patchy::Rect bounds{10, 20, 100, 80};
  for (const auto type : {patchy::LayerStyleGradientType::Linear, patchy::LayerStyleGradientType::Radial,
                          patchy::LayerStyleGradientType::Angle, patchy::LayerStyleGradientType::Reflected,
                          patchy::LayerStyleGradientType::Diamond}) {
    gradient.type = type;
    gradient.angle_degrees = 37.0F;
    gradient.scale = 0.73F;
    gradient.offset_x_percent = 18.0F;
    gradient.offset_y_percent = -11.0F;
    const auto position = patchy::gradient_position(gradient, bounds, 73, 49);
    CHECK(std::isfinite(position));
    CHECK(position >= 0.0F);
    CHECK(position <= 1.0F);
  }
}

void gradient_linear_geometry_uses_angle_projected_layer_span() {
  patchy::LayerStyleGradient gradient;
  gradient.type = patchy::LayerStyleGradientType::Linear;
  gradient.scale = 1.0F;
  const patchy::Rect wide_bounds{20, 30, 1000, 100};

  gradient.angle_degrees = 90.0F;
  const auto vertical_top = patchy::gradient_position(gradient, wide_bounds, 520, 30);
  const auto vertical_bottom = patchy::gradient_position(gradient, wide_bounds, 520, 129);
  CHECK(vertical_top > 0.99F);
  CHECK(vertical_bottom < 0.01F);

  gradient.angle_degrees = 0.0F;
  const auto horizontal_left = patchy::gradient_position(gradient, wide_bounds, 20, 80);
  const auto horizontal_right = patchy::gradient_position(gradient, wide_bounds, 1019, 80);
  CHECK(horizontal_left < 0.01F);
  CHECK(horizontal_right > 0.99F);

  gradient.angle_degrees = 45.0F;
  CHECK(patchy::gradient_position(gradient, wide_bounds, 20, 129) < 0.01F);
  CHECK(patchy::gradient_position(gradient, wide_bounds, 1019, 30) > 0.99F);

  gradient.type = patchy::LayerStyleGradientType::Reflected;
  gradient.angle_degrees = 90.0F;
  CHECK(patchy::gradient_position(gradient, wide_bounds, 520, 79) < 0.02F);
  CHECK(patchy::gradient_position(gradient, wide_bounds, 520, 30) > 0.98F);
}

void compositor_aligned_gradients_use_visible_alpha_bounds() {
  const auto gradient = [] {
    patchy::LayerStyleGradient value;
    value.type = patchy::LayerStyleGradientType::Linear;
    value.angle_degrees = 90.0F;
    value.align_with_layer = true;
    value.color_stops = {{0.0F, patchy::RgbColor{0, 0, 0}},
                         {1.0F, patchy::RgbColor{255, 255, 255}}};
    return value;
  }();

  const auto make_layer = [](patchy::Document& document) -> patchy::Layer& {
    auto pixels = solid_rgba(160, 160, 0, 0, 0, 0);
    for (std::int32_t y = 50; y < 110; ++y) {
      for (std::int32_t x = 40; x < 120; ++x) {
        auto* pixel = pixels.pixel(x, y);
        pixel[0] = 100;
        pixel[1] = 100;
        pixel[2] = 100;
        pixel[3] = 255;
      }
    }
    return document.add_pixel_layer("Padded", std::move(pixels));
  };

  patchy::Document overlay_document(160, 160, patchy::PixelFormat::rgb8());
  overlay_document.add_pixel_layer("Base", solid_rgb(160, 160, 10, 120, 200));
  auto& overlay_layer = make_layer(overlay_document);
  patchy::LayerGradientFill fill;
  fill.enabled = true;
  fill.blend_mode = patchy::BlendMode::Normal;
  fill.opacity = 1.0F;
  fill.gradient = gradient;
  overlay_layer.layer_style().gradient_fills.push_back(fill);

  const auto overlay = patchy::Compositor{}.flatten_rgb8(overlay_document);
  const auto overlay_top = overlay.pixel(80, 50)[0];
  const auto overlay_bottom = overlay.pixel(80, 109)[0];
  CHECK(overlay_top > 240U);
  CHECK(overlay_bottom < 15U);
  CHECK(static_cast<int>(overlay_top) - static_cast<int>(overlay_bottom) > 220);

  patchy::Document stroke_document(160, 160, patchy::PixelFormat::rgb8());
  stroke_document.add_pixel_layer("Base", solid_rgb(160, 160, 10, 120, 200));
  auto& stroke_layer = make_layer(stroke_document);
  patchy::LayerStroke stroke;
  stroke.enabled = true;
  stroke.blend_mode = patchy::BlendMode::Normal;
  stroke.opacity = 1.0F;
  stroke.size = 4.0F;
  stroke.position = patchy::LayerStrokePosition::Inside;
  stroke.uses_gradient = true;
  stroke.gradient = gradient;
  stroke_layer.layer_style().strokes.push_back(stroke);

  const auto stroked = patchy::Compositor{}.flatten_rgb8(stroke_document);
  const auto stroke_top = stroked.pixel(80, 50)[0];
  const auto stroke_bottom = stroked.pixel(80, 109)[0];
  CHECK(stroke_top > 240U);
  CHECK(stroke_bottom < 15U);
  CHECK(static_cast<int>(stroke_top) - static_cast<int>(stroke_bottom) > 220);
}

void psd_bevel_examine_classic_gradient_matches_photoshop_if_available() {
  const auto psd_path = patchy::test::local_psd_fixture_path("bevel_examine.psd");
  const auto bmp_path = patchy::test::local_psd_fixture_path("bevel_examine_photoshop.bmp");
  if (!std::filesystem::exists(psd_path)) return;
  const auto document = patchy::psd::DocumentIo::read_file(psd_path);
  const patchy::LayerStyleGradient* imported_gradient = nullptr;
  std::function<void(const std::vector<patchy::Layer>&)> find_gradient = [&](const auto& layers) {
    for (const auto& layer : layers) {
      if (!layer.layer_style().gradient_fills.empty() && imported_gradient == nullptr) {
        imported_gradient = &layer.layer_style().gradient_fills.front().gradient;
      }
      find_gradient(layer.children());
    }
  };
  find_gradient(document.layers());
  CHECK(imported_gradient != nullptr);
  CHECK(imported_gradient->smoothness == 4096U);
  CHECK(imported_gradient->interpolation == patchy::GradientInterpolationMethod::Classic);
  CHECK(std::abs(imported_gradient->angle_degrees - 90.0F) < 0.01F);
  CHECK(imported_gradient->align_with_layer);
  CHECK(imported_gradient->type == patchy::LayerStyleGradientType::Linear);

  if (!std::filesystem::exists(bmp_path)) return;
  const auto reference_document = patchy::bmp::DocumentIo::read_file(bmp_path);
  const auto reference = patchy::Compositor{}.flatten_rgb8(reference_document);
  const auto actual = patchy::Compositor{}.flatten_rgb8(document);
  std::filesystem::create_directories("test-artifacts");
  patchy::Document actual_document(document.width(), document.height(), patchy::PixelFormat::rgb8());
  actual_document.add_pixel_layer("Background", solid_rgb(document.width(), document.height(), 255, 255, 255));
  actual_document.add_pixel_layer("Patchy", actual);
  patchy::bmp::DocumentIo::write_file(
      actual_document, std::filesystem::path("test-artifacts") / "bevel_examine_patchy.bmp",
      patchy::bmp::WriteOptions{patchy::bmp::BmpEncoding::Rgb24, patchy::bmp::BmpPaletteMode::Exact, true});
  const auto metrics = rgb_diff_metrics(reference, actual);
  std::uint64_t styled_delta_sum = 0;
  std::size_t styled_channel_count = 0;
  for (std::int32_t y = 0; y < reference.height(); ++y) {
    for (std::int32_t x = 0; x < reference.width(); ++x) {
      const auto* expected = reference.pixel(x, y);
      if (expected[0] >= 250 && expected[1] >= 250 && expected[2] >= 250) continue;
      const auto* rendered = actual.pixel(x, y);
      for (int channel = 0; channel < 3; ++channel) {
        const auto delta = static_cast<std::uint8_t>(
            std::abs(static_cast<int>(expected[channel]) - static_cast<int>(rendered[channel])));
        styled_delta_sum += delta;
        ++styled_channel_count;
      }
    }
  }
  const auto styled_mean_delta = static_cast<double>(styled_delta_sum) /
                                 static_cast<double>(std::max<std::size_t>(1, styled_channel_count));
  std::cout << "  bevel_examine PS diff: max " << metrics.max_channel_delta
            << ", mean " << metrics.mean_abs_channel_delta << ", styled mean " << styled_mean_delta << '\n';
  CHECK(metrics.mean_abs_channel_delta <= 1.0);
  CHECK(styled_mean_delta <= 20.0);
}

void compositor_renders_photoshop_style_satin() {
  const auto render = [](patchy::LayerSatin satin, patchy::RgbColor source_color,
                         patchy::RgbColor background_color, patchy::Rect bounds) {
    patchy::Document document(96, 96, patchy::PixelFormat::rgb8());
    document.add_pixel_layer(
        "Background", solid_rgb(96, 96, background_color.red, background_color.green, background_color.blue));
    patchy::Layer layer(document.allocate_layer_id(), "Satin",
                        solid_rgba(bounds.width, bounds.height, source_color.red, source_color.green,
                                   source_color.blue, 255));
    auto& source = document.add_layer(std::move(layer));
    source.set_bounds(bounds);
    source.layer_style().satins.push_back(satin);
    return patchy::Compositor{}.flatten_rgb8(document);
  };
  const auto near_byte = [](std::uint8_t actual, int expected, int tolerance) {
    CHECK(std::abs(static_cast<int>(actual) - expected) <= tolerance);
  };
  const auto near_float = [](float actual, float expected) {
    CHECK(std::abs(actual - expected) <= 1.0e-6F);
  };

  // Satin uses Photoshop's exact separable tent. A vertical impulse is
  // constant through the vertical pass at its center, exposing the horizontal
  // [1,2,3,2,1] / 9 profile directly.
  std::vector<float> impulse(9U * 9U, 0.0F);
  for (int y = 0; y < 9; ++y) {
    impulse[static_cast<std::size_t>(y * 9 + 4)] = 1.0F;
  }
  patchy::render_detail::blur_satin_tent_mask_in_place(impulse, 9, 9, 3.0F);
  const std::array<float, 9> expected_size_three{0.0F, 0.0F, 1.0F / 9.0F, 2.0F / 9.0F, 3.0F / 9.0F,
                                                 2.0F / 9.0F, 1.0F / 9.0F, 0.0F, 0.0F};
  for (int x = 0; x < 9; ++x) {
    near_float(impulse[static_cast<std::size_t>(4 * 9 + x)], expected_size_three[static_cast<std::size_t>(x)]);
  }
  std::fill(impulse.begin(), impulse.end(), 0.0F);
  for (int x = 0; x < 9; ++x) {
    impulse[static_cast<std::size_t>(4 * 9 + x)] = 1.0F;
  }
  patchy::render_detail::blur_satin_tent_mask_in_place(impulse, 9, 9, 3.0F);
  for (int y = 0; y < 9; ++y) {
    near_float(impulse[static_cast<std::size_t>(y * 9 + 4)], expected_size_three[static_cast<std::size_t>(y)]);
  }
  auto size_zero = std::vector<float>{-1.0F, -0.25F, 0.5F, 1.0F};
  const auto size_zero_before = size_zero;
  patchy::render_detail::blur_satin_tent_mask_in_place(size_zero, 2, 2, 0.0F);
  CHECK(size_zero == size_zero_before);

  std::fill(impulse.begin(), impulse.end(), 0.0F);
  for (int y = 0; y < 9; ++y) {
    impulse[static_cast<std::size_t>(y * 9 + 4)] = 1.0F;
  }
  patchy::render_detail::blur_satin_tent_mask_in_place(impulse, 9, 9, 1.0F);
  near_float(impulse[static_cast<std::size_t>(4 * 9 + 3)], 0.25F);
  near_float(impulse[static_cast<std::size_t>(4 * 9 + 4)], 0.5F);
  near_float(impulse[static_cast<std::size_t>(4 * 9 + 5)], 0.25F);

  patchy::LayerSatin satin;
  satin.enabled = true;
  satin.blend_mode = patchy::BlendMode::Normal;
  satin.color = patchy::RgbColor{0, 0, 0};
  satin.opacity = 1.0F;
  satin.angle_degrees = 0.0F;
  satin.distance = 10.0F;
  satin.size = 4.0F;
  satin.invert = false;
  constexpr patchy::Rect kBounds{20, 20, 56, 56};
  const auto horizontal = render(satin, {128, 128, 128}, {0, 0, 0}, kBounds);

  // Exact Photoshop 2026 Size 4 transition over straight RGB 128. The signed
  // band ends at x=29, then the [1,2,3,4,3,2,1] / 16 tent falls to zero.
  const std::array<std::uint8_t, 7> expected_transition{8U, 24U, 48U, 80U, 104U, 120U, 128U};
  for (int index = 0; index < static_cast<int>(expected_transition.size()); ++index) {
    CHECK(horizontal.pixel(27 + index, 48)[0] == expected_transition[static_cast<std::size_t>(index)]);
  }
  CHECK(horizontal.pixel(20, 48)[0] == 0U);
  CHECK(horizontal.pixel(48, 48)[0] == 128U);
  CHECK(horizontal.pixel(48, 20)[0] == 128U);
  // Satin is an interior effect: non-zero layer placement must not leak into
  // the one-pixel exterior around the layer.
  CHECK(horizontal.pixel(19, 48)[0] == 0U);
  CHECK(horizontal.pixel(76, 48)[0] == 0U);

  auto vertical_settings = satin;
  vertical_settings.angle_degrees = 90.0F;
  const auto vertical = render(vertical_settings, {128, 128, 128}, {0, 0, 0}, kBounds);
  CHECK(vertical.pixel(48, 20)[0] == 0U);
  CHECK(vertical.pixel(48, 48)[0] == 128U);
  CHECK(vertical.pixel(20, 48)[0] == 128U);

  auto oblique_settings = satin;
  oblique_settings.angle_degrees = 30.0F;
  oblique_settings.size = 0.0F;
  const auto oblique = render(oblique_settings, {128, 128, 128}, {0, 0, 0}, kBounds);
  // Photoshop resolves d=10 at 30 degrees to integral components |v|=(9,5).
  CHECK(oblique.pixel(28, 48)[0] == 0U);
  CHECK(oblique.pixel(29, 48)[0] == 128U);
  CHECK(oblique.pixel(48, 24)[0] == 0U);
  CHECK(oblique.pixel(48, 25)[0] == 128U);

  auto inverted_settings = satin;
  inverted_settings.invert = true;
  const auto inverted = render(inverted_settings, {128, 128, 128}, {0, 0, 0}, kBounds);
  for (const auto x : {20, 27, 28, 29, 30, 31, 32, 33, 48}) {
    const auto sum = static_cast<int>(horizontal.pixel(x, 48)[0]) +
                     static_cast<int>(inverted.pixel(x, 48)[0]);
    CHECK(std::abs(sum - 128) <= 1);
  }

  auto short_distance = satin;
  short_distance.distance = 2.0F;
  const auto short_distance_render = render(short_distance, {128, 128, 128}, {0, 0, 0}, kBounds);
  CHECK(horizontal.pixel(30, 48)[0] + 40 < short_distance_render.pixel(30, 48)[0]);

  auto hard_settings = satin;
  hard_settings.distance = 5.0F;
  hard_settings.size = 0.0F;
  const auto hard = render(hard_settings, {128, 128, 128}, {0, 0, 0}, kBounds);
  auto zero_distance_settings = hard_settings;
  zero_distance_settings.distance = 0.0F;
  auto one_distance_settings = zero_distance_settings;
  one_distance_settings.distance = 1.0F;
  const auto zero_distance = render(zero_distance_settings, {128, 128, 128}, {0, 0, 0}, kBounds);
  const auto one_distance = render(one_distance_settings, {128, 128, 128}, {0, 0, 0}, kBounds);
  CHECK(zero_distance.data().size() == one_distance.data().size());
  CHECK(std::equal(zero_distance.data().begin(), zero_distance.data().end(), one_distance.data().begin()));
  CHECK(zero_distance.pixel(20, 48)[0] == 0U);
  CHECK(zero_distance.pixel(21, 48)[0] == 128U);
  CHECK(zero_distance.pixel(74, 48)[0] == 128U);
  CHECK(zero_distance.pixel(75, 48)[0] == 0U);
  CHECK(zero_distance.pixel(48, 48)[0] == 128U);
  auto soft_settings = hard_settings;
  soft_settings.size = 10.0F;
  const auto soft = render(soft_settings, {128, 128, 128}, {0, 0, 0}, kBounds);
  CHECK(soft.pixel(27, 48)[0] + 20 < hard.pixel(27, 48)[0]);

  auto half_opacity = satin;
  half_opacity.opacity = 0.5F;
  const auto half = render(half_opacity, {128, 128, 128}, {0, 0, 0}, kBounds);
  near_byte(half.pixel(20, 48)[0], 64, 1);

  auto colored = satin;
  colored.color = patchy::RgbColor{240, 40, 20};
  colored.blend_mode = patchy::BlendMode::Normal;
  const auto normal = render(colored, {120, 120, 120}, {0, 0, 0}, kBounds);
  colored.blend_mode = patchy::BlendMode::Multiply;
  const auto multiply = render(colored, {120, 120, 120}, {0, 0, 0}, kBounds);
  colored.blend_mode = patchy::BlendMode::Screen;
  const auto screen = render(colored, {120, 120, 120}, {0, 0, 0}, kBounds);
  const auto* normal_edge = normal.pixel(20, 48);
  const auto* multiply_edge = multiply.pixel(20, 48);
  const auto* screen_edge = screen.pixel(20, 48);
  CHECK(multiply_edge[0] < normal_edge[0]);
  CHECK(multiply_edge[1] < normal_edge[1]);
  CHECK(screen_edge[0] > normal_edge[0]);
  CHECK(screen_edge[1] > normal_edge[1]);

  auto disabled = satin;
  disabled.enabled = false;
  const auto unchanged = render(disabled, {37, 57, 77}, {3, 5, 7}, kBounds);
  const auto* unchanged_inside = unchanged.pixel(48, 48);
  CHECK(unchanged_inside[0] == 37U);
  CHECK(unchanged_inside[1] == 57U);
  CHECK(unchanged_inside[2] == 77U);

  patchy::Document half_alpha_document(96, 96, patchy::PixelFormat::rgba8());
  patchy::Layer half_alpha_layer(half_alpha_document.allocate_layer_id(), "Half-alpha Satin",
                                 solid_rgba(56, 56, 128, 128, 128, 128));
  auto& half_alpha_source = half_alpha_document.add_layer(std::move(half_alpha_layer));
  half_alpha_source.set_bounds(kBounds);
  auto half_alpha_satin = satin;
  half_alpha_satin.size = 0.0F;
  half_alpha_source.layer_style().satins.push_back(half_alpha_satin);
  const auto half_alpha_result = patchy::flatten_document_rgba8(half_alpha_document);
  const auto* half_alpha_band = half_alpha_result.pixel(20, 48);
  CHECK(half_alpha_band[0] == 64U);
  CHECK(half_alpha_band[1] == 64U);
  CHECK(half_alpha_band[2] == 64U);
  CHECK(half_alpha_band[3] == 128U);
  const auto* half_alpha_center = half_alpha_result.pixel(48, 48);
  CHECK(half_alpha_center[0] == 128U);
  CHECK(half_alpha_center[1] == 128U);
  CHECK(half_alpha_center[2] == 128U);
  CHECK(half_alpha_center[3] == 128U);

  patchy::Document half_alpha_backdrop_document(96, 96, patchy::PixelFormat::rgb8());
  half_alpha_backdrop_document.add_pixel_layer("White", solid_rgb(96, 96, 255, 255, 255));
  patchy::Layer half_alpha_backdrop_layer(half_alpha_backdrop_document.allocate_layer_id(), "Half-alpha Satin",
                                          solid_rgba(56, 56, 128, 128, 128, 128));
  auto& half_alpha_backdrop_source = half_alpha_backdrop_document.add_layer(std::move(half_alpha_backdrop_layer));
  half_alpha_backdrop_source.set_bounds(kBounds);
  half_alpha_backdrop_source.layer_style().satins.push_back(half_alpha_satin);
  const auto half_alpha_backdrop_result = patchy::Compositor{}.flatten_rgb8(half_alpha_backdrop_document);
  CHECK(half_alpha_backdrop_result.pixel(20, 48)[0] == 159U);
  CHECK(half_alpha_backdrop_result.pixel(48, 48)[0] == 191U);

  patchy::Document masked_document(96, 96, patchy::PixelFormat::rgb8());
  masked_document.add_pixel_layer("Background", solid_rgb(96, 96, 0, 0, 0));
  patchy::Layer masked_layer(masked_document.allocate_layer_id(), "Masked Satin",
                             solid_rgba(56, 56, 0, 0, 0, 255));
  auto& masked_source = masked_document.add_layer(std::move(masked_layer));
  masked_source.set_bounds(kBounds);
  patchy::PixelBuffer mask_pixels(28, 56, patchy::PixelFormat::gray8());
  std::fill(mask_pixels.data().begin(), mask_pixels.data().end(), std::uint8_t{255});
  masked_source.set_mask(patchy::LayerMask{patchy::Rect{20, 20, 28, 56}, std::move(mask_pixels), 0, false});
  auto masked_satin = satin;
  masked_satin.color = patchy::RgbColor{255, 255, 255};
  masked_source.layer_style().satins.push_back(masked_satin);
  const auto masked = patchy::Compositor{}.flatten_rgb8(masked_document);
  // The mask reshapes both the Satin matte and its final interior clip. Its
  // right edge becomes a new lobe, with no effect leaking into the hidden half.
  CHECK(masked.pixel(47, 48)[0] > 240U);
  CHECK(masked.pixel(48, 48)[0] == 0U);
}

void compositor_renders_drop_shadow_spread() {
  auto make_document = [](float spread) {
    patchy::Document document(56, 48, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Background", solid_rgb(56, 48, 0, 0, 0));
    patchy::Layer layer(document.allocate_layer_id(), "Source", solid_rgba(4, 4, 0, 0, 0, 255));
    auto& source = document.add_layer(std::move(layer));
    source.set_bounds(patchy::Rect{26, 22, 4, 4});

    patchy::LayerDropShadow shadow;
    shadow.enabled = true;
    shadow.blend_mode = patchy::BlendMode::Normal;
    shadow.color = patchy::RgbColor{255, 255, 255};
    shadow.opacity = 1.0F;
    shadow.angle_degrees = 90.0F;
    shadow.distance = 0.0F;
    shadow.size = 8.0F;
    shadow.spread = spread;
    source.layer_style().drop_shadows.push_back(shadow);
    return document;
  };

  const auto no_spread = patchy::Compositor{}.flatten_rgb8(make_document(0.0F));
  const auto spread = patchy::Compositor{}.flatten_rgb8(make_document(100.0F));
  const auto* no_spread_px = no_spread.pixel(18, 23);
  const auto* spread_px = spread.pixel(18, 23);
  CHECK(spread_px[0] > 15);
  CHECK(spread_px[0] > no_spread_px[0] + 10);
}

void compositor_drop_shadow_full_spread_keeps_rounded_support() {
  // qual_rca_pinout.psd's white label plates: spread 100, size 21. Spread must expand
  // the matte with rounded Euclidean corners (Photoshop semantics, COM-probed); the
  // old post-blur gain binarized the box blur's tail, so the plate was the kernel's
  // rectangular support: per-glyph boxes jutting out to size * sqrt(2) on diagonals.
  patchy::Document document(160, 160, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(160, 160, 0, 0, 0));
  patchy::Layer layer(document.allocate_layer_id(), "Source", solid_rgba(40, 40, 10, 10, 10, 255));
  auto& source = document.add_layer(std::move(layer));
  source.set_bounds(patchy::Rect{60, 60, 40, 40});

  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.blend_mode = patchy::BlendMode::Normal;
  shadow.color = patchy::RgbColor{255, 255, 255};
  shadow.opacity = 1.0F;
  shadow.angle_degrees = 90.0F;
  shadow.distance = 0.0F;
  shadow.spread = 100.0F;
  shadow.size = 21.0F;
  source.layer_style().drop_shadows.push_back(shadow);

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  const auto euclidean_distance_from_source = [](std::int32_t x, std::int32_t y) {
    const auto dx = static_cast<float>(x < 60 ? 60 - x : (x > 99 ? x - 99 : 0));
    const auto dy = static_cast<float>(y < 60 ? 60 - y : (y > 99 ? y - 99 : 0));
    return std::sqrt(dx * dx + dy * dy);
  };

  // The plate must stay solid out to nearly the full size along the axes AND the
  // diagonals (rounded expansion), and die out within ~1px past it everywhere: any
  // shadow beyond that is a rectangular corner chunk (or float dust) leaking through.
  CHECK(flattened.pixel(118, 80)[0] >= 250);   // axis, 19px out
  CHECK(flattened.pixel(112, 112)[0] >= 250);  // diagonal, 18.4px out
  int painted_beyond_falloff = 0;
  for (std::int32_t y = 0; y < flattened.height(); ++y) {
    for (std::int32_t x = 0; x < flattened.width(); ++x) {
      if (euclidean_distance_from_source(x, y) > 23.0F && flattened.pixel(x, y)[0] > 8) {
        ++painted_beyond_falloff;
      }
    }
  }
  CHECK(painted_beyond_falloff == 0);
}

void compositor_renders_drop_shadow_beyond_outer_glow() {
  patchy::Document document(96, 80, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(96, 80, 232, 224, 204));

  patchy::Layer layer(document.allocate_layer_id(), "Styled", solid_rgba(24, 10, 255, 255, 255, 255));
  auto& source = document.add_layer(std::move(layer));
  source.set_bounds(patchy::Rect{34, 18, 24, 10});

  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.blend_mode = patchy::BlendMode::Multiply;
  shadow.color = patchy::RgbColor{0, 0, 0};
  shadow.opacity = 1.0F;
  shadow.angle_degrees = 90.0F;
  shadow.distance = 3.0F;
  shadow.size = 30.0F;
  shadow.spread = 74.0F;
  source.layer_style().drop_shadows.push_back(shadow);

  patchy::LayerOuterGlow glow;
  glow.enabled = true;
  glow.blend_mode = patchy::BlendMode::Normal;
  glow.color = patchy::RgbColor{184, 81, 74};
  glow.opacity = 1.0F;
  glow.size = 18.0F;
  glow.spread = 100.0F;
  source.layer_style().outer_glows.push_back(glow);

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  const auto* shadow_px = flattened.pixel(46, 48);
  CHECK(shadow_px[0] < 220);
  CHECK(shadow_px[1] < 214);
  CHECK(shadow_px[2] < 196);
}

void compositor_renders_inner_shadow() {
  patchy::Document document(40, 40, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(40, 40, 255, 255, 255));
  patchy::Layer layer(document.allocate_layer_id(), "Source", solid_rgba(20, 20, 255, 255, 255, 255));
  auto& source = document.add_layer(std::move(layer));
  source.set_bounds(patchy::Rect{10, 10, 20, 20});

  patchy::LayerInnerShadow shadow;
  shadow.enabled = true;
  shadow.blend_mode = patchy::BlendMode::Normal;
  shadow.color = patchy::RgbColor{0, 0, 0};
  shadow.opacity = 1.0F;
  shadow.distance = 0.0F;
  shadow.size = 8.0F;
  source.layer_style().inner_shadows.push_back(shadow);

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  CHECK(flattened.pixel(11, 11)[0] < flattened.pixel(20, 20)[0]);
  CHECK(flattened.pixel(20, 20)[0] > 220);
}

void compositor_renders_inner_glow() {
  patchy::Document document(40, 40, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(40, 40, 0, 0, 0));
  patchy::Layer layer(document.allocate_layer_id(), "Source", solid_rgba(20, 20, 0, 0, 0, 255));
  auto& source = document.add_layer(std::move(layer));
  source.set_bounds(patchy::Rect{10, 10, 20, 20});

  patchy::LayerInnerGlow glow;
  glow.enabled = true;
  glow.blend_mode = patchy::BlendMode::Normal;
  glow.color = patchy::RgbColor{255, 255, 255};
  glow.opacity = 1.0F;
  glow.size = 8.0F;
  glow.source = patchy::LayerInnerGlowSource::Edge;
  source.layer_style().inner_glows.push_back(glow);

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  CHECK(flattened.pixel(11, 11)[0] > flattened.pixel(20, 20)[0] + 20);
  CHECK(flattened.pixel(20, 20)[0] < 20);
}

patchy::PixelBuffer choke_probe_square_with_hole(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
  // 120x120 solid square with an 8x8 transparent hole at local 56..63 (document
  // 96..103 once placed at 40,40): the hole is the shape whose interior falloff
  // exposes a non-Euclidean choke support.
  auto pixels = solid_rgba(120, 120, r, g, b, 255);
  for (std::int32_t y = 56; y < 64; ++y) {
    for (std::int32_t x = 56; x < 64; ++x) {
      pixels.pixel(x, y)[3] = 0;
    }
  }
  return pixels;
}

float choke_probe_distance_from_hole(std::int32_t x, std::int32_t y) {
  const auto dx = static_cast<float>(x < 96 ? 96 - x : (x > 103 ? x - 103 : 0));
  const auto dy = static_cast<float>(y < 96 ? 96 - y : (y > 103 ? y - 103 : 0));
  return std::sqrt(dx * dx + dy * dy);
}

void compositor_inner_shadow_full_choke_keeps_rounded_interior() {
  // Photoshop's Choke is the interior mirror of the drop-shadow Spread (COM-probed
  // July 2026 with choke 0/50/100 renders): the inverse matte expands with rounded
  // Euclidean corners to choke% x size and only the remaining (1 - choke%) x size
  // is blurred. The old post-blur gain ((1 - blur) / (1 - choke)) instead amplified
  // the box blur's square-support tail: a small transparent hole radiated a
  // ~1.5 x size rounded box of half-tone dust rather than a size-radius disc.
  patchy::Document document(200, 200, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(200, 200, 255, 255, 255));
  patchy::Layer layer(document.allocate_layer_id(), "Source", choke_probe_square_with_hole(255, 255, 255));
  auto& source = document.add_layer(std::move(layer));
  source.set_bounds(patchy::Rect{40, 40, 120, 120});

  patchy::LayerInnerShadow shadow;
  shadow.enabled = true;
  shadow.blend_mode = patchy::BlendMode::Normal;
  shadow.color = patchy::RgbColor{0, 0, 0};
  shadow.opacity = 1.0F;
  shadow.angle_degrees = 120.0F;
  shadow.distance = 0.0F;
  shadow.choke = 100.0F;
  shadow.size = 21.0F;
  source.layer_style().inner_shadows.push_back(shadow);

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  // Solid out to the full size along the axis AND the diagonal from the hole
  // (Photoshop renders exactly these pixels solid), and along the straight edge.
  CHECK(flattened.pixel(124, 99)[0] <= 5);   // axis, 21st pixel right of the hole
  CHECK(flattened.pixel(117, 117)[0] <= 5);  // diagonal, 19.8px from the hole corner
  CHECK(flattened.pixel(59, 80)[0] <= 5);    // straight edge band, 20th pixel deep
  CHECK(flattened.pixel(64, 80)[0] >= 250);  // straight edge band ends at size
  // Hard bound past the Euclidean support: interior pixels farther than size + 2.5
  // from both the hole and the outer contour must stay clean white.
  int painted_beyond_falloff = 0;
  for (std::int32_t y = 40; y < 160; ++y) {
    for (std::int32_t x = 40; x < 160; ++x) {
      const auto interior_depth = std::min(std::min(x - 40, y - 40), std::min(159 - x, 159 - y));
      if (interior_depth > 23 && choke_probe_distance_from_hole(x, y) > 23.5F && flattened.pixel(x, y)[0] < 247) {
        ++painted_beyond_falloff;
      }
    }
  }
  CHECK(painted_beyond_falloff == 0);
}

void compositor_inner_glow_full_choke_keeps_rounded_interior() {
  // Inner glow's Edge-source choke shares the inner-shadow pipeline; same geometry
  // and Photoshop-probed expectations with the colors flipped.
  patchy::Document document(200, 200, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(200, 200, 0, 0, 0));
  patchy::Layer layer(document.allocate_layer_id(), "Source", choke_probe_square_with_hole(0, 0, 0));
  auto& source = document.add_layer(std::move(layer));
  source.set_bounds(patchy::Rect{40, 40, 120, 120});

  patchy::LayerInnerGlow glow;
  glow.enabled = true;
  glow.blend_mode = patchy::BlendMode::Normal;
  glow.color = patchy::RgbColor{255, 255, 255};
  glow.opacity = 1.0F;
  glow.choke = 100.0F;
  glow.size = 21.0F;
  glow.source = patchy::LayerInnerGlowSource::Edge;
  source.layer_style().inner_glows.push_back(glow);

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  CHECK(flattened.pixel(124, 99)[0] >= 250);   // axis, 21st pixel right of the hole
  CHECK(flattened.pixel(117, 117)[0] >= 250);  // diagonal, 19.8px from the hole corner
  CHECK(flattened.pixel(59, 80)[0] >= 250);    // straight edge band, 20th pixel deep
  CHECK(flattened.pixel(64, 80)[0] <= 5);      // straight edge band ends at size
  int painted_beyond_falloff = 0;
  for (std::int32_t y = 40; y < 160; ++y) {
    for (std::int32_t x = 40; x < 160; ++x) {
      const auto interior_depth = std::min(std::min(x - 40, y - 40), std::min(159 - x, 159 - y));
      if (interior_depth > 23 && choke_probe_distance_from_hole(x, y) > 23.5F && flattened.pixel(x, y)[0] > 8) {
        ++painted_beyond_falloff;
      }
    }
  }
  CHECK(painted_beyond_falloff == 0);
}

void compositor_inner_glow_center_choke_erodes_matte_geometrically() {
  // Center-source choke erodes the matte geometrically (COM-probed: choke 100 pulls
  // the glow back to a hard Euclidean erosion by the full size, dark band outside
  // it); the old code ignored choke for the Center source entirely.
  patchy::Document document(200, 200, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(200, 200, 0, 0, 0));
  patchy::Layer layer(document.allocate_layer_id(), "Source", solid_rgba(120, 120, 0, 0, 0, 255));
  auto& source = document.add_layer(std::move(layer));
  source.set_bounds(patchy::Rect{40, 40, 120, 120});

  patchy::LayerInnerGlow glow;
  glow.enabled = true;
  glow.blend_mode = patchy::BlendMode::Normal;
  glow.color = patchy::RgbColor{255, 255, 255};
  glow.opacity = 1.0F;
  glow.choke = 100.0F;
  glow.size = 21.0F;
  glow.source = patchy::LayerInnerGlowSource::Center;
  source.layer_style().inner_glows.push_back(glow);

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  CHECK(flattened.pixel(50, 99)[0] <= 5);    // depth 10: inside the choked band, no glow
  CHECK(flattened.pixel(60, 99)[0] <= 5);    // depth 20: still inside the band
  CHECK(flattened.pixel(62, 99)[0] >= 250);  // depth 22: the eroded core lights up
  CHECK(flattened.pixel(99, 99)[0] >= 250);  // deep center stays lit
}

}  // namespace

std::vector<patchy::test::TestCase> gradients_interior_effects_tests() {
  return {
      {"gradient_midpoints_remap_color_and_alpha_segments",
       gradient_midpoints_remap_color_and_alpha_segments},
      {"gradient_presets_have_stable_ids_and_recipes", gradient_presets_have_stable_ids_and_recipes},
      {"grd_v5_round_trips_solid_dynamic_noise_and_hierarchy",
       grd_v5_round_trips_solid_dynamic_noise_and_hierarchy},
      {"gradient_methods_noise_dither_and_geometry_are_deterministic",
       gradient_methods_noise_dither_and_geometry_are_deterministic},
      {"gradient_linear_geometry_uses_angle_projected_layer_span",
       gradient_linear_geometry_uses_angle_projected_layer_span},
      {"compositor_aligned_gradients_use_visible_alpha_bounds",
       compositor_aligned_gradients_use_visible_alpha_bounds},
      {"psd_bevel_examine_classic_gradient_matches_photoshop_if_available",
       psd_bevel_examine_classic_gradient_matches_photoshop_if_available},
      {"compositor_renders_photoshop_style_satin", compositor_renders_photoshop_style_satin},
      {"compositor_renders_drop_shadow_spread", compositor_renders_drop_shadow_spread},
      {"compositor_drop_shadow_full_spread_keeps_rounded_support",
       compositor_drop_shadow_full_spread_keeps_rounded_support},
      {"compositor_renders_drop_shadow_beyond_outer_glow",
       compositor_renders_drop_shadow_beyond_outer_glow},
      {"compositor_renders_inner_shadow", compositor_renders_inner_shadow},
      {"compositor_renders_inner_glow", compositor_renders_inner_glow},
      {"compositor_inner_shadow_full_choke_keeps_rounded_interior",
       compositor_inner_shadow_full_choke_keeps_rounded_interior},
      {"compositor_inner_glow_full_choke_keeps_rounded_interior",
       compositor_inner_glow_full_choke_keeps_rounded_interior},
      {"compositor_inner_glow_center_choke_erodes_matte_geometrically",
       compositor_inner_glow_center_choke_erodes_matte_geometrically},
  };
}
