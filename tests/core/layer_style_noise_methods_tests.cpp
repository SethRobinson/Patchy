// GitHub issue 742: Noise on the shadow and glow effects, and the Smooth and Stripes gradient
// methods. The pinned colors are Photoshop 2027's own (16-bit Gradient Fill captures, rounded
// to 8 bits; docs/gradients-methods.md, "Smooth and Stripes").
#include "core/adjustment_layer.hpp"
#include "core/blend_math.hpp"
#include "core/document.hpp"
#include "core/pattern_presets.hpp"
#include "core/vector_raster.hpp"
#include "core/vector_shape.hpp"
#include "formats/bmp_document_io.hpp"
#include "psd/psd_document_io.hpp"
#include "render/compositor.hpp"
#include "test_harness.hpp"
#include <algorithm>
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
using patchy::test::psd_layer_block_payload;
using patchy::test::psd_layer_extra_data;
using patchy::test::solid_rgb;
using patchy::test::solid_rgba;

struct MethodStop {
  int location;  // 0..4096
  int red;
  int green;
  int blue;
};

struct MethodSample {
  int x;  // pixel of a 4096 px wide fill; the gradient position is x / 4096
  int red;
  int green;
  int blue;
};

struct MethodProbe {
  const char* name;
  patchy::GradientInterpolationMethod method;
  int tolerance;
  std::vector<MethodStop> stops;
  std::vector<MethodSample> samples;
};

patchy::LayerStyleGradient probe_gradient(const MethodProbe& probe) {
  patchy::LayerStyleGradient gradient;
  gradient.interpolation = probe.method;
  for (const auto& stop : probe.stops) {
    gradient.color_stops.push_back(patchy::GradientColorStop{
        static_cast<float>(stop.location) / 4096.0F,
        patchy::RgbColor{static_cast<std::uint8_t>(stop.red), static_cast<std::uint8_t>(stop.green),
                         static_cast<std::uint8_t>(stop.blue)}});
  }
  gradient.alpha_stops = {{0.0F, 1.0F}, {1.0F, 1.0F}};
  return gradient;
}

void gradient_smooth_and_stripes_match_photoshop_samples() {
  using Method = patchy::GradientInterpolationMethod;
  const std::vector<MethodProbe> probes{
      {"bw-smooth", Method::Smooth, 3, {{0, 0, 0, 0}, {4096, 255, 255, 255}},
       {{128, 0, 0, 0}, {512, 4, 4, 4}, {1024, 29, 29, 29}, {1536, 63, 63, 63}, {2048, 99, 99, 99},
        {2560, 137, 137, 137}, {3072, 179, 179, 179}, {3584, 221, 221, 221}, {3968, 248, 248, 248}}},
      {"five-gray-smooth", Method::Smooth, 2,
       {{0, 0, 0, 0}, {1024, 200, 200, 200}, {2048, 50, 50, 50}, {3072, 255, 255, 255}, {4096, 100, 100, 100}},
       {{128, 2, 2, 2}, {512, 76, 76, 76}, {1024, 200, 200, 200}, {1536, 121, 121, 121}, {2048, 50, 50, 50},
        {2560, 146, 146, 146}, {3072, 255, 255, 255}, {3584, 174, 174, 174}, {3968, 115, 115, 115}}},
      {"rgb3-smooth", Method::Smooth, 3, {{0, 255, 0, 0}, {2048, 0, 255, 0}, {4096, 0, 0, 255}},
       {{128, 252, 47, 0}, {512, 238, 111, 0}, {1024, 208, 168, 0}, {1536, 155, 224, 0}, {2048, 0, 255, 0},
        {2560, 0, 224, 134}, {3072, 0, 170, 191}, {3584, 0, 113, 228}, {3968, 0, 48, 250}}},
      {"g3-smooth", Method::Smooth, 2, {{0, 80, 80, 80}, {2048, 200, 200, 200}, {4096, 120, 120, 120}},
       {{128, 85, 85, 85}, {512, 106, 106, 106}, {1024, 138, 138, 138}, {1536, 175, 175, 175}, {2048, 200, 200, 200},
        {2560, 186, 186, 186}, {3072, 159, 159, 159}, {3584, 138, 138, 138}, {3968, 124, 124, 124}}},
      {"five-gray-stripes", Method::Stripes, 0,
       {{0, 0, 0, 0}, {1024, 200, 200, 200}, {2048, 50, 50, 50}, {3072, 255, 255, 255}, {4096, 100, 100, 100}},
       {{128, 0, 0, 0}, {512, 0, 0, 0}, {1024, 200, 200, 200}, {1536, 200, 200, 200}, {2048, 50, 50, 50},
        {2560, 50, 50, 50}, {3072, 255, 255, 255}, {3584, 255, 255, 255}, {3968, 255, 255, 255}}},
      {"bwb-uneven-stripes", Method::Stripes, 0, {{0, 0, 0, 0}, {1229, 255, 255, 255}, {4096, 0, 0, 0}},
       {{128, 0, 0, 0}, {512, 0, 0, 0}, {1024, 0, 0, 0}, {1536, 255, 255, 255}, {2048, 255, 255, 255},
        {2560, 255, 255, 255}, {3072, 255, 255, 255}, {3584, 255, 255, 255}, {3968, 255, 255, 255}}},
      {"rgb3-stripes", Method::Stripes, 0, {{0, 255, 0, 0}, {2048, 0, 255, 0}, {4096, 0, 0, 255}},
       {{128, 255, 0, 0}, {512, 255, 0, 0}, {1024, 255, 0, 0}, {1536, 255, 0, 0}, {2048, 0, 255, 0},
        {2560, 0, 255, 0}, {3072, 0, 255, 0}, {3584, 0, 255, 0}, {3968, 0, 255, 0}}},
  };
  for (const auto& probe : probes) {
    const auto gradient = probe_gradient(probe);
    for (const auto& sample : probe.samples) {
      const auto position = static_cast<float>(sample.x) / 4096.0F;
      const auto color = patchy::gradient_color(gradient, position);
      const auto precise = patchy::gradient_color_precise(gradient, position, false, false);
      const int expected[3] = {sample.red, sample.green, sample.blue};
      const int actual[3] = {color.red, color.green, color.blue};
      for (int channel = 0; channel < 3; ++channel) {
        if (std::abs(actual[channel] - expected[channel]) > probe.tolerance) {
          const auto message = std::string(probe.name) + " at x " + std::to_string(sample.x) + " channel " +
                               std::to_string(channel) + ": " + std::to_string(actual[channel]) +
                               " against Photoshop's " + std::to_string(expected[channel]);
          patchy::test::check(false, message.c_str(), __FILE__, __LINE__);
        }
        // The precise sampler (16 and 32-bit renders) agrees with the 8-bit one.
        CHECK(std::abs(precise[static_cast<std::size_t>(channel)] - actual[channel]) <= 1.0);
      }
    }
  }
}

