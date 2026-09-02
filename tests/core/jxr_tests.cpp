#include "core/document.hpp"
#include "core/layer_tree.hpp"
#include "formats/format_registry.hpp"
#include "formats/jxr_document_io.hpp"
#include "support/srgb_transfer.hpp"

#include "core_test_support.hpp"
#include "local_psd_fixtures.hpp"
#include "test_harness.hpp"
#include "unicode_path_names.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

// True when this build has no JPEG XR codec (everything but Windows). Codec-dependent tests
// [SKIP] instead of failing, matching the HEIF and camera-raw convention.
[[nodiscard]] bool codec_unavailable(const char* what) {
  if (patchy::jxr::is_available()) {
    return false;
  }
  std::cout << "[SKIP] " << what << ": JPEG XR needs the in-box Windows codec\n";
  return true;
}

[[nodiscard]] patchy::Document solid_document(std::int32_t width, std::int32_t height, bool with_alpha) {
  const auto format = with_alpha ? patchy::PixelFormat::rgba8() : patchy::PixelFormat::rgb8();
  patchy::Document document(width, height, format);
  patchy::PixelBuffer pixels(width, height, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < height; ++y) {
    auto row = pixels.row(y);
    for (std::int32_t x = 0; x < width; ++x) {
      // Four flat quadrants: lossy JPEG XR keeps flat areas close, so quadrant interiors
      // stay comparable without pinning bytes.
      const bool right = x >= width / 2;
      const bool bottom = y >= height / 2;
      auto* pixel = row.data() + static_cast<std::size_t>(x) * 4U;
      pixel[0] = right ? 220 : 30;
      pixel[1] = bottom ? 200 : 40;
      pixel[2] = (right == bottom) ? 180 : 60;
      pixel[3] = with_alpha && right && bottom ? 128 : 255;
    }
  }
  document.add_pixel_layer("Background", std::move(pixels));
  return document;
}

[[nodiscard]] std::array<int, 4> pixel_at(const patchy::Document& document, std::int32_t x, std::int32_t y) {
  const auto& pixels = std::as_const(document.layers().front()).pixels();
  const auto channels = pixels.format().channels;
  const auto* source = pixels.row(y).data() + static_cast<std::size_t>(x) * channels;
  return {source[0], source[1], source[2], channels >= 4 ? source[3] : 255};
}

void jxr_extensions_sniff_and_registry_routing() {
  CHECK(patchy::jxr::is_jxr_extension("jxr"));
  CHECK(patchy::jxr::is_jxr_extension(".JXR"));
  CHECK(patchy::jxr::is_jxr_extension("wdp"));
  CHECK(patchy::jxr::is_jxr_extension("hdp"));
  CHECK(!patchy::jxr::is_jxr_extension("jpg"));
  CHECK(!patchy::jxr::is_jxr_extension("jxl"));

  // Unlike HEIF and camera raw, JPEG XR registers a writer: that is what keeps Save on
  // .jxr instead of routing it to Save As through is_read_only_source_extension.
  const auto* handler = patchy::builtin_format_registry().find_by_extension(".jxr");
  CHECK(handler != nullptr);
  CHECK(handler->can_write());
  CHECK(patchy::builtin_format_registry().find_by_extension(".wdp") == handler);
  CHECK(patchy::builtin_format_registry().find_by_extension(".hdp") == handler);

  // "II" 0xBC + version. The version byte is what separates a JPEG XR from a TIFF, which
  // shares the little-endian byte order mark.
  const std::vector<std::uint8_t> jxr_magic = {0x49, 0x49, 0xBC, 0x01, 0x20, 0x00, 0x00, 0x00};
  CHECK(patchy::jxr::sniff(jxr_magic));
  const std::vector<std::uint8_t> tiff_magic = {0x49, 0x49, 0x2A, 0x00, 0x08, 0x00, 0x00, 0x00};
  CHECK(!patchy::jxr::sniff(tiff_magic));
  const std::vector<std::uint8_t> big_endian_tiff = {0x4D, 0x4D, 0x00, 0x2A, 0x00, 0x00, 0x00, 0x08};
  CHECK(!patchy::jxr::sniff(big_endian_tiff));
  const std::vector<std::uint8_t> png_magic = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
  CHECK(!patchy::jxr::sniff(png_magic));
  CHECK(!patchy::jxr::sniff(std::vector<std::uint8_t>{0x49, 0x49, 0xBC}));  // truncated
  CHECK(!patchy::jxr::sniff(std::vector<std::uint8_t>{}));
}

