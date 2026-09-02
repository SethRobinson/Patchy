// miniz's zlib-compatibility aliases turn the identifier `compress` into a macro, which
// would rewrite WriteOptions::compress; keep only the mz_* names in this TU.
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES

#include "core/document.hpp"
#include "core/layer_tree.hpp"
#include "formats/format_registry.hpp"
#include "formats/miniz/miniz.h"
#include "formats/rttex_document_io.hpp"

#include "core_test_support.hpp"
#include "local_psd_fixtures.hpp"
#include "test_harness.hpp"
#include "unicode_path_names.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

using patchy::rttex::Encoding;
using patchy::rttex::PowerOfTwo;
using patchy::rttex::WriteOptions;

// A gradient with a distinct top-left pixel, so a flipped or shifted decode produces wrong
// values instead of a plausible picture. `translucent` puts one half-transparent pixel at
// (1, 0), which is what flips the writer's alpha auto-detection.
[[nodiscard]] patchy::Document gradient_document(std::int32_t width, std::int32_t height, bool translucent) {
  patchy::Document document(width, height, translucent ? patchy::PixelFormat::rgba8() : patchy::PixelFormat::rgb8());
  patchy::PixelBuffer pixels(width, height, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < height; ++y) {
    auto row = pixels.row(y);
    for (std::int32_t x = 0; x < width; ++x) {
      auto* pixel = row.data() + static_cast<std::size_t>(x) * 4U;
      pixel[0] = static_cast<std::uint8_t>((x * 37 + 5) & 255);
      pixel[1] = static_cast<std::uint8_t>((y * 59 + 9) & 255);
      pixel[2] = static_cast<std::uint8_t>(((x + y) * 13 + 200) & 255);
      pixel[3] = translucent && x == 1 && y == 0 ? std::uint8_t{128} : std::uint8_t{255};
    }
  }
  pixels.row(0)[0] = 250;
  pixels.row(0)[1] = 10;
  pixels.row(0)[2] = 20;
  document.add_pixel_layer("Background", std::move(pixels));
  return document;
}

[[nodiscard]] std::array<int, 4> pixel_at(const patchy::Document& document, std::int32_t x, std::int32_t y) {
  const auto& pixels = std::as_const(document.layers().front()).pixels();
  const auto channels = pixels.format().channels;
  const auto* source = pixels.row(y).data() + static_cast<std::size_t>(x) * channels;
  return {source[0], source[1], source[2], channels >= 4 ? source[3] : 255};
}

[[nodiscard]] int channel_count(const patchy::Document& document) {
  return std::as_const(document.layers().front()).pixels().format().channels;
}

[[nodiscard]] std::uint32_t u32_at(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
  return static_cast<std::uint32_t>(bytes[offset]) | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8U) |
         (static_cast<std::uint32_t>(bytes[offset + 2]) << 16U) | (static_cast<std::uint32_t>(bytes[offset + 3]) << 24U);
}

[[nodiscard]] std::int32_t i32_at(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
  return static_cast<std::int32_t>(u32_at(bytes, offset));
}

void put_i32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::int32_t value) {
  const auto raw = static_cast<std::uint32_t>(value);
  bytes[offset] = static_cast<std::uint8_t>(raw & 0xffU);
  bytes[offset + 1] = static_cast<std::uint8_t>((raw >> 8U) & 0xffU);
  bytes[offset + 2] = static_cast<std::uint8_t>((raw >> 16U) & 0xffU);
  bytes[offset + 3] = static_cast<std::uint8_t>((raw >> 24U) & 0xffU);
}

void append_i32(std::vector<std::uint8_t>& bytes, std::int32_t value) {
  bytes.resize(bytes.size() + 4);
  put_i32(bytes, bytes.size() - 4, value);
}

// Strips an RTPACK wrapper with the test's own inflate so the reader is not trusted to check
// itself; bare textures pass through.
[[nodiscard]] std::vector<std::uint8_t> unwrap(const std::vector<std::uint8_t>& bytes) {
  if (bytes.size() < 6 || std::memcmp(bytes.data(), "RTPACK", 6) != 0) {
    return bytes;
  }
  const auto compressed_size = u32_at(bytes, 8);
  const auto decompressed_size = u32_at(bytes, 12);
  CHECK(bytes[16] == 1);
  CHECK(bytes.size() == patchy::rttex::kRtpackHeaderSize + compressed_size);
  std::vector<std::uint8_t> inflated(decompressed_size);
  mz_ulong length = decompressed_size;
  CHECK(mz_uncompress(inflated.data(), &length, bytes.data() + patchy::rttex::kRtpackHeaderSize, compressed_size) ==
        MZ_OK);
  CHECK(length == decompressed_size);
  return inflated;
}

[[nodiscard]] std::vector<std::uint8_t> wrap_in_rtpack(const std::vector<std::uint8_t>& raw) {
  mz_ulong length = mz_compressBound(static_cast<mz_ulong>(raw.size()));
  std::vector<std::uint8_t> compressed(length);
  CHECK(mz_compress(compressed.data(), &length, raw.data(), static_cast<mz_ulong>(raw.size())) == MZ_OK);
  compressed.resize(length);
  std::vector<std::uint8_t> packed = {'R', 'T', 'P', 'A', 'C', 'K', 0, 0};
  append_i32(packed, static_cast<std::int32_t>(compressed.size()));
  append_i32(packed, static_cast<std::int32_t>(raw.size()));
  packed.push_back(1);
  packed.resize(patchy::rttex::kRtpackHeaderSize, 0);
  packed.insert(packed.end(), compressed.begin(), compressed.end());
  return packed;
}

