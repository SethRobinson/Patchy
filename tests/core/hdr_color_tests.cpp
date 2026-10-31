// Paint colors for 32-bit documents (core/hdr_color.hpp; docs/high-bit-depth.md, "Color
// choice at 32 bits"): a base color plus an Intensity in stops, and the paint path that
// carries it into 32-bit layers only.
#include "core/document.hpp"
#include "core/document_depth.hpp"
#include "core/hdr_color.hpp"
#include "core/pixel_depth.hpp"
#include "core/pixel_tools.hpp"
#include "test_harness.hpp"
#include <cmath>
#include <limits>
#include <vector>

namespace {

using namespace patchy;

bool near(double a, double b, double tolerance) { return std::abs(a - b) <= tolerance; }

double decoded(std::uint8_t value) { return srgb_decode(static_cast<double>(value) / 255.0); }

void hdr_color_splits_and_rebuilds_linear_values() {
  // Within display range: 0 stops, the base is the sRGB encoding.
  const auto in_range = hdr_color_from_linear({0.5, 0.2, 0.0});
  CHECK(in_range.stops == 0.0);
  CHECK(in_range.base[0] == static_cast<std::uint8_t>(std::lround(srgb_encode(0.5) * 255.0)));
  CHECK(in_range.base[2] == 0);
  CHECK(hdr_color_from_linear({1.0, 1.0, 1.0}).stops == 0.0);

  // Overbright: the stops bring the brightest channel to 1.0, and the color comes back.
  const auto bright = hdr_color_from_linear({3.0, 0.75, 0.375});
  CHECK(near(bright.stops, std::log2(3.0), 1e-9));
  CHECK(bright.base[0] == 255);
  const auto rebuilt = hdr_linear_color(bright);
  CHECK(near(rebuilt[0], 3.0, 1e-9));
  CHECK(near(rebuilt[1], 0.75, 0.02));
  CHECK(near(rebuilt[2], 0.375, 0.01));

  // Negative and non-finite channels read as 0; the stops clamp to the offered range.
  const auto odd = hdr_color_from_linear({-2.0, std::numeric_limits<double>::quiet_NaN(), 0.25});
  CHECK(odd.base[0] == 0 && odd.base[1] == 0 && odd.stops == 0.0);
  const auto huge = hdr_color_from_linear({1.0e9, 0.0, 0.0});
  CHECK(huge.stops == kMaxColorIntensityStops);
  CHECK(clamp_color_intensity_stops(std::numeric_limits<double>::infinity()) == 0.0);
  CHECK(clamp_color_intensity_stops(-30.0) == kMinColorIntensityStops);

  // Intensity scales linear light: +1 stop doubles, -1 halves.
  const HdrColor base{{149, 33, 33}, 0.0};
  const HdrColor up{{149, 33, 33}, 1.7};
  CHECK(near(hdr_linear_color(up)[0], hdr_linear_color(base)[0] * std::exp2(1.7), 1e-12));
  CHECK(color_intensity_gain(1.0) == 2.0F);
  CHECK(color_intensity_gain(-1.0) == 0.5F);
  CHECK(color_intensity_gain(0.0) == 1.0F);
}

void hdr_color_preview_swatches_scale_light_and_clip_at_white() {
  const HdrColor white{{255, 255, 255}, 0.0};
  const auto darker = hdr_preview_color(white, -1.0);
  CHECK(darker[0] == static_cast<std::uint8_t>(std::lround(srgb_encode(0.5) * 255.0)));
  CHECK(hdr_preview_color(white, 1.0)[0] == 255);  // clipped: an SDR display ends at white
  const HdrColor gray{{100, 100, 100}, 0.0};
  const auto expected = static_cast<std::uint8_t>(std::lround(srgb_encode(decoded(100) * 2.0) * 255.0));
  CHECK(hdr_preview_color(gray, 1.0)[1] == expected);
  // The offset adds to the color's own Intensity.
  const HdrColor lifted{{100, 100, 100}, 1.0};
  CHECK(hdr_preview_color(lifted, 0.0)[0] == expected);
  CHECK(hdr_preview_color(lifted, -1.0)[0] == 100);
}

Document flat_document(BitDepth depth) {
  Document document(8, 8, PixelFormat::rgba8());
  PixelBuffer pixels(8, 8, PixelFormat::rgba8());
  pixels.clear(0);
  document.add_pixel_layer("Paint", std::move(pixels));
  convert_document_depth(document, depth);
  return document;
}

// Fill, a brush dab and the eraser's background on a layer without alpha: 32-bit layers take
// the gains in linear light, 8 and 16-bit ones paint exactly what they did without them.
void hdr_color_gain_paints_32_bit_layers_only() {
  EditOptions plain;
  plain.primary = EditColor{149, 33, 33, 255};
  plain.secondary = EditColor{20, 40, 220, 255};
  plain.brush_size = 5;
  auto gained = plain;
  gained.primary_gain = 4.0F;
  gained.secondary_gain = 2.0F;

  auto linear = flat_document(BitDepth::Float32);
  const auto id = linear.layers().front().id();
  CHECK(!fill_rect(linear, id, Rect{0, 0, 8, 8}, gained).empty());
  const auto& layer = std::as_const(linear).layers().front();
  const auto filled = load_pixel(layer.pixels().format(), layer.pixels().pixel(2, 2));
  CHECK(near(filled[0], decoded(149) * 4.0 * 255.0, 0.01));
  CHECK(filled[0] > 255.0F);  // above display white
  CHECK(near(filled[1], decoded(33) * 4.0 * 255.0, 0.01));
  CHECK(near(filled[3], 255.0, 0.001));

  auto dab = flat_document(BitDepth::Float32);
  CHECK(!paint_brush_dab(dab, dab.layers().front().id(), 4.0, 4.0, gained, false).empty());
  const auto& dabbed = std::as_const(dab).layers().front();
  const auto centre = load_pixel(dabbed.pixels().format(), dabbed.pixels().pixel(4, 4));
  CHECK(near(centre[0], decoded(149) * 4.0 * 255.0, 0.5));

  for (const auto depth : {BitDepth::UInt8, BitDepth::UInt16}) {
    auto with_gain = flat_document(depth);
    auto without = flat_document(depth);
    CHECK(!fill_rect(with_gain, with_gain.layers().front().id(), Rect{0, 0, 8, 8}, gained).empty());
    CHECK(!fill_rect(without, without.layers().front().id(), Rect{0, 0, 8, 8}, plain).empty());
    const auto& a = std::as_const(with_gain).layers().front().pixels();
    const auto& b = std::as_const(without).layers().front().pixels();
    CHECK(std::vector<std::uint8_t>(a.data().begin(), a.data().end()) ==
          std::vector<std::uint8_t>(b.data().begin(), b.data().end()));
  }

  // The eraser on a 32-bit layer with locked transparency erases to the background, gained.
  auto locked = flat_document(BitDepth::Float32);
  const auto locked_id = locked.layers().front().id();
  CHECK(!fill_rect(locked, locked_id, Rect{0, 0, 8, 8}, plain).empty());
  auto erase = gained;
  erase.lock_transparent_pixels = true;
  CHECK(!paint_brush_dab(locked, locked_id, 4.0, 4.0, erase, true).empty());
  const auto& erased = std::as_const(locked).layers().front();
  const auto background = load_pixel(erased.pixels().format(), erased.pixels().pixel(4, 4));
  CHECK(near(background[2], decoded(220) * 2.0 * 255.0, 0.5));
}

}  // namespace

std::vector<patchy::test::TestCase> hdr_color_tests() {
  return {
      {"hdr_color_splits_and_rebuilds_linear_values", hdr_color_splits_and_rebuilds_linear_values},
      {"hdr_color_preview_swatches_scale_light_and_clip_at_white",
       hdr_color_preview_swatches_scale_light_and_clip_at_white},
      {"hdr_color_gain_paints_32_bit_layers_only", hdr_color_gain_paints_32_bit_layers_only},
  };
}
