// RGB profile conversion (docs/rgb-profiles.md): sRGB detection by colorants and curves,
// Display P3 and Adobe RGB pixels to sRGB at 8, 16 and 32 bits against the published
// matrices, whole-document conversion, profile files on Unicode paths, and RGB PSD saves
// that no longer carry the source profile.
#include "color/rgb_profiles.hpp"
#include "core/document.hpp"
#include "core/pixel_depth.hpp"
#include "psd/psd_document_io.hpp"
#include "psd_test_support.hpp"
#include "test_harness.hpp"
#include "unicode_path_names.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace {

using namespace patchy;

std::vector<std::uint8_t> display_p3() {
  return create_rgb_matrix_profile({"Display P3 (test)", 0.3127, 0.3290, 0.680, 0.320, 0.265, 0.690, 0.150, 0.060, 0.0});
}

std::vector<std::uint8_t> adobe_rgb() {
  return create_rgb_matrix_profile(
      {"Adobe RGB (1998) compatible (test)", 0.3127, 0.3290, 0.64, 0.33, 0.21, 0.71, 0.15, 0.06, 563.0 / 256.0});
}

std::vector<std::uint8_t> srgb_copy(const char* name, double gamma = 0.0) {
  return create_rgb_matrix_profile({name, 0.3127, 0.3290, 0.64, 0.33, 0.30, 0.60, 0.15, 0.06, gamma});
}

// Published linear-light matrices to linear sRGB (D65 on both sides, so no adaptation).
constexpr std::array<double, 9> kP3ToSrgb{1.2249, -0.2247, 0.0, -0.0420, 1.0419, 0.0, -0.0197, -0.0786, 1.0979};
constexpr std::array<double, 9> kAdobeToSrgb{1.3982, -0.3982, 0.0, 0.0, 1.0, 0.0, 0.0, -0.0429, 1.0429};

std::array<double, 3> apply(const std::array<double, 9>& m, std::array<double, 3> v) {
  return {m[0] * v[0] + m[1] * v[1] + m[2] * v[2], m[3] * v[0] + m[4] * v[1] + m[5] * v[2],
          m[6] * v[0] + m[7] * v[1] + m[8] * v[2]};
}

// The expected 8-bit sRGB value of an encoded source pixel.
std::array<double, 3> expected_srgb(const std::array<double, 9>& m, std::array<double, 3> encoded, double gamma) {
  std::array<double, 3> linear{};
  for (std::size_t c = 0; c < 3; ++c) {
    linear[c] = gamma > 0.0 ? std::pow(encoded[c], gamma) : srgb_decode(encoded[c]);
  }
  const auto out = apply(m, linear);
  return {srgb_encode(out[0]) * 255.0, srgb_encode(out[1]) * 255.0, srgb_encode(out[2]) * 255.0};
}

constexpr std::array<std::array<std::uint8_t, 3>, 5> kSamples{{
    {200, 100, 50}, {128, 128, 128}, {0, 0, 0}, {255, 255, 255}, {60, 180, 120}}};

void rgb_profile_recognizes_srgb_variants() {
  CHECK(is_srgb_profile(srgb_copy("sRGB IEC61966-2.1")));
  CHECK(is_srgb_profile(srgb_copy("Untitled matrix profile")));  // colorants and curve decide
  CHECK(is_srgb_profile(srgb_copy("sRGB built-in", 2.2)));        // named sRGB, near curve
  CHECK(!is_srgb_profile(srgb_copy("Untitled gamma 1.8", 1.8)));  // sRGB primaries, other curve
  CHECK(!is_srgb_profile(srgb_copy("sRGB linear", 1.0)));
  CHECK(!is_srgb_profile(display_p3()));
  CHECK(!is_srgb_profile(adobe_rgb()));
  CHECK(!is_srgb_profile({}));
  CHECK(rgb_profile_needs_conversion(display_p3()));
  CHECK(rgb_profile_needs_conversion(adobe_rgb()));
  CHECK(!rgb_profile_needs_conversion(srgb_copy("sRGB IEC61966-2.1")));
  CHECK(!rgb_profile_needs_conversion({}));
  const std::vector<std::uint8_t> garbage(200, 7);
  CHECK(!rgb_profile_needs_conversion(garbage));
  CHECK(!is_usable_rgb_profile(garbage));
  CHECK(rgb_profile_description(display_p3()) == "Display P3 (test)");
  CHECK(display_p3() == display_p3());  // no save timestamp in the bytes
  CHECK(document_rgb_profile_name({}) == kSrgbProfileName);
}

