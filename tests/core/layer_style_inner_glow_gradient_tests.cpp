// GitHub issue 746: an Inner Glow filled with a gradient. The pinned numbers are Photoshop
// 2027's own (docs/layer-effects-render-shadows-glows.md, "Gradient Inner Glow").
#include "core/document.hpp"
#include "core/style_contour.hpp"
#include "formats/bmp_document_io.hpp"
#include "psd/asl_io.hpp"
#include "psd/psd_document_io.hpp"
#include "psd/psd_layer_effects.hpp"
#include "render/compositor.hpp"
#include "test_harness.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "core_test_support.hpp"
#include "local_psd_fixtures.hpp"
#include "psd_test_support.hpp"
#include "test_groups.hpp"

namespace {

using patchy::test::find_layer_named;
using patchy::test::solid_rgb;
using patchy::test::solid_rgba;

using Source = patchy::LayerInnerGlowSource;
using Technique = patchy::LayerGlowTechnique;

// The capture geometry: a 100 x 100 square at (20, 20) on a white page, the glow at Normal
// 100% so a flattened pixel shows the glow's own color and opacity.
patchy::Document glow_document(const patchy::LayerInnerGlow& glow, std::uint8_t layer_gray = 0) {
  patchy::Document document(140, 140, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(140, 140, 255, 255, 255));
  auto& layer = document.add_layer(patchy::Layer(document.allocate_layer_id(), "Square",
                                                solid_rgba(100, 100, layer_gray, layer_gray, layer_gray, 255)));
  layer.set_bounds(patchy::Rect{20, 20, 100, 100});
  layer.layer_style().inner_glows.push_back(glow);
  return document;
}

patchy::LayerInnerGlow probe_glow(Source source, Technique technique = Technique::Softer, float range = 100.0F,
                                  float choke = 0.0F, float size = 20.0F) {
  patchy::LayerInnerGlow glow;
  glow.enabled = true;
  glow.blend_mode = patchy::BlendMode::Normal;
  glow.color = patchy::RgbColor{255, 255, 255};
  glow.opacity = 1.0F;
  glow.size = size;
  glow.choke = choke;
  glow.range = range;
  glow.source = source;
  glow.technique = technique;
  return glow;
}

// Red at the first stop, blue at the last, straight segments (Smoothness 0): the blue byte
// of an opaque glow is the stop position and the red byte its complement.
patchy::LayerStyleGradient red_to_blue(float first_opacity = 1.0F, float last_opacity = 1.0F) {
  patchy::LayerStyleGradient gradient;
  gradient.color_stops = {{0.0F, {255, 0, 0}}, {1.0F, {0, 0, 255}}};
  gradient.alpha_stops = {{0.0F, first_opacity}, {1.0F, last_opacity}};
  gradient.smoothness = 0;
  return gradient;
}

patchy::StyleContour cone_contour() {
  patchy::StyleContour contour;
  contour.name = "Cone";
  contour.points = {{0.0F, 0.0F, false}, {128.0F, 255.0F, false}, {255.0F, 0.0F, false}};
  return contour;
}

// The stop position is one minus the coverage the same glow has with a solid color, and the
// gradient's own opacity there is the glow's alpha: Edge and Center, both techniques, Range,
// Choke and a shaped contour (Photoshop 2027: every probe document within 1 to 2 levels).
void compositor_inner_glow_gradient_follows_solid_coverage() {
  struct Variant {
    const char* name;
    patchy::LayerInnerGlow glow;
  };
  auto contoured = probe_glow(Source::Edge);
  contoured.contour = cone_contour();
  auto contoured_center = probe_glow(Source::Center);
  contoured_center.contour = cone_contour();
  const std::vector<Variant> variants{
      {"edge softer", probe_glow(Source::Edge)},
      {"center softer", probe_glow(Source::Center)},
      {"edge softer range 50", probe_glow(Source::Edge, Technique::Softer, 50.0F)},
      {"center softer range 50", probe_glow(Source::Center, Technique::Softer, 50.0F)},
      {"edge precise", probe_glow(Source::Edge, Technique::Precise)},
      {"center precise range 50 choke 30", probe_glow(Source::Center, Technique::Precise, 50.0F, 30.0F)},
      {"edge softer choke 30", probe_glow(Source::Edge, Technique::Softer, 100.0F, 30.0F)},
      {"edge softer size 60", probe_glow(Source::Edge, Technique::Softer, 100.0F, 0.0F, 60.0F)},
      {"center softer size 60", probe_glow(Source::Center, Technique::Softer, 100.0F, 0.0F, 60.0F)},
      {"edge cone contour", contoured},
      {"center cone contour", contoured_center},
  };
  for (const auto& variant : variants) {
    const auto solid = patchy::Compositor{}.flatten_rgb8(glow_document(variant.glow));
    auto opaque = variant.glow;
    opaque.gradient = red_to_blue();
    const auto colored = patchy::Compositor{}.flatten_rgb8(glow_document(opaque));
    auto fading = variant.glow;
    fading.gradient = red_to_blue(1.0F, 0.0F);
    const auto faded = patchy::Compositor{}.flatten_rgb8(glow_document(fading));
    auto rising = variant.glow;
    rising.gradient = red_to_blue(0.0F, 1.0F);
    const auto risen = patchy::Compositor{}.flatten_rgb8(glow_document(rising));
    // The expected bytes come from the solid glow's own byte, so a product of two of them
    // (opacity stops) can sit a second level away.
    int worst = 0;
    int worst_blend = 0;
    const auto error = [](double expected, std::uint8_t actual) {
      return static_cast<int>(std::ceil(std::abs(expected - static_cast<double>(actual)) - 0.5));
    };
    const auto note = [&](double expected, std::uint8_t actual) { worst = std::max(worst, error(expected, actual)); };
    const auto note_blend = [&](double expected, std::uint8_t actual) {
      worst_blend = std::max(worst_blend, error(expected, actual));
    };
    for (std::int32_t y = 20; y < 120; ++y) {
      for (std::int32_t x = 20; x < 120; ++x) {
        const auto coverage = static_cast<double>(solid.pixel(x, y)[0]) / 255.0;
        // Opaque stops: the glow covers the whole square, red where the solid glow is
        // strongest and blue where it has faded out.
        note(255.0 * coverage, colored.pixel(x, y)[0]);
        note(0.0, colored.pixel(x, y)[1]);
        note(255.0 * (1.0 - coverage), colored.pixel(x, y)[2]);
        // Opacity 100 to 0: the alpha is the coverage again, on the same colors.
        note_blend(255.0 * coverage * coverage, faded.pixel(x, y)[0]);
        note_blend(255.0 * (1.0 - coverage) * coverage, faded.pixel(x, y)[2]);
        // Opacity 0 to 100: transparent where the solid glow is strongest.
        note_blend(255.0 * coverage * (1.0 - coverage), risen.pixel(x, y)[0]);
        note_blend(255.0 * (1.0 - coverage) * (1.0 - coverage), risen.pixel(x, y)[2]);
      }
    }
    std::cout << variant.name << " worst error " << worst << ", with opacity stops " << worst_blend << "\n";
    CHECK(worst <= 1);
    CHECK(worst_blend <= 2);
  }
}

// Photoshop 2027's own bytes along the middle row of the 100 x 100 square (Softer, Size 20,
// Range 100, Normal 100%), counted inward from the left edge.
void compositor_inner_glow_gradient_matches_photoshop_samples() {
  constexpr std::array<int, 20> kEdgeCoverage{121, 109, 98, 87, 77, 67, 58, 50, 42, 35,
                                             29,  23,  18, 13, 10, 6,  4,  2,  1,  0};
  for (const bool center : {false, true}) {
    auto glow = probe_glow(center ? Source::Center : Source::Edge);
    glow.gradient = red_to_blue();
    const auto opaque = patchy::Compositor{}.flatten_rgb8(glow_document(glow));
    glow.gradient = red_to_blue(1.0F, 0.0F);
    const auto faded = patchy::Compositor{}.flatten_rgb8(glow_document(glow));
    glow.gradient = red_to_blue(0.5F, 0.5F);
    const auto half = patchy::Compositor{}.flatten_rgb8(glow_document(glow));
    for (std::size_t depth = 0; depth < kEdgeCoverage.size(); ++depth) {
      const auto x = 20 + static_cast<std::int32_t>(depth);
      const auto coverage = center ? 255 - kEdgeCoverage[depth] : kEdgeCoverage[depth];
      // Opaque stops: red is the coverage, blue its complement.
      CHECK(std::abs(static_cast<int>(opaque.pixel(x, 70)[0]) - coverage) <= 1);
      CHECK(std::abs(static_cast<int>(opaque.pixel(x, 70)[2]) - (255 - coverage)) <= 1);
      CHECK(opaque.pixel(x, 70)[1] == 0);
      // Opacity 100 to 0: red plus blue is the glow's alpha, the coverage again.
      const auto alpha = static_cast<int>(faded.pixel(x, 70)[0]) + static_cast<int>(faded.pixel(x, 70)[2]);
      CHECK(std::abs(alpha - coverage) <= 1);
      // Constant 50% opacity: half strength everywhere, whatever the coverage.
      const auto half_alpha = static_cast<int>(half.pixel(x, 70)[0]) + static_cast<int>(half.pixel(x, 70)[2]);
      CHECK(std::abs(half_alpha - 128) <= 1);
    }
    // Past the glow's reach: an Edge glow shows its last stop, a Center glow its first.
    const auto* deep = opaque.pixel(70, 70);
    CHECK(std::abs(static_cast<int>(deep[0]) - (center ? 255 : 0)) <= 1);
    CHECK(std::abs(static_cast<int>(deep[2]) - (center ? 0 : 255)) <= 1);
  }
  // Size 0: a solid Edge glow draws nothing, and a gradient one still covers the whole shape
  // in its last stop (zero coverage); a Center glow shows its first stop. Stops fading to 0%
  // at the last stop leave an Edge glow invisible again.
  for (const auto technique : {Technique::Softer, Technique::Precise}) {
    auto edge = probe_glow(Source::Edge, technique, 100.0F, 0.0F, 0.0F);
    CHECK(patchy::Compositor{}.flatten_rgb8(glow_document(edge)).pixel(70, 70)[0] == 0);
    edge.gradient = red_to_blue();
    const auto edge_pixels = patchy::Compositor{}.flatten_rgb8(glow_document(edge));
    for (const auto x : {20, 45, 70, 119}) {
      CHECK(edge_pixels.pixel(x, 70)[0] == 0 && edge_pixels.pixel(x, 70)[2] == 255);
    }
    edge.gradient = red_to_blue(1.0F, 0.0F);
    const auto faded_edge = patchy::Compositor{}.flatten_rgb8(glow_document(edge));
    CHECK(faded_edge.pixel(20, 70)[0] == 0 && faded_edge.pixel(20, 70)[2] == 0);
    auto center = probe_glow(Source::Center, technique, 100.0F, 0.0F, 0.0F);
    center.gradient = red_to_blue();
    const auto center_pixels = patchy::Compositor{}.flatten_rgb8(glow_document(center));
    for (const auto x : {20, 45, 70, 119}) {
      CHECK(center_pixels.pixel(x, 70)[0] >= 254 && center_pixels.pixel(x, 70)[2] <= 1);
    }
  }
  // The effect's Opacity scales the gradient glow like a solid one (Opacity 50: 128).
  auto dim = probe_glow(Source::Edge);
  dim.opacity = 0.5F;
  dim.gradient = red_to_blue();
  const auto dimmed = patchy::Compositor{}.flatten_rgb8(glow_document(dim));
  CHECK(std::abs(static_cast<int>(dimmed.pixel(70, 70)[2]) - 128) <= 1);
  CHECK(std::abs(static_cast<int>(dimmed.pixel(20, 70)[0]) + static_cast<int>(dimmed.pixel(20, 70)[2]) - 128) <= 1);
}

// A solid glow renders as it always did: the gradient field is unset by default, and a
// glow that never had one writes the same style bytes as before the field existed.
void inner_glow_without_gradient_keeps_color_rendering_and_bytes() {
  patchy::LayerInnerGlow glow;
  CHECK(!glow.gradient.has_value());
  auto solid = probe_glow(Source::Edge);
  solid.color = patchy::RgbColor{40, 200, 90};
  const auto flattened = patchy::Compositor{}.flatten_rgb8(glow_document(solid));
  // Coverage 121 of the green on black at the edge; nothing past the glow's reach.
  CHECK(std::abs(static_cast<int>(flattened.pixel(20, 70)[1]) - 95) <= 1);
  CHECK(flattened.pixel(70, 70)[0] == 0 && flattened.pixel(70, 70)[1] == 0 && flattened.pixel(70, 70)[2] == 0);

  patchy::LayerStyle style;
  style.inner_glows.push_back(solid);
  const auto payload = patchy::psd::photoshop_lfx2_layer_style_payload(style);
  const std::string_view text(reinterpret_cast<const char*>(payload.data()), payload.size());
  CHECK(text.find("IrGl") != std::string_view::npos);
  CHECK(text.find("Clr ") != std::string_view::npos);
  CHECK(text.find("Grad") == std::string_view::npos);
  style.inner_glows.front().gradient = red_to_blue();
  const auto with_gradient = patchy::psd::photoshop_lfx2_layer_style_payload(style);
  const std::string_view gradient_text(reinterpret_cast<const char*>(with_gradient.data()), with_gradient.size());
  // The gradient takes the color's slot, as in an Outer Glow.
  CHECK(gradient_text.find("Grad") != std::string_view::npos);
  CHECK(gradient_text.find("Clr ") != std::string_view::npos);  // the stops' own colors
  CHECK(gradient_text.find("gradientsInterpolationMethod") != std::string_view::npos);
}

// Noise moves a gradient glow's opacity and never its stop position. An opaque gradient at
// Noise 50 keeps half its pixels and spreads the rest evenly down to half strength
// (Photoshop 2027: mean 87.7%, 51.9% untouched, never below 127).
void inner_glow_gradient_noise_moves_opacity_only() {
  auto glow = probe_glow(Source::Edge);
  glow.gradient = red_to_blue();
  const auto clean = patchy::Compositor{}.flatten_rgb8(glow_document(glow));
  glow.noise = 50.0F;
  const auto noisy = patchy::Compositor{}.flatten_rgb8(glow_document(glow));
  double alpha_sum = 0.0;
  int untouched = 0;
  int lowest = 255;
  int count = 0;
  for (std::int32_t y = 20; y < 120; ++y) {
    for (std::int32_t x = 20; x < 120; ++x) {
      const auto alpha = static_cast<int>(noisy.pixel(x, y)[0]) + static_cast<int>(noisy.pixel(x, y)[2]);
      alpha_sum += alpha;
      untouched += alpha >= 254 ? 1 : 0;
      lowest = std::min(lowest, alpha);
      ++count;
      // Same stop: red to blue in the same proportion as without Noise.
      const auto clean_blue = static_cast<double>(clean.pixel(x, y)[2]) / 255.0;
      CHECK(std::abs(static_cast<double>(noisy.pixel(x, y)[2]) - clean_blue * alpha) <= 1.5);
      CHECK(noisy.pixel(x, y)[1] == 0);
    }
  }
  const auto mean = alpha_sum / (255.0 * count);
  const auto share = static_cast<double>(untouched) / count;
  std::cout << "noise 50 mean alpha " << mean << " untouched share " << share << " lowest " << lowest << "\n";
  CHECK(std::abs(mean - 0.875) <= 0.01);
  CHECK(std::abs(share - 0.5) <= 0.03);
  CHECK(lowest >= 126);
}

patchy::LayerStyleGradient exchange_gradient() {
  patchy::LayerStyleGradient gradient;
  gradient.name = "Warm to cool";
  gradient.color_stops = {{0.0F, {255, 0, 0}}, {0.5F, {255, 255, 0}}, {1.0F, {0, 0, 255}}};
  gradient.alpha_stops = {{0.0F, 1.0F}, {1.0F, 0.5F}};
  gradient.interpolation = patchy::GradientInterpolationMethod::Perceptual;
  return gradient;
}

void psd_gradient_inner_glow_survives_copy_and_unrelated_edits() {
  patchy::Document doc(24, 24, patchy::PixelFormat::rgb8());
  doc.add_pixel_layer("Base", solid_rgb(24, 24, 12, 20, 30));
  auto& layer = doc.add_layer(patchy::Layer(doc.allocate_layer_id(), "Glow", solid_rgba(12, 12, 100, 120, 140, 255)));
  layer.set_bounds({6, 6, 12, 12});
  patchy::LayerInnerGlow glow;
  glow.enabled = true;
  glow.source = Source::Center;
  glow.technique = Technique::Precise;
  glow.noise = 12.0F;
  glow.jitter = 30.0F;
  glow.gradient = exchange_gradient();
  // A second instance keeps its plain color beside the gradient one.
  patchy::LayerInnerGlow plain;
  plain.enabled = true;
  plain.color = patchy::RgbColor{9, 99, 199};
  layer.layer_style().inner_glows = {glow, plain};
  for (const bool large : {false, true}) {
    patchy::psd::WriteOptions options;
    options.large_document = large;
    auto loaded = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(doc, options));
    const auto& style = std::as_const(loaded).layers()[1].layer_style();
    CHECK(style.inner_glows.size() == 2);
    CHECK(style.inner_glows[0].gradient == glow.gradient);
    CHECK(style.inner_glows[0].source == Source::Center && style.inner_glows[0].technique == Technique::Precise);
    CHECK(style.inner_glows[0].noise == 12.0F && style.inner_glows[0].jitter == 30.0F);
    // The color of a gradient glow is its first stop, as for an Outer Glow.
    CHECK(style.inner_glows[0].color.red == 255 && style.inner_glows[0].color.green == 0);
    CHECK(!style.inner_glows[1].gradient.has_value());
    CHECK(style.inner_glows[1].color.red == 9 && style.inner_glows[1].color.green == 99 &&
          style.inner_glows[1].color.blue == 199);
    loaded.layers()[0].set_name("Renamed base");
    auto copied_style = style;
    auto& copy = loaded.add_layer(patchy::Layer(loaded.allocate_layer_id(), "Pasted style",
                                               solid_rgba(2, 2, 90, 30, 80, 255)));
    copy.layer_style() = std::move(copied_style);
    copy.layer_style().inner_glows[0].size = 3.0F;
    const auto written = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(loaded, options));
    CHECK(written.layers()[0].name() == "Renamed base");
    CHECK(written.layers()[1].layer_style().inner_glows[0].gradient == glow.gradient);
    CHECK(written.layers()[2].layer_style().inner_glows[0].gradient == glow.gradient);
    CHECK(written.layers()[2].layer_style().inner_glows[0].size == 3.0F);
    CHECK(!written.layers()[2].layer_style().inner_glows[1].gradient.has_value());
  }
}

