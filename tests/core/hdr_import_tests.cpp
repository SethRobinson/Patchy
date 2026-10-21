// HDR sources open with their highlights intact (docs/hdr-formats.md, "PQ and HLG"): the
// shared PQ/HLG decode in formats/hdr_transfer and the AVIF reader that uses it.
#include "core/document.hpp"
#include "core/pixel_depth.hpp"
#include "formats/avif_document_io.hpp"
#include "formats/hdr_transfer.hpp"
#include "formats/heif_document_io.hpp"
#include "core_test_support.hpp"
#include "local_psd_fixtures.hpp"
#include "test_harness.hpp"
#if defined(PATCHY_TEST_LIBAVIF)
#include <avif/avif.h>
#endif
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using patchy::BitDepth;
namespace hdr = patchy::hdr;

// Deep editing is off by default in the test harness; the deep cases turn it on.
class DeepEditing {
public:
  explicit DeepEditing(bool enabled) {
    patchy::set_deep_editing_override(enabled);
  }
  ~DeepEditing() {
    patchy::set_deep_editing_override(std::nullopt);
  }
  DeepEditing(const DeepEditing&) = delete;
  DeepEditing& operator=(const DeepEditing&) = delete;
};

// SMPTE ST 2084 inverse EOTF, written out independently of the reader's decode.
double pq_signal_for_nits(double nits) {
  constexpr double m1 = 2610.0 / 16384.0;
  constexpr double m2 = 2523.0 / 4096.0 * 128.0;
  constexpr double c1 = 3424.0 / 4096.0;
  constexpr double c2 = 2413.0 / 4096.0 * 32.0;
  constexpr double c3 = 2392.0 / 4096.0 * 32.0;
  const double y = std::pow(nits / 10000.0, m1);
  return std::pow((c1 + c2 * y) / (1.0 + c3 * y), m2);
}

bool within(double value, double expected, double tolerance) {
  if (std::abs(value - expected) <= tolerance) {
    return true;
  }
  std::cout << "  got " << value << ", expected " << expected << " +- " << tolerance << '\n';
  return false;
}