void check_eight_bit(const std::vector<std::uint8_t>& profile, const std::array<double, 9>& matrix, double gamma) {
  const auto transform = RgbProfileTransform::create(profile, {}, RenderingIntent::RelativeColorimetric, true);
  CHECK(transform.has_value());
  if (!transform.has_value()) return;
  PixelBuffer pixels(static_cast<std::int32_t>(kSamples.size()), 1, PixelFormat::rgba8());
  for (std::size_t i = 0; i < kSamples.size(); ++i) {
    auto* px = pixels.pixel(static_cast<std::int32_t>(i), 0);
    std::copy(kSamples[i].begin(), kSamples[i].end(), px);
    px[3] = static_cast<std::uint8_t>(40 + i);
  }
  CHECK(transform->convert(pixels));
  for (std::size_t i = 0; i < kSamples.size(); ++i) {
    const auto* px = pixels.pixel(static_cast<std::int32_t>(i), 0);
    const auto expected = expected_srgb(
        matrix, {kSamples[i][0] / 255.0, kSamples[i][1] / 255.0, kSamples[i][2] / 255.0}, gamma);
    for (std::size_t c = 0; c < 3; ++c) {
      CHECK(std::abs(px[c] - expected[c]) <= 2.0);
    }
    CHECK(px[3] == 40 + i);  // alpha untouched
  }
  // Single colors take the same path.
  const auto red = transform->convert_color(RgbColor{200, 100, 50});
  CHECK(red == (RgbColor{pixels.pixel(0, 0)[0], pixels.pixel(0, 0)[1], pixels.pixel(0, 0)[2]}));
}

void rgb_profile_converts_p3_and_adobe_rgb_to_srgb_at_8_bits() {
  check_eight_bit(display_p3(), kP3ToSrgb, 0.0);
  check_eight_bit(adobe_rgb(), kAdobeToSrgb, 563.0 / 256.0);
}

void rgb_profile_converts_at_16_bits() {
  const auto transform = RgbProfileTransform::create(display_p3(), {}, RenderingIntent::RelativeColorimetric, true);
  CHECK(transform.has_value());
  if (!transform.has_value()) return;
  PixelBuffer pixels(static_cast<std::int32_t>(kSamples.size()), 1,
                     with_bit_depth(PixelFormat::rgb8(), BitDepth::UInt16));
  for (std::size_t i = 0; i < kSamples.size(); ++i) {
    auto* px = reinterpret_cast<std::uint16_t*>(pixels.pixel(static_cast<std::int32_t>(i), 0));
    for (std::size_t c = 0; c < 3; ++c) px[c] = widen_u8_to_u16(kSamples[i][c]);
  }
  CHECK(transform->convert(pixels));
  for (std::size_t i = 0; i < kSamples.size(); ++i) {
    const auto* px = reinterpret_cast<const std::uint16_t*>(pixels.pixel(static_cast<std::int32_t>(i), 0));
    const auto expected = expected_srgb(
        kP3ToSrgb, {kSamples[i][0] / 255.0, kSamples[i][1] / 255.0, kSamples[i][2] / 255.0}, 0.0);
    for (std::size_t c = 0; c < 3; ++c) {
      // Within a third of an 8-bit step: no 8-bit intermediate.
      CHECK(std::abs(px[c] / 257.0 - expected[c]) <= 0.35);
    }
  }
}

void rgb_profile_converts_linear_32_bit_without_clipping() {
  const auto transform = RgbProfileTransform::create(display_p3(), {}, RenderingIntent::RelativeColorimetric, true);
  CHECK(transform.has_value() && transform->converts_linear());
  if (!transform.has_value()) return;
  const std::array<std::array<float, 4>, 3> samples{{{0.5F, 0.25F, 0.1F, 1.0F},
                                                     {1.0F, 0.0F, 0.0F, 0.5F},  // P3 red: outside sRGB
                                                     {4.0F, 4.0F, 4.0F, 1.0F}}};  // HDR white
  PixelBuffer pixels(3, 1, with_bit_depth(PixelFormat::rgba8(), BitDepth::Float32));
  for (std::int32_t x = 0; x < 3; ++x) {
    std::memcpy(pixels.pixel(x, 0), samples[static_cast<std::size_t>(x)].data(), sizeof(float) * 4);
  }
  CHECK(transform->convert(pixels));
  for (std::int32_t x = 0; x < 3; ++x) {
    std::array<float, 4> out{};
    std::memcpy(out.data(), pixels.pixel(x, 0), sizeof(float) * 4);
    const auto& in = samples[static_cast<std::size_t>(x)];
    const auto expected = apply(kP3ToSrgb, {in[0], in[1], in[2]});
    for (std::size_t c = 0; c < 3; ++c) {
      CHECK(std::abs(out[c] - expected[c]) <= 0.003 * std::max(1.0, std::abs(expected[c])));
    }
    CHECK(out[3] == in[3]);
  }
  // Negative green and blue survive: nothing clips.
  std::array<float, 4> red{};
  std::memcpy(red.data(), pixels.pixel(1, 0), sizeof(float) * 4);
  CHECK(red[0] > 1.2F && red[1] < -0.03F && red[2] < -0.01F);
}

