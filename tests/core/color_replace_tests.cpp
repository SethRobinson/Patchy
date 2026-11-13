// The Color Replacement tool's core rules (GitHub issue 720; core/color_replace.hpp,
// docs/color-replacement.md): the tolerance match is the Magic Wand's metric, each Sampling
// and Limits choice picks the pixels it says, each Mode paints with the blend math of the
// layer mode of that name, alpha never changes, a selection limits the change, overlapping
// dabs never compound, and 16 and 32-bit pixels give the 8-bit result.
#include "core/blend_math.hpp"
#include "core/color_replace.hpp"
#include "core/pixel_depth.hpp"
#include "core/pixel_tools.hpp"
#include "test_groups.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string_view>
#include <vector>

namespace {

using patchy::BitDepth;
using patchy::ColorReplaceLimits;
using patchy::ColorReplaceMode;
using patchy::ColorReplaceOptions;
using patchy::ColorReplaceRgb;
using patchy::ColorReplaceSampling;
using patchy::ColorReplaceStroke;
using patchy::EditColor;
using patchy::PixelBuffer;
using patchy::PixelFormat;
using patchy::Rect;

using Rgb = std::array<std::uint8_t, 3>;

constexpr Rgb kRed{210, 40, 40};
constexpr Rgb kDarkRed{150, 20, 20};
constexpr Rgb kGreen{40, 170, 60};
constexpr Rgb kWhite{255, 255, 255};
constexpr EditColor kBlue{30, 60, 220, 255};
constexpr EditColor kBackground{255, 255, 255, 255};

void put(PixelBuffer& pixels, int x, int y, const Rgb& color) {
  auto* px = pixels.pixel(x, y);
  px[0] = color[0];
  px[1] = color[1];
  px[2] = color[2];
  if (pixels.format().channels >= 4) {
    px[3] = 255;
  }
}

Rgb get(const PixelBuffer& pixels, int x, int y) {
  const auto* px = pixels.pixel(x, y);
  return {px[0], px[1], px[2]};
}

PixelBuffer filled(int width, int height, const Rgb& color, PixelFormat format = PixelFormat::rgb8()) {
  PixelBuffer pixels(width, height, format);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      put(pixels, x, y, color);
    }
  }
  return pixels;
}

void fill_rect(PixelBuffer& pixels, int left, int top, int right, int bottom, const Rgb& color) {
  for (int y = top; y <= bottom; ++y) {
    for (int x = left; x <= right; ++x) {
      put(pixels, x, y, color);
    }
  }
}

// What Color mode at full weight makes of `color` with the blue foreground.
Rgb recolored(const Rgb& color, ColorReplaceMode mode = ColorReplaceMode::Color) {
  return patchy::blend_rgb({kBlue.r, kBlue.g, kBlue.b}, color, patchy::color_replace_blend_mode(mode));
}

ColorReplaceOptions options_with(ColorReplaceLimits limits, int tolerance = 30, bool anti_alias = false,
                                 ColorReplaceMode mode = ColorReplaceMode::Color) {
  ColorReplaceOptions options;
  options.limits = limits;
  options.tolerance = tolerance;
  options.anti_alias = anti_alias;
  options.mode = mode;
  return options;
}

void color_replace_tolerance_is_the_magic_wand_metric() {
  // A percent is the accepted color distance as a share of one channel's range, and the wand
  // accepts twice its tolerance: 30% is a distance of 76, wand tolerance 38.
  CHECK(patchy::color_replace_wand_tolerance(0) == 0);
  CHECK(patchy::color_replace_wand_tolerance(1) == 1);
  CHECK(patchy::color_replace_wand_tolerance(30) == 38);
  CHECK(patchy::color_replace_wand_tolerance(60) == 77);
  CHECK(patchy::color_replace_wand_tolerance(100) == 128);
  CHECK(patchy::color_replace_wand_tolerance(250) == 128);

  const std::vector<Rgb> colors{{0, 0, 0},     {255, 255, 255}, {210, 40, 40},  {150, 20, 20}, {255, 128, 0},
                                {40, 170, 60}, {211, 40, 40},   {200, 100, 90}, {90, 60, 200}, {128, 128, 128}};
  for (const int percent : {0, 1, 10, 30, 50, 75, 99}) {
    const auto wand = patchy::color_replace_wand_tolerance(percent);
    for (const auto& a : colors) {
      for (const auto& b : colors) {
        const ColorReplaceRgb fa{static_cast<float>(a[0]), static_cast<float>(a[1]), static_cast<float>(a[2])};
        const ColorReplaceRgb fb{static_cast<float>(b[0]), static_cast<float>(b[1]), static_cast<float>(b[2])};
        const bool wand_accepts = patchy::color_within_tolerance(a.data(), b.data(), 3, wand);
        CHECK((patchy::color_replace_match(fa, fb, percent, false) == 1.0F) == wand_accepts);
        CHECK(patchy::color_replace_match(fa, fb, percent, false) == (wand_accepts ? 1.0F : 0.0F));
        const auto soft = patchy::color_replace_match(fa, fb, percent, true);
        CHECK(soft >= 0.0F && soft <= 1.0F);
        CHECK((soft == 1.0F) == wand_accepts);
      }
    }
  }
  // Tolerance 0 is an exact match even with anti-alias; 100 takes every color, 99 does not.
  CHECK(patchy::color_replace_match({210, 40, 40}, {211, 40, 40}, 0, true) == 0.0F);
  CHECK(patchy::color_replace_match({0, 0, 0}, {255, 255, 255}, 100, false) == 1.0F);
  CHECK(patchy::color_replace_match({0, 0, 0}, {255, 255, 255}, 99, true) == 0.0F);
  // The anti-alias fringe: a straight line from the tolerance distance to a quarter past it.
  // Tolerance 20 is wand 26, distance 52; the fringe ends at 65.
  CHECK(patchy::color_replace_match({52, 0, 0}, {0, 0, 0}, 20, true) == 1.0F);
  const auto halfway = patchy::color_replace_match({58.5F, 0, 0}, {0, 0, 0}, 20, true);
  CHECK(std::abs(halfway - 0.5F) < 0.001F);
  CHECK(patchy::color_replace_match({65, 0, 0}, {0, 0, 0}, 20, true) == 0.0F);
  CHECK(patchy::color_replace_match({58.5F, 0, 0}, {0, 0, 0}, 20, false) == 0.0F);
}