void hdr_transfer_decodes_pq_and_hlg_reference_points() {
  CHECK(within(hdr::pq_signal_to_nits(0.0), 0.0, 1e-9));
  CHECK(within(hdr::pq_signal_to_nits(1.0), 10000.0, 1e-6));
  CHECK(within(hdr::pq_signal_to_nits(pq_signal_for_nits(203.0)), 203.0, 1e-6));
  CHECK(within(hdr::pq_signal_to_nits(pq_signal_for_nits(1000.0)), 1000.0, 1e-6));
  // ST 2084's published anchor: signal 0.5081 is about 100 nits.
  CHECK(within(hdr::pq_signal_to_nits(0.5081), 100.0, 0.5));
  CHECK(within(hdr::hlg_signal_to_scene(0.0), 0.0, 1e-12));
  CHECK(within(hdr::hlg_signal_to_scene(0.5), 1.0 / 12.0, 1e-9));
  CHECK(within(hdr::hlg_signal_to_scene(1.0), 1.0, 1e-6));

  // Primaries: sRGB to itself is the identity, BT.2020 and Display P3 match the published
  // conversion matrices.
  const auto same = hdr::linear_to_srgb_matrix(hdr::kBt709Primaries);
  const std::array<double, 9> identity{1, 0, 0, 0, 1, 0, 0, 0, 1};
  for (std::size_t i = 0; i < 9; ++i) {
    CHECK(within(same[i], identity[i], 1e-9));
  }
  const auto bt2020 = hdr::linear_to_srgb_matrix(hdr::kBt2020Primaries);
  CHECK(within(bt2020[0], 1.6605, 1e-3));
  CHECK(within(bt2020[1], -0.5876, 1e-3));
  CHECK(within(bt2020[2], -0.0728, 1e-3));
  CHECK(within(bt2020[4], 1.1329, 1e-3));
  const auto p3 = hdr::linear_to_srgb_matrix(hdr::kDisplayP3Primaries);
  CHECK(within(p3[0], 1.2249, 1e-3));
  CHECK(within(p3[1], -0.2247, 1e-3));
  CHECK(within(p3[8], 1.0979, 1e-3));

  // The converter: PQ reference white and the BT.2408 HLG point (75% signal) both land on
  // 1.0; a 1000 nit PQ highlight on 1000/203; alpha scales to 0..1.
  const auto code = [](double signal) { return static_cast<std::uint16_t>(std::lround(signal * 65535.0)); };
  const auto pq_white = code(pq_signal_for_nits(203.0));
  const auto pq_peak = code(pq_signal_for_nits(1000.0));
  const std::vector<std::uint16_t> pq{pq_white, pq_white, pq_white, 65535, pq_peak, pq_peak, pq_peak, 32768};
  std::vector<float> out(8);
  hdr::signal_rgba16_to_linear_srgb(pq.data(), 2, hdr::Transfer::Pq, hdr::kBt709Primaries, out.data());
  CHECK(within(out[0], 1.0, 1e-3) && within(out[1], 1.0, 1e-3) && within(out[2], 1.0, 1e-3));
  CHECK(within(out[3], 1.0, 1e-6));
  CHECK(within(out[4], 1000.0 / 203.0, 5e-3));
  CHECK(within(out[7], 32768.0 / 65535.0, 1e-6));
  const auto hlg_ref = code(0.75);
  const std::vector<std::uint16_t> hlg{hlg_ref, hlg_ref, hlg_ref, 65535};
  hdr::signal_rgba16_to_linear_srgb(hlg.data(), 1, hdr::Transfer::Hlg, hdr::kBt2020Primaries, out.data());
  CHECK(within(out[0], 1.0, 5e-3) && within(out[1], 1.0, 5e-3) && within(out[2], 1.0, 5e-3));
}

#if defined(PATCHY_TEST_LIBAVIF)

// A lossless 12-bit 4:4:4 AVIF whose RGB samples carry `signals` (0..1) under the given
// nclx transfer and primaries, encoded straight through libavif because Patchy's own writer
// never produces PQ or HLG.
std::vector<std::uint8_t> encode_hdr_avif(const std::vector<std::array<double, 3>>& signals,
                                          avifTransferCharacteristics transfer, avifColorPrimaries primaries) {
  const auto width = static_cast<std::uint32_t>(signals.size());
  avifImage* image = avifImageCreate(width, 1, 12, AVIF_PIXEL_FORMAT_YUV444);
  CHECK(image != nullptr);
  image->yuvRange = AVIF_RANGE_FULL;
  image->colorPrimaries = primaries;
  image->transferCharacteristics = transfer;
  image->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_IDENTITY;
  std::vector<std::uint16_t> samples;
  for (const auto& px : signals) {
    for (const double s : px) {
      samples.push_back(static_cast<std::uint16_t>(std::lround(s * 65535.0)));
    }
  }
  avifRGBImage rgb;
  avifRGBImageSetDefaults(&rgb, image);
  rgb.format = AVIF_RGB_FORMAT_RGB;
  rgb.depth = 16;
  rgb.pixels = reinterpret_cast<std::uint8_t*>(samples.data());
  rgb.rowBytes = width * 3U * static_cast<std::uint32_t>(sizeof(std::uint16_t));
  CHECK(avifImageRGBToYUV(image, &rgb) == AVIF_RESULT_OK);
  avifEncoder* encoder = avifEncoderCreate();
  CHECK(encoder != nullptr);
  encoder->codecChoice = AVIF_CODEC_CHOICE_AOM;
  encoder->quality = AVIF_QUALITY_LOSSLESS;
  encoder->qualityAlpha = AVIF_QUALITY_LOSSLESS;
  encoder->speed = AVIF_SPEED_FASTEST;
  avifRWData output = AVIF_DATA_EMPTY;
  CHECK(avifEncoderWrite(encoder, image, &output) == AVIF_RESULT_OK);
  std::vector<std::uint8_t> bytes(output.data, output.data + output.size);
  avifRWDataFree(&output);
  avifEncoderDestroy(encoder);
  avifImageDestroy(image);
  return bytes;
}