[[nodiscard]] bool read_throws(const std::vector<std::uint8_t>& bytes, const char* expected_fragment = nullptr) {
  try {
    (void)patchy::rttex::read_rttex(bytes);
  } catch (const std::exception& error) {
    return expected_fragment == nullptr || std::string(error.what()).find(expected_fragment) != std::string::npos;
  }
  return false;
}

[[nodiscard]] std::string metadata_value(const patchy::Document& document, const char* key) {
  const auto& values = document.metadata().values;
  const auto found = values.find(key);
  return found == values.end() ? std::string() : found->second;
}

// Capture-free encoder for the JPEG test: hands back a real JPEG lifted from the committed
// fixture, so the write path and the stb decode both run without Qt.
std::vector<std::uint8_t> g_test_jpeg;
std::int32_t g_test_encoder_width = 0;
std::int32_t g_test_encoder_height = 0;
int g_test_encoder_quality = 0;

std::vector<std::uint8_t> test_jpeg_encoder(const patchy::rttex::RgbImage& image, int quality) {
  g_test_encoder_width = image.width;
  g_test_encoder_height = image.height;
  g_test_encoder_quality = quality;
  return g_test_jpeg;
}

void rttex_extensions_sniff_and_registry_routing() {
  CHECK(patchy::rttex::is_rttex_extension("rttex"));
  CHECK(patchy::rttex::is_rttex_extension(".RTTEX"));
  CHECK(!patchy::rttex::is_rttex_extension("rtfont"));
  CHECK(!patchy::rttex::is_rttex_extension("tex"));

  const auto* handler = patchy::builtin_format_registry().find_by_extension(".rttex");
  CHECK(handler != nullptr);
  CHECK(handler->identifier == "patchy.formats.rttex");
  CHECK(handler->can_write());

  const std::vector<std::uint8_t> packed = {'R', 'T', 'P', 'A', 'C', 'K', 0, 0, 1, 0};
  const std::vector<std::uint8_t> bare = {'R', 'T', 'T', 'X', 'T', 'R', 0, 0};
  const std::vector<std::uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
  const std::vector<std::uint8_t> tiff = {0x49, 0x49, 0x2A, 0x00, 0x08, 0x00, 0x00, 0x00};
  CHECK(patchy::rttex::sniff(packed));
  CHECK(patchy::rttex::sniff(bare));
  CHECK(!patchy::rttex::sniff(png));
  CHECK(!patchy::rttex::sniff(tiff));
  CHECK(!patchy::rttex::sniff(std::vector<std::uint8_t>{'R', 'T', 'T'}));
  CHECK(!patchy::rttex::sniff(std::vector<std::uint8_t>{}));
  CHECK(handler->sniff(packed));

  CHECK(patchy::rttex::next_power_of_two(1) == 1);
  CHECK(patchy::rttex::next_power_of_two(2) == 2);
  CHECK(patchy::rttex::next_power_of_two(3) == 4);
  CHECK(patchy::rttex::next_power_of_two(480) == 512);
  CHECK(patchy::rttex::next_power_of_two(1024) == 1024);
}

void rttex_uncompressed_layout_matches_proton_spec() {
  // A 2x1 opaque image already at a power of two: the file is exactly the 100-byte texture
  // header, the 24-byte mip header, and six pixel bytes, laid out like Proton's structs.
  patchy::Document document(2, 1, patchy::PixelFormat::rgb8());
  patchy::PixelBuffer pixels(2, 1, patchy::PixelFormat::rgba8());
  const std::array<std::uint8_t, 8> source = {10, 20, 30, 255, 40, 50, 60, 255};
  std::copy(source.begin(), source.end(), pixels.row(0).begin());
  document.add_pixel_layer("Background", std::move(pixels));

  WriteOptions options;
  options.compress = false;
  const auto bytes = patchy::rttex::write_rttex(document, options);

  std::vector<std::uint8_t> expected = {'R', 'T', 'T', 'X', 'T', 'R', 0, 0};
  append_i32(expected, 1);  // height
  append_i32(expected, 2);  // width
  append_i32(expected, patchy::rttex::kFormatUnsignedByte);
  append_i32(expected, 1);  // originalHeight
  append_i32(expected, 2);  // originalWidth
  expected.push_back(0);    // bUsesAlpha
  expected.push_back(0);    // bAlreadyCompressed
  expected.push_back(0);
  expected.push_back(0);    // reservedFlags
  append_i32(expected, 1);  // mipmapCount
  for (int index = 0; index < 16; ++index) {
    append_i32(expected, 0);
  }
  CHECK(expected.size() == patchy::rttex::kTextureHeaderSize);
  append_i32(expected, 1);  // mip height
  append_i32(expected, 2);  // mip width
  append_i32(expected, 6);  // dataSize
  append_i32(expected, 0);  // mipLevel
  append_i32(expected, 0);
  append_i32(expected, 0);
  CHECK(expected.size() == patchy::rttex::kTextureHeaderSize + patchy::rttex::kMipHeaderSize);
  expected.insert(expected.end(), {10, 20, 30, 40, 50, 60});
  CHECK(bytes == expected);

  // The registry's one-argument writer uses the defaults, which include the RTPACK wrapper.
  const auto packed = patchy::rttex::write_rttex(document);
  CHECK(packed.size() >= 6 && std::memcmp(packed.data(), "RTPACK", 6) == 0);
  CHECK(unwrap(packed) == expected);
}

