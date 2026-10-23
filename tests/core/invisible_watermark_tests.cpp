// Invisible watermark detection (docs/file-info.md, "Watermarks"): Patchy's dwtDct
// decoder against images marked by a reference of the library's embedding algorithm
// (tests/invisible_watermark_reference.hpp), and clean images that must not match.

#include "core/invisible_watermark.hpp"
#include "invisible_watermark_reference.hpp"
#include "test_groups.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;
using patchy::test::WatermarkEmbedPath;

const patchy::KnownInvisibleWatermark& known(std::string_view id) {
  const auto& all = patchy::known_invisible_watermarks();
  const auto found =
      std::find_if(all.begin(), all.end(), [&](const auto& item) { return item.id == id; });
  CHECK(found != all.end());
  return *found;
}

patchy::InvisibleWatermarkScan scan(const Bytes& rgb, int width, int height) {
  return patchy::scan_invisible_watermarks(rgb, width, height, static_cast<std::size_t>(width) * 3, 3);
}

int distance(const Bytes& a, const Bytes& b) {
  int differing = 0;
  for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i) {
    differing += a[i] != b[i] ? 1 : 0;
  }
  return differing + static_cast<int>(std::max(a.size(), b.size()) - std::min(a.size(), b.size()));
}

void watermark_payloads_and_tolerance_are_pinned() {
  CHECK(patchy::known_invisible_watermarks().size() == 3);
  const auto& sd1 = known("stableDiffusion1");
  CHECK(sd1.bits.size() == 136);
  // "S" = 0x53.
  CHECK((Bytes(sd1.bits.begin(), sd1.bits.begin() + 8) == Bytes{0, 1, 0, 1, 0, 0, 1, 1}));
  CHECK(known("stableDiffusion2").bits.size() == 32);
  const auto& xl = known("stableDiffusionXL");
  CHECK(xl.bits.size() == 48);
  // generative-models' WATERMARK_MESSAGE = 0b101100111110110010010000011110111011000110011110.
  const std::string_view message = "101100111110110010010000011110111011000110011110";
  for (std::size_t i = 0; i < message.size(); ++i) {
    CHECK(xl.bits[i] == (message[i] == '1' ? 1 : 0));
  }
  CHECK(patchy::invisible_watermark_tolerance(136) == 11);
  CHECK(patchy::invisible_watermark_tolerance(48) == 4);
  CHECK(patchy::invisible_watermark_tolerance(32) == 2);
}

void watermark_decoder_finds_each_known_payload() {
  struct Case {
    int width;
    int height;
    std::uint64_t seed;
  };
  for (const auto& image : {Case{512, 512, 11}, Case{768, 512, 12}, Case{517, 389, 13}}) {
    const auto clean = patchy::test::watermark_fixture_image(image.width, image.height, image.seed);
    for (const auto& payload : patchy::known_invisible_watermarks()) {
      for (const auto path : {WatermarkEmbedPath::Uint8, WatermarkEmbedPath::Float}) {
        const auto marked =
            patchy::test::embed_dwt_dct_reference(clean, image.width, image.height, payload.bits, path);
        const bool centered = path == WatermarkEmbedPath::Float;
        const auto decode = [&](patchy::DwtDctBitRule rule) {
          return distance(patchy::decode_dwt_dct_bits(marked, image.width, image.height,
                                                      static_cast<std::size_t>(image.width) * 3, 3,
                                                      static_cast<int>(payload.bits.size()),
                                                      {false, centered, rule}),
                          payload.bits);
        };
        const auto library = decode(patchy::DwtDctBitRule::Library);
        const auto cluster = decode(patchy::DwtDctBitRule::TwoCluster);
        const auto differing = std::min(library, cluster);
        std::cout << "  " << payload.id << " " << image.width << "x" << image.height
                  << (path == WatermarkEmbedPath::Uint8 ? " uint8" : " float") << ": " << library
                  << " (library rule) / " << cluster << " (two-cluster) of " << payload.bits.size()
                  << " bits differ\n";
        const auto result = scan(marked, image.width, image.height);
        CHECK(result.scanned);
        CHECK(result.matches.size() == 1);
        if (result.matches.size() == 1) {
          CHECK(result.matches[0].id == payload.id);
          CHECK(!result.matches[0].red_blue_swapped);
          CHECK(result.matches[0].differing_bits <= differing);
          CHECK(result.matches[0].bit_count == static_cast<int>(payload.bits.size()));
        }
      }
    }
  }
}

void watermark_decoder_reads_swapped_channel_order() {
  // A caller that hands the library RGB where it expects BGR marks the other chroma axis.
  const auto clean = patchy::test::watermark_fixture_image(512, 512, 21);
  const auto& xl = known("stableDiffusionXL");
  const auto marked =
      patchy::test::embed_dwt_dct_reference(clean, 512, 512, xl.bits, WatermarkEmbedPath::Float, true);
  const auto result = scan(marked, 512, 512);
  CHECK(result.matches.size() == 1);
  if (result.matches.size() == 1) {
    CHECK(result.matches[0].id == "stableDiffusionXL");
    CHECK(result.matches[0].red_blue_swapped);
  }
}

