#include "core/photo_develop.hpp"
#include "core/pixel_depth.hpp"
#include "core/splitmix64.hpp"
#include "formats/raw_tone.hpp"
#include "test_harness.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

using patchy::BitDepth;
using patchy::PhotoDeveloper;
using patchy::PhotoDevelopParams;
using patchy::PixelBuffer;
using patchy::PixelFormat;

PixelFormat rgba_format(BitDepth depth) {
  return PixelFormat{patchy::ColorMode::RGB, depth, 4};
}

// A deterministic buffer covering the whole sample range, with varied alpha; 32-bit
// color includes values above 1 (HDR) and exact zeros.
PixelBuffer sample_buffer(PixelFormat format, std::int32_t width = 37, std::int32_t height = 11) {
  PixelBuffer pixels(width, height, format);
  std::uint64_t state = 0x392U;
  const auto sample_bytes = patchy::bytes_per_channel(format.bit_depth);
  for (std::int32_t y = 0; y < height; ++y) {
    auto* px = pixels.row(y).data();
    for (std::int32_t x = 0; x < width; ++x) {
      for (std::uint16_t c = 0; c < format.channels; ++c, px += sample_bytes) {
        const auto random = patchy::splitmix64(state++);
        switch (format.bit_depth) {
          case BitDepth::UInt8:
            *px = static_cast<std::uint8_t>(random & 0xFFU);
            break;
          case BitDepth::UInt16: {
            const auto value = static_cast<std::uint16_t>(random & 0xFFFFU);
            std::memcpy(px, &value, sizeof(value));
            break;
          }
          case BitDepth::Float32: {
            const auto unit = static_cast<float>(random >> 40U) / static_cast<float>(1U << 24U);
            const auto value = c == 3 ? unit : (x % 5 == 0 ? 0.0F : unit * 3.0F);
            std::memcpy(px, &value, sizeof(value));
            break;
          }
        }
      }
    }
  }
  return pixels;
}

bool same_bytes(const PixelBuffer& a, const PixelBuffer& b) {
  if (a.width() != b.width() || a.height() != b.height() || a.format() != b.format()) return false;
  for (std::int32_t y = 0; y < a.height(); ++y) {
    const auto ra = a.row(y);
    const auto rb = b.row(y);
    if (std::memcmp(ra.data(), rb.data(), ra.size()) != 0) return false;
  }
  return true;
}

PixelBuffer solid_rgb8(std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
  PixelBuffer pixels(1, 1, PixelFormat::rgba8());
  auto* px = pixels.row(0).data();
  px[0] = red;
  px[1] = green;
  px[2] = blue;
  px[3] = 77;
  return pixels;
}

std::array<int, 4> developed_rgb8(std::uint8_t red, std::uint8_t green, std::uint8_t blue,
                                  const PhotoDevelopParams& params) {
  auto pixels = solid_rgb8(red, green, blue);
  PhotoDeveloper(params).apply(pixels);
  const auto* px = pixels.row(0).data();
  return {px[0], px[1], px[2], px[3]};
}

bool near(int value, int expected, int tolerance = 1) {
  return std::abs(value - expected) <= tolerance;
}

void photo_develop_neutral_settings_are_identity_at_every_depth() {
  for (const auto depth : {BitDepth::UInt8, BitDepth::UInt16, BitDepth::Float32}) {
    for (const std::uint16_t channels : {std::uint16_t{3}, std::uint16_t{4}}) {
      const PixelFormat format{patchy::ColorMode::RGB, depth, channels};
      const auto original = sample_buffer(format);
      auto developed = original;
      const PhotoDeveloper developer(PhotoDevelopParams{});
      CHECK(developer.is_identity());
      developer.apply(developed);
      CHECK(same_bytes(developed, original));
    }
  }
  // Non-finite input normalizes to neutral rather than poisoning the pixels.
  PhotoDevelopParams nan_params;
  nan_params.exposure_ev = std::nan("");
  CHECK(PhotoDeveloper(nan_params).is_identity());
}