void rttex_pads_to_power_of_two_and_crops_on_read() {
  const auto document = gradient_document(3, 5, /*translucent*/ true);
  WriteOptions options;
  options.compress = false;
  const auto bytes = patchy::rttex::write_rttex(document, options);

  // Header: 4x8 texture, 3x5 original, alpha kept.
  CHECK(i32_at(bytes, 8) == 8);
  CHECK(i32_at(bytes, 12) == 4);
  CHECK(i32_at(bytes, 16) == patchy::rttex::kFormatUnsignedByte);
  CHECK(i32_at(bytes, 20) == 5);
  CHECK(i32_at(bytes, 24) == 3);
  CHECK(bytes[28] == 1);
  CHECK(i32_at(bytes, 100) == 8);
  CHECK(i32_at(bytes, 104) == 4);
  CHECK(i32_at(bytes, 108) == 4 * 8 * 4);
  CHECK(bytes.size() == 124 + 4 * 8 * 4);

  // Bottom-up rows: file row r holds screen row 7 - r, so the three padding rows come
  // first, then the image in the first three columns with transparent black to the right.
  for (std::int32_t file_row = 0; file_row < 8; ++file_row) {
    const std::int32_t screen_y = 7 - file_row;
    for (std::int32_t x = 0; x < 4; ++x) {
      const auto offset = 124 + (static_cast<std::size_t>(file_row) * 4 + static_cast<std::size_t>(x)) * 4;
      const std::array<int, 4> actual = {bytes[offset], bytes[offset + 1], bytes[offset + 2], bytes[offset + 3]};
      if (screen_y >= 5 || x >= 3) {
        CHECK(actual == (std::array<int, 4>{0, 0, 0, 0}));
      } else {
        CHECK(actual == pixel_at(document, x, screen_y));
      }
    }
  }

  const auto decoded = patchy::rttex::read_rttex(bytes);
  CHECK(decoded.notices.empty());
  CHECK(decoded.document.width() == 3);
  CHECK(decoded.document.height() == 5);
  CHECK(decoded.document.layers().size() == 1);
  CHECK(decoded.document.layers().front().name() == "Background");
  CHECK(channel_count(decoded.document) == 4);
  for (std::int32_t y = 0; y < 5; ++y) {
    for (std::int32_t x = 0; x < 3; ++x) {
      CHECK(pixel_at(decoded.document, x, y) == pixel_at(document, x, y));
    }
  }
  CHECK(metadata_value(decoded.document, patchy::rttex::kMetadataEncoding) == "rgba8");
  CHECK(metadata_value(decoded.document, patchy::rttex::kMetadataPowerOfTwo) == "pad");
  CHECK(metadata_value(decoded.document, patchy::rttex::kMetadataCompressed) == "0");
  CHECK(metadata_value(decoded.document, patchy::rttex::kMetadataForceAlpha).empty());
}

void rttex_alpha_detection_force_alpha_and_metadata() {
  const auto opaque = gradient_document(4, 4, /*translucent*/ false);
  WriteOptions options;
  options.compress = false;

  // Every pixel opaque: RTPack drops the channel, so the file is RGB888.
  auto bytes = patchy::rttex::write_rttex(opaque, options);
  CHECK(bytes[28] == 0);
  CHECK(i32_at(bytes, 108) == 4 * 4 * 3);
  auto decoded = patchy::rttex::read_rttex(bytes);
  CHECK(channel_count(decoded.document) == 3);
  CHECK(pixel_at(decoded.document, 0, 0) == pixel_at(opaque, 0, 0));
  CHECK(pixel_at(decoded.document, 3, 3) == pixel_at(opaque, 3, 3));
  CHECK(metadata_value(decoded.document, patchy::rttex::kMetadataForceAlpha).empty());

  // -force_alpha keeps RGBA even so, and the reader records that for the re-save prefill.
  options.force_alpha = true;
  bytes = patchy::rttex::write_rttex(opaque, options);
  CHECK(bytes[28] == 1);
  CHECK(i32_at(bytes, 108) == 4 * 4 * 4);
  decoded = patchy::rttex::read_rttex(bytes);
  CHECK(channel_count(decoded.document) == 4);
  CHECK(metadata_value(decoded.document, patchy::rttex::kMetadataForceAlpha) == "1");

  // A single translucent pixel keeps the channel without the flag.
  options.force_alpha = false;
  const auto translucent = gradient_document(4, 4, /*translucent*/ true);
  bytes = patchy::rttex::write_rttex(translucent, options);
  CHECK(bytes[28] == 1);
  decoded = patchy::rttex::read_rttex(bytes);
  CHECK(channel_count(decoded.document) == 4);
  CHECK(pixel_at(decoded.document, 1, 0)[3] == 128);
  CHECK(metadata_value(decoded.document, patchy::rttex::kMetadataForceAlpha).empty());
}

