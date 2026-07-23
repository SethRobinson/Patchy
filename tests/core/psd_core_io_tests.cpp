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
#include "formats/miniz/miniz.h"
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
#include "psd_test_support.hpp"
#include "test_groups.hpp"

namespace {

using patchy::test::PsdLayerChannelRecord;
using patchy::test::psd_first_layer_extra_data;
using patchy::test::psd_layer_block_payload;
using patchy::test::psd_layer_channel_records;
using patchy::test::read_pascal_padded;
using patchy::test::read_u32_be_at;
using patchy::test::solid_rgb;
using patchy::test::solid_rgba;
using patchy::test::test_image_resource_payload;
using patchy::test::write_ascii4;
using patchy::test::write_pascal_padded;

std::uint16_t psd_composite_compression(std::span<const std::uint8_t> bytes) {
  patchy::psd::BigEndianReader reader(bytes);
  const auto header = patchy::psd::read_header(reader);

  const auto color_mode_length = reader.read_u32();
  reader.skip(color_mode_length);
  const auto image_resource_length = reader.read_u32();
  reader.skip(image_resource_length);
  const auto layer_mask_length = header.large_document ? reader.read_u64() : reader.read_u32();
  reader.skip(static_cast<std::size_t>(layer_mask_length));
  return reader.read_u16();
}

void write_test_image_resource(patchy::psd::BigEndianWriter& writer, std::uint16_t id, const std::string& name,
                               std::span<const std::uint8_t> payload) {
  write_ascii4(writer, "8BIM");
  writer.write_u16(id);
  write_pascal_padded(writer, name, 2);
  writer.write_u32(static_cast<std::uint32_t>(payload.size()));
  writer.write_bytes(payload);
  if ((payload.size() % 2U) != 0) {
    writer.write_u8(0);
  }
}

std::vector<std::uint8_t> test_alpha_channel_names_payload(const std::vector<std::string>& names) {
  std::vector<std::uint8_t> payload;
  for (const auto& name : names) {
    const auto length = std::min<std::size_t>(name.size(), 255U);
    payload.push_back(static_cast<std::uint8_t>(length));
    payload.insert(payload.end(), name.begin(), name.begin() + static_cast<std::ptrdiff_t>(length));
  }
  return payload;
}

std::vector<std::uint8_t> test_alpha_identifiers_payload(const std::vector<std::uint32_t>& identifiers) {
  patchy::psd::BigEndianWriter writer;
  writer.write_u32(static_cast<std::uint32_t>(identifiers.size()));
  for (const auto identifier : identifiers) {
    writer.write_u32(identifier);
  }
  return writer.bytes();
}

std::vector<std::uint8_t> test_channel_display_record(patchy::RgbColor color, std::uint16_t opacity_percent,
                                                      std::uint8_t mode, bool legacy_padding = false) {
  patchy::psd::BigEndianWriter writer;
  writer.write_u16(0);  // RGB color space.
  writer.write_u16(static_cast<std::uint16_t>(color.red) * 257U);
  writer.write_u16(static_cast<std::uint16_t>(color.green) * 257U);
  writer.write_u16(static_cast<std::uint16_t>(color.blue) * 257U);
  writer.write_u16(0);
  writer.write_u16(opacity_percent);
  writer.write_u8(mode);
  if (legacy_padding) {
    writer.write_u8(0);
  }
  return writer.bytes();
}

std::vector<std::uint8_t> test_display_info_float_payload(
    const std::vector<std::vector<std::uint8_t>>& records) {
  patchy::psd::BigEndianWriter writer;
  writer.write_u32(1);
  for (const auto& record : records) {
    CHECK(record.size() == 13U);
    writer.write_bytes(record);
  }
  return writer.bytes();
}

std::vector<std::uint8_t> flat_psd_with_test_planes(
    bool large_document, std::uint16_t color_mode, std::int32_t width, std::int32_t height,
    const std::vector<std::vector<std::uint8_t>>& planes,
    std::span<const std::uint8_t> image_resources = {}, std::uint16_t compression = 0) {
  CHECK(width > 0 && height > 0);
  CHECK(!planes.empty());
  const auto pixel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  for (const auto& plane : planes) {
    CHECK(plane.size() == pixel_count);
  }

  patchy::psd::BigEndianWriter writer;
  patchy::psd::write_header(writer, patchy::psd::Header{
                                        large_document,
                                        static_cast<std::uint16_t>(planes.size()),
                                        static_cast<std::uint32_t>(height),
                                        static_cast<std::uint32_t>(width),
                                        8,
                                        color_mode,
                                    });
  writer.write_u32(0);
  writer.write_u32(static_cast<std::uint32_t>(image_resources.size()));
  writer.write_bytes(image_resources);
  if (large_document) {
    writer.write_u64(0);
  } else {
    writer.write_u32(0);
  }
  writer.write_u16(compression);

  if (compression == 0U) {
    for (const auto& plane : planes) {
      writer.write_bytes(plane);
    }
    return writer.bytes();
  }

  CHECK(compression == 1U);
  std::vector<std::vector<std::uint8_t>> rows;
  rows.reserve(planes.size() * static_cast<std::size_t>(height));
  for (const auto& plane : planes) {
    for (std::int32_t y = 0; y < height; ++y) {
      const auto offset = static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
      rows.push_back(patchy::psd::encode_packbits_row(
          std::span<const std::uint8_t>(plane).subspan(offset, static_cast<std::size_t>(width))));
    }
  }
  for (const auto& row : rows) {
    if (large_document) {
      writer.write_u32(static_cast<std::uint32_t>(row.size()));
    } else {
      CHECK(row.size() <= 0xffffU);
      writer.write_u16(static_cast<std::uint16_t>(row.size()));
    }
  }
  for (const auto& row : rows) {
    writer.write_bytes(row);
  }
  return writer.bytes();
}

std::int16_t psd_signed_layer_count(std::span<const std::uint8_t> bytes) {
  patchy::psd::BigEndianReader reader(bytes);
  const auto header = patchy::psd::read_header(reader);
  reader.skip(reader.read_u32());
  reader.skip(reader.read_u32());
  const auto layer_mask_length = header.large_document ? reader.read_u64() : reader.read_u32();
  CHECK(layer_mask_length > 0U);
  const auto layer_info_length = header.large_document ? reader.read_u64() : reader.read_u32();
  CHECK(layer_info_length >= 2U);
  return static_cast<std::int16_t>(reader.read_u16());
}

patchy::psd::Header test_psd_header(std::span<const std::uint8_t> bytes) {
  patchy::psd::BigEndianReader reader(bytes);
  return patchy::psd::read_header(reader);
}

std::vector<std::uint8_t> psd_raw_image_resources(std::span<const std::uint8_t> bytes) {
  patchy::psd::BigEndianReader reader(bytes);
  (void)patchy::psd::read_header(reader);

  const auto color_mode_length = reader.read_u32();
  reader.skip(color_mode_length);
  const auto image_resource_length = reader.read_u32();
  return reader.read_bytes(image_resource_length);
}

int test_image_resource_count(std::span<const std::uint8_t> resources, std::uint16_t id) {
  patchy::psd::BigEndianReader reader(resources);
  int count = 0;
  while (reader.remaining() > 0) {
    auto signature = reader.read_bytes(4);
    CHECK(signature[0] == '8');
    CHECK(signature[1] == 'B');
    const auto resource_id = reader.read_u16();
    (void)read_pascal_padded(reader, 2);
    const auto payload_length = reader.read_u32();
    reader.skip(payload_length);
    if ((payload_length % 2U) != 0 && reader.remaining() > 0) {
      reader.skip(1);
    }
    if (resource_id == id) {
      ++count;
    }
  }
  return count;
}

void write_packbits_literal_row(patchy::psd::BigEndianWriter& writer, std::span<const std::uint8_t> values) {
  CHECK(!values.empty());
  CHECK(values.size() <= 128U);
  writer.write_u8(static_cast<std::uint8_t>(values.size() - 1U));
  writer.write_bytes(values);
}

std::vector<std::uint8_t> layered_cmyk_psd_with_transparency() {
  patchy::psd::BigEndianWriter layer_extra;
  layer_extra.write_u32(0);
  layer_extra.write_u32(0);
  write_pascal_padded(layer_extra, "CMYK Layer", 4);

  patchy::psd::BigEndianWriter layer_info;
  layer_info.write_u16(1);
  layer_info.write_u32(0);
  layer_info.write_u32(0);
  layer_info.write_u32(1);
  layer_info.write_u32(2);
  layer_info.write_u16(5);
  for (const auto channel_id : {0xFFFFU, 0U, 1U, 2U, 3U}) {
    layer_info.write_u16(static_cast<std::uint16_t>(channel_id));
    layer_info.write_u32(4);
  }
  write_ascii4(layer_info, "8BIM");
  write_ascii4(layer_info, "norm");
  layer_info.write_u8(255);
  layer_info.write_u8(0);
  layer_info.write_u8(0);
  layer_info.write_u8(0);
  layer_info.write_u32(static_cast<std::uint32_t>(layer_extra.bytes().size()));
  layer_info.write_bytes(layer_extra.bytes());

  const std::array<std::array<std::uint8_t, 2>, 5> channels{{
      {255, 64},   // transparency
      {255, 255},  // cyan
      {0, 255},    // magenta
      {0, 255},    // yellow
      {255, 127},  // black
  }};
  for (const auto& channel : channels) {
    layer_info.write_u16(0);
    layer_info.write_bytes(channel);
  }
  if ((layer_info.bytes().size() % 2U) != 0) {
    layer_info.write_u8(0);
  }

  patchy::psd::BigEndianWriter layer_mask;
  layer_mask.write_u32(static_cast<std::uint32_t>(layer_info.bytes().size()));
  layer_mask.write_bytes(layer_info.bytes());
  layer_mask.write_u32(0);

  patchy::psd::BigEndianWriter writer;
  patchy::psd::write_header(writer, patchy::psd::Header{false, 4, 1, 2, 8, 4});
  writer.write_u32(0);
  writer.write_u32(0);
  writer.write_u32(static_cast<std::uint32_t>(layer_mask.bytes().size()));
  writer.write_bytes(layer_mask.bytes());
  writer.write_u16(0);
  for (std::size_t i = 0; i < 8U; ++i) {
    writer.write_u8(0);
  }
  return writer.bytes();
}

std::vector<std::uint8_t> psd_resolution_payload(double horizontal_ppi, double vertical_ppi) {
  patchy::psd::BigEndianWriter writer;
  writer.write_u32(static_cast<std::uint32_t>(std::lround(horizontal_ppi * 65536.0)));
  writer.write_u16(1);
  writer.write_u16(1);
  writer.write_u32(static_cast<std::uint32_t>(std::lround(vertical_ppi * 65536.0)));
  writer.write_u16(1);
  writer.write_u16(1);
  return writer.bytes();
}

std::vector<std::uint8_t> psd_grid_guides_payload(
    std::int32_t horizontal_cycle_32,
    std::int32_t vertical_cycle_32,
    const std::vector<std::pair<std::int32_t, patchy::GuideOrientation>>& guides) {
  patchy::psd::BigEndianWriter writer;
  writer.write_u32(1);
  writer.write_u32(static_cast<std::uint32_t>(horizontal_cycle_32));
  writer.write_u32(static_cast<std::uint32_t>(vertical_cycle_32));
  writer.write_u32(static_cast<std::uint32_t>(guides.size()));
  for (const auto& [position_32, orientation] : guides) {
    writer.write_u32(static_cast<std::uint32_t>(position_32));
    writer.write_u8(orientation == patchy::GuideOrientation::Horizontal ? 1U : 0U);
  }
  return writer.bytes();
}

void psd_flat_rgb8_round_trips() {
  patchy::Document document(2, 1, patchy::PixelFormat::rgb8());
  patchy::PixelBuffer pixels(2, 1, patchy::PixelFormat::rgb8());
  pixels.pixel(0, 0)[0] = 1;
  pixels.pixel(0, 0)[1] = 2;
  pixels.pixel(0, 0)[2] = 3;
  pixels.pixel(1, 0)[0] = 4;
  pixels.pixel(1, 0)[1] = 5;
  pixels.pixel(1, 0)[2] = 6;
  document.add_pixel_layer("Background", std::move(pixels));

  const auto bytes = patchy::psd::DocumentIo::write_flat_rgb8(document);
  CHECK(patchy::psd::DocumentIo::can_read(bytes));

  const auto read = patchy::psd::DocumentIo::read(bytes);
  CHECK(read.width() == 2);
  CHECK(read.height() == 1);
  CHECK(read.layers().size() == 1);
  const auto* px = read.layers().front().pixels().pixel(1, 0);
  CHECK(px[0] == 4);
  CHECK(px[1] == 5);
  CHECK(px[2] == 6);
}

void psd_flat_rgb8_writer_uses_rle_for_compressible_data() {
  patchy::Document document(32, 16, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(32, 16, 20, 40, 80));

  const auto bytes = patchy::psd::DocumentIo::write_flat_rgb8(document);
  CHECK(psd_composite_compression(bytes) == 1U);
  CHECK(bytes.size() < 900U);

  const auto read = patchy::psd::DocumentIo::read(bytes);
  CHECK(read.layers().size() == 1);
  const auto* px = read.layers().front().pixels().pixel(31, 15);
  CHECK(px[0] == 20);
  CHECK(px[1] == 40);
  CHECK(px[2] == 80);
}

void psd_flat_rgb8_writer_keeps_raw_for_incompressible_data() {
  patchy::Document document(128, 1, patchy::PixelFormat::rgb8());
  patchy::PixelBuffer pixels(128, 1, patchy::PixelFormat::rgb8());
  for (std::int32_t x = 0; x < 128; ++x) {
    auto* px = pixels.pixel(x, 0);
    px[0] = static_cast<std::uint8_t>(x);
    px[1] = static_cast<std::uint8_t>(x + 17);
    px[2] = static_cast<std::uint8_t>(255 - x);
  }
  document.add_pixel_layer("Background", std::move(pixels));

  const auto bytes = patchy::psd::DocumentIo::write_flat_rgb8(document);
  CHECK(psd_composite_compression(bytes) == 0U);

  const auto read = patchy::psd::DocumentIo::read(bytes);
  const auto* px = read.layers().front().pixels().pixel(127, 0);
  CHECK(px[0] == 127);
  CHECK(px[1] == 144);
  CHECK(px[2] == 128);
}

void psd_flat_rle_rgb8_reads() {
  patchy::psd::BigEndianWriter writer;
  patchy::psd::write_header(writer, patchy::psd::Header{false, 3, 1, 2, 8, 3});
  writer.write_u32(0);
  writer.write_u32(0);
  writer.write_u32(0);
  writer.write_u16(1);
  writer.write_u16(3);
  writer.write_u16(3);
  writer.write_u16(3);
  writer.write_u8(1);
  writer.write_u8(1);
  writer.write_u8(4);
  writer.write_u8(1);
  writer.write_u8(2);
  writer.write_u8(5);
  writer.write_u8(1);
  writer.write_u8(3);
  writer.write_u8(6);

  const auto read = patchy::psd::DocumentIo::read(writer.bytes());
  CHECK(read.layers().size() == 1);
  const auto* px0 = read.layers().front().pixels().pixel(0, 0);
  const auto* px1 = read.layers().front().pixels().pixel(1, 0);
  CHECK(px0[0] == 1);
  CHECK(px0[1] == 2);
  CHECK(px0[2] == 3);
  CHECK(px1[0] == 4);
  CHECK(px1[1] == 5);
  CHECK(px1[2] == 6);
}

void psd_flat_raw_cmyk8_imports_as_rgb() {
  patchy::psd::BigEndianWriter writer;
  patchy::psd::write_header(writer, patchy::psd::Header{false, 4, 1, 2, 8, 4});
  writer.write_u32(0);
  writer.write_u32(0);
  writer.write_u32(0);
  writer.write_u16(0);
  writer.write_u8(255);
  writer.write_u8(255);
  writer.write_u8(0);
  writer.write_u8(255);
  writer.write_u8(0);
  writer.write_u8(255);
  writer.write_u8(255);
  writer.write_u8(127);

  const auto read = patchy::psd::DocumentIo::read(writer.bytes());
  CHECK(read.format() == patchy::PixelFormat::rgb8());
  CHECK(read.layers().size() == 1);
  const auto color_mode = read.metadata().values.find("psd.color_mode");
  CHECK(color_mode != read.metadata().values.end());
  CHECK(color_mode->second == "CMYK");
  const auto* px0 = read.layers().front().pixels().pixel(0, 0);
  const auto* px1 = read.layers().front().pixels().pixel(1, 0);
  CHECK(px0[0] == 255);
  CHECK(px0[1] == 0);
  CHECK(px0[2] == 0);
  CHECK(px1[0] == 127);
  CHECK(px1[1] == 127);
  CHECK(px1[2] == 127);
}

void psd_flat_rle_cmyk8_imports_as_rgb() {
  patchy::psd::BigEndianWriter writer;
  patchy::psd::write_header(writer, patchy::psd::Header{false, 4, 1, 2, 8, 4});
  writer.write_u32(0);
  writer.write_u32(0);
  writer.write_u32(0);
  writer.write_u16(1);
  for (std::uint16_t channel = 0; channel < 4; ++channel) {
    writer.write_u16(3);
  }
  const std::array<std::uint8_t, 2> cyan{0, 255};
  const std::array<std::uint8_t, 2> magenta{255, 0};
  const std::array<std::uint8_t, 2> yellow{0, 255};
  const std::array<std::uint8_t, 2> black{255, 255};
  write_packbits_literal_row(writer, cyan);
  write_packbits_literal_row(writer, magenta);
  write_packbits_literal_row(writer, yellow);
  write_packbits_literal_row(writer, black);

  const auto read = patchy::psd::DocumentIo::read(writer.bytes());
  CHECK(read.layers().size() == 1);
  const auto* px0 = read.layers().front().pixels().pixel(0, 0);
  const auto* px1 = read.layers().front().pixels().pixel(1, 0);
  CHECK(px0[0] == 0);
  CHECK(px0[1] == 255);
  CHECK(px0[2] == 0);
  CHECK(px1[0] == 255);
  CHECK(px1[1] == 0);
  CHECK(px1[2] == 255);
}

void psd_layered_cmyk8_imports_as_rgba() {
  const auto read = patchy::psd::DocumentIo::read(layered_cmyk_psd_with_transparency());
  CHECK(read.layers().size() == 1);
  const auto& layer = read.layers().front();
  CHECK(layer.name() == "CMYK Layer");
  CHECK(layer.pixels().format() == patchy::PixelFormat::rgba8());
  const auto color_mode = read.metadata().values.find("psd.color_mode");
  CHECK(color_mode != read.metadata().values.end());
  CHECK(color_mode->second == "CMYK");

  const auto* px0 = layer.pixels().pixel(0, 0);
  const auto* px1 = layer.pixels().pixel(1, 0);
  CHECK(px0[0] == 255);
  CHECK(px0[1] == 0);
  CHECK(px0[2] == 0);
  CHECK(px0[3] == 255);
  CHECK(px1[0] == 127);
  CHECK(px1[1] == 127);
  CHECK(px1[2] == 127);
  CHECK(px1[3] == 64);
}

void psd_imported_cmyk_icc_profile_is_not_exported_as_rgb_profile() {
  const std::vector<std::uint8_t> source_icc{1, 2, 3, 4};
  patchy::psd::BigEndianWriter resources;
  write_test_image_resource(resources, 1039, "cmyk", source_icc);

  patchy::psd::BigEndianWriter writer;
  patchy::psd::write_header(writer, patchy::psd::Header{false, 4, 1, 1, 8, 4});
  writer.write_u32(0);
  writer.write_u32(static_cast<std::uint32_t>(resources.bytes().size()));
  writer.write_bytes(resources.bytes());
  writer.write_u32(0);
  writer.write_u16(0);
  writer.write_u8(0);
  writer.write_u8(0);
  writer.write_u8(0);
  writer.write_u8(0);

  auto document = patchy::psd::DocumentIo::read(writer.bytes());
  CHECK(document.color_state().embedded_icc_profile.empty());
  CHECK(test_image_resource_payload(document.metadata().raw_psd_image_resources, 1039).has_value());

  // The unusable profile falls back to the naive CMYK mix: the all-zero (full-ink)
  // channel bytes decode to black exactly as they did before ICC support.
  CHECK(document.layers().size() == 1);
  const auto* pixel = document.layers().front().pixels().pixel(0, 0);
  CHECK(pixel[0] == 0);
  CHECK(pixel[1] == 0);
  CHECK(pixel[2] == 0);

  const auto exported_without_rgb_profile =
      psd_raw_image_resources(patchy::psd::DocumentIo::write_flat_rgb8(document));
  CHECK(!test_image_resource_payload(exported_without_rgb_profile, 1039).has_value());

  const std::vector<std::uint8_t> replacement_icc{9, 8, 7};
  document.color_state().embedded_icc_profile = replacement_icc;
  const auto exported_with_rgb_profile =
      psd_raw_image_resources(patchy::psd::DocumentIo::write_flat_rgb8(document));
  CHECK(test_image_resource_payload(exported_with_rgb_profile, 1039).value() == replacement_icc);
}

void psd_image_resources_round_trip_and_icc_profile_is_exposed() {
  const auto resolution_payload = psd_resolution_payload(144.0, 240.0);
  const std::vector<std::uint8_t> icc_payload{10, 20, 30, 40};
  patchy::psd::BigEndianWriter resources;
  write_test_image_resource(resources, 1005, "dpi", resolution_payload);
  write_test_image_resource(resources, 1039, "", icc_payload);

  patchy::psd::BigEndianWriter writer;
  patchy::psd::write_header(writer, patchy::psd::Header{false, 3, 1, 1, 8, 3});
  writer.write_u32(0);
  writer.write_u32(static_cast<std::uint32_t>(resources.bytes().size()));
  writer.write_bytes(resources.bytes());
  writer.write_u32(0);
  writer.write_u16(0);
  writer.write_u8(1);
  writer.write_u8(2);
  writer.write_u8(3);

  auto document = patchy::psd::DocumentIo::read(writer.bytes());
  CHECK(document.metadata().raw_psd_image_resources == resources.bytes());
  CHECK(document.color_state().embedded_icc_profile == icc_payload);
  CHECK(std::abs(document.print_settings().horizontal_ppi - 144.0) < 0.01);
  CHECK(std::abs(document.print_settings().vertical_ppi - 240.0) < 0.01);

  const auto flat_resources = psd_raw_image_resources(patchy::psd::DocumentIo::write_flat_rgb8(document));
  CHECK(test_image_resource_payload(flat_resources, 1005).value() == resolution_payload);
  CHECK(test_image_resource_payload(flat_resources, 1039).value() == icc_payload);
  CHECK(test_image_resource_count(flat_resources, 1005) == 1);

  const std::vector<std::uint8_t> replacement_icc{90, 91, 92, 93, 94};
  document.color_state().embedded_icc_profile = replacement_icc;
  document.print_settings().horizontal_ppi = 300.0;
  document.print_settings().vertical_ppi = 150.0;
  const auto layered_resources = psd_raw_image_resources(patchy::psd::DocumentIo::write_layered_rgb8(document));
  CHECK(test_image_resource_payload(layered_resources, 1005).value() == psd_resolution_payload(300.0, 150.0));
  CHECK(test_image_resource_payload(layered_resources, 1039).value() == replacement_icc);
  CHECK(test_image_resource_count(layered_resources, 1005) == 1);
}

void psd_resolution_resource_units_are_display_only() {
  // Photoshop 2026 ground truth (COM byte-patch probe, July 2026): resource 1005
  // resolutions are stored as pixels/inch no matter what the unit fields say; a
  // unit-2 (px/cm) file whose fixed 16.16 value is 144 opens at 144 PPI in Photoshop.
  // The unit fields are display preferences and must survive a round trip.
  const std::vector<std::uint8_t> px_cm_display_payload{
      0x00, 0x90, 0x00, 0x00, 0x00, 0x02, 0x00, 0x04,
      0x00, 0x90, 0x00, 0x00, 0x00, 0x02, 0x00, 0x04};
  patchy::psd::BigEndianWriter resources;
  write_test_image_resource(resources, 1005, "", px_cm_display_payload);

  patchy::psd::BigEndianWriter writer;
  patchy::psd::write_header(writer, patchy::psd::Header{false, 3, 1, 1, 8, 3});
  writer.write_u32(0);
  writer.write_u32(static_cast<std::uint32_t>(resources.bytes().size()));
  writer.write_bytes(resources.bytes());
  writer.write_u32(0);
  writer.write_u16(0);
  writer.write_u8(1);
  writer.write_u8(2);
  writer.write_u8(3);

  auto document = patchy::psd::DocumentIo::read(writer.bytes());
  CHECK(std::abs(document.print_settings().horizontal_ppi - 144.0) < 0.01);
  CHECK(std::abs(document.print_settings().vertical_ppi - 144.0) < 0.01);
  CHECK(document.print_settings().horizontal_resolution_display_unit == 2);
  CHECK(document.print_settings().vertical_resolution_display_unit == 2);
  CHECK(document.print_settings().width_display_unit == 4);
  CHECK(document.print_settings().height_display_unit == 4);

  // A resolution edit keeps the file's display units; only the values move.
  document.print_settings().horizontal_ppi = 240.0;
  document.print_settings().vertical_ppi = 240.0;
  const auto exported = psd_raw_image_resources(patchy::psd::DocumentIo::write_flat_rgb8(document));
  const std::vector<std::uint8_t> expected_payload{
      0x00, 0xF0, 0x00, 0x00, 0x00, 0x02, 0x00, 0x04,
      0x00, 0xF0, 0x00, 0x00, 0x00, 0x02, 0x00, 0x04};
  CHECK(test_image_resource_payload(exported, 1005).value() == expected_payload);
}

void psd_grid_guides_resource_round_trip_and_replaces_duplicates() {
  const auto grid_guides_payload =
      psd_grid_guides_payload(640, 960, {{321, patchy::GuideOrientation::Vertical},
                                         {1344, patchy::GuideOrientation::Horizontal}});
  const auto duplicate_grid_guides_payload = psd_grid_guides_payload(32, 32, {});
  const std::vector<std::uint8_t> unrelated_payload{7, 8, 9, 10, 11};

  patchy::psd::BigEndianWriter resources;
  write_test_image_resource(resources, 1032, "first", grid_guides_payload);
  write_test_image_resource(resources, 2000, "raw", unrelated_payload);
  write_test_image_resource(resources, 1032, "duplicate", duplicate_grid_guides_payload);

  patchy::psd::BigEndianWriter writer;
  patchy::psd::write_header(writer, patchy::psd::Header{false, 3, 2, 2, 8, 3});
  writer.write_u32(0);
  writer.write_u32(static_cast<std::uint32_t>(resources.bytes().size()));
  writer.write_bytes(resources.bytes());
  writer.write_u32(0);
  writer.write_u16(0);
  for (int channel = 0; channel < 3; ++channel) {
    for (int pixel = 0; pixel < 4; ++pixel) {
      writer.write_u8(static_cast<std::uint8_t>(channel * 20 + pixel));
    }
  }

  auto document = patchy::psd::DocumentIo::read(writer.bytes());
  CHECK(document.metadata().raw_psd_image_resources == resources.bytes());
  CHECK(document.grid_settings().horizontal_cycle_32 == 640);
  CHECK(document.grid_settings().vertical_cycle_32 == 960);
  CHECK(document.guides().size() == 2);
  CHECK(document.guides()[0].orientation == patchy::GuideOrientation::Vertical);
  CHECK(document.guides()[0].position_32 == 321);
  CHECK(document.guides()[1].orientation == patchy::GuideOrientation::Horizontal);
  CHECK(document.guides()[1].position_32 == 1344);

  const auto flat_resources = psd_raw_image_resources(patchy::psd::DocumentIo::write_flat_rgb8(document));
  CHECK(test_image_resource_payload(flat_resources, 1032).value() == grid_guides_payload);
  CHECK(test_image_resource_payload(flat_resources, 2000).value() == unrelated_payload);
  CHECK(test_image_resource_count(flat_resources, 1032) == 1);

  document.grid_settings().horizontal_cycle_32 = 320;
  document.grid_settings().vertical_cycle_32 = 384;
  document.guides().clear();
  document.guides().push_back(patchy::DocumentGuide{patchy::GuideOrientation::Horizontal, 512});
  const auto replacement_payload =
      psd_grid_guides_payload(320, 384, {{512, patchy::GuideOrientation::Horizontal}});
  const auto layered_resources = psd_raw_image_resources(patchy::psd::DocumentIo::write_layered_rgb8(document));
  CHECK(test_image_resource_payload(layered_resources, 1032).value() == replacement_payload);
  CHECK(test_image_resource_payload(layered_resources, 2000).value() == unrelated_payload);
  CHECK(test_image_resource_count(layered_resources, 1032) == 1);

  patchy::Document blank(2, 2, patchy::PixelFormat::rgb8());
  blank.add_pixel_layer("Background", solid_rgb(2, 2, 0, 0, 0));
  const auto blank_resources = psd_raw_image_resources(patchy::psd::DocumentIo::write_flat_rgb8(blank));
  CHECK(!test_image_resource_payload(blank_resources, 1032).has_value());
}

void psd_layered_rgb8_round_trips_pixel_layers() {
  patchy::Document document(3, 2, patchy::PixelFormat::rgb8());
  auto& background = document.add_pixel_layer("Background", solid_rgb(3, 2, 255, 255, 255));
  background.set_opacity(1.0F);

  auto top_pixels = solid_rgba(2, 1, 200, 10, 20, 128);
  patchy::Layer bounded_layer(document.allocate_layer_id(), "Paint", std::move(top_pixels));
  auto& top = document.add_layer(std::move(bounded_layer));
  top.set_bounds(patchy::Rect{1, 1, 2, 1});
  top.set_opacity(0.75F);
  top.set_blend_mode(patchy::BlendMode::Multiply);

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  CHECK(patchy::psd::DocumentIo::can_read(bytes));

  const auto read = patchy::psd::DocumentIo::read(bytes);
  CHECK(read.width() == 3);
  CHECK(read.height() == 2);
  CHECK(read.layers().size() == 2);
  CHECK(read.layers()[0].name() == "Background");
  CHECK(read.layers()[1].name() == "Paint");
  CHECK(read.layers()[1].bounds().x == 1);
  CHECK(read.layers()[1].bounds().y == 1);
  CHECK(read.layers()[1].pixels().format() == patchy::PixelFormat::rgba8());
  CHECK(read.layers()[1].blend_mode() == patchy::BlendMode::Multiply);
  CHECK(read.layers()[1].pixels().pixel(0, 0)[0] == 200);
  CHECK(read.layers()[1].pixels().pixel(0, 0)[3] == 128);
}

// Old Photoshop writes empty layers with zero-length channel data: no payload and no
// 2-byte compression marker at all. Builds a 1x1 RGB PSD whose bottom layer is a normal
// 1x1 pixel layer and whose top layer is such an empty layer.
std::vector<std::uint8_t> psd_with_zero_length_channel_layer() {
  patchy::psd::BigEndianWriter layer_info;
  layer_info.write_u16(2);

  layer_info.write_u32(0);
  layer_info.write_u32(0);
  layer_info.write_u32(1);
  layer_info.write_u32(1);
  layer_info.write_u16(4);
  layer_info.write_u16(0xFFFF);
  layer_info.write_u32(3);
  layer_info.write_u16(0);
  layer_info.write_u32(3);
  layer_info.write_u16(1);
  layer_info.write_u32(3);
  layer_info.write_u16(2);
  layer_info.write_u32(3);
  write_ascii4(layer_info, "8BIM");
  write_ascii4(layer_info, "norm");
  layer_info.write_u8(255);
  layer_info.write_u8(0);
  layer_info.write_u8(0);
  layer_info.write_u8(0);
  patchy::psd::BigEndianWriter dot_extra;
  dot_extra.write_u32(0);
  dot_extra.write_u32(0);
  write_pascal_padded(dot_extra, "Dot", 4);
  layer_info.write_u32(static_cast<std::uint32_t>(dot_extra.bytes().size()));
  layer_info.write_bytes(dot_extra.bytes());

  layer_info.write_u32(0);
  layer_info.write_u32(0);
  layer_info.write_u32(0);
  layer_info.write_u32(0);
  layer_info.write_u16(4);
  layer_info.write_u16(0xFFFF);
  layer_info.write_u32(0);
  layer_info.write_u16(0);
  layer_info.write_u32(0);
  layer_info.write_u16(1);
  layer_info.write_u32(0);
  layer_info.write_u16(2);
  layer_info.write_u32(0);
  write_ascii4(layer_info, "8BIM");
  write_ascii4(layer_info, "norm");
  layer_info.write_u8(255);
  layer_info.write_u8(0);
  layer_info.write_u8(0);
  layer_info.write_u8(0);
  patchy::psd::BigEndianWriter empty_extra;
  empty_extra.write_u32(0);
  empty_extra.write_u32(0);
  write_pascal_padded(empty_extra, "Empty", 4);
  layer_info.write_u32(static_cast<std::uint32_t>(empty_extra.bytes().size()));
  layer_info.write_bytes(empty_extra.bytes());

  // Channel data in record order: the pixel layer's four raw channels (A, R, G, B); the
  // empty layer contributes no bytes at all.
  for (const std::uint8_t value : std::array<std::uint8_t, 4>{200, 10, 20, 30}) {
    layer_info.write_u16(0);
    layer_info.write_u8(value);
  }
  if ((layer_info.bytes().size() % 2U) != 0) {
    layer_info.write_u8(0);
  }

  patchy::psd::BigEndianWriter layer_mask;
  layer_mask.write_u32(static_cast<std::uint32_t>(layer_info.bytes().size()));
  layer_mask.write_bytes(layer_info.bytes());
  layer_mask.write_u32(0);

  patchy::psd::BigEndianWriter writer;
  patchy::psd::write_header(writer, patchy::psd::Header{false, 3, 1, 1, 8, 3});
  writer.write_u32(0);
  writer.write_u32(0);
  writer.write_u32(static_cast<std::uint32_t>(layer_mask.bytes().size()));
  writer.write_bytes(layer_mask.bytes());
  writer.write_u16(0);
  writer.write_u8(255);
  writer.write_u8(255);
  writer.write_u8(255);
  return writer.bytes();
}

void psd_zero_length_layer_channels_read_as_empty() {
  const auto read = patchy::psd::DocumentIo::read(psd_with_zero_length_channel_layer());
  CHECK(read.width() == 1);
  CHECK(read.height() == 1);
  CHECK(read.layers().size() == 2);
  CHECK(read.layers()[0].name() == "Dot");
  CHECK(read.layers()[0].pixels().pixel(0, 0)[0] == 10);
  CHECK(read.layers()[0].pixels().pixel(0, 0)[1] == 20);
  CHECK(read.layers()[0].pixels().pixel(0, 0)[2] == 30);
  CHECK(read.layers()[0].pixels().pixel(0, 0)[3] == 200);
  CHECK(read.layers()[1].name() == "Empty");
  CHECK(read.layers()[1].bounds().width == 0);
  CHECK(read.layers()[1].bounds().height == 0);
}

void psd_layered_writer_uses_rle_for_compressible_layer_channels() {
  patchy::Document document(32, 4, patchy::PixelFormat::rgb8());
  auto& layer = document.add_pixel_layer("Masked", solid_rgba(32, 4, 10, 20, 30, 128));

  patchy::PixelBuffer mask_pixels(32, 4, patchy::PixelFormat::gray8());
  mask_pixels.clear(255);
  layer.set_mask(patchy::LayerMask{patchy::Rect{0, 0, 32, 4}, std::move(mask_pixels), 0, false});

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto channels = psd_layer_channel_records(bytes);
  CHECK(channels.size() == 5U);
  CHECK(std::all_of(channels.begin(), channels.end(),
                    [](const PsdLayerChannelRecord& channel) { return channel.compression == 1U; }));
  CHECK(std::any_of(channels.begin(), channels.end(),
                    [](const PsdLayerChannelRecord& channel) { return channel.id == -1; }));
  CHECK(std::any_of(channels.begin(), channels.end(),
                    [](const PsdLayerChannelRecord& channel) { return channel.id == -2; }));

  const auto read = patchy::psd::DocumentIo::read(bytes);
  CHECK(read.layers().size() == 1);
  CHECK(read.layers().front().pixels().pixel(0, 0)[3] == 128);
  CHECK(read.layers().front().mask().has_value());
  CHECK(*read.layers().front().mask()->pixels.pixel(31, 3) == 255);
}

void psd_layer_locks_import_and_export_lspf() {
  for (const auto flags :
       {patchy::kLayerLockTransparentPixels, patchy::kLayerLockImagePixels, patchy::kLayerLockPosition,
        patchy::kLayerLockAll}) {
    patchy::Document document(2, 2, patchy::PixelFormat::rgb8());
    auto& layer = document.add_pixel_layer("Locked", solid_rgba(2, 2, 20, 40, 60, 255));
    patchy::set_layer_lock_flags(layer, flags);

    const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
    const auto payload = psd_layer_block_payload(psd_first_layer_extra_data(bytes), "lspf");
    CHECK(payload.has_value());
    CHECK(read_u32_be_at(*payload, 0) == flags);

    const auto read = patchy::psd::DocumentIo::read(bytes);
    CHECK(read.layers().size() == 1);
    CHECK(patchy::layer_lock_flags(read.layers().front()) == flags);
  }
}

void psd_layer_masks_render_and_round_trip() {
  patchy::Document document(4, 2, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(4, 2, 255, 255, 255));
  auto& top = document.add_pixel_layer("Masked Red", solid_rgb(4, 2, 220, 20, 20));

  patchy::PixelBuffer mask_pixels(2, 2, patchy::PixelFormat::gray8());
  mask_pixels.clear(255);
  top.set_mask(patchy::LayerMask{patchy::Rect{0, 0, 2, 2}, std::move(mask_pixels), 0, false});

  auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  CHECK(flattened.pixel(0, 0)[0] == 220);
  CHECK(flattened.pixel(3, 0)[0] == 255);

  const auto read = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(document));
  CHECK(read.layers().size() == 2);
  const auto& read_top = read.layers()[1];
  CHECK(read_top.mask().has_value());
  CHECK(read_top.mask()->bounds.x == 0);
  CHECK(read_top.mask()->bounds.width == 2);
  CHECK(read_top.mask()->default_color == 0);
  CHECK(*read_top.mask()->pixels.pixel(1, 1) == 255);

