#include "formats/heif_document_io.hpp"
#include "formats/animation_timing.hpp"
#include "formats/webp_animation_io.hpp"
#include "core/document.hpp"
#include "core/layer_metadata.hpp"
#include "formats/acv_curves_io.hpp"
#include "formats/bmp_document_io.hpp"
#include "formats/aseprite_document_io.hpp"
#include "formats/avif_document_io.hpp"
#include "formats/document_flatten.hpp"
#include "formats/format_registry.hpp"
#include "formats/gif_document_io.hpp"
#include "formats/ico_document_io.hpp"
#include "formats/ilbm_document_io.hpp"
#include "formats/image_open_diagnosis.hpp"
#include "formats/exr_document_io.hpp"
#include "formats/hdr_document_io.hpp"
#include "formats/jp2_document_io.hpp"
#include "core/pixel_depth.hpp"
#include "formats/pcx_document_io.hpp"
#include "formats/tga_document_io.hpp"
#include "psd/psd_binary.hpp"
#include "psd/psd_document_io.hpp"
#include "core/palette_presets.hpp"
#include "test_harness.hpp"
#include "local_psd_fixtures.hpp"
#include "synthetic_dng.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <exception>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <numeric>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>
#include "core_test_support.hpp"
#include "test_groups.hpp"