void color_replace_tokens_are_stable() {
  using namespace std::string_view_literals;
  CHECK(patchy::color_replace_mode_token(ColorReplaceMode::Hue) == "hue"sv);
  CHECK(patchy::color_replace_mode_token(ColorReplaceMode::Saturation) == "saturation"sv);
  CHECK(patchy::color_replace_mode_token(ColorReplaceMode::Color) == "color"sv);
  CHECK(patchy::color_replace_mode_token(ColorReplaceMode::Luminosity) == "luminosity"sv);
  CHECK(patchy::color_replace_sampling_token(ColorReplaceSampling::Continuous) == "continuous"sv);
  CHECK(patchy::color_replace_sampling_token(ColorReplaceSampling::Once) == "once"sv);
  CHECK(patchy::color_replace_sampling_token(ColorReplaceSampling::BackgroundSwatch) == "background"sv);
  CHECK(patchy::color_replace_limits_token(ColorReplaceLimits::Discontiguous) == "discontiguous"sv);
  CHECK(patchy::color_replace_limits_token(ColorReplaceLimits::Contiguous) == "contiguous"sv);
  CHECK(patchy::color_replace_limits_token(ColorReplaceLimits::FindEdges) == "findEdges"sv);
  for (const auto mode : {ColorReplaceMode::Hue, ColorReplaceMode::Saturation, ColorReplaceMode::Color,
                          ColorReplaceMode::Luminosity}) {
    CHECK(patchy::color_replace_mode_from_token(patchy::color_replace_mode_token(mode)) == mode);
  }
  for (const auto sampling : {ColorReplaceSampling::Continuous, ColorReplaceSampling::Once,
                              ColorReplaceSampling::BackgroundSwatch}) {
    CHECK(patchy::color_replace_sampling_from_token(patchy::color_replace_sampling_token(sampling)) == sampling);
  }
  for (const auto limits : {ColorReplaceLimits::Discontiguous, ColorReplaceLimits::Contiguous,
                            ColorReplaceLimits::FindEdges}) {
    CHECK(patchy::color_replace_limits_from_token(patchy::color_replace_limits_token(limits)) == limits);
  }
  CHECK(!patchy::color_replace_mode_from_token("normal").has_value());
  CHECK(!patchy::color_replace_sampling_from_token("").has_value());
  CHECK(!patchy::color_replace_limits_from_token("findedges").has_value());
  // The defaults the options bar starts from.
  const ColorReplaceOptions defaults;
  CHECK(defaults.mode == ColorReplaceMode::Color && defaults.limits == ColorReplaceLimits::Contiguous);
  CHECK(defaults.tolerance == 30 && defaults.anti_alias);
}