  flattened = patchy::Compositor{}.flatten_rgb8(read);
  CHECK(flattened.pixel(0, 0)[0] == 220);
  CHECK(flattened.pixel(3, 0)[0] == 255);
}

std::uint8_t psd_first_layer_mask_flags(std::span<const std::uint8_t> bytes) {
  const auto extra_data = psd_first_layer_extra_data(bytes);
  patchy::psd::BigEndianReader reader(extra_data);
  const auto mask_length = reader.read_u32();
  CHECK(mask_length >= 18U);
  reader.skip(16);  // mask rectangle
  reader.skip(1);   // default color
  return reader.read_u8();
}

void psd_layer_mask_link_state_round_trips() {
  for (const auto linked : {true, false}) {
    patchy::Document document(4, 2, patchy::PixelFormat::rgb8());
    auto& layer = document.add_pixel_layer("Masked", solid_rgb(4, 2, 220, 20, 20));
    patchy::PixelBuffer mask_pixels(2, 2, patchy::PixelFormat::gray8());
    mask_pixels.clear(255);
    layer.set_mask(patchy::LayerMask{patchy::Rect{0, 0, 2, 2}, std::move(mask_pixels), 0, false});
    patchy::set_layer_mask_linked(layer, linked);

    const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
    // Photoshop persists the layer/mask chain toggle as bit 0 of the mask flags byte.
    CHECK(psd_first_layer_mask_flags(bytes) == (linked ? 0x00U : 0x01U));

    const auto read = patchy::psd::DocumentIo::read(bytes);
    CHECK(read.layers().size() == 1);
    CHECK(patchy::layer_mask_linked(read.layers().front()) == linked);
  }
}

