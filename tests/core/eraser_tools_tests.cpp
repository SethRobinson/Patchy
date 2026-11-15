// The Background Eraser's and the Magic Eraser's core rules (GitHub issue 727;
// core/background_eraser.hpp, core magic_erase, docs/eraser-tools.md). The Background Eraser
// erases exactly the pixels the Color Replacement tool repaints for the same dab (they share
// core/color_match_stroke.hpp), and the Magic Eraser erases exactly the region the Fill tool
// floods for the same click (they share the flood's region pass). Both follow the Eraser under
// a transparency lock.
#include "core/background_eraser.hpp"
#include "core/color_match_stroke.hpp"
#include "core/color_replace.hpp"
#include "core/document.hpp"
#include "core/document_depth.hpp"
#include "core/pixel_depth.hpp"
#include "core/pixel_tools.hpp"
#include "core_test_support.hpp"
#include "test_groups.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using patchy::BackgroundEraserOptions;
using patchy::BackgroundEraserStroke;
using patchy::BitDepth;
using patchy::ColorMatchLimits;
using patchy::ColorMatchSampling;
using patchy::Document;
using patchy::EditColor;
using patchy::LayerId;
using patchy::PixelBuffer;
using patchy::PixelFormat;
using patchy::Rect;
using patchy::test::tool_options;

using Rgba = std::array<std::uint8_t, 4>;

constexpr Rgba kRed{210, 40, 40, 255};
constexpr Rgba kDarkRed{150, 20, 20, 255};
// 85 from kRed: past the 30% tolerance distance (76), inside its anti-alias fringe (95).
constexpr Rgba kNearRed{125, 40, 40, 255};
constexpr Rgba kGreen{40, 170, 60, 255};
constexpr EditColor kWhite{255, 255, 255, 255};
constexpr EditColor kBlue{30, 60, 220, 255};

void put(PixelBuffer& pixels, int x, int y, const Rgba& color) {
  auto* px = pixels.pixel(x, y);
  for (std::uint16_t channel = 0; channel < pixels.format().channels && channel < 4; ++channel) {
    px[channel] = color[channel];
  }
}

Rgba get(const PixelBuffer& pixels, int x, int y) {
  const auto* px = pixels.pixel(x, y);
  return {px[0], px[1], px[2], pixels.format().channels >= 4 ? px[3] : std::uint8_t{255}};
}

void fill_rect(PixelBuffer& pixels, int left, int top, int right, int bottom, const Rgba& color) {
  for (int y = top; y <= bottom; ++y) {
    for (int x = left; x <= right; ++x) {
      put(pixels, x, y, color);
    }
  }
}

PixelBuffer filled(int width, int height, const Rgba& color, PixelFormat format = PixelFormat::rgba8()) {
  PixelBuffer pixels(width, height, format);
  fill_rect(pixels, 0, 0, width - 1, height - 1, color);
  return pixels;
}

BackgroundEraserOptions eraser_options(ColorMatchLimits limits, int tolerance = 30) {
  BackgroundEraserOptions options;
  options.limits = limits;
  options.tolerance = tolerance;
  return options;
}

// Red on the left, a column of in-between pixels, dark red, a green wall, red again.
PixelBuffer make_match_scene() {
  auto pixels = filled(24, 12, kRed);
  fill_rect(pixels, 8, 0, 8, 11, kNearRed);
  fill_rect(pixels, 9, 0, 13, 11, kDarkRed);
  fill_rect(pixels, 14, 0, 16, 11, kGreen);
  // (17..23 stays red: matching, but cut off from the left by the wall.)
  return pixels;
}