void jxr_tone_map_scrgb_preserves_sdr_and_rolls_off_highlights() {
  // The knee: everything at or below it is the identity, so ordinary SDR content converts
  // exactly as it would with no HDR in the picture. That is the whole point of the curve.
  CHECK(patchy::jxr::highlight_rolloff(0.0F) == 0.0F);
  CHECK(std::abs(patchy::jxr::highlight_rolloff(0.25F) - 0.25F) < 1e-6F);
  CHECK(std::abs(patchy::jxr::highlight_rolloff(0.5F) - 0.5F) < 1e-6F);

  // scRGB permits negative components for out-of-gamut colors, and a decoded float buffer
  // can carry NaN; both floor at black rather than wrapping.
  CHECK(patchy::jxr::highlight_rolloff(-1.0F) == 0.0F);
  CHECK(patchy::jxr::highlight_rolloff(std::nanf("")) == 0.0F);

  // Above the knee: strictly increasing, never saturating early, and the 1000 nit ceiling
  // lands on display white.
  float previous = patchy::jxr::highlight_rolloff(0.5F);
  for (float value = 0.55F; value <= 12.5F; value += 0.05F) {
    const float mapped = patchy::jxr::highlight_rolloff(value);
    CHECK(mapped > previous);
    CHECK(mapped <= 1.0F);
    previous = mapped;
  }
  CHECK(std::abs(patchy::jxr::highlight_rolloff(12.5F) - 1.0F) < 1e-5F);
  CHECK(patchy::jxr::highlight_rolloff(1000.0F) == 1.0F);

  // The HDR range must stay legible rather than clipping flat: scRGB 1.0 (SDR reference
  // white) has to sit clearly below the 1000 nit ceiling once encoded to 8 bits.
  const auto sdr_white = patchy::linear_to_srgb8(patchy::jxr::highlight_rolloff(1.0F));
  const auto hdr_ceiling = patchy::linear_to_srgb8(patchy::jxr::highlight_rolloff(12.5F));
  CHECK(hdr_ceiling == 255);
  CHECK(sdr_white > 200);              // still reads as white, not a washed-out grey
  CHECK(hdr_ceiling - sdr_white > 20);  // real headroom for highlight detail

  // Buffer form: three HDR pixels plus alpha handling.
  const std::vector<float> samples = {
      0.25F, 0.5F,  12.5F, 1.0F,   // in-range color, alpha opaque
      -2.0F, 0.0F,  4.0F,  0.5F,   // negative component, half alpha
      0.5F,  0.5F,  0.5F,  2.0F,   // alpha above 1 clamps
  };
  const auto rgba = patchy::jxr::tone_map_scrgb_to_rgba8(samples, 3, 1);
  CHECK(rgba.size() == 12);
  CHECK(rgba[0] == patchy::linear_to_srgb8(0.25F));
  CHECK(rgba[1] == patchy::linear_to_srgb8(0.5F));
  CHECK(rgba[2] == 255);
  CHECK(rgba[3] == 255);
  CHECK(rgba[4] == 0);
  CHECK(rgba[5] == 0);
  CHECK(rgba[7] == 128);  // alpha is linear coverage, never sRGB-encoded
  CHECK(rgba[11] == 255);

  // Undersized input is a programming error, not a silent partial decode.
  bool threw = false;
  try {
    (void)patchy::jxr::tone_map_scrgb_to_rgba8(samples, 4, 1);
  } catch (const std::exception&) {
    threw = true;
  }
  CHECK(threw);
}