void psd_legacy_document_alpha_marker_stays_a_layer_mask() {
  // The old import marker no longer promotes a layer mask into a saved PSD alpha.
  // A layered save keeps it as the layer's real -2 mask; merged transparency is
  // derived separately and must not reappear as a saved document channel.
  patchy::Document document(4, 2, patchy::PixelFormat::rgb8());
  auto& layer = document.add_pixel_layer("Flat", solid_rgb(4, 2, 30, 90, 200));
  patchy::PixelBuffer mask_pixels(4, 2, patchy::PixelFormat::gray8());
  mask_pixels.clear(128);
  layer.set_mask(patchy::LayerMask{patchy::Rect{0, 0, 4, 2}, std::move(mask_pixels), 255, false});
  patchy::set_layer_mask_is_document_alpha(layer, true);
  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  CHECK(psd_signed_layer_count(bytes) == -1);
  const auto records = psd_layer_channel_records(bytes);
  CHECK(std::any_of(records.begin(), records.end(),
                    [](const PsdLayerChannelRecord& record) { return record.id == -2; }));
  const auto reread = patchy::psd::DocumentIo::read(bytes);
  CHECK(reread.layers().size() == 1);
  const auto& recovered = reread.layers().front();
  CHECK(recovered.mask().has_value());
  CHECK(recovered.mask()->pixels.pixel(1, 1)[0] == 128);
  CHECK(!patchy::layer_mask_is_document_alpha(recovered));
  CHECK(reread.channels().empty());
}