void photo_develop_tone_and_color_stages_match_develop_raw() {
  // The tone stage alone is exactly Develop Raw's 16-bit tone table, and the color stage
  // alone exactly its saturation/vibrance, so both paths share one implementation.
  PhotoDevelopParams tone;
  tone.contrast = 45.0;
  tone.shadows = -20.0;
  tone.highlights = 30.0;
  const auto lut = patchy::raw::build_tone_lut({tone.contrast, tone.highlights, tone.shadows});
  const auto source = sample_buffer(rgba_format(BitDepth::UInt16));
  auto toned = source;
  PhotoDeveloper(tone).apply(toned);
  bool tone_matches = true;
  for (std::int32_t y = 0; y < source.height(); ++y) {
    for (std::int32_t x = 0; x < source.width(); ++x) {
      std::array<std::uint16_t, 4> before{};
      std::array<std::uint16_t, 4> after{};
      std::memcpy(before.data(), source.pixel(x, y), sizeof(before));
      std::memcpy(after.data(), toned.pixel(x, y), sizeof(after));
      for (std::size_t c = 0; c < 3; ++c) tone_matches = tone_matches && after[c] == lut[before[c]];
      tone_matches = tone_matches && after[3] == before[3];
    }
  }
  CHECK(tone_matches);

  PhotoDevelopParams color;
  color.saturation = 35.0;
  color.vibrance = -25.0;
  auto colored = source;
  PhotoDeveloper(color).apply(colored);
  bool color_matches = true;
  for (std::int32_t y = 0; y < source.height(); ++y) {
    for (std::int32_t x = 0; x < source.width(); ++x) {
      std::array<std::uint16_t, 3> expected{};
      std::array<std::uint16_t, 3> after{};
      std::memcpy(expected.data(), source.pixel(x, y), sizeof(expected));
      std::memcpy(after.data(), colored.pixel(x, y), sizeof(after));
      patchy::raw::apply_color(expected, color.saturation, color.vibrance);
      color_matches = color_matches && after == expected;
    }
  }
  CHECK(color_matches);
}

void photo_develop_pinned_exposure_saturation_and_white_balance_values() {
  PhotoDevelopParams brighter;
  brighter.exposure_ev = 1.0;
  const auto up = developed_rgb8(128, 128, 128, brighter);
  CHECK(near(up[0], 176) && near(up[1], 176) && near(up[2], 176));
  CHECK(up[3] == 77);  // alpha untouched
  PhotoDevelopParams darker;
  darker.exposure_ev = -1.0;
  const auto down = developed_rgb8(128, 128, 128, darker);
  CHECK(near(down[0], 92) && near(down[1], 92) && near(down[2], 92));

  PhotoDevelopParams saturated;
  saturated.saturation = 50.0;
  const auto vivid = developed_rgb8(200, 100, 50, saturated);
  CHECK(near(vivid[0], 241) && near(vivid[1], 91) && near(vivid[2], 16));
  PhotoDevelopParams gray;
  gray.saturation = -100.0;
  const auto mono = developed_rgb8(200, 100, 50, gray);
  CHECK(near(mono[0], 118) && near(mono[1], 118) && near(mono[2], 118));

  PhotoDevelopParams warm;
  warm.temperature = 50.0;
  const auto warmed = developed_rgb8(128, 128, 128, warm);
  CHECK(near(warmed[0], 139) && near(warmed[1], 126) && near(warmed[2], 114));
  PhotoDevelopParams magenta;
  magenta.tint = 60.0;
  const auto tinted = developed_rgb8(128, 128, 128, magenta);
  CHECK(tinted[0] > tinted[1] + 5 && tinted[2] == tinted[0]);

  // White keeps its luminance under any white balance shift.
  for (const auto& [temperature, tint] : {std::pair{100.0, 0.0}, std::pair{-100.0, 40.0}, std::pair{30.0, -100.0}}) {
    const auto gains = patchy::photo_white_balance_gains(temperature, tint);
    CHECK(std::abs(0.2126 * gains[0] + 0.7152 * gains[1] + 0.0722 * gains[2] - 1.0) < 1e-12);
  }
}