void color_replace_each_mode_paints_with_its_blend_mode() {
  const std::array<std::uint8_t, 4> original{210, 40, 40, 200};
  struct Case {
    ColorReplaceMode mode;
    patchy::BlendMode blend;
  };
  for (const auto& c : {Case{ColorReplaceMode::Hue, patchy::BlendMode::Hue},
                        Case{ColorReplaceMode::Saturation, patchy::BlendMode::Saturation},
                        Case{ColorReplaceMode::Color, patchy::BlendMode::Color},
                        Case{ColorReplaceMode::Luminosity, patchy::BlendMode::Luminosity}}) {
    CHECK(patchy::color_replace_blend_mode(c.mode) == c.blend);
    const auto expected = patchy::blend_rgb({kBlue.r, kBlue.g, kBlue.b}, {210, 40, 40}, c.blend);
    const auto full = patchy::color_replace_pixel_rgba8(original, kBlue, c.mode, 1.0F);
    CHECK(full[0] == expected[0] && full[1] == expected[1] && full[2] == expected[2]);
    CHECK(full[3] == 200);  // alpha never changes
    const auto half = patchy::color_replace_pixel_rgba8(original, kBlue, c.mode, 0.5F);
    for (std::size_t channel = 0; channel < 3; ++channel) {
      const auto middle = (static_cast<int>(original[channel]) + static_cast<int>(expected[channel])) / 2;
      CHECK(std::abs(static_cast<int>(half[channel]) - middle) <= 1);
    }
    CHECK(half[3] == 200);
    CHECK(patchy::color_replace_pixel_rgba8(original, kBlue, c.mode, 0.0F) == original);
  }
  // Hue and Color turn the red blue; Luminosity keeps it red; the four results differ.
  const auto hue = patchy::color_replace_pixel_rgba8(original, kBlue, ColorReplaceMode::Hue, 1.0F);
  const auto color = patchy::color_replace_pixel_rgba8(original, kBlue, ColorReplaceMode::Color, 1.0F);
  const auto luminosity = patchy::color_replace_pixel_rgba8(original, kBlue, ColorReplaceMode::Luminosity, 1.0F);
  const auto saturation = patchy::color_replace_pixel_rgba8(original, kBlue, ColorReplaceMode::Saturation, 1.0F);
  CHECK(hue[2] > hue[0] && color[2] > color[0]);
  CHECK(luminosity[0] > luminosity[2]);
  CHECK(hue != color && hue != luminosity && color != luminosity && saturation != luminosity);

  // A stroke in each mode writes exactly that.
  for (const auto mode : {ColorReplaceMode::Hue, ColorReplaceMode::Saturation, ColorReplaceMode::Color,
                          ColorReplaceMode::Luminosity}) {
    auto pixels = filled(24, 24, kRed);
    ColorReplaceStroke stroke(pixels, options_with(ColorReplaceLimits::Contiguous, 30, false, mode),
                              ColorReplaceSampling::Continuous, kBlue, kBackground);
    const auto dirty = stroke.round_dab(pixels, 12, 12, 10, 0);
    CHECK(!dirty.empty());
    CHECK(get(pixels, 12, 12) == recolored(kRed, mode));
    CHECK(get(pixels, 1, 1) == kRed);  // outside the tip
  }
}

// Two red blocks split by a white gap, all under one dab centered on the left block.
void color_replace_limits_pick_connected_or_all_matching_pixels() {
  const auto make = [] {
    auto pixels = filled(40, 20, kWhite);
    fill_rect(pixels, 4, 4, 15, 15, kRed);
    fill_rect(pixels, 24, 4, 35, 15, kRed);
    return pixels;
  };
  const auto run = [&](ColorReplaceLimits limits) {
    auto pixels = make();
    ColorReplaceStroke stroke(pixels, options_with(limits), ColorReplaceSampling::Continuous, kBlue, kBackground);
    stroke.round_dab(pixels, 10, 10, 80, 0);
    return pixels;
  };
  const auto discontiguous = run(ColorReplaceLimits::Discontiguous);
  CHECK(get(discontiguous, 10, 10) == recolored(kRed));
  CHECK(get(discontiguous, 30, 10) == recolored(kRed));
  CHECK(get(discontiguous, 20, 10) == kWhite);

  const auto contiguous = run(ColorReplaceLimits::Contiguous);
  CHECK(get(contiguous, 10, 10) == recolored(kRed));
  CHECK(get(contiguous, 4, 4) == recolored(kRed));
  CHECK(get(contiguous, 30, 10) == kRed);  // matches, but is not connected to the pointer
  CHECK(get(contiguous, 20, 10) == kWhite);

  const auto find_edges = run(ColorReplaceLimits::FindEdges);
  CHECK(get(find_edges, 10, 10) == recolored(kRed));
  CHECK(get(find_edges, 30, 10) == kRed);

  // Contiguous and Find Edges start at the pointer: a dab centered on a color that does not
  // match the sample changes nothing, while Discontiguous still takes every match under it.
  for (const auto limits : {ColorReplaceLimits::Contiguous, ColorReplaceLimits::FindEdges}) {
    auto pixels = make();
    ColorReplaceStroke stroke(pixels, options_with(limits), ColorReplaceSampling::BackgroundSwatch, kBlue,
                              EditColor{kRed[0], kRed[1], kRed[2], 255});
    CHECK(stroke.round_dab(pixels, 20, 10, 80, 0).empty());  // centered on the white gap
    CHECK(get(pixels, 10, 10) == kRed);
  }
  auto pixels = make();
  ColorReplaceStroke stroke(pixels, options_with(ColorReplaceLimits::Discontiguous),
                            ColorReplaceSampling::BackgroundSwatch, kBlue, EditColor{kRed[0], kRed[1], kRed[2], 255});
  CHECK(!stroke.round_dab(pixels, 20, 10, 80, 0).empty());
  CHECK(get(pixels, 10, 10) == recolored(kRed) && get(pixels, 30, 10) == recolored(kRed));
}

