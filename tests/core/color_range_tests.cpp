// Select > Color Range's mask (GitHub issue 709, src/core/color_range.cpp, docs/color-range.md).
#include "core/color_range.hpp"
#include "core/pixel_depth.hpp"
#include "test_groups.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

using patchy::BitDepth;
using patchy::ColorRangeClassifier;
using patchy::ColorRangeMode;
using patchy::ColorRangeOptions;
using patchy::ColorRangeSample;
using patchy::PixelBuffer;
using patchy::PixelFormat;

ColorRangeOptions sampled(int fuzziness, std::vector<ColorRangeSample> samples, bool invert = false) {
  ColorRangeOptions options;
  options.mode = ColorRangeMode::SampledColors;
  options.fuzziness = fuzziness;
  options.samples = std::move(samples);
  options.invert = invert;
  return options;
}

ColorRangeOptions fixed_range(ColorRangeMode mode, int fuzziness) {
  ColorRangeOptions options;
  options.mode = mode;
  options.fuzziness = fuzziness;
  return options;
}

int coverage_of(const ColorRangeOptions& options, int red, int green, int blue, int alpha = 255) {
  return ColorRangeClassifier(options).classify(static_cast<std::uint8_t>(red), static_cast<std::uint8_t>(green),
                                                static_cast<std::uint8_t>(blue), static_cast<std::uint8_t>(alpha));
}

// A row of grays 0..255 on a red channel ramp: pixel x is (x, 40, 40, 255).
PixelBuffer red_ramp_rgba8() {
  PixelBuffer pixels(256, 2, PixelFormat::rgba8());
  for (std::int32_t y = 0; y < 2; ++y) {
    for (std::int32_t x = 0; x < 256; ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = static_cast<std::uint8_t>(x);
      px[1] = 40;
      px[2] = 40;
      px[3] = 255;
    }
  }
  return pixels;
}

void color_range_sampled_fuzziness_zero_and_max() {
  const ColorRangeSample white{255, 255, 255, false};
  // Fuzziness 0: exact matches only.
  const auto exact = sampled(0, {white});
  CHECK(coverage_of(exact, 255, 255, 255) == 255);
  CHECK(coverage_of(exact, 254, 255, 255) == 0);
  CHECK(coverage_of(exact, 0, 0, 0) == 0);
  // Fuzziness 1 has no room for a ramp: still the exact color alone.
  CHECK(coverage_of(sampled(1, {white}), 254, 255, 255) == 0);
  // The maximum: full to distance 100, fading to nothing at 200.
  const auto widest = sampled(patchy::kMaxColorRangeFuzziness, {white});
  CHECK(coverage_of(widest, 255, 255, 255) == 255);
  CHECK(coverage_of(widest, 155, 255, 255) == 255);  // distance 100
  CHECK(coverage_of(widest, 105, 255, 255) == 128);  // distance 150: halfway down
  CHECK(coverage_of(widest, 55, 255, 255) == 0);     // distance 200
  CHECK(coverage_of(widest, 0, 0, 0) == 0);          // distance 441
  // Out-of-range values clamp.
  CHECK(coverage_of(sampled(100000, {white}), 105, 255, 255) == 128);
  CHECK(coverage_of(sampled(-5, {white}), 254, 255, 255) == 0);
  // No samples select nothing; inverted, everything.
  CHECK(coverage_of(sampled(40, {}), 10, 20, 30) == 0);
  CHECK(coverage_of(sampled(40, {}, true), 10, 20, 30) == 255);
}