void asl_gradient_inner_glow_round_trips() {
  patchy::psd::AslStyle entry;
  entry.id = "74600000-aaaa-bbbb-cccc-000000000746";
  entry.name = "Gradient inner glow";
  patchy::LayerInnerGlow glow;
  glow.enabled = true;
  glow.source = Source::Center;
  glow.range = 65.0F;
  auto& gradient = glow.gradient.emplace();
  gradient.name = "Copper glow";
  gradient.interpolation = patchy::GradientInterpolationMethod::Linear;
  gradient.color_stops = {{0.0F, {240, 90, 5}}, {0.5F, {255, 245, 20}, 0.3F}, {1.0F, {80, 0, 120}}};
  gradient.alpha_stops = {{0.0F, 1.0F}, {1.0F, 0.5F, 0.7F}};
  entry.style.inner_glows.push_back(glow);
  patchy::psd::AslStyle solid;
  solid.id = "74600000-aaaa-bbbb-cccc-000000000747";
  solid.name = "Solid inner glow";
  patchy::LayerInnerGlow solid_glow;
  solid_glow.enabled = true;
  solid_glow.color = patchy::RgbColor{12, 34, 56};
  solid.style.inner_glows.push_back(solid_glow);
  const std::vector<patchy::psd::AslStyle> styles{entry, solid};
  const auto bytes = patchy::psd::write_asl(styles, {});
  CHECK(!bytes.empty());
  CHECK(patchy::psd::write_asl(styles, {}) == bytes);
  std::string error;
  const auto parsed = patchy::psd::read_asl(bytes, error);
  CHECK(parsed.has_value());
  CHECK(error.empty());
  CHECK(parsed->warnings.empty());
  CHECK(parsed->styles.size() == 2U);
  const auto& read_glow = parsed->styles[0].style.inner_glows;
  CHECK(read_glow.size() == 1);
  CHECK(read_glow.front().gradient == glow.gradient);
  CHECK(read_glow.front().source == Source::Center && read_glow.front().range == 65.0F);
  const auto& read_solid = parsed->styles[1].style.inner_glows;
  CHECK(read_solid.size() == 1);
  CHECK(!read_solid.front().gradient.has_value());
  CHECK(read_solid.front().color.red == 12 && read_solid.front().color.green == 34 &&
        read_solid.front().color.blue == 56);
}