// Find Edges stops at an abrupt step that is still within tolerance; a gentle ramp to the same
// color does not stop it. Contiguous crosses both.
void color_replace_find_edges_stops_at_abrupt_steps() {
  // Tolerance 60: wand 77, so colors within distance 154 match and a step may be at most 77.
  // Red to dark red is a step of about 63 (crossed); red to (120, 0, 0) is about 107 (an edge).
  constexpr Rgb kDeepRed{120, 0, 0};
  auto stepped = filled(40, 12, kRed);
  fill_rect(stepped, 20, 0, 39, 11, kDeepRed);
  const auto run = [](PixelBuffer pixels, ColorReplaceLimits limits) {
    ColorReplaceStroke stroke(pixels, options_with(limits, 60), ColorReplaceSampling::Continuous, kBlue,
                              kBackground);
    stroke.round_dab(pixels, 8, 6, 100, 0);
    return pixels;
  };
  const auto contiguous = run(stepped, ColorReplaceLimits::Contiguous);
  CHECK(get(contiguous, 8, 6) == recolored(kRed));
  CHECK(get(contiguous, 30, 6) == recolored(kDeepRed));
  const auto find_edges = run(stepped, ColorReplaceLimits::FindEdges);
  CHECK(get(find_edges, 8, 6) == recolored(kRed));
  CHECK(get(find_edges, 19, 6) == recolored(kRed));
  CHECK(get(find_edges, 20, 6) == kDeepRed);  // the far side of the edge stays
  CHECK(get(find_edges, 30, 6) == kDeepRed);

  // The same two colors joined by a ramp: Find Edges follows it across.
  auto ramp = filled(40, 12, kRed);
  for (int x = 10; x < 30; ++x) {
    const auto t = static_cast<double>(x - 10) / 19.0;
    const Rgb color{static_cast<std::uint8_t>(std::lround(210.0 + (120.0 - 210.0) * t)),
                    static_cast<std::uint8_t>(std::lround(40.0 * (1.0 - t))),
                    static_cast<std::uint8_t>(std::lround(40.0 * (1.0 - t)))};
    fill_rect(ramp, x, 0, x, 11, color);
  }
  fill_rect(ramp, 30, 0, 39, 11, kDeepRed);
  const auto ramp_edges = run(ramp, ColorReplaceLimits::FindEdges);
  CHECK(get(ramp_edges, 35, 6) == recolored(kDeepRed));

  // A small step (red to dark red) is not an edge.
  auto small_step = filled(40, 12, kRed);
  fill_rect(small_step, 20, 0, 39, 11, kDarkRed);
  const auto small = run(small_step, ColorReplaceLimits::FindEdges);
  CHECK(get(small, 30, 6) == recolored(kDarkRed));
}