void rttex_rgba4444_and_rgb565_round_trip_within_quantization() {
  // Extremes must survive exactly (0 and 255 map to the bottom and top codes and back); the
  // rest lands within half a quantization step after the reader's bit replication.
  patchy::Document document(4, 2, patchy::PixelFormat::rgba8());
  patchy::PixelBuffer pixels(4, 2, patchy::PixelFormat::rgba8());
  const std::array<std::uint8_t, 32> source = {
      0,  0,  0,  255, 255, 255, 255, 255, 255, 0, 0,   128, 0,  255, 0,  64,
      17, 34, 51, 0,   100, 150, 200, 255, 250, 5, 127, 200, 30, 60,  90, 255,
  };
  std::copy(source.begin(), source.begin() + 16, pixels.row(0).begin());
  std::copy(source.begin() + 16, source.end(), pixels.row(1).begin());
  document.add_pixel_layer("Background", std::move(pixels));

  WriteOptions options;
  options.encoding = Encoding::Rgba4444;
  options.compress = false;
  const auto bytes = patchy::rttex::write_rttex(document, options);
  CHECK(i32_at(bytes, 16) == patchy::rttex::kFormat4444);
  CHECK(bytes[28] == 1);
  CHECK(i32_at(bytes, 108) == 4 * 2 * 2);
  const auto decoded = patchy::rttex::read_rttex(bytes);
  CHECK(channel_count(decoded.document) == 4);
  CHECK(metadata_value(decoded.document, patchy::rttex::kMetadataEncoding) == "rgba4444");
  CHECK(pixel_at(decoded.document, 0, 0) == (std::array<int, 4>{0, 0, 0, 255}));
  CHECK(pixel_at(decoded.document, 1, 0) == (std::array<int, 4>{255, 255, 255, 255}));
  for (std::int32_t y = 0; y < 2; ++y) {
    for (std::int32_t x = 0; x < 4; ++x) {
      const auto expected = pixel_at(document, x, y);
      const auto actual = pixel_at(decoded.document, x, y);
      if (expected[3] == 0) {
        // Flattening composites, so a fully transparent pixel keeps no color (every flat
        // writer shares this); only its alpha is meaningful.
        CHECK(actual[3] == 0);
        continue;
      }
      for (std::size_t channel = 0; channel < 4U; ++channel) {
        CHECK(std::abs(actual[channel] - expected[channel]) <= 9);
      }
    }
  }

  // Opaque input selects RGB565: 5 and 6 bit fields, tighter tolerances, no alpha byte.
  const auto opaque = gradient_document(5, 3, /*translucent*/ false);
  const auto bytes565 = patchy::rttex::write_rttex(opaque, options);
  CHECK(i32_at(bytes565, 16) == patchy::rttex::kFormat565);
  CHECK(bytes565[28] == 0);
  CHECK(i32_at(bytes565, 108) == 8 * 4 * 2);
  const auto decoded565 = patchy::rttex::read_rttex(bytes565);
  CHECK(channel_count(decoded565.document) == 3);
  CHECK(decoded565.document.width() == 5);
  CHECK(decoded565.document.height() == 3);
  for (std::int32_t y = 0; y < 3; ++y) {
    for (std::int32_t x = 0; x < 5; ++x) {
      const auto expected = pixel_at(opaque, x, y);
      const auto actual = pixel_at(decoded565.document, x, y);
      CHECK(std::abs(actual[0] - expected[0]) <= 5);
      CHECK(std::abs(actual[1] - expected[1]) <= 3);
      CHECK(std::abs(actual[2] - expected[2]) <= 5);
      CHECK(actual[3] == 255);
    }
  }
}