// A document Patchy writes by itself, for the Photoshop check (docs/psd-layer-styles.md):
// gradient Inner Glows from the edge and from the center, with opacity stops, a contour,
// Noise and the default Screen blend. Saved with its flatten under test-artifacts.
patchy::Document exchange_document() {
  patchy::Document document(480, 140, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(480, 140, 255, 255, 255));
  const auto add = [&document](const char* name, std::int32_t left, std::uint8_t gray,
                               const patchy::LayerInnerGlow& glow) {
    auto& layer = document.add_layer(
        patchy::Layer(document.allocate_layer_id(), name, solid_rgba(100, 100, gray, gray, gray, 255)));
    layer.set_bounds(patchy::Rect{left, 20, 100, 100});
    layer.layer_style().inner_glows.push_back(glow);
  };
  patchy::LayerStyleGradient sunset;
  sunset.name = "Sunset";
  sunset.color_stops = {{0.0F, {250, 220, 40}}, {0.5F, {230, 40, 160}}, {1.0F, {30, 80, 220}}};
  sunset.alpha_stops = {{0.0F, 1.0F}, {1.0F, 0.3F}};
  auto edge = probe_glow(Source::Edge);
  edge.gradient = sunset;
  add("edge-gradient", 20, 128, edge);
  auto center = probe_glow(Source::Center, Technique::Softer, 50.0F);
  center.gradient = sunset;
  center.gradient->interpolation = patchy::GradientInterpolationMethod::Perceptual;
  add("center-gradient", 135, 128, center);
  auto precise = probe_glow(Source::Edge, Technique::Precise, 60.0F, 20.0F, 30.0F);
  precise.gradient = red_to_blue(1.0F, 0.0F);
  precise.contour = cone_contour();
  precise.noise = 20.0F;
  add("precise-contour-noise", 250, 40, precise);
  patchy::LayerInnerGlow screen;  // the defaults a new Inner Glow starts from, plus a gradient
  screen.enabled = true;
  screen.size = 25.0F;
  screen.gradient = sunset;
  add("screen-default", 365, 60, screen);
  return document;
}

