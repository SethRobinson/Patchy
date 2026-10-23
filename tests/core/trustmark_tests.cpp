// TrustMark watermarks (docs/watermarks.md): the packet layout and BCH code against vectors
// from Adobe's Python reference, the image pre- and post-processing, and, when the watermark
// models are staged (<build>/ai-models-download), decoding the reference's own marked images
// from local-test-fixtures/trustmark and an embed-then-scan round trip.

#include "ai/trustmark_watermark.hpp"
#include "core/trustmark_payload.hpp"
#include "formats/stb/stb_image.h"
#include "local_psd_fixtures.hpp"
#include "test_groups.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace tm = patchy::trustmark;
using Bits = std::vector<std::uint8_t>;

Bits bits_of(std::string_view text) {
  Bits bits;
  for (const char ch : text) {
    bits.push_back(static_cast<std::uint8_t>(ch == '1' ? 1 : 0));
  }
  return bits;
}

std::string text_of(const tm::Packet& packet) {
  std::string text;
  for (const auto bit : packet) {
    text.push_back(bit != 0 ? '1' : '0');
  }
  return text;
}

std::uint64_t next_random(std::uint64_t& state) {
  std::uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
  z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31U);
}

// Packets the Python reference built (local-test-fixtures/trustmark/fixtures.json; the
// numbers are pinned here so the test needs no fixture).
void trustmark_packets_match_the_reference() {
  CHECK(text_of(tm::encode_packet(tm::text_to_bits("mysecret"), tm::Schema::Bch5)) ==
        "1101101111100111100111100101110001111100101100101111010000000111010110000100010000110111010111110001");
  const char* const text_vectors[] = {
      "1010000100000110101001000011100100010110011101011000011110011011110010001000010011101100001110010000",
      "1010000100000110101001000011100100010110010000000000000000000111000000000010100001100101000010000001",
      "1010000100000110101001000011100100010110010000000000000000000000000001010000101101000001000001100010",
      "1010000100000110101001000011100100010110010000000000000000000000000000000000101110100110010011010011",
  };
  const char* const binary_vectors[] = {
      "1100101000000000000000000000000000000000011110010100011110011011111011110100110110001010001011010000",
      "1100101000000000000000000000000000000000000000000000000000000110001110010010001111010110110101110001",
      "1100101000000000000000000000000000000000000000000000000000000000000010001111111011110001110000110010",
      "1100101000000000000000000000000000000000000000000000000000000000000000000000110010000001100111110011",
  };
  for (int code = 0; code < 4; ++code) {
    const auto schema = static_cast<tm::Schema>(code);
    CHECK(text_of(tm::encode_packet(tm::text_to_bits("PATCHY"), schema)) == text_vectors[code]);
    CHECK(text_of(tm::encode_packet(bits_of("1100101"), schema)) == binary_vectors[code]);
    // A clean packet decodes to itself with nothing corrected.
    tm::Packet packet{};
    const auto expected = bits_of(text_vectors[code]);
    std::copy(expected.begin(), expected.end(), packet.begin());
    const auto decoded = tm::decode_packet(packet);
    CHECK(decoded.has_value());
    if (decoded) {
      CHECK(decoded->schema == schema);
      CHECK(decoded->corrected_bits == 0);
      CHECK(static_cast<int>(decoded->data_bits.size()) == tm::schema_data_bits(schema));
    }
  }
  CHECK(tm::schema_data_bits(tm::Schema::BchSuper) == 40 && tm::schema_data_bits(tm::Schema::Bch3) == 75);
  CHECK(tm::schema_text_capacity(tm::Schema::Bch5) == 8);
  CHECK(tm::schema_text_capacity(tm::Schema::BchSuper) == 5);
  CHECK(tm::schema_name(tm::Schema::Bch4) == "BCH_4");
  CHECK(tm::schema_from_name("BCH_SUPER") == tm::Schema::BchSuper);
  CHECK(!tm::schema_from_name("BCH_6").has_value());
}