const float* float_pixels(const patchy::Document& document) {
  return reinterpret_cast<const float*>(std::as_const(document).layers().front().pixels().data().data());
}

void avif_pq_and_hlg_open_as_linear_32_bit() {
  const double white = pq_signal_for_nits(203.0);
  const double peak = pq_signal_for_nits(1000.0);
  // White at 203 nits, a 1000 nit highlight, black, and pure BT.2020 red at 203 nits.
  const auto pq = encode_hdr_avif({{white, white, white}, {peak, peak, peak}, {0.0, 0.0, 0.0}, {white, 0.0, 0.0}},
                                  AVIF_TRANSFER_CHARACTERISTICS_PQ, AVIF_COLOR_PRIMARIES_BT2020);
  {
    const DeepEditing deep(true);
    const auto read = patchy::avif::read_avif_detailed(pq);
    const auto& document = read.read.document;
    CHECK(std::as_const(document).color_state().bit_depth == BitDepth::Float32);
    CHECK(read.read.notices.empty());
    if (std::as_const(document).color_state().bit_depth == BitDepth::Float32) {
      const float* px = float_pixels(document);
      // 12-bit samples quantize the signal by 1/4095: about 0.3% at these levels.
      CHECK(within(px[0], 1.0, 0.01) && within(px[1], 1.0, 0.01) && within(px[2], 1.0, 0.01));
      CHECK(within(px[3], 1.0, 1e-6));
      CHECK(within(px[4], 1000.0 / 203.0, 0.05));
      CHECK(within(px[8], 0.0, 1e-4));
      // BT.2020 red is outside sRGB: red above 1, green below 0 (kept, not clipped).
      CHECK(within(px[12], 1.6605, 0.02));
      CHECK(px[13] < -0.1F);
    }
  }
  {
    // Without deep editing the same file bakes through the JPEG XR highlight rolloff:
    // reference white lands near 225, the highlight above it.
    const DeepEditing off(false);
    const auto read = patchy::avif::read_avif_detailed(pq);
    const auto& document = read.read.document;
    CHECK(std::as_const(document).color_state().bit_depth == BitDepth::UInt8);
    const auto& pixels = std::as_const(document).layers().front().pixels();
    CHECK(within(pixels.pixel(0, 0)[0], 225.0, 6.0));
    CHECK(pixels.pixel(1, 0)[0] > pixels.pixel(0, 0)[0] + 10);
    CHECK(read.read.notices.size() == 1 && read.read.notices.front().find("tone mapped") != std::string::npos);
  }
  // HLG: the BT.2408 75% signal is reference white.
  const auto hlg = encode_hdr_avif({{0.75, 0.75, 0.75}}, AVIF_TRANSFER_CHARACTERISTICS_HLG, AVIF_COLOR_PRIMARIES_BT2020);
  const DeepEditing deep(true);
  const auto read = patchy::avif::read_avif_detailed(hlg);
  CHECK(std::as_const(read.read.document).color_state().bit_depth == BitDepth::Float32);
  if (std::as_const(read.read.document).color_state().bit_depth == BitDepth::Float32) {
    const float* px = float_pixels(read.read.document);
    CHECK(within(px[0], 1.0, 0.01) && within(px[1], 1.0, 0.01) && within(px[2], 1.0, 0.01));
  }
}