void rttex_stretch_force_square_and_exact_size_options() {
  const auto document = gradient_document(6, 3, /*translucent*/ false);
  WriteOptions options;
  options.compress = false;

  // Stretch: the image is resampled to 8x4 and the header records 8x4 as the original too.
  options.power_of_two = PowerOfTwo::Stretch;
  auto bytes = patchy::rttex::write_rttex(document, options);
  CHECK(i32_at(bytes, 8) == 4);
  CHECK(i32_at(bytes, 12) == 8);
  CHECK(i32_at(bytes, 20) == 4);
  CHECK(i32_at(bytes, 24) == 8);
  auto decoded = patchy::rttex::read_rttex(bytes);
  CHECK(decoded.document.width() == 8);
  CHECK(decoded.document.height() == 4);
  // Clamped-edge resampling keeps the distinct top-left pixel in the corner.
  CHECK(pixel_at(decoded.document, 0, 0)[0] > 200);
  CHECK(pixel_at(decoded.document, 7, 3)[0] != 0);

  // Force square on top of padding: 8x8 with the 6x3 original recorded.
  options.power_of_two = PowerOfTwo::Pad;
  options.force_square = true;
  bytes = patchy::rttex::write_rttex(document, options);
  CHECK(i32_at(bytes, 8) == 8);
  CHECK(i32_at(bytes, 12) == 8);
  CHECK(i32_at(bytes, 20) == 3);
  CHECK(i32_at(bytes, 24) == 6);
  decoded = patchy::rttex::read_rttex(bytes);
  CHECK(decoded.document.width() == 6);
  CHECK(decoded.document.height() == 3);
  CHECK(pixel_at(decoded.document, 5, 2) == pixel_at(document, 5, 2));

  // Exact size (-nopowerof2): 6x3 texture, no padding, and the reader notes it was not padded.
  options.force_square = false;
  options.power_of_two = PowerOfTwo::None;
  bytes = patchy::rttex::write_rttex(document, options);
  CHECK(i32_at(bytes, 8) == 3);
  CHECK(i32_at(bytes, 12) == 6);
  CHECK(i32_at(bytes, 108) == 6 * 3 * 3);
  decoded = patchy::rttex::read_rttex(bytes);
  CHECK(decoded.document.width() == 6);
  CHECK(decoded.document.height() == 3);
  CHECK(metadata_value(decoded.document, patchy::rttex::kMetadataPowerOfTwo) == "none");
  for (std::int32_t y = 0; y < 3; ++y) {
    for (std::int32_t x = 0; x < 6; ++x) {
      CHECK(pixel_at(decoded.document, x, y) == pixel_at(document, x, y));
    }
  }
}

void rttex_compress_wraps_in_rtpack_zlib() {
  const auto document = gradient_document(7, 9, /*translucent*/ true);
  WriteOptions options;
  options.compress = false;
  const auto raw = patchy::rttex::write_rttex(document, options);
  options.compress = true;
  const auto packed = patchy::rttex::write_rttex(document, options);

  CHECK(std::memcmp(packed.data(), "RTPACK", 6) == 0);
  CHECK(packed[6] == 0);   // version
  CHECK(packed[16] == 1);  // C_COMPRESSION_ZLIB
  CHECK(u32_at(packed, 8) == packed.size() - patchy::rttex::kRtpackHeaderSize);
  CHECK(u32_at(packed, 12) == raw.size());
  for (std::size_t offset = 17; offset < patchy::rttex::kRtpackHeaderSize; ++offset) {
    CHECK(packed[offset] == 0);
  }
  // The stream is RFC1950 zlib (the 0x78 CMF byte), which is what Proton's inflateInit expects.
  CHECK(packed[patchy::rttex::kRtpackHeaderSize] == 0x78);
  CHECK(unwrap(packed) == raw);
  CHECK(packed.size() < raw.size());

  const auto decoded = patchy::rttex::read_rttex(packed);
  CHECK(decoded.document.width() == 7);
  CHECK(decoded.document.height() == 9);
  CHECK(metadata_value(decoded.document, patchy::rttex::kMetadataCompressed) == "1");
  for (std::int32_t y = 0; y < 9; ++y) {
    for (std::int32_t x = 0; x < 7; ++x) {
      CHECK(pixel_at(decoded.document, x, y) == pixel_at(document, x, y));
    }
  }

  // A stored (type 0) wrapper is accepted even though Proton never writes one.
  std::vector<std::uint8_t> stored = {'R', 'T', 'P', 'A', 'C', 'K', 0, 0};
  append_i32(stored, static_cast<std::int32_t>(raw.size()));
  append_i32(stored, static_cast<std::int32_t>(raw.size()));
  stored.push_back(0);
  stored.resize(patchy::rttex::kRtpackHeaderSize, 0);
  stored.insert(stored.end(), raw.begin(), raw.end());
  CHECK(patchy::rttex::read_rttex(stored).document.width() == 7);
}