void background_eraser_erases_the_pixels_color_replacement_repaints() {
  // One dab with a full tip over the whole scene, for every Limits choice: the weights are the
  // shared match's, the Background Eraser lowers alpha by them and Color Replacement paints by
  // them, so the two tools change exactly the same pixels.
  const auto source = make_match_scene();
  const Rect rect{0, 0, 24, 12};
  const std::vector<float> footprint(static_cast<std::size_t>(rect.width) * rect.height, 1.0F);
  const auto sample = patchy::color_match_pixel_at(source, 3, 6).rgb;
  for (const auto limits :
       {ColorMatchLimits::Discontiguous, ColorMatchLimits::Contiguous, ColorMatchLimits::FindEdges}) {
    const auto options = eraser_options(limits);
    const auto weights = patchy::color_match_dab_weights(source, rect, 3, 6, sample, options.match(), footprint);

    auto erased = source;
    BackgroundEraserStroke erase(source, options, ColorMatchSampling::Continuous, kWhite, false);
    CHECK(!erase.dab(erased, 3, 6, rect, footprint).empty());

    patchy::ColorReplaceOptions replace_options;
    replace_options.limits = limits;
    replace_options.tolerance = options.tolerance;
    replace_options.anti_alias = true;
    auto replaced = source;
    patchy::ColorReplaceStroke replace(source, replace_options, ColorMatchSampling::Continuous, kBlue, kWhite);
    CHECK(!replace.dab(replaced, 3, 6, rect, footprint).empty());

    int full = 0;
    int partial = 0;
    int untouched = 0;
    for (int y = 0; y < rect.height; ++y) {
      for (int x = 0; x < rect.width; ++x) {
        const auto weight = weights[static_cast<std::size_t>(y) * rect.width + x];
        const auto before = get(source, x, y);
        const auto after = get(erased, x, y);
        // The color never changes; the alpha falls by the weight.
        CHECK(after[0] == before[0] && after[1] == before[1] && after[2] == before[2]);
        CHECK(after == patchy::background_eraser_pixel_rgba8(before, weight, false, kWhite));
        const auto painted = get(replaced, x, y);
        CHECK(painted == patchy::color_replace_pixel_rgba8(before, kBlue, patchy::ColorReplaceMode::Color, weight));
        // The same pixels: erased if and only if repainted.
        CHECK((after[3] != before[3]) == (painted != before));
        if (weight >= 1.0F) {
          CHECK(after[3] == 0);
          ++full;
        } else if (weight > 0.0F) {
          CHECK(after[3] > 0 && after[3] < 255);
          ++partial;
        } else {
          CHECK(after[3] == 255);
          ++untouched;
        }
      }
    }
    CHECK(full > 0 && untouched > 0);
    // Find Edges gives no partly matching fringe; the other two erase the in-between column in part.
    CHECK((partial > 0) == (limits != ColorMatchLimits::FindEdges));
  }
}

void background_eraser_limits_pick_connected_or_all_matching_pixels() {
  const auto source = make_match_scene();
  const Rect rect{0, 0, 24, 12};
  const std::vector<float> footprint(static_cast<std::size_t>(rect.width) * rect.height, 1.0F);
  const auto erase_with = [&](ColorMatchLimits limits) {
    auto pixels = source;
    BackgroundEraserStroke stroke(source, eraser_options(limits), ColorMatchSampling::Continuous, kWhite, false);
    (void)stroke.dab(pixels, 3, 6, rect, footprint);
    return pixels;
  };
  // Dark red is 66 from red: inside the 30% tolerance (76), but an abrupt step (over 38).
  const auto discontiguous = erase_with(ColorMatchLimits::Discontiguous);
  CHECK(get(discontiguous, 3, 6)[3] == 0);
  CHECK(get(discontiguous, 11, 6)[3] == 0);
  CHECK(get(discontiguous, 15, 6)[3] == 255);  // the green wall
  CHECK(get(discontiguous, 20, 6)[3] == 0);    // red beyond the wall: matching, so erased

  const auto contiguous = erase_with(ColorMatchLimits::Contiguous);
  CHECK(get(contiguous, 3, 6)[3] == 0);
  CHECK(get(contiguous, 8, 6)[3] > 0 && get(contiguous, 8, 6)[3] < 255);  // the fringe column, in part
  // The fringe column does not carry the join: dark red beyond it is cut off.
  CHECK(get(contiguous, 11, 6)[3] == 255);
  CHECK(get(contiguous, 15, 6)[3] == 255);
  CHECK(get(contiguous, 20, 6)[3] == 255);

  // Without the in-between column the dark red joins up under Contiguous and stops Find Edges.
  auto joined_source = source;
  fill_rect(joined_source, 8, 0, 8, 11, kRed);
  const auto erase_joined = [&](ColorMatchLimits limits) {
    auto pixels = joined_source;
    BackgroundEraserStroke stroke(joined_source, eraser_options(limits), ColorMatchSampling::Continuous, kWhite,
                                  false);
    (void)stroke.dab(pixels, 3, 6, rect, footprint);
    return pixels;
  };
  const auto joined = erase_joined(ColorMatchLimits::Contiguous);
  CHECK(get(joined, 11, 6)[3] == 0);
  CHECK(get(joined, 20, 6)[3] == 255);
  const auto edges = erase_joined(ColorMatchLimits::FindEdges);
  CHECK(get(edges, 8, 6)[3] == 0);
  CHECK(get(edges, 11, 6)[3] == 255);  // the step from red to dark red is an edge
  CHECK(get(edges, 20, 6)[3] == 255);
}