void avif_sdr_high_bit_depth_still_opens_at_16_bits() {
  // A 12-bit sRGB file is not HDR: deep editing keeps it at 16 bits, as before.
  const auto bytes = encode_hdr_avif({{0.5, 0.25, 1.0}}, AVIF_TRANSFER_CHARACTERISTICS_SRGB, AVIF_COLOR_PRIMARIES_BT709);
  const DeepEditing deep(true);
  const auto read = patchy::avif::read_avif_detailed(bytes);
  CHECK(std::as_const(read.read.document).color_state().bit_depth == BitDepth::UInt16);
  CHECK(read.read.notices.empty());
}

#endif  // PATCHY_TEST_LIBAVIF

void heif_orientation_moves_whole_deep_pixels() {
  // 2x1 pixels of 8 bytes (RGBA16): rotating 90 degrees clockwise (orientation 6) stacks
  // them vertically with every byte of each pixel kept together.
  std::vector<std::uint8_t> pixels(16);
  for (std::size_t i = 0; i < pixels.size(); ++i) {
    pixels[i] = static_cast<std::uint8_t>(i);
  }
  const auto rotated = patchy::heif::apply_exif_orientation(pixels, 2, 1, 6, 8);
  CHECK(rotated.width == 1 && rotated.height == 2);
  CHECK(rotated.rgba.size() == 16);
  CHECK(rotated.rgba[0] == 0 && rotated.rgba[7] == 7 && rotated.rgba[8] == 8 && rotated.rgba[15] == 15);
  const auto mirrored = patchy::heif::apply_exif_orientation(pixels, 2, 1, 2, 8);
  CHECK(mirrored.rgba[0] == 8 && mirrored.rgba[8] == 0);
}

// The committed HEIC fixtures were written by macOS ImageIO from self-authored 16-bit values
// (NOTICE-THIRD-PARTY.md): 80x48 pixels, five 16-pixel columns (the Windows codec refuses
// tiny frames), left to right:
//   hdr-pq-10bit.heic:  PQ, BT.2020: 203 nits white, 1000 nits, 100 nits, black, red at 203 nits
//   hdr-hlg-10bit.heic: HLG, BT.2020: 0.75, 0.5, 1.0, black, red at 0.75
//   p3-10bit.heic:      10-bit Display P3 SDR: (0.5 0.25 1), (0.25 0.5 0.75), white, black, gray
template <typename Sample>
const Sample* heif_column(const patchy::Document& document, int column) {
  const auto& pixels = std::as_const(document).layers().front().pixels();
  const auto channels = static_cast<std::size_t>(pixels.format().channels);
  return reinterpret_cast<const Sample*>(pixels.row(24).data()) + static_cast<std::size_t>(8 + 16 * column) * channels;
}

void heif_nclx_transfer_is_read_from_the_meta_box() {
  const auto transfer = [](const char* name) {
    return patchy::heif::nclx_transfer_characteristics(
        patchy::test::read_binary_file(patchy::test::committed_format_fixture_path("heif", name)));
  };
  constexpr std::uint16_t kPq = 16;
  constexpr std::uint16_t kHlg = 18;
  CHECK(transfer("hdr-pq-10bit.heic") == kPq);
  CHECK(transfer("hdr-hlg-10bit.heic") == kHlg);
  const auto p3 = transfer("p3-10bit.heic");
  CHECK(p3 != kPq && p3 != kHlg);
  // No meta box, or a truncated one: nothing.
  CHECK(!patchy::heif::nclx_transfer_characteristics(std::vector<std::uint8_t>{0, 0, 0, 8, 'f', 't', 'y', 'p'}));
  CHECK(!patchy::heif::nclx_transfer_characteristics(std::vector<std::uint8_t>{0, 0, 0, 64, 'm', 'e', 't', 'a'}));
}