void trustmark_bch_corrects_up_to_its_limit() {
  std::uint64_t state = 0x7A5711ULL;
  for (int code = 0; code < 4; ++code) {
    const auto schema = static_cast<tm::Schema>(code);
    const int t = tm::schema_correctable_bits(schema);
    for (int trial = 0; trial < 40; ++trial) {
      Bits data(static_cast<std::size_t>(tm::schema_data_bits(schema)));
      for (auto& bit : data) {
        bit = static_cast<std::uint8_t>(next_random(state) & 1U);
      }
      const auto packet = tm::encode_packet(data, schema);
      auto damaged = packet;
      const int flips = trial % (t + 1);
      std::vector<int> positions;
      while (static_cast<int>(positions.size()) < flips) {
        // The 96 coded bits only: the schema code is read before correction.
        const int position = static_cast<int>(next_random(state) % 96U);
        if (std::find(positions.begin(), positions.end(), position) == positions.end()) {
          positions.push_back(position);
          damaged[static_cast<std::size_t>(position)] ^= 1U;
        }
      }
      const auto decoded = tm::decode_packet(damaged);
      CHECK(decoded.has_value());
      if (decoded) {
        CHECK(decoded->data_bits == data);
        CHECK(decoded->corrected_bits == flips);
      }
    }
  }
}

void trustmark_text_and_hex_forms() {
  const auto bits = tm::text_to_bits("Patchy26");
  CHECK(bits.size() == 56);
  CHECK(tm::bits_to_text(bits) == "Patchy26");
  auto padded = bits;
  padded.resize(61, 0);
  CHECK(tm::bits_to_text(padded) == "Patchy26");
  CHECK(tm::bits_to_text(tm::text_to_bits("  hi  ")) == "hi");
  CHECK(tm::bits_to_text(Bits{0, 0, 0, 0, 0, 0, 1}).empty());  // a control character: an ID, not text
  CHECK(tm::bits_to_hex(bits_of("1010111")) == "0xAE");
  CHECK(tm::bits_to_hex(bits_of("11110000")) == "0xF0");
}

patchy::PixelBuffer solid(int width, int height, int channels, std::uint8_t value) {
  patchy::PixelBuffer image(width, height, channels == 4 ? patchy::PixelFormat::rgba8() : patchy::PixelFormat::rgb8());
  image.clear(value);
  return image;
}

void trustmark_region_and_model_input() {
  using patchy::ai::TrustMarkVariant;
  const auto whole = patchy::ai::trustmark_region(800, 500, TrustMarkVariant::Q);
  CHECK(whole.x == 0 && whole.y == 0 && whole.width == 800 && whole.height == 500);
  const auto wide = patchy::ai::trustmark_region(1400, 560, TrustMarkVariant::Q);
  CHECK(wide.x == 420 && wide.y == 0 && wide.width == 560 && wide.height == 560);
  const auto square = patchy::ai::trustmark_region(800, 500, TrustMarkVariant::P);
  CHECK(square.x == 150 && square.width == 500 && square.height == 500);
  CHECK(patchy::ai::trustmark_decoder_input_size(TrustMarkVariant::P) == 224);
  CHECK(patchy::ai::trustmark_decoder_input_size(TrustMarkVariant::C) == 256);

  auto image = solid(1000, 700, 3, 191);
  // A left half at 0 and a right half at 255: the triangle filter averages across the edge.
  for (int y = 0; y < image.height(); ++y) {
    auto row = image.row(y);
    for (int x = 0; x < image.width(); ++x) {
      for (int c = 0; c < 3; ++c) {
        row[static_cast<std::size_t>(x) * 3 + static_cast<std::size_t>(c)] = x < 500 ? 0 : 255;
      }
    }
  }
  const auto input = patchy::ai::trustmark_model_input(image, {0, 0, 1000, 700}, 256);
  CHECK(input.size() == 3U * 256U * 256U);
  CHECK(input[0] == -1.0F);
  CHECK(input[255] == 1.0F);
  CHECK(input[2U * 256U * 256U + 128U * 256U + 64U] == -1.0F);
  const float middle = input[128U * 256U + 128U];
  CHECK(middle > -1.0F && middle < 1.0F);  // averaged across the edge, not point-sampled
  CHECK(patchy::ai::trustmark_model_input(solid(4, 4, 3, 0), {0, 0, 0, 0}, 256).empty());
}