void background_eraser_sampling_picks_which_color_is_erased() {
  // A stroke from the red half into the green half.
  auto source = filled(40, 12, kRed);
  fill_rect(source, 20, 0, 39, 11, kGreen);
  const auto stroke_with = [&](ColorMatchSampling sampling, EditColor background) {
    auto pixels = source;
    BackgroundEraserStroke stroke(source, eraser_options(ColorMatchLimits::Contiguous), sampling, background, false);
    for (int x = 4; x <= 36; x += 2) {
      (void)stroke.round_dab(pixels, x, 6, 6, 0);
    }
    return pixels;
  };
  // Continuous: every dab erases the color under its own center.
  const auto continuous = stroke_with(ColorMatchSampling::Continuous, kWhite);
  CHECK(get(continuous, 6, 6)[3] == 0);
  CHECK(get(continuous, 32, 6)[3] == 0);
  // Once: the color under the first dab, red, for the whole stroke.
  const auto once = stroke_with(ColorMatchSampling::Once, kWhite);
  CHECK(get(once, 6, 6)[3] == 0);
  CHECK(get(once, 32, 6)[3] == 255);
  // Background Swatch: the background color, here the green, wherever the stroke starts.
  const auto swatch = stroke_with(ColorMatchSampling::BackgroundSwatch, EditColor{40, 170, 60, 255});
  CHECK(get(swatch, 6, 6)[3] == 255);
  CHECK(get(swatch, 32, 6)[3] == 0);
  // Outside the brush nothing changes.
  CHECK(get(continuous, 6, 0)[3] == 255);
}

void background_eraser_follows_the_eraser_under_a_transparency_lock() {
  // Lock Transparent Pixels (and the locked Background): the pixels move toward the background
  // color and keep their alpha; fully transparent pixels stay empty.
  PixelBuffer source(24, 8, PixelFormat::rgba8());
  for (int y = 0; y < 8; ++y) {
    for (int x = 0; x < 24; ++x) {
      put(source, x, y, {kRed[0], kRed[1], kRed[2], static_cast<std::uint8_t>(x < 8 ? 0 : (x < 16 ? 90 : 255))});
    }
  }
  const Rect rect{0, 0, 24, 8};
  const std::vector<float> full(static_cast<std::size_t>(rect.width) * rect.height, 1.0F);
  auto locked = source;
  BackgroundEraserStroke locked_stroke(source, eraser_options(ColorMatchLimits::Discontiguous),
                                       ColorMatchSampling::Continuous, kWhite, true);
  CHECK(!locked_stroke.dab(locked, 20, 4, rect, full).empty());
  CHECK((get(locked, 20, 4) == Rgba{255, 255, 255, 255}));
  CHECK((get(locked, 12, 4) == Rgba{255, 255, 255, 90}));
  CHECK(get(locked, 3, 4) == get(source, 3, 4));

  // The same layer unlocked erases to transparency and keeps the color.
  auto unlocked = source;
  BackgroundEraserStroke unlocked_stroke(source, eraser_options(ColorMatchLimits::Discontiguous),
                                         ColorMatchSampling::Continuous, kWhite, false);
  CHECK(!unlocked_stroke.dab(unlocked, 20, 4, rect, full).empty());
  CHECK((get(unlocked, 20, 4) == Rgba{kRed[0], kRed[1], kRed[2], 0}));
  CHECK((get(unlocked, 12, 4) == Rgba{kRed[0], kRed[1], kRed[2], 0}));

  // A layer without alpha has no transparency to erase to: the background color, at the weight.
  const auto opaque = filled(12, 8, kRed, PixelFormat::rgb8());
  auto painted = opaque;
  BackgroundEraserStroke opaque_stroke(opaque, eraser_options(ColorMatchLimits::Discontiguous),
                                       ColorMatchSampling::Continuous, kWhite, false);
  const std::vector<float> half(12U * 8U, 0.5F);
  CHECK(!opaque_stroke.dab(painted, 6, 4, Rect{0, 0, 12, 8}, half).empty());
  CHECK(painted.format().channels == 3);
  const auto mixed = get(painted, 6, 4);
  CHECK(mixed == patchy::background_eraser_pixel_rgba8(kRed, 0.5F, true, kWhite));
  CHECK(mixed[0] > kRed[0] && mixed[0] < 255 && mixed[1] > kRed[1] && mixed[1] < 255);

  // The ensure_layer_alpha_for_erase step a caller takes for an unlocked layer without alpha.
  Document document(12, 8, PixelFormat::rgb8());
  const auto id = document.add_pixel_layer("Photo", filled(12, 8, kRed, PixelFormat::rgb8())).id();
  patchy::ensure_layer_alpha_for_erase(*document.find_layer(id));
  const auto& promoted = std::as_const(document).find_layer(id)->pixels();
  CHECK(promoted.format().channels == 4);
  CHECK(get(promoted, 5, 5) == kRed);
}