void gradient_smooth_and_stripes_ignore_smoothness_and_keep_other_methods() {
  using Method = patchy::GradientInterpolationMethod;
  patchy::LayerStyleGradient gradient;
  gradient.color_stops = {{0.0F, patchy::RgbColor{20, 40, 200}}, {0.5F, patchy::RgbColor{240, 200, 30}},
                          {1.0F, patchy::RgbColor{10, 160, 90}}};
  gradient.alpha_stops = {{0.0F, 1.0F}, {0.5F, 0.2F}, {1.0F, 0.8F}};
  const auto same = [](patchy::RgbColor a, patchy::RgbColor b) {
    return a.red == b.red && a.green == b.green && a.blue == b.blue;
  };
  for (const auto method : {Method::Smooth, Method::Stripes}) {
    gradient.interpolation = method;
    for (const auto position : {0.1F, 0.3F, 0.5F, 0.72F, 0.95F}) {
      gradient.smoothness = 4096;
      const auto full = patchy::gradient_color(gradient, position);
      const auto full_opacity = patchy::gradient_stop_opacity(gradient, position);
      gradient.smoothness = 0;
      CHECK(same(full, patchy::gradient_color(gradient, position)));
      CHECK(full_opacity == patchy::gradient_stop_opacity(gradient, position));
    }
    // The ends are the end stops, as for every method.
    CHECK(same(patchy::gradient_color(gradient, 0.0F), gradient.color_stops.front().color));
    CHECK(same(patchy::gradient_color(gradient, 1.0F), gradient.color_stops.back().color));
  }
  // Stripes holds each stop's color and opacity up to the next stop.
  gradient.interpolation = Method::Stripes;
  CHECK(same(patchy::gradient_color(gradient, 0.49F), gradient.color_stops[0].color));
  CHECK(same(patchy::gradient_color(gradient, 0.5F), gradient.color_stops[1].color));
  CHECK(same(patchy::gradient_color(gradient, 0.99F), gradient.color_stops[1].color));
  CHECK(patchy::gradient_stop_opacity(gradient, 0.49F) == 1.0F);
  CHECK(patchy::gradient_stop_opacity(gradient, 0.5F) == 0.2F);
  CHECK(patchy::gradient_stop_opacity(gradient, 0.99F) == 0.2F);
  // Smooth passes through each stop and never leaves the range of a segment's two ends.
  gradient.interpolation = Method::Smooth;
  CHECK(same(patchy::gradient_color(gradient, 0.5F), gradient.color_stops[1].color));
  CHECK(std::abs(patchy::gradient_stop_opacity(gradient, 0.5F) - 0.2F) < 1.0e-5F);
  for (int step = 0; step <= 100; ++step) {
    const auto opacity = patchy::gradient_stop_opacity(gradient, static_cast<float>(step) / 100.0F);
    CHECK(opacity >= 0.2F - 1.0e-5F && opacity <= 1.0F + 1.0e-5F);
  }
  // Two stops at one place, and a single stop, are handled.
  patchy::LayerStyleGradient degenerate;
  degenerate.interpolation = Method::Smooth;
  degenerate.color_stops = {{0.0F, patchy::RgbColor{0, 0, 0}}, {0.5F, patchy::RgbColor{100, 100, 100}},
                            {0.5F, patchy::RgbColor{200, 200, 200}}, {1.0F, patchy::RgbColor{255, 255, 255}}};
  for (const auto position : {0.25F, 0.5F, 0.75F}) {
    const auto color = patchy::gradient_color(degenerate, position);
    CHECK(color.red == color.green && color.green == color.blue);
  }
  degenerate.color_stops = {{0.3F, patchy::RgbColor{9, 8, 7}}};
  CHECK(same(patchy::gradient_color(degenerate, 0.7F), patchy::RgbColor{9, 8, 7}));
  degenerate.interpolation = Method::Stripes;
  CHECK(same(patchy::gradient_color(degenerate, 0.1F), patchy::RgbColor{9, 8, 7}));
}