void trustmark_residual_keeps_alpha_and_mean_free() {
  constexpr std::size_t plane = 256U * 256U;
  auto image = solid(300, 200, 4, 128);
  for (int y = 0; y < image.height(); ++y) {
    auto row = image.row(y);
    for (int x = 0; x < image.width(); ++x) {
      row[static_cast<std::size_t>(x) * 4 + 3] = static_cast<std::uint8_t>(x % 251);
    }
  }
  const auto cover = patchy::ai::trustmark_model_input(image, {0, 0, 300, 200}, 256);
  // Identical stego: nothing changes.
  auto unchanged = image;
  patchy::ai::apply_trustmark_residual(unchanged, {0, 0, 300, 200}, cover, cover, 1.0F);
  CHECK(std::equal(unchanged.data().begin(), unchanged.data().end(), image.data().begin()));
  // A uniform shift is a color shift: removed with the per-channel mean, so still nothing.
  auto shifted = cover;
  for (auto& value : shifted) {
    value += 0.1F;
  }
  auto still = image;
  patchy::ai::apply_trustmark_residual(still, {0, 0, 300, 200}, cover, shifted, 1.0F);
  CHECK(std::equal(still.data().begin(), still.data().end(), image.data().begin()));
  // A checker pattern shows in the color channels; alpha is untouched.
  auto pattern = cover;
  for (std::size_t c = 0; c < 3U; ++c) {
    for (std::size_t i = 0; i < plane; ++i) {
      pattern[c * plane + i] += (((i / 256U) / 32U + (i % 256U) / 32U) % 2U) != 0 ? 0.08F : -0.08F;
    }
  }
  auto marked = image;
  patchy::ai::apply_trustmark_residual(marked, {0, 0, 300, 200}, cover, pattern, 1.0F);
  int changed = 0;
  for (int y = 0; y < marked.height(); ++y) {
    const auto before = image.row(y);
    const auto after = marked.row(y);
    for (int x = 0; x < marked.width(); ++x) {
      CHECK(after[static_cast<std::size_t>(x) * 4 + 3] == before[static_cast<std::size_t>(x) * 4 + 3]);
      changed += after[static_cast<std::size_t>(x) * 4] != before[static_cast<std::size_t>(x) * 4] ? 1 : 0;
    }
  }
  CHECK(changed > 300 * 200 / 2);
}

// --------------------------------------------------------------- with the models

bool watermark_models_staged() {
#if defined(PATCHY_HAVE_AI_SELECTION)
  patchy::ai::set_model_directory(std::filesystem::current_path() / "ai-models");
  patchy::ai::set_downloaded_model_directory(std::filesystem::current_path() / "ai-models-download");
  if (!patchy::ai::trustmark_models_available()) {
    std::printf("  %s\n", patchy::ai::trustmark_unavailable_reason().c_str());
    return false;
  }
  return true;
#else
  CHECK(!patchy::ai::trustmark_models_available());
  return false;
#endif
}

struct DownloadFolderReset {
  ~DownloadFolderReset() { patchy::ai::set_downloaded_model_directory({}); }
};

std::filesystem::path fixture(std::string_view name) {
  return patchy::test::local_format_fixture_path("trustmark", name);
}

patchy::PixelBuffer load_rgb(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  int width = 0;
  int height = 0;
  int channels = 0;
  auto* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels, 3);
  CHECK(pixels != nullptr);
  patchy::PixelBuffer image(width, height, patchy::PixelFormat::rgb8());
  if (pixels != nullptr) {
    std::copy_n(pixels, static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3U, image.data().begin());
    stbi_image_free(pixels);
  }
  return image;
}

void trustmark_decodes_the_reference_marks() {
  const DownloadFolderReset reset;
  if (!watermark_models_staged()) {
    patchy::test::skip("the watermark models are not staged (setup-ai-deps provisions them)");
  }
  if (!std::filesystem::exists(fixture("trustmark-q-bch5-text.png"))) {
    patchy::test::skip("optional fixture missing: local-test-fixtures/trustmark");
  }
  struct Case {
    const char* name;
    patchy::ai::TrustMarkVariant variant;
    tm::Schema schema;
    const char* text;
    const char* bits;
  };
  const Case cases[] = {
      {"trustmark-q-bch5-text", patchy::ai::TrustMarkVariant::Q, tm::Schema::Bch5, "Patchy26", nullptr},
      {"trustmark-p-bch4-text", patchy::ai::TrustMarkVariant::P, tm::Schema::Bch4, "Patchy P4", nullptr},
      {"trustmark-c-super-binary", patchy::ai::TrustMarkVariant::C, tm::Schema::BchSuper, nullptr,
       "1011001110110110001110000111100110100101"},
      {"trustmark-q-bch3-wide", patchy::ai::TrustMarkVariant::Q, tm::Schema::Bch3, "WideImage1", nullptr},
  };
  for (const auto& item : cases) {
    for (const char* suffix : {".png", "-q90.jpg"}) {
      const auto scan = patchy::ai::scan_trustmark(load_rgb(fixture(std::string(item.name) + suffix)));
      CHECK(scan.ran);
      CHECK(scan.marks.size() == 1);
      if (scan.marks.size() != 1) {
        continue;
      }
      const auto& mark = scan.marks.front();
      std::printf("  %s%s: variant %s, %s, %d corrected, confidence %.1f, text \"%s\"\n", item.name, suffix,
                  std::string(patchy::ai::trustmark_variant_name(mark.variant)).c_str(),
                  std::string(tm::schema_name(mark.schema)).c_str(), mark.corrected_bits, mark.confidence,
                  mark.text.c_str());
      CHECK(mark.variant == item.variant);
      CHECK(mark.schema == item.schema);
      CHECK(mark.confidence >= patchy::ai::kTrustMarkMinimumConfidence);
      if (item.text != nullptr) {
        CHECK(mark.text == item.text);
      } else {
        CHECK(mark.data_bits == bits_of(item.bits));
      }
    }
  }
  const auto clean = patchy::ai::scan_trustmark(load_rgb(fixture("no-trustmark.png")));
  CHECK(clean.ran);
  CHECK(clean.marks.empty());
}