void background_eraser_overlapping_dabs_do_not_compound_and_selection_limits() {
  const auto source = filled(30, 10, kRed);
  const Rect rect{0, 0, 30, 10};
  const std::vector<float> half(static_cast<std::size_t>(rect.width) * rect.height, 0.5F);
  const std::vector<float> full(half.size(), 1.0F);
  auto pixels = source;
  BackgroundEraserStroke stroke(source, eraser_options(ColorMatchLimits::Contiguous), ColorMatchSampling::Continuous,
                                kWhite, false);
  // Selected in full left of x = 10, half selected to x = 19, not selected from there.
  const BackgroundEraserStroke::SelectionCoverage selection = [](std::int32_t x, std::int32_t) {
    return x < 10 ? 1.0F : (x < 20 ? 0.5F : 0.0F);
  };
  CHECK(!stroke.dab(pixels, 5, 5, rect, half, selection).empty());
  const auto after_one = pixels;
  const auto half_alpha = patchy::background_eraser_pixel_rgba8(kRed, 0.5F, false, kWhite)[3];
  CHECK(half_alpha > 100 && half_alpha < 155);
  CHECK(get(pixels, 5, 5)[3] == half_alpha);
  CHECK(get(pixels, 15, 5)[3] == patchy::background_eraser_pixel_rgba8(kRed, 0.25F, false, kWhite)[3]);
  CHECK(get(pixels, 25, 5)[3] == 255);
  // The same dab again changes nothing: a pixel keeps the largest weight a dab gave it.
  CHECK(stroke.dab(pixels, 5, 5, rect, half, selection).empty());
  for (int x = 0; x < 30; ++x) {
    CHECK(get(pixels, x, 5) == get(after_one, x, 5));
  }
  // A stronger dab erases from the stroke-start pixel, never from the half-erased one.
  CHECK(!stroke.dab(pixels, 5, 5, rect, full, selection).empty());
  CHECK(get(pixels, 5, 5)[3] == 0);
  CHECK(get(pixels, 15, 5)[3] == half_alpha);
  CHECK(get(pixels, 25, 5)[3] == 255);
  // A dab whose center is on a pixel the stroke already erased still samples the stroke-start color.
  CHECK(stroke.sample().has_value());
}

void background_eraser_16_and_32_bit_erase_the_same_pixels() {
  auto base = filled(48, 16, kRed);
  fill_rect(base, 20, 0, 21, 15, kNearRed);
  fill_rect(base, 22, 0, 47, 15, kGreen);
  const auto options = eraser_options(ColorMatchLimits::Contiguous);
  auto shallow = base;
  BackgroundEraserStroke shallow_stroke(base, options, ColorMatchSampling::Once, kWhite, false);
  for (int x = 4; x <= 44; x += 3) {
    (void)shallow_stroke.round_dab(shallow, x, 8, 12, 50);
  }
  for (const auto depth : {BitDepth::UInt16, BitDepth::Float32}) {
    const auto deep_start = patchy::convert_pixel_buffer_depth(base, depth, patchy::SampleKind::Color);
    auto deep = deep_start;
    BackgroundEraserStroke deep_stroke(deep_start, options, ColorMatchSampling::Once, kWhite, false);
    for (int x = 4; x <= 44; x += 3) {
      (void)deep_stroke.round_dab(deep, x, 8, 12, 50);
    }
    CHECK(deep.format().bit_depth == depth);
    bool erased = false;
    for (int y = 0; y < 16; ++y) {
      for (int x = 0; x < 48; ++x) {
        const auto expected = get(shallow, x, y);
        const auto values = patchy::load_pixel(deep.format(), deep.pixel(x, y));
        const auto start = patchy::load_pixel(deep_start.format(), deep_start.pixel(x, y));
        // The color is untouched at depth, and the alpha is the 8-bit alpha within a step.
        for (std::size_t channel = 0; channel < 3; ++channel) {
          CHECK(std::abs(values[channel] - start[channel]) <= 0.001F);
        }
        CHECK(std::abs(static_cast<double>(values[3]) - static_cast<double>(expected[3])) <= 1.0);
        erased = erased || expected[3] < 255;
      }
    }
    CHECK(erased);
    CHECK(patchy::load_pixel(deep.format(), deep.pixel(6, 8))[3] == 0.0F);
    CHECK(patchy::load_pixel(deep.format(), deep.pixel(40, 8))[3] == 255.0F);  // Once sampled the red
  }
}