void psd_gradient_smooth_and_stripes_round_trip() {
  using Method = patchy::GradientInterpolationMethod;
  for (const auto& [method, token] :
       {std::pair{Method::Smooth, std::string("Smoo")}, std::pair{Method::Stripes, std::string("GIMs")}}) {
    patchy::Document document(24, 24, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Background", solid_rgb(24, 24, 255, 255, 255));
    patchy::Layer styled(document.allocate_layer_id(), "Styled", solid_rgba(12, 12, 180, 40, 70, 255));
    auto& layer = document.add_layer(std::move(styled));
    layer.set_bounds(patchy::Rect{6, 6, 12, 12});
    patchy::LayerGradientFill fill;
    fill.enabled = true;
    fill.gradient.color_stops = {{0.0F, patchy::RgbColor{255, 0, 0}}, {0.5F, patchy::RgbColor{0, 255, 0}},
                                 {1.0F, patchy::RgbColor{0, 0, 255}}};
    fill.gradient.alpha_stops = {{0.0F, 1.0F}, {1.0F, 1.0F}};
    fill.gradient.interpolation = method;
    layer.layer_style().gradient_fills.push_back(fill);
    patchy::LayerStroke stroke;
    stroke.enabled = true;
    stroke.size = 2.0F;
    stroke.uses_gradient = true;
    stroke.gradient = fill.gradient;
    layer.layer_style().strokes.push_back(stroke);
    patchy::LayerOuterGlow glow;
    glow.enabled = true;
    glow.gradient = fill.gradient;
    layer.layer_style().outer_glows.push_back(glow);

    const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
    const auto payload = psd_layer_block_payload(psd_layer_extra_data(bytes, 1), "lfx2");
    CHECK(payload.has_value());
    const std::string text(payload->begin(), payload->end());
    // Photoshop's own four-character codes, as its files hold them.
    CHECK(text.find(token) != std::string::npos);
    const auto read = patchy::psd::DocumentIo::read(bytes);
    const auto& style = read.layers()[1].layer_style();
    CHECK(style.gradient_fills.size() == 1 && style.gradient_fills.front().gradient.interpolation == method);
    CHECK(style.strokes.size() == 1 && style.strokes.front().gradient.interpolation == method);
    CHECK(style.outer_glows.size() == 1 && style.outer_glows.front().gradient.has_value() &&
          style.outer_glows.front().gradient->interpolation == method);
    // The render moves with the method: a Stripes overlay has two flat halves.
    const auto flattened = patchy::Compositor{}.flatten_rgb8(read);
    CHECK(flattened.width() == 24);
  }
}

// A flat shadow: Size 0, Distance 0, the layer's own pixels hidden (Fill 0) and the shadow
// not knocked out, so every pixel of the square shows Opacity x the noisy coverage.
patchy::Document flat_shadow_document(float noise, std::int32_t left, std::int32_t top) {
  patchy::Document document(96, 96, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Backdrop", solid_rgb(96, 96, 255, 255, 255));
  patchy::Layer layer(document.allocate_layer_id(), "Content", solid_rgba(64, 64, 128, 128, 128, 255));
  layer.set_bounds(patchy::Rect{left, top, 64, 64});
  layer.set_fill_opacity(0.0F);
  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.blend_mode = patchy::BlendMode::Normal;
  shadow.color = patchy::RgbColor{0, 0, 0};
  shadow.opacity = 1.0F;
  shadow.distance = 0.0F;
  shadow.size = 0.0F;
  shadow.layer_conceals = false;
  shadow.noise = noise;
  layer.layer_style().drop_shadows.push_back(shadow);
  document.add_layer(std::move(layer));
  return document;
}

void layer_effect_noise_matches_photoshop_statistics_and_is_document_anchored() {
  // Photoshop 2027, a 64 x 64 black shadow at Opacity 100 on white: Noise 50 leaves a mean
  // coverage of 87.99% between 49.8% and 100%, Noise 100 a mean of 74.26% between 0 and
  // 100%, half the pixels untouched. Patchy's pattern is its own; the statistics are pinned.
  const auto coverage_stats = [](const patchy::PixelBuffer& image, std::int32_t left, std::int32_t top) {
    double sum = 0.0;
    int minimum = 255;
    int maximum = 0;
    int full = 0;
    for (std::int32_t y = top; y < top + 64; ++y) {
      for (std::int32_t x = left; x < left + 64; ++x) {
        const auto coverage = 255 - static_cast<int>(image.pixel(x, y)[0]);
        sum += coverage;
        minimum = std::min(minimum, coverage);
        maximum = std::max(maximum, coverage);
        full += coverage == 255 ? 1 : 0;
      }
    }
    struct Stats {
      double mean;
      int minimum;
      int maximum;
      double full_share;
    };
    return Stats{sum / (64.0 * 64.0 * 255.0), minimum, maximum, full / (64.0 * 64.0)};
  };
  const patchy::Compositor compositor;
  const auto clean = compositor.flatten_rgb8(flat_shadow_document(0.0F, 16, 16));
  const auto clean_stats = coverage_stats(clean, 16, 16);
  CHECK(clean_stats.minimum == 255 && clean_stats.maximum == 255);

  const auto half = compositor.flatten_rgb8(flat_shadow_document(50.0F, 16, 16));
  const auto half_stats = coverage_stats(half, 16, 16);
  CHECK(std::abs(half_stats.mean - 0.875) < 0.02);
  CHECK(half_stats.maximum == 255);
  CHECK(half_stats.minimum >= 126 && half_stats.minimum <= 136);
  CHECK(std::abs(half_stats.full_share - 0.5) < 0.05);

  const auto full = compositor.flatten_rgb8(flat_shadow_document(100.0F, 16, 16));
  const auto full_stats = coverage_stats(full, 16, 16);
  CHECK(std::abs(full_stats.mean - 0.75) < 0.02);
  CHECK(full_stats.maximum == 255);
  CHECK(full_stats.minimum <= 6);
  CHECK(std::abs(full_stats.full_share - 0.5) < 0.05);

  // The same render twice gives the same bytes, and nothing outside the square changes.
  const auto again = compositor.flatten_rgb8(flat_shadow_document(100.0F, 16, 16));
  CHECK(patchy::test::rgb_diff_metrics(full, again).differing_pixels == 0);
  CHECK(full.pixel(8, 8)[0] == 255 && full.pixel(90, 90)[0] == 255);

  // The grain belongs to the document, not the layer: where a moved layer still covers a
  // pixel, that pixel keeps its value (Photoshop does the same).
  const auto moved = compositor.flatten_rgb8(flat_shadow_document(100.0F, 23, 19));
  int compared = 0;
  for (std::int32_t y = 19; y < 80; ++y) {
    for (std::int32_t x = 23; x < 80; ++x) {
      CHECK(full.pixel(x, y)[0] == moved.pixel(x, y)[0]);
      ++compared;
    }
  }
  CHECK(compared == 61 * 57);
}

void layer_effect_noise_stays_inside_each_effect() {
  const patchy::Compositor compositor;
  const auto render = [&](auto configure) {
    patchy::Document document(120, 120, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Backdrop", solid_rgb(120, 120, 255, 255, 255));
    patchy::Layer layer(document.allocate_layer_id(), "Content", solid_rgba(40, 40, 200, 200, 200, 255));
    layer.set_bounds(patchy::Rect{40, 40, 40, 40});
    configure(layer.layer_style());
    document.add_layer(std::move(layer));
    return compositor.flatten_rgb8(document);
  };
  const auto differing = [](const patchy::PixelBuffer& a, const patchy::PixelBuffer& b, patchy::Rect area) {
    int count = 0;
    for (std::int32_t y = area.y; y < area.y + area.height; ++y) {
      for (std::int32_t x = area.x; x < area.x + area.width; ++x) {
        count += a.pixel(x, y)[0] != b.pixel(x, y)[0] ? 1 : 0;
      }
    }
    return count;
  };
  const patchy::Rect whole{0, 0, 120, 120};
  const patchy::Rect inside{40, 40, 40, 40};
  // Past Size 10 plus two pixels there is no blurred matte, so no grain either.
  const patchy::Rect far_corner{0, 0, 24, 24};

  for (const bool glow_effect : {false, true}) {
    const auto exterior = [&](float noise) {
      return render([&](patchy::LayerStyle& style) {
        if (glow_effect) {
          patchy::LayerOuterGlow glow;
          glow.enabled = true;
          glow.color = patchy::RgbColor{0, 0, 0};
          glow.opacity = 1.0F;
          glow.size = 10.0F;
          glow.range = 100.0F;
          glow.noise = noise;
          style.outer_glows.push_back(glow);
        } else {
          patchy::LayerDropShadow shadow;
          shadow.enabled = true;
          shadow.blend_mode = patchy::BlendMode::Normal;
          shadow.opacity = 1.0F;
          shadow.distance = 0.0F;
          shadow.size = 10.0F;
          shadow.noise = noise;
          style.drop_shadows.push_back(shadow);
        }
      });
    };
    const auto clean = exterior(0.0F);
    const auto noisy = exterior(100.0F);
    CHECK(differing(clean, noisy, whole) > 200);
    CHECK(differing(clean, noisy, inside) == 0);
    CHECK(differing(clean, noisy, far_corner) == 0);
    // Coverage never exceeds the blurred matte over 12%: the outermost ring stays faint.
    for (std::int32_t y = 30; y < 90; ++y) {
      const auto matte = 255 - static_cast<int>(clean.pixel(31, y)[0]);
      const auto grain = 255 - static_cast<int>(noisy.pixel(31, y)[0]);
      // Both are read back from 8-bit pixels, so allow the matte its rounding step.
      CHECK(grain <= static_cast<int>(std::ceil((matte + 1) / 0.12)) + 1);
    }
  }

  for (const bool glow_effect : {false, true}) {
    const auto interior = [&](float noise) {
      return render([&](patchy::LayerStyle& style) {
        if (glow_effect) {
          patchy::LayerInnerGlow glow;
          glow.enabled = true;
          glow.blend_mode = patchy::BlendMode::Normal;
          glow.color = patchy::RgbColor{0, 0, 0};
          glow.opacity = 1.0F;
          glow.size = 8.0F;
          glow.range = 100.0F;
          glow.noise = noise;
          style.inner_glows.push_back(glow);
        } else {
          patchy::LayerInnerShadow shadow;
          shadow.enabled = true;
          shadow.blend_mode = patchy::BlendMode::Normal;
          shadow.opacity = 1.0F;
          shadow.distance = 0.0F;
          shadow.size = 8.0F;
          shadow.noise = noise;
          style.inner_shadows.push_back(shadow);
        }
      });
    };
    const auto clean = interior(0.0F);
    const auto noisy = interior(100.0F);
    CHECK(differing(clean, noisy, inside) > 100);
    // Only the layer's own pixels change, and its middle, past the falloff, stays clean.
    CHECK(differing(clean, noisy, whole) == differing(clean, noisy, inside));
    CHECK(differing(clean, noisy, patchy::Rect{52, 52, 16, 16}) == 0);
  }
}

void psd_layer_effect_noise_round_trips() {
  patchy::Document document(24, 24, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(24, 24, 255, 255, 255));
  patchy::Layer styled(document.allocate_layer_id(), "Styled", solid_rgba(12, 12, 180, 40, 70, 255));
  auto& layer = document.add_layer(std::move(styled));
  layer.set_bounds(patchy::Rect{6, 6, 12, 12});
  patchy::LayerDropShadow drop;
  drop.enabled = true;
  drop.noise = 12.0F;
  layer.layer_style().drop_shadows.push_back(drop);
  patchy::LayerInnerShadow inner;
  inner.enabled = true;
  inner.noise = 34.0F;
  layer.layer_style().inner_shadows.push_back(inner);
  patchy::LayerOuterGlow outer_glow;
  outer_glow.enabled = true;
  outer_glow.noise = 56.0F;
  layer.layer_style().outer_glows.push_back(outer_glow);
  patchy::LayerInnerGlow inner_glow;
  inner_glow.enabled = true;
  inner_glow.noise = 100.0F;
  layer.layer_style().inner_glows.push_back(inner_glow);

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto read = patchy::psd::DocumentIo::read(bytes);
  const auto& style = read.layers()[1].layer_style();
  CHECK(style.drop_shadows.size() == 1 && style.drop_shadows.front().noise == 12.0F);
  CHECK(style.inner_shadows.size() == 1 && style.inner_shadows.front().noise == 34.0F);
  CHECK(style.outer_glows.size() == 1 && style.outer_glows.front().noise == 56.0F);
  CHECK(style.inner_glows.size() == 1 && style.inner_glows.front().noise == 100.0F);
  // A second save of the reread document keeps them too.
  const auto again = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(read));
  CHECK(again.layers()[1].layer_style().drop_shadows.front().noise == 12.0F);
  CHECK(again.layers()[1].layer_style().inner_glows.front().noise == 100.0F);

  // Without Noise the effects write 0, as before.
  patchy::Document plain(24, 24, patchy::PixelFormat::rgb8());
  plain.add_pixel_layer("Background", solid_rgb(24, 24, 255, 255, 255));
  patchy::Layer plain_styled(plain.allocate_layer_id(), "Styled", solid_rgba(12, 12, 180, 40, 70, 255));
  auto& plain_layer = plain.add_layer(std::move(plain_styled));
  patchy::LayerDropShadow plain_drop;
  plain_drop.enabled = true;
  plain_layer.layer_style().drop_shadows.push_back(plain_drop);
  const auto plain_read = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(plain));
  CHECK(plain_read.layers()[1].layer_style().drop_shadows.front().noise == 0.0F);
}

// The file Photoshop is asked to open (docs/psd-layer-styles.md): every setting GitHub issue
// 742 adds, on the layout of the Photoshop-made fixture below so the two flattens line up.
// Left in test-artifacts as layer-style-742.psd with Patchy's flatten beside it.
patchy::Document exchange_document() {
  patchy::Document document(640, 200, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(640, 200, 255, 255, 255));
  const auto add = [&](const char* name, patchy::Rect rect, std::uint8_t r, std::uint8_t g,
                       std::uint8_t b) -> patchy::Layer& {
    patchy::Layer layer(document.allocate_layer_id(), name, solid_rgba(rect.width, rect.height, r, g, b, 255));
    layer.set_bounds(rect);
    return document.add_layer(std::move(layer));
  };
  patchy::LayerStyleGradient three;
  three.color_stops = {{0.0F, patchy::RgbColor{230, 40, 40}}, {0.5F, patchy::RgbColor{250, 220, 40}},
                       {1.0F, patchy::RgbColor{30, 80, 220}}};
  three.alpha_stops = {{0.0F, 1.0F}, {1.0F, 1.0F}};
  three.angle_degrees = 0.0F;

  auto& shadows = add("noise-shadows", patchy::Rect{40, 50, 100, 100}, 150, 170, 200);
  patchy::LayerDropShadow drop;
  drop.enabled = true;
  drop.distance = 14.0F;
  drop.size = 12.0F;
  drop.noise = 30.0F;
  shadows.layer_style().drop_shadows.push_back(drop);
  patchy::LayerInnerShadow inner;
  inner.enabled = true;
  inner.distance = 8.0F;
  inner.size = 10.0F;
  inner.noise = 40.0F;
  shadows.layer_style().inner_shadows.push_back(inner);

  auto& glows = add("noise-glows", patchy::Rect{200, 50, 100, 100}, 240, 240, 240);
  patchy::LayerOuterGlow outer_glow;
  outer_glow.enabled = true;
  outer_glow.color = patchy::RgbColor{230, 60, 20};
  outer_glow.opacity = 0.9F;
  outer_glow.size = 16.0F;
  outer_glow.noise = 50.0F;
  glows.layer_style().outer_glows.push_back(outer_glow);
  patchy::LayerInnerGlow inner_glow;
  inner_glow.enabled = true;
  inner_glow.blend_mode = patchy::BlendMode::Normal;
  inner_glow.color = patchy::RgbColor{230, 60, 20};
  inner_glow.opacity = 0.9F;
  inner_glow.size = 14.0F;
  inner_glow.noise = 60.0F;
  glows.layer_style().inner_glows.push_back(inner_glow);

  auto& gradient_glow = add("glow-gradient", patchy::Rect{360, 50, 60, 100}, 200, 200, 200);
  patchy::LayerOuterGlow glow;
  glow.enabled = true;
  glow.opacity = 0.9F;
  glow.size = 18.0F;
  glow.noise = 25.0F;
  auto& glow_gradient = glow.gradient.emplace();
  glow_gradient.color_stops = {{0.0F, patchy::RgbColor{250, 220, 40}}, {1.0F, patchy::RgbColor{30, 80, 220}}};
  glow_gradient.alpha_stops = {{0.0F, 1.0F}, {1.0F, 1.0F}};
  glow_gradient.interpolation = patchy::GradientInterpolationMethod::Smooth;
  gradient_glow.layer_style().outer_glows.push_back(glow);

  auto& smooth = add("overlay-smooth", patchy::Rect{460, 30, 150, 60}, 128, 128, 128);
  patchy::LayerGradientFill smooth_fill;
  smooth_fill.enabled = true;
  smooth_fill.gradient = three;
  smooth_fill.gradient.interpolation = patchy::GradientInterpolationMethod::Smooth;
  smooth.layer_style().gradient_fills.push_back(smooth_fill);

  auto& stripes = add("overlay-stripes", patchy::Rect{460, 110, 150, 60}, 128, 128, 128);
  patchy::LayerGradientFill stripes_fill = smooth_fill;
  stripes_fill.gradient.interpolation = patchy::GradientInterpolationMethod::Stripes;
  stripes.layer_style().gradient_fills.push_back(stripes_fill);

  const auto preset = patchy::builtin_pattern_presets()[9];
  document.metadata().patterns.adopt(patchy::builtin_pattern_resource(preset.id));
  // Not linked with the layer, so the tile grid starts at the document origin plus the
  // offset in both applications (a linked pattern also depends on the layer's 'fxrp' point).
  auto& offsets = add("pattern-offsets", patchy::Rect{10, 10, 40, 30}, 90, 90, 90);
  patchy::LayerPatternOverlay overlay;
  overlay.enabled = true;
  overlay.pattern_id = preset.id;
  overlay.pattern_name = preset.english_name;
  overlay.link_with_layer = false;
  overlay.phase_x = 17.0F;
  overlay.phase_y = -9.0F;
  offsets.layer_style().pattern_overlays.push_back(overlay);
  auto& texture = add("texture-offsets", patchy::Rect{70, 10, 40, 30}, 90, 90, 90);
  patchy::LayerBevelEmboss bevel;
  bevel.enabled = true;
  bevel.texture.enabled = true;
  bevel.texture.pattern_id = preset.id;
  bevel.texture.pattern_name = preset.english_name;
  bevel.texture.link_with_layer = false;
  bevel.texture.phase_x = 5.0F;
  bevel.texture.phase_y = 11.0F;
  texture.layer_style().bevels.push_back(bevel);
  return document;
}

void psd_layer_style_742_exchange_file_round_trips() {
  using Method = patchy::GradientInterpolationMethod;
  const auto document = exchange_document();
  const auto saved = std::filesystem::path("test-artifacts") / "layer-style-742.psd";
  std::filesystem::create_directories(saved.parent_path());
  patchy::psd::DocumentIo::write_layered_rgb8_file(document, saved);
  const auto read = patchy::psd::DocumentIo::read_file(saved);
  const auto style_of = [&](std::string_view name) -> const patchy::LayerStyle& {
    const auto* layer = find_layer_named(read.layers(), name);
    CHECK(layer != nullptr);
    return layer->layer_style();
  };
  CHECK(style_of("noise-shadows").drop_shadows.front().noise == 30.0F);
  CHECK(style_of("noise-shadows").inner_shadows.front().noise == 40.0F);
  CHECK(style_of("noise-glows").outer_glows.front().noise == 50.0F);
  CHECK(style_of("noise-glows").inner_glows.front().noise == 60.0F);
  const auto& glow = style_of("glow-gradient").outer_glows.front();
  CHECK(glow.noise == 25.0F && glow.gradient.has_value() && glow.gradient->interpolation == Method::Smooth);
  CHECK(style_of("overlay-smooth").gradient_fills.front().gradient.interpolation == Method::Smooth);
  CHECK(style_of("overlay-stripes").gradient_fills.front().gradient.interpolation == Method::Stripes);
  CHECK(style_of("pattern-offsets").pattern_overlays.front().phase_x == 17.0F);
  CHECK(style_of("pattern-offsets").pattern_overlays.front().phase_y == -9.0F);
  CHECK(style_of("texture-offsets").bevels.front().texture.phase_x == 5.0F);
  CHECK(style_of("texture-offsets").bevels.front().texture.phase_y == 11.0F);
  // The offset moves the tile: the same overlay without it draws other pixels.
  auto unshifted = exchange_document();
  for (auto& layer : unshifted.layers()) {
    for (auto& found : layer.layer_style().pattern_overlays) {
      found.phase_x = 0.0F;
      found.phase_y = 0.0F;
    }
  }
  const auto shifted_render = patchy::Compositor{}.flatten_rgb8(document);
  const auto unshifted_render = patchy::Compositor{}.flatten_rgb8(unshifted);
  int moved = 0;
  for (std::int32_t y = 10; y < 40; ++y) {
    for (std::int32_t x = 10; x < 50; ++x) {
      moved += shifted_render.pixel(x, y)[0] != unshifted_render.pixel(x, y)[0] ? 1 : 0;
    }
  }
  CHECK(moved > 100);

  // The reread file renders what the document did, grain included.
  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  CHECK(patchy::test::rgb_diff_metrics(flattened, patchy::Compositor{}.flatten_rgb8(read)).differing_pixels == 0);
  patchy::test::write_rgb8_bmp_artifact("layer-style-742", flattened);
  // The Stripes overlay is three flat bands... two of them visible: red up to the middle stop, then yellow.
  CHECK(flattened.pixel(470, 140)[0] == 230 && flattened.pixel(470, 140)[1] == 40);
  CHECK(flattened.pixel(530, 140)[0] == 230 && flattened.pixel(530, 140)[1] == 40);
  CHECK(flattened.pixel(540, 140)[0] == 250 && flattened.pixel(540, 140)[1] == 220);
  CHECK(flattened.pixel(605, 140)[0] == 250 && flattened.pixel(605, 140)[1] == 220);
  // The Smooth overlay passes through the middle stop's color at the middle.
  CHECK(std::abs(flattened.pixel(535, 60)[0] - 250) <= 2 && std::abs(flattened.pixel(535, 60)[1] - 220) <= 3);
}

// Photoshop 2027's own file (local-test-fixtures/layer-style-742, made by fixture.jsx there):
// Noise on all four effects, a Smooth gradient Outer Glow and Gradient Overlays in the Smooth
// and Stripes methods. Patchy reads the settings, renders grain of the same strength and the
// overlays' colors, and keeps everything through an edit and a save.
void photoshop_noise_fixture_imports_edits_and_saves_if_available() {
  const auto dir = patchy::test::local_fixtures_root() / "layer-style-742";
  patchy::test::skip_unless_exists(dir / "photoshop-noise-methods.psd");
  patchy::test::skip_unless_exists(dir / "photoshop-noise-methods.bmp");
  auto document = patchy::psd::DocumentIo::read_file(dir / "photoshop-noise-methods.psd");
  const auto reference =
      patchy::Compositor{}.flatten_rgb8(patchy::bmp::DocumentIo::read_file(dir / "photoshop-noise-methods.bmp"));
  const auto* shadows = find_layer_named(document.layers(), "noise-shadows");
  const auto* glows = find_layer_named(document.layers(), "noise-glows");
  const auto* gradient_glow = find_layer_named(document.layers(), "glow-gradient");
  CHECK(shadows != nullptr && glows != nullptr && gradient_glow != nullptr);
  CHECK(shadows->layer_style().drop_shadows.front().noise == 30.0F);
  CHECK(shadows->layer_style().inner_shadows.front().noise == 40.0F);
  CHECK(glows->layer_style().outer_glows.front().noise == 50.0F);
  CHECK(glows->layer_style().inner_glows.front().noise == 60.0F);
  const auto& glow = gradient_glow->layer_style().outer_glows.front();
  CHECK(glow.noise == 25.0F);
  CHECK(glow.gradient.has_value() && glow.gradient->interpolation == patchy::GradientInterpolationMethod::Smooth);
  const auto* overlay_smooth = find_layer_named(document.layers(), "overlay-smooth");
  const auto* overlay_stripes = find_layer_named(document.layers(), "overlay-stripes");
  CHECK(overlay_smooth != nullptr && overlay_stripes != nullptr);
  CHECK(overlay_smooth->layer_style().gradient_fills.front().gradient.interpolation ==
        patchy::GradientInterpolationMethod::Smooth);
  CHECK(overlay_stripes->layer_style().gradient_fills.front().gradient.interpolation ==
        patchy::GradientInterpolationMethod::Stripes);

  const auto actual = patchy::Compositor{}.flatten_rgb8(document);
  CHECK(actual.width() == reference.width() && actual.height() == reference.height());
  patchy::test::write_rgb8_bmp_artifact("layer-style-742-photoshop-fixture", actual);
  // The two overlays, pixel for pixel (measured November 2026: Smooth within 2, Stripes exact).
  const auto overlay_error = [&](int top) {
    int worst = 0;
    for (int y = top; y < top + 60; ++y) {
      for (int x = 460; x < 610; ++x) {
        for (int channel = 0; channel < 3; ++channel) {
          worst = std::max(worst, std::abs(static_cast<int>(actual.pixel(x, y)[channel]) -
                                           static_cast<int>(reference.pixel(x, y)[channel])));
        }
      }
    }
    return worst;
  };
  std::cout << "overlay-smooth max error " << overlay_error(30) << " overlay-stripes max error " << overlay_error(110)
            << "\n";
  CHECK(overlay_error(30) <= 3);
  CHECK(overlay_error(110) <= 1);
  // The grain differs pixel by pixel (Patchy's own pattern), so compare what the eye sees:
  // the average color of each 20 x 20 block, and of each effect's whole area.
  const auto block_mean = [](const patchy::PixelBuffer& image, int left, int top, int size, int channel) {
    double sum = 0.0;
    for (int y = top; y < top + size; ++y) {
      for (int x = left; x < left + size; ++x) {
        sum += image.pixel(x, y)[channel];
      }
    }
    return sum / (size * size);
  };
  struct Area {
    const char* name;
    int left;
    int width;
    double block_limit;
    double area_limit;
  };
  // Measured November 2026: shadows 4.4 and 0.13, glows 15.1 and 0.12 (the worst block is a
  // glow corner, where the corner arc of Patchy's blur differs), gradient glow 1.1 and 0.1.
  for (const auto& area : {Area{"noise-shadows", 20, 140, 8.0, 1.5}, Area{"noise-glows", 180, 140, 20.0, 1.5},
                           Area{"glow-gradient", 340, 100, 8.0, 1.5}}) {
    double worst_block = 0.0;
    double area_error = 0.0;
    int blocks = 0;
    for (int top = 20; top + 20 <= 180; top += 20) {
      for (int left = area.left; left + 20 <= area.left + area.width; left += 20) {
        for (int channel = 0; channel < 3; ++channel) {
          const auto error =
              block_mean(actual, left, top, 20, channel) - block_mean(reference, left, top, 20, channel);
          worst_block = std::max(worst_block, std::abs(error));
          area_error += error;
          ++blocks;
        }
      }
    }
    area_error = std::abs(area_error / blocks);
    std::cout << area.name << " worst block mean error " << worst_block << " area mean error " << area_error << "\n";
    CHECK(worst_block <= area.block_limit);
    CHECK(area_error <= area.area_limit);
  }

  // Edit as the dialog would (the style is regenerated, not kept as raw bytes), save, reread.
  for (auto& layer : document.layers()) {
    std::erase_if(layer.unknown_psd_blocks(), [](const patchy::UnknownPsdBlock& block) {
      return block.key == "lfx2" || block.key == "lmfx" || block.key == "lrFX";
    });
    for (auto& shadow : layer.layer_style().drop_shadows) shadow.noise = 65.0F;
    for (auto& found : layer.layer_style().outer_glows) {
      if (found.gradient) found.gradient->interpolation = patchy::GradientInterpolationMethod::Stripes;
    }
  }
  const auto saved = std::filesystem::path("test-artifacts") / "layer-style-742-photoshop-edited.psd";
  std::filesystem::create_directories(saved.parent_path());
  patchy::psd::DocumentIo::write_layered_rgb8_file(document, saved);
  const auto reread = patchy::psd::DocumentIo::read_file(saved);
  const auto* reread_shadows = find_layer_named(reread.layers(), "noise-shadows");
  const auto* reread_glows = find_layer_named(reread.layers(), "noise-glows");
  const auto* reread_gradient = find_layer_named(reread.layers(), "glow-gradient");
  CHECK(reread_shadows != nullptr && reread_glows != nullptr && reread_gradient != nullptr);
  CHECK(reread_shadows->layer_style().drop_shadows.front().noise == 65.0F);
  CHECK(reread_shadows->layer_style().inner_shadows.front().noise == 40.0F);
  CHECK(reread_glows->layer_style().outer_glows.front().noise == 50.0F);
  CHECK(reread_glows->layer_style().inner_glows.front().noise == 60.0F);
  CHECK(reread_gradient->layer_style().outer_glows.front().noise == 25.0F);
  CHECK(reread_gradient->layer_style().outer_glows.front().gradient->interpolation ==
        patchy::GradientInterpolationMethod::Stripes);
}

// Photoshop 2027's own Gradient Fill layers in the two methods (local-test-fixtures/
// layer-style-742, five evenly spaced stops on 600 x 80): Patchy reads the method from the
// file, renders the fill again from its settings and stays close to Photoshop's flatten.
void photoshop_gradient_method_fills_match_reference_if_available() {
  using Method = patchy::GradientInterpolationMethod;
  const auto dir = patchy::test::local_fixtures_root() / "layer-style-742";
  struct Fixture {
    const char* stem;
    Method method;
    int max_error;
    double mean_error;
  };
  // Measured November 2026: Smooth max 2 and mean 0.08, Stripes identical.
  for (const auto& fixture : {Fixture{"photoshop-fill-smooth", Method::Smooth, 3, 0.5},
                              Fixture{"photoshop-fill-stripes", Method::Stripes, 1, 0.05}}) {
    patchy::test::skip_unless_exists(dir / (std::string(fixture.stem) + ".psd"));
    patchy::test::skip_unless_exists(dir / (std::string(fixture.stem) + ".bmp"));
    auto document = patchy::psd::DocumentIo::read_file(dir / (std::string(fixture.stem) + ".psd"));
    const auto reference = patchy::Compositor{}.flatten_rgb8(
        patchy::bmp::DocumentIo::read_file(dir / (std::string(fixture.stem) + ".bmp")));
    patchy::Layer* fill = nullptr;
    for (auto& layer : document.layers()) {
      if (std::as_const(layer).vector_shape() != nullptr) fill = &layer;
    }
    CHECK(fill != nullptr);
    if (fill == nullptr) return;
    CHECK(std::as_const(*fill).vector_shape()->fill.gradient.interpolation == fixture.method);
    patchy::update_vector_shape_raster(*fill, patchy::Rect::from_size(document.width(), document.height()),
                                       &document.metadata().patterns);
    const auto actual = patchy::Compositor{}.flatten_rgb8(document);
    CHECK(actual.width() == reference.width() && actual.height() == reference.height());
    patchy::test::write_rgb8_bmp_artifact(std::string("layer-style-742-") + fixture.stem, actual);
    const auto metrics = patchy::test::rgb_diff_metrics(actual, reference);
    std::cout << fixture.stem << " max error " << metrics.max_channel_delta << " mean "
              << metrics.mean_abs_channel_delta << "\n";
    CHECK(metrics.max_channel_delta <= fixture.max_error);
    CHECK(metrics.mean_abs_channel_delta <= fixture.mean_error);

    // Saved by Patchy and read again, the method is still there.
    const auto reread = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(document));
    bool found = false;
    for (const auto& layer : reread.layers()) {
      if (const auto* shape = layer.vector_shape(); shape != nullptr) {
        CHECK(shape->fill.gradient.interpolation == fixture.method);
        found = true;
      }
    }
    CHECK(found);
  }
}