namespace {

using patchy::test::solid_rgb;
using patchy::test::solid_rgba;

patchy::PixelBuffer solid_rgba_square(std::int32_t size, std::uint8_t r, std::uint8_t g, std::uint8_t b,
                                      std::uint8_t a) {
  patchy::PixelBuffer pixels(size, size, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < size; ++y) {
    for (std::int32_t x = 0; x < size; ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = r;
      px[1] = g;
      px[2] = b;
      px[3] = a;
    }
  }
  return pixels;
}

void ico_reads_multi_size_and_round_trips_named_layers() {
  patchy::Document document(32, 32, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("32x32", solid_rgba_square(32, 20, 40, 200, 255));
  {
    patchy::Layer small_layer(document.allocate_layer_id(), "16x16", solid_rgba_square(16, 200, 30, 10, 255));
    small_layer.set_visible(false);
    document.add_layer(std::move(small_layer));
  }

  const auto bytes = patchy::ico::DocumentIo::write(document, patchy::ico::WriteOptions{{16, 32}, true, false, 0, 0});
  CHECK(patchy::ico::DocumentIo::can_read(bytes));

  std::vector<std::string> notices;
  const auto read_back = patchy::ico::DocumentIo::read(bytes, &notices);
  CHECK(read_back.width() == 32);
  CHECK(read_back.height() == 32);
  CHECK(read_back.layers().size() == 2);
  CHECK(read_back.layers()[0].name() == "16x16");
  CHECK(!read_back.layers()[0].visible());
  CHECK(read_back.layers()[1].name() == "32x32");
  CHECK(read_back.layers()[1].visible());
  // Exact-size layers are written verbatim, so both sizes survive byte-for-byte.
  const auto* small_px = read_back.layers()[0].pixels().pixel(3, 3);
  CHECK(small_px[0] == 200);
  CHECK(small_px[1] == 30);
  CHECK(small_px[2] == 10);
  CHECK(small_px[3] == 255);
  const auto* large_px = read_back.layers()[1].pixels().pixel(20, 20);
  CHECK(large_px[0] == 20);
  CHECK(large_px[1] == 40);
  CHECK(large_px[2] == 200);
  CHECK(!notices.empty());
}

void ico_transparent_pixels_round_trip_and_mask_fallback_applies() {
  patchy::Document document(8, 8, patchy::PixelFormat::rgba8());
  auto pixels = solid_rgba_square(8, 90, 120, 30, 255);
  pixels.pixel(0, 0)[3] = 0;
  pixels.pixel(7, 7)[3] = 0;
  document.add_pixel_layer("8x8", std::move(pixels));

  const auto bytes = patchy::ico::DocumentIo::write(document, patchy::ico::WriteOptions{{8}, true, false, 0, 0});
  const auto read_back = patchy::ico::DocumentIo::read(bytes);
  CHECK(read_back.layers().size() == 1);
  CHECK(read_back.layers()[0].pixels().pixel(0, 0)[3] == 0);
  CHECK(read_back.layers()[0].pixels().pixel(7, 7)[3] == 0);
  CHECK(read_back.layers()[0].pixels().pixel(4, 4)[3] == 255);

  // A 32-bit entry whose alpha channel is uniformly zero must fall back to the AND mask
  // (the classic real-world authoring bug) instead of importing fully transparent.
  auto broken = bytes;
  // Find the XOR pixel data: header 6 + entry 16 + BITMAPINFOHEADER 40, rows of 8 RGBA px.
  const std::size_t xor_offset = 6 + 16 + 40;
  for (std::size_t px = 0; px < 64; ++px) {
    broken[xor_offset + px * 4 + 3] = 0;
  }
  const auto masked = patchy::ico::DocumentIo::read(broken);
  CHECK(masked.layers()[0].pixels().pixel(4, 4)[3] == 255);
  CHECK(masked.layers()[0].pixels().pixel(0, 0)[3] == 0);
}

void cur_round_trips_hotspot() {
  patchy::Document document(32, 32, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("32x32", solid_rgba_square(32, 255, 255, 255, 255));

  const auto bytes = patchy::ico::DocumentIo::write(document, patchy::ico::WriteOptions{{16, 32}, true, true, 10, 12});
  CHECK(bytes[2] == 2);  // ICONDIR type = cursor

  const auto read_back = patchy::ico::DocumentIo::read(bytes);
  CHECK(read_back.layers().size() == 2);
  const auto& largest = read_back.layers().back();
  const auto found = largest.metadata().find(patchy::ico::kLayerMetadataCursorHotspot);
  CHECK(found != largest.metadata().end());
  CHECK(found->second == "10,12");
  // The 16px entry scales the hotspot proportionally.
  const auto& small_layer = read_back.layers().front();
  const auto small_found = small_layer.metadata().find(patchy::ico::kLayerMetadataCursorHotspot);
  CHECK(small_found != small_layer.metadata().end());
  CHECK(small_found->second == "5,6");
}

void ico_writer_png_entry_for_256() {
  // A fake PNG codec exercises the container plumbing without Qt: the payload is the PNG
  // signature + LE dimensions + raw RGBA.
  patchy::ico::set_png_codec(
      [](std::span<const std::uint8_t> bytes) {
        patchy::ico::RgbaImage image;
        if (bytes.size() < 16) {
          return image;
        }
        image.width = static_cast<std::int32_t>(bytes[8] | (bytes[9] << 8U) | (bytes[10] << 16U) | (bytes[11] << 24U));
        image.height =
            static_cast<std::int32_t>(bytes[12] | (bytes[13] << 8U) | (bytes[14] << 16U) | (bytes[15] << 24U));
        image.rgba.assign(bytes.begin() + 16, bytes.end());
        return image;
      },
      [](const patchy::ico::RgbaImage& image) {
        std::vector<std::uint8_t> bytes = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
        for (const auto value : {image.width, image.height}) {
          bytes.push_back(static_cast<std::uint8_t>(value & 0xff));
          bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
          bytes.push_back(static_cast<std::uint8_t>((value >> 16) & 0xff));
          bytes.push_back(static_cast<std::uint8_t>((value >> 24) & 0xff));
        }
        bytes.insert(bytes.end(), image.rgba.begin(), image.rgba.end());
        return bytes;
      });

  patchy::Document document(256, 256, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("256x256", solid_rgba_square(256, 10, 200, 100, 255));
  const auto bytes = patchy::ico::DocumentIo::write(document, patchy::ico::WriteOptions{{32, 256}, true, false, 0, 0});

  // Directory: the 256 entry stores width/height bytes as 0 and a PNG payload.
  const std::size_t second_entry = 6 + 16;
  CHECK(bytes[second_entry] == 0);
  CHECK(bytes[second_entry + 1] == 0);
  const auto payload_offset = static_cast<std::size_t>(bytes[second_entry + 12] | (bytes[second_entry + 13] << 8U) |
                                                       (bytes[second_entry + 14] << 16U) |
                                                       (bytes[second_entry + 15] << 24U));
  CHECK(bytes[payload_offset] == 0x89);
  CHECK(bytes[payload_offset + 1] == 0x50);

  const auto read_back = patchy::ico::DocumentIo::read(bytes);
  CHECK(read_back.width() == 256);
  CHECK(read_back.layers().back().name() == "256x256");
  const auto* px = read_back.layers().back().pixels().pixel(128, 128);
  CHECK(px[0] == 10);
  CHECK(px[1] == 200);
  CHECK(px[2] == 100);

  patchy::ico::set_png_codec(nullptr, nullptr);
}

void ico_reads_real_world_samples() {
  // Committed fixtures: pillow-* are Pillow-authored; cpython-py.ico (PSF-2.0) and
  // vscode-code.ico (MIT) are real-world multi-size icons (see NOTICE-THIRD-PARTY.md).
  // The core suite has no PNG decoder, so PNG-compressed entries are skipped with a notice
  // here; the UI suite re-reads these fixtures with the Qt codec installed.
  patchy::ico::set_png_codec(nullptr, nullptr);
  // pillow-multisize-png.ico is ALL PNG entries: without a decoder it must fail with a clear
  // message rather than open empty (the UI suite reads it fully with the Qt codec).
  bool all_png_rejected = false;
  try {
    (void)patchy::ico::DocumentIo::read_file(
        patchy::test::committed_format_fixture_path("ico", "pillow-multisize-png.ico"));
  } catch (const std::exception& error) {
    all_png_rejected = std::string(error.what()).find("no readable images") != std::string::npos;
  }
  CHECK(all_png_rejected);

  const std::array<const char*, 5> names = {"pillow-bmp32.ico", "pillow-indexed.ico", "pillow-cursor.cur",
                                            "cpython-py.ico", "vscode-code.ico"};
  for (const auto* name : names) {
    const auto path = patchy::test::committed_format_fixture_path("ico", name);
    CHECK(std::filesystem::exists(path));
    std::vector<std::string> notices;
    const auto document = patchy::ico::DocumentIo::read_file(path, &notices);
    CHECK(!document.layers().empty());
    CHECK(document.width() > 0);
    CHECK(document.height() > 0);
    for (const auto& layer : document.layers()) {
      CHECK(layer.pixels().width() > 0);
      CHECK(layer.pixels().height() > 0);
    }
  }

  const auto cursor =
      patchy::ico::DocumentIo::read_file(patchy::test::committed_format_fixture_path("ico", "pillow-cursor.cur"));
  const auto hotspot = cursor.layers().back().metadata().find(patchy::ico::kLayerMetadataCursorHotspot);
  CHECK(hotspot != cursor.layers().back().metadata().end());
  CHECK(hotspot->second == "7,9");

  const auto indexed =
      patchy::ico::DocumentIo::read_file(patchy::test::committed_format_fixture_path("ico", "pillow-indexed.ico"));
  CHECK(indexed.width() == 32);
  // The Pillow art is a green disc with a yellow inset square; (24, 24) is disc-only.
  const auto* disc = indexed.layers().back().pixels().pixel(24, 24);
  CHECK(disc[3] == 255);
  CHECK(disc[1] > disc[0]);
}

void tga_reads_types_1_2_9_10_and_origin_flags() {
  const auto header = [](std::uint8_t color_map_type, std::uint8_t image_type, std::uint16_t map_length,
                         std::uint8_t map_bits, std::uint16_t width, std::uint16_t height, std::uint8_t depth,
                         std::uint8_t descriptor) {
    std::vector<std::uint8_t> bytes = {0, color_map_type, image_type, 0, 0,
                                       static_cast<std::uint8_t>(map_length & 0xff),
                                       static_cast<std::uint8_t>(map_length >> 8U), map_bits, 0, 0, 0, 0,
                                       static_cast<std::uint8_t>(width & 0xff),
                                       static_cast<std::uint8_t>(width >> 8U),
                                       static_cast<std::uint8_t>(height & 0xff),
                                       static_cast<std::uint8_t>(height >> 8U), depth, descriptor};
    return bytes;
  };

  // Type 2 raw, default bottom-up origin: the FIRST file row is the BOTTOM image row.
  {
    auto bytes = header(0, 2, 0, 0, 2, 2, 24, 0);
    // bottom row: blue, green; top row: red, white (BGR order in the file)
    const std::uint8_t px[] = {255, 0, 0, /**/ 0, 255, 0, /**/ 0, 0, 255, /**/ 255, 255, 255};
    bytes.insert(bytes.end(), std::begin(px), std::end(px));
    const auto document = patchy::tga::DocumentIo::read(bytes);
    CHECK(document.layers().front().pixels().pixel(0, 0)[0] == 255);  // top-left red
    CHECK(document.layers().front().pixels().pixel(1, 1)[1] == 255);  // bottom-right green
    CHECK(document.layers().front().pixels().pixel(0, 1)[2] == 255);  // bottom-left blue
  }
  // Type 2 top-down + right-to-left: both axes mirror.
  {
    auto bytes = header(0, 2, 0, 0, 2, 1, 24, 0x30);
    const std::uint8_t px[] = {255, 0, 0, /**/ 0, 255, 0};  // file order: blue, green
    bytes.insert(bytes.end(), std::begin(px), std::end(px));
    const auto document = patchy::tga::DocumentIo::read(bytes);
    CHECK(document.layers().front().pixels().pixel(1, 0)[2] == 255);  // blue lands right
    CHECK(document.layers().front().pixels().pixel(0, 0)[1] == 255);  // green lands left
  }
  // Type 10 RLE: a 3-pixel run + 1 literal, top-down.
  {
    auto bytes = header(0, 10, 0, 0, 4, 1, 32, 0x28);
    const std::uint8_t packets[] = {0x82, 10, 20, 30, 255, /**/ 0x00, 40, 50, 60, 128};
    bytes.insert(bytes.end(), std::begin(packets), std::end(packets));
    const auto document = patchy::tga::DocumentIo::read(bytes);
    const auto& pixels = document.layers().front().pixels();
    CHECK(pixels.pixel(0, 0)[2] == 10);
    CHECK(pixels.pixel(2, 0)[2] == 10);
    CHECK(pixels.pixel(3, 0)[2] == 40);
    CHECK(pixels.pixel(3, 0)[3] == 128);
  }
  // Type 1 indexed with a 2-entry color map; the palette must reach indexed_palette().
  {
    auto bytes = header(1, 1, 2, 24, 2, 1, 8, 0x20);
    const std::uint8_t map[] = {0, 0, 200, /**/ 0, 180, 0};  // BGR entries: red-ish, green-ish
    bytes.insert(bytes.end(), std::begin(map), std::end(map));
    bytes.push_back(0);
    bytes.push_back(1);
    const auto document = patchy::tga::DocumentIo::read(bytes);
    CHECK(document.layers().front().pixels().pixel(0, 0)[0] == 200);
    CHECK(document.layers().front().pixels().pixel(1, 0)[1] == 180);
    CHECK(document.indexed_palette().has_value());
    CHECK(document.indexed_palette()->colors.size() == 2);
  }
  // Type 9 RLE indexed.
  {
    auto bytes = header(1, 9, 2, 24, 3, 1, 8, 0x20);
    const std::uint8_t map[] = {30, 30, 30, /**/ 250, 250, 250};
    bytes.insert(bytes.end(), std::begin(map), std::end(map));
    const std::uint8_t packets[] = {0x81, 1, /**/ 0x00, 0};  // run of two index-1, literal index-0
    bytes.insert(bytes.end(), std::begin(packets), std::end(packets));
    const auto document = patchy::tga::DocumentIo::read(bytes);
    CHECK(document.layers().front().pixels().pixel(0, 0)[0] == 250);
    CHECK(document.layers().front().pixels().pixel(1, 0)[0] == 250);
    CHECK(document.layers().front().pixels().pixel(2, 0)[0] == 30);
  }
  // 16-bit files are rejected with a clear message.
  {
    auto bytes = header(0, 2, 0, 0, 1, 1, 16, 0);
    bytes.push_back(0);
    bytes.push_back(0);
    bool rejected = false;
    try {
      (void)patchy::tga::DocumentIo::read(bytes);
    } catch (const std::exception& error) {
      rejected = std::string(error.what()).find("16-bit") != std::string::npos;
    }
    CHECK(rejected);
  }
}

void tga_writer_rle32_round_trips() {
  patchy::Document document(33, 7, patchy::PixelFormat::rgba8());
  patchy::PixelBuffer pixels(33, 7, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < 7; ++y) {
    for (std::int32_t x = 0; x < 33; ++x) {
      auto* px = pixels.pixel(x, y);
      // Mix of long runs and per-pixel noise to exercise both packet kinds.
      if (y < 3) {
        px[0] = 200;
        px[1] = 100;
        px[2] = 50;
        px[3] = 255;
      } else {
        px[0] = static_cast<std::uint8_t>(x * 7);
        px[1] = static_cast<std::uint8_t>(y * 31);
        px[2] = static_cast<std::uint8_t>(x + y);
        px[3] = x % 5 == 0 ? 0 : 255;
      }
    }
  }
  document.add_pixel_layer("Art", pixels);

  const auto bytes = patchy::tga::DocumentIo::write(document);
  CHECK(patchy::tga::DocumentIo::can_read(bytes));
  const auto read_back = patchy::tga::DocumentIo::read(bytes);
  CHECK(read_back.width() == 33);
  CHECK(read_back.height() == 7);
  const auto& round = read_back.layers().front().pixels();
  for (std::int32_t y = 0; y < 7; ++y) {
    for (std::int32_t x = 0; x < 33; ++x) {
      const auto* expected = pixels.pixel(x, y);
      const auto* actual = round.pixel(x, y);
      CHECK(actual[3] == expected[3]);
      if (expected[3] == 0) {
        // The flatten composites fully transparent pixels away; their RGB is not preserved.
        continue;
      }
      CHECK(actual[0] == expected[0]);
      CHECK(actual[1] == expected[1]);
      CHECK(actual[2] == expected[2]);
    }
  }
}

void tga_palette_mode_writes_indexed() {
  patchy::Document document(8, 4, patchy::PixelFormat::rgb8());
  const auto* preset = patchy::find_builtin_palette_preset("gameboy");
  CHECK(preset != nullptr);
  patchy::DocumentPaletteEditing editing;
  editing.palette.colors.assign(preset->colors.begin(), preset->colors.end());
  document.palette_editing() = editing;

  patchy::PixelBuffer pixels(8, 4, patchy::PixelFormat::rgb8());
  for (std::int32_t y = 0; y < 4; ++y) {
    for (std::int32_t x = 0; x < 8; ++x) {
      const auto& color = preset->colors[static_cast<std::size_t>(x % 4)];
      auto* px = pixels.pixel(x, y);
      px[0] = color.red;
      px[1] = color.green;
      px[2] = color.blue;
    }
  }
  document.add_pixel_layer("Pixels", std::move(pixels));

  const auto bytes = patchy::tga::DocumentIo::write(document);
  CHECK(bytes[2] == 1);  // image type 1 = raw indexed
  const auto read_back = patchy::tga::DocumentIo::read(bytes);
  CHECK(read_back.indexed_palette().has_value());
  CHECK(read_back.indexed_palette()->colors.size() == preset->colors.size());
  for (std::int32_t x = 0; x < 8; ++x) {
    const auto& expected = preset->colors[static_cast<std::size_t>(x % 4)];
    const auto* px = read_back.layers().front().pixels().pixel(x, 2);
    CHECK(px[0] == expected.red);
    CHECK(px[1] == expected.green);
    CHECK(px[2] == expected.blue);
  }
}

void tga_reads_real_world_samples() {
  const std::array<const char*, 5> names = {"pillow-rgb24-raw.tga", "pillow-rgb24-rle.tga", "pillow-rgba32-rle.tga",
                                            "pillow-indexed.tga", "pillow-gray.tga"};
  for (const auto* name : names) {
    const auto path = patchy::test::committed_format_fixture_path("tga", name);
    CHECK(std::filesystem::exists(path));
    const auto document = patchy::tga::DocumentIo::read_file(path);
    CHECK(document.width() == 48);
    CHECK(document.height() == 32);
    const auto& pixels = document.layers().front().pixels();
    // Red quadrant top-left, background top-right (identical Pillow art in every mode).
    const auto* red = pixels.pixel(5, 5);
    const auto* background = pixels.pixel(40, 5);
    const bool gray = std::string(name).find("gray") != std::string::npos;
    if (gray) {
      CHECK(pixels.pixel(5, 5)[0] == pixels.pixel(5, 5)[1]);
      CHECK(red[0] > background[0]);
    } else {
      CHECK(red[0] > 150);
      CHECK(red[1] < 100);
      CHECK(background[2] > background[0]);
    }
  }
  const auto indexed =
      patchy::tga::DocumentIo::read_file(patchy::test::committed_format_fixture_path("tga", "pillow-indexed.tga"));
  CHECK(indexed.indexed_palette().has_value());
  const auto rgba =
      patchy::tga::DocumentIo::read_file(patchy::test::committed_format_fixture_path("tga", "pillow-rgba32-rle.tga"));
  CHECK(rgba.layers().front().pixels().pixel(40, 28)[3] == 128);  // ellipse half-alpha survives
}

// Minimal spec-compliant GIF parser + LZW decoder used ONLY to validate Patchy's encoder
// with no external dependencies (Qt's qgif and Pillow provide the real-world checks).
// Parses a full frame sequence (GCE delay/disposal/transparency, local color tables,
// NETSCAPE loop extension); the legacy top-level fields mirror frame 0 plus the global
// table so single-frame tests read them directly.
struct DecodedGifFrame {
  std::vector<patchy::RgbColor> palette;  // the local table when present, else the global
  bool has_local_table{false};
  int transparent_index{-1};
  int delay_cs{0};
  int disposal{0};
  std::vector<std::uint8_t> indexes;
};

struct DecodedGif {
  std::int32_t width{0};
  std::int32_t height{0};
  std::vector<patchy::RgbColor> palette;  // the global color table (padded entries included)
  int transparent_index{-1};
  std::vector<std::uint8_t> indexes;
  std::vector<DecodedGifFrame> frames;
  bool loop_forever{false};
};

std::vector<std::uint8_t> reference_decode_gif_lzw(std::span<const std::uint8_t> data, int min_code_size) {
  std::vector<std::uint8_t> indexes;
  const auto clear_code = 1U << min_code_size;
  const auto end_code = clear_code + 1U;
  std::array<std::uint16_t, 4096> prefix{};
  std::array<std::uint8_t, 4096> suffix{};
  std::uint32_t next = clear_code + 2;
  int width_bits = min_code_size + 1;
  std::uint32_t bit_pos = 0;
  const auto read_code = [&]() -> std::uint32_t {
    std::uint32_t code = 0;
    for (int bit = 0; bit < width_bits; ++bit) {
      const auto byte_index = (bit_pos + static_cast<std::uint32_t>(bit)) / 8U;
      const auto bit_index = (bit_pos + static_cast<std::uint32_t>(bit)) % 8U;
      CHECK(byte_index < data.size());
      code |= static_cast<std::uint32_t>((data[byte_index] >> bit_index) & 1U) << bit;
    }
    bit_pos += static_cast<std::uint32_t>(width_bits);
    return code;
  };
  const auto expand = [&](std::uint32_t code, std::vector<std::uint8_t>& out) {
    std::vector<std::uint8_t> reversed;
    while (code >= clear_code + 2) {
      reversed.push_back(suffix[code]);
      code = prefix[code];
    }
    reversed.push_back(static_cast<std::uint8_t>(code));
    out.insert(out.end(), reversed.rbegin(), reversed.rend());
  };
  const auto first_symbol = [&](std::uint32_t code) {
    while (code >= clear_code + 2) {
      code = prefix[code];
    }
    return static_cast<std::uint8_t>(code);
  };
  std::int64_t prev = -1;
  while (true) {
    const auto code = read_code();
    if (code == clear_code) {
      next = clear_code + 2;
      width_bits = min_code_size + 1;
      prev = -1;
      continue;
    }
    if (code == end_code) {
      break;
    }
    if (prev < 0) {
      CHECK(code < clear_code);
      indexes.push_back(static_cast<std::uint8_t>(code));
      prev = static_cast<std::int64_t>(code);
      continue;
    }
    if (code < next) {
      expand(code, indexes);
      prefix[next] = static_cast<std::uint16_t>(prev);
      suffix[next] = first_symbol(code);
    } else {
      CHECK(code == next);
      prefix[next] = static_cast<std::uint16_t>(prev);
      suffix[next] = first_symbol(static_cast<std::uint32_t>(prev));
      expand(code, indexes);
    }
    ++next;
    if (next == (1U << width_bits) && width_bits < 12) {
      ++width_bits;
    }
    prev = static_cast<std::int64_t>(code);
  }
  return indexes;
}

DecodedGif reference_decode_gif(std::span<const std::uint8_t> bytes) {
  DecodedGif result;
  std::size_t pos = 0;
  const auto u8 = [&] { return bytes[pos++]; };
  const auto u16 = [&] {
    const auto value = static_cast<std::uint16_t>(bytes[pos] | (bytes[pos + 1] << 8U));
    pos += 2;
    return value;
  };
  const auto read_color_table = [&](std::size_t entries) {
    std::vector<patchy::RgbColor> table;
    for (std::size_t i = 0; i < entries; ++i) {
      const auto r = u8();
      const auto g = u8();
      const auto b = u8();
      table.push_back(patchy::RgbColor{r, g, b});
    }
    return table;
  };
  CHECK(bytes.size() > 13);
  CHECK(std::equal(bytes.begin(), bytes.begin() + 6, reinterpret_cast<const std::uint8_t*>("GIF89a")));
  pos = 6;
  result.width = u16();
  result.height = u16();
  const auto packed = u8();
  u8();  // background index
  u8();  // aspect
  CHECK((packed & 0x80U) != 0);
  result.palette = read_color_table(std::size_t{1} << ((packed & 0x07U) + 1U));

  // GCE state applies to the next image descriptor only.
  int pending_transparent = -1;
  int pending_delay_cs = 0;
  int pending_disposal = 0;
  while (pos < bytes.size()) {
    const auto block = u8();
    if (block == 0x3b) {
      break;
    }
    if (block == 0x21) {
      const auto label = u8();
      if (label == 0xf9) {
        const auto size = u8();
        CHECK(size == 4);
        const auto gce_packed = u8();
        pending_disposal = static_cast<int>((gce_packed >> 2U) & 0x07U);
        pending_delay_cs = u16();
        const auto transparent = u8();
        if ((gce_packed & 0x01U) != 0) {
          pending_transparent = transparent;
        }
        CHECK(u8() == 0);
      } else if (label == 0xff) {
        const auto app_size = u8();
        CHECK(app_size == 11);
        const auto app_id_start = pos;
        pos += app_size;
        const bool netscape = std::equal(bytes.begin() + static_cast<std::ptrdiff_t>(app_id_start),
                                         bytes.begin() + static_cast<std::ptrdiff_t>(app_id_start + app_size),
                                         reinterpret_cast<const std::uint8_t*>("NETSCAPE2.0"));
        for (auto size = u8(); size != 0; size = u8()) {
          if (netscape && size == 3 && bytes[pos] == 1) {
            const auto loop_count = static_cast<std::uint16_t>(bytes[pos + 1] | (bytes[pos + 2] << 8U));
            result.loop_forever = loop_count == 0;
          }
          pos += size;
        }
      } else {
        for (auto size = u8(); size != 0; size = u8()) {
          pos += size;
        }
      }
      continue;
    }
    CHECK(block == 0x2c);
    CHECK(u16() == 0);  // left
    CHECK(u16() == 0);  // top
    CHECK(u16() == result.width);
    CHECK(u16() == result.height);
    const auto image_packed = u8();
    CHECK((image_packed & 0x40U) == 0);  // not interlaced
    DecodedGifFrame frame;
    frame.has_local_table = (image_packed & 0x80U) != 0;
    frame.palette = frame.has_local_table ? read_color_table(std::size_t{1} << ((image_packed & 0x07U) + 1U))
                                          : result.palette;
    frame.transparent_index = pending_transparent;
    frame.delay_cs = pending_delay_cs;
    frame.disposal = pending_disposal;
    pending_transparent = -1;
    pending_delay_cs = 0;
    pending_disposal = 0;
    const auto min_code_size = u8();
    std::vector<std::uint8_t> data;
    for (auto size = u8(); size != 0; size = u8()) {
      data.insert(data.end(), bytes.begin() + static_cast<std::ptrdiff_t>(pos),
                  bytes.begin() + static_cast<std::ptrdiff_t>(pos + size));
      pos += size;
    }
    frame.indexes = reference_decode_gif_lzw(data, min_code_size);
    result.frames.push_back(std::move(frame));
  }

  CHECK(!result.frames.empty());
  result.transparent_index = result.frames.front().transparent_index;
  result.indexes = result.frames.front().indexes;
  return result;
}

void gif_lzw_round_trips_through_reference_decoder() {
  // A pattern with long runs, noise, and >256 dictionary entries so the code width grows.
  const std::int32_t width = 101;
  const std::int32_t height = 53;
  std::vector<patchy::RgbColor> palette;
  for (int i = 0; i < 200; ++i) {
    palette.push_back(patchy::RgbColor{static_cast<std::uint8_t>(i), static_cast<std::uint8_t>(255 - i),
                                       static_cast<std::uint8_t>(i * 3)});
  }
  std::vector<std::uint8_t> indexes(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
  std::uint64_t state = 0x243F6A8885A308D3ULL;
  for (std::size_t i = 0; i < indexes.size(); ++i) {
    if ((i / 97) % 2 == 0) {
      indexes[i] = static_cast<std::uint8_t>((i / 13) % palette.size());
    } else {
      state = state * 6364136223846793005ULL + 1442695040888963407ULL;
      indexes[i] = static_cast<std::uint8_t>((state >> 33U) % palette.size());
    }
  }

  const auto bytes = patchy::gif::encode(width, height, palette, indexes, 5);
  const auto decoded = reference_decode_gif(bytes);
  CHECK(decoded.width == width);
  CHECK(decoded.height == height);
  CHECK(decoded.transparent_index == 5);
  CHECK(decoded.indexes == indexes);
  CHECK(decoded.palette.size() >= palette.size());
  for (std::size_t i = 0; i < palette.size(); ++i) {
    CHECK(decoded.palette[i].red == palette[i].red);
    CHECK(decoded.palette[i].green == palette[i].green);
    CHECK(decoded.palette[i].blue == palette[i].blue);
  }
}

void gif_encoder_bytes_are_stable() {
  // Cross-platform byte-identical rule: the exact encoder output is pinned by an FNV-1a
  // hash. A change here means the GIF bytes changed — re-pin only for deliberate encoder
  // changes (the failure output prints the new hash).
  std::vector<patchy::RgbColor> palette = {{0, 0, 0}, {255, 255, 255}, {200, 30, 40}, {40, 60, 200}};
  std::vector<std::uint8_t> indexes;
  for (int i = 0; i < 64; ++i) {
    indexes.push_back(static_cast<std::uint8_t>((i / 3) % 4));
  }
  const auto bytes = patchy::gif::encode(8, 8, palette, indexes, 3);
  std::uint64_t hash = 1469598103934665603ULL;
  for (const auto byte : bytes) {
    hash ^= byte;
    hash *= 1099511628211ULL;
  }
  constexpr std::uint64_t kExpected = 0x67535dc068a89baaULL;
  if (hash != kExpected) {
    std::cout << "gif encoder hash: 0x" << std::hex << hash << std::dec << " size " << bytes.size() << "\n";
  }
  CHECK(hash == kExpected);

  const auto decoded = reference_decode_gif(bytes);
  CHECK(decoded.indexes == indexes);
}

void gif_document_write_quantizes_and_round_trips() {
  // RGB document: exact colors fit, so quantization must preserve them; transparency
  // reserves one slot.
  patchy::Document document(16, 8, patchy::PixelFormat::rgba8());
  patchy::PixelBuffer pixels(16, 8, patchy::PixelFormat::rgba8());
  const std::array<patchy::RgbColor, 3> colors = {{{10, 200, 50}, {240, 240, 240}, {60, 60, 220}}};
  for (std::int32_t y = 0; y < 8; ++y) {
    for (std::int32_t x = 0; x < 16; ++x) {
      auto* px = pixels.pixel(x, y);
      if (x < 2) {
        px[3] = 0;
        continue;
      }
      const auto& color = colors[static_cast<std::size_t>(x % 3)];
      px[0] = color.red;
      px[1] = color.green;
      px[2] = color.blue;
      px[3] = 255;
    }
  }
  document.add_pixel_layer("Art", std::move(pixels));

  const auto bytes = patchy::gif::write(document);
  const auto decoded = reference_decode_gif(bytes);
  CHECK(decoded.width == 16);
  CHECK(decoded.height == 8);
  CHECK(decoded.transparent_index >= 0);
  // Transparent pixels map to the transparent slot; opaque ones keep their exact color.
  CHECK(decoded.indexes[0] == static_cast<std::uint8_t>(decoded.transparent_index));
  const auto& c5 = decoded.palette[decoded.indexes[5]];
  CHECK(c5.red == colors[5 % 3].red);
  CHECK(c5.green == colors[5 % 3].green);
  CHECK(c5.blue == colors[5 % 3].blue);
}

void gif_animation_encodes_frames_delays_and_loop() {
  // Three frames with different palettes (frame 2 exercises the local color table path),
  // transparency only on frame 3, and distinct delays.
  const std::int32_t width = 9;
  const std::int32_t height = 7;
  const auto frame_indexes = [&](int seed) {
    std::vector<std::uint8_t> indexes;
    for (std::int32_t i = 0; i < width * height; ++i) {
      indexes.push_back(static_cast<std::uint8_t>((i + seed) % 4));
    }
    return indexes;
  };
  std::vector<patchy::gif::GifFrame> frames(3);
  frames[0].palette = {{0, 0, 0}, {255, 0, 0}, {0, 255, 0}, {0, 0, 255}};
  frames[0].indexes = frame_indexes(0);
  frames[0].delay_cs = 10;
  frames[1].palette = {{10, 20, 30}, {40, 50, 60}, {70, 80, 90}, {100, 110, 120}, {130, 140, 150}};
  frames[1].indexes = frame_indexes(1);
  frames[1].delay_cs = 4;
  frames[2].palette = {{200, 200, 200}, {150, 150, 150}, {0, 0, 0}, {255, 255, 0}};
  frames[2].indexes = frame_indexes(2);
  frames[2].transparent_index = 2;
  frames[2].delay_cs = 25;

  const auto bytes = patchy::gif::encode_animation(width, height, frames);
  const auto decoded = reference_decode_gif(bytes);
  CHECK(decoded.width == width);
  CHECK(decoded.height == height);
  CHECK(decoded.loop_forever);
  CHECK(decoded.frames.size() == 3);
  // The global table is frame 1's palette; frame 1 carries no local table, later frames do.
  CHECK(!decoded.frames[0].has_local_table);
  CHECK(decoded.frames[1].has_local_table);
  CHECK(decoded.frames[2].has_local_table);
  for (std::size_t frame = 0; frame < frames.size(); ++frame) {
    const auto& expected = frames[frame];
    const auto& actual = decoded.frames[frame];
    CHECK(actual.indexes == expected.indexes);
    CHECK(actual.delay_cs == expected.delay_cs);
    CHECK(actual.disposal == 2);  // restore to background, so each frame displays alone
    CHECK(actual.transparent_index == expected.transparent_index);
    CHECK(actual.palette.size() >= expected.palette.size());
    for (std::size_t i = 0; i < expected.palette.size(); ++i) {
      CHECK(actual.palette[i].red == expected.palette[i].red);
      CHECK(actual.palette[i].green == expected.palette[i].green);
      CHECK(actual.palette[i].blue == expected.palette[i].blue);
    }
  }
  for (std::size_t i = 0; i < frames[0].palette.size(); ++i) {
    CHECK(decoded.palette[i].red == frames[0].palette[i].red);
    CHECK(decoded.palette[i].green == frames[0].palette[i].green);
    CHECK(decoded.palette[i].blue == frames[0].palette[i].blue);
  }
}

void gif_layer_name_delay_token_round_trips() {
  using patchy::gif::format_delay_seconds_token;
  using patchy::gif::parse_layer_name_delay_cs;
  // Accepted trailing tokens.
  CHECK(parse_layer_name_delay_cs("blink 0.25s") == std::uint16_t{25});
  CHECK(parse_layer_name_delay_cs("Frame 1 0.1s") == std::uint16_t{10});
  CHECK(parse_layer_name_delay_cs("1s") == std::uint16_t{100});
  CHECK(parse_layer_name_delay_cs("0s") == std::uint16_t{0});
  CHECK(parse_layer_name_delay_cs(".5s") == std::uint16_t{50});
  CHECK(parse_layer_name_delay_cs("walk cycle 2.s") == std::uint16_t{200});
  CHECK(parse_layer_name_delay_cs("0.045s") == std::uint16_t{5});   // rounds half-up
  CHECK(parse_layer_name_delay_cs("0.044s") == std::uint16_t{4});
  CHECK(parse_layer_name_delay_cs("700s") == std::uint16_t{65535});  // clamps to the u16 wire range
  CHECK(parse_layer_name_delay_cs("999999999999999999999s") == std::uint16_t{65535});
  CHECK(parse_layer_name_delay_cs("stuff 0.4s  ") == std::uint16_t{40});  // trailing spaces trim
  // Rejected names fall back to the dialog default.
  CHECK(!parse_layer_name_delay_cs("blink").has_value());
  CHECK(!parse_layer_name_delay_cs("0.25S").has_value());  // uppercase S is not a token
  CHECK(!parse_layer_name_delay_cs("2.5.1s").has_value());
  CHECK(!parse_layer_name_delay_cs("s").has_value());
  CHECK(!parse_layer_name_delay_cs(".s").has_value());
  CHECK(!parse_layer_name_delay_cs("12 s").has_value());  // the last token holds no digits
  CHECK(!parse_layer_name_delay_cs("0,5s").has_value());
  CHECK(!parse_layer_name_delay_cs("").has_value());
  CHECK(!parse_layer_name_delay_cs("1e2s").has_value());

  CHECK(format_delay_seconds_token(10) == "0.1s");
  CHECK(format_delay_seconds_token(4) == "0.04s");
  CHECK(format_delay_seconds_token(100) == "1s");
  CHECK(format_delay_seconds_token(250) == "2.5s");
  CHECK(format_delay_seconds_token(0) == "0s");
  CHECK(format_delay_seconds_token(65535) == "655.35s");
  // Every wire value formats to a token that parses back to itself.
  for (std::uint32_t cs = 0; cs <= 65535; cs += 7) {
    const auto token = "layer " + format_delay_seconds_token(static_cast<std::uint16_t>(cs));
    CHECK(parse_layer_name_delay_cs(token) == static_cast<std::uint16_t>(cs));
  }

  // Stripping removes exactly the trailing token plus surrounding whitespace; names
  // without a valid token pass through untouched.
  using patchy::gif::strip_layer_name_delay_token;
  CHECK(strip_layer_name_delay_token("blink 0.25s") == "blink");
  CHECK(strip_layer_name_delay_token("Frame 1 0.1s") == "Frame 1");
  CHECK(strip_layer_name_delay_token("stuff 0.4s  ") == "stuff");
  CHECK(strip_layer_name_delay_token("gap  \t 2s") == "gap");
  CHECK(strip_layer_name_delay_token("0.25s") == "");  // a token-only name strips to empty
  CHECK(strip_layer_name_delay_token("blink") == "blink");
  CHECK(strip_layer_name_delay_token("12 s") == "12 s");
  CHECK(strip_layer_name_delay_token("0.25S") == "0.25S");
  CHECK(strip_layer_name_delay_token("") == "");
}

void aseprite_imports_layers_groups_and_palette() {
  // Fixture authored by Aseprite 1.3.17 itself (scratch script; see the fixture comment in
  // NOTICE-THIRD-PARTY.md): Base, group "Props" holding half-opacity Multiply "Star" at
  // (5,6), hidden "Hidden", and "Weird" with the unsupported Exclusion blend.
  std::vector<std::string> notices;
  const auto document = patchy::aseprite::DocumentIo::read_file(
      patchy::test::committed_format_fixture_path("aseprite", "aseprite-layered.aseprite"), &notices);
  CHECK(document.width() == 32);
  CHECK(document.height() == 24);
  CHECK(document.layers().size() == 4);

  const auto& base = document.layers()[0];
  CHECK(base.name() == "Base");
  CHECK(base.pixels().pixel(3, 3)[2] == 200);

  // An RGB-mode file must NOT adopt its palette chunk (Aseprite pads a 256-black legacy
  // palette into RGB files; adopting it dragged documents into an all-black palette mode).
  CHECK(!document.indexed_palette().has_value());

  const auto& group = document.layers()[1];
  CHECK(group.kind() == patchy::LayerKind::Group);
  CHECK(group.name() == "Props");
  // Header flag bit 2 is unset, so the group chunk's opacity byte (0 in this file!) is
  // meaningless and the group must render at full opacity with Normal blend.
  CHECK(group.opacity() == 1.0F);
  CHECK(group.blend_mode() == patchy::BlendMode::Normal);
  CHECK(group.children().size() == 1);
  const auto& star = group.children()[0];
  CHECK(star.name() == "Star");
  CHECK(star.blend_mode() == patchy::BlendMode::Multiply);
  CHECK(star.bounds().x == 5);
  CHECK(star.bounds().y == 6);
  CHECK(star.opacity() > 0.45F);
  CHECK(star.opacity() < 0.55F);
  CHECK(star.pixels().pixel(0, 0)[0] == 200);

  CHECK(!document.layers()[2].visible());
  CHECK(document.layers()[3].name() == "Weird");
  // Exclusion is a supported blend mode now (July 2026), so no fallback notice appears.
  CHECK(document.layers()[3].blend_mode() == patchy::BlendMode::Exclusion);
  for (const auto& notice : notices) {
    CHECK(notice.find("Weird") == std::string::npos);
  }

  // The composite must show the group's child: Star multiplies red over blue at 50%, so
  // the star area is a dark blue clearly below the base color (this was the group-opacity
  // regression: the group imported at opacity 0 and Star vanished).
  const auto flattened = patchy::flatten_document_rgba8(document);
  const auto* base_px = flattened.pixel(2, 2);
  CHECK(base_px[0] == 10);
  CHECK(base_px[1] == 20);
  CHECK(base_px[2] == 200);
  const auto* star_px = flattened.pixel(7, 8);
  CHECK(star_px[2] < 160);  // multiplied down from 200
  CHECK(star_px[2] > 60);   // but not black: 50% layer opacity keeps half the base
  // The Exclusion layer renders for real: red over blue gives the magenta Aseprite shows.
  const auto* weird_px = flattened.pixel(22, 12);
  CHECK(weird_px[0] == 194);
  CHECK(weird_px[1] == 54);
  CHECK(weird_px[2] == 178);
}

void aseprite_blend_modes_match_aseprite_render() {
  // Fixture authored in Aseprite 1.3.17: base (10,20,200) with six 4x4 (200,40,40) squares
  // using Exclusion / Hue / Color / Addition / Subtract / Divide. The expected values are
  // Aseprite's own flattened render (committed as aseprite-blend-modes-reference.png);
  // the non-separable modes tolerate 1/255 of double-rounding drift.
  const auto document = patchy::aseprite::DocumentIo::read_file(
      patchy::test::committed_format_fixture_path("aseprite", "aseprite-blend-modes.aseprite"));
  CHECK(document.width() == 24);
  CHECK(document.layers().size() == 7);
  const auto flattened = patchy::flatten_document_rgba8(document);

  struct ExpectedSquare {
    patchy::BlendMode mode;
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
    int tolerance;
  };
  const std::array<ExpectedSquare, 6> expected = {{
      {patchy::BlendMode::Exclusion, 194, 54, 178, 0},
      {patchy::BlendMode::Hue, 122, 0, 0, 1},
      {patchy::BlendMode::Color, 122, 0, 0, 1},
      {patchy::BlendMode::LinearDodge, 210, 60, 240, 0},
      {patchy::BlendMode::Subtract, 0, 0, 160, 0},
      {patchy::BlendMode::Divide, 13, 128, 255, 0},
  }};
  for (std::size_t index = 0; index < expected.size(); ++index) {
    const auto& square = expected[index];
    CHECK(document.layers()[index + 1].blend_mode() == square.mode);
    const auto* px = flattened.pixel(static_cast<std::int32_t>(index) * 4 + 2, 2);
    CHECK(std::abs(static_cast<int>(px[0]) - static_cast<int>(square.r)) <= square.tolerance);
    CHECK(std::abs(static_cast<int>(px[1]) - static_cast<int>(square.g)) <= square.tolerance);
    CHECK(std::abs(static_cast<int>(px[2]) - static_cast<int>(square.b)) <= square.tolerance);
  }

  // Writer round trip: every new mode survives Patchy's own .aseprite writer.
  const auto rewritten = patchy::aseprite::DocumentIo::read(patchy::aseprite::DocumentIo::write(document));
  for (std::size_t index = 0; index < expected.size(); ++index) {
    CHECK(rewritten.layers()[index + 1].blend_mode() == expected[index].mode);
  }
}

void psd_round_trips_new_blend_modes() {
  patchy::Document document(4, 4, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Base", solid_rgb(4, 4, 10, 20, 200));
  const std::array<patchy::BlendMode, 7> modes = {patchy::BlendMode::Exclusion,   patchy::BlendMode::Hue,
                                                  patchy::BlendMode::Color,       patchy::BlendMode::LinearDodge,
                                                  patchy::BlendMode::Subtract,    patchy::BlendMode::Divide,
                                                  patchy::BlendMode::Dissolve};
  for (const auto mode : modes) {
    auto& layer = document.add_pixel_layer("Layer", solid_rgba(4, 4, 200, 40, 40, 255));
    layer.set_blend_mode(mode);
  }
  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto read_back = patchy::psd::DocumentIo::read(bytes, patchy::psd::ReadOptions{true, false, true});
  CHECK(read_back.layers().size() == modes.size() + 1);
  for (std::size_t index = 0; index < modes.size(); ++index) {
    CHECK(read_back.layers()[index + 1].blend_mode() == modes[index]);
  }
}

void aseprite_indexed_transparent_index() {
  std::vector<std::string> notices;
  const auto document = patchy::aseprite::DocumentIo::read_file(
      patchy::test::committed_format_fixture_path("aseprite", "aseprite-indexed-frames.aseprite"), &notices);
  CHECK(document.width() == 16);
  CHECK(document.height() == 12);
  CHECK(document.indexed_palette().has_value());
  CHECK(document.indexed_palette()->colors.size() == 4);
  const auto& pixels = document.layers().front().pixels();
  CHECK(pixels.pixel(0, 0)[3] == 0);  // transparent index
  CHECK(pixels.pixel(1, 0)[0] == 200);
  CHECK(pixels.pixel(8, 0)[2] == 200);
  bool noted_frames = false;
  for (const auto& notice : notices) {
    noted_frames = noted_frames || notice.find("first frame") != std::string::npos;
  }
  CHECK(noted_frames);
}

void aseprite_rejects_adobe_swatch_ase() {
  const std::vector<std::uint8_t> swatch = {'A', 'S', 'E', 'F', 0, 1, 0, 0};
  bool rejected = false;
  try {
    (void)patchy::aseprite::DocumentIo::read(swatch);
  } catch (const std::exception& error) {
    rejected = std::string(error.what()).find("Adobe swatch") != std::string::npos;
  }
  CHECK(rejected);
  CHECK(!patchy::aseprite::sniff(swatch));
}

namespace {

// Builds a minimal one-frame 32-bit Aseprite file from hand-rolled chunks.
class AseBytes {
 public:
  void u8(std::uint8_t value) { bytes_.push_back(value); }
  void u16(std::uint16_t value) {
    u8(static_cast<std::uint8_t>(value & 0xFFU));
    u8(static_cast<std::uint8_t>(value >> 8U));
  }
  void u32(std::uint32_t value) {
    u16(static_cast<std::uint16_t>(value & 0xFFFFU));
    u16(static_cast<std::uint16_t>(value >> 16U));
  }
  void zeros(std::size_t count) { bytes_.insert(bytes_.end(), count, 0); }
  void patch_u32(std::size_t offset, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
      bytes_[offset + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((value >> (8U * i)) & 0xFFU);
    }
  }
  [[nodiscard]] std::size_t size() const { return bytes_.size(); }
  [[nodiscard]] const std::vector<std::uint8_t>& bytes() const { return bytes_; }

 private:
  std::vector<std::uint8_t> bytes_;
};

// A layer chunk (0x2004) with the given child level; `declared_size` overrides the chunk
// size field when non-zero.
void append_ase_layer_chunk(AseBytes& out, std::uint16_t child_level, std::uint16_t layer_type,
                            const std::string& name) {
  const auto start = out.size();
  out.u32(0);  // size, patched below
  out.u16(0x2004);
  out.u16(1);  // flags: visible
  out.u16(layer_type);
  out.u16(child_level);
  out.u16(0);
  out.u16(0);
  out.u16(0);    // blend mode Normal
  out.u8(255);   // opacity
  out.zeros(3);
  out.u16(static_cast<std::uint16_t>(name.size()));
  for (const auto character : name) {
    out.u8(static_cast<std::uint8_t>(character));
  }
  out.patch_u32(start, static_cast<std::uint32_t>(out.size() - start));
}

std::vector<std::uint8_t> finish_ase_file(AseBytes& out, std::size_t frame_start, std::uint32_t chunk_count) {
  out.patch_u32(frame_start, static_cast<std::uint32_t>(out.size() - frame_start));
  out.patch_u32(frame_start + 12, chunk_count);
  out.patch_u32(0, static_cast<std::uint32_t>(out.size()));
  return out.bytes();
}

std::size_t begin_ase_file(AseBytes& out) {
  out.u32(0);       // file size, patched at the end
  out.u16(0xA5E0);  // magic
  out.u16(1);       // frames
  out.u16(4);       // width
  out.u16(4);       // height
  out.u16(32);      // depth
  out.u32(1);       // flags: layer opacity valid
  out.zeros(128 - out.size());
  const auto frame_start = out.size();
  out.u32(0);       // frame bytes, patched
  out.u16(0xF1FA);  // frame magic
  out.u16(0);       // old chunk count
  out.u16(100);     // duration
  out.zeros(2);
  out.u32(0);       // new chunk count, patched
  return frame_start;
}

}  // namespace

void aseprite_out_of_range_child_level_lands_at_root() {
  // Two image layers claiming child level 1 with no group open. The reader used to grow
  // its parent stack with null slots for the first and dereference one for the second.
  AseBytes out;
  const auto frame_start = begin_ase_file(out);
  append_ase_layer_chunk(out, 1, 0, "orphan-a");
  append_ase_layer_chunk(out, 1, 0, "orphan-b");
  const auto bytes = finish_ase_file(out, frame_start, 2);
  const auto document = patchy::aseprite::DocumentIo::read(bytes);
  CHECK(document.layers().size() == 2);
  CHECK(document.layers()[0].name() == "orphan-a");
  CHECK(document.layers()[1].name() == "orphan-b");

  // A group at the root followed by a layer that skips a level and one that claims level
  // 1: the skipped level lands at the root and closes the group (the root push_back may
  // have moved it), so the third layer is a root layer too instead of a write through a
  // stale group pointer.
  AseBytes nested;
  const auto nested_start = begin_ase_file(nested);
  append_ase_layer_chunk(nested, 0, 1, "group");
  append_ase_layer_chunk(nested, 2, 0, "skips-a-level");
  append_ase_layer_chunk(nested, 1, 0, "child");
  const auto nested_bytes = finish_ase_file(nested, nested_start, 3);
  const auto nested_document = patchy::aseprite::DocumentIo::read(nested_bytes);
  CHECK(nested_document.layers().size() == 3);
  CHECK(nested_document.layers()[0].name() == "group");
  CHECK(nested_document.layers()[0].children().empty());
  CHECK(nested_document.layers()[1].name() == "skips-a-level");
  CHECK(nested_document.layers()[1].kind() == patchy::LayerKind::Pixel);
  CHECK(nested_document.layers()[2].name() == "child");
}

void aseprite_rejects_cel_chunk_shorter_than_its_header() {
  // A compressed cel whose chunk size (10) ends before its fixed fields: the reader used to
  // compute a wrapped negative payload size and hand inflate the rest of the file.
  AseBytes out;
  const auto frame_start = begin_ase_file(out);
  append_ase_layer_chunk(out, 0, 0, "layer");
  const auto cel_start = out.size();
  out.u32(10);      // declared chunk size, shorter than the 26-byte cel header
  out.u16(0x2005);  // cel chunk
  out.u16(0);       // layer index
  out.u16(0);       // x
  out.u16(0);       // y
  out.u8(255);      // opacity
  out.u16(2);       // compressed image cel
  out.zeros(7);
  out.u16(2);       // width
  out.u16(2);       // height
  out.zeros(16);    // "compressed" payload
  (void)cel_start;
  const auto bytes = finish_ase_file(out, frame_start, 2);
  bool threw = false;
  try {
    (void)patchy::aseprite::DocumentIo::read(bytes);
  } catch (const std::exception&) {
    threw = true;
  }
  CHECK(threw);
}

void aseprite_write_read_round_trips_layers_and_palette() {
  patchy::Document document(20, 20, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Backdrop", solid_rgba_square(20, 30, 60, 90, 255));
  // add_pixel_layer requires full-canvas dimensions; shrink via a hand-built layer.
  {
    patchy::Layer sprite_layer(document.allocate_layer_id(), "Sprite", solid_rgba_square(6, 220, 50, 40, 255));
    sprite_layer.set_bounds(patchy::Rect{7, 3, 6, 6});
    sprite_layer.set_blend_mode(patchy::BlendMode::Multiply);
    sprite_layer.set_opacity(0.5F);
    document.add_layer(std::move(sprite_layer));
  }
  {
    patchy::Layer group(document.allocate_layer_id(), "Bits", patchy::LayerKind::Group);
    patchy::Layer child(document.allocate_layer_id(), "Dot", solid_rgba_square(2, 10, 250, 10, 255));
    child.set_bounds(patchy::Rect{1, 1, 2, 2});
    child.set_visible(false);
    group.add_child(std::move(child));
    document.add_layer(std::move(group));
  }

  const auto bytes = patchy::aseprite::DocumentIo::write(document);
  CHECK(patchy::aseprite::sniff(bytes));
  const auto read_back = patchy::aseprite::DocumentIo::read(bytes);
  CHECK(read_back.width() == 20);
  CHECK(read_back.height() == 20);
  CHECK(read_back.layers().size() == 3);
  CHECK(read_back.layers()[0].name() == "Backdrop");
  CHECK(read_back.layers()[1].name() == "Sprite");
  CHECK(read_back.layers()[1].blend_mode() == patchy::BlendMode::Multiply);
  CHECK(read_back.layers()[1].bounds().x == 7);
  CHECK(read_back.layers()[1].bounds().y == 3);
  CHECK(read_back.layers()[1].opacity() > 0.45F);
  CHECK(read_back.layers()[1].opacity() < 0.55F);
  CHECK(read_back.layers()[2].kind() == patchy::LayerKind::Group);
  CHECK(read_back.layers()[2].children().size() == 1);
  CHECK(!read_back.layers()[2].children()[0].visible());
  CHECK(read_back.layers()[1].pixels().pixel(2, 2)[0] == 220);
  CHECK(patchy::layer_group_expanded(read_back.layers()[2]));

  // Issue 179: a closed folder writes Aseprite's "displayed collapsed" layer flag (32).
  {
    auto folded = document;
    patchy::set_layer_group_expanded(folded.layers()[2], false);
    const auto folded_read = patchy::aseprite::DocumentIo::read(patchy::aseprite::DocumentIo::write(folded));
    CHECK(folded_read.layers()[2].kind() == patchy::LayerKind::Group);
    CHECK(!patchy::layer_group_expanded(folded_read.layers()[2]));
  }

  // Palette-mode documents round trip as indexed with the document palette.
  patchy::Document indexed_doc(8, 4, patchy::PixelFormat::rgb8());
  const auto* preset = patchy::find_builtin_palette_preset("gameboy");
  CHECK(preset != nullptr);
  patchy::DocumentPaletteEditing editing;
  editing.palette.colors.assign(preset->colors.begin(), preset->colors.end());
  indexed_doc.palette_editing() = editing;
  patchy::PixelBuffer pixels(8, 4, patchy::PixelFormat::rgb8());
  for (std::int32_t y = 0; y < 4; ++y) {
    for (std::int32_t x = 0; x < 8; ++x) {
      const auto& color = preset->colors[static_cast<std::size_t>(x % 4)];
      auto* px = pixels.pixel(x, y);
      px[0] = color.red;
      px[1] = color.green;
      px[2] = color.blue;
    }
  }
  indexed_doc.add_pixel_layer("Pixels", std::move(pixels));
  const auto indexed_bytes = patchy::aseprite::DocumentIo::write(indexed_doc);
  const auto indexed_back = patchy::aseprite::DocumentIo::read(indexed_bytes);
  CHECK(indexed_back.indexed_palette().has_value());
  for (std::int32_t x = 0; x < 8; ++x) {
    const auto& expected = preset->colors[static_cast<std::size_t>(x % 4)];
    const auto* px = indexed_back.layers().front().pixels().pixel(x, 1);
    CHECK(px[0] == expected.red);
    CHECK(px[1] == expected.green);
    CHECK(px[2] == expected.blue);
  }

  // Written to disk for the Aseprite CLI verification pass (Aseprite must open it).
  std::filesystem::create_directories("test-artifacts");
  patchy::aseprite::DocumentIo::write_file(document, "test-artifacts/aseprite_written.aseprite");
  patchy::aseprite::DocumentIo::write_file(indexed_doc, "test-artifacts/aseprite_written_indexed.aseprite");
}

void pcx_reads_8bit_indexed_and_24bit_rle() {
  const std::array<const char*, 3> names = {"pillow-rgb24.pcx", "pillow-indexed.pcx", "pillow-rgb24-odd.pcx"};
  for (const auto* name : names) {
    const auto path = patchy::test::committed_format_fixture_path("pcx", name);
    CHECK(std::filesystem::exists(path));
    const auto document = patchy::pcx::DocumentIo::read_file(path);
    CHECK(document.width() >= 47);
    CHECK(document.height() >= 31);
    const auto& pixels = document.layers().front().pixels();
    const auto* red = pixels.pixel(5, 5);
    CHECK(red[0] > 150);
    CHECK(red[1] < 100);
    const auto* background = pixels.pixel(40, 5);
    CHECK(background[2] > background[0]);
  }
  const auto indexed =
      patchy::pcx::DocumentIo::read_file(patchy::test::committed_format_fixture_path("pcx", "pillow-indexed.pcx"));
  CHECK(indexed.indexed_palette().has_value());
}

void pcx_write_read_round_trips() {
  // RGB path.
  patchy::Document document(21, 9, patchy::PixelFormat::rgb8());
  patchy::PixelBuffer pixels(21, 9, patchy::PixelFormat::rgb8());
  for (std::int32_t y = 0; y < 9; ++y) {
    for (std::int32_t x = 0; x < 21; ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = static_cast<std::uint8_t>(x * 12);
      px[1] = static_cast<std::uint8_t>(y * 28);
      px[2] = static_cast<std::uint8_t>(200 - x * 3);
    }
  }
  document.add_pixel_layer("Art", pixels);
  const auto bytes = patchy::pcx::DocumentIo::write(document);
  CHECK(patchy::pcx::DocumentIo::can_read(bytes));
  const auto read_back = patchy::pcx::DocumentIo::read(bytes);
  CHECK(read_back.width() == 21);
  CHECK(read_back.height() == 9);
  for (std::int32_t y = 0; y < 9; ++y) {
    for (std::int32_t x = 0; x < 21; ++x) {
      const auto* expected = pixels.pixel(x, y);
      const auto* actual = read_back.layers().front().pixels().pixel(x, y);
      CHECK(actual[0] == expected[0]);
      CHECK(actual[1] == expected[1]);
      CHECK(actual[2] == expected[2]);
    }
  }

  // Palette-mode path: indexed PCX with the document palette at EOF.
  patchy::Document indexed_doc(8, 4, patchy::PixelFormat::rgb8());
  const auto* preset = patchy::find_builtin_palette_preset("gameboy");
  CHECK(preset != nullptr);
  patchy::DocumentPaletteEditing editing;
  editing.palette.colors.assign(preset->colors.begin(), preset->colors.end());
  indexed_doc.palette_editing() = editing;
  patchy::PixelBuffer indexed_pixels(8, 4, patchy::PixelFormat::rgb8());
  for (std::int32_t y = 0; y < 4; ++y) {
    for (std::int32_t x = 0; x < 8; ++x) {
      const auto& color = preset->colors[static_cast<std::size_t>(x % 4)];
      auto* px = indexed_pixels.pixel(x, y);
      px[0] = color.red;
      px[1] = color.green;
      px[2] = color.blue;
    }
  }
  indexed_doc.add_pixel_layer("Pixels", std::move(indexed_pixels));
  const auto indexed_bytes = patchy::pcx::DocumentIo::write(indexed_doc);
  const auto indexed_back = patchy::pcx::DocumentIo::read(indexed_bytes);
  CHECK(indexed_back.indexed_palette().has_value());
  for (std::int32_t x = 0; x < 8; ++x) {
    const auto& expected = preset->colors[static_cast<std::size_t>(x % 4)];
    const auto* px = indexed_back.layers().front().pixels().pixel(x, 2);
    CHECK(px[0] == expected.red);
    CHECK(px[1] == expected.green);
    CHECK(px[2] == expected.blue);
  }
  // Written to disk for the Pillow verification pass.
  std::filesystem::create_directories("test-artifacts");
  patchy::pcx::DocumentIo::write_file(document, "test-artifacts/pcx_written_rgb.pcx");
  patchy::pcx::DocumentIo::write_file(indexed_doc, "test-artifacts/pcx_written_indexed.pcx");
}

// Synthesizes a 4-plane ILBM byte-by-byte: 8x2, EHB off, ByteRun1 off (BODY raw),
// masking 2 (transparent color 3).
[[nodiscard]] std::vector<std::uint8_t> synthesize_ilbm_fixture() {
  patchy::psd::BigEndianWriter form;
  const auto put_id = [&form](const char* id) {
    for (int i = 0; i < 4; ++i) {
      form.write_u8(static_cast<std::uint8_t>(id[i]));
    }
  };
  put_id("FORM");
  form.write_u32(0);  // patched at the end
  put_id("ILBM");
  put_id("BMHD");
  form.write_u32(20);
  form.write_u16(8);
  form.write_u16(2);
  form.write_u16(0);
  form.write_u16(0);
  form.write_u8(4);   // planes
  form.write_u8(2);   // masking: transparent color
  form.write_u8(0);   // uncompressed
  form.write_u8(0);
  form.write_u16(3);  // transparent color
  form.write_u8(1);
  form.write_u8(1);
  form.write_u16(8);
  form.write_u16(2);
  put_id("CMAP");
  form.write_u32(16 * 3);
  for (int i = 0; i < 16; ++i) {
    form.write_u8(static_cast<std::uint8_t>(i * 16));
    form.write_u8(static_cast<std::uint8_t>(255 - i * 16));
    form.write_u8(static_cast<std::uint8_t>(i * 8));
  }
  put_id("BODY");
  // 2 rows x 4 planes x 2 bytes (8px word-aligned row). Row pixels: indexes 0..7 then 8..15.
  form.write_u32(2 * 4 * 2);
  const auto write_plane_rows = [&form](std::uint8_t base) {
    for (std::uint8_t plane = 0; plane < 4; ++plane) {
      std::uint8_t byte = 0;
      for (int x = 0; x < 8; ++x) {
        const auto index = static_cast<std::uint8_t>(base + x);
        byte = static_cast<std::uint8_t>(byte << 1U);
        byte |= (index >> plane) & 1U;
      }
      form.write_u8(byte);
      form.write_u8(0);  // word padding
    }
  };
  write_plane_rows(0);
  write_plane_rows(8);
  auto bytes = form.bytes();
  const auto form_size = static_cast<std::uint32_t>(bytes.size() - 8U);
  bytes[4] = static_cast<std::uint8_t>((form_size >> 24U) & 0xffU);
  bytes[5] = static_cast<std::uint8_t>((form_size >> 16U) & 0xffU);
  bytes[6] = static_cast<std::uint8_t>((form_size >> 8U) & 0xffU);
  bytes[7] = static_cast<std::uint8_t>(form_size & 0xffU);
  return bytes;
}

void ilbm_reads_planar_masked_body() {
  const auto bytes = synthesize_ilbm_fixture();
  CHECK(patchy::ilbm::DocumentIo::can_read(bytes));
  const auto document = patchy::ilbm::DocumentIo::read(bytes);
  CHECK(document.width() == 8);
  CHECK(document.height() == 2);
  CHECK(document.indexed_palette().has_value());
  CHECK(document.indexed_palette()->colors.size() == 16);
  const auto& pixels = document.layers().front().pixels();
  // Row 0 holds indexes 0..7: pixel x has color (16x, 255-16x, 8x); index 3 is transparent.
  CHECK(pixels.pixel(2, 0)[0] == 32);
  CHECK(pixels.pixel(2, 0)[1] == 255 - 32);
  CHECK(pixels.pixel(2, 0)[3] == 255);
  CHECK(pixels.pixel(3, 0)[3] == 0);  // transparent color
  CHECK(pixels.pixel(4, 1)[0] == 192);  // row 1 starts at index 8 -> x=4 is index 12
}

void ilbm_rejects_ham_with_message() {
  auto bytes = synthesize_ilbm_fixture();
  // Splice a CAMG chunk with the HAM bit right after the form type by rebuilding: simplest
  // is to flip masking? No — append CAMG before BODY is complex; instead patch a HAM CAMG
  // into a copy by replacing the CMAP id with CAMG (still parsed before BODY).
  // Cleaner: build a minimal HAM file.
  patchy::psd::BigEndianWriter form;
  const auto put_id = [&form](const char* id) {
    for (int i = 0; i < 4; ++i) {
      form.write_u8(static_cast<std::uint8_t>(id[i]));
    }
  };
  put_id("FORM");
  form.write_u32(0);
  put_id("ILBM");
  put_id("BMHD");
  form.write_u32(20);
  form.write_u16(4);
  form.write_u16(1);
  form.write_u16(0);
  form.write_u16(0);
  form.write_u8(6);
  form.write_u8(0);
  form.write_u8(0);
  form.write_u8(0);
  form.write_u16(0);
  form.write_u8(1);
  form.write_u8(1);
  form.write_u16(4);
  form.write_u16(1);
  put_id("CAMG");
  form.write_u32(4);
  form.write_u32(0x800);  // HAM
  put_id("CMAP");
  form.write_u32(3);
  form.write_u8(0);
  form.write_u8(0);
  form.write_u8(0);
  form.write_u8(0);  // pad
  put_id("BODY");
  form.write_u32(6 * 2);
  for (int i = 0; i < 12; ++i) {
    form.write_u8(0);
  }
  auto ham_bytes = form.bytes();
  const auto form_size = static_cast<std::uint32_t>(ham_bytes.size() - 8U);
  ham_bytes[7] = static_cast<std::uint8_t>(form_size & 0xffU);
  ham_bytes[6] = static_cast<std::uint8_t>((form_size >> 8U) & 0xffU);
  bool rejected = false;
  try {
    (void)patchy::ilbm::DocumentIo::read(ham_bytes);
  } catch (const std::exception& error) {
    rejected = std::string(error.what()).find("HAM") != std::string::npos;
  }
  CHECK(rejected);
}

void ilbm_write_read_round_trips_indexed() {
  patchy::Document document(19, 7, patchy::PixelFormat::rgba8());
  patchy::PixelBuffer pixels(19, 7, patchy::PixelFormat::rgba8());
  const std::array<patchy::RgbColor, 5> colors = {
      {{0, 0, 0}, {255, 255, 255}, {200, 30, 40}, {40, 60, 200}, {30, 180, 40}}};
  for (std::int32_t y = 0; y < 7; ++y) {
    for (std::int32_t x = 0; x < 19; ++x) {
      auto* px = pixels.pixel(x, y);
      if (x == 0 && y == 0) {
        px[3] = 0;  // one transparent pixel -> masking type 2
        continue;
      }
      const auto& color = colors[static_cast<std::size_t>((x + y) % colors.size())];
      px[0] = color.red;
      px[1] = color.green;
      px[2] = color.blue;
      px[3] = 255;
    }
  }
  document.add_pixel_layer("Art", pixels);
  const auto bytes = patchy::ilbm::DocumentIo::write(document);
  CHECK(patchy::ilbm::DocumentIo::can_read(bytes));
  const auto read_back = patchy::ilbm::DocumentIo::read(bytes);
  CHECK(read_back.width() == 19);
  CHECK(read_back.height() == 7);
  const auto& round = read_back.layers().front().pixels();
  CHECK(round.pixel(0, 0)[3] == 0);
  for (std::int32_t y = 0; y < 7; ++y) {
    for (std::int32_t x = 0; x < 19; ++x) {
      if (x == 0 && y == 0) {
        continue;
      }
      const auto& expected = colors[static_cast<std::size_t>((x + y) % colors.size())];
      const auto* actual = round.pixel(x, y);
      CHECK(actual[0] == expected.red);
      CHECK(actual[1] == expected.green);
      CHECK(actual[2] == expected.blue);
    }
  }
  std::filesystem::create_directories("test-artifacts");
  patchy::ilbm::DocumentIo::write_file(document, "test-artifacts/ilbm_written.lbm");
}

void webp_animation_lossless_timing_loops_and_limits() {
  patchy::PixelBuffer first(16, 12, patchy::PixelFormat::rgba8());
  patchy::PixelBuffer second(16, 12, patchy::PixelFormat::rgba8());
  for (int y = 0; y < 12; ++y) for (int x = 0; x < 16; ++x) {
    auto* a = first.pixel(x, y); auto* b = second.pixel(x, y);
    a[0] = static_cast<std::uint8_t>(x * 13); a[1] = 60; a[2] = 170; a[3] = 127;
    b[0] = 240; b[1] = static_cast<std::uint8_t>(y * 17); b[2] = 20; b[3] = 255;
  }
  for (const auto loops : {0, 1, 3, 65535}) {
    patchy::webp::AnimationEncoder encoder(16, 12, static_cast<std::uint16_t>(loops), 75, true);
    encoder.add(first, 33); encoder.add(second, 67); encoder.add(first, 101);
    const auto bytes = encoder.finish();
    int count = 0;
    const std::array<unsigned, 3> durations{33, 67, 101};
    CHECK(patchy::webp::decode_animation(bytes, 1024 * 1024, [&](const auto& info) {
      CHECK(info.width == 16); CHECK(info.height == 12); CHECK(info.loop_count == loops);
      CHECK(info.frame_count == 3);
    }, [&](patchy::PixelBuffer pixels, unsigned duration) {
      CHECK(duration == durations[static_cast<std::size_t>(count)]);
      const auto& expected = count == 1 ? second : first;
      CHECK(std::equal(pixels.data().begin(), pixels.data().end(), expected.data().begin()));
      ++count;
    }));
    CHECK(count == 3);
    bool threw = false;
    try { patchy::webp::decode_animation(bytes, 10, [](const auto&) {}, [](auto, auto) {}); }
    catch (const std::exception&) { threw = true; }
    CHECK(threw);
    auto truncated = bytes; truncated.resize(bytes.size() / 2);
    threw = false;
    try { patchy::webp::decode_animation(truncated, 0, [](const auto&) {}, [](auto, auto) {}); }
    catch (const std::exception&) { threw = true; }
    CHECK(threw);
  }
  bool threw = false;
  try { patchy::webp::AnimationEncoder invalid(16384, 12, 0, 75, false); }
  catch (const std::exception&) { threw = true; }
  CHECK(threw);
  patchy::webp::AnimationEncoder encoder(16, 12, 0, 20, false);
  encoder.add(first, 0); encoder.add(second, 50);
  CHECK(!encoder.finish().empty());
}

void webp_animation_millisecond_tokens_preserve_gif_rounding() {
  using patchy::animation::parse_layer_name_delay_ms;
  CHECK(parse_layer_name_delay_ms("Frame 1 0.033s") == 33u);
  CHECK(parse_layer_name_delay_ms("Frame 1 0.0449s") == 45u);
  CHECK(patchy::gif::parse_layer_name_delay_cs("Frame 1 0.0449s") == 4);
  CHECK(parse_layer_name_delay_ms("Frame 1 .5s") == 500u);
  CHECK(parse_layer_name_delay_ms("Frame 1 0s") == 0u);
  CHECK(parse_layer_name_delay_ms("99999999999999999999999s") == patchy::animation::kMaxFrameDelayMs);
  for (const auto name : {"", "word", "0.5S", "0,5s", "1e2s", "2.3.4s", ".s"}) {
    CHECK(!parse_layer_name_delay_ms(name).has_value());
  }
  for (const unsigned delay : {0U, 1U, 33U, 100U, 1001U, 16777215U}) {
    CHECK(parse_layer_name_delay_ms(patchy::animation::format_delay_seconds_token(delay)) == delay);
  }
}

// GitHub issue 362: a file opens by its content when its name says another format or
// none. The sniff names what Patchy can open and nothing else.
void sniff_openable_image_format_names_what_patchy_opens() {
  using patchy::formats::extensions_share_format;
  using patchy::formats::sniff_openable_image_format;
  using Bytes = std::vector<std::uint8_t>;
  const auto of = [](std::string_view text) {
    return Bytes(text.begin(), text.end());
  };
  const auto extension_of = [&](const Bytes& bytes) {
    const auto sniffed = sniff_openable_image_format(bytes);
    return sniffed.has_value() ? sniffed->extension : std::string("(none)");
  };
  const auto psd = of(std::string_view("8BPS\0\x01\0\0\0\0\0\0\0\x03", 14));
  CHECK(extension_of(psd) == "psd");
  CHECK(sniff_openable_image_format(psd)->name == "Photoshop");
  CHECK(extension_of(of("\x89PNG\r\n\x1a\n")) == "png");
  CHECK(extension_of(Bytes{0xff, 0xd8, 0xff, 0xe0, 0, 16}) == "jpg");
  CHECK(extension_of(of("GIF89a....")) == "gif");
  CHECK(extension_of(of("%PDF-1.7 ...")) == "pdf");
  // Formats Patchy cannot open, unrecognized bytes, text and an empty file: nothing.
  CHECK(extension_of(of("qoif....")) == "(none)");
  CHECK(extension_of(of("PK\x03\x04 zip")) == "(none)");
  CHECK(extension_of(Bytes{0x01, 0x02, 0x03, 0x04, 0x05, 0x00, 0xfe, 0xfd}) == "(none)");
  CHECK(extension_of(of("just some text")) == "(none)");
  CHECK(extension_of(Bytes{}) == "(none)");

  CHECK(extensions_share_format("jpg", "JPEG"));
  CHECK(extensions_share_format("tif", "tiff"));
  CHECK(extensions_share_format("psd", "PSB"));
  CHECK(extensions_share_format("png", "apng"));
  CHECK(!extensions_share_format("psd", "bin"));
  CHECK(!extensions_share_format("png", "jpg"));
  CHECK(!extensions_share_format("psd", ""));
}

// GitHub issue 117: the reason a rejected JPEG or TIFF could not be opened, from its bytes.
void undecodable_image_diagnosis_names_the_real_reason() {
  using patchy::formats::diagnose_undecodable_image;
  using Reason = patchy::formats::UndecodableImageReason;
  using Bytes = std::vector<std::uint8_t>;
  const auto of = [](std::string_view text) {
    return Bytes(text.begin(), text.end());
  };

  CHECK(diagnose_undecodable_image(Bytes{}, "jpg").reason == Reason::EmptyFile);
  CHECK(diagnose_undecodable_image(of("<!DOCTYPE html><html><body>Not found</body></html>"), "jpg").reason ==
        Reason::TextContent);
  CHECK(diagnose_undecodable_image(of("this is not a PNG"), "png").reason == Reason::TextContent);

  // Another format's bytes behind the extension, with the rename hint only for formats Patchy opens.
  const Bytes png_head{0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a, 0, 0, 0, 13, 'I', 'H', 'D', 'R'};
  const auto png_as_jpg = diagnose_undecodable_image(png_head, "JPG");
  CHECK(png_as_jpg.reason == Reason::DifferentFormat);
  CHECK(png_as_jpg.detail == "PNG");
  CHECK(png_as_jpg.suggested_extension == "png");
  // The same bytes under their own extension are not "different": a damaged PNG stays Unknown.
  CHECK(diagnose_undecodable_image(png_head, "png").reason == Reason::Unknown);
  Bytes heic{0, 0, 0, 0x18, 'f', 't', 'y', 'p', 'h', 'e', 'i', 'c', 0, 0, 0, 0};
  const auto heic_as_jpg = diagnose_undecodable_image(heic, "jpeg");
  CHECK(heic_as_jpg.reason == Reason::DifferentFormat && heic_as_jpg.detail == "HEIC" &&
        heic_as_jpg.suggested_extension == "heic");
  CHECK(diagnose_undecodable_image(heic, "heif").reason == Reason::Unknown);
  const Bytes jp2{0, 0, 0, 0x0c, 'j', 'P', ' ', ' ', 0x0d, 0x0a, 0x87, 0x0a};
  const auto jp2_as_jpg = diagnose_undecodable_image(jp2, "jpg");
  CHECK(jp2_as_jpg.reason == Reason::DifferentFormat && jp2_as_jpg.detail == "JPEG 2000" &&
        jp2_as_jpg.suggested_extension.empty());
  // An honest name for a format Patchy has no decoder for (JPEG 2000, QOI, OpenEXR, ...).
  const auto jp2_as_jp2 = diagnose_undecodable_image(jp2, "jp2");
  CHECK(jp2_as_jp2.reason == Reason::UnsupportedFormat && jp2_as_jp2.detail == "JPEG 2000");
  const auto qoi = diagnose_undecodable_image(of("qoif...."), "qoi");
  CHECK(qoi.reason == Reason::UnsupportedFormat && qoi.detail == "QOI");
  CHECK(diagnose_undecodable_image(of("#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n"), "hdr").detail == "Radiance HDR");
  CHECK(diagnose_undecodable_image(Bytes{0x76, 0x2f, 0x31, 0x01, 2, 0, 0, 0}, "exr").reason == Reason::UnsupportedFormat);
  CHECK(diagnose_undecodable_image(of("qoif...."), "png").reason == Reason::DifferentFormat);
  // AVIF decodes in-process now (docs/avif.md): under its own name a damaged file keeps the
  // decoder's message, under another name the rename hint points at .avif.
  Bytes avif{0, 0, 0, 0x1c, 'f', 't', 'y', 'p', 'a', 'v', 'i', 'f', 0, 0, 0, 0};
  CHECK(diagnose_undecodable_image(avif, "avif").reason == Reason::Unknown);
  const auto avif_as_jpg = diagnose_undecodable_image(avif, "jpg");
  CHECK(avif_as_jpg.reason == Reason::DifferentFormat && avif_as_jpg.detail == "AVIF" &&
        avif_as_jpg.suggested_extension == "avif");
  const auto pdf_as_tif = diagnose_undecodable_image(of("%PDF-1.7 ..."), "tif");
  CHECK(pdf_as_tif.reason == Reason::DifferentFormat && pdf_as_tif.detail == "PDF");
  CHECK(diagnose_undecodable_image(of("<?xml version=\"1.0\"?><svg xmlns=\"x\"/>"), "jpg").detail == "SVG");

  // JPEG: SOI, an APP0, a frame header, then scan data with or without EOI.
  const auto jpeg = [](std::uint8_t frame_marker, std::uint8_t precision, bool with_scan, bool with_eoi) {
    Bytes b{0xff, 0xd8, 0xff, 0xe0, 0, 16, 'J', 'F', 'I', 'F', 0, 1, 1, 0, 0, 1, 0, 1, 0, 0};
    if (frame_marker != 0) {
      const Bytes sof{0xff, frame_marker, 0, 11, precision, 0, 8, 0, 8, 1, 1, 0x11, 0};
      b.insert(b.end(), sof.begin(), sof.end());
    }
    if (with_scan) {
      const Bytes sos{0xff, 0xda, 0, 8, 1, 1, 0, 0, 63, 0, 0x12, 0x34, 0x56, 0x78};
      b.insert(b.end(), sos.begin(), sos.end());
    }
    if (with_eoi) {
      b.push_back(0xff);
      b.push_back(0xd9);
    }
    return b;
  };
  CHECK(diagnose_undecodable_image(jpeg(0xc0, 8, true, true), "jpg").reason == Reason::Unknown);
  CHECK(diagnose_undecodable_image(jpeg(0xc0, 8, true, false), "jpg").reason == Reason::JpegTruncated);
  CHECK(diagnose_undecodable_image(jpeg(0xc2, 8, false, false), "jpg").reason == Reason::JpegTruncated);
  CHECK(diagnose_undecodable_image(jpeg(0, 8, true, true), "jpg").reason == Reason::JpegNoFrame);
  CHECK(diagnose_undecodable_image(jpeg(0xc3, 8, true, true), "jpg").reason == Reason::JpegLossless);
  CHECK(diagnose_undecodable_image(jpeg(0xc5, 8, true, true), "jpg").reason == Reason::JpegHierarchical);
  CHECK(diagnose_undecodable_image(jpeg(0xc1, 12, true, true), "jpg").reason == Reason::Jpeg12Bit);
  // A segment length that runs past the end of the file (a cut-short header).
  Bytes cut = jpeg(0xc0, 8, true, true);
  cut.resize(8);
  CHECK(diagnose_undecodable_image(cut, "jpg").reason == Reason::JpegTruncated);
  CHECK(diagnose_undecodable_image(Bytes{0x12, 0x34, 0x56, 0x78, 0x9a}, "jpg").reason == Reason::JpegNoFrame);

  // TIFF: a one-entry IFD carrying the Compression tag, in both byte orders and BigTIFF.
  const auto tiff = [](bool little_endian, bool big, std::uint16_t compression, std::uint16_t bits = 8,
                       std::uint16_t samples = 3, std::uint16_t sample_format = 1) {
    Bytes b;
    const auto put = [&b, little_endian](std::uint64_t value, int width) {
      for (int i = 0; i < width; ++i) {
        const int shift = little_endian ? i * 8 : (width - 1 - i) * 8;
        b.push_back(static_cast<std::uint8_t>((value >> shift) & 0xff));
      }
    };
    b.push_back(little_endian ? 'I' : 'M');
    b.push_back(little_endian ? 'I' : 'M');
    if (big) {
      put(43, 2);
      put(8, 2);
      put(0, 2);
      put(16, 8);  // first IFD
      put(4, 8);   // four entries
      const std::pair<std::uint16_t, std::uint16_t> fields[] = {
          {std::uint16_t{258}, bits}, {std::uint16_t{259}, compression}, {std::uint16_t{277}, samples},
          {std::uint16_t{339}, sample_format}};
      for (const auto& [tag, value] : fields) {
        put(tag, 2);
        put(3, 2);
        put(1, 8);
        put(value, 2);
        put(0, 6);
      }
      put(0, 8);  // next IFD
    } else {
      put(42, 2);
      put(8, 4);
      put(4, 2);
      const std::pair<std::uint16_t, std::uint16_t> fields[] = {
          {std::uint16_t{258}, bits}, {std::uint16_t{259}, compression}, {std::uint16_t{277}, samples},
          {std::uint16_t{339}, sample_format}};
      for (const auto& [tag, value] : fields) {
        put(tag, 2);
        put(3, 2);
        put(1, 4);
        put(value, 2);
        put(0, 2);
      }
      put(0, 4);
    }
    return b;
  };
  // Compression 7 is decoded by Patchy itself now (src/ui/tiff_jpeg_reader.cpp): a failure with it
  // means the sample layout, so plain 8-bit says Unknown and 12-bit names the depth.
  CHECK(diagnose_undecodable_image(tiff(true, false, 7), "tif").reason == Reason::Unknown);
  CHECK(diagnose_undecodable_image(tiff(false, false, 6), "tiff").reason == Reason::TiffJpegCompression);
  CHECK(diagnose_undecodable_image(tiff(true, false, 5), "tif").reason == Reason::Unknown);
  CHECK(diagnose_undecodable_image(tiff(false, false, 32773), "tif").reason == Reason::Unknown);
  const auto lzma = diagnose_undecodable_image(tiff(true, false, 34925), "tif");
  CHECK(lzma.reason == Reason::TiffUnsupportedCompression && lzma.detail == "LZMA");
  const auto webp = diagnose_undecodable_image(tiff(true, true, 50001), "tif");
  CHECK(webp.reason == Reason::TiffUnsupportedCompression && webp.detail == "WebP");
  const auto big_lzw = diagnose_undecodable_image(tiff(false, true, 5), "tif");
  CHECK(big_lzw.reason == Reason::Unknown);
  const auto odd = diagnose_undecodable_image(tiff(true, false, 60000), "tif");
  CHECK(odd.reason == Reason::TiffUnsupportedCompression && odd.detail == "code 60000");
  // Sample layouts Qt's reader rejects (measured on the imagecodecs-samples corpus).
  const auto bits12 = diagnose_undecodable_image(tiff(true, false, 1, 12, 1), "tif");
  CHECK(bits12.reason == Reason::TiffUnsupportedBitDepth && bits12.detail == "12");
  const auto bits64 = diagnose_undecodable_image(tiff(false, true, 1, 64, 3, 3), "tif");
  CHECK(bits64.reason == Reason::TiffUnsupportedBitDepth && bits64.detail == "64");
  CHECK(diagnose_undecodable_image(tiff(true, false, 1, 32, 3, 1), "tif").reason == Reason::TiffIntegerSamples);
  CHECK(diagnose_undecodable_image(tiff(true, false, 1, 32, 3, 3), "tif").reason == Reason::Unknown);
  const auto packed = diagnose_undecodable_image(tiff(true, false, 1, 4, 3), "tif");
  CHECK(packed.reason == Reason::TiffPackedColor && packed.detail == "4");
  CHECK(diagnose_undecodable_image(tiff(true, false, 1, 4, 1), "tif").reason == Reason::Unknown);
  CHECK(diagnose_undecodable_image(tiff(true, false, 1, 16, 4, 2), "tif").reason == Reason::Unknown);
  const auto jxr = diagnose_undecodable_image(tiff(true, false, 34934), "tif");
  CHECK(jxr.reason == Reason::TiffUnsupportedCompression && jxr.detail == "JPEG XR");
  CHECK(diagnose_undecodable_image(tiff(true, false, 7, 12), "tif").reason == Reason::TiffUnsupportedBitDepth);
  Bytes short_tiff = tiff(true, false, 7);
  short_tiff.resize(12);
  CHECK(diagnose_undecodable_image(short_tiff, "tif").reason == Reason::TiffUnreadable);
  CHECK(diagnose_undecodable_image(Bytes{0x12, 0x34, 0x56, 0x78, 0x9a}, "tif").reason == Reason::TiffUnreadable);
}

// Radiance HDR, OpenEXR and JPEG 2000 round trips through Patchy's own writers (docs/hdr-formats.md).
patchy::Document float_document_for_hdr_tests(std::int32_t width, std::int32_t height) {
  using patchy::BitDepth;
  const auto format = patchy::with_bit_depth(patchy::PixelFormat::rgba8(), BitDepth::Float32);
  patchy::Document document(width, height, format);
  document.color_state().bit_depth = BitDepth::Float32;
  patchy::PixelBuffer pixels(width, height, format);
  for (std::int32_t y = 0; y < height; ++y) {
    auto* row = reinterpret_cast<float*>(pixels.row(y).data());
    for (std::int32_t x = 0; x < width; ++x) {
      // A range of linear values: dark, mid, bright and well over 1.0 (HDR), with runs.
      const float base = static_cast<float>((x / 4) * 4 + y) * 0.37F;
      row[x * 4 + 0] = base;
      row[x * 4 + 1] = base * 0.5F + 0.01F;
      row[x * 4 + 2] = (x % 7 == 0) ? 12.5F : base * 0.1F;
      row[x * 4 + 3] = 1.0F;
    }
  }
  document.add_pixel_layer("Background", std::move(pixels));
  return document;
}

void check_float_round_trip(const patchy::Document& original, const patchy::Document& reopened,
                            float relative_tolerance) {
  CHECK(reopened.width() == original.width() && reopened.height() == original.height());
  CHECK(std::as_const(reopened).color_state().bit_depth == patchy::BitDepth::Float32);
  CHECK(std::as_const(reopened).layers().size() == 1);
  if (std::as_const(reopened).layers().empty() || reopened.width() != original.width()) {
    return;
  }
  const auto& a = std::as_const(original).layers().front().pixels();
  const auto& b = std::as_const(reopened).layers().front().pixels();
  float worst = 0.0F;
  for (std::int32_t y = 0; y < original.height(); ++y) {
    const auto* ra = reinterpret_cast<const float*>(a.row(y).data());
    const auto* rb = reinterpret_cast<const float*>(b.row(y).data());
    for (std::int32_t x = 0; x < original.width(); ++x) {
      // Relative to the pixel's largest channel (RGBE's shared exponent sets the precision).
      const float largest = std::max({1e-3F, std::abs(ra[x * 4]), std::abs(ra[x * 4 + 1]), std::abs(ra[x * 4 + 2])});
      for (int c = 0; c < 4; ++c) {
        worst = std::max(worst, std::abs(ra[x * 4 + c] - rb[x * 4 + c]) / largest);
      }
    }
  }
  CHECK(worst <= relative_tolerance);
}

void hdr_rgbe_round_trips_linear_values() {
  std::uint8_t rgbe[4];
  const float rgb[3] = {0.5F, 2.0F, 100.0F};
  patchy::hdr::float_to_rgbe(rgb, rgbe);
  float back[3];
  patchy::hdr::rgbe_to_float(rgbe, back);
  // RGBE shares one exponent: each channel's error is relative to the largest channel.
  for (int c = 0; c < 3; ++c) {
    CHECK(std::abs(back[c] - rgb[c]) / 100.0F < 0.005F);
  }
  const float zero[3] = {0.0F, 0.0F, 0.0F};
  patchy::hdr::float_to_rgbe(zero, rgbe);
  CHECK(rgbe[3] == 0);

  // Run-length scanlines (width 24) and flat ones (width 4) both read back.
  for (const auto width : {24, 4}) {
    const auto document = float_document_for_hdr_tests(width, 5);
    const auto bytes = patchy::hdr::write_hdr(document);
    CHECK(patchy::hdr::sniff(bytes));
    const auto reopened = patchy::hdr::read_hdr(bytes);
    check_float_round_trip(document, reopened.document, 0.01F);
  }
  // A bottom-up file flips its rows.
  {
    const auto document = float_document_for_hdr_tests(4, 3);
    auto bytes = patchy::hdr::write_hdr(document);
    const std::string header(bytes.begin(), bytes.end());
    const auto at = header.find("-Y 3");
    CHECK(at != std::string::npos);
    bytes[at] = '+';
    const auto flipped = patchy::hdr::read_hdr(bytes);
    const auto* top =
        reinterpret_cast<const float*>(std::as_const(flipped.document).layers().front().pixels().row(0).data());
    const auto* original_bottom =
        reinterpret_cast<const float*>(std::as_const(document).layers().front().pixels().row(2).data());
    const float largest = std::max({1e-3F, original_bottom[0], original_bottom[1], original_bottom[2]});
    CHECK(std::abs(top[0] - original_bottom[0]) / largest < 0.01F);
  }
  bool rejected = false;
  try {
    const std::string truncated = "#?RADIANCE\n";
    (void)patchy::hdr::read_hdr(std::vector<std::uint8_t>(truncated.begin(), truncated.end()));
  } catch (const std::exception&) {
    rejected = true;
  }
  CHECK(rejected);
}

void exr_round_trips_float_rgba() {
  const auto document = float_document_for_hdr_tests(13, 7);
  const auto bytes = patchy::exr::write_exr(document);
  CHECK(patchy::exr::sniff(bytes));
  const auto reopened = patchy::exr::read_exr(bytes);
  check_float_round_trip(document, reopened.document, 1e-6F);
  bool rejected = false;
  try {
    (void)patchy::exr::read_exr(std::vector<std::uint8_t>{0x76, 0x2f, 0x31, 0x01, 2, 0, 0, 0});
  } catch (const std::exception&) {
    rejected = true;
  }
  CHECK(rejected);
}

void jp2_round_trips_losslessly_at_8_and_16_bits() {
  using patchy::BitDepth;
  for (const auto depth : {BitDepth::UInt8, BitDepth::UInt16}) {
    const auto format = patchy::with_bit_depth(patchy::PixelFormat::rgba8(), depth);
    patchy::Document document(19, 11, format);
    document.color_state().bit_depth = depth;
    patchy::PixelBuffer pixels(19, 11, format);
    for (std::int32_t y = 0; y < 11; ++y) {
      auto row = pixels.row(y);
      for (std::int32_t x = 0; x < 19; ++x) {
        const int values[4] = {x * 13 % 256, y * 23 % 256, (x + y) * 7 % 256, x < 10 ? 255 : 128};
        for (int c = 0; c < 4; ++c) {
          if (depth == BitDepth::UInt8) {
            row[x * 4 + c] = static_cast<std::uint8_t>(values[c]);
          } else {
            reinterpret_cast<std::uint16_t*>(row.data())[x * 4 + c] = static_cast<std::uint16_t>(values[c] * 257);
          }
        }
      }
    }
    const patchy::PixelBuffer expected = pixels;
    document.add_pixel_layer("Background", std::move(pixels));
    const auto bytes = patchy::jp2::write_jp2(document);
    CHECK(patchy::jp2::sniff(bytes));
    const auto reopened = patchy::jp2::read_jp2(bytes);
    CHECK(reopened.document.width() == 19 && reopened.document.height() == 11);
    CHECK(std::as_const(reopened.document).color_state().bit_depth == depth);
    const auto& back = std::as_const(reopened.document).layers().front().pixels();
    CHECK(back.data().size() == expected.data().size());
    if (back.data().size() == expected.data().size()) {
      CHECK(std::equal(back.data().begin(), back.data().end(), expected.data().begin()));
    }
  }
  bool rejected = false;
  try {
    (void)patchy::jp2::read_jp2(std::vector<std::uint8_t>{0xff, 0x4f, 0xff, 0x51, 0, 0});
  } catch (const std::exception&) {
    rejected = true;
  }
  CHECK(rejected);
}


// AVIF through the vendored libavif + dav1d (docs/avif.md). An 8x8 lossless 4:4:4 file written
// by Pillow 12.1 (libavif/aom): red, green over blue, half-transparent white, in 511 bytes.
constexpr std::uint8_t kQuadrantsAvif[] = {
    0x00, 0x00, 0x00, 0x20, 0x66, 0x74, 0x79, 0x70, 0x61, 0x76, 0x69, 0x66, 0x00, 0x00, 0x00, 0x00,
    0x61, 0x76, 0x69, 0x66, 0x6d, 0x69, 0x66, 0x31, 0x6d, 0x69, 0x61, 0x66, 0x4d, 0x41, 0x31, 0x41,
    0x00, 0x00, 0x01, 0x86, 0x6d, 0x65, 0x74, 0x61, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x21,
    0x68, 0x64, 0x6c, 0x72, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x70, 0x69, 0x63, 0x74,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x0e, 0x70, 0x69, 0x74, 0x6d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x2c, 0x69,
    0x6c, 0x6f, 0x63, 0x00, 0x00, 0x00, 0x00, 0x44, 0x00, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x01, 0xc5, 0x00, 0x00, 0x00, 0x3a, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x00, 0x01, 0xae, 0x00, 0x00, 0x00, 0x17, 0x00, 0x00, 0x00, 0x42, 0x69, 0x69, 0x6e, 0x66, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x1a, 0x69, 0x6e, 0x66, 0x65, 0x02, 0x00, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x00, 0x61, 0x76, 0x30, 0x31, 0x43, 0x6f, 0x6c, 0x6f, 0x72, 0x00, 0x00,
    0x00, 0x00, 0x1a, 0x69, 0x6e, 0x66, 0x65, 0x02, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x61,
    0x76, 0x30, 0x31, 0x41, 0x6c, 0x70, 0x68, 0x61, 0x00, 0x00, 0x00, 0x00, 0x1a, 0x69, 0x72, 0x65,
    0x66, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0e, 0x61, 0x75, 0x78, 0x6c, 0x00, 0x02, 0x00,
    0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0xc3, 0x69, 0x70, 0x72, 0x70, 0x00, 0x00, 0x00, 0x9d, 0x69,
    0x70, 0x63, 0x6f, 0x00, 0x00, 0x00, 0x14, 0x69, 0x73, 0x70, 0x65, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x10, 0x70, 0x69, 0x78, 0x69, 0x00,
    0x00, 0x00, 0x00, 0x03, 0x08, 0x08, 0x08, 0x00, 0x00, 0x00, 0x0c, 0x61, 0x76, 0x31, 0x43, 0x81,
    0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x13, 0x63, 0x6f, 0x6c, 0x72, 0x6e, 0x63, 0x6c, 0x78, 0x00,
    0x01, 0x00, 0x0d, 0x00, 0x06, 0x80, 0x00, 0x00, 0x00, 0x0e, 0x70, 0x69, 0x78, 0x69, 0x00, 0x00,
    0x00, 0x00, 0x01, 0x08, 0x00, 0x00, 0x00, 0x0c, 0x61, 0x76, 0x31, 0x43, 0x81, 0x00, 0x1c, 0x00,
    0x00, 0x00, 0x00, 0x38, 0x61, 0x75, 0x78, 0x43, 0x00, 0x00, 0x00, 0x00, 0x75, 0x72, 0x6e, 0x3a,
    0x6d, 0x70, 0x65, 0x67, 0x3a, 0x6d, 0x70, 0x65, 0x67, 0x42, 0x3a, 0x63, 0x69, 0x63, 0x70, 0x3a,
    0x73, 0x79, 0x73, 0x74, 0x65, 0x6d, 0x73, 0x3a, 0x61, 0x75, 0x78, 0x69, 0x6c, 0x69, 0x61, 0x72,
    0x79, 0x3a, 0x61, 0x6c, 0x70, 0x68, 0x61, 0x00, 0x00, 0x00, 0x00, 0x1e, 0x69, 0x70, 0x6d, 0x61,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01, 0x04, 0x01, 0x02, 0x83, 0x04, 0x00,
    0x02, 0x04, 0x01, 0x05, 0x86, 0x07, 0x00, 0x00, 0x00, 0x59, 0x6d, 0x64, 0x61, 0x74, 0x12, 0x00,
    0x0a, 0x05, 0x18, 0x08, 0xbf, 0x61, 0x50, 0x32, 0x0c, 0x10, 0x00, 0x00, 0x18, 0xde, 0xa2, 0x95,
    0x3f, 0xbe, 0x84, 0xa5, 0x28, 0x12, 0x00, 0x0a, 0x05, 0x38, 0x08, 0xbf, 0x60, 0x90, 0x32, 0x2f,
    0x10, 0x00, 0x00, 0x00, 0x0f, 0xfa, 0x40, 0x50, 0x52, 0xae, 0x04, 0x2e, 0x42, 0xd8, 0x40, 0x9c,
    0xa1, 0x59, 0xe0, 0x20, 0x95, 0xf8, 0x2c, 0x68, 0x1d, 0xb6, 0x78, 0x98, 0xfe, 0x14, 0x34, 0x3a,
    0x1f, 0x56, 0x47, 0xec, 0x18, 0x5e, 0xeb, 0xe0, 0xf3, 0x88, 0x43, 0x15, 0x5f, 0xf2, 0x80,
};

// Why this build cannot decode AVIF, or nothing when it can.
[[nodiscard]] std::optional<std::string> avif_decoder_missing_reason() {
  if (!patchy::avif::is_available()) {
    return "this build has no AVIF decoder";
  }
  return std::nullopt;
}

void avif_decodes_synthetic_still_and_rejects_damage() {
  CHECK(patchy::avif::is_avif_extension("avif"));
  CHECK(patchy::avif::is_avif_extension(".AVIFS"));
  CHECK(!patchy::avif::is_avif_extension("heic"));
  const std::vector<std::uint8_t> bytes(std::begin(kQuadrantsAvif), std::end(kQuadrantsAvif));
  CHECK(patchy::avif::sniff(bytes));
  // The HEVC HEIF sniff and the AVIF sniff never claim each other's files.
  CHECK(!patchy::heif::sniff(bytes));
  const std::vector<std::uint8_t> heic{0,   0,   0,   0x18, 'f', 't', 'y', 'p', 'h', 'e', 'i', 'c',
                                       0,   0,   0,   0,    'm', 'i', 'f', '1', 'h', 'e', 'i', 'c'};
  CHECK(!patchy::avif::sniff(heic));
  const auto* handler = patchy::builtin_format_registry().find_by_extension("avif");
  CHECK(handler != nullptr && handler->identifier == "patchy.formats.avif" && handler->can_write());
  if (const auto reason = avif_decoder_missing_reason()) {
    std::cout << "  note: decode not checked: " << *reason << '\n';
    return;
  }
  const auto result = patchy::avif::read_avif_detailed(bytes);
  const auto& document = result.read.document;
  CHECK(document.width() == 8 && document.height() == 8);
  CHECK(std::as_const(document).color_state().bit_depth == patchy::BitDepth::UInt8);
  CHECK(std::as_const(document).layers().size() == 1);
  CHECK(result.frame_count == 1);
  CHECK(!result.pixels_in_source_space);  // no ICC profile in the file
  const auto& pixels = std::as_const(document).layers().front().pixels();
  const auto close_to = [&](std::int32_t x, std::int32_t y, int r, int g, int b, int a) {
    const auto px = pixels.pixel(x, y);
    return std::abs(static_cast<int>(px[0]) - r) <= 4 && std::abs(static_cast<int>(px[1]) - g) <= 4 &&
           std::abs(static_cast<int>(px[2]) - b) <= 4 && std::abs(static_cast<int>(px[3]) - a) <= 1;
  };
  CHECK(close_to(1, 1, 255, 0, 0, 255));
  CHECK(close_to(6, 1, 0, 255, 0, 255));
  CHECK(close_to(1, 6, 0, 0, 255, 255));
  CHECK(close_to(6, 6, 255, 255, 255, 128));
  // The registry reaches the same reader.
  CHECK(handler->read(bytes).document.width() == 8);

  bool rejected = false;
  try {
    const std::vector<std::uint8_t> truncated(bytes.begin(), bytes.begin() + 200);
    (void)patchy::avif::read_avif(truncated);
  } catch (const std::exception&) {
    rejected = true;
  }
  CHECK(rejected);
  rejected = false;
  try {
    (void)patchy::avif::read_avif(std::vector<std::uint8_t>{0x89, 'P', 'N', 'G', 0, 0, 0, 0});
  } catch (const std::exception& error) {
    rejected = std::string(error.what()).find("not an AVIF") != std::string::npos;
  }
  CHECK(rejected);
}

void avif_imports_image_sequence_frames_as_layers() {
  if (const auto reason = avif_decoder_missing_reason()) {
    patchy::test::skip(*reason);
  }
  // Three 4x4 frames (100, 200 and 300 ms) written by Pillow as an avis sequence.
  const auto bytes = patchy::test::read_binary_file(patchy::test::committed_format_fixture_path("avif", "frames.avif"));
  CHECK(patchy::avif::sniff(bytes));
  int names = 0;
  patchy::avif::ReadOptions options;
  options.frame_name = [&](int index, std::uint32_t delay_ms) {
    ++names;
    return "F" + std::to_string(index) + "/" + std::to_string(delay_ms);
  };
  const auto result = patchy::avif::read_avif_detailed(bytes, options);
  const auto& document = result.read.document;
  CHECK(result.frame_count == 3);
  CHECK(names == 3);
  const auto& layers = std::as_const(document).layers();
  CHECK(layers.size() == 3);
  if (layers.size() == 3) {
    // Frame 1 on top (the WebP and JPEG XL convention), delays in the names.
    CHECK(layers.back().name() == "F1/100");
    CHECK(layers[1].name() == "F2/200");
    CHECK(layers.front().name() == "F3/300");
    const auto top = layers.back().pixels().pixel(2, 2);
    const auto bottom = layers.front().pixels().pixel(2, 2);
    CHECK(top[0] < 8 && std::abs(static_cast<int>(top[2]) - 200) <= 4);        // frame 1: (0, 50, 200)
    CHECK(std::abs(static_cast<int>(bottom[0]) - 200) <= 4 && bottom[2] < 48);  // frame 3: (200, 50, 40)
  }
  const auto& values = std::as_const(document).metadata().values;
  CHECK(values.find(patchy::avif::kLoopCountMetadata) != values.end());
}

void avif_round_trips_losslessly_and_lossy_through_libaom() {
  if (const auto reason = avif_decoder_missing_reason()) {
    patchy::test::skip(*reason);
  }
  const auto* handler = patchy::builtin_format_registry().find_by_extension("avif");
  CHECK(handler != nullptr && handler->can_write());
  using patchy::BitDepth;
  const auto make = [](BitDepth depth, bool translucent) {
    const auto format = patchy::with_bit_depth(patchy::PixelFormat::rgba8(), depth);
    patchy::Document document(21, 13, format);
    document.color_state().bit_depth = depth;
    patchy::PixelBuffer pixels(21, 13, format);
    for (std::int32_t y = 0; y < 13; ++y) {
      auto row = pixels.row(y);
      for (std::int32_t x = 0; x < 21; ++x) {
        const int values[4] = {x * 12 % 256, y * 19 % 256, (x * y) % 256, translucent && x >= 10 ? 96 + y : 255};
        for (int c = 0; c < 4; ++c) {
          if (depth == BitDepth::UInt8) {
            row[x * 4 + c] = static_cast<std::uint8_t>(values[c]);
          } else {
            reinterpret_cast<std::uint16_t*>(row.data())[x * 4 + c] = static_cast<std::uint16_t>(values[c] * 257);
          }
        }
      }
    }
    document.add_pixel_layer("Background", std::move(pixels));
    return document;
  };
  // 8-bit lossless: the identity matrix and full range give the exact pixels back, alpha too.
  {
    const auto document = make(BitDepth::UInt8, true);
    patchy::avif::WriteOptions options;
    options.lossless = true;
    options.speed = 10;
    const auto bytes = patchy::avif::write_avif(document, options);
    CHECK(patchy::avif::sniff(bytes));
    const auto reopened = patchy::avif::read_avif(bytes);
    CHECK(reopened.document.width() == 21 && reopened.document.height() == 13);
    const auto& back = std::as_const(reopened.document).layers().front().pixels();
    const auto& expected = std::as_const(document).layers().front().pixels();
    CHECK(back.data().size() == expected.data().size());
    if (back.data().size() == expected.data().size()) {
      CHECK(std::equal(back.data().begin(), back.data().end(), expected.data().begin()));
    }
  }
  // An opaque 8-bit lossy save: no alpha plane, colors within the codec's rounding.
  {
    const auto document = make(BitDepth::UInt8, false);
    patchy::avif::WriteOptions options;
    options.quality = 90;
    options.speed = 10;
    const auto bytes = patchy::avif::write_avif(document, options);
    const auto reopened = patchy::avif::read_avif(bytes);
    const auto& back = std::as_const(reopened.document).layers().front().pixels();
    const auto& expected = std::as_const(document).layers().front().pixels();
    int worst = 0;
    for (std::int32_t y = 0; y < 13; ++y) {
      for (std::int32_t x = 0; x < 21; ++x) {
        CHECK(back.pixel(x, y)[3] == 255);
        for (int c = 0; c < 3; ++c) {
          worst = std::max(worst, std::abs(static_cast<int>(back.pixel(x, y)[c]) - static_cast<int>(expected.pixel(x, y)[c])));
        }
      }
    }
    CHECK(worst <= 24);
  }
  // 16-bit documents write 12-bit samples and reopen as 16-bit (deep editing on) within the
  // 12-bit quantization step.
  if (patchy::deep_editing_enabled()) {
    const auto document = make(BitDepth::UInt16, true);
    patchy::avif::WriteOptions options;
    options.lossless = true;
    options.speed = 10;
    const auto bytes = patchy::avif::write_avif(document, options);
    const auto reopened = patchy::avif::read_avif(bytes);
    CHECK(std::as_const(reopened.document).color_state().bit_depth == BitDepth::UInt16);
    const auto* back = reinterpret_cast<const std::uint16_t*>(std::as_const(reopened.document).layers().front().pixels().data().data());
    const auto* expected = reinterpret_cast<const std::uint16_t*>(std::as_const(document).layers().front().pixels().data().data());
    int worst = 0;
    for (std::size_t i = 0; i < 21U * 13U * 4U; ++i) {
      worst = std::max(worst, std::abs(static_cast<int>(back[i]) - static_cast<int>(expected[i])));
    }
    CHECK(worst <= 16);
  }
  // The registry writer uses the defaults and the result opens.
  CHECK(handler->read(handler->write(make(BitDepth::UInt8, false))).document.width() == 21);
}

void avif_decodes_corpus_samples_if_available() {
  if (const auto reason = avif_decoder_missing_reason()) {
    patchy::test::skip(*reason);
  }
  // cgohlke/imagecodecs-samples (CC BY 4.0, untracked local fixtures): gray and RGB, with and
  // without alpha, 8 and 16-bit, stills and sequences.
  const auto directory = patchy::test::source_root_path() / "local-test-fixtures" / "imagecodecs-samples" / "images";
  patchy::test::skip_unless_exists(directory / "rgb_u2.avif");
  const bool deep = patchy::deep_editing_enabled();
  int decoded = 0;
  for (const auto& entry : std::filesystem::directory_iterator(directory)) {
    const auto name = entry.path().filename().string();
    if (!name.ends_with(".avif")) {
      continue;
    }
    const auto result = patchy::avif::read_avif_detailed(patchy::test::read_binary_file(entry.path()));
    const auto& document = result.read.document;
    CHECK(document.width() > 0 && document.height() > 0);
    const bool sixteen = name.find("_u2") != std::string::npos;
    CHECK(std::as_const(document).color_state().bit_depth ==
          (sixteen && deep ? patchy::BitDepth::UInt16 : patchy::BitDepth::UInt8));
    const bool frames = name.find("_frames") != std::string::npos;
    CHECK(frames ? result.frame_count > 1 : result.frame_count == 1);
    CHECK(std::as_const(document).layers().size() == result.frame_count);
    ++decoded;
  }
  CHECK(decoded == 12);
}

}  // namespace

std::vector<patchy::test::TestCase> flat_formats_misc_tests() {
  return {
      {"ico_reads_multi_size_and_round_trips_named_layers", ico_reads_multi_size_and_round_trips_named_layers},
      {"ico_transparent_pixels_round_trip_and_mask_fallback_applies",
       ico_transparent_pixels_round_trip_and_mask_fallback_applies},
      {"cur_round_trips_hotspot", cur_round_trips_hotspot},
      {"ico_writer_png_entry_for_256", ico_writer_png_entry_for_256},
      {"ico_reads_real_world_samples", ico_reads_real_world_samples},
      {"tga_reads_types_1_2_9_10_and_origin_flags", tga_reads_types_1_2_9_10_and_origin_flags},
      {"tga_writer_rle32_round_trips", tga_writer_rle32_round_trips},
      {"tga_palette_mode_writes_indexed", tga_palette_mode_writes_indexed},
      {"tga_reads_real_world_samples", tga_reads_real_world_samples},
      {"gif_lzw_round_trips_through_reference_decoder", gif_lzw_round_trips_through_reference_decoder},
      {"webp_animation_lossless_timing_loops_and_limits", webp_animation_lossless_timing_loops_and_limits},
      {"webp_animation_millisecond_tokens_preserve_gif_rounding", webp_animation_millisecond_tokens_preserve_gif_rounding},
      {"gif_encoder_bytes_are_stable", gif_encoder_bytes_are_stable},
      {"gif_document_write_quantizes_and_round_trips", gif_document_write_quantizes_and_round_trips},
      {"gif_animation_encodes_frames_delays_and_loop", gif_animation_encodes_frames_delays_and_loop},
      {"gif_layer_name_delay_token_round_trips", gif_layer_name_delay_token_round_trips},
      {"sniff_openable_image_format_names_what_patchy_opens", sniff_openable_image_format_names_what_patchy_opens},
      {"undecodable_image_diagnosis_names_the_real_reason", undecodable_image_diagnosis_names_the_real_reason},
      {"hdr_rgbe_round_trips_linear_values", hdr_rgbe_round_trips_linear_values},
      {"exr_round_trips_float_rgba", exr_round_trips_float_rgba},
      {"jp2_round_trips_losslessly_at_8_and_16_bits", jp2_round_trips_losslessly_at_8_and_16_bits},
      {"avif_decodes_synthetic_still_and_rejects_damage", avif_decodes_synthetic_still_and_rejects_damage},
      {"avif_imports_image_sequence_frames_as_layers", avif_imports_image_sequence_frames_as_layers},
      {"avif_decodes_corpus_samples_if_available", avif_decodes_corpus_samples_if_available},
      {"avif_round_trips_losslessly_and_lossy_through_libaom", avif_round_trips_losslessly_and_lossy_through_libaom},
      {"aseprite_imports_layers_groups_and_palette", aseprite_imports_layers_groups_and_palette},
      {"aseprite_indexed_transparent_index", aseprite_indexed_transparent_index},
      {"aseprite_rejects_adobe_swatch_ase", aseprite_rejects_adobe_swatch_ase},
      {"aseprite_write_read_round_trips_layers_and_palette", aseprite_write_read_round_trips_layers_and_palette},
      {"aseprite_out_of_range_child_level_lands_at_root", aseprite_out_of_range_child_level_lands_at_root},
      {"aseprite_rejects_cel_chunk_shorter_than_its_header", aseprite_rejects_cel_chunk_shorter_than_its_header},
      {"aseprite_blend_modes_match_aseprite_render", aseprite_blend_modes_match_aseprite_render},
      {"psd_round_trips_new_blend_modes", psd_round_trips_new_blend_modes},
      {"pcx_reads_8bit_indexed_and_24bit_rle", pcx_reads_8bit_indexed_and_24bit_rle},
      {"pcx_write_read_round_trips", pcx_write_read_round_trips},
      {"ilbm_reads_planar_masked_body", ilbm_reads_planar_masked_body},
      {"ilbm_rejects_ham_with_message", ilbm_rejects_ham_with_message},
      {"ilbm_write_read_round_trips_indexed", ilbm_write_read_round_trips_indexed},
  };
}