void background_eraser_tokens_are_the_shared_match_tokens() {
  using patchy::color_match_limits_from_token;
  using patchy::color_match_limits_token;
  using patchy::color_match_sampling_from_token;
  using patchy::color_match_sampling_token;
  CHECK(color_match_sampling_token(ColorMatchSampling::Continuous) == std::string_view("continuous"));
  CHECK(color_match_sampling_token(ColorMatchSampling::Once) == std::string_view("once"));
  CHECK(color_match_sampling_token(ColorMatchSampling::BackgroundSwatch) == std::string_view("background"));
  CHECK(color_match_limits_token(ColorMatchLimits::Discontiguous) == std::string_view("discontiguous"));
  CHECK(color_match_limits_token(ColorMatchLimits::Contiguous) == std::string_view("contiguous"));
  CHECK(color_match_limits_token(ColorMatchLimits::FindEdges) == std::string_view("findEdges"));
  CHECK(color_match_sampling_from_token("once") == ColorMatchSampling::Once);
  CHECK(color_match_limits_from_token("findEdges") == ColorMatchLimits::FindEdges);
  CHECK(!color_match_sampling_from_token("Once").has_value());
  CHECK(!color_match_limits_from_token("").has_value());
  // The Color Replacement tool's names are the same enums and tokens.
  CHECK(patchy::color_replace_sampling_token(patchy::ColorReplaceSampling::BackgroundSwatch) ==
        color_match_sampling_token(ColorMatchSampling::BackgroundSwatch));
  CHECK(patchy::color_replace_limits_token(patchy::ColorReplaceLimits::FindEdges) ==
        color_match_limits_token(ColorMatchLimits::FindEdges));
  CHECK(static_cast<int>(ColorMatchSampling::Continuous) == 0 && static_cast<int>(ColorMatchSampling::Once) == 1 &&
        static_cast<int>(ColorMatchSampling::BackgroundSwatch) == 2);
  CHECK(static_cast<int>(ColorMatchLimits::Discontiguous) == 0 && static_cast<int>(ColorMatchLimits::Contiguous) == 1 &&
        static_cast<int>(ColorMatchLimits::FindEdges) == 2);
  const BackgroundEraserOptions defaults;
  CHECK(defaults.limits == ColorMatchLimits::Contiguous && defaults.tolerance == 30);
  CHECK(defaults.match().anti_alias);
}

// ---- Magic Eraser

// A 48x32 RGBA layer: light gray with a little noise, a dark bar across the middle, a red
// block top left with a one-pixel rim of in-between pixels, and a second red block bottom right.
PixelBuffer make_flood_scene() {
  PixelBuffer pixels(48, 32, PixelFormat::rgba8());
  for (int y = 0; y < 32; ++y) {
    for (int x = 0; x < 48; ++x) {
      const auto gray = static_cast<std::uint8_t>((x + y) % 2 == 0 ? 236 : 230);
      put(pixels, x, y, {gray, gray, gray, 255});
    }
  }
  fill_rect(pixels, 0, 14, 47, 16, {20, 20, 20, 255});
  fill_rect(pixels, 5, 3, 16, 10, kNearRed);
  fill_rect(pixels, 6, 4, 15, 9, kRed);
  fill_rect(pixels, 30, 20, 40, 28, kRed);
  return pixels;
}

struct FloodDocument {
  Document document;
  LayerId layer_id{};
};

FloodDocument make_flood_document(PixelBuffer pixels, BitDepth depth = BitDepth::UInt8) {
  FloodDocument result{Document(pixels.width(), pixels.height(), PixelFormat::rgba8()), {}};
  result.layer_id = result.document.add_pixel_layer("Layer", std::move(pixels)).id();
  if (depth != BitDepth::UInt8) {
    patchy::convert_document_depth(result.document, depth);
  }
  return result;
}

const PixelBuffer& layer_pixels(const FloodDocument& scene) {
  return scene.document.find_layer(scene.layer_id)->pixels();
}

std::uint8_t alpha8(const FloodDocument& scene, int x, int y) {
  const auto& pixels = layer_pixels(scene);
  return patchy::display_rgba8_at(pixels, x, y)[3];
}