// A Gradient Map's 'grdm' block names the method with the same four characters
// (Photoshop 2027 writes Smoo and GIMs there), and the layer's own text form keeps it.
void psd_gradient_map_smooth_and_stripes_round_trip() {
  using Method = patchy::GradientInterpolationMethod;
  for (const auto& [method, token] :
       {std::pair{Method::Smooth, std::string("Smoo")}, std::pair{Method::Stripes, std::string("GIMs")}}) {
    patchy::Document document(8, 8, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Background", solid_rgb(8, 8, 120, 120, 120));
    patchy::AdjustmentSettings settings;
    settings.kind = patchy::AdjustmentKind::GradientMap;
    settings.gradient_map.gradient.interpolation = method;
    patchy::Layer layer(document.allocate_layer_id(), "Gradient Map", patchy::LayerKind::Adjustment);
    patchy::configure_adjustment_layer(layer, settings);
    const auto from_metadata = patchy::adjustment_settings_from_layer(layer);
    CHECK(from_metadata.has_value() && from_metadata->gradient_map.gradient.interpolation == method);
    document.add_layer(std::move(layer));

    const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
    const auto grdm = psd_layer_block_payload(psd_layer_extra_data(bytes, 1), "grdm");
    CHECK(grdm.has_value() && grdm->size() > 8U);
    if (grdm.has_value() && grdm->size() > 8U) {
      CHECK(std::string(grdm->begin() + 4, grdm->begin() + 8) == token);
    }
    const auto read = patchy::psd::DocumentIo::read(bytes);
    const auto reread = patchy::adjustment_settings_from_layer(read.layers()[1]);
    CHECK(reread.has_value() && reread->gradient_map.gradient.interpolation == method);
  }
}

void photoshop_gradient_map_methods_import_if_available() {
  using Method = patchy::GradientInterpolationMethod;
  const auto dir = patchy::test::local_fixtures_root() / "layer-style-742";
  for (const auto& [stem, method] : {std::pair{std::string("photoshop-gradient-map-smooth"), Method::Smooth},
                                     std::pair{std::string("photoshop-gradient-map-stripes"), Method::Stripes}}) {
    patchy::test::skip_unless_exists(dir / (stem + ".psd"));
    patchy::test::skip_unless_exists(dir / (stem + ".bmp"));
    const auto document = patchy::psd::DocumentIo::read_file(dir / (stem + ".psd"));
    const patchy::Layer* map = nullptr;
    for (const auto& layer : document.layers()) {
      if (layer.kind() == patchy::LayerKind::Adjustment) map = &layer;
    }
    CHECK(map != nullptr);
    if (map == nullptr) return;
    const auto settings = patchy::adjustment_settings_from_layer(*map);
    CHECK(settings.has_value() && settings->kind == patchy::AdjustmentKind::GradientMap);
    CHECK(settings->gradient_map.gradient.interpolation == method);
    // A black to white ramp under the map: Photoshop's flatten is the gradient itself.
    const auto reference =
        patchy::Compositor{}.flatten_rgb8(patchy::bmp::DocumentIo::read_file(dir / (stem + ".bmp")));
    const auto actual = patchy::Compositor{}.flatten_rgb8(document);
    const auto metrics = patchy::test::rgb_diff_metrics(actual, reference);
    std::cout << stem << " max error " << metrics.max_channel_delta << " mean " << metrics.mean_abs_channel_delta
              << "\n";
    // Measured November 2026: Smooth 2, Stripes 0.
    CHECK(metrics.max_channel_delta <= (method == Method::Stripes ? 1 : 3));
  }
}

}  // namespace