void psd_psb_saved_channels_round_trip_names_pixels_and_metadata() {
  const std::string unicode_name =
      "\xE3\x82\xB9\xE3\x83\x9D\xE3\x83\x83\xE3\x83\x88";  // Japanese "Spot".
  const auto spot_record = test_channel_display_record(patchy::RgbColor{9, 80, 210}, 73, 2);

  for (const bool large_document : {false, true}) {
    for (const bool compressible : {false, true}) {
      const std::int32_t width = compressible ? 32 : 128;
      const std::int32_t height = compressible ? 4 : 1;
      patchy::Document document(width, height, patchy::PixelFormat::rgb8());
      patchy::PixelBuffer base(width, height, patchy::PixelFormat::rgb8());
      for (std::int32_t y = 0; y < height; ++y) {
        for (std::int32_t x = 0; x < width; ++x) {
          auto* pixel = base.pixel(x, y);
          pixel[0] = compressible ? 20U : static_cast<std::uint8_t>(x);
          pixel[1] = compressible ? 60U : static_cast<std::uint8_t>(x + 47);
          pixel[2] = compressible ? 100U : static_cast<std::uint8_t>(255 - x);
        }
      }
      document.add_pixel_layer("Background", std::move(base));

      const auto add_channel = [&](std::string name, patchy::DocumentChannelKind kind, std::uint8_t offset) {
        patchy::PixelBuffer pixels(width, height, patchy::PixelFormat::gray8());
        for (std::int32_t y = 0; y < height; ++y) {
          for (std::int32_t x = 0; x < width; ++x) {
            *pixels.pixel(x, y) = compressible
                                      ? offset
                                      : static_cast<std::uint8_t>(offset + x * 37 + y * 19);
          }
        }
        return patchy::DocumentChannel(document.allocate_channel_id(), std::move(name), kind,
                                       std::move(pixels));
      };

      auto first = add_channel("Duplicate", patchy::DocumentChannelKind::Alpha, 11);
      patchy::DocumentChannelDisplayInfo first_display;
      first_display.color = patchy::RgbColor{12, 34, 56};
      first_display.opacity = 0.37F;
      first_display.color_indicates = patchy::DocumentChannelColorIndicates::SelectedAreas;
      first.set_display_info(first_display);
      first.set_photoshop_identifier(std::uint32_t{101});
      document.add_channel(std::move(first));

      document.add_channel(add_channel("Duplicate", patchy::DocumentChannelKind::Alpha, 33));

      auto spot = add_channel(unicode_name, patchy::DocumentChannelKind::Spot, 77);
      patchy::DocumentChannelDisplayInfo spot_display;
      spot_display.color = patchy::RgbColor{9, 80, 210};
      spot_display.opacity = 0.73F;
      // Kind, not normalized presentation metadata, controls resource 1053.
      // The preserved raw record below still describes this channel as a spot.
      spot_display.color_indicates = patchy::DocumentChannelColorIndicates::MaskedAreas;
      spot.set_display_info(spot_display);
      spot.set_raw_photoshop_display_info(spot_record);
      spot.set_photoshop_identifier(std::uint32_t{0x12345678U});
      document.add_channel(std::move(spot));

      patchy::psd::WriteOptions options;
      options.large_document = large_document;
      const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document, options);
      const auto header = test_psd_header(bytes);
      CHECK(header.large_document == large_document);
      CHECK(header.channels == 6);
      CHECK(psd_composite_compression(bytes) == (compressible ? 1U : 0U));
      CHECK(test_image_resource_payload(psd_raw_image_resources(bytes), 1053).value() ==
            test_alpha_identifiers_payload({101, 1}));

      const auto read = patchy::psd::DocumentIo::read(bytes);
      CHECK(read.channels().size() == 3);
      CHECK(read.channels()[0].name() == "Duplicate");
      CHECK(read.channels()[1].name() == "Duplicate");
      CHECK(read.channels()[2].name() == unicode_name);
      CHECK(read.channels()[0].kind() == patchy::DocumentChannelKind::Alpha);
      CHECK(read.channels()[2].kind() == patchy::DocumentChannelKind::Spot);
      CHECK(read.channels()[0].photoshop_identifier() == std::optional<std::uint32_t>{101});
      CHECK(!read.channels()[2].photoshop_identifier().has_value());
      CHECK(read.channels()[0].display_info().color.red == 12);
      CHECK(read.channels()[0].display_info().color.green == 34);
      CHECK(read.channels()[0].display_info().color.blue == 56);
      CHECK(std::abs(read.channels()[0].display_info().opacity - 0.37F) < 0.001F);
      CHECK(read.channels()[0].display_info().color_indicates ==
            patchy::DocumentChannelColorIndicates::SelectedAreas);
      CHECK(read.channels()[2].raw_photoshop_display_info() == spot_record);
      CHECK(read.channels()[2].display_info().color_indicates ==
            patchy::DocumentChannelColorIndicates::SpotColor);

      const auto last_x = width - 1;
      const auto last_y = height - 1;
      const auto expected_first = compressible
                                      ? std::uint8_t{11}
                                      : static_cast<std::uint8_t>(11 + last_x * 37 + last_y * 19);
      const auto expected_spot = compressible
                                     ? std::uint8_t{77}
                                     : static_cast<std::uint8_t>(77 + last_x * 37 + last_y * 19);
      CHECK(read.channels()[0].pixels().pixel(last_x, last_y)[0] == expected_first);
      CHECK(read.channels()[2].pixels().pixel(last_x, last_y)[0] == expected_spot);
    }
  }
}