void magic_eraser_erases_the_region_the_fill_tool_floods() {
  // For the same click, Tolerance and Contiguous, the pixels the Magic Eraser clears are the
  // pixels the Fill tool paints: both walk collect_flood_region with the same color test.
  struct Click {
    int x;
    int y;
  };
  for (const auto depth : {BitDepth::UInt8, BitDepth::UInt16, BitDepth::Float32}) {
    for (const auto click : {Click{2, 2}, Click{10, 6}, Click{20, 15}, Click{35, 24}}) {
      for (const int tolerance : {0, 8, 32, 60, 255}) {
        for (const bool contiguous : {true, false}) {
          auto filled_scene = make_flood_document(make_flood_scene(), depth);
          auto erased_scene = make_flood_document(make_flood_scene(), depth);
          const auto before = layer_pixels(filled_scene);
          auto options = tool_options(3, 250, 7);  // a color the scene does not hold
          options.flood_tolerance = tolerance;
          options.flood_contiguous = contiguous;
          const auto fill_dirty =
              patchy::flood_fill(filled_scene.document, filled_scene.layer_id, click.x, click.y, options);
          const auto erase_dirty = patchy::magic_erase(erased_scene.document, erased_scene.layer_id, click.x, click.y,
                                                       options, /*anti_alias=*/false);
          CHECK(fill_dirty == erase_dirty);
          int region = 0;
          for (int y = 0; y < 32; ++y) {
            for (int x = 0; x < 48; ++x) {
              const bool was_filled = patchy::display_rgba8_at(layer_pixels(filled_scene), x, y) !=
                                      patchy::display_rgba8_at(before, x, y);
              const auto alpha = alpha8(erased_scene, x, y);
              CHECK(alpha == (was_filled ? 0 : 255));
              region += was_filled ? 1 : 0;
            }
          }
          CHECK(region > 0);
        }
      }
    }
  }
}

void magic_eraser_contiguous_and_all_matching_pixels() {
  auto scene = make_flood_document(make_flood_scene());
  auto options = tool_options();
  options.flood_tolerance = 32;
  // Contiguous: the clicked red block only.
  const auto dirty = patchy::magic_erase(scene.document, scene.layer_id, 10, 6, options, false);
  CHECK((dirty == Rect{6, 4, 10, 6}));
  CHECK(alpha8(scene, 10, 6) == 0);
  CHECK(alpha8(scene, 5, 6) == 255);    // the in-between rim is past the tolerance
  CHECK(alpha8(scene, 35, 24) == 255);  // the other red block is not connected
  CHECK(alpha8(scene, 2, 2) == 255);
  // The erased pixels keep their color.
  const auto* px = layer_pixels(scene).pixel(10, 6);
  CHECK(px[0] == kRed[0] && px[1] == kRed[1] && px[2] == kRed[2]);

  // Contiguous off: every red pixel of the layer.
  auto all = make_flood_document(make_flood_scene());
  options.flood_contiguous = false;
  const auto all_dirty = patchy::magic_erase(all.document, all.layer_id, 10, 6, options, false);
  CHECK((all_dirty == Rect{6, 4, 35, 25}));
  CHECK(alpha8(all, 10, 6) == 0);
  CHECK(alpha8(all, 35, 24) == 0);
  CHECK(alpha8(all, 20, 15) == 255);
  CHECK(alpha8(all, 2, 2) == 255);

  // A click outside the canvas, or on a pixel the erase cannot change, does nothing.
  CHECK(patchy::magic_erase(all.document, all.layer_id, -1, 4, options, false).empty());
  CHECK(patchy::magic_erase(all.document, all.layer_id, 10, 6, options, false).empty());
}

void magic_eraser_anti_alias_erases_the_fringe_in_part() {
  // Tolerance 38 accepts a distance of 76: the rim (85 from the red) is past it and inside the
  // fringe that ends at 95, so Anti-alias erases it in part where it touches the region.
  auto options = tool_options();
  options.flood_tolerance = 38;
  auto hard = make_flood_document(make_flood_scene());
  CHECK(!patchy::magic_erase(hard.document, hard.layer_id, 10, 6, options, false).empty());
  CHECK(alpha8(hard, 5, 6) == 255);
  auto soft = make_flood_document(make_flood_scene());
  const auto dirty = patchy::magic_erase(soft.document, soft.layer_id, 10, 6, options, true);
  CHECK((dirty == Rect{5, 3, 12, 8}));
  CHECK(alpha8(soft, 10, 6) == 0);
  const auto rim = alpha8(soft, 5, 6);
  CHECK(rim > 0 && rim < 255);
  // (95 - 85) / (95 - 76) of the rim goes: about 134 of 255 goes, 121 stays.
  CHECK(std::abs(static_cast<int>(rim) - 121) <= 2);
  CHECK(alpha8(soft, 4, 6) == 255);     // the gray beyond the rim is far outside the fringe
  CHECK(alpha8(soft, 35, 24) == 255);   // still contiguous

  // Contiguous off: every fringe pixel goes in part, touching the region or not.
  auto lone_pixels = make_flood_scene();
  put(lone_pixels, 44, 2, kNearRed);
  auto lone = make_flood_document(std::move(lone_pixels));
  options.flood_contiguous = false;
  CHECK(!patchy::magic_erase(lone.document, lone.layer_id, 10, 6, options, true).empty());
  CHECK(alpha8(lone, 44, 2) == rim);
  CHECK(alpha8(lone, 35, 24) == 0);

  // Tolerance 0 has no fringe.
  auto exact = make_flood_document(make_flood_scene());
  options.flood_tolerance = 0;
  options.flood_contiguous = true;
  CHECK(!patchy::magic_erase(exact.document, exact.layer_id, 10, 6, options, true).empty());
  CHECK(alpha8(exact, 5, 6) == 255);
  CHECK(alpha8(exact, 10, 6) == 0);
}

