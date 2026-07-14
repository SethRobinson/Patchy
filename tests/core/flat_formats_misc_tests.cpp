#include "color/color_management.hpp"
#include "core/adjustment_layer.hpp"
#include "core/blend_math.hpp"
#include "core/document.hpp"
#include "core/layer_metadata.hpp"
#include "core/layer_tree.hpp"
#include "core/gradient_presets.hpp"
#include "filters/filter_engine.hpp"
#include "filters/filter_registry.hpp"
#include "filters/smart_filter_recipe_mapping.hpp"
#include "filters/smart_filter_renderer.hpp"
#include "formats/acv_curves_io.hpp"
#include "formats/bmp_document_io.hpp"
#include "formats/aseprite_document_io.hpp"
#include "formats/document_flatten.hpp"
#include "formats/format_registry.hpp"
#include "formats/gif_document_io.hpp"
#include "formats/heif_document_io.hpp"
#include "formats/ico_document_io.hpp"
#include "formats/ilbm_document_io.hpp"
#include "formats/image_density_probe.hpp"
#include "formats/palette_io.hpp"
#include "formats/pcx_document_io.hpp"
#include "formats/raw_document_io.hpp"
#include "formats/raw_tone.hpp"
#include "formats/raw_white_balance.hpp"
#include "formats/tga_document_io.hpp"
#include "plugins/legacy_photoshop_adapter.hpp"
#include "plugins/plugin_host.hpp"
#include "psd/abr_reader.hpp"
#include "psd/grd_io.hpp"
#include "psd/asl_io.hpp"
#include "psd/pat_reader.hpp"
#include "psd/psd_binary.hpp"
#include "psd/psd_descriptor.hpp"
#include "psd/psd_filter_effects.hpp"
#include "psd/psd_layer_effects.hpp"
#include "psd/psd_patterns.hpp"
#include "psd/psd_smart_objects.hpp"
#include "core/text_warp.hpp"
#include "core/warp_mesh.hpp"
#include "psd/psd_document_io.hpp"
#include "core/contour_presets.hpp"
#include "core/magnetic_lasso.hpp"
#include "core/palette.hpp"
#include "core/palette_presets.hpp"
#include "core/pattern_presets.hpp"
#include "core/style_contour.hpp"
#include "core/style_presets.hpp"
#include "core/pixel_tools.hpp"
#include "core/quick_select.hpp"
#include "render/compositor.hpp"
#include "render/layer_compositor.hpp"
#include "render/tile_cache.hpp"
#include "support/string_utils.hpp"
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
struct DecodedGif {
  std::int32_t width{0};
  std::int32_t height{0};
  std::vector<patchy::RgbColor> palette;
  int transparent_index{-1};
  std::vector<std::uint8_t> indexes;
};

DecodedGif reference_decode_gif(std::span<const std::uint8_t> bytes) {
  DecodedGif result;
  std::size_t pos = 0;
  const auto u8 = [&] { return bytes[pos++]; };
  const auto u16 = [&] {
    const auto value = static_cast<std::uint16_t>(bytes[pos] | (bytes[pos + 1] << 8U));
    pos += 2;
    return value;
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
  const auto table_entries = std::size_t{1} << ((packed & 0x07U) + 1U);
  for (std::size_t i = 0; i < table_entries; ++i) {
    const auto r = u8();
    const auto g = u8();
    const auto b = u8();
    result.palette.push_back(patchy::RgbColor{r, g, b});
  }

  std::vector<std::uint8_t> data;
  int min_code_size = 0;
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
        u16();  // delay
        const auto transparent = u8();
        if ((gce_packed & 0x01U) != 0) {
          result.transparent_index = transparent;
        }
        CHECK(u8() == 0);
      } else {
        for (auto size = u8(); size != 0; size = u8()) {
          pos += size;
        }
      }
      continue;
    }
    CHECK(block == 0x2c);
    u16();
    u16();
    CHECK(u16() == result.width);
    CHECK(u16() == result.height);
    CHECK(u8() == 0);  // no local table, not interlaced
    min_code_size = u8();
    for (auto size = u8(); size != 0; size = u8()) {
      data.insert(data.end(), bytes.begin() + static_cast<std::ptrdiff_t>(pos),
                  bytes.begin() + static_cast<std::ptrdiff_t>(pos + size));
      pos += size;
    }
  }

  // LZW decode.
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
      result.indexes.push_back(static_cast<std::uint8_t>(code));
      prev = static_cast<std::int64_t>(code);
      continue;
    }
    if (code < next) {
      expand(code, result.indexes);
      prefix[next] = static_cast<std::uint16_t>(prev);
      suffix[next] = first_symbol(code);
    } else {
      CHECK(code == next);
      prefix[next] = static_cast<std::uint16_t>(prev);
      suffix[next] = first_symbol(static_cast<std::uint32_t>(prev));
      expand(code, result.indexes);
    }
    ++next;
    if (next == (1U << width_bits) && width_bits < 12) {
      ++width_bits;
    }
    prev = static_cast<std::int64_t>(code);
  }
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
  const std::array<patchy::BlendMode, 6> modes = {patchy::BlendMode::Exclusion,   patchy::BlendMode::Hue,
                                                  patchy::BlendMode::Color,       patchy::BlendMode::LinearDodge,
                                                  patchy::BlendMode::Subtract,    patchy::BlendMode::Divide};
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
      {"gif_encoder_bytes_are_stable", gif_encoder_bytes_are_stable},
      {"gif_document_write_quantizes_and_round_trips", gif_document_write_quantizes_and_round_trips},
      {"aseprite_imports_layers_groups_and_palette", aseprite_imports_layers_groups_and_palette},
      {"aseprite_indexed_transparent_index", aseprite_indexed_transparent_index},
      {"aseprite_rejects_adobe_swatch_ase", aseprite_rejects_adobe_swatch_ase},
      {"aseprite_write_read_round_trips_layers_and_palette", aseprite_write_read_round_trips_layers_and_palette},
      {"aseprite_blend_modes_match_aseprite_render", aseprite_blend_modes_match_aseprite_render},
      {"psd_round_trips_new_blend_modes", psd_round_trips_new_blend_modes},
      {"pcx_reads_8bit_indexed_and_24bit_rle", pcx_reads_8bit_indexed_and_24bit_rle},
      {"pcx_write_read_round_trips", pcx_write_read_round_trips},
      {"ilbm_reads_planar_masked_body", ilbm_reads_planar_masked_body},
      {"ilbm_rejects_ham_with_message", ilbm_rejects_ham_with_message},
      {"ilbm_write_read_round_trips_indexed", ilbm_write_read_round_trips_indexed},
  };
}