void color_range_sampled_edge_is_soft_and_monotonic() {
  const auto options = sampled(40, {ColorRangeSample{100, 40, 40, false}});
  const auto mask = patchy::color_range_mask(red_ramp_rgba8(), options);
  CHECK(mask.format() == PixelFormat::gray8());
  CHECK(mask.width() == 256);
  CHECK(mask.height() == 2);
  const auto row = mask.row(0);
  // Full within half the fuzziness, gone at the fuzziness, partial between.
  CHECK(row[100] == 255);
  CHECK(row[120] == 255);
  CHECK(row[80] == 255);
  CHECK(row[140] == 0);
  CHECK(row[60] == 0);
  CHECK(row[130] == 128);
  CHECK(row[70] == 128);
  int partial = 0;
  bool falling = true;
  for (std::size_t x = 100; x + 1 < 256; ++x) {
    falling = falling && row[x] >= row[x + 1];
    partial += row[x] > 0 && row[x] < 255 ? 1 : 0;
  }
  CHECK(falling);
  CHECK(partial == 19);  // distances 21 to 39
  // Symmetric about the sample.
  for (int d = 0; d <= 60; ++d) {
    CHECK(row[static_cast<std::size_t>(100 + d)] == row[static_cast<std::size_t>(100 - d)]);
  }
  CHECK(std::equal(row.begin(), row.end(), mask.row(1).begin()));
}

void color_range_samples_add_and_subtract() {
  const ColorRangeSample red{200, 40, 40, false};
  const ColorRangeSample blue{40, 40, 200, false};
  const auto both = sampled(40, {red, blue});
  CHECK(coverage_of(both, 200, 40, 40) == 255);
  CHECK(coverage_of(both, 40, 40, 200) == 255);
  CHECK(coverage_of(both, 40, 200, 40) == 0);
  // Added samples combine by maximum, never by sum.
  CHECK(coverage_of(both, 170, 40, 40) == coverage_of(sampled(40, {red}), 170, 40, 40));
  // A subtract sample takes its own range back out, softly.
  const ColorRangeSample dark_red{170, 40, 40, true};
  const auto carved = sampled(40, {red, dark_red});
  CHECK(coverage_of(carved, 170, 40, 40) == 0);
  CHECK(coverage_of(carved, 230, 40, 40) == coverage_of(sampled(40, {red}), 230, 40, 40));
  const auto between = coverage_of(carved, 200, 40, 40);  // 30 from the subtract sample: half out
  CHECK(between == 127);
  // The subtraction scales: where both ramps are half, a quarter is left.
  const auto same = sampled(40, {red, ColorRangeSample{200, 40, 40, true}});
  CHECK(coverage_of(same, 200, 40, 40) == 0);
  CHECK(coverage_of(same, 230, 40, 40) == 64);
  // Subtract samples alone select nothing.
  CHECK(coverage_of(sampled(40, {dark_red}), 170, 40, 40) == 0);
}

void color_range_hue_bands_select_their_sector() {
  struct Case {
    ColorRangeMode mode;
    std::array<int, 3> color;
  };
  const std::array<Case, 6> pure{{
      {ColorRangeMode::Reds, {255, 0, 0}},
      {ColorRangeMode::Yellows, {255, 255, 0}},
      {ColorRangeMode::Greens, {0, 255, 0}},
      {ColorRangeMode::Cyans, {0, 255, 255}},
      {ColorRangeMode::Blues, {0, 0, 255}},
      {ColorRangeMode::Magentas, {255, 0, 255}},
  }};
  for (const auto& band : pure) {
    for (const auto& other : pure) {
      const auto coverage = coverage_of(fixed_range(band.mode, 40), other.color[0], other.color[1], other.color[2]);
      CHECK(coverage == (band.mode == other.mode ? 255 : 0));
    }
    // Grays, black and white have no hue.
    CHECK(coverage_of(fixed_range(band.mode, 200), 128, 128, 128) == 0);
    CHECK(coverage_of(fixed_range(band.mode, 200), 0, 0, 0) == 0);
    CHECK(coverage_of(fixed_range(band.mode, 200), 255, 255, 255) == 0);
  }
  // Orange at exactly 30 degrees sits on the Reds/Yellows border: inside both sectors.
  CHECK(coverage_of(fixed_range(ColorRangeMode::Reds, 0), 254, 127, 0) == 255);
  CHECK(coverage_of(fixed_range(ColorRangeMode::Yellows, 0), 254, 127, 0) == 255);
  // Fuzziness 0 is a hard sector edge; more fuzziness reaches further round the wheel.
  CHECK(coverage_of(fixed_range(ColorRangeMode::Reds, 0), 255, 160, 0) == 0);
  const auto near = coverage_of(fixed_range(ColorRangeMode::Reds, 100), 255, 160, 0);
  const auto wide = coverage_of(fixed_range(ColorRangeMode::Reds, 200), 255, 160, 0);
  CHECK(near > 0);
  CHECK(near < 255);
  CHECK(wide > near);
  // At the maximum the soft edge ends at the neighbouring band's center.
  CHECK(coverage_of(fixed_range(ColorRangeMode::Reds, 200), 255, 255, 0) == 0);
  CHECK(coverage_of(fixed_range(ColorRangeMode::Reds, 200), 255, 254, 0) > 0);
  // Weak colors are selected in part: chroma 64 is half, 128 and up is full.
  CHECK(coverage_of(fixed_range(ColorRangeMode::Reds, 40), 164, 100, 100) == 128);
  CHECK(coverage_of(fixed_range(ColorRangeMode::Reds, 40), 228, 100, 100) == 255);
  // Hue wraps: a red leaning to magenta is still a red.
  CHECK(coverage_of(fixed_range(ColorRangeMode::Reds, 0), 255, 0, 100) == 255);
}