Document tagged_document(const std::vector<std::uint8_t>& profile) {
  Document document(4, 1, PixelFormat::rgba8());
  PixelBuffer pixels(4, 1, PixelFormat::rgba8());
  for (std::int32_t x = 0; x < 4; ++x) {
    auto* px = pixels.pixel(x, 0);
    px[0] = 200;
    px[1] = 100;
    px[2] = 50;
    px[3] = static_cast<std::uint8_t>(100 + x);
  }
  auto& layer = document.add_pixel_layer("Photo", std::move(pixels));
  LayerMask mask;
  mask.bounds = Rect{0, 0, 4, 1};
  mask.pixels = PixelBuffer(4, 1, PixelFormat::gray8());
  mask.pixels.clear(77);
  layer.set_mask(std::move(mask));
  document.indexed_palette() = DocumentIndexedPalette{{RgbColor{200, 100, 50}}, 8, {}};
  document.color_state().embedded_icc_profile = profile;
  return document;
}

void rgb_profile_converts_documents() {
  auto document = tagged_document(display_p3());
  const auto conversion = convert_document_rgb_profile(document, {}, RenderingIntent::RelativeColorimetric, true);
  CHECK(conversion.has_value());
  if (!conversion.has_value()) return;
  CHECK(conversion->source_name == "Display P3 (test)");
  CHECK(conversion->destination_name == kSrgbProfileName);
  CHECK(!conversion->kept_stored_colors);
  CHECK(document.color_state().embedded_icc_profile.empty());
  const auto& layer = std::as_const(document).layers().front();
  const auto expected = expected_srgb(kP3ToSrgb, {200 / 255.0, 100 / 255.0, 50 / 255.0}, 0.0);
  for (std::int32_t x = 0; x < 4; ++x) {
    const auto* px = layer.pixels().pixel(x, 0);
    for (std::size_t c = 0; c < 3; ++c) CHECK(std::abs(px[c] - expected[c]) <= 2.0);
    CHECK(px[3] == 100 + x);
  }
  CHECK(layer.mask().has_value() && layer.mask()->pixels.pixel(2, 0)[0] == 77);  // masks are coverage
  const auto palette_color = document.indexed_palette()->colors.front();
  CHECK(palette_color == (RgbColor{layer.pixels().pixel(0, 0)[0], layer.pixels().pixel(0, 0)[1],
                                   layer.pixels().pixel(0, 0)[2]}));

  // To another profile: the destination becomes the embedded profile, and back again
  // lands within rounding.
  const auto adobe = adobe_rgb();
  CHECK(convert_document_rgb_profile(document, adobe, RenderingIntent::RelativeColorimetric, true).has_value());
  CHECK(document.color_state().embedded_icc_profile == adobe);
  CHECK(convert_document_rgb_profile(document, {}, RenderingIntent::Perceptual, false).has_value());
  for (std::size_t c = 0; c < 3; ++c) {
    CHECK(std::abs(std::as_const(document).layers().front().pixels().pixel(0, 0)[c] - expected[c]) <= 3.0);
  }

  // An sRGB-tagged document keeps its pixels; the tag goes.
  auto srgb = tagged_document(srgb_copy("sRGB IEC61966-2.1"));
  const auto before = std::as_const(srgb).layers().front().pixels().data();
  const std::vector<std::uint8_t> before_bytes(before.begin(), before.end());
  CHECK(convert_document_rgb_profile(srgb, {}, RenderingIntent::RelativeColorimetric, true).has_value());
  const auto after = std::as_const(srgb).layers().front().pixels().data();
  CHECK(std::equal(after.begin(), after.end(), before_bytes.begin(), before_bytes.end()));
  CHECK(srgb.color_state().embedded_icc_profile.empty());

  // Layer effects keep their colors and say so; an unusable destination leaves everything.
  auto styled = tagged_document(display_p3());
  styled.layers().front().layer_style().drop_shadows.push_back(LayerDropShadow{});
  const auto styled_conversion = convert_document_rgb_profile(styled, {}, RenderingIntent::RelativeColorimetric, true);
  CHECK(styled_conversion.has_value() && styled_conversion->kept_stored_colors);
  auto untouched = tagged_document(display_p3());
  const std::vector<std::uint8_t> garbage(300, 9);
  CHECK(!convert_document_rgb_profile(untouched, garbage, RenderingIntent::RelativeColorimetric, true).has_value());
  CHECK(untouched.color_state().embedded_icc_profile == display_p3());
  CHECK(std::as_const(untouched).layers().front().pixels().pixel(0, 0)[0] == 200);

  // 32-bit documents convert in linear light.
  auto deep = tagged_document(display_p3());
  deep.color_state().bit_depth = BitDepth::Float32;
  auto& deep_layer = deep.layers().front();
  deep_layer.pixels() = convert_pixel_buffer_depth(std::as_const(deep_layer).pixels(), BitDepth::Float32,
                                                   SampleKind::Color);
  CHECK(convert_document_rgb_profile(deep, {}, RenderingIntent::RelativeColorimetric, true).has_value());
  float red = 0.0F;
  std::memcpy(&red, std::as_const(deep).layers().front().pixels().pixel(0, 0), sizeof(float));
  CHECK(std::abs(srgb_encode(red) * 255.0 - expected[0]) <= 1.0);
}