void jxr_round_trips_rgba8_if_available() {
  if (codec_unavailable("jxr_round_trips_rgba8_if_available")) {
    return;
  }
  const auto document = solid_document(32, 24, /*with_alpha*/ true);

  // Lossless is byte-exact by definition, so this half asserts real equality. It is the
  // strongest check available without pinning encoder output, which the repo forbids for
  // platform codecs.
  const auto lossless = patchy::jxr::write_jxr(document, patchy::jxr::WriteOptions{90, true});
  CHECK(patchy::jxr::sniff(lossless));
  const auto decoded = patchy::jxr::read_jxr(lossless);
  CHECK(decoded.document.width() == 32);
  CHECK(decoded.document.height() == 24);
  CHECK(decoded.document.layers().size() == 1);
  CHECK(decoded.document.layers().front().name() == "Background");
  for (const auto& [x, y] : {std::pair{4, 4}, std::pair{28, 4}, std::pair{4, 20}, std::pair{28, 20}}) {
    const auto expected = pixel_at(document, x, y);
    const auto actual = pixel_at(decoded.document, x, y);
    CHECK(actual == expected);
  }
  // The document really used alpha, so the reader must have kept a fourth channel.
  CHECK(std::as_const(decoded.document.layers().front()).pixels().format().channels == 4);

  // Lossy: statistics only. JPEG XR at this quality may subsample chroma, so quadrant
  // interiors are compared with a tolerance and nothing is byte-pinned.
  const auto lossy = patchy::jxr::write_jxr(document, patchy::jxr::WriteOptions{90, false});
  CHECK(patchy::jxr::sniff(lossy));
  const auto lossy_decoded = patchy::jxr::read_jxr(lossy);
  CHECK(lossy_decoded.document.width() == 32);
  CHECK(lossy_decoded.document.height() == 24);
  for (const auto& [x, y] : {std::pair{4, 4}, std::pair{28, 4}, std::pair{4, 20}}) {
    const auto expected = pixel_at(document, x, y);
    const auto actual = pixel_at(lossy_decoded.document, x, y);
    for (std::size_t channel = 0; channel < 3U; ++channel) {
      CHECK(std::abs(actual[channel] - expected[channel]) <= 24);
    }
  }

  // A fully opaque document writes a 24bpp frame and reads back as rgb8, which is what
  // keeps an ordinary screenshot from growing a pointless alpha plane.
  const auto opaque = solid_document(16, 16, /*with_alpha*/ false);
  const auto opaque_decoded = patchy::jxr::read_jxr(patchy::jxr::write_jxr(opaque, patchy::jxr::WriteOptions{90, true}));
  CHECK(std::as_const(opaque_decoded.document.layers().front()).pixels().format().channels == 3);
  CHECK(pixel_at(opaque_decoded.document, 4, 4) == pixel_at(opaque, 4, 4));
}

void jxr_rejects_garbage_and_empty_documents() {
  if (codec_unavailable("jxr_rejects_garbage_and_empty_documents")) {
    return;
  }
  // A file that sniffs as JPEG XR but has no usable image must produce a clean error, not
  // a crash or a zero-sized document.
  const std::vector<std::uint8_t> truncated = {0x49, 0x49, 0xBC, 0x01, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00};
  bool threw = false;
  try {
    (void)patchy::jxr::read_jxr(truncated);
  } catch (const std::exception&) {
    threw = true;
  }
  CHECK(threw);

  threw = false;
  try {
    (void)patchy::jxr::write_jxr(patchy::Document{}, patchy::jxr::WriteOptions{});
  } catch (const std::exception&) {
    threw = true;
  }
  CHECK(threw);
}

void jxr_unicode_path_round_trips_if_available() {
  if (codec_unavailable("jxr_unicode_path_round_trips_if_available")) {
    return;
  }
  // Every new file-writing entry point gets a Unicode-path test (AGENTS.md): the write goes
  // through std::filesystem, so a UTF-8/ANSI mix-up would write a mojibake name here.
  const auto dir = patchy::test::unicode_artifact_dir(u8"jxr");
  const auto path = dir / patchy::test::unicode_path_piece(patchy::test::kUnicodeCombinedStem).concat(".jxr");
  const auto document = solid_document(16, 12, /*with_alpha*/ false);
  patchy::jxr::write_jxr_file(document, path, patchy::jxr::WriteOptions{90, true});
  CHECK(std::filesystem::exists(path));
  CHECK(patchy::test::directory_holds_only(dir, {path}));

  const auto decoded = patchy::jxr::read_jxr(patchy::test::read_binary_file(path));
  CHECK(decoded.document.width() == 16);
  CHECK(decoded.document.height() == 12);
  CHECK(pixel_at(decoded.document, 3, 3) == pixel_at(document, 3, 3));
}