void color_range_tones_split_the_luma_range() {
  const auto shadows = fixed_range(ColorRangeMode::Shadows, 0);
  const auto midtones = fixed_range(ColorRangeMode::Midtones, 0);
  const auto highlights = fixed_range(ColorRangeMode::Highlights, 0);
  // Fuzziness 0: hard thirds (the two borders belong to both neighbours).
  CHECK(coverage_of(shadows, 0, 0, 0) == 255);
  CHECK(coverage_of(shadows, 85, 85, 85) == 255);
  CHECK(coverage_of(shadows, 86, 86, 86) == 0);
  CHECK(coverage_of(midtones, 84, 84, 84) == 0);
  CHECK(coverage_of(midtones, 85, 85, 85) == 255);
  CHECK(coverage_of(midtones, 128, 128, 128) == 255);
  CHECK(coverage_of(midtones, 170, 170, 170) == 255);
  CHECK(coverage_of(midtones, 171, 171, 171) == 0);
  CHECK(coverage_of(highlights, 169, 169, 169) == 0);
  CHECK(coverage_of(highlights, 170, 170, 170) == 255);
  CHECK(coverage_of(highlights, 255, 255, 255) == 255);
  // Luma weights green most: pure green is a midtone, pure blue a shadow.
  CHECK(coverage_of(midtones, 0, 255, 0) == 255);
  CHECK(coverage_of(shadows, 0, 0, 255) == 255);
  // The soft edge: at the maximum each range fades over a whole third.
  const auto soft_shadows = fixed_range(ColorRangeMode::Shadows, 200);
  CHECK(coverage_of(soft_shadows, 85, 85, 85) == 255);
  CHECK(coverage_of(soft_shadows, 170, 170, 170) == 0);
  const auto mid = coverage_of(soft_shadows, 128, 128, 128);
  CHECK(mid > 120);
  CHECK(mid < 135);
  const auto soft_highlights = fixed_range(ColorRangeMode::Highlights, 200);
  CHECK(coverage_of(soft_highlights, 85, 85, 85) == 0);
  CHECK(coverage_of(soft_highlights, 128, 128, 128) + mid >= 254);
  CHECK(coverage_of(soft_highlights, 128, 128, 128) + mid <= 256);
  const auto soft_midtones = fixed_range(ColorRangeMode::Midtones, 200);
  CHECK(coverage_of(soft_midtones, 0, 0, 0) == 0);
  CHECK(coverage_of(soft_midtones, 255, 255, 255) == 0);
  CHECK(coverage_of(soft_midtones, 43, 43, 43) > 120);
  CHECK(coverage_of(soft_midtones, 128, 128, 128) == 255);
  // Monotonic over the gray ramp.
  bool falling = true;
  bool rising = true;
  for (int level = 0; level < 255; ++level) {
    falling = falling && coverage_of(soft_shadows, level, level, level) >= coverage_of(soft_shadows, level + 1, level + 1, level + 1);
    rising = rising && coverage_of(soft_highlights, level, level, level) <= coverage_of(soft_highlights, level + 1, level + 1, level + 1);
  }
  CHECK(falling);
  CHECK(rising);
}