void magic_eraser_opacity_and_selection_limit_the_erase() {
  auto scene = make_flood_document(make_flood_scene());
  auto options = tool_options();
  options.flood_tolerance = 32;
  options.primary.a = 128;  // Opacity 50%
  CHECK(!patchy::magic_erase(scene.document, scene.layer_id, 10, 6, options, false).empty());
  const auto half = alpha8(scene, 10, 6);
  CHECK(std::abs(static_cast<int>(half) - 127) <= 1);
  // A second click erases half of what is left (the region is found again from the new pixels).
  CHECK(!patchy::magic_erase(scene.document, scene.layer_id, 10, 6, options, false).empty());
  CHECK(std::abs(static_cast<int>(alpha8(scene, 10, 6)) - 63) <= 1);

  // A selection limits the region: only the selected columns of the block go.
  auto selected = make_flood_document(make_flood_scene());
  options.primary.a = 255;
  options.selection = Rect{0, 0, 10, 32};
  options.selection_mask = [](std::int32_t x, std::int32_t) { return x < 10; };
  const auto dirty = patchy::magic_erase(selected.document, selected.layer_id, 8, 6, options, false);
  CHECK((dirty == Rect{6, 4, 4, 6}));
  CHECK(alpha8(selected, 9, 6) == 0);
  CHECK(alpha8(selected, 10, 6) == 255);
  // A click outside the selection does nothing.
  CHECK(patchy::magic_erase(selected.document, selected.layer_id, 12, 6, options, false).empty());
}

void magic_eraser_sample_all_layers_reads_the_composite() {
  // The layer is one flat red; the composite shows a green square over part of it. The region
  // comes from the composite, and the erase is written to the layer.
  auto scene = make_flood_document(filled(32, 24, kRed));
  std::vector<std::uint8_t> composite(32U * 24U * 4U);
  for (int y = 0; y < 24; ++y) {
    for (int x = 0; x < 32; ++x) {
      const bool square = x >= 8 && x < 20 && y >= 6 && y < 16;
      const auto& color = square ? kGreen : kRed;
      std::copy(color.begin(), color.end(), composite.begin() + (static_cast<std::size_t>(y) * 32U + x) * 4U);
    }
  }
  const patchy::FloodSampleImage image{composite.data(), 32, 24, 32U * 4U};
  auto options = tool_options();
  options.flood_tolerance = 32;
  const auto dirty = patchy::magic_erase(scene.document, scene.layer_id, 10, 8, options, false, &image);
  CHECK((dirty == Rect{8, 6, 12, 10}));
  CHECK(alpha8(scene, 10, 8) == 0);
  CHECK(alpha8(scene, 2, 2) == 255);
  CHECK(alpha8(scene, 20, 8) == 255);
  // Without the composite the same click takes the layer's own flat red: everything.
  auto own = make_flood_document(filled(32, 24, kRed));
  CHECK((patchy::magic_erase(own.document, own.layer_id, 10, 8, options, false) == Rect{0, 0, 32, 24}));

  // The layer never grows: a layer smaller than the canvas loses only its own pixels.
  Document document(32, 24, PixelFormat::rgba8());
  const auto id = document.add_pixel_layer("Piece", filled(32, 24, kRed)).id();
  document.find_layer(id)->set_pixels(filled(6, 5, kRed));
  document.find_layer(id)->set_bounds(Rect{10, 8, 6, 5});
  std::vector<std::uint8_t> flat(32U * 24U * 4U);
  for (std::size_t index = 0; index < flat.size(); index += 4U) {
    std::copy(kRed.begin(), kRed.end(), flat.begin() + static_cast<std::ptrdiff_t>(index));
  }
  const patchy::FloodSampleImage flat_image{flat.data(), 32, 24, 32U * 4U};
  const auto piece_dirty = patchy::magic_erase(document, id, 2, 2, options, false, &flat_image);
  CHECK((piece_dirty == Rect{10, 8, 6, 5}));
  CHECK((std::as_const(document).find_layer(id)->bounds() == Rect{10, 8, 6, 5}));
  CHECK(std::as_const(document).find_layer(id)->pixels().pixel(3, 2)[3] == 0);
}