void rgb_profile_reads_profile_files_on_unicode_paths() {
  const auto dir = patchy::test::unicode_artifact_dir(u8"rgb-profiles");
  const auto profile = display_p3();
  const auto path = dir / patchy::test::unicode_path_piece(
                              std::u8string(patchy::test::kUnicodeCombinedStem) + u8".icc");
  {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(profile.data()), static_cast<std::streamsize>(profile.size()));
  }
  CHECK(read_rgb_profile(path) == profile);
  const auto bad = dir / patchy::test::unicode_path_piece(u8"せす not a profile.icc");
  {
    std::ofstream out(bad, std::ios::binary);
    out << std::string(400, 'x');
  }
  CHECK(read_rgb_profile(bad).empty());
  CHECK(read_rgb_profile(dir / "missing.icc").empty());
}

void rgb_profile_psd_saves_drop_the_converted_source_profile() {
  auto document = tagged_document(display_p3());
  document.indexed_palette().reset();
  const auto tagged = psd::DocumentIo::write_layered_rgb8(document);
  auto reopened = psd::DocumentIo::read(tagged);
  CHECK(reopened.color_state().embedded_icc_profile == display_p3());
  CHECK(patchy::test::test_image_resource_payload(reopened.metadata().raw_psd_image_resources, 1039).has_value());
  CHECK(convert_document_rgb_profile(reopened, {}, RenderingIntent::RelativeColorimetric, true).has_value());
  const auto converted = psd::DocumentIo::read(psd::DocumentIo::write_layered_rgb8(reopened));
  CHECK(converted.color_state().embedded_icc_profile.empty());
  CHECK(!patchy::test::test_image_resource_payload(converted.metadata().raw_psd_image_resources, 1039).has_value());
  // Converting to another profile writes that one.
  CHECK(convert_document_rgb_profile(reopened, adobe_rgb(), RenderingIntent::RelativeColorimetric, true).has_value());
  const auto adobe = psd::DocumentIo::read(psd::DocumentIo::write_layered_rgb8(reopened));
  CHECK(adobe.color_state().embedded_icc_profile == adobe_rgb());
}

}  // namespace

std::vector<patchy::test::TestCase> rgb_profile_tests() {
  return {
      {"rgb_profile_recognizes_srgb_variants", rgb_profile_recognizes_srgb_variants},
      {"rgb_profile_converts_p3_and_adobe_rgb_to_srgb_at_8_bits",
       rgb_profile_converts_p3_and_adobe_rgb_to_srgb_at_8_bits},
      {"rgb_profile_converts_at_16_bits", rgb_profile_converts_at_16_bits},
      {"rgb_profile_converts_linear_32_bit_without_clipping", rgb_profile_converts_linear_32_bit_without_clipping},
      {"rgb_profile_converts_documents", rgb_profile_converts_documents},
      {"rgb_profile_reads_profile_files_on_unicode_paths", rgb_profile_reads_profile_files_on_unicode_paths},
      {"rgb_profile_psd_saves_drop_the_converted_source_profile",
       rgb_profile_psd_saves_drop_the_converted_source_profile},
  };
}