void psd_inner_glow_gradient_exchange_file_round_trips() {
  const auto document = exchange_document();
  const auto dir = std::filesystem::path("test-artifacts");
  std::filesystem::create_directories(dir);
  const auto path = dir / "layer-style-746-inner-glow-gradient.psd";
  patchy::psd::DocumentIo::write_layered_rgb8_file(document, path);
  patchy::test::write_rgb8_bmp_artifact("layer-style-746-inner-glow-gradient", patchy::Compositor{}.flatten_rgb8(document));
  const auto reread = patchy::psd::DocumentIo::read_file(path);
  for (const auto* name : {"edge-gradient", "center-gradient", "precise-contour-noise", "screen-default"}) {
    const auto* before = find_layer_named(document.layers(), name);
    const auto* after = find_layer_named(reread.layers(), name);
    CHECK(before != nullptr && after != nullptr);
    const auto& expected = before->layer_style().inner_glows.front();
    const auto& actual = after->layer_style().inner_glows.front();
    CHECK(actual.gradient == expected.gradient);
    CHECK(actual.source == expected.source && actual.technique == expected.technique);
    CHECK(actual.range == expected.range && actual.choke == expected.choke && actual.size == expected.size);
    CHECK(actual.noise == expected.noise && actual.blend_mode == expected.blend_mode);
    CHECK(actual.contour == expected.contour);
  }
  const auto first = patchy::Compositor{}.flatten_rgb8(document);
  const auto second = patchy::Compositor{}.flatten_rgb8(reread);
  CHECK(patchy::test::rgb_diff_metrics(first, second).max_channel_delta == 0);
}