// The left half is red and the right half green; one stroke crosses both.
void color_replace_sampling_picks_which_color_is_replaced() {
  const auto make = [] {
    auto pixels = filled(60, 20, kRed);
    fill_rect(pixels, 30, 0, 59, 19, kGreen);
    return pixels;
  };
  const auto stroke_across = [&](ColorReplaceSampling sampling, EditColor background, int start_x, int end_x,
                                 ColorReplaceLimits limits = ColorReplaceLimits::Contiguous) {
    auto pixels = make();
    ColorReplaceStroke stroke(pixels, options_with(limits), sampling, kBlue, background);
    const int step = start_x <= end_x ? 2 : -2;
    for (int x = start_x; step > 0 ? x <= end_x : x >= end_x; x += step) {
      stroke.round_dab(pixels, x, 10, 8, 0);
    }
    return pixels;
  };
  // Continuous samples under the pointer at every dab, so both colors are replaced.
  const auto continuous = stroke_across(ColorReplaceSampling::Continuous, kBackground, 6, 54);
  CHECK(get(continuous, 10, 10) == recolored(kRed));
  CHECK(get(continuous, 50, 10) == recolored(kGreen));

  // Once keeps the color under the first dab: the green half is left alone.
  const auto once = stroke_across(ColorReplaceSampling::Once, kBackground, 6, 54);
  CHECK(get(once, 10, 10) == recolored(kRed));
  CHECK(get(once, 28, 10) == recolored(kRed));
  CHECK(get(once, 50, 10) == kGreen);
  // Started on the green side, it is the red half that stays.
  const auto once_green = stroke_across(ColorReplaceSampling::Once, kBackground, 54, 6);
  CHECK(get(once_green, 50, 10) == recolored(kGreen));
  CHECK(get(once_green, 10, 10) == kRed);

  // Background Swatch replaces the background color wherever the stroke starts.
  const EditColor green{kGreen[0], kGreen[1], kGreen[2], 255};
  const auto swatch = stroke_across(ColorReplaceSampling::BackgroundSwatch, green, 6, 54);
  CHECK(get(swatch, 10, 10) == kRed);
  CHECK(get(swatch, 50, 10) == recolored(kGreen));

  // Continuous reads the stroke-start pixels: a dab centered on pixels an earlier dab already
  // repainted still samples the red that was there, so it keeps replacing red.
  auto pixels = filled(40, 20, kRed);
  ColorReplaceStroke stroke(pixels, options_with(ColorReplaceLimits::Contiguous), ColorReplaceSampling::Continuous,
                            kBlue, kBackground);
  stroke.round_dab(pixels, 10, 10, 12, 0);
  CHECK(get(pixels, 12, 10) == recolored(kRed));
  stroke.round_dab(pixels, 12, 10, 12, 0);  // centered on a repainted pixel
  CHECK(get(pixels, 17, 10) == recolored(kRed));
  CHECK(stroke.sample().has_value());
  CHECK((*stroke.sample())[0] == 210.0F && (*stroke.sample())[2] == 40.0F);
}

void color_replace_keeps_alpha_and_skips_transparent_pixels() {
  PixelBuffer pixels(24, 24, PixelFormat::rgba8());
  for (int y = 0; y < 24; ++y) {
    for (int x = 0; x < 24; ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = kRed[0];
      px[1] = kRed[1];
      px[2] = kRed[2];
      px[3] = x < 8 ? 0 : (x < 16 ? 90 : 255);
    }
  }
  const auto before = pixels;
  ColorReplaceStroke stroke(pixels, options_with(ColorReplaceLimits::Discontiguous), ColorReplaceSampling::Continuous,
                            kBlue, kBackground);
  CHECK(!stroke.round_dab(pixels, 12, 12, 40, 0).empty());
  for (int y = 0; y < 24; ++y) {
    for (int x = 0; x < 24; ++x) {
      CHECK(pixels.pixel(x, y)[3] == before.pixel(x, y)[3]);
    }
  }
  CHECK(get(pixels, 3, 12) == kRed);  // fully transparent: left as it is
  CHECK(get(pixels, 12, 12) == recolored(kRed));
  CHECK(get(pixels, 20, 12) == recolored(kRed));
  // A dab whose center is transparent has nothing to sample.
  ColorReplaceStroke second(before, options_with(ColorReplaceLimits::Discontiguous), ColorReplaceSampling::Continuous,
                            kBlue, kBackground);
  auto untouched = before;
  CHECK(second.round_dab(untouched, 3, 12, 40, 0).empty());
  CHECK(get(untouched, 12, 12) == kRed);
}

void color_replace_selection_limits_the_change() {
  auto pixels = filled(40, 20, kRed);
  ColorReplaceStroke stroke(pixels, options_with(ColorReplaceLimits::Contiguous), ColorReplaceSampling::Continuous,
                            kBlue, kBackground);
  // Selected in full left of x = 15, half selected to x = 24, not selected from there.
  const ColorReplaceStroke::SelectionCoverage selection = [](std::int32_t x, std::int32_t) {
    return x < 15 ? 1.0F : (x < 25 ? 0.5F : 0.0F);
  };
  stroke.round_dab(pixels, 20, 10, 60, 0, selection);
  CHECK(get(pixels, 10, 10) == recolored(kRed));
  CHECK(get(pixels, 30, 10) == kRed);
  const auto half = get(pixels, 20, 10);
  const auto full = recolored(kRed);
  for (std::size_t channel = 0; channel < 3; ++channel) {
    const auto middle = (static_cast<int>(kRed[channel]) + static_cast<int>(full[channel])) / 2;
    CHECK(std::abs(static_cast<int>(half[channel]) - middle) <= 1);
  }
}