std::vector<patchy::test::TestCase> layer_style_noise_methods_tests() {
  return {
      {"psd_gradient_map_smooth_and_stripes_round_trip", psd_gradient_map_smooth_and_stripes_round_trip},
      {"photoshop_gradient_map_methods_import_if_available", photoshop_gradient_map_methods_import_if_available},
      {"photoshop_gradient_method_fills_match_reference_if_available",
       photoshop_gradient_method_fills_match_reference_if_available},
      {"psd_layer_style_742_exchange_file_round_trips", psd_layer_style_742_exchange_file_round_trips},
      {"photoshop_noise_fixture_imports_edits_and_saves_if_available",
       photoshop_noise_fixture_imports_edits_and_saves_if_available},
      {"gradient_smooth_and_stripes_match_photoshop_samples", gradient_smooth_and_stripes_match_photoshop_samples},
      {"gradient_smooth_and_stripes_ignore_smoothness_and_keep_other_methods",
       gradient_smooth_and_stripes_ignore_smoothness_and_keep_other_methods},
      {"psd_gradient_smooth_and_stripes_round_trip", psd_gradient_smooth_and_stripes_round_trip},
      {"layer_effect_noise_matches_photoshop_statistics_and_is_document_anchored",
       layer_effect_noise_matches_photoshop_statistics_and_is_document_anchored},
      {"layer_effect_noise_stays_inside_each_effect", layer_effect_noise_stays_inside_each_effect},
      {"psd_layer_effect_noise_round_trips", psd_layer_effect_noise_round_trips},
  };
}