void photo_develop_32_bit_keeps_linear_light_and_highlights() {
  PixelBuffer pixels(3, 1, PixelFormat::rgbf32());
  const std::array<float, 9> values = {0.25F, 0.5F, 1.0F, 3.0F, 6.0F, 0.0F, 0.18F, 0.18F, 0.18F};
  std::memcpy(pixels.row(0).data(), values.data(), sizeof(values));

  // Exposure alone is an exact multiply of linear light, above 1 included.
  PhotoDevelopParams exposure;
  exposure.exposure_ev = 1.0;
  auto doubled = pixels;
  PhotoDeveloper(exposure).apply(doubled);
  std::array<float, 9> out{};
  std::memcpy(out.data(), doubled.row(0).data(), sizeof(out));
  bool exact = true;
  for (std::size_t i = 0; i < out.size(); ++i) exact = exact && out[i] == values[i] * 2.0F;
  CHECK(exact);

  // The tone curve continues with slope 1 above white: a positive contrast keeps HDR
  // values where they were, and mid gray moves the way the 16-bit curve moves it.
  PhotoDevelopParams contrast;
  contrast.contrast = 60.0;
  auto toned = pixels;
  PhotoDeveloper(contrast).apply(toned);
  std::memcpy(out.data(), toned.row(0).data(), sizeof(out));
  CHECK(std::abs(out[3] - 3.0F) < 1e-4F);
  CHECK(std::abs(out[4] - 6.0F) < 1e-4F);
  CHECK(out[5] == 0.0F);
  const auto lut = patchy::raw::build_tone_lut({60.0, 0.0, 0.0});
  const auto gray_encoded = patchy::srgb_encode(0.18);
  const auto expected_gray = patchy::srgb_decode(lut[static_cast<std::size_t>(std::lround(gray_encoded * 65535.0))] / 65535.0);
  CHECK(std::abs(out[6] - expected_gray) < 2e-4);
  CHECK(out[6] < 0.18F);  // below mid gray, contrast darkens

  // Saturation never pushes a channel below zero.
  PhotoDevelopParams saturation;
  saturation.saturation = 100.0;
  auto vivid = pixels;
  PhotoDeveloper(saturation).apply(vivid);
  std::memcpy(out.data(), vivid.row(0).data(), sizeof(out));
  bool non_negative = true;
  for (const auto value : out) non_negative = non_negative && value >= 0.0F;
  CHECK(non_negative);
  CHECK(out[0] < 0.25F && out[2] > 1.0F);
}

void photo_develop_auto_white_balance_neutralizes_the_mean() {
  const std::array<double, 3> cast = {0.24, 0.20, 0.16};  // within the slider range
  const auto [temperature, tint] = patchy::photo_auto_white_balance(cast);
  CHECK(temperature < 0.0);  // a warm cast is cooled
  const auto gains = patchy::photo_white_balance_gains(temperature, tint);
  const auto red = cast[0] * gains[0];
  const auto green = cast[1] * gains[1];
  const auto blue = cast[2] * gains[2];
  CHECK(std::abs(red / green - 1.0) < 1e-9);
  CHECK(std::abs(blue / green - 1.0) < 1e-9);
  // Out of range casts clamp; black or empty means no change.
  const auto [extreme_temperature, extreme_tint] = patchy::photo_auto_white_balance({1.0, 1e-3, 1e-3});
  CHECK(extreme_temperature == -100.0 && extreme_tint >= -100.0 && extreme_tint <= 100.0);
  const auto black = patchy::photo_auto_white_balance({0.0, 0.0, 0.0});
  CHECK(black.first == 0.0 && black.second == 0.0);

  // The mean ignores transparent pixels and decodes 8-bit sRGB to linear light.
  PixelBuffer pixels(2, 1, PixelFormat::rgba8());
  const std::array<std::uint8_t, 8> bytes = {255, 128, 0, 255, 0, 0, 255, 0};
  std::memcpy(pixels.row(0).data(), bytes.data(), bytes.size());
  const auto mean = patchy::photo_mean_linear_rgb(pixels);
  CHECK(std::abs(mean[0] - 1.0) < 1e-6);
  CHECK(std::abs(mean[1] - patchy::srgb_decode(128.0 / 255.0)) < 1e-6);
  CHECK(mean[2] == 0.0);
}

void photo_develop_leaves_non_rgb_buffers_alone() {
  PixelBuffer gray(4, 2, PixelFormat::gray8());
  for (std::int32_t y = 0; y < 2; ++y) {
    for (auto& byte : gray.row(y)) byte = 99;
  }
  const auto original = gray;
  PhotoDevelopParams params;
  params.exposure_ev = 2.0;
  params.saturation = 50.0;
  PhotoDeveloper(params).apply(gray);
  CHECK(same_bytes(gray, original));
}

}  // namespace

std::vector<patchy::test::TestCase> photo_develop_tests() {
  return {
      {"photo_develop_neutral_settings_are_identity_at_every_depth",
       photo_develop_neutral_settings_are_identity_at_every_depth},
      {"photo_develop_tone_and_color_stages_match_develop_raw", photo_develop_tone_and_color_stages_match_develop_raw},
      {"photo_develop_pinned_exposure_saturation_and_white_balance_values",
       photo_develop_pinned_exposure_saturation_and_white_balance_values},
      {"photo_develop_32_bit_keeps_linear_light_and_highlights", photo_develop_32_bit_keeps_linear_light_and_highlights},
      {"photo_develop_auto_white_balance_neutralizes_the_mean", photo_develop_auto_white_balance_neutralizes_the_mean},
      {"photo_develop_leaves_non_rgb_buffers_alone", photo_develop_leaves_non_rgb_buffers_alone},
  };
}