void rttex_jpeg_uses_installed_encoder_or_falls_back() {
  patchy::rttex::set_jpeg_encoder(nullptr);
  CHECK(!patchy::rttex::has_jpeg_encoder());
  const auto opaque = gradient_document(4, 4, /*translucent*/ false);
  WriteOptions options;
  options.encoding = Encoding::Jpeg;
  options.jpeg_quality = 75;
  options.compress = false;
  bool threw = false;
  try {
    (void)patchy::rttex::write_rttex(opaque, options);
  } catch (const std::exception& error) {
    threw = std::string(error.what()).find("JPEG") != std::string::npos;
  }
  CHECK(threw);

  // Lift the real JPEG out of the committed fixture and hand it back from a stub encoder.
  const auto fixture =
      unwrap(patchy::test::read_binary_file(patchy::test::committed_format_fixture_path("rttex", "jpeg-4x4.rttex")));
  const auto data_size = static_cast<std::size_t>(i32_at(fixture, 108));
  g_test_jpeg.assign(fixture.begin() + 124, fixture.begin() + 124 + static_cast<std::ptrdiff_t>(data_size));
  CHECK(g_test_jpeg.size() == data_size);
  CHECK(g_test_jpeg[0] == 0xFF && g_test_jpeg[1] == 0xD8);
  patchy::rttex::set_jpeg_encoder(test_jpeg_encoder);
  CHECK(patchy::rttex::has_jpeg_encoder());

  std::vector<std::string> notices;
  const auto bytes = patchy::rttex::write_rttex(opaque, options, &notices);
  CHECK(notices.empty());
  CHECK(g_test_encoder_width == 4);
  CHECK(g_test_encoder_height == 4);
  CHECK(g_test_encoder_quality == 75);
  CHECK(i32_at(bytes, 16) == patchy::rttex::kFormatEmbeddedFile);
  CHECK(bytes[28] == 0);  // bUsesAlpha
  CHECK(bytes[29] == 1);  // bAlreadyCompressed
  CHECK(i32_at(bytes, 108) == static_cast<std::int32_t>(data_size));
  CHECK(std::equal(g_test_jpeg.begin(), g_test_jpeg.end(), bytes.begin() + 124));

  // Reading decodes the embedded JPEG through stb_image, top-down (no flip).
  const auto decoded = patchy::rttex::read_rttex(bytes);
  CHECK(decoded.document.width() == 4);
  CHECK(decoded.document.height() == 4);
  CHECK(channel_count(decoded.document) == 3);
  CHECK(metadata_value(decoded.document, patchy::rttex::kMetadataEncoding) == "jpeg");
  CHECK(std::abs(pixel_at(decoded.document, 0, 0)[0] - 63) <= 3);
  CHECK(std::abs(pixel_at(decoded.document, 2, 2)[0] - 24) <= 3);

  // RTPack's rule: transparency means no JPEG. The writer falls back to lossless RGBA and
  // says so.
  const auto translucent = gradient_document(4, 4, /*translucent*/ true);
  notices.clear();
  const auto fallback = patchy::rttex::write_rttex(translucent, options, &notices);
  CHECK(notices.size() == 1);
  CHECK(notices.front().find("JPEG") != std::string::npos);
  CHECK(i32_at(fallback, 16) == patchy::rttex::kFormatUnsignedByte);
  CHECK(fallback[28] == 1);
  CHECK(fallback[29] == 0);

  patchy::rttex::set_jpeg_encoder(nullptr);
}

void rttex_reads_committed_proton_fixtures() {
  // Real RTPack output from the Proton sample apps; the expected values come from an
  // independent Python decoder (zlib + struct) run over the same files.
  struct Sample {
    std::int32_t x;
    std::int32_t y;
    std::array<int, 4> rgba;
    int tolerance;
  };
  struct Fixture {
    const char* name;
    std::int32_t width;
    std::int32_t height;
    int channels;
    const char* encoding;
    std::vector<Sample> samples;
  };
  const std::vector<Fixture> fixtures = {
      {"rgb-4x4.rttex", 4, 4, 3, "rgba8",
       {{0, 0, {60, 60, 60, 255}, 0}, {2, 2, {25, 25, 25, 255}, 0}, {3, 3, {0, 0, 0, 255}, 0}}},
      {"rgba-64x32.rttex", 64, 32, 4, "rgba8", {{32, 16, {255, 255, 255, 255}, 0}, {0, 0, {0, 0, 0, 0}, 0}}},
      {"rgba-10x10-in-16x16.rttex", 10, 10, 4, "rgba8", {{5, 5, {255, 255, 255, 255}, 0}, {0, 0, {0, 0, 0, 0}, 0}}},
      {"rgb-19x3-in-32x4.rttex", 19, 3, 3, "rgba8",
       {{0, 0, {178, 178, 178, 255}, 0}, {18, 2, {178, 178, 178, 255}, 0}, {9, 1, {178, 178, 178, 255}, 0}}},
      {"jpeg-4x4.rttex", 4, 4, 3, "jpeg",
       {{0, 0, {63, 63, 63, 255}, 3}, {2, 2, {24, 24, 24, 255}, 3}, {3, 3, {1, 1, 1, 255}, 3}}},
      {"rgba4444-61x80-in-64x128.rttex", 61, 80, 4, "rgba4444",
       {{30, 40, {34, 34, 17, 255}, 0}, {0, 79, {0, 0, 0, 68}, 0}, {0, 0, {51, 0, 17, 0}, 0}}},
      {"rgb565-122x35-in-128x64.rttex", 122, 35, 3, "rgba4444",
       {{61, 17, {123, 0, 0, 255}, 0}, {0, 0, {173, 162, 140, 255}, 0}, {121, 34, {173, 162, 148, 255}, 0}}},
  };
  for (const auto& fixture : fixtures) {
    const auto bytes = patchy::test::read_binary_file(patchy::test::committed_format_fixture_path("rttex", fixture.name));
    CHECK(!bytes.empty());
    CHECK(patchy::rttex::sniff(bytes));
    const auto result = patchy::rttex::read_rttex(bytes);
    CHECK(result.notices.empty());
    CHECK(result.document.width() == fixture.width);
    CHECK(result.document.height() == fixture.height);
    CHECK(result.document.layers().size() == 1);
    CHECK(channel_count(result.document) == fixture.channels);
    CHECK(metadata_value(result.document, patchy::rttex::kMetadataEncoding) == fixture.encoding);
    CHECK(metadata_value(result.document, patchy::rttex::kMetadataPowerOfTwo) == "pad");
    CHECK(metadata_value(result.document, patchy::rttex::kMetadataCompressed) == "1");
    for (const auto& sample : fixture.samples) {
      const auto actual = pixel_at(result.document, sample.x, sample.y);
      for (std::size_t channel = 0; channel < 4U; ++channel) {
        CHECK(std::abs(actual[channel] - sample.rgba[channel]) <= sample.tolerance);
      }
    }
  }

  // The 16-bit fixture re-saves with the prefilled encoding without drifting: the reader's
  // bit replication and the writer's rounded quantization are exact inverses.
  const auto fixture4444 = patchy::rttex::read_rttex(patchy::test::read_binary_file(
      patchy::test::committed_format_fixture_path("rttex", "rgba4444-61x80-in-64x128.rttex")));
  WriteOptions options;
  options.encoding = Encoding::Rgba4444;
  const auto resaved = patchy::rttex::read_rttex(patchy::rttex::write_rttex(fixture4444.document, options));
  CHECK(resaved.document.width() == 61);
  CHECK(resaved.document.height() == 80);
  for (std::int32_t y = 0; y < 80; y += 7) {
    for (std::int32_t x = 0; x < 61; x += 5) {
      const auto expected = pixel_at(fixture4444.document, x, y);
      const auto actual = pixel_at(resaved.document, x, y);
      if (expected[3] == 0) {
        CHECK(actual[3] == 0);  // colors under zero alpha do not survive a flatten
      } else {
        CHECK(actual == expected);
      }
    }
  }
}