void color_range_alpha_scales_and_invert_flips() {
  const auto options = sampled(40, {ColorRangeSample{10, 200, 30, false}});
  CHECK(coverage_of(options, 10, 200, 30, 255) == 255);
  CHECK(coverage_of(options, 10, 200, 30, 128) == 128);
  CHECK(coverage_of(options, 10, 200, 30, 0) == 0);
  // A clear pixel has no color, whatever its stored RGB; Invert selects it.
  auto inverted = options;
  inverted.invert = true;
  CHECK(coverage_of(inverted, 10, 200, 30, 255) == 0);
  CHECK(coverage_of(inverted, 10, 200, 30, 0) == 255);
  CHECK(coverage_of(inverted, 200, 10, 30, 255) == 255);
  for (const auto mode : {ColorRangeMode::Reds, ColorRangeMode::Shadows, ColorRangeMode::Midtones}) {
    auto range = fixed_range(mode, 40);
    for (int level = 0; level <= 255; level += 17) {
      const auto plain = coverage_of(range, level, 30, 60, 200);
      range.invert = true;
      CHECK(coverage_of(range, level, 30, 60, 200) == 255 - plain);
      range.invert = false;
    }
  }
}

void color_range_mask_reads_every_depth_and_layout() {
  const auto options = sampled(60, {ColorRangeSample{100, 40, 40, false}});
  const auto source8 = red_ramp_rgba8();
  const auto mask8 = patchy::color_range_mask(source8, options);

  // RGB without alpha reads as opaque.
  PixelBuffer rgb(256, 2, PixelFormat::rgb8());
  for (std::int32_t y = 0; y < 2; ++y) {
    for (std::int32_t x = 0; x < 256; ++x) {
      auto* px = rgb.pixel(x, y);
      px[0] = static_cast<std::uint8_t>(x);
      px[1] = 40;
      px[2] = 40;
    }
  }
  const auto mask_rgb = patchy::color_range_mask(rgb, options);
  CHECK(std::equal(mask8.data().begin(), mask8.data().end(), mask_rgb.data().begin(), mask_rgb.data().end()));

  // 16 and 32-bit copies of the same picture give the same mask: they are compared as
  // their display-encoded 8-bit colors.
  const auto source16 = patchy::convert_pixel_buffer_depth(source8, BitDepth::UInt16, patchy::SampleKind::Color);
  CHECK(source16.format().bit_depth == BitDepth::UInt16);
  const auto mask16 = patchy::color_range_mask(source16, options);
  CHECK(std::equal(mask8.data().begin(), mask8.data().end(), mask16.data().begin(), mask16.data().end()));
  const auto source32 = patchy::convert_pixel_buffer_depth(source8, BitDepth::Float32, patchy::SampleKind::Color);
  CHECK(source32.format().bit_depth == BitDepth::Float32);
  const auto mask32 = patchy::color_range_mask(source32, options);
  int worst = 0;
  for (std::size_t index = 0; index < mask8.data().size(); ++index) {
    worst = std::max(worst, std::abs(static_cast<int>(mask8.data()[index]) - static_cast<int>(mask32.data()[index])));
  }
  CHECK(worst <= 9);  // one display level of rounding on the ramp (255 / 30 per level)

  // A 32-bit highlight above 1.0 is a highlight; its clear pixels stay unselected.
  PixelBuffer hdr(2, 1, patchy::with_bit_depth(PixelFormat::rgba8(), BitDepth::Float32));
  patchy::store_pixel(hdr.format(), hdr.pixel(0, 0), {4.0F * 255.0F, 4.0F * 255.0F, 4.0F * 255.0F, 255.0F});
  patchy::store_pixel(hdr.format(), hdr.pixel(1, 0), {4.0F * 255.0F, 4.0F * 255.0F, 4.0F * 255.0F, 0.0F});
  const auto highlights = patchy::color_range_mask(hdr, fixed_range(ColorRangeMode::Highlights, 40));
  CHECK(highlights.row(0)[0] == 255);
  CHECK(highlights.row(0)[1] == 0);

  // Empty in, empty out; a gray buffer has no color to compare.
  CHECK(patchy::color_range_mask(PixelBuffer(), options).empty());
  CHECK(patchy::color_range_mask(PixelBuffer(4, 4, PixelFormat::gray8()), options).empty());
}