void psd_photoshop_saved_channels_fixture_imports_and_resaves() {
  // Photoshop 2026-authored ground truth: an opaque RGB document with two
  // duplicate-named saved alpha channels and one Unicode-named spot channel.
  // The three RGB components are derived; exactly these three stored channels
  // must follow them, with no phantom merged-transparency channel.
  const auto fixture_path =
      patchy::test::committed_psd_fixture_path("photoshop-saved-channels.psd");
  const auto document = patchy::psd::DocumentIo::read_file(fixture_path);
  CHECK(document.width() == 16);
  CHECK(document.height() == 12);
  CHECK(document.layers().size() == 1);
  CHECK(!document.layers().front().mask().has_value());
  CHECK(document.channels().size() == 3);
  const auto legacy_names = test_alpha_channel_names_payload(
      {"Duplicate", "Duplicate", "???????"});
  CHECK(test_image_resource_payload(document.metadata().raw_psd_image_resources, 1006).value() ==
        legacy_names);

  const std::string unicode_name =
      "\xE7\x89\xB9\xE8\x89\xB2\xE3\x83\x81\xE3\x83\xA3\xE3\x83\xB3\xE3\x83\x8D\xE3\x83\xAB";
  const auto& alpha_masked = document.channels()[0];
  const auto& alpha_selected = document.channels()[1];
  const auto& spot = document.channels()[2];
  CHECK(alpha_masked.name() == "Duplicate");
  CHECK(alpha_selected.name() == "Duplicate");
  CHECK(spot.name() == unicode_name);
  CHECK(alpha_masked.kind() == patchy::DocumentChannelKind::Alpha);
  CHECK(alpha_selected.kind() == patchy::DocumentChannelKind::Alpha);
  CHECK(spot.kind() == patchy::DocumentChannelKind::Spot);

  const auto check_display = [](const patchy::DocumentChannel& channel, patchy::RgbColor color,
                                float opacity, patchy::DocumentChannelColorIndicates mode) {
    CHECK(channel.display_info().color.red == color.red);
    CHECK(channel.display_info().color.green == color.green);
    CHECK(channel.display_info().color.blue == color.blue);
    CHECK(std::abs(channel.display_info().opacity - opacity) < 0.001F);
    CHECK(channel.display_info().color_indicates == mode);
    CHECK(channel.raw_photoshop_display_info().size() == 13U);
  };
  check_display(alpha_masked, patchy::RgbColor{12, 34, 56}, 0.37F,
                patchy::DocumentChannelColorIndicates::MaskedAreas);
  check_display(alpha_selected, patchy::RgbColor{210, 120, 30}, 0.61F,
                patchy::DocumentChannelColorIndicates::SelectedAreas);
  check_display(spot, patchy::RgbColor{12, 34, 210}, 0.63F,
                patchy::DocumentChannelColorIndicates::SpotColor);
  CHECK(alpha_masked.photoshop_identifier() == std::optional<std::uint32_t>{3});
  CHECK(alpha_selected.photoshop_identifier() == std::optional<std::uint32_t>{4});
  CHECK(!spot.photoshop_identifier().has_value());

  CHECK(alpha_masked.pixels().pixel(0, 0)[0] == 255);
  CHECK(alpha_masked.pixels().pixel(4, 0)[0] == 128);
  CHECK(alpha_masked.pixels().pixel(8, 0)[0] == 0);
  CHECK(alpha_selected.pixels().pixel(0, 0)[0] == 64);
  CHECK(alpha_selected.pixels().pixel(8, 4)[0] == 200);
  CHECK(spot.pixels().pixel(12, 8)[0] == 0);
  CHECK(spot.pixels().pixel(0, 8)[0] == 96);
  CHECK(spot.pixels().pixel(8, 8)[0] == 255);

  std::filesystem::create_directories("test-artifacts");
  const auto patchy_path =
      std::filesystem::path("test-artifacts") / "photoshop-saved-channels-patchy.psd";
  const auto patchy_bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  CHECK(test_image_resource_payload(psd_raw_image_resources(patchy_bytes), 1006).value() ==
        legacy_names);
  patchy::psd::DocumentIo::write_layered_rgb8_file(document, patchy_path);
  const auto reread = patchy::psd::DocumentIo::read_file(patchy_path);
  CHECK(reread.channels().size() == document.channels().size());
  for (std::size_t index = 0; index < document.channels().size(); ++index) {
    const auto& before = document.channels()[index];
    const auto& after = reread.channels()[index];
    CHECK(after.name() == before.name());
    CHECK(after.kind() == before.kind());
    CHECK(after.photoshop_identifier() == before.photoshop_identifier());
    CHECK(after.raw_photoshop_display_info() == before.raw_photoshop_display_info());
    CHECK(after.pixels().data().size() == before.pixels().data().size());
    CHECK(std::equal(after.pixels().data().begin(), after.pixels().data().end(),
                     before.pixels().data().begin()));
  }
}

void psd_legacy_channel_name_fallback_counts_unicode_scalars() {
  patchy::Document document(1, 1, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(1, 1, 255, 255, 255));

  std::string name(254, 'A');
  name += "\xF0\x9F\x98\x80";  // One supplementary Unicode scalar.
  name += "tail retained only in resource 1045";
  patchy::PixelBuffer pixels(1, 1, patchy::PixelFormat::gray8());
  pixels.clear(127);
  document.add_channel(patchy::DocumentChannel(document.allocate_channel_id(), name,
                                                patchy::DocumentChannelKind::Alpha,
                                                std::move(pixels)));

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto legacy = test_image_resource_payload(psd_raw_image_resources(bytes), 1006).value();
  std::vector<std::uint8_t> expected{255U};
  expected.insert(expected.end(), 254U, static_cast<std::uint8_t>('A'));
  expected.push_back(static_cast<std::uint8_t>('?'));
  CHECK(legacy == expected);

  const auto reread = patchy::psd::DocumentIo::read(bytes);
  CHECK(reread.channels().size() == 1);
  CHECK(reread.channels().front().name() == name);
}

void psd_saved_channel_coexists_with_real_layer_mask() {
  patchy::Document document(6, 4, patchy::PixelFormat::rgb8());
  auto& layer = document.add_pixel_layer("Masked", solid_rgb(6, 4, 200, 40, 20));
  patchy::PixelBuffer mask_pixels(4, 2, patchy::PixelFormat::gray8());
  mask_pixels.clear(128);
  layer.set_mask(patchy::LayerMask{patchy::Rect{1, 1, 4, 2}, std::move(mask_pixels), 255, false});

  patchy::PixelBuffer alpha(6, 4, patchy::PixelFormat::gray8());
  alpha.clear(29);
  document.add_channel(patchy::DocumentChannel(document.allocate_channel_id(), "Saved Alpha",
                                                patchy::DocumentChannelKind::Alpha, std::move(alpha)));

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto records = psd_layer_channel_records(bytes);
  CHECK(std::any_of(records.begin(), records.end(),
                    [](const PsdLayerChannelRecord& record) { return record.id == -2; }));
  CHECK(psd_signed_layer_count(bytes) == -1);  // mask transparency is the derived first extra plane.

  const auto read = patchy::psd::DocumentIo::read(bytes);
  CHECK(read.layers().size() == 1);
  CHECK(read.layers().front().mask().has_value());
  CHECK(read.layers().front().mask()->pixels.pixel(0, 0)[0] == 128);
  CHECK(read.channels().size() == 1);
  CHECK(read.channels().front().name() == "Saved Alpha");
  CHECK(read.channels().front().pixels().pixel(5, 3)[0] == 29);
}

void psd_merged_transparency_is_structural_before_saved_channels() {
  patchy::Document transparent(4, 3, patchy::PixelFormat::rgba8());
  transparent.add_pixel_layer("Paint", solid_rgba(4, 3, 30, 100, 220, 128));
  patchy::PixelBuffer saved_pixels(4, 3, patchy::PixelFormat::gray8());
  saved_pixels.clear(61);
  auto saved = patchy::DocumentChannel(transparent.allocate_channel_id(), "Saved After Transparency",
                                       patchy::DocumentChannelKind::Alpha, std::move(saved_pixels));
  saved.set_photoshop_identifier(std::uint32_t{700});
  transparent.add_channel(std::move(saved));

  const auto transparent_bytes = patchy::psd::DocumentIo::write_layered_rgb8(transparent);
  CHECK(test_psd_header(transparent_bytes).channels == 5);
  CHECK(psd_signed_layer_count(transparent_bytes) == -1);
  const auto transparent_read = patchy::psd::DocumentIo::read(transparent_bytes);
  CHECK(transparent_read.channels().size() == 1);
  CHECK(transparent_read.channels().front().name() == "Saved After Transparency");
  CHECK(transparent_read.channels().front().photoshop_identifier() == std::optional<std::uint32_t>{700});
  CHECK(transparent_read.channels().front().pixels().pixel(3, 2)[0] == 61);
  CHECK(!transparent_read.layers().front().mask().has_value());

  // The label alone has no special meaning. With a positive layer count, a saved
  // channel literally named "Transparency" remains a normal editable channel.
  patchy::Document opaque(4, 3, patchy::PixelFormat::rgb8());
  opaque.add_pixel_layer("Background", solid_rgb(4, 3, 30, 100, 220));
  patchy::PixelBuffer literal_pixels(4, 3, patchy::PixelFormat::gray8());
  literal_pixels.clear(147);
  opaque.add_channel(patchy::DocumentChannel(opaque.allocate_channel_id(), "Transparency",
                                              patchy::DocumentChannelKind::Alpha,
                                              std::move(literal_pixels)));
  const auto literal_bytes = patchy::psd::DocumentIo::write_layered_rgb8(opaque);
  CHECK(psd_signed_layer_count(literal_bytes) == 1);
  const auto literal_read = patchy::psd::DocumentIo::read(literal_bytes);
  CHECK(literal_read.channels().size() == 1);
  CHECK(literal_read.channels().front().name() == "Transparency");
  CHECK(literal_read.channels().front().pixels().pixel(0, 0)[0] == 147);
}