void watermark_decoder_ignores_clean_and_other_images() {
  // Clean images of several kinds: photo-like, flat, gradient, checkerboard, noise.
  std::vector<std::pair<Bytes, std::pair<int, int>>> images;
  for (std::uint64_t seed = 100; seed < 106; ++seed) {
    images.push_back({patchy::test::watermark_fixture_image(512, 512, seed), {512, 512}});
  }
  Bytes flat(512 * 512 * 3, 128);
  images.push_back({flat, {512, 512}});
  Bytes gradient(640 * 480 * 3);
  Bytes checker(640 * 480 * 3);
  Bytes noise(640 * 480 * 3);
  std::uint64_t state = 0xC1EA4ULL;
  for (int y = 0; y < 480; ++y) {
    for (int x = 0; x < 640; ++x) {
      const auto i = (static_cast<std::size_t>(y) * 640 + x) * 3;
      gradient[i] = static_cast<std::uint8_t>(x * 255 / 639);
      gradient[i + 1] = static_cast<std::uint8_t>(y * 255 / 479);
      gradient[i + 2] = static_cast<std::uint8_t>(255 - x * 255 / 639);
      const std::uint8_t cell = ((x / 8 + y / 8) % 2) != 0 ? 230 : 20;
      checker[i] = cell;
      checker[i + 1] = static_cast<std::uint8_t>(255 - cell);
      checker[i + 2] = cell;
      for (int c = 0; c < 3; ++c) {
        noise[i + static_cast<std::size_t>(c)] =
            static_cast<std::uint8_t>(patchy::test::watermark_fixture_splitmix64(state));
      }
    }
  }
  images.push_back({gradient, {640, 480}});
  images.push_back({checker, {640, 480}});
  images.push_back({noise, {640, 480}});
  // Another 48-bit message in the same scheme (an unknown payload) is not reported.
  Bytes other(48);
  for (std::size_t i = 0; i < other.size(); ++i) {
    other[i] = static_cast<std::uint8_t>((i * 7 + 3) % 5 < 2 ? 1 : 0);
  }
  CHECK(distance(other, known("stableDiffusionXL").bits) > 12);
  images.push_back({patchy::test::embed_dwt_dct_reference(images.front().first, 512, 512, other,
                                                          WatermarkEmbedPath::Float),
                    {512, 512}});
  for (const auto& [rgb, size] : images) {
    const auto result = scan(rgb, size.first, size.second);
    CHECK(result.scanned);
    CHECK(result.matches.empty());
    for (const auto& payload : patchy::known_invisible_watermarks()) {
      for (int reading = 0; reading < 8; ++reading) {
        const patchy::DwtDctDecodeOptions options{
            (reading & 1) != 0, (reading & 2) != 0,
            (reading & 4) != 0 ? patchy::DwtDctBitRule::TwoCluster : patchy::DwtDctBitRule::Library};
        const auto bits = patchy::decode_dwt_dct_bits(rgb, size.first, size.second,
                                                      static_cast<std::size_t>(size.first) * 3, 3,
                                                      static_cast<int>(payload.bits.size()), options);
        CHECK(distance(bits, payload.bits) >
              patchy::invisible_watermark_tolerance(static_cast<int>(payload.bits.size())));
      }
    }
  }
}

void watermark_decoder_refuses_small_or_malformed_input() {
  const auto tiny = patchy::test::watermark_fixture_image(255, 256, 31);
  CHECK(!scan(tiny, 255, 256).scanned);
  CHECK(patchy::decode_dwt_dct_bits(tiny, 255, 256, 255 * 3, 3, 48).empty());
  const auto image = patchy::test::watermark_fixture_image(300, 300, 32);
  // Short buffers, a bad stride or channel count never read out of bounds.
  CHECK(!patchy::scan_invisible_watermarks(std::span(image).first(image.size() - 1), 300, 300, 900, 3).scanned);
  CHECK(!patchy::scan_invisible_watermarks(image, 300, 300, 899, 3).scanned);
  CHECK(!patchy::scan_invisible_watermarks(image, 300, 300, 900, 2).scanned);
  CHECK(!patchy::scan_invisible_watermarks(image, -1, 300, 900, 3).scanned);
  // RGBA rows (alpha ignored) and the PixelBuffer overload decode the same bits.
  const auto& xl = known("stableDiffusionXL");
  const auto marked = patchy::test::embed_dwt_dct_reference(image, 300, 300, xl.bits, WatermarkEmbedPath::Uint8);
  patchy::PixelBuffer rgba(300, 300, patchy::PixelFormat::rgba8());
  for (int y = 0; y < 300; ++y) {
    auto row = rgba.row(y);
    for (int x = 0; x < 300; ++x) {
      const auto source = (static_cast<std::size_t>(y) * 300 + x) * 3;
      std::copy_n(marked.begin() + static_cast<std::ptrdiff_t>(source), 3, row.begin() + x * 4);
      row[static_cast<std::size_t>(x) * 4 + 3] = static_cast<std::uint8_t>(x);
    }
  }
  const auto from_rgba = patchy::scan_invisible_watermarks(rgba);
  const auto from_rgb = scan(marked, 300, 300);
  CHECK(from_rgba.scanned && from_rgb.scanned);
  CHECK(from_rgba.matches.size() == from_rgb.matches.size());
  CHECK(!patchy::scan_invisible_watermarks(patchy::PixelBuffer(300, 300, patchy::PixelFormat::rgb16())).scanned);
}

}  // namespace

std::vector<patchy::test::TestCase> invisible_watermark_tests() {
  return {
      {"watermark_payloads_and_tolerance_are_pinned", watermark_payloads_and_tolerance_are_pinned},
      {"watermark_decoder_finds_each_known_payload", watermark_decoder_finds_each_known_payload},
      {"watermark_decoder_reads_swapped_channel_order", watermark_decoder_reads_swapped_channel_order},
      {"watermark_decoder_ignores_clean_and_other_images", watermark_decoder_ignores_clean_and_other_images},
      {"watermark_decoder_refuses_small_or_malformed_input", watermark_decoder_refuses_small_or_malformed_input},
  };
}