void jxr_hdr_fixture_tone_maps_instead_of_clipping() {
  const auto bytes = patchy::test::read_binary_file(
      patchy::test::committed_format_fixture_path("jxr", "hdr-ramp.jxr"));
  CHECK(!bytes.empty());
  CHECK(patchy::jxr::sniff(bytes));
  if (codec_unavailable("jxr_hdr_fixture_tone_maps_instead_of_clipping")) {
    return;
  }

  // Self-authored 8x2 lossless 128bppRGBAFloat scRGB ramp (see NOTICE-THIRD-PARTY.md):
  // row 0 walks the SDR range 0..1, row 1 walks the HDR range 1..12.5 (80 to 1000 nits).
  // This is the regression guard for the whole float path: pixel-format classification,
  // WIC's float conversion keeping values above 1.0, and the rolloff itself.
  const auto result = patchy::jxr::read_jxr(bytes);
  CHECK(result.document.width() == 8);
  CHECK(result.document.height() == 2);
  CHECK(result.notices.size() == 1);
  CHECK(result.notices.front().find("tone mapped") != std::string::npos);

  // Tolerance, not byte pins: the decode runs through the platform codec. It is far tighter
  // than the failure this guards against, which would move these by 30 or more.
  const std::array<int, 8> expected_sdr = {0, 106, 146, 175, 198, 211, 219, 225};
  const std::array<int, 8> expected_hdr = {225, 246, 250, 252, 253, 254, 255, 255};
  for (std::int32_t x = 0; x < 8; ++x) {
    const auto sdr = pixel_at(result.document, x, 0);
    const auto hdr = pixel_at(result.document, x, 1);
    CHECK(std::abs(sdr[0] - expected_sdr[static_cast<std::size_t>(x)]) <= 2);
    CHECK(std::abs(hdr[0] - expected_hdr[static_cast<std::size_t>(x)]) <= 2);
  }

  // The two properties that matter, stated directly. Highlights above SDR white must stay
  // distinguishable (a clamping decode would flatten all of row 1 to one value), and SDR
  // content must not be dragged down (the reason a filmic curve was rejected).
  CHECK(pixel_at(result.document, 7, 1)[0] > pixel_at(result.document, 0, 1)[0] + 20);
  CHECK(pixel_at(result.document, 7, 0)[0] > 200);
  CHECK(pixel_at(result.document, 3, 0)[0] > 150);
}

void jxr_reads_real_captures_if_available() {
  // Untracked real-world samples (drop an NVIDIA HDR capture into local-test-fixtures/jxr/).
  const auto dir = patchy::test::local_format_fixture_path("jxr", "").parent_path();
  if (!std::filesystem::exists(dir)) {
    std::cout << "[SKIP] jxr_reads_real_captures_if_available: no local-test-fixtures/jxr\n";
    return;
  }
  if (codec_unavailable("jxr_reads_real_captures_if_available")) {
    return;
  }
  int decoded_count = 0;
  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    if (!entry.is_regular_file() ||
        !patchy::jxr::is_jxr_extension(entry.path().extension().string())) {
      continue;
    }
    const auto bytes = patchy::test::read_binary_file(entry.path());
    const auto result = patchy::jxr::read_jxr(bytes);
    CHECK(result.document.width() > 0);
    CHECK(result.document.height() > 0);
    CHECK(result.document.layers().size() == 1);

    // Statistics only, never byte pins: the decode runs through the platform codec. The
    // point of the tone map is that an HDR frame does not come back as a field of white,
    // so assert the image actually has range.
    const auto& pixels = std::as_const(result.document.layers().front()).pixels();
    const auto channels = pixels.format().channels;
    std::uint8_t lowest = 255;
    std::uint8_t highest = 0;
    long long total = 0;
    long long samples = 0;
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      const auto row = pixels.row(y);
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        const auto value = row[static_cast<std::size_t>(x) * channels];
        lowest = std::min(lowest, value);
        highest = std::max(highest, value);
        total += value;
        ++samples;
      }
    }
    const auto mean = static_cast<double>(total) / static_cast<double>(samples);
    std::cout << "  " << entry.path().filename().string() << ": " << result.document.width() << 'x'
              << result.document.height() << " red min " << static_cast<int>(lowest) << " max "
              << static_cast<int>(highest) << " mean " << mean;
    for (const auto& notice : result.notices) {
      std::cout << " | " << notice;
    }
    std::cout << '\n';
    CHECK(highest > lowest);   // not a flat field
    CHECK(mean < 250.0);       // highlights did not blow the whole frame to white
    ++decoded_count;
  }
  if (decoded_count == 0) {
    std::cout << "[SKIP] jxr_reads_real_captures_if_available: no .jxr files in local-test-fixtures/jxr\n";
  }
}

}  // namespace

std::vector<patchy::test::TestCase> jxr_tests() {
  return {
      {"jxr_extensions_sniff_and_registry_routing", jxr_extensions_sniff_and_registry_routing},
      {"jxr_tone_map_scrgb_preserves_sdr_and_rolls_off_highlights",
       jxr_tone_map_scrgb_preserves_sdr_and_rolls_off_highlights},
      {"jxr_round_trips_rgba8_if_available", jxr_round_trips_rgba8_if_available},
      {"jxr_rejects_garbage_and_empty_documents", jxr_rejects_garbage_and_empty_documents},
      {"jxr_unicode_path_round_trips_if_available", jxr_unicode_path_round_trips_if_available},
      {"jxr_hdr_fixture_tone_maps_instead_of_clipping", jxr_hdr_fixture_tone_maps_instead_of_clipping},
      {"jxr_reads_real_captures_if_available", jxr_reads_real_captures_if_available},
  };
}