void psd_cmyk_extra_plane_imports_as_saved_channel() {
  patchy::psd::BigEndianWriter resources;
  const auto names = test_alpha_channel_names_payload({"Ink Mask"});
  const auto identifiers = test_alpha_identifiers_payload({505});
  write_test_image_resource(resources, 1006, "", names);
  write_test_image_resource(resources, 1053, "", identifiers);

  const std::vector<std::vector<std::uint8_t>> planes{
      {255, 255},  // cyan
      {0, 255},    // magenta
      {0, 255},    // yellow
      {255, 127},  // black
      {7, 201},    // saved channel (after all four CMYK components)
  };
  for (const std::uint16_t compression : {std::uint16_t{0}, std::uint16_t{1}}) {
    const auto bytes = flat_psd_with_test_planes(false, 4, 2, 1, planes, resources.bytes(), compression);
    const auto read = patchy::psd::DocumentIo::read(bytes);
    CHECK(read.metadata().values.at("psd.color_mode") == "CMYK");
    CHECK(read.layers().size() == 1);
    CHECK(read.channels().size() == 1);
    CHECK(read.channels().front().name() == "Ink Mask");
    CHECK(read.channels().front().photoshop_identifier() == std::optional<std::uint32_t>{505});
    CHECK(read.channels().front().pixels().pixel(0, 0)[0] == 7);
    CHECK(read.channels().front().pixels().pixel(1, 0)[0] == 201);
  }
}

void psd_saved_channel_resource_mismatches_use_fallback_names() {
  const std::vector<std::vector<std::uint8_t>> planes{
      {10, 20}, {30, 40}, {50, 60}, {70, 80}, {90, 100},
  };

  patchy::psd::BigEndianWriter partial_resources;
  const auto one_name = test_alpha_channel_names_payload({"Named Only"});
  const auto one_identifier = test_alpha_identifiers_payload({99});
  const auto one_display =
      test_display_info_float_payload({test_channel_display_record(patchy::RgbColor{200, 20, 40}, 80, 2)});
  write_test_image_resource(partial_resources, 1006, "", one_name);
  write_test_image_resource(partial_resources, 1053, "", one_identifier);
  write_test_image_resource(partial_resources, 1077, "", one_display);

  const auto partial = patchy::psd::DocumentIo::read(
      flat_psd_with_test_planes(false, 3, 2, 1, planes, partial_resources.bytes()));
  CHECK(partial.channels().size() == 2);
  CHECK(partial.channels()[0].name() == "Named Only");
  CHECK(partial.channels()[1].name() == "Alpha 2");
  CHECK(partial.channels()[0].kind() == patchy::DocumentChannelKind::Spot);
  CHECK(partial.channels()[1].kind() == patchy::DocumentChannelKind::Alpha);
  CHECK(!partial.channels()[0].photoshop_identifier().has_value());
  CHECK(partial.channels()[1].photoshop_identifier() == std::optional<std::uint32_t>{99});

  // When the floating-point display resource (1077) is absent, the legacy 1007
  // 14-byte records remain authoritative and are retained byte-for-byte.
  patchy::psd::BigEndianWriter legacy_resources;
  const auto legacy_record =
      test_channel_display_record(patchy::RgbColor{15, 120, 230}, 65, 2, true);
  write_test_image_resource(legacy_resources, 1007, "", legacy_record);
  const auto legacy = patchy::psd::DocumentIo::read(
      flat_psd_with_test_planes(false, 3, 2, 1, planes, legacy_resources.bytes()));
  CHECK(legacy.channels().size() == 2);
  CHECK(legacy.channels()[0].kind() == patchy::DocumentChannelKind::Spot);
  CHECK(legacy.channels()[0].raw_photoshop_display_info() == legacy_record);
  CHECK(std::abs(legacy.channels()[0].display_info().opacity - 0.65F) < 0.001F);

  const auto missing =
      patchy::psd::DocumentIo::read(flat_psd_with_test_planes(false, 3, 2, 1, planes));
  CHECK(missing.channels().size() == 2);
  CHECK(missing.channels()[0].name() == "Alpha 1");
  CHECK(missing.channels()[1].name() == "Alpha 2");
}

void psd_opaque_allows_53_saved_channels_but_transparent_throws() {
  patchy::Document opaque(1, 1, patchy::PixelFormat::rgb8());
  opaque.add_pixel_layer("Background", solid_rgb(1, 1, 20, 40, 60));
  for (std::size_t index = 0; index < opaque.maximum_saved_channel_count(); ++index) {
    patchy::PixelBuffer pixels(1, 1, patchy::PixelFormat::gray8());
    pixels.clear(static_cast<std::uint8_t>(index));
    opaque.add_channel(patchy::DocumentChannel(opaque.allocate_channel_id(),
                                                "Alpha " + std::to_string(index + 1U),
                                                patchy::DocumentChannelKind::Alpha,
                                                std::move(pixels)));
  }
  CHECK(opaque.channels().size() == 53);
  const auto opaque_bytes = patchy::psd::DocumentIo::write_layered_rgb8(opaque);
  CHECK(test_psd_header(opaque_bytes).channels == 56);
  CHECK(patchy::psd::DocumentIo::read(opaque_bytes).channels().size() == 53);

  auto transparent = opaque;
  transparent.layers().front().set_pixels(solid_rgba(1, 1, 20, 40, 60, 0));
  bool threw = false;
  try {
    (void)patchy::psd::DocumentIo::write_layered_rgb8(transparent);
  } catch (const std::runtime_error&) {
    threw = true;
  }
  CHECK(threw);
}

void psb_transparency_channel_is_not_a_layer_mask_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("PSBtest/Content.psb");
  if (!std::filesystem::exists(path)) {
    std::cout << "[SKIP] PSBtest fixture missing: " << path.string() << '\n';
    return;
  }
  // The real Photoshop file behind the bug report: one text layer on a transparent
  // canvas, 4 composite channels, resource 1006 = "Transparency". Photoshop shows no
  // layer mask; neither may Patchy.
  const auto document = patchy::psd::DocumentIo::read_file(path);
  CHECK(document.layers().size() == 1);
  const auto& layer = document.layers().front();
  CHECK(patchy::layer_is_text(layer));
  CHECK(!layer.mask().has_value());
}

std::uint8_t psd_first_layer_record_flags(std::span<const std::uint8_t> bytes) {
  patchy::psd::BigEndianReader reader(bytes);
  (void)patchy::psd::read_header(reader);
  reader.skip(reader.read_u32());  // color mode data
  reader.skip(reader.read_u32());  // image resources
  (void)reader.read_u32();         // layer and mask info length
  (void)reader.read_u32();         // layer info length
  const auto layer_count = static_cast<std::int16_t>(reader.read_u16());
  CHECK(layer_count != 0);
  reader.skip(16);  // bounds
  const auto channel_count = reader.read_u16();
  reader.skip(static_cast<std::size_t>(channel_count) * 6U);
  reader.skip(8);  // blend signature + key
  reader.skip(2);  // opacity + clipping
  return reader.read_u8();
}

void psd_layer_record_flags_mark_photoshop5_layers() {
  for (const auto visible : {true, false}) {
    patchy::Document document(4, 2, patchy::PixelFormat::rgb8());
    auto& layer = document.add_pixel_layer("Layer", solid_rgb(4, 2, 10, 20, 30));
    layer.set_visible(visible);
    const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
    // Bit 3 ("Photoshop 5.0 and later") must be set; without it Photoshop applies
    // legacy semantics, e.g. treating an unlinked mask rectangle as layer-relative.
    CHECK(psd_first_layer_record_flags(bytes) == (visible ? 0x08U : 0x0AU));
  }
}

// interface_mock2.psd (2018) carries an empty layer whose channels record zero-length
// data (no compression marker): the file must load rather than fail with "Invalid PSD
// layer channel length".
void psd_interface_mock2_loads_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("interface_mock2.psd");
  if (!std::filesystem::exists(path)) {
    return;
  }

  const auto document = patchy::psd::DocumentIo::read_file(path);
  CHECK(document.width() == 1024);
  CHECK(document.height() == 600);
  CHECK(document.layers().size() == 11);

  const auto& empty_layer = document.layers()[1];
  CHECK(empty_layer.name() == "inventory");
  CHECK(empty_layer.bounds().width == 0);
  CHECK(empty_layer.bounds().height == 0);

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  CHECK(flattened.width() == 1024);
  CHECK(flattened.height() == 600);
}

std::vector<std::uint8_t> zlib_deflate(std::span<const std::uint8_t> raw) {
  std::vector<std::uint8_t> compressed(mz_compressBound(static_cast<mz_ulong>(raw.size())));
  mz_ulong compressed_length = static_cast<mz_ulong>(compressed.size());
  CHECK(mz_compress(compressed.data(), &compressed_length, raw.data(),
                    static_cast<mz_ulong>(raw.size())) == MZ_OK);
  compressed.resize(compressed_length);
  return compressed;
}

// 16-bit samples are full-range big-endian u16; the loader converts value/257 rounded.
constexpr std::array<std::uint16_t, 5> kDeep16Samples{0, 128, 256, 32768, 65535};
constexpr std::array<std::uint8_t, 5> kDeep16Expected{0, 0, 1, 128, 255};

void psd_16_bit_flat_raw_composite_converts_to_8_bit() {
  patchy::psd::BigEndianWriter writer;
  patchy::psd::write_header(writer, patchy::psd::Header{false, 3, 1, 5, 16, 3});
  writer.write_u32(0);
  writer.write_u32(0);
  writer.write_u32(0);
  writer.write_u16(0);
  for (int channel = 0; channel < 3; ++channel) {
    for (const auto sample : kDeep16Samples) {
      writer.write_u16(sample);
    }
  }

  std::vector<std::string> notices;
  patchy::psd::ReadOptions options;
  options.notices = &notices;
  const auto read = patchy::psd::DocumentIo::read(writer.bytes(), options);
  CHECK(read.format() == patchy::PixelFormat::rgb8());
  CHECK(read.layers().size() == 1);
  for (std::size_t x = 0; x < kDeep16Samples.size(); ++x) {
    const auto* px = read.layers().front().pixels().pixel(static_cast<std::int32_t>(x), 0);
    CHECK(px[0] == kDeep16Expected[x]);
    CHECK(px[1] == kDeep16Expected[x]);
    CHECK(px[2] == kDeep16Expected[x]);
  }
  CHECK(std::any_of(notices.begin(), notices.end(), [](const std::string& notice) {
    return notice.find("16-bit") != std::string::npos;
  }));
}

void psd_16_bit_flat_rle_composite_converts_to_8_bit() {
  patchy::psd::BigEndianWriter row;
  for (const auto sample : kDeep16Samples) {
    row.write_u16(sample);
  }
  patchy::psd::BigEndianWriter encoded_row;
  write_packbits_literal_row(encoded_row, row.bytes());

  patchy::psd::BigEndianWriter writer;
  patchy::psd::write_header(writer, patchy::psd::Header{false, 3, 1, 5, 16, 3});
  writer.write_u32(0);
  writer.write_u32(0);
  writer.write_u32(0);
  writer.write_u16(1);
  for (int channel = 0; channel < 3; ++channel) {
    writer.write_u16(static_cast<std::uint16_t>(encoded_row.bytes().size()));
  }
  for (int channel = 0; channel < 3; ++channel) {
    writer.write_bytes(encoded_row.bytes());
  }

  const auto read = patchy::psd::DocumentIo::read(writer.bytes());
  CHECK(read.layers().size() == 1);
  for (std::size_t x = 0; x < kDeep16Samples.size(); ++x) {
    const auto* px = read.layers().front().pixels().pixel(static_cast<std::int32_t>(x), 0);
    CHECK(px[0] == kDeep16Expected[x]);
    CHECK(px[1] == kDeep16Expected[x]);
    CHECK(px[2] == kDeep16Expected[x]);
  }
}

