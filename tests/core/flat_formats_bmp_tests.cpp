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

#include "test_groups.hpp"

namespace {

void append_u16_le(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value & 0xffU));
  bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
}

void append_u32_le(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value & 0xffU));
  bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
  bytes.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xffU));
  bytes.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xffU));
}

void append_i32_le(std::vector<std::uint8_t>& bytes, std::int32_t value) {
  append_u32_le(bytes, static_cast<std::uint32_t>(value));
}

std::uint16_t read_u16_le_at(std::span<const std::uint8_t> bytes, std::size_t offset) {
  CHECK(offset + 2U <= bytes.size());
  return static_cast<std::uint16_t>(bytes[offset]) |
         static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U);
}

std::uint32_t read_u32_le_at(std::span<const std::uint8_t> bytes, std::size_t offset) {
  CHECK(offset + 4U <= bytes.size());
  return static_cast<std::uint32_t>(bytes[offset]) |
         (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
         (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
         (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

std::size_t bmp_row_stride(std::int32_t width, std::uint16_t bits_per_pixel) {
  return static_cast<std::size_t>((((static_cast<std::uint32_t>(width) * bits_per_pixel) + 31U) & ~31U) >> 3U);
}

void pack_test_bmp_index(std::vector<std::uint8_t>& row, std::int32_t x, std::uint16_t bits_per_pixel,
                         std::uint8_t index) {
  if (bits_per_pixel == 2) {
    const auto offset = static_cast<std::size_t>(x) / 4U;
    const auto shift = 6U - static_cast<unsigned>((x % 4) * 2);
    row[offset] = static_cast<std::uint8_t>(row[offset] | static_cast<std::uint8_t>((index & 0x03U) << shift));
  } else if (bits_per_pixel == 4) {
    const auto offset = static_cast<std::size_t>(x) / 2U;
    if ((x % 2) == 0) {
      row[offset] = static_cast<std::uint8_t>(row[offset] | static_cast<std::uint8_t>((index & 0x0fU) << 4U));
    } else {
      row[offset] = static_cast<std::uint8_t>(row[offset] | static_cast<std::uint8_t>(index & 0x0fU));
    }
  } else {
    row[static_cast<std::size_t>(x)] = index;
  }
}

std::vector<std::uint8_t> indexed_bmp_fixture(std::uint16_t bits_per_pixel, std::int32_t width, std::int32_t height,
                                              const std::vector<patchy::RgbColor>& palette,
                                              const std::vector<std::uint8_t>& top_down_indices,
                                              bool top_down = false, std::int32_t xppm = 11811,
                                              std::int32_t yppm = 5906) {
  const auto row_stride = bmp_row_stride(width, bits_per_pixel);
  const auto pixel_bytes = row_stride * static_cast<std::size_t>(height);
  const auto pixel_offset = 14U + 40U + static_cast<std::uint32_t>(palette.size() * 4U);
  const auto file_size = pixel_offset + static_cast<std::uint32_t>(pixel_bytes);

  std::vector<std::uint8_t> bytes;
  bytes.reserve(file_size);
  bytes.push_back('B');
  bytes.push_back('M');
  append_u32_le(bytes, file_size);
  append_u16_le(bytes, 0);
  append_u16_le(bytes, 0);
  append_u32_le(bytes, pixel_offset);
  append_u32_le(bytes, 40);
  append_i32_le(bytes, width);
  append_i32_le(bytes, top_down ? -height : height);
  append_u16_le(bytes, 1);
  append_u16_le(bytes, bits_per_pixel);
  append_u32_le(bytes, 0);
  append_u32_le(bytes, static_cast<std::uint32_t>(pixel_bytes));
  append_i32_le(bytes, xppm);
  append_i32_le(bytes, yppm);
  append_u32_le(bytes, static_cast<std::uint32_t>(palette.size()));
  append_u32_le(bytes, 0);

  for (const auto color : palette) {
    bytes.push_back(color.blue);
    bytes.push_back(color.green);
    bytes.push_back(color.red);
    bytes.push_back(0);
  }

  std::vector<std::uint8_t> row(row_stride, 0);
  for (std::int32_t file_y = 0; file_y < height; ++file_y) {
    std::fill(row.begin(), row.end(), std::uint8_t{0});
    const auto source_y = top_down ? file_y : (height - 1 - file_y);
    for (std::int32_t x = 0; x < width; ++x) {
      pack_test_bmp_index(row, x, bits_per_pixel,
                          top_down_indices[static_cast<std::size_t>(source_y) * static_cast<std::size_t>(width) +
                                           static_cast<std::size_t>(x)]);
    }
    bytes.insert(bytes.end(), row.begin(), row.end());
  }
  return bytes;
}

void bmp_reader_rejects_invalid_headers() {
  CHECK(!patchy::bmp::DocumentIo::can_read({}));
  const std::vector<std::uint8_t> not_bmp{'N', 'O'};
  CHECK(!patchy::bmp::DocumentIo::can_read(not_bmp));

  bool short_bmp_rejected = false;
  try {
    const std::vector<std::uint8_t> short_bmp{'B', 'M'};
    (void)patchy::bmp::DocumentIo::read(short_bmp);
  } catch (const std::exception&) {
    short_bmp_rejected = true;
  }
  CHECK(short_bmp_rejected);

  const auto valid = indexed_bmp_fixture(2, 1, 1, {patchy::RgbColor{0, 0, 0}, patchy::RgbColor{255, 255, 255}},
                                         {1});
  CHECK(patchy::bmp::DocumentIo::can_read(valid));
  const auto document = patchy::bmp::DocumentIo::read(valid);
  CHECK(document.width() == 1);
  CHECK(document.height() == 1);
}

void append_u32_be_bytes(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
  bytes.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
  bytes.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
  bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
  bytes.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

std::vector<std::uint8_t> density_probe_png_fixture(std::optional<std::array<std::uint32_t, 2>> pixels_per_meter,
                                                    std::uint8_t phys_unit = 1) {
  std::vector<std::uint8_t> bytes{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  const auto add_chunk = [&bytes](const char* type, const std::vector<std::uint8_t>& payload) {
    append_u32_be_bytes(bytes, static_cast<std::uint32_t>(payload.size()));
    bytes.insert(bytes.end(), type, type + 4);
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    append_u32_be_bytes(bytes, 0);  // CRC, unchecked by the probe
  };
  add_chunk("IHDR", {0, 0, 0, 1, 0, 0, 0, 1, 8, 6, 0, 0, 0});
  if (pixels_per_meter.has_value()) {
    std::vector<std::uint8_t> phys;
    append_u32_be_bytes(phys, (*pixels_per_meter)[0]);
    append_u32_be_bytes(phys, (*pixels_per_meter)[1]);
    phys.push_back(phys_unit);
    add_chunk("pHYs", phys);
  }
  add_chunk("IDAT", {0});
  add_chunk("IEND", {});
  return bytes;
}

std::vector<std::uint8_t> density_probe_jpeg_fixture(const std::vector<std::vector<std::uint8_t>>& app_segments) {
  // Each segment is {marker_low_byte, payload...}; the helper adds FF + the length.
  std::vector<std::uint8_t> bytes{0xFF, 0xD8};
  for (const auto& segment : app_segments) {
    bytes.push_back(0xFF);
    bytes.push_back(segment.front());
    const auto length = static_cast<std::uint16_t>(segment.size() - 1U + 2U);
    bytes.push_back(static_cast<std::uint8_t>((length >> 8U) & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>(length & 0xFFU));
    bytes.insert(bytes.end(), segment.begin() + 1, segment.end());
  }
  bytes.insert(bytes.end(), {0xFF, 0xDA, 0x00, 0x02, 0x12, 0x34});
  return bytes;
}

std::vector<std::uint8_t> jfif_app0_segment(std::uint8_t units, std::uint16_t x_density, std::uint16_t y_density) {
  return {0xE0, 'J', 'F', 'I', 'F', 0, 1, 2, units,
          static_cast<std::uint8_t>((x_density >> 8U) & 0xFFU), static_cast<std::uint8_t>(x_density & 0xFFU),
          static_cast<std::uint8_t>((y_density >> 8U) & 0xFFU), static_cast<std::uint8_t>(y_density & 0xFFU),
          0, 0};
}

void image_density_probe_reads_png_phys() {
  using patchy::formats::ImageDensityContainer;
  using patchy::formats::probe_image_density;

  // 11811 / 5906 pixels per meter are the pHYs values for 300 / 150 PPI.
  const auto tagged = probe_image_density(density_probe_png_fixture(std::array<std::uint32_t, 2>{11811, 5906}));
  CHECK(tagged.container == ImageDensityContainer::Png);
  CHECK(tagged.density.has_value());
  CHECK(std::abs(tagged.density->horizontal_ppi - 11811.0 * 0.0254) < 0.0001);
  CHECK(std::abs(tagged.density->vertical_ppi - 5906.0 * 0.0254) < 0.0001);

  const auto untagged = probe_image_density(density_probe_png_fixture(std::nullopt));
  CHECK(untagged.container == ImageDensityContainer::Png);
  CHECK(!untagged.density.has_value());

  // Unit 0 records only an aspect ratio, never an absolute density.
  const auto aspect_only =
      probe_image_density(density_probe_png_fixture(std::array<std::uint32_t, 2>{11811, 11811}, 0));
  CHECK(aspect_only.container == ImageDensityContainer::Png);
  CHECK(!aspect_only.density.has_value());

  const std::vector<std::uint8_t> not_an_image{'B', 'M', 1, 2, 3, 4};
  CHECK(probe_image_density(not_an_image).container == ImageDensityContainer::Unrecognized);
}

void image_density_probe_reads_jpeg_jfif_and_exif() {
  using patchy::formats::ImageDensityContainer;
  using patchy::formats::probe_image_density;

  const auto jfif_inch = probe_image_density(density_probe_jpeg_fixture({jfif_app0_segment(1, 300, 150)}));
  CHECK(jfif_inch.container == ImageDensityContainer::Jpeg);
  CHECK(jfif_inch.density.has_value());
  CHECK(std::abs(jfif_inch.density->horizontal_ppi - 300.0) < 0.0001);
  CHECK(std::abs(jfif_inch.density->vertical_ppi - 150.0) < 0.0001);

  const auto jfif_cm = probe_image_density(density_probe_jpeg_fixture({jfif_app0_segment(2, 118, 118)}));
  CHECK(jfif_cm.density.has_value());
  CHECK(std::abs(jfif_cm.density->horizontal_ppi - 118.0 * 2.54) < 0.0001);

  const auto jfif_aspect_only = probe_image_density(density_probe_jpeg_fixture({jfif_app0_segment(0, 1, 1)}));
  CHECK(jfif_aspect_only.container == ImageDensityContainer::Jpeg);
  CHECK(!jfif_aspect_only.density.has_value());

  // EXIF little-endian: XResolution/YResolution 240/1, ResolutionUnit 2 (inch).
  const std::vector<std::uint8_t> exif_ii{
      0xE1, 'E', 'x', 'i', 'f', 0, 0,
      'I', 'I', 0x2A, 0x00, 0x08, 0x00, 0x00, 0x00,
      0x03, 0x00,
      0x1A, 0x01, 0x05, 0x00, 0x01, 0x00, 0x00, 0x00, 0x32, 0x00, 0x00, 0x00,
      0x1B, 0x01, 0x05, 0x00, 0x01, 0x00, 0x00, 0x00, 0x3A, 0x00, 0x00, 0x00,
      0x28, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00,
      0xF0, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
      0xF0, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00};
  // EXIF beside a JFIF density: the EXIF value wins (the camera-file convention).
  const auto exif_wins =
      probe_image_density(density_probe_jpeg_fixture({jfif_app0_segment(1, 96, 96), exif_ii}));
  CHECK(exif_wins.density.has_value());
  CHECK(std::abs(exif_wins.density->horizontal_ppi - 240.0) < 0.0001);
  CHECK(std::abs(exif_wins.density->vertical_ppi - 240.0) < 0.0001);

  // EXIF big-endian with ResolutionUnit 3 (centimeters): 118 px/cm -> 299.72 PPI.
  const std::vector<std::uint8_t> exif_mm{
      0xE1, 'E', 'x', 'i', 'f', 0, 0,
      'M', 'M', 0x00, 0x2A, 0x00, 0x00, 0x00, 0x08,
      0x00, 0x03,
      0x01, 0x1A, 0x00, 0x05, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x32,
      0x01, 0x1B, 0x00, 0x05, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x3A,
      0x01, 0x28, 0x00, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x03, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x76, 0x00, 0x00, 0x00, 0x01,
      0x00, 0x00, 0x00, 0x76, 0x00, 0x00, 0x00, 0x01};
  const auto exif_cm = probe_image_density(density_probe_jpeg_fixture({exif_mm}));
  CHECK(exif_cm.density.has_value());
  CHECK(std::abs(exif_cm.density->horizontal_ppi - 118.0 * 2.54) < 0.0001);

  // EXIF ResolutionUnit 1 means explicitly unitless; the JFIF density still applies.
  auto exif_unitless = exif_ii;
  exif_unitless[7U + 8U + 2U + 2U * 12U + 8U] = 0x01;  // ResolutionUnit value 2 -> 1
  const auto jfif_fallback =
      probe_image_density(density_probe_jpeg_fixture({jfif_app0_segment(1, 144, 144), exif_unitless}));
  CHECK(jfif_fallback.density.has_value());
  CHECK(std::abs(jfif_fallback.density->horizontal_ppi - 144.0) < 0.0001);

  const auto untagged = probe_image_density(density_probe_jpeg_fixture({}));
  CHECK(untagged.container == ImageDensityContainer::Jpeg);
  CHECK(!untagged.density.has_value());
}

void bmp_indexed_reads_2_4_8_bit_palettes_and_rows() {
  const std::vector<patchy::RgbColor> palette{
      patchy::RgbColor{0, 0, 0}, patchy::RgbColor{220, 20, 30}, patchy::RgbColor{30, 210, 80},
      patchy::RgbColor{40, 70, 230}, patchy::RgbColor{250, 220, 20}, patchy::RgbColor{20, 200, 220},
      patchy::RgbColor{210, 40, 220}, patchy::RgbColor{255, 255, 255}};
  const std::vector<std::uint8_t> indices{0, 1, 2, 3, 0, 3, 2, 1, 0, 3};

  for (const auto bits : {std::uint16_t{2}, std::uint16_t{4}, std::uint16_t{8}}) {
    const auto fixture_palette_size = bits == 2 ? 4U : palette.size();
    const std::vector<patchy::RgbColor> fixture_palette(palette.begin(),
                                                        palette.begin() + static_cast<std::ptrdiff_t>(fixture_palette_size));
    const auto bytes = indexed_bmp_fixture(bits, 5, 2, fixture_palette, indices, bits == 4);
    const auto document = patchy::bmp::DocumentIo::read(bytes);
    CHECK(document.width() == 5);
    CHECK(document.height() == 2);
    CHECK(document.format() == patchy::PixelFormat::rgb8());
    CHECK(document.indexed_palette().has_value());
    CHECK(document.indexed_palette()->source_bit_depth == bits);
    CHECK(document.indexed_palette()->colors.size() == fixture_palette_size);
    CHECK(std::abs(document.print_settings().horizontal_ppi - 300.0) < 0.02);
    CHECK(std::abs(document.print_settings().vertical_ppi - 150.0) < 0.02);

    const auto& pixels = document.layers().front().pixels();
    for (std::int32_t y = 0; y < 2; ++y) {
      for (std::int32_t x = 0; x < 5; ++x) {
        const auto expected = fixture_palette[indices[static_cast<std::size_t>(y) * 5U + static_cast<std::size_t>(x)]];
        const auto* pixel = pixels.pixel(x, y);
        CHECK(pixel[0] == expected.red);
        CHECK(pixel[1] == expected.green);
        CHECK(pixel[2] == expected.blue);
      }
    }
  }
}

patchy::Document indexed_test_document(const std::vector<patchy::RgbColor>& colors, std::int32_t width) {
  const auto height = static_cast<std::int32_t>((colors.size() + static_cast<std::size_t>(width) - 1U) /
                                               static_cast<std::size_t>(width));
  patchy::Document document(width, height, patchy::PixelFormat::rgb8());
  patchy::PixelBuffer pixels(width, height, patchy::PixelFormat::rgb8());
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      const auto index = std::min<std::size_t>(static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                                   static_cast<std::size_t>(x),
                                               colors.size() - 1U);
      const auto color = colors[index];
      auto* pixel = pixels.pixel(x, y);
      pixel[0] = color.red;
      pixel[1] = color.green;
      pixel[2] = color.blue;
    }
  }
  document.add_pixel_layer("Indexed Source", std::move(pixels));
  return document;
}

void bmp_exact_indexed_writes_and_round_trips() {
  const std::vector<patchy::RgbColor> palette{patchy::RgbColor{0, 0, 0}, patchy::RgbColor{250, 10, 30},
                                              patchy::RgbColor{20, 220, 80}, patchy::RgbColor{40, 80, 240}};
  auto document = indexed_test_document({palette[2], palette[1], palette[3], palette[0], palette[1], palette[2]}, 3);
  document.print_settings().horizontal_ppi = 72.0;
  document.print_settings().vertical_ppi = 144.0;
  document.indexed_palette() = patchy::DocumentIndexedPalette{palette, 4};

  const auto bytes = patchy::bmp::DocumentIo::write(
      document, patchy::bmp::WriteOptions{patchy::bmp::BmpEncoding::Indexed4, patchy::bmp::BmpPaletteMode::Exact, true});
  CHECK(read_u16_le_at(bytes, 28) == 4);
  CHECK(read_u32_le_at(bytes, 34) == 8);
  CHECK(read_u32_le_at(bytes, 46) == palette.size());
  CHECK(bytes[54] == palette[0].blue);
  CHECK(bytes[55] == palette[0].green);
  CHECK(bytes[56] == palette[0].red);

  const auto round_tripped = patchy::bmp::DocumentIo::read(bytes);
  CHECK(round_tripped.indexed_palette().has_value());
  CHECK(round_tripped.indexed_palette()->colors.size() == palette.size());
  CHECK(round_tripped.indexed_palette()->colors[1].red == palette[1].red);
  CHECK(std::abs(round_tripped.print_settings().horizontal_ppi - 72.0) < 0.02);
  CHECK(std::abs(round_tripped.print_settings().vertical_ppi - 144.0) < 0.02);
  const auto& source = document.layers().front().pixels();
  const auto& read = round_tripped.layers().front().pixels();
  CHECK(source.data().size() == read.data().size());
  CHECK(std::equal(source.data().begin(), source.data().end(), read.data().begin()));
}

void bmp_exact_indexed_fails_when_palette_overflows() {
  const std::vector<patchy::RgbColor> colors{patchy::RgbColor{0, 0, 0},    patchy::RgbColor{50, 0, 0},
                                             patchy::RgbColor{0, 50, 0},   patchy::RgbColor{0, 0, 50},
                                             patchy::RgbColor{80, 80, 80}};
  const auto document = indexed_test_document(colors, 5);

  bool failed = false;
  try {
    (void)patchy::bmp::DocumentIo::write(
        document,
        patchy::bmp::WriteOptions{patchy::bmp::BmpEncoding::Indexed2, patchy::bmp::BmpPaletteMode::Exact, true});
  } catch (const std::exception&) {
    failed = true;
  }
  CHECK(failed);
  CHECK(document.layers().front().pixels().pixel(4, 0)[0] == 80);
}

void bmp_quantized_indexed_writes_deterministically() {
  std::vector<patchy::RgbColor> colors;
  for (int i = 0; i < 12; ++i) {
    colors.push_back(patchy::RgbColor{static_cast<std::uint8_t>(i * 19),
                                      static_cast<std::uint8_t>(255 - i * 13),
                                      static_cast<std::uint8_t>((i * 37) % 256)});
  }
  const auto document = indexed_test_document(colors, 4);
  const auto options =
      patchy::bmp::WriteOptions{patchy::bmp::BmpEncoding::Indexed2, patchy::bmp::BmpPaletteMode::Quantize, true};
  const auto first = patchy::bmp::DocumentIo::write(document, options);
  const auto second = patchy::bmp::DocumentIo::write(document, options);
  CHECK(first == second);
  CHECK(read_u16_le_at(first, 28) == 2);
  CHECK(read_u32_le_at(first, 46) <= 4);
  const auto read = patchy::bmp::DocumentIo::read(first);
  CHECK(read.indexed_palette().has_value());
  CHECK(read.indexed_palette()->colors.size() <= 4);
  CHECK(read.width() == 4);
  CHECK(read.height() == 3);
}

void bmp_palette_file_export_uses_pal_and_bmp_palettes() {
  const auto pal_path = std::filesystem::path("test-palette.pal");
  {
    std::ofstream file(pal_path, std::ios::binary);
    file << "JASC-PAL\n0100\n4\n0 0 0\n255 0 0\n0 255 0\n0 0 255\n";
  }

  const auto document =
      indexed_test_document({patchy::RgbColor{240, 10, 10}, patchy::RgbColor{10, 240, 10},
                             patchy::RgbColor{10, 10, 240}, patchy::RgbColor{12, 12, 12}},
                            4);
  const auto pal_bytes = patchy::bmp::DocumentIo::write(
      document, patchy::bmp::WriteOptions{patchy::bmp::BmpEncoding::Indexed4,
                                          patchy::bmp::BmpPaletteMode::PaletteFile, false, pal_path});
  CHECK(read_u16_le_at(pal_bytes, 28) == 4);
  CHECK(read_u32_le_at(pal_bytes, 46) == 4);
  const auto pal_read = patchy::bmp::DocumentIo::read(pal_bytes);
  CHECK(pal_read.indexed_palette().has_value());
  CHECK(pal_read.indexed_palette()->colors[1].red == 255);
  CHECK(pal_read.layers().front().pixels().pixel(0, 0)[0] == 255);
  CHECK(pal_read.layers().front().pixels().pixel(1, 0)[1] == 255);
  CHECK(pal_read.layers().front().pixels().pixel(2, 0)[2] == 255);
  std::filesystem::remove(pal_path);

  const auto bmp_palette_path = std::filesystem::path("test-palette-source.bmp");
  const auto bmp_palette = std::vector<patchy::RgbColor>{patchy::RgbColor{0, 0, 0}, patchy::RgbColor{255, 255, 0}};
  const auto bmp_palette_bytes = indexed_bmp_fixture(8, 1, 1, bmp_palette, {1});
  {
    std::ofstream file(bmp_palette_path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bmp_palette_bytes.data()),
               static_cast<std::streamsize>(bmp_palette_bytes.size()));
  }
  const auto bmp_bytes = patchy::bmp::DocumentIo::write(
      indexed_test_document({patchy::RgbColor{250, 250, 20}}, 1),
      patchy::bmp::WriteOptions{patchy::bmp::BmpEncoding::Indexed8,
                                patchy::bmp::BmpPaletteMode::PaletteFile, false, bmp_palette_path});
  CHECK(read_u32_le_at(bmp_bytes, 46) == 2);
  const auto bmp_read = patchy::bmp::DocumentIo::read(bmp_bytes);
  CHECK(bmp_read.layers().front().pixels().pixel(0, 0)[0] == 255);
  CHECK(bmp_read.layers().front().pixels().pixel(0, 0)[1] == 255);
  CHECK(bmp_read.layers().front().pixels().pixel(0, 0)[2] == 0);
  std::filesystem::remove(bmp_palette_path);
}

void bmp_rgb24_and_rgba32_write_and_read() {
  patchy::Document document(2, 1, patchy::PixelFormat::rgba8());
  patchy::PixelBuffer pixels(2, 1, patchy::PixelFormat::rgba8());
  auto* left = pixels.pixel(0, 0);
  left[0] = 255;
  left[3] = 64;
  auto* right = pixels.pixel(1, 0);
  right[1] = 255;
  right[3] = 255;
  document.add_pixel_layer("Alpha", std::move(pixels));

  const auto rgba = patchy::bmp::DocumentIo::write(
      document, patchy::bmp::WriteOptions{patchy::bmp::BmpEncoding::Rgba32, patchy::bmp::BmpPaletteMode::Exact, true});
  CHECK(read_u16_le_at(rgba, 28) == 32);
  CHECK(read_u32_le_at(rgba, 30) == 3);
  const auto rgba_read = patchy::bmp::DocumentIo::read(rgba);
  CHECK(rgba_read.format() == patchy::PixelFormat::rgba8());
  CHECK(rgba_read.layers().front().pixels().pixel(0, 0)[3] == 64);
  CHECK(rgba_read.layers().front().pixels().pixel(1, 0)[1] == 255);

  const auto rgb = patchy::bmp::DocumentIo::write(
      document, patchy::bmp::WriteOptions{patchy::bmp::BmpEncoding::Rgb24, patchy::bmp::BmpPaletteMode::Exact, true});
  CHECK(read_u16_le_at(rgb, 28) == 24);
  const auto rgb_read = patchy::bmp::DocumentIo::read(rgb);
  CHECK(rgb_read.format() == patchy::PixelFormat::rgb8());
  CHECK(rgb_read.layers().front().pixels().pixel(0, 0)[0] == 255);
  CHECK(rgb_read.layers().front().pixels().pixel(0, 0)[1] > 190);
  CHECK(rgb_read.layers().front().pixels().pixel(0, 0)[2] > 190);
}

void format_registry_finds_psd() {
  patchy::FormatRegistry registry;
  patchy::register_builtin_formats(registry);
  CHECK(registry.find_by_extension(".psd") != nullptr);
  CHECK(registry.find_by_extension("PSB") != nullptr);
  CHECK(registry.find_by_extension(".bmp") != nullptr);
}

void format_registry_allows_read_only_handlers() {
  patchy::FormatRegistry registry;
  registry.register_handler({"patchy.test.readonly",
                             "Read Only",
                             {".ro"},
                             [](std::span<const std::uint8_t>) {
                               return patchy::FormatReadResult{
                                   patchy::Document(2, 2, patchy::PixelFormat::rgba8()), {"notice"}};
                             },
                             nullptr,
                             nullptr});
  const auto* handler = registry.find_by_extension(".ro");
  CHECK(handler != nullptr);
  CHECK(!handler->can_write());
  const auto result = handler->read({});
  CHECK(result.notices.size() == 1);

  bool rejected_readless = false;
  try {
    registry.register_handler({"patchy.test.noread", "No Read", {".nr"}, nullptr, nullptr, nullptr});
  } catch (const std::invalid_argument&) {
    rejected_readless = true;
  }
  CHECK(rejected_readless);

  CHECK(patchy::builtin_format_registry().find_by_extension(".bmp") != nullptr);
  CHECK(patchy::builtin_format_registry().find_by_extension(".bmp")->can_write());
}

void string_utils_normalize_extensions_and_names() {
  CHECK(patchy::ascii_lower_copy("BackGround") == "background");
  CHECK(patchy::normalized_extension("PSD") == ".psd");
  CHECK(patchy::normalized_extension(".8BF") == ".8bf");
  CHECK(patchy::normalized_extension(".PNG", false) == "png");
  CHECK(patchy::normalized_extension("") == "");
}

}  // namespace

std::vector<patchy::test::TestCase> flat_formats_bmp_tests() {
  return {
      {"bmp_reader_rejects_invalid_headers", bmp_reader_rejects_invalid_headers},
      {"image_density_probe_reads_png_phys", image_density_probe_reads_png_phys},
      {"image_density_probe_reads_jpeg_jfif_and_exif", image_density_probe_reads_jpeg_jfif_and_exif},
      {"bmp_indexed_reads_2_4_8_bit_palettes_and_rows", bmp_indexed_reads_2_4_8_bit_palettes_and_rows},
      {"bmp_exact_indexed_writes_and_round_trips", bmp_exact_indexed_writes_and_round_trips},
      {"bmp_exact_indexed_fails_when_palette_overflows", bmp_exact_indexed_fails_when_palette_overflows},
      {"bmp_quantized_indexed_writes_deterministically", bmp_quantized_indexed_writes_deterministically},
      {"bmp_palette_file_export_uses_pal_and_bmp_palettes", bmp_palette_file_export_uses_pal_and_bmp_palettes},
      {"bmp_rgb24_and_rgba32_write_and_read", bmp_rgb24_and_rgba32_write_and_read},
      {"string_utils_normalize_extensions_and_names", string_utils_normalize_extensions_and_names},
      {"format_registry_finds_psd", format_registry_finds_psd},
      {"format_registry_allows_read_only_handlers", format_registry_allows_read_only_handlers},
  };
}