void color_replace_overlapping_dabs_do_not_compound() {
  // A soft tip: partial weights near the rim. A second pass over the same place changes nothing,
  // and overlapping dabs give each pixel its largest weight, not a sum.
  auto pixels = filled(60, 30, kRed);
  const auto start = pixels;
  ColorReplaceStroke stroke(pixels, options_with(ColorReplaceLimits::Contiguous), ColorReplaceSampling::Continuous,
                            kBlue, kBackground);
  stroke.round_dab(pixels, 20, 15, 20, 100);
  const auto after_one = pixels;
  const auto rim = get(pixels, 27, 15);
  CHECK(rim != kRed && rim != recolored(kRed));  // a partial weight
  CHECK(stroke.round_dab(pixels, 20, 15, 20, 100).empty());
  for (int y = 0; y < 30; ++y) {
    for (int x = 0; x < 60; ++x) {
      CHECK(get(pixels, x, y) == get(after_one, x, y));
    }
  }
  stroke.round_dab(pixels, 26, 15, 20, 100);
  // Each pixel is the stroke-start color repainted at the larger of the two dabs' coverages.
  for (int x = 8; x < 40; ++x) {
    const auto first = patchy::brush_coverage(static_cast<double>((x - 20) * (x - 20)), 10, 100);
    const auto second = patchy::brush_coverage(static_cast<double>((x - 26) * (x - 26)), 10, 100);
    const auto expected = patchy::color_replace_pixel_rgba8({kRed[0], kRed[1], kRed[2], 255}, kBlue,
                                                            ColorReplaceMode::Color, std::max(first, second));
    const auto actual = get(pixels, x, 15);
    CHECK(actual[0] == expected[0] && actual[1] == expected[1] && actual[2] == expected[2]);
  }
  CHECK(get(start, 20, 15) == kRed);  // the caller's copy of the start pixels is untouched
}

// A red block on white with one column of in-between pixels along its edge.
void color_replace_anti_alias_softens_the_edge() {
  // Red mixed 55% toward white: about 169 from red, past the tolerance distance (154 at 60%)
  // and inside the fringe (to 192.5). White itself is 307 away.
  constexpr Rgb kEdge{235, 158, 158};
  const auto make = [&] {
    auto pixels = filled(40, 20, kWhite);
    fill_rect(pixels, 0, 0, 19, 19, kRed);
    fill_rect(pixels, 20, 0, 20, 19, kEdge);
    return pixels;
  };
  const ColorReplaceRgb red{210, 40, 40};
  const ColorReplaceRgb edge{235, 158, 158};
  const auto share = patchy::color_replace_match(edge, red, 60, true);
  CHECK(share > 0.0F && share < 1.0F);
  CHECK(patchy::color_replace_match(edge, red, 60, false) == 0.0F);

  for (const auto limits : {ColorReplaceLimits::Contiguous, ColorReplaceLimits::Discontiguous}) {
    auto hard = make();
    ColorReplaceStroke hard_stroke(hard, options_with(limits, 60, false), ColorReplaceSampling::Continuous, kBlue,
                                   kBackground);
    hard_stroke.round_dab(hard, 10, 10, 60, 0);
    CHECK(get(hard, 10, 10) == recolored(kRed));
    CHECK(get(hard, 20, 10) == kEdge);
    CHECK(get(hard, 30, 10) == kWhite);

    auto soft = make();
    ColorReplaceStroke soft_stroke(soft, options_with(limits, 60, true), ColorReplaceSampling::Continuous, kBlue,
                                   kBackground);
    soft_stroke.round_dab(soft, 10, 10, 60, 0);
    CHECK(get(soft, 10, 10) == recolored(kRed));
    const auto expected =
        patchy::color_replace_pixel_rgba8({kEdge[0], kEdge[1], kEdge[2], 255}, kBlue, ColorReplaceMode::Color, share);
    const auto actual = get(soft, 20, 10);
    CHECK(actual[0] == expected[0] && actual[1] == expected[1] && actual[2] == expected[2]);
    CHECK(actual != kEdge);
    CHECK(get(soft, 30, 10) == kWhite);
  }
  // Find Edges gives no fringe even with Anti-alias on.
  auto edges = make();
  ColorReplaceStroke edges_stroke(edges, options_with(ColorReplaceLimits::FindEdges, 60, true),
                                  ColorReplaceSampling::Continuous, kBlue, kBackground);
  edges_stroke.round_dab(edges, 10, 10, 60, 0);
  CHECK(get(edges, 10, 10) == recolored(kRed));
  CHECK(get(edges, 20, 10) == kEdge);
}

// Red, a dark red band and green, with a half-transparent strip along the top.
PixelBuffer make_deep_base() {
  PixelBuffer base(48, 24, PixelFormat::rgba8());
  for (int y = 0; y < 24; ++y) {
    for (int x = 0; x < 48; ++x) {
      put(base, x, y, x < 24 ? kRed : kGreen);
      if (x >= 16 && x < 24) {
        put(base, x, y, kDarkRed);
      }
      base.pixel(x, y)[3] = y < 8 ? 120 : 255;
    }
  }
  return base;
}