// 16-bit files keep an empty standard layer info section and store the layers in the
// Lr16 global tagged block; non-empty channels typically use zip-with-prediction.
void psd_16_bit_lr16_layers_convert_with_zip_prediction() {
  // Red decodes to {0xFFFF, 0x8000}: first u16 literal, second delta-encoded.
  const auto red_zip = zlib_deflate(std::array<std::uint8_t, 4>{0xFF, 0xFF, 0x80, 0x01});
  // Blue is plain zip of {0x8080, 0x4040}.
  const auto blue_zip = zlib_deflate(std::array<std::uint8_t, 4>{0x80, 0x80, 0x40, 0x40});

  patchy::psd::BigEndianWriter layer_extra;
  layer_extra.write_u32(0);
  layer_extra.write_u32(0);
  write_pascal_padded(layer_extra, "Deep Layer", 4);

  patchy::psd::BigEndianWriter layer_info;
  layer_info.write_u16(1);
  layer_info.write_u32(0);
  layer_info.write_u32(0);
  layer_info.write_u32(1);
  layer_info.write_u32(2);
  layer_info.write_u16(4);
  layer_info.write_u16(0xFFFFU);
  layer_info.write_u32(6);
  layer_info.write_u16(0);
  layer_info.write_u32(static_cast<std::uint32_t>(2U + red_zip.size()));
  layer_info.write_u16(1);
  layer_info.write_u32(6);
  layer_info.write_u16(2);
  layer_info.write_u32(static_cast<std::uint32_t>(2U + blue_zip.size()));
  write_ascii4(layer_info, "8BIM");
  write_ascii4(layer_info, "norm");
  layer_info.write_u8(255);
  layer_info.write_u8(0);
  layer_info.write_u8(0);
  layer_info.write_u8(0);
  layer_info.write_u32(static_cast<std::uint32_t>(layer_extra.bytes().size()));
  layer_info.write_bytes(layer_extra.bytes());
  layer_info.write_u16(0);  // transparency: raw 16-bit
  layer_info.write_u16(0xFFFFU);
  layer_info.write_u16(0xFFFFU);
  layer_info.write_u16(3);  // red: zip with prediction
  layer_info.write_bytes(red_zip);
  layer_info.write_u16(0);  // green: raw 16-bit
  layer_info.write_u16(0);
  layer_info.write_u16(0xFFFFU);
  layer_info.write_u16(2);  // blue: zip without prediction
  layer_info.write_bytes(blue_zip);

  patchy::psd::BigEndianWriter layer_mask;
  layer_mask.write_u32(0);  // empty standard layer info
  layer_mask.write_u32(0);  // global layer mask info
  write_ascii4(layer_mask, "8BIM");
  write_ascii4(layer_mask, "Lr16");
  layer_mask.write_u32(static_cast<std::uint32_t>(layer_info.bytes().size()));
  layer_mask.write_bytes(layer_info.bytes());
  while ((layer_mask.bytes().size() % 4U) != 0) {
    layer_mask.write_u8(0);
  }

  patchy::psd::BigEndianWriter writer;
  patchy::psd::write_header(writer, patchy::psd::Header{false, 3, 1, 2, 16, 3});
  writer.write_u32(0);
  writer.write_u32(0);
  writer.write_u32(static_cast<std::uint32_t>(layer_mask.bytes().size()));
  writer.write_bytes(layer_mask.bytes());
  writer.write_u16(0);
  for (int i = 0; i < 6; ++i) {
    writer.write_u16(0);  // raw 16-bit composite, 3 planes x 2 px
  }

  const auto read = patchy::psd::DocumentIo::read(writer.bytes());
  CHECK(read.layers().size() == 1);
  CHECK(read.layers().front().name() == "Deep Layer");
  CHECK(read.layers().front().pixels().format() == patchy::PixelFormat::rgba8());
  const auto* px0 = read.layers().front().pixels().pixel(0, 0);
  const auto* px1 = read.layers().front().pixels().pixel(1, 0);
  CHECK(px0[0] == 255);
  CHECK(px0[1] == 0);
  CHECK(px0[2] == 128);
  CHECK(px0[3] == 255);
  CHECK(px1[0] == 128);
  CHECK(px1[1] == 255);
  CHECK(px1[2] == 64);
  CHECK(px1[3] == 255);

  // The consumed Lr16 block must not be re-emitted from a converted 8-bit save.
  const auto resaved = patchy::psd::DocumentIo::write_layered_rgb8(read);
  const std::array<std::uint8_t, 4> lr16_key{'L', 'r', '1', '6'};
  CHECK(std::search(resaved.begin(), resaved.end(), lr16_key.begin(), lr16_key.end()) ==
        resaved.end());
}

// The merged-transparency flag of a 16-bit file lives in the Lr16 layer count sign;
// the prefer-flat path (smart-object rendering) must walk to it.
void psd_16_bit_merged_transparency_flag_reads_from_lr16() {
  patchy::psd::BigEndianWriter layer_extra;
  layer_extra.write_u32(0);
  layer_extra.write_u32(0);
  write_pascal_padded(layer_extra, "L", 4);

  patchy::psd::BigEndianWriter layer_info;
  layer_info.write_u16(0xFFFFU);  // layer count -1: composite carries merged alpha
  layer_info.write_u32(0);
  layer_info.write_u32(0);
  layer_info.write_u32(0);
  layer_info.write_u32(0);
  layer_info.write_u16(4);
  for (const auto channel_id : {0xFFFFU, 0U, 1U, 2U}) {
    layer_info.write_u16(static_cast<std::uint16_t>(channel_id));
    layer_info.write_u32(0);
  }
  write_ascii4(layer_info, "8BIM");
  write_ascii4(layer_info, "norm");
  layer_info.write_u8(255);
  layer_info.write_u8(0);
  layer_info.write_u8(0);
  layer_info.write_u8(0);
  layer_info.write_u32(static_cast<std::uint32_t>(layer_extra.bytes().size()));
  layer_info.write_bytes(layer_extra.bytes());

  patchy::psd::BigEndianWriter layer_mask;
  layer_mask.write_u32(0);
  layer_mask.write_u32(0);
  write_ascii4(layer_mask, "8BIM");
  write_ascii4(layer_mask, "Lr16");
  layer_mask.write_u32(static_cast<std::uint32_t>(layer_info.bytes().size()));
  layer_mask.write_bytes(layer_info.bytes());
  while ((layer_mask.bytes().size() % 4U) != 0) {
    layer_mask.write_u8(0);
  }

  patchy::psd::BigEndianWriter writer;
  patchy::psd::write_header(writer, patchy::psd::Header{false, 4, 1, 2, 16, 3});
  writer.write_u32(0);
  writer.write_u32(0);
  writer.write_u32(static_cast<std::uint32_t>(layer_mask.bytes().size()));
  writer.write_bytes(layer_mask.bytes());
  writer.write_u16(0);
  for (int i = 0; i < 6; ++i) {
    writer.write_u16(0);  // RGB planes
  }
  writer.write_u16(0xFFFFU);  // merged alpha plane: opaque, transparent
  writer.write_u16(0);

  patchy::psd::ReadOptions options;
  options.prefer_flat_composite = true;
  const auto read = patchy::psd::DocumentIo::read(writer.bytes(), options);
  CHECK(read.layers().size() == 1);
  const auto& mask = read.layers().front().mask();
  CHECK(mask.has_value());
  CHECK(mask->pixels.pixel(0, 0)[0] == 255);
  CHECK(mask->pixels.pixel(1, 0)[0] == 0);
}

void psd_32_bit_flat_raw_composite_converts_to_8_bit() {
  // Linear floats sRGB-encode on conversion; out-of-range values clamp.
  const std::array<float, 5> samples{0.0F, 0.25F, 0.5F, 1.0F, 2.0F};
  const std::array<std::uint8_t, 5> expected{0, 137, 188, 255, 255};

  patchy::psd::BigEndianWriter writer;
  patchy::psd::write_header(writer, patchy::psd::Header{false, 3, 1, 5, 32, 3});
  writer.write_u32(0);
  writer.write_u32(0);
  writer.write_u32(0);
  writer.write_u16(0);
  for (int channel = 0; channel < 3; ++channel) {
    for (const auto sample : samples) {
      writer.write_u32(std::bit_cast<std::uint32_t>(sample));
    }
  }

  std::vector<std::string> notices;
  patchy::psd::ReadOptions options;
  options.notices = &notices;
  const auto read = patchy::psd::DocumentIo::read(writer.bytes(), options);
  CHECK(read.layers().size() == 1);
  for (std::size_t x = 0; x < samples.size(); ++x) {
    const auto* px = read.layers().front().pixels().pixel(static_cast<std::int32_t>(x), 0);
    CHECK(px[0] == expected[x]);
    CHECK(px[1] == expected[x]);
    CHECK(px[2] == expected[x]);
  }
  CHECK(std::any_of(notices.begin(), notices.end(), [](const std::string& notice) {
    return notice.find("32-bit") != std::string::npos;
  }));
}

void psd_32_bit_lr32_zip_prediction_layer_converts() {
  // Red decodes to floats {1.0f, 0.25f} (big-endian 3F800000, 3E800000). The
  // prediction filter stores each row as byte planes (all MSBs first) and then
  // byte-delta-encodes: shuffled 3F 3E 80 80 00 00 00 00 -> deltas below.
  const auto red_zip = zlib_deflate(
      std::array<std::uint8_t, 8>{0x3F, 0xFF, 0x42, 0x00, 0x80, 0x00, 0x00, 0x00});

  const auto write_f32 = [](patchy::psd::BigEndianWriter& target, float value) {
    target.write_u32(std::bit_cast<std::uint32_t>(value));
  };

  patchy::psd::BigEndianWriter layer_extra;
  layer_extra.write_u32(0);
  layer_extra.write_u32(0);
  write_pascal_padded(layer_extra, "HDR Layer", 4);

  patchy::psd::BigEndianWriter layer_info;
  layer_info.write_u16(1);
  layer_info.write_u32(0);
  layer_info.write_u32(0);
  layer_info.write_u32(1);
  layer_info.write_u32(2);
  layer_info.write_u16(4);
  layer_info.write_u16(0xFFFFU);
  layer_info.write_u32(10);
  layer_info.write_u16(0);
  layer_info.write_u32(static_cast<std::uint32_t>(2U + red_zip.size()));
  layer_info.write_u16(1);
  layer_info.write_u32(10);
  layer_info.write_u16(2);
  layer_info.write_u32(10);
  write_ascii4(layer_info, "8BIM");
  write_ascii4(layer_info, "norm");
  layer_info.write_u8(255);
  layer_info.write_u8(0);
  layer_info.write_u8(0);
  layer_info.write_u8(0);
  layer_info.write_u32(static_cast<std::uint32_t>(layer_extra.bytes().size()));
  layer_info.write_bytes(layer_extra.bytes());
  layer_info.write_u16(0);  // transparency: raw floats (linear scale, not sRGB)
  write_f32(layer_info, 1.0F);
  write_f32(layer_info, 1.0F);
  layer_info.write_u16(3);  // red: zip with prediction
  layer_info.write_bytes(red_zip);
  layer_info.write_u16(0);  // green: raw floats
  write_f32(layer_info, 0.0F);
  write_f32(layer_info, 0.5F);
  layer_info.write_u16(0);  // blue: raw floats
  write_f32(layer_info, 0.5F);
  write_f32(layer_info, 0.0F);

  patchy::psd::BigEndianWriter layer_mask;
  layer_mask.write_u32(0);
  layer_mask.write_u32(0);
  write_ascii4(layer_mask, "8BIM");
  write_ascii4(layer_mask, "Lr32");
  layer_mask.write_u32(static_cast<std::uint32_t>(layer_info.bytes().size()));
  layer_mask.write_bytes(layer_info.bytes());
  while ((layer_mask.bytes().size() % 4U) != 0) {
    layer_mask.write_u8(0);
  }

  patchy::psd::BigEndianWriter writer;
  patchy::psd::write_header(writer, patchy::psd::Header{false, 3, 1, 2, 32, 3});
  writer.write_u32(0);
  writer.write_u32(0);
  writer.write_u32(static_cast<std::uint32_t>(layer_mask.bytes().size()));
  writer.write_bytes(layer_mask.bytes());
  writer.write_u16(0);
  for (int i = 0; i < 6; ++i) {
    writer.write_u32(0);  // raw float composite, 3 planes x 2 px
  }

  const auto read = patchy::psd::DocumentIo::read(writer.bytes());
  CHECK(read.layers().size() == 1);
  CHECK(read.layers().front().name() == "HDR Layer");
  const auto* px0 = read.layers().front().pixels().pixel(0, 0);
  const auto* px1 = read.layers().front().pixels().pixel(1, 0);
  CHECK(px0[0] == 255);
  CHECK(px0[1] == 0);
  CHECK(px0[2] == 188);
  CHECK(px0[3] == 255);
  CHECK(px1[0] == 137);
  CHECK(px1[1] == 188);
  CHECK(px1[2] == 0);
  CHECK(px1[3] == 255);
}