// Photoshop 2027's capture set (local-test-fixtures/layer-style-746/probes, made by
// probe.jsx there): each document holds one glow geometry on a row of hard squares and a row
// of feathered ones, a column per fill. Patchy reads each file, renders it from the settings
// and stays close to Photoshop's flatten.
void photoshop_inner_glow_gradient_probes_match_if_available() {
  const auto dir = patchy::test::local_fixtures_root() / "layer-style-746" / "probes";
  // soft_edges: the feathered row is compared too, soft edge included. Every document does
  // since Precise and Choke measure a soft matte from its opaque boundary
  // (docs/layer-effects-render-shadows-glows.md, "Soft mattes under Choke and Precise").
  struct Probe {
    const char* name;
    Source source;
    Technique technique;
    bool soft_edges;
  };
  const std::vector<Probe> probes{
      {"edge-soft-r100", Source::Edge, Technique::Softer, true},
      {"center-soft-r100", Source::Center, Technique::Softer, true},
      {"edge-soft-r50", Source::Edge, Technique::Softer, true},
      {"center-soft-r50", Source::Center, Technique::Softer, true},
      {"edge-precise-r100", Source::Edge, Technique::Precise, true},
      {"center-precise-r100", Source::Center, Technique::Precise, true},
      {"edge-precise-r50", Source::Edge, Technique::Precise, true},
      {"center-precise-r50", Source::Center, Technique::Precise, true},
      {"edge-soft-choke30", Source::Edge, Technique::Softer, true},
      {"center-soft-choke30", Source::Center, Technique::Softer, true},
      {"edge-soft-cone", Source::Edge, Technique::Softer, true},
      {"center-soft-cone", Source::Center, Technique::Softer, true},
      {"edge-soft-size60", Source::Edge, Technique::Softer, true},
      {"center-soft-size60", Source::Center, Technique::Softer, true},
      {"center-soft-size0", Source::Center, Technique::Softer, true},
      {"edge-soft-screen75", Source::Edge, Technique::Softer, true},
      {"edge-soft-size0", Source::Edge, Technique::Softer, true},
      {"edge-soft-aa-cone", Source::Edge, Technique::Softer, true},
      {"edge-precise-cone", Source::Edge, Technique::Precise, true},
      {"center-precise-choke30-r50", Source::Center, Technique::Precise, true},
  };
  // The columns of every document, left to right. Noise is Patchy's own grain and gradient
  // jitter is not rendered, so those cells compare by their average color only.
  struct Cell {
    const char* name;
    bool gradient;
    bool grain;
    bool jitter;
  };
  const std::vector<Cell> cells{
      {"solid-k", false, false, false}, {"rb-k", true, false, false},      {"rb-w", true, false, false},
      {"fade-k", true, false, false},   {"fade-w", true, false, false},    {"rise-k", true, false, false},
      {"half-k", true, false, false},   {"noise-k", true, true, false},    {"jitter-k", true, false, true},
      {"real-g", true, false, false},   {"solid50-k", false, false, false}, {"rb50-k", true, false, false},
      {"fadenoise-k", true, true, false}, {"perc-k", true, false, false},  {"smooth-k", true, false, false},
      {"mid-k", true, false, false},
  };
  patchy::test::skip_unless_exists(dir / (std::string(probes.front().name) + ".psd"));
  int worst_solid = 0;
  int worst_gradient = 0;
  double worst_grain_mean = 0.0;
  for (const auto& probe : probes) {
    const auto psd = dir / (std::string(probe.name) + ".psd");
    const auto bmp = dir / (std::string(probe.name) + ".bmp");
    patchy::test::skip_unless_exists(psd);
    patchy::test::skip_unless_exists(bmp);
    const auto document = patchy::psd::DocumentIo::read_file(psd);
    const auto reference = patchy::Compositor{}.flatten_rgb8(patchy::bmp::DocumentIo::read_file(bmp));
    const auto actual = patchy::Compositor{}.flatten_rgb8(document);
    CHECK(actual.width() == reference.width() && actual.height() == reference.height());
    patchy::test::write_rgb8_bmp_artifact(std::string("layer-style-746-") + probe.name, actual);
    for (std::size_t column = 0; column < cells.size(); ++column) {
      const auto& cell = cells[column];
      for (const bool feathered : {false, true}) {
        const auto layer_name = std::string(cell.name) + (feathered ? "-f" : "");
        const auto* layer = find_layer_named(document.layers(), layer_name);
        CHECK(layer != nullptr);
        CHECK(layer->layer_style().inner_glows.size() == 1);
        const auto& glow = layer->layer_style().inner_glows.front();
        CHECK(glow.enabled && glow.source == probe.source && glow.technique == probe.technique);
        CHECK(glow.gradient.has_value() == cell.gradient);
        const auto left = 20 + static_cast<std::int32_t>(column) * 120;
        const auto top = feathered ? 140 : 20;
        int worst = 0;
        double mean_error = 0.0;
        // A feathered square compares with its whole soft edge (the feather of 4 fades
        // out within 10 px of the square): the glow folds into the layer's color and the
        // layer meets the page once, as in Photoshop
        // (docs/layer-effects-render-shadows-glows.md, "Interior effects on soft edges").
        const auto inset = feathered ? -10 : -4;
        const auto side = 100 - 2 * inset;
        for (std::int32_t y = top + inset; y < top + inset + side; ++y) {
          for (std::int32_t x = left + inset; x < left + inset + side; ++x) {
            for (int channel = 0; channel < 3; ++channel) {
              const auto error = static_cast<int>(actual.pixel(x, y)[channel]) -
                                 static_cast<int>(reference.pixel(x, y)[channel]);
              worst = std::max(worst, std::abs(error));
              mean_error += error;
            }
          }
        }
        mean_error = std::abs(mean_error / (static_cast<double>(side) * side * 3.0));
        if (cell.jitter || (feathered && !probe.soft_edges)) {
          continue;
        }
        if (cell.grain) {
          worst_grain_mean = std::max(worst_grain_mean, mean_error);
          if (mean_error > 1.5) {
            std::cout << probe.name << " " << layer_name << " mean error " << mean_error << "\n";
          }
          continue;
        }
        auto& total = cell.gradient ? worst_gradient : worst_solid;
        total = std::max(total, worst);
        if (worst > 2) {
          std::cout << probe.name << " " << layer_name << " max error " << worst << "\n";
        }
      }
    }
  }
  std::cout << "inner glow probes: " << probes.size() << " documents, solid max error " << worst_solid
            << ", gradient max error " << worst_gradient << ", grain mean error " << worst_grain_mean << "\n";
  // Measured November 2026: solid 2, gradient 9, grain mean 2.6. The 9 is the smooth
  // three-stop gradient where Range 50 and Choke 30 saturate a Precise glow: Photoshop stops
  // short of the end stop there (3 levels at Range 100, 6 at Range 50). Two-stop gradients
  // stay within 6 in every method; the grain figure is a feathered square with fading stops.
  // The feathered squares stay inside the same figures over their soft edges; before the
  // glow folded into the layer's color they were 42 (solid) and 64 (gradient) levels off,
  // and before Precise and Choke measured a soft matte from its opaque boundary the feathered
  // solid squares of those documents were 60 (Precise) and 151 (Choke 30) off.
  CHECK(worst_solid <= 3);
  CHECK(worst_gradient <= 10);
  CHECK(worst_grain_mean <= 3.0);
}

}  // namespace

std::vector<patchy::test::TestCase> layer_style_inner_glow_gradient_tests() {
  return {
      {"compositor_inner_glow_gradient_follows_solid_coverage",
       compositor_inner_glow_gradient_follows_solid_coverage},
      {"compositor_inner_glow_gradient_matches_photoshop_samples",
       compositor_inner_glow_gradient_matches_photoshop_samples},
      {"inner_glow_without_gradient_keeps_color_rendering_and_bytes",
       inner_glow_without_gradient_keeps_color_rendering_and_bytes},
      {"inner_glow_gradient_noise_moves_opacity_only", inner_glow_gradient_noise_moves_opacity_only},
      {"psd_gradient_inner_glow_survives_copy_and_unrelated_edits",
       psd_gradient_inner_glow_survives_copy_and_unrelated_edits},
      {"asl_gradient_inner_glow_round_trips", asl_gradient_inner_glow_round_trips},
      {"psd_inner_glow_gradient_exchange_file_round_trips", psd_inner_glow_gradient_exchange_file_round_trips},
      {"photoshop_inner_glow_gradient_probes_match_if_available",
       photoshop_inner_glow_gradient_probes_match_if_available},
  };
}