// Where the platform decoder exposes them, PQ and HLG HEIF open as 32-bit linear sRGB with
// deep editing on: ImageIO on macOS, WIC on Windows, the host libheif on Linux.
void heif_high_bit_depth_opens_at_full_depth() {
  const auto read = [](const char* name) {
    return patchy::heif::read_heif(
        patchy::test::read_binary_file(patchy::test::committed_format_fixture_path("heif", name)));
  };
  // The decoder may be missing (no Store extension, no libheif plug-in, the Flatpak).
  try {
    const DeepEditing deep(true);
    (void)read("hdr-pq-10bit.heic");
  } catch (const std::runtime_error& error) {
    patchy::test::skip(std::string("HEIC decoder unavailable here: ") + error.what());
  }
  {
    const DeepEditing deep(true);
    const auto pq = read("hdr-pq-10bit.heic");
    CHECK(std::as_const(pq.document).color_state().bit_depth == BitDepth::Float32);
    CHECK(pq.document.width() == 80 && pq.document.height() == 48);
    if (std::as_const(pq.document).color_state().bit_depth == BitDepth::Float32) {
      // Lossy 10-bit HEVC: a few percent.
      CHECK(within(heif_column<float>(pq.document, 0)[0], 1.0, 0.03));
      CHECK(within(heif_column<float>(pq.document, 1)[0], 1000.0 / 203.0, 0.15));
      CHECK(within(heif_column<float>(pq.document, 2)[0], 100.0 / 203.0, 0.02));
      CHECK(within(heif_column<float>(pq.document, 3)[0], 0.0, 0.01));
      CHECK(within(heif_column<float>(pq.document, 4)[0], 1.6605, 0.06));
      CHECK(heif_column<float>(pq.document, 4)[1] < -0.05F);
    }
    const auto hlg = read("hdr-hlg-10bit.heic");
#if defined(_WIN32)
    // WIC maps HLG to a 10,000-nit peak with its own system gamma: kept on the 8-bit path.
    CHECK(std::as_const(hlg.document).color_state().bit_depth == BitDepth::UInt8);
#else
    CHECK(std::as_const(hlg.document).color_state().bit_depth == BitDepth::Float32);
#endif
    if (std::as_const(hlg.document).color_state().bit_depth == BitDepth::Float32) {
      CHECK(within(heif_column<float>(hlg.document, 0)[0], 1.0, 0.03));
      CHECK(within(heif_column<float>(hlg.document, 2)[0], 1000.0 / 203.0, 0.15));
    }
#if defined(_WIN32)
    // WIC hands 10-bit SDR HEIC over as 8 bits.
    const auto p3_depth = BitDepth::UInt8;
#else
    const auto p3_depth = BitDepth::UInt16;
#endif
    const auto p3 = read("p3-10bit.heic");
    CHECK(std::as_const(p3.document).color_state().bit_depth == p3_depth);
    if (std::as_const(p3.document).color_state().bit_depth == BitDepth::UInt16) {
      CHECK(heif_column<std::uint16_t>(p3.document, 2)[0] > 64000);  // white
      CHECK(heif_column<std::uint16_t>(p3.document, 3)[0] < 1000);   // black
    }
#if defined(__APPLE__)
    // An ordinary 8-bit HEIC is not the macOS reader's: it defers to the Qt route as before.
    bool deferred = false;
    try {
      (void)read("quadrants.heic");
    } catch (const std::runtime_error&) {
      deferred = true;
    }
    CHECK(deferred);
#endif
  }
  // Without deep editing nothing changes: 8 bits everywhere (macOS defers to the Qt route).
  const DeepEditing off(false);
#if defined(__APPLE__)
  bool deferred = false;
  try {
    (void)read("hdr-pq-10bit.heic");
  } catch (const std::runtime_error&) {
    deferred = true;
  }
  CHECK(deferred);
#else
  const auto eight_bit = read("hdr-pq-10bit.heic");
  CHECK(std::as_const(eight_bit.document).color_state().bit_depth == BitDepth::UInt8);
#endif
}