// Real Photoshop-written 16-bit file (raw composite + zip-prediction Lr16 layers).
void psd_16_bit_flat_filter_list_loads_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("Flat-filter-list.psd");
  if (!std::filesystem::exists(path)) {
    std::cout << "[SKIP] local 16-bit fixture missing: " << path.string() << '\n';
    return;
  }

  std::vector<std::string> notices;
  patchy::psd::ReadOptions options;
  options.retain_flat_composite = true;
  options.notices = &notices;
  const auto document = patchy::psd::DocumentIo::read_file(path, options);
  CHECK(document.width() == 320);
  CHECK(document.height() == 480);
  CHECK(!document.layers().empty());
  CHECK(document.metadata().psd_flat_composite.has_value());
  const auto& layers = document.layers();
  CHECK(std::any_of(layers.begin(), layers.end(), [](const patchy::Layer& layer) {
    return layer.kind() == patchy::LayerKind::Group;
  }));
  CHECK(std::any_of(notices.begin(), notices.end(), [](const std::string& notice) {
    return notice.find("16-bit") != std::string::npos;
  }));

  // The decoded layers (zip-prediction Lr16 data) flattened by Patchy must agree
  // with Photoshop's own merged composite (raw data) from the same file: the two
  // decode paths cross-check each other.
  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  const auto& composite = *document.metadata().psd_flat_composite;
  CHECK(composite.width() == flattened.width());
  CHECK(composite.height() == flattened.height());
  double total_abs_diff = 0.0;
  std::size_t far_off_pixels = 0;
  for (std::int32_t y = 0; y < composite.height(); ++y) {
    for (std::int32_t x = 0; x < composite.width(); ++x) {
      const auto* rendered = flattened.pixel(x, y);
      const auto* merged = composite.pixel(x, y);
      int pixel_max_diff = 0;
      for (int channel = 0; channel < 3; ++channel) {
        const auto diff = std::abs(static_cast<int>(rendered[channel]) - static_cast<int>(merged[channel]));
        total_abs_diff += diff;
        pixel_max_diff = std::max(pixel_max_diff, diff);
      }
      if (pixel_max_diff > 24) {
        ++far_off_pixels;
      }
    }
  }
  const auto pixel_count = static_cast<double>(composite.width()) * composite.height();
  const auto mean_abs_diff = total_abs_diff / (pixel_count * 3.0);
  const auto far_off_fraction = static_cast<double>(far_off_pixels) / pixel_count;
  std::cout << "  16-bit fixture flatten vs Photoshop composite mean abs diff: " << mean_abs_diff
            << ", pixels off by >24: " << (far_off_fraction * 100.0) << "%\n";
  // Baseline July 2026: mean 0.97, far-off 1.9%. The far-off pixels are the file's
  // CS4-era 'ic *' vector shape layers whose vmsk blocks fail to parse (they
  // import preserved-and-locked with empty rasters); that is independent of bit depth.
  CHECK(mean_abs_diff < 2.0);
  CHECK(far_off_fraction < 0.025);
}

// Photoshop 2026-written deep fixtures (COM-generated, July 2026): left half filled
// (135,206,235), a (16,16)-(48,48) fill of (255,64,0), white elsewhere; layered
// variants keep two layers (zip-prediction channels) and a compat-off white
// composite, flat variants carry the real image as an RLE composite. 32-bit fills
// store value/255 as linear floats, so the expected colors for those are exactly
// what Photoshop itself produced when converting the same files to 8-bit.
void check_photoshop_deep_fixture(const std::string& file_name, bool layered,
                                  const std::array<int, 3>& sky, const std::array<int, 3>& hot) {
  const auto path = patchy::test::local_psd_fixture_path(file_name);
  if (!std::filesystem::exists(path)) {
    std::cout << "[SKIP] local deep fixture missing: " << path.string() << '\n';
    return;
  }
  const auto document = patchy::psd::DocumentIo::read_file(path);
  CHECK(document.width() == 64);
  CHECK(document.height() == 64);
  CHECK(document.layers().size() == (layered ? 2U : 1U));
  const auto check_pixel = [](const std::uint8_t* px, const std::array<int, 3>& expected) {
    CHECK(px[0] == expected[0]);
    CHECK(px[1] == expected[1]);
    CHECK(px[2] == expected[2]);
  };
  const auto& background = document.layers().front();
  check_pixel(background.pixels().pixel(8, 8), sky);
  check_pixel(background.pixels().pixel(56, 56), {255, 255, 255});
  if (layered) {
    const auto& overlay = document.layers()[1];
    CHECK(overlay.name() == "Layer 1");
    CHECK(overlay.bounds().x == 16);
    CHECK(overlay.bounds().y == 16);
    CHECK(overlay.pixels().format() == patchy::PixelFormat::rgba8());
    check_pixel(overlay.pixels().pixel(8, 8), hot);
    CHECK(overlay.pixels().pixel(8, 8)[3] == 255);
  } else {
    check_pixel(background.pixels().pixel(24, 24), hot);
  }
}

void psd_photoshop_16_bit_fixtures_load_if_available() {
  check_photoshop_deep_fixture("ps2026-16bit-flat.psd", false, {135, 206, 235}, {255, 64, 0});
  check_photoshop_deep_fixture("ps2026-16bit.psd", true, {135, 206, 235}, {255, 64, 0});
}

void psd_photoshop_32_bit_fixtures_load_if_available() {
  check_photoshop_deep_fixture("ps2026-32bit-flat.psd", false, {192, 232, 246}, {255, 137, 0});
  check_photoshop_deep_fixture("ps2026-32bit.psd", true, {192, 232, 246}, {255, 137, 0});
}

}  // namespace

std::vector<patchy::test::TestCase> psd_core_io_tests() {
  return {
      {"psd_flat_rgb8_round_trips", psd_flat_rgb8_round_trips},
      {"psd_flat_rgb8_writer_uses_rle_for_compressible_data",
       psd_flat_rgb8_writer_uses_rle_for_compressible_data},
      {"psd_flat_rgb8_writer_keeps_raw_for_incompressible_data",
       psd_flat_rgb8_writer_keeps_raw_for_incompressible_data},
      {"psd_flat_rle_rgb8_reads", psd_flat_rle_rgb8_reads},
      {"psd_flat_raw_cmyk8_imports_as_rgb", psd_flat_raw_cmyk8_imports_as_rgb},
      {"psd_flat_rle_cmyk8_imports_as_rgb", psd_flat_rle_cmyk8_imports_as_rgb},
      {"psd_layered_cmyk8_imports_as_rgba", psd_layered_cmyk8_imports_as_rgba},
      {"psd_imported_cmyk_icc_profile_is_not_exported_as_rgb_profile",
       psd_imported_cmyk_icc_profile_is_not_exported_as_rgb_profile},
      {"psd_image_resources_round_trip_and_icc_profile_is_exposed",
       psd_image_resources_round_trip_and_icc_profile_is_exposed},
      {"psd_resolution_resource_units_are_display_only", psd_resolution_resource_units_are_display_only},
      {"psd_grid_guides_resource_round_trip_and_replaces_duplicates",
       psd_grid_guides_resource_round_trip_and_replaces_duplicates},
      {"psd_layered_rgb8_round_trips_pixel_layers", psd_layered_rgb8_round_trips_pixel_layers},
      {"psd_zero_length_layer_channels_read_as_empty", psd_zero_length_layer_channels_read_as_empty},
      {"psd_interface_mock2_loads_if_available", psd_interface_mock2_loads_if_available},
      {"psd_16_bit_flat_raw_composite_converts_to_8_bit", psd_16_bit_flat_raw_composite_converts_to_8_bit},
      {"psd_16_bit_flat_rle_composite_converts_to_8_bit", psd_16_bit_flat_rle_composite_converts_to_8_bit},
      {"psd_16_bit_lr16_layers_convert_with_zip_prediction",
       psd_16_bit_lr16_layers_convert_with_zip_prediction},
      {"psd_16_bit_merged_transparency_flag_reads_from_lr16",
       psd_16_bit_merged_transparency_flag_reads_from_lr16},
      {"psd_32_bit_flat_raw_composite_converts_to_8_bit", psd_32_bit_flat_raw_composite_converts_to_8_bit},
      {"psd_32_bit_lr32_zip_prediction_layer_converts", psd_32_bit_lr32_zip_prediction_layer_converts},
      {"psd_16_bit_flat_filter_list_loads_if_available", psd_16_bit_flat_filter_list_loads_if_available},
      {"psd_photoshop_16_bit_fixtures_load_if_available", psd_photoshop_16_bit_fixtures_load_if_available},
      {"psd_photoshop_32_bit_fixtures_load_if_available", psd_photoshop_32_bit_fixtures_load_if_available},
      {"psd_layered_writer_uses_rle_for_compressible_layer_channels",
       psd_layered_writer_uses_rle_for_compressible_layer_channels},
      {"psd_layer_locks_import_and_export_lspf", psd_layer_locks_import_and_export_lspf},
      {"psd_layer_masks_render_and_round_trip", psd_layer_masks_render_and_round_trip},
      {"psd_layer_mask_link_state_round_trips", psd_layer_mask_link_state_round_trips},
      {"psd_legacy_document_alpha_marker_stays_a_layer_mask",
       psd_legacy_document_alpha_marker_stays_a_layer_mask},
      {"psd_psb_saved_channels_round_trip_names_pixels_and_metadata",
       psd_psb_saved_channels_round_trip_names_pixels_and_metadata},
      {"psd_photoshop_saved_channels_fixture_imports_and_resaves",
       psd_photoshop_saved_channels_fixture_imports_and_resaves},
      {"psd_legacy_channel_name_fallback_counts_unicode_scalars",
       psd_legacy_channel_name_fallback_counts_unicode_scalars},
      {"psd_saved_channel_coexists_with_real_layer_mask",
       psd_saved_channel_coexists_with_real_layer_mask},
      {"psd_merged_transparency_is_structural_before_saved_channels",
       psd_merged_transparency_is_structural_before_saved_channels},
      {"psd_cmyk_extra_plane_imports_as_saved_channel",
       psd_cmyk_extra_plane_imports_as_saved_channel},
      {"psd_saved_channel_resource_mismatches_use_fallback_names",
       psd_saved_channel_resource_mismatches_use_fallback_names},
      {"psd_opaque_allows_53_saved_channels_but_transparent_throws",
       psd_opaque_allows_53_saved_channels_but_transparent_throws},
      {"psb_transparency_channel_is_not_a_layer_mask_if_available",
       psb_transparency_channel_is_not_a_layer_mask_if_available},
      {"psd_layer_record_flags_mark_photoshop5_layers", psd_layer_record_flags_mark_photoshop5_layers},
  };
}