void color_range_is_deterministic_and_tokens_are_stable() {
  // The same options give the same bytes on every run, and the bytes are pinned: a digest
  // of every mode over one mixed picture.
  PixelBuffer picture(64, 64, PixelFormat::rgba8());
  for (std::int32_t y = 0; y < 64; ++y) {
    for (std::int32_t x = 0; x < 64; ++x) {
      auto* px = picture.pixel(x, y);
      px[0] = static_cast<std::uint8_t>(x * 4 + (y & 3));
      px[1] = static_cast<std::uint8_t>(y * 4 + (x & 3));
      px[2] = static_cast<std::uint8_t>((x * 7 + y * 13) & 255);
      px[3] = static_cast<std::uint8_t>(x < 8 ? x * 32 : 255);
    }
  }
  std::uint64_t digest = 1469598103934665603ULL;
  for (int index = 0; index < patchy::kColorRangeModeCount; ++index) {
    ColorRangeOptions options;
    options.mode = static_cast<ColorRangeMode>(index);
    options.fuzziness = 30 + index * 17;
    options.samples = {ColorRangeSample{120, 90, 60, false}, ColorRangeSample{30, 200, 180, false},
                       ColorRangeSample{128, 100, 70, true}};
    options.invert = (index % 3) == 1;
    const auto first = patchy::color_range_mask(picture, options);
    const auto second = patchy::color_range_mask(picture, options);
    CHECK(std::equal(first.data().begin(), first.data().end(), second.data().begin(), second.data().end()));
    for (const auto value : first.data()) {
      digest = (digest ^ value) * 1099511628211ULL;
    }
  }
  constexpr std::uint64_t kPinnedDigest = 0xbc340bc3e545c434ULL;
  if (digest != kPinnedDigest) {
    // Printed so a deliberate change of the rule can re-pin it.
    std::fprintf(stderr, "color_range digest: 0x%016llx\n", static_cast<unsigned long long>(digest));
  }
  CHECK(digest == kPinnedDigest);

  const std::array<const char*, patchy::kColorRangeModeCount> tokens{
      "sampled", "reds", "yellows", "greens", "cyans", "blues", "magentas", "highlights", "midtones", "shadows"};
  for (int index = 0; index < patchy::kColorRangeModeCount; ++index) {
    const auto mode = static_cast<ColorRangeMode>(index);
    CHECK(patchy::color_range_mode_token(mode) == tokens[static_cast<std::size_t>(index)]);
    CHECK(patchy::color_range_mode_from_token(tokens[static_cast<std::size_t>(index)]) == mode);
  }
  CHECK(!patchy::color_range_mode_from_token("skin").has_value());
  CHECK(!patchy::color_range_mode_from_token("").has_value());
}

}  // namespace

std::vector<patchy::test::TestCase> color_range_tests() {
  return {
      {"color_range_sampled_fuzziness_zero_and_max", color_range_sampled_fuzziness_zero_and_max},
      {"color_range_sampled_edge_is_soft_and_monotonic", color_range_sampled_edge_is_soft_and_monotonic},
      {"color_range_samples_add_and_subtract", color_range_samples_add_and_subtract},
      {"color_range_hue_bands_select_their_sector", color_range_hue_bands_select_their_sector},
      {"color_range_tones_split_the_luma_range", color_range_tones_split_the_luma_range},
      {"color_range_alpha_scales_and_invert_flips", color_range_alpha_scales_and_invert_flips},
      {"color_range_mask_reads_every_depth_and_layout", color_range_mask_reads_every_depth_and_layout},
      {"color_range_is_deterministic_and_tokens_are_stable", color_range_is_deterministic_and_tokens_are_stable},
  };
}