// The same stroke on a 16-bit copy lands on the 8-bit result.
void check_deep_variant(BitDepth depth, int allowed) {
  const auto base = make_deep_base();
  for (const auto mode : {ColorReplaceMode::Hue, ColorReplaceMode::Saturation, ColorReplaceMode::Color,
                          ColorReplaceMode::Luminosity}) {
    const auto options = options_with(ColorReplaceLimits::Contiguous, 30, true, mode);
    auto shallow = base;
    ColorReplaceStroke shallow_stroke(shallow, options, ColorReplaceSampling::Once, kBlue, kBackground);
    auto deep = patchy::convert_pixel_buffer_depth(base, depth, patchy::SampleKind::Color);
    const auto deep_start = deep;
    ColorReplaceStroke deep_stroke(deep, options, ColorReplaceSampling::Once, kBlue, kBackground);
    for (int x = 6; x <= 40; x += 3) {
      shallow_stroke.round_dab(shallow, x, 12, 14, 50);
      deep_stroke.round_dab(deep, x, 12, 14, 50);
    }
    CHECK(deep.format().bit_depth == depth);
    bool changed = false;
    for (int y = 0; y < 24; ++y) {
      for (int x = 0; x < 48; ++x) {
        const auto expected = shallow.pixel(x, y);
        const auto actual = patchy::display_rgba8_at(deep, x, y);
        for (std::size_t channel = 0; channel < 3; ++channel) {
          CHECK(std::abs(static_cast<int>(actual[channel]) - static_cast<int>(expected[channel])) <= allowed);
        }
        CHECK(actual[3] == expected[3]);
        // Alpha is untouched at depth too.
        CHECK(patchy::load_pixel(deep.format(), deep.pixel(x, y))[3] ==
              patchy::load_pixel(deep_start.format(), deep_start.pixel(x, y))[3]);
        changed = changed || expected[0] != base.pixel(x, y)[0] || expected[1] != base.pixel(x, y)[1] ||
                  expected[2] != base.pixel(x, y)[2];
      }
    }
    CHECK(changed);
    CHECK(get(shallow, 44, 12) == kGreen);  // Once sampled the red: the green side stays
  }
}

void color_replace_16_bit_matches_8_bit() {
  check_deep_variant(BitDepth::UInt16, 1);
}

// A 32-bit layer is linear light. The match compares display-encoded colors, so the stroke
// changes the same pixels as at 8 bits; the paint is the Brush Mode's linear-light blend of the
// stroke-start pixel (core/blend_math_deep), and alpha is untouched.
void color_replace_32_bit_changes_the_same_pixels_in_linear_light() {
  const auto base = make_deep_base();
  for (const auto mode : {ColorReplaceMode::Hue, ColorReplaceMode::Saturation, ColorReplaceMode::Color,
                          ColorReplaceMode::Luminosity}) {
    const auto options = options_with(ColorReplaceLimits::Contiguous, 30, false, mode);
    auto shallow = base;
    ColorReplaceStroke shallow_stroke(shallow, options, ColorReplaceSampling::Once, kBlue, kBackground);
    auto deep = patchy::convert_pixel_buffer_depth(base, BitDepth::Float32, patchy::SampleKind::Color);
    const auto deep_start = deep;
    ColorReplaceStroke deep_stroke(deep, options, ColorReplaceSampling::Once, kBlue, kBackground);
    for (int x = 6; x <= 40; x += 3) {
      shallow_stroke.round_dab(shallow, x, 12, 14, 0);
      deep_stroke.round_dab(deep, x, 12, 14, 0);
    }
    const auto format = deep.format();
    CHECK(format.bit_depth == BitDepth::Float32);
    const patchy::DeepChannels paint{
        static_cast<float>(patchy::srgb_decode(kBlue.r / 255.0) * 255.0),
        static_cast<float>(patchy::srgb_decode(kBlue.g / 255.0) * 255.0),
        static_cast<float>(patchy::srgb_decode(kBlue.b / 255.0) * 255.0)};
    int painted = 0;
    for (int y = 0; y < 24; ++y) {
      for (int x = 0; x < 48; ++x) {
        const auto start = patchy::load_pixel(format, deep_start.pixel(x, y));
        const auto actual = patchy::load_pixel(format, deep.pixel(x, y));
        CHECK(actual[3] == start[3]);
        if (std::abs(y - 12) <= 3 && x >= 2 && x < 24) {
          // Well inside the dabs, on the reds: painted in full in both runs.
          ++painted;
          const auto expected =
              patchy::color_replace_pixel_deep(start, paint, mode, 1.0F, patchy::DeepDomain::Linear);
          for (std::size_t channel = 0; channel < 3; ++channel) {
            CHECK(std::abs(actual[channel] - expected[channel]) < 0.01F);
          }
          CHECK(get(shallow, x, y) == recolored(get(base, x, y), mode));
        } else if (std::abs(y - 12) > 8 || x >= 24) {
          // Outside the dabs, or green (Once sampled the red): untouched in both runs.
          CHECK(actual == start);
          CHECK(get(shallow, x, y) == get(base, x, y));
        }
      }
    }
    CHECK(painted > 100);
    CHECK(deep_stroke.sample().has_value() && shallow_stroke.sample().has_value());
    // Both strokes compared against the same display color.
    for (std::size_t channel = 0; channel < 3; ++channel) {
      CHECK(std::abs((*deep_stroke.sample())[channel] - (*shallow_stroke.sample())[channel]) < 0.6F);
    }
  }
}