void rttex_rejects_pvrtc_rtfont_truncated_and_absurd_headers() {
  WriteOptions options;
  options.compress = false;
  const auto valid = patchy::rttex::write_rttex(gradient_document(2, 2, /*translucent*/ false), options);
  CHECK(patchy::rttex::read_rttex(valid).document.width() == 2);

  auto pvrtc = valid;
  put_i32(pvrtc, 16, 35842);
  CHECK(read_throws(pvrtc, "PVRTC"));

  auto unknown = valid;
  put_i32(unknown, 16, 4242);
  CHECK(read_throws(unknown, "4242"));

  std::vector<std::uint8_t> font = {'R', 'T', 'F', 'O', 'N', 'T', 0, 0};
  font.resize(200, 0);
  CHECK(read_throws(wrap_in_rtpack(font), "not a Proton texture"));
  CHECK(read_throws(font));

  const std::vector<std::uint8_t> truncated(valid.begin(), valid.begin() + 60);
  CHECK(read_throws(truncated, "truncated"));

  auto packed = wrap_in_rtpack(valid);
  put_i32(packed, 12, -1);  // decompressedSize 0xFFFFFFFF
  CHECK(read_throws(packed));
  packed = wrap_in_rtpack(valid);
  put_i32(packed, 8, 1 << 20);  // compressedSize past the end
  CHECK(read_throws(packed, "truncated"));
  packed = wrap_in_rtpack(valid);
  packed[16] = 7;  // unknown compression type
  CHECK(read_throws(packed, "compression"));
  packed = wrap_in_rtpack(valid);
  packed[packed.size() / 2] ^= 0xFF;  // damage the zlib stream
  CHECK(read_throws(packed));

  auto huge = valid;
  put_i32(huge, 12, 1 << 30);
  CHECK(read_throws(huge, "size"));

  auto no_mips = valid;
  put_i32(no_mips, 32, 0);
  CHECK(read_throws(no_mips, "mip"));

  auto overrun = valid;
  put_i32(overrun, 108, 100000);
  CHECK(read_throws(overrun, "truncated"));

  auto mismatched_mip = valid;
  put_i32(mismatched_mip, 104, 9);
  CHECK(read_throws(mismatched_mip, "mip"));

  bool threw = false;
  try {
    (void)patchy::rttex::write_rttex(patchy::Document{}, options);
  } catch (const std::exception&) {
    threw = true;
  }
  CHECK(threw);
}

void rttex_unicode_path_round_trips() {
  // Every new file-writing entry point gets a Unicode-path test (AGENTS.md).
  const auto dir = patchy::test::unicode_artifact_dir(u8"rttex");
  const auto path = dir / patchy::test::unicode_path_piece(patchy::test::kUnicodeCombinedStem).concat(".rttex");
  const auto document = gradient_document(5, 4, /*translucent*/ true);
  patchy::rttex::write_rttex_file(document, path);
  CHECK(std::filesystem::exists(path));
  CHECK(patchy::test::directory_holds_only(dir, {path}));

  const auto decoded = patchy::rttex::read_rttex_file(path);
  CHECK(decoded.document.width() == 5);
  CHECK(decoded.document.height() == 4);
  CHECK(pixel_at(decoded.document, 3, 2) == pixel_at(document, 3, 2));
  // The file form names the layer after the stem, the flat-format convention.
  CHECK(decoded.document.layers().front().name() == patchy::test::utf8_string(patchy::test::kUnicodeCombinedStem));
}