void trustmark_embed_then_scan_round_trip() {
  const DownloadFolderReset reset;
  if (!watermark_models_staged()) {
    patchy::test::skip("the watermark models are not staged (setup-ai-deps provisions them)");
  }
  // A photo-like synthetic image (smooth color fields and fine noise), RGBA with varying alpha.
  patchy::PixelBuffer image(640, 420, patchy::PixelFormat::rgba8());
  std::uint64_t state = 0x5EED;
  for (int y = 0; y < image.height(); ++y) {
    auto row = image.row(y);
    for (int x = 0; x < image.width(); ++x) {
      const int noise = static_cast<int>(next_random(state) % 17U) - 8;
      auto* px = row.data() + static_cast<std::size_t>(x) * 4U;
      px[0] = static_cast<std::uint8_t>(std::clamp(60 + x * 150 / 640 + noise, 0, 255));
      px[1] = static_cast<std::uint8_t>(std::clamp(90 + y * 120 / 420 + noise, 0, 255));
      px[2] = static_cast<std::uint8_t>(std::clamp(200 - (x + y) * 90 / 1060 + noise, 0, 255));
      px[3] = static_cast<std::uint8_t>(200 + (x % 56));
    }
  }
  const auto original = image;
  const auto packet = tm::encode_packet(tm::text_to_bits("Patchy01"), tm::Schema::Bch5);
  patchy::ai::embed_trustmark_q(image, packet, patchy::ai::kTrustMarkDefaultStrength);
  int changed = 0;
  long long total_difference = 0;
  for (int y = 0; y < image.height(); ++y) {
    const auto before = original.row(y);
    const auto after = image.row(y);
    for (std::size_t i = 0; i < before.size(); ++i) {
      if (i % 4U == 3U) {
        CHECK(before[i] == after[i]);
        continue;
      }
      changed += before[i] != after[i] ? 1 : 0;
      total_difference += std::abs(static_cast<int>(before[i]) - static_cast<int>(after[i]));
    }
  }
  const double mean_difference = static_cast<double>(total_difference) / (640.0 * 420.0 * 3.0);
  std::printf("  embed changed %d values, mean |difference| %.2f levels\n", changed, mean_difference);
  CHECK(changed > 0);
  CHECK(mean_difference < 4.0);  // invisible: TrustMark Q averages about 43 dB PSNR
  const auto scan = patchy::ai::scan_trustmark(image);
  CHECK(scan.marks.size() == 1);
  if (!scan.marks.empty()) {
    CHECK(scan.marks.front().variant == patchy::ai::TrustMarkVariant::Q);
    CHECK(scan.marks.front().schema == tm::Schema::Bch5);
    CHECK(scan.marks.front().text == "Patchy01");
  }
  CHECK(patchy::ai::scan_trustmark(original).marks.empty());
}

}  // namespace

std::vector<patchy::test::TestCase> trustmark_tests() {
  return {
      {"trustmark_packets_match_the_reference", trustmark_packets_match_the_reference},
      {"trustmark_bch_corrects_up_to_its_limit", trustmark_bch_corrects_up_to_its_limit},
      {"trustmark_text_and_hex_forms", trustmark_text_and_hex_forms},
      {"trustmark_region_and_model_input", trustmark_region_and_model_input},
      {"trustmark_residual_keeps_alpha_and_mean_free", trustmark_residual_keeps_alpha_and_mean_free},
      {"trustmark_decodes_the_reference_marks", trustmark_decodes_the_reference_marks},
      {"trustmark_embed_then_scan_round_trip", trustmark_embed_then_scan_round_trip},
  };
}