// The dab's box may hang off the buffer, and a tip with a gap in the middle still joins up.
void color_replace_dab_clips_to_the_buffer_and_joins_across_tip_gaps() {
  auto pixels = filled(20, 20, kRed);
  ColorReplaceStroke stroke(pixels, options_with(ColorReplaceLimits::Contiguous), ColorReplaceSampling::Continuous,
                            kBlue, kBackground);
  const auto dirty = stroke.round_dab(pixels, 2, 2, 12, 0);
  CHECK(!dirty.empty());
  CHECK(dirty.x >= 0 && dirty.y >= 0 && dirty.x + dirty.width <= 20 && dirty.y + dirty.height <= 20);
  CHECK(get(pixels, 0, 0) == recolored(kRed));
  CHECK(get(pixels, 15, 15) == kRed);
  // A center outside the buffer has nothing to sample.
  CHECK(stroke.round_dab(pixels, -3, 10, 12, 0).empty());

  // A ring-shaped footprint (no coverage at the hot spot): Contiguous still starts there.
  auto ring_pixels = filled(21, 21, kRed);
  ColorReplaceStroke ring_stroke(ring_pixels, options_with(ColorReplaceLimits::Contiguous),
                                 ColorReplaceSampling::Continuous, kBlue, kBackground);
  std::vector<float> ring(21U * 21U, 0.0F);
  for (int y = 0; y < 21; ++y) {
    for (int x = 0; x < 21; ++x) {
      const auto d2 = (x - 10) * (x - 10) + (y - 10) * (y - 10);
      ring[static_cast<std::size_t>(y) * 21U + static_cast<std::size_t>(x)] = d2 >= 36 && d2 <= 81 ? 1.0F : 0.0F;
    }
  }
  CHECK(!ring_stroke.dab(ring_pixels, 10, 10, Rect{0, 0, 21, 21}, ring).empty());
  CHECK(get(ring_pixels, 10, 10) == kRed);             // no coverage at the center
  CHECK(get(ring_pixels, 10, 2) == recolored(kRed));   // on the ring
  CHECK(get(ring_pixels, 0, 0) == kRed);

  // The weights of one dab, called directly: tip coverage times the match.
  const auto source = filled(9, 9, kRed);
  std::vector<float> half_tip(81U, 0.5F);
  const auto weights = patchy::color_replace_dab_weights(source, Rect{0, 0, 9, 9}, 4, 4, {210, 40, 40},
                                                         options_with(ColorReplaceLimits::Contiguous), half_tip);
  CHECK(weights.size() == 81U);
  CHECK(std::all_of(weights.begin(), weights.end(), [](float weight) { return weight == 0.5F; }));
}

}  // namespace

std::vector<patchy::test::TestCase> color_replace_tests() {
  return {
      {"color_replace_tolerance_is_the_magic_wand_metric", color_replace_tolerance_is_the_magic_wand_metric},
      {"color_replace_tokens_are_stable", color_replace_tokens_are_stable},
      {"color_replace_each_mode_paints_with_its_blend_mode", color_replace_each_mode_paints_with_its_blend_mode},
      {"color_replace_limits_pick_connected_or_all_matching_pixels",
       color_replace_limits_pick_connected_or_all_matching_pixels},
      {"color_replace_find_edges_stops_at_abrupt_steps", color_replace_find_edges_stops_at_abrupt_steps},
      {"color_replace_sampling_picks_which_color_is_replaced", color_replace_sampling_picks_which_color_is_replaced},
      {"color_replace_keeps_alpha_and_skips_transparent_pixels", color_replace_keeps_alpha_and_skips_transparent_pixels},
      {"color_replace_selection_limits_the_change", color_replace_selection_limits_the_change},
      {"color_replace_overlapping_dabs_do_not_compound", color_replace_overlapping_dabs_do_not_compound},
      {"color_replace_anti_alias_softens_the_edge", color_replace_anti_alias_softens_the_edge},
      {"color_replace_16_bit_matches_8_bit", color_replace_16_bit_matches_8_bit},
      {"color_replace_32_bit_changes_the_same_pixels_in_linear_light",
       color_replace_32_bit_changes_the_same_pixels_in_linear_light},
      {"color_replace_dab_clips_to_the_buffer_and_joins_across_tip_gaps",
       color_replace_dab_clips_to_the_buffer_and_joins_across_tip_gaps},
  };
}