void rttex_writes_inspection_artifacts() {
  // One texture per encoding under test-artifacts/rttex/, for the engine-side cross-check
  // (Proton's RTPack.exe re-reads them through its own loader; see docs/rttex.md). Each file
  // must also read back at the true size through Patchy's own reader.
  const auto dir = std::filesystem::path("test-artifacts") / "rttex";
  std::filesystem::create_directories(dir);
  const auto translucent = gradient_document(30, 20, /*translucent*/ true);
  const auto opaque = gradient_document(30, 20, /*translucent*/ false);
  struct Variant {
    const char* name;
    const patchy::Document* document;
    WriteOptions options;
  };
  WriteOptions rgba8;
  WriteOptions rgba8_uncompressed;
  rgba8_uncompressed.compress = false;
  WriteOptions sixteen_bit;
  sixteen_bit.encoding = Encoding::Rgba4444;
  WriteOptions stretched;
  stretched.power_of_two = PowerOfTwo::Stretch;
  WriteOptions exact;
  exact.power_of_two = PowerOfTwo::None;
  WriteOptions square;
  square.force_square = true;
  const std::vector<Variant> variants = {
      {"rgba8888-padded.rttex", &translucent, rgba8},
      {"rgb888-padded.rttex", &opaque, rgba8},
      {"rgba8888-uncompressed.rttex", &translucent, rgba8_uncompressed},
      {"rgba4444-padded.rttex", &translucent, sixteen_bit},
      {"rgb565-padded.rttex", &opaque, sixteen_bit},
      {"rgb888-stretched.rttex", &opaque, stretched},
      {"rgb888-exact-size.rttex", &opaque, exact},
      {"rgba8888-square.rttex", &translucent, square},
  };
  for (const auto& variant : variants) {
    const auto path = dir / variant.name;
    patchy::rttex::write_rttex_file(*variant.document, path, variant.options);
    const auto decoded = patchy::rttex::read_rttex_file(path);
    const bool stretched_variant = variant.options.power_of_two == PowerOfTwo::Stretch;
    CHECK(decoded.document.width() == (stretched_variant ? 32 : 30));
    CHECK(decoded.document.height() == (stretched_variant ? 32 : 20));
  }
}

void rttex_local_fixture_sweep_if_available() {
  const auto dir = patchy::test::local_format_fixture_path("rttex", "").parent_path();
  if (!std::filesystem::exists(dir)) {
    std::cout << "[SKIP] rttex_local_fixture_sweep_if_available: no local-test-fixtures/rttex\n";
    return;
  }
  int decoded_count = 0;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
    if (!entry.is_regular_file() || !patchy::rttex::is_rttex_extension(entry.path().extension().string())) {
      continue;
    }
    const auto bytes = patchy::test::read_binary_file(entry.path());
    try {
      const auto result = patchy::rttex::read_rttex(bytes);
      CHECK(result.document.width() > 0);
      CHECK(result.document.height() > 0);
      CHECK(result.document.layers().size() == 1);
      std::cout << "  " << entry.path().filename().string() << ": " << result.document.width() << 'x'
                << result.document.height() << ' ' << metadata_value(result.document, patchy::rttex::kMetadataEncoding)
                << " channels " << channel_count(result.document);
      for (const auto& notice : result.notices) {
        std::cout << " | " << notice;
      }
      std::cout << '\n';
    } catch (const std::exception& error) {
      // PVRTC is the one documented refusal; anything else is a real failure.
      std::cout << "  " << entry.path().filename().string() << ": " << error.what() << '\n';
      CHECK(std::string(error.what()).find("PVRTC") != std::string::npos);
    }
    ++decoded_count;
  }
  if (decoded_count == 0) {
    std::cout << "[SKIP] rttex_local_fixture_sweep_if_available: no .rttex files in local-test-fixtures/rttex\n";
  }
}

}  // namespace

std::vector<patchy::test::TestCase> rttex_tests() {
  return {
      {"rttex_extensions_sniff_and_registry_routing", rttex_extensions_sniff_and_registry_routing},
      {"rttex_uncompressed_layout_matches_proton_spec", rttex_uncompressed_layout_matches_proton_spec},
      {"rttex_pads_to_power_of_two_and_crops_on_read", rttex_pads_to_power_of_two_and_crops_on_read},
      {"rttex_alpha_detection_force_alpha_and_metadata", rttex_alpha_detection_force_alpha_and_metadata},
      {"rttex_rgba4444_and_rgb565_round_trip_within_quantization",
       rttex_rgba4444_and_rgb565_round_trip_within_quantization},
      {"rttex_stretch_force_square_and_exact_size_options", rttex_stretch_force_square_and_exact_size_options},
      {"rttex_compress_wraps_in_rtpack_zlib", rttex_compress_wraps_in_rtpack_zlib},
      {"rttex_jpeg_uses_installed_encoder_or_falls_back", rttex_jpeg_uses_installed_encoder_or_falls_back},
      {"rttex_reads_committed_proton_fixtures", rttex_reads_committed_proton_fixtures},
      {"rttex_rejects_pvrtc_rtfont_truncated_and_absurd_headers",
       rttex_rejects_pvrtc_rtfont_truncated_and_absurd_headers},
      {"rttex_unicode_path_round_trips", rttex_unicode_path_round_trips},
      {"rttex_writes_inspection_artifacts", rttex_writes_inspection_artifacts},
      {"rttex_local_fixture_sweep_if_available", rttex_local_fixture_sweep_if_available},
  };
}