#if defined(PATCHY_TEST_LIBAVIF) || defined(__APPLE__)
// Checks the gray patch rows of Patchy's HDR test chart (local-test-fixtures/hdr-display,
// README.txt there) in a 32-bit document: row y=150 holds 0.18 .. 16, row y=410 1.0 .. 2.0.
void check_chart_patches(const patchy::Document& document) {
  CHECK(std::as_const(document).color_state().bit_depth == BitDepth::Float32);
  if (std::as_const(document).color_state().bit_depth != BitDepth::Float32 || document.width() != 1920 ||
      document.height() != 1340) {
    CHECK(document.width() == 1920 && document.height() == 1340);
    return;
  }
  const auto& pixels = std::as_const(document).layers().front().pixels();
  const auto channels = static_cast<std::size_t>(pixels.format().channels);
  const std::array<int, 9> centers{153, 359, 565, 771, 977, 1183, 1389, 1595, 1801};
  const std::array<double, 9> coarse{0.18, 0.5, 1.0, 1.5, 2.0, 3.0, 4.0, 8.0, 16.0};
  const std::array<double, 9> fine{1.0, 1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 1.8, 2.0};
  for (std::size_t i = 0; i < centers.size(); ++i) {
    const auto x = static_cast<std::size_t>(centers[i]);
    // Lossy 10-bit coding: within 5% of the value.
    const auto* top = reinterpret_cast<const float*>(pixels.row(150).data()) + x * channels;
    const auto* second = reinterpret_cast<const float*>(pixels.row(410).data()) + x * channels;
    CHECK(within(top[1], coarse[i], coarse[i] * 0.05));
    CHECK(within(second[1], fine[i], fine[i] * 0.05));
  }
}
#endif

void hdr_chart_from_another_encoder_opens_with_its_patch_values_if_available() {
  // The chart encoded as 10-bit PQ AVIF and HEIC by macOS ImageIO (nclx PQ, BT.2020): a
  // second encoder beside libaom. Untracked; copy local-test-fixtures/hdr-display/pq-samples
  // to run it.
  const auto avif_path = patchy::test::local_format_fixture_path("hdr-display", "pq-samples/chart_pq.avif");
  patchy::test::skip_unless_exists(avif_path);
  const DeepEditing deep(true);
#if defined(PATCHY_TEST_LIBAVIF)
  check_chart_patches(patchy::avif::read_avif_detailed(patchy::test::read_binary_file(avif_path)).read.document);
#endif
#if defined(__APPLE__)
  const auto heic_path = patchy::test::local_format_fixture_path("hdr-display", "pq-samples/chart_pq.heic");
  check_chart_patches(patchy::heif::read_heif(patchy::test::read_binary_file(heic_path)).document);
#endif
}

}  // namespace

std::vector<patchy::test::TestCase> hdr_import_tests() {
  return {
      {"hdr_transfer_decodes_pq_and_hlg_reference_points", hdr_transfer_decodes_pq_and_hlg_reference_points},
      {"heif_orientation_moves_whole_deep_pixels", heif_orientation_moves_whole_deep_pixels},
      {"heif_nclx_transfer_is_read_from_the_meta_box", heif_nclx_transfer_is_read_from_the_meta_box},
      {"heif_high_bit_depth_opens_at_full_depth", heif_high_bit_depth_opens_at_full_depth},
      {"hdr_chart_from_another_encoder_opens_with_its_patch_values_if_available",
       hdr_chart_from_another_encoder_opens_with_its_patch_values_if_available},
#if defined(PATCHY_TEST_LIBAVIF)
      {"avif_pq_and_hlg_open_as_linear_32_bit", avif_pq_and_hlg_open_as_linear_32_bit},
      {"avif_sdr_high_bit_depth_still_opens_at_16_bits", avif_sdr_high_bit_depth_still_opens_at_16_bits},
#endif
  };
}