void magic_eraser_follows_the_eraser_under_a_transparency_lock() {
  // Locked transparency (the locked Background): the region moves toward the background color
  // (options.secondary) and keeps its alpha.
  auto locked = make_flood_document(make_flood_scene());
  auto options = tool_options();
  options.flood_tolerance = 32;
  options.lock_transparent_pixels = true;
  options.secondary = EditColor{10, 20, 200, 255};
  CHECK(!patchy::magic_erase(locked.document, locked.layer_id, 10, 6, options, false).empty());
  const auto* px = layer_pixels(locked).pixel(10, 6);
  CHECK(px[0] == 10 && px[1] == 20 && px[2] == 200 && px[3] == 255);
  CHECK(alpha8(locked, 2, 2) == 255);

  // A transparent click under the lock does nothing.
  PixelBuffer holes = make_flood_scene();
  fill_rect(holes, 40, 0, 47, 8, {0, 0, 0, 0});
  auto hole_scene = make_flood_document(std::move(holes));
  CHECK(patchy::magic_erase(hole_scene.document, hole_scene.layer_id, 44, 4, options, false).empty());

  // An unlocked layer without alpha gains an alpha channel and erases to transparency.
  Document document(20, 10, PixelFormat::rgb8());
  auto photo = filled(20, 10, kRed, PixelFormat::rgb8());
  fill_rect(photo, 10, 0, 19, 9, kGreen);
  const auto id = document.add_pixel_layer("Photo", std::move(photo)).id();
  options.lock_transparent_pixels = false;
  const auto dirty = patchy::magic_erase(document, id, 3, 3, options, false);
  CHECK((dirty == Rect{0, 0, 10, 10}));
  const auto& pixels = std::as_const(document).find_layer(id)->pixels();
  CHECK(pixels.format().channels == 4);
  CHECK(pixels.pixel(3, 3)[3] == 0);
  CHECK(pixels.pixel(15, 3)[3] == 255);
  CHECK(pixels.pixel(15, 3)[1] == kGreen[1]);

  // A click that erases nothing leaves a layer without alpha as it is.
  Document untouched(20, 10, PixelFormat::rgb8());
  const auto untouched_id = untouched.add_pixel_layer("Photo", filled(20, 10, kRed, PixelFormat::rgb8())).id();
  options.selection = Rect{15, 0, 5, 10};
  options.selection_mask = [](std::int32_t x, std::int32_t) { return x >= 15; };
  CHECK(patchy::magic_erase(untouched, untouched_id, 3, 3, options, false).empty());
  CHECK(std::as_const(untouched).find_layer(untouched_id)->pixels().format().channels == 3);
}

}  // namespace

std::vector<patchy::test::TestCase> eraser_tools_tests() {
  return {
      {"background_eraser_erases_the_pixels_color_replacement_repaints",
       background_eraser_erases_the_pixels_color_replacement_repaints},
      {"background_eraser_limits_pick_connected_or_all_matching_pixels",
       background_eraser_limits_pick_connected_or_all_matching_pixels},
      {"background_eraser_sampling_picks_which_color_is_erased",
       background_eraser_sampling_picks_which_color_is_erased},
      {"background_eraser_follows_the_eraser_under_a_transparency_lock",
       background_eraser_follows_the_eraser_under_a_transparency_lock},
      {"background_eraser_overlapping_dabs_do_not_compound_and_selection_limits",
       background_eraser_overlapping_dabs_do_not_compound_and_selection_limits},
      {"background_eraser_16_and_32_bit_erase_the_same_pixels", background_eraser_16_and_32_bit_erase_the_same_pixels},
      {"background_eraser_tokens_are_the_shared_match_tokens", background_eraser_tokens_are_the_shared_match_tokens},
      {"magic_eraser_erases_the_region_the_fill_tool_floods", magic_eraser_erases_the_region_the_fill_tool_floods},
      {"magic_eraser_contiguous_and_all_matching_pixels", magic_eraser_contiguous_and_all_matching_pixels},
      {"magic_eraser_anti_alias_erases_the_fringe_in_part", magic_eraser_anti_alias_erases_the_fringe_in_part},
      {"magic_eraser_opacity_and_selection_limit_the_erase", magic_eraser_opacity_and_selection_limit_the_erase},
      {"magic_eraser_sample_all_layers_reads_the_composite", magic_eraser_sample_all_layers_reads_the_composite},
      {"magic_eraser_follows_the_eraser_under_a_transparency_lock",
       magic_eraser_follows_the_eraser_under_a_transparency_lock},
  };
}
